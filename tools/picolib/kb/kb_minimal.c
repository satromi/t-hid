/*
 * kb_minimal.c — 最小のキー処理モジュール
 *
 * ベースレイヤー (レイヤー 0) だけを使い、押されているキーを
 * そのまま HID レポートにする。レイヤー切替やマクロはない。
 * 自作モジュールの出発点として使う。
 */

#include "kb_brain_api.h"

KB_EXPORT(kb_scan) void kb_scan(void)
{
	int mod = 0;
	int keys[6] = { 0 };
	int n = 0;

	for (int r = 0; r < KB_ROWS; r++) {
		int bits = kb_row(r);
		for (int c = 0; c < KB_COLS; c++) {
			if (!(bits & (1 << c))) continue;

			int kc = kb_keymap(0, r, c);
			if (kc == KC_NO || kc == KC_TRNS || (kc & KC_ACTION_MASK)) continue;

			if (kc >= 0xE0 && kc <= 0xE7) {
				mod |= 1 << (kc - 0xE0);	/* 修飾キーはビットで送る */
			} else if (n < 6) {
				keys[n++] = kc;
			}
		}
	}
	kb_report(mod, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]);
}
