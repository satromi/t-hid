/*
 *----------------------------------------------------------------------
 *    MCP Server over MQTT for μT-Kernel 3.0
 *
 *    Tool implementations (28 tools)
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include "tk_mcp_int.h"
#include "tk_mcp_hal.h"

/* app_main.c で定義 — MCP 手動制御フラグ */
extern BOOL g_led_manual;

/*----------------------------------------------------------------------
 * send_keys — USB HID でテキスト送出
 */
/*
 * ASCII → HID キーコード変換テーブル
 * [0] = HID keycode, [1] = modifier (0x02 = SHIFT)
 */
LOCAL const UB ascii_to_hid[][2] = {
    /* 0x20 ' ' */ {0x2C, 0x00},
    /* 0x21 '!' */ {0x1E, 0x02},
    /* 0x22 '"' */ {0x34, 0x02},
    /* 0x23 '#' */ {0x20, 0x02},
    /* 0x24 '$' */ {0x21, 0x02},
    /* 0x25 '%' */ {0x22, 0x02},
    /* 0x26 '&' */ {0x24, 0x02},
    /* 0x27 '\''*/ {0x34, 0x00},
    /* 0x28 '(' */ {0x26, 0x02},
    /* 0x29 ')' */ {0x27, 0x02},
    /* 0x2A '*' */ {0x25, 0x02},
    /* 0x2B '+' */ {0x2E, 0x02},
    /* 0x2C ',' */ {0x36, 0x00},
    /* 0x2D '-' */ {0x2D, 0x00},
    /* 0x2E '.' */ {0x37, 0x00},
    /* 0x2F '/' */ {0x38, 0x00},
    /* 0x30-0x39 '0'-'9' */
    {0x27, 0x00}, {0x1E, 0x00}, {0x1F, 0x00}, {0x20, 0x00}, {0x21, 0x00},
    {0x22, 0x00}, {0x23, 0x00}, {0x24, 0x00}, {0x25, 0x00}, {0x26, 0x00},
    /* 0x3A ':' */ {0x33, 0x02},
    /* 0x3B ';' */ {0x33, 0x00},
    /* 0x3C '<' */ {0x36, 0x02},
    /* 0x3D '=' */ {0x2E, 0x00},
    /* 0x3E '>' */ {0x37, 0x02},
    /* 0x3F '?' */ {0x38, 0x02},
    /* 0x40 '@' */ {0x1F, 0x02},
    /* 0x41-0x5A 'A'-'Z' */
    {0x04, 0x02}, {0x05, 0x02}, {0x06, 0x02}, {0x07, 0x02}, {0x08, 0x02},
    {0x09, 0x02}, {0x0A, 0x02}, {0x0B, 0x02}, {0x0C, 0x02}, {0x0D, 0x02},
    {0x0E, 0x02}, {0x0F, 0x02}, {0x10, 0x02}, {0x11, 0x02}, {0x12, 0x02},
    {0x13, 0x02}, {0x14, 0x02}, {0x15, 0x02}, {0x16, 0x02}, {0x17, 0x02},
    {0x18, 0x02}, {0x19, 0x02}, {0x1A, 0x02}, {0x1B, 0x02}, {0x1C, 0x02},
    {0x1D, 0x02},
    /* 0x5B '[' */ {0x2F, 0x00},
    /* 0x5C '\\'*/ {0x31, 0x00},
    /* 0x5D ']' */ {0x30, 0x00},
    /* 0x5E '^' */ {0x23, 0x02},
    /* 0x5F '_' */ {0x2D, 0x02},
    /* 0x60 '`' */ {0x35, 0x00},
    /* 0x61-0x7A 'a'-'z' */
    {0x04, 0x00}, {0x05, 0x00}, {0x06, 0x00}, {0x07, 0x00}, {0x08, 0x00},
    {0x09, 0x00}, {0x0A, 0x00}, {0x0B, 0x00}, {0x0C, 0x00}, {0x0D, 0x00},
    {0x0E, 0x00}, {0x0F, 0x00}, {0x10, 0x00}, {0x11, 0x00}, {0x12, 0x00},
    {0x13, 0x00}, {0x14, 0x00}, {0x15, 0x00}, {0x16, 0x00}, {0x17, 0x00},
    {0x18, 0x00}, {0x19, 0x00}, {0x1A, 0x00}, {0x1B, 0x00}, {0x1C, 0x00},
    {0x1D, 0x00},
};

cJSON *tool_send_keys(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *text = cJSON_GetObjectItem(args, "text");
    if (text == NULL || !cJSON_IsString(text)) {
        cJSON_AddStringToObject(result, "error", "missing text parameter");
        return result;
    }

    const char *str = text->valuestring;
    ID dd = tk_opn_dev((UB *)"usbk", TD_WRITE);
    if (dd >= 0) {
        T_USB_HID_KBD_REPORT report;
        INT i, sent = 0;
        for (i = 0; str[i] != '\0'; i++) {
            UB ch = (UB)str[i];

            /* ASCII → HID 変換 (0x20-0x7A) */
            if (ch < 0x20 || ch > 0x7A) {
                if (ch == '\n') {
                    SZ asize; INT r;
                    memset(&report, 0, sizeof(report));
                    report.keycode[0] = 0x28;
                    for (r = 0; r < 50; r++) {
                        if (tk_swri_dev(dd, 0, &report, sizeof(report), &asize) == E_OK) break;
                        tk_dly_tsk(1);
                    }
                    memset(&report, 0, sizeof(report));
                    for (r = 0; r < 50; r++) {
                        if (tk_swri_dev(dd, 0, &report, sizeof(report), &asize) == E_OK) break;
                        tk_dly_tsk(1);
                    }
                    sent++;
                }
                continue;
            }

            UB hid_key = ascii_to_hid[ch - 0x20][0];
            UB hid_mod = ascii_to_hid[ch - 0x20][1];

            /* キー押下レポート (E_BUSY なら 1ms 待ちリトライ) */
            memset(&report, 0, sizeof(report));
            report.modifier = hid_mod;
            report.keycode[0] = hid_key;
            {
                SZ asize;
                INT retry;
                for (retry = 0; retry < 50; retry++) {
                    ER rc = tk_swri_dev(dd, 0, &report, sizeof(report), &asize);
                    if (rc == E_OK) break;
                    tk_dly_tsk(1);
                }
            }

            /* キーリリースレポート */
            memset(&report, 0, sizeof(report));
            {
                SZ asize;
                INT retry;
                for (retry = 0; retry < 50; retry++) {
                    ER rc = tk_swri_dev(dd, 0, &report, sizeof(report), &asize);
                    if (rc == E_OK) break;
                    tk_dly_tsk(1);
                }
            }

            sent++;
        }
        tk_cls_dev(dd, 0);
        cJSON_AddNumberToObject(result, "chars_sent", sent);
    } else {
        cJSON_AddStringToObject(result, "error", "USB HID device not available");
    }

    return result;
}

/*----------------------------------------------------------------------
 * press_combo — 修飾キー + キーコードを同時送信
 *
 * args:
 *   modifier: ビットマスク (HID_MOD_LCTRL=1, LSHIFT=2, LALT=4, LGUI=8,
 *             RCTRL=16, RSHIFT=32, RALT=64, RGUI=128)、もしくは名前文字列
 *             "ctrl","shift","alt","gui","ctrl+shift" 等 (+ 区切り)
 *   key: HID キーコード (数値 0x04=A, 0x05=B, ..., 0x28=Enter 等)
 *        または ASCII 1 文字 (小文字優先)
 *   hold_ms: 押下時間 (デフォルト 10ms)
 */

LOCAL UB parse_modifier(cJSON *item)
{
    if (item == NULL) return 0;
    if (cJSON_IsNumber(item)) return (UB)item->valueint;
    if (!cJSON_IsString(item) || item->valuestring == NULL) return 0;
    const char *s = item->valuestring;
    UB mod = 0;
    while (*s) {
        /* トークン抽出 (+ 区切り or , 区切り) */
        const char *p = s;
        while (*p && *p != '+' && *p != ',') p++;
        UW len = (UW)(p - s);
        if      (len == 4 && strncmp(s, "ctrl", 4) == 0)  mod |= 0x01;
        else if (len == 5 && strncmp(s, "shift", 5) == 0) mod |= 0x02;
        else if (len == 3 && strncmp(s, "alt", 3) == 0)   mod |= 0x04;
        else if (len == 3 && strncmp(s, "gui", 3) == 0)   mod |= 0x08;
        else if (len == 3 && strncmp(s, "win", 3) == 0)   mod |= 0x08;
        else if (len == 3 && strncmp(s, "cmd", 3) == 0)   mod |= 0x08;
        s = (*p) ? p + 1 : p;
    }
    return mod;
}

