/*
 *----------------------------------------------------------------------
 *    MCP Server over MQTT for μT-Kernel 3.0
 *
 *    Core: main task, security, protocol, dispatch
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include "tk_mcp_int.h"

/*----------------------------------------------------------------------
 * モジュール変数
 */
T_MCP_CONF mcp_conf;
LOCAL ID mcp_tskid = 0;
LOCAL ID mcp_super_tskid = 0;        /* MQTT supervisor task */
LOCAL volatile BOOL mcp_running;
LOCAL UB mcp_rxbuf[MCP_BUF_SZ];

/* MQTT トピック */
char mcp_topic_req[48];
char mcp_topic_res[48];

/* MQTT 設定 (mcp_task と supervisor の両方が再接続時に参照する) */
LOCAL char mcp_device_id[12];
LOCAL char mcp_client_id[20];

/*----------------------------------------------------------------------
 * セキュリティ: API キー + HMAC + レート制限
 */
UB  mcp_api_key[APIKEY_LEN];
BOOL mcp_api_key_active = FALSE;

/* レート制限 (FastLock で排他制御) */
LOCAL UW rate_tick_start = 0;
LOCAL UINT rate_count = 0;
FastLock rate_lock;

/* ツール権限: FALSE=安全(誰でも), TRUE=危険(API キー必須) */
LOCAL BOOL tool_is_dangerous(const char *name)
{
    /* 安全ツール (読み取り専用 + プロトコル) */
    if (strcmp(name, "get_tasks") == 0) return FALSE;
    if (strcmp(name, "get_uptime") == 0) return FALSE;
    if (strcmp(name, "get_network") == 0) return FALSE;
    if (strcmp(name, "get_datetime") == 0) return FALSE;
    if (strcmp(name, "get_keyboard_status") == 0) return FALSE;
    if (strcmp(name, "read_adc") == 0) return FALSE;
    if (strcmp(name, "wasm_info") == 0) return FALSE;
    if (strcmp(name, "wasm_flash_list") == 0) return FALSE;
    if (strcmp(name, "kb_wasm_status") == 0) return FALSE;
    if (strcmp(name, "u2f_status") == 0) return FALSE;
    if (strcmp(name, "wifi_get_credentials") == 0) return FALSE;   /* SSID のみ, PSK は含まない */
    /* それ以外は全て危険 */
    return TRUE;
}

/* 埋込キーヘッダ (ビルド時に .mcp_api_key から生成) */
#include "mcp_apikey_embedded.h"

/* API キーを Flash からロード。Flash 未設定時は .mcp_api_key (埋込) にフォールバック */
LOCAL void apikey_load(void)
{
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)
    const UB *flash = (const UB *)(FLASH_STORAGE_BASE + APIKEY_FLASH_OFFSET);
    if (flash[0] == 'A' && flash[1] == 'K' && flash[2] == '0' && flash[3] == '1') {
        memcpy(mcp_api_key, flash + 4, APIKEY_LEN);
        mcp_api_key_active = TRUE;
        return;
    }
#endif
#ifdef MCP_APIKEY_EMBEDDED
    /* .mcp_api_key ファイルから生成された埋込デフォルトを使用 */
    memcpy(mcp_api_key, mcp_apikey_embedded, APIKEY_LEN);
    mcp_api_key_active = TRUE;
#else
    mcp_api_key_active = FALSE;
#endif
}

/* レート制限チェック */
LOCAL BOOL rate_check(void)
{
    if (mcp_conf.rate_limit == 0) return TRUE;
    Lock(&rate_lock);
    SYSTIM otm;
    tk_get_otm(&otm);
    UW now = otm.lo;
    if (now - rate_tick_start >= 1000) {
        rate_tick_start = now;
        rate_count = 1;
        Unlock(&rate_lock);
        return TRUE;
    }
    rate_count++;
    BOOL ok = (rate_count <= mcp_conf.rate_limit);
    Unlock(&rate_lock);
    return ok;
}

