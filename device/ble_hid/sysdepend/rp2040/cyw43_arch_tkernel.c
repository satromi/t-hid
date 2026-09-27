/*
 * cyw43_arch_tkernel.c — CYW43 architecture abstraction for μT-Kernel
 *
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CYW43439 の初期化、排他制御、タイミング関数を μT-Kernel で実装。
 *
 * Pico W のハードウェア:
 *   GP23 = WL_REG_ON (CYW43 power enable)
 *   GP24 = WL_HOST_WAKE (CYW43 → RP2040 IRQ)
 *   GP25 = SPI CS (shared with onboard LED via CYW43 GPIO)
 *   GP29 = SPI CLK/DATA (PIO half-duplex)
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <sys/sysdef.h>

#include "cyw43.h"
#include "cyw43_internal.h"   /* cyw43_int_t */
#include "cyw43_configport.h"
#if CYW43_LWIP
#include "lwip/init.h"
#include "lwip/timeouts.h"
#endif
#ifdef KB_OUTPUT_BLE
#include "btstack_port_tkernel.h"
#endif

/*----------------------------------------------------------------------
 * CYW43 ドライバのグローバルインスタンス (cyw43_ctrl.c で定義済み)
 */
extern cyw43_t cyw43_state;

/*----------------------------------------------------------------------
 * 排他制御 — FastLock
 * CYW43 ドライバの SPI アクセスをタスク間で保護
 */
LOCAL FastLock cyw43_lock;
LOCAL volatile BOOL lock_initialized = FALSE;
LOCAL volatile BOOL cyw43_ready = FALSE;  /* CYW43 完全初期化済み */
LOCAL volatile ID   cyw43_poll_tskid = 0; /* ポーリングタスク ID (起床用) */

/* 再入可能ロック: CYW43 ドライバは cyw43_wifi_set_up → cyw43_wifi_on のように
 * CYW43_THREAD_ENTER をネストして呼ぶため、FastLock (非再入) では不可。
 * ロック保持タスクを記録して、同一タスクからの再入を許可する。 */
LOCAL volatile ID   cyw43_lock_owner = 0;
LOCAL volatile INT  cyw43_lock_depth = 0;

EXPORT void cyw43_arch_set_ready(void)
{
    cyw43_ready = TRUE;
}

EXPORT BOOL cyw43_arch_is_ready(void)
{
    return cyw43_ready;
}

EXPORT void cyw43_arch_set_poll_task(ID tskid)
{
    cyw43_poll_tskid = tskid;
}

/* 診断用: ロック競合カウンタ (同一タスク再入以外で Lock() 呼出回数) */
volatile uint32_t cyw43_lock_acquire_count = 0;
volatile uint32_t cyw43_lock_contention_count = 0;

EXPORT void cyw43_tkernel_lock(void)
{
    if (!lock_initialized) return;
    ID me = tk_get_tid();
    if (cyw43_lock_owner == me) {
        /* 同一タスクからの再入 — FastLock を取らずに深度だけ増加 */
        cyw43_lock_depth++;
        return;
    }
    /* 他タスクがロックを保持していれば Lock() で待機 → 競合としてカウント */
    if (cyw43_lock_owner != 0) cyw43_lock_contention_count++;
    Lock(&cyw43_lock);
    cyw43_lock_owner = me;
    cyw43_lock_depth = 1;
    cyw43_lock_acquire_count++;
}

EXPORT void cyw43_tkernel_unlock(void)
{
    if (!lock_initialized) return;
    cyw43_lock_depth--;
    if (cyw43_lock_depth == 0) {
        cyw43_lock_owner = 0;
        Unlock(&cyw43_lock);
    }
}

EXPORT void cyw43_tkernel_yield(void)
{
    tk_dly_tsk(1);  /* 1ms yield — 他タスクに CPU を譲る */
}

/*----------------------------------------------------------------------
 * IOCTL 待ちフック — 単に yield するだけ (SPI 読出しはしない)
 *
 * 重要: ここで cyw43_ll_process_packets を呼んではならない。
 * do_ioctl 自身のループが cyw43_ll_sdpcm_poll_device で SPI 読出しを行うため、
 * ここでも process_packets を呼ぶと CONTROL_HEADER (IOCTL レスポンス) を
 * process_packets が誤って消費し、「got unexpected packet 0」となって
 * do_ioctl がレスポンスを受け取れずタイムアウトするレース条件が発生する。
 *
 * この関数は単に 1ms yield するのみ。他タスクへの実行機会を与える用途。
 * (lwIP sys_check_timeouts はロック保持中に呼んでも安全: 同一ロックで保護)
 */
