/*
 * kb_wasm_test.c — キーボードブレインの wasm キー処理を PC 上で検証する
 *
 * app_h533/kb_wasm.c (実機と同じソース) と wasm3 をネイティブビルドし、
 * μT-Kernel API は shim/ の最小代替で置き換える。
 *
 *   1. 同等性: kb_default.c を -DKB_PARITY でビルドしたモジュールと
 *      組み込みのキー処理 (kb_process.c) に同じ入力列を与え、
 *      毎スキャンのレポートとレイヤー状態が一致することを確認する
 *   2. 追加機能: Ctrl+Shift+キー と J+K コンボ
 *   3. 異常系: trap したモジュールは無効化され組み込み処理に戻る /
 *      kb_scan を持たないモジュールは attach で拒否される
 *   4. detach / 再 attach
 *   5. Flash 保存と起動時の読み込み (Flash は RAM 上の配列で代替)
 *   6. ホストが提供しない関数を import するモジュールは attach で拒否される
 *
 * --check module.wasm で自作モジュールの事前チェックもできる (make check)。
 *
 * キーマップは test_keymap.c (既定のキーマップに TEAMS と TG(2) を足したもの)。
 *
 * 使い方: make run / make check MOD=... (tools/kb_wasm_test/Makefile)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kb_process.h"
#include "kb_keymap.h"
#include "kb_wasm.h"
#include "../../device/flash/stm32h5_flash.h"

/*======================================================================
 * μT-Kernel API の代替
 *====================================================================*/

LOCAL UB *g_module;
LOCAL UW  g_module_size;
LOCAL UW  g_now_ms;

ER   CreateLock(FastLock *lock, CONST UB *name) { (void)lock; (void)name; return E_OK; }
void Lock(FastLock *lock)   { (void)lock; }
void Unlock(FastLock *lock) { (void)lock; }
ER   tk_get_otm(SYSTIM *tim) { tim->hi = 0; tim->lo = g_now_ms; return E_OK; }
void Kfree(void *p) { free(p); }
UW   in_w(UW addr) { (void)addr; return 0; }
void out_w(UW addr, UW val) { (void)addr; (void)val; }

/* tk_wasm の slot から受信済みモジュールを引き取る代わりにファイルの中身を渡す */
ER tk_wasm_take_bytecode(INT slot, UB **pbuf, UW *psize)
{
	(void)slot;
	if (g_module == NULL) return E_OBJ;
	*pbuf  = g_module;
	*psize = g_module_size;
	g_module = NULL;
	return E_OK;
}

/* Flash データ領域の代わり (消去状態 = 0xFF) */
UB fake_flash[512 * 1024];

void *Kmalloc(size_t size) { return malloc(size); }

ER h5_flash_erase(UW offset, UW len)
{
	UW start = offset & ~(8192u - 1);
	UW end = (offset + len + 8191u) & ~(8192u - 1);
	memset(&fake_flash[start], 0xFF, end - start);
	return E_OK;
}

ER h5_flash_program(UW offset, const void *data, UW len)
{
	const UB *src = (const UB *)data;
	UW i;
	if (offset % 16) return E_PAR;
	for (i = 0; i < len; i++) {
		/* 消去済みでない場所への書き込みは実機ではエラー */
		if (fake_flash[offset + i] != 0xFF) return E_IO;
		fake_flash[offset + i] = src[i];
	}
	return E_OK;
}

/* kb_send() で送られたレポートを記録する */
LOCAL T_HID_KBD_REPORT g_sent[16];
LOCAL INT g_nsent;

ER kb_brain_send_report(const T_HID_KBD_REPORT *report)
{
	if (g_nsent < 16) g_sent[g_nsent] = *report;
	g_nsent++;
	return E_OK;
}

/*======================================================================
 * ヘルパー
 *====================================================================*/

LOCAL INT failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { failures++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } \
} while (0)

LOCAL void load_module(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (fp == NULL) { printf("cannot open %s\n", path); exit(2); }
	fseek(fp, 0, SEEK_END);
	g_module_size = (UW)ftell(fp);
	fseek(fp, 0, SEEK_SET);
	g_module = malloc(g_module_size);
	if (fread(g_module, 1, g_module_size, fp) != g_module_size) exit(2);
	fclose(fp);
}

