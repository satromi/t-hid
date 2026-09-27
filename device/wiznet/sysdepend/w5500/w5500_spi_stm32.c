/*
 *----------------------------------------------------------------------
 *    W5500 SPI HAL — STM32H533 (NUCLEO-H533RE + WIZ550io)
 *
 *    SPI1: PA5=SCK(AF5), PA6=MISO(AF5), PA7=MOSI(AF5)
 *    CS=PB6 (GPIO), INT=PC7 (EXTI falling), RST=PA9 (GPIO)
 *
 *    ISR → タスク委譲パターン (W5100S RP2040 ドライバと同一設計)
 *    ISR は SPI アクセスなし、処理タスクがタスクコンテキストで操作
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(MTKBSP_CPU_STM32H5) || defined(MTKBSP_CPU_STM32H533)

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <sys/sysdef.h>

#include "w5500_reg.h"
#include "../../wiznet_drv.h"
#include "../../../include/dev_wiznet.h"

/* tk_socket.h: select_flgid 参照 */
#include "../../tk_socket.h"

/*======================================================================
 * ハードウェア定義 (STM32H533 + WIZ550io)
 *====================================================================*/

/* SPI1 ペリフェラル */
#define SPI_BASE	MTK_SPI1_BASE		/* 0x40013000 */

/* GPIO ベースアドレス */
#define GPIOA_BASE	MTK_GPIOA_BASE		/* 0x42020000 */
#define GPIOB_BASE	MTK_GPIOB_BASE		/* 0x42020400 */
#define GPIOC_BASE	MTK_GPIOC_BASE		/* 0x42020800 */

/* GPIO レジスタオフセット */
#define GPIOx_MODER	0x00
#define GPIOx_OTYPER	0x04
#define GPIOx_OSPEEDR	0x08
#define GPIOx_PUPDR	0x0C
#define GPIOx_IDR	0x10
#define GPIOx_ODR	0x14
#define GPIOx_BSRR	0x18
#define GPIOx_AFRL	0x20
#define GPIOx_AFRH	0x24

/* ピン割り当て */
#define PIN_CS		6	/* PB6 */
#define PIN_INT		7	/* PC7 */
#define PIN_RST		9	/* PA9 */

/* W5500 INTn 割り込み: EXTI7 → STM32H533 EXTI7_IRQn = 18 */
#define W5500_INTNO	18
#define W5500_EXTI_LINE	7

/* ソケット数 */
#define W5500_SOCK_NUM	8

/* タスク設定 */
#define W5500_TASK_PRI		4
#define W5500_TASK_STKSZ	1024

/* イベントフラグ */
#define W5500_EVT_IRQ		(1u << 0)
#define W5500_SOCK_EVT_RECV	(1u << 0)
#define W5500_SOCK_EVT_CON	(1u << 1)
#define W5500_SOCK_EVT_DISCON	(1u << 2)
#define W5500_SOCK_EVT_TIMEOUT	(1u << 3)
#define W5500_SOCK_EVT_SENDOK	(1u << 4)

/*----------------------------------------------------------------------
 *  SPI 排他制御 — FastLock
 */
LOCAL FastLock spi_lock;

/*----------------------------------------------------------------------
 *  イベントフラグ
 */
LOCAL ID w5500_irq_flgid = 0;
LOCAL ID w5500_sock_flgid[W5500_SOCK_NUM] = {0};

/* wiznet_sock.h 互換 (tk_socket.c が参照) */
ID wiznet_sock_flgid[W5500_SOCK_NUM];

/*----------------------------------------------------------------------
 *  GPIO ヘルパー: ビットフィールド書き換え
 */
LOCAL void gpio_set_mode(UW base, INT pin, UW mode)
{
	UW val = in_w(base + GPIOx_MODER);
	val &= ~(3u << (pin * 2));
	val |= (mode << (pin * 2));
	out_w(base + GPIOx_MODER, val);
}

