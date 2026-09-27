/*
 *----------------------------------------------------------------------
 *    W5500 SPI HAL 設定 — RP2040 用
 *
 *    WIZ550io 接続例 (W5100S-EVB-Pico と同じ SPI0):
 *      MISO = GP16, CS = GP17, SCLK = GP18, MOSI = GP19
 *      RST  = GP20, INT = GP21
 *----------------------------------------------------------------------
 */

#ifndef __W5500_SPI_RP2040_H__
#define __W5500_SPI_RP2040_H__

/* SPI ペリフェラル */
#define W5500_SPI_BASE       0x4003C000	/* SPI0 */
#define W5500_SPI_PRESCALE   10         /* 125MHz / (10*2) = 6.25MHz */

/* GPIO ピン */
#define W5500_PIN_MISO       16
#define W5500_PIN_CS         17
#define W5500_PIN_SCLK       18
#define W5500_PIN_MOSI       19
#define W5500_PIN_RST        20
#define W5500_PIN_INT        21

/* 割り込み */
#define W5500_INTNO          13         /* IO_IRQ_BANK0 */
#define W5500_INTPRI         2

/* ソケット数 */
#define W5500_SOCK_NUM       8

/* タスク設定 */
#define W5500_TASK_PRI       4
#define W5500_TASK_STKSZ     512

/* イベント */
#define W5500_EVT_IRQ        (1u << 0)
#define W5500_SOCK_EVT_RECV    (1u << 0)
#define W5500_SOCK_EVT_CON     (1u << 1)
#define W5500_SOCK_EVT_DISCON  (1u << 2)
#define W5500_SOCK_EVT_TIMEOUT (1u << 3)
#define W5500_SOCK_EVT_SENDOK  (1u << 4)
#define W5500_SOCK_EVT_BREAK   (1u << 5)

/* 公開 API */
void wiznet_spi_init(void);
void wiznet_int_init(void);
void wiznet_hw_reset(void);

#endif /* __W5500_SPI_RP2040_H__ */
