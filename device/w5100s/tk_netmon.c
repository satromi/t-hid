/*
 *----------------------------------------------------------------------
 *    μT-Kernel ネットワークリンク監視タスク
 *
 *    PHY リンク状態を 2 秒周期で監視し、リンク変化を検出する。
 *    リンクアップ時にコールバックを呼び出し、DHCP 再取得などに使う。
 *
 *    RTOS 機能:
 *    - 専用タスク: リンク監視ループ
 *    - tk_dly_tsk: 2 秒間隔の周期待ち (CPU を他タスクに譲る)
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "w5100s_reg.h"
#include "tk_netmon.h"
#include "tk_dhcp.h"

/* タスク設定 */
#define NETMON_TASK_PRI     12  /* 低優先度 */
#define NETMON_TASK_STKSZ   512
#define NETMON_POLL_MS      2000  /* 2 秒間隔 */

LOCAL ID netmon_tskid;
LOCAL BOOL netmon_running;
LOCAL BOOL netmon_link_up;
LOCAL UB netmon_dhcp_sn;
LOCAL FP_LINK_CHANGE netmon_callback;

/*----------------------------------------------------------------------
 * リンク監視タスク
 */
LOCAL void netmon_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    /* 初期状態取得 */
    netmon_link_up = (wizphy_getphylink() == PHY_LINK_ON);
    W5DBG("NETMON: link %s\n", netmon_link_up ? "UP" : "DOWN");

    while (netmon_running) {
        tk_dly_tsk(NETMON_POLL_MS);

        BOOL current = (wizphy_getphylink() == PHY_LINK_ON);

        if (current != netmon_link_up) {
            netmon_link_up = current;
            W5DBG("NETMON: link %s\n",
                      netmon_link_up ? "UP" : "DOWN");

            if (netmon_callback != NULL) {
                netmon_callback(netmon_link_up);
            }

            /* リンクアップ時に DHCP を自動再実行 (旧タスク完全停止後) */
            if (netmon_link_up) {
                W5DBG("NETMON: restarting DHCP\n");
                tk_dhcp_stop();
                tk_dly_tsk(500);  /* ソケット解放を確実に待つ */
                tk_dhcp_start(netmon_dhcp_sn, NULL, NULL);
            }
        }
    }

    tk_ext_tsk();
}

/*======================================================================
 * 公開 API
 *====================================================================*/

EXPORT ER tk_netmon_start(UB dhcp_sn, FP_LINK_CHANGE on_change)
{
    netmon_dhcp_sn = dhcp_sn;
    netmon_callback = on_change;
    netmon_running = TRUE;

    T_CTSK ctsk;
    ctsk.exinf  = NULL;
    ctsk.tskatr = TA_HLNG | TA_RNG3;
    ctsk.task   = (FP)netmon_task;
    ctsk.itskpri = NETMON_TASK_PRI;
    ctsk.stksz  = NETMON_TASK_STKSZ;
    ctsk.bufptr = NULL;
    netmon_tskid = tk_cre_tsk(&ctsk);
    if (netmon_tskid < 0) return E_LIMIT;
    tk_sta_tsk(netmon_tskid, 0);

    return E_OK;
}

EXPORT void tk_netmon_stop(void)
{
    netmon_running = FALSE;

    if (netmon_tskid > 0) {
        INT retry;
        for (retry = 0; retry < 50; retry++) {
            T_RTSK rtsk;
            if (tk_ref_tsk(netmon_tskid, &rtsk) != E_OK) break;
            if (rtsk.tskstat == TTS_DMT) break;
            tk_dly_tsk(100);
        }
        tk_del_tsk(netmon_tskid);
        netmon_tskid = 0;
    }
}

EXPORT BOOL tk_netmon_is_link_up(void)
{
    return netmon_link_up;
}

#endif /* CPU_RP2040 */
