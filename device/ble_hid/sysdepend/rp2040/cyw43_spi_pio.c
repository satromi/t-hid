/*
 * cyw43_spi_pio.c — CYW43 PIO-based half-duplex SPI HAL for RP2040
 *
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Pico W SPI バス:
 *   GP24 = DATA (bidirectional, PIO out/in/set pins)
 *   GP29 = CLK  (output only, PIO sideset)
 *   GP25 = CS   (active LOW, GPIO 直接制御)
 *   GP23 = WL_REG_ON (power enable, GPIO 直接制御)
 *
 * PIO プログラム (spi_gap01_sample0):
 *   sideset で GP29 に CLK を生成し、GP24 でデータを送受信。
 *   X レジスタ = TX ビット数 - 1、Y レジスタ = RX ビット数 - 1。
 *   TX: OSR → GP24 (MSB first, sideset で CLK トグル)
 *   RX: GP24 → ISR (MSB first, sideset で CLK トグル)
 *
 * DMA 転送 (RP2040 DMA CH10/CH11): bswap + DREQ ペース制御。
 *
 * ==== DMA メモリ保護 (将来の ARMv7-A 等への移植時に対応要) ====
 * RP2040 (Cortex-M0+, ARMv6-M) はキャッシュ/MMU なしのため、
 * DMA と CPU がメモリを共有しても整合性問題は発生しない。
 * ARMv7-A 系 (RZ/A2M 等) に移植する場合、以下の対応が必要:
 *   1. spi_header[] / spid_buf[] を含む cyw43_int_t 構造体を
 *      TTB_ATR_NORMAL_NOT_CACHE 属性のメモリ領域に配置 (リンカスクリプトで).
 *   2. または DMA 開始前に CleanDCache、完了後に InvalidateDCache を呼ぶ.
 *   3. DMA_MEMORY_BARRIER() は ARMv7-A では DMB 命令に展開されるため
 *      コンパイラ最適化だけでなくメモリ順序保証としても機能する.
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <sys/sysdef.h>

#include "cyw43.h"
#include "cyw43_internal.h"
#include "cyw43_spi.h"
#include "cyw43_spi_pio.h"
#include "../../cyw43_configport.h"

/*======================================================================
 * PIO プログラム: spi_gap01_sample0
 *
 * .side_set 1
 * lp:      out pins, 1     side 0    ; offset 0: DATA bit out, CLK=LOW
 *          jmp x-- lp      side 1    ; offset 1: CLK=HIGH, loop TX
 * lp1_end: set pindirs, 0  side 0    ; offset 2: DATA → input, CLK=LOW
 *          nop             side 1    ; offset 3: CLK=HIGH (gap)
 * lp2:     in pins, 1      side 0    ; offset 4: sample DATA, CLK=LOW
 *          jmp y-- lp2     side 1    ; offset 5: CLK=HIGH, loop RX
 * end:                               ; offset 6 (implicit)
 *
 * PIO 命令エンコーディング (.side_set 1):
 *   [15:13] = opcode, [12] = sideset, [11:8] = delay, [7:0] = operands
 *====================================================================*/
/*
 * spi_gap01_sample0 (TX と RX の間に 1 クロックの gap、立ち下がり後にサンプル)
 *
 * .side_set 1
 * lp:      out pins, 1     side 0    ; offset 0: DATA bit out, CLK=LOW
 *          jmp x-- lp      side 1    ; offset 1: CLK=HIGH, loop TX
 * lp1_end: set pindirs, 0  side 0    ; offset 2: DATA → input, CLK=LOW
 *          nop             side 1    ; offset 3: CLK=HIGH (gap)
 * lp2:     in pins, 1      side 0    ; offset 4: sample DATA, CLK=LOW
 *          jmp y-- lp2     side 1    ; offset 5: CLK=HIGH, loop RX
 * end:                               ; offset 6 (implicit)
 */
static const uint16_t spi_program[] = {
    0x6001,   /* 0: out pins, 1     side 0 */
    0x1040,   /* 1: jmp x-- 0       side 1 */
    0xE080,   /* 2: set pindirs, 0  side 0  (lp1_end) */
    0xB042,   /* 3: nop             side 1 */
    0x4001,   /* 4: in pins, 1      side 0 */
    0x1084,   /* 5: jmp y-- 4       side 1 */
};

#define SPI_PROGRAM_LEN     6
#define SPI_OFFSET_LP1_END  2    /* TX 部分のみの wrap 境界 */
#define SPI_OFFSET_END      6    /* プログラム全体の終端 */

/*----------------------------------------------------------------------
 * メモリバリア (DMA 転送前後)
 *
 * RP2040 (Cortex-M0+ / ARMv6-M) にはキャッシュがないため機能的には NOP だが、
 * コンパイラ最適化による DMA バッファへのアクセス並び替えを防止する。
 * ARMv7-A 系への移植時はキャッシュコヒーレンシ確保のため必須。
 */
