/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    STM32H5 USB_DRD_FS デバイス HAL + ドライバブリッジ
 *
 *    ISR は HW ステータスクリア + PMA コピーのみ行い、
 *    プロトコル処理はイベントフラグ経由でタスクに委譲する
 *
 *    STM32H533 Reference Manual RM0481
 *    "Universal serial bus full-speed host/device interface (USB)"
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef MTKBSP_CPU_STM32H5

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <string.h>
#include "../../usb_hid.h"

/*======================================================================
 *  レジスタ定義
 *======================================================================*/

#define USB_BASE		0x40016000UL	/* USB_DRD_FS (non-secure) */
#define USB_PMA_BASE		0x40016400UL	/* Packet memory 2KB */

#define USB_CHEP(n)		(USB_BASE + 0x04 * (n))
#define USB_CNTR		(USB_BASE + 0x40)
#define USB_ISTR		(USB_BASE + 0x44)
#define USB_FNR			(USB_BASE + 0x48)
#define USB_DADDR		(USB_BASE + 0x4C)
#define USB_LPMCSR		(USB_BASE + 0x54)
#define USB_BCDR		(USB_BASE + 0x58)

/* CNTR */
#define CNTR_USBRST		(1u << 0)
#define CNTR_PDWN		(1u << 1)
#define CNTR_SUSPRDY		(1u << 2)
#define CNTR_SUSPEN		(1u << 3)
#define CNTR_L2RES		(1u << 4)
#define CNTR_RESETM		(1u << 10)
#define CNTR_SUSPM		(1u << 11)
#define CNTR_WKUPM		(1u << 12)
#define CNTR_CTRM		(1u << 15)
#define CNTR_HOST		(1u << 31)

/* ISTR (フラグは 0 書き込みでクリア) */
#define ISTR_IDN_MASK		0x0Fu
#define ISTR_DIR		(1u << 4)
#define ISTR_RESET		(1u << 10)
#define ISTR_SUSP		(1u << 11)
#define ISTR_WKUP		(1u << 12)
#define ISTR_ERR		(1u << 13)
#define ISTR_PMAOVR		(1u << 14)
#define ISTR_CTR		(1u << 15)

/* DADDR */
#define DADDR_EF		(1u << 7)

/* BCDR */
#define BCDR_DPPU		(1u << 15)

/*
 *  CHEPnR
 *    VTRX/VTTX : 0 書き込みでクリア、1 書き込みは無効
 *    DTOG/STAT : 1 書き込みでトグル、0 書き込みは無効
 *    その他    : 通常の読み書き (SETUP は読み出し専用)
 */
#define CHEP_EA_MASK		0x000Fu
#define CHEP_STTX_MASK		0x0030u
#define CHEP_DTOGTX		0x0040u
#define CHEP_VTTX		0x0080u
#define CHEP_KIND		0x0100u
#define CHEP_UTYPE_MASK		0x0600u
#define CHEP_SETUP		0x0800u
#define CHEP_STRX_MASK		0x3000u
#define CHEP_DTOGRX		0x4000u
#define CHEP_VTRX		0x8000u
#define CHEP_REG_MASK		0x07FF8F8Fu	/* トグルビット以外 */

#define CHEP_UTYPE_BULK		0x0000u
#define CHEP_UTYPE_CONTROL	0x0200u
#define CHEP_UTYPE_ISO		0x0400u
#define CHEP_UTYPE_INTERRUPT	0x0600u

#define CHEP_TX_DIS		0x0000u
#define CHEP_TX_STALL		0x0010u
#define CHEP_TX_NAK		0x0020u
#define CHEP_TX_VALID		0x0030u
#define CHEP_RX_DIS		0x0000u
#define CHEP_RX_STALL		0x1000u
#define CHEP_RX_NAK		0x2000u
#define CHEP_RX_VALID		0x3000u

