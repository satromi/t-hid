/*
 *	sys_arch.c — lwIP NO_SYS=1 用最小 sys_arch 実装
 *
 *	NO_SYS=1 では sys_arch のほとんどは不要だが、
 *	sys_now() だけは timeouts.c が呼び出すため実装が必要。
 */

#include "lwip/opt.h"

#if NO_SYS

#include <stdint.h>

/* cyw43_arch_tkernel.c で実装 */
extern uint32_t cyw43_hal_ticks_ms(void);

uint32_t sys_now(void)
{
    return cyw43_hal_ticks_ms();
}

#endif /* NO_SYS */
