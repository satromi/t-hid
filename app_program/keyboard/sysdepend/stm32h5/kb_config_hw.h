/*
 * kb_config_hw.h — STM32H533 (NUCLEO-H533RE) hardware definitions
 *
 * NUCLEO-H533RE はキーボードブレインとして動作し、マトリクスを
 * 直接スキャンしない。左右の Pico (I2C スレーブ) からマトリクスを
 * 読み取り、キー処理と USB HID 出力を行う。
 */

#ifndef __KB_CONFIG_HW_H__
#define __KB_CONFIG_HW_H__

/*--------------------------------------------------------------------
 * LED heartbeat
 *   ユーザー LED (LD2 = PA5) は W5500 の SPI1_SCK と共用のため使わない
 *--------------------------------------------------------------------*/
#define KB_LED_ON()	do { } while (0)
#define KB_LED_OFF()	do { } while (0)

/*--------------------------------------------------------------------
 * I2C master (Arduino D15 = PB8 = I2C1_SCL, D14 = PB9 = I2C1_SDA)
 *--------------------------------------------------------------------*/
#define BRAIN_I2C_ADDR_LEFT	0x31	/* 左手 Pico (brain_slave_left) */
#define BRAIN_I2C_ADDR_RIGHT	0x32	/* 右手 Pico (brain_slave_right) */

#define KB_I2C_DEBUG_DUMP()	do { } while (0)

#endif /* __KB_CONFIG_HW_H__ */