/* API キー検証 (平文一致 or HMAC 署名) */
LOCAL BOOL verify_auth(cJSON *req, const char *method)
{
    if (!mcp_api_key_active) return TRUE;  /* キー未設定 = 全許可 */

    /* tools/call の場合はツール名で判定 */
    if (strcmp(method, "tools/call") == 0) {
        cJSON *params = cJSON_GetObjectItem(req, "params");
        if (params) {
            cJSON *name = cJSON_GetObjectItem(params, "name");
            if (name && cJSON_IsString(name) && !tool_is_dangerous(name->valuestring)) {
                return TRUE;  /* 安全ツール */
            }
        }
    }
    /* initialize, ping, tools/list は常に許可 */
    if (strcmp(method, "initialize") == 0) return TRUE;
    if (strcmp(method, "ping") == 0) return TRUE;
    if (strcmp(method, "tools/list") == 0) return TRUE;
    if (strcmp(method, "notifications/initialized") == 0) return TRUE;

    /* api_key フィールドで検証 */
    cJSON *key_item = cJSON_GetObjectItem(req, "api_key");
    if (key_item != NULL && cJSON_IsString(key_item)) {
        /* hex 文字列を比較 */
        const char *provided = key_item->valuestring;
        char expected[APIKEY_LEN * 2 + 1];
        sha256_to_hex(mcp_api_key, expected);
        /* 先頭 32 文字 (16 bytes 分) で比較 (フルキーは 64 hex chars) */
        if (strlen(provided) >= 64 && memcmp(provided, expected, 64) == 0) {
            return TRUE;
        }
    }

    /* signature フィールドで HMAC 検証 */
    cJSON *sig_item = cJSON_GetObjectItem(req, "signature");
    cJSON *ts_item  = cJSON_GetObjectItem(req, "timestamp");
    if (sig_item != NULL && cJSON_IsString(sig_item) &&
        ts_item != NULL && cJSON_IsNumber(ts_item)) {
        /* HMAC(api_key, "method|timestamp") */
        char msg[128];
        INT mlen = 0;
        const char *m = method;
        while (*m && mlen < 100) msg[mlen++] = *m++;
        msg[mlen++] = '|';
        UW ts = (UW)ts_item->valuedouble;
        /* timestamp を文字列に */
        char tsbuf[12];
        INT ti = 11; tsbuf[ti] = '\0';
        do { tsbuf[--ti] = '0' + (ts % 10); ts /= 10; } while (ts > 0);
        while (tsbuf[ti]) msg[mlen++] = tsbuf[ti++];
        msg[mlen] = '\0';

        UB digest[32];
        hmac_sha256(mcp_api_key, APIKEY_LEN, (const UB *)msg, mlen, digest);
        char expected_sig[65];
        sha256_to_hex(digest, expected_sig);

        if (strlen(sig_item->valuestring) >= 64 &&
            memcmp(sig_item->valuestring, expected_sig, 64) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/*----------------------------------------------------------------------
 * cJSON メモリフック: Kmalloc/Kfree にリダイレクト
 */
LOCAL void *cjson_malloc(size_t sz) { return Kmalloc(sz); }
LOCAL void  cjson_free(void *p)     { Kfree(p); }

/*----------------------------------------------------------------------
 * デバイス ID 生成 (MAC 下位 3 バイト)
 */
LOCAL void make_device_id(char *buf)
{
    UB mac[6];
    getSHAR(mac);
    /* "kb-XXYYZZ" 形式 */
    static const char hex[] = "0123456789abcdef";
    buf[0] = 'k'; buf[1] = 'b'; buf[2] = '-';
    buf[3] = hex[mac[3] >> 4]; buf[4] = hex[mac[3] & 0xF];
    buf[5] = hex[mac[4] >> 4]; buf[6] = hex[mac[4] & 0xF];
    buf[7] = hex[mac[5] >> 4]; buf[8] = hex[mac[5] & 0xF];
    buf[9] = '\0';
}

/*----------------------------------------------------------------------
 * JSON-RPC レスポンス送信
 */
LOCAL void mcp_send_response(cJSON *result, cJSON *id)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    if (id != NULL) {
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
    }
    cJSON_AddItemToObject(resp, "result", result);

    char *json_str = cJSON_PrintUnformatted(resp);
    if (json_str != NULL) {
        UH slen = (UH)strlen(json_str);
        ER pub_err = tk_mqtt_publish(mcp_topic_res, (const UB *)json_str, slen);
        if (pub_err != E_OK) {
            W5DBG("MCP: pub failed len=%d err=%d\n", slen, pub_err);
        }
        cJSON_free(json_str);
    }
    cJSON_Delete(resp);
}

LOCAL void mcp_send_error(INT code, const char *msg, cJSON *id)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    if (id != NULL) {
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
    }
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg);
    cJSON_AddItemToObject(resp, "error", err);

    char *json_str = cJSON_PrintUnformatted(resp);
    if (json_str != NULL) {
        tk_mqtt_publish(mcp_topic_res, (const UB *)json_str, strlen(json_str));
        cJSON_free(json_str);
    }
    cJSON_Delete(resp);
}

/*======================================================================
 * MCP メソッドハンドラ
 *====================================================================*/

/*----------------------------------------------------------------------
 * initialize — MCP 接続初期化
 */
LOCAL cJSON *handle_initialize(cJSON *params)
{
    (void)params;

    cJSON *result = cJSON_CreateObject();

    /* protocolVersion */
    cJSON_AddStringToObject(result, "protocolVersion", MCP_PROTOCOL_VERSION);

    /* serverInfo */
    cJSON *info = cJSON_CreateObject();
    cJSON_AddStringToObject(info, "name", MCP_SERVER_NAME);
    cJSON_AddStringToObject(info, "version", MCP_SERVER_VERSION);
    cJSON_AddItemToObject(result, "serverInfo", info);

    /* capabilities */
    cJSON *cap = cJSON_CreateObject();
    cJSON_AddItemToObject(cap, "tools", cJSON_CreateObject());
    cJSON_AddItemToObject(result, "capabilities", cap);

    return result;
}

/*----------------------------------------------------------------------
 * ping
 */
LOCAL cJSON *handle_ping(cJSON *params)
{
    (void)params;
    return cJSON_CreateObject();
}

