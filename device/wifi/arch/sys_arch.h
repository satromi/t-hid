/*
 *	arch/sys_arch.h — lwIP NO_SYS=1 用最小 sys_arch
 *
 *	NO_SYS=1 では sys_arch の実装は不要だが、
 *	sys_now() だけは timeouts.c が要求する。
 */

#ifndef __LWIP_ARCH_SYS_ARCH_H__
#define __LWIP_ARCH_SYS_ARCH_H__

#include <stdint.h>

typedef uint32_t sys_prot_t;

#endif /* __LWIP_ARCH_SYS_ARCH_H__ */