LOCAL UB parse_keycode(cJSON *item)
{
    if (item == NULL) return 0;
    if (cJSON_IsNumber(item)) return (UB)item->valueint;
    if (!cJSON_IsString(item) || item->valuestring == NULL) return 0;
    const char *s = item->valuestring;
    if (s[0] == '\0') return 0;
    UB ch = (UB)s[0];
    /* ASCII 大文字 → 小文字 */
    if (ch >= 'A' && ch <= 'Z') ch += 32;
    if (ch >= 0x20 && ch <= 0x7A) {
        return ascii_to_hid[ch - 0x20][0];
    }
    return 0;
}

cJSON *tool_press_combo(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    UB mod = parse_modifier(cJSON_GetObjectItem(args, "modifier"));
    UB key = parse_keycode(cJSON_GetObjectItem(args, "key"));
    if (mod == 0 && key == 0) {
        cJSON_AddStringToObject(result, "error", "modifier and/or key required");
        return result;
    }

    cJSON *hold = cJSON_GetObjectItem(args, "hold_ms");
    UW hold_ms = (cJSON_IsNumber(hold) && hold->valueint > 0)
                 ? (UW)hold->valueint : 10;
    if (hold_ms > 5000) hold_ms = 5000;    /* 安全上限 5 秒 */

    ID dd = tk_opn_dev((UB *)"usbk", TD_WRITE);
    if (dd < 0) {
        cJSON_AddStringToObject(result, "error", "USB HID device not available");
        return result;
    }

    T_USB_HID_KBD_REPORT report;
    SZ asize;
    INT retry;
    /* 押下レポート (modifier + keycode) */
    memset(&report, 0, sizeof(report));
    report.modifier = mod;
    report.keycode[0] = key;
    for (retry = 0; retry < 50; retry++) {
        if (tk_swri_dev(dd, 0, &report, sizeof(report), &asize) == E_OK) break;
        tk_dly_tsk(1);
    }
    tk_dly_tsk(hold_ms);
    /* 全リリース */
    memset(&report, 0, sizeof(report));
    for (retry = 0; retry < 50; retry++) {
        if (tk_swri_dev(dd, 0, &report, sizeof(report), &asize) == E_OK) break;
        tk_dly_tsk(1);
    }
    tk_cls_dev(dd, 0);

    cJSON_AddNumberToObject(result, "modifier", mod);
    cJSON_AddNumberToObject(result, "key", key);
    cJSON_AddNumberToObject(result, "hold_ms", hold_ms);
    cJSON_AddStringToObject(result, "status", "sent");
    return result;
}

/*----------------------------------------------------------------------
 * set_layer — TG() レイヤー切替
 *
 * args:
 *   layer: レイヤー番号 (0..7)
 *   action: "on" | "off" | "toggle" | "clear_all" (デフォルト "toggle")
 *
 * action=clear_all の場合は全トグルを解除。
 * layer=0 (base) は常時アクティブなので on/off/toggle は no-op。
 */
extern UB   kb_get_current_layer(void);
extern UB   kb_get_active_layers(void);
extern void kb_set_toggled_layer(UB layer_num, BOOL on);
extern void kb_clear_toggled_layers(void);

/* 弱シンボル: キーボードフレームワーク非リンク (WiFi-only BLE=0) 時の stub。
 * kb_main.c で定義される強シンボルが存在すれば上書きされる。 */
__attribute__((weak)) UB   kb_get_current_layer(void) { return 0; }
__attribute__((weak)) UB   kb_get_active_layers(void) { return 1; }
__attribute__((weak)) void kb_set_toggled_layer(UB l, BOOL on) { (void)l; (void)on; }
__attribute__((weak)) void kb_clear_toggled_layers(void) {}

cJSON *tool_set_layer(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *action = cJSON_GetObjectItem(args, "action");
    const char *act = (cJSON_IsString(action) && action->valuestring) ?
                      action->valuestring : "toggle";

    if (strcmp(act, "clear_all") == 0) {
        kb_clear_toggled_layers();
        cJSON_AddStringToObject(result, "action", "clear_all");
        cJSON_AddNumberToObject(result, "current_layer", kb_get_current_layer());
        cJSON_AddNumberToObject(result, "active_layers", kb_get_active_layers());
        return result;
    }

    cJSON *layer = cJSON_GetObjectItem(args, "layer");
    if (!cJSON_IsNumber(layer)) {
        cJSON_AddStringToObject(result, "error", "layer parameter required");
        return result;
    }
    INT layer_num = layer->valueint;
    if (layer_num < 0 || layer_num > 7) {
        cJSON_AddStringToObject(result, "error", "layer must be 0..7");
        return result;
    }

    UB before = kb_get_active_layers();
    BOOL currently_on = (before & (1u << layer_num)) != 0;
    BOOL new_on;
    if (strcmp(act, "on") == 0)        new_on = TRUE;
    else if (strcmp(act, "off") == 0)  new_on = FALSE;
    else                               new_on = !currently_on;  /* toggle */

    kb_set_toggled_layer((UB)layer_num, new_on);

    cJSON_AddStringToObject(result, "action", act);
    cJSON_AddNumberToObject(result, "layer", layer_num);
    cJSON_AddBoolToObject(result, "layer_on", new_on);
    cJSON_AddNumberToObject(result, "current_layer", kb_get_current_layer());
    cJSON_AddNumberToObject(result, "active_layers", kb_get_active_layers());
    return result;
}

/*----------------------------------------------------------------------
 * get_keyboard_status — キーボードの状態取得
 */
extern const char *kb_output_active_name(void);
__attribute__((weak)) const char *kb_output_active_name(void) { return "none"; }

cJSON *tool_get_keyboard_status(cJSON *args)
{
    (void)args;

    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "current_layer", kb_get_current_layer());
    cJSON_AddNumberToObject(result, "active_layers", kb_get_active_layers());
    const char *out_name = kb_output_active_name();
    cJSON_AddStringToObject(result, "hid_output", out_name);
    cJSON_AddBoolToObject(result, "usb_connected", strcmp(out_name, "USB") == 0);
    cJSON_AddStringToObject(result, "layout", "QWERTY");

    return result;
}

/*----------------------------------------------------------------------
 * get_tasks — RTOS タスク一覧
 */
cJSON *tool_get_tasks(cJSON *args)
{
    (void)args;

    cJSON *result = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();

    static const char *state_names[] = {
        "RUN", "READY", "WAIT", "SUSPEND", "WAIT-SUS", "DORMANT", "NOEXS"
    };

    ID id;
    for (id = 1; id <= 32; id++) {
        T_RTSK rtsk;
        if (tk_ref_tsk(id, &rtsk) != E_OK) continue;
        if (rtsk.tskstat == 0) continue;  /* 存在しないタスク */

        cJSON *t = cJSON_CreateObject();
        cJSON_AddNumberToObject(t, "id", id);
        cJSON_AddNumberToObject(t, "priority", rtsk.tskpri);

        INT st = 0;
        if (rtsk.tskstat & TTS_RUN) st = 0;
        else if (rtsk.tskstat & TTS_RDY) st = 1;
        else if (rtsk.tskstat & TTS_WAI) st = 2;
        else if (rtsk.tskstat & TTS_SUS) st = 3;
        else if (rtsk.tskstat & TTS_DMT) st = 5;
        cJSON_AddStringToObject(t, "state", state_names[st]);

        cJSON_AddItemToArray(arr, t);
    }

    cJSON_AddItemToObject(result, "tasks", arr);
    return result;
}

/*----------------------------------------------------------------------
 * get_uptime — 起動後経過時間
 */
cJSON *tool_get_uptime(cJSON *args)
{
    (void)args;

    cJSON *result = cJSON_CreateObject();
    SYSTIM otm;
    tk_get_otm(&otm);

    /* ミリ秒 → 秒 (64bit 対応: 49.7 日超でも正常) */
    UD ms_total = ((UD)(UW)otm.hi << 32) | otm.lo;
    UW sec = (UW)(ms_total / 1000);
    UW ms  = (UW)(ms_total % 1000);

    cJSON_AddNumberToObject(result, "uptime_sec", sec);
    cJSON_AddNumberToObject(result, "uptime_ms", ms);

    /* 人間可読形式 */
    char buf[32];
    UW h = sec / 3600;
    UW m = (sec % 3600) / 60;
    UW s = sec % 60;
    /* sprintf 相当の手動フォーマット */
    INT pos = 0;
    if (h > 0) {
        buf[pos++] = '0' + (h / 10); buf[pos++] = '0' + (h % 10);
        buf[pos++] = 'h';
    }
    buf[pos++] = '0' + (m / 10); buf[pos++] = '0' + (m % 10);
    buf[pos++] = 'm';
    buf[pos++] = '0' + (s / 10); buf[pos++] = '0' + (s % 10);
    buf[pos++] = 's';
    buf[pos] = '\0';
    cJSON_AddStringToObject(result, "formatted", buf);

    return result;
}

