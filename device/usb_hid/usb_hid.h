/*
 *----------------------------------------------------------------------
 *    USB Device Driver for μT-Kernel 3.0 BSP
 *
 *    USB デバイスミドルウェア ヘッダ
 *----------------------------------------------------------------------
 */

#ifndef __USB_HID_H__
#define __USB_HID_H__

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <string.h>

#define USB_HID_SYSDEP_PATH_(a)	#a
#define USB_HID_SYSDEP_PATH(a)	USB_HID_SYSDEP_PATH_(a)
#define USB_HID_SYSDEP_FILE()	USB_HID_SYSDEP_PATH(sysdepend/TARGET_CPU_DIR/usb_hid_sysdep.h)
#include USB_HID_SYSDEP_FILE()

/*
 *  デバイス状態
 */
#define USB_DEV_STATE_RESET		0
#define USB_DEV_STATE_INIT		1
#define USB_DEV_STATE_ADDRESSED		2
#define USB_DEV_STATE_CONFIGURED	3
#define USB_DEV_STATE_SUSPENDED		4

/*
 *  EP0 状態
 */
#define USB_EP0_STATE_IDLE		0
#define USB_EP0_STATE_SETUP		1
#define USB_EP0_STATE_DATAIN		2
#define USB_EP0_STATE_DATAOUT		3
#define USB_EP0_STATE_STATUSIN		4
#define USB_EP0_STATE_STATUSOUT		5

/*
 *  Configuration descriptor 属性
 */
#define C_RESERVED		(1<<7)
#define C_SELF_POWERED		(1<<6)
#define C_REMOTE_WAKEUP		(1<<5)
#define C_POWER(mA)		((mA)/2)

/*
 *  ミドルウェア設定
 */
#ifndef USB_MW_MAX_EP
#define USB_MW_MAX_EP			15
#endif

#ifndef USB_MW_MAX_CONFIG
#define USB_MW_MAX_CONFIG		1
#endif

#ifndef USB_MW_MAX_IF
#define USB_MW_MAX_IF			1
#endif

/*
 *  EP0 最大パケットサイズ
 */
#define USB_FS_MAX_PACKET_SIZE		64
#define USB_MAX_EP0_SIZE		USB_FS_MAX_PACKET_SIZE

/*
 *  セルフパワー設定
 */
#define USB_CFG_SELF_POWERED		0x01
#define USB_CFG_REMOTE_WAKEUP		0x02

/*
 *  USB Feature
 */
#define USB_FEATURE_EP_HALT		0
#define USB_FEATURE_REMOTE_WAKEUP	1

/*
 *  USB データアライメント
 */
#define USB_DATA_ALIGN			4

/*
 *  ミドルウェア エンドポイント管理
 */
typedef struct {
	UH	status;
	UH	max_pkt_size;
	UW	xfer_size;
	UW	xfer_remain;
} T_USB_MW_EP;

/*
 *  USB デバイスミドルウェアハンドル
 */
typedef struct {
	UB		dev_data[32];
	UB		id;
	UB		dev_speed;
	UB		dev_address;
	UB		dev_config;
	UB		dev_test_mode;
	UB		dev_remote_wakeup;
	UB		ep0_state;
	UH		ep0_data_len;
	UB		dev_state;
	UB		dev_old_state;
	T_USB_MW_EP	ep_in[USB_MW_MAX_EP];
	T_USB_MW_EP	ep_out[USB_MW_MAX_EP];

	T_USB_SETUP_REQ	request;
	void		*p_cls;
	void		*p_ll;		/* 低レベル HAL ハンドルへのポインタ */
} T_USB_MW;

/*
 *  HID Keyboard エンドポイント定義
 */
#define USB_HID_EP_IN		0x81	/* EP1 IN */
#define USB_HID_EP_SIZE		8	/* 8 bytes (modifier + reserved + 6 keys) */
#define USB_HID_POLL_MS		10	/* 10ms ポーリング間隔 */

/*
 *  FIDO (CTAPHID) インタフェース — USB_HID_FIDO 定義時のみ
 *    キーボード (IF0) と FIDO (IF1) の複合デバイスになる
 */
#define USB_FIDO_IF		1
#define USB_FIDO_EP_IN		0x82	/* EP2 IN */
#define USB_FIDO_EP_OUT		0x02	/* EP2 OUT */
#define USB_FIDO_PKT_SIZE	64	/* CTAPHID パケット長 */
#define USB_FIDO_POLL_MS	5

/*
 *  HID Configuration Descriptor 合計長
 */
#define USB_HID_KBD_DESC_LEN	(INTERFACE_DESCRIPTOR_LENGTH + \
				 HID_DESCRIPTOR_LENGTH + \
				 ENDPOINT_DESCRIPTOR_LENGTH)
