/*
 * cyw43_configport.h — CYW43 driver port configuration for μT-Kernel
 *
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CYW43 ドライバが <cyw43_configport.h> をインクルードして
 * プラットフォーム固有の設定を取得する。
 */

#ifndef CYW43_CONFIGPORT_H
#define CYW43_CONFIGPORT_H

#include <stdint.h>
#include <stddef.h>   /* size_t */
#include <stdbool.h>

/* C11 static_assert サポート */
#ifndef static_assert
#define static_assert _Static_assert
#endif

/* エラーコード (CYW43 ドライバが返す負の errno 値) */
#define CYW43_EPERM         (-1)
#define CYW43_EIO           (-5)
#define CYW43_EINVAL        (-22)
#define CYW43_ETIMEDOUT     (-110)

/* HAL GPIO 関数 (cyw43_ctrl.c / cyw43_ll.c から呼ばれる) */
#define CYW43_HAL_PIN_MODE_INPUT    0
#define CYW43_HAL_PIN_MODE_OUTPUT   1
#define CYW43_HAL_PIN_PULL_NONE     0
#define CYW43_HAL_PIN_PULL_UP       1
#define CYW43_HAL_MAC_WLAN0         0

void cyw43_hal_pin_config(int pin, int mode, int pull, int init_value);
void cyw43_hal_pin_low(int pin);
void cyw43_hal_pin_high(int pin);
int  cyw43_hal_pin_read(int pin);
void cyw43_hal_pin_config_irq_falling(int pin, int enable);
void cyw43_hal_get_mac(int idx, uint8_t buf[6]);

/* 内部ポーリングスケジュール関数 (cyw43_arch_tkernel.c で実装) */
void cyw43_schedule_internal_poll_dispatch(void (*func)(void));

/* スレッドロックチェック (デバッグ用、本番では no-op) */
#define CYW43_THREAD_LOCK_CHECK

/* 配列サイズマクロ */
#ifndef CYW43_ARRAY_SIZE
#define CYW43_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* HAL タイミング関数 */
uint32_t cyw43_hal_ticks_us(void);
uint32_t cyw43_hal_ticks_ms(void);

/* IOCTL 待ちフック — ロックを一時解放してポーリングタスクに実行機会を与える
 * (ロックを持ったまま待つと poll タスクが進まずデッドロックする) */
void cyw43_tkernel_ioctl_wait(void);
#define CYW43_DO_IOCTL_WAIT    cyw43_tkernel_ioctl_wait()

/* CYW43 ドライバが使用するユーティリティマクロ */
#ifndef MIN
#define MIN(a,b)  ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b)  ((a) > (b) ? (a) : (b))
#endif

/*----------------------------------------------------------------------
 * バス設定 — SPI (PIO 経由)
 */
#define CYW43_USE_SPI               1

/*----------------------------------------------------------------------
 * Bluetooth 有効化 (SPI バス経由)
 * WiFi+BT 統合ファームウェアを使用
 */
#define CYW43_ENABLE_BLUETOOTH      1

/*----------------------------------------------------------------------
 * lwIP 有効/無効
 * WIFI_CYW43 定義時: CYW43 WiFi + lwIP を使用
 * BLE-only 時: WiFi/TCP は W5100S で賄うため lwIP 不要
 */
#ifdef WIFI_CYW43
#define CYW43_LWIP                  1
#else
#define CYW43_LWIP                  0
#endif

/*----------------------------------------------------------------------
 * Pico W GPIO 割当
 * CYW43439 は RP2040 内部で接続されている (外部ピンではない)
 */
#define CYW43_PIN_WL_REG_ON        23  /* WL_REG_ON: CYW43 power enable */
#define CYW43_PIN_WL_HOST_WAKE     24  /* WL_HOST_WAKE: CYW43 → RP2040 (active HIGH) */
/* SPI: GP25(CS), GP29(CLK/DATA) — PIO で制御 */

/*----------------------------------------------------------------------
 * ファームウェア — WiFi+BT combined
 */
/* ファームウェアヘッダ — INCPATH に cyw43-driver/firmware があるため直接参照 */
#define CYW43_CHIPSET_FIRMWARE_INCLUDE_FILE \
    "wb43439A0_7_95_49_00_combined.h"
#define CYW43_WIFI_NVRAM_INCLUDE_FILE \
    "wifi_nvram_43439.h"

/*----------------------------------------------------------------------
 * 排他制御 — μT-Kernel FastLock
 * CYW43 ドライバの SPI アクセスをタスク間で排他制御する
 */
void cyw43_tkernel_lock(void);
void cyw43_tkernel_unlock(void);
#define CYW43_THREAD_ENTER  cyw43_tkernel_lock()
#define CYW43_THREAD_EXIT   cyw43_tkernel_unlock()
#define CYW43_SDPCM_SEND_COMMON_WAIT cyw43_tkernel_yield()
void cyw43_tkernel_yield(void);

/*----------------------------------------------------------------------
 * タイミング
 */
void cyw43_delay_us(uint32_t us);
void cyw43_delay_ms(uint32_t ms);
#define CYW43_DELAY_US(us)  cyw43_delay_us(us)
#define CYW43_DELAY_MS(ms)  cyw43_delay_ms(ms)

/*----------------------------------------------------------------------
 * printf — tm_printf は <tm/tmonitor.h> で宣言
 * CYW43 のログ出力先を μT-Kernel のデバッグ出力にリダイレクト
 */
#include <tm/tmonitor.h>
#define CYW43_PRINTF(...)   tm_printf((UB *)__VA_ARGS__)

/*----------------------------------------------------------------------
 * GPIO (CYW43 WL_GPIO for onboard LED on Pico W)
 */
#define CYW43_GPIO              1
#define CYW43_NUM_GPIOS         3   /* CYW43439: GPIO 0-2 */

/*----------------------------------------------------------------------
 * イベントポーリングフック (長時間ブロック操作中の yield)
 */
#define CYW43_EVENT_POLL_HOOK   cyw43_tkernel_yield()

/*----------------------------------------------------------------------
 * LWIP マクロ無効化 (TCP/IP は W5100S 経由で独自実装)
 */
#define LWIP_MAKEU32(a,b,c,d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

#endif /* CYW43_CONFIGPORT_H */
