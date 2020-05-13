// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024-2025 Sultan Alsawaf <sultan@kerneltoast.com>.
 * Adapted for Linux 4.19 (SM8250)
 */

#include <linux/cpufreq.h>
#include <linux/fie.h>
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
 * The maximum amount of time allowed for a CPU frequency ramp up to latch
 * before reporting the entire CPU domain as throttled to the scheduler. This
 * helps recover performance lost due to the scheduler's lack of awareness of
 * varying transition latency, which can exceed 10 ms in some cases.
 */
static u64 cpu_ramp_up_lat_cntpct __read_mostly = 500 * NSEC_PER_USEC;

/*
 * Compare two CPU frequencies to see if they are sufficiently close, within ~5%
 * of each other by default. This mimics capacity_greater() in sched/fair.c,
 * with the intent being that if the real CPU frequency is close enough to the
 * target frequency then there's no need to inform the scheduler about it.
 */
#define cpu_freqs_similar(lower_freq, higher_freq) \
	((u64)(lower_freq) * 1078 >= (u64)(higher_freq) * 1024)

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
	cpu_ramp_up_lat_cntpct = ns_to_cntpct(cpu_ramp_up_lat_cntpct);
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
	struct htd_data {
		u64 start;
		u64 cpu_cyc;
		u64 const_cyc;
	} htd; /* Hardware Throttle Data */
};

static DEFINE_PER_CPU(struct cpu_pmu, cpu_pmu_evs) = {
	.sfd.lock = __RAW_SPIN_LOCK_UNLOCKED(cpu_pmu_evs.sfd.lock)
};

static DEFINE_PER_CPU_READ_MOSTLY(bool, cpu_has_amu);
static DEFINE_PER_CPU_READ_MOSTLY(bool, cpu_has_amu_const);
static DEFINE_PER_CPU_READ_MOSTLY(u32, cpu_max_freq);

enum cpu_throttle_src {
	CPU_CPUFREQ_THROTTLE,
	CPU_HW_THROTTLE,
	MAX_CPU_THROTTLE_SRCS
};

struct throt_data {
	struct list_head node;
	raw_spinlock_t throt_lock;
	raw_spinlock_t idle_cpu_lock;
	raw_spinlock_t rate_lock;
	cpumask_t cpus;
	unsigned int cap[MAX_CPU_THROTTLE_SRCS];
	unsigned int idle_cpus;
	unsigned int nr_domain_cpus;
	u64 last_htd_cntpct;
	int cpu;
	struct fie_rate_info rate;
};

static DEFINE_PER_CPU_READ_MOSTLY(struct throt_data *, domain_throt_data);
static LIST_HEAD(domain_throt_list);

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
	struct throt_data *t;
	int cpu;

	for_each_cpu(cpu, cpus)
		per_cpu(cpu_max_freq, cpu) = max_freq;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	BUG_ON(!t);
	memset32(t->cap, UINT_MAX, ARRAY_SIZE(t->cap));
	raw_spin_lock_init(&t->throt_lock);
	raw_spin_lock_init(&t->idle_cpu_lock);
	raw_spin_lock_init(&t->rate_lock);
	cpumask_copy(&t->cpus, cpus);
	t->cpu = cpumask_first(cpus);
	t->nr_domain_cpus = cpumask_weight(cpus);

	for_each_cpu(cpu, cpus)
		per_cpu(domain_throt_data, cpu) = t;

	list_add_tail(&t->node, &domain_throt_list);
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

/* Must be called with t->throt_lock held */
static void update_thermal_pressure(struct throt_data *t,
				    enum cpu_throttle_src src, unsigned int cap)
{
	unsigned int capped_freq = UINT_MAX;
	int i;

	/*
	 * Update the thermal pressure for the designated source if it's
	 * different, and then aggregate the thermal pressure applied by all
	 * sources. This updates all CPUs within the same clock domain.
	 */
	if (t->cap[src] == cap)
		return;

	t->cap[src] = cap;
	for (i = 0; i < ARRAY_SIZE(t->cap); i++) {
		if (t->cap[i] < capped_freq)
			capped_freq = t->cap[i];
	}
	arch_update_thermal_pressure(&t->cpus, capped_freq);
}

void fie_cpufreq_pressure(int cpu, unsigned int cap)
{
	struct throt_data *t = per_cpu(domain_throt_data, cpu);
	unsigned long flags;

	if (!t)
		return;

	/* Update the throttle set via cpufreq policy (e.g., via LMh) */
	raw_spin_lock_irqsave(&t->throt_lock, flags);
	update_thermal_pressure(t, CPU_CPUFREQ_THROTTLE, cap);
	raw_spin_unlock_irqrestore(&t->throt_lock, flags);
}
EXPORT_SYMBOL_GPL(fie_cpufreq_pressure);

