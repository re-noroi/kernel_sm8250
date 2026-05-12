/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#ifndef _FIE_H_
#define _FIE_H_

struct rq;

void fie_update_rq_clock(struct rq *rq);
void fie_init_cpu_domain(const struct cpumask *cpus, unsigned int max_freq);

#endif /* _FIE_H_ */
