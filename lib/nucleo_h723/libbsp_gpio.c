/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP
 *
 *    GPIO abstraction for STM32H723 (Nucleo-144)
 *    RP2040 の gpio_set_pin/gpio_set_val/gpio_get_val と
 *    同じ API をSTM32H7 レジスタ操作で実装する
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef NUCLEO_H723

#include <tk/tkernel.h>
#include <bsp/libbsp.h>

/*
 *  GPIO ピンモード設定
 *  no: STM32_PIN(port, pin) でエンコードされたピン番号
 *  mode: GPIO_MODE_IN(0), GPIO_MODE_OUT(1), GPIO_MODE_AF(2), GPIO_MODE_AN(3)
 */
EXPORT ER gpio_set_pin(UINT no, UINT mode)
{
	UW port = STM32_PIN_PORT(no);
	UW pin  = STM32_PIN_NUM(no);
	UW base = STM32_GPIO_BASE(port);
	UW moder;

	if (pin > 15) return E_PAR;

	/* MODER: 2bit per pin */
	moder = in_w(base + GPIO_MODER_OFF);
	moder &= ~(0x03u << (pin * 2));
	moder |= ((mode & 0x03) << (pin * 2));
	out_w(base + GPIO_MODER_OFF, moder);

	if (mode == GPIO_MODE_OUT) {
		/* Push-pull, high speed */
		UW ospeedr = in_w(base + GPIO_OSPEEDR_OFF);
		ospeedr &= ~(0x03u << (pin * 2));
		ospeedr |= (0x02u << (pin * 2));	/* High speed */
		out_w(base + GPIO_OSPEEDR_OFF, ospeedr);
	} else if (mode == GPIO_MODE_IN) {
		/* Input: pull-up */
		UW pupdr = in_w(base + GPIO_PUPDR_OFF);
		pupdr &= ~(0x03u << (pin * 2));
		pupdr |= (0x01u << (pin * 2));	/* Pull-up */
		out_w(base + GPIO_PUPDR_OFF, pupdr);
	}

	return E_OK;
}

/*
 *  GPIO 出力値設定
 *  BSRR レジスタで atomic set/reset
 */
EXPORT ER gpio_set_val(UINT no, UINT val)
{
	UW port = STM32_PIN_PORT(no);
	UW pin  = STM32_PIN_NUM(no);
	UW base = STM32_GPIO_BASE(port);

	if (val)
		out_w(base + GPIO_BSRR_OFF, (1u << pin));		/* Set */
	else
		out_w(base + GPIO_BSRR_OFF, (1u << (pin + 16)));	/* Reset */

	return E_OK;
}

/*
 *  GPIO 入力値取得
 *  IDR レジスタから読み出し
 */
EXPORT UINT gpio_get_val(UINT no)
{
	UW port = STM32_PIN_PORT(no);
	UW pin  = STM32_PIN_NUM(no);
	UW base = STM32_GPIO_BASE(port);

	return (in_w(base + GPIO_IDR_OFF) >> pin) & 1;
}

#endif /* NUCLEO_H723 */
