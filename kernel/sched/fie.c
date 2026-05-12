// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024-2025 Sultan Alsawaf <sultan@kerneltoast.com>.
 * Adapted for Linux 4.19 (SM8250)
 */

#include <linux/cpufreq.h>
#include <linux/perf_event.h>
#include <linux/reboot.h>
#include <linux/sched/topology.h>
#include <linux/units.h>
#include <linux/arch_topology.h>
#include <asm/arch_timer.h>
#include <asm/cputype.h>
#include <asm/perf_event.h>
#include <trace/events/power.h>
#include "sched.h"

/* Fallback for older 4.19 headers */
#ifndef ARMV8_PMUV3_PERFCTR_CPU_CYCLES
#define ARMV8_PMUV3_PERFCTR_CPU_CYCLES 0x11
#endif

/*
 * The minimum sample time required to measure the performance counters. This
 * should take into account the resolution of the system timer. At Qualcomm's
 * timer rate of 19200000 Hz, a sample window of 3 us provides an error margin
 * of ~1.7%.
 */
static u64 cpu_min_sample_cntpct __read_mostly = 3 * NSEC_PER_USEC;

/*
 * CNTPCT_EL0 arithmetic helpers to avoid overflowing a u64 when converting
 * between ticks and nanoseconds. This avoids needing mult_frac() in a hot path.
 */
static u64 cntpct_mult __read_mostly;
static u64 cntpct_div __read_mostly;

static u64 cntpct_to_ns(u64 cntpct)
{
	return cntpct * cntpct_mult / cntpct_div;
}

static u64 ns_to_cntpct(u64 ns)
{
	return DIV_ROUND_UP_ULL(ns * cntpct_div, cntpct_mult);
}

static void calc_cntpct_arith(void)
{
	int cd;

	/*
	 * Calculate lossless arithmetic to convert between timer ticks and
	 * nanoseconds, extracting all common denominators up through 10.
	 */
	cntpct_mult = NSEC_PER_SEC;
	cntpct_div = arch_timer_get_rate();
	for (cd = 10; cd > 1; cd--) {
		while (!(cntpct_mult % cd) && !(cntpct_div % cd)) {
			cntpct_div /= cd;
			cntpct_mult /= cd;
		}
	}

	/* Compute all nanosecond time intervals in terms of CNTPCT_EL0 ticks */
	cpu_min_sample_cntpct = ns_to_cntpct(cpu_min_sample_cntpct);
}

/* The PMU/AMU event stats. Order is assumed by the *pmu_read() functions. */
struct pmu_stat {
	u64 cntpct;
	u64 const_cyc;
	u64 cpu_cyc;
};

struct cpu_pmu {
	struct pmu_stat *cur_ptr[2] __aligned(16);
	struct pmu_stat cur[2];
	struct sfd_data {
		raw_spinlock_t lock;
		u64 cpu_cyc;
		u64 const_cyc;
		bool stale;
	} sfd; /* Scale Frequency Data */
};

static DEFINE_PER_CPU(struct cpu_pmu, cpu_pmu_evs) = {
	.sfd.lock = __RAW_SPIN_LOCK_UNLOCKED(cpu_pmu_evs.sfd.lock)
};

static DEFINE_PER_CPU_READ_MOSTLY(bool, cpu_has_amu);
static DEFINE_PER_CPU_READ_MOSTLY(bool, cpu_has_amu_const);
static DEFINE_PER_CPU_READ_MOSTLY(u32, cpu_max_freq);

static DEFINE_STATIC_KEY_FALSE(fie_ready);
static int cpuhp_state;

enum pmu_events {
	CPU_CYCLES,
	PMU_EVT_MAX
};

static const u32 pmu_evt_id[PMU_EVT_MAX] = {
	[CPU_CYCLES] = ARMV8_PMUV3_PERFCTR_CPU_CYCLES
};

struct cpu_pmu_evt {
	struct perf_event *pev[PMU_EVT_MAX];
};

static DEFINE_PER_CPU(struct cpu_pmu_evt, pevt_pcpu);

static __always_inline bool cpu_supports_amu_const(int cpu)
{
	return per_cpu(cpu_has_amu_const, cpu);
}

void fie_init_cpu_domain(const struct cpumask *cpus, unsigned int max_freq)
{
	int cpu;

	for_each_cpu(cpu, cpus)
		per_cpu(cpu_max_freq, cpu) = max_freq;
}
EXPORT_SYMBOL_GPL(fie_init_cpu_domain);

static struct perf_event *create_pev(struct perf_event_attr *attr, int cpu)
{
	return perf_event_create_kernel_counter(attr, cpu, NULL, NULL, NULL);
}

static void release_perf_events(int cpu)
{
	struct cpu_pmu_evt *cpev = &per_cpu(pevt_pcpu, cpu);
	int i;

	for (i = 0; i < PMU_EVT_MAX; i++) {
		if (IS_ERR(cpev->pev[i]))
			break;

		perf_event_release_kernel(cpev->pev[i]);
	}
}

