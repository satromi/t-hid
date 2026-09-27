/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    RP2040 USB デバイス HAL + ドライバブリッジ
 *    ISR は HW ステータスクリア + DPRAM コピーのみ行い、
 *    プロトコル処理はイベントフラグ経由でタスクに委譲する
 *
 *    RP2040 Datasheet Section 4.1 USB
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <string.h>
#include "../../usb_hid.h"

/*
 *  USB リセット待ちタイムアウト (10us x 100 x 100 = 100ms)
 */
#define USB_RESET_WAIT_MAX	(100 * 100)

/*
 *  デフォルト割り込みマスク (RP2040 Datasheet Section 4.1.2.6)
 */
#define USB_INT_DEFAULT_MASK	(USB_INTE_BUFF_STATUS | USB_INTE_BUS_RESET | \
				 USB_INTE_SETUP_REQ   | USB_INTE_DEV_SUSPEND | \
				 USB_INTE_DEV_RESUME_FROM_HOST)
#define USB_MUXING_DEFAULT	(USB_MUXING_TO_PHY | USB_MUXING_SOFTCON)
#define USB_PWR_DEFAULT		(USB_PWR_VBUS_DETECT | USB_PWR_VBUS_DETECT_OVR_EN)

/*
 *  デバイス制御ブロック (シングルポート)
 */
LOCAL T_USB_LL_CB ll_devcb;

/*----------------------------------------------------------------------
 *  バッファコントロール書き込み (RP2040 Datasheet Section 4.1.2.5.1)
 *
 *  このレジスタは USB クロックドメイン (48MHz) から読まれる。AVAIL を
 *  他のビットと同一ストアで書くと、長さや PID が反映される前に
 *  ハードウェアがバッファを掴む可能性がある。AVAIL 以外を先に書き、
 *  USB クロック数サイクル分空けてから AVAIL を立てる。
 */
LOCAL void ll_ep_buf_set(T_USB_EP *ep, UW val)
{
	volatile INT i;

	*ep->buf_ctrl_reg = val & ~USB_BUF_CTRL_AVAIL;
	for(i = 0; i < 6; i++) {}	/* > 3 USB クロック */
	*ep->buf_ctrl_reg = val;
}

/*----------------------------------------------------------------------
 *  送信バッファ設定 (RP2040 Datasheet Section 4.1.2.5.1)
 */
LOCAL void ll_ep_buf_tx(T_USB_EP *ep, UW len, UW mode)
{
	UW val = len | mode | USB_BUF_CTRL_AVAIL;

	val |= USB_BUF_CTRL_FULL;
	val |= ep->next_pid ? USB_BUF_CTRL_DATA1_PID : USB_BUF_CTRL_DATA0_PID;
	ep->next_pid ^= 1;
	ll_ep_buf_set(ep, val);
}

/*----------------------------------------------------------------------
 *  受信バッファ設定 (RP2040 Datasheet Section 4.1.2.5.1)
 */
LOCAL void ll_ep_buf_rx(T_USB_EP *ep, UW len, UW mode)
{
	UW val = len | mode | USB_BUF_CTRL_AVAIL;

	val |= ep->next_pid ? USB_BUF_CTRL_DATA1_PID : USB_BUF_CTRL_DATA0_PID;
	ep->next_pid ^= 1;
	ll_ep_buf_set(ep, val);
}

/*----------------------------------------------------------------------
 *  USB デバイス HAL 初期化
 *  return: 制御ブロックポインタ (NULL = エラー)
 */
