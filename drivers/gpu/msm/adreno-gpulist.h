/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2002,2007-2021, The Linux Foundation. All rights reserved.
 */

#define ANY_ID (~0)

#define DEFINE_ADRENO_REV(_rev, _core, _major, _minor, _patchid) \
	.gpurev = _rev, .core = _core, .major = _major, .minor = _minor, \
	.patchid = _patchid

#define DEFINE_DEPRECATED_CORE(_name, _rev, _core, _major, _minor, _patchid) \
static const struct adreno_gpu_core adreno_gpu_core_##_name = { \
	DEFINE_ADRENO_REV(_rev, _core, _major, _minor, _patchid), \
	.features = ADRENO_DEPRECATED, \
}

/* a620 and a650 */
static const struct adreno_reglist a650_gbif_regs[] = {
	{A6XX_GBIF_QSB_SIDE0, 0x00071620},
	{A6XX_GBIF_QSB_SIDE1, 0x00071620},
	{A6XX_GBIF_QSB_SIDE2, 0x00071620},
	{A6XX_GBIF_QSB_SIDE3, 0x00071620},
	{A6XX_RBBM_GBIF_CLIENT_QOS_CNTL, 0x3},
};

/* These are for a620 and a650 */
static const struct a6xx_protected_regs a620_protected_regs[] = {
	{ A6XX_CP_PROTECT_REG + 0, 0x00000, 0x004ff, 0 },
	{ A6XX_CP_PROTECT_REG + 1, 0x00501, 0x00506, 0 },
	{ A6XX_CP_PROTECT_REG + 2, 0x0050b, 0x007ff, 0 },
	{ A6XX_CP_PROTECT_REG + 3, 0x0050e, 0x0050e, 1 },
	{ A6XX_CP_PROTECT_REG + 4, 0x00510, 0x00510, 1 },
	{ A6XX_CP_PROTECT_REG + 5, 0x00534, 0x00534, 1 },
	{ A6XX_CP_PROTECT_REG + 6, 0x00800, 0x00882, 1 },
	{ A6XX_CP_PROTECT_REG + 7, 0x008a0, 0x008a8, 1 },
	{ A6XX_CP_PROTECT_REG + 8, 0x008ab, 0x008cf, 1 },
	{ A6XX_CP_PROTECT_REG + 9, 0x008d0, 0x0098c, 0 },
	{ A6XX_CP_PROTECT_REG + 10, 0x00900, 0x0094d, 1 },
	{ A6XX_CP_PROTECT_REG + 11, 0x0098d, 0x00bff, 1 },
	{ A6XX_CP_PROTECT_REG + 12, 0x00e00, 0x00e01, 1 },
	{ A6XX_CP_PROTECT_REG + 13, 0x00e03, 0x00e0f, 1 },
	{ A6XX_CP_PROTECT_REG + 14, 0x03c00, 0x03cc3, 1 },
	{ A6XX_CP_PROTECT_REG + 15, 0x03cc4, 0x05cc3, 0 },
	{ A6XX_CP_PROTECT_REG + 16, 0x08630, 0x087ff, 1 },
	{ A6XX_CP_PROTECT_REG + 17, 0x08e00, 0x08e00, 1 },
	{ A6XX_CP_PROTECT_REG + 18, 0x08e08, 0x08e08, 1 },
	{ A6XX_CP_PROTECT_REG + 19, 0x08e50, 0x08e6f, 1 },
	{ A6XX_CP_PROTECT_REG + 20, 0x08e80, 0x090ff, 1 },
	{ A6XX_CP_PROTECT_REG + 21, 0x09624, 0x097ff, 1 },
	{ A6XX_CP_PROTECT_REG + 22, 0x09e60, 0x09e71, 1 },
	{ A6XX_CP_PROTECT_REG + 23, 0x09e78, 0x09fff, 1 },
	{ A6XX_CP_PROTECT_REG + 24, 0x0a630, 0x0a7ff, 1 },
	{ A6XX_CP_PROTECT_REG + 25, 0x0ae02, 0x0ae02, 1 },
	{ A6XX_CP_PROTECT_REG + 26, 0x0ae50, 0x0b17f, 1 },
	{ A6XX_CP_PROTECT_REG + 27, 0x0b604, 0x0b604, 1 },
	{ A6XX_CP_PROTECT_REG + 28, 0x0b608, 0x0b60f, 1 },
	{ A6XX_CP_PROTECT_REG + 29, 0x0be02, 0x0be03, 1 },
	{ A6XX_CP_PROTECT_REG + 30, 0x0be20, 0x0d5ff, 1 },
	{ A6XX_CP_PROTECT_REG + 31, 0x0f000, 0x0fbff, 1 },
	{ A6XX_CP_PROTECT_REG + 32, 0x0fc00, 0x11bff, 0 },
	{ A6XX_CP_PROTECT_REG + 47, 0x11c00, 0x11c00, 1 },
	{ 0 },
};

