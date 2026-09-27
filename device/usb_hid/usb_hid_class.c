/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    HID Keyboard クラス実装 + ディスクリプタ定義
 *    6KRO Boot Protocol Keyboard
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(CPU_STM32H7) || defined(MTKBSP_CPU_STM32H5)

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <string.h>
#include "usb_hid.h"
#include "../../app_program/keyboard/kb_config.h"

/*======================================================================
 *  USB ディスクリプタ定義
 *======================================================================*/

/*
 *  HID Report Descriptor (6KRO Boot Protocol Keyboard)
 */
LOCAL UB reportDescriptor[] __attribute__((aligned(USB_DATA_ALIGN))) = {
	0x05, 0x01,	/* Usage Page (Generic Desktop) */
	0x09, 0x06,	/* Usage (Keyboard) */
	0xA1, 0x01,	/* Collection (Application) */
	0x05, 0x07,	/*   Usage Page (Key Codes) */
	0x19, 0xE0,	/*   Usage Minimum (224) */
	0x29, 0xE7,	/*   Usage Maximum (231) */
	0x15, 0x00,	/*   Logical Minimum (0) */
	0x25, 0x01,	/*   Logical Maximum (1) */
	0x75, 0x01,	/*   Report Size (1) */
	0x95, 0x08,	/*   Report Count (8) */
	0x81, 0x02,	/*   Input (Data, Variable, Absolute) - Modifier keys */
	0x95, 0x01,	/*   Report Count (1) */
	0x75, 0x08,	/*   Report Size (8) */
	0x81, 0x01,	/*   Input (Constant) - Reserved byte */
	0x95, 0x05,	/*   Report Count (5) */
	0x75, 0x01,	/*   Report Size (1) */
	0x05, 0x08,	/*   Usage Page (LEDs) */
	0x19, 0x01,	/*   Usage Minimum (Num Lock) */
	0x29, 0x05,	/*   Usage Maximum (Kana) */
	0x91, 0x02,	/*   Output (Data, Variable, Absolute) - LED report */
	0x95, 0x01,	/*   Report Count (1) */
	0x75, 0x03,	/*   Report Size (3) */
	0x91, 0x01,	/*   Output (Constant) - LED padding */
	/*
	 *  キーコード配列。Usage 範囲を 255 まで宣言する。
	 *  101 までに制限すると International1-5 (0x87-0x8B) が範囲外に
	 *  なり、JIS の Ro (0x87) と Yen (0x89) をホストが無視する。
	 *
	 *  Logical Minimum はグローバル項目で、上の修飾キー部で 0 に
	 *  設定したものがここまで有効なため再指定しない。これにより
	 *  全体が 63 バイトとなり EP0 の 1 パケット (64) に収まる。
	 */
	0x95, 0x06,		/*   Report Count (6) */
	0x75, 0x08,		/*   Report Size (8) */
	0x26, 0xFF, 0x00,	/*   Logical Maximum (255) */
	0x05, 0x07,		/*   Usage Page (Key Codes) */
	0x19, 0x00,		/*   Usage Minimum (0) */
	0x2A, 0xFF, 0x00,	/*   Usage Maximum (255) */
	0x81, 0x00,	/*   Input (Data, Array) - Key codes */
	0xC0		/* End Collection */
};

#if defined(USB_HID_FIDO)
/*
 *  FIDO HID Report Descriptor (CTAPHID, 64 バイトの入出力レポート)
 */
#define FIDO_REPORT_DESC_LEN	34

LOCAL UB fidoReportDescriptor[FIDO_REPORT_DESC_LEN]
			__attribute__((aligned(USB_DATA_ALIGN))) = {
	0x06, 0xD0, 0xF1,	/* Usage Page (FIDO Alliance) */
	0x09, 0x01,		/* Usage (CTAPHID) */
	0xA1, 0x01,		/* Collection (Application) */
	0x09, 0x20,		/*   Usage (Input Report Data) */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x26, 0xFF, 0x00,	/*   Logical Maximum (255) */
	0x75, 0x08,		/*   Report Size (8) */
	0x95, 0x40,		/*   Report Count (64) */
	0x81, 0x02,		/*   Input (Data, Variable, Absolute) */
	0x09, 0x21,		/*   Usage (Output Report Data) */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x26, 0xFF, 0x00,	/*   Logical Maximum (255) */
	0x75, 0x08,		/*   Report Size (8) */
	0x95, 0x40,		/*   Report Count (64) */
	0x91, 0x02,		/*   Output (Data, Variable, Absolute) */
	0xC0			/* End Collection */
};
#endif

