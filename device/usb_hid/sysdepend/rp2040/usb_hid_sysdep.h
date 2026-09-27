/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    RP2040 USB デバイス HAL 定義
 *    RP2040 Datasheet Section 4.1 USB
 *----------------------------------------------------------------------
 */

#ifndef __USB_HID_SYSDEP_H__
#define __USB_HID_SYSDEP_H__

#include <tk/tkernel.h>
#include <tk/syslib.h>

/*
 *  USB 型定義・定数定義
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
 *  RP2040 USB コントローラ ベースアドレス
 *  RP2040 Datasheet Section 4.1.4
 */
#define USB_REGS_BASE		0x50110000u
#define USB_DPRAM_BASE		0x50100000u
#define USB_DPRAM_SIZE		4096

/*
 *  RP2040 USB レジスタオフセット (RP2040 Datasheet Section 4.1.4)
 */
#define USB_REG_ADDR_ENDP	0x0000
#define USB_REG_MAIN_CTRL	0x0040
#define USB_REG_SOF_WR		0x0044
#define USB_REG_SOF_RD		0x0048
#define USB_REG_SIE_CTRL	0x004C	/* RP2040 Datasheet Section 4.1.4.6 */
#define USB_REG_SIE_STATUS	0x0050	/* RP2040 Datasheet Section 4.1.4.7 */
#define USB_REG_INT_EP_CTRL	0x0054
#define USB_REG_BUFF_STATUS	0x0058
#define USB_REG_MUXING		0x0074
#define USB_REG_PWR		0x0078
#define USB_REG_INTR		0x008C
#define USB_REG_INTE		0x0090
#define USB_REG_INTF		0x0094
#define USB_REG_INTS		0x0098

/*
 *  MAIN_CTRL ビット定義
 */
#define USB_MAIN_CTRL_CONTROLLER_EN	0x00000001

/*
 *  SIE_CTRL ビット定義 (RP2040 Datasheet Section 4.1.4.6)
 */
#define USB_SIE_CTRL_PULLUP_EN		0x00010000
#define USB_SIE_CTRL_EP0_INT_1BUF	0x20000000

/*
 *  SIE_STATUS ビット定義 (RP2040 Datasheet Section 4.1.4.7)
 */
#define USB_SIE_STATUS_VBUS_DETECTED	0x00000001
#define USB_SIE_STATUS_SUSPENDED	0x00000010
#define USB_SIE_STATUS_SPEED		0x00000300
#define USB_SIE_STATUS_RESUME		0x00000800
#define USB_SIE_STATUS_CONNECTED	0x00010000
#define USB_SIE_STATUS_SETUP_REC	0x00020000
#define USB_SIE_STATUS_TRANS_COMPLETE	0x00040000
#define USB_SIE_STATUS_BUS_RESET	0x00080000

/*
 *  MUXING ビット定義
 */
#define USB_MUXING_TO_PHY		0x00000001
#define USB_MUXING_SOFTCON		0x00000008

/*
 *  PWR ビット定義
 */
#define USB_PWR_VBUS_DETECT		0x00000004
#define USB_PWR_VBUS_DETECT_OVR_EN	0x00000008

/*
 *  INTE / INTS ビット定義 (RP2040 Datasheet Section 4.1.2.6)
 */
#define USB_INTE_BUFF_STATUS		0x00000010
#define USB_INTE_BUS_RESET		0x00001000
#define USB_INTE_DEV_SUSPEND		0x00004000
#define USB_INTE_DEV_RESUME_FROM_HOST	0x00008000
#define USB_INTE_SETUP_REQ		0x00010000
#define USB_INTE_DEV_SOF		0x00020000

#define USB_INTS_BUFF_STATUS		USB_INTE_BUFF_STATUS
#define USB_INTS_BUS_RESET		USB_INTE_BUS_RESET
#define USB_INTS_DEV_SUSPEND		USB_INTE_DEV_SUSPEND
#define USB_INTS_DEV_RESUME_FROM_HOST	USB_INTE_DEV_RESUME_FROM_HOST
#define USB_INTS_SETUP_REQ		USB_INTE_SETUP_REQ
#define USB_INTS_DEV_SOF		USB_INTE_DEV_SOF

/*
 *  EP バッファコントロール ビット定義 (RP2040 Datasheet Section 4.1.2.5.1)
 */
