/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    STM32H7 USB OTG FS デバイス HAL + ドライバブリッジ
 *    DWC2 core (Device mode, Full-Speed)
 *
 *    ISR は HW ステータスクリア + FIFO コピーのみ行い、
 *    プロトコル処理はイベントフラグ経由でタスクに委譲する
 *
 *    STM32H723 Reference Manual RM0468 Section 61
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_STM32H7

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <string.h>
#include "../../usb_hid.h"

/*
 *  STM32H7 にはアトミック set/clear レジスタがないため
 *  read-modify-write マクロを定義
 */
#define set_w(addr, bits)	out_w((addr), in_w((addr)) | (bits))
#define clr_w(addr, bits)	out_w((addr), in_w((addr)) & ~(bits))

/*
 *  タイムアウト
 */
#define USB_RESET_WAIT_MAX	(100 * 100)

/*
 *  デバイス制御ブロック (シングルポート)
 */
LOCAL T_USB_LL_CB ll_devcb;

/*----------------------------------------------------------------------
 *  FIFO ヘルパー: 32bit 単位で FIFO に書き込む
 */
LOCAL void ll_write_fifo(UW fifo_addr, const UB *src, UW len)
{
	UW words = (len + 3) / 4;
	UW i;
	const UW *s32 = (const UW *)src;
	volatile UW *fifo = (volatile UW *)fifo_addr;

	for (i = 0; i < words; i++) {
		*fifo = s32[i];
	}
}

/*----------------------------------------------------------------------
 *  FIFO ヘルパー: 32bit 単位で FIFO から読み出す
 */
LOCAL void ll_read_fifo(UB *dst, UW fifo_addr, UW len)
{
	UW words = (len + 3) / 4;
	UW i;
	UW *d32 = (UW *)dst;
	volatile UW *fifo = (volatile UW *)fifo_addr;

	for (i = 0; i < words; i++) {
		d32[i] = *fifo;
	}
}

/*----------------------------------------------------------------------
 *  FIFO ヘルパー: 読み捨て (不要なデータをフラッシュ)
 */
LOCAL void ll_flush_rx_fifo(UW fifo_addr, UW len)
{
	UW words = (len + 3) / 4;
	UW i;
	volatile UW *fifo = (volatile UW *)fifo_addr;

	for (i = 0; i < words; i++) {
		(void)*fifo;
	}
}

/*----------------------------------------------------------------------
 *  USB デバイス HAL 初期化
 *  return: 制御ブロックポインタ (NULL = エラー)
 */
