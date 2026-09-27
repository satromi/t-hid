/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    STM32H7 USB OTG FS デバイス HAL 定義
 *    DWC2 core (Device mode, Full-Speed)
 *    STM32H723 Reference Manual RM0468 Section 61
 *----------------------------------------------------------------------
 */

#ifndef __USB_HID_SYSDEP_H__
#define __USB_HID_SYSDEP_H__

#include <tk/tkernel.h>
#include <tk/syslib.h>

/*
 *  USB 型定義・定数定義
 *  (RP2040 版と共通 — ミドルウェア層が参照する)
 */

/*
 *  USB コントロールリクエストフィールド
 */
#define USB_DEVICE_TO_HOST		0x80
#define USB_HOST_TO_DEVICE		0x00
#define USB_REQUEST_DIR_MASK		USB_DEVICE_TO_HOST
#define USB_REQUEST_TYPE_VENDOR		0x40
#define USB_REQUEST_TYPE_CLASS		0x20
#define USB_REQUEST_TYPE_STANDARD	0x00
#define USB_REQUEST_TYPE_MASK		0x60
#define USB_RECIPIENT_OTHER		0x03
#define USB_RECIPIENT_ENDPOINT		0x02
#define USB_RECIPIENT_INTERFACE		0x01
#define USB_RECIPIENT_DEVICE		0x00

/*
 *  USB 標準リクエスト
 */
#define GET_STATUS			0x00
#define CLEAR_FEATURE			0x01
#define SET_FEATURE			0x03
#define SET_ADDRESS			0x05
#define GET_DESCRIPTOR			0x06
#define SET_DESCRIPTOR			0x07
#define GET_CONFIGURATION		0x08
#define SET_CONFIGURATION		0x09
#define GET_INTERFACE			0x0A
#define SET_INTERFACE			0x0B

/*
 *  USB ディスクリプタタイプ
 */
#define DEVICE_DESCRIPTOR		(1)
#define CONFIGURATION_DESCRIPTOR	(2)
#define STRING_DESCRIPTOR		(3)
#define INTERFACE_DESCRIPTOR		(4)
#define ENDPOINT_DESCRIPTOR		(5)
#define DEVICE_QUALIFIER_DESCRIPTOR	(6)
#define OTHER_SPEED_CONFIGURATION_DESC	(7)
#define BOS_DESCRIPTOR			(15)
#define HID_DESCRIPTOR			(33)
#define HID_REPORT_DESCRIPTOR		(34)

/*
 *  USB ディスクリプタ長
 */
#define DEVICE_DESCRIPTOR_LENGTH	0x12
#define CONFIGURATION_DESCRIPTOR_LENGTH	0x09
#define INTERFACE_DESCRIPTOR_LENGTH	0x09
#define ENDPOINT_DESCRIPTOR_LENGTH	0x07
#define HID_DESCRIPTOR_LENGTH		0x09

/*
 *  USB エンドポイントタイプ
 */
#define USB_EP_TYPE_CTRL		0x00
#define USB_EP_TYPE_ISOC		0x01
#define USB_EP_TYPE_BULK		0x02
#define USB_EP_TYPE_INTR		0x03

/*
 *  USB スピード
 */
#define USB_SPEED_HIGH			0
#define USB_SPEED_HIGH_IN_FULL		1
#define USB_SPEED_LOW			2
#define USB_SPEED_FULL			3

#define USB_DEVICE_SPEED_HIGH		0
#define USB_DEVICE_SPEED_FULL		1
#define USB_DEVICE_SPEED_LOW		2

/*
 *  USB HID クラス定数
 */
#define HID_CLASS			0x03

/*
 *  USB セットアップリクエスト構造体
 */
typedef struct {
	UB	bmRequest;
	UB	bRequest;
	UH	wValue;
	UH	wIndex;
	UH	wLength;
} T_USB_SETUP_REQ;

/*
 *  最大デバイスエンドポイント数 (DWC2 FS: EP0-EP8)
 */
#ifndef USB_MAX_EPS
#define USB_MAX_EPS			9
#endif

/*
 *  USB データアライメント
 */
#define USB_DATA_ALIGN			4

/*
 *  USB デバイス エンドポイント定義 (DWC2)
 *  RP2040 版の T_USB_EP と同じフィールド名を維持し、
 *  ミドルウェア層との互換性を保つ
 */