EXPORT T_USB_LL_CB * dev_usb_hid_llinit(T_USB_INIT *p_init)
{
	T_USB_LL_CB *p_cb;
	UW	imask = USB_INT_DEFAULT_MASK;
	INT	tick = USB_RESET_WAIT_MAX;

	if(p_init == NULL) return NULL;

	p_cb = &ll_devcb;
	memset(p_cb, 0, sizeof(T_USB_LL_CB));
	p_cb->base  = USB_REGS_BASE;
	p_cb->dpram = (T_USB_DPRAM *)USB_DPRAM_BASE;
	memcpy(&p_cb->init, p_init, sizeof(T_USB_INIT));
	p_cb->setup_buf = &p_cb->dpram->setup_packet[0];

	/*
	 *  USB コントローラリセット
	 */
	set_w(RESETS_RESET, RESETS_RESET_USBCTRL);
	clr_w(RESETS_RESET, RESETS_RESET_USBCTRL);
	while((in_w(RESETS_RESET_DONE) & RESETS_RESET_USBCTRL) == 0) {
		volatile INT i;
		for(i = 0; i < 100; i++) {}	/* ~10us 待ち */
		if(--tick <= 0) return NULL;	/* タイムアウト */
	}

	memset(p_cb->dpram, 0, sizeof(T_USB_DPRAM));

	/*
	 *  内蔵 PHY にミュクシング
	 */
	out_w(p_cb->base + USB_REG_MUXING, USB_MUXING_DEFAULT);

	/*
	 *  VBUS 検出オーバーライド (Pico は VBUS 検出ピンなし)
	 */
	out_w(p_cb->base + USB_REG_PWR, USB_PWR_DEFAULT);

	/*
	 *  USB コントローラ有効 (デバイスモード)
	 */
	out_w(p_cb->base + USB_REG_MAIN_CTRL, USB_MAIN_CTRL_CONTROLLER_EN);

	/*
	 *  EP0 割り込み設定 (RP2040 Datasheet Section 4.1.4.6)
	 */
	out_w(p_cb->base + USB_REG_SIE_CTRL, USB_SIE_CTRL_EP0_INT_1BUF);

	/*
	 *  割り込みマスク設定
	 */
	if(p_init->sof_enable == 1)
		imask |= USB_INTE_DEV_SOF;
	out_w(p_cb->base + USB_REG_INTE, imask);

	/*
	 *  EP データバッファ設定 (DPRAM 割り当て)
	 *  RP2040 Datasheet Section 4.1.2.4
	 */
	dev_usb_hid_ep_setbuf(p_cb, 0x81, &p_cb->dpram->epx_data[0 * 64]);
	dev_usb_hid_ep_setbuf(p_cb, 0x01, &p_cb->dpram->epx_data[1 * 64]);
	dev_usb_hid_ep_setbuf(p_cb, 0x83, &p_cb->dpram->epx_data[2 * 64]);

	return p_cb;
}

/*----------------------------------------------------------------------
 *  USB デバイス HAL 終了
 */
