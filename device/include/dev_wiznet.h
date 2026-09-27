/*
 *----------------------------------------------------------------------
 *    WIZnet Ethernet Device Driver for μT-Kernel 3.0 BSP
 *
 *    チップ共通公開 API ヘッダ (W5100S / W5500)
 *    デバイス名: "neta" (network adapter)
 *----------------------------------------------------------------------
 */

#ifndef __DEV_WIZNET_H__
#define __DEV_WIZNET_H__

#include <tk/typedef.h>

/* μT-Kernel デバイス名 (チップ共通) */
#define WIZNET_DEVNM	"neta"

/* ソケット数 (チップ依存) */
#if defined(WIZCHIP_W5500)
#define WIZNET_SOCK_NUM	8	/* W5500: 8 ソケット */
#else
#define WIZNET_SOCK_NUM	4	/* W5100S: 4 ソケット */
#endif

/*----------------------------------------------------------------------
 * ネットワークアダプタ属性 (tk_swri_dev / tk_srea_dev の start パラメータ)
 */
typedef enum {
    TDN_NET_IP       = -100,  /* IP アドレス (UB[4]) */
    TDN_NET_MASK     = -101,  /* サブネットマスク (UB[4]) */
    TDN_NET_GW       = -102,  /* デフォルトゲートウェイ (UB[4]) */
    TDN_NET_MAC      = -103,  /* MAC アドレス (UB[6]) */
    TDN_NET_STATUS   = -104,  /* リンク状態 (UW: 0=down, 1=up) */
} T_DN_NET_ATR;

/*----------------------------------------------------------------------
 * ソケット属性 (サブデバイス経由で操作)
 */
typedef enum {
    TDN_SOC_CONNECT  = -110,  /* TCP 接続 (T_NET_ADDR) */
    TDN_SOC_LISTEN   = -120,  /* TCP リッスン (UH port) */
    TDN_SOC_STATUS   = -130,  /* ソケット状態取得 (UB) */
    TDN_SOC_CLOSE    = -140,  /* ソケット切断 */
    TDN_SOC_UDP      = -150,  /* UDP ソケット開始 (UH port) */
} T_DN_SOC_ATR;

/*----------------------------------------------------------------------
 * 接続先アドレス構造体
 */
typedef struct {
    UB  ip[4];     /* 接続先 IP アドレス */
    UH  port;      /* 接続先ポート番号 */
} T_NET_ADDR;

/*----------------------------------------------------------------------
 * ソケット状態値 (W5100S/W5500 Sn_SR 共通)
 */
#define NET_SOCK_CLOSED      0x00
#define NET_SOCK_INIT        0x13
#define NET_SOCK_LISTEN      0x14
#define NET_SOCK_ESTABLISHED 0x17
#define NET_SOCK_CLOSE_WAIT  0x1C
#define NET_SOCK_UDP         0x22

/*----------------------------------------------------------------------
 * デバイス初期化 (knl_start_device / usermain から呼び出し)
 */
IMPORT ER dev_init_wiznet(UW unit);

/*----------------------------------------------------------------------
 * SPI HAL 統一 API (チップ依存の実装は sysdepend/ 配下)
 */
void wiznet_spi_init(void);
void wiznet_hw_reset(void);
void wiznet_int_init(void);

/* (旧名 W5100S_DEVNM, W5100S_SOCK_NUM, dev_init_w5100s は廃止) */

#endif /* __DEV_WIZNET_H__ */