typedef struct {
	UB	num;			/* EP番号 (0-8) */
	UB	is_in;			/* 方向 (0:OUT, 1:IN) */
	UB	is_stall;		/* STALLフラグ */
	UB	type;			/* EP タイプ */
	UB	next_pid;		/* DATA PID (DWC2 では HW 管理だが互換用) */
	UW	max_pkt_size;		/* 最大パケットサイズ */
	UB	*xfer_buf;		/* 転送バッファポインタ */
	UW	xfer_remain;		/* 残り転送長 */
	UW	xfer_done;		/* 転送済みカウント */
	UW	xfer_req;		/* 今回の要求カウント */
	UB	*data_buf;		/* EP データバッファ (FIFO 読み書き用ステージング) */
} T_USB_EP;

/*
 *  USB デバイス初期化パラメータ
 */
typedef struct {
	UW	num_eps;		/* エンドポイント数 */
	UW	speed;			/* USBスピード */
	UW	sof_enable;		/* SOF割り込み有効 (0 or 1) */
} T_USB_INIT;

/*
 *  USB デバイス ハードウェア制御ブロック (DWC2)
 */
typedef struct _T_USB_LL_CB T_USB_LL_CB;

struct _T_USB_LL_CB {
	UW		base;			/* OTG_FS ベースアドレス */
	T_USB_INIT	init;			/* 初期化パラメータ */
	T_USB_EP	in_ep[USB_MAX_EPS];	/* IN EP データ */
	T_USB_EP	out_ep[USB_MAX_EPS];	/* OUT EP データ */
	volatile UB	dev_addr;		/* USB アドレス (保留用) */
	UB		suspended;		/* サスペンドフラグ */

	/* EP0 OUT 受信バッファ */
	UB		ep0_buf[64] __attribute__((aligned(4)));

	/* ISR → タスク通信 */
	ID	usb_flgid;		/* イベントフラグ ID */
	UB	setup_shadow[8];	/* SETUP パケットシャドウコピー */
	volatile UB	setup_buf[8];	/* SETUP パケットバッファ */

	void	*p_mw;			/* ミドルウェアハンドルへのポインタ */
};

/*
 *  USB デバイス IRQ 番号 (sysdef.h で定義済み)
 */
#define INTNO_USBCTRL		INTNO_OTG_HS
#define INTPRI_USBCTRL		INTPRI_OTG_HS

/*
 *  USB 処理タスク設定
 */
#define USB_TASK_PRI		5	/* KB スキャナ(8) より高い */
#define USB_TASK_STKSZ		1024

/*
 *  ISR → タスク イベントフラグビット定義
 */
#define USB_EVT_SETUP		(1 << 0)
#define USB_EVT_EP0_IN		(1 << 1)
#define USB_EVT_EP0_OUT		(1 << 2)
#define USB_EVT_EP1_IN		(1 << 3)
#define USB_EVT_BUS_RESET	(1 << 4)
#define USB_EVT_SUSPEND		(1 << 5)
#define USB_EVT_RESUME		(1 << 6)
#define USB_EVT_ANY		(0x7F)

/*
 *  関数プロトタイプ
 */
IMPORT T_USB_LL_CB *dev_usb_hid_llinit(T_USB_INIT *p_init);
IMPORT ER dev_usb_hid_lldeinit(T_USB_LL_CB *p_cb);
IMPORT ER dev_usb_hid_connect(T_USB_LL_CB *p_cb);
IMPORT ER dev_usb_hid_disconnect(T_USB_LL_CB *p_cb);
IMPORT ER dev_usb_hid_set_addr(T_USB_LL_CB *p_cb, UB address);
IMPORT ER dev_usb_hid_ep_activate(T_USB_LL_CB *p_cb, T_USB_EP *ep);
IMPORT ER dev_usb_hid_ep_deactivate(T_USB_LL_CB *p_cb, T_USB_EP *ep);
IMPORT ER dev_usb_hid_ep_recv(T_USB_LL_CB *p_cb, T_USB_EP *ep);
IMPORT ER dev_usb_hid_ep_send(T_USB_LL_CB *p_cb, T_USB_EP *ep);
IMPORT ER dev_usb_hid_ep_stall(T_USB_LL_CB *p_cb, T_USB_EP *ep);
IMPORT ER dev_usb_hid_ep_unstall(T_USB_LL_CB *p_cb, T_USB_EP *ep);
IMPORT ER dev_usb_hid_ep_setbuf(T_USB_LL_CB *p_cb, UH ep_addr, UB *data);
IMPORT void usb_hid_inthdr(UINT intno);

#endif	/* __USB_HID_SYSDEP_H__ */
