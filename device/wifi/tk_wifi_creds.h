/*
 *----------------------------------------------------------------------
 *    WiFi Credentials Flash Storage for μT-Kernel 3.0
 *    CYW43439 (Pico W) + lwIP
 *
 *    SSID / PSK / auth を Flash に永続化する。MCP の apikey と同じ
 *    末尾セクタ (FLASH_STORAGE_BASE) の offset 0x200 以降に配置。
 *----------------------------------------------------------------------
 */

#ifndef __TK_WIFI_CREDS_H__
#define __TK_WIFI_CREDS_H__

#include <tk/tkernel.h>
#include "tk_wifi.h"

/* 公開 API 用の受け皿 (NUL 終端 char 配列) */
typedef struct {
    char ssid[33];   /* 最大 32 文字 + NUL */
    char psk [65];   /* 最大 64 文字 + NUL */
    UW   auth;       /* TK_WIFI_AUTH_* */
    BOOL from_flash; /* TRUE = Flash 由来, FALSE = fallback */
} T_WIFI_CREDS;

/*----------------------------------------------------------------------
 * Flash から認証情報を読み込み out に格納する。
 *   Flash 未書込 / magic 不一致 / CRC 不整合 → E_NOEXS (out は未変更)
 *   正常 → E_OK, out->from_flash=TRUE
 */
ER tk_wifi_creds_load(T_WIFI_CREDS *out);

/*----------------------------------------------------------------------
 * Flash に認証情報を書き込む。
 *   ssid: 1..32 文字, NUL 終端必須
 *   psk:  NULL または空文字 (open 用)、または 8..63 文字 (WPA2/3 PSK)
 *   auth: TK_WIFI_AUTH_*
 *
 *   戻り値: E_OK=成功, E_PAR=引数不正, E_IO=Flash 書込エラー
 */
ER tk_wifi_creds_save(const char *ssid, const char *psk, UW auth);

/*----------------------------------------------------------------------
 * Flash 領域をクリア (fallback に戻す)。
 *   戻り値: E_OK=成功, E_IO=Flash 書込エラー
 */
ER tk_wifi_creds_clear(void);

/*----------------------------------------------------------------------
 * Flash または fallback から取得 (必ず成功)。
 *   Flash にあれば from_flash=TRUE、なければ fallback_* を埋める。
 */
void tk_wifi_creds_get(T_WIFI_CREDS *out,
                       const char *fallback_ssid,
                       const char *fallback_psk,
                       UW fallback_auth);

/*----------------------------------------------------------------------
 * Flash 上の PSK 長を返す (PSK 自体は返さない、存在確認用)。
 *   Flash 未書込なら 0。
 */
UH tk_wifi_creds_psk_len(void);

#endif /* __TK_WIFI_CREDS_H__ */
