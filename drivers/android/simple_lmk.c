// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019-2023 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#define pr_fmt(fmt) "simple_lmk: " fmt

#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/oom.h>
#include <linux/sched/mm.h>
#include <linux/sort.h>
#include <linux/swap.h>
#include <linux/psi.h>
#include <uapi/linux/sched/types.h>

/* The minimum number of pages to free per reclaim */
#define MIN_FREE_PAGES (CONFIG_ANDROID_SIMPLE_LMK_MINFREE * SZ_1M / PAGE_SIZE)

/* Kill up to this many victims per reclaim */
#define MAX_VICTIMS 1024

/* Timeout in jiffies for each reclaim */
#define RECLAIM_EXPIRES msecs_to_jiffies(CONFIG_ANDROID_SIMPLE_LMK_TIMEOUT_MSEC)

/* Android oom_score_adj range is 0 to 1000 */
#define ADJ_MAX 1000

struct victim_info {
	struct task_struct *tsk;
	struct mm_struct *mm;
	unsigned long size;
	unsigned long score;
};

static struct victim_info victims[MAX_VICTIMS] __cacheline_aligned_in_smp;
static struct task_struct *task_bucket[ADJ_MAX + 1] __cacheline_aligned;
static DECLARE_WAIT_QUEUE_HEAD(oom_waitq);
static DECLARE_WAIT_QUEUE_HEAD(reaper_waitq);
static DECLARE_COMPLETION(reclaim_done);
static DECLARE_COMPLETION(psi_init_done);
static __cacheline_aligned_in_smp DEFINE_RWLOCK(mm_free_lock);

static unsigned long get_target_free_pages(void)
{
	unsigned long deficit;

	if (nr_free_pages() >= totalreserve_pages)
		return MIN_FREE_PAGES;

	deficit = totalreserve_pages - nr_free_pages();
	return max_t(unsigned long, MIN_FREE_PAGES, deficit + (deficit >> 2));
}

static int nr_victims;
static bool reclaim_active;

#define LMK_TIERS 3
static const short tier_min_adj[LMK_TIERS] = { 800, 200, 1 };

static atomic_t needs_reclaim = ATOMIC_INIT(0);
static atomic_t needs_reap = ATOMIC_INIT(0);
static atomic_t nr_killed = ATOMIC_INIT(0);
static atomic_t target_min_adj = ATOMIC_INIT(tier_min_adj[0]);

static int victim_cmp(const void *lhs_ptr, const void *rhs_ptr)
{
	const struct victim_info *lhs = (typeof(lhs))lhs_ptr;
	const struct victim_info *rhs = (typeof(rhs))rhs_ptr;

	if (rhs->score > lhs->score)
		return 1;
	if (rhs->score < lhs->score)
		return -1;
	return 0;
}

static int victim_cmp_size(const void *lhs_ptr, const void *rhs_ptr)
{
	const struct victim_info *lhs = (typeof(lhs))lhs_ptr;
	const struct victim_info *rhs = (typeof(rhs))rhs_ptr;

	if (rhs->size > lhs->size)
		return 1;
	if (rhs->size < lhs->size)
		return -1;
	return 0;
}

static void victim_swap(void *lhs_ptr, void *rhs_ptr, int size)
{
	struct victim_info *lhs = (typeof(lhs))lhs_ptr;
	struct victim_info *rhs = (typeof(rhs))rhs_ptr;

	swap(*lhs, *rhs);
}

static unsigned long get_total_mm_pages(struct mm_struct *mm)
{
	unsigned long pages = 0;
	int i;

	for (i = 0; i < NR_MM_COUNTERS; i++)
		pages += get_mm_counter(mm, i);

	return pages;
}