#if defined(USB_HID_GAMEPAD)
/*
 *  Gamepad HID Report Descriptor (T_USB_HID_PAD_REPORT, 9 バイト)
 *
 *  ボタンは 1 ビットずつの可変フィールド、軸は符号付き 8 ビットの絶対値、
 *  ハットは 0..7 で範囲外 (8) を中立とする (Null State)。汎用の HID
 *  ゲームパッドとして読めるように、ボタン・ハット・軸を 1 つのレポートに
 *  入れ、レポート ID は付けない。
 */
LOCAL UB padReportDescriptor[] __attribute__((aligned(USB_DATA_ALIGN))) = {
	0x05, 0x01,		/* Usage Page (Generic Desktop) */
	0x09, 0x05,		/* Usage (Gamepad) */
	0xA1, 0x01,		/* Collection (Application) */
	0x05, 0x09,		/*   Usage Page (Button) */
	0x19, 0x01,		/*   Usage Minimum (1) */
	0x29, 0x20,		/*   Usage Maximum (32) */
	0x15, 0x00,		/*   Logical Minimum (0) */
	0x25, 0x01,		/*   Logical Maximum (1) */
	0x75, 0x01,		/*   Report Size (1) */
	0x95, 0x20,		/*   Report Count (32) */
	0x81, 0x02,		/*   Input (Data, Variable, Absolute) - Buttons */
	0x05, 0x01,		/*   Usage Page (Generic Desktop) */
	0x09, 0x39,		/*   Usage (Hat switch) */
	0x25, 0x07,		/*   Logical Maximum (7) */
	0x35, 0x00,		/*   Physical Minimum (0) */
	0x46, 0x3B, 0x01,	/*   Physical Maximum (315) */
	0x65, 0x14,		/*   Unit (Degrees) */
	0x75, 0x04,		/*   Report Size (4) */
	0x95, 0x01,		/*   Report Count (1) */
	0x81, 0x42,		/*   Input (Data, Variable, Absolute, Null) - Hat */
	0x45, 0x00,		/*   Physical Maximum (0): 以降は論理値のまま */
	0x65, 0x00,		/*   Unit (None) */
	0x81, 0x03,		/*   Input (Constant) - Hat padding (4 bits) */
	0x09, 0x30,		/*   Usage (X) */
	0x09, 0x31,		/*   Usage (Y) */
	0x09, 0x32,		/*   Usage (Z) */
	0x09, 0x35,		/*   Usage (Rz) */
	0x15, 0x81,		/*   Logical Minimum (-127) */
	0x25, 0x7F,		/*   Logical Maximum (127) */
	0x75, 0x08,		/*   Report Size (8) */
	0x95, 0x04,		/*   Report Count (4) */
	0x81, 0x02,		/*   Input (Data, Variable, Absolute) - Axes */
	0xC0			/* End Collection */
};
#endif

/*
 *  HID Configuration Descriptor
 */