EXPORT void cyw43_tkernel_ioctl_wait(void)
{
    /* 注意: sys_check_timeouts() をここで呼ぶと、IOCTL 処理中の
     * SPI/DMA 操作と lwIP タイマ内の tcp_output/etharp が衝突して
     * 無限ループや再帰で全タスク凍結する現象が実機で確認された。
     * sys_check_timeouts は poll タスクの cyw43_arch_tkernel_poll
     * だけで呼ぶこと。ここは純粋な yield のみ。 */
    tk_dly_tsk(1);
}

/*----------------------------------------------------------------------
 * HAL GPIO 関数 (cyw43_ctrl.c から呼ばれる)
 */
EXPORT void cyw43_hal_pin_config(int pin, int mode, int pull, int init_value)
{
    (void)pull;
    out_w(IO_BANK0_BASE + 0x04 + pin * 8, 5);  /* FUNCSEL=SIO */
    if (mode == 1) {  /* OUTPUT */
        if (init_value) out_w(GPIO_OUT_SET, (1u << pin));
        else            out_w(GPIO_OUT_CLR, (1u << pin));
        out_w(GPIO_OE_SET, (1u << pin));
    } else {
        out_w(GPIO_OE_CLR, (1u << pin));
    }
}

EXPORT void cyw43_hal_pin_config_irq_falling(int pin, int enable)
{
    /* GP24 (WL_HOST_WAKE) の割り込み設定
     * CYW43 は INTERRUPT_POLARITY_HIGH 設定なので立上りエッジでアサート。
     * 関数名は "falling" だが、実態は CYW43 イベント通知エッジ (=rising) を設定。
     * IO_BANK0 PROC0_INTE0..3 レジスタ: 0x100 + (pin/8)*4
     * 各 pin は 4bit: bit0=LEVEL_LOW, bit1=LEVEL_HIGH, bit2=EDGE_LOW, bit3=EDGE_HIGH */
    if (enable) {
        out_w(IO_BANK0_BASE + 0x100 + (pin / 8) * 4,
              (1u << ((pin % 8) * 4 + 3)));  /* EDGE_HIGH */
    }
}

EXPORT void cyw43_hal_get_mac(int idx, uint8_t buf[6])
{
    (void)idx;
    /* CYW43 の OTP MAC が使えない場合のフォールバック */
    buf[0] = 0x02; buf[1] = 0xCA; buf[2] = 0xFE;
    buf[3] = 0x00; buf[4] = 0x00; buf[5] = 0x01;
}

EXPORT void cyw43_hal_pin_low(int pin)
{
    out_w(GPIO_OUT_CLR, (1u << pin));
}

EXPORT void cyw43_hal_pin_high(int pin)
{
    out_w(GPIO_OUT_SET, (1u << pin));
}

EXPORT int cyw43_hal_pin_read(int pin)
{
    /*
     * WL_HOST_WAKE (GP24) の読出し:
     * PIO FUNCSEL 時に GPIO_IN が CYW43 の GP24 アサートを確実に検出できない
     * ケースが実機で確認された (poll 間隔や環境に依存)。
     *
     * 対策: IRQ ハンドラで poll タスクを即時起床する「割込み駆動」に加え、
     * pin_read で常に active を返すことで cyw43_ll_has_work を常時 true にして
     * SPI ステータスレジスタで確実にデータ有無を判断させる「ポーリング駆動」を
     * 併用 (フォールバック) する。両方の冗長性で安定接続を確保。
     */
    if (pin == CYW43_PIN_WL_HOST_WAKE && cyw43_ready) {
        return 1;  /* 常に active → SPI ステータスレジスタで判断 */
    }
    return (in_w(GPIO_IN) >> pin) & 1;
}

/*----------------------------------------------------------------------
 * タイミング関数
 */
EXPORT void cyw43_delay_us(uint32_t us)
{
    /*
     * 短いディレイ (< 1000μs) はビジーウェイト。
     * 長いディレイは tk_dly_tsk() で RTOS に委譲。
     */
    if (us < 1000) {
        volatile uint32_t count = us * 31;  /* ~125MHz で概算 */
        while (count--) {}
    } else {
        tk_dly_tsk((us + 999) / 1000);
    }
}

EXPORT uint32_t cyw43_hal_ticks_us(void)
{
    SYSTIM tim;
    tk_get_otm(&tim);
    return (uint32_t)(tim.lo * 1000);  /* ms → μs 概算 */
}

EXPORT uint32_t cyw43_hal_ticks_ms(void)
{
    SYSTIM tim;
    tk_get_otm(&tim);
    return (uint32_t)tim.lo;
}

