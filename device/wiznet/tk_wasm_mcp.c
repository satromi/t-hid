/*
 *----------------------------------------------------------------------
 *    tk_wasm_mcp — wasm ランタイムの MCP ツール実装
 *
 *    wasm_begin / wasm_chunk / wasm_run / wasm_stop / wasm_abort / wasm_info
 *    既存の vm_* 一族と類似の API で、.wasm モジュールを注入・実行する。
 *
 *    data chunk は base64 文字列で受け取る (JSON 透過のため)。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include "tk_mcp_int.h"
#include "tk_wasm.h"

/*----------------------------------------------------------------------
 * base64 decode (パディング '=' 許容、無効文字で終了)
 *
 * 戻り値: decoded 長 (byte 数)。buf_cap を超える場合は buf_cap で打ち切り。
 */
LOCAL INT b64_decode(const char *src, UB *out, UW out_cap)
{
    static const signed char t[256] = {
        /* 0-31 invalid */
        [0 ... 255] = -1,
        ['A'] =  0, ['B'] =  1, ['C'] =  2, ['D'] =  3, ['E'] =  4,
        ['F'] =  5, ['G'] =  6, ['H'] =  7, ['I'] =  8, ['J'] =  9,
        ['K'] = 10, ['L'] = 11, ['M'] = 12, ['N'] = 13, ['O'] = 14,
        ['P'] = 15, ['Q'] = 16, ['R'] = 17, ['S'] = 18, ['T'] = 19,
        ['U'] = 20, ['V'] = 21, ['W'] = 22, ['X'] = 23, ['Y'] = 24,
        ['Z'] = 25,
        ['a'] = 26, ['b'] = 27, ['c'] = 28, ['d'] = 29, ['e'] = 30,
        ['f'] = 31, ['g'] = 32, ['h'] = 33, ['i'] = 34, ['j'] = 35,
        ['k'] = 36, ['l'] = 37, ['m'] = 38, ['n'] = 39, ['o'] = 40,
        ['p'] = 41, ['q'] = 42, ['r'] = 43, ['s'] = 44, ['t'] = 45,
        ['u'] = 46, ['v'] = 47, ['w'] = 48, ['x'] = 49, ['y'] = 50,
        ['z'] = 51,
        ['0'] = 52, ['1'] = 53, ['2'] = 54, ['3'] = 55, ['4'] = 56,
        ['5'] = 57, ['6'] = 58, ['7'] = 59, ['8'] = 60, ['9'] = 61,
        ['+'] = 62, ['/'] = 63,
        /* URL-safe variant */
        ['-'] = 62, ['_'] = 63,
    };
    UW written = 0;
    UW acc = 0;
    INT bits = 0;
    while (*src) {
        UB c = (UB)*src++;
        if (c == '=' || c <= ' ') continue;
        signed char v = t[c];
        if (v < 0) break;
        acc = (acc << 6) | (UW)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (written < out_cap) out[written++] = (UB)((acc >> bits) & 0xFF);
            else return (INT)written;
        }
    }
    return (INT)written;
}

/* hex→32byte SHA-256 (hex 64 chars 必須) */
LOCAL BOOL parse_sha256(const char *hex, UB out[32])
{
    if (hex == NULL) return FALSE;
    INT i;
    for (i = 0; i < 32; i++) {
        char h1 = hex[i*2], h2 = hex[i*2 + 1];
        if (!h1 || !h2) return FALSE;
        INT v1 = (h1 >= '0' && h1 <= '9') ? h1 - '0' :
                 (h1 >= 'a' && h1 <= 'f') ? h1 - 'a' + 10 :
                 (h1 >= 'A' && h1 <= 'F') ? h1 - 'A' + 10 : -1;
        INT v2 = (h2 >= '0' && h2 <= '9') ? h2 - '0' :
                 (h2 >= 'a' && h2 <= 'f') ? h2 - 'a' + 10 :
                 (h2 >= 'A' && h2 <= 'F') ? h2 - 'A' + 10 : -1;
        if (v1 < 0 || v2 < 0) return FALSE;
        out[i] = (UB)((v1 << 4) | v2);
    }
    return TRUE;
}

LOCAL INT json_get_int(cJSON *o, const char *key, INT dflt)
{
    cJSON *v = cJSON_GetObjectItem(o, key);
    return (v && cJSON_IsNumber(v)) ? v->valueint : dflt;
}

