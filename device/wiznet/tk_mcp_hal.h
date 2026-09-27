/*
 *----------------------------------------------------------------------
 *    MCP Tools GPIO/ADC/PWM HAL — プラットフォーム抽象化
 *
 *    tk_mcp_tools.c が使用するハードウェア操作をプラットフォーム毎に
 *    インライン関数/マクロで提供する。
 *----------------------------------------------------------------------
 */

#ifndef __TK_MCP_HAL_H__
#define __TK_MCP_HAL_H__

#include <sys/machine.h>
#include <tk/tkernel.h>
#include <tk/syslib.h>

#if defined(CPU_RP2040)
/*======================================================================
 * RP2040 実装 (既存のレジスタ直接操作をそのまま使用)
 *====================================================================*/

/* LED (GP25) */
#define MCP_LED_PIN		25
#define mcp_led_on()		out_w(GPIO_OUT_SET, (1u << MCP_LED_PIN))
#define mcp_led_off()		out_w(GPIO_OUT_CLR, (1u << MCP_LED_PIN))

/* GPIO */
#define mcp_gpio_set_input(pin) \
    do { out_w(IO_BANK0_BASE + 0x04 + (pin) * 8, 5); \
         out_w(GPIO_OE_CLR, (1u << (pin))); } while(0)
#define mcp_gpio_set_output(pin) \
    do { out_w(IO_BANK0_BASE + 0x04 + (pin) * 8, 5); \
         out_w(GPIO_OE_SET, (1u << (pin))); } while(0)
#define mcp_gpio_read(pin)	((in_w(GPIO_IN) >> (pin)) & 1)
#define mcp_gpio_write_high(pin) out_w(GPIO_OUT_SET, (1u << (pin)))
#define mcp_gpio_write_low(pin)  out_w(GPIO_OUT_CLR, (1u << (pin)))

/* ADC */
#define MCP_HAS_ADC		1
#define MCP_ADC_TEMP_CH		4
static inline UW mcp_adc_read_raw(INT ch) {
    UW cs = (1u << 0) | ((UW)ch << 12) | (1u << 1);
    out_w(ADC_CS, cs);
    out_w(ADC_CS, in_w(ADC_CS) | (1u << 2));
    while (!(in_w(ADC_CS) & (1u << 8))) {}
    return in_w(ADC_RESULT) & 0xFFF;
}

/* PWM */
#define MCP_HAS_PWM		1
#define MCP_PWM_BASE		PWM_BASE
static inline void mcp_pwm_set_funcsel(INT pin) {
    out_w(IO_BANK0_BASE + 0x04 + (pin) * 8, 4);
    out_w(GPIO_OE_SET, (1u << (pin)));
}

/* Memory peek/poke — RP2040 は全アドレス空間にアクセス可能 */
#define MCP_HAS_PEEK		1
#define MCP_HAS_WATCHDOG	1
#define MCP_HAS_FLASH		1

#elif defined(MTKBSP_CPU_STM32H5)
/*======================================================================
 * STM32H5 実装 (NUCLEO-H533RE)
 *====================================================================*/
#include <sys/sysdef.h>

/*
 * GPIO ヘルパー — STM32 はポート + ピンでアクセス
 * MCP ツールは RP2040 互換のフラットピン番号 (0-29) を使う。
 * STM32 では pin 0-15 = GPIOA, 16-31 = GPIOB として簡易マッピング。
 */
#define _STM32_GPIO_BASE(pin) \
    (((pin) < 16) ? MTK_GPIOA_BASE : MTK_GPIOB_BASE)
#define _STM32_GPIO_BIT(pin)  ((pin) & 0x0F)

static inline void mcp_gpio_set_input(INT pin) {
    UW base = _STM32_GPIO_BASE(pin);
    INT bit = _STM32_GPIO_BIT(pin);
    UW val = in_w(base + 0x00);  /* MODER */
    val &= ~(3u << (bit * 2));   /* 00 = Input */
    out_w(base + 0x00, val);
}