/*----------------------------------------------------------------------
 * ツール定義テーブル
 */
LOCAL const T_MCP_TOOL tools[] = {
    { "send_keys", "Send text via USB HID keyboard",
      "{\"type\":\"object\",\"properties\":{\"text\":{\"type\":\"string\",\"description\":\"Text to type\"}},\"required\":[\"text\"]}",
      tool_send_keys },
    { "press_combo", "Press a key combination (e.g. Ctrl+S) via USB HID. Modifier + key held together.",
      "{\"type\":\"object\",\"properties\":{\"modifier\":{\"type\":[\"integer\",\"string\"],\"description\":\"Modifier bitmap (1=Ctrl,2=Shift,4=Alt,8=GUI) or '+'-separated names e.g. 'ctrl+shift'\"},\"key\":{\"type\":[\"integer\",\"string\"],\"description\":\"HID keycode (0x04=A) or single ASCII char\"},\"hold_ms\":{\"type\":\"integer\",\"description\":\"Hold duration ms (default 10, max 5000)\"}}}",
      tool_press_combo },
    { "set_layer", "Toggle/set keyboard TG() layer state (0..7). action=on/off/toggle/clear_all.",
      "{\"type\":\"object\",\"properties\":{\"layer\":{\"type\":\"integer\",\"description\":\"Layer number (0-7); required unless action=clear_all\"},\"action\":{\"type\":\"string\",\"enum\":[\"on\",\"off\",\"toggle\",\"clear_all\"],\"description\":\"Default: toggle\"}}}",
      tool_set_layer },
    { "get_keyboard_status", "Get current keyboard status",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_get_keyboard_status },
    { "get_tasks", "List all RTOS tasks with priority, state, and stack usage",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_get_tasks },
    { "get_uptime", "Get system uptime since boot",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_get_uptime },
    { "get_network", "Get network status (IP, MAC, link state)",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_get_network },
    { "get_datetime", "Get current date and time (SNTP synced)",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_get_datetime },
    { "set_led", "Control onboard LED (GP25). Set state for manual on/off, or heartbeat to resume blinking",
      "{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"boolean\",\"description\":\"LED on/off (manual)\"},\"heartbeat\":{\"type\":\"boolean\",\"description\":\"Resume heartbeat blinking\"}}}",
      tool_set_led },
    { "reboot", "Reboot the device (soft reset via AIRCR)",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_reboot },
    { "read_adc", "Read ADC channel. ch=4 is RP2040 internal temperature sensor",
      "{\"type\":\"object\",\"properties\":{\"ch\":{\"type\":\"integer\",\"description\":\"ADC channel 0-4 (4=temp sensor)\"}},\"required\":[\"ch\"]}",
      tool_read_adc },
    { "gpio_control", "Read or write a GPIO pin",
      "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO number 0-29\"},\"mode\":{\"type\":\"string\",\"enum\":[\"read\",\"output_high\",\"output_low\"],\"description\":\"Operation\"}},\"required\":[\"pin\",\"mode\"]}",
      tool_gpio_control },
    { "pwm_control", "Set PWM output on a GPIO pin",
      "{\"type\":\"object\",\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIO number\"},\"freq\":{\"type\":\"integer\",\"description\":\"Frequency in Hz\"},\"duty\":{\"type\":\"integer\",\"description\":\"Duty cycle 0-100 percent\"}},\"required\":[\"pin\",\"duty\"]}",
      tool_pwm_control },
    { "peek", "Read 32-bit value from a memory/register address",
      "{\"type\":\"object\",\"properties\":{\"addr\":{\"type\":\"integer\",\"description\":\"Address (hex as integer, e.g. 0x4001C000)\"}},\"required\":[\"addr\"]}",
      tool_peek },
    { "poke", "Write 32-bit value to a memory/register address. USE WITH CAUTION",
      "{\"type\":\"object\",\"properties\":{\"addr\":{\"type\":\"integer\",\"description\":\"Address\"},\"value\":{\"type\":\"integer\",\"description\":\"32-bit value to write\"}},\"required\":[\"addr\",\"value\"]}",
      tool_poke },
    { "tcp_connect", "Connect to a TCP host, send data, receive response, and close",
      "{\"type\":\"object\",\"properties\":{\"host\":{\"type\":\"string\",\"description\":\"IP address (dotted decimal)\"},\"port\":{\"type\":\"integer\",\"description\":\"TCP port\"},\"send\":{\"type\":\"string\",\"description\":\"Data to send (optional)\"},\"timeout\":{\"type\":\"integer\",\"description\":\"Timeout in ms (default 5000)\"}},\"required\":[\"host\",\"port\"]}",
      tool_tcp_connect },
    { "flash_write", "Read or write persistent data in Flash (4KB sector at end of 2MB). key/value store",
      "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"read\",\"write\",\"erase\"],\"description\":\"Operation\"},\"offset\":{\"type\":\"integer\",\"description\":\"Byte offset within 4KB sector (0-4095)\"},\"data\":{\"type\":\"string\",\"description\":\"Hex string to write (e.g. '48656C6C6F')\"},\"length\":{\"type\":\"integer\",\"description\":\"Bytes to read (for read action)\"}},\"required\":[\"action\"]}",
      tool_flash_write },
    { "watchdog", "Configure or query the RP2040 hardware watchdog timer",
      "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"status\",\"enable\",\"feed\",\"disable\"],\"description\":\"Operation\"},\"timeout_ms\":{\"type\":\"integer\",\"description\":\"Watchdog timeout in ms (for enable, max 8388)\"}},\"required\":[\"action\"]}",
      tool_watchdog },
    { "task_create", "Create and start a new RTOS task that periodically runs a simple action",
      "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"description\":\"Task name (for display)\"},\"action\":{\"type\":\"string\",\"enum\":[\"blink\",\"adc_log\",\"heartbeat\"],\"description\":\"Predefined action\"},\"pin\":{\"type\":\"integer\",\"description\":\"GPIO pin (for blink)\"},\"interval_ms\":{\"type\":\"integer\",\"description\":\"Action interval in ms\"}},\"required\":[\"action\"]}",
      tool_task_create },
    { "task_stop", "Stop or terminate a running task by ID. Also stops dynamic tasks created by task_create",
      "{\"type\":\"object\",\"properties\":{\"task_id\":{\"type\":\"integer\",\"description\":\"RTOS task ID\"},\"slot\":{\"type\":\"integer\",\"description\":\"Dynamic task slot 0-3 (alternative to task_id)\"}},\"required\":[]}",
      tool_task_stop },
    { "shell", "Execute a simple built-in command and return output. Commands: mem, reg <addr>, tasks, net, time, echo <text>",
      "{\"type\":\"object\",\"properties\":{\"cmd\":{\"type\":\"string\",\"description\":\"Command string\"}},\"required\":[\"cmd\"]}",
      tool_shell },
    { "vm_run", "[DEPRECATED - prefer wasm_run] Run a bytecode program on a 17-opcode stack VM. Use wasm_begin/chunk/run for new code — VM will be removed in a future release.",
      "{\"type\":\"object\",\"properties\":{\"program\":{\"type\":\"string\",\"description\":\"Hex bytecode string\"},\"name\":{\"type\":\"string\",\"description\":\"Task label\"}},\"required\":[\"program\"]}",
      tool_vm_run },
    { "vm_stop", "[DEPRECATED - prefer wasm_stop] Stop a running Bytecode VM instance. Use wasm_stop for wasm3 slots.",
      "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\",\"description\":\"VM slot 0-3, or -1 for all\"}},\"required\":[\"slot\"]}",
      tool_vm_stop },
    { "vm_store", "[DEPRECATED] Store/load Bytecode VM program to/from Flash for auto-run. wasm persistence will be added as wasm_store/wasm_load.",
      "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"store\",\"load\",\"clear\",\"status\"],\"description\":\"Operation\"},\"program\":{\"type\":\"string\",\"description\":\"Hex bytecode (for store)\"}},\"required\":[\"action\"]}",
      tool_vm_store },
    { "agent_start", "Start autonomous recovery agent that monitors tasks/network and auto-recovers",
      "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"stop\",\"status\"],\"description\":\"Operation\"},\"interval_ms\":{\"type\":\"integer\",\"description\":\"Check interval (default 5000)\"}},\"required\":[\"action\"]}",
      tool_agent_start },
    { "net_scan", "Scan TCP ports on a target IP",
      "{\"type\":\"object\",\"properties\":{\"host\":{\"type\":\"string\",\"description\":\"Target IP\"},\"ports\":{\"type\":\"string\",\"description\":\"Comma-separated ports or range (e.g. '80,443,8080' or '1-100')\"},\"timeout\":{\"type\":\"integer\",\"description\":\"Per-port timeout ms (default 1000)\"}},\"required\":[\"host\",\"ports\"]}",
      tool_net_scan },
    { "relay", "Send an MQTT message to another device's MCP topic (AI-to-AI relay)",
      "{\"type\":\"object\",\"properties\":{\"device_id\":{\"type\":\"string\",\"description\":\"Target device ID (e.g. kb-XXYYZZ)\"},\"method\":{\"type\":\"string\",\"description\":\"JSON-RPC method to call\"},\"params\":{\"type\":\"string\",\"description\":\"JSON params string\"}},\"required\":[\"device_id\",\"method\"]}",
      tool_relay },
    { "mcp_auth", "Manage API key: generate (create new key), revoke (remove key), status (check if active). Dangerous tools require api_key or HMAC signature when key is active.",
      "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"generate\",\"revoke\",\"status\"],\"description\":\"Operation\"}},\"required\":[\"action\"]}",
      tool_mcp_auth },
    { "wifi_set_credentials", "Save WiFi SSID/PSK to Flash. Takes effect after next reboot.",
      "{\"type\":\"object\",\"properties\":{\"ssid\":{\"type\":\"string\",\"description\":\"SSID (1..32 chars)\"},\"psk\":{\"type\":\"string\",\"description\":\"Passphrase (8..63 chars for WPA2/3, empty for open)\"},\"auth\":{\"type\":\"string\",\"enum\":[\"open\",\"wpa2\",\"wpa3\"],\"description\":\"Auth type (default: wpa2)\"}},\"required\":[\"ssid\"]}",
      tool_wifi_set_credentials },
    { "wifi_get_credentials", "Get stored WiFi SSID and auth type (PSK is never returned, only length).",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_wifi_get_credentials },
#if defined(USE_WASM)
    { "wasm_begin", "Begin WASM module upload. Allocates scratch buffer in slot.",
      "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\",\"description\":\"Slot 0..3\"},\"size\":{\"type\":\"integer\",\"description\":\"Total bytes of .wasm\"},\"sha256\":{\"type\":\"string\",\"description\":\"64-char hex SHA-256 (optional, '' = skip check)\"},\"memory_kb\":{\"type\":\"integer\",\"description\":\"Linear memory size in KB (default 16)\"}},\"required\":[\"slot\",\"size\"]}",
      tool_wasm_begin },
    { "wasm_chunk", "Append base64-encoded chunk to WASM slot at offset.",
      "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\"},\"offset\":{\"type\":\"integer\"},\"data\":{\"type\":\"string\",\"description\":\"base64 bytes\"}},\"required\":[\"slot\",\"offset\",\"data\"]}",
      tool_wasm_chunk },
    { "wasm_run", "Verify SHA-256, parse, load, spawn task to call exported function.",
      "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\"},\"entry\":{\"type\":\"string\",\"description\":\"Exported function name (default 'run')\"}},\"required\":[\"slot\"]}",
      tool_wasm_run },
    { "wasm_stop", "Force-terminate WASM task and free module.",
      "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\"}},\"required\":[\"slot\"]}",
      tool_wasm_stop },
    { "wasm_abort", "Cancel in-progress upload (before wasm_run).",
      "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\"}},\"required\":[\"slot\"]}",
      tool_wasm_abort },
    { "wasm_info", "Get state of all WASM slots (empty/loading/loaded/running/stopped/error).",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_wasm_info },
    { "wasm_store", "Persist loaded RAM slot to Flash slot (4 slots × 32KB). Optionally mark for autoboot at power-on.",
      "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\",\"description\":\"RAM slot 0..3\"},\"flash_slot\":{\"type\":\"integer\",\"description\":\"Flash slot 0..3\"},\"name\":{\"type\":\"string\",\"description\":\"Display name (0..31 chars)\"},\"auto_boot\":{\"type\":\"boolean\",\"description\":\"Auto-run at next boot\"}},\"required\":[\"slot\",\"flash_slot\"]}",
      tool_wasm_store },
    { "wasm_load", "Restore a Flash slot to a RAM slot. Optionally run immediately.",
      "{\"type\":\"object\",\"properties\":{\"flash_slot\":{\"type\":\"integer\"},\"slot\":{\"type\":\"integer\",\"description\":\"Target RAM slot 0..3\"},\"run\":{\"type\":\"boolean\",\"description\":\"Call tk_wasm_run after load\"},\"entry\":{\"type\":\"string\",\"description\":\"Export name (default 'run')\"}},\"required\":[\"flash_slot\",\"slot\"]}",
      tool_wasm_load },
    { "wasm_erase", "Erase a Flash slot (fills with 0xFF). Clears autoboot if set to this slot.",
      "{\"type\":\"object\",\"properties\":{\"flash_slot\":{\"type\":\"integer\"}},\"required\":[\"flash_slot\"]}",
      tool_wasm_erase },
    { "wasm_flash_list", "List persisted WASM modules and current autoboot target.",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_wasm_flash_list },
#endif /* USE_WASM */
#if defined(KB_BRAIN) && defined(USE_WASM)
    { "kb_wasm_attach", "Use a WASM module uploaded with wasm_begin/wasm_chunk as the keyboard key processor. The module must export kb_scan() (called every 2ms) and may export kb_init(). Host imports: kb_row, kb_keymap, kb_report, kb_send, kb_set_layers, kb_now_ms, log_printf.",
      "{\"type\":\"object\",\"properties\":{\"slot\":{\"type\":\"integer\",\"description\":\"RAM slot holding the uploaded module (state=loaded)\"},\"save\":{\"type\":\"boolean\",\"description\":\"Also store the module in Flash and attach it automatically at boot\"}},\"required\":[\"slot\"]}",
      tool_kb_wasm_attach },
    { "kb_wasm_detach", "Detach the WASM key processor and return to the built-in keymap.",
      "{\"type\":\"object\",\"properties\":{\"erase\":{\"type\":\"boolean\",\"description\":\"Also erase the module saved in Flash\"}}}",
      tool_kb_wasm_detach },
    { "kb_wasm_status", "Get keyboard brain status: WASM key processor state, scan count, max kb_scan time, split halves connection.",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_kb_wasm_status },
#endif /* KB_BRAIN && USE_WASM */
#if defined(USE_U2F)
    { "u2f_status", "Get FIDO U2F authenticator status: ready, P-256 engine (PKA or software), signature counter, registrations/authentications since boot.",
      "{\"type\":\"object\",\"properties\":{}}",
      tool_u2f_status },
#endif /* USE_U2F */
};

