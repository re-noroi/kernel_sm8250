// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal bpf stub 
 */

#include <linux/bpf.h>
#include <linux/filter.h>

#define CREATE_TRACE_POINTS
#include <trace/events/gpu_mem.h>

EXPORT_TRACEPOINT_SYMBOL(gpu_mem_total);

const struct bpf_func_proto * __weak bpf_tracing_func_proto(
	enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return NULL;
}

const struct bpf_func_proto * __weak tracing_prog_func_proto(
	enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return NULL;
}

static const struct bpf_func_proto *
tp_stub_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return bpf_base_func_proto(func_id);
}

static bool tp_stub_is_valid_access(int off, int size,
				    enum bpf_access_type type,
				    const struct bpf_prog *prog,
				    struct bpf_insn_access_aux *info)
{
	if (type != BPF_READ)
		return false;
	if (off < 0 || off + size > 2048)
		return false;
	if (off % size != 0)
		return false;
	return true;
}

const struct bpf_verifier_ops tracepoint_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops tracepoint_prog_ops = {
};

const struct bpf_verifier_ops raw_tracepoint_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops raw_tracepoint_prog_ops = {
};

const struct bpf_verifier_ops raw_tracepoint_writable_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops raw_tracepoint_writable_prog_ops = {
};

const struct bpf_verifier_ops tracing_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops tracing_prog_ops = {
};