EXPORT void cyw43_delay_ms(uint32_t ms)
{
    if (ms > 0) {
        tk_dly_tsk(ms);
    }
}

/*----------------------------------------------------------------------
 * GPIO IRQ ハンドラ (IO_IRQ_BANK0 = IRQ 13)
 *
 * CYW43 が GP24 (WL_HOST_WAKE) を HIGH にアサートすると立上りエッジで起動。
 * poll タスクを起床してイベント処理を即時に行う。
 */
#define RP2040_IRQ_IO_BANK0   13

/* PROC0 INTR (edge/level 検出ラッチ) レジスタ: 0x0F0 + (pin/8)*4
 * - INTR0 (0x0F0) pins 0-7
 * - INTR1 (0x0F4) pins 8-15
 * - INTR2 (0x0F8) pins 16-23
 * - INTR3 (0x0FC) pins 24-29
 * 旧コードは 0x0F8 と誤定義されていて pin 24 アクセスが INTE1 に届いていた。
 * IRQ が登録されなかったため顕在化していなかったバグ。 */
#define IO_BANK0_PROC0_INTR   (IO_BANK0_BASE + 0x0F0)

/* GP24 PROC0_INTE3 のアドレス (ビット 3 = EDGE_HIGH) */
#define IO_BANK0_PROC0_INTE   (IO_BANK0_BASE + 0x100)
/* RP2040 アトミック SET/CLR エイリアス */
#define RP2040_ATOMIC_SET_OFFSET  0x2000
#define RP2040_ATOMIC_CLR_OFFSET  0x3000

LOCAL void cyw43_gpio_irq_handler(UINT intno)
{
    (void)intno;
    /* GP24 用レジスタ: INTR3 (pins 24-31), ビット位置 = (24%8)*4 + 3 = 3 (EDGE_HIGH) */
    uint32_t intr_reg = IO_BANK0_PROC0_INTR + (24 / 8) * 4;
    uint32_t mask = (1u << ((24 % 8) * 4 + 3));

    if (in_w(intr_reg) & mask) {
        /* 割り込みクリア (書込みで W1C) */
        out_w(intr_reg, mask);
        /* CYW43 初期化完了前は無視 (ポーリング未起動のため起床先が無い) */
        if (cyw43_ready && cyw43_poll_tskid > 0) {
            tk_wup_tsk(cyw43_poll_tskid);
        }
    }
    ClearInt(RP2040_IRQ_IO_BANK0);
}

/*----------------------------------------------------------------------
 * SPI 転送前後で GP24 IRQ をマスク/アンマスク
 *
 * GP24 は CYW43 の WL_HOST_WAKE と SPI DATA を兼用。SPI 転送中はデータビット
 * 変化で EDGE_HIGH が多発してフラッド → tk_wup_tsk 連打 → 他タスク (特に
 * SysTick 系) が相対的に回らず、tk_dly_tsk が進まないため実機で動作不能に
 * なることを過去に確認。転送中は必ずマスクし、転送後に pending をクリアして
 * からアンマスクすることで、フラッドを防ぎつつイベント通知は拾えるようにする。
 */
EXPORT void cyw43_irq_mask_for_spi(void)
{
    /* PROC0_INTE3 の bit 3 (EDGE_HIGH for pin 24) を atomic CLR */
    out_w(IO_BANK0_PROC0_INTE + (24 / 8) * 4 + RP2040_ATOMIC_CLR_OFFSET,
          (1u << ((24 % 8) * 4 + 3)));
}

EXPORT void cyw43_irq_unmask_after_spi(void)
{
    /* 1. pending INTR (edge latch) をクリア */
    out_w(IO_BANK0_PROC0_INTR + (24 / 8) * 4,
          (1u << ((24 % 8) * 4 + 3)));
    /* 2. INTE 有効化 (atomic SET) */
    out_w(IO_BANK0_PROC0_INTE + (24 / 8) * 4 + RP2040_ATOMIC_SET_OFFSET,
          (1u << ((24 % 8) * 4 + 3)));
}

/*----------------------------------------------------------------------
 * CYW43 アーキテクチャ初期化
 *
 * 1. FastLock 生成
 * 2. lwIP 初期化 (CYW43_LWIP=1 時)
 * 3. CYW43 ドライバ初期化 (GPIO 設定、電源 ON、SPI、FW ロード)
 * 4. GPIO IRQ ハンドラ登録
 */