LOCAL const char *state_str(UB state)
{
    switch (state) {
        case WASM_STATE_EMPTY:    return "empty";
        case WASM_STATE_LOADING:  return "loading";
        case WASM_STATE_LOADED:   return "loaded";
        case WASM_STATE_RUNNING:  return "running";
        case WASM_STATE_STOPPED:  return "stopped";
        case WASM_STATE_ERROR:    return "error";
    }
    return "?";
}

/*----------------------------------------------------------------------
 * wasm_begin — スロット確保 + metadata 初期化
 *
 * args: slot (int), size (int), sha256 (64 hex chars), memory_kb (int, optional)
 */
cJSON *tool_wasm_begin(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    INT slot = json_get_int(args, "slot", 0);
    INT size = json_get_int(args, "size", 0);
    INT memkb = json_get_int(args, "memory_kb", WASM_DEFAULT_MEMORY_KB);

    cJSON *sha_item = cJSON_GetObjectItem(args, "sha256");
    UB sha[32] = {0};
    if (cJSON_IsString(sha_item) && sha_item->valuestring) {
        if (!parse_sha256(sha_item->valuestring, sha)) {
            cJSON_AddStringToObject(result, "error", "sha256 must be 64 hex chars");
            return result;
        }
    }
    /* sha_item 無し = sha 全ゼロ = 検証スキップ */

    ER err = tk_wasm_begin(slot, (UW)size, sha, (UW)memkb);
    if (err != E_OK) {
        cJSON_AddStringToObject(result, "error", "wasm_begin failed");
        cJSON_AddNumberToObject(result, "err_code", err);
        return result;
    }
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "slot", slot);
    cJSON_AddNumberToObject(result, "size", size);
    return result;
}

/*----------------------------------------------------------------------
 * wasm_chunk — base64 data を offset に書込
 *
 * args: slot (int), offset (int), data (base64 string)
 */
cJSON *tool_wasm_chunk(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    INT slot   = json_get_int(args, "slot", 0);
    INT offset = json_get_int(args, "offset", 0);
    cJSON *dat = cJSON_GetObjectItem(args, "data");
    if (!cJSON_IsString(dat) || dat->valuestring == NULL) {
        cJSON_AddStringToObject(result, "error", "data (base64) required");
        return result;
    }

    /* モジュール最大 32KB を想定 — 1 chunk は十分小さい */
    UB *buf = (UB *)Kmalloc(WASM_MAX_MODULE_SIZE);
    if (buf == NULL) {
        cJSON_AddStringToObject(result, "error", "out of memory");
        return result;
    }
    INT decoded = b64_decode(dat->valuestring, buf, WASM_MAX_MODULE_SIZE);
    if (decoded <= 0) {
        Kfree(buf);
        cJSON_AddStringToObject(result, "error", "base64 decode failed");
        return result;
    }

    ER err = tk_wasm_chunk(slot, (UW)offset, buf, (UW)decoded);
    Kfree(buf);
    if (err != E_OK) {
        cJSON_AddStringToObject(result, "error", "wasm_chunk failed");
        cJSON_AddNumberToObject(result, "err_code", err);
        return result;
    }

    T_WASM_SLOT_INFO info;
    tk_wasm_get_info(slot, &info);
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "slot", slot);
    cJSON_AddNumberToObject(result, "offset", offset);
    cJSON_AddNumberToObject(result, "written", decoded);
    cJSON_AddNumberToObject(result, "received", info.received_bytes);
    cJSON_AddNumberToObject(result, "expected", info.expected_size);
    return result;
}

/*----------------------------------------------------------------------
 * wasm_run — 検証 + parse + load + task 起動
 *
 * args: slot (int), entry (string, default "run")
 */
cJSON *tool_wasm_run(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    INT slot = json_get_int(args, "slot", 0);
    cJSON *entry_item = cJSON_GetObjectItem(args, "entry");
    const char *entry = (cJSON_IsString(entry_item) && entry_item->valuestring) ?
                        entry_item->valuestring : "run";

    ER err = tk_wasm_run(slot, entry);
    T_WASM_SLOT_INFO info;
    tk_wasm_get_info(slot, &info);

    cJSON_AddNumberToObject(result, "slot", slot);
    cJSON_AddStringToObject(result, "state", state_str(info.state));
    if (err != E_OK) {
        cJSON_AddStringToObject(result, "error", "wasm_run failed");
        cJSON_AddNumberToObject(result, "err_code", err);
        if (info.last_error[0]) cJSON_AddStringToObject(result, "m3_error", info.last_error);
        return result;
    }
    cJSON_AddStringToObject(result, "status", "running");
    cJSON_AddStringToObject(result, "entry", entry);
    return result;
}

