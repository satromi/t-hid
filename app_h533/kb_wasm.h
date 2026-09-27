/*
 * kb_wasm.h — wasm モジュールによるキー処理の差し替え
 *
 * wasm モジュールが kb_scan() を export していれば、スキャンごとに
 * 組み込みのキー処理 (kb_process.c) の代わりにそれを呼ぶ。
 *
 * モジュールから使えるホスト関数 (import module "env"):
 *   i32  kb_row(i32 row)                       マトリクス行 (0..19) のビット列
 *   i32  kb_keymap(i32 layer, i32 row, i32 col) 組み込みキーマップのキーコード
 *   void kb_report(i32 mod, i32 k0..k5)        次に送る HID レポートを設定
 *   i32  kb_send(void)                         設定済みレポートを今すぐ送る
 *   void kb_set_layers(i32 mask)               MCP に見せるレイヤー状態
 *   i32  kb_now_ms(void)                       起動からのミリ秒
 *   i32  log_printf(const char *msg, i32 len)  コンソール出力
 *
 * モジュールが export する関数:
 *   void kb_scan(void)   必須。スキャン周期 (2ms) ごとに呼ばれる
 *   void kb_init(void)   任意。アタッチ直後に 1 回呼ばれる
 *
 * kb_scan() 終了時点のレポートが前回送信分と異なれば送信する。
 */

#ifndef __KB_WASM_H__
#define __KB_WASM_H__

#include <tk/tkernel.h>
#include "kb_output.h"

/* 初期化 (起動時に 1 回) */
ER   kb_wasm_init(void);

/* tk_wasm の slot に受信済みのモジュールをキー処理としてアタッチ */
ER   kb_wasm_attach(INT slot);

/* アタッチ中のモジュールを外し、組み込みのキー処理に戻す */
ER   kb_wasm_detach(void);

/* アタッチ中か */
BOOL kb_wasm_active(void);

/* アタッチ中のモジュールを Flash に保存 (次回起動時に自動アタッチ) */
ER   kb_wasm_save(void);

/* 保存済みモジュールを消去 */
ER   kb_wasm_erase_saved(void);

/* 保存済みモジュールのサイズ (無ければ 0) */
UW   kb_wasm_saved_size(void);

/* 保存済みモジュールを読み込んでアタッチ (スタックに余裕のあるタスクから呼ぶ) */
ER   kb_wasm_boot(void);

/*
 * 1 スキャン分の処理。rows は左 10 行 + 右 10 行。
 * 処理した (アタッチ中) なら TRUE を返し、送るべきレポートを *report に入れる。
 * trap した場合はモジュールを無効化して FALSE を返す。
 */
BOOL kb_wasm_scan(const UB rows[20], T_HID_KBD_REPORT *report);

/* 状態取得 (MCP 用) */
typedef struct {
	BOOL	active;
	UW	module_size;
	UW	scan_count;
	UW	max_scan_us;		/* kb_scan() の最大実行時間 */
	UB	layers;			/* kb_set_layers() で通知された値 */
	char	last_error[48];
} T_KB_WASM_STATUS;

void kb_wasm_get_status(T_KB_WASM_STATUS *st);

#endif /* __KB_WASM_H__ */
