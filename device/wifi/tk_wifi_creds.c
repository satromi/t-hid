/*
 *----------------------------------------------------------------------
 *    WiFi Credentials Flash Storage (Pico W / CYW43 + lwIP)
 *
 *    MCP の apikey と同一セクタ (FLASH_STORAGE_BASE) の offset 0x200 以降
 *    に SSID/PSK/auth を保存する。セクタ書込は apikey / vm_store と同じ
 *    「セクタ読取 → 更新 → erase → program」パターン。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) && defined(WIFI_CYW43)

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "tk_wifi_creds.h"
#include "../wiznet/tk_mcp_int.h"   /* FLASH_STORAGE_*, flash_safe_erase_program */

/* Flash 上の packed レコード (192 bytes) */
typedef struct {
    UW  magic;              /* WIFI_CREDS_MAGIC */
    UB  version;            /* 1 */
    UB  reserved;
    UH  ssid_len;
    UH  psk_len;
    UH  _pad;               /* 4 バイト境界整列 */
    UW  auth;
    UB  ssid[WIFI_CREDS_SSID_MAX];   /* 32 B */
    UB  psk [WIFI_CREDS_PSK_MAX];    /* 64 B */
    UW  crc32;              /* 先頭〜psk 末尾までの CRC32 */
    UB  padding[76];        /* 合計 192 */
} T_WIFI_CREDS_REC;

_Static_assert(sizeof(T_WIFI_CREDS_REC) == WIFI_CREDS_RECORD_SIZE,
               "T_WIFI_CREDS_REC size must be WIFI_CREDS_RECORD_SIZE");

