/*
 *----------------------------------------------------------------------
 *    Device Driver for μT-Kernel 3.00.05
 *
 *    Copyright (C) 2020-2021 by Ken Sakamura.
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2021/11.
 *
 *----------------------------------------------------------------------
 */


/*
 *	config_device.h
 *	Device configuration definition
 */

#ifndef	__DEV_CONFIG_H__
#define	__DEV_CONFIG_H__

/* ------------------------------------------------------------------------ */
/* Device usage settings
 *	1: Use   0: Do not use
 */

#define DEVCNF_USE_SER		1		// Serial communication device
#define DEVCNF_USE_ADC		0		// A/D conversion device (disabled for test)
#define DEVCNF_USE_IIC		1		// I2C communication device
#define DEVCNF_USE_USBHID	1		// USB HID Keyboard device
/*
 * WIZnet Ethernet
 *   Pico W には W5100S/W5500 が無いため常に無効。
 *   RP2040 はビルド時の NET=1 (USE_NET) で選択する。W5100S-EVB-Pico は
 *   GP16-21 を SPI/CS/RST/INT に使い、TL Split のマトリクスと重なる。
 */
#if defined(_PICO_W_) || !defined(USE_NET)
#define DEVCNF_USE_WIZNET	0
#else
#define DEVCNF_USE_WIZNET	1
#endif

#endif	/* __DEV_CONFIG_H__ */