static unsigned long find_victims(int *vindex)
{
	short i, min_adj = ADJ_MAX, max_adj = 0;
	short limit_adj = atomic_read(&target_min_adj);
	unsigned long pages_found = 0;
	unsigned long target_pages = get_target_free_pages();
	struct task_struct *tsk;

	/*
	 * Phase 1: Walk the process list under RCU to collect pinned
	 * candidates. get_task_struct() prevents the task from being freed
	 * after we drop RCU, so the bucket chains remain valid.
	 */
	rcu_read_lock();
	for_each_process(tsk) {
		struct signal_struct *sig;
		short adj;

		/*
		 * Search for suitable tasks with a positive adj (importance).
		 * Since only tasks with a positive adj can be targeted, that
		 * naturally excludes tasks which shouldn't be killed, like init
		 * and kthreads. Although oom_score_adj can still be changed
		 * while this code runs, it doesn't really matter; we just need
		 * a snapshot of the task's adj.
		 */
		sig = tsk->signal;
		adj = READ_ONCE(sig->oom_score_adj);
		if (adj < limit_adj || adj > ADJ_MAX ||
		    sig->flags & (SIGNAL_GROUP_EXIT | SIGNAL_GROUP_COREDUMP) ||
		    (thread_group_empty(tsk) && tsk->flags & PF_EXITING))
			continue;

		get_task_struct(tsk);
		tsk->simple_lmk_next = task_bucket[adj];
		task_bucket[adj] = tsk;

		if (adj > max_adj)
			max_adj = adj;
		if (adj < min_adj)
			min_adj = adj;
	}
	rcu_read_unlock();

	/*
	 * Phase 2: Evaluate pinned candidates. Each candidate gets a brief
	 * RCU critical section only around find_lock_task_mm() (which needs
	 * RCU for for_each_thread()). This avoids holding rcu_read_lock()
	 * across the entire process walk and evaluation pass.
	 */
	for (i = max_adj; i >= min_adj; i--) {
		int old_vindex;
		struct task_struct *next;

		tsk = task_bucket[i];
		if (!tsk)
			continue;

		task_bucket[i] = NULL;

		old_vindex = *vindex;
		do {
			struct task_struct *vtsk;
			unsigned long pages = 0;

			next = tsk->simple_lmk_next;

			rcu_read_lock();
			vtsk = find_lock_task_mm(tsk);
			if (!vtsk || !vtsk->mm) {
				if (vtsk)
					task_unlock(vtsk);
				rcu_read_unlock();
				goto drop_ref;
			}

			pages = get_total_mm_pages(vtsk->mm);
			if (!pages) {
				task_unlock(vtsk);
				rcu_read_unlock();
				goto drop_ref;
			}

			get_task_struct(vtsk);
			mmgrab(vtsk->mm);
			task_unlock(vtsk);
			rcu_read_unlock();

			victims[*vindex].tsk = vtsk;
			victims[*vindex].mm = vtsk->mm;
			victims[*vindex].size = pages;

			pages_found += pages;

			if (++*vindex == MAX_VICTIMS) {
				put_task_struct(tsk);
				/*
				 * Drain the rest of this bucket's chain
				 * since task_bucket[i] is already NULL
				 * and drain_remaining won't find them.
				 */
				while (next) {
					tsk = next;
					next = tsk->simple_lmk_next;
					put_task_struct(tsk);
				}
				goto drain_remaining;
			}
drop_ref:
			put_task_struct(tsk);
		} while ((tsk = next));

		if (*vindex == old_vindex)
			continue;

		if (*vindex == MAX_VICTIMS || pages_found >= target_pages)
			break;
		}

drain_remaining:
	/* Release refs for any candidates still in buckets we didn't visit */
	for (i = min_adj; i <= max_adj; i++) {
		tsk = task_bucket[i];
		task_bucket[i] = NULL;
		while (tsk) {
			struct task_struct *next = tsk->simple_lmk_next;
			put_task_struct(tsk);
			tsk = next;
		}
	}

	return pages_found;
}

static int process_victims(int vlen)
{
	unsigned long pages_found = 0;
	unsigned long target_pages = get_target_free_pages();
	int i, nr_to_kill = 0;

	/*
	 * Calculate the number of tasks that need to be killed and quickly
	 * release the references to those that'll live.
	 */
	for (i = 0; i < vlen; i++) {
		struct victim_info *victim = &victims[i];
		struct task_struct *vtsk = victim->tsk;

		/* The victim's mm and task refs were taken in find_victims */
		if (pages_found >= target_pages) {
			mmdrop(victim->mm);
			put_task_struct(vtsk);
			victim->mm = NULL;
			victim->tsk = NULL;
		} else {
			pages_found += victim->size;
			nr_to_kill++;
		}
	}

	return nr_to_kill;
}