LOCAL void gpio_set_af(UW base, INT pin, UW af)
{
	UW offset = (pin < 8) ? GPIOx_AFRL : GPIOx_AFRH;
	INT shift = (pin % 8) * 4;
	UW val = in_w(base + offset);
	val &= ~(0xFu << shift);
	val |= (af << shift);
	out_w(base + offset, val);
}

LOCAL void gpio_set_speed(UW base, INT pin, UW speed)
{
	UW val = in_w(base + GPIOx_OSPEEDR);
	val &= ~(3u << (pin * 2));
	val |= (speed << (pin * 2));
	out_w(base + GPIOx_OSPEEDR, val);
}

LOCAL void gpio_set_pupd(UW base, INT pin, UW pupd)
{
	UW val = in_w(base + GPIOx_PUPDR);
	val &= ~(3u << (pin * 2));
	val |= (pupd << (pin * 2));
	out_w(base + GPIOx_PUPDR, val);
}

/*----------------------------------------------------------------------
 *  SPI 1byte 送受信 (ポーリング)
 *
 *  STM32H5 の SPI は STM32H7 と同じ新型ペリフェラル。
 *  1byte 転送: CR2.TSIZE=1, CR1.CSTART で開始。
 *  ここでは TSIZE=0 (無制限モード) で SPE/CSTART を維持する方式。
 */
LOCAL uint8_t spi_xfer_byte(uint8_t tx)
{
	volatile INT timeout;

	/* TX パケット空き待ち */
	timeout = 100000;
	while (!(in_w(SPI_BASE + SPI_SR) & SPI_SR_TXP)) {
		if (--timeout <= 0) return 0xFF;
	}
	/* 8bit 書き込み (TXDR の下位 8bit) */
	*((volatile uint8_t *)(SPI_BASE + SPI_TXDR)) = tx;

	/* RX パケット到着待ち */
	timeout = 100000;
	while (!(in_w(SPI_BASE + SPI_SR) & SPI_SR_RXP)) {
		if (--timeout <= 0) return 0xFF;
	}
	return *((volatile uint8_t *)(SPI_BASE + SPI_RXDR));
}

/*----------------------------------------------------------------------
 *  CS 制御 (PB6)
 */
LOCAL void wizchip_select(void)
{
	out_w(GPIOB_BASE + GPIOx_BSRR, (1u << (PIN_CS + 16)));  /* Reset = LOW */
}

LOCAL void wizchip_deselect(void)
{
	out_w(GPIOB_BASE + GPIOx_BSRR, (1u << PIN_CS));  /* Set = HIGH */
}

/*----------------------------------------------------------------------
 *  クリティカルセクション — FastLock
 */
LOCAL void wizchip_cris_enter(void) { Lock(&spi_lock); }
LOCAL void wizchip_cris_exit(void)  { Unlock(&spi_lock); }

/*----------------------------------------------------------------------
 *  SPI コールバック
 */
LOCAL uint8_t wizchip_spi_readbyte(void)  { return spi_xfer_byte(0xFF); }
LOCAL void wizchip_spi_writebyte(uint8_t wb) { spi_xfer_byte(wb); }

LOCAL void wizchip_spi_readburst(uint8_t *pBuf, uint16_t len)
{
	uint16_t i;
	for (i = 0; i < len; i++) pBuf[i] = spi_xfer_byte(0xFF);
}

LOCAL void wizchip_spi_writeburst(uint8_t *pBuf, uint16_t len)
{
	uint16_t i;
	for (i = 0; i < len; i++) spi_xfer_byte(pBuf[i]);
}

/*----------------------------------------------------------------------
 *  W5500 割り込みハンドラ (PC7/INTn, active LOW)
 *
 *  ISR では SPI アクセスを一切行わない (デッドロック回避)。
 *  DisableInt → フラグ通知 → 処理タスクで IR クリア → EnableInt。
 */