EXPORT ER cyw43_arch_tkernel_init(void)
{
    /* FastLock */
    CreateLock(&cyw43_lock, (CONST UB *)"cyw4");
    lock_initialized = TRUE;

#if CYW43_LWIP
    /* lwIP 初期化 (NO_SYS=1: cyw43_cb_tcpip_init の前に必要) */
    lwip_init();
#endif

    /*
     * GPIO 設定と CYW43 パワーオンは cyw43_init() → cyw43_spi_gpio_setup()
     * → cyw43_spi_reset() の内部で行われる。
     */
    cyw43_init(&cyw43_state);

    /*
     * GPIO IRQ (IO_BANK0) ハンドラ登録 + EDGE_HIGH 検出有効化。
     *
     * 過去に SPI 転送中のデータビットで EDGE_HIGH がフラッドして SysTick が
     * 相対的に進まない問題が出たため、cyw43_spi_pio.c 側で転送前後に
     * cyw43_irq_mask_for_spi()/cyw43_irq_unmask_after_spi() を呼んで
     * マスクする。cyw43_hal_pin_read の pin=1 フォールバックは保持、
     * IRQ 漏れがあっても polling (5ms) で拾えるフェイルセーフ設計。
     */
    {
        T_DINT dint;
        dint.intatr = TA_HLNG;
        dint.inthdr = (FP)cyw43_gpio_irq_handler;
        ER derr = tk_def_int(RP2040_IRQ_IO_BANK0, &dint);
        if (derr == E_OK) {
            EnableInt(RP2040_IRQ_IO_BANK0, 2);   /* ARMv6-M 4段階中 中優先度 */
            /* cyw43_hal_pin_config_irq_falling は cyw43_init 内で
             * 呼ばれて INTE3 bit 3 (EDGE_HIGH) が有効化済み。 */
        }
    }

    return E_OK;
}

/*----------------------------------------------------------------------
 * CYW43 ポーリング (ポーリングタスク / IOCTL 待ちから呼ばれる)
 *
 * cyw43_sleep 減算ロジック:
 *   cyw43_sleep は cyw43-driver が「CYW43 バスを起床状態に維持する残時間」として使う。
 *   cyw43_poll_func 内で cyw43_sleep==0 なら cyw43_ll_bus_sleep(true) が呼ばれ、
 *   CYW43439 の SPI バスがスリープに入り省電力化される。
 *   IOCTL や IRQ 発生時に cyw43_ensure_up() が呼ばれて cyw43_sleep = CYW43_SLEEP_MAX(50) に戻る。
 *   ここで 100ms 毎に 1 減算 → 5 秒アイドルで自動スリープ。
 */
extern uint32_t cyw43_sleep;  /* cyw43_ctrl.c で定義 */

EXPORT void cyw43_arch_tkernel_poll(void)
{
    cyw43_tkernel_lock();
    if (cyw43_poll != NULL) {
        cyw43_poll();
    }
#if CYW43_LWIP
    sys_check_timeouts();
#endif

    /* cyw43_sleep カウンタ減算 (poll 5ms 周期 × 20 = 100ms 毎に 1 減算
     *   → CYW43_SLEEP_MAX=50 → 5 秒アイドルで自動バススリープ) */
    {
        static volatile uint32_t sleep_prescaler = 0;
        if (++sleep_prescaler >= 20) {
            sleep_prescaler = 0;
            if (cyw43_sleep > 0) {
                cyw43_sleep--;
            }
        }
    }

    cyw43_tkernel_unlock();
}

/*----------------------------------------------------------------------
 * CYW43 GPIO 操作 (Pico W オンボード LED 用)
 *
 * Pico W の GP25 は CYW43 SPI CS と共有されているため、
 * オンボード LED は CYW43 のワイヤレス GPIO 経由で制御する。
 * CYW43 GPIO 0 = オンボード LED。
 */
EXPORT void cyw43_arch_gpio_put(int gpio, int value)
{
    /*
     * CYW43 初期化完了前は何もしない。
     * Scanner タスクの KB_LED_ON() が CYW43 初期化中に呼ばれると
     * cyw43_ensure_up() → SPI 通信 → ハング → USB レポート停止になる。
     */
    if (!cyw43_ready) {
        return;
    }
    cyw43_gpio_set(&cyw43_state, gpio, value);
}

/*----------------------------------------------------------------------
 * CYW43 省電力管理
 *
 * CYW43439 は PM2 (Power Management Mode 2) で自動スリープが可能。
 * BLE 接続中は connection interval 間にスリープに入り、
 * 次のイベントまで自動で復帰する。
 *
 * PM2 設定後は CYW43 が自律的にスリープ/ウェイクを行うため、
 * RP2040 側の追加処理は不要。
 */