#define DMA_MEMORY_BARRIER()   __asm__ volatile ("dmb" ::: "memory")

/*----------------------------------------------------------------------
 * busy_wait_cycles — 決定論的 CPU サイクル待ち (Cortex-M0+, 125MHz)
 *
 * subs+bcs の 4 サイクルループで
 * min_cycles 以上待機する。volatile ループ (コンパイラ最適化依存) より正確。
 * 100ns 目標 → min_cycles=12 (~128ns)。
 */
LOCAL __attribute__((always_inline)) void busy_wait_cycles(uint32_t min_cycles)
{
    __asm__ volatile (
        ".syntax unified\n"
        "1: subs %0, #3\n"
        "   bcs 1b\n"
        : "+r"(min_cycles) : : "cc"
    );
}

/*----------------------------------------------------------------------
 * DMA BUSY 待ちタイムアウト (μs). pio_sm_put_blocking 同様、DMA 停止時の
 * タスクハング防止。2KB 転送 ~65μs の 15倍マージン。
 */
#define DMA_WAIT_TIMEOUT_US    1000

/*----------------------------------------------------------------------
 * DMA バッファアライメント要件のコンパイル時検証
 */
_Static_assert(((uintptr_t)PIO_TXF0 & 3) == 0, "PIO_TXF0 must be 4-byte aligned");
_Static_assert(((uintptr_t)PIO_RXF0 & 3) == 0, "PIO_RXF0 must be 4-byte aligned");

/* PIO exec 用命令エンコーディング (sideset なし) */
#define PIO_ENCODE_OUT_X_32   0x6020   /* out x, 32 */
#define PIO_ENCODE_OUT_Y_32   0x6040   /* out y, 32 */
#define PIO_ENCODE_JMP_0      0x0000   /* jmp 0 */
#define PIO_ENCODE_SET_PINS_1 0xE001   /* set pins, 1 */
#define PIO_ENCODE_SET_PINS_0 0xE000   /* set pins, 0 */
#define PIO_ENCODE_MOV_PINS_NULL 0xA003 /* mov pins, null (data LOW) */

/*----------------------------------------------------------------------
 * PIO ヘルパー
 */
LOCAL void pio_sm_disable(void)
{
    out_w(CYW43_PIO_BASE + PIO_CTRL,
          in_w(CYW43_PIO_BASE + PIO_CTRL) & ~(1u << CYW43_PIO_SM));
}

LOCAL void pio_sm_enable(void)
{
    out_w(CYW43_PIO_BASE + PIO_CTRL,
          in_w(CYW43_PIO_BASE + PIO_CTRL) | (1u << CYW43_PIO_SM));
}

LOCAL void pio_sm_restart(void)
{
    /* SM restart + clkdiv restart */
    out_w(CYW43_PIO_BASE + PIO_CTRL,
          in_w(CYW43_PIO_BASE + PIO_CTRL)
          | (1u << (CYW43_PIO_SM + 4))    /* SM_RESTART */
          | (1u << (CYW43_PIO_SM + 8)));   /* CLKDIV_RESTART */
}

LOCAL void pio_sm_clear_fifos(void)
{
    /* SHIFTCTRL の FJOIN_RX ビットをトグルして FIFO をクリア */
    uint32_t sc = in_w(CYW43_PIO_BASE + PIO_SM0_SHIFTCTRL);
    out_w(CYW43_PIO_BASE + PIO_SM0_SHIFTCTRL, sc | (1u << 31));
    out_w(CYW43_PIO_BASE + PIO_SM0_SHIFTCTRL, sc);
}

LOCAL void pio_sm_exec(uint16_t instr)
{
    out_w(CYW43_PIO_BASE + PIO_SM0_INSTR, instr);
}

LOCAL void pio_sm_put_blocking(uint32_t data)
{
    /* X/Y レジスタロード用 (DMA 導入後は小さい転送のみ)。
     * タイムアウトガードで異常時の無限ループを防ぐ。 */
    uint32_t t0 = in_w(RP2040_TIMER_TIMERAWL);
    while (in_w(CYW43_PIO_BASE + PIO_FSTAT) &
           (1u << (PIO_FSTAT_TXFULL_SHIFT + CYW43_PIO_SM))) {
        if ((in_w(RP2040_TIMER_TIMERAWL) - t0) > PIO_FIFO_TIMEOUT_US) break;
    }
    out_w(CYW43_PIO_BASE + PIO_TXF0, data);
}

