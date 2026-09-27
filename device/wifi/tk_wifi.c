/*
 *----------------------------------------------------------------------
 *    WiFi Connection Manager for μT-Kernel 3.0
 *    CYW43439 (Pico W) + lwIP
 *
 *    cyw43_wifi_set_up() → cyw43_wifi_join() → lwIP DHCP 自動取得
 *    ポーリングは BLE タスクの cyw43_arch_tkernel_poll() で駆動
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) && defined(WIFI_CYW43)

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "cyw43.h"
#include "cyw43_country.h"
#include "tk_wifi.h"

#if CYW43_LWIP
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#endif

/* cyw43_arch_tkernel.c で定義 */
extern cyw43_t cyw43_state;
extern void cyw43_tkernel_lock(void);
extern void cyw43_tkernel_unlock(void);

/* 内部状態 */
LOCAL T_WIFI_STATE wifi_state = TK_WIFI_ST_OFF;

/*----------------------------------------------------------------------
 * WiFi 初期化 — STA モード有効化
 */
EXPORT ER tk_wifi_init(void)
{
    if (wifi_state != TK_WIFI_ST_OFF) return E_OK;  /* 再初期化防止 */

    /*
     * CYW43 ドライバ関数は内部で CYW43_THREAD_ENTER/EXIT (= lock/unlock) を
     * 呼ぶため、外側でロックを取得してはならない (FastLock は非再入)。
     *
     * cyw43_wifi_set_up() 内部で:
     *   1. cyw43_ensure_up() (電源ON, FW ダウンロード — ioctl 使用)
     *   2. cyw43_wifi_on() (WiFi ラジオ有効化)
     *   3. cyw43_cb_tcpip_init() (lwIP netif 初期化)
     */
    cyw43_wifi_set_up(&cyw43_state, CYW43_ITF_STA, true,
                      CYW43_COUNTRY_WORLDWIDE);

    wifi_state = TK_WIFI_ST_DISCONNECTED;
    tm_printf((UB *)"WiFi: STA mode enabled\n");

    return E_OK;
}

/*----------------------------------------------------------------------
 * AP 接続 + DHCP IP 取得
 */
EXPORT ER tk_wifi_connect(const T_WIFI_CONF *conf, TMO tmout)
{
    if (conf == NULL || conf->ssid == NULL) return E_PAR;
    if (wifi_state == TK_WIFI_ST_OFF) return E_OBJ;

    /* 接続試行前に CYW43 内部の scan/join 状態を明示的にクリア。
     * tk_wifi_set_up 直後の初回でも CYW43 内部に scanning が残っていると
     * 新しい join が 60 秒以上 link=1 で詰まることが実機で確認されたため、
     * 初回も含めて常に leave してから join する (未関連付けの状態で leave
     * を呼んでも安全)。 */
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    tk_dly_tsk(100);   /* disassociate 反映待ち */

    wifi_state = TK_WIFI_ST_CONNECTING;
    tm_printf((UB *)"WiFi: connecting to '%s'...\n", conf->ssid);

    /* WiFi 接続 (非同期: 完了は cyw43_wifi_link_status で確認)
     * ロック不要: cyw43_wifi_join() 内部で CYW43_THREAD_ENTER/EXIT */
    SYSTIM t_join_start;
    tk_get_otm(&t_join_start);
    int ret = cyw43_wifi_join(&cyw43_state,
                              strlen(conf->ssid), (const uint8_t *)conf->ssid,
                              conf->key ? strlen(conf->key) : 0,
                              conf->key ? (const uint8_t *)conf->key : NULL,
                              conf->key ? conf->auth : CYW43_AUTH_OPEN,
                              NULL, 0);  /* BSSID=any, channel=any */
    SYSTIM t_join_done;
    tk_get_otm(&t_join_done);
    tm_printf((UB *)"WiFi: join call took %ums ret=%d\n",
              (unsigned)(t_join_done.lo - t_join_start.lo), ret);

    if (ret != 0) {
        tm_printf((UB *)"WiFi: join failed (%d)\n", ret);
        wifi_state = TK_WIFI_ST_ERROR;
        return E_IO;
    }

    /* WiFi 接続 + DHCP IP 取得を待機 */
    UW elapsed = 0;
    UW last_log = 0;
    int last_link = -99;

    while (1) {
        tk_dly_tsk(100);  /* 100ms ポーリング (cyw43_poll は BLE タスクが駆動) */
        elapsed += 100;

        /* リンク状態確認 */
        int link = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link != last_link || (elapsed - last_log) >= 5000) {
            tm_printf((UB *)"WiFi: t=%us link=%d\n",
                      (unsigned)(elapsed / 1000), link);
            last_link = link;
            last_log = elapsed;
        }

        if (link == CYW43_LINK_FAIL || link == CYW43_LINK_NONET ||
            link == CYW43_LINK_BADAUTH) {
            tm_printf((UB *)"WiFi: link failed (%d)\n", link);
            wifi_state = TK_WIFI_ST_ERROR;
            return E_IO;
        }

#if CYW43_LWIP
        /* CYW43_LINK_UP は lwIP DHCP による IP 取得も含む */
        int tcpip_link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

        if (tcpip_link == CYW43_LINK_UP) {
            wifi_state = TK_WIFI_ST_READY;
            struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
            tm_printf((UB *)"WiFi: connected  IP=%d.%d.%d.%d\n",
                ip4_addr1(netif_ip4_addr(n)),
                ip4_addr2(netif_ip4_addr(n)),
                ip4_addr3(netif_ip4_addr(n)),
                ip4_addr4(netif_ip4_addr(n)));
            return E_OK;
        }
#else
        if (link == CYW43_LINK_JOIN) {
            wifi_state = TK_WIFI_ST_CONNECTED;
            tm_printf((UB *)"WiFi: joined (no lwIP)\n");
            return E_OK;
        }
#endif

        /* タイムアウトチェック (単純加算 — ラップアラウンドなし) */
        if (tmout != TMO_FEVR && elapsed > (UW)tmout) {
            tm_printf((UB *)"WiFi: timeout\n");
            wifi_state = TK_WIFI_ST_ERROR;
            return E_TMOUT;
        }
    }
}

