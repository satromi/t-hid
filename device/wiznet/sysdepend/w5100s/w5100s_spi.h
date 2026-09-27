/*
 *----------------------------------------------------------------------
 *    W5100S Ethernet Device Driver for μT-Kernel 3.0 BSP
 *
 *    RP2040 SPI HAL + 割り込み管理
 *    W5100S-EVB-Pico: SPI0 (GP16-19), RST (GP20), INTn (GP21)
 *----------------------------------------------------------------------
 */

#ifndef __W5100S_SPI_H__
#define __W5100S_SPI_H__

#include <tk/tkernel.h>
#include "../../../include/dev_wiznet.h"  /* WIZNET_SOCK_NUM */

/* SPI0 ベースアドレス */
#define W5100S_SPI_BASE		SPI0_BASE

/* W5100S-EVB-Pico GPIO 割当 */
#define W5100S_PIN_CS		17
#define W5100S_PIN_RST		20
#define W5100S_PIN_INT		21

/* SPI クロック: sys_clk / (CPSR * (1+SCR)) = 125M / (10*2) = 6.25MHz
 * 31.25MHz ではボード配線で動作せず、低速に変更 */
#define W5100S_SPI_PRESCALE	10
#define W5100S_SPI_POSTDIV	1	/* SCR=1 */

/* W5100S INTn 割り込み — RP2040 IO_IRQ_BANK0 = IRQ 13 */
#define W5100S_INTNO		13

/*----------------------------------------------------------------------
 * W5100S 処理タスク用イベントフラグ
 * ISR → 処理タスク: IRQ 通知のみ (SPI アクセスなし)
 */
#define WIZNET_EVT_IRQ		(1u << 0)

extern ID wiznet_irq_flgid;	/* ISR → 処理タスク通知用 */

/*----------------------------------------------------------------------
 * ソケット別イベントフラグ
 * 処理タスク → アプリタスク: ソケットイベント通知
 */
#define WIZNET_SOCK_EVT_RECV	(1u << 0)
#define WIZNET_SOCK_EVT_CON	(1u << 1)
#define WIZNET_SOCK_EVT_DISCON	(1u << 2)
#define WIZNET_SOCK_EVT_TIMEOUT	(1u << 3)
#define WIZNET_SOCK_EVT_SENDOK	(1u << 4)
#define WIZNET_SOCK_EVT_BREAK	(1u << 5)	/* tk_sock_break() による中止 */
#define WIZNET_SOCK_EVT_ANY	(0x3F)

extern ID wiznet_sock_flgid[WIZNET_SOCK_NUM];  /* ソケット別イベントフラグ */

/* W5100S 処理タスク設定 */
#define W5100S_TASK_PRI		4	/* USB(5)より高優先度 */
#define W5100S_TASK_STKSZ	512

/* 初期化・リセット */
void wiznet_spi_init(void);	/* SPI + タスク + イベントフラグ (割り込みは登録しない) */
void wiznet_int_init(void);	/* GPIO 割り込み登録 (HW リセット後に呼ぶこと) */
void wiznet_hw_reset(void);

#endif /* __W5100S_SPI_H__ */