LOCAL void pio_sm_set_wrap(uint32_t bottom, uint32_t top)
{
    uint32_t ec = in_w(CYW43_PIO_BASE + PIO_SM0_EXECCTRL);
    ec &= ~((0x1Fu << 7) | (0x1Fu << 12));   /* WRAP_BOTTOM, WRAP_TOP クリア */
    ec |= (bottom << 7) | (top << 12);
    out_w(CYW43_PIO_BASE + PIO_SM0_EXECCTRL, ec);
}

/*----------------------------------------------------------------------
 * DMA ヘルパー (bswap 転送)
 */
LOCAL void dma_channel_abort(int ch)
{
    out_w(DMA_CH_CTRL(ch), 0);  /* EN=0 で停止 */
    /* BUSY ビットがクリアされるまで待つ (タイムアウトガード付き) */
    uint32_t t0 = in_w(RP2040_TIMER_TIMERAWL);
    while (in_w(DMA_CH_CTRL_TRIG(ch)) & DMA_CTRL_BUSY) {
        if ((in_w(RP2040_TIMER_TIMERAWL) - t0) > DMA_WAIT_TIMEOUT_US) break;
    }
}

LOCAL void dma_channel_wait(int ch)
{
    uint32_t t0 = in_w(RP2040_TIMER_TIMERAWL);
    while (in_w(DMA_CH_CTRL_TRIG(ch)) & DMA_CTRL_BUSY) {
        if ((in_w(RP2040_TIMER_TIMERAWL) - t0) > DMA_WAIT_TIMEOUT_US) break;
    }
}

LOCAL void pio_sm_set_data_pindirs(int output)
{
    /* SET PINDIRS 命令で GP24 の方向を変更 (SET_BASE=GP24) */
    if (output) {
        pio_sm_exec(0xE081);   /* set pindirs, 1 */
    } else {
        pio_sm_exec(0xE080);   /* set pindirs, 0 */
    }
}

/*----------------------------------------------------------------------
 * CS 制御
 */
LOCAL void cs_set(int value)
{
    if (value) {
        out_w(GPIO_OUT_SET, (1u << CYW43_PIN_CS));
    } else {
        out_w(GPIO_OUT_CLR, (1u << CYW43_PIN_CS));
    }
}

/*----------------------------------------------------------------------
 * SPI 通信開始/終了
 *
 * GP24 (DATA) と GP29 (CLK) を PIO 機能に切替え、CS をアサート。
 * 通信終了時は CS をデアサート (GP24 は GPIO に戻して IRQ 検出に使う)。
 *
 * WL_HOST_WAKE (GP24) IRQ の マスク/アンマスク:
 *   SPI 転送中は GP24 のデータビット変化で EDGE_HIGH がフラッドする
 *   (tk_wup_tsk 連打で SysTick が相対的に進まない現象) ため、転送前後で
 *   cyw43_irq_mask_for_spi/unmask で IRQ を抑止する。pending INTR の
 *   クリアは unmask 関数内で実施。
 */
extern void cyw43_irq_mask_for_spi(void);
extern void cyw43_irq_unmask_after_spi(void);

LOCAL void start_spi_comms(void)
{
    /* GP24 IRQ をマスク (SPI 転送中のフラッド抑止) */
    cyw43_irq_mask_for_spi();

    /* GP24 → PIO0 function (FUNCSEL=6) */
    out_w(IO_BANK0_BASE + 0x04 + 24 * 8, 6);
    /* GP29 → PIO0 function (FUNCSEL=6) */
    out_w(IO_BANK0_BASE + 0x04 + 29 * 8, 6);
    /* GP29 pull-down のみ設定 (12mA/fast-slew を上書きしない) */
    {
        UW pad29 = in_w(GPIO(29));
        pad29 = (pad29 & ~(GPIO_PUE | GPIO_PDE)) | GPIO_PDE;
        out_w(GPIO(29), pad29);
    }

    cs_set(0);  /* CS LOW = assert */
}

LOCAL void stop_spi_comms(void)
{
    cs_set(1);  /* CS HIGH = deassert */

    /* GP24 を SIO に戻さない (PIO のまま)。
     * GPIO_IN レジスタは FUNCSEL に関わらずピンの物理レベルを読めるため、
     * IRQ 検出 (cyw43_hal_pin_read) は PIO モードでも動作する。
     * CS deassert 後の IRQ ラインセトリング待ち (~128ns、目標 100ns)。
     * busy_wait_cycles はコンパイラ最適化に依存しない決定論的タイミング。 */
    busy_wait_cycles(12);

    /* SPI 転送中に発生した INTR edge latch をクリアしてから IRQ 再有効化 */
    cyw43_irq_unmask_after_spi();
}

/*======================================================================
 * cyw43_spi.h HAL 実装
 *====================================================================*/

/*----------------------------------------------------------------------
 * cyw43_spi_gpio_setup — GPIO ピン初期設定
 */