void fie_rate_set(int cpu, unsigned int freq)
{
	struct throt_data *t = per_cpu(domain_throt_data, cpu);

	if (!t)
		return;

	raw_spin_lock(&t->rate_lock);
	if (!t->rate.set_time)
		t->rate.set_time = arch_counter_get_cntpct();
	t->rate.freq = freq;
	raw_spin_unlock(&t->rate_lock);
}
EXPORT_SYMBOL_GPL(fie_rate_set);

static void fie_rate_info(int cpu, struct fie_rate_info *r)
{
	struct throt_data *t = per_cpu(domain_throt_data, cpu);

	raw_spin_lock(&t->rate_lock);
	*r = t->rate;
	raw_spin_unlock(&t->rate_lock);
}

static void fie_rate_latched(int cpu, const struct fie_rate_info *cookie)
{
	struct throt_data *t = per_cpu(domain_throt_data, cpu);

	raw_spin_lock(&t->rate_lock);
	if (t->rate.set_time == cookie->set_time &&
	    t->rate.freq == cookie->freq)
		t->rate.set_time = 0;
	raw_spin_unlock(&t->rate_lock);
}

static void reset_htd_data(struct htd_data *htd)
{
	htd->cpu_cyc = htd->const_cyc = htd->start = 0;
}

static void add_htd_data(struct htd_data *htd, const struct pmu_stat *cur,
			 const struct pmu_stat *prev)
{
	/* Record the starting time of this sample window */
	if (!htd->start)
		htd->start = cur->cntpct;

	/* Accumulate data for calculating the CPU's frequency */
	htd->cpu_cyc += cur->cpu_cyc - prev->cpu_cyc;
	htd->const_cyc += cur->const_cyc - prev->const_cyc;
}

static void update_cpu_hw_throttle(void)
{
	int cpu = raw_smp_processor_id();
	struct throt_data *t = per_cpu(domain_throt_data, cpu);
	struct cpu_pmu *pmu = &per_cpu(cpu_pmu_evs, cpu);
	struct htd_data *htd = &pmu->htd;
	struct fie_rate_info rate_info;
	u64 freq, max_freq, ns;

	if (!t)
		goto reset_stats;

	/*
	 * Check that enough time has passed to measure the CPU's frequency. If
	 * not, it means that the CPU spent so much time idle during this jiffy
	 * that checking for hardware throttling is pointless; as such, the
	 * stats should be reset so that stale data is not carried forward.
	 */
	if (htd->const_cyc < cpu_min_sample_cntpct)
		goto reset_stats;

	/* Calculate the measured frequency */
	max_freq = per_cpu(cpu_max_freq, cpu);
	ns = cntpct_to_ns(htd->const_cyc);
	freq = min(max_freq, USEC_PER_SEC * htd->cpu_cyc / ns);

	/*
	 * It may take a while for a CPU frequency change to latch, or the
	 * hardware may have other intentions and opaquely refuse to switch a
	 * CPU domain to the governor's desired target frequency due to hardware
	 * throttling. This is evident by observing the real CPU frequency
	 * measured via the cycle counter: sometimes it takes several
	 * milliseconds for a frequency switch to latch, while other times under
	 * heavy load the target frequency won't latch indefinitely due to
	 * hardware throttling.
	 *
	 * Therefore, when a CPU frequency switch to a higher frequency exceeds
	 * a specified latency threshold, tell the scheduler to assume that the
	 * respective CPU domain is throttled to the actual measured frequency.
	 * This helps mitigate misguided scheduling decisions from hurting
	 * performance, since the scheduler would be otherwise unaware that a
	 * CPU domain is throttled.
	 */
	fie_rate_info(cpu, &rate_info);

	/*
	 * Assume the raw measured frequency is the same as the set frequency
	 * if they are sufficiently close to each other.
	 */
	if (cpu_freqs_similar(freq, rate_info.freq))
		freq = rate_info.freq;

	if (freq < rate_info.freq) {
		/*
		 * If the measured frequency is below the target frequency, it
		 * can be due to two reasons: unknown hardware throttling or
		 * high transition latency to the requested frequency. In the
		 * case of high transition latency, give the transition at least
		 * cpu_ramp_up_lat_cntpct timer ticks to latch before telling
		 * the scheduler that this CPU domain is throttled.
		 */
		if (htd->start < rate_info.set_time + cpu_ramp_up_lat_cntpct)
			goto reset_stats;
	} else {
		/* Reject sample windows older than the last rate switch */
		if (htd->start < rate_info.set_time)
			goto reset_stats;

		/*
		 * Notify that the requested rate is now "latched"; i.e., that
		 * the measured frequency is either at or above the target.
		 */
		if (rate_info.set_time)
			fie_rate_latched(cpu, &rate_info);

		/* Indicate there's no hardware throttle detected */
		freq = UINT_MAX;
	}

	/*
	 * Report the throttle detected by measuring the real frequency, unless
	 * there's a newer frequency measurement from another CPU in the domain.
	 */
	raw_spin_lock(&t->throt_lock);
	if (htd->start > t->last_htd_cntpct) {
		t->last_htd_cntpct = htd->start;
		update_thermal_pressure(t, CPU_HW_THROTTLE, freq);
	}
	raw_spin_unlock(&t->throt_lock);

reset_stats:
	reset_htd_data(htd);
}