#define NUM_TOOLS  (sizeof(tools) / sizeof(tools[0]))

/*----------------------------------------------------------------------
 * tools/list
 */
LOCAL cJSON *handle_tools_list(cJSON *params)
{
    (void)params;

    cJSON *result = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();

    UINT i;
    for (i = 0; i < NUM_TOOLS; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", tools[i].name);
        cJSON_AddStringToObject(t, "description", tools[i].description);
        cJSON_AddRawToObject(t, "inputSchema", tools[i].schema);
        cJSON_AddItemToArray(arr, t);
    }
    cJSON_AddItemToObject(result, "tools", arr);

    return result;
}

/*----------------------------------------------------------------------
 * tools/call
 */
LOCAL cJSON *handle_tools_call(cJSON *params)
{
    cJSON *name_item = cJSON_GetObjectItem(params, "name");
    cJSON *args_item = cJSON_GetObjectItem(params, "arguments");
    if (name_item == NULL || !cJSON_IsString(name_item)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddBoolToObject(err, "isError", 1);
        cJSON *content = cJSON_CreateArray();
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "type", "text");
        cJSON_AddStringToObject(msg, "text", "Missing tool name");
        cJSON_AddItemToArray(content, msg);
        cJSON_AddItemToObject(err, "content", content);
        return err;
    }

    const char *tool_name = name_item->valuestring;
    W5DBG("MCP: tool=%s\n", tool_name);

    UINT i;
    for (i = 0; i < NUM_TOOLS; i++) {
        if (strcmp(tool_name, tools[i].name) == 0) {
            W5DBG("MCP: calling handler\n");
            cJSON *tool_result = tools[i].handler(args_item);
            W5DBG("MCP: handler done\n");

            /* MCP tools/call レスポンス形式 */
            char *text = cJSON_PrintUnformatted(tool_result);
            cJSON_Delete(tool_result);

            cJSON *result = cJSON_CreateObject();
            cJSON *content = cJSON_CreateArray();
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", "text");
            cJSON_AddStringToObject(item, "text", text ? text : "{}");
            if (text) cJSON_free(text);

            cJSON_AddItemToArray(content, item);
            cJSON_AddItemToObject(result, "content", content);
            return result;
        }
    }

    /* ツールが見つからない */
    cJSON *err = cJSON_CreateObject();
    cJSON_AddBoolToObject(err, "isError", 1);
    cJSON *content = cJSON_CreateArray();
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "text");
    cJSON_AddStringToObject(msg, "text", "Unknown tool");
    cJSON_AddItemToArray(content, msg);
    cJSON_AddItemToObject(err, "content", content);
    return err;
}

