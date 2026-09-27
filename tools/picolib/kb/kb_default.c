/*
 * kb_default.c — キーボードブレイン用 wasm キー処理
 *
 * ファームウェア組み込みのキー処理 (kb_process.c) と同じ規則で
 * MO/TG レイヤーと 6KRO レポートを作り、さらに次を追加する。
 *
 *   - Ctrl+Shift+キー (キーマップの LSFT_LCTL_KC, 例: TEAMS = Ctrl+Shift+M)
 *   - コンボ: J と K を同時に押すと Esc
 *
 * -DKB_PARITY でビルドすると追加機能を外し、組み込み処理と
 * 完全に同じレポートを出す (tools/kb_wasm_test の突き合わせ用)。
 */

#include "kb_brain_api.h"

static int toggled;		/* TG で有効にしたレイヤー */
static int active = 1;		/* 前回スキャンで確定したレイヤー */
static int prev[KB_ROWS];	/* 押下エッジ検出用 */

static int lookup(int row, int col, int layers)
{
	for (int layer = KB_LAYERS - 1; layer >= 0; layer--) {
		if (!(layers & (1 << layer))) continue;
		int kc = kb_keymap(layer, row, col);
		if (kc != KC_TRNS) return kc;
	}
	return KC_NO;
}

KB_EXPORT(kb_init) void kb_init(void)
{
	toggled = 0;
	active = 1;
	for (int r = 0; r < KB_ROWS; r++) prev[r] = 0;
	KB_LOG("kb_default: loaded");
}

KB_EXPORT(kb_scan) void kb_scan(void)
{
	int rows[KB_ROWS];
	int momentary = 0;

	for (int r = 0; r < KB_ROWS; r++) rows[r] = kb_row(r);

	/* 1 回目: レイヤー操作キーを先に解決する */
	for (int r = 0; r < KB_ROWS; r++) {
		for (int c = 0; c < KB_COLS; c++) {
			if (!(rows[r] & (1 << c))) continue;
			int kc = lookup(r, c, active);
			int type = kc & KC_ACTION_MASK;
			if (type == KC_ACTION_MO) {
				momentary |= 1 << (kc & 0x0FFF);
			} else if (type == KC_ACTION_TG && !(prev[r] & (1 << c))) {
				toggled ^= 1 << (kc & 0x0FFF);
			}
		}
	}
	active = 1 | momentary | toggled;

	/* 2 回目: 修飾キーと通常キーを集める */
	int mod = 0;
	int keys[6] = { 0, 0, 0, 0, 0, 0 };
	int n = 0;

	for (int r = 0; r < KB_ROWS; r++) {
		for (int c = 0; c < KB_COLS; c++) {
			if (!(rows[r] & (1 << c))) continue;
			int kc = lookup(r, c, active);
			int type = kc & KC_ACTION_MASK;

#ifndef KB_PARITY
			if (type == KC_ACTION_CS) {
				mod |= MOD_LCTRL | MOD_LSHIFT;
				if (n < 6) keys[n++] = kc & 0xFF;
				continue;
			}
#endif
			if (type != 0 || kc == KC_NO || kc == KC_TRNS) continue;

			if (kc >= 0xE0 && kc <= 0xE7) {
				mod |= 1 << (kc - 0xE0);
			} else if (n < 6) {
				keys[n++] = kc;
			}
		}
	}

#ifndef KB_PARITY
	/* コンボ: J と K が同時に押されていれば両方を消して Esc にする */
	{
		int j = -1, k = -1;
		for (int i = 0; i < n; i++) {
			if (keys[i] == KC_J) j = i;
			if (keys[i] == KC_K) k = i;
		}
		if (j >= 0 && k >= 0) {
			int out = 0;
			for (int i = 0; i < n; i++) {
				if (i != j && i != k) keys[out++] = keys[i];
			}
			keys[out++] = KC_ESC;
			while (out < 6) keys[out++] = 0;
			n = out;
		}
	}
#endif

	for (int r = 0; r < KB_ROWS; r++) prev[r] = rows[r];

	kb_set_layers(active);
	kb_report(mod, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]);
}