LOCAL void w5500_inthdr(UINT intno)
{
	DisableInt(intno);
	/* EXTI の立下り保留フラグは 1 書込みでクリア (しないと再発火し続ける) */
	out_w(EXTI_FPR1, 1u << W5500_EXTI_LINE);
	ClearInt(intno);

	if (w5500_irq_flgid > 0) {
		tk_set_flg(w5500_irq_flgid, W5500_EVT_IRQ);
	}
}

/*----------------------------------------------------------------------
 *  W5500 処理タスク
 *
 *  ISR からの IRQ 通知を受け、タスクコンテキストで SPI レジスタ操作。
 *  W5500 は SIR (Socket Interrupt Register) で全ソケットの割り込み状態を確認し、
 *  各ソケットの Sn_IR を読んでイベントフラグに振り分ける。
 */
LOCAL void w5500_task(INT stacd, void *exinf)
{
	(void)stacd; (void)exinf;

	while (1) {
		UINT flgptn;
		tk_wai_flg(w5500_irq_flgid, W5500_EVT_IRQ,
			   TWF_ORW | TWF_BITCLR, &flgptn, TMO_FEVR);

		/* W5500: SIR で割り込み発生ソケットを確認 */
		UB sir = getIR() & 0xFF;
		INT sn;

		for (sn = 0; sn < W5500_SOCK_NUM; sn++) {
			if (!(sir & (1 << sn))) continue;

			UB sn_ir = getSn_IR(sn);
			UINT evt = 0;

			if (sn_ir & Sn_IR_RECV)    evt |= W5500_SOCK_EVT_RECV;
			if (sn_ir & Sn_IR_CON)     evt |= W5500_SOCK_EVT_CON;
			if (sn_ir & Sn_IR_DISCON)  evt |= W5500_SOCK_EVT_DISCON;
			if (sn_ir & Sn_IR_TIMEOUT) evt |= W5500_SOCK_EVT_TIMEOUT;
			if (sn_ir & Sn_IR_SENDOK)  evt |= W5500_SOCK_EVT_SENDOK;

			/* Sn_IR クリア (W1C) */
			setSn_IR(sn, sn_ir);

			if (evt != 0 && w5500_sock_flgid[sn] > 0) {
				tk_set_flg(w5500_sock_flgid[sn], evt);
			}

			/* select 用統合フラグ */
			if (evt != 0 && select_flgid > 0) {
				tk_set_flg(select_flgid,
					   (evt & 0xFF) << (sn * 8));
			}
		}

		/* 割り込み再有効化 (IR クリア後、INTn が HIGH に戻っていれば再発火しない) */
		EnableInt(W5500_INTNO, 2);

		/*
		 * 処理中に別ソケットのイベントが立つと INTn は LOW のまま
		 * 立下りエッジが来ないため、レベルを見て処理を継続する。
		 */
		if (!(in_w(GPIOC_BASE + GPIOx_IDR) & (1u << PIN_INT))) {
			tk_set_flg(w5500_irq_flgid, W5500_EVT_IRQ);
		}
	}
}

/*----------------------------------------------------------------------
 *  W5500 ハードウェアリセット (PA9 = RSTn, active low)
 */
EXPORT void wiznet_hw_reset(void)
{
	out_w(GPIOA_BASE + GPIOx_BSRR, (1u << (PIN_RST + 16)));  /* RST LOW */
	tk_dly_tsk(10);   /* 10ms (min 500us) */
	out_w(GPIOA_BASE + GPIOx_BSRR, (1u << PIN_RST));         /* RST HIGH */
	tk_dly_tsk(100);  /* PLL lock + 内部初期化待ち */
}

/*----------------------------------------------------------------------
 *  SPI1 + GPIO + イベントフラグ + 処理タスク 初期化
 */