/*======================================================================
 * MCP タスク
 *====================================================================*/

/*----------------------------------------------------------------------
 * MQTT メッセージ受信コールバック
 */
LOCAL ID mcp_flgid = 0;
LOCAL UH mcp_rxlen = 0;

LOCAL void mcp_on_mqtt_recv(const T_MQTT_MSG *msg)
{
    /* request トピックのみ処理 */
    if (msg->payload_len > 0 && msg->payload_len < MCP_BUF_SZ) {
        memcpy(mcp_rxbuf, msg->payload, msg->payload_len);
        mcp_rxbuf[msg->payload_len] = '\0';
        mcp_rxlen = msg->payload_len;
        if (mcp_flgid > 0) {
            tk_set_flg(mcp_flgid, MCP_EVT_MSG);
        }
    }
}

/*----------------------------------------------------------------------
 * JSON-RPC ディスパッチャ
 */
LOCAL void mcp_dispatch(const char *json_str)
{
    cJSON *req = cJSON_Parse(json_str);
    if (req == NULL) {
        mcp_send_error(-32700, "Parse error", NULL);
        return;
    }

    cJSON *method = cJSON_GetObjectItem(req, "method");
    cJSON *params = cJSON_GetObjectItem(req, "params");
    cJSON *id     = cJSON_GetObjectItem(req, "id");

    if (method == NULL || !cJSON_IsString(method)) {
        mcp_send_error(-32600, "Invalid Request", id);
        cJSON_Delete(req);
        return;
    }

    const char *m = method->valuestring;
    cJSON *result = NULL;

    W5DBG("MCP: method=%s\n", m);

    /* レート制限 */
    if (!rate_check()) {
        mcp_send_error(-32003, "Rate limit exceeded", id);
        cJSON_Delete(req);
        return;
    }

    /* API キー認証 */
    if (strcmp(m, "tools/call") == 0) {
        cJSON *p = cJSON_GetObjectItem(params, "name");
        if (p && cJSON_IsString(p) && strcmp(p->valuestring, "mcp_auth") == 0) {
            /* mcp_auth(status) はフリー。generate/revoke はキー設定済みなら認証必須 */
            cJSON *a = cJSON_GetObjectItem(
                cJSON_GetObjectItem(params, "arguments"), "action");
            if (a && cJSON_IsString(a) && strcmp(a->valuestring, "status") != 0) {
                /* generate/revoke: キーが設定済みなら認証必須 */
                if (mcp_api_key_active && !verify_auth(req, m)) {
                    mcp_send_error(-32001, "API key required for key management", id);
                    cJSON_Delete(req);
                    return;
                }
            }
        } else if (!verify_auth(req, m)) {
            mcp_send_error(-32001, "API key required", id);
            cJSON_Delete(req);
            return;
        }
    } else if (!verify_auth(req, m)) {
        mcp_send_error(-32001, "API key required", id);
        cJSON_Delete(req);
        return;
    }

    if (strcmp(m, "initialize") == 0) {
        result = handle_initialize(params);
    } else if (strcmp(m, "ping") == 0) {
        result = handle_ping(params);
    } else if (strcmp(m, "tools/list") == 0) {
        result = handle_tools_list(params);
    } else if (strcmp(m, "tools/call") == 0) {
        result = handle_tools_call(params);
    } else if (strcmp(m, "notifications/initialized") == 0) {
        /* 通知 (id なし) — 応答不要 */
        cJSON_Delete(req);
        return;
    } else {
        mcp_send_error(-32601, "Method not found", id);
        cJSON_Delete(req);
        return;
    }

    if (result != NULL) {
        mcp_send_response(result, id);
        W5DBG("MCP: sent ok\n");
    }

    cJSON_Delete(req);
    W5DBG("MCP: dispatch done\n");
}

