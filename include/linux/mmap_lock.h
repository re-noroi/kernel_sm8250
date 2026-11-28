/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_LOCK_H
#define _LINUX_MMAP_LOCK_H

/*
 * 4.19 compatibility shim for the mmap_lock API introduced by the
 * Scarlet 5.4 tree.  4.19 still has mm->mmap_sem, so provide the newer
 * API as wrappers around it instead of renaming the field tree-wide.
 * Adapted from include/linux/mmap_lock.h in the source tree.
 */

#include <linux/mmdebug.h>
#include <linux/rwsem.h>

#define MMAP_LOCK_INITIALIZER(name) \
	.mmap_sem = __RWSEM_INITIALIZER((name).mmap_sem),

static inline void mmap_assert_locked(struct mm_struct *mm)
{
	lockdep_assert_held(&mm->mmap_sem);
	VM_BUG_ON_MM(!rwsem_is_locked(&mm->mmap_sem), mm);
}

static inline void mmap_assert_write_locked(struct mm_struct *mm)
{
	lockdep_assert_held_write(&mm->mmap_sem);
	VM_BUG_ON_MM(!rwsem_is_locked(&mm->mmap_sem), mm);
}

#ifdef CONFIG_PER_VMA_LOCK
/*
 * Drop all currently-held per-VMA locks.
 * This is called from the mmap_lock implementation directly before releasing
 * a write-locked mmap_lock (or downgrading it to read-locked).
 * This should normally NOT be called manually from other places.
 * If you want to call this manually anyway, keep in mind that this will release
 * *all* VMA write locks, including ones from further up the stack.
 */
void __mm_reset_vma_seqcounts(struct mm_struct *mm);

static inline void vma_end_write_all(struct mm_struct *mm)
{
	int seq;

	mmap_assert_write_locked(mm);
	/*
	 * Nobody can concurrently modify mm->mm_lock_seq due to exclusive
	 * mmap_write_lock being held.
	 * -1 is the vm_lock_seq of a never write-locked VMA and the value
	 * __mm_reset_vma_seqcounts() stores, so mm_lock_seq must never take it.
	 */
	seq = mm->mm_lock_seq + 1;
	if (unlikely(seq == -1))
		seq = 0;
	/*
	 * We need RELEASE semantics here to ensure that preceding stores into
	 * the VMA take effect before we unlock it with this store.
	 * Pairs with ACQUIRE semantics in vma_start_read().
	 */
	smp_store_release(&mm->mm_lock_seq, seq);

	/*
	 * VMA seqcounts are not updated on unlock, so once mm_lock_seq has
	 * wrapped, a VMA still carrying a value from before the wrap would look
	 * write-locked when that value comes round again, and vma_start_write()
	 * would then not exclude readers. Reset every VMA's seqcount so that no
	 * stale value survives the wrap.
	 */
	if (unlikely(!seq))
		__mm_reset_vma_seqcounts(mm);
}
#else
static inline void vma_end_write_all(struct mm_struct *mm) {}
#endif

static inline void mmap_init_lock(struct mm_struct *mm)
{
	init_rwsem(&mm->mmap_sem);
}

static inline void mmap_write_lock(struct mm_struct *mm)
{
	down_write(&mm->mmap_sem);
}

static inline void mmap_write_lock_nested(struct mm_struct *mm, int subclass)
{
	down_write_nested(&mm->mmap_sem, subclass);
}

static inline int mmap_write_lock_killable(struct mm_struct *mm)
{
	return down_write_killable(&mm->mmap_sem);
}

static inline bool mmap_write_trylock(struct mm_struct *mm)
{
	return down_write_trylock(&mm->mmap_sem) != 0;
}

static inline void mmap_write_unlock(struct mm_struct *mm)
{
	vma_end_write_all(mm);
	up_write(&mm->mmap_sem);
}

static inline void mmap_write_downgrade(struct mm_struct *mm)
{
	vma_end_write_all(mm);
	downgrade_write(&mm->mmap_sem);
}

static inline void mmap_read_lock(struct mm_struct *mm)
{
	down_read(&mm->mmap_sem);
}

static inline int mmap_read_lock_killable(struct mm_struct *mm)
{
	return down_read_killable(&mm->mmap_sem);
}

static inline bool mmap_read_trylock(struct mm_struct *mm)
{
	return down_read_trylock(&mm->mmap_sem) != 0;
}

static inline void mmap_read_unlock(struct mm_struct *mm)
{
	up_read(&mm->mmap_sem);
}

static inline bool mmap_read_trylock_non_owner(struct mm_struct *mm)
{
	if (down_read_trylock(&mm->mmap_sem)) {
#ifdef CONFIG_DEBUG_LOCK_ALLOC
		/*
		 * 4.19's rwsem_release() takes (map, nested, ip) and
		 * rw_semaphore.dep_map only exists with lock debugging on.
		 */
		rwsem_release(&mm->mmap_sem.dep_map, 1, _RET_IP_);
#endif
		return true;
	}
	return false;
}

static inline void mmap_read_unlock_non_owner(struct mm_struct *mm)
{
	up_read_non_owner(&mm->mmap_sem);
}

#endif /* _LINUX_MMAP_LOCK_H */