static void set_task_rt_prio(struct task_struct *tsk, int priority)
{
	const struct sched_param rt_prio = {
		.sched_priority = priority
	};

	sched_setscheduler_nocheck(tsk, SCHED_RR, &rt_prio);
}

static void scan_and_kill(void)
{
	static struct mm_struct *drop_mms[MAX_VICTIMS];
	int i, nr_to_kill, nr_found = 0;
	unsigned long pages_found;
	int num_drop;

	/* Populate the victims array with tasks sorted by adj and then size */
	pages_found = find_victims(&nr_found);
	if (unlikely(!nr_found)) {
		pr_err_ratelimited("No processes available to kill!\n");
		return;
	}

	/*
	 * Sort all victims by size (descending) to kill largest first,
	 * then select the minimum number needed to meet the target.
	 */
	sort(victims, nr_found, sizeof(*victims), victim_cmp_size, victim_swap);
		nr_to_kill = process_victims(nr_found);

	/*
	 * Store the final number of victims for simple_lmk_mm_freed() and the
	 * reaper thread, and indicate that reclaim is active.
	 */
	num_drop = 0;
	write_lock(&mm_free_lock);
	nr_victims = nr_to_kill;
	reclaim_active = true;
	for (i = 0; i < nr_to_kill; i++) {
		struct mm_struct *mm = victims[i].mm;

		if (mm && test_bit(MMF_OOM_SKIP, &mm->flags)) {
			victims[i].mm = NULL;
			drop_mms[num_drop++] = mm;
			atomic_inc(&nr_killed);
		}
	}
	if (atomic_read(&nr_killed) >= nr_victims)
		complete(&reclaim_done);
	write_unlock(&mm_free_lock);

	for (i = 0; i < num_drop; i++)
		mmdrop(drop_mms[i]);

	/* Kill the victims */
	for (i = 0; i < nr_to_kill; i++) {
		struct victim_info *victim = &victims[i];
		struct task_struct *t, *vtsk = victim->tsk;
		struct mm_struct *mm = victim->mm;

		pr_info("Killing %s with adj %d to free %lu KiB\n", vtsk->comm,
			vtsk->signal->oom_score_adj,
			victim->size << (PAGE_SHIFT - 10));

		/* Make the victim reap anonymous memory first in exit_mmap() */
		set_bit(MMF_OOM_VICTIM, &mm->flags);

		/*
		 * Yield between kills to let RCU grace periods progress
		 * and reduce scheduling contention from the thundering
		 * herd of exiting victims.
		 */
		if (i)
			cond_resched();

		/*
		 * Thaw the victim first so it can receive and process the
		 * kill signal immediately. Signals can't wake frozen tasks;
		 * only a thaw operation can.
		 */
		__thaw_task(vtsk);

		/* Accelerate the victim's death by forcing the kill signal */
		do_send_sig_info(SIGKILL, SEND_SIG_FORCED, vtsk, PIDTYPE_TGID);

		if (mm)
			set_bit(MMF_SIMPLE_LMK_VICTIM, &mm->flags);

		/*
		 * Mark the thread group dead so that other kernel code knows
		 * to prioritize their memory allocation. TIF_MEMDIE is
		 * sufficient for this - it tells the page allocator to
		 * give these tasks priority without changing scheduling
		 * class. Elevating to SCHED_RR causes RT throttling when
		 * many processes are killed simultaneously, which can
		 * trigger watchdog resets.
		 */
		rcu_read_lock();
		for_each_thread(vtsk, t)
			set_tsk_thread_flag(t, TIF_MEMDIE);
		rcu_read_unlock();

		/* Allow the victim to run on any CPU. This won't schedule. */
		set_cpus_allowed_ptr(vtsk, cpu_all_mask);

		/* Store the number of anon pages to sort victims for reaping */
		if (mm)
			victim->score = get_mm_counter(mm, MM_ANONPAGES);
		else
			victim->score = 0;

		/* We don't need the task_struct anymore */
		put_task_struct(vtsk);
		victim->tsk = NULL;
	}

	/*
	 * Sort the victims by descending order of anonymous pages so the reaper
	 * thread can prioritize reaping the victims with the most anonymous
	 * pages first. Then wake the reaper thread if it's asleep. The lock
	 * orders the needs_reap store before waitqueue_active().
	 */
	write_lock(&mm_free_lock);
	sort(victims, nr_to_kill, sizeof(*victims), victim_cmp, victim_swap);
	atomic_set(&needs_reap, 1);
	write_unlock(&mm_free_lock);
	if (waitqueue_active(&reaper_waitq))
		wake_up(&reaper_waitq);

	/* Wait until all the victims die or until the timeout is reached */
	if (!wait_for_completion_timeout(&reclaim_done, RECLAIM_EXPIRES))
		pr_info("Timeout hit waiting for victims to die, proceeding\n");

	/*
	 * The kernel's memory accounting can lag behind the actual freeing of
	 * memory. To avoid a "kill-loop" where we kill more processes than
	 * necessary due to stale vmpressure events, wait for the system state
	 * to actually recover. We wait until free pages reach a safe level
	 * AND the reaper thread has finished its work.
	 */
	wait_event_timeout(oom_waitq,
			   nr_free_pages() >= totalreserve_pages &&
			   atomic_read(&needs_reap) == 0,
			   msecs_to_jiffies(200));

	/* If memory already recovered, skip escalation entirely */
	if (nr_free_pages() >= totalreserve_pages)
		goto deescalate;

	/* Escalate PSI trigger if kill failed to free enough memory */
	if (nr_free_pages() < totalreserve_pages) {
		int current_adj = atomic_read(&target_min_adj);
		if (current_adj == tier_min_adj[0])
			atomic_set(&target_min_adj, tier_min_adj[1]);
		else if (current_adj == tier_min_adj[1])
			atomic_set(&target_min_adj, tier_min_adj[2]);

		atomic_set(&needs_reclaim, 1);
		if (waitqueue_active(&oom_waitq))
			wake_up(&oom_waitq);
	} else {
deescalate:
		/*
		 * Memory recovered — de-escalate to least aggressive tier.
		 * This prevents the driver from staying at a high kill tier
		 * after a transient memory spike has resolved.
		 */
		atomic_set(&target_min_adj, tier_min_adj[0]);
	}

	/* Clean up for future reclaims but let the reaper thread keep going */
	write_lock(&mm_free_lock);
	reinit_completion(&reclaim_done);
	reclaim_active = false;
	atomic_set(&nr_killed, 0);
	write_unlock(&mm_free_lock);
}

