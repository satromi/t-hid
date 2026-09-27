/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    USB デバイスミドルウェア + 標準リクエスト処理 + mSDI ドライバ
 *    ISR は HW 処理のみ、プロトコル処理はタスクで実行
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(CPU_STM32H7) || defined(MTKBSP_CPU_STM32H5)

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "../include/dev_usb_hid.h"
#include "../common/drvif/msdrvif.h"
#include "usb_hid.h"

/* CONFIGURED 通知用イベントフラグ (mSDI セクションで定義、ここで前方宣言) */
LOCAL ID usb_hid_cfg_flgid;

/*======================================================================
 *  USB 標準リクエスト処理
 *======================================================================*/

#define USB_NUM_STD_REQ		13
#define USB_NUM_STR_DESC	7

LOCAL void req_get_status(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void req_clear_feature(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void req_set_feature(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void req_get_descriptor(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void req_set_address(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void req_get_config(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void req_set_config(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void ep_req_get_status(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void ep_req_clear_feature(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
LOCAL void ep_req_set_feature(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);

/*
 *  デバイスリクエスト関数テーブル
 */
LOCAL void (*req_handler_dev[USB_NUM_STD_REQ])(T_USB_MW *, T_USB_SETUP_REQ *) = {
	req_get_status,      req_clear_feature,   usb_ep0_stall, req_set_feature,
	usb_ep0_stall,       req_set_address,     req_get_descriptor, usb_ep0_stall,
	req_get_config, req_set_config, usb_ep0_stall, usb_ep0_stall,
	usb_ep0_stall
};

LOCAL void (*req_handler_ep[USB_NUM_STD_REQ])(T_USB_MW *, T_USB_SETUP_REQ *) = {
	ep_req_get_status,   ep_req_clear_feature, usb_ep0_stall, ep_req_set_feature,
	usb_ep0_stall,       usb_ep0_stall,        usb_ep0_stall, usb_ep0_stall,
	usb_ep0_stall,       usb_ep0_stall,        usb_ep0_stall, usb_ep0_stall,
	usb_ep0_stall
};

LOCAL UB *str_desc_table[USB_NUM_STR_DESC] = {
	USB_LANGID_DESC,
	USB_MANUFACTURER_STR,
	USB_PRODUCT_STR,
	USB_SERIAL_STR,
	USB_CONFIG_STR,
	USB_INTERFACE_STR,
	USB_USER_STR
};

/*----------------------------------------------------------------------*/

LOCAL void req_get_status(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	switch(p_usb->dev_state) {
	case USB_DEV_STATE_ADDRESSED:
	case USB_DEV_STATE_CONFIGURED:
		p_usb->dev_data[0] = USB_CFG_SELF_POWERED;
		p_usb->dev_data[1] = 0;
		if(p_usb->dev_remote_wakeup)
			p_usb->dev_data[0] |= USB_CFG_REMOTE_WAKEUP;
		usb_ep0_send(p_usb, p_usb->dev_data, 2);
		break;
	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

LOCAL void req_clear_feature(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	switch(p_usb->dev_state) {
	case USB_DEV_STATE_ADDRESSED:
	case USB_DEV_STATE_CONFIGURED:
		if(req->wValue == USB_FEATURE_REMOTE_WAKEUP) {
			p_usb->dev_remote_wakeup = 0;
			usb_hid_class_setup(p_usb, req);
			usb_ep0_send_status(p_usb);
		}
		break;
	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

LOCAL void req_set_feature(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	if(req->wValue == USB_FEATURE_REMOTE_WAKEUP) {
		p_usb->dev_remote_wakeup = 1;
		usb_hid_class_setup(p_usb, req);
		usb_ep0_send_status(p_usb);
	}
}

LOCAL void req_get_descriptor(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	UH	len;
	UB	*pbuf;

	switch(req->wValue >> 8) {
	case DEVICE_DESCRIPTOR:
		pbuf = USB_DEVICE_DESC;
		len  = USB_DEVICE_DESC[0];
		break;
	case CONFIGURATION_DESCRIPTOR:
		pbuf = USB_FS_CFG_DESC;
		len  = USB_FS_CFG_DESC_LEN;
		break;
	case STRING_DESCRIPTOR:
		if(((UB)req->wValue) < USB_NUM_STR_DESC) {
			pbuf = str_desc_table[(UB)req->wValue];
			if(pbuf == NULL) {
				usb_ep0_stall(p_usb, req);
				return;
			}
			len = pbuf[0];
		} else {
			usb_ep0_stall(p_usb, req);
			return;
		}
		break;
	case DEVICE_QUALIFIER_DESCRIPTOR:
		usb_ep0_stall(p_usb, req);
		return;
	default:
		usb_ep0_stall(p_usb, req);
		return;
	}

	if(len != 0 && req->wLength != 0) {
		if(len > req->wLength)
			len = req->wLength;
		usb_ep0_send(p_usb, pbuf, len);
	}
}

LOCAL void req_set_address(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	UB dev_addr;

	if(req->wIndex == 0 && req->wLength == 0) {
		dev_addr = (UB)(req->wValue) & 0x7F;
		if(p_usb->dev_state == USB_DEV_STATE_CONFIGURED) {
			usb_ep0_stall(p_usb, req);
		} else {
			p_usb->dev_address = dev_addr;
			ll_set_addr(p_usb, dev_addr);
			usb_ep0_send_status(p_usb);
			if(dev_addr != 0)
				p_usb->dev_state = USB_DEV_STATE_ADDRESSED;
			else
				p_usb->dev_state = USB_DEV_STATE_INIT;
		}
	} else {
		usb_ep0_stall(p_usb, req);
	}
}

LOCAL void req_get_config(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	if(req->wLength != 1) {
		usb_ep0_stall(p_usb, req);
		return;
	}
	switch(p_usb->dev_state) {
	case USB_DEV_STATE_ADDRESSED:
		p_usb->dev_data[0] = 0;
		usb_ep0_send(p_usb, p_usb->dev_data, 1);
		break;
	case USB_DEV_STATE_CONFIGURED:
		p_usb->dev_data[0] = p_usb->dev_config;
		usb_ep0_send(p_usb, p_usb->dev_data, 1);
		break;
	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

LOCAL void req_set_config(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	UB cfgidx = (UB)(req->wValue);

	if(cfgidx > USB_MW_MAX_CONFIG) {
		usb_ep0_stall(p_usb, req);
		return;
	}

	switch(p_usb->dev_state) {
	case USB_DEV_STATE_ADDRESSED:
		if(cfgidx) {
			p_usb->dev_config = cfgidx;
			p_usb->dev_state = USB_DEV_STATE_CONFIGURED;
			if(usb_hid_class_init(p_usb, cfgidx) != E_OK) {
				usb_ep0_stall(p_usb, req);
				return;
			}
			usb_ep0_send_status(p_usb);
			/* CONFIGURED イベント通知 — 待機中タスクを起床 */
			if(usb_hid_cfg_flgid > 0)
				tk_set_flg(usb_hid_cfg_flgid, USB_HID_EVT_CONFIGURED);
		} else {
			usb_ep0_send_status(p_usb);
		}
		break;
	case USB_DEV_STATE_CONFIGURED:
		if(cfgidx == 0) {
			p_usb->dev_state = USB_DEV_STATE_ADDRESSED;
			p_usb->dev_config = cfgidx;
			usb_hid_class_deinit(p_usb, cfgidx);
			usb_ep0_send_status(p_usb);
		} else if(cfgidx != p_usb->dev_config) {
			usb_hid_class_deinit(p_usb, cfgidx);
			p_usb->dev_config = cfgidx;
			if(usb_hid_class_init(p_usb, cfgidx) != E_OK) {
				usb_ep0_stall(p_usb, req);
				return;
			}
			usb_ep0_send_status(p_usb);
		} else {
			usb_ep0_send_status(p_usb);
		}
		break;
	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

LOCAL void ep_req_get_status(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	T_USB_MW_EP *pep;
	UB ep_addr = req->wIndex & 0xFF;

	switch(p_usb->dev_state) {
	case USB_DEV_STATE_ADDRESSED:
		if((ep_addr & 0x7F) != 0x00)
			ll_stall_ep(p_usb, ep_addr);
		break;
	case USB_DEV_STATE_CONFIGURED:
		pep = ((ep_addr & USB_DEVICE_TO_HOST) != 0) ?
			&p_usb->ep_in[ep_addr & 0x7F] :
			&p_usb->ep_out[ep_addr & 0x7F];
		if(ll_is_stalled(p_usb, ep_addr))
			pep->status = 0x0001;
		else
			pep->status = 0x0000;
		usb_ep0_send(p_usb, (UB *)&pep->status, 2);
		break;
	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

LOCAL void ep_req_clear_feature(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	UB ep_addr = req->wIndex & 0xFF;

	switch(p_usb->dev_state) {
	case USB_DEV_STATE_ADDRESSED:
		if((ep_addr & 0x7F) != 0x00)
			ll_stall_ep(p_usb, ep_addr);
		break;
	case USB_DEV_STATE_CONFIGURED:
		if(req->wValue == USB_FEATURE_EP_HALT) {
			if((ep_addr & 0x7F) != 0x00) {
				ll_unstall_ep(p_usb, ep_addr);
				usb_hid_class_setup(p_usb, req);
			}
			usb_ep0_send_status(p_usb);
		}
		break;
	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

LOCAL void ep_req_set_feature(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	UB ep_addr = req->wIndex & 0xFF;

	switch(p_usb->dev_state) {
	case USB_DEV_STATE_ADDRESSED:
		if((ep_addr & 0x7F) != 0x00)
			ll_stall_ep(p_usb, ep_addr);
		break;
	case USB_DEV_STATE_CONFIGURED:
		if(req->wValue == USB_FEATURE_EP_HALT) {
			if((ep_addr & 0x7F) != 0x00)
				ll_stall_ep(p_usb, ep_addr);
		}
		usb_hid_class_setup(p_usb, req);
		usb_ep0_send_status(p_usb);
		break;
	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

/*======================================================================
 *  ミドルウェア初期化・制御
 *======================================================================*/

EXPORT ER usb_hid_mw_init(T_USB_MW *p_usb, UB id)
{
	if(p_usb == NULL) return E_SYS;
	p_usb->dev_state = USB_DEV_STATE_INIT;
	p_usb->id = id;
	return ll_drv_init(p_usb);
}

EXPORT ER usb_hid_mw_deinit(T_USB_MW *p_usb)
{
	p_usb->dev_state = USB_DEV_STATE_RESET;
	usb_hid_class_deinit(p_usb, p_usb->dev_config);
	return ll_drv_deinit(p_usb);
}

EXPORT ER usb_hid_mw_start(T_USB_MW *p_usb)
{
	p_usb->ep0_state = USB_EP0_STATE_IDLE;
	return ll_drv_start(p_usb);
}

EXPORT ER usb_hid_mw_stop(T_USB_MW *p_usb)
{
	usb_hid_class_deinit(p_usb, p_usb->dev_config);
	ll_drv_stop(p_usb);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  SETUP ステージ処理
 */
EXPORT ER usb_hid_on_setup(T_USB_MW *p_usb, UB *psetup)
{
	T_USB_SETUP_REQ *req = &p_usb->request;
	volatile UB *p = psetup;

	req->bmRequest = p[0];
	req->bRequest  = p[1];
	req->wValue    = p[2] | (p[3] << 8);
	req->wIndex    = p[4] | (p[5] << 8);
	req->wLength   = p[6] | (p[7] << 8);

	p_usb->ep0_state    = USB_EP0_STATE_SETUP;
	p_usb->ep0_data_len = p_usb->request.wLength;

	switch(p_usb->request.bmRequest & 0x1F) {
	case USB_RECIPIENT_DEVICE:
		if(req->bRequest < USB_NUM_STD_REQ)
			req_handler_dev[req->bRequest](p_usb, req);
		else
			usb_ep0_stall(p_usb, req);
		break;
	case USB_RECIPIENT_INTERFACE:
		if(p_usb->dev_state == USB_DEV_STATE_CONFIGURED) {
			if((req->wIndex & 0xFF) < USB_HID_NUM_IF) {
				usb_hid_class_setup(p_usb, req);
				if(req->wLength == 0)
					usb_ep0_send_status(p_usb);
			} else {
				usb_ep0_stall(p_usb, req);
			}
		} else {
			usb_ep0_stall(p_usb, req);
		}
		break;
	case USB_RECIPIENT_ENDPOINT:
		if((req->bmRequest & USB_REQUEST_TYPE_MASK) == USB_REQUEST_TYPE_CLASS)
			usb_hid_class_setup(p_usb, req);
		else {
			if(req->bRequest < USB_NUM_STD_REQ)
				req_handler_ep[req->bRequest](p_usb, req);
			else
				usb_ep0_stall(p_usb, req);
		}
		break;
	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  DATA OUT ステージ処理
 */
EXPORT ER usb_hid_on_data_out(T_USB_MW *p_usb, UB epnum, UB *pdata)
{
	T_USB_MW_EP *pep;

	if(epnum == 0) {
		pep = &p_usb->ep_out[0];
		if(p_usb->ep0_state == USB_EP0_STATE_DATAOUT) {
			if(pep->xfer_remain > pep->max_pkt_size) {
				pep->xfer_remain -= pep->max_pkt_size;
				if(pep->xfer_remain < pep->max_pkt_size)
					usb_ep0_continue_rx(p_usb, pdata, pep->xfer_remain);
				else
					usb_ep0_continue_rx(p_usb, pdata, pep->max_pkt_size);
			} else {
				if(p_usb->dev_state == USB_DEV_STATE_CONFIGURED)
					usb_class_ep0_rx_ready(p_usb);
				usb_ep0_send_status(p_usb);
			}
		}
	} else if(p_usb->dev_state == USB_DEV_STATE_CONFIGURED) {
		usb_class_data_out(p_usb, epnum);
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  DATA IN ステージ処理
 */
EXPORT ER usb_hid_on_data_in(T_USB_MW *p_usb, UB epnum, UB *pdata)
{
	T_USB_MW_EP *pep;

	if(epnum == 0) {
		pep = &p_usb->ep_in[0];
		if(p_usb->ep0_state == USB_EP0_STATE_DATAIN) {
			if(pep->xfer_remain > pep->max_pkt_size) {
				pep->xfer_remain -= pep->max_pkt_size;
				usb_ep0_continue_tx(p_usb, pdata, pep->xfer_remain);
				ll_start_rx(p_usb, 0, NULL, 0);
			} else {
				if((pep->xfer_size % pep->max_pkt_size) == 0 &&
				    pep->xfer_size >= pep->max_pkt_size &&
				    pep->xfer_size < p_usb->ep0_data_len) {
					usb_ep0_continue_tx(p_usb, NULL, 0);
					p_usb->ep0_data_len = 0;
					ll_start_rx(p_usb, 0, NULL, 0);
				} else {
					if(p_usb->dev_state == USB_DEV_STATE_CONFIGURED)
						usb_class_ep0_tx_sent(p_usb);
					usb_ep0_recv_status(p_usb);
				}
			}
		}
	} else if(p_usb->dev_state == USB_DEV_STATE_CONFIGURED) {
		usb_hid_class_data_in(p_usb, epnum);
	}
	return E_OK;
}

/*----------------------------------------------------------------------
 *  RESET 処理
 */
EXPORT ER usb_hid_on_reset(T_USB_MW *p_usb)
{
	ll_open_ep(p_usb, 0x00, USB_EP_TYPE_CTRL, USB_MAX_EP0_SIZE);
	p_usb->ep_out[0].max_pkt_size = USB_MAX_EP0_SIZE;
	ll_open_ep(p_usb, 0x80, USB_EP_TYPE_CTRL, USB_MAX_EP0_SIZE);
	p_usb->ep_in[0].max_pkt_size = USB_MAX_EP0_SIZE;
	p_usb->dev_state = USB_DEV_STATE_INIT;
	return usb_hid_class_deinit(p_usb, p_usb->dev_config);
}

EXPORT ER usb_hid_set_speed(T_USB_MW *p_usb, UB speed)
{
	p_usb->dev_speed = speed & 3;
	return E_OK;
}

EXPORT ER usb_hid_on_suspend(T_USB_MW *p_usb)
{
	/*
	 *  RESUME を挟まず SUSPEND が続いた場合に退避値を壊さない。
	 *  上書きすると dev_old_state が SUSPENDED になり、以降
	 *  RESUME しても状態が戻らず、インタフェース宛リクエストが
	 *  すべて STALL されるようになる。
	 */
	if(p_usb->dev_state != USB_DEV_STATE_SUSPENDED)
		p_usb->dev_old_state = p_usb->dev_state;
	p_usb->dev_state = USB_DEV_STATE_SUSPENDED;
	usb_class_suspend(p_usb);
	return E_OK;
}

EXPORT ER usb_hid_on_resume(T_USB_MW *p_usb)
{
	p_usb->dev_state = p_usb->dev_old_state;
	usb_class_resume(p_usb);
	return E_OK;
}

/*----------------------------------------------------------------------
 *  ディスクリプタ検索
 */
EXPORT UB * usb_find_desc(T_USB_MW *p_usb, UB type)
{
	UH	wTotalLength;
	UB	*cptr = USB_FS_CFG_DESC;
	UB	*dptr;

	(void)p_usb;

	if(cptr == NULL) return NULL;
	if(cptr[0] != CONFIGURATION_DESCRIPTOR_LENGTH ||
	    cptr[1] != CONFIGURATION_DESCRIPTOR)
		return NULL;

	wTotalLength = cptr[2] | (cptr[3] << 8);
	if(wTotalLength <= (CONFIGURATION_DESCRIPTOR_LENGTH + 2))
		return NULL;

	dptr = cptr + CONFIGURATION_DESCRIPTOR_LENGTH;
	do {
		/* Bounds check: descriptor length must be >= 2 to prevent infinite loop */
		if(dptr[0] < 2)
			return NULL;
		if(dptr[1] == type)
			return dptr;
		dptr += dptr[0];
	} while(dptr < (cptr + wTotalLength));
	return NULL;
}

/*======================================================================
 *  EP0 コントロール転送
 *======================================================================*/

/*
 *  コントロール DATA IN 送信
 */
EXPORT ER usb_ep0_send(T_USB_MW *p_usb, UB *buf, UH len)
{
	p_usb->ep0_state          = USB_EP0_STATE_DATAIN;
	p_usb->ep_in[0].xfer_size  = len;
	p_usb->ep_in[0].xfer_remain = len;
	return ll_start_tx(p_usb, 0x00, buf, len);
}

/*
 *  コントロール DATA OUT 受信開始
 */
EXPORT ER usb_ep0_recv(T_USB_MW *p_usb, UB *buf, UH len)
{
	p_usb->ep0_state = USB_EP0_STATE_DATAOUT;
	p_usb->ep_out[0].xfer_size  = len;
	p_usb->ep_out[0].xfer_remain = len;
	return ll_start_rx(p_usb, 0, buf, len);
}

/*
 *  コントロール STATUS IN 送信
 */
EXPORT ER usb_ep0_send_status(T_USB_MW *p_usb)
{
	p_usb->ep0_state = USB_EP0_STATE_STATUSIN;
	return ll_start_tx(p_usb, 0x00, NULL, 0);
}

/*
 *  コントロール STATUS OUT 受信
 */
EXPORT ER usb_ep0_recv_status(T_USB_MW *p_usb)
{
	p_usb->ep0_state = USB_EP0_STATE_STATUSOUT;
	return ll_start_rx(p_usb, 0, NULL, 0);
}

/*
 *  コントロール STALL 送信
 */
EXPORT void usb_ep0_stall(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	(void)req;
	ll_stall_ep(p_usb, 0x80);
	ll_stall_ep(p_usb, 0x00);
}

/*======================================================================
 *  USB 処理タスク
 *
 *  ISR からイベントフラグで通知を受け、プロトコル処理を実行
 *======================================================================*/

LOCAL void usb_hid_task(INT stacd, void *exinf)
{
	T_USB_MW *p_usb = (T_USB_MW *)exinf;
	T_USB_LL_CB *p_cb = (T_USB_LL_CB *)p_usb->p_ll;
	UINT	flgptn;
	ER	err;

	(void)stacd;

	while(1) {
		err = tk_wai_flg(p_cb->usb_flgid, USB_EVT_ANY,
				 TWF_ORW | TWF_BITCLR, &flgptn, TMO_FEVR);
		if(err != E_OK) continue;

		/* BUS_RESET — 最優先 (状態リセット) */
		if(flgptn & USB_EVT_BUS_RESET) {
			usb_hid_set_speed(p_usb, USB_DEVICE_SPEED_FULL);
			usb_hid_on_reset(p_usb);
		}

		/*
		 *  RESUME
		 *  同時にバスリセットが来ていれば状態は INIT から始め直すので、
		 *  サスペンド前の状態 (CONFIGURED など) に戻さない。
		 */
		if((flgptn & USB_EVT_RESUME) && !(flgptn & USB_EVT_BUS_RESET)) {
			usb_hid_on_resume(p_usb);
		}

		/* SETUP */
		if(flgptn & USB_EVT_SETUP) {
			if(p_usb->dev_state == USB_DEV_STATE_SUSPENDED)
				usb_hid_on_resume(p_usb);
			usb_hid_on_setup(p_usb, p_cb->setup_shadow);
		}

		/*
		 *  EP0 IN 完了
		 *
		 *  64 バイトを超える転送では残りを継続送信する。
		 *  HAL 側が ISR で xfer_buf を送信済み分だけ進めているので、
		 *  その位置を渡して続きから送る。
		 */
		if(flgptn & USB_EVT_EP0_IN) {
			usb_hid_on_data_in(p_usb, 0, p_cb->in_ep[0].xfer_buf);
		}

		/* EP0 OUT 完了 */
		if(flgptn & USB_EVT_EP0_OUT) {
			usb_hid_on_data_out(p_usb, 0, p_cb->out_ep[0].xfer_buf);
		}

		/* EP1 IN 完了 (HID レポート送信完了) */
		if(flgptn & USB_EVT_EP1_IN) {
			usb_hid_on_data_in(p_usb, 1, NULL);
		}

#if defined(USB_HID_FIDO)
		/* EP2 (FIDO CTAPHID) */
		if(flgptn & USB_EVT_EP2_IN) {
			usb_hid_on_data_in(p_usb, 2, NULL);
		}
		if(flgptn & USB_EVT_EP2_OUT) {
			usb_hid_on_data_out(p_usb, 2, NULL);
		}
#endif

#if defined(USB_HID_GAMEPAD)
		/* EP3 IN 完了 (ゲームパッドレポート送信完了) */
		if(flgptn & USB_EVT_EP3_IN) {
			usb_hid_on_data_in(p_usb, 3, NULL);
		}
#endif

		/* SUSPEND */
		if(flgptn & USB_EVT_SUSPEND) {
			usb_hid_on_suspend(p_usb);
		}
	}
}

/*======================================================================
 *  mSDI デバイスドライバ ("usbk")
 *======================================================================*/

/*
 *  ドライバ制御ブロック
 */
typedef struct {
	UW	unit;
	UINT	omode;
	ID	evtmbfid;
	BOOL	configured;
} T_USB_HID_DCB;

LOCAL T_USB_HID_DCB	dev_usb_hid_cb;
LOCAL T_USB_MW		usb_hid_mw;
EXPORT ID usb_hid_get_cfg_flgid(void)
{
	return usb_hid_cfg_flgid;
}

/*
 *  外部参照: ディスクリプタ初期化
 */
extern void usb_desc_init(void);

/*----------------------------------------------------------------------
 *  mSDI コールバック
 */
LOCAL ER dev_usb_hid_openfn(ID devid, UINT omode, T_MSDI *p_msdi)
{
	T_USB_HID_DCB *p = (T_USB_HID_DCB *)p_msdi->dmsdi.exinf;
	(void)devid;
	p->omode = omode;
	return E_OK;
}

LOCAL ER dev_usb_hid_closefn(ID devid, UINT omode, T_MSDI *p_msdi)
{
	(void)devid; (void)omode; (void)p_msdi;
	return E_OK;
}

LOCAL INT dev_usb_hid_readfn(T_DEVREQ *req, T_MSDI *p_msdi)
{
	(void)req; (void)p_msdi;
	return E_NOSPT;
}

LOCAL INT dev_usb_hid_writefn(T_DEVREQ *req, T_MSDI *p_msdi)
{
	T_USB_HID_DCB *p_dcb = (T_USB_HID_DCB *)(p_msdi->dmsdi.exinf);
	T_USB_HID_KBD_REPORT *report;
	ER	rc;

	(void)p_dcb;

#if defined(USB_HID_GAMEPAD)
	if(req->start == USB_HID_DN_PAD) {
		if(req->size < (W)sizeof(T_USB_HID_PAD_REPORT)) {
			req->asize = 0;
			return E_PAR;
		}
		if(usb_hid_mw.dev_state != USB_DEV_STATE_CONFIGURED) {
			req->asize = 0;
			return E_IO;
		}
		rc = usb_hid_send_pad_report(&usb_hid_mw, (const UB *)req->buf,
					     sizeof(T_USB_HID_PAD_REPORT));
		if(rc != E_OK) {
			req->asize = 0;
			return rc;
		}
		req->asize = sizeof(T_USB_HID_PAD_REPORT);
		return E_OK;
	}
#endif

	if(req->start == USB_HID_DN_KBD) {
		if(req->size < (W)sizeof(T_USB_HID_KBD_REPORT)) {
			req->asize = 0;
			return E_PAR;
		}
		if(usb_hid_mw.dev_state != USB_DEV_STATE_CONFIGURED) {
			req->asize = 0;
			return E_IO;
		}
		report = (T_USB_HID_KBD_REPORT *)req->buf;
		rc = usb_hid_send_report(&usb_hid_mw,
				       (UB *)report,
				       sizeof(T_USB_HID_KBD_REPORT));
		if(rc != E_OK) {
			req->asize = 0;
			return (rc == E_BUSY) ? E_BUSY : E_IO;
		}
		req->asize = sizeof(T_USB_HID_KBD_REPORT);
		return E_OK;
	}
	return E_NOSPT;
}

LOCAL INT dev_usb_hid_eventfn(INT evttyp, void *evtinf, T_MSDI *p_msdi)
{
	(void)evttyp; (void)evtinf; (void)p_msdi;
	return E_NOSPT;
}

/*----------------------------------------------------------------------
 *  USB デバイスドライバ初期化・登録
 *
 *  knl_start_device() (devinit.c) から呼ばれる
 *  ISR + タスク方式: ISR は HW 処理のみ、プロトコルはタスクで実行
 */
EXPORT ER dev_init_usb_hid(UW unit)
{
	T_USB_HID_DCB	*p_dcb;
	T_DMSDI		dmsdi;
	T_IDEV		idev;
	T_MSDI		*p_msdi;
	T_USB_LL_CB	*p_cb;
	T_USB_INIT	usb_init;
	ER		err;
	ER		rc;

	p_dcb = &dev_usb_hid_cb;
	memset(p_dcb, 0, sizeof(T_USB_HID_DCB));
	p_dcb->unit = unit;

	/*
	 *  mSDI デバイス登録
	 */
	strcpy((char *)dmsdi.devnm, USB_HID_DEVNM);
	dmsdi.exinf   = p_dcb;
	dmsdi.drvatr  = 0;
	dmsdi.devatr  = TDK_UNDEF;
	dmsdi.nsub    = 0;
	dmsdi.blksz   = 1;
	dmsdi.openfn  = dev_usb_hid_openfn;
	dmsdi.closefn = dev_usb_hid_closefn;
	dmsdi.readfn  = dev_usb_hid_readfn;
	dmsdi.writefn = dev_usb_hid_writefn;
	dmsdi.eventfn = dev_usb_hid_eventfn;

	err = msdi_def_dev(&dmsdi, &idev, &p_msdi);
	if(err != E_OK) return err;
	p_dcb->evtmbfid = idev.evtmbfid;

	/*
	 *  ディスクリプタ初期化 (文字列 → Unicode 変換)
	 */
	usb_desc_init();

	/*
	 *  USB HAL 初期化
	 */
	usb_init.num_eps = 16;
	usb_init.speed         = USB_SPEED_FULL;
	usb_init.sof_enable    = 0;
	p_cb = dev_usb_hid_llinit(&usb_init);
	if(p_cb == NULL) {
		msdi_del_dev(p_msdi);
		return E_IO;
	}

	/*
	 *  USB ミドルウェア初期化
	 */
	memset(&usb_hid_mw, 0, sizeof(T_USB_MW));
	usb_hid_mw.p_ll = p_cb;
	rc = usb_hid_mw_init(&usb_hid_mw, 0);
	if(rc != E_OK) {
		msdi_del_dev(p_msdi);
		return E_IO;
	}

	/*
	 *  EP データバッファ設定 (HAL 固有 — dev_usb_hid_llinit 内で設定済み)
	 */

	/*
	 *  イベントフラグ生成 (ISR → タスク通信用)
	 */
	{
		T_CFLG cflg;
		cflg.exinf   = NULL;
		cflg.flgatr  = TA_TPRI | TA_WMUL;
		cflg.iflgptn = 0;
		p_cb->usb_flgid = tk_cre_flg(&cflg);
		if(p_cb->usb_flgid <= 0) {
			msdi_del_dev(p_msdi);
			return E_NOMEM;
		}
	}

	/*
	 *  CONFIGURED 通知用イベントフラグ生成 (アプリ向け)
	 */
	{
		T_CFLG cflg;
		cflg.exinf   = NULL;
		cflg.flgatr  = TA_TPRI | TA_WMUL;
		cflg.iflgptn = 0;
		usb_hid_cfg_flgid = tk_cre_flg(&cflg);
	}

	/*
	 *  USB 処理タスク生成・起動
	 */
	{
		T_CTSK ctsk;
		ID tskid;

		ctsk.exinf   = &usb_hid_mw;
		ctsk.tskatr  = TA_HLNG | TA_RNG3;
		ctsk.task    = (FP)usb_hid_task;
		ctsk.itskpri = USB_TASK_PRI;
		ctsk.stksz   = USB_TASK_STKSZ;
		ctsk.bufptr  = NULL;
		tskid = tk_cre_tsk(&ctsk);
		if(tskid <= 0) {
			tk_del_flg(p_cb->usb_flgid);
			msdi_del_dev(p_msdi);
			return E_NOMEM;
		}
		tk_sta_tsk(tskid, 0);
	}

	/*
	 *  割り込みハンドラ登録 (SER/I2C ドライバと同じパターン)
	 */
	{
		T_DINT dint;
		dint.intatr = TA_HLNG;
		dint.inthdr = usb_hid_inthdr;
		err = tk_def_int(INTNO_USBCTRL, &dint);
		if(err != E_OK) {
			msdi_del_dev(p_msdi);
			return err;
		}
		EnableInt(INTNO_USBCTRL, INTPRI_USBCTRL);
	}

	/*
	 *  USB 開始 (D+ プルアップ有効)
	 */
	rc = usb_hid_mw_start(&usb_hid_mw);
	if(rc != E_OK) {
		msdi_del_dev(p_msdi);
		return E_IO;
	}

	tm_printf((UB *)"USB HID Keyboard ready\n");
	return E_OK;
}

/*----------------------------------------------------------------------
 *  USB D+ プルアップ有効化 (再接続用)
 */
EXPORT ER usb_hid_connect(void)
{
	return usb_hid_mw_start(&usb_hid_mw);
}

#endif	/* CPU_RP2040 || CPU_STM32H7 || MTKBSP_CPU_STM32H5 */