/*----------------------------------------------------------------------
 * get_network — ネットワーク情報
 */
cJSON *tool_get_network(cJSON *args)
{
    (void)args;

    cJSON *result = cJSON_CreateObject();

    UB ip[4], mac[6], gw[4], mask[4];
    getSIPR(ip);
    getSHAR(mac);
    getGAR(gw);
    getSUBR(mask);

    char buf[20];

    /* IP */
    INT pos = 0;
    INT i;
    for (i = 0; i < 4; i++) {
        if (i > 0) buf[pos++] = '.';
        if (ip[i] >= 100) buf[pos++] = '0' + ip[i] / 100;
        if (ip[i] >= 10)  buf[pos++] = '0' + (ip[i] / 10) % 10;
        buf[pos++] = '0' + ip[i] % 10;
    }
    buf[pos] = '\0';
    cJSON_AddStringToObject(result, "ip", buf);

    /* MAC */
    static const char hex[] = "0123456789ABCDEF";
    pos = 0;
    for (i = 0; i < 6; i++) {
        if (i > 0) buf[pos++] = ':';
        buf[pos++] = hex[mac[i] >> 4];
        buf[pos++] = hex[mac[i] & 0xF];
    }
    buf[pos] = '\0';
    cJSON_AddStringToObject(result, "mac", buf);

    /* Link */
#if defined(WIFI_CYW43)
    cJSON_AddBoolToObject(result, "link_up",
        (tk_wifi_get_state() == TK_WIFI_ST_READY) ? 1 : 0);
#else
    cJSON_AddBoolToObject(result, "link_up",
        (wizphy_getphylink() == PHY_LINK_ON) ? 1 : 0);
#endif

    /* Hostname */
    char hostname[64];
#if defined(WIFI_CYW43)
    /* WiFi: ホスト名はハードコード (DHCP hostname 未設定) */
    memcpy(hostname, "pico-w-kb", 10);
#else
    tk_net_gethostname(hostname, 64);
#endif
    cJSON_AddStringToObject(result, "hostname", hostname);

    return result;
}

/*----------------------------------------------------------------------
 * get_datetime — 現在時刻
 */
cJSON *tool_get_datetime(cJSON *args)
{
    (void)args;

    cJSON *result = cJSON_CreateObject();

    UW unix_sec = dt_getunixtime();
    /* cJSON の double→sprintf がスタック消費するため文字列で返す */
    {
        char ts[12];
        UW v = unix_sec;
        INT i = 11;
        ts[i] = '\0';
        do { ts[--i] = '0' + (v % 10); v /= 10; } while (v > 0);
        cJSON_AddStringToObject(result, "unix_timestamp", &ts[i]);
    }

    DATE_TIM dt;
    if (dt_gettime(&dt) == E_OK) {
        char buf[20];
        dt_format(&dt, buf, sizeof(buf));
        cJSON_AddStringToObject(result, "datetime", buf);
    } else {
        cJSON_AddStringToObject(result, "datetime", "not synced");
    }

    return result;
}

/*----------------------------------------------------------------------
 * set_led — オンボード LED (GP25)
 */
cJSON *tool_set_led(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();

    /* heartbeat パラメータ: true でハートビート再開 */
    cJSON *hb = cJSON_GetObjectItem(args, "heartbeat");
    if (hb != NULL && cJSON_IsTrue(hb)) {
        g_led_manual = FALSE;
        cJSON_AddBoolToObject(result, "heartbeat", 1);
        return result;
    }

    /* state パラメータ: LED 手動 ON/OFF */
    cJSON *state = cJSON_GetObjectItem(args, "state");
    if (state == NULL) {
        cJSON_AddStringToObject(result, "error", "specify state or heartbeat");
        return result;
    }

    BOOL on = cJSON_IsTrue(state);
    g_led_manual = TRUE;
    if (on) {
        mcp_led_on();
    } else {
        mcp_led_off();
    }

    cJSON_AddBoolToObject(result, "led", on);
    cJSON_AddBoolToObject(result, "heartbeat", 0);
    return result;
}

/*----------------------------------------------------------------------
 * reboot — ソフトリセット
 */
cJSON *tool_reboot(cJSON *args)
{
    (void)args;

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "rebooting");

    /* レスポンス送信のため少し待ってからリセット */
    tk_dly_tsk(500);

    /* AIRCR SYSRESETREQ */
    out_w(0xE000ED0C, 0x05FA0004);

    /* ここには到達しない */
    return result;
}

/*----------------------------------------------------------------------
 * read_adc — ADC チャンネル読み取り (ch4 = 温度センサ)
 *
 * RP2040 ADC: 12bit, 3.3V 基準
 * 温度センサ: V = 0.706 - 0.001721 * T → T = (0.706 - V) / 0.001721
 */
cJSON *tool_read_adc(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *ch_item = cJSON_GetObjectItem(args, "ch");
    if (ch_item == NULL || !cJSON_IsNumber(ch_item)) {
        cJSON_AddStringToObject(result, "error", "missing ch");
        return result;
    }
    INT ch = ch_item->valueint;
    if (ch < 0 || ch > 4) {
        cJSON_AddStringToObject(result, "error", "ch must be 0-4");
        return result;
    }

#if MCP_HAS_ADC
    UW raw = mcp_adc_read_raw(ch);

    cJSON_AddNumberToObject(result, "ch", ch);
    cJSON_AddNumberToObject(result, "raw", raw);

    /* 電圧 (mV) */
    UW mv = (raw * 3300) / 4095;
    cJSON_AddNumberToObject(result, "voltage_mv", mv);

    /* 温度センサの場合は摂氏に変換 */
    if (ch == MCP_ADC_TEMP_CH) {
        /* T = 27 - (V - 0.706) / 0.001721 */
        /* 整数演算: T_x10 = 270 - (mv - 706) * 10000 / 1721 */
        W temp_x10 = 270 - (W)(mv - 706) * 10000 / 1721;
        cJSON_AddNumberToObject(result, "temp_c_x10", temp_x10);
    }

    return result;
#else
    cJSON_AddStringToObject(result, "error", "ADC not supported on this platform");
    return result;
#endif
}

/*----------------------------------------------------------------------
 * gpio_control — GPIO 読み書き
 */
cJSON *tool_gpio_control(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *pin_item = cJSON_GetObjectItem(args, "pin");
    cJSON *mode_item = cJSON_GetObjectItem(args, "mode");

    if (pin_item == NULL || !cJSON_IsNumber(pin_item) ||
        mode_item == NULL || !cJSON_IsString(mode_item)) {
        cJSON_AddStringToObject(result, "error", "missing pin/mode");
        return result;
    }

    INT pin = pin_item->valueint;
    if (pin < 0 || pin > 29) {
        cJSON_AddStringToObject(result, "error", "pin must be 0-29");
        return result;
    }

    const char *mode = mode_item->valuestring;

    if (strcmp(mode, "read") == 0) {
        /* 入力として読み取り */
        mcp_gpio_set_input(pin);
        UW val = mcp_gpio_read(pin);
        cJSON_AddNumberToObject(result, "pin", pin);
        cJSON_AddNumberToObject(result, "value", val);
    } else if (strcmp(mode, "output_high") == 0) {
        mcp_gpio_set_output(pin);
        mcp_gpio_write_high(pin);
        cJSON_AddNumberToObject(result, "pin", pin);
        cJSON_AddNumberToObject(result, "value", 1);
    } else if (strcmp(mode, "output_low") == 0) {
        mcp_gpio_set_output(pin);
        mcp_gpio_write_low(pin);
        cJSON_AddNumberToObject(result, "pin", pin);
        cJSON_AddNumberToObject(result, "value", 0);
    } else {
        cJSON_AddStringToObject(result, "error", "mode: read/output_high/output_low");
    }

    return result;
}

/*----------------------------------------------------------------------
 * pwm_control — PWM 出力
 *
 * RP2040 PWM: 8 スライス × 2 チャンネル (A/B)
 * GPIO N → slice = N/2, channel = N%2
 * freq = 125MHz / (wrap+1) / div
 */
