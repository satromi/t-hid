/*
 *----------------------------------------------------------------------
 *    WiFi ネットワーク互換層
 *
 *    WizNet (W5100S/W5500) 固有のシンボルを WiFi (CYW43 + lwIP) 環境で
 *    提供するための互換ヘッダ。MQTT/MCP コードの共有に使用。
 *----------------------------------------------------------------------
 */

#ifndef __TK_NET_COMPAT_H__
#define __TK_NET_COMPAT_H__

#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "tk_wifi.h"

/* ソケットプロトコル定数 (WizNet Sn_MR_TCP/UDP 互換) */
#ifndef Sn_MR_TCP
#define Sn_MR_TCP   0x01    /* == TK_PROTO_TCP */
#endif
#ifndef Sn_MR_UDP
#define Sn_MR_UDP   0x02    /* == TK_PROTO_UDP */
#endif

/* デバッグ出力マクロ (WizNet W5DBG 互換) */
#ifndef W5DBG
#define W5DBG(fmt, ...)  tm_printf((UB *)(fmt), ##__VA_ARGS__)
#endif

/* MAC アドレス取得 (WizNet getSHAR 互換) */
void getSHAR(UB *mac);

/* IP アドレス取得 (WizNet getSIPR 互換) */
void getSIPR(UB *ip);

/* ゲートウェイ取得 (WizNet getGAR 互換) */
void getGAR(UB *gw);

/* サブネットマスク取得 (WizNet getSUBR 互換) */
void getSUBR(UB *sn);

#endif /* __TK_NET_COMPAT_H__ */
