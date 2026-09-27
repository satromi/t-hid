/*
 * kb_wasm.c — wasm モジュールによるキー処理の差し替え
 *
 * モジュールは MCP の wasm_begin / wasm_chunk で tk_wasm の slot に
 * 受信しておき、kb_wasm_attach() で引き取る。専用タスクは作らず、
 * キースキャンタスクから kb_scan() を直接呼ぶ。
 *
 * attach/detach (MCP タスク) と kb_wasm_scan (スキャンタスク) は
 * FastLock で排他する。
 */

#include <sys/machine.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "kb_keymap.h"
#include "kb_wasm.h"
#include "kb_brain.h"

#if defined(USE_WASM)

#include "wasm3.h"
#include "m3_env.h"
#include "../device/wiznet/tk_wasm.h"
#include "../device/wiznet/tk_sha256.h"
#include "../device/flash/stm32h5_flash.h"

/*----------------------------------------------------------------------
 * Flash 保存形式 (H5_FLASH_KBWASM_OFFSET, 32KB)
 *   [0..63]  ヘッダ  [64..] モジュール本体
 */
#define KBW_MAGIC		0x3157424BUL	/* "KBW1" */
#define KBW_HDR_SIZE		64
#define KBW_MAX_BODY		(H5_FLASH_KBWASM_SIZE - KBW_HDR_SIZE)

typedef struct {
	UW	magic;
	UW	size;
	UB	sha256[32];
	UB	reserved[KBW_HDR_SIZE - 40];
} T_KBW_HEADER;

#define KB_WASM_STACK_SIZE	4096	/* wasm3 の値スタック (バイト) */

/*----------------------------------------------------------------------
 * DWT サイクルカウンタ (kb_scan の実行時間計測)
 */
#define DEMCR			0xE000EDFC
#define DEMCR_TRCENA		(1u << 24)
#define DWT_CTRL		0xE0001000
#define DWT_CYCCNT		0xE0001004
#define DWT_CTRL_CYCCNTENA	(1u << 0)
#define CPU_MHZ			250

/*----------------------------------------------------------------------
 * 内部状態
 */
LOCAL struct {
	FastLock	lock;
	IM3Environment	env;
	IM3Runtime	runtime;
	IM3Function	fn_scan;
	IM3Function	fn_init;	/* 未実行の kb_init (無ければ NULL) */
	UB		*bytecode;	/* wasm3 はパース後も参照するので保持 */
	UW		size;
	BOOL		active;

	/* kb_scan 1 回分の入出力 */
	const UB	*rows;
	T_HID_KBD_REPORT pending;

	UW		scan_count;
	UW		max_scan_us;
	UB		layers;
	char		last_error[48];
} kw;

LOCAL void set_error(const char *msg)
{
	UW n = 0;
	while (msg != NULL && msg[n] && n < sizeof(kw.last_error) - 1) {
		kw.last_error[n] = msg[n];
		n++;
	}
	kw.last_error[n] = '\0';
}

/*======================================================================
 * ホスト関数
 *====================================================================*/

m3ApiRawFunction(host_kb_row)
{
	m3ApiReturnType(int32_t);
	m3ApiGetArg(int32_t, row);
	int32_t v = 0;
	if (kw.rows != NULL && row >= 0 && row < MATRIX_ROWS) v = kw.rows[row];
	m3ApiReturn(v);
}

m3ApiRawFunction(host_kb_keymap)
{
	m3ApiReturnType(int32_t);
	m3ApiGetArg(int32_t, layer);
	m3ApiGetArg(int32_t, row);
	m3ApiGetArg(int32_t, col);
	int32_t v = KC_NO;
	if (layer >= 0 && layer < NUM_LAYERS && row >= 0 && row < MATRIX_ROWS &&
	    col >= 0 && col < MATRIX_COLS) {
		v = keymaps[layer][row][col];
	}
	m3ApiReturn(v);
}

m3ApiRawFunction(host_kb_report)
{
	m3ApiGetArg(int32_t, mod);
	m3ApiGetArg(int32_t, k0);
	m3ApiGetArg(int32_t, k1);
	m3ApiGetArg(int32_t, k2);
	m3ApiGetArg(int32_t, k3);
	m3ApiGetArg(int32_t, k4);
	m3ApiGetArg(int32_t, k5);
	kw.pending.modifier   = (UB)mod;
	kw.pending.reserved   = 0;
	kw.pending.keycode[0] = (UB)k0;
	kw.pending.keycode[1] = (UB)k1;
	kw.pending.keycode[2] = (UB)k2;
	kw.pending.keycode[3] = (UB)k3;
	kw.pending.keycode[4] = (UB)k4;
	kw.pending.keycode[5] = (UB)k5;
	m3ApiSuccess();
}