/*----------------------------------------------------------------------
 * MQTT 接続セットアップ (mcp_task 初期化と supervisor 再接続で共用)
 *   - 設定は file-static の mcp_device_id / mcp_client_id /
 *     mcp_topic_req / mcp_topic_res に組み立てる
 *   - tk_mqtt_start を最大 3 回まで retry
 *   - 成功したら request トピックを subscribe
 */
LOCAL ER mcp_mqtt_connect(void)
{
    /* device_id / client_id / topic は冪等に組立 (毎回同じ結果) */
    make_device_id(mcp_device_id);
    safe_topic_build(mcp_client_id, sizeof(mcp_client_id),
                     "mcp-", mcp_device_id, NULL);
    safe_topic_build(mcp_topic_req, sizeof(mcp_topic_req),
                     "mcp/", mcp_device_id, "/request");
    safe_topic_build(mcp_topic_res, sizeof(mcp_topic_res),
                     "mcp/", mcp_device_id, "/response");

    T_MQTT_CONF mqtt_conf;
    mqtt_conf.sn         = mcp_conf.sn;
    mqtt_conf.broker_ip  = mcp_conf.broker_ip;
    mqtt_conf.broker_port = mcp_conf.broker_port;
    mqtt_conf.keepalive  = 60;
    mqtt_conf.username   = mcp_conf.mqtt_user;
    mqtt_conf.password   = mcp_conf.mqtt_pass;
    mqtt_conf.client_id  = mcp_client_id;

    INT retry;
    for (retry = 0; retry < 3; retry++) {
        ER err = tk_mqtt_start(&mqtt_conf, mcp_on_mqtt_recv);
        if (err != E_OK) {
            W5DBG("MCP: mqtt_start failed (%d)\n", err);
            tk_dly_tsk(3000);
            continue;
        }

        /* MQTT 接続完了待ち (最大 15 秒) */
        INT w;
        for (w = 0; w < 15; w++) {
            if (tk_mqtt_is_connected()) break;
            tk_dly_tsk(1000);
        }
        if (tk_mqtt_is_connected()) break;

        W5DBG("MCP: MQTT connect timeout (retry %d)\n", retry + 1);
        tk_mqtt_stop();
        tk_dly_tsk(5000);
    }
    if (!tk_mqtt_is_connected()) {
        return E_TMOUT;
    }

    W5DBG("MCP: sub %s\n", mcp_topic_req);
    tk_mqtt_subscribe(mcp_topic_req, 0);
    return E_OK;
}

