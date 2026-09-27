/*
 * cyw43_spi_pio.h — CYW43 PIO-based half-duplex SPI for RP2040
 *
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Pico W の CYW43439 SPI バスピン割当:
 *   GP23 = WL_REG_ON (CYW43 power enable, active HIGH)
 *   GP24 = SPI DATA (bidirectional, half-duplex) + WL_HOST_WAKE (IRQ)
 *   GP25 = SPI CS (active LOW)
 *   GP29 = SPI CLK (output only, PIO sideset で駆動)
 *
 * PIO の sideset 機能で GP29 にクロックを生成し、GP24 でデータを送受信する。
 */

#ifndef CYW43_SPI_PIO_H
#define CYW43_SPI_PIO_H

#include <stdint.h>

/*----------------------------------------------------------------------
 * RP2040 PIO ベースアドレス・レジスタオフセット
 */
#define PIO0_BASE           0x50200000
#define PIO1_BASE           0x50300000

/* 使用する PIO インスタンス */
/* PIO0 を使用: RESETS bit=10, GPIO FUNCSEL=6 と整合させる */
#define CYW43_PIO_BASE      PIO0_BASE

/* PIO レジスタオフセット */
#define PIO_CTRL            0x000
#define PIO_FSTAT           0x004
#define PIO_FDEBUG          0x008
#define PIO_FLEVEL          0x00C
#define PIO_TXF0            0x010
#define PIO_RXF0            0x020
#define PIO_IRQ             0x030
#define PIO_IRQ_FORCE       0x034
#define PIO_INPUT_SYNC_BYPASS 0x038
#define PIO_INSTR_MEM0      0x048
#define PIO_SM0_CLKDIV      0x0C8
#define PIO_SM0_EXECCTRL    0x0CC
#define PIO_SM0_SHIFTCTRL   0x0D0
#define PIO_SM0_ADDR        0x0D4
#define PIO_SM0_INSTR       0x0D8
#define PIO_SM0_PINCTRL     0x0DC

/* FSTAT ビット */
#define PIO_FSTAT_TXFULL_SHIFT  16
#define PIO_FSTAT_TXEMPTY_SHIFT 24
#define PIO_FSTAT_RXFULL_SHIFT  0
#define PIO_FSTAT_RXEMPTY_SHIFT 8

/* FDEBUG ビット */
#define PIO_FDEBUG_TXSTALL_LSB  24

/* Pico W CYW43 SPI GPIO */
#define CYW43_PIN_DATA      24      /* SPI DATA (bidirectional, GP24) */
#define CYW43_PIN_CLK       29      /* SPI CLK (output, GP29) */
#define CYW43_PIN_CS        25      /* SPI CS (active LOW, GP25) */
#define CYW43_PIN_ON        23      /* WL_REG_ON (power enable, GP23) */
#define CYW43_PIN_IRQ       24      /* WL_HOST_WAKE (= DATA pin, GP24) */

/* PIO SM 番号 (SM0 を使用) */
#define CYW43_PIO_SM        0

/* SPI クロック分周: 125MHz / DIV = PIO clock
 * PIO は 2 サイクル/ビット (out+jmp) → 実効 PIO_CLK / 2 Mbit/s
 * DIV_INT=2, DIV_FRAC=0 (31.25 Mbit/s) */
#define CYW43_PIO_CLK_DIV_INT   2
#define CYW43_PIO_CLK_DIV_FRAC  0

/* RP2040 TIMER (64-bit microsecond counter) */
#define RP2040_TIMER_BASE       0x40054000
#define RP2040_TIMER_TIMERAWL   (RP2040_TIMER_BASE + 0x28)  /* 下位32bit, ラッチなし */

/* FIFO タイムアウト (マイクロ秒) */
#define PIO_FIFO_TIMEOUT_US     1000  /* 1ms: 2KB転送~65us の 15倍マージン */

/*----------------------------------------------------------------------
 * RP2040 DMA レジスタ (bswap 転送用)
 */
#define DMA_BASE                0x50000000
#define DMA_CH(n)               (DMA_BASE + (n) * 0x40)
#define DMA_CH_READ_ADDR(n)     (DMA_CH(n) + 0x00)
#define DMA_CH_WRITE_ADDR(n)    (DMA_CH(n) + 0x04)
#define DMA_CH_TRANS_COUNT(n)   (DMA_CH(n) + 0x08)
#define DMA_CH_CTRL_TRIG(n)     (DMA_CH(n) + 0x0C)
#define DMA_CH_CTRL(n)          (DMA_CH(n) + 0x10)  /* alias: no trigger */

/* CTRL ビット */
#define DMA_CTRL_EN             (1u << 0)
#define DMA_CTRL_DATA_SIZE_W    (2u << 2)    /* 32-bit transfers */
#define DMA_CTRL_INCR_READ      (1u << 4)
#define DMA_CTRL_INCR_WRITE     (1u << 5)
#define DMA_CTRL_BSWAP          (1u << 22)
#define DMA_CTRL_BUSY           (1u << 24)

/* PIO DREQ: PIO0 SM0 TX=0, RX=4 */
#define DREQ_PIO0_TX0           0
#define DREQ_PIO0_RX0           4

/* DMA チャネル (CYW43 SPI 専用: CH10=TX, CH11=RX) */
#define CYW43_DMA_CH_TX         10
#define CYW43_DMA_CH_RX         11

#endif /* CYW43_SPI_PIO_H */
