/*
 *----------------------------------------------------------------------
 *    μT-Kernel Native DHCP Client
 *
 *    ioLibrary dhcp.c の完全置き換え。
 *    RTOS 機能:
 *    - 専用 DHCP タスク (ポーリングループ不要)
 *    - 周期ハンドラ (tk_cre_cyc) で 1 秒タイマー
 *    - tk_sock_sendto/recvfrom (割り込み駆動 UDP)
 *    - tk_dly_tsk (ビジーウェイト排除)
 *----------------------------------------------------------------------
 */

#ifndef __TK_DHCP_H__
#define __TK_DHCP_H__

#include <tk/tkernel.h>

/* DHCP 結果コード */
#define TK_DHCP_SUCCESS		0	/* IP 取得成功 */
#define TK_DHCP_RUNNING		1	/* 処理中 */
#define TK_DHCP_TIMEOUT		2	/* タイムアウト (サーバー無応答) */
#define TK_DHCP_CONFLICT	3	/* IP 競合 */

/* DHCP コールバック */
typedef struct {
    void (*ip_assign)(void);	/* IP 初回割当 */
    void (*ip_update)(void);	/* IP 更新 */
    void (*ip_conflict)(void);	/* IP 競合検出 */
} T_DHCP_CB;

/* DHCP 取得結果 */
typedef struct {
    UB	ip[4];		/* 割り当て IP */
    UB	sn[4];		/* サブネットマスク */
    UB	gw[4];		/* ゲートウェイ */
    UB	dns[4];		/* DNS サーバー */
    UW	lease_time;	/* リース時間 (秒) */
} T_DHCP_INFO;

/*
 * DHCP クライアント開始
 *   sn:   使用するソケット番号 (0-3)
 *   cb:   コールバック (NULL 可 → デフォルト動作)
 *   info: 結果格納先 (NULL 可)
 *
 * 内部で DHCP タスクと周期ハンドラを生成する。
 * 呼び出し元タスクは即座にリターンする (非同期)。
 */
ER tk_dhcp_start(UB sn, const T_DHCP_CB *cb, T_DHCP_INFO *info);

/* DHCP クライアント停止 */
void tk_dhcp_stop(void);

/* DHCP 取得情報の取得 (LEASED 状態で有効) */
void tk_dhcp_get_info(T_DHCP_INFO *info);

/* DHCP ステート取得 */
#define TK_DHCP_ST_INIT		0
#define TK_DHCP_ST_DISCOVER	1
#define TK_DHCP_ST_REQUEST	2
#define TK_DHCP_ST_LEASED	3
#define TK_DHCP_ST_REREQUEST	4
#define TK_DHCP_ST_STOP		5
INT tk_dhcp_get_state(void);

#endif /* __TK_DHCP_H__ */