m3ApiRawFunction(host_kb_send)
{
	m3ApiReturnType(int32_t);
	m3ApiReturn((int32_t)kb_brain_send_report(&kw.pending));
}

m3ApiRawFunction(host_kb_set_layers)
{
	m3ApiGetArg(int32_t, mask);
	kw.layers = (UB)mask;
	m3ApiSuccess();
}

m3ApiRawFunction(host_kb_now_ms)
{
	m3ApiReturnType(int32_t);
	SYSTIM tim;
	tk_get_otm(&tim);
	m3ApiReturn((int32_t)tim.lo);
}

m3ApiRawFunction(host_kb_log_printf)
{
	m3ApiReturnType(int32_t);
	m3ApiGetArgMem(const char *, msg);
	m3ApiGetArg(uint32_t, len);
	m3ApiCheckMem(msg, len);
	char buf[97];
	UW n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
	memcpy(buf, msg, n);
	buf[n] = '\0';
	tm_printf((UB *)"[kb.wasm] %s\n", buf);
	m3ApiReturn((int32_t)n);
}

LOCAL M3Result link_one(IM3Module module, const char *name, const char *sig,
			M3RawCall fn)
{
	M3Result r = m3_LinkRawFunction(module, "env", name, sig, fn);
	/* モジュールが import していない関数は無視する */
	if (r == m3Err_functionLookupFailed) r = m3Err_none;
	return r;
}

LOCAL M3Result link_host(IM3Module module)
{
	M3Result r;

	if ((r = link_one(module, "kb_row",        "i(i)",       host_kb_row)))        return r;
	if ((r = link_one(module, "kb_keymap",     "i(iii)",     host_kb_keymap)))     return r;
	if ((r = link_one(module, "kb_report",     "v(iiiiiii)", host_kb_report)))     return r;
	if ((r = link_one(module, "kb_send",       "i()",        host_kb_send)))       return r;
	if ((r = link_one(module, "kb_set_layers", "v(i)",       host_kb_set_layers))) return r;
	if ((r = link_one(module, "kb_now_ms",     "i()",        host_kb_now_ms)))     return r;
	if ((r = link_one(module, "log_printf",    "i(*i)",      host_kb_log_printf)))    return r;
	return m3Err_none;
}

/*
 * ホスト関数に結び付かなかった import を探す (例: clang が暗黙に出す memset)。
 * 残したままだと、その関数を呼んだ時点で初めて trap するので取り付け時に弾く。
 */
