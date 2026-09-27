/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2024 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *
 *    System dependencies definition (STM32H5 series)
 *    Included also from assembler program.
 *
 *    STM32H533RET6: Cortex-M33, 250MHz, 512KB Flash, 272KB SRAM
 *    Reference: RM0481 (STM32H533xx Reference Manual)
 *----------------------------------------------------------------------
 */

#ifndef __TK_SYSDEF_DEPEND_CPU_H__
#define __TK_SYSDEF_DEPEND_CPU_H__

#include <sys/machine.h>

/* CPU Core-dependent definition (ARMv8-M) */
#include <sys/sysdepend/stm32_cube/cpu/core/armv8m/sysdef.h>

/* ------------------------------------------------------------------------ */
/*
 * Internal Memory (Main RAM)
 *   STM32H533: SRAM 272KB @ 0x20000000
 */
#define INTERNAL_RAM_START      0x20000000
#if defined(TZ_NONSECURE)
#define INTERNAL_RAM_SIZE       0x0003C000      /* 240KB: 上位 32KB は TrustZone のセキュア側 */
#else
#define INTERNAL_RAM_SIZE       0x00044000      /* 272KB */
#endif

#define INTERNAL_RAM_END        (INTERNAL_RAM_START + INTERNAL_RAM_SIZE)

/* ------------------------------------------------------------------------ */
/*
 * System Timer clock
 */
#define MIN_TIMER_PERIOD	1
#define MAX_TIMER_PERIOD	50

/* ------------------------------------------------------------------------ */
/*
 * Number of Interrupt vectors
 *   STM32H533: IRQ 0-132 (I3C2_ER_IRQn = 132, CMSIS stm32h533xx.h)
 */
#define	N_SYSVEC		16	/* Number of System Exceptions */
#define N_INTVEC		133	/* Number of Interrupt vectors */

/*
 * Exception vector table alignment
 */
#define	EXCTBL_ALIGN		1024

/*
 * The number of the implemented bit width for priority value fields.
 *   STM32H533: 4-bit priority (Cortex-M33 NVIC)
 */
#define INTPRI_BITWIDTH		4

/* ------------------------------------------------------------------------ */
/*
 * Interrupt Priority Levels
 */
#define INTPRI_MAX_EXTINT_PRI	1	/* Highest Ext. interrupt level */
#define INTPRI_SVC		0	/* SVCall */
#define INTPRI_SYSTICK		1	/* SysTick */
#define INTPRI_PENDSV		15	/* PendSV */

/*
 * Time-event handler interrupt level
 */
#define TIMER_INTLEVEL		0

/* ------------------------------------------------------------------------ */
/*
 * EXTI (Extended interrupt controller)
 *   STM32H5: EXTI base = 0x44022000 (RM0481 Section 2.2)
 */
#define	N_EXTIEVT		58	/* Number of EXTI event input (STM32H533) */

#define MTK_EXTI_BASE		0x44022000

#define	EXTI_RTSR1	(MTK_EXTI_BASE + 0x00)
#define	EXTI_FTSR1	(MTK_EXTI_BASE + 0x04)
#define	EXTI_SWIER1	(MTK_EXTI_BASE + 0x08)
#define	EXTI_RPR1	(MTK_EXTI_BASE + 0x0C)	/* Rising pending (H5 uses RPR/FPR) */
#define	EXTI_FPR1	(MTK_EXTI_BASE + 0x10)	/* Falling pending */

#define	EXTI_RTSR2	(MTK_EXTI_BASE + 0x20)
#define	EXTI_FTSR2	(MTK_EXTI_BASE + 0x24)
#define	EXTI_SWIER2	(MTK_EXTI_BASE + 0x28)
#define	EXTI_RPR2	(MTK_EXTI_BASE + 0x2C)
#define	EXTI_FPR2	(MTK_EXTI_BASE + 0x30)

/* Block 3 (int_stm32h5.c が参照) */
#define	EXTI_RTSR3	(MTK_EXTI_BASE + 0x40)
#define	EXTI_FTSR3	(MTK_EXTI_BASE + 0x44)
#define	EXTI_SWIER3	(MTK_EXTI_BASE + 0x48)

/* EXTI IMR/EMR (STM32H5: different layout from H7) */
#define	EXTI_CPUIMR1	(MTK_EXTI_BASE + 0x80)
#define	EXTI_CPUEMR1	(MTK_EXTI_BASE + 0x84)
#define	EXTI_CPUPR1	(MTK_EXTI_BASE + 0x88)	/* Compat alias for int_stm32h5.c */

