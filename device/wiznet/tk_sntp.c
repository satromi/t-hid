/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native SNTP Client (RFC 4330)
 *
 *    NTP パケット (48 byte) を UDP で 1 往復して時刻同期。
 *    NTP タイムスタンプ (1900-01-01 起点) を UNIX タイムスタンプ
 *    (1970-01-01 起点) に変換し、tk_set_tim() でシステム時刻を設定。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#if defined(WIZCHIP_W5500)
#include "wiznet_drv.h"
#else
#include "w5100s_reg.h"
#endif
#include "tk_socket.h"
#include "tk_sntp.h"
#include "../include/dev_wiznet.h"
#include <tk/datetime.h>

/* NTP 定数 */
#define NTP_PORT        123
#define NTP_PKT_SZ      48
#define NTP_UNIX_EPOCH  2208988800UL  /* 1900-01-01 〜 1970-01-01 の秒数 */

/* 最後に取得した UNIX タイムスタンプ */
LOCAL UW sntp_unixtime = 0;

EXPORT ER tk_sntp_sync(UB sn, const UB *ntp_ip, TMO tmout)
{
    if (sn >= WIZNET_SOCK_NUM || ntp_ip == NULL) return E_PAR;

    UB buf[NTP_PKT_SZ];
    ER err;

    /* UDP ソケットオープン */
    W5DBG("SNTP: open sn=%d sr=0x%02x\n", sn, getSn_SR(sn));
    err = tk_sock_open(sn, Sn_MR_UDP, 0);
    W5DBG("SNTP: open ret=%d sr=0x%02x\n", err, getSn_SR(sn));
    if (err != E_OK) return err;

    /* NTP リクエスト組み立て (SNTPv4 client) */
    memset(buf, 0, NTP_PKT_SZ);
    buf[0] = 0x23;  /* LI=0, VN=4, Mode=3 (Client) */

    /* 送信 */
    W sent = tk_sock_sendto(sn, buf, NTP_PKT_SZ,
                            (UB *)ntp_ip, NTP_PORT, tmout);
    W5DBG("SNTP: sendto %d.%d.%d.%d:%d ret=%d\n",
          ntp_ip[0], ntp_ip[1], ntp_ip[2], ntp_ip[3], NTP_PORT, sent);
    if (sent < 0) {
        tk_sock_close(sn);
        return (sent == E_TMOUT) ? E_TMOUT : E_IO;
    }

    /* 応答受信 */
    UB svr_addr[4];
    UH svr_port;
    W rcvd = tk_sock_recvfrom(sn, buf, NTP_PKT_SZ,
                               svr_addr, &svr_port, tmout);
    tk_sock_close(sn);

    W5DBG("SNTP: rcvd=%d from %d.%d.%d.%d:%d\n",
          rcvd, svr_addr[0], svr_addr[1], svr_addr[2], svr_addr[3], svr_port);

    if (rcvd < NTP_PKT_SZ) {
        return (rcvd == E_TMOUT) ? E_TMOUT : E_IO;
    }

    /* Kiss-of-Death チェック: stratum=0 は拒否 (RFC 4330 Section 8) */
    if (buf[1] == 0) return E_IO;

    /* Transmit Timestamp (bytes 40-43 = 秒, 44-47 = 小数) */
    UW ntp_secs = ((UW)buf[40] << 24) | ((UW)buf[41] << 16) |
                  ((UW)buf[42] << 8) | buf[43];

    if (ntp_secs < NTP_UNIX_EPOCH) return E_IO;  /* 不正な応答 */

    sntp_unixtime = ntp_secs - NTP_UNIX_EPOCH;

    /*
     * μT-Kernel システム時刻に設定
     * T-Kernel の SYSTIM は 1985-01-01 GMT 基準 (hi:lo = 64bit ms)
     * UNIX epoch (1970) → T-Kernel epoch (1985) に変換
     */
    {
        UD ms = (UD)(sntp_unixtime - TK_EPOCH_DIFF) * 1000;
        SYSTIM tim;
        tim.hi = (W)(ms >> 32);
        tim.lo = (UW)(ms & 0xFFFFFFFF);
        tk_set_tim(&tim);
    }

    W5DBG("SNTP: synced (unix=%d)\n", sntp_unixtime);

    return E_OK;
}

EXPORT UW tk_sntp_get_unixtime(void)
{
    return sntp_unixtime;
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
