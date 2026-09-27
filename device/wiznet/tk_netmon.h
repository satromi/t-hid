/*
 *----------------------------------------------------------------------
 *    μT-Kernel ネットワークリンク監視タスク
 *
 *    PHY リンク状態を周期的に監視し、リンクダウン→アップで
 *    DHCP を自動再実行する。
 *
 *    RTOS 機能:
 *    - 周期ハンドラ: 2 秒間隔でリンク状態チェック
 *    - イベントフラグ: リンク変化通知
 *----------------------------------------------------------------------
 */

#ifndef __TK_NETMON_H__
#define __TK_NETMON_H__

#include <tk/tkernel.h>

/* リンク変化コールバック */
typedef void (*FP_LINK_CHANGE)(BOOL link_up);

/*
 * リンク監視開始
 *   dhcp_sn:  DHCP に使うソケット番号 (リンクアップ時に DHCP 再実行)
 *   on_change: リンク変化コールバック (NULL 可)
 */
ER tk_netmon_start(UB dhcp_sn, FP_LINK_CHANGE on_change);

/* リンク監視停止 */
void tk_netmon_stop(void);

/* 現在のリンク状態 */
BOOL tk_netmon_is_link_up(void);

#endif /* __TK_NETMON_H__ */
