/*
 *----------------------------------------------------------------------
 *    MCP Server over MQTT for μT-Kernel 3.0
 *    Internal shared header for split compilation units.
 *----------------------------------------------------------------------
 */

#ifndef __TK_MCP_INT_H__
#define __TK_MCP_INT_H__

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <string.h>

#include "cJSON.h"
#include "tk_mcp.h"
#include "tk_mqtt.h"
#include "tk_socket.h"
#include "tk_sha256.h"
#include "w5100s_reg.h"
#include "../include/dev_w5100s.h"
#include "../include/dev_usb_hid.h"

#include <tk/datetime.h>

/* datetime extension */
extern ER dt_get_date(void *date);  /* datetime.h */

/*----------------------------------------------------------------------
 * 定数
 */
#define MCP_TASK_PRI        10
#define MCP_TASK_STKSZ      8192
#define MCP_BUF_SZ          512
#define MCP_TOPIC_MAXLEN    48	/* MQTT トピック名最大長 */

#define MCP_PROTOCOL_VERSION  "2024-11-05"
#define MCP_SERVER_NAME       "utk3-keyboard"
#define MCP_SERVER_VERSION    "1.0.0"

/* Flash ストレージ (2MB Flash 末尾 4KB セクタ) */
#define FLASH_STORAGE_OFFSET  (2 * 1024 * 1024 - 4096)
#define FLASH_STORAGE_BASE    (0x10000000 + FLASH_STORAGE_OFFSET)
#define FLASH_SECTOR_SIZE     4096
#define FLASH_PAGE_SIZE       256

/* セキュリティ: API キー */
#define APIKEY_FLASH_OFFSET   0x100
#define APIKEY_MAGIC          0x31304B41  /* "AK01" */
#define APIKEY_LEN            32

/* ADC レジ���タ */
#define ADC_BASE    0x4004C000
#define ADC_CS      (ADC_BASE + 0x00)
#define ADC_RESULT  (ADC_BASE + 0x04)

/* PWM レジスタ */
#define PWM_BASE  0x40050000

/* ウォッチドッグ レジスタ */
#define WATCHDOG_BASE  0x40058000
#define WD_CTRL        (WATCHDOG_BASE + 0x00)
#define WD_LOAD        (WATCHDOG_BASE + 0x04)
#define WD_REASON      (WATCHDOG_BASE + 0x08)
#define WD_TICK        (WATCHDOG_BASE + 0x2C)

/* TCP ツール定数 */
#define TCP_TOOL_SN  1
#define TCP_TOOL_RXBUF  256

/* 動的タスク */
#define MAX_DYN_TASKS  4

/* VM 定数 */
#define VM_DEFAULT_PROG  256  /* デフォルトプログラムサイズ */
#define VM_STACK_SZ  16
#define VM_MAX_INST  4  /* 同時実行 VM インスタンス */

/* VM opcodes */
#define OP_NOP     0x00
#define OP_PUSH    0x01
#define OP_POP     0x02
#define OP_DUP     0x03
#define OP_ADD     0x04
#define OP_SUB     0x05
#define OP_GT      0x06
#define OP_LT      0x07
#define OP_JMP     0x08
#define OP_JZ      0x09
#define OP_JNZ     0x0A
#define OP_GPIO_RD 0x0B
#define OP_GPIO_WR 0x0C
#define OP_ADC_RD  0x0D
#define OP_DELAY   0x0E
#define OP_HALT    0x0F
#define OP_MQTT    0x10

/* VM Flash マジック */
#define VM_FLASH_MAGIC  0x31304D56  /* "VM01" */

/* MCP イベントフラグ */
#define MCP_EVT_MSG  (1u << 0)

/*----------------------------------------------------------------------
 * 型定義
 */
typedef cJSON *(*FP_TOOL)(cJSON *args);

typedef struct {
    const char *name;
    const char *description;
    const char *schema;     /* inputSchema JSON 文字列 */
    FP_TOOL     handler;
} T_MCP_TOOL;

typedef struct {
    UB   *prog;         /* Kmalloc 動的確保 (NULL=未使用) */
    UW   prog_size;     /* 確保サイズ */
    UH   prog_len;      /* 実際のバイトコード長 */
    W    stack[VM_STACK_SZ];
    INT  sp;
    UH   pc;
    ID   tskid;
    BOOL running;
} T_VM_INST;