/*
 *  PMA バッファディスクリプタ (PMA 先頭に CHEP 毎 8 バイト)
 *    TXBD: [15:0] ADDR, [25:16] COUNT
 *    RXBD: [15:0] ADDR, [25:16] COUNT, [30:26] NUM_BLOCK, [31] BLSIZE
 */
#define PMA_TXBD(n)		(USB_PMA_BASE + 8 * (n))
#define PMA_RXBD(n)		(USB_PMA_BASE + 8 * (n) + 4)
#define PMA_COUNT(v)		(((v) >> 16) & 0x3FFu)
#define RXBD_BLSIZE		(1u << 31)

/*
 *  PMA バッファ割り当て (ディスクリプタ表 64B の後ろに各 64B 固定)
 */
#define PMA_TX_ADDR(n)		(0x040u + (UW)(n) * 0x80u)
#define PMA_RX_ADDR(n)		(0x080u + (UW)(n) * 0x80u)
#define PMA_BUF_SIZE		64u

/* RCC / PWR / CRS (USB クロック・電源) */
#define RCC_BASE_		0x44020C00UL
#define RCC_CR_			(RCC_BASE_ + 0x000)
#define RCC_APB1LENR_		(RCC_BASE_ + 0x09C)
#define RCC_APB2ENR_		(RCC_BASE_ + 0x0A4)
#define RCC_CCIPR4_		(RCC_BASE_ + 0x0E4)
#define RCC_CR_HSI48ON		(1u << 12)
#define RCC_CR_HSI48RDY		(1u << 13)
#define RCC_APB1LENR_CRSEN	(1u << 24)
#define RCC_APB2ENR_USBEN	(1u << 24)
#define RCC_CCIPR4_USBSEL_MASK	(3u << 4)
#define RCC_CCIPR4_USBSEL_HSI48	(3u << 4)

#define PWR_USBSCR_		(0x44020800UL + 0x38)
#define PWR_USBSCR_USB33SV	(1u << 25)

#define CRS_CR_			(0x40006000UL + 0x00)
#define CRS_CR_CEN		(1u << 5)
#define CRS_CR_AUTOTRIMEN	(1u << 6)

/*======================================================================
 *  内部データ
 *======================================================================*/

LOCAL T_USB_LL_CB ll_devcb;

/*======================================================================
 *  CHEP レジスタ操作
 *======================================================================*/

/* STAT_TX を state にする (トグルで目的値へ) */
LOCAL void chep_set_tx_status(UINT n, UW state)
{
	UW v = in_w(USB_CHEP(n)) & (CHEP_REG_MASK | CHEP_STTX_MASK);
	v ^= (state & CHEP_STTX_MASK);
	out_w(USB_CHEP(n), v | CHEP_VTRX | CHEP_VTTX);
}

/* STAT_RX を state にする */
LOCAL void chep_set_rx_status(UINT n, UW state)
{
	UW v = in_w(USB_CHEP(n)) & (CHEP_REG_MASK | CHEP_STRX_MASK);
	v ^= (state & CHEP_STRX_MASK);
	out_w(USB_CHEP(n), v | CHEP_VTRX | CHEP_VTTX);
}

/* DTOG_TX を 0 (DATA0) にする */
LOCAL void chep_clear_dtog_tx(UINT n)
{
	UW r = in_w(USB_CHEP(n));
	if (r & CHEP_DTOGTX) {
		out_w(USB_CHEP(n), (r & CHEP_REG_MASK) | CHEP_VTRX | CHEP_VTTX | CHEP_DTOGTX);
	}
}

/* DTOG_RX を 0 (DATA0) にする */
LOCAL void chep_clear_dtog_rx(UINT n)
{
	UW r = in_w(USB_CHEP(n));
	if (r & CHEP_DTOGRX) {
		out_w(USB_CHEP(n), (r & CHEP_REG_MASK) | CHEP_VTRX | CHEP_VTTX | CHEP_DTOGRX);
	}
}