void cyw43_spi_gpio_setup(void)
{
    /* GP23 (WL_REG_ON): SIO 出力, pull-up, LOW (電源OFF) */
    out_w(IO_BANK0_BASE + 0x04 + 23 * 8, 5);   /* FUNCSEL=SIO */
    out_w(GPIO(23), GPIO_IE | GPIO_PUE | GPIO_DRIVE_4MA);
    out_w(GPIO_OE_SET, (1u << CYW43_PIN_ON));
    out_w(GPIO_OUT_CLR, (1u << CYW43_PIN_ON));

    /* GP24 (DATA): SIO 出力, LOW (初期状態) */
    out_w(IO_BANK0_BASE + 0x04 + 24 * 8, 5);   /* FUNCSEL=SIO */
    out_w(GPIO_OE_SET, (1u << CYW43_PIN_DATA));
    out_w(GPIO_OUT_CLR, (1u << CYW43_PIN_DATA));

    /* GP25 (CS): SIO 出力, HIGH (deassert) */
    out_w(IO_BANK0_BASE + 0x04 + 25 * 8, 5);   /* FUNCSEL=SIO */
    out_w(GPIO_OE_SET, (1u << CYW43_PIN_CS));
    cs_set(1);
}

/*----------------------------------------------------------------------
 * cyw43_spi_reset — CYW43 ハードウェアリセット
 *
 * LOW 20ms → HIGH → 250ms 待ち → WL_HOST_WAKE 再設定
 */
void cyw43_spi_reset(void)
{
    out_w(GPIO_OUT_CLR, (1u << CYW43_PIN_ON));   /* WL_REG_ON LOW */
    cyw43_delay_ms(20);
    out_w(GPIO_OUT_SET, (1u << CYW43_PIN_ON));   /* WL_REG_ON HIGH */
    cyw43_delay_ms(250);                          /* CYW43439 ブート待ち */

    /* GP24 (WL_HOST_WAKE) を SIO 入力に再設定 */
    out_w(IO_BANK0_BASE + 0x04 + 24 * 8, 5);    /* FUNCSEL=SIO */
    out_w(GPIO_OE_CLR, (1u << CYW43_PIN_IRQ));   /* input */
}

/*----------------------------------------------------------------------
 * cyw43_spi_init — PIO SPI 初期化
 */
