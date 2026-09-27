/*
 * kb_config_hw.h — STM32H723 (Nucleo-144) hardware definitions
 *
 * ターゲット依存のピン割当、LED、I2C アドレスを定義する
 */

#ifndef __KB_CONFIG_HW_H__
#define __KB_CONFIG_HW_H__

#include <bsp/libbsp.h>

/*--------------------------------------------------------------------
 * GPIO Pin assignments
 *   Nucleo-144 CN7/CN10 Arduino headers
 *   ROW: PD0-PD7, PE2, PE3 (10 pins)
 *   COL: PE4, PE5, PE6, PF0 (4 pins)
 *   Reserved: PA11,PA12 (USB), PB8,PB9 (I2C), PB0 (LED)
 *--------------------------------------------------------------------*/
#define ROW_PINS_L  { STM32_PIN(GPIO_PORT_D,0), STM32_PIN(GPIO_PORT_D,1), \
                      STM32_PIN(GPIO_PORT_D,2), STM32_PIN(GPIO_PORT_D,3), \
                      STM32_PIN(GPIO_PORT_D,4), STM32_PIN(GPIO_PORT_D,5), \
                      STM32_PIN(GPIO_PORT_D,6), STM32_PIN(GPIO_PORT_D,7), \
                      STM32_PIN(GPIO_PORT_E,2), STM32_PIN(GPIO_PORT_E,3) }
#define COL_PINS_L  { STM32_PIN(GPIO_PORT_E,4), STM32_PIN(GPIO_PORT_E,5), \
                      STM32_PIN(GPIO_PORT_E,6), STM32_PIN(GPIO_PORT_F,0) }
#define ROW_PINS_R  ROW_PINS_L
#define COL_PINS_R  COL_PINS_L

/*--------------------------------------------------------------------
 * LED heartbeat control (PB0 = Nucleo Green LED)
 *--------------------------------------------------------------------*/
#define KB_LED_ON()	led_on()
#define KB_LED_OFF()	led_off()

/*--------------------------------------------------------------------
 * Matrix column init (STM32H7: gpio_set_pin で input + pull-up)
 *--------------------------------------------------------------------*/
#define KB_COL_INIT(pin)	gpio_set_pin((pin), GPIO_MODE_IN)

/*--------------------------------------------------------------------
 * I2C hardware (for slave mode)
 *--------------------------------------------------------------------*/
#define I2C0_BASE_ADDR       0x40005400	/* I2C1 base (STM32H7) */
#define INTNO_I2C0           31	/* I2C1_EV_IRQn (STM32H723) */

/*--------------------------------------------------------------------
 * I2C slave debug dump (STM32H7 I2C レジスタ)
 *--------------------------------------------------------------------*/
#define KB_I2C_DEBUG_DUMP() do { \
	UW _base = I2C0_BASE_ADDR; \
	tm_printf((UB *)"I2C1 CR1=0x%08x OAR1=0x%08x ISR=0x%08x\n", \
		  in_w(_base + 0x00), in_w(_base + 0x08), \
		  in_w(_base + 0x18)); \
} while(0)

#endif /* __KB_CONFIG_HW_H__ */