static inline void mcp_gpio_set_output(INT pin) {
    UW base = _STM32_GPIO_BASE(pin);
    INT bit = _STM32_GPIO_BIT(pin);
    UW val = in_w(base + 0x00);  /* MODER */
    val &= ~(3u << (bit * 2));
    val |= (1u << (bit * 2));    /* 01 = Output */
    out_w(base + 0x00, val);
}

static inline UW mcp_gpio_read(INT pin) {
    UW base = _STM32_GPIO_BASE(pin);
    INT bit = _STM32_GPIO_BIT(pin);
    return (in_w(base + 0x10) >> bit) & 1;  /* IDR */
}

static inline void mcp_gpio_write_high(INT pin) {
    UW base = _STM32_GPIO_BASE(pin);
    INT bit = _STM32_GPIO_BIT(pin);
    out_w(base + 0x18, (1u << bit));  /* BSRR set */
}

static inline void mcp_gpio_write_low(INT pin) {
    UW base = _STM32_GPIO_BASE(pin);
    INT bit = _STM32_GPIO_BIT(pin);
    out_w(base + 0x18, (1u << (bit + 16)));  /* BSRR reset */
}

/* LED — PA5 は SPI 使用中。LD3 (PB0 = red) を代用 */
#define MCP_LED_PIN		16  /* PB0 = flat pin 16 */
#define mcp_led_on()		mcp_gpio_write_high(MCP_LED_PIN)
#define mcp_led_off()		mcp_gpio_write_low(MCP_LED_PIN)

/*----------------------------------------------------------------------
 * ADC — STM32H533 ADC1 (RM0481 Section 25)
 *   ADC1 base = 0x42028000
 *   ADC_CCR (common) = ADC1_BASE + 0x308
 *   温度センサ = VSENSE (ch18), TSEN bit で有効化
 *   RCC_AHB2ENR bit 10 = ADC enable
 */
#define MCP_HAS_ADC		1
#define MCP_ADC_TEMP_CH		18

#define STM32_ADC1_BASE		0x42028000UL
#define STM32_ADC_ISR		(STM32_ADC1_BASE + 0x00)
#define STM32_ADC_CR		(STM32_ADC1_BASE + 0x08)
#define STM32_ADC_CFGR		(STM32_ADC1_BASE + 0x0C)
#define STM32_ADC_SMPR1		(STM32_ADC1_BASE + 0x14)
#define STM32_ADC_SMPR2		(STM32_ADC1_BASE + 0x18)
#define STM32_ADC_SQR1		(STM32_ADC1_BASE + 0x30)
#define STM32_ADC_DR		(STM32_ADC1_BASE + 0x40)
#define STM32_ADC_CCR		(STM32_ADC1_BASE + 0x308)

#define RCC_AHB2ENR_ADCEN	(1 << 10)

/* ADC 初期化 (usermain.c から 1 回だけ呼ぶ) */
static inline void mcp_adc_init(void) {
    /* RCC: ADC クロック有効化 */
    out_w(RCC_AHB2ENR, in_w(RCC_AHB2ENR) | RCC_AHB2ENR_ADCEN);
    (void)in_w(RCC_AHB2ENR);

    /* Deep powerdown 解除 */
    out_w(STM32_ADC_CR, in_w(STM32_ADC_CR) & ~(1u << 29));  /* DEEPPWD=0 */

    /* 電圧レギュレータ有効化 */
    out_w(STM32_ADC_CR, in_w(STM32_ADC_CR) | (1u << 28));   /* ADVREGEN=1 */
    { volatile INT i; for (i = 0; i < 10000; i++) {} }       /* ~10μs 待ち */

    /* キャリブレーション */
    out_w(STM32_ADC_CR, in_w(STM32_ADC_CR) | (1u << 31));   /* ADCAL=1 */
    while (in_w(STM32_ADC_CR) & (1u << 31)) {}               /* 完了待ち */

    /* 解像度 12bit, 右寄せ */
    out_w(STM32_ADC_CFGR, (in_w(STM32_ADC_CFGR) & ~(3u << 3)) | (2u << 3));

    /* サンプリング時間: 全チャネル 64.5 サイクル */
    out_w(STM32_ADC_SMPR1, 0x15B6DB6D);  /* SMP[9:0] = 5 (64.5 cycles) */
    out_w(STM32_ADC_SMPR2, 0x15B6DB6D);  /* SMP[18:10] */

    /* 温度センサ + VREF 有効化 */
    out_w(STM32_ADC_CCR, in_w(STM32_ADC_CCR) | (1u << 23) | (1u << 22));

    /* ADC 有効化 */
    out_w(STM32_ADC_CR, in_w(STM32_ADC_CR) | (1u << 0));    /* ADEN=1 */
    while (!(in_w(STM32_ADC_ISR) & (1u << 0))) {}            /* ADRDY 待ち */
}