/*----------------------------------------------------------------------
 * 切断
 */
EXPORT ER tk_wifi_disconnect(void)
{
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);

    wifi_state = TK_WIFI_ST_DISCONNECTED;
    tm_printf((UB *)"WiFi: disconnected\n");
    return E_OK;
}

/*----------------------------------------------------------------------
 * 状態取得
 */
EXPORT T_WIFI_STATE tk_wifi_get_state(void)
{
    return wifi_state;
}

/*----------------------------------------------------------------------
 * 接続情報取得
 */
EXPORT ER tk_wifi_get_info(T_WIFI_INFO *info)
{
    if (info == NULL) return E_PAR;
    memset(info, 0, sizeof(*info));

    /* MAC アドレス */
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, info->mac);

    /* RSSI */
    int32_t rssi = 0;
    cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    info->rssi = (INT)rssi;

#if CYW43_LWIP
    /* IP/Mask/GW (lwIP netif から取得) */
    struct netif *n = &cyw43_state.netif[CYW43_ITF_STA];
    if (n != NULL) {
        const ip4_addr_t *ip = netif_ip4_addr(n);
        const ip4_addr_t *mask = netif_ip4_netmask(n);
        const ip4_addr_t *gw = netif_ip4_gw(n);
        info->ip[0] = ip4_addr1(ip);   info->ip[1] = ip4_addr2(ip);
        info->ip[2] = ip4_addr3(ip);   info->ip[3] = ip4_addr4(ip);
        info->mask[0] = ip4_addr1(mask); info->mask[1] = ip4_addr2(mask);
        info->mask[2] = ip4_addr3(mask); info->mask[3] = ip4_addr4(mask);
        info->gw[0] = ip4_addr1(gw);   info->gw[1] = ip4_addr2(gw);
        info->gw[2] = ip4_addr3(gw);   info->gw[3] = ip4_addr4(gw);
    }
#endif

    return E_OK;
}

/*----------------------------------------------------------------------
 * DNS 名前解決 (ブロッキング)
 *
 * lwIP の dns_gethostbyname() はコールバック API のため、イベントフラグで
 * 待機を実現。DHCP で取得した DNS サーバが lwIP 内部で自動設定される。
 * コールバックは cyw43_poll タスクコンテキスト (ロック保持中) で呼ばれる。
 */
#if CYW43_LWIP
typedef struct {
    ID    flgid;
    ip_addr_t result;
    BOOL  success;
} dns_wait_ctx_t;

LOCAL void dns_resolved_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    dns_wait_ctx_t *ctx = (dns_wait_ctx_t *)arg;
    if (ipaddr != NULL) {
        ctx->result  = *ipaddr;
        ctx->success = TRUE;
    } else {
        ctx->success = FALSE;
    }
    tk_set_flg(ctx->flgid, 0x01);
}

EXPORT ER tk_wifi_dns_resolve(const char *hostname, UB *result, TMO tmout)
{
    if (hostname == NULL || result == NULL) return E_PAR;

    dns_wait_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    T_CFLG cflg;
    cflg.exinf   = NULL;
    cflg.flgatr  = TA_TFIFO | TA_WMUL;
    cflg.iflgptn = 0;
    ctx.flgid = tk_cre_flg(&cflg);
    if (ctx.flgid < 0) return E_SYS;

    cyw43_tkernel_lock();
    err_t err = dns_gethostbyname(hostname, &ctx.result,
                                  dns_resolved_cb, &ctx);
    cyw43_tkernel_unlock();

    ER ret;
    if (err == ERR_OK) {
        /* キャッシュヒット: 即解決 (コールバックは呼ばれない) */
        ret = E_OK;
        ctx.success = TRUE;
    } else if (err == ERR_INPROGRESS) {
        /* 問合せ中: フラグ待機 */
        UINT flgptn;
        ret = tk_wai_flg(ctx.flgid, 0x01, TWF_ORW | TWF_BITCLR, &flgptn, tmout);
    } else {
        ret = E_IO;
    }
    tk_del_flg(ctx.flgid);

    if (ret != E_OK) return ret;
    if (!ctx.success) return E_IO;

    const ip4_addr_t *ip = ip_2_ip4(&ctx.result);
    result[0] = ip4_addr1(ip);
    result[1] = ip4_addr2(ip);
    result[2] = ip4_addr3(ip);
    result[3] = ip4_addr4(ip);
    return E_OK;
}
#else
EXPORT ER tk_wifi_dns_resolve(const char *hostname, UB *result, TMO tmout)
{
    (void)hostname; (void)result; (void)tmout;
    return E_NOSPT;
}
#endif

#endif /* CPU_RP2040 && WIFI_CYW43 */