EXPORT UB usb_hid_config_desc[USB_HID_CFG_DESC_LEN]
			__attribute__((aligned(USB_DATA_ALIGN))) = {
	/* Configuration Descriptor */
	CONFIGURATION_DESCRIPTOR_LENGTH,
	CONFIGURATION_DESCRIPTOR,
	(USB_HID_CFG_DESC_LEN & 0xFF),
	(USB_HID_CFG_DESC_LEN >> 8),
	USB_HID_NUM_IF,				/* bNumInterfaces */
	0x01,					/* bConfigurationValue */
	0x00,					/* iConfiguration */
	C_RESERVED | C_REMOTE_WAKEUP,		/* bmAttributes */
	C_POWER(100),				/* bMaxPower (100mA) */

	/* Interface Descriptor */
	INTERFACE_DESCRIPTOR_LENGTH,
	INTERFACE_DESCRIPTOR,
	0x00,					/* bInterfaceNumber */
	0x00,					/* bAlternateSetting */
	0x01,					/* bNumEndpoints */
	HID_CLASS,				/* bInterfaceClass */
	0x01,					/* bInterfaceSubClass (Boot) */
	0x01,					/* bInterfaceProtocol (Keyboard) */
	0x00,					/* iInterface */

	/* HID Descriptor */
	HID_DESCRIPTOR_LENGTH,
	HID_DESCRIPTOR,
	0x11, 0x01,				/* bcdHID (1.11) */
	0x00,					/* bCountryCode */
	0x01,					/* bNumDescriptors */
	HID_REPORT_DESCRIPTOR,			/* bDescriptorType */
	(sizeof(reportDescriptor) & 0xFF),
	(sizeof(reportDescriptor) >> 8),

	/* Endpoint Descriptor (IN) */
	ENDPOINT_DESCRIPTOR_LENGTH,
	ENDPOINT_DESCRIPTOR,
	USB_HID_EP_IN,				/* bEndpointAddress */
	USB_EP_TYPE_INTR,			/* bmAttributes */
	(USB_HID_EP_SIZE & 0xFF),
	(USB_HID_EP_SIZE >> 8),
	USB_HID_POLL_MS,			/* bInterval */

#if defined(USB_HID_FIDO)
	/* Interface Descriptor (FIDO) */
	INTERFACE_DESCRIPTOR_LENGTH,
	INTERFACE_DESCRIPTOR,
	USB_FIDO_IF,				/* bInterfaceNumber */
	0x00,					/* bAlternateSetting */
	0x02,					/* bNumEndpoints */
	HID_CLASS,				/* bInterfaceClass */
	0x00,					/* bInterfaceSubClass */
	0x00,					/* bInterfaceProtocol */
	0x00,					/* iInterface */

	/* HID Descriptor (FIDO) */
	HID_DESCRIPTOR_LENGTH,
	HID_DESCRIPTOR,
	0x11, 0x01,				/* bcdHID (1.11) */
	0x00,					/* bCountryCode */
	0x01,					/* bNumDescriptors */
	HID_REPORT_DESCRIPTOR,			/* bDescriptorType */
	FIDO_REPORT_DESC_LEN, 0x00,

	/* Endpoint Descriptor (FIDO IN) */
	ENDPOINT_DESCRIPTOR_LENGTH,
	ENDPOINT_DESCRIPTOR,
	USB_FIDO_EP_IN,
	USB_EP_TYPE_INTR,
	USB_FIDO_PKT_SIZE, 0x00,
	USB_FIDO_POLL_MS,

	/* Endpoint Descriptor (FIDO OUT) */
	ENDPOINT_DESCRIPTOR_LENGTH,
	ENDPOINT_DESCRIPTOR,
	USB_FIDO_EP_OUT,
	USB_EP_TYPE_INTR,
	USB_FIDO_PKT_SIZE, 0x00,
	USB_FIDO_POLL_MS,
#endif

#if defined(USB_HID_GAMEPAD)
	/* Interface Descriptor (Gamepad) */
	INTERFACE_DESCRIPTOR_LENGTH,
	INTERFACE_DESCRIPTOR,
	USB_PAD_IF,				/* bInterfaceNumber */
	0x00,					/* bAlternateSetting */
	0x01,					/* bNumEndpoints */
	HID_CLASS,				/* bInterfaceClass */
	0x00,					/* bInterfaceSubClass (No boot) */
	0x00,					/* bInterfaceProtocol */
	0x00,					/* iInterface */

	/* HID Descriptor (Gamepad) */
	HID_DESCRIPTOR_LENGTH,
	HID_DESCRIPTOR,
	0x11, 0x01,				/* bcdHID (1.11) */
	0x00,					/* bCountryCode */
	0x01,					/* bNumDescriptors */
	HID_REPORT_DESCRIPTOR,			/* bDescriptorType */
	(sizeof(padReportDescriptor) & 0xFF),
	(sizeof(padReportDescriptor) >> 8),

	/* Endpoint Descriptor (Gamepad IN) */
	ENDPOINT_DESCRIPTOR_LENGTH,
	ENDPOINT_DESCRIPTOR,
	USB_PAD_EP_IN,
	USB_EP_TYPE_INTR,
	USB_PAD_EP_SIZE, 0x00,
	USB_PAD_POLL_MS,
#endif
};