/*----------------------------------------------------------------------
 * wasm_stop / wasm_abort
 */
cJSON *tool_wasm_stop(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    INT slot = json_get_int(args, "slot", 0);
    ER err = tk_wasm_stop(slot);
    cJSON_AddNumberToObject(result, "slot", slot);
    cJSON_AddStringToObject(result, "status", err == E_OK ? "stopped" : "error");
    if (err != E_OK) cJSON_AddNumberToObject(result, "err_code", err);
    return result;
}

cJSON *tool_wasm_abort(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    INT slot = json_get_int(args, "slot", 0);
    ER err = tk_wasm_abort(slot);
    cJSON_AddNumberToObject(result, "slot", slot);
    cJSON_AddStringToObject(result, "status", err == E_OK ? "aborted" : "error");
    if (err != E_OK) cJSON_AddNumberToObject(result, "err_code", err);
    return result;
}

/* sha256 32 bytes → 64-char lowercase hex (NUL terminated, 計 65 bytes) */
LOCAL void sha256_to_hex_local(const UB in[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    INT i;
    for (i = 0; i < 32; i++) {
        out[i*2]   = hex[in[i] >> 4];
        out[i*2+1] = hex[in[i] & 0xF];
    }
    out[64] = '\0';
}

LOCAL cJSON *flash_info_to_json(const T_WASM_FLASH_INFO *info)
{
    cJSON *o = cJSON_CreateObject();
    if (o == NULL) return NULL;   /* heap 枯渇時は caller 側でスキップ */
    cJSON_AddNumberToObject(o, "flash_slot", info->flash_slot);
    cJSON_AddBoolToObject  (o, "valid", info->valid);
    if (!info->valid) return o;
    cJSON_AddNumberToObject(o, "size", info->size);
    cJSON_AddNumberToObject(o, "memory_kb", info->memory_kb);
    cJSON_AddNumberToObject(o, "priority", info->priority);
    cJSON_AddNumberToObject(o, "flags", info->flags);
    cJSON_AddBoolToObject  (o, "auto_boot",
                            (info->flags & WASM_STORE_FLAG_AUTO_BOOT) ? 1 : 0);
    cJSON_AddStringToObject(o, "name", info->name);
    char hex[65];
    sha256_to_hex_local(info->sha256, hex);
    cJSON_AddStringToObject(o, "sha256", hex);
    return o;
}

/* cJSON_AddItemToArray は item==NULL で segv するので wrap */
LOCAL void array_add_safe(cJSON *arr, cJSON *item)
{
    if (item != NULL) cJSON_AddItemToArray(arr, item);
}

/*----------------------------------------------------------------------
 * wasm_info — 全スロット状態 (RAM + Flash + autoboot)
 */
cJSON *tool_wasm_info(cJSON *args)
{
    (void)args;
    cJSON *result = cJSON_CreateObject();
    cJSON *slots = cJSON_AddArrayToObject(result, "slots");
    INT i;
    for (i = 0; i < WASM_MAX_SLOT; i++) {
        T_WASM_SLOT_INFO info;
        tk_wasm_get_info(i, &info);
        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "slot", i);
        cJSON_AddStringToObject(s, "state", state_str(info.state));
        cJSON_AddNumberToObject(s, "size", info.expected_size);
        cJSON_AddNumberToObject(s, "received", info.received_bytes);
        cJSON_AddNumberToObject(s, "memory_kb", info.memory_kb);
        cJSON_AddNumberToObject(s, "tskid", info.tskid);
        if (info.last_error[0]) cJSON_AddStringToObject(s, "error", info.last_error);
        cJSON_AddItemToArray(slots, s);
    }

    cJSON *flash = cJSON_AddArrayToObject(result, "flash");
    for (i = 0; i < WASM_FLASH_SLOTS; i++) {
        T_WASM_FLASH_INFO fi;
        (void)tk_wasm_flash_get_info(i, &fi);
        array_add_safe(flash, flash_info_to_json(&fi));
    }
    cJSON_AddNumberToObject(result, "autoboot_slot", tk_wasm_autoboot_get());
    return result;
}

/*----------------------------------------------------------------------
 * wasm_store — RAM slot → Flash slot
 *
 * args: slot (int), flash_slot (int), name (string, optional), auto_boot (bool, optional)
 */