LOCAL void rows_to_halves(const UB rows[MATRIX_ROWS], T_MATRIX_STATE *l, T_MATRIX_STATE *r)
{
	memcpy(l->rows, &rows[0], MATRIX_ROWS_PER_HAND);
	memcpy(r->rows, &rows[MATRIX_ROWS_PER_HAND], MATRIX_ROWS_PER_HAND);
}

/* キーマップ上で kc を最初に見つけた (layer, row, col) */
LOCAL BOOL find_key(keycode_t kc, INT *layer, INT *row, INT *col)
{
	INT l, r, c;
	for (l = 0; l < NUM_LAYERS; l++)
		for (r = 0; r < MATRIX_ROWS; r++)
			for (c = 0; c < MATRIX_COLS; c++)
				if (keymaps[l][r][c] == kc) { *layer = l; *row = r; *col = c; return TRUE; }
	return FALSE;
}

LOCAL BOOL report_has_key(const T_HID_KBD_REPORT *rep, UB kc)
{
	INT i;
	for (i = 0; i < 6; i++) if (rep->keycode[i] == kc) return TRUE;
	return FALSE;
}

LOCAL UW rng_state = 12345;
LOCAL UW rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return rng_state;
}

/*======================================================================
 * 1. 同等性
 *====================================================================*/

LOCAL void test_parity(const char *path, INT scans)
{
	T_KB_STATE st;
	T_KB_WASM_STATUS ws;
	UB rows[MATRIX_ROWS];
	UB held_r[8], held_c[8];
	INT nheld = 0;
	INT i, k, mismatch = 0, layer_mismatch = 0, layer_changes = 0;
	UB last_layers = 1;

	printf("[parity] %s, %d scans\n", path, scans);
	load_module(path);
	CHECK(kb_wasm_attach(0) == E_OK, "attach");
	kb_process_init(&st);

	/*
	 * MO/TG キーの位置 (入力を偏らせてレイヤー操作を頻繁に起こす)。
	 * TG は MO で入るレイヤーに置かれていてもよい。
	 */
	INT lyr, mo_r = -1, mo_c = -1, tg_r = -1, tg_c = -1;
	INT r, c, tg_toggles = 0;
	for (lyr = 0; lyr < NUM_LAYERS; lyr++)
		for (r = 0; r < MATRIX_ROWS; r++)
			for (c = 0; c < MATRIX_COLS; c++) {
				keycode_t kc = keymaps[lyr][r][c];
				if (kc == KC_TRNS) continue;
				if ((kc & 0xF000) == KC_ACTION_MO && mo_r < 0 && lyr == 0) { mo_r = r; mo_c = c; }
				if ((kc & 0xF000) == KC_ACTION_TG && tg_r < 0) { tg_r = r; tg_c = c; }
			}
	printf("  MO key %s, TG key %s\n", mo_r >= 0 ? "found" : "none", tg_r >= 0 ? "found" : "none");

	for (i = 0; i < scans; i++) {
		T_MATRIX_STATE L, R;
		T_HID_KBD_REPORT wrep;

		/* 数スキャンに 1 回、キーを 1 つ押すか離す */
		if ((rng() % 4) == 0) {
			if (nheld > 0 && (nheld >= 8 || (rng() % 2))) {
				k = rng() % nheld;
				held_r[k] = held_r[nheld - 1];
				held_c[k] = held_c[nheld - 1];
				nheld--;
			} else {
				UW pick = rng() % 10;
				if (pick == 0 && mo_r >= 0)      { held_r[nheld] = mo_r; held_c[nheld] = mo_c; }
				else if (pick == 1 && tg_r >= 0) { held_r[nheld] = tg_r; held_c[nheld] = tg_c; }
				else { held_r[nheld] = rng() % MATRIX_ROWS; held_c[nheld] = rng() % MATRIX_COLS; }
				nheld++;
			}
		}
		memset(rows, 0, sizeof(rows));
		for (k = 0; k < nheld; k++) rows[held_r[k]] |= (UB)(1u << held_c[k]);

		rows_to_halves(rows, &L, &R);
		kb_process_keys(&st, &L, &R);
		g_now_ms += 2;

		if (!kb_wasm_scan(rows, &wrep)) {
			CHECK(0, "kb_wasm_scan returned FALSE at scan %d", i);
			break;
		}
		if (memcmp(&wrep, &st.report, sizeof(wrep)) != 0) {
			if (mismatch < 5) {
				printf("  mismatch at scan %d: builtin mod=%02x k=%02x %02x %02x / wasm mod=%02x k=%02x %02x %02x\n",
				       i, st.report.modifier, st.report.keycode[0], st.report.keycode[1],
				       st.report.keycode[2], wrep.modifier, wrep.keycode[0],
				       wrep.keycode[1], wrep.keycode[2]);
			}
			mismatch++;
		}
		kb_wasm_get_status(&ws);
		if (ws.layers != st.active_layers) layer_mismatch++;
		if ((st.active_layers ^ last_layers) & st.toggled_layers & ~last_layers) tg_toggles++;
		if (st.active_layers != last_layers) { layer_changes++; last_layers = st.active_layers; }
	}

	kb_wasm_get_status(&ws);
	printf("  report mismatches=%d, layer mismatches=%d, layer changes=%d (TG on=%d), scans=%u\n",
	       mismatch, layer_mismatch, layer_changes, tg_toggles, ws.scan_count);
	CHECK(mismatch == 0, "report mismatch");
	CHECK(layer_mismatch == 0, "layer mismatch");
	CHECK(layer_changes > 100, "layer operations were not exercised");
	CHECK(tg_r < 0 || tg_toggles > 10, "TG was not exercised");
	kb_wasm_detach();
}

