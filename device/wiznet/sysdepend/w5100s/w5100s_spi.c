/*
 *----------------------------------------------------------------------
 *    W5100S Ethernet Device Driver for μT-Kernel 3.0 BSP
 *
 *    RP2040 SPI HAL + W5100S 処理タスク
 *
 *    ISR は GPIO 割り込み検知のみ (SPI アクセスなし, デッドロック回避)
 *    処理タスクがタスクコンテキストで SPI 経由のレジスタ操作を行い、
 *    ソケット別イベントフラグでアプリタスクに通知する
 *
 *    排他制御: FastLock (SPI アクセスのタスク間排他)
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <sys/sysdef.h>

#include "w5100s_reg.h"
#include "../../../include/dev_wiznet.h"
#include "w5100s_spi.h"

/* tk_socket.h: select_flgid 参照 */
#include "../../tk_socket.h"

#define SPI_BASE	W5100S_SPI_BASE

/*----------------------------------------------------------------------
 *  SPI 排他制御 — FastLock (タスク間排他)
 */
LOCAL FastLock spi_lock;

/*----------------------------------------------------------------------
 *  イベントフラグ
 */
ID wiznet_irq_flgid = 0;		/* ISR → 処理タスク */
ID wiznet_sock_flgid[WIZNET_SOCK_NUM] = {0};	/* 処理タスク → アプリ (ソケット別) */

/*----------------------------------------------------------------------
 *  SPI 1byte 送受信 (ポーリング)
 *
 *  意図的なビジーウェイト: SPI FIFO 操作は数十ns で完了する。
 *  tk_dly_tsk() の最小粒度 (1ms) より遥かに短いため RTOS 待機は不可能。
 *  1byte あたり ~32ns @31.25MHz、バースト 64byte でも ~2μs。
 */
LOCAL uint8_t spi_xfer_byte(uint8_t tx)
{
	/*
	 * タイムアウト: 正常時は数十ns で完了するが、
	 * HW 異常時の無限ループを防止する。
	 * 125MHz で 10000 ループ ≒ ~80μs (十分な余裕)。
	 */
	volatile INT timeout;

	timeout = 10000;
	while (!(in_w(SPI_BASE + SSPSR) & SSPSR_TNF)) {
		if (--timeout <= 0) return 0xFF;
	}
	out_w(SPI_BASE + SSPDR, tx);

	timeout = 10000;
	while (!(in_w(SPI_BASE + SSPSR) & SSPSR_RNE)) {
		if (--timeout <= 0) return 0xFF;
	}
	return (uint8_t)(in_w(SPI_BASE + SSPDR) & 0xFF);
}

/*----------------------------------------------------------------------
 *  CS 制御
 */
LOCAL void wizchip_select(void)
{
	out_w(GPIO_OUT_CLR, (1u << W5100S_PIN_CS));
}

LOCAL void wizchip_deselect(void)
{
	out_w(GPIO_OUT_SET, (1u << W5100S_PIN_CS));
}

/*----------------------------------------------------------------------
 *  クリティカルセクション — FastLock でタスク間排他
 */
LOCAL void wizchip_cris_enter(void)
{
	Lock(&spi_lock);
}

LOCAL void wizchip_cris_exit(void)
{
	Unlock(&spi_lock);
}

/*----------------------------------------------------------------------
 *  SPI コールバック (ioLibrary 登録用)
 */
LOCAL uint8_t wizchip_spi_readbyte(void)
{
	return spi_xfer_byte(0xFF);
}

LOCAL void wizchip_spi_writebyte(uint8_t wb)
{
	spi_xfer_byte(wb);
}

LOCAL void wizchip_spi_readburst(uint8_t *pBuf, uint16_t len)
{
	uint16_t i;
	for (i = 0; i < len; i++) {
		pBuf[i] = spi_xfer_byte(0xFF);
	}
}

LOCAL void wizchip_spi_writeburst(uint8_t *pBuf, uint16_t len)
{
	uint16_t i;
	for (i = 0; i < len; i++) {
		spi_xfer_byte(pBuf[i]);
	}
}

/*----------------------------------------------------------------------
 *  W5100S 割り込みハンドラ (GP21/INTn, active LOW, レベルトリガ)
 *
 *  ★ SPI アクセスを一切行わない ★
 *
 *  W5100S の INTn はレベル LOW (アクティブロー) であり、
 *  W5100S 側の IR レジスタをクリアしない限り LOW を維持し続ける。
 *  ISR では SPI アクセスできないため IR クリアは不可能。
 *
 *  対策: ISR で割り込みを無効化 (DisableInt) し、
 *  処理タスクが IR をクリアした後に再有効化 (EnableInt) する。
 *  これにより ISR の連続発火を防止する。
 */
