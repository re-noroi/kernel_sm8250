#define pr_fmt(fmt) "zram: lz4hc: " fmt

#define LZ4_HC_STATIC_LINKING_ONLY

#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/sizes.h>
#include <linux/vmalloc.h>

#include "backend_lz4hc.h"

static size_t lz4hc_workspace_size(const struct zcomp_params *params)
{
	/* The bundled freestanding LZ4HC would otherwise use a 64 KiB stack. */
	if (params->level >= LZ4HC_CLEVEL_OPT_MIN)
		return LZ4_STREAMHC_EXTSIZE;
	return LZ4HC_MEM_COMPRESS;
}

static void lz4hc_release_params(struct zcomp_params *params)
{
	vfree(params->drv_data);
	params->drv_data = NULL;
}

static int lz4hc_setup_params(struct zcomp_params *params)
{
	LZ4_streamHC_t *dict_stream;
	size_t size;
	int ret;

	if (params->level == ZCOMP_PARAM_NO_LEVEL)
		params->level = LZ4HC_DEFAULT_CLEVEL;

	if (!params->dict || !params->dict_sz)
		return 0;

	if (params->dict_sz > SZ_64K)
		return -EINVAL;

	size = lz4hc_workspace_size(params);
	dict_stream = vzalloc(size);
	if (!dict_stream)
		return -ENOMEM;

	if (!LZ4_initStreamHC(dict_stream, size)) {
		vfree(dict_stream);
		return -EINVAL;
	}
	LZ4_setCompressionLevel(dict_stream, params->level);
	ret = LZ4_loadDictHC(dict_stream, params->dict, params->dict_sz);
	if (ret != params->dict_sz) {
		vfree(dict_stream);
		return -EINVAL;
	}
	params->drv_data = dict_stream;

	return 0;
}

static void lz4hc_destroy(struct zcomp_ctx *ctx)
{
	vfree(ctx->context);
}

static int lz4hc_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	size_t size = lz4hc_workspace_size(params);
	void *mem;

	BUILD_BUG_ON(sizeof(LZ4_streamHC_t) != LZ4HC_MEM_COMPRESS);

	mem = vmalloc(size);
	if (!mem)
		return -ENOMEM;

	if (!LZ4_initStreamHC(mem, size)) {
		vfree(mem);
		return -EINVAL;
	}
	ctx->context = mem;
	return 0;
}

static int lz4hc_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req)
{
	int ret;

	if (!params->drv_data) {
		ret = LZ4_compress_HC_extStateHC_fastReset(ctx->context,
							  req->src, req->dst,
							  req->src_len, req->dst_len,
							  params->level);
	} else {
		/* Restore the dictionary state, leaving the scratch area untouched. */
		memcpy(ctx->context, params->drv_data, sizeof(LZ4_streamHC_t));
		ret = LZ4_compress_HC_continue(ctx->context, req->src, req->dst,
					      req->src_len, req->dst_len);
	}
	if (!ret)
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int lz4hc_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			    struct zcomp_req *req)
{
	int ret;

	ret = LZ4_decompress_safe_usingDict(req->src, req->dst, req->src_len,
					    req->dst_len, params->dict,
					    params->dict_sz);
	if (ret != req->dst_len)
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_lz4hc = {
	.compress	= lz4hc_compress,
	.decompress	= lz4hc_decompress,
	.create_ctx	= lz4hc_create,
	.destroy_ctx	= lz4hc_destroy,
	.setup_params	= lz4hc_setup_params,
	.release_params	= lz4hc_release_params,
	.name		= "lz4hc",
};