int cyw43_spi_init(cyw43_int_t *self)
{
    (void)self;

    /* PIO0 リセット解除 */
    out_w(RESETS_BASE + 0x00,
          in_w(RESETS_BASE + 0x00) & ~(1u << 10));  /* RESET_PIO0 */
    while (!(in_w(RESETS_BASE + 0x08) & (1u << 10))) {}  /* RESET_DONE */

    /* DMA リセット解除 (bit 2) */
    out_w(RESETS_BASE + 0x00,
          in_w(RESETS_BASE + 0x00) & ~(1u << 2));   /* RESET_DMA */
    while (!(in_w(RESETS_BASE + 0x08) & (1u << 2))) {}  /* RESET_DONE */

    /* PIO プログラムをオフセット 0 にロード */
    {
        uint32_t i;
        for (i = 0; i < SPI_PROGRAM_LEN; i++) {
            out_w(CYW43_PIO_BASE + PIO_INSTR_MEM0 + i * 4, spi_program[i]);
        }
    }

    /* クロック分周 (125MHz / 2 = 62.5MHz PIO) */
    out_w(CYW43_PIO_BASE + PIO_SM0_CLKDIV,
          (CYW43_PIO_CLK_DIV_INT << 16) | (CYW43_PIO_CLK_DIV_FRAC << 8));

    /*
     * PINCTRL レジスタ:
     *   OUT_BASE     = 24 (GP24, DATA)
     *   SET_BASE     = 24 (GP24, pindirs 切替用)
     *   SIDESET_BASE = 29 (GP29, CLK)
     *   IN_BASE      = 24 (GP24, DATA)
     *   OUT_COUNT    = 1
     *   SET_COUNT    = 1
     *   SIDESET_COUNT= 1
     */
    out_w(CYW43_PIO_BASE + PIO_SM0_PINCTRL,
          (24u << 0)  |   /* OUT_BASE = GP24 */
          (24u << 5)  |   /* SET_BASE = GP24 */
          (29u << 10) |   /* SIDESET_BASE = GP29 */
          (24u << 15) |   /* IN_BASE = GP24 */
          (1u  << 20) |   /* OUT_COUNT = 1 */
          (1u  << 26) |   /* SET_COUNT = 1 */
          (1u  << 29));   /* SIDESET_COUNT = 1 */

    /*
     * EXECCTRL:
     *   SIDE_EN = 0 (sideset always applied)
     *   SIDE_PINDIR = 0 (sideset controls pin value, not direction)
     *   WRAP_BOTTOM = 0, WRAP_TOP = SPI_OFFSET_END - 1 (= 5)
     */
    out_w(CYW43_PIO_BASE + PIO_SM0_EXECCTRL,
          (0 << 7)                       |   /* WRAP_BOTTOM */
          ((SPI_OFFSET_END - 1) << 12));     /* WRAP_TOP = 5 */

    /*
     * SHIFTCTRL: MSB first, 32-bit, autopush/autopull
     *   IN_SHIFTDIR  = 0 (left/MSB first)
     *   OUT_SHIFTDIR = 0 (left/MSB first)
     *   AUTOPUSH = 1, PUSH_THRESH = 0 (= 32 bits)
     *   AUTOPULL = 1, PULL_THRESH = 0 (= 32 bits)
     */
    out_w(CYW43_PIO_BASE + PIO_SM0_SHIFTCTRL,
          (1u << 16) |   /* AUTOPUSH */
          (1u << 17) |   /* AUTOPULL */
          (0u << 20) |   /* PUSH_THRESH = 0 (= 32) */
          (0u << 25));   /* PULL_THRESH = 0 (= 32) */

    /* GP29 (CLK) を PIO0 に割当 */
    out_w(IO_BANK0_BASE + 0x04 + 29 * 8, 6);   /* FUNCSEL=PIO0 */

    /* PAD 設定 */
    out_w(GPIO(29), GPIO_DRIVE_12MA | GPIO_SLEWDAST | GPIO_SHEMITT | GPIO_IE);  /* CLK: 高駆動力 */
    out_w(GPIO(24), GPIO_PDE | GPIO_SHEMITT | GPIO_IE | GPIO_DRIVE_4MA);        /* DATA: pull-down + Schmitt */
    out_w(GPIO(25), GPIO_DRIVE_12MA | GPIO_SHEMITT | GPIO_IE);                  /* CS */

    /*
     * GP29 (CLK) を PIO pindirs で出力に設定。
     * PIO 制御下では GPIO_OE_SET は無効。PIO の pindirs で制御する。
     * SET PINDIRS 命令は SET_BASE のピンに作用するため、
     * PINCTRL の SET_BASE を一時的に GP29 に変更して exec する。
     */
    {
        uint32_t saved_pinctrl = in_w(CYW43_PIO_BASE + PIO_SM0_PINCTRL);

        /* SET_BASE = GP29, SET_COUNT = 1 に一時変更 */
        out_w(CYW43_PIO_BASE + PIO_SM0_PINCTRL,
              (saved_pinctrl & ~((0x1Fu << 5) | (0x7u << 26)))
              | (29u << 5) | (1u << 26));

        /* set pindirs, 1 (side 0) → GP29 を出力に */
        pio_sm_exec(0xE081);

        /* PINCTRL を復元 (SET_BASE = GP24) */
        out_w(CYW43_PIO_BASE + PIO_SM0_PINCTRL, saved_pinctrl);
    }

    /* GP24 (DATA) を PIO pindirs で出力に設定 + DATA HIGH 初期化 */
    pio_sm_exec(0xE081);  /* set pindirs, 1 (SET_BASE=GP24) */
    pio_sm_exec(PIO_ENCODE_SET_PINS_1);  /* set pins, 1 (DATA HIGH) */

    /* Input sync bypass for GP24 (PIO 入力レイテンシ削減) */
    out_w(CYW43_PIO_BASE + PIO_INPUT_SYNC_BYPASS,
          in_w(CYW43_PIO_BASE + PIO_INPUT_SYNC_BYPASS) | (1u << 24));

    return 0;
}

/*----------------------------------------------------------------------
 * cyw43_spi_deinit
 */
void cyw43_spi_deinit(cyw43_int_t *self)
{
    (void)self;
    pio_sm_disable();
}

/*----------------------------------------------------------------------
 * cyw43_spi_set_polarity — PIO 版では固定極性 (no-op)
 *
 * CYW43 ドライバが呼び出すが、PIO プログラムの極性は固定。
 */
void cyw43_spi_set_polarity(cyw43_int_t *self, int pol)
{
    (void)self; (void)pol;
}

/*----------------------------------------------------------------------
 * cyw43_spi_transfer — 半二重 SPI 転送
 *
 * CYW43 gSPI プロトコル:
 *   1. CS assert
 *   2. GP24/GP29 を PIO に切替
 *   3. X = TX ビット数 - 1, Y = RX ビット数 - 1 を PIO にロード
 *   4. PIO が TX → 方向切替 → RX を自律実行
 *   5. CS deassert
 *
 * tx/rx は 32-bit アライン、長さは 4 の倍数。
 * 同一バッファ (tx == rx) の場合: TX 部分を送信後、残りを受信。
 *
 * DMA bswap + DREQ ペース制御。
 */