LOCAL void chep_clear_ctr_rx(UINT n)
{
	UW r = in_w(USB_CHEP(n));
	out_w(USB_CHEP(n), (r & CHEP_REG_MASK & ~CHEP_VTRX) | CHEP_VTTX);
}

LOCAL void chep_clear_ctr_tx(UINT n)
{
	UW r = in_w(USB_CHEP(n));
	out_w(USB_CHEP(n), (r & CHEP_REG_MASK & ~CHEP_VTTX) | CHEP_VTRX);
}

/* EP 種別とアドレスを設定 (STAT/DTOG と反対方向の設定は保持) */
LOCAL void chep_set_type(UINT n, UW utype)
{
	UW r = in_w(USB_CHEP(n));
	r &= CHEP_REG_MASK & ~(CHEP_UTYPE_MASK | CHEP_EA_MASK | CHEP_KIND);
	out_w(USB_CHEP(n), r | utype | n | CHEP_VTRX | CHEP_VTTX);
}

/*======================================================================
 *  PMA アクセス (32bit ワード単位)
 *======================================================================*/

LOCAL void pma_write(UW pma_addr, const UB *src, UW len)
{
	UW addr = USB_PMA_BASE + pma_addr;
	UW i;

	for (i = 0; i < len; i += 4) {
		UW w = 0;
		UW k;
		for (k = 0; k < 4 && (i + k) < len; k++) {
			w |= (UW)src[i + k] << (8 * k);
		}
		out_w(addr + i, w);
	}
}

LOCAL void pma_read(UB *dst, UW pma_addr, UW len)
{
	UW addr = USB_PMA_BASE + pma_addr;
	UW i;

	for (i = 0; i < len; i += 4) {
		UW w = in_w(addr + i);
		UW k;
		for (k = 0; k < 4 && (i + k) < len; k++) {
			dst[i + k] = (UB)(w >> (8 * k));
		}
	}
}

/*
 *  RXBD の受信バイト数
 *    COUNT_RX の PMA への書き戻しは CTR_RX より数サイクル遅れることがあり、
 *    割り込み直後に読むと 0 が返る。少し待ってから読む。SETUP は必ず
 *    8 バイトなので、8 になるまで読み直す。
 */
LOCAL UW pma_rx_count(UINT n, BOOL setup)
{
	volatile UW dly;
	UW cnt;
	UINT retry;

	for (dly = 0; dly < 10; dly++) {}
	cnt = PMA_COUNT(in_w(PMA_RXBD(n)));
	for (retry = 0; setup && cnt != 8 && retry < 100; retry++) {
		cnt = PMA_COUNT(in_w(PMA_RXBD(n)));
	}
	return cnt;
}

/* RXBD の受信可能サイズ (NUM_BLOCK/BLSIZE) を設定 */
LOCAL void pma_set_rx_size(UINT n, UW size)
{
	UW v = PMA_RX_ADDR(n);

	if (size > 62) {
		/* 32 バイトブロック */
		v |= RXBD_BLSIZE | (((size + 31) / 32 - 1) << 26);
	} else {
		/* 2 バイトブロック */
		v |= ((size + 1) / 2) << 26;
	}
	out_w(PMA_RXBD(n), v);
}

/*======================================================================
 *  EP0 初期化 (バスリセット時、ISR から呼ぶ)
 *======================================================================*/

LOCAL void ll_setup_ep0(T_USB_LL_CB *p_cb)
{
	UINT n;

	/* EP1-7 を無効化 */
	for (n = 1; n < USB_MAX_EPS; n++) {
		chep_set_tx_status(n, CHEP_TX_DIS);
		chep_set_rx_status(n, CHEP_RX_DIS);
	}

	chep_set_type(0, CHEP_UTYPE_CONTROL);
	out_w(PMA_TXBD(0), PMA_TX_ADDR(0));
	pma_set_rx_size(0, USB_MAX_EP0_SIZE);
	chep_clear_dtog_tx(0);
	chep_clear_dtog_rx(0);
	chep_set_tx_status(0, CHEP_TX_NAK);
	chep_set_rx_status(0, CHEP_RX_VALID);

	p_cb->in_ep[0].max_pkt_size  = USB_MAX_EP0_SIZE;
	p_cb->out_ep[0].max_pkt_size = USB_MAX_EP0_SIZE;
	p_cb->in_ep[0].xfer_remain   = 0;
	p_cb->out_ep[0].xfer_buf     = NULL;
	p_cb->dev_addr = 0;

	/* アドレス 0 で応答開始 */
	out_w(USB_DADDR, DADDR_EF);
}