EXPORT void wiznet_spi_init(void)
{
	INT sn;

	/* FastLock 生成 */
	CreateLock(&spi_lock, (CONST UB *)"spi1");

	/*
	 * GPIO クロック有効化 (GPIOA, GPIOB, GPIOC)
	 */
	{
		UW val = in_w(RCC_AHB2ENR);
		val |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
		out_w(RCC_AHB2ENR, val);
		/* クロック安定待ち (ダミーリード) */
		(void)in_w(RCC_AHB2ENR);
	}

	/*
	 * SPI1 クロック有効化 (APB2)
	 */
	{
		UW val = in_w(RCC_APB2ENR);
		val |= RCC_APB2ENR_SPI1EN;
		out_w(RCC_APB2ENR, val);
		(void)in_w(RCC_APB2ENR);
	}

	/*
	 * GPIO ピン設定
	 *   PA5 = SPI1_SCK  (AF5, Very High Speed, No Pull)
	 *   PA6 = SPI1_MISO (AF5, Very High Speed, No Pull)
	 *   PA7 = SPI1_MOSI (AF5, Very High Speed, No Pull)
	 *   PB6 = CS         (GPIO Output, Push-Pull, High)
	 *   PA9 = RST        (GPIO Output, Push-Pull, High)
	 *   PC7 = INT        (GPIO Input, Pull-Up)
	 */

	/* PA5, PA6, PA7 → AF5 (SPI1) */
	gpio_set_af(GPIOA_BASE, 5, 5);    /* PA5 AF5 = SPI1_SCK */
	gpio_set_af(GPIOA_BASE, 6, 5);    /* PA6 AF5 = SPI1_MISO */
	gpio_set_af(GPIOA_BASE, 7, 5);    /* PA7 AF5 = SPI1_MOSI */
	gpio_set_mode(GPIOA_BASE, 5, 2);  /* AF mode */
	gpio_set_mode(GPIOA_BASE, 6, 2);  /* AF mode */
	gpio_set_mode(GPIOA_BASE, 7, 2);  /* AF mode */
	gpio_set_speed(GPIOA_BASE, 5, 3); /* Very High Speed */
	gpio_set_speed(GPIOA_BASE, 6, 3);
	gpio_set_speed(GPIOA_BASE, 7, 3);

	/* PB6 = CS (GPIO Output, initial HIGH) */
	wizchip_deselect();
	gpio_set_mode(GPIOB_BASE, PIN_CS, 1);  /* Output */

	/* PA9 = RST (GPIO Output, initial HIGH) */
	out_w(GPIOA_BASE + GPIOx_BSRR, (1u << PIN_RST));
	gpio_set_mode(GPIOA_BASE, PIN_RST, 1);  /* Output */

	/* PC7 = INT (Input, Pull-Up) */
	gpio_set_mode(GPIOC_BASE, PIN_INT, 0);  /* Input */
	gpio_set_pupd(GPIOC_BASE, PIN_INT, 1);  /* Pull-Up */

	/*
	 * SPI1 設定 (STM32H5 新型 SPI)
	 *   - Master, CPOL=0, CPHA=0 (W5500 Mode 0)
	 *   - 8bit data frame
	 *   - Baud = APB2 / 32 (250MHz/32 = 7.8125MHz, W5500 max 80MHz)
	 *   - Software CS management (AFCNTR=1)
	 */
	out_w(SPI_BASE + SPI_CR1, 0);       /* SPE=0 (disable) */
	/*
	 * CFG1: DSIZE=7 (8bit), MBR=4 (/32), FTHLV=0 (threshold=1 frame)
	 * FTHLV[8:5]=0 はデフォルト値で、1byte 送受信ごとに TXP/RXP が
	 * アサートされる。byte-by-byte ポーリングに最適。
	 */
	out_w(SPI_BASE + SPI_CFG1,
	      SPI_CFG1_DSIZE_8BIT | SPI_CFG1_MBR_DIV32);
	out_w(SPI_BASE + SPI_CFG2,
	      SPI_CFG2_MASTER | SPI_CFG2_AFCNTR);
	out_w(SPI_BASE + SPI_CR2, 0);       /* TSIZE=0 (無制限) */
	out_w(SPI_BASE + SPI_IER, 0);       /* 割り込み無効 */
	out_w(SPI_BASE + SPI_IFCR, 0xFFFF); /* 全フラグクリア */

	/* SPI 有効化 + 転送開始 */
	out_w(SPI_BASE + SPI_CR1, SPI_CR1_SPE);
	{
		UW cr1 = in_w(SPI_BASE + SPI_CR1);
		out_w(SPI_BASE + SPI_CR1, cr1 | SPI_CR1_CSTART);
	}

	/* コールバック登録 */
	reg_wizchip_cris_cbfunc(wizchip_cris_enter, wizchip_cris_exit);
	reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
	reg_wizchip_spi_cbfunc(wizchip_spi_readbyte, wizchip_spi_writebyte);
	reg_wizchip_spiburst_cbfunc(wizchip_spi_readburst, wizchip_spi_writeburst);

	/* IRQ 通知用イベントフラグ */
	{
		T_CFLG cflg;
		cflg.exinf   = NULL;
		cflg.flgatr  = TA_TPRI | TA_WMUL;
		cflg.iflgptn = 0;
		w5500_irq_flgid = tk_cre_flg(&cflg);
	}

	/* ソケット別イベントフラグ */
	for (sn = 0; sn < W5500_SOCK_NUM; sn++) {
		T_CFLG cflg;
		cflg.exinf   = NULL;
		cflg.flgatr  = TA_TPRI | TA_WMUL;
		cflg.iflgptn = 0;
		w5500_sock_flgid[sn] = tk_cre_flg(&cflg);
		wiznet_sock_flgid[sn] = w5500_sock_flgid[sn];
	}

	/* W5500 処理タスク生成・起動 */
	{
		T_CTSK ctsk;
		ID tskid;

		ctsk.exinf   = NULL;
		ctsk.tskatr  = TA_HLNG | TA_RNG3;
		ctsk.task    = (FP)w5500_task;
		ctsk.itskpri = W5500_TASK_PRI;
		ctsk.stksz   = W5500_TASK_STKSZ;
		ctsk.bufptr  = NULL;
		tskid = tk_cre_tsk(&ctsk);
		if (tskid > 0) tk_sta_tsk(tskid, 0);
	}
}