cJSON *tool_wasm_store(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    INT ram_slot   = json_get_int(args, "slot", 0);
    INT flash_slot = json_get_int(args, "flash_slot", 0);
    cJSON *name_item = cJSON_GetObjectItem(args, "name");
    const char *name = (cJSON_IsString(name_item) && name_item->valuestring)
                       ? name_item->valuestring : NULL;
    cJSON *ab_item = cJSON_GetObjectItem(args, "auto_boot");
    BOOL auto_boot = (ab_item && cJSON_IsBool(ab_item) && cJSON_IsTrue(ab_item));
    UW flags = auto_boot ? WASM_STORE_FLAG_AUTO_BOOT : 0;

    ER err = tk_wasm_store(ram_slot, flash_slot, name, flags);
    if (err != E_OK) {
        cJSON_AddStringToObject(result, "error", "wasm_store failed");
        cJSON_AddNumberToObject(result, "err_code", err);
        return result;
    }

    T_WASM_FLASH_INFO fi;
    (void)tk_wasm_flash_get_info(flash_slot, &fi);
    cJSON_AddStringToObject(result, "status", "ok");
    cJSON_AddNumberToObject(result, "flash_slot", flash_slot);
    cJSON_AddItemToObject  (result, "info", flash_info_to_json(&fi));
    cJSON_AddNumberToObject(result, "autoboot_slot", tk_wasm_autoboot_get());
    return result;
}

/*----------------------------------------------------------------------
 * wasm_load — Flash slot → RAM slot [, optionally run]
 *
 * args: flash_slot (int), slot (int), run (bool, optional), entry (string, optional)
 */
cJSON *tool_wasm_load(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    INT flash_slot = json_get_int(args, "flash_slot", 0);
    INT ram_slot   = json_get_int(args, "slot", 0);
    cJSON *run_item = cJSON_GetObjectItem(args, "run");
    BOOL run_now = (run_item && cJSON_IsBool(run_item) && cJSON_IsTrue(run_item));
    cJSON *entry_item = cJSON_GetObjectItem(args, "entry");
    const char *entry = (cJSON_IsString(entry_item) && entry_item->valuestring)
                        ? entry_item->valuestring : "run";

    ER err = tk_wasm_load(flash_slot, ram_slot);
    if (err != E_OK) {
        cJSON_AddStringToObject(result, "error", "wasm_load failed");
        cJSON_AddNumberToObject(result, "err_code", err);
        return result;
    }
    cJSON_AddStringToObject(result, "status", "loaded");
    cJSON_AddNumberToObject(result, "slot", ram_slot);
    cJSON_AddNumberToObject(result, "flash_slot", flash_slot);

    if (run_now) {
        ER rerr = tk_wasm_run(ram_slot, entry);
        T_WASM_SLOT_INFO info;
        tk_wasm_get_info(ram_slot, &info);
        cJSON_AddStringToObject(result, "state", state_str(info.state));
        if (rerr != E_OK) {
            cJSON_AddStringToObject(result, "run_error", "wasm_run failed");
            cJSON_AddNumberToObject(result, "run_err_code", rerr);
            if (info.last_error[0]) {
                cJSON_AddStringToObject(result, "m3_error", info.last_error);
            }
        } else {
            cJSON_AddStringToObject(result, "entry", entry);
        }
    }
    return result;
}

/*----------------------------------------------------------------------
 * wasm_erase — Flash slot を 0xFF で埋める
 */
cJSON *tool_wasm_erase(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    INT flash_slot = json_get_int(args, "flash_slot", 0);
    ER err = tk_wasm_erase(flash_slot);
    cJSON_AddNumberToObject(result, "flash_slot", flash_slot);
    cJSON_AddStringToObject(result, "status", err == E_OK ? "erased" : "error");
    if (err != E_OK) cJSON_AddNumberToObject(result, "err_code", err);
    cJSON_AddNumberToObject(result, "autoboot_slot", tk_wasm_autoboot_get());
    return result;
}

/*----------------------------------------------------------------------
 * wasm_flash_list — Flash slot 一覧 + autoboot
 */
cJSON *tool_wasm_flash_list(cJSON *args)
{
    (void)args;
    cJSON *result = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(result, "slots");
    INT i;
    for (i = 0; i < WASM_FLASH_SLOTS; i++) {
        T_WASM_FLASH_INFO fi;
        (void)tk_wasm_flash_get_info(i, &fi);
        array_add_safe(arr, flash_info_to_json(&fi));
    }
    cJSON_AddNumberToObject(result, "autoboot_slot", tk_wasm_autoboot_get());
    return result;
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