#define	EXTI_CPUIMR2	(MTK_EXTI_BASE + 0x90)
#define	EXTI_CPUEMR2	(MTK_EXTI_BASE + 0x94)
#define	EXTI_CPUPR2	(MTK_EXTI_BASE + 0x98)

#define	EXTI_CPUIMR3	(MTK_EXTI_BASE + 0xA0)
#define	EXTI_CPUEMR3	(MTK_EXTI_BASE + 0xA4)
#define	EXTI_CPUPR3	(MTK_EXTI_BASE + 0xA8)

/* EXTI configuration registers (for EXTI line → GPIO port mapping) */
#define	EXTI_EXTICR1	(MTK_EXTI_BASE + 0x60)
#define	EXTI_EXTICR2	(MTK_EXTI_BASE + 0x64)
#define	EXTI_EXTICR3	(MTK_EXTI_BASE + 0x68)
#define	EXTI_EXTICR4	(MTK_EXTI_BASE + 0x6C)

/* ------------------------------------------------------------------------ */
/*
 * Coprocessor
 */
#define CPU_HAS_FPU		1
#define CPU_HAS_DSP		1	/* Cortex-M33 has DSP extension */

#if USE_FPU
#define NUM_COPROCESSOR		1
#else
#define NUM_COPROCESSOR		0
#endif

/* ------------------------------------------------------------------------ */
/*
 * RCC (Reset & Clock control)
 *   STM32H5: RCC base = 0x44020C00 (RM0481 Section 2.2)
 */
#define MTK_RCC_BASE		0x44020C00

#define RCC_AHB2ENR	(MTK_RCC_BASE + 0x008C)	/* AHB2 clock enable (GPIO) */
#define RCC_APB1LENR	(MTK_RCC_BASE + 0x009C)	/* APB1 low clock enable (TIM) */
#define RCC_APB2ENR	(MTK_RCC_BASE + 0x00A4)	/* APB2 clock enable (SPI1, USART1) */

/* RCC AHB2ENR bits (GPIO clock enable) */
#define RCC_AHB2ENR_GPIOAEN	(1 << 0)
#define RCC_AHB2ENR_GPIOBEN	(1 << 1)
#define RCC_AHB2ENR_GPIOCEN	(1 << 2)

/* RCC APB2ENR bits */
#define RCC_APB2ENR_SPI1EN	(1 << 12)
#define RCC_APB2ENR_USART1EN	(1 << 14)

/* ------------------------------------------------------------------------ */
/*
 * GPIO
 *   STM32H5: GPIO base = 0x42020000 (RM0481 Section 2.2)
 */
#define	MTK_GPIOA_BASE	0x42020000
#define	MTK_GPIOB_BASE	0x42020400
#define	MTK_GPIOC_BASE	0x42020800
#define	MTK_GPIOD_BASE	0x42020C00
#define	MTK_GPIOE_BASE	0x42021000
#define	MTK_GPIOF_BASE	0x42021400
#define	MTK_GPIOG_BASE	0x42021800
#define	MTK_GPIOH_BASE	0x42021C00

#define GPIO_MODER(n)	(MTK_GPIO##n##_BASE + 0x00)
#define GPIO_OTYPER(n)	(MTK_GPIO##n##_BASE + 0x04)
#define GPIO_OSPEEDR(n)	(MTK_GPIO##n##_BASE + 0x08)
#define GPIO_PUPDR(n)	(MTK_GPIO##n##_BASE + 0x0C)
#define GPIO_IDR(n)	(MTK_GPIO##n##_BASE + 0x10)
#define GPIO_ODR(n)	(MTK_GPIO##n##_BASE + 0x14)
#define GPIO_BSRR(n)	(MTK_GPIO##n##_BASE + 0x18)
#define GPIO_LCKR(n)	(MTK_GPIO##n##_BASE + 0x1C)
#define GPIO_AFRL(n)	(MTK_GPIO##n##_BASE + 0x20)
#define GPIO_AFRH(n)	(MTK_GPIO##n##_BASE + 0x24)

/* ------------------------------------------------------------------------ */
/*
 * SPI1 registers
 *   STM32H5 SPI: 0x40013000 (RM0481 Section 2.2)
 *   STM32H5 uses the new SPI peripheral (different from STM32F4/H7)
 */
#define MTK_SPI1_BASE		0x40013000