/*----------------------------------------------------------------------
 * MCP タスク本体
 */
LOCAL void mcp_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    /* cJSON メモリフック設定 */
    cJSON_Hooks hooks;
    hooks.malloc_fn = cjson_malloc;
    hooks.free_fn   = cjson_free;
    cJSON_InitHooks(&hooks);

    /* セキュリティ初期化 (rate_lock は tk_mcp_start で初期化済み) */
    apikey_load();
    W5DBG("MCP: api_key %s\n", mcp_api_key_active ? "active" : "none");

    /* 初期 MQTT 接続 (失敗しても supervisor が後で復旧を試みる) */
    if (mcp_mqtt_connect() != E_OK) {
        W5DBG("MCP: MQTT initial connect failed (supervisor will retry)\n");
    }

    /* メッセージ処理ループ
     *   MQTT が落ちている間は wait_flg が 5 秒 timeout で帰るだけで
     *   何もしない。supervisor タスクが復旧後に recv callback が動き、
     *   このループが再びメッセージを処理するようになる。 */
    while (mcp_running) {
        UINT flgptn;
        ER err = tk_wai_flg(mcp_flgid, MCP_EVT_MSG,
                         TWF_ORW | TWF_BITCLR, &flgptn, 5000);

        if (err == E_OK && mcp_rxlen > 0) {
            mcp_dispatch((const char *)mcp_rxbuf);
            mcp_rxlen = 0;
        }
    }

    tk_mqtt_stop();
    tk_ext_tsk();
}

