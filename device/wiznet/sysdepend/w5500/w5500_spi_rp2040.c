/*
 *----------------------------------------------------------------------
 *    W5500 SPI HAL — RP2040 (Pico + WIZ550io)
 *
 *    SPI0: GP16=MISO, GP17=CS, GP18=SCLK, GP19=MOSI
 *    RST=GP20, INT=GP21 (GPIO level LOW)
 *
 *    W5100S RP2040 ドライバ (w5100s_spi.c) と同一パターン。
 *    ISR → タスク委譲、FastLock 排他制御。
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

#include "w5500_reg.h"
#include "../../wiznet_drv.h"
#include "../../../include/dev_wiznet.h"
#include "../../tk_socket.h"
#include "w5500_spi_rp2040.h"

#define SPI_BASE	W5500_SPI_BASE

/*----------------------------------------------------------------------
 *  SPI 排他制御
 */
LOCAL FastLock spi_lock;

/*----------------------------------------------------------------------
 *  イベントフラグ
 */
LOCAL ID w5500_irq_flgid = 0;
LOCAL ID w5500_sock_flgid_local[W5500_SOCK_NUM] = {0};

/* wiznet_sock.h 互換 */
ID wiznet_sock_flgid[W5500_SOCK_NUM];

/*----------------------------------------------------------------------
 *  SPI 1byte 送受信 (ポーリング)
 */
LOCAL uint8_t spi_xfer_byte(uint8_t tx)
{
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
LOCAL void wizchip_select(void)   { out_w(GPIO_OUT_CLR, (1u << W5500_PIN_CS)); }
LOCAL void wizchip_deselect(void) { out_w(GPIO_OUT_SET, (1u << W5500_PIN_CS)); }

/*----------------------------------------------------------------------
 *  クリティカルセクション
 */
LOCAL void wizchip_cris_enter(void) { Lock(&spi_lock); }
LOCAL void wizchip_cris_exit(void)  { Unlock(&spi_lock); }

/*----------------------------------------------------------------------
 *  SPI コールバック
 */
LOCAL uint8_t wizchip_spi_readbyte(void)         { return spi_xfer_byte(0xFF); }
LOCAL void wizchip_spi_writebyte(uint8_t wb)     { spi_xfer_byte(wb); }

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
 *  ISR (GP21/INTn, active LOW)
 */
LOCAL void w5500_inthdr(UINT intno)
{
	DisableInt(intno);
	ClearInt(intno);
	if (w5500_irq_flgid > 0) {
		tk_set_flg(w5500_irq_flgid, W5500_EVT_IRQ);
	}
}

/*----------------------------------------------------------------------
 *  W5500 処理タスク
 */
LOCAL void w5500_task(INT stacd, void *exinf)
{
	(void)stacd; (void)exinf;

	while (1) {
		UINT flgptn;
		tk_wai_flg(w5500_irq_flgid, W5500_EVT_IRQ,
			   TWF_ORW | TWF_BITCLR, &flgptn, TMO_FEVR);

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

			setSn_IR(sn, sn_ir);

			if (evt != 0 && w5500_sock_flgid_local[sn] > 0)
				tk_set_flg(w5500_sock_flgid_local[sn], evt);

			if (evt != 0 && select_flgid > 0)
				tk_set_flg(select_flgid, (evt & 0xFF) << (sn * 8));
		}

		EnableInt(W5500_INTNO, W5500_INTPRI);
	}
}

/*----------------------------------------------------------------------
 *  HW リセット (GP20 = RSTn)
 */
EXPORT void wiznet_hw_reset(void)
{
	out_w(GPIO_OUT_CLR, (1u << W5500_PIN_RST));
	tk_dly_tsk(10);
	out_w(GPIO_OUT_SET, (1u << W5500_PIN_RST));
	tk_dly_tsk(100);
}

/*----------------------------------------------------------------------
 *  SPI + タスク + イベントフラグ 初期化
 */