/*
 *  デバイスディスクリプタ
 *  VID/PID は kb_config.h から取得
 */
EXPORT UB usb_device_descriptor[DEVICE_DESCRIPTOR_LENGTH]
			__attribute__((aligned(USB_DATA_ALIGN))) = {
	0x12,			/* bLength */
	DEVICE_DESCRIPTOR,	/* bDescriptorType */
	0x00, 0x02,		/* bcdUSB (USB 2.0) */
	0x00,			/* bDeviceClass */
	0x00,			/* bDeviceSubClass */
	0x00,			/* bDeviceProtocol */
	USB_MAX_EP0_SIZE,	/* bMaxPacketSize0 */
	(KB_VENDOR_ID  & 0xFF), (KB_VENDOR_ID  >> 8),
	(KB_PRODUCT_ID & 0xFF), (KB_PRODUCT_ID >> 8),
	0x00, 0x01,		/* bcdDevice (1.00) */
	0x01,			/* iManufacturer */
	0x02,			/* iProduct */
	0x03,			/* iSerialNumber */
	0x01			/* bNumConfigurations */
};

/*
 *  Language ID ディスクリプタ
 */
EXPORT UB usb_langid_descriptor[]
			__attribute__((aligned(USB_DATA_ALIGN))) = {
	0x04,
	STRING_DESCRIPTOR,
	0x09, 0x04		/* English (US) */
};

/*
 *  ASCII → USB String Descriptor 変換
 */
LOCAL UB ascii_to_usb_string(const char *src, UB *dst, UB max_len)
{
	UB i;
	for(i = 0; i < max_len && src[i] != '\0'; i++) {
		dst[2 + i * 2]     = src[i];
		dst[2 + i * 2 + 1] = 0;
	}
	dst[0] = 2 + i * 2;	/* bLength */
	dst[1] = STRING_DESCRIPTOR;
	return i;
}

/*
 *  文字列ディスクリプタバッファ
 */
EXPORT UB usb_manufacturer_string[64]
			__attribute__((aligned(USB_DATA_ALIGN)));
EXPORT UB usb_product_string[64]
			__attribute__((aligned(USB_DATA_ALIGN)));
EXPORT UB usb_serial_string[32]
			__attribute__((aligned(USB_DATA_ALIGN)));
EXPORT UB usb_config_string[64]
			__attribute__((aligned(USB_DATA_ALIGN)));
EXPORT UB usb_interface_string[64]
			__attribute__((aligned(USB_DATA_ALIGN)));

/*
 *  ディスクリプタ初期化 (文字列を Unicode 変換)
 */
EXPORT void usb_desc_init(void)
{
	ascii_to_usb_string(KB_MANUFACTURER,    usb_manufacturer_string, 30);
	ascii_to_usb_string(KB_PRODUCT,         usb_product_string, 30);
	ascii_to_usb_string("000001",           usb_serial_string, 12);
	ascii_to_usb_string("HID Config",       usb_config_string, 30);
	ascii_to_usb_string("HID Interface",    usb_interface_string, 30);
}

/*======================================================================
 *  HID Keyboard クラス実装
 *======================================================================*/

LOCAL T_USB_HID hid_class_data __attribute__((aligned(USB_DATA_ALIGN)));

#if defined(USB_HID_FIDO) || defined(USB_HID_GAMEPAD)
/*
 *  n 番目 (0 起点) の HID ディスクリプタ
 */
LOCAL UB *find_hid_desc(UW n)
{
	UB *p   = usb_hid_config_desc;
	UB *end = usb_hid_config_desc + USB_HID_CFG_DESC_LEN;

	for (p += p[0]; p < end && p[0] >= 2; p += p[0]) {
		if (p[1] == HID_DESCRIPTOR) {
			if (n == 0) return p;
			n--;
		}
	}
	return NULL;
}
#endif