static inline UW mcp_adc_read_raw(INT ch) {
    out_w(STM32_ADC_SQR1, ((UW)ch << 6));  /* L=0, SQ1=ch */
    out_w(STM32_ADC_CR, in_w(STM32_ADC_CR) | (1u << 2));  /* ADSTART */
    while (!(in_w(STM32_ADC_ISR) & (1u << 2))) {}          /* EOC 待ち */
    return in_w(STM32_ADC_DR) & 0xFFF;
}

/*----------------------------------------------------------------------
 * PWM — TIM2 (APB1, 250MHz)
 *   TIM2_CH1 = PA0 (AF1, Arduino A0)
 *   TIM2_CH2 = PA1 (AF1, Arduino A1)
 *   TIM3_CH3 = PB0 (AF2, Arduino D3) ※LED と共用に注意
 */
#define MCP_HAS_PWM		1
#define MCP_PWM_BASE		0x40000000UL  /* TIM2 */

/* ピンの AF 設定 (PWM 出力用) */
static inline void mcp_pwm_set_funcsel(INT pin) {
    UW base = _STM32_GPIO_BASE(pin);
    INT bit = _STM32_GPIO_BIT(pin);
    UW af;

    /* ピンごとに AF を選択 (TIM2=AF1, TIM3=AF2) */
    if (pin < 4) af = 1;        /* PA0-PA3: TIM2 AF1 */
    else if (pin >= 16 && pin <= 17) af = 2;  /* PB0-PB1: TIM3 AF2 */
    else return;  /* PWM 非対応ピン */

    /* GPIO を AF モードに設定 */
    {
        UW val = in_w(base + 0x00);  /* MODER */
        val &= ~(3u << (bit * 2));
        val |= (2u << (bit * 2));    /* 10 = Alternate Function */
        out_w(base + 0x00, val);
    }
    /* AF レジスタ設定 */
    {
        UW offset = (bit < 8) ? 0x20 : 0x24;  /* AFRL or AFRH */
        INT shift = (bit % 8) * 4;
        UW val = in_w(base + offset);
        val &= ~(0xFu << shift);
        val |= (af << shift);
        out_w(base + offset, val);
    }
}

/*----------------------------------------------------------------------
 * IWDG (Independent Watchdog, RM0481 Section 38)
 *   IWDG base = 0x40003000
 *   LSI ≒ 32kHz
 */
#define MCP_HAS_WATCHDOG	1

#define STM32_IWDG_BASE		0x40003000UL
#define STM32_IWDG_KR		(STM32_IWDG_BASE + 0x00)
#define STM32_IWDG_PR		(STM32_IWDG_BASE + 0x04)
#define STM32_IWDG_RLR		(STM32_IWDG_BASE + 0x08)
#define STM32_IWDG_SR		(STM32_IWDG_BASE + 0x0C)

/*----------------------------------------------------------------------
 * Flash (RM0481 Section 7)
 *   Flash base = 0x40022000
 *   セクタサイズ = 8KB, 64 セクタ (512KB)
 *   プログラム単位 = 128bit (16 bytes, quadword)
 */
#define MCP_HAS_FLASH		1

/* 消去・書き込みは device/flash/stm32h5_flash.c が行う */

/* peek/poke */
#define MCP_HAS_PEEK		1

#else
#error "Unsupported platform for MCP HAL"
#endif

#endif /* __TK_MCP_HAL_H__ */