#define SPI_CR1		0x00	/* Control register 1 */
#define SPI_CR2		0x04	/* Control register 2 */
#define SPI_CFG1	0x08	/* Configuration register 1 */
#define SPI_CFG2	0x0C	/* Configuration register 2 */
#define SPI_IER		0x10	/* Interrupt enable register */
#define SPI_SR		0x14	/* Status register */
#define SPI_IFCR	0x18	/* Interrupt/status flags clear register */
#define SPI_TXDR	0x20	/* Transmit data register */
#define SPI_RXDR	0x30	/* Receive data register */

/* SPI_CR1 bits */
#define SPI_CR1_SPE	(1 << 0)	/* SPI enable */
#define SPI_CR1_CSTART	(1 << 9)	/* Master transfer start */

/* SPI_CR2 bits */
#define SPI_CR2_TSIZE_MASK	0xFFFF	/* Transfer size (0 = unlimited) */

/* SPI_CFG1 bits */
#define SPI_CFG1_DSIZE_8BIT	(7 << 0)	/* 8-bit data frame */
#define SPI_CFG1_MBR_DIV8	(2 << 28)	/* Baud = fPCLK / 8 */
#define SPI_CFG1_MBR_DIV16	(3 << 28)	/* Baud = fPCLK / 16 */
#define SPI_CFG1_MBR_DIV32	(4 << 28)	/* Baud = fPCLK / 32 */

/* SPI_CFG2 bits */
#define SPI_CFG2_MASTER	(1 << 22)	/* Master mode */
#define SPI_CFG2_SSOE	(1 << 29)	/* SS output enable */
#define SPI_CFG2_SSOM	(1 << 30)	/* SS output management */
#define SPI_CFG2_AFCNTR	(1 << 31)	/* Alternate function GPIOs control */

/* SPI_SR bits */
#define SPI_SR_RXP	(1 << 0)	/* RX packet available */
#define SPI_SR_TXP	(1 << 1)	/* TX packet space available */
#define SPI_SR_EOT	(1 << 3)	/* End of transfer */
#define SPI_SR_TXTF	(1 << 4)	/* TX transfer filled */
#define SPI_SR_OVR	(1 << 6)	/* Overrun */

/* SPI_IFCR bits */
#define SPI_IFCR_EOTC	(1 << 3)	/* Clear EOT flag */
#define SPI_IFCR_TXTFC	(1 << 4)	/* Clear TXTF flag */
#define SPI_IFCR_OVRC	(1 << 6)	/* Clear OVR flag */

/* ------------------------------------------------------------------------ */
/*
 * Physical timer (for STM32H5)
 *   TIM2/TIM3 on APB1 (same base as H7)
 */
#define	CPU_HAS_PTMR	1

#define	MTK_TIM2_BASE	0x40000000
#define	MTK_TIM3_BASE	0x40000400

#define	TIMxCR1		0x00
#define TIMxCR2		0x04
#define TIMxSMCR	0x08
#define TIMxDIER	0x0C
#define TIMxSR		0x10
#define TIMxEGR		0x14
#define TIMxCCMR1	0x18
#define TIMxCCMR2	0x1C
#define TIMxCCER	0x20
#define TIMxCNT		0x24
#define TIMxPSC		0x28
#define TIMxARR		0x2C
#define TIMxCCR1	0x34
#define TIMxCCR2	0x38
#define TIMxCCR3	0x3C
#define TIMxCCR4	0x40

#define	TIMxCR1_CEN	(1<<0)
#define	TIMxCR1_OPM	(1<<3)
#define	TIMxCR1_DIR	(1<<4)
#define	TIMxDIER_UIE	(1<<0)
#define TIMxSR_UIF	(1<<0)
#define TIMxEGR_UG	(1<<0)

#define TIM2PSC_PSC_INIT	0
#define TIM3PSC_PSC_INIT	0

/* Physical timer interrupt number (CMSIS stm32h533xx.h: TIM2_IRQn, TIM3_IRQn) */
#define INTNO_TIM2	45
#define INTNO_TIM3	46

/* Physical timer interrupt priority */
#define INTPRI_TIM2	5
#define INTPRI_TIM3	5

/* Physical timer Maximum count */
#define PTMR_MAX_CNT16    (0x0000FFFF)
#define PTMR_MAX_CNT32    (0xFFFFFFFF)

#endif /* __TK_SYSDEF_DEPEND_CPU_H__ */