cJSON *tool_pwm_control(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *pin_item  = cJSON_GetObjectItem(args, "pin");
    cJSON *duty_item = cJSON_GetObjectItem(args, "duty");
    cJSON *freq_item = cJSON_GetObjectItem(args, "freq");

    if (pin_item == NULL || !cJSON_IsNumber(pin_item) ||
        duty_item == NULL || !cJSON_IsNumber(duty_item)) {
        cJSON_AddStringToObject(result, "error", "missing pin/duty");
        return result;
    }

    INT pin  = pin_item->valueint;
    INT duty = duty_item->valueint;
    UW freq  = (freq_item && cJSON_IsNumber(freq_item)) ?
               (UW)freq_item->valueint : 1000;

    if (pin < 0 || pin > 29 || duty < 0 || duty > 100 || freq == 0) {
        cJSON_AddStringToObject(result, "error", "invalid params");
        return result;
    }

#if MCP_HAS_PWM
    /* GPIO を PWM 機能に設定 */
    mcp_pwm_set_funcsel(pin);

#if defined(CPU_RP2040)
    /* RP2040: PWM スライスレジスタ (各 0x14 バイト間隔) */
    INT slice = pin / 2;
    INT ch    = pin % 2;  /* 0=A, 1=B */

    UW wrap = (125000000 / freq) - 1;
    if (wrap > 0xFFFF) wrap = 0xFFFF;
    UW cc_val = (wrap * (UW)duty) / 100;

    UW slice_base = MCP_PWM_BASE + (UW)slice * 0x14;
    out_w(slice_base + 0x00, 0);          /* CSR: disable */
    out_w(slice_base + 0x04, (1 << 4));   /* DIV: int=1, frac=0 */
    out_w(slice_base + 0x08, wrap);       /* TOP (wrap) */

    UW cc_reg = in_w(slice_base + 0x0C);
    if (ch == 0) {
        cc_reg = (cc_reg & 0xFFFF0000) | (cc_val & 0xFFFF);
    } else {
        cc_reg = (cc_reg & 0x0000FFFF) | ((cc_val & 0xFFFF) << 16);
    }
    out_w(slice_base + 0x0C, cc_reg);
    out_w(slice_base + 0x00, 1);          /* CSR: enable */

    cJSON_AddNumberToObject(result, "slice", slice);
    cJSON_AddNumberToObject(result, "channel", ch);
    cJSON_AddNumberToObject(result, "wrap", wrap);

#elif defined(MTKBSP_CPU_STM32H5)
    /* STM32H5: TIM2 PWM (APB1 = 250MHz) */
    /* pin 0-3 → TIM2 CH1-CH4, pin 16-17 → TIM3 CH3-CH4 */
    UW tim_base;
    INT tim_ch;

    if (pin < 4) {
        tim_base = 0x40000000;  /* TIM2 */
        tim_ch = pin + 1;       /* CH1-CH4 */
        /* TIM2 クロック有効化 (APB1LENR bit 0) */
        out_w(RCC_APB1LENR, in_w(RCC_APB1LENR) | (1u << 0));
    } else if (pin >= 16 && pin <= 17) {
        tim_base = 0x40000400;  /* TIM3 */
        tim_ch = (pin - 16) + 3; /* CH3-CH4 */
        out_w(RCC_APB1LENR, in_w(RCC_APB1LENR) | (1u << 1));
    } else {
        cJSON_AddStringToObject(result, "error", "pin not PWM-capable");
        return result;
    }

    /* PSC + ARR で周波数設定: freq = 250MHz / ((PSC+1) * (ARR+1)) */
    UW psc = 249;                         /* 250MHz / 250 = 1MHz timer clock */
    UW arr = (1000000 / freq) - 1;        /* ARR = 1MHz / freq - 1 */
    if (arr > 0xFFFF) arr = 0xFFFF;
    UW ccr = (arr * (UW)duty) / 100;

    out_w(tim_base + 0x00, 0);            /* CR1: disable */
    out_w(tim_base + 0x28, psc);          /* PSC */
    out_w(tim_base + 0x2C, arr);          /* ARR */

    /* CCMR: PWM mode 1 (110) + preload enable */
    {
        UW ccmr_offset = (tim_ch <= 2) ? 0x18 : 0x1C;  /* CCMR1 or CCMR2 */
        INT ccmr_shift = ((tim_ch - 1) % 2) * 8;
        UW ccmr = in_w(tim_base + ccmr_offset);
        ccmr &= ~(0xFF << ccmr_shift);
        ccmr |= ((6 << 4) | (1 << 3)) << ccmr_shift;  /* OC1M=110, OC1PE=1 */
        out_w(tim_base + ccmr_offset, ccmr);
    }

    /* CCR: duty */
    out_w(tim_base + 0x34 + (tim_ch - 1) * 4, ccr);

    /* CCER: enable channel output */
    {
        UW ccer = in_w(tim_base + 0x20);
        ccer |= (1u << ((tim_ch - 1) * 4));  /* CCxE */
        out_w(tim_base + 0x20, ccer);
    }

    out_w(tim_base + 0x14, 1);            /* EGR: UG (update) */
    out_w(tim_base + 0x00, 1);            /* CR1: CEN (enable) */

    cJSON_AddNumberToObject(result, "timer_ch", tim_ch);
#endif

    cJSON_AddNumberToObject(result, "pin", pin);
    cJSON_AddNumberToObject(result, "freq", freq);
    cJSON_AddNumberToObject(result, "duty", duty);

    return result;
#else
    cJSON_AddStringToObject(result, "error", "PWM not supported on this platform");
    return result;
#endif
}

/*----------------------------------------------------------------------
 * 保護領域: peek/poke/shell reg で読み書きさせないアドレス
 *   秘密鍵などを持つアプリが強い定義で上書きする
 */
__attribute__((weak)) BOOL mcp_addr_is_protected(UW addr)
{
    (void)addr;
    return FALSE;
}

/*----------------------------------------------------------------------
 * peek — メモリ/レジスタ読み取り (32bit)
 */
cJSON *tool_peek(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
#if MCP_HAS_PEEK
    cJSON *addr_item = cJSON_GetObjectItem(args, "addr");
    if (addr_item == NULL || !cJSON_IsNumber(addr_item)) {
        cJSON_AddStringToObject(result, "error", "missing addr");
        return result;
    }

    UW addr = (UW)addr_item->valuedouble;

    if (addr & 3) {
        cJSON_AddStringToObject(result, "error", "addr must be 4-byte aligned");
        return result;
    }
    if (mcp_addr_is_protected(addr)) {
        cJSON_AddStringToObject(result, "error", "protected address");
        return result;
    }

    UW val = in_w(addr);

    /* 大きな数値は文字列で返す (cJSON の double→sprintf がスタック爆発するため) */
    char buf[12];
    fmt_hex32(buf, addr);
    cJSON_AddStringToObject(result, "addr", buf);
    fmt_hex32(buf, val);
    cJSON_AddStringToObject(result, "value", buf);
#else
    cJSON_AddStringToObject(result, "error", "peek not supported on this platform");
#endif
    return result;
}

/*----------------------------------------------------------------------
 * poke — メモリ/レジスタ書き込み (32bit)
 */
cJSON *tool_poke(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
#if MCP_HAS_PEEK
    cJSON *addr_item = cJSON_GetObjectItem(args, "addr");
    cJSON *val_item  = cJSON_GetObjectItem(args, "value");
    if (addr_item == NULL || val_item == NULL) {
        cJSON_AddStringToObject(result, "error", "missing addr/value");
        return result;
    }

    UW addr = (UW)addr_item->valuedouble;
    UW val  = (UW)val_item->valuedouble;

    if (addr & 3) {
        cJSON_AddStringToObject(result, "error", "addr must be 4-byte aligned");
        return result;
    }

    /* 安全チェック: 危険な領域への書き込みを制限 */
    /* Flash XIP 領域 (0x10000000-0x101FFFFF) は直接書き込み不可 */
    if (addr >= 0x10000000 && addr < 0x10200000) {
        cJSON_AddStringToObject(result, "error", "Flash XIP region is read-only. Use flash_write");
        return result;
    }
    /* ベクタテーブル先頭 (0x20000000-0x200000BF) は破壊禁止 */
    if (addr >= 0x20000000 && addr < 0x200000C0) {
        cJSON_AddStringToObject(result, "error", "vector table region is protected");
        return result;
    }
    if (mcp_addr_is_protected(addr)) {
        cJSON_AddStringToObject(result, "error", "protected address");
        return result;
    }

    out_w(addr, val);
    UW readback = in_w(addr);

    char buf[12];
    fmt_hex32(buf, addr);
    cJSON_AddStringToObject(result, "addr", buf);
    fmt_hex32(buf, val);
    cJSON_AddStringToObject(result, "written", buf);
    fmt_hex32(buf, readback);
    cJSON_AddStringToObject(result, "readback", buf);
#else
    cJSON_AddStringToObject(result, "error", "poke not supported on this platform");
#endif
    return result;
}

/*----------------------------------------------------------------------
 * tcp_connect — 任意ホストに TCP 接続 + 送受信
 *   ソケット 1 (UTIL_SN) を一時使用
 */
