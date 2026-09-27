/*
 * kb_brain.c — NUCLEO-H533RE キーボードブレイン
 *
 * スキャン周期 (SCAN_PERIOD_MS) ごとに:
 *   1. 左右の Pico (I2C スレーブ) から共有メモリ (CRC8 + 10 行) を読む
 *   2. wasm モジュールがアタッチされていれば kb_scan() で、
 *      なければ組み込みのキー処理 (kb_process.c) でレポートを作る
 *   3. 前回と変わっていれば USB HID ("usbk") に送る
 *      (GAMEPAD=1 ならゲームパッドのレポートも。組み込みのキー処理のみ)
 *
 * デバウンスは各 Pico が済ませているので、ここでは行わない。
 */

#include <sys/machine.h>
#include <stddef.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include <dev_i2c.h>
#include "../device/include/dev_usb_hid.h"

#include "kb_config.h"
#include "kb_process.h"
#include "split_transport.h"
#include "kb_brain.h"
#include "kb_wasm.h"

#define BRAIN_TASK_PRI		8
#define BRAIN_TASK_STKSZ	8192	/* wasm3 の解釈実行に使う */

/* 連続でこの回数読めなければ切断とみなしてキーを全解放する */
#define BRAIN_MAX_ERRORS	10

#define SCAN_EVT_TICK		(1u << 0)

IMPORT BOOL ll_is_suspended(void);
IMPORT ER   dev_usb_hid_remote_wakeup(void);

/*----------------------------------------------------------------------
 * 片手分の接続状態
 */
typedef struct {
	UB		addr;
	const char	*name;
	UB		errors;
	BOOL		connected;
	T_MATRIX_STATE	matrix;		/* 最後に読めた状態 */
} T_BRAIN_HALF;

LOCAL struct {
	ID		flgid;
	ID		i2c_dd;
	ID		usb_dd;
	T_BRAIN_HALF	left;
	T_BRAIN_HALF	right;
	T_KB_STATE	kb;
	T_HID_KBD_REPORT last_sent;
	T_HID_PAD_REPORT pad_sent;	/* 最後に送れたゲームパッドレポート */
	BOOL		usb_ready;
} brain;

/*======================================================================
 * スレーブ読み取り
 *====================================================================*/

/*
 * 共有メモリ先頭 (checksum, smatrix[10]) を 1 回の転送で読む。
 * スレーブ側の更新と重なって不整合になれば CRC で弾かれ、次の周期で読み直す。
 */
LOCAL ER read_half_once(T_BRAIN_HALF *h, T_MATRIX_STATE *out)
{
	T_I2C_EXEC exec;
	SZ asz;
	UB reg = SHMEM_OFF_CHECKSUM;
	UB buf[1 + MATRIX_ROWS_PER_HAND];
	ER err;

	exec.sadr     = h->addr;
	exec.snd_size = 1;
	exec.snd_data = &reg;
	exec.rcv_size = sizeof(buf);
	exec.rcv_data = buf;

	err = tk_swri_dev(brain.i2c_dd, TDN_I2C_EXEC, &exec, sizeof(exec), &asz);
	if (err < E_OK) return err;
	if (crc8(&buf[1], MATRIX_ROWS_PER_HAND) != buf[0]) return E_IO;

	memcpy(out->rows, &buf[1], MATRIX_ROWS_PER_HAND);
	return E_OK;
}

LOCAL void read_half(T_BRAIN_HALF *h)
{
	T_MATRIX_STATE m;

	if (read_half_once(h, &m) == E_OK) {
		h->matrix = m;
		h->errors = 0;
		if (!h->connected) {
			h->connected = TRUE;
			tm_printf((UB *)"Brain: %s half connected (0x%02x)\n", h->name, h->addr);
		}
		return;
	}

	/* 一時的な失敗なら前回の状態を保持する (キーが一瞬離れたことにしない) */
	if (h->errors < 255) h->errors++;
	if (h->errors == BRAIN_MAX_ERRORS) {
		memset(&h->matrix, 0, sizeof(h->matrix));
		if (h->connected) {
			h->connected = FALSE;
			tm_printf((UB *)"Brain: %s half disconnected\n", h->name);
		}
	}
}