LOCAL const char *find_unresolved_import(IM3Module module)
{
	UW i;

	for (i = 0; i < module->numFunctions; i++) {
		IM3Function f = &module->functions[i];
		if (f->import.moduleUtf8 != NULL && f->compiled == NULL) {
			return f->import.fieldUtf8;
		}
	}
	return NULL;
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT ER kb_wasm_init(void)
{
	ER err;

	memset(&kw, 0, sizeof(kw));
	err = CreateLock(&kw.lock, (UB *)"kbwa");
	if (err < E_OK) return err;

	kw.env = m3_NewEnvironment();
	if (kw.env == NULL) return E_NOMEM;

	out_w(DEMCR, in_w(DEMCR) | DEMCR_TRCENA);
	out_w(DWT_CTRL, in_w(DWT_CTRL) | DWT_CTRL_CYCCNTENA);
	return E_OK;
}

/* ロック保持中に呼ぶ */
LOCAL void release_module(void)
{
	kw.active  = FALSE;
	kw.fn_scan = NULL;
	kw.fn_init = NULL;
	if (kw.runtime != NULL) {
		m3_FreeRuntime(kw.runtime);	/* ロード済みモジュールも解放される */
		kw.runtime = NULL;
	}
	if (kw.bytecode != NULL) {
		Kfree(kw.bytecode);
		kw.bytecode = NULL;
	}
	kw.size = 0;
}

/*
 * buf (Kmalloc 領域, 所有権を受け取る) のモジュールをキー処理にする
 */
LOCAL ER attach_buffer(UB *buf, UW size)
{
	IM3Module module = NULL;
	M3Result r;
	ER err;

	Lock(&kw.lock);
	release_module();
	kw.last_error[0] = '\0';
	kw.bytecode = buf;
	kw.size = size;

	kw.runtime = m3_NewRuntime(kw.env, KB_WASM_STACK_SIZE, NULL);
	if (kw.runtime == NULL) {
		set_error("NewRuntime failed");
		err = E_NOMEM;
		goto fail;
	}

	r = m3_ParseModule(kw.env, &module, kw.bytecode, kw.size);
	if (r != NULL) { set_error(r); err = E_IO; goto fail; }

	r = m3_LoadModule(kw.runtime, module);
	if (r != NULL) {
		m3_FreeModule(module);
		set_error(r);
		err = E_IO;
		goto fail;
	}

	r = link_host(module);
	if (r != NULL) { set_error(r); err = E_IO; goto fail; }

	{
		const char *missing = find_unresolved_import(module);
		if (missing != NULL) {
			char msg[sizeof(kw.last_error)];
			UW n = 0, k = 0;
			const char *pre = "unknown import: ";
			while (pre[k] && n < sizeof(msg) - 1) msg[n++] = pre[k++];
			for (k = 0; missing[k] && n < sizeof(msg) - 1; k++) msg[n++] = missing[k];
			msg[n] = '\0';
			set_error(msg);
			err = E_NOEXS;
			goto fail;
		}
	}

	r = m3_FindFunction(&kw.fn_scan, kw.runtime, "kb_scan");
	if (r != NULL) { set_error("kb_scan not exported"); err = E_NOEXS; goto fail; }

	/* 全キー解放の状態から始める */
	memset(&kw.pending, 0, sizeof(kw.pending));
	kw.scan_count  = 0;
	kw.max_scan_us = 0;
	kw.layers      = 1;

	/* kb_init はスタックに余裕のあるスキャンタスクで最初に呼ぶ */
	if (m3_FindFunction(&kw.fn_init, kw.runtime, "kb_init") != NULL) {
		kw.fn_init = NULL;
	}

	kw.active = TRUE;
	Unlock(&kw.lock);
	tm_printf((UB *)"kb.wasm: attached (%d bytes)\n", size);
	return E_OK;

fail:
	tm_printf((UB *)"kb.wasm: attach failed: %s\n", kw.last_error);
	release_module();
	Unlock(&kw.lock);
	return err;
}

EXPORT ER kb_wasm_attach(INT slot)
{
	UB *buf;
	UW size;
	ER err;

	if (kw.env == NULL) return E_OBJ;

	err = tk_wasm_take_bytecode(slot, &buf, &size);
	if (err < E_OK) return err;
	return attach_buffer(buf, size);
}

/*----------------------------------------------------------------------
 * Flash 保存 / 起動時読み込み
 */
EXPORT ER kb_wasm_save(void)
{
	T_KBW_HEADER hdr;
	SHA256_CTX ctx;
	ER err;

	Lock(&kw.lock);
	if (!kw.active || kw.bytecode == NULL) { Unlock(&kw.lock); return E_OBJ; }
	if (kw.size > KBW_MAX_BODY) { Unlock(&kw.lock); return E_LIMIT; }

	memset(&hdr, 0xFF, sizeof(hdr));
	hdr.magic = KBW_MAGIC;
	hdr.size  = kw.size;
	sha256_init(&ctx);
	sha256_update(&ctx, kw.bytecode, kw.size);
	sha256_final(&ctx, hdr.sha256);

	err = h5_flash_erase(H5_FLASH_KBWASM_OFFSET, KBW_HDR_SIZE + kw.size);
	if (err == E_OK) {
		err = h5_flash_program(H5_FLASH_KBWASM_OFFSET + KBW_HDR_SIZE, kw.bytecode, kw.size);
	}
	/* ヘッダは本体の後に書き、途中で電源が落ちても不完全な本体を読まない */
	if (err == E_OK) {
		err = h5_flash_program(H5_FLASH_KBWASM_OFFSET, &hdr, sizeof(hdr));
	}
	Unlock(&kw.lock);

	tm_printf((UB *)"kb.wasm: save %s (%d bytes)\n", (err == E_OK) ? "ok" : "failed", hdr.size);
	return err;
}

EXPORT ER kb_wasm_erase_saved(void)
{
	return h5_flash_erase(H5_FLASH_KBWASM_OFFSET, KBW_HDR_SIZE);
}

EXPORT UW kb_wasm_saved_size(void)
{
	const T_KBW_HEADER *hdr = (const T_KBW_HEADER *)H5_FLASH_PTR(H5_FLASH_KBWASM_OFFSET);

	if (hdr->magic != KBW_MAGIC || hdr->size == 0 || hdr->size > KBW_MAX_BODY) return 0;
	return hdr->size;
}

EXPORT ER kb_wasm_boot(void)
{
	const T_KBW_HEADER *hdr = (const T_KBW_HEADER *)H5_FLASH_PTR(H5_FLASH_KBWASM_OFFSET);
	const UB *body = H5_FLASH_PTR(H5_FLASH_KBWASM_OFFSET + KBW_HDR_SIZE);
	UW size = kb_wasm_saved_size();
	UB calc[32];
	SHA256_CTX ctx;
	UB *buf;

	if (kw.env == NULL) return E_OBJ;
	if (size == 0) return E_NOEXS;

	sha256_init(&ctx);
	sha256_update(&ctx, body, size);
	sha256_final(&ctx, calc);
	if (memcmp(calc, hdr->sha256, 32) != 0) {
		tm_printf((UB *)"kb.wasm: saved module is corrupted\n");
		return E_IO;
	}

	buf = (UB *)Kmalloc(size);
	if (buf == NULL) return E_NOMEM;
	memcpy(buf, body, size);

	tm_printf((UB *)"kb.wasm: loading saved module\n");
	return attach_buffer(buf, size);
}

EXPORT ER kb_wasm_detach(void)
{
	Lock(&kw.lock);
	release_module();
	Unlock(&kw.lock);
	tm_printf((UB *)"kb.wasm: detached (builtin keymap)\n");
	return E_OK;
}

EXPORT BOOL kb_wasm_active(void)
{
	return kw.active;
}

EXPORT BOOL kb_wasm_scan(const UB rows[20], T_HID_KBD_REPORT *report)
{
	BOOL handled = FALSE;
	M3Result r;
	UW t0, us;

	if (!kw.active) return FALSE;

	Lock(&kw.lock);
	if (kw.active && kw.fn_scan != NULL) {
		kw.rows = rows;
		r = NULL;
		if (kw.fn_init != NULL) {
			r = m3_CallV(kw.fn_init);
			kw.fn_init = NULL;
		}
		t0 = in_w(DWT_CYCCNT);
		if (r == NULL) r = m3_CallV(kw.fn_scan);
		us = (in_w(DWT_CYCCNT) - t0) / CPU_MHZ;
		kw.rows = NULL;

		if (r != NULL) {
			/*
			 * trap 後の wasm3 内部状態は解放時に二次 fault を起こし得るため
			 * runtime はここでは解放せず、次の attach/detach に任せる。
			 */
			set_error(r);
			kw.active = FALSE;
			tm_printf((UB *)"kb.wasm: trap: %s (builtin keymap)\n", r);
		} else {
			kw.scan_count++;
			if (us > kw.max_scan_us) kw.max_scan_us = us;
			*report = kw.pending;
			handled = TRUE;
		}
	}
	Unlock(&kw.lock);
	return handled;
}

EXPORT void kb_wasm_get_status(T_KB_WASM_STATUS *st)
{
	if (st == NULL) return;
	st->active      = kw.active;
	st->module_size = kw.size;
	st->scan_count  = kw.scan_count;
	st->max_scan_us = kw.max_scan_us;
	st->layers      = kw.layers;
	memcpy(st->last_error, kw.last_error, sizeof(st->last_error));
}

#else /* !USE_WASM — wasm ランタイムなし: 常に組み込みキー処理 */

EXPORT ER   kb_wasm_init(void)            { return E_OK; }
EXPORT ER   kb_wasm_attach(INT slot)      { (void)slot; return E_NOSPT; }
EXPORT ER   kb_wasm_detach(void)          { return E_OK; }
EXPORT BOOL kb_wasm_active(void)          { return FALSE; }
EXPORT BOOL kb_wasm_scan(const UB rows[20], T_HID_KBD_REPORT *report)
{
	(void)rows; (void)report;
	return FALSE;
}
EXPORT void kb_wasm_get_status(T_KB_WASM_STATUS *st)
{
	if (st != NULL) memset(st, 0, sizeof(*st));
}
EXPORT ER   kb_wasm_save(void)            { return E_NOSPT; }
EXPORT ER   kb_wasm_erase_saved(void)     { return E_NOSPT; }
EXPORT UW   kb_wasm_saved_size(void)      { return 0; }
EXPORT ER   kb_wasm_boot(void)            { return E_NOEXS; }

#endif /* USE_WASM */