/*======================================================================
 *  HAL 関数
 *======================================================================*/

/*----------------------------------------------------------------------
 *  USB ペリフェラル初期化
 *    HSI48 (CRS で SOF 同期) を USB カーネルクロックにする
 */
EXPORT T_USB_LL_CB * dev_usb_hid_llinit(T_USB_INIT *p_init)
{
	T_USB_LL_CB *p_cb = &ll_devcb;
	volatile UW dly;

	memset(p_cb, 0, sizeof(T_USB_LL_CB));
	p_cb->base = USB_BASE;
	p_cb->init = *p_init;

	/* HSI48 起動 */
	out_w(RCC_CR_, in_w(RCC_CR_) | RCC_CR_HSI48ON);
	while (!(in_w(RCC_CR_) & RCC_CR_HSI48RDY)) {}

	/* USB カーネルクロック = HSI48 */
	out_w(RCC_CCIPR4_, (in_w(RCC_CCIPR4_) & ~RCC_CCIPR4_USBSEL_MASK) |
			   RCC_CCIPR4_USBSEL_HSI48);

	/* CRS: USB SOF (CFGR の既定値) で HSI48 を自動トリム */
	out_w(RCC_APB1LENR_, in_w(RCC_APB1LENR_) | RCC_APB1LENR_CRSEN);
	(void)in_w(RCC_APB1LENR_);
	out_w(CRS_CR_, in_w(CRS_CR_) | CRS_CR_AUTOTRIMEN | CRS_CR_CEN);

	/* VDDUSB 有効 (内部 3.3V 検出を省略して有効と宣言) */
	out_w(PWR_USBSCR_, in_w(PWR_USBSCR_) | PWR_USBSCR_USB33SV);

	/* USB ペリフェラルクロック */
	out_w(RCC_APB2ENR_, in_w(RCC_APB2ENR_) | RCC_APB2ENR_USBEN);
	(void)in_w(RCC_APB2ENR_);

	/* D+ プルアップ OFF のままリセット */
	out_w(USB_BCDR, 0);

	/* PDWN 解除 → トランシーバ起動待ち (tSTARTUP 1us) → リセット解除 */
	out_w(USB_CNTR, CNTR_USBRST);
	for (dly = 0; dly < 1000; dly++) {}
	out_w(USB_CNTR, 0);
	out_w(USB_ISTR, 0);

	/* デバイスモード + 割り込み許可 */
	out_w(USB_CNTR, CNTR_CTRM | CNTR_RESETM | CNTR_SUSPM | CNTR_WKUPM);

	return p_cb;
}