#define USB_BUF_CTRL_FULL		0x00008000u
#define USB_BUF_CTRL_LAST		0x00004000u
#define USB_BUF_CTRL_DATA0_PID		0x00000000u
#define USB_BUF_CTRL_DATA1_PID		0x00002000u
#define USB_BUF_CTRL_SEL		0x00001000u
#define USB_BUF_CTRL_STALL		0x00000800u
#define USB_BUF_CTRL_AVAIL		0x00000400u
#define USB_BUF_CTRL_LEN_MASK		0x000003FFu

/*
 *  EP コントロール ビット定義
 */
#define USB_EP_CTRL_ENABLE		0x80000000u
#define USB_EP_CTRL_DOUBLE_BUF		0x40000000u
#define USB_EP_CTRL_INT_PER_BUF		0x20000000u
#define USB_EP_TYPE_SHIFT		26

/*
 *  最大デバイスエンドポイント数
 */
#ifndef USB_MAX_EPS
#define USB_MAX_EPS			16
#endif

/*
 *  USB DPRAM 構造体 (RP2040 Datasheet Section 4.1.2.4)
 */
typedef struct {
	volatile UB	setup_packet[8];		/* 0x000 */

	struct {					/* 0x008-0x07F */
		UW	in;
		UW	out;
	} ep_ctrl[USB_MAX_EPS - 1];		/* EP1-EP15 endpoint control */

	struct {					/* 0x080-0x0FF */
		UW	in;
		UW	out;
	} ep_buf_ctrl[USB_MAX_EPS];		/* EP0-EP15 buffer control */

	UB	ep0_buf_a[0x40];			/* 0x100-0x13F */
	UB	ep0_buf_b[0x40];			/* 0x140-0x17F */
	UB	epx_data[USB_DPRAM_SIZE - 0x180];	/* 0x180-0xFFF */
} T_USB_DPRAM;

/*
 *  USB デバイス エンドポイント定義
 */
typedef struct {
	UB	num;			/* EP番号 (0-15) */
	UB	is_in;			/* 方向 (0:OUT, 1:IN) */
	UB	is_stall;		/* STALLフラグ */
	UB	type;			/* EP タイプ */
	UB	next_pid;		/* 次の DATA PID (0 or 1) */
	UW	*ep_ctrl_reg;		/* EP コントロールレジスタ */
	UW	*buf_ctrl_reg;		/* EP バッファコントロール */
	UB	*data_buf;		/* DPRAM データバッファ */
	UW	max_pkt_size;		/* 最大パケットサイズ */
	UB	*xfer_buf;		/* 転送バッファポインタ */
	UW	xfer_remain;		/* 残り転送長 */
	UW	xfer_done;		/* 転送済みカウント */
	UW	xfer_req;		/* 今回の要求カウント */
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
 *  USB デバイス ハードウェア制御ブロック (低レベル)
 */
typedef struct _T_USB_LL_CB T_USB_LL_CB;

struct _T_USB_LL_CB {
	UW		base;			/* レジスタベースアドレス */
	T_USB_DPRAM	*dpram;			/* DPRAM ベースアドレス */
	T_USB_INIT	init;			/* 初期化パラメータ */
	T_USB_EP	in_ep[USB_MAX_EPS];	/* IN EP データ */
	T_USB_EP	out_ep[USB_MAX_EPS];	/* OUT EP データ */
	volatile UB	*setup_buf;		/* Setup パケットバッファ */
	volatile UB	dev_addr;		/* USB アドレス */
	UB		suspended;		/* サスペンドフラグ */
	UB		ep0_out_len;		/* EP0 OUT データ長 */

	/* ISR → タスク通信 */
	ID	usb_flgid;		/* イベントフラグ ID */
	UB	setup_shadow[8];	/* SETUP パケットシャドウコピー */

	void	*p_mw;			/* ミドルウェアハンドルへのポインタ */
};

/*
 *  USB デバイス IRQ 番号
 */
#define INTNO_USBCTRL		5
#define INTPRI_USBCTRL		2

/*
 *  USB 処理タスク設定
 */
#define USB_TASK_PRI		5	/* KB スキャナ(8) より高い */
#define USB_TASK_STKSZ		768

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
#define USB_EVT_EP3_IN		(1 << 7)
#define USB_EVT_ANY		(0xFF)

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