static const struct adreno_reglist a650_hwcg_regs[] = {
	{A6XX_RBBM_CLOCK_CNTL_SP0, 0x02222222},
	{A6XX_RBBM_CLOCK_CNTL2_SP0, 0x02222220},
	{A6XX_RBBM_CLOCK_DELAY_SP0, 0x00000080},
	{A6XX_RBBM_CLOCK_HYST_SP0, 0x0000F3CF},
	{A6XX_RBBM_CLOCK_CNTL_TP0, 0x02222222},
	{A6XX_RBBM_CLOCK_CNTL2_TP0, 0x22222222},
	{A6XX_RBBM_CLOCK_CNTL3_TP0, 0x22222222},
	{A6XX_RBBM_CLOCK_CNTL4_TP0, 0x00022222},
	{A6XX_RBBM_CLOCK_DELAY_TP0, 0x11111111},
	{A6XX_RBBM_CLOCK_DELAY2_TP0, 0x11111111},
	{A6XX_RBBM_CLOCK_DELAY3_TP0, 0x11111111},
	{A6XX_RBBM_CLOCK_DELAY4_TP0, 0x00011111},
	{A6XX_RBBM_CLOCK_HYST_TP0, 0x77777777},
	{A6XX_RBBM_CLOCK_HYST2_TP0, 0x77777777},
	{A6XX_RBBM_CLOCK_HYST3_TP0, 0x77777777},
	{A6XX_RBBM_CLOCK_HYST4_TP0, 0x00077777},
	{A6XX_RBBM_CLOCK_CNTL_RB0, 0x22222222},
	{A6XX_RBBM_CLOCK_CNTL2_RB0, 0x01002222},
	{A6XX_RBBM_CLOCK_CNTL_CCU0, 0x00002220},
	{A6XX_RBBM_CLOCK_HYST_RB_CCU0, 0x00040F00},
	{A6XX_RBBM_CLOCK_CNTL_RAC, 0x25222022},
	{A6XX_RBBM_CLOCK_CNTL2_RAC, 0x00005555},
	{A6XX_RBBM_CLOCK_DELAY_RAC, 0x00000011},
	{A6XX_RBBM_CLOCK_HYST_RAC, 0x00445044},
	{A6XX_RBBM_CLOCK_CNTL_TSE_RAS_RBBM, 0x04222222},
	{A6XX_RBBM_CLOCK_MODE_VFD, 0x00002222},
	{A6XX_RBBM_CLOCK_MODE_GPC, 0x00222222},
	{A6XX_RBBM_CLOCK_DELAY_HLSQ_2, 0x00000002},
	{A6XX_RBBM_CLOCK_MODE_HLSQ, 0x00002222},
	{A6XX_RBBM_CLOCK_DELAY_TSE_RAS_RBBM, 0x00004000},
	{A6XX_RBBM_CLOCK_DELAY_VFD, 0x00002222},
	{A6XX_RBBM_CLOCK_DELAY_GPC, 0x00000200},
	{A6XX_RBBM_CLOCK_DELAY_HLSQ, 0x00000000},
	{A6XX_RBBM_CLOCK_HYST_TSE_RAS_RBBM, 0x00000000},
	{A6XX_RBBM_CLOCK_HYST_VFD, 0x00000000},
	{A6XX_RBBM_CLOCK_HYST_GPC, 0x04104004},
	{A6XX_RBBM_CLOCK_HYST_HLSQ, 0x00000000},
	{A6XX_RBBM_CLOCK_CNTL_TEX_FCHE, 0x00000222},
	{A6XX_RBBM_CLOCK_DELAY_TEX_FCHE, 0x00000111},
	{A6XX_RBBM_CLOCK_HYST_TEX_FCHE, 0x00000777},
	{A6XX_RBBM_CLOCK_CNTL_UCHE, 0x22222222},
	{A6XX_RBBM_CLOCK_HYST_UCHE, 0x00000004},
	{A6XX_RBBM_CLOCK_DELAY_UCHE, 0x00000002},
	{A6XX_RBBM_ISDB_CNT, 0x00000182},
	{A6XX_RBBM_RAC_THRESHOLD_CNT, 0x00000000},
	{A6XX_RBBM_SP_HYST_CNT, 0x00000000},
	{A6XX_RBBM_CLOCK_CNTL_GMU_GX, 0x00000222},
	{A6XX_RBBM_CLOCK_DELAY_GMU_GX, 0x00000111},
	{A6XX_RBBM_CLOCK_HYST_GMU_GX, 0x00000555},
};