#define USB_HID_FIDO_DESC_LEN	(INTERFACE_DESCRIPTOR_LENGTH + \
				 HID_DESCRIPTOR_LENGTH + \
				 ENDPOINT_DESCRIPTOR_LENGTH * 2)

#if defined(USB_HID_FIDO)
#define USB_HID_FIDO_NUM_IF	1
#define USB_HID_FIDO_CFG_LEN	USB_HID_FIDO_DESC_LEN
#else
#define USB_HID_FIDO_NUM_IF	0
#define USB_HID_FIDO_CFG_LEN	0
#endif

/*
 *  ゲームパッドインタフェース — USB_HID_GAMEPAD 定義時のみ
 *    キーボード (と FIDO) の後ろのインタフェースになる。
 *    EP3 は FIDO (EP2) と重ならない番号として固定で使う。
 */
#define USB_PAD_IF		(1 + USB_HID_FIDO_NUM_IF)
#define USB_PAD_EP_IN		0x83	/* EP3 IN */
#define USB_PAD_EP_SIZE		16
#define USB_PAD_POLL_MS		2
#define USB_HID_PAD_DESC_LEN	(INTERFACE_DESCRIPTOR_LENGTH + \
				 HID_DESCRIPTOR_LENGTH + \
				 ENDPOINT_DESCRIPTOR_LENGTH)

#if defined(USB_HID_GAMEPAD)
#define USB_HID_PAD_NUM_IF	1
#define USB_HID_PAD_CFG_LEN	USB_HID_PAD_DESC_LEN
#if !defined(USB_EVT_EP3_IN)
#error "USB_HID_GAMEPAD: this USB HAL has no EP3 IN completion event"
#endif
#else
#define USB_HID_PAD_NUM_IF	0
#define USB_HID_PAD_CFG_LEN	0
#endif

#define USB_HID_NUM_IF		(1 + USB_HID_FIDO_NUM_IF + USB_HID_PAD_NUM_IF)
#define USB_HID_CFG_DESC_LEN	(CONFIGURATION_DESCRIPTOR_LENGTH + \
				 USB_HID_KBD_DESC_LEN + \
				 USB_HID_FIDO_CFG_LEN + USB_HID_PAD_CFG_LEN)

/*
 *  HID クラスリクエスト
 */
#define USB_HID_GET_REPORT		0x01
#define USB_HID_GET_IDLE		0x02
#define USB_HID_GET_PROTOCOL		0x03
#define USB_HID_SET_REPORT		0x09
#define USB_HID_SET_IDLE		0x0A
#define USB_HID_SET_PROTOCOL		0x0B

/*
 *  HID データ状態
 */
#define USB_HID_IDLE			0
#define USB_HID_BUSY			1

/*
 *  HID クラスデータ
 */
typedef struct {
	UB	hid_data[32];
	UB	out_report[8];		/* SET_REPORT 受信用 (LED 状態) */
	UB	protocol;
	UB	idle_state;
	UB	alt_setting;
	UB	data_state;
} T_USB_HID;

/*
 *  クラスドライバ接続
 *  HID Keyboard クラスを直接接続 (マクロ間接呼出しを排除)
 */
IMPORT ER usb_hid_class_init(T_USB_MW *p_usb, UB idx);
IMPORT ER usb_hid_class_deinit(T_USB_MW *p_usb, UB idx);
IMPORT void usb_hid_class_setup(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);
IMPORT void usb_hid_class_data_in(T_USB_MW *p_usb, UB epnum);

#if defined(USB_HID_FIDO)
IMPORT void usb_hid_class_data_out(T_USB_MW *p_usb, UB epnum);
#define usb_class_data_out(a, b)	usb_hid_class_data_out((a), (b))

/*
 *  FIDO パケット送受信 (CTAPHID 層から呼ぶ)
 *    受信: EP2 OUT のパケットを recv_cb に渡す (USB 処理タスクの文脈)
 *    送信: 64 バイト 1 パケットを EP2 IN で送り、完了を待つ
 */
typedef void (*FP_USB_FIDO_RECV)(const UB *pkt);
IMPORT void usb_fido_set_recv_callback(FP_USB_FIDO_RECV recv_cb);
IMPORT ER   usb_fido_send(const UB *pkt, TMO tmout);
#else
#define usb_class_data_out(a, b)
#endif

/* 未使用クラスコールバック (空マクロ) */
#define usb_class_suspend(a)
#define usb_class_resume(a)
#define usb_class_ep0_rx_ready(a)
#define usb_class_ep0_tx_sent(a)
#define usb_class_in_incomplete(a, b)
#define usb_class_out_incomplete(a, b)

/*
 *  Configuration descriptor 外部参照
 */
extern UB usb_hid_config_desc[];