/* CRC32/IEEE-802.3 の bitwise 実装 (テーブル不要、呼出頻度低なので小サイズ優先) */
LOCAL UW crc32_calc(const UB *data, UW len)
{
    UW crc = 0xFFFFFFFFu;
    UW i;
    for (i = 0; i < len; i++) {
        crc ^= (UW)data[i];
        INT b;
        for (b = 0; b < 8; b++) {
            UW mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

/* PSK 長の妥当性 (0 = OPEN 時は空 OK, WPA/WPA2/WPA3 PSK は 8..63) */
LOCAL BOOL psk_len_valid(UW len)
{
    if (len == 0) return TRUE;
    if (len >= 8 && len <= 63) return TRUE;
    return FALSE;
}

EXPORT ER tk_wifi_creds_load(T_WIFI_CREDS *out)
{
    if (out == NULL) return E_PAR;
    const T_WIFI_CREDS_REC *rec =
        (const T_WIFI_CREDS_REC *)(FLASH_STORAGE_BASE + WIFI_CREDS_FLASH_OFFSET);

    if (rec->magic != WIFI_CREDS_MAGIC) return E_NOEXS;
    if (rec->version != 1) return E_NOEXS;
    if (rec->ssid_len == 0 || rec->ssid_len > WIFI_CREDS_SSID_MAX) return E_NOEXS;
    if (rec->psk_len > WIFI_CREDS_PSK_MAX) return E_NOEXS;

    /* CRC 検証: magic 〜 psk 末尾まで (crc32 フィールドを除く) */
    UW crc_len = (UW)offsetof(T_WIFI_CREDS_REC, crc32);
    UW calc = crc32_calc((const UB *)rec, crc_len);
    if (calc != rec->crc32) return E_NOEXS;

    memset(out, 0, sizeof(*out));
    memcpy(out->ssid, rec->ssid, rec->ssid_len);
    out->ssid[rec->ssid_len] = '\0';
    if (rec->psk_len > 0) {
        memcpy(out->psk, rec->psk, rec->psk_len);
    }
    out->psk[rec->psk_len] = '\0';
    out->auth = rec->auth;
    out->from_flash = TRUE;
    return E_OK;
}

EXPORT UH tk_wifi_creds_psk_len(void)
{
    const T_WIFI_CREDS_REC *rec =
        (const T_WIFI_CREDS_REC *)(FLASH_STORAGE_BASE + WIFI_CREDS_FLASH_OFFSET);
    if (rec->magic != WIFI_CREDS_MAGIC) return 0;
    if (rec->version != 1) return 0;
    return rec->psk_len;
}

EXPORT ER tk_wifi_creds_save(const char *ssid, const char *psk, UW auth)
{
    if (ssid == NULL) return E_PAR;
    UW slen = (UW)strlen(ssid);
    UW plen = (psk != NULL) ? (UW)strlen(psk) : 0;
    if (slen == 0 || slen > WIFI_CREDS_SSID_MAX) return E_PAR;
    if (plen > WIFI_CREDS_PSK_MAX) return E_PAR;
    if (!psk_len_valid(plen)) return E_PAR;

    /* セクタバッファ確保と現セクタコピー */
    UB *sector = (UB *)Kmalloc(FLASH_SECTOR_SIZE);
    if (sector == NULL) return E_NOMEM;
    memcpy(sector, (const void *)FLASH_STORAGE_BASE, FLASH_SECTOR_SIZE);

    /* WiFi creds 領域 (192 bytes) を 0xFF で消して再構築 */
    T_WIFI_CREDS_REC *rec =
        (T_WIFI_CREDS_REC *)(sector + WIFI_CREDS_FLASH_OFFSET);
    memset(rec, 0xFF, sizeof(*rec));

    rec->magic    = WIFI_CREDS_MAGIC;
    rec->version  = 1;
    rec->reserved = 0;
    rec->ssid_len = (UH)slen;
    rec->psk_len  = (UH)plen;
    rec->_pad     = 0;
    rec->auth     = auth;
    memset(rec->ssid, 0, sizeof(rec->ssid));
    memcpy(rec->ssid, ssid, slen);
    memset(rec->psk,  0, sizeof(rec->psk));
    if (plen > 0) memcpy(rec->psk, psk, plen);
    memset(rec->padding, 0xFF, sizeof(rec->padding));

    UW crc_len = (UW)offsetof(T_WIFI_CREDS_REC, crc32);
    rec->crc32 = crc32_calc((const UB *)rec, crc_len);

    /* Flash 再書込 (割り込み無効で XIP 切替の安全性を確保) */
    {
        UINT imask;
        Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(FLASH_STORAGE_OFFSET, sector, FLASH_SECTOR_SIZE);
        Asm("msr primask, %0" :: "r"(imask));
    }
    Kfree(sector);

    /* 書込確認 */
    const T_WIFI_CREDS_REC *v =
        (const T_WIFI_CREDS_REC *)(FLASH_STORAGE_BASE + WIFI_CREDS_FLASH_OFFSET);
    if (v->magic != WIFI_CREDS_MAGIC) return E_IO;
    return E_OK;
}

EXPORT ER tk_wifi_creds_clear(void)
{
    UB *sector = (UB *)Kmalloc(FLASH_SECTOR_SIZE);
    if (sector == NULL) return E_NOMEM;
    memcpy(sector, (const void *)FLASH_STORAGE_BASE, FLASH_SECTOR_SIZE);
    memset(sector + WIFI_CREDS_FLASH_OFFSET, 0xFF, WIFI_CREDS_RECORD_SIZE);

    {
        UINT imask;
        Asm("mrs %0, primask; cpsid i" : "=r"(imask));
        flash_safe_erase_program(FLASH_STORAGE_OFFSET, sector, FLASH_SECTOR_SIZE);
        Asm("msr primask, %0" :: "r"(imask));
    }
    Kfree(sector);
    return E_OK;
}

EXPORT void tk_wifi_creds_get(T_WIFI_CREDS *out,
                              const char *fallback_ssid,
                              const char *fallback_psk,
                              UW fallback_auth)
{
    if (out == NULL) return;
    if (tk_wifi_creds_load(out) == E_OK) return;

    /* Fallback 値を詰める (ハードコード or ビルド埋込) */
    memset(out, 0, sizeof(*out));
    if (fallback_ssid != NULL) {
        size_t n = strlen(fallback_ssid);
        if (n > WIFI_CREDS_SSID_MAX) n = WIFI_CREDS_SSID_MAX;
        memcpy(out->ssid, fallback_ssid, n);
        out->ssid[n] = '\0';
    }
    if (fallback_psk != NULL) {
        size_t n = strlen(fallback_psk);
        if (n > WIFI_CREDS_PSK_MAX) n = WIFI_CREDS_PSK_MAX;
        memcpy(out->psk, fallback_psk, n);
        out->psk[n] = '\0';
    }
    out->auth = fallback_auth;
    out->from_flash = FALSE;
}

#else  /* !CPU_RP2040 || !WIFI_CYW43 */

#include <stddef.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include "tk_wifi_creds.h"

EXPORT ER tk_wifi_creds_load(T_WIFI_CREDS *out) { (void)out; return E_NOSPT; }
EXPORT ER tk_wifi_creds_save(const char *ssid, const char *psk, UW auth) {
    (void)ssid; (void)psk; (void)auth; return E_NOSPT;
}
EXPORT ER tk_wifi_creds_clear(void) { return E_NOSPT; }
EXPORT UH tk_wifi_creds_psk_len(void) { return 0; }
EXPORT void tk_wifi_creds_get(T_WIFI_CREDS *out,
                              const char *fallback_ssid,
                              const char *fallback_psk,
                              UW fallback_auth)
{
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (fallback_ssid != NULL) {
        size_t n = strlen(fallback_ssid);
        if (n > WIFI_CREDS_SSID_MAX) n = WIFI_CREDS_SSID_MAX;
        memcpy(out->ssid, fallback_ssid, n);
        out->ssid[n] = '\0';
    }
    if (fallback_psk != NULL) {
        size_t n = strlen(fallback_psk);
        if (n > WIFI_CREDS_PSK_MAX) n = WIFI_CREDS_PSK_MAX;
        memcpy(out->psk, fallback_psk, n);
        out->psk[n] = '\0';
    }
    out->auth = fallback_auth;
    out->from_flash = FALSE;
}

#endif /* CPU_RP2040 && WIFI_CYW43 */