EXPORT T_USB_LL_CB * dev_usb_hid_llinit(T_USB_INIT *p_init)
{
	T_USB_LL_CB *p_cb;
	INT tick;

	if (p_init == NULL) return NULL;

	p_cb = &ll_devcb;
	memset(p_cb, 0, sizeof(T_USB_LL_CB));
	p_cb->base = USB_OTG_FS_BASE;
	memcpy(&p_cb->init, p_init, sizeof(T_USB_INIT));

	/*
	 *  USB OTG FS クロック有効化 (RCC_AHB1ENR)
	 */
	set_w(RCC_AHB1ENR, RCC_AHB1ENR_USB1OTGHSEN);
	{
		volatile INT i;
		for (i = 0; i < 100; i++) {}	/* クロック安定待ち */
	}

	/*
	 *  コアソフトリセット
	 */
	/* AHB マスターアイドル待ち */
	tick = USB_RESET_WAIT_MAX;
	while ((in_w(OTG_GRSTCTL) & OTG_GRSTCTL_AHBIDL) == 0) {
		volatile INT i;
		for (i = 0; i < 100; i++) {}
		if (--tick <= 0) return NULL;
	}
	/* コアリセット */
	set_w(OTG_GRSTCTL, OTG_GRSTCTL_CSRST);
	tick = USB_RESET_WAIT_MAX;
	while ((in_w(OTG_GRSTCTL) & OTG_GRSTCTL_CSRST) != 0) {
		volatile INT i;
		for (i = 0; i < 100; i++) {}
		if (--tick <= 0) return NULL;
	}
	/* リセット後安定待ち */
	{
		volatile INT i;
		for (i = 0; i < 1000; i++) {}
	}

	/*
	 *  デバイスモード設定 (read-modify-write で既存フィールドを保持)
	 */
	{
		UW gusbcfg = in_w(OTG_GUSBCFG);
		gusbcfg |= OTG_GUSBCFG_PHYSEL;		/* 内蔵 FS PHY 選択 */
		gusbcfg |= OTG_GUSBCFG_FDMOD;		/* デバイスモード強制 */
		gusbcfg &= ~OTG_GUSBCFG_TRDT_MASK;
		gusbcfg |= OTG_GUSBCFG_TRDT(6);	/* TRDT=6 (AHB≥32MHz) */
		out_w(OTG_GUSBCFG, gusbcfg);
	}
	{
		volatile INT i;
		for (i = 0; i < 10000; i++) {}	/* モード切替安定待ち (~25ms) */
	}

	/*
	 *  デバイススピード設定 (Full-Speed)
	 */
	out_w(OTG_DCFG, (in_w(OTG_DCFG) & ~OTG_DCFG_DSPD_MASK) | OTG_DCFG_DSPD_FS);

	/*
	 *  FIFO サイズ設定
	 *    RX FIFO:  128 words (512 bytes) — 全 EP 共有
	 *    TX0 FIFO: 32 words (128 bytes) — EP0 IN
	 *    TX1 FIFO: 32 words (128 bytes) — EP1 IN
	 */
	out_w(OTG_GRXFSIZ, USB_OTG_RX_FIFO_SZ);
	out_w(OTG_DIEPTXF0,
	      ((UW)USB_OTG_TX0_FIFO_SZ << 16) | USB_OTG_RX_FIFO_SZ);
	out_w(OTG_DIEPTXF(1),
	      ((UW)USB_OTG_TX1_FIFO_SZ << 16) |
	      (USB_OTG_RX_FIFO_SZ + USB_OTG_TX0_FIFO_SZ));

	/*
	 *  全 TX FIFO フラッシュ
	 */
	out_w(OTG_GRSTCTL, OTG_GRSTCTL_TXFFLSH | OTG_GRSTCTL_TXFNUM(0x10));
	tick = USB_RESET_WAIT_MAX;
	while ((in_w(OTG_GRSTCTL) & OTG_GRSTCTL_TXFFLSH) != 0) {
		if (--tick <= 0) break;
	}

	/*
	 *  RX FIFO フラッシュ
	 */
	out_w(OTG_GRSTCTL, OTG_GRSTCTL_RXFFLSH);
	tick = USB_RESET_WAIT_MAX;
	while ((in_w(OTG_GRSTCTL) & OTG_GRSTCTL_RXFFLSH) != 0) {
		if (--tick <= 0) break;
	}

	/*
	 *  全割り込みクリア
	 */
	out_w(OTG_GINTSTS, 0xFFFFFFFF);

	/*
	 *  VBUS 検出バイパス (Nucleo は VBUS 検出ピン未接続)
	 *  STM32 HAL (stm32h7xx_ll_usb.c USB_DevInit) と同じ手順:
	 *  1. GCCFG.VBDEN クリア (HW VBUS 検出無効)
	 *  2. GCCFG.PWRDWN セット (PHY パワーダウン解除)
	 *  3. GOTGCTL.BVALOEN セット (B-session override 有効)
	 *  4. GOTGCTL.BVALOVAL セット (B-session valid = 1)
	 */
	out_w(OTG_GCCFG, OTG_GCCFG_PWRDWN);	/* PWRDWN=1, VBDEN=0 */
	set_w(OTG_GOTGCTL, OTG_GOTGCTL_BVALOEN | OTG_GOTGCTL_BVALOVAL);

	/*
	 *  グローバル割り込みマスク設定
	 */
	{
		UW imask = OTG_GINTSTS_USBRST | OTG_GINTSTS_ENUMDNE |
			   OTG_GINTSTS_RXFLVL | OTG_GINTSTS_IEPINT |
			   OTG_GINTSTS_OEPINT | OTG_GINTSTS_USBSUSP |
			   OTG_GINTSTS_WKUINT;
		if (p_init->sof_enable)
			imask |= OTG_GINTSTS_SOF;
		out_w(OTG_GINTMSK, imask);
	}

	/*
	 *  デバイス EP 割り込みマスク設定
	 */
	out_w(OTG_DIEPMSK, OTG_DIEPINT_XFRC);		/* IN: 転送完了 */
	out_w(OTG_DOEPMSK, OTG_DOEPINT_XFRC | OTG_DOEPINT_STUP);  /* OUT: 転送完了 + SETUP */

	/* EP0 IN + OUT, EP1 IN の割り込みを有効化 */
	out_w(OTG_DAINTMSK, OTG_DAINTMSK_IEPM(0) | OTG_DAINTMSK_OEPM(0) |
			    OTG_DAINTMSK_IEPM(1));

	/*
	 *  グローバル割り込み有効化
	 */
	out_w(OTG_GAHBCFG, OTG_GAHBCFG_GINTMSK | OTG_GAHBCFG_TXFELVL);

	return p_cb;
}