static int simple_lmk_reclaim_thread(void *data)
{
	/* Use maximum RT priority */
	set_task_rt_prio(current, MAX_RT_PRIO - 1);
	set_freezable();

	while (1) {
		wait_event_freezable(oom_waitq, atomic_read(&needs_reclaim));
		/*
		 * Clear needs_reclaim before scanning so that any escalation
		 * signal set by scan_and_kill() (or a new PSI event arriving
		 * during the scan) is not lost.
		 */
		atomic_set(&needs_reclaim, 0);
		scan_and_kill();
	}

	return 0;
}

static struct mm_struct *next_reap_victim(void)
{
	struct mm_struct *mm = NULL;
	bool should_retry = false;
	int i;

	/* Take a write lock so no victim's mm can be freed while scanning */
	write_lock(&mm_free_lock);
	for (i = 0; i < nr_victims; i++, mm = NULL) {
		/* Check if this victim is alive and hasn't been reaped yet */
		mm = victims[i].mm;
		if (!mm || test_bit(MMF_OOM_SKIP, &mm->flags))
			continue;

		/* Do a trylock so the reaper thread doesn't sleep */
		if (!down_read_trylock(&mm->mmap_sem)) {
			should_retry = true;
			continue;
		}

		/*
		 * If mm_users is 0, __mmput is running. exit_mmap tears down
		 * VMAs without holding mmap_lock at the end. We must skip
		 * to avoid use-after-free when walking VMAs.
		 */
		if (!atomic_read(&mm->mm_users)) {
			up_read(&mm->mmap_sem);
			continue;
		}

		/*
		 * Check MMF_OOM_SKIP again under the lock in case this mm was
		 * reaped by exit_mmap() and then had its page tables destroyed.
		 * No mmgrab() is needed because the reclaim thread sets
		 * MMF_OOM_VICTIM under task_lock() for the mm's task, which
		 * guarantees that MMF_OOM_VICTIM is always set before the
		 * victim mm can enter exit_mmap(). Therefore, an mmap read lock
		 * is sufficient to keep the mm struct itself from being freed.
		 */
		if (!test_bit(MMF_OOM_SKIP, &mm->flags))
			break;
		up_read(&mm->mmap_sem);
	}

