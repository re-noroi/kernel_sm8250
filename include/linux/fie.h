/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#ifndef _FIE_H_
#define _FIE_H_

#include <linux/cpumask.h>
#include <linux/types.h>

struct rq;

struct fie_rate_info {
	u64 set_time;
	unsigned int freq;
};

void fie_update_rq_clock(struct rq *rq);
void fie_init_cpu_domain(const struct cpumask *cpus, unsigned int max_freq);
void fie_cpufreq_pressure(int cpu, unsigned int capped_freq);
void fie_rate_set(int cpu, unsigned int freq);
void fie_tick_entry(void);

#endif /* _FIE_H_ */