/*----------------------------------------------------------------------
 *  USB デバイス HAL 終了
 */
EXPORT ER dev_usb_hid_lldeinit(T_USB_LL_CB *p_cb)
{
	if (p_cb == NULL) return E_PAR;

	/* グローバル割り込み無効 */
	out_w(OTG_GAHBCFG, 0);
	/* ソフトディスコネクト */
	set_w(OTG_DCTL, OTG_DCTL_SDIS);
	/* USB OTG FS クロック無効化 */
	clr_w(RCC_AHB1ENR, RCC_AHB1ENR_USB1OTGHSEN);

	return E_OK;
}

/*----------------------------------------------------------------------
 *  D+ プルアップ有効 (デバイス接続)
 *  DWC2: DCTL.SDIS をクリアすると D+ プルアップが有効になる
 */
EXPORT ER dev_usb_hid_connect(T_USB_LL_CB *p_cb)
{
	if (p_cb == NULL) return E_PAR;
	clr_w(OTG_DCTL, OTG_DCTL_SDIS);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  デバイス切断
 */
EXPORT ER dev_usb_hid_disconnect(T_USB_LL_CB *p_cb)
{
	if (p_cb == NULL) return E_PAR;
	set_w(OTG_DCTL, OTG_DCTL_SDIS);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  USB アドレス設定
 *  DWC2: DCFG.DAD フィールドに書き込む
 *  SET_ADDRESS は STATUS フェーズ完了後にラッチする必要がある
 */
EXPORT ER dev_usb_hid_set_addr(T_USB_LL_CB *p_cb, UB address)
{
	if (p_cb == NULL) return E_PAR;
	if (address == 0) {
		out_w(OTG_DCFG, (in_w(OTG_DCFG) & ~OTG_DCFG_DAD_MASK));
	} else {
		/* STATUS IN 完了後にラッチするため保留 */
		p_cb->dev_addr = address;
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  エンドポイント有効化
 */
EXPORT ER dev_usb_hid_ep_activate(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UW ctl;

	if (p_cb == NULL || ep == NULL) return E_PAR;

	if (ep->num == 0) {
		/* EP0 は常に有効 — max packet size のみ設定 */
		if (ep->is_in) {
			ctl = in_w(OTG_DIEPCTL(0));
			ctl &= ~OTG_DIEPCTL_MPSIZ_MASK;
			/* EP0 MPS: 0=64, 1=32, 2=16, 3=8 */
			out_w(OTG_DIEPCTL(0), ctl);
		} else {
			/* EP0 OUT: SETUP 受信準備 */
			out_w(OTG_DOEPTSIZ(0),
			      OTG_DOEPTSIZ_STUPCNT(3) |
			      OTG_DOEPTSIZ_PKTCNT(1) |
			      (ep->max_pkt_size & OTG_DOEPTSIZ_XFRSIZ_MASK));
			out_w(OTG_DOEPCTL(0),
			      in_w(OTG_DOEPCTL(0)) | OTG_DOEPCTL_EPENA | OTG_DOEPCTL_CNAK);
		}
	} else {
		/* EPn IN/OUT */
		if (ep->is_in) {
			ctl = OTG_DIEPCTL_USBAEP |
			      OTG_DIEPCTL_EPTYP(ep->type) |
			      OTG_DIEPCTL_TXFNUM(ep->num) |
			      OTG_DIEPCTL_SD0PID |
			      (ep->max_pkt_size & OTG_DIEPCTL_MPSIZ_MASK);
			out_w(OTG_DIEPCTL(ep->num), ctl);
		} else {
			ctl = OTG_DOEPCTL_USBAEP |
			      OTG_DOEPCTL_EPTYP(ep->type) |
			      OTG_DOEPCTL_SD0PID |
			      (ep->max_pkt_size & OTG_DOEPCTL_MPSIZ_MASK);
			out_w(OTG_DOEPCTL(ep->num), ctl);
		}
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  エンドポイント無効化
 */
EXPORT ER dev_usb_hid_ep_deactivate(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	if (p_cb == NULL || ep == NULL) return E_PAR;

	if (ep->is_in) {
		if (in_w(OTG_DIEPCTL(ep->num)) & OTG_DIEPCTL_EPENA) {
			set_w(OTG_DIEPCTL(ep->num), OTG_DIEPCTL_EPDIS | OTG_DIEPCTL_SNAK);
		}
		out_w(OTG_DIEPCTL(ep->num), 0);
	} else {
		if (in_w(OTG_DOEPCTL(ep->num)) & OTG_DOEPCTL_EPENA) {
			set_w(OTG_DOEPCTL(ep->num), OTG_DOEPCTL_EPDIS | OTG_DOEPCTL_SNAK);
		}
		out_w(OTG_DOEPCTL(ep->num), 0);
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP 受信開始 (OUT endpoint)
 */
EXPORT ER dev_usb_hid_ep_recv(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UW pktcnt, xfrsiz;

	if (p_cb == NULL || ep == NULL) return E_PAR;

	if (ep->xfer_remain == 0) {
		pktcnt = 1;
		xfrsiz = ep->max_pkt_size;
	} else {
		pktcnt = (ep->xfer_remain + ep->max_pkt_size - 1) / ep->max_pkt_size;
		xfrsiz = ep->xfer_remain;
	}

	if (ep->num == 0) {
		/* EP0 OUT: 1 パケットのみ */
		if (xfrsiz > ep->max_pkt_size)
			xfrsiz = ep->max_pkt_size;
		pktcnt = 1;
		out_w(OTG_DOEPTSIZ(0),
		      OTG_DOEPTSIZ_STUPCNT(3) |
		      OTG_DOEPTSIZ_PKTCNT(pktcnt) |
		      (xfrsiz & OTG_DOEPTSIZ_XFRSIZ_MASK));
	} else {
		out_w(OTG_DOEPTSIZ(ep->num),
		      OTG_DOEPTSIZ_PKTCNT(pktcnt) |
		      (xfrsiz & OTG_DOEPTSIZ_XFRSIZ_MASK));
	}

	/* EP 有効化 + NAK クリア */
	set_w(OTG_DOEPCTL(ep->num), OTG_DOEPCTL_EPENA | OTG_DOEPCTL_CNAK);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP 送信開始 (IN endpoint)
 */
EXPORT ER dev_usb_hid_ep_send(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UW pktcnt, xfrsiz;

	if (p_cb == NULL || ep == NULL) return E_PAR;

	if (ep->xfer_remain > ep->max_pkt_size) {
		ep->xfer_req = ep->max_pkt_size;
		ep->xfer_remain -= ep->xfer_req;
	} else {
		ep->xfer_req = ep->xfer_remain;
		ep->xfer_remain = 0;
	}

	xfrsiz = ep->xfer_req;
	pktcnt = (xfrsiz == 0) ? 1 : ((xfrsiz + ep->max_pkt_size - 1) / ep->max_pkt_size);

	if (ep->num == 0) {
		out_w(OTG_DIEPTSIZ(0),
		      OTG_DIEPTSIZ_PKTCNT(pktcnt) |
		      (xfrsiz & OTG_DIEPTSIZ_XFRSIZ_MASK));
	} else {
		out_w(OTG_DIEPTSIZ(ep->num),
		      OTG_DIEPTSIZ_PKTCNT(pktcnt) |
		      (xfrsiz & OTG_DIEPTSIZ_XFRSIZ_MASK));
	}

	/* EP 有効化 + NAK クリア */
	set_w(OTG_DIEPCTL(ep->num), OTG_DIEPCTL_EPENA | OTG_DIEPCTL_CNAK);

	/* データを TX FIFO に書き込む */
	if (xfrsiz > 0 && ep->xfer_buf != NULL) {
		ll_write_fifo(OTG_FIFO(ep->num), ep->xfer_buf, xfrsiz);
	}

	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP STALL 設定
 */
EXPORT ER dev_usb_hid_ep_stall(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	if (p_cb == NULL || ep == NULL) return E_PAR;

	if (ep->is_in) {
		if (in_w(OTG_DIEPCTL(ep->num)) & OTG_DIEPCTL_EPENA)
			set_w(OTG_DIEPCTL(ep->num), OTG_DIEPCTL_EPDIS);
		set_w(OTG_DIEPCTL(ep->num), OTG_DIEPCTL_STALL);
	} else {
		set_w(OTG_DOEPCTL(ep->num), OTG_DOEPCTL_STALL);
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP STALL クリア
 */
EXPORT ER dev_usb_hid_ep_unstall(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	if (p_cb == NULL || ep == NULL) return E_PAR;

	if (ep->is_in) {
		UW ctl = in_w(OTG_DIEPCTL(ep->num));
		ctl &= ~OTG_DIEPCTL_STALL;
		ctl |= OTG_DIEPCTL_SD0PID;
		out_w(OTG_DIEPCTL(ep->num), ctl);
	} else {
		UW ctl = in_w(OTG_DOEPCTL(ep->num));
		ctl &= ~OTG_DOEPCTL_STALL;
		ctl |= OTG_DOEPCTL_SD0PID;
		out_w(OTG_DOEPCTL(ep->num), ctl);
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP データバッファ設定
 *  DWC2 は FIFO ベースなので DPRAM バッファは不要だが、
 *  ミドルウェア互換のためステージングバッファを設定する
 */
EXPORT ER dev_usb_hid_ep_setbuf(T_USB_LL_CB *p_cb, UH ep_addr, UB *data)
{
	T_USB_EP *ep;

	if ((ep_addr & 0x7F) >= USB_MAX_EPS) return E_PAR;
	if ((ep_addr & 0x80) == 0x80)
		ep = &p_cb->in_ep[ep_addr & 0x7F];
	else
		ep = &p_cb->out_ep[ep_addr & 0x7F];
	ep->data_buf = data;
	return E_OK;
}

/*----------------------------------------------------------------------
 *  RXFLVL 割り込みハンドラ (RX FIFO non-empty)
 *  SETUP パケットとOUT データを FIFO から読み出す
 */
LOCAL UINT ll_rxflvl_handler(T_USB_LL_CB *p_cb)
{
	UW grxstsp = in_w(OTG_GRXSTSP);
	UW epnum  = grxstsp & OTG_GRXSTSP_EPNUM_MASK;
	UW bcnt   = (grxstsp & OTG_GRXSTSP_BCNT_MASK) >> OTG_GRXSTSP_BCNT_SHIFT;
	UW pktsts = (grxstsp & OTG_GRXSTSP_PKTSTS_MASK) >> OTG_GRXSTSP_PKTSTS_SHIFT;
	T_USB_EP *ep;
	UINT evt = 0;

	switch (pktsts) {
	case OTG_GRXSTSP_PKTSTS_SETUP_DATA:
		/* SETUP パケット (8 bytes) を読み出し */
		if (bcnt == 8) {
			ll_read_fifo(p_cb->setup_shadow, OTG_FIFO(0), 8);
		} else {
			ll_flush_rx_fifo(OTG_FIFO(0), bcnt);
		}
		break;

	case OTG_GRXSTSP_PKTSTS_OUT_DATA:
		/* OUT データ */
		ep = &p_cb->out_ep[epnum];
		if (bcnt > 0 && ep->xfer_buf != NULL) {
			ll_read_fifo(ep->xfer_buf, OTG_FIFO(0), bcnt);
			ep->xfer_buf += bcnt;
			ep->xfer_done += bcnt;
		} else if (bcnt > 0) {
			ll_flush_rx_fifo(OTG_FIFO(0), bcnt);
		}
		break;

	case OTG_GRXSTSP_PKTSTS_SETUP_CPLT:
		/* SETUP complete — EP0 IN の PID を DATA1 にリセット */
		p_cb->in_ep[0].next_pid = 1;
		break;

	case OTG_GRXSTSP_PKTSTS_OUT_CPLT:
		/* OUT transfer complete — 何もしない (DOEPINT.XFRC で処理) */
		break;

	default:
		/* その他のパケットステータス */
		if (bcnt > 0)
			ll_flush_rx_fifo(OTG_FIFO(0), bcnt);
		break;
	}

	return evt;
}

/*----------------------------------------------------------------------
 *  USB デバイス 割り込みハンドラ (HLL)
 *
 *  μT-Kernel HLL 割り込みハンドラ形式: void handler(UINT intno)
 *  HW ステータスクリアと FIFO データコピーのみ行い、
 *  プロトコル処理はイベントフラグ経由でタスクに委譲する
 */
EXPORT void usb_hid_inthdr(UINT intno)
{
	T_USB_LL_CB *p_cb = &ll_devcb;
	UW gintsts = in_w(OTG_GINTSTS) & in_w(OTG_GINTMSK);
	UINT evt = 0;

	/*
	 *  RX FIFO non-empty (SETUP パケット・OUT データ受信)
	 *  最優先で処理 — FIFO を読み出さないと後続の割り込みがブロックされる
	 */
	if (gintsts & OTG_GINTSTS_RXFLVL) {
		/* GINTMSK.RXFLVL を一時マスクして再入防止 */
		clr_w(OTG_GINTMSK, OTG_GINTSTS_RXFLVL);
		evt |= ll_rxflvl_handler(p_cb);
		set_w(OTG_GINTMSK, OTG_GINTSTS_RXFLVL);
	}

	/*
	 *  OUT endpoint 割り込み
	 */
	if (gintsts & OTG_GINTSTS_OEPINT) {
		UW daint = in_w(OTG_DAINT) & in_w(OTG_DAINTMSK);

		/* EP0 OUT */
		if (daint & OTG_DAINTMSK_OEPM(0)) {
			UW doepint = in_w(OTG_DOEPINT(0));

			if (doepint & OTG_DOEPINT_STUP) {
				/* SETUP phase done */
				out_w(OTG_DOEPINT(0), OTG_DOEPINT_STUP);
				p_cb->suspended = 0;
				evt |= USB_EVT_SETUP;

				/* EP0 OUT 再準備 */
				out_w(OTG_DOEPTSIZ(0),
				      OTG_DOEPTSIZ_STUPCNT(3) |
				      OTG_DOEPTSIZ_PKTCNT(1) |
				      (64 & OTG_DOEPTSIZ_XFRSIZ_MASK));
				set_w(OTG_DOEPCTL(0), OTG_DOEPCTL_EPENA | OTG_DOEPCTL_CNAK);
			}

			if (doepint & OTG_DOEPINT_XFRC) {
				out_w(OTG_DOEPINT(0), OTG_DOEPINT_XFRC);
				evt |= USB_EVT_EP0_OUT;

				/* EP0 OUT 再準備 (次の受信) */
				out_w(OTG_DOEPTSIZ(0),
				      OTG_DOEPTSIZ_STUPCNT(3) |
				      OTG_DOEPTSIZ_PKTCNT(1) |
				      (64 & OTG_DOEPTSIZ_XFRSIZ_MASK));
				set_w(OTG_DOEPCTL(0), OTG_DOEPCTL_EPENA | OTG_DOEPCTL_CNAK);
			}
		}
	}

	/*
	 *  IN endpoint 割り込み
	 */
	if (gintsts & OTG_GINTSTS_IEPINT) {
		UW daint = in_w(OTG_DAINT) & in_w(OTG_DAINTMSK);

		/* EP0 IN */
		if (daint & OTG_DAINTMSK_IEPM(0)) {
			UW diepint = in_w(OTG_DIEPINT(0));

			if (diepint & OTG_DIEPINT_XFRC) {
				out_w(OTG_DIEPINT(0), OTG_DIEPINT_XFRC);
				evt |= USB_EVT_EP0_IN;

				/* SET_ADDRESS ラッチ (STATUS IN 完了後) */
				if (p_cb->dev_addr > 0 &&
				    p_cb->in_ep[0].xfer_remain == 0) {
					out_w(OTG_DCFG,
					      (in_w(OTG_DCFG) & ~OTG_DCFG_DAD_MASK) |
					      OTG_DCFG_DAD(p_cb->dev_addr));
					p_cb->dev_addr = 0;
				}
			}
		}

		/* EP1 IN */
		if (daint & OTG_DAINTMSK_IEPM(1)) {
			UW diepint = in_w(OTG_DIEPINT(1));

			if (diepint & OTG_DIEPINT_XFRC) {
				out_w(OTG_DIEPINT(1), OTG_DIEPINT_XFRC);
				p_cb->in_ep[1].xfer_done = p_cb->in_ep[1].xfer_req;
				p_cb->in_ep[1].xfer_buf += p_cb->in_ep[1].xfer_done;
				if (p_cb->in_ep[1].xfer_remain == 0) {
					evt |= USB_EVT_EP1_IN;
				} else {
					/* multi-packet 継続 */
					p_cb->in_ep[1].xfer_done = 0;
					dev_usb_hid_ep_send(p_cb, &p_cb->in_ep[1]);
				}
			}
		}
	}

	/*
	 *  USB リセット
	 */
	if (gintsts & OTG_GINTSTS_USBRST) {
		out_w(OTG_GINTSTS, OTG_GINTSTS_USBRST);
		p_cb->suspended = 0;
		p_cb->dev_addr = 0;
		out_w(OTG_DCFG, (in_w(OTG_DCFG) & ~OTG_DCFG_DAD_MASK));

		/* EP0 OUT 再準備 */
		out_w(OTG_DOEPTSIZ(0),
		      OTG_DOEPTSIZ_STUPCNT(3) |
		      OTG_DOEPTSIZ_PKTCNT(1) |
		      (64 & OTG_DOEPTSIZ_XFRSIZ_MASK));

		evt |= USB_EVT_BUS_RESET;
	}

	/*
	 *  エニュメレーション完了
	 */
	if (gintsts & OTG_GINTSTS_ENUMDNE) {
		out_w(OTG_GINTSTS, OTG_GINTSTS_ENUMDNE);
		/* EP0 MPS = 64 (Full-speed) → DIEPCTL0[1:0] = 0 */
		clr_w(OTG_DIEPCTL(0), 0x03);
		/* GUSBCFG.TRDT を FS 用に再設定 */
		out_w(OTG_GUSBCFG,
		      (in_w(OTG_GUSBCFG) & ~OTG_GUSBCFG_TRDT_MASK) |
		      OTG_GUSBCFG_TRDT(6));
	}

	/*
	 *  サスペンド
	 */
	if (gintsts & OTG_GINTSTS_USBSUSP) {
		out_w(OTG_GINTSTS, OTG_GINTSTS_USBSUSP);
		p_cb->suspended = 1;
		evt |= USB_EVT_SUSPEND;
	}

	/*
	 *  リジューム / ウェイクアップ
	 */
	if (gintsts & OTG_GINTSTS_WKUINT) {
		out_w(OTG_GINTSTS, OTG_GINTSTS_WKUINT);
		p_cb->suspended = 0;
		evt |= USB_EVT_RESUME;
	}

	/*
	 *  SOF
	 */
	if (gintsts & OTG_GINTSTS_SOF) {
		out_w(OTG_GINTSTS, OTG_GINTSTS_SOF);
	}

	ClearInt(intno);

	/* タスクにイベント通知 */
	if (evt != 0 && p_cb->usb_flgid > 0) {
		tk_set_flg(p_cb->usb_flgid, evt);
	}
}

/*======================================================================
 *  ドライバブリッジ: ミドルウェア → HAL マッピング
 *  (RP2040 版と同一インターフェース)
 *======================================================================*/

EXPORT ER ll_drv_init(T_USB_MW *p_usb)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	p_cb->p_mw = p_usb;
	return E_OK;
}

EXPORT ER ll_drv_deinit(T_USB_MW *p_usb)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	dev_usb_hid_lldeinit(p_cb);
	return E_OK;
}

EXPORT ER ll_drv_start(T_USB_MW *p_usb)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	dev_usb_hid_connect(p_cb);
	return E_OK;
}

EXPORT ER ll_drv_stop(T_USB_MW *p_usb)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	dev_usb_hid_disconnect(p_cb);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP オープン
 */
EXPORT ER ll_open_ep(T_USB_MW *p_usb, UB ep_addr, UB ep_type, UH ep_mps)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep;
	UB epnum = ep_addr & 0x7F;

	if (ep_addr & 0x80) {
		ep = &p_cb->in_ep[epnum];
		ep->is_in = 1;
	} else {
		ep = &p_cb->out_ep[epnum];
		ep->is_in = 0;
	}
	ep->num = epnum;
	ep->type = ep_type;
	ep->max_pkt_size = ep_mps;
	ep->next_pid = 0;

	dev_usb_hid_ep_activate(p_cb, ep);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP クローズ
 */
EXPORT ER ll_close_ep(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep;

	if (ep_addr & 0x80)
		ep = &p_cb->in_ep[ep_addr & 0x7F];
	else
		ep = &p_cb->out_ep[ep_addr & 0x7F];
	dev_usb_hid_ep_deactivate(p_cb, ep);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  送信開始
 */
EXPORT ER ll_start_tx(T_USB_MW *p_usb, UB ep_addr, UB *buf, UW len)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = &p_cb->in_ep[ep_addr & 0x7F];

	ep->xfer_buf = buf;
	ep->xfer_remain = len;
	ep->xfer_done = 0;
	ep->is_in = 1;
	dev_usb_hid_ep_send(p_cb, ep);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  受信設定
 */
EXPORT ER ll_start_rx(T_USB_MW *p_usb, UB ep_num, UB *buf, UW len)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = &p_cb->out_ep[ep_num];

	ep->xfer_buf = buf;
	ep->xfer_remain = len;
	ep->xfer_done = 0;
	ep->is_in = 0;
	dev_usb_hid_ep_recv(p_cb, ep);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  アドレス設定
 */
EXPORT ER ll_set_addr(T_USB_MW *p_usb, UB address)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	dev_usb_hid_set_addr(p_cb, address);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP STALL
 */
EXPORT ER ll_stall_ep(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep;

	if (ep_addr & 0x80)
		ep = &p_cb->in_ep[ep_addr & 0x7F];
	else
		ep = &p_cb->out_ep[ep_addr & 0x7F];
	ep->is_stall = 1;
	dev_usb_hid_ep_stall(p_cb, ep);
	return E_OK;
}

EXPORT ER ll_unstall_ep(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep;

	if (ep_addr & 0x80)
		ep = &p_cb->in_ep[ep_addr & 0x7F];
	else
		ep = &p_cb->out_ep[ep_addr & 0x7F];
	ep->is_stall = 0;
	dev_usb_hid_ep_unstall(p_cb, ep);
	return E_OK;
}

EXPORT BOOL ll_is_stalled(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;

	if (ep_addr & 0x80)
		return p_cb->in_ep[ep_addr & 0x7F].is_stall;
	else
		return p_cb->out_ep[ep_addr & 0x7F].is_stall;
}

#endif	/* CPU_STM32H7 */
