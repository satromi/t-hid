/*
 *----------------------------------------------------------------------
 *    MCP Server over MQTT for μT-Kernel 3.0
 *----------------------------------------------------------------------
 */

#ifndef __TK_MCP_H__
#define __TK_MCP_H__

#include <tk/tkernel.h>

/* MCP サーバ設定 */
typedef struct {
    UB  sn;             /* MQTT 用ソケット番号 (0-3) */
    UB  broker_ip[4];   /* MQTT ブローカ IP */
    UH  broker_port;    /* MQTT ブローカ ポート (default 1883) */
    const char *mqtt_user;  /* MQTT ユーザー名 (NULL=匿名) */
    const char *mqtt_pass;  /* MQTT パスワード (NULL=なし) */
    UINT rate_limit;    /* コマンド/秒 上限 (0=無制限) */
} T_MCP_CONF;

/* MCP サーバ起動 */
ER tk_mcp_start(const T_MCP_CONF *conf);

/* MCP サーバ停止 */
ER tk_mcp_stop(void);

#endif /* __TK_MCP_H__ */