static void set_cpu_hw_throttle_idle(int cpu, bool idle)
{
	struct throt_data *t = per_cpu(domain_throt_data, cpu);

	if (!t)
		return;

	raw_spin_lock(&t->idle_cpu_lock);
	if (idle) {
		/*
		 * Clear the measured hardware throttle for the CPU domain when
		 * all CPUs in the domain are idle.
		 */
		if (++t->idle_cpus == t->nr_domain_cpus) {
			raw_spin_lock(&t->throt_lock);
			update_thermal_pressure(t, CPU_HW_THROTTLE, UINT_MAX);
			raw_spin_unlock(&t->throt_lock);
		}
	} else {
		t->idle_cpus--;
	}
	raw_spin_unlock(&t->idle_cpu_lock);
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
	struct htd_data *htd = &pmu->htd;
	struct pmu_stat cur, prev;

	if (local_cpu) {
		pmu_update_stats(cpu, pmu, &cur, &prev);
		add_htd_data(htd, &cur, &prev);
	}

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

/*
 * Called from scheduler_tick() just before it updates the thermal load average.
 * Updates the measured hardware throttle of this CPU domain just before that
 * happens. update_cpu_hw_throttle() checks the starting time of the sample
 * window to ensure that only the latest measurements from a CPU in a CPU domain
 * are used.
 */
static void fie_tick_entry(void *data, struct rq *rq)
{
	update_cpu_hw_throttle();
}

static void fie_cpu_idle(int cpu, bool idle)
{
	struct cpu_pmu *pmu = &per_cpu(cpu_pmu_evs, cpu);
	struct sfd_data *sfd = &pmu->sfd;
	struct htd_data *htd = &pmu->htd;
	struct pmu_stat cur, prev;

	if (!static_branch_unlikely(&fie_ready))
		return;

	if (unlikely(!cpu_active(cpu)))
		return;

	set_cpu_hw_throttle_idle(cpu, idle);

	if (idle) {
		pmu_update_stats(cpu, pmu, &cur, &prev);

		raw_spin_lock(&sfd->lock);
		add_sfd_data(sfd, &cur, &prev);
		raw_spin_unlock(&sfd->lock);
	} else {
		if (!cpu_supports_amu_const(cpu))
			pmu_update_stats(cpu, pmu, &cur, NULL);

		/* Discard stale hardware throttle detection data */
		reset_htd_data(htd);
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
	struct htd_data *htd = &pmu->htd;
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
	reset_htd_data(htd);

	/* Clear the hardware throttle idle flag for this CPU */
	set_cpu_hw_throttle_idle(cpu, false);
	return 0;
}

static int fie_cpuhp_down(unsigned int cpu)
{
	if (!per_cpu(cpu_has_amu, cpu))
		release_perf_events(cpu);

	/*
	 * Set the hardware throttle idle flag for this CPU, so this CPU is
	 * considered idle insofar as the hardware throttle detection is
	 * concerned. IRQs must be disabled around set_cpu_hw_throttle_idle()
	 * because this may be the last non-idle CPU in the domain, in which
	 * case t->throt_lock would be taken, requiring IRQs to be disabled.
	 */
	local_irq_disable();
	set_cpu_hw_throttle_idle(cpu, true);
	local_irq_enable();
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

	/* Install the scheduler tick entry hook to detect CPU HW throttling */
	BUG_ON(register_trace_android_rvh_tick_entry(fie_tick_entry, NULL));

	/* Begin updating CPU scheduler statistics from update_rq_clock() */
	static_branch_enable(&fie_ready);
	register_reboot_notifier(&fie_reboot_nb);

	pr_info("FIE: Frequency Invariance Engine initialized\n");
	return 0;
}
late_initcall(fie_init);