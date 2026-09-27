/*
 * kb_taphold.c — タップ/ホールドの例 (時間を使う処理)
 *
 * Space キーを
 *   200ms 未満で離す (タップ) → Space
 *   200ms 以上押し続ける (ホールド) → Shift
 * として扱う。ホールド中に押した他のキーには Shift が付く。
 * その他のキーは kb_minimal.c と同じくベースレイヤーのまま送る。
 *
 * kb_now_ms() で押した時刻を覚え、スキャンごとに経過時間を見る。
 */

#include "kb_brain_api.h"

#define KC_SPC		0x2C
#define HOLD_MS		200

static int space_down;		/* Space を押している */
static int space_since;		/* 押し始めた時刻 */
static int space_used;		/* ホールド中に他のキーを押した */

KB_EXPORT(kb_scan) void kb_scan(void)
{
	int mod = 0;
	int keys[6] = { 0 };
	int n = 0;
	int space = 0;
	int now = kb_now_ms();

	for (int r = 0; r < KB_ROWS; r++) {
		int bits = kb_row(r);
		for (int c = 0; c < KB_COLS; c++) {
			if (!(bits & (1 << c))) continue;

			int kc = kb_keymap(0, r, c);
			if (kc == KC_SPC) { space = 1; continue; }
			if (kc == KC_NO || kc == KC_TRNS || (kc & KC_ACTION_MASK)) continue;

			if (kc >= 0xE0 && kc <= 0xE7) {
				mod |= 1 << (kc - 0xE0);
			} else if (n < 6) {
				keys[n++] = kc;
			}
		}
	}

	if (space && !space_down) {		/* 押した */
		space_down = 1;
		space_since = now;
		space_used = 0;
	}

	if (space_down) {
		if (n > 0) space_used = 1;
		/* ホールド判定になった、または他のキーと組み合わせたら Shift */
		if (now - space_since >= HOLD_MS || space_used) mod |= MOD_LSHIFT;
	}

	if (!space && space_down) {		/* 離した */
		space_down = 0;
		if (now - space_since < HOLD_MS && !space_used) {
			/* タップ: Space を 1 回送る */
			kb_report(mod, KC_SPC, 0, 0, 0, 0, 0);
			kb_send();
		}
	}

	kb_report(mod, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]);
}