static int create_perf_events(int cpu)
{
	struct cpu_pmu_evt *cpev = &per_cpu(pevt_pcpu, cpu);
	struct perf_event_attr attr = {
		.type = PERF_TYPE_RAW,
		.size = sizeof(attr),
		.pinned = 1,
		.config1 = 0x1
	};
	int i;

	for (i = 0; i < PMU_EVT_MAX; i++) {
		attr.config = pmu_evt_id[i];
		cpev->pev[i] = create_pev(&attr, cpu);
		if (WARN_ON(IS_ERR(cpev->pev[i])))
			goto release_pevs;
	}

	return 0;

release_pevs:
	release_perf_events(cpu);
	return PTR_ERR(cpev->pev[i]);
}

static noinline void __aligned(L1_CACHE_BYTES)
fie_amu_const_read(struct pmu_stat *stat)
{
	register u64 cntpct, const_cyc, cpu_cyc;

	asm volatile("isb\n\t"
		     "mrs %0, cntpct_el0\n\t"
		     "mrs %1, amevcntr01_el0\n\t"
		     "mrs %2, amevcntr00_el0\n\t"
		     "isb"
		     : "=r" (cntpct), "=r" (const_cyc), "=r" (cpu_cyc));

	*stat = (typeof(*stat)){ cntpct, const_cyc, cpu_cyc };
}

static noinline void __aligned(L1_CACHE_BYTES)
fie_amu_read(struct pmu_stat *stat)
{
	register u64 cntpct, cpu_cyc;

	asm volatile("isb\n\t"
		     "mrs %0, cntpct_el0\n\t"
		     "mrs %1, amevcntr00_el0\n\t"
		     "isb"
		     : "=r" (cntpct), "=r" (cpu_cyc));

	*stat = (typeof(*stat)){ cntpct, cntpct, cpu_cyc };
}

static noinline void __aligned(L1_CACHE_BYTES)
fie_pmu_read(struct pmu_stat *stat)
{
	register u64 cntpct, cpu_cyc;

	asm volatile("isb\n\t"
		     "mrs %0, cntpct_el0\n\t"
		     "mrs %1, pmccntr_el0\n\t"
		     "isb"
		     : "=r" (cntpct), "=r" (cpu_cyc));

	*stat = (typeof(*stat)){ cntpct, cntpct, cpu_cyc };
}

static void pmu_get_stats(struct pmu_stat *stat)
{
	int cpu = raw_smp_processor_id();

	if (likely(per_cpu(cpu_has_amu, cpu))) {
		if (cpu_supports_amu_const(cpu))
			fie_amu_const_read(stat);
		else
			fie_amu_read(stat);
	} else {
		fie_pmu_read(stat);
	}
}

#define pmu_read_cur_ptrs(pmu, val1, val2) \
	asm volatile("ldp %[v1], %[v2], %[v]"				\
		     : [v1] "=r" (val1), [v2] "=r" (val2)		\
		     : [v] "Q" (*(__uint128_t *)pmu->cur_ptr))

#define __PMU_CMPXCHG_DBL_LOOP(new1_expr, new2_expr) \
	struct pmu_stat *old1, *old2, *new1, *new2;			\
	do {								\
		pmu_read_cur_ptrs(pmu, old1, old2);			\
		new1 = (new1_expr), new2 = (new2_expr);			\
	} while (!cmpxchg_double_local(&pmu->cur_ptr[0],		\
				       &pmu->cur_ptr[1],		\
				       old1, old2, new1, new2))
#define pmu_get_cur_writer(pmu) \
({									\
	__PMU_CMPXCHG_DBL_LOOP(old2 ? old1 : NULL, NULL);		\
	old2 ? old2 : old1;						\
})
#define pmu_put_cur_writer(pmu, cur) \
({									\
	__PMU_CMPXCHG_DBL_LOOP(cur, old1 ? old1 : old2);		\
})

static void pmu_update_stats(int cpu, struct cpu_pmu *pmu,
			     struct pmu_stat *cur, struct pmu_stat *prev)
{
	struct pmu_stat *cur_ptr;

	if (prev) {
		struct pmu_stat *ptr1, *ptr2;
		pmu_read_cur_ptrs(pmu, ptr1, ptr2);
		if (!ptr1)
			ptr1 = &pmu->cur[!(ptr2 - &pmu->cur[0])];
		*prev = *ptr1;
	}

	pmu_get_stats(cur);

	cur_ptr = pmu_get_cur_writer(pmu);
	*cur_ptr = *cur;
	pmu_put_cur_writer(pmu, cur_ptr);
}

static void reset_sfd_data(struct sfd_data *sfd)
{
	sfd->cpu_cyc = sfd->const_cyc = sfd->stale = 0;
}

static void add_sfd_data(struct sfd_data *sfd, const struct pmu_stat *cur,
			 const struct pmu_stat *prev)
{
	u64 delta_const_cyc = cur->const_cyc - prev->const_cyc;

	if (sfd->stale && delta_const_cyc >= cpu_min_sample_cntpct)
		reset_sfd_data(sfd);

	sfd->cpu_cyc += cur->cpu_cyc - prev->cpu_cyc;
	sfd->const_cyc += delta_const_cyc;
}

