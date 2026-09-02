// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal bpf stub 
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/btf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <trace/events/sched.h>
#include <trace/events/vmscan.h>
#include <trace/events/oom.h>
#include <trace/events/power.h>
#include <trace/events/gpu_mem.h>

/* Export tracepoint symbols for BPF programs */
EXPORT_TRACEPOINT_SYMBOL(sched_process_exit);
EXPORT_TRACEPOINT_SYMBOL(sched_process_fork);
EXPORT_TRACEPOINT_SYMBOL(sched_switch);
EXPORT_TRACEPOINT_SYMBOL(vmscan_mm_vmscan_direct_reclaim_begin);
EXPORT_TRACEPOINT_SYMBOL(vmscan_mm_vmscan_direct_reclaim_end);
EXPORT_TRACEPOINT_SYMBOL(vmscan_mm_vmscan_kswapd_sleep);
EXPORT_TRACEPOINT_SYMBOL(vmscan_mm_vmscan_kswapd_wake);
EXPORT_TRACEPOINT_SYMBOL(oom_mark_victim);
EXPORT_TRACEPOINT_SYMBOL(power_cpu_frequency);
EXPORT_TRACEPOINT_SYMBOL(power_cpu_frequency_limits);
EXPORT_TRACEPOINT_SYMBOL(scheduler_sched_cpu_util);
EXPORT_TRACEPOINT_SYMBOL(gpu_mem_total);

/* Weak symbols for optional tracepoints */
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

/* Base function proto for all stub program types */
static const struct bpf_func_proto *
tp_stub_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return bpf_base_func_proto(func_id);
}

/* Base access check for all stub program types */
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

/* ============================================================
 * Verifier ops for BPF program types NOT in net/core/filter.c
 * ============================================================ */

/* Tracepoint programs */
const struct bpf_verifier_ops tracepoint_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops tracepoint_prog_ops = {
};

/* Raw tracepoint programs */
const struct bpf_verifier_ops raw_tracepoint_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops raw_tracepoint_prog_ops = {
};

/* Raw tracepoint writable programs */
const struct bpf_verifier_ops raw_tracepoint_writable_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops raw_tracepoint_writable_prog_ops = {
};

/* Tracing programs (kprobe, uprobe, etc.) */
const struct bpf_verifier_ops tracing_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops tracing_prog_ops = {
};

/* Kprobe programs */
const struct bpf_verifier_ops kprobe_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops kprobe_prog_ops = {
};

/* Uprobe programs */
const struct bpf_verifier_ops uprobe_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops uprobe_prog_ops = {
};

/* Cgroup sysctl programs */
const struct bpf_verifier_ops cgroup_sysctl_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_sysctl_prog_ops = {
};

/* Cgroup device programs */
const struct bpf_verifier_ops cgroup_device_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_device_prog_ops = {
};

/* Struct ops programs */
const struct bpf_verifier_ops struct_ops_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops struct_ops_prog_ops = {
};

/* LSM programs */
const struct bpf_verifier_ops lsm_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops lsm_prog_ops = {
};

/* CGROUP_SOCKOPT programs */
const struct bpf_verifier_ops cgroup_sockopt_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_sockopt_prog_ops = {
};

/* CGROUP_GETSOCKOPT programs */
const struct bpf_verifier_ops cgroup_getsockopt_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_getsockopt_prog_ops = {
};

/* CGROUP_BIND programs */
const struct bpf_verifier_ops cgroup_bind_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_bind_prog_ops = {
};

/* CGROUP_CONNECT programs */
const struct bpf_verifier_ops cgroup_connect_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_connect_prog_ops = {
};

/* CGROUP_SENDMSG programs */
const struct bpf_verifier_ops cgroup_sendmsg_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_sendmsg_prog_ops = {
};

/* CGROUP_RECVMSG programs */
const struct bpf_verifier_ops cgroup_recvmsg_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_recvmsg_prog_ops = {
};

/* CGROUP_GETPEERNAME programs */
const struct bpf_verifier_ops cgroup_getpeername_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_getpeername_prog_ops = {
};

/* CGROUP_GETSOCKNAME programs */
const struct bpf_verifier_ops cgroup_getsockname_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops cgroup_getsockname_prog_ops = {
};

/* SK_RELEASE programs */
const struct bpf_verifier_ops sk_release_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops sk_release_prog_ops = {
};

/* EXT programs */
const struct bpf_verifier_ops ext_verifier_ops = {
	.get_func_proto  = tp_stub_func_proto,
	.is_valid_access = tp_stub_is_valid_access,
};

const struct bpf_prog_ops ext_prog_ops = {
};