EXPORT void cyw43_arch_tkernel_enable_pm(void)
{
    /* CYW43 PM2 (Aggressive Power Save) を有効化 */
    cyw43_wifi_pm(&cyw43_state, CYW43_PERFORMANCE_PM);
    tm_printf((UB *)"BLE: CYW43 power management enabled\n");
}

/*----------------------------------------------------------------------
 * CYW43 完全パワーダウン (WL_REG_ON = LOW)
 *
 * BLE を使用しない場合 (USB 接続時) に CYW43 を完全にオフにして省電力。
 * 再起動には cyw43_arch_tkernel_init() の呼び直しが必要。
 */
EXPORT void cyw43_arch_tkernel_power_down(void)
{
    cyw43_deinit(&cyw43_state);

    /* WL_REG_ON (GP23) = LOW → CYW43 電源オフ */
    out_w(IO_BANK0_BASE + 0x04 + 23 * 8, 5);  /* FUNCSEL=SIO */
    out_w(GPIO_OE_SET, (1u << 23));
    out_w(GPIO_OUT_CLR, (1u << 23));

    tm_printf((UB *)"BLE: CYW43 powered down\n");
}

/*======================================================================
 * CYW43 ドライバが要求する HAL スタブ関数
 *
 * cyw43_ctrl.c / cyw43_ll.c から呼ばれるプラットフォーム依存関数。
 *====================================================================*/

/*----------------------------------------------------------------------
 * TCP/IP コールバック — lwIP 無効時のスタブ
 * CYW43_LWIP=1 時は cyw43_lwip.c が提供する
 */
#if !CYW43_LWIP
void cyw43_cb_tcpip_init(cyw43_t *self, int itf)
{
    (void)self; (void)itf;
}

void cyw43_cb_tcpip_deinit(cyw43_t *self, int itf)
{
    (void)self; (void)itf;
}

void cyw43_cb_tcpip_set_link_up(cyw43_t *self, int itf)
{
    (void)self; (void)itf;
}

void cyw43_cb_tcpip_set_link_down(cyw43_t *self, int itf)
{
    (void)self; (void)itf;
}
#endif

/*----------------------------------------------------------------------
 * MAC アドレス生成 — CYW43 が OTP MAC を持たない場合のフォールバック
 * cyw43_ll.c から呼ばれる
 */
void cyw43_hal_generate_laa_mac(int idx, uint8_t buf[6])
{
    (void)idx;
    /* ローカル管理アドレス (bit1 of byte0 = 1) */
    buf[0] = 0x02;
    buf[1] = 0xCA;
    buf[2] = 0xFE;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = (uint8_t)idx;
}

/*----------------------------------------------------------------------
 * 内部ポーリングのスケジュール — CYW43 ドライバが割り込み処理後に呼ぶ
 * cyw43_ctrl.c から呼ばれる。BLE タスクを起床させる。
 */
void cyw43_schedule_internal_poll_dispatch(void (*func)(void))
{
    (void)func;
#ifdef KB_OUTPUT_BLE
    btstack_tkernel_trigger();
#else
    /* WiFi-only モード: ポーリングタスクを起床させてイベント即時処理 */
    if (cyw43_poll_tskid > 0) {
        tk_wup_tsk(cyw43_poll_tskid);
    }
#endif
}

/*----------------------------------------------------------------------
 * CYW43 BT バス (SPI 経由 HCI)
 *
 * cyw43_btbus_init/read/write は cybt_shared_bus.c で実装。
 */

/*----------------------------------------------------------------------
 * その他の CYW43/lwIP スタブ (WiFi 非使用時に必要)
 */
#if !CYW43_LWIP
void cyw43_cb_process_ethernet(void *cb_data, int itf,
                                size_t len, const uint8_t *buf)
{
    (void)cb_data; (void)itf; (void)len; (void)buf;
}

/* cyw43_ll_send_ethernet は pbuf_copy_partial を無条件に参照する。lwIP なしでは
 * is_pbuf=true の送信は発生しないため、リンクを通すだけの空実装を置く。 */
struct pbuf;
uint16_t pbuf_copy_partial(const struct pbuf *p, void *dataptr,
                           uint16_t len, uint16_t offset)
{
    (void)p; (void)dataptr; (void)len; (void)offset;
    return 0;
}
#endif

/* BT データ到着時の cyw43_ctrl コールバック。BLE 有効時も実処理は
 * hci_transport_cyw43 の poll で行うためスタブのまま。 */
void cyw43_bluetooth_hci_process(void) { }

#endif /* CPU_RP2040 */