/*----------------------------------------------------------------------
 *  EXTI 割り込み登録 (PC7 = EXTI7, falling edge)
 *
 *  W5500 HW リセット後に呼ぶこと。
 */
EXPORT void wiznet_int_init(void)
{
	T_DINT dint;

	/*
	 * EXTI7 を PC7 に割り当て (EXTICR2[31:24] = 0x02 = GPIOC)
	 * STM32H5: EXTICR2 は EXTI4-7 を 8bit ずつ持ち、EXTI7 は bits[31:24]
	 */
	{
		UW val = in_w(EXTI_EXTICR2);
		val &= ~(0xFFu << 24);   /* EXTI7 フィールドクリア */
		val |= (0x02u << 24);    /* 0x02 = GPIOC */
		out_w(EXTI_EXTICR2, val);
	}

	/* Falling edge trigger (INTn = active LOW) */
	{
		UW val = in_w(EXTI_FTSR1);
		val |= (1u << 7);  /* EXTI line 7 */
		out_w(EXTI_FTSR1, val);
	}

	/* Rising edge 無効 */
	{
		UW val = in_w(EXTI_RTSR1);
		val &= ~(1u << 7);
		out_w(EXTI_RTSR1, val);
	}

	/* 設定中に立った保留フラグを捨ててからマスクを外す */
	out_w(EXTI_FPR1, 1u << W5500_EXTI_LINE);

	/* EXTI 割り込みマスク有効化 */
	{
		UW val = in_w(EXTI_CPUIMR1);
		val |= (1u << 7);
		out_w(EXTI_CPUIMR1, val);
	}

	/* μT-Kernel 割り込みハンドラ登録 */
	dint.intatr = TA_HLNG;
	dint.inthdr = (FP)w5500_inthdr;
	tk_def_int(W5500_INTNO, &dint);

	EnableInt(W5500_INTNO, 2);
}

#endif /* MTKBSP_CPU_STM32H5 || MTKBSP_CPU_STM32H533 */