#define USB_HS_CFG_DESC		usb_hid_config_desc
#define USB_HS_CFG_DESC_LEN	USB_HID_CFG_DESC_LEN
#define USB_FS_CFG_DESC		usb_hid_config_desc
#define USB_FS_CFG_DESC_LEN	USB_HID_CFG_DESC_LEN
#define USB_OTR_CFG_DESC	usb_hid_config_desc
#define USB_OTR_CFG_DESC_LEN	USB_HID_CFG_DESC_LEN

/*
 *  ディスクリプタ外部参照 (usb_hid_class.c で定義)
 */
extern UB usb_device_descriptor[];
extern UB usb_langid_descriptor[];
extern UB usb_manufacturer_string[];
extern UB usb_product_string[];
extern UB usb_serial_string[];
extern UB usb_config_string[];
extern UB usb_interface_string[];

#define USB_DEVICE_DESC		usb_device_descriptor
#define USB_LANGID_DESC		usb_langid_descriptor
#define USB_MANUFACTURER_STR	usb_manufacturer_string
#define USB_PRODUCT_STR		usb_product_string
#define USB_SERIAL_STR		usb_serial_string
#define USB_CONFIG_STR		usb_config_string
#define USB_INTERFACE_STR	usb_interface_string
#define USB_USER_STR		((UB *)0)

/*
 *  ミドルウェア関数プロトタイプ
 */
IMPORT ER usb_hid_mw_init(T_USB_MW *p_usb, UB id);
IMPORT ER usb_hid_mw_deinit(T_USB_MW *p_usb);
IMPORT ER usb_hid_mw_start(T_USB_MW *p_usb);
IMPORT ER usb_hid_mw_stop(T_USB_MW *p_usb);

IMPORT ER usb_hid_on_setup(T_USB_MW *p_usb, UB *psetup);
IMPORT ER usb_hid_on_data_out(T_USB_MW *p_usb, UB epnum, UB *pdata);
IMPORT ER usb_hid_on_data_in(T_USB_MW *p_usb, UB epnum, UB *pdata);
IMPORT ER usb_hid_on_reset(T_USB_MW *p_usb);
IMPORT ER usb_hid_set_speed(T_USB_MW *p_usb, UB speed);
IMPORT ER usb_hid_on_suspend(T_USB_MW *p_usb);
IMPORT ER usb_hid_on_resume(T_USB_MW *p_usb);

IMPORT ER usb_ep0_send(T_USB_MW *p_usb, UB *buf, UH len);
IMPORT ER usb_ep0_recv(T_USB_MW *p_usb, UB *buf, UH len);
IMPORT ER usb_ep0_send_status(T_USB_MW *p_usb);
IMPORT ER usb_ep0_recv_status(T_USB_MW *p_usb);
IMPORT void usb_ep0_stall(T_USB_MW *p_usb, T_USB_SETUP_REQ *req);

IMPORT UB *usb_find_desc(T_USB_MW *p_usb, UB type);

/*
 *  HAL ドライバブリッジ関数 (usb_hid_rp2040.c で実装)
 */
IMPORT ER ll_drv_init(T_USB_MW *p_usb);
IMPORT ER ll_drv_deinit(T_USB_MW *p_usb);
IMPORT ER ll_drv_start(T_USB_MW *p_usb);
IMPORT ER ll_drv_stop(T_USB_MW *p_usb);
IMPORT ER ll_open_ep(T_USB_MW *p_usb, UB ep_addr, UB ep_type, UH ep_mps);
IMPORT ER ll_close_ep(T_USB_MW *p_usb, UB ep_addr);
IMPORT ER ll_start_tx(T_USB_MW *p_usb, UB ep_addr, UB *buf, UW len);
IMPORT ER ll_start_rx(T_USB_MW *p_usb, UB ep_num, UB *buf, UW len);
IMPORT ER ll_set_addr(T_USB_MW *p_usb, UB address);
IMPORT ER ll_stall_ep(T_USB_MW *p_usb, UB ep_addr);
IMPORT ER ll_unstall_ep(T_USB_MW *p_usb, UB ep_addr);
IMPORT BOOL ll_is_stalled(T_USB_MW *p_usb, UB ep_addr);

/*
 *  継続転送マクロ
 */
#define usb_ep0_continue_tx(d, b, l)  ll_start_tx((d), 0x00, (b), (l))
#define usb_ep0_continue_rx(d, b, l)  ll_start_rx((d), 0x00, (b), (l))

/*
 *  HID レポート送信関数
 */
IMPORT ER usb_hid_send_report(T_USB_MW *p_usb, UB *pdata, UH len);

#if defined(USB_HID_GAMEPAD)
/*
 *  ゲームパッドレポート送信 (待たない。EP3 IN が使用中なら E_BUSY)
 */
IMPORT ER usb_hid_send_pad_report(T_USB_MW *p_usb, const UB *pdata, UH len);
#endif

#endif	/* __USB_HID_H__ */