/*======================================================================
 * 2. 追加機能
 *====================================================================*/

LOCAL void press_and_scan(const INT *r, const INT *c, INT n, T_HID_KBD_REPORT *rep)
{
	UB rows[MATRIX_ROWS];
	INT i;
	memset(rows, 0, sizeof(rows));
	for (i = 0; i < n; i++) rows[r[i]] |= (UB)(1u << c[i]);
	CHECK(kb_wasm_scan(rows, rep), "kb_wasm_scan");
}

LOCAL void test_features(const char *path)
{
	T_HID_KBD_REPORT rep;
	INT l, r[2], c[2], lj, lk;

	printf("[features] %s\n", path);
	load_module(path);
	CHECK(kb_wasm_attach(0) == E_OK, "attach");

	/* J+K → Esc (J, K ともベースレイヤーにある前提) */
	if (find_key(KC_J, &lj, &r[0], &c[0]) && find_key(KC_K, &lk, &r[1], &c[1]) &&
	    lj == 0 && lk == 0) {
		press_and_scan(r, c, 1, &rep);
		CHECK(report_has_key(&rep, KC_J) && !report_has_key(&rep, KC_ESC), "J alone");
		press_and_scan(r, c, 2, &rep);
		CHECK(report_has_key(&rep, KC_ESC), "J+K should give Esc");
		CHECK(!report_has_key(&rep, KC_J) && !report_has_key(&rep, KC_K), "J+K should hide J,K");
		printf("  J+K combo: keycode[0]=%02x\n", rep.keycode[0]);
	} else {
		printf("  J/K not on base layer, combo test skipped\n");
	}
	press_and_scan(r, c, 0, &rep);

	/* Ctrl+Shift+キー (キーマップの LSFT_LCTL_KC) */
	{
		INT rr, cc, found = 0;
		for (l = 0; l < NUM_LAYERS && !found; l++)
			for (rr = 0; rr < MATRIX_ROWS && !found; rr++)
				for (cc = 0; cc < MATRIX_COLS && !found; cc++)
					if ((keymaps[l][rr][cc] & 0xF000) == 0xF000 &&
					    keymaps[l][rr][cc] != KC_TRNS) {
						found = 1;
						UB key = (UB)(keymaps[l][rr][cc] & 0xFF);
						INT pr[2], pc[2], n = 0;
						if (l > 0) {
							/* そのレイヤーを MO で有効にする */
							INT ml, mr, mc;
							if (!find_key(KC_MO(l), &ml, &mr, &mc)) { found = 2; break; }
							pr[n] = mr; pc[n] = mc; n++;
							press_and_scan(pr, pc, n, &rep);
						}
						pr[n] = rr; pc[n] = cc; n++;
						press_and_scan(pr, pc, n, &rep);
						CHECK(rep.modifier == 0x03 && report_has_key(&rep, key),
						      "Ctrl+Shift key: mod=%02x key=%02x", rep.modifier, rep.keycode[0]);
						printf("  Ctrl+Shift key (layer %d): mod=%02x keycode=%02x\n",
						       l, rep.modifier, rep.keycode[0]);
					}
		if (!found) printf("  no LSFT_LCTL_KC in keymap, skipped\n");
	}
	kb_wasm_detach();
}

