/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/stringify.h>

#define DECLARE_HOOK(name, proto, args)     \
    static inline void trace_##name(proto) {}   \
    static inline bool trace_##name##_enabled(void) { return false; } \
    static inline int register_trace_##name(void (*probe)(void *__data, proto), \
        void *data) { return -ENODEV; }

#define DECLARE_RESTRICTED_HOOK(name, proto, args, cond) \
    DECLARE_HOOK(name, PARAMS(proto), PARAMS(args))