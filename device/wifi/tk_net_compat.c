/*
 *----------------------------------------------------------------------
 *    WiFi ネットワーク互換層 — WizNet API shim for CYW43 WiFi
 *
 *    WizNet チップ固有の getSHAR() / getSIPR() 等を
 *    CYW43 WiFi ドライバ経由で提供する。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) && defined(WIFI_CYW43)

#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>

#include "cyw43.h"
#include "tk_wifi.h"
#include "tk_net_compat.h"

extern cyw43_t cyw43_state;

/*
 * getSHAR — MAC アドレス取得 (WizNet 互換)
 */
void getSHAR(UB *mac)
{
    cyw43_wifi_get_mac(&cyw43_state, 0, mac);
}

/*
 * getSIPR — IP アドレス取得 (WizNet 互換)
 */
void getSIPR(UB *ip)
{
    T_WIFI_INFO info;
    if (tk_wifi_get_info(&info) == E_OK) {
        memcpy(ip, info.ip, 4);
    } else {
        memset(ip, 0, 4);
    }
}

/*
 * getGAR — ゲートウェイ取得 (WizNet 互換)
 */
void getGAR(UB *gw)
{
    T_WIFI_INFO info;
    if (tk_wifi_get_info(&info) == E_OK) {
        memcpy(gw, info.gw, 4);
    } else {
        memset(gw, 0, 4);
    }
}

/*
 * getSUBR — サブネットマスク取得 (WizNet 互換)
 */
void getSUBR(UB *sn)
{
    T_WIFI_INFO info;
    if (tk_wifi_get_info(&info) == E_OK) {
        memcpy(sn, info.mask, 4);
    } else {
        memset(sn, 0, 4);
    }
}

#endif /* CPU_RP2040 && WIFI_CYW43 */