/*======================================================================
 * 3. 異常系 / 4. detach
 *====================================================================*/

LOCAL void test_errors(const char *trap_path, const char *noscan_path, const char *ok_path)
{
	T_HID_KBD_REPORT rep;
	T_KB_WASM_STATUS ws;
	UB rows[MATRIX_ROWS];

	memset(rows, 0, sizeof(rows));

	printf("[trap] %s\n", trap_path);
	load_module(trap_path);
	CHECK(kb_wasm_attach(0) == E_OK, "attach trap module");
	CHECK(kb_wasm_active(), "active after attach");
	CHECK(!kb_wasm_scan(rows, &rep), "trap should return FALSE");
	kb_wasm_get_status(&ws);
	CHECK(!ws.active, "inactive after trap");
	CHECK(ws.last_error[0] != '\0', "last_error set");
	printf("  last_error=\"%s\"\n", ws.last_error);
	CHECK(!kb_wasm_scan(rows, &rep), "falls back to builtin");

	printf("[no kb_scan] %s\n", noscan_path);
	load_module(noscan_path);
	CHECK(kb_wasm_attach(0) == E_NOEXS, "attach without kb_scan must fail");
	CHECK(!kb_wasm_active(), "not active");

	printf("[detach/reattach] %s\n", ok_path);
	load_module(ok_path);
	CHECK(kb_wasm_attach(0) == E_OK, "attach");
	CHECK(kb_wasm_scan(rows, &rep), "scan");
	kb_wasm_detach();
	CHECK(!kb_wasm_active(), "inactive after detach");
	CHECK(!kb_wasm_scan(rows, &rep), "builtin after detach");
	load_module(ok_path);
	CHECK(kb_wasm_attach(0) == E_OK, "reattach");
	g_nsent = 0;
	CHECK(kb_wasm_scan(rows, &rep), "scan after reattach");
	kb_wasm_detach();

	CHECK(kb_wasm_attach(0) == E_OBJ, "attach with empty slot must fail");
}

/*======================================================================
 * 5. Flash 保存と起動時の読み込み
 *====================================================================*/

LOCAL void test_persist(const char *path)
{
	T_HID_KBD_REPORT rep;
	UB rows[MATRIX_ROWS];
	UW saved;

	printf("[persist] %s\n", path);
	memset(fake_flash, 0xFF, sizeof(fake_flash));
	memset(rows, 0, sizeof(rows));

	CHECK(kb_wasm_saved_size() == 0, "no saved module initially");
	CHECK(kb_wasm_boot() == E_NOEXS, "boot without saved module");
	CHECK(kb_wasm_save() == E_OBJ, "save without attached module");

	load_module(path);
	CHECK(kb_wasm_attach(0) == E_OK, "attach");
	saved = g_module_size;
	CHECK(kb_wasm_save() == E_OK, "save");
	CHECK(kb_wasm_saved_size() == saved, "saved size %u", kb_wasm_saved_size());
	/* 2 回目の保存 (消去 → 書き込みが繰り返せること) */
	CHECK(kb_wasm_save() == E_OK, "save again");
	kb_wasm_detach();

	CHECK(kb_wasm_boot() == E_OK, "boot from flash");
	CHECK(kb_wasm_active(), "active after boot");
	CHECK(kb_wasm_scan(rows, &rep), "scan after boot");
	kb_wasm_detach();

	/* 本体が壊れていれば読み込まない */
	fake_flash[H5_FLASH_KBWASM_OFFSET + 64 + 10] ^= 0x01;
	CHECK(kb_wasm_boot() == E_IO, "corrupted module must be rejected");
	CHECK(!kb_wasm_active(), "inactive after corrupted boot");
	fake_flash[H5_FLASH_KBWASM_OFFSET + 64 + 10] ^= 0x01;

	CHECK(kb_wasm_erase_saved() == E_OK, "erase");
	CHECK(kb_wasm_saved_size() == 0, "no saved module after erase");
	CHECK(kb_wasm_boot() == E_NOEXS, "boot after erase");
}

