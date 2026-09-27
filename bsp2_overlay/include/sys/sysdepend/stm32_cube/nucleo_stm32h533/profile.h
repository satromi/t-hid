/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2024 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *
 *    NUCLEO-H533RE (STM32H533RET6) Service Profile
 *----------------------------------------------------------------------
 */

#ifndef _MTKBSP_SYS_DEPEND_PROFILE_H_
#define _MTKBSP_SYS_DEPEND_PROFILE_H_

/*
 **** CPU Core depended profile (ARMv8-M)
 */
#include <sys/sysdepend/stm32_cube/cpu/core/armv8m/profile.h>

/*
 **** Target-depeneded profile (STM32Cube NUCLEO STM32H533)
 */

/*
 * Power management
 */
#define TK_SUPPORT_LOWPOWER	FALSE

/*
 * Device Support
 */
#define TK_SUPPORT_IOPORT	TRUE

/*
 * Physical timer
 */
#if USE_PTMR
#define TK_SUPPORT_PTIMER	TRUE
#define TK_MAX_PTIMER		2
#else
#define TK_SUPPORT_PTIMER	FALSE
#define TK_MAX_PTIMER		0
#endif

#endif /* _MTKBSP_SYS_DEPEND_PROFILE_H_ */
