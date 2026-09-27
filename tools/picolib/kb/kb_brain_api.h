/*
 * kb_brain_api.h — NUCLEO-H533RE キーボードブレイン用 wasm API
 *
 * wasm モジュールがキー処理を担うとき、ホスト (μT-Kernel) から
 * import する関数と、モジュールが export する関数の宣言。
 *
 * ビルド:
 *   clang --target=wasm32 -nostdlib -O2 -Wl,--no-entry -Wl,--allow-undefined \
 *         -Wl,--initial-memory=65536 -Wl,--max-memory=65536 \
 *         -Wl,-z,stack-size=4096 -o kb.wasm kb.c
 *
 * 実行:
 *   MCP の wasm_begin / wasm_chunk でアップロードし、wasm_run ではなく
 *   kb_wasm_attach {slot} でキー処理として取り付ける。
 */

#ifndef __KB_BRAIN_API_H__
#define __KB_BRAIN_API_H__

#include <stdint.h>

/*----------------------------------------------------------------------
 * マトリクス / キーマップ
 *   行 0..9 が左手、10..19 が右手。各行の bit0..3 が列 0..3。
 */
#define KB_ROWS		20
#define KB_COLS		4
#define KB_LAYERS	3

/* 行 row の押下状態 (デバウンス済み) */
int  kb_row(int row);

/* ファームウェアに組み込まれたキーマップのキーコード (16bit) */
int  kb_keymap(int layer, int row, int col);

/*----------------------------------------------------------------------
 * HID 出力
 *   kb_report() で次に送るレポートを設定する。kb_scan() から戻った時点の
 *   レポートが前回と異なればホストが送信する。
 *   マクロなど 1 回の kb_scan() で複数のレポートを送りたいときは、
 *   kb_report() → kb_send() を繰り返す。
 */
void kb_report(int modifier, int k0, int k1, int k2, int k3, int k4, int k5);
int  kb_send(void);

/* MCP の get_keyboard_status に見せるレイヤー状態 (bit n = レイヤー n) */
void kb_set_layers(int mask);

/* 起動からのミリ秒 */
int  kb_now_ms(void);

/* コンソール出力 (最大 96 文字) */
int  log_printf(const char *msg, uint32_t len);
#define KB_LOG(s)	log_printf((s), sizeof(s) - 1)

/*----------------------------------------------------------------------
 * モジュールが export する関数
 */
#define KB_EXPORT(name)	__attribute__((export_name(#name)))

/*----------------------------------------------------------------------
 * キーコード (ファームウェアの kb_hid_keycodes.h と同じ値)
 */
#define KC_NO		0x0000
#define KC_TRNS		0xFFFF
#define KC_ACTION_MASK	0xF000
#define KC_ACTION_MO	0x1000		/* MO(n): 押している間レイヤー n */
#define KC_ACTION_TG	0x2000		/* TG(n): 押すたびにレイヤー n を切替 */
#define KC_ACTION_XMK	0x3000		/* 拡張アクション (ブレインでは未使用) */
#define KC_ACTION_CS	0xF000		/* Ctrl+Shift+キー (キーマップの LSFT_LCTL_KC) */

#define KC_A		0x04
#define KC_J		0x0D
#define KC_K		0x0E
#define KC_ESC		0x29
#define KC_LCTRL	0xE0
#define KC_LSHIFT	0xE1

#define MOD_LCTRL	0x01
#define MOD_LSHIFT	0x02

#endif /* __KB_BRAIN_API_H__ */