cJSON *tool_tcp_connect(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *host_item = cJSON_GetObjectItem(args, "host");
    cJSON *port_item = cJSON_GetObjectItem(args, "port");
    cJSON *send_item = cJSON_GetObjectItem(args, "send");
    cJSON *tmout_item = cJSON_GetObjectItem(args, "timeout");

    if (host_item == NULL || !cJSON_IsString(host_item) ||
        port_item == NULL || !cJSON_IsNumber(port_item)) {
        cJSON_AddStringToObject(result, "error", "missing host/port");
        return result;
    }

    UB ip[4];
    if (!parse_ip(host_item->valuestring, ip)) {
        cJSON_AddStringToObject(result, "error", "invalid IP format");
        return result;
    }
    INT port_val = port_item->valueint;
    if (port_val < 1 || port_val > 65535) {
        cJSON_AddStringToObject(result, "error", "port must be 1-65535");
        return result;
    }
    UH port = (UH)port_val;
    TMO tmout = (tmout_item && cJSON_IsNumber(tmout_item)) ?
                (TMO)tmout_item->valueint : 5000;
    if (tmout < 100) tmout = 100;
    if (tmout > 30000) tmout = 30000;

    ER err = tk_sock_open(TCP_TOOL_SN, Sn_MR_TCP, 0);
    if (err != E_OK) {
        cJSON_AddStringToObject(result, "error", "socket open failed");
        return result;
    }

    err = tk_sock_connect(TCP_TOOL_SN, ip, port, tmout);
    if (err != E_OK) {
        cJSON_AddStringToObject(result, "error", "connect failed");
        tk_sock_close(TCP_TOOL_SN);
        return result;
    }

    cJSON_AddStringToObject(result, "status", "connected");

    /* 送信 */
    if (send_item != NULL && cJSON_IsString(send_item)) {
        const char *s = send_item->valuestring;
        W sent = tk_sock_send(TCP_TOOL_SN, (const UB *)s, (UH)strlen(s), tmout);
        cJSON_AddNumberToObject(result, "sent", sent > 0 ? sent : 0);
    }

    /* 受信 (最大 256 bytes) */
    UB rxbuf[TCP_TOOL_RXBUF];
    W rcvd = tk_sock_recv(TCP_TOOL_SN, rxbuf, TCP_TOOL_RXBUF - 1, tmout);
    if (rcvd > 0) {
        rxbuf[rcvd] = '\0';
        cJSON_AddStringToObject(result, "recv", (const char *)rxbuf);
        cJSON_AddNumberToObject(result, "recv_len", rcvd);
    } else {
        cJSON_AddNumberToObject(result, "recv_len", 0);
    }

    tk_sock_disconnect(TCP_TOOL_SN, 3000);
    tk_sock_close(TCP_TOOL_SN);

    return result;
}

/*----------------------------------------------------------------------
 * flash_write — Flash 末尾 4KB セクタの読み書き
 *
 * RP2040 XIP Flash: 0x10000000 ～ 0x101FFFFF (2MB)
 * 末尾 4KB セクタ: 0x101FF000 ～ 0x101FFFFF
 * 書き込みは SSI (XIP) 経由ではなく ROM 関数で行う。
 */
/* (FLASH_STORAGE_* はヘッダで定義済み) */

cJSON *tool_flash_write(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
#if MCP_HAS_FLASH
    cJSON *action_item = cJSON_GetObjectItem(args, "action");
    if (action_item == NULL || !cJSON_IsString(action_item)) {
        cJSON_AddStringToObject(result, "error", "missing action");
        return result;
    }

    const char *action = action_item->valuestring;

    if (strcmp(action, "read") == 0) {
        cJSON *off_item = cJSON_GetObjectItem(args, "offset");
        cJSON *len_item = cJSON_GetObjectItem(args, "length");
        UW offset = (off_item && cJSON_IsNumber(off_item)) ? (UW)off_item->valueint : 0;
        UW length = (len_item && cJSON_IsNumber(len_item)) ? (UW)len_item->valueint : 16;
        if (offset + length > FLASH_SECTOR_SIZE) length = FLASH_SECTOR_SIZE - offset;
        if (length > 128) length = 128;  /* レスポンスサイズ制限 */

        const UB *flash = (const UB *)(FLASH_STORAGE_BASE + offset);
        char hex[258];  /* 128 bytes * 2 + NUL */
        static const char hx[] = "0123456789ABCDEF";
        UW i;
        for (i = 0; i < length; i++) {
            hex[i * 2]     = hx[flash[i] >> 4];
            hex[i * 2 + 1] = hx[flash[i] & 0xF];
        }
        hex[length * 2] = '\0';

        cJSON_AddStringToObject(result, "action", "read");
        cJSON_AddNumberToObject(result, "offset", offset);
        cJSON_AddNumberToObject(result, "length", length);
        cJSON_AddStringToObject(result, "data", hex);

    } else if (strcmp(action, "write") == 0) {
        cJSON *off_item  = cJSON_GetObjectItem(args, "offset");
        cJSON *data_item = cJSON_GetObjectItem(args, "data");
        if (data_item == NULL || !cJSON_IsString(data_item)) {
            cJSON_AddStringToObject(result, "error", "missing data");
            return result;
        }

        UW offset = (off_item && cJSON_IsNumber(off_item)) ? (UW)off_item->valueint : 0;
        const char *hex_str = data_item->valuestring;
        UW hex_len = strlen(hex_str);
        UW byte_len = hex_len / 2;
        if (byte_len > FLASH_PAGE_SIZE) byte_len = FLASH_PAGE_SIZE;
        if (offset + byte_len > FLASH_SECTOR_SIZE) {
            cJSON_AddStringToObject(result, "error", "exceeds sector");
            return result;
        }

        /* セクタ全体を RAM にコピー → 更新 → erase → program */
        UB *sector_buf = (UB *)Kmalloc(FLASH_SECTOR_SIZE);
        if (sector_buf == NULL) {
            cJSON_AddStringToObject(result, "error", "out of memory");
            return result;
        }
        memcpy(sector_buf, (const void *)FLASH_STORAGE_BASE, FLASH_SECTOR_SIZE);

        UW i;
        for (i = 0; i < byte_len; i++) {
            sector_buf[offset + i] = (hex_val(hex_str[i*2]) << 4) | hex_val(hex_str[i*2+1]);
        }

        { UINT imask; Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(FLASH_STORAGE_OFFSET, sector_buf, FLASH_SECTOR_SIZE);
        Asm("msr primask, %0" :: "r"(imask)); }
        Kfree(sector_buf);

        cJSON_AddStringToObject(result, "action", "write");
        cJSON_AddNumberToObject(result, "offset", offset);
        cJSON_AddNumberToObject(result, "bytes", byte_len);

    } else if (strcmp(action, "erase") == 0) {
        { UINT imask; Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(FLASH_STORAGE_OFFSET, NULL, 0);
        Asm("msr primask, %0" :: "r"(imask)); }

        cJSON_AddStringToObject(result, "action", "erased");

    } else {
        cJSON_AddStringToObject(result, "error", "action: read/write/erase");
    }
#else
    cJSON_AddStringToObject(result, "error", "flash not supported on this platform");
#endif
    return result;
}

/*----------------------------------------------------------------------
 * watchdog — RP2040 ハードウェアウォッチドッグ
 */