/* チャンク受信バッファ (vm_load で使用) */
typedef struct {
    UB   *buf;          /* Kmalloc 動的確保 */
    UW   total_size;    /* 予定サイ�� */
    UW   received;      /* 受信済み��イト数 */
    BOOL active;
} T_VM_CHUNKED;

typedef struct {
    ID  tskid;
    INT action;    /* 0=blink, 1=adc_log, 2=heartbeat */
    INT pin;
    UW  interval;
    BOOL running;
} T_DYN_TASK;

/*----------------------------------------------------------------------
 * extern 宣言: 共有グローバル変数
 */
extern T_MCP_CONF mcp_conf;
extern char mcp_topic_req[48];
extern char mcp_topic_res[48];
extern T_VM_INST vm_inst[VM_MAX_INST];

extern UB  mcp_api_key[APIKEY_LEN];
extern BOOL mcp_api_key_active;
extern FastLock rate_lock;

/*----------------------------------------------------------------------
 * 前方宣言: ツール関数 (tk_mcp_tools.c)
 */
cJSON *tool_send_keys(cJSON *args);
cJSON *tool_press_combo(cJSON *args);
cJSON *tool_set_layer(cJSON *args);
cJSON *tool_get_keyboard_status(cJSON *args);
cJSON *tool_get_tasks(cJSON *args);
cJSON *tool_get_uptime(cJSON *args);
cJSON *tool_get_network(cJSON *args);
cJSON *tool_get_datetime(cJSON *args);
cJSON *tool_set_led(cJSON *args);
cJSON *tool_reboot(cJSON *args);
cJSON *tool_read_adc(cJSON *args);
cJSON *tool_gpio_control(cJSON *args);
cJSON *tool_pwm_control(cJSON *args);
cJSON *tool_peek(cJSON *args);
cJSON *tool_poke(cJSON *args);
cJSON *tool_tcp_connect(cJSON *args);
cJSON *tool_flash_write(cJSON *args);
cJSON *tool_watchdog(cJSON *args);
cJSON *tool_task_create(cJSON *args);
cJSON *tool_task_stop(cJSON *args);
cJSON *tool_shell(cJSON *args);
cJSON *tool_net_scan(cJSON *args);
cJSON *tool_relay(cJSON *args);
cJSON *tool_mcp_auth(cJSON *args);

/* 前方宣言: VM 関数 (tk_mcp_vm.c) */
cJSON *tool_vm_run(cJSON *args);
cJSON *tool_vm_stop(cJSON *args);
cJSON *tool_vm_store(cJSON *args);
cJSON *tool_agent_start(cJSON *args);

/* 前方宣言: Flash 操作 (tk_mcp_vm.c) */
void flash_safe_erase_program(UW offset, const UB *data, UW len);

/*----------------------------------------------------------------------
 * static inline ヘルパー関数
 */

/* 安全な文字列結合ヘルパー (バッファオーバーフロー防止) */
static inline void safe_topic_build(char *buf, UH bufsz, const char *prefix,
                                    const char *id, const char *suffix)
{
    UH pos = 0, i;
    const char *parts[] = { prefix, id, suffix };
    INT p;
    for (p = 0; p < 3; p++) {
        if (parts[p] == NULL) continue;
        for (i = 0; parts[p][i] && pos < bufsz - 1; i++)
            buf[pos++] = parts[p][i];
    }
    buf[pos] = '\0';
}

/* UW を "0x%08X" 形式の文字列に変換 */
static inline void fmt_hex32(char *buf, UW val)
{
    static const char hx[] = "0123456789ABCDEF";
    buf[0] = '0'; buf[1] = 'x';
    INT i;
    for (i = 0; i < 8; i++) {
        buf[2 + i] = hx[(val >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
}

/* hex 文字を値に変換 */
static inline UB hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

/* IP アドレス文字列パース */
static inline UB parse_ip(const char *str, UB *ip)
{
    INT i, val;
    const char *p = str;
    for (i = 0; i < 4; i++) {
        val = 0;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
        if (val > 255) return 0;
        ip[i] = (UB)val;
        if (i < 3) { if (*p != '.') return 0; p++; }
    }
    return 1;
}

#endif /* __TK_MCP_INT_H__ */