#if defined(USB_HID_FIDO)
/*======================================================================
 *  FIDO (CTAPHID) インタフェース
 *======================================================================*/

#define FIDO_EVT_TX_IDLE	(1u << 0)

LOCAL struct {
	T_USB_MW	*p_usb;		/* CONFIGURED 中のみ非 NULL */
	ID		flgid;		/* EP2 IN 空き通知 */
	FP_USB_FIDO_RECV recv_cb;
	UB		idle_state;
	UB		rx_buf[USB_FIDO_PKT_SIZE] __attribute__((aligned(USB_DATA_ALIGN)));
	UB		tx_buf[USB_FIDO_PKT_SIZE] __attribute__((aligned(USB_DATA_ALIGN)));
} fido;

LOCAL void fido_arm_rx(T_USB_MW *p_usb)
{
	memset(fido.rx_buf, 0, sizeof(fido.rx_buf));
	ll_start_rx(p_usb, USB_FIDO_EP_OUT, fido.rx_buf, USB_FIDO_PKT_SIZE);
}

LOCAL void fido_class_init(T_USB_MW *p_usb)
{
	if (fido.flgid <= 0) {
		T_CFLG cflg;
		cflg.exinf   = NULL;
		cflg.flgatr  = TA_TFIFO | TA_WSGL;
		cflg.iflgptn = 0;
		fido.flgid = tk_cre_flg(&cflg);
	}
	ll_open_ep(p_usb, USB_FIDO_EP_IN, USB_EP_TYPE_INTR, USB_FIDO_PKT_SIZE);
	ll_open_ep(p_usb, USB_FIDO_EP_OUT, USB_EP_TYPE_INTR, USB_FIDO_PKT_SIZE);
	fido.p_usb = p_usb;
	fido_arm_rx(p_usb);
	if (fido.flgid > 0) tk_set_flg(fido.flgid, FIDO_EVT_TX_IDLE);
}

LOCAL void fido_class_deinit(T_USB_MW *p_usb)
{
	fido.p_usb = NULL;
	ll_close_ep(p_usb, USB_FIDO_EP_IN);
	ll_close_ep(p_usb, USB_FIDO_EP_OUT);
}

/*
 *  FIDO インタフェース宛てのリクエスト
 */