	if (!mm) {
		if (should_retry)
			/* Return ERR_PTR(-EAGAIN) to try reaping again later */
			mm = ERR_PTR(-EAGAIN);
		else if (!reclaim_active)
			/*
			 * Nothing left to reap, so stop simple_lmk_mm_freed()
			 * from iterating over the victims array since reclaim
			 * is no longer active. Return NULL to stop reaping.
			 */
			nr_victims = 0;
	}
	write_unlock(&mm_free_lock);

	return mm;
}

static void reap_victims(void)
{
	struct mm_struct *mm;

	while ((mm = next_reap_victim())) {
		if (IS_ERR(mm)) {
			/* Wait one jiffy before trying to reap again */
			schedule_timeout_uninterruptible(1);
			continue;
		}

		/*
		 * Try to reap the victim. Unflag the mm for exit_mmap() reaping
		 * and mark it as reaped with MMF_OOM_SKIP if successful.
		 */
		if (__oom_reap_task_mm(mm)) {
			clear_bit(MMF_OOM_VICTIM, &mm->flags);
			set_bit(MMF_OOM_SKIP, &mm->flags);
		}
		up_read(&mm->mmap_sem);

		/* Yield to let RCU grace periods and other work proceed */
		cond_resched();
	}
}

static int simple_lmk_reaper_thread(void *data)
{
	/* Use a lower priority than the reclaim thread */
	set_task_rt_prio(current, MAX_RT_PRIO - 2);
	set_freezable();

	while (1) {
		wait_event_freezable(reaper_waitq,
				     atomic_cmpxchg_relaxed(&needs_reap, 1, 0));
		reap_victims();
	}

	return 0;
}

void simple_lmk_mm_freed(struct mm_struct *mm)
{
	int i;
	bool matched = false;

	/*
	 * Victims are guaranteed to have MMF_OOM_SKIP set after exit_mmap()
	 * finishes. Use this to ignore unrelated dying processes.
	 */
	if (!test_bit(MMF_OOM_SKIP, &mm->flags) || !test_bit(MMF_SIMPLE_LMK_VICTIM, &mm->flags))
		return;

	/*
	 * Fast path: if no reclaim is active and the reaper is done, then
	 * there's no need to search the victims array.
	 */
	if (!reclaim_active && !atomic_read(&needs_reap))
		return;

	write_lock(&mm_free_lock);
	for (i = 0; i < nr_victims; i++) {
		if (victims[i].mm == mm) {
			/*
			 * Clear out this victim from the victims array and only
			 * increment nr_killed if reclaim is active. If reclaim
			 * isn't active, then clearing out the victim is done
			 * solely for the reaper thread to avoid freed victims.
			 */
			victims[i].mm = NULL;
			if (reclaim_active &&
			    atomic_inc_return_relaxed(&nr_killed) == nr_victims)
				complete(&reclaim_done);
			matched = true;
			break;
		}
	}
	write_unlock(&mm_free_lock);

	if (matched)
		mmdrop(mm);
}

static struct psi_trigger *psi_triggers[LMK_TIERS];
static DECLARE_WAIT_QUEUE_HEAD(psi_waitq);

