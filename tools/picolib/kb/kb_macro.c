/*
 * kb_macro.c — マクロ (文字列入力) の例
 *
 * Insert キーを押すと "hello" と Enter を入力する。その他のキーは
 * kb_minimal.c と同じくベースレイヤーのまま送る。
 *
 * 1 回の kb_scan() の中で kb_report() → kb_send() を繰り返すと、
 * 複数のレポートを順に送れる。kb_send() は 1 回あたり USB の送信完了
 * (最大 10ms 程度) まで待つので、長い文字列はスキャンを止める点に注意。
 */

#include "kb_brain_api.h"

#define KC_INS	0x49
#define KC_ENT	0x28

static int prev_ins;	/* Insert が押されていたか (押下エッジの検出) */

static void tap(int mod, int key)
{
	kb_report(mod, key, 0, 0, 0, 0, 0);	/* 押す */
	kb_send();
	kb_report(0, 0, 0, 0, 0, 0, 0);		/* 離す */
	kb_send();
}

static void type_hello(void)
{
	static const unsigned char keys[] = { 0x0B, 0x08, 0x0F, 0x0F, 0x12 };	/* h e l l o */
	for (unsigned i = 0; i < sizeof(keys); i++) tap(0, keys[i]);
	tap(0, KC_ENT);
}

KB_EXPORT(kb_scan) void kb_scan(void)
{
	int mod = 0;
	int keys[6] = { 0 };
	int n = 0;
	int ins = 0;

	for (int r = 0; r < KB_ROWS; r++) {
		int bits = kb_row(r);
		for (int c = 0; c < KB_COLS; c++) {
			if (!(bits & (1 << c))) continue;

			int kc = kb_keymap(0, r, c);
			if (kc == KC_INS) { ins = 1; continue; }	/* マクロ用に横取り */
			if (kc == KC_NO || kc == KC_TRNS || (kc & KC_ACTION_MASK)) continue;

			if (kc >= 0xE0 && kc <= 0xE7) {
				mod |= 1 << (kc - 0xE0);
			} else if (n < 6) {
				keys[n++] = kc;
			}
		}
	}

	if (ins && !prev_ins) {
		KB_LOG("macro: hello");
		type_hello();
	}
	prev_ins = ins;

	kb_report(mod, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]);
}