cJSON *tool_watchdog(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
#if MCP_HAS_WATCHDOG
    cJSON *action_item = cJSON_GetObjectItem(args, "action");
    if (action_item == NULL || !cJSON_IsString(action_item)) {
        cJSON_AddStringToObject(result, "error", "missing action");
        return result;
    }

    const char *action = action_item->valuestring;

    if (strcmp(action, "status") == 0) {
#ifdef CPU_RP2040
        UW ctrl = in_w(WD_CTRL);
        UW reason = in_w(WD_REASON);
        cJSON_AddBoolToObject(result, "enabled", (ctrl & (1u << 30)) ? 1 : 0);
        cJSON_AddBoolToObject(result, "last_reset_by_wdog", (reason & 1) ? 1 : 0);
        cJSON_AddBoolToObject(result, "last_reset_by_force", (reason & 2) ? 1 : 0);
#elif defined(MTKBSP_CPU_STM32H5)
        cJSON_AddBoolToObject(result, "enabled", 1);
        cJSON_AddStringToObject(result, "type", "IWDG");
#endif

    } else if (strcmp(action, "enable") == 0) {
        cJSON *tmout_item = cJSON_GetObjectItem(args, "timeout_ms");
        UW timeout_ms = (tmout_item && cJSON_IsNumber(tmout_item)) ?
                        (UW)tmout_item->valueint : 5000;

#ifdef CPU_RP2040
        if (timeout_ms > 8388) timeout_ms = 8388;
        out_w(WD_TICK, 12 | (1u << 9));
        out_w(WD_LOAD, timeout_ms * 1000);
        out_w(WD_CTRL, (1u << 30) | (1u << 25) | (1u << 24));
#elif defined(MTKBSP_CPU_STM32H5)
        /* IWDG: LSI ≒ 32kHz, prescaler /64 → 500Hz, RLR = timeout_ms/2 */
        if (timeout_ms > 8000) timeout_ms = 8000;
        UW rlr = (timeout_ms * 500) / 1000;
        if (rlr > 4095) rlr = 4095;
        out_w(STM32_IWDG_KR, 0x5555);   /* Unlock */
        while (in_w(STM32_IWDG_SR) & 3) {}  /* PVU+RVU 完了待ち */
        out_w(STM32_IWDG_PR, 5);        /* /64 */
        out_w(STM32_IWDG_RLR, rlr);
        out_w(STM32_IWDG_KR, 0xCCCC);   /* Start */
#endif
        /* cJSON_AddNumberToObject は newlib float sprintf で
         * 2-4KB スタック消費 → MCP タスクが Hard fault する症状を観測。
         * fmt_hex32 でゼロ詰め hex 文字列化して回避 (task_stop と同じパターン)。 */
        char tmstr[12];
        fmt_hex32(tmstr, timeout_ms);
        cJSON_AddStringToObject(result, "status", "enabled");
        cJSON_AddStringToObject(result, "timeout_ms", tmstr);

    } else if (strcmp(action, "feed") == 0) {
#ifdef CPU_RP2040
        out_w(WD_LOAD, in_w(WD_CTRL) & 0x00FFFFFF);
#elif defined(MTKBSP_CPU_STM32H5)
        out_w(STM32_IWDG_KR, 0xAAAA);   /* Reload */
#endif
        cJSON_AddStringToObject(result, "status", "fed");

    } else if (strcmp(action, "disable") == 0) {
#ifdef CPU_RP2040
        /* WD_CTRL を 0 直書きすると TRIGGER bit (31) と PAUSE bit
         * 全部クリアになり、disable 後 MCP タスクが応答しなくなる
         * (再現あり)。RP2040 SDK の watchdog_disable() と同じく
         * ENABLE bit (30) のみ落とす RMW にする。 */
        out_w(WD_CTRL, in_w(WD_CTRL) & ~(1u << 30));
        cJSON_AddStringToObject(result, "status", "disabled");
#elif defined(MTKBSP_CPU_STM32H5)
        /* IWDG は一度有効化すると無効化不可 */
        cJSON_AddStringToObject(result, "error", "IWDG cannot be disabled once started");
#endif

    } else {
        cJSON_AddStringToObject(result, "error", "action: status/enable/feed/disable");
    }
#else
    cJSON_AddStringToObject(result, "error", "watchdog not supported on this platform");
#endif
    return result;
}

/*----------------------------------------------------------------------
 * task_create — AI がタスクを動的生成
 *
 * 定義済みアクション (blink/adc_log/heartbeat) のみ実行可能。
 * 最大 4 個の動的タスク。
 */
LOCAL T_DYN_TASK dyn_tasks[MAX_DYN_TASKS];

LOCAL void dyn_task_entry(INT stacd, void *exinf)
{
    T_DYN_TASK *dt = (T_DYN_TASK *)exinf;

    while (dt->running) {
        switch (dt->action) {
        case 0:  /* blink */
            mcp_gpio_set_output(dt->pin);
            mcp_gpio_write_high(dt->pin);
            tk_dly_tsk(dt->interval / 2);
            mcp_gpio_write_low(dt->pin);
            tk_dly_tsk(dt->interval / 2);
            break;

        case 1: { /* adc_log */
#if MCP_HAS_ADC
            (void)mcp_adc_read_raw(MCP_ADC_TEMP_CH);
#endif
            tk_dly_tsk(dt->interval);
            break;
        }

        case 2:  /* heartbeat */
            mcp_gpio_set_output(dt->pin);
            mcp_gpio_write_high(dt->pin);
            tk_dly_tsk(100);
            mcp_gpio_write_low(dt->pin);
            tk_dly_tsk(dt->interval - 100);
            break;
        }
    }
    tk_ext_tsk();
}

cJSON *tool_task_create(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *action_item = cJSON_GetObjectItem(args, "action");
    if (action_item == NULL || !cJSON_IsString(action_item)) {
        cJSON_AddStringToObject(result, "error", "missing action");
        return result;
    }

    /* 空きスロット検索 */
    INT slot = -1;
    INT i;
    for (i = 0; i < MAX_DYN_TASKS; i++) {
        if (!dyn_tasks[i].running) { slot = i; break; }
    }
    if (slot < 0) {
        cJSON_AddStringToObject(result, "error", "max dynamic tasks reached (4)");
        return result;
    }

    const char *action = action_item->valuestring;
    cJSON *pin_item = cJSON_GetObjectItem(args, "pin");
    cJSON *intv_item = cJSON_GetObjectItem(args, "interval_ms");

    T_DYN_TASK *dt = &dyn_tasks[slot];
    dt->pin = (pin_item && cJSON_IsNumber(pin_item)) ? pin_item->valueint : 25;
    dt->interval = (intv_item && cJSON_IsNumber(intv_item)) ? (UW)intv_item->valueint : 1000;
    dt->running = TRUE;

    if (strcmp(action, "blink") == 0) {
        dt->action = 0;
    } else if (strcmp(action, "adc_log") == 0) {
        dt->action = 1;
    } else if (strcmp(action, "heartbeat") == 0) {
        dt->action = 2;
    } else {
        cJSON_AddStringToObject(result, "error", "action: blink/adc_log/heartbeat");
        dt->running = FALSE;
        return result;
    }

    T_CTSK ctsk;
    ctsk.exinf  = (void *)dt;
    ctsk.tskatr = TA_HLNG | TA_RNG3;
    ctsk.task   = (FP)dyn_task_entry;
    ctsk.itskpri = 14;
    ctsk.stksz  = 2048;
    ctsk.bufptr = NULL;
    dt->tskid = tk_cre_tsk(&ctsk);
    if (dt->tskid < 0) {
        dt->running = FALSE;
        cJSON_AddStringToObject(result, "error", "task create failed");
        return result;
    }
    tk_sta_tsk(dt->tskid, 0);

    cJSON_AddStringToObject(result, "status", "created");
    cJSON_AddNumberToObject(result, "slot", slot);
    cJSON_AddNumberToObject(result, "task_id", dt->tskid);
    cJSON_AddStringToObject(result, "action", action);

    return result;
}

/*----------------------------------------------------------------------
 * task_stop — タスク停止 (動的タスク slot or 任意タスク ID)
 */
cJSON *tool_task_stop(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *slot_item = cJSON_GetObjectItem(args, "slot");
    cJSON *id_item   = cJSON_GetObjectItem(args, "task_id");

    if (slot_item != NULL && cJSON_IsNumber(slot_item)) {
        /* 動的タスクスロット停止 */
        INT slot = slot_item->valueint;
        if (slot < 0 || slot >= MAX_DYN_TASKS) {
            cJSON_AddStringToObject(result, "error", "slot 0-3");
            return result;
        }
        if (!dyn_tasks[slot].running) {
            cJSON_AddStringToObject(result, "error", "slot not running");
            return result;
        }
        /* 強制終了: cooperative exit + 2 秒 polling は race と
         * MCP タスクスタック高水位上昇 (Hard fault 観測) を招くため、
         * tk_ter_tsk で即座に止める (id-path と同じパターン)。 */
        dyn_tasks[slot].running = FALSE;
        ID tskid = dyn_tasks[slot].tskid;
        T_RTSK rtsk;
        if (tk_ref_tsk(tskid, &rtsk) == E_OK && !(rtsk.tskstat & TTS_DMT)) {
            tk_ter_tsk(tskid);
        }
        tk_del_tsk(tskid);
        dyn_tasks[slot].tskid = 0;

        /* cJSON_AddNumberToObject は内部で double sprintf("%g") を呼び
         * 2-4KB のスタックを消費する。
         * sprintf("%d") も newlib では float-aware printf を引き込みうるので、
         * slot は 0..3 の小整数のため手書き変換で文字列化する。 */
        char slot_str[2] = { (char)('0' + (slot & 0x7)), '\0' };
        cJSON_AddStringToObject(result, "status", "stopped");
        cJSON_AddStringToObject(result, "slot", slot_str);

    } else if (id_item != NULL && cJSON_IsNumber(id_item)) {
        /* 任意タスク ID を強制終了 */
        ID tskid = (ID)id_item->valueint;

        /* 安全チェック: システムタスク (T1-T8) は止めない
         * T1=初期, T2=WIZnet, T3=SPI, T4=DHCP, T5=netmon,
         * T6=httpd, T7=MCP, T8=MQTT */
        if (tskid <= 8) {
            cJSON_AddStringToObject(result, "error",
                "cannot stop system task (id 1-8). Use slot for dynamic tasks");
            return result;
        }

        T_RTSK rtsk;
        ER err = tk_ref_tsk(tskid, &rtsk);
        if (err != E_OK) {
            cJSON_AddStringToObject(result, "error", "task not found");
            return result;
        }

        if (!(rtsk.tskstat & TTS_DMT)) {
            tk_ter_tsk(tskid);
        }
        tk_del_tsk(tskid);

        cJSON_AddStringToObject(result, "status", "terminated");
        cJSON_AddNumberToObject(result, "task_id", tskid);

    } else {
        cJSON_AddStringToObject(result, "error", "specify slot or task_id");
    }

    return result;
}