static int simple_lmk_psi_thread(void *data)
{
	set_task_rt_prio(current, MAX_RT_PRIO - 3);
	set_freezable();

	/* Wait for PSI triggers to be created before accessing them */
	wait_for_completion(&psi_init_done);

	while (!kthread_should_stop()) {
		short min_adj = ADJ_MAX;

		/*
		 * Sleep until a PSI trigger fires. wait_event_freezable
		 * checks try_to_freeze() before sleeping, allowing the
		 * freezer to suspend us.
		 */
		wait_event_freezable(psi_waitq,
				     READ_ONCE(psi_triggers[0]->event) ||
				     READ_ONCE(psi_triggers[1]->event) ||
				     READ_ONCE(psi_triggers[2]->event));

		/* Check triggers from highest to lowest severity */
		if (cmpxchg(&psi_triggers[2]->event, 1, 0)) {
			min_adj = tier_min_adj[2];
		} else if (cmpxchg(&psi_triggers[1]->event, 1, 0)) {
			min_adj = tier_min_adj[1];
		} else if (cmpxchg(&psi_triggers[0]->event, 1, 0)) {
			min_adj = tier_min_adj[0];
		}

		/*
		 * Map PSI stall events to target adj levels.
		 * reclaim_active is used to ensure we don't start a new cycle
		 * while scan_and_kill is still in its settle phase.
		 */
		if (min_adj != ADJ_MAX && !reclaim_active) {
			atomic_set(&target_min_adj, min_adj);
			if (!atomic_xchg(&needs_reclaim, 1) && waitqueue_active(&oom_waitq))
			wake_up(&oom_waitq);
	}
	}

	return 0;
}

/* Initialize Simple LMK when lmkd in Android writes to the minfree parameter */
static int simple_lmk_init_set(const char *val, const struct kernel_param *kp)
{
	static atomic_t init_done = ATOMIC_INIT(0);
	struct task_struct *thread;
	int thresholds[LMK_TIERS] = {
		CONFIG_ANDROID_SIMPLE_LMK_PSI_THRESHOLD_LOW_US,
		CONFIG_ANDROID_SIMPLE_LMK_PSI_THRESHOLD_MED_US,
		CONFIG_ANDROID_SIMPLE_LMK_PSI_THRESHOLD_HIGH_US
	};
	int i;

	if (!atomic_cmpxchg(&init_done, 0, 1)) {
		thread = kthread_run(simple_lmk_reaper_thread, NULL,
				     "simple_lmkd_reaper");
		if (WARN_ON(IS_ERR(thread)))
			return PTR_ERR(thread);

		thread = kthread_run(simple_lmk_reclaim_thread, NULL,
				     "simple_lmkd");
		if (WARN_ON(IS_ERR(thread)))
			return PTR_ERR(thread);

		/*
		 * Create PSI triggers before the PSI monitor thread so
		 * the triggers are ready when the thread wakes up.
		 */
		for (i = 0; i < LMK_TIERS; i++) {
			char buf[64];
			snprintf(buf, sizeof(buf), "full %d %d", thresholds[i],
				 CONFIG_ANDROID_SIMPLE_LMK_PSI_WINDOW_MS * 1000);
			psi_triggers[i] = psi_trigger_create(&psi_system, buf, PSI_MEM);
			if (WARN_ON(IS_ERR(psi_triggers[i])))
				return PTR_ERR(psi_triggers[i]);
			psi_trigger_set_waitq(psi_triggers[i], &psi_waitq);
		}

		thread = kthread_run(simple_lmk_psi_thread, NULL,
				     "simple_lmkd_psi");
		if (WARN_ON(IS_ERR(thread)))
			return PTR_ERR(thread);

		complete(&psi_init_done);
	}

	return 0;
}

static const struct kernel_param_ops simple_lmk_init_ops = {
	.set = simple_lmk_init_set
};

/* Needed to prevent Android from thinking there's no LMK and thus rebooting */
#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "lowmemorykiller."
module_param_cb(minfree, &simple_lmk_init_ops, NULL, 0200);
