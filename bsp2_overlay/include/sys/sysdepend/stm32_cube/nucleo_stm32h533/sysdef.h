/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2024 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *
 *    System dependencies definition (STM32Cube NUCLEO STM32H533)
 *    Included also from assembler program.
 *----------------------------------------------------------------------
 */

#ifndef _MTKBSP_SYS_SYSDEF_DEPEND_H_
#define _MTKBSP_SYS_SYSDEF_DEPEND_H_

/*
 * TrustZone
 *   make TZ=1 では μT-Kernel を非セキュア状態で動かす (セキュア側は tz_h533/secure)。
 *   タスク起動時の EXC_RETURN を非セキュアのスタックに戻る値にするために使う。
 */
#if defined(TZ_NONSECURE)
#define TRUSTZONE_ENABLE	1
#define TRUSTZONE_SECURE	0
#else
#define TRUSTZONE_ENABLE	0
#define TRUSTZONE_SECURE	0
#endif

/* CPU-dependent definition */
#include <sys/sysdepend/stm32_cube/cpu/stm32h5/sysdef.h>

#endif /* _MTKBSP_SYS_SYSDEF_DEPEND_H_ */