int cyw43_spi_transfer(cyw43_int_t *self, const uint8_t *tx, size_t tx_length,
                       uint8_t *rx, size_t rx_length)
{
    (void)self;

    if (tx == NULL && rx == NULL) return -1;

    start_spi_comms();

    if (rx != NULL) {
        /*
         * TX+RX 複合転送:
         *   tx_length バイト送信 → (rx_length - tx_length) バイト受信
         *   単一 CS アサーション内で連続実行。
         */
        if (tx == NULL) {
            tx = rx;  /* tx==NULL なら rx バッファを TX にも使う */
        }

        uint32_t tx_words = tx_length / 4;
        uint32_t rx_words = (rx_length - tx_length) / 4;

        pio_sm_disable();
        pio_sm_set_wrap(0, SPI_OFFSET_END - 1);  /* 全プログラム */
        pio_sm_clear_fifos();
        pio_sm_set_data_pindirs(1);  /* DATA = output */
        pio_sm_restart();

        /* X = TX ビット数 - 1, Y = RX ビット数 - 1 */
        pio_sm_put_blocking(tx_length * 8 - 1);
        pio_sm_exec(PIO_ENCODE_OUT_X_32);
        pio_sm_put_blocking(rx_words > 0 ? (rx_length - tx_length) * 8 - 1 : 0);
        pio_sm_exec(PIO_ENCODE_OUT_Y_32);
        pio_sm_exec(PIO_ENCODE_JMP_0);

        /* DMA 転送: bswap + DREQ ペース制御 */
        dma_channel_abort(CYW43_DMA_CH_TX);
        dma_channel_abort(CYW43_DMA_CH_RX);

        /* DMA 開始前: TX バッファへの書込みを確実に完了させる (ARMv7-A 以上で必須) */
        DMA_MEMORY_BARRIER();

        /* DMA TX: メモリ→PIO TX FIFO, bswap, DREQ=PIO0_TX0 */
        out_w(DMA_CH_READ_ADDR(CYW43_DMA_CH_TX), (uint32_t)tx);
        out_w(DMA_CH_WRITE_ADDR(CYW43_DMA_CH_TX),
              CYW43_PIO_BASE + PIO_TXF0);
        out_w(DMA_CH_TRANS_COUNT(CYW43_DMA_CH_TX), tx_words);
        out_w(DMA_CH_CTRL_TRIG(CYW43_DMA_CH_TX),
              DMA_CTRL_EN | DMA_CTRL_DATA_SIZE_W |
              DMA_CTRL_INCR_READ |       /* メモリ側インクリメント */
              DMA_CTRL_BSWAP |           /* バイトスワップ */
              (DREQ_PIO0_TX0 << 15));    /* TREQ_SEL = PIO0 TX0 */

        /* DMA RX: PIO RX FIFO→メモリ, bswap, DREQ=PIO0_RX0 */
        if (rx_words > 0) {
            out_w(DMA_CH_READ_ADDR(CYW43_DMA_CH_RX),
                  CYW43_PIO_BASE + PIO_RXF0);
            out_w(DMA_CH_WRITE_ADDR(CYW43_DMA_CH_RX),
                  (uint32_t)(rx + tx_length));
            out_w(DMA_CH_TRANS_COUNT(CYW43_DMA_CH_RX), rx_words);
            out_w(DMA_CH_CTRL_TRIG(CYW43_DMA_CH_RX),
                  DMA_CTRL_EN | DMA_CTRL_DATA_SIZE_W |
                  DMA_CTRL_INCR_WRITE |      /* メモリ側インクリメント */
                  DMA_CTRL_BSWAP |           /* バイトスワップ */
                  (DREQ_PIO0_RX0 << 15));    /* TREQ_SEL = PIO0 RX0 */
        }

        pio_sm_enable();

        /* DMA 完了待ち */
        dma_channel_wait(CYW43_DMA_CH_TX);
        if (rx_words > 0) {
            dma_channel_wait(CYW43_DMA_CH_RX);
        }

        /* DMA 完了後: RX バッファ読出し前にバリアで整合性を保証 */
        DMA_MEMORY_BARRIER();

        /* TX 部分の rx バッファをゼロクリア */
        memset(rx, 0, tx_length);

    } else if (tx != NULL) {
        /*
         * TX のみ (書き込み操作)
         */
        uint32_t tx_words = tx_length / 4;

        pio_sm_disable();
        pio_sm_set_wrap(0, SPI_OFFSET_LP1_END - 1);  /* TX 部分のみ */
        pio_sm_clear_fifos();
        pio_sm_set_data_pindirs(1);  /* DATA = output */
        pio_sm_restart();

        pio_sm_put_blocking(tx_length * 8 - 1);
        pio_sm_exec(PIO_ENCODE_OUT_X_32);
        pio_sm_put_blocking(0);
        pio_sm_exec(PIO_ENCODE_OUT_Y_32);
        pio_sm_exec(PIO_ENCODE_JMP_0);

        /* DMA TX 転送 */
        dma_channel_abort(CYW43_DMA_CH_TX);

        /* DMA 開始前: TX バッファへの書込みを確実に完了 */
        DMA_MEMORY_BARRIER();

        out_w(DMA_CH_READ_ADDR(CYW43_DMA_CH_TX), (uint32_t)tx);
        out_w(DMA_CH_WRITE_ADDR(CYW43_DMA_CH_TX),
              CYW43_PIO_BASE + PIO_TXF0);
        out_w(DMA_CH_TRANS_COUNT(CYW43_DMA_CH_TX), tx_words);
        out_w(DMA_CH_CTRL_TRIG(CYW43_DMA_CH_TX),
              DMA_CTRL_EN | DMA_CTRL_DATA_SIZE_W |
              DMA_CTRL_INCR_READ | DMA_CTRL_BSWAP |
              (DREQ_PIO0_TX0 << 15));

        pio_sm_enable();

        /* DMA 完了待ち */
        dma_channel_wait(CYW43_DMA_CH_TX);

        /* TX 完了待ち (TXSTALL: FIFO→PIO shift 完了) */
        {
            uint32_t stall_mask = (1u << (PIO_FDEBUG_TXSTALL_LSB + CYW43_PIO_SM));
            out_w(CYW43_PIO_BASE + PIO_FDEBUG, stall_mask);
            while (!(in_w(CYW43_PIO_BASE + PIO_FDEBUG) & stall_mask)) {}
        }

        pio_sm_disable();
        pio_sm_set_data_pindirs(0);  /* DATA = input */
    }

    /* DATA ピンを LOW に設定 (次回の出力開始時のため) */
    pio_sm_exec(PIO_ENCODE_MOV_PINS_NULL);

    stop_spi_comms();
    return 0;
}