/*----------------------------------------------------------------------
 * shell — 簡易シェルコマンド
 *
 * 使えるコマンド:
 *   mem          — メモリ使用量
 *   reg <addr>   — レジスタ読み取り (hex)
 *   tasks        — タスク一覧 (テキスト)
 *   net          — ネットワーク情報
 *   time         — 現在時刻
 *   echo <text>  — テキスト返送
 *   help         — コマンド一覧
 */
cJSON *tool_shell(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *cmd_item = cJSON_GetObjectItem(args, "cmd");
    if (cmd_item == NULL || !cJSON_IsString(cmd_item)) {
        cJSON_AddStringToObject(result, "error", "missing cmd");
        return result;
    }

    const char *cmd = cmd_item->valuestring;
    char out[256];
    INT pos = 0;

    if (strcmp(cmd, "mem") == 0) {
        /* ヒープ情報: Kmalloc で簡易チェック */
        void *p = Kmalloc(1024);
        if (p) {
            Kfree(p);
            /* 正確なヒープサイズは取得困難なので概算 */
            pos += 14; memcpy(out, "heap: avail ok", 14);
        } else {
            pos += 16; memcpy(out, "heap: low memory", 16);
        }

    } else if (strncmp(cmd, "reg ", 4) == 0) {
#if MCP_HAS_PEEK
        /* reg 0xADDRESS */
        const char *addr_str = cmd + 4;
        UW addr = 0;
        if (addr_str[0] == '0' && addr_str[1] == 'x') addr_str += 2;
        while (*addr_str) {
            addr <<= 4;
            addr |= hex_val(*addr_str);
            addr_str++;
        }
        if (addr & 3) {
            pos = 18; memcpy(out, "error: unaligned", 16);
        } else if (mcp_addr_is_protected(addr)) {
            memcpy(out, "error: protected", 16); pos = 16;
        } else {
            UW val = in_w(addr);
            char hbuf[12];
            fmt_hex32(hbuf, addr);
            memcpy(out + pos, hbuf, 10); pos += 10;
            out[pos++] = '=';
            fmt_hex32(hbuf, val);
            memcpy(out + pos, hbuf, 10); pos += 10;
        }
#else
        const char *ns = "not supported";
        memcpy(out, ns, 13); pos = 13;
#endif

    } else if (strcmp(cmd, "tasks") == 0) {
        ID id;
        for (id = 1; id <= 32 && pos < 230; id++) {
            T_RTSK rtsk;
            if (tk_ref_tsk(id, &rtsk) != E_OK) continue;
            if (rtsk.tskstat == 0) continue;
            /* "T%d p%d %s\n" */
            out[pos++] = 'T';
            if (id >= 10) out[pos++] = '0' + id / 10;
            out[pos++] = '0' + id % 10;
            out[pos++] = ' '; out[pos++] = 'p';
            if (rtsk.tskpri >= 10) out[pos++] = '0' + rtsk.tskpri / 10;
            out[pos++] = '0' + rtsk.tskpri % 10;
            out[pos++] = ' ';
            const char *st = (rtsk.tskstat & 0x01) ? "RUN" :
                             (rtsk.tskstat & 0x02) ? "RDY" :
                             (rtsk.tskstat & 0x04) ? "WAI" :
                             (rtsk.tskstat & 0x20) ? "DMT" : "?";
            while (*st) out[pos++] = *st++;
            out[pos++] = '\n';
        }

    } else if (strcmp(cmd, "net") == 0) {
        UB ip[4]; getSIPR(ip);
        const char *pre = "IP=";
        memcpy(out + pos, pre, 3); pos += 3;
        INT i;
        for (i = 0; i < 4; i++) {
            if (i > 0) out[pos++] = '.';
            UB v = ip[i];
            if (v >= 100) out[pos++] = '0' + v / 100;
            if (v >= 10)  out[pos++] = '0' + (v / 10) % 10;
            out[pos++] = '0' + v % 10;
        }

    } else if (strcmp(cmd, "time") == 0) {
        DATE_TIM dt;
        if (dt_gettime(&dt) == E_OK) {
            char tbuf[20];
            dt_format(&dt, tbuf, sizeof(tbuf));
            memcpy(out, tbuf, 19); pos = 19;
        } else {
            pos = 10; memcpy(out, "not synced", 10);
        }

    } else if (strncmp(cmd, "echo ", 5) == 0) {
        const char *text = cmd + 5;
        UH len = (UH)strlen(text);
        if (len > 250) len = 250;
        memcpy(out, text, len); pos = len;

    } else if (strcmp(cmd, "help") == 0) {
        const char *h = "mem|reg <hex>|tasks|net|time|echo <text>|help";
        memcpy(out, h, strlen(h)); pos = strlen(h);

    } else {
        const char *e = "unknown command. try: help";
        memcpy(out, e, strlen(e)); pos = strlen(e);
    }

    out[pos] = '\0';
    cJSON_AddStringToObject(result, "output", out);

    return result;
}

/*----------------------------------------------------------------------
 * net_scan — TCP ポートスキャン
 */
cJSON *tool_net_scan(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *host_item  = cJSON_GetObjectItem(args, "host");
    cJSON *ports_item = cJSON_GetObjectItem(args, "ports");
    cJSON *tmout_item = cJSON_GetObjectItem(args, "timeout");

    if (host_item == NULL || !cJSON_IsString(host_item) ||
        ports_item == NULL || !cJSON_IsString(ports_item)) {
        cJSON_AddStringToObject(result, "error", "missing host/ports");
        return result;
    }

    UB ip[4];
    if (!parse_ip(host_item->valuestring, ip)) {
        cJSON_AddStringToObject(result, "error", "invalid IP");
        return result;
    }

    TMO tmout = (tmout_item && cJSON_IsNumber(tmout_item)) ?
                (TMO)tmout_item->valueint : 1000;

    /* ポートリストをパース (カンマ区切り or "start-end") */
    UH ports[32];
    INT num_ports = 0;
    const char *p = ports_item->valuestring;

    /* "start-end" 形式チェック */
    const char *dash = strchr(p, '-');
    if (dash != NULL) {
        INT start = 0, end = 0;
        while (*p >= '0' && *p <= '9') { start = start * 10 + (*p - '0'); p++; }
        p = dash + 1;
        while (*p >= '0' && *p <= '9') { end = end * 10 + (*p - '0'); p++; }
        INT i;
        for (i = start; i <= end && num_ports < 32; i++) {
            ports[num_ports++] = (UH)i;
        }
    } else {
        /* カンマ区切り */
        while (*p && num_ports < 32) {
            INT val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
            ports[num_ports++] = (UH)val;
            if (*p == ',') p++;
        }
    }

    /* スキャン */
    cJSON *open_arr = cJSON_CreateArray();
    INT i;
    for (i = 0; i < num_ports; i++) {
        ER err = tk_sock_open(TCP_TOOL_SN, Sn_MR_TCP, 0);
        if (err != E_OK) continue;

        err = tk_sock_connect(TCP_TOOL_SN, ip, ports[i], tmout);
        if (err == E_OK) {
            cJSON_AddItemToArray(open_arr, cJSON_CreateNumber(ports[i]));
            tk_sock_disconnect(TCP_TOOL_SN, 1000);
        }
        tk_sock_close(TCP_TOOL_SN);
    }

    cJSON_AddStringToObject(result, "host", host_item->valuestring);
    cJSON_AddNumberToObject(result, "scanned", num_ports);
    cJSON_AddItemToObject(result, "open", open_arr);

    return result;
}

/*----------------------------------------------------------------------
 * relay — 別デバイスの MCP に JSON-RPC を転送 (AI ↔ AI)
 */