/*----------------------------------------------------------------------
 * MQTT supervisor タスク
 *   5 秒間隔で tk_mqtt_is_connected() を確認し、切断されていたら
 *   tk_mqtt_stop() で死んだ MQTT タスクを掃除してから mcp_mqtt_connect()
 *   を再実行する。mcp_running が FALSE になれば自身も終了。
 */
LOCAL void mcp_supervisor_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    /* 起動直後は mcp_task 側が初期接続中。5 秒だけ待って監視開始 */
    tk_dly_tsk(5000);

    while (mcp_running) {
        if (!tk_mqtt_is_connected()) {
            W5DBG("MCP super: MQTT down, reconnecting...\n");
            tk_mqtt_stop();   /* 残骸タスクを掃除 (idempotent) */
            ER err = mcp_mqtt_connect();
            if (err == E_OK) {
                W5DBG("MCP super: MQTT restored\n");
            } else {
                W5DBG("MCP super: reconnect failed, retry next cycle\n");
            }
        }
        tk_dly_tsk(5000);
    }
    tk_ext_tsk();
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT ER tk_mcp_start(const T_MCP_CONF *conf)
{
    if (conf == NULL) return E_PAR;
    if (mcp_tskid > 0) return E_OBJ;

    memcpy(&mcp_conf, conf, sizeof(T_MCP_CONF));
    mcp_running = TRUE;

    /* セキュリティ初期化 (タスク起動前に完了させる) */
    CreateLock(&rate_lock, (CONST UB *)"rate");

    /* イベントフラグ生成 */
    T_CFLG cflg;
    cflg.exinf   = NULL;
    cflg.flgatr  = TA_TPRI | TA_WMUL;
    cflg.iflgptn = 0;
    mcp_flgid = tk_cre_flg(&cflg);
    if (mcp_flgid < 0) return E_NOMEM;

    /* MCP タスク生成・起動 */
    T_CTSK ctsk;
    ctsk.exinf   = NULL;
    ctsk.tskatr  = TA_HLNG | TA_RNG3;
    ctsk.task    = (FP)mcp_task;
    ctsk.itskpri = MCP_TASK_PRI;
    ctsk.stksz   = MCP_TASK_STKSZ;
    ctsk.bufptr  = NULL;

    mcp_tskid = tk_cre_tsk(&ctsk);
    if (mcp_tskid < 0) {
        tk_del_flg(mcp_flgid);
        mcp_flgid = 0;
        return E_NOMEM;
    }

    tk_sta_tsk(mcp_tskid, 0);

    /* MQTT supervisor タスク (低優先度、独立スタック)
     *   失敗しても MCP 本体は動かす方針: 監視ロスはあるが MCP は使える */
    T_CTSK super_ctsk;
    super_ctsk.exinf   = NULL;
    super_ctsk.tskatr  = TA_HLNG | TA_RNG3;
    super_ctsk.task    = (FP)mcp_supervisor_task;
    super_ctsk.itskpri = MCP_TASK_PRI + 2;   /* MCP より低優先度 */
    super_ctsk.stksz   = 4096;               /* W5DBG/tm_printf 余裕分 */
    super_ctsk.bufptr  = NULL;
    mcp_super_tskid = tk_cre_tsk(&super_ctsk);
    if (mcp_super_tskid > 0) {
        tk_sta_tsk(mcp_super_tskid, 0);
    } else {
        W5DBG("MCP: supervisor task create failed (%d)\n", mcp_super_tskid);
    }

    return E_OK;
}

EXPORT ER tk_mcp_stop(void)
{
    if (mcp_tskid <= 0) return E_OBJ;

    mcp_running = FALSE;

    /* タスクを起床して終了を促す */
    if (mcp_flgid > 0) {
        tk_set_flg(mcp_flgid, MCP_EVT_MSG);
    }

    /* MCP タスク終了待ち */
    INT retry;
    for (retry = 0; retry < 50; retry++) {
        T_RTSK rtsk;
        if (tk_ref_tsk(mcp_tskid, &rtsk) != E_OK) break;
        if (rtsk.tskstat == TTS_DMT) break;
        tk_dly_tsk(100);
    }

    tk_del_tsk(mcp_tskid);
    mcp_tskid = 0;

    /* supervisor 終了待ち (mcp_running=FALSE で 5 秒以内に自然終了) */
    if (mcp_super_tskid > 0) {
        for (retry = 0; retry < 60; retry++) {
            T_RTSK rtsk;
            if (tk_ref_tsk(mcp_super_tskid, &rtsk) != E_OK) break;
            if (rtsk.tskstat == TTS_DMT) break;
            tk_dly_tsk(100);
        }
        tk_del_tsk(mcp_super_tskid);
        mcp_super_tskid = 0;
    }

    if (mcp_flgid > 0) {
        tk_del_flg(mcp_flgid);
        mcp_flgid = 0;
    }

    return E_OK;
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