/*======================================================================
 * 6. 未知の import
 *====================================================================*/

LOCAL void test_bad_import(const char *path)
{
	T_KB_WASM_STATUS ws;

	printf("[unknown import] %s\n", path);
	load_module(path);
	CHECK(kb_wasm_attach(0) == E_NOEXS, "attach with unknown import must fail");
	kb_wasm_get_status(&ws);
	CHECK(strstr(ws.last_error, "no_such_host_function") != NULL,
	      "error names the import: \"%s\"", ws.last_error);
	printf("  last_error=\"%s\"\n", ws.last_error);
}

/*======================================================================
 * 自作モジュールの事前チェック (--check)
 *
 * 取り付けと、ランダムなキー入力での kb_scan() を試し、trap しないこと、
 * 送られるレポートが HID として妥当なことを確かめる。
 *====================================================================*/

LOCAL INT check_module(const char *path, INT scans)
{
	T_KB_WASM_STATUS ws;
	T_HID_KBD_REPORT rep, last;
	UB rows[MATRIX_ROWS];
	UB held_r[6], held_c[6];
	INT nheld = 0, i, k, changes = 0, bad = 0;

	printf("[check] %s\n", path);
	load_module(path);
	if (kb_wasm_attach(0) != E_OK) {
		kb_wasm_get_status(&ws);
		printf("  NG: attach failed: %s\n", ws.last_error[0] ? ws.last_error : "(see above)");
		return 1;
	}
	memset(&last, 0, sizeof(last));

	for (i = 0; i < scans; i++) {
		if ((rng() % 8) == 0) {
			if (nheld > 0 && (nheld >= 6 || (rng() % 2))) {
				k = rng() % nheld;
				held_r[k] = held_r[nheld - 1];
				held_c[k] = held_c[nheld - 1];
				nheld--;
			} else {
				held_r[nheld] = rng() % MATRIX_ROWS;
				held_c[nheld] = rng() % MATRIX_COLS;
				nheld++;
			}
		}
		memset(rows, 0, sizeof(rows));
		for (k = 0; k < nheld; k++) rows[held_r[k]] |= (UB)(1u << held_c[k]);
		g_now_ms += 2;

		if (!kb_wasm_scan(rows, &rep)) {
			kb_wasm_get_status(&ws);
			printf("  NG: trap at scan %d: %s\n", i, ws.last_error);
			return 1;
		}
		/* 修飾キー (0xE0-0xE7) は keycode[] ではなく modifier のビットで送る */
		for (k = 0; k < 6; k++) {
			if (rep.keycode[k] >= 0xE0 && rep.keycode[k] <= 0xE7) bad++;
		}
		if (memcmp(&rep, &last, sizeof(rep)) != 0) { changes++; last = rep; }
	}

	kb_wasm_get_status(&ws);
	printf("  OK: %d scans, report changed %d times, kb_send() %d times\n",
	       scans, changes, g_nsent);
	if (bad > 0) {
		printf("  WARNING: modifier keycodes (0xE0-0xE7) found in keycode[] %d times\n", bad);
	}
	printf("  (kb_scan() の実行時間は実機で kb_wasm_status の max_scan_us を確認)\n");
	kb_wasm_detach();
	return 0;
}

int main(int argc, char **argv)
{
	if (kb_wasm_init() != E_OK) { printf("kb_wasm_init failed\n"); return 2; }

	if (argc == 3 && strcmp(argv[1], "--check") == 0) {
		return check_module(argv[2], 50000);
	}
	if (argc < 6) {
		printf("usage: %s parity.wasm default.wasm trap.wasm noscan.wasm badimport.wasm\n"
		       "       %s --check module.wasm\n", argv[0], argv[0]);
		return 2;
	}

	test_parity(argv[1], 200000);
	test_features(argv[2]);
	test_errors(argv[3], argv[4], argv[2]);
	test_persist(argv[2]);
	test_bad_import(argv[5]);

	printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
	return failures ? 1 : 0;
}