static void update_freq_scale(int cpu, struct rq *rq, bool local_cpu)
{
	struct cpu_pmu *pmu = &per_cpu(cpu_pmu_evs, cpu);
	struct sfd_data *sfd = &pmu->sfd;
	struct pmu_stat cur, prev;

	if (local_cpu)
		pmu_update_stats(cpu, pmu, &cur, &prev);

	raw_spin_lock(&sfd->lock);
	if (local_cpu)
		add_sfd_data(sfd, &cur, &prev);

	if (rq->cpu == cpu) {
		if (sfd->const_cyc >= cpu_min_sample_cntpct) {
			u64 max_freq = per_cpu(cpu_max_freq, cpu);
			u64 freq, ns = cntpct_to_ns(sfd->const_cyc);

			freq = min(max_freq, USEC_PER_SEC * sfd->cpu_cyc / ns);
			
			per_cpu(arch_freq_scale, cpu) =
				SCHED_CAPACITY_SCALE * freq / max_freq;
			reset_sfd_data(sfd);
		} else if (sfd->const_cyc) {
			sfd->stale = true;
		}
	}
	raw_spin_unlock(&sfd->lock);

	if (rq->cpu != cpu)
		update_freq_scale(rq->cpu, rq, false);
}

void fie_update_rq_clock(struct rq *rq)
{
	int cpu = raw_smp_processor_id();

	if (!static_branch_unlikely(&fie_ready))
		return;

	if (unlikely(!cpu_active(cpu) || !cpu_active(rq->cpu)))
		return;

	update_freq_scale(cpu, rq, true);
}

static void fie_cpu_idle(int cpu, bool idle)
{
	struct cpu_pmu *pmu = &per_cpu(cpu_pmu_evs, cpu);
	struct sfd_data *sfd = &pmu->sfd;
	struct pmu_stat cur, prev;

	if (!static_branch_unlikely(&fie_ready))
		return;

	if (unlikely(!cpu_active(cpu)))
		return;

	if (idle) {
		pmu_update_stats(cpu, pmu, &cur, &prev);

		raw_spin_lock(&sfd->lock);
		add_sfd_data(sfd, &cur, &prev);
		raw_spin_unlock(&sfd->lock);
	} else {
		if (!cpu_supports_amu_const(cpu))
			pmu_update_stats(cpu, pmu, &cur, NULL);
	}
}

/* Use mainline Linux cpuidle tracepoint for 4.19 */
static void fie_idle_probe(void *data, unsigned int state, unsigned int cpu_id)
{
	fie_cpu_idle(cpu_id, state != PWR_EVENT_EXIT);
}

static int fie_cpuhp_up(unsigned int cpu)
{
	struct cpu_pmu *pmu = &per_cpu(cpu_pmu_evs, cpu);
	struct sfd_data *sfd = &pmu->sfd;
	bool has_amu;
	int ret;

#ifdef CONFIG_ARM64_AMU_EXTN
	has_amu = this_cpu_has_cap(ARM64_HAS_AMU_EXTN);
#else
	has_amu = false;
#endif
	per_cpu(cpu_has_amu, cpu) = has_amu;

	if (has_amu) {
		u32 midr = read_cpuid_id();
		per_cpu(cpu_has_amu_const, cpu) =
			!(MIDR_IMPLEMENTOR(midr) == ARM_CPU_IMP_ARM &&
			  MIDR_PARTNUM(midr) == ARM_CPU_PART_CORTEX_A510);
	} else {
		per_cpu(cpu_has_amu_const, cpu) = false;
	}

	if (!has_amu) {
		ret = create_perf_events(cpu);
		if (ret)
			return ret;
	}

	pmu->cur_ptr[0] = &pmu->cur[0];
	pmu->cur_ptr[1] = &pmu->cur[1];

	local_irq_disable();
	pmu_get_stats(&pmu->cur[0]);
	local_irq_enable();
	reset_sfd_data(sfd);

	return 0;
}

static int fie_cpuhp_down(unsigned int cpu)
{
	if (!per_cpu(cpu_has_amu, cpu))
		release_perf_events(cpu);
	return 0;
}

static int fie_reboot(struct notifier_block *notifier, unsigned long val,
		      void *cmd)
{
	static_branch_disable(&fie_ready);
	kick_all_cpus_sync();
	cpuhp_remove_state_nocalls(cpuhp_state);
	return NOTIFY_OK;
}

static struct notifier_block fie_reboot_nb = {
	.notifier_call = fie_reboot,
	.priority = INT_MAX
};

static int __init fie_init(void)
{
	cpuhp_state = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "fie",
					fie_cpuhp_up, fie_cpuhp_down);
	BUG_ON(cpuhp_state <= 0);

	calc_cntpct_arith();

	/* Register standard mainline CPU idle tracepoint */
	BUG_ON(register_trace_cpu_idle(fie_idle_probe, NULL));

	static_branch_enable(&fie_ready);
	register_reboot_notifier(&fie_reboot_nb);

	pr_info("FIE: Frequency Invariance Engine initialized\n");
	return 0;
}
late_initcall(fie_init);