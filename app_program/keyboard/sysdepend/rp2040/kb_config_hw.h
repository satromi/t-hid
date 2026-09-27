/*
 * kb_config_hw.h — RP2040 (Raspberry Pi Pico) hardware definitions
 *
 * ターゲット依存のピン割当、LED、I2C アドレスを定義する
 */

#ifndef __KB_CONFIG_HW_H__
#define __KB_CONFIG_HW_H__

/*--------------------------------------------------------------------
 * GPIO Pin assignments
 *   Available: 2-7, 10-22, 26-28
 *   Reserved:  0,1 (UART), 8,9 (I2C), 25 (LED)
 *
 * 左右で割当が異なる。右手モジュールは基板上で左手とは 180 度回転して
 * 実装されるため、同じ GPIO が反対側のピン列に現れる。各ネットが基板上で
 * 到達する順序とピン列の並び順を一致させ、配線の交差を無くしてある。
 *--------------------------------------------------------------------*/
/*
 * 並び順は LAYOUT マクロのマトリクス行と一致させる
 * (左手 = 行 0〜9, 右手 = 行 10〜19)。
 *
 * 基板上の配線順は左右とも「主要キー列を外側から内側へ 7 本 →
 * カーソル/矢印クラスタ → 親指クラスタ → 内側の 2 キー列」であり、
 * マトリクス行の並びとは一致しない。クラスタが配線順では 8 番目
 * なのに対しマトリクス行では左手が行 4、右手が行 15 に入るためで、
 * 配線順のまま並べるとそこから先が 1 つずれる。
 *
 * また右手はマトリクス行が内側から外側へ振られているので、
 * 主要列は配線順と逆順に並ぶ。
 */
/*                row  0   1   2   3   4   5   6   7   8   9 */
#define ROW_PINS_L  { 22, 19, 17, 16, 27, 13, 12, 11, 28, 10 }
#define COL_PINS_L  { 18, 20, 21, 26 }
#define ROW_PINS_R  { 17,  2, 16, 13, 14,  4, 15, 11, 10,  6 }
#define COL_PINS_R  { 12,  7,  5,  3 }

/*--------------------------------------------------------------------
 * LED heartbeat control
 *
 * Pico W: GP25 は CYW43 SPI CS と共有されている。
 *   直接 GPIO 操作すると BLE 通信が壊れる。
 *   オンボード LED は CYW43 GPIO 経由で制御する。
 *   BLE 初期化前は LED 制御不可のため no-op にする。
 *
 * Pico RP2040: GP25 がオンボード LED に直結。
 *--------------------------------------------------------------------*/
#ifdef _PICO_W_
/* Pico W: CYW43 GPIO 経由 (cyw43_arch_gpio_put は BLE 初期化後のみ有効) */
extern void cyw43_arch_gpio_put(int gpio, int value);
#define KB_LED_ON()	cyw43_arch_gpio_put(0, 1)
#define KB_LED_OFF()	cyw43_arch_gpio_put(0, 0)
#else
/* Pico RP2040: GP25 直接制御 */
#define KB_LED_ON()	out_w(GPIO_OUT_SET, (1 << 25))
#define KB_LED_OFF()	out_w(GPIO_OUT_CLR, (1 << 25))
#endif

/*--------------------------------------------------------------------
 * Matrix column init (RP2040 固有: SIO + pad 設定)
 *--------------------------------------------------------------------*/
#define KB_COL_INIT(pin) do { \
	out_w(GPIO_CTRL(pin), GPIO_CTRL_FUNCSEL_SIO); \
	out_w(GPIO_OE_CLR, 1 << (pin)); \
	out_w(GPIO(pin), GPIO_IE | GPIO_PUE | GPIO_SHEMITT | GPIO_DRIVE_4MA); \
} while(0)

/*--------------------------------------------------------------------
 * I2C hardware (for slave mode)
 *--------------------------------------------------------------------*/
#define I2C0_BASE_ADDR       0x40044000
#define INTNO_I2C0           23	/* RP2040 I2C0 IRQ */

/*--------------------------------------------------------------------
 * I2C slave debug dump (RP2040 DW_apb_i2c レジスタ)
 *--------------------------------------------------------------------*/
#define KB_I2C_DEBUG_DUMP() do { \
	UW _base = I2C0_BASE_ADDR; \
	tm_printf((UB *)"I2C0 CON=0x%08x SAR=0x%08x ENABLE=0x%08x INTR_MASK=0x%08x\n", \
		  in_w(_base + 0x00), in_w(_base + 0x08), \
		  in_w(_base + 0x6C), in_w(_base + 0x30)); \
} while(0)

#endif /* __KB_CONFIG_HW_H__ */