static const struct adreno_a6xx_core adreno_gpu_core_a650 = {
	.base = {
		DEFINE_ADRENO_REV(ADRENO_REV_A650, 6, 5, 0, 0),
		.features = ADRENO_64BIT | ADRENO_RPMH | ADRENO_GPMU |
			ADRENO_IOCOHERENT | ADRENO_CONTENT_PROTECTION |
			ADRENO_IFPC | ADRENO_APRIV,
		.gpudev = &adreno_a6xx_gpudev,
		.gmem_size = SZ_1M + SZ_128K, /* verified 1152kB */
		.busy_mask = 0xfffffffe,
		.bus_width = 32,
	},
	.prim_fifo_threshold = 0x00300000,
	.gmu_major = 2,
	.gmu_minor = 0,
	.sqefw_name = "a650_sqe.fw",
	.gmufw_name = "a650_gmu.bin",
	.zap_name = "a650_zap",
	.hwcg = a650_hwcg_regs,
	.hwcg_count = ARRAY_SIZE(a650_hwcg_regs),
	.vbif = a650_gbif_regs,
	.vbif_count = ARRAY_SIZE(a650_gbif_regs),
	.veto_fal10 = true,
	.pdc_in_aop = true,
	.hang_detect_cycles = 0xcfffff,
	.protected_regs = a620_protected_regs,
	.disable_tseskip = true,
};

static const struct adreno_a6xx_core adreno_gpu_core_a650v2 = {
	.base = {
		DEFINE_ADRENO_REV(ADRENO_REV_A650, 6, 5, 0, ANY_ID),
		.features = ADRENO_64BIT | ADRENO_RPMH | ADRENO_GPMU |
			ADRENO_IOCOHERENT | ADRENO_CONTENT_PROTECTION |
			ADRENO_IFPC | ADRENO_PREEMPTION | ADRENO_ACD |
			ADRENO_LM | ADRENO_APRIV,
		.gpudev = &adreno_a6xx_gpudev,
		.gmem_size = SZ_1M + SZ_128K, /* verified 1152kB */
		.busy_mask = 0xfffffffe,
		.bus_width = 32,
	},
	.prim_fifo_threshold = 0x00300000,
	.gmu_major = 2,
	.gmu_minor = 0,
	.sqefw_name = "a650_sqe.fw",
	.gmufw_name = "a650_gmu.bin",
	.zap_name = "a650_zap",
	.hwcg = a650_hwcg_regs,
	.hwcg_count = ARRAY_SIZE(a650_hwcg_regs),
	.vbif = a650_gbif_regs,
	.vbif_count = ARRAY_SIZE(a650_gbif_regs),
	.veto_fal10 = true,
	.pdc_in_aop = true,
	.hang_detect_cycles = 0x3ffff,
	.protected_regs = a620_protected_regs,
	.disable_tseskip = true,
};

static const struct adreno_gpu_core *adreno_gpulist[] = {
	&adreno_gpu_core_a650.base,
	&adreno_gpu_core_a650v2.base,
};
