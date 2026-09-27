/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP
 *
 *    GPIO / BSP abstraction layer for STM32H723 (Nucleo-144)
 *----------------------------------------------------------------------
 */

#ifndef __BSP_LIBBSP_DEPEND_H723_H__
#define __BSP_LIBBSP_DEPEND_H723_H__

#include <tk/tkernel.h>
#include <sys/sysdef.h>

/*
 *  GPIO ポート+ピン番号エンコーディング
 *  STM32H7 は GPIOポート(A-K) + ピン(0-15) で指定する
 *  上位8bit = ポート番号 (0=A, 1=B, ...), 下位8bit = ピン番号 (0-15)
 */
#define STM32_PIN(port, pin)	(((UW)(port) << 8) | (pin))
#define STM32_PIN_PORT(p)	(((p) >> 8) & 0xFF)
#define STM32_PIN_NUM(p)	((p) & 0xFF)

/* ポートインデックス */
#define GPIO_PORT_A	0
#define GPIO_PORT_B	1
#define GPIO_PORT_C	2
#define GPIO_PORT_D	3
#define GPIO_PORT_E	4
#define GPIO_PORT_F	5
#define GPIO_PORT_G	6

/* ポートベースアドレスを取得 (GPIOA_BASE=0x58020000, 各 0x400 間隔) */
#define STM32_GPIO_BASE(port)	(GPIOA_BASE + (UW)(port) * 0x400)

/* GPIO レジスタオフセット */
#define GPIO_MODER_OFF	0x00
#define GPIO_OTYPER_OFF	0x04
#define GPIO_OSPEEDR_OFF 0x08
#define GPIO_PUPDR_OFF	0x0C
#define GPIO_IDR_OFF	0x10
#define GPIO_ODR_OFF	0x14
#define GPIO_BSRR_OFF	0x18

/* GPIO モード */
#define GPIO_MODE_IN	0
#define GPIO_MODE_OUT	1
#define GPIO_MODE_AF	2
#define GPIO_MODE_AN	3

/* BSP GPIO API (RP2040 版と同じインターフェース) */
ER	gpio_set_pin(UINT no, UINT mode);	/* GPIO ピンモード設定 */
ER	gpio_set_val(UINT no, UINT val);	/* GPIO 出力値設定 */
UINT	gpio_get_val(UINT no);			/* GPIO 入力値取得 */

/*
 *  LED 制御マクロ
 *  Nucleo H723: PB0=Green, PB14=Red, PE1=Yellow
 */
#define LED_PIN		STM32_PIN(GPIO_PORT_B, 0)	/* Green LED */

#define led_on()	out_w(STM32_GPIO_BASE(GPIO_PORT_B) + GPIO_BSRR_OFF, (1u << 0))
#define led_off()	out_w(STM32_GPIO_BASE(GPIO_PORT_B) + GPIO_BSRR_OFF, (1u << 16))

#endif	/* __BSP_LIBBSP_DEPEND_H723_H__ */