/*======================================================================
 * USB HID 出力
 *====================================================================*/

EXPORT ER kb_brain_send_report(const T_HID_KBD_REPORT *report)
{
	SZ asize;
	ER err = E_IO;
	INT retry;

	if (brain.usb_dd < 0) return E_OBJ;

	/* サスペンド中にキーが押されたらホストを起こす */
	if (ll_is_suspended()) {
		UB any = report->modifier;
		INT i;
		for (i = 0; i < 6; i++) any |= report->keycode[i];
		if (any) dev_usb_hid_remote_wakeup();
	}

	for (retry = 0; retry < 50; retry++) {
		err = tk_swri_dev(brain.usb_dd, 0, (void *)report, sizeof(*report), &asize);
		if (err != E_BUSY) break;
		tk_dly_tsk(1);
	}

	if (err == E_OK) {
		brain.last_sent = *report;
		if (!brain.usb_ready) {
			brain.usb_ready = TRUE;
			tm_printf((UB *)"Brain: USB HID output active\n");
		}
	} else if (brain.usb_ready) {
		brain.usb_ready = FALSE;
		tm_printf((UB *)"Brain: USB HID send failed (%d)\n", err);
	}
	return err;
}

#if defined(USB_HID_GAMEPAD)
/*
 * ゲームパッドレポートを送る。待たないので、E_BUSY (前のレポートがまだ
 * ホストに読まれていない) なら次の周期に送り直す。
 */
LOCAL void brain_send_pad(const T_HID_PAD_REPORT *pad)
{
	SZ asize;
	ER err;

	if (brain.usb_dd < 0) return;

	if (ll_is_suspended()) {
		UB any = pad->buttons[0] | pad->buttons[1] | pad->buttons[2] | pad->buttons[3];
		if (any || pad->hat != HID_PAD_HAT_CENTER) dev_usb_hid_remote_wakeup();
	}

	err = tk_swri_dev(brain.usb_dd, USB_HID_DN_PAD, (void *)pad, sizeof(*pad), &asize);
	if (err == E_OK) brain.pad_sent = *pad;
}
#endif

/*======================================================================
 * スキャンタスク
 *====================================================================*/

LOCAL void scan_cyc_handler(void *exinf)
{
	(void)exinf;
	tk_set_flg(brain.flgid, SCAN_EVT_TICK);
}

LOCAL void brain_scan_once(void)
{
	T_HID_KBD_REPORT report;
	UB rows[MATRIX_ROWS];

	read_half(&brain.left);
	read_half(&brain.right);

	memcpy(&rows[0], brain.left.matrix.rows, MATRIX_ROWS_PER_HAND);
	memcpy(&rows[MATRIX_ROWS_PER_HAND], brain.right.matrix.rows, MATRIX_ROWS_PER_HAND);

	if (!kb_wasm_scan(rows, &report)) {
		kb_process_keys(&brain.kb, &brain.left.matrix, &brain.right.matrix);
		report = brain.kb.report;
	} else {
		/* wasm のキー処理はゲームパッドを出さない: 押したままにしない */
		kb_pad_report_clear(&brain.kb.pad);
	}

	if (memcmp(&report, &brain.last_sent, sizeof(report)) != 0) {
		kb_brain_send_report(&report);
	}

#if defined(USB_HID_GAMEPAD)
	if (memcmp(&brain.kb.pad, &brain.pad_sent, sizeof(brain.pad_sent)) != 0) {
		brain_send_pad(&brain.kb.pad);
	}
#endif
}