/*======================================================================
 * Register / Bytes 関数
 *
 * cyw43-driver の cyw43_spi.c (汎用 SPI 層) はバッファレイアウトが
 * PIO 実装と互換性がないため、PIO 用のバッファレイアウトで実装する。
 * cyw43_spi.c はビルドから除外し、ここで同名関数を提供する。
 *====================================================================*/

#include "cyw43_spi.h"  /* SPI_READ_TEST_REGISTER, BUS_FUNCTION 等 */

/* rev16 相当 (各16ビットハーフ内でバイトスワップ)
 * 0xAABBCCDD → 0xBBAADDCC
 * 旧実装 (16bit word swap) は誤り: CYW43 が SPI コマンドを解釈できない */
#define SWAP32(x) ((uint32_t)((((x) & 0xFF00FF00u) >> 8) | (((x) & 0x00FF00FFu) << 8)))

LOCAL uint32_t make_cmd(int write, int inc, uint32_t fn, uint32_t addr, uint32_t sz)
{
    return ((uint32_t)write << 31) | ((uint32_t)inc << 30) |
           (fn << 28) | ((addr & 0x1FFFF) << 11) | sz;
}

uint32_t read_reg_u32_swap(cyw43_int_t *self, uint32_t fn, uint32_t reg)
{
    uint32_t buf[2] = {0};
    buf[0] = SWAP32(make_cmd(0, 1, fn, reg, 4));
    int ret = cyw43_spi_transfer(self, NULL, 4, (uint8_t *)buf, 8);
    if (ret != 0) return (uint32_t)ret;
    return SWAP32(buf[1]);
}

int write_reg_u32_swap(cyw43_int_t *self, uint32_t fn, uint32_t reg, uint32_t val)
{
    uint32_t buf[2];
    buf[0] = SWAP32(make_cmd(1, 1, fn, reg, 4));
    buf[1] = SWAP32(val);
    return cyw43_spi_transfer(self, (uint8_t *)buf, 8, NULL, 0);
}

/* rx バッファは常に buf32[0] から開始。
 * tx=NULL 時、spi_transfer は tx=rx として buf32[0] のコマンドを送信し、
 * レスポンスを buf32[1] (non-BACKPLANE) または buf32[index-1] (BACKPLANE) に格納。*/
uint32_t cyw43_read_reg_u32(cyw43_int_t *self, uint32_t fn, uint32_t reg)
{
    int index = (CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4) + 1 + 1;
    uint32_t buf32[index];
    uint32_t padding = (fn == BACKPLANE_FUNCTION) ? CYW43_BACKPLANE_READ_PAD_LEN_BYTES : 0;
    buf32[0] = make_cmd(0, 1, fn, reg, 4);
    int ret = cyw43_spi_transfer(self, NULL, 4, (uint8_t *)buf32, 8 + padding);
    if (ret != 0) return (uint32_t)ret;
    return buf32[padding > 0 ? index - 1 : 1];
}

