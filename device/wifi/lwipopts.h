/*
 *----------------------------------------------------------------------
 *    lwIP Configuration for μT-Kernel 3.0 + Pico W (CYW43439)
 *
 *    NO_SYS=1 (ポーリングモード): BLE タスクのポーリングループで
 *    sys_check_timeouts() を呼び出す。RTOS 統合 (sys_arch) 不要。
 *----------------------------------------------------------------------
 */

#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/*======================================================================
 * OS / Threading
 *====================================================================*/
#define NO_SYS                  1       /* ポーリングモード (OS 統合なし) */
#define SYS_LIGHTWEIGHT_PROT    0       /* 単一 lwIP スレッドのため不要 */
#define LWIP_SOCKET             0       /* BSD socket API 不使用 */
#define LWIP_NETCONN            0       /* netconn API 不使用 */
#define LWIP_CALLBACK_API       1       /* raw callback API を使用 */

/*======================================================================
 * IPv4
 *====================================================================*/
#define LWIP_IPV4               1
#define LWIP_IPV6               0       /* RAM 節約: IPv6 不要 */
#define LWIP_DHCP               1       /* DHCP クライアント (lwIP 内蔵) */
#define LWIP_DNS                1       /* DNS リゾルバ (lwIP 内蔵) */
#define LWIP_ICMP               1       /* Ping 応答 */
#define LWIP_IGMP               0       /* マルチキャスト不要 */
#define LWIP_AUTOIP             0       /* AutoIP 不要 */
#define LWIP_ACD                1       /* ACD: DHCP が要求 (lwIP 2.2+) */

/*======================================================================
 * TCP
 *====================================================================*/
#define LWIP_TCP                1
#define TCP_MSS                 1460
/* MCP tools/list レスポンスは約 9KB になるため 17KB 確保。
 * partial send は mqtt_send がループでカバーするが、バッファ大きい方が
 * カーネル待ち回数が減って送信完了が速い。 */
#define TCP_SND_BUF             (12 * TCP_MSS)  /* 17520 bytes */
#define TCP_WND                 (8 * TCP_MSS)   /* 受信ウィンドウ 11680 bytes */
#define TCP_SND_QUEUELEN        (4 * TCP_SND_BUF / TCP_MSS)
#define LWIP_TCP_KEEPALIVE      1               /* TCP キープアライブ */

/*======================================================================
 * UDP
 *====================================================================*/
#define LWIP_UDP                1
#define LWIP_RAW                0       /* raw IP ソケット不要 */

/*======================================================================
 * メモリ設定 (RP2040: 264KB SRAM, WiFi に ~38KB 割当)
 *====================================================================*/
#define MEM_SIZE                (16 * 1024)      /* lwIP ヒープ: 16KB */
#define MEM_ALIGNMENT           4                /* ARM 4byte アライン */
#define MEMP_NUM_PBUF           16               /* PBUF_ROM/REF プール */
#define MEMP_NUM_TCP_PCB        4                /* 同時 TCP 接続数 */
#define MEMP_NUM_TCP_PCB_LISTEN 2                /* TCP リッスンソケット */
#define MEMP_NUM_TCP_SEG        64               /* TCP セグメント (TCP_SND_QUEUELEN=48 をカバー) */
#define MEMP_NUM_UDP_PCB        4                /* 同時 UDP ソケット */
#define MEMP_NUM_NETBUF         0                /* netconn 不使用 */
#define MEMP_NUM_NETCONN        0
#define MEMP_NUM_ARP_QUEUE      4
#define MEMP_NUM_SYS_TIMEOUT    16               /* タイマープール (デフォルト ~5 では TCP 追加で枯渇) */
#define PBUF_POOL_SIZE          12               /* パケットバッファプール */
#define PBUF_POOL_BUFSIZE       1536             /* MTU アライン */

/*======================================================================
 * タイマ
 *====================================================================*/
#define LWIP_TIMERS             1
#define LWIP_TIMERS_CUSTOM      0

/*======================================================================
 * ネットワークインターフェース
 *====================================================================*/
#define LWIP_NETIF_HOSTNAME     1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1

/*======================================================================
 * チェックサム
 *====================================================================*/
#define LWIP_CHKSUM_ALGORITHM   3               /* ソフトウェア計算 */

/*======================================================================
 * デバッグ (開発時のみ有効化)
 *====================================================================*/
#define LWIP_DEBUG              0
#define LWIP_STATS              0
#define LWIP_STATS_DISPLAY      0

/*======================================================================
 * CYW43 統合
 *====================================================================*/
#include <stdint.h>
extern uint32_t cyw43_hal_ticks_us(void);
#define LWIP_RAND()             ((u32_t)cyw43_hal_ticks_us())

/* lwIP の ethernet_input を使用 (cyw43_lwip.c が呼ぶ) */
#define LWIP_ARP                1
#define LWIP_ETHERNET           1

#endif /* __LWIPOPTS_H__ */