cJSON *tool_relay(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *devid_item  = cJSON_GetObjectItem(args, "device_id");
    cJSON *method_item = cJSON_GetObjectItem(args, "method");
    cJSON *params_item = cJSON_GetObjectItem(args, "params");

    if (devid_item == NULL || !cJSON_IsString(devid_item) ||
        method_item == NULL || !cJSON_IsString(method_item)) {
        cJSON_AddStringToObject(result, "error", "missing device_id/method");
        return result;
    }

    /* ターゲットトピック: mcp/{device_id}/request */
    char topic[MCP_TOPIC_MAXLEN];
    safe_topic_build(topic, sizeof(topic), "mcp/",
                     devid_item->valuestring, "/request");

    /* JSON-RPC メッセージ組み立て */
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "jsonrpc", "2.0");
    cJSON_AddStringToObject(msg, "method", method_item->valuestring);
    if (params_item != NULL && cJSON_IsString(params_item)) {
        cJSON_AddRawToObject(msg, "params", params_item->valuestring);
    }
    cJSON_AddNumberToObject(msg, "id", 99);

    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);

    if (json_str != NULL) {
        ER err = tk_mqtt_publish(topic, (const UB *)json_str, strlen(json_str));
        cJSON_free(json_str);
        if (err == E_OK) {
            cJSON_AddStringToObject(result, "status", "sent");
            cJSON_AddStringToObject(result, "target", topic);
        } else {
            cJSON_AddStringToObject(result, "error", "publish failed");
        }
    } else {
        cJSON_AddStringToObject(result, "error", "json format failed");
    }

    return result;
}

/*----------------------------------------------------------------------
 * mcp_auth — API キー管理
 */
cJSON *tool_mcp_auth(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *action_item = cJSON_GetObjectItem(args, "action");
    if (action_item == NULL || !cJSON_IsString(action_item)) {
        cJSON_AddStringToObject(result, "error", "missing action");
        return result;
    }

    const char *action = action_item->valuestring;

    if (strcmp(action, "generate") == 0) {
        /* キー既存時は上書き禁止 (revoke してから再生成) */
        if (mcp_api_key_active) {
            cJSON_AddStringToObject(result, "error",
                "API key already active. Call revoke first to regenerate.");
            return result;
        }
        /* ランダムキー生成 */
        INT i;
#ifdef CPU_RP2040
        INT bit;
        /* RP2040 ROSC RANDOMBIT (0x4006001C bit 0) */
        for (i = 0; i < APIKEY_LEN; i++) {
            UB byte = 0;
            for (bit = 0; bit < 8; bit++) {
                byte = (byte << 1) | (in_w(0x4006001C) & 1);
                volatile INT d; for (d = 0; d < 10; d++) {}
            }
            mcp_api_key[i] = byte;
        }
#else
        /* フォールバック: uptime ベースの簡易乱数 */
        {
            SYSTIM otm;
            tk_get_otm(&otm);
            UW seed = otm.lo;
            for (i = 0; i < APIKEY_LEN; i++) {
                seed = seed * 1103515245 + 12345;
                mcp_api_key[i] = (UB)(seed >> 16);
            }
        }
#endif
        mcp_api_key_active = TRUE;

#if MCP_HAS_FLASH
        /* Flash に保存 (vm_store セクタの 0x100 に書き込み) */
        UB *sector = (UB *)Kmalloc(FLASH_SECTOR_SIZE);
        if (sector == NULL) {
            mcp_api_key_active = FALSE;
            cJSON_AddStringToObject(result, "error", "out of memory");
            return result;
        }
        memcpy(sector, (const void *)FLASH_STORAGE_BASE, FLASH_SECTOR_SIZE);
        sector[APIKEY_FLASH_OFFSET]     = 'A';
        sector[APIKEY_FLASH_OFFSET + 1] = 'K';
        sector[APIKEY_FLASH_OFFSET + 2] = '0';
        sector[APIKEY_FLASH_OFFSET + 3] = '1';
        memcpy(&sector[APIKEY_FLASH_OFFSET + 4], mcp_api_key, APIKEY_LEN);

        { UINT imask; Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(FLASH_STORAGE_OFFSET, sector, FLASH_SECTOR_SIZE);
        Asm("msr primask, %0" :: "r"(imask)); }
        Kfree(sector);
#endif

        /* API キーを hex で返す (これが唯一の取得チャンス) */
        char hex[APIKEY_LEN * 2 + 1];
        sha256_to_hex(mcp_api_key, hex);
        cJSON_AddStringToObject(result, "status", "generated");
        cJSON_AddStringToObject(result, "api_key", hex);
        cJSON_AddStringToObject(result, "note",
            "Save this key! It will not be shown again. "
            "Include as 'api_key' field in requests for dangerous tools.");

    } else if (strcmp(action, "revoke") == 0) {
#if MCP_HAS_FLASH
        UB *sector = (UB *)Kmalloc(FLASH_SECTOR_SIZE);
        if (sector == NULL) {
            cJSON_AddStringToObject(result, "error", "out of memory");
            return result;
        }
        memcpy(sector, (const void *)FLASH_STORAGE_BASE, FLASH_SECTOR_SIZE);
        memset(&sector[APIKEY_FLASH_OFFSET], 0xFF, 4 + APIKEY_LEN);

        { UINT imask; Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(FLASH_STORAGE_OFFSET, sector, FLASH_SECTOR_SIZE);
        Asm("msr primask, %0" :: "r"(imask)); }
        Kfree(sector);
#endif
        memset(mcp_api_key, 0, APIKEY_LEN);
        mcp_api_key_active = FALSE;
        cJSON_AddStringToObject(result, "status", "revoked");

    } else if (strcmp(action, "status") == 0) {
        cJSON_AddBoolToObject(result, "api_key_active", mcp_api_key_active);
        cJSON_AddNumberToObject(result, "rate_limit", mcp_conf.rate_limit);
    } else {
        cJSON_AddStringToObject(result, "error", "action: generate/revoke/status");
    }

    return result;
}

/*----------------------------------------------------------------------
 * wifi_set_credentials / wifi_get_credentials — WiFi SSID/PSK の Flash 保存
 *
 * WIFI_CYW43 未定義ビルド (W5100S) ではスタブ。
 */
#if defined(WIFI_CYW43) && defined(CPU_RP2040)
#include "../wifi/tk_wifi_creds.h"

LOCAL UW auth_str_to_enum(const char *s)
{
    if (s == NULL) return TK_WIFI_AUTH_WPA2_PSK;  /* デフォルト */
    if (strcmp(s, "open") == 0) return TK_WIFI_AUTH_OPEN;
    if (strcmp(s, "wpa3") == 0) return TK_WIFI_AUTH_WPA3_PSK;
    /* wpa2 含む既定 */
    return TK_WIFI_AUTH_WPA2_PSK;
}

LOCAL const char *auth_enum_to_str(UW a)
{
    if (a == TK_WIFI_AUTH_OPEN)     return "open";
    if (a == TK_WIFI_AUTH_WPA3_PSK) return "wpa3";
    return "wpa2";
}

cJSON *tool_wifi_set_credentials(cJSON *args)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *ssid_item = cJSON_GetObjectItem(args, "ssid");
    cJSON *psk_item  = cJSON_GetObjectItem(args, "psk");
    cJSON *auth_item = cJSON_GetObjectItem(args, "auth");

    if (!cJSON_IsString(ssid_item) || ssid_item->valuestring == NULL) {
        cJSON_AddStringToObject(result, "error", "ssid required (string)");
        return result;
    }
    const char *ssid = ssid_item->valuestring;
    const char *psk  = (cJSON_IsString(psk_item)) ? psk_item->valuestring : NULL;
    UW auth = auth_str_to_enum(cJSON_IsString(auth_item) ? auth_item->valuestring : NULL);

    ER err = tk_wifi_creds_save(ssid, psk, auth);
    if (err == E_PAR) {
        cJSON_AddStringToObject(result, "error", "invalid ssid (1..32) or psk (0 or 8..63)");
        return result;
    }
    if (err != E_OK) {
        cJSON_AddStringToObject(result, "error", "flash write failed");
        cJSON_AddNumberToObject(result, "err_code", err);
        return result;
    }

    cJSON_AddStringToObject(result, "status", "saved");
    cJSON_AddStringToObject(result, "ssid", ssid);
    cJSON_AddStringToObject(result, "auth", auth_enum_to_str(auth));
    cJSON_AddBoolToObject(result, "reboot_required", 1);
    return result;
}

cJSON *tool_wifi_get_credentials(cJSON *args)
{
    (void)args;
    cJSON *result = cJSON_CreateObject();
    T_WIFI_CREDS creds;
    ER err = tk_wifi_creds_load(&creds);
    if (err == E_OK) {
        cJSON_AddStringToObject(result, "source", "flash");
        cJSON_AddStringToObject(result, "ssid", creds.ssid);
        cJSON_AddStringToObject(result, "auth", auth_enum_to_str(creds.auth));
        cJSON_AddNumberToObject(result, "psk_len", tk_wifi_creds_psk_len());
    } else {
        cJSON_AddStringToObject(result, "source", "fallback");
    }
    return result;
}
#else
cJSON *tool_wifi_set_credentials(cJSON *args)
{
    (void)args;
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "error", "wifi not supported on this platform");
    return result;
}

cJSON *tool_wifi_get_credentials(cJSON *args)
{
    (void)args;
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "error", "wifi not supported on this platform");
    return result;
}
#endif

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