int cyw43_read_reg_u16(cyw43_int_t *self, uint32_t fn, uint32_t reg)
{
    int index = (CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4) + 1 + 1;
    uint32_t buf32[index];
    uint32_t padding = (fn == BACKPLANE_FUNCTION) ? CYW43_BACKPLANE_READ_PAD_LEN_BYTES : 0;
    buf32[0] = make_cmd(0, 1, fn, reg, 2);
    int ret = cyw43_spi_transfer(self, NULL, 4, (uint8_t *)buf32, 8 + padding);
    if (ret != 0) return ret;
    return (int)buf32[padding > 0 ? index - 1 : 1];
}

int cyw43_read_reg_u8(cyw43_int_t *self, uint32_t fn, uint32_t reg)
{
    int index = (CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4) + 1 + 1;
    uint32_t buf32[index];
    uint32_t padding = (fn == BACKPLANE_FUNCTION) ? CYW43_BACKPLANE_READ_PAD_LEN_BYTES : 0;
    buf32[0] = make_cmd(0, 1, fn, reg, 1);
    int ret = cyw43_spi_transfer(self, NULL, 4, (uint8_t *)buf32, 8 + padding);
    if (ret != 0) return ret;
    return (int)buf32[padding > 0 ? index - 1 : 1];
}

int cyw43_write_reg_u32(cyw43_int_t *self, uint32_t fn, uint32_t reg, uint32_t val)
{
    uint32_t buf[2];
    buf[0] = make_cmd(1, 1, fn, reg, 4);
    buf[1] = val;
    return cyw43_spi_transfer(self, (uint8_t *)buf, 8, NULL, 0);
}

int cyw43_write_reg_u16(cyw43_int_t *self, uint32_t fn, uint32_t reg, uint16_t val)
{
    uint32_t buf[2];
    buf[0] = make_cmd(1, 1, fn, reg, 2);
    buf[1] = (uint32_t)val;
    return cyw43_spi_transfer(self, (uint8_t *)buf, 8, NULL, 0);
}

int cyw43_write_reg_u8(cyw43_int_t *self, uint32_t fn, uint32_t reg, uint32_t val)
{
    uint32_t buf[2];
    buf[0] = make_cmd(1, 1, fn, reg, 1);
    buf[1] = val;
    return cyw43_spi_transfer(self, (uint8_t *)buf, 8, NULL, 0);
}

int cyw43_read_bytes(cyw43_int_t *self, uint32_t fn, uint32_t addr, size_t len, uint8_t *buf)
{
    uint32_t padding = (fn == BACKPLANE_FUNCTION) ? CYW43_BACKPLANE_READ_PAD_LEN_BYTES : 0;
    size_t aligned_len = (len + 3) & ~3u;
    self->spi_header[padding > 0 ? 0 : (CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4)] =
        make_cmd(0, 1, fn, addr, len);

    int ret = cyw43_spi_transfer(self, NULL, 4,
        (uint8_t *)&self->spi_header[padding > 0 ? 0 : (CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4)],
        aligned_len + 4 + padding);

    if (ret != 0) return ret;

    if (buf != self->spid_buf) {
        memcpy(buf, self->spid_buf, len);
    }
    return 0;
}

int cyw43_write_bytes(cyw43_int_t *self, uint32_t fn, uint32_t addr, size_t len, const uint8_t *src)
{
    size_t aligned_len = (len + 3) & ~3u;

    /* WLAN F2 FIFO の ready 待ち */
    if (fn == WLAN_FUNCTION) {
        int f2_ready_attempts = 1000;
        while (f2_ready_attempts-- > 0) {
            uint32_t bus_status = cyw43_read_reg_u32(self, BUS_FUNCTION, SPI_STATUS_REGISTER);
            if (bus_status & STATUS_F2_RX_READY) {
                break;
            }
        }
        if (f2_ready_attempts <= 0) {
            CYW43_PRINTF("F2 not ready\n");
            return -CYW43_EIO;
        }
    }

    if (src == self->spid_buf) {
        self->spi_header[(CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4)] =
            make_cmd(1, 1, fn, addr, len);
        return cyw43_spi_transfer(self,
            (uint8_t *)&self->spi_header[(CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4)],
            aligned_len + 4, NULL, 0);
    } else {
        self->spi_header[(CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4)] =
            make_cmd(1, 1, fn, addr, len);
        memcpy(self->spid_buf, src, len);
        return cyw43_spi_transfer(self,
            (uint8_t *)&self->spi_header[(CYW43_BACKPLANE_READ_PAD_LEN_BYTES / 4)],
            aligned_len + 4, NULL, 0);
    }
}

#endif /* CPU_RP2040 */