EXPORT void wiznet_spi_init(void)
{
	INT sn;

	CreateLock(&spi_lock, (CONST UB *)"spi0");

	/* SPI0 リセット解除 + 初期化 */
	set_w(RESETS_RESET, RESETS_RESET_SPI0);
	clr_w(RESETS_RESET, RESETS_RESET_SPI0);
	while ((in_w(RESETS_RESET_DONE) & RESETS_RESET_SPI0) == 0) {}

	out_w(SPI_BASE + SSPCR1, 0);
	out_w(SPI_BASE + SSPCR0,
	      SSPCR0_FRF_SPI | SSPCR0_DSS_8BIT |
	      (W5500_SPI_PRESCALE << SSPCR0_SCR_SHIFT));
	out_w(SPI_BASE + SSPCPSR, W5500_SPI_PRESCALE);
	out_w(SPI_BASE + SSPCR1, SSPCR1_SSE);

	/* GPIO を SPI 機能に (SSE=1 の後) */
	out_w(IO_BANK0_BASE + 0x04 + W5500_PIN_MISO*8, 1);
	out_w(IO_BANK0_BASE + 0x04 + W5500_PIN_SCLK*8, 1);
	out_w(IO_BANK0_BASE + 0x04 + W5500_PIN_MOSI*8, 1);

	/* PAD 設定 */
	out_w(PADS_BANK0_BASE + 0x04 + W5500_PIN_MISO*4, (1<<6)|(1<<4)|(1<<1));
	out_w(PADS_BANK0_BASE + 0x04 + W5500_PIN_SCLK*4, (1<<4)|(1<<1));
	out_w(PADS_BANK0_BASE + 0x04 + W5500_PIN_MOSI*4, (1<<4)|(1<<1));

	/* RX FIFO 空にする */
	while (in_w(SPI_BASE + SSPSR) & SSPSR_RNE) {
		(void)in_w(SPI_BASE + SSPDR);
	}

	wizchip_deselect();

	/* コールバック登録 */
	reg_wizchip_cris_cbfunc(wizchip_cris_enter, wizchip_cris_exit);
	reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
	reg_wizchip_spi_cbfunc(wizchip_spi_readbyte, wizchip_spi_writebyte);
	reg_wizchip_spiburst_cbfunc(wizchip_spi_readburst, wizchip_spi_writeburst);

	/* イベントフラグ */
	{
		T_CFLG cflg = { .exinf = NULL, .flgatr = TA_TPRI|TA_WMUL, .iflgptn = 0 };
		w5500_irq_flgid = tk_cre_flg(&cflg);
	}

	for (sn = 0; sn < W5500_SOCK_NUM; sn++) {
		T_CFLG cflg = { .exinf = NULL, .flgatr = TA_TPRI|TA_WMUL, .iflgptn = 0 };
		w5500_sock_flgid_local[sn] = tk_cre_flg(&cflg);
		wiznet_sock_flgid[sn] = w5500_sock_flgid_local[sn];
	}

	/* 処理タスク */
	{
		T_CTSK ctsk = {
			.exinf = NULL, .tskatr = TA_HLNG|TA_RNG3,
			.task = (FP)w5500_task, .itskpri = W5500_TASK_PRI,
			.stksz = W5500_TASK_STKSZ, .bufptr = NULL
		};
		ID tskid = tk_cre_tsk(&ctsk);
		if (tskid > 0) tk_sta_tsk(tskid, 0);
	}
}

/*----------------------------------------------------------------------
 *  GPIO 割り込み登録 (GP21 = INTn)
 */
EXPORT void wiznet_int_init(void)
{
	T_DINT dint;
	dint.intatr = TA_HLNG;
	dint.inthdr = (FP)w5500_inthdr;
	tk_def_int(W5500_INTNO, &dint);

	/* GP21 LEVEL_LOW 割り込み (PROC0_INTE2, bit 20) */
	out_w(IO_BANK0_BASE + 0x100 + 8, (1u << 20));
	EnableInt(W5500_INTNO, W5500_INTPRI);
}

#endif /* CPU_RP2040 */
