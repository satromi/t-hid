/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2024 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *
 *    NUCLEO-H533RE (CPU: STM32H533RET6) board definition
 *    ARM Cortex-M33 (ARMv8-M Mainline), 250MHz, 512KB Flash, 272KB SRAM
 *----------------------------------------------------------------------
 */

#ifndef _MTKBSP_SYS_SYSDEPEND_MACHINE_H_
#define _MTKBSP_SYS_SYSDEPEND_MACHINE_H_

/* ----- NUCLEO STM32H533RE definition ----- */
#define MTKBSP_STM32CUBE		1	/* Target system   : STM32Cube */
#define MTKBSP_NUCLEO_STM32H533		1	/* Target Board    : NUCLEO STM32H533RE */

#define MTKBSP_CPU_STM32		1	/* Target CPU type : STM32 group */
#define MTKBSP_CPU_STM32H5		1	/* Target CPU type : STM32H5 series */
#define MTKBSP_CPU_STM32H533		1	/* Target CPU      : STM32H533 */

#define MTKBSP_CPU_CORE_ARMV8M		1	/* Target CPU-Core type : ARMv8-M */
#define MTKBSP_CPU_CORE_ACM33		1	/* Target CPU-Core      : ARM Cortex-M33 */

#define KNL_SYSDEP_PATH	sysdepend/stm32_cube

#define TARGET_DIR	stm32_cube/nucleo_stm32h533
#define TARGET_GRP_DIR	stm32_cube
#define	TARGET_CPU_DIR	stm32h5

/*
 **** CPU Core depended profile (ARMv8-M)
 */
#include <sys/sysdepend/stm32_cube/cpu/core/armv8m/machine.h>

#endif /* _MTKBSP_SYS_SYSDEPEND_MACHINE_H_ */