LOCAL void w5100s_inthdr(UINT intno)
{
	/* レベルトリガなので、IR クリア前に再発火しないよう割り込み無効化 */
	DisableInt(intno);

	ClearInt(intno);

	/* 処理タスクに通知 (SPI アクセスなし) */
	if (wiznet_irq_flgid > 0) {
		tk_set_flg(wiznet_irq_flgid, WIZNET_EVT_IRQ);
	}
}

/*----------------------------------------------------------------------
 *  W5100S 処理タスク
 *
 *  ISR からの IRQ 通知を受け、タスクコンテキストで安全に
 *  W5100S レジスタを SPI 経由で読み書きする。
 *  ソケット別の割り込み要因を解析し、各ソケットのイベントフラグに振り分ける。
 *
 *  USB HID ドライバの usb_hid_task() と同じ ISR→タスク委譲パターン。
 */
LOCAL void w5100s_task(INT stacd, void *exinf)
{
	(void)stacd; (void)exinf;

	while (1) {
		UINT flgptn;
		tk_wai_flg(wiznet_irq_flgid, WIZNET_EVT_IRQ,
			   TWF_ORW | TWF_BITCLR, &flgptn, TMO_FEVR);

		/* タスクコンテキストで SPI アクセス (FastLock 安全) */
		UB sir = getIR() & 0x0F;
		INT sn;

		for (sn = 0; sn < WIZNET_SOCK_NUM; sn++) {
			if (!(sir & (1 << sn))) continue;

			UB sn_ir = getSn_IR(sn);
			UINT evt = 0;

			if (sn_ir & Sn_IR_RECV)
				evt |= WIZNET_SOCK_EVT_RECV;
			if (sn_ir & Sn_IR_CON)
				evt |= WIZNET_SOCK_EVT_CON;
			if (sn_ir & Sn_IR_DISCON)
				evt |= WIZNET_SOCK_EVT_DISCON;
			if (sn_ir & Sn_IR_TIMEOUT)
				evt |= WIZNET_SOCK_EVT_TIMEOUT;
			if (sn_ir & Sn_IR_SENDOK)
				evt |= WIZNET_SOCK_EVT_SENDOK;

			/* Sn_IR クリア (W1C) */
			setSn_IR(sn, sn_ir);

			/* ソケット別イベントフラグに通知 */
			if (evt != 0 && wiznet_sock_flgid[sn] > 0) {
				tk_set_flg(wiznet_sock_flgid[sn], evt);
			}

			/* select 用統合フラグにも通知 (B-1) */
			if (evt != 0 && select_flgid > 0) {
				tk_set_flg(select_flgid,
					   (evt & 0xFF) << (sn * 8));
			}
		}

		/* グローバル IR クリア */
		setIR(sir);

		/*
		 * ISR が DisableInt で無効化した割り込みを再有効化。
		 * IR クリア完了後なので、INTn が HIGH に戻っていれば
		 * 再発火しない。まだ未処理イベントがあれば再発火して
		 * 次のループで処理される。
		 */
		EnableInt(W5100S_INTNO, 2);
	}
}

/*----------------------------------------------------------------------
 *  W5100S ハードウェアリセット (GP20 = RSTn, active low)
 */
EXPORT void wiznet_hw_reset(void)
{
	out_w(GPIO_OUT_CLR, (1u << W5100S_PIN_RST));
	tk_dly_tsk(10);	/* RSTn LOW: 10ms (min 500μs) */
	out_w(GPIO_OUT_SET, (1u << W5100S_PIN_RST));
	tk_dly_tsk(100);	/* PLL lock + 内部初期化待ち: 100ms */
}

/*----------------------------------------------------------------------
 *  SPI + イベントフラグ + 処理タスク + 割り込み 初期化
 */