EXPORT ER dev_usb_hid_lldeinit(T_USB_LL_CB *p_cb)
{
	(void)p_cb;
	out_w(USB_BCDR, 0);
	out_w(USB_CNTR, CNTR_USBRST | CNTR_PDWN);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  D+ プルアップ制御
 */
EXPORT ER dev_usb_hid_connect(T_USB_LL_CB *p_cb)
{
	(void)p_cb;
	out_w(USB_BCDR, in_w(USB_BCDR) | BCDR_DPPU);
	return E_OK;
}

EXPORT ER dev_usb_hid_disconnect(T_USB_LL_CB *p_cb)
{
	(void)p_cb;
	out_w(USB_BCDR, in_w(USB_BCDR) & ~BCDR_DPPU);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  アドレス設定
 *    STATUS IN 完了後に DADDR へ反映する (ISR の EP0 IN 処理)
 */
EXPORT ER dev_usb_hid_set_addr(T_USB_LL_CB *p_cb, UB address)
{
	if (address == 0) {
		p_cb->dev_addr = 0;
		out_w(USB_DADDR, DADDR_EF);
	} else {
		p_cb->dev_addr = address;
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  EP 有効化 / 無効化
 *    EP0 はバスリセット時に ISR で設定済みなので何もしない。
 *    同じ番号の IN と OUT は 1 つの CHEP を共有する。
 */
EXPORT ER dev_usb_hid_ep_activate(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UINT n = ep->num;
	UW utype;
	UINT imask;

	(void)p_cb;
	if (n == 0 || n >= USB_MAX_EPS) return E_OK;

	switch (ep->type) {
	case USB_EP_TYPE_BULK:	utype = CHEP_UTYPE_BULK;	break;
	case USB_EP_TYPE_ISOC:	utype = CHEP_UTYPE_ISO;		break;
	case USB_EP_TYPE_CTRL:	utype = CHEP_UTYPE_CONTROL;	break;
	default:		utype = CHEP_UTYPE_INTERRUPT;	break;
	}

	DI(imask);
	chep_set_type(n, utype);
	if (ep->is_in) {
		out_w(PMA_TXBD(n), PMA_TX_ADDR(n));
		chep_clear_dtog_tx(n);
		chep_set_tx_status(n, CHEP_TX_NAK);
	} else {
		pma_set_rx_size(n, ep->max_pkt_size);
		chep_clear_dtog_rx(n);
		chep_set_rx_status(n, CHEP_RX_NAK);
	}
	EI(imask);

	ep->is_stall = 0;
	ep->xfer_buf = NULL;
	ep->xfer_remain = 0;
	return E_OK;
}

EXPORT ER dev_usb_hid_ep_deactivate(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UINT n = ep->num;
	UINT imask;

	(void)p_cb;
	if (n == 0 || n >= USB_MAX_EPS) return E_OK;

	DI(imask);
	if (ep->is_in) {
		chep_set_tx_status(n, CHEP_TX_DIS);
		chep_clear_dtog_tx(n);
	} else {
		chep_set_rx_status(n, CHEP_RX_DIS);
		chep_clear_dtog_rx(n);
	}
	EI(imask);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  受信開始 (1 パケット分を受け付ける)
 */
EXPORT ER dev_usb_hid_ep_recv(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UINT n = ep->num;
	UINT imask;

	(void)p_cb;
	DI(imask);
	pma_set_rx_size(n, (n == 0) ? USB_MAX_EP0_SIZE : ep->max_pkt_size);
	chep_set_rx_status(n, CHEP_RX_VALID);
	EI(imask);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  送信開始 (1 パケット)
 *    EP0 の後続パケットはミドルウェアが、EP1 以降は ISR が続きを送る
 */
EXPORT ER dev_usb_hid_ep_send(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UINT n = ep->num;
	UW len;
	UINT imask;

	(void)p_cb;
	len = ep->xfer_remain;
	if (len > ep->max_pkt_size) len = ep->max_pkt_size;
	if (len > PMA_BUF_SIZE) len = PMA_BUF_SIZE;

	DI(imask);
	if (len > 0 && ep->xfer_buf != NULL) {
		pma_write(PMA_TX_ADDR(n), ep->xfer_buf, len);
	}
	out_w(PMA_TXBD(n), PMA_TX_ADDR(n) | (len << 16));
	ep->xfer_req = len;
	ep->xfer_remain -= len;
	chep_set_tx_status(n, CHEP_TX_VALID);
	EI(imask);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  STALL 制御
 */
EXPORT ER dev_usb_hid_ep_stall(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UINT imask;

	(void)p_cb;
	DI(imask);
	if (ep->is_in)
		chep_set_tx_status(ep->num, CHEP_TX_STALL);
	else
		chep_set_rx_status(ep->num, CHEP_RX_STALL);
	EI(imask);
	return E_OK;
}

EXPORT ER dev_usb_hid_ep_unstall(T_USB_LL_CB *p_cb, T_USB_EP *ep)
{
	UINT n = ep->num;
	UINT imask;

	(void)p_cb;
	DI(imask);
	if (ep->is_in) {
		chep_clear_dtog_tx(n);
		chep_set_tx_status(n, CHEP_TX_NAK);
	} else {
		chep_clear_dtog_rx(n);
		chep_set_rx_status(n, (ep->xfer_buf != NULL) ? CHEP_RX_VALID : CHEP_RX_NAK);
	}
	EI(imask);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  リモートウェイクアップ (サスペンド中にホストを起こす)
 *    ホストが SET_FEATURE(DEVICE_REMOTE_WAKEUP) で許可した場合のみ。
 *    RESUME 信号は 1〜15ms 出す。自分で起こした場合は WKUP 割り込みが
 *    上がらないことがあるため、再開の通知もここで行う。
 */
EXPORT ER dev_usb_hid_remote_wakeup(void)
{
	T_USB_LL_CB *p_cb = &ll_devcb;
	T_USB_MW *p_mw = (T_USB_MW *)p_cb->p_mw;
	UINT imask;

	if (!p_cb->suspended) return E_OBJ;
	if (p_mw == NULL || !p_mw->dev_remote_wakeup) return E_NOSPT;

	DI(imask);
	out_w(USB_CNTR, (in_w(USB_CNTR) & ~(CNTR_SUSPEN | CNTR_SUSPRDY)) | CNTR_L2RES);
	EI(imask);
	tk_dly_tsk(5);
	DI(imask);
	out_w(USB_CNTR, in_w(USB_CNTR) & ~CNTR_L2RES);
	p_cb->suspended = 0;
	EI(imask);

	if (p_cb->usb_flgid > 0) tk_set_flg(p_cb->usb_flgid, USB_EVT_RESUME);
	return E_OK;
}

/*======================================================================
 *  割り込みハンドラ
 *======================================================================*/

/*
 *  EP 番号 → IN 完了イベント
 */
LOCAL UINT ep_in_event(UINT n)
{
	switch (n) {
	case 1:	return USB_EVT_EP1_IN;
	case 2:	return USB_EVT_EP2_IN;
	case 3:	return USB_EVT_EP3_IN;
	default: return 0;
	}
}

/*
 *  CTR (転送完了) 処理。evt に通知イベントを積む。
 *  新しい SETUP を受けたら、それ以前の EP0 IN/OUT 完了は前の制御転送の
 *  ものなので evt から落とし、*flush_ep0 を立てる (タスク側に溜まった
 *  分も inthdr で消す)。
 */
LOCAL void ll_ctr_handler(T_USB_LL_CB *p_cb, UINT n, UINT *pevt, BOOL *flush_ep0)
{
	UW r = in_w(USB_CHEP(n));
	UINT evt = *pevt;

	/* IN 完了 */
	if (r & CHEP_VTTX) {
		T_USB_EP *ep = &p_cb->in_ep[n];

		chep_clear_ctr_tx(n);
		if (ep->xfer_buf != NULL) ep->xfer_buf += ep->xfer_req;
		ep->xfer_done += ep->xfer_req;

		if (n == 0) {
			evt |= USB_EVT_EP0_IN;

			/* SET_ADDRESS の STATUS IN 完了後にアドレスを反映 */
			if (p_cb->dev_addr > 0 && ep->xfer_remain == 0) {
				out_w(USB_DADDR, DADDR_EF | p_cb->dev_addr);
				p_cb->dev_addr = 0;
			}
		} else if (ep->xfer_remain > 0) {
			dev_usb_hid_ep_send(p_cb, ep);
		} else {
			evt |= ep_in_event(n);
		}
	}

	/* OUT / SETUP 受信 */
	if (r & CHEP_VTRX) {
		T_USB_EP *ep = &p_cb->out_ep[n];
		UW cnt = pma_rx_count(n, (n == 0 && (r & CHEP_SETUP)) ? TRUE : FALSE);

		if (n == 0 && (r & CHEP_SETUP)) {
			/* SETUP ビットは CTR_RX クリアまで保持されるので先に読む */
			if (cnt == 8) {
				pma_read(p_cb->setup_shadow, PMA_RX_ADDR(0), 8);
			}
			chep_clear_ctr_rx(0);

			if (cnt == 8) {
				/* 前の制御転送の未送信分と STALL を解除して新しい転送を待つ */
				chep_set_tx_status(0, CHEP_TX_NAK);
				chep_set_rx_status(0, CHEP_RX_NAK);
				p_cb->in_ep[0].xfer_remain = 0;
				evt &= ~(USB_EVT_EP0_IN | USB_EVT_EP0_OUT);
				evt |= USB_EVT_SETUP;
				*flush_ep0 = TRUE;
			} else {
				chep_set_tx_status(0, CHEP_TX_STALL);
				chep_set_rx_status(0, CHEP_RX_STALL);
			}
			p_cb->suspended = 0;
		} else {
			chep_clear_ctr_rx(n);
			if (cnt > 0 && ep->xfer_buf != NULL) {
				if (n != 0 && cnt > ep->xfer_remain) cnt = ep->xfer_remain;
				pma_read(ep->xfer_buf, PMA_RX_ADDR(n), cnt);
				ep->xfer_buf += cnt;
				if (n != 0) ep->xfer_remain -= cnt;
			}
			ep->xfer_done = cnt;

			if (n == 0) {
				if (cnt > 0) {
					evt |= USB_EVT_EP0_OUT;
				} else {
					/* STATUS OUT 完了。ミドルウェアへの通知は不要 */
					chep_set_rx_status(0, CHEP_RX_VALID);
				}
			} else if (n == 2) {
				evt |= USB_EVT_EP2_OUT;
			}
		}
	}

	*pevt = evt;
}

/*----------------------------------------------------------------------
 *  USB 割り込みハンドラ
 *
 *  μT-Kernel HLL 割り込みハンドラ形式: void handler(UINT intno)
 */
EXPORT void usb_hid_inthdr(UINT intno)
{
	T_USB_LL_CB *p_cb = &ll_devcb;
	UW istr;
	UINT evt = 0;
	BOOL flush_ep0 = FALSE;

	/* 転送完了: 保留中の EP を全て処理 */
	while ((istr = in_w(USB_ISTR)) & ISTR_CTR) {
		ll_ctr_handler(p_cb, istr & ISTR_IDN_MASK, &evt, &flush_ep0);
	}

	/* 新しい SETUP より前の EP0 完了通知がタスク側に残っていれば捨てる */
	if (flush_ep0 && p_cb->usb_flgid > 0) {
		tk_clr_flg(p_cb->usb_flgid, ~(UINT)(USB_EVT_EP0_IN | USB_EVT_EP0_OUT));
	}

	/* バスリセット */
	if (istr & ISTR_RESET) {
		out_w(USB_ISTR, ~ISTR_RESET);
		p_cb->suspended = 0;
		ll_setup_ep0(p_cb);
		evt |= USB_EVT_BUS_RESET;
	}

	/* サスペンド: SUSPEN を立てるとバス再開時に WKUP が上がる */
	if (istr & ISTR_SUSP) {
		out_w(USB_CNTR, in_w(USB_CNTR) | CNTR_SUSPEN);
		out_w(USB_ISTR, ~ISTR_SUSP);
		if (!p_cb->suspended) {
			p_cb->suspended = 1;
			evt |= USB_EVT_SUSPEND;
		}
	}

	/* ウェイクアップ */
	if (istr & ISTR_WKUP) {
		out_w(USB_CNTR, in_w(USB_CNTR) & ~(CNTR_SUSPEN | CNTR_SUSPRDY));
		out_w(USB_ISTR, ~ISTR_WKUP);
		if (p_cb->suspended) {
			p_cb->suspended = 0;
			evt |= USB_EVT_RESUME;
		}
	}

	/* その他のフラグ (ERR/PMAOVR/SOF/ESOF) は捨てる */
	if (istr & (ISTR_ERR | ISTR_PMAOVR)) {
		out_w(USB_ISTR, ~(ISTR_ERR | ISTR_PMAOVR));
	}

	ClearInt(intno);

	/* タスクにイベント通知 */
	if (evt != 0 && p_cb->usb_flgid > 0) {
		tk_set_flg(p_cb->usb_flgid, evt);
	}
}

/*======================================================================
 *  ドライバブリッジ: ミドルウェア → HAL マッピング
 *  (usb_hid.c が呼ぶ共通インターフェース)
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

LOCAL T_USB_EP *ll_get_ep(T_USB_LL_CB *p_cb, UB ep_addr)
{
	UB epnum = ep_addr & 0x7F;

	if (epnum >= USB_MAX_EPS) return NULL;
	return (ep_addr & 0x80) ? &p_cb->in_ep[epnum] : &p_cb->out_ep[epnum];
}

EXPORT ER ll_open_ep(T_USB_MW *p_usb, UB ep_addr, UB ep_type, UH ep_mps)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = ll_get_ep(p_cb, ep_addr);

	if (ep == NULL) return E_PAR;
	ep->is_in = (ep_addr & 0x80) ? 1 : 0;
	ep->num = ep_addr & 0x7F;
	ep->type = ep_type;
	ep->max_pkt_size = ep_mps;
	ep->next_pid = 0;
	return dev_usb_hid_ep_activate(p_cb, ep);
}

EXPORT ER ll_close_ep(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = ll_get_ep(p_cb, ep_addr);

	if (ep == NULL) return E_PAR;
	return dev_usb_hid_ep_deactivate(p_cb, ep);
}

EXPORT ER ll_start_tx(T_USB_MW *p_usb, UB ep_addr, UB *buf, UW len)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = &p_cb->in_ep[ep_addr & 0x7F];

	ep->xfer_buf = buf;
	ep->xfer_remain = len;
	ep->xfer_done = 0;
	ep->is_in = 1;
	return dev_usb_hid_ep_send(p_cb, ep);
}

EXPORT ER ll_start_rx(T_USB_MW *p_usb, UB ep_num, UB *buf, UW len)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = &p_cb->out_ep[ep_num & 0x7F];

	ep->xfer_buf = buf;
	ep->xfer_remain = len;
	ep->xfer_done = 0;
	ep->is_in = 0;
	return dev_usb_hid_ep_recv(p_cb, ep);
}

EXPORT ER ll_set_addr(T_USB_MW *p_usb, UB address)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	return dev_usb_hid_set_addr(p_cb, address);
}

EXPORT ER ll_stall_ep(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = ll_get_ep(p_cb, ep_addr);

	if (ep == NULL) return E_PAR;
	ep->is_in = (ep_addr & 0x80) ? 1 : 0;
	ep->num = ep_addr & 0x7F;
	ep->is_stall = 1;
	return dev_usb_hid_ep_stall(p_cb, ep);
}

EXPORT ER ll_unstall_ep(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = ll_get_ep(p_cb, ep_addr);

	if (ep == NULL) return E_PAR;
	ep->is_stall = 0;
	return dev_usb_hid_ep_unstall(p_cb, ep);
}

EXPORT BOOL ll_is_stalled(T_USB_MW *p_usb, UB ep_addr)
{
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	T_USB_EP *ep = ll_get_ep(p_cb, ep_addr);

	return (ep != NULL) ? ep->is_stall : FALSE;
}

EXPORT BOOL ll_is_suspended(void)
{
	return ll_devcb.suspended ? TRUE : FALSE;
}

#endif	/* MTKBSP_CPU_STM32H5 */