LOCAL void brain_task(INT stacd, void *exinf)
{
	(void)stacd; (void)exinf;

	brain.i2c_dd = tk_opn_dev((UB *)"iica", TD_UPDATE);
	if (brain.i2c_dd < E_OK) {
		tm_printf((UB *)"Brain: I2C open failed (%d)\n", brain.i2c_dd);
		tk_ext_tsk();
	}

	brain.usb_dd = tk_opn_dev((UB *)USB_HID_DEVNM, TD_UPDATE);
	if (brain.usb_dd < E_OK) {
		tm_printf((UB *)"Brain: USB HID open failed (%d)\n", brain.usb_dd);
	}

	/* 保存済みの wasm キー処理があれば取り付ける (wasm3 のためこのタスクで行う) */
	kb_wasm_boot();

	tm_printf((UB *)"Brain: scanning left=0x%02x right=0x%02x every %dms\n",
		  brain.left.addr, brain.right.addr, SCAN_PERIOD_MS);

	while (1) {
		UINT flgptn;
		tk_wai_flg(brain.flgid, SCAN_EVT_TICK, TWF_ORW | TWF_BITCLR,
			   &flgptn, TMO_FEVR);
		brain_scan_once();
	}
}

EXPORT ER kb_brain_start(void)
{
	T_CFLG cflg;
	T_CCYC ccyc;
	T_CTSK ctsk;
	ID id;

	memset(&brain, 0, sizeof(brain));
	brain.i2c_dd = -1;
	brain.usb_dd = -1;
	brain.left.addr  = BRAIN_I2C_ADDR_LEFT;
	brain.left.name  = "left";
	brain.right.addr = BRAIN_I2C_ADDR_RIGHT;
	brain.right.name = "right";
	kb_process_init(&brain.kb);

	/* 起動直後に全解放レポートを 1 回送るため、送信済み値を無効にしておく */
	memset(&brain.last_sent, 0xFF, sizeof(brain.last_sent));
	memset(&brain.pad_sent, 0xFF, sizeof(brain.pad_sent));

	cflg.exinf   = NULL;
	cflg.flgatr  = TA_TPRI | TA_WMUL;
	cflg.iflgptn = 0;
	brain.flgid = tk_cre_flg(&cflg);
	if (brain.flgid < E_OK) return brain.flgid;

	ccyc.exinf  = NULL;
	ccyc.cycatr = TA_HLNG | TA_STA;
	ccyc.cychdr = (FP)scan_cyc_handler;
	ccyc.cyctim = SCAN_PERIOD_MS;
	ccyc.cycphs = 0;
	id = tk_cre_cyc(&ccyc);
	if (id < E_OK) return id;

	ctsk.exinf   = NULL;
	ctsk.tskatr  = TA_HLNG | TA_RNG3;
	ctsk.task    = (FP)brain_task;
	ctsk.itskpri = BRAIN_TASK_PRI;
	ctsk.stksz   = BRAIN_TASK_STKSZ;
	ctsk.bufptr  = NULL;
	id = tk_cre_tsk(&ctsk);
	if (id < E_OK) return id;
	return tk_sta_tsk(id, 0);
}

EXPORT BOOL kb_brain_left_connected(void)  { return brain.left.connected; }
EXPORT BOOL kb_brain_right_connected(void) { return brain.right.connected; }

/*======================================================================
 * MCP から参照するキーボード状態 (tk_mcp_tools.c の weak 定義を上書き)
 *====================================================================*/

EXPORT UB kb_get_active_layers(void)
{
	if (kb_wasm_active()) {
		T_KB_WASM_STATUS st;
		kb_wasm_get_status(&st);
		return st.layers;
	}
	return brain.kb.active_layers;
}

EXPORT UB kb_get_current_layer(void)
{
	UB active = kb_get_active_layers();
	INT i;
	for (i = 7; i >= 0; i--) {
		if (active & (1u << i)) return (UB)i;
	}
	return 0;
}

EXPORT void kb_set_toggled_layer(UB layer_num, BOOL on)
{
	UB mask;

	if (layer_num == 0 || layer_num >= 8) return;
	mask = (UB)(1u << layer_num);
	if (on) brain.kb.toggled_layers |= mask;
	else    brain.kb.toggled_layers &= (UB)~mask;
}

EXPORT void kb_clear_toggled_layers(void)
{
	brain.kb.toggled_layers = 0;
}

EXPORT const char *kb_output_active_name(void)
{
	if (kb_wasm_active()) return "USB (wasm keymap)";
	return "USB";
}