EXPORT ER dev_usb_hid_lldeinit(T_USB_LL_CB *p_cb)
{
	INT tick = USB_RESET_WAIT_MAX;

	if(p_cb == NULL) return E_PAR;

	set_w(RESETS_RESET, RESETS_RESET_USBCTRL);
	clr_w(RESETS_RESET, RESETS_RESET_USBCTRL);
	while((in_w(RESETS_RESET_DONE) & RESETS_RESET_USBCTRL) == 0) {
		volatile INT i;
		for(i = 0; i < 100; i++) {}
		if(--tick <= 0) return E_TMOUT;
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  D+ プルアップ有効 (デバイス接続)
 */
EXPORT ER dev_usb_hid_connect(T_USB_LL_CB *p_cb)
{
	if(p_cb == NULL) return E_PAR;
	set_w(p_cb->base + USB_REG_SIE_CTRL, USB_SIE_CTRL_PULLUP_EN);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  デバイス切断
 */
EXPORT ER dev_usb_hid_disconnect(T_USB_LL_CB *p_cb)
{
	if(p_cb == NULL) return E_PAR;
	clr_w(p_cb->base + USB_REG_SIE_STATUS,
	      USB_SIE_STATUS_SETUP_REC | USB_SIE_STATUS_BUS_RESET);
	clr_w(p_cb->base + USB_REG_BUFF_STATUS, 0xFFFFFFFF);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  USB アドレス設定
 */
EXPORT ER dev_usb_hid_set_addr(T_USB_LL_CB *p_cb, UB address)
{
	if(p_cb == NULL) return E_PAR;
	if(address == 0)
		out_w(p_cb->base + USB_REG_ADDR_ENDP, 0);
	else
		p_cb->dev_addr = address;
	return E_OK;
}

/*----------------------------------------------------------------------
 *  エンドポイント有効化
 */
EXPORT ER dev_usb_hid_ep_activate(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UW type, doffset;

	if(p_cb == NULL || ep == NULL) return E_PAR;

	doffset = (UW)ep->data_buf - (UW)p_cb->dpram;
	type  = ((UW)ep->type & 3) << USB_EP_TYPE_SHIFT;
	type |= USB_EP_CTRL_ENABLE | USB_EP_CTRL_INT_PER_BUF | doffset;

	/*
	 *  EP0 にはエンドポイント制御レジスタが無く ep_ctrl_reg は NULL。
	 *  EP0 の制御は SIE_CTRL 側で行う。
	 */
	if(ep->ep_ctrl_reg != NULL)
		*ep->ep_ctrl_reg = type;

	if(!ep->is_in && ep->num == 0) {
		ep->xfer_req = ep->max_pkt_size;
		ll_ep_buf_rx(ep, ep->xfer_req, 0);
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  エンドポイント無効化
 */
EXPORT ER dev_usb_hid_ep_deactivate(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	if(p_cb == NULL || ep == NULL) return E_PAR;
	*ep->ep_ctrl_reg = 0;
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP 受信開始
 */
EXPORT ER dev_usb_hid_ep_recv(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	if(p_cb == NULL || ep == NULL) return E_PAR;

	if(ep->xfer_remain > ep->max_pkt_size) {
		ep->xfer_req = ep->max_pkt_size;
		ep->xfer_remain -= ep->xfer_req;
	} else {
		ep->xfer_req = ep->xfer_remain;
		ep->xfer_remain = 0;
	}

	/*
	 *  EP0 も含めて必ずバッファをアームする。
	 *  制御 IN 転送のステータスステージはホストからのゼロ長 OUT で、
	 *  受け口が用意されていないと NAK が返り、ホストは制御転送の
	 *  タイムアウトまで待つことになる。
	 */
	ll_ep_buf_rx(ep, ep->xfer_req, 0);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP 送信開始
 */
EXPORT ER dev_usb_hid_ep_send(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	if(p_cb == NULL || ep == NULL) return E_PAR;

	if(ep->xfer_remain > ep->max_pkt_size) {
		ep->xfer_req = ep->max_pkt_size;
		ep->xfer_remain -= ep->xfer_req;
	} else {
		ep->xfer_req = ep->xfer_remain;
		ep->xfer_remain = 0;
	}
	if(ep->xfer_req > 0)
		memcpy(ep->data_buf, ep->xfer_buf, ep->xfer_req);
	ll_ep_buf_tx(ep, ep->xfer_req, 0);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP STALL 設定
 */
EXPORT ER dev_usb_hid_ep_stall(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	if(p_cb == NULL || ep == NULL) return E_PAR;
	if(ep->num == 0) {
		ll_ep_buf_rx(ep, 0, USB_BUF_CTRL_STALL);
		ll_ep_buf_tx(ep, 0, USB_BUF_CTRL_STALL);
	} else {
		if(ep->is_in)
			ll_ep_buf_tx(ep, 0, USB_BUF_CTRL_STALL);
		else
			ll_ep_buf_rx(ep, 0, USB_BUF_CTRL_STALL);
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP STALL クリア
 */
EXPORT ER dev_usb_hid_ep_unstall(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	if(p_cb == NULL || ep == NULL) return E_PAR;
	if(ep->is_in)
		ll_ep_buf_tx(ep, ep->xfer_req, 0);
	else
		ll_ep_buf_rx(ep, ep->xfer_req, 0);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP データバッファ設定
 */
EXPORT ER dev_usb_hid_ep_setbuf(T_USB_LL_CB *p_cb, UH ep_addr, UB *data)
{
	T_USB_EP *ep;

	if((ep_addr & 0x7F) >= p_cb->init.num_eps) return E_PAR;
	if((ep_addr & 0x80) == 0x80)
		ep = &p_cb->in_ep[ep_addr & 0x7F];
	else
		ep = &p_cb->out_ep[ep_addr];
	ep->data_buf = data;
	return E_OK;
}

/*----------------------------------------------------------------------
 *  セットアップパケット受信処理 (ISR コンテキスト)
 *  SETUP パケットをシャドウバッファにコピーし、PID をリセット
 */
LOCAL void ll_setup_received(T_USB_LL_CB *p_cb)
{
	/*
	 *  SETUP の後はデータ/ステータスステージとも DATA1 から始まる。
	 *  IN 側だけを初期化すると OUT 側の PID が転送をまたいで
	 *  トグルし続け、制御 IN 転送のステータスステージ (OUT の
	 *  ゼロ長パケット) が DATA0 になる回が生じる。ホストはその
	 *  転送を完了と見なせず、制御転送のタイムアウトまで待つ。
	 */
	p_cb->in_ep[0].next_pid  = 1;
	p_cb->out_ep[0].next_pid = 1;
	memcpy(p_cb->setup_shadow, (void *)p_cb->setup_buf, 8);
}

/*----------------------------------------------------------------------
 *  エンドポイントバッファステータスハンドラ (ISR コンテキスト)
 *
 *  HW ステータスクリア + DPRAM データコピーのみ行い、
 *  プロトコル処理はタスクに委譲する。
 *  multi-packet 継続と SET_ADDRESS ラッチは ISR 内で処理。
 *  戻り値: タスクに通知するイベントビットマスク
 *
 *  RP2040 Datasheet Section 4.1.2.6
 */
LOCAL UINT ll_buf_status_handler(T_USB_LL_CB *p_cb)
{
	T_USB_EP *ep;
	UW	buffers = in_w(p_cb->base + USB_REG_BUFF_STATUS);
	UW	bit = 1;
	UH	len;
	UINT	i;
	UINT	evt = 0;

	for(i = 0; i < USB_MAX_EPS * 2; i++) {
		if(buffers == 0) break;
		if((buffers & bit) != 0) {
			clr_w(p_cb->base + USB_REG_BUFF_STATUS, bit);

			if((i & 1) == 0)
				ep = &p_cb->in_ep[i >> 1];
			else
				ep = &p_cb->out_ep[i >> 1];

			len = *ep->buf_ctrl_reg & USB_BUF_CTRL_LEN_MASK;
			if(len > ep->max_pkt_size) len = ep->max_pkt_size;

			if((i >> 1) == 0) {
				/* EP0 処理 */
				if(ep->is_in) {
					ep->xfer_done = len;
					ep->xfer_buf += ep->xfer_done;
					evt |= USB_EVT_EP0_IN;
					/* SET_ADDRESS ラッチ (HW タイミング必須) */
					if(p_cb->dev_addr > 0 && ep->xfer_remain == 0) {
						out_w(p_cb->base + USB_REG_ADDR_ENDP,
						      p_cb->dev_addr);
						p_cb->dev_addr = 0;
					}
				} else {
					/*
					 *  受信長はバッファコントロールから読む。
					 *  SETUP 時点の wLength で完了を先取りすると、
					 *  データ到着前に DPRAM を読むことになる。
					 */
					ep->xfer_done = len;
					if(len != 0 && ep->xfer_buf != NULL) {
						memcpy(ep->xfer_buf,
						       p_cb->dpram->ep0_buf_a, len);
						ep->xfer_buf += len;
					}
					evt |= USB_EVT_EP0_OUT;
				}
			} else {
				/* EPn 処理 */
				if(ep->is_in) {
					ep->xfer_done = len;
					if(ep->xfer_done != 0)
						memcpy(ep->data_buf, ep->xfer_buf, len);
					ep->xfer_buf += ep->xfer_done;
					if(ep->xfer_remain == 0) {
						evt |= (ep->num == 3) ?
						       USB_EVT_EP3_IN : USB_EVT_EP1_IN;
					} else {
						/* multi-packet 継続 (ISR 内で処理) */
						ep->xfer_done = 0;
						dev_usb_hid_ep_send(p_cb, ep);
					}
				} else {
					if(len != 0)
						memcpy(ep->xfer_buf, ep->data_buf, len);
					ep->xfer_done += len;
					ep->xfer_buf  += len;
					if(ep->xfer_remain > 0 && len >= ep->max_pkt_size) {
						/* multi-packet 継続 (ISR 内で処理) */
						ep->xfer_done = 0;
						dev_usb_hid_ep_recv(p_cb, ep);
					}
				}
			}
			buffers &= ~bit;
		}
		bit <<= 1;
	}
	return evt;
}

/*----------------------------------------------------------------------
 *  USB デバイス 割り込みハンドラ (HLL)
 *
 *  μT-Kernel HLL 割り込みハンドラ形式: void handler(UINT intno)
 *  HW ステータスクリアと DPRAM データコピーのみ行い、
 *  プロトコル処理はイベントフラグ経由でタスクに委譲する
 *  RP2040 Datasheet Section 4.1.2.6
 */
EXPORT void usb_hid_inthdr(UINT intno)
{
	T_USB_LL_CB *p_cb = &ll_devcb;
	UW	isr = in_w(p_cb->base + USB_REG_INTS);
	UW	mask;
	UINT	evt = 0;

	/* SETUP リクエスト (RP2040 Datasheet Section 4.1.4.7) */
	if(isr & USB_INTS_SETUP_REQ) {
		UB	*p;
		UB	bmRequest;
		UH	wLength;

		clr_w(p_cb->base + USB_REG_SIE_STATUS, USB_SIE_STATUS_SETUP_REC);
		p_cb->suspended = 0;

		p = (UB *)p_cb->setup_buf;
		bmRequest = p[0];
		wLength = p[6] | (p[7] << 8);
		ll_setup_received(p_cb);
		evt |= USB_EVT_SETUP;
	}

	/* バッファステータス (EP 転送完了) */
	if(isr & USB_INTS_BUFF_STATUS) {
		evt |= ll_buf_status_handler(p_cb);
	}

	/* バスリセット */
	if(isr & USB_INTS_BUS_RESET) {
		clr_w(p_cb->base + USB_REG_SIE_STATUS, USB_SIE_STATUS_BUS_RESET);
		p_cb->suspended = 0;
		out_w(p_cb->base + USB_REG_ADDR_ENDP, 0);
		p_cb->dev_addr = 0;
		evt |= USB_EVT_BUS_RESET;
	}

	/* レジューム */
	if(isr & USB_INTS_DEV_RESUME_FROM_HOST) {
		clr_w(p_cb->base + USB_REG_SIE_STATUS, USB_SIE_STATUS_RESUME);
		mask = USB_INT_DEFAULT_MASK;
		if(p_cb->init.sof_enable == 1)
			mask |= USB_INTE_DEV_SOF;
		out_w(p_cb->base + USB_REG_INTE, mask);
		p_cb->suspended = 0;
		evt |= USB_EVT_RESUME;
	}

	/* サスペンド */
	if(isr & USB_INTS_DEV_SUSPEND) {
		clr_w(p_cb->base + USB_REG_SIE_STATUS, USB_SIE_STATUS_SUSPENDED);
		p_cb->suspended = 1;
		evt |= USB_EVT_SUSPEND;
	}

	/* SOF */
	if(isr & USB_INTS_DEV_SOF) {
		in_w(p_cb->base + USB_REG_SOF_RD);
	}

	ClearInt(intno);

	/* タスクにイベント通知 */
	if(evt != 0 && p_cb->usb_flgid > 0) {
		tk_set_flg(p_cb->usb_flgid, evt);
	}
}

/*======================================================================
 *  ドライバブリッジ: ミドルウェア → HAL マッピング
 *======================================================================*/

/*----------------------------------------------------------------------
 *  ドライバ初期化
 */
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
 *  EP オープン (RP2040 Datasheet Section 4.1.2.4)
 */
EXPORT ER ll_open_ep(T_USB_MW *p_usb, UB ep_addr, UB ep_type, UH ep_mps)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep;
	UB epnum = ep_addr & 0x7F;

	if(ep_addr & 0x80) {
		ep = &p_cb->in_ep[epnum];
		ep->is_in = 1;
	} else {
		ep = &p_cb->out_ep[epnum];
		ep->is_in = 0;
	}
	ep->num       = epnum;
	ep->type      = ep_type;
	ep->max_pkt_size = ep_mps;
	ep->next_pid  = 0;

	/* DPRAM ポインタ設定 (RP2040 Datasheet Section 4.1.2.4) */
	if(epnum == 0) {
		ep->data_buf = p_cb->dpram->ep0_buf_a;
		if(ep->is_in)
			ep->buf_ctrl_reg = &p_cb->dpram->ep_buf_ctrl[0].in;
		else
			ep->buf_ctrl_reg = &p_cb->dpram->ep_buf_ctrl[0].out;
		ep->ep_ctrl_reg = NULL;	/* EP0 はコントロールレジスタなし */
	} else {
		if(ep->is_in) {
			ep->buf_ctrl_reg   = &p_cb->dpram->ep_buf_ctrl[epnum].in;
			ep->ep_ctrl_reg = &p_cb->dpram->ep_ctrl[epnum - 1].in;
		} else {
			ep->buf_ctrl_reg   = &p_cb->dpram->ep_buf_ctrl[epnum].out;
			ep->ep_ctrl_reg = &p_cb->dpram->ep_ctrl[epnum - 1].out;
		}
	}

	if(epnum != 0)
		dev_usb_hid_ep_activate(p_cb, ep);
	else if(!ep->is_in)
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

	if(ep_addr & 0x80)
		ep = &p_cb->in_ep[ep_addr & 0x7F];
	else
		ep = &p_cb->out_ep[ep_addr];
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

	ep->xfer_buf  = buf;
	ep->xfer_remain   = len;
	ep->xfer_done = 0;
	ep->is_in      = 1;
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

	ep->xfer_buf  = buf;
	ep->xfer_remain   = len;
	ep->xfer_done = 0;
	ep->is_in      = 0;
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

	if(ep_addr & 0x80)
		ep = &p_cb->in_ep[ep_addr & 0x7F];
	else
		ep = &p_cb->out_ep[ep_addr];
	ep->is_stall = 1;
	dev_usb_hid_ep_stall(p_cb, ep);
	return E_OK;
}

EXPORT ER ll_unstall_ep(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep;

	if(ep_addr & 0x80)
		ep = &p_cb->in_ep[ep_addr & 0x7F];
	else
		ep = &p_cb->out_ep[ep_addr];
	ep->is_stall = 0;
	dev_usb_hid_ep_unstall(p_cb, ep);
	return E_OK;
}

EXPORT BOOL ll_is_stalled(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;

	if(ep_addr & 0x80)
		return p_cb->in_ep[ep_addr & 0x7F].is_stall;
	else
		return p_cb->out_ep[ep_addr].is_stall;
}

#endif	/* CPU_RP2040 */