LOCAL void fido_class_setup(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	UB *pbuf = NULL;
	UH len = 0;

	switch (req->bmRequest & USB_REQUEST_TYPE_MASK) {
	case USB_REQUEST_TYPE_STANDARD:
		if (req->bRequest == GET_DESCRIPTOR) {
			if ((req->wValue >> 8) == HID_REPORT_DESCRIPTOR) {
				pbuf = fidoReportDescriptor;
				len  = FIDO_REPORT_DESC_LEN;
			} else if ((req->wValue >> 8) == HID_DESCRIPTOR) {
				pbuf = find_hid_desc(1);
				if (pbuf != NULL) len = pbuf[0];
			}
			if (len > 0) {
				if (len > req->wLength) len = req->wLength;
				usb_ep0_send(p_usb, pbuf, len);
			} else {
				usb_ep0_stall(p_usb, req);
			}
		} else if (req->bRequest == GET_INTERFACE) {
			p_usb->dev_data[0] = 0;
			usb_ep0_send(p_usb, p_usb->dev_data, 1);
		}
		break;

	case USB_REQUEST_TYPE_CLASS:
		switch (req->bRequest) {
		case USB_HID_SET_IDLE:
			fido.idle_state = (UB)(req->wValue >> 8);
			break;
		case USB_HID_GET_IDLE:
			p_usb->dev_data[0] = fido.idle_state;
			usb_ep0_send(p_usb, p_usb->dev_data, 1);
			break;
		default:
			usb_ep0_stall(p_usb, req);
			break;
		}
		break;

	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

/*
 *  EP2 OUT 受信完了 (USB 処理タスク)
 */
EXPORT void usb_hid_class_data_out(T_USB_MW *p_usb, UB epnum)
{
	if (epnum != (USB_FIDO_EP_OUT & 0x7F)) return;
	if (fido.recv_cb != NULL) fido.recv_cb(fido.rx_buf);
	fido_arm_rx(p_usb);
}

EXPORT void usb_fido_set_recv_callback(FP_USB_FIDO_RECV recv_cb)
{
	fido.recv_cb = recv_cb;
}

/*
 *  EP2 IN で 1 パケット送信 (前回の送信完了を待ってから送る)
 */
EXPORT ER usb_fido_send(const UB *pkt, TMO tmout)
{
	UINT flgptn;
	ER err;

	if (fido.p_usb == NULL || fido.flgid <= 0) return E_IO;

	err = tk_wai_flg(fido.flgid, FIDO_EVT_TX_IDLE, TWF_ORW | TWF_BITCLR,
			 &flgptn, tmout);
	if (err != E_OK) return err;
	if (fido.p_usb == NULL) return E_IO;

	memcpy(fido.tx_buf, pkt, USB_FIDO_PKT_SIZE);
	ll_start_tx(fido.p_usb, USB_FIDO_EP_IN, fido.tx_buf, USB_FIDO_PKT_SIZE);
	return E_OK;
}
#endif /* USB_HID_FIDO */

#if defined(USB_HID_GAMEPAD)
/*======================================================================
 *  ゲームパッドインタフェース
 *======================================================================*/

LOCAL struct {
	BOOL	opened;		/* CONFIGURED 中のみ TRUE */
	BOOL	busy;		/* EP3 IN 送信中 (完了通知で落とす) */
	UB	idle_state;
	UH	report_len;
	UB	report[USB_PAD_EP_SIZE] __attribute__((aligned(USB_DATA_ALIGN)));
} pad;

LOCAL void pad_class_init(T_USB_MW *p_usb)
{
	ll_open_ep(p_usb, USB_PAD_EP_IN, USB_EP_TYPE_INTR, USB_PAD_EP_SIZE);
	pad.busy = FALSE;
	pad.opened = TRUE;
}

LOCAL void pad_class_deinit(T_USB_MW *p_usb)
{
	pad.opened = FALSE;
	ll_close_ep(p_usb, USB_PAD_EP_IN);
}

/*
 *  ゲームパッドインタフェース宛てのリクエスト
 */
LOCAL void pad_class_setup(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	UB *pbuf = NULL;
	UH len = 0;

	switch (req->bmRequest & USB_REQUEST_TYPE_MASK) {
	case USB_REQUEST_TYPE_STANDARD:
		if (req->bRequest == GET_DESCRIPTOR) {
			if ((req->wValue >> 8) == HID_REPORT_DESCRIPTOR) {
				pbuf = padReportDescriptor;
				len  = sizeof(padReportDescriptor);
			} else if ((req->wValue >> 8) == HID_DESCRIPTOR) {
				pbuf = find_hid_desc(USB_PAD_IF);
				if (pbuf != NULL) len = pbuf[0];
			}
			if (len > 0) {
				if (len > req->wLength) len = req->wLength;
				usb_ep0_send(p_usb, pbuf, len);
			} else {
				usb_ep0_stall(p_usb, req);
			}
		} else if (req->bRequest == GET_INTERFACE) {
			p_usb->dev_data[0] = 0;
			usb_ep0_send(p_usb, p_usb->dev_data, 1);
		}
		break;

	case USB_REQUEST_TYPE_CLASS:
		switch (req->bRequest) {
		case USB_HID_SET_IDLE:
			pad.idle_state = (UB)(req->wValue >> 8);
			break;
		case USB_HID_GET_IDLE:
			p_usb->dev_data[0] = pad.idle_state;
			usb_ep0_send(p_usb, p_usb->dev_data, 1);
			break;
		case USB_HID_GET_REPORT:
			len = pad.report_len;
			if (len > req->wLength) len = req->wLength;
			usb_ep0_send(p_usb, pad.report, len);
			break;
		default:
			usb_ep0_stall(p_usb, req);
			break;
		}
		break;

	default:
		usb_ep0_stall(p_usb, req);
		break;
	}
}

/*----------------------------------------------------------------------
 *  ゲームパッドレポート送信 (EP3 IN)
 *
 *  タスクコンテキストから呼ばれる。busy を ISR 経由の完了通知と共有する
 *  ため DI/EI で排他する。ホストが EP3 を読まない間 (BIOS のブート
 *  プロトコルなど) は送信が終わらないので、待たずに E_BUSY を返す。
 */
EXPORT ER usb_hid_send_pad_report(T_USB_MW *p_usb, const UB *pdata, UH len)
{
	UINT imask;

	if (p_usb->dev_state != USB_DEV_STATE_CONFIGURED || !pad.opened)
		return E_IO;
	if (len > sizeof(pad.report)) len = sizeof(pad.report);

	DI(imask);
	if (pad.busy) {
		EI(imask);
		return E_BUSY;
	}
	pad.busy = TRUE;
	EI(imask);

	memcpy(pad.report, pdata, len);
	pad.report_len = len;
	ll_start_tx(p_usb, USB_PAD_EP_IN, pad.report, len);
	return E_OK;
}
#endif /* USB_HID_GAMEPAD */

/*----------------------------------------------------------------------
 *  HID Keyboard クラス初期化
 */
EXPORT ER usb_hid_class_init(T_USB_MW *p_usb, UB idx)
{
	(void)idx;
	ll_open_ep(p_usb, USB_HID_EP_IN, USB_EP_TYPE_INTR, USB_HID_EP_SIZE);
	memset(&hid_class_data, 0, sizeof(T_USB_HID));
	hid_class_data.data_state = USB_HID_IDLE;
	p_usb->p_cls = &hid_class_data;
#if defined(USB_HID_FIDO)
	fido_class_init(p_usb);
#endif
#if defined(USB_HID_GAMEPAD)
	pad_class_init(p_usb);
#endif
	return E_OK;
}

/*----------------------------------------------------------------------
 *  HID Keyboard クラス終了
 */
EXPORT ER usb_hid_class_deinit(T_USB_MW *p_usb, UB idx)
{
	(void)idx;
	ll_close_ep(p_usb, USB_HID_EP_IN);
#if defined(USB_HID_FIDO)
	fido_class_deinit(p_usb);
#endif
#if defined(USB_HID_GAMEPAD)
	pad_class_deinit(p_usb);
#endif
	p_usb->p_cls = NULL;
	return E_OK;
}

/*----------------------------------------------------------------------
 *  HID Keyboard クラスセットアップ
 */
EXPORT void usb_hid_class_setup(T_USB_MW *p_usb, T_USB_SETUP_REQ *req)
{
	T_USB_HID *p_hid = (T_USB_HID *)p_usb->p_cls;
	UH	len = 0;
	UB	*pbuf = NULL;
	UB	*p;
	UW	i;

#if defined(USB_HID_FIDO)
	if ((req->bmRequest & 0x1F) == USB_RECIPIENT_INTERFACE &&
	    (req->wIndex & 0xFF) == USB_FIDO_IF) {
		fido_class_setup(p_usb, req);
		return;
	}
#endif
#if defined(USB_HID_GAMEPAD)
	if ((req->bmRequest & 0x1F) == USB_RECIPIENT_INTERFACE &&
	    (req->wIndex & 0xFF) == USB_PAD_IF) {
		pad_class_setup(p_usb, req);
		return;
	}
#endif

	switch(req->bmRequest & USB_REQUEST_TYPE_MASK) {
	case USB_REQUEST_TYPE_STANDARD:
		switch(req->bRequest) {
		case GET_DESCRIPTOR:
			if((req->wValue >> 8) == HID_REPORT_DESCRIPTOR) {
				len  = sizeof(reportDescriptor);
				pbuf = reportDescriptor;
			} else if((req->wValue >> 8) == HID_DESCRIPTOR) {
				pbuf = usb_find_desc(p_usb, HID_DESCRIPTOR);
				if(pbuf != NULL) {
					p   = p_usb->dev_data;
					len = pbuf[0];
					for(i = 0; i < len && i < 32; i++)
						*p++ = *pbuf++;
					pbuf = p_usb->dev_data;
				}
			}
			if(len > 0) {
				if(len > req->wLength)
					len = req->wLength;
				usb_ep0_send(p_usb, pbuf, len);
			}
			break;
		case GET_INTERFACE:
			p_usb->dev_data[0] = p_hid->alt_setting;
			usb_ep0_send(p_usb, p_usb->dev_data, 1);
			break;
		case SET_INTERFACE:
			p_hid->alt_setting = (UB)(req->wValue);
			break;
		}
		break;

	case USB_REQUEST_TYPE_CLASS:
		switch(req->bRequest) {
		case USB_HID_SET_PROTOCOL:
			p_hid->protocol = (UB)(req->wValue);
			break;
		case USB_HID_GET_PROTOCOL:
			p_usb->dev_data[0] = p_hid->protocol;
			usb_ep0_send(p_usb, p_usb->dev_data, 1);
			break;
		case USB_HID_SET_IDLE:
			p_hid->idle_state = (UB)(req->wValue >> 8);
			break;
		case USB_HID_GET_IDLE:
			p_usb->dev_data[0] = p_hid->idle_state;
			usb_ep0_send(p_usb, p_usb->dev_data, 1);
			break;
		case USB_HID_SET_REPORT:
			/*
			 *  レポートディスクリプタで LED の Output レポートを
			 *  宣言しているため、ホストは起動直後にこれを送る。
			 */
			if(req->wLength > 0 &&
			   req->wLength <= sizeof(p_hid->out_report)) {
				usb_ep0_recv(p_usb, p_hid->out_report,
				             req->wLength);
			} else {
				usb_ep0_stall(p_usb, req);
				return;
			}
			break;
		case USB_HID_GET_REPORT:
			{
				UH n = req->wLength;
				if(n > sizeof(p_hid->hid_data))
					n = sizeof(p_hid->hid_data);
				usb_ep0_send(p_usb, p_hid->hid_data, n);
			}
			break;
		default:
			usb_ep0_stall(p_usb, req);
			return;
		}
		break;
	}
}

/*----------------------------------------------------------------------
 *  HID DATA IN 完了コールバック
 */
EXPORT void usb_hid_class_data_in(T_USB_MW *p_usb, UB epnum)
{
#if defined(USB_HID_GAMEPAD)
	if (epnum == (USB_PAD_EP_IN & 0x7F)) {
		pad.busy = FALSE;
		return;
	}
#endif
#if defined(USB_HID_FIDO)
	if (epnum == (USB_FIDO_EP_IN & 0x7F)) {
		if (fido.flgid > 0) tk_set_flg(fido.flgid, FIDO_EVT_TX_IDLE);
		return;
	}
#else
	(void)epnum;
#endif
	((T_USB_HID *)p_usb->p_cls)->data_state = USB_HID_IDLE;
}

/*----------------------------------------------------------------------
 *  HID Keyboard レポート送信
 *  pdata: 8バイト (modifier + reserved + 6 keycodes)
 *
 *  タスクコンテキストから呼ばれる。data_state を ISR と共有するため、
 *  割り込みを禁止して (DI/EI) 排他制御する。
 */
EXPORT ER usb_hid_send_report(T_USB_MW *p_usb, UB *pdata, UH len)
{
	T_USB_HID *p_hid = (T_USB_HID *)p_usb->p_cls;
	UW i;
	UINT imask;
	ER rc;

	if(p_usb->dev_state == USB_DEV_STATE_CONFIGURED) {
		DI(imask);
		if(p_hid->data_state == USB_HID_IDLE) {
			p_hid->data_state = USB_HID_BUSY;
			EI(imask);
			for(i = 0; i < len && i < 32; i++)
				p_hid->hid_data[i] = *pdata++;
			ll_start_tx(p_usb, USB_HID_EP_IN,
					    p_hid->hid_data, i);
			rc = E_OK;
		} else {
			EI(imask);
			rc = E_BUSY;
		}
		return rc;
	}
	return E_BUSY;
}

#endif	/* CPU_RP2040 || CPU_STM32H7 || MTKBSP_CPU_STM32H5 */
