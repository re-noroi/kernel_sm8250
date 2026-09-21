#ifndef __KSU_H_UTIL
#define __KSU_H_UTIL

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0) // ksyscall start
#if defined(__aarch64__)
#define KSU_SYS_PREFIX(name) __arm64_sys_##name
#elif defined(__x86_64__)
#define KSU_SYS_PREFIX(name) __x64_sys_##name
#else // arm / 32-bit
#define KSU_SYS_PREFIX(name) sys_##name
#endif

/**
 * ksyscall: call syscalls from kernelspace
 * - tries to copy unistd's syscall()
 *
 * usage: ksyscall(close, fd);
 */
#define __ksyscall(name, a, b, c, d, e, f) ({			\
	extern long KSU_SYS_PREFIX(name)(struct pt_regs *);	\
	struct pt_regs regs = { 0 };				\
	PT_REGS_PARM1(&regs) = (unsigned long)(a);		\
	PT_REGS_PARM2(&regs) = (unsigned long)(b);		\
	PT_REGS_PARM3(&regs) = (unsigned long)(c);		\
	PT_REGS_SYSCALL_PARM4(&regs) = (unsigned long)(d);	\
	PT_REGS_PARM5(&regs) = (unsigned long)(e);		\
	PT_REGS_PARM6(&regs) = (unsigned long)(f);		\
	(long)KSU_SYS_PREFIX(name)(&regs);			\
})

#define __ksyscall_pad(a, b, c, d, e, f, ...)	a, b, c, d, e, f
#define __ksyscall_exp(fn, args)		fn args
#define ksyscall(name, ...)			__ksyscall_exp(__ksyscall, (name, __ksyscall_pad(__VA_ARGS__, 0, 0, 0, 0, 0, 0)))
#endif // ksyscall end

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
static __always_inline int ksu_sys_umount(char __user *name, int flags) { return (int)ksyscall(umount, name, flags); }
#define ksu_sys_setns(fd, flags)	ksyscall(setns, fd, flags)
#define ksu_close_fd(fd)		ksyscall(close, fd)
#else
static __always_inline int ksu_sys_umount(char __user *name, int flags) { return (int)sys_umount(name, flags); }
#define ksu_close_fd sys_close
#define ksu_sys_setns sys_setns
#define ksys_unshare sys_unshare
#endif

static inline struct file *ksu_filp_open_nonotify(const char *path, int flags)
{
	struct path p;
	struct file *f;
	int ret;
	ret = kern_path(path, (flags & O_NOFOLLOW) ? 0 : LOOKUP_FOLLOW, &p);
	if (ret) {
		return ERR_PTR(ret);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)
	f = dentry_open_nonotify(&p, flags, current_cred());
#else
	f = dentry_open(&p, flags | __FMODE_NONOTIFY, current_cred());
#endif

	path_put(&p);
	return f;
}

#endif // __KSU_H_UTIL