EXPORT void wiznet_spi_init(void)
{
	INT sn;

	/* FastLock 生成 */
	CreateLock(&spi_lock, (CONST UB *)"spi0");

	/*
	 * SPI0 リセット解除 + 初期化 + GPIO ピン設定
	 *
	 * hw_setting.c の module_tbl/pinfnc_tbl では SPI0 のリセット解除と
	 * GP16/18/19 の FUNCSEL=SPI 設定を行わない。
	 * SPI ペリフェラルが未設定 (SSE=0) の状態で FUNCSEL=SPI にすると、
	 * SCLK/MOSI が浮いて W5100S に偽クロックが入り、チップが混乱する。
	 * ここで SSE=1 の直後に FUNCSEL を切り替える。
	 */

	/* SPI0 リセット解除 */
	set_w(RESETS_RESET, RESETS_RESET_SPI0);
	clr_w(RESETS_RESET, RESETS_RESET_SPI0);
	while ((in_w(RESETS_RESET_DONE) & RESETS_RESET_SPI0) == 0) {}

	/* SPI0 ペリフェラル設定 */
	out_w(SPI_BASE + SSPCR1, 0);           /* SSE=0 (disable) */
	out_w(SPI_BASE + SSPCR0,
	      SSPCR0_FRF_SPI | SSPCR0_DSS_8BIT |
	      (W5100S_SPI_POSTDIV << SSPCR0_SCR_SHIFT));
	out_w(SPI_BASE + SSPCPSR, W5100S_SPI_PRESCALE);
	out_w(SPI_BASE + SSPCR1, SSPCR1_SSE);  /* SSE=1 (enable) */

	/* SSE=1 の状態で GPIO を SPI 機能に切替 (SCLK/MOSI が確定的に駆動される) */
	out_w(IO_BANK0_BASE + 0x04 + 16*8, 1); /* GP16 FUNCSEL=SPI */
	out_w(IO_BANK0_BASE + 0x04 + 18*8, 1); /* GP18 FUNCSEL=SPI */
	out_w(IO_BANK0_BASE + 0x04 + 19*8, 1); /* GP19 FUNCSEL=SPI */

	/* PAD 設定 */
	out_w(PADS_BANK0_BASE + 0x04 + 16*4,
	      (1<<6)|(1<<4)|(1<<1));  /* MISO: IE, 4mA, Schmitt */
	out_w(PADS_BANK0_BASE + 0x04 + 18*4,
	      (1<<4)|(1<<1));         /* SCLK: 4mA, Schmitt */
	out_w(PADS_BANK0_BASE + 0x04 + 19*4,
	      (1<<4)|(1<<1));         /* MOSI: 4mA, Schmitt */

	/* RX FIFO 空にする */
	while (in_w(SPI_BASE + SSPSR) & SSPSR_RNE) {
		(void)in_w(SPI_BASE + SSPDR);
	}

	wizchip_deselect();

	/* ioLibrary コールバック登録 */
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
		wiznet_irq_flgid = tk_cre_flg(&cflg);
	}

	/* ソケット別イベントフラグ (4 ソケット分) */
	for (sn = 0; sn < WIZNET_SOCK_NUM; sn++) {
		T_CFLG cflg;
		cflg.exinf   = NULL;
		cflg.flgatr  = TA_TPRI | TA_WMUL;
		cflg.iflgptn = 0;
		wiznet_sock_flgid[sn] = tk_cre_flg(&cflg);
	}

	/* W5100S 処理タスク生成・起動 */
	{
		T_CTSK ctsk;
		ID tskid;

		ctsk.exinf   = NULL;
		ctsk.tskatr  = TA_HLNG | TA_RNG3;
		ctsk.task    = (FP)w5100s_task;
		ctsk.itskpri = W5100S_TASK_PRI;
		ctsk.stksz   = W5100S_TASK_STKSZ;
		ctsk.bufptr  = NULL;
		tskid = tk_cre_tsk(&ctsk);
		if (tskid > 0) tk_sta_tsk(tskid, 0);
	}

}

/*----------------------------------------------------------------------
 *  GPIO 割り込み登録
 *
 *  W5100S ハードウェアリセット後に呼ぶこと。
 *  リセット前に呼ぶと、INTn が LOW のまま残っている場合に
 *  スプリアス割り込みが発生する。
 */
EXPORT void wiznet_int_init(void)
{
	T_DINT dint;
	dint.intatr = TA_HLNG;
	dint.inthdr = (FP)w5100s_inthdr;
	tk_def_int(W5100S_INTNO, &dint);

	/* GP21 LEVEL_LOW 割り込み有効化 (PROC0_INTE2, bit 20) */
	out_w(IO_BANK0_BASE + 0x100 + 8, (1u << 20));
	EnableInt(W5100S_INTNO, 2);
}

#endif /* CPU_RP2040 */
