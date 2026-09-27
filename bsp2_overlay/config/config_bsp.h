/*
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2024 by Ken Sakamura.
 *    This software is distributed under the T-License 2.1.
 *
 *	config_bsp.h — NUCLEO-H533RE + WIZ550io BSP 設定
 */

#ifndef _MTKBSP_BSP_CONFIG_DEVENV_H_
#define _MTKBSP_BSP_CONFIG_DEVENV_H_

/* 静的メモリ割当 */
#define USE_STATIC_SYS_MEM	(0)
#define SYSTEM_MEM_SIZE		(32*1024)

/* デバッグ用メモリ情報 */
#define USE_DEBUG_SYSMEMINFO	(1)

/* デバイス使用設定 */
#define DEVCNF_USE_HAL_IIC	0
#define DEVCNF_USE_HAL_ADC	0
#define DEVCNF_USE_W5500	1	/* WIZ550io (W5500) Ethernet */

/*
 * W5500 SPI ピン設定 (NUCLEO-H533RE Arduino ヘッダ)
 *   SPI1_SCK  = PA5  (D13)
 *   SPI1_MOSI = PA7  (D11)
 *   SPI1_MISO = PA6  (D12)
 *   CS  = PB6  (D10, GPIO)
 *   INT = PC7  (D9,  GPIO EXTI)
 *   RST = PA9  (D8,  GPIO)
 */
#define W5500_SPI_INSTANCE	SPI1
#define W5500_PIN_CS_PORT	GPIOB
#define W5500_PIN_CS_PIN	6
#define W5500_PIN_INT_PORT	GPIOC
#define W5500_PIN_INT_PIN	7
#define W5500_PIN_RST_PORT	GPIOA
#define W5500_PIN_RST_PIN	9

#endif /* _MTKBSP_BSP_CONFIG_DEVENV_H_ */
