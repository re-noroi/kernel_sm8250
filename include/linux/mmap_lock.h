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

static inline void vma_end_write_all(struct mm_struct *mm) {}

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
		rwsem_release(&mm->mmap_sem.dep_map, _RET_IP_);
		return true;
	}
	return false;
}

static inline void mmap_read_unlock_non_owner(struct mm_struct *mm)
{
	up_read_non_owner(&mm->mmap_sem);
}

#endif /* _LINUX_MMAP_LOCK_H */
