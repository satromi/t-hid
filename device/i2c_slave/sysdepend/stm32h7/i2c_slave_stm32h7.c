/*
 *----------------------------------------------------------------------
 *    Device Driver for μT-Kernel 3.0 BSP
 *
 *    I2C Slave — STM32H7 hardware-specific code
 *    STM32H723 Reference Manual RM0468 Section 52 (I2C)
 *
 *    STM32H7 I2C は DW_apb_i2c (RP2040) とは異なるアーキテクチャ。
 *    TXDR/RXDR レジスタ + ISR ステータスフラグ方式。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_STM32H7

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <sys/sysdef.h>
#include <string.h>

#include "../../i2c_slave.h"

/*----------------------------------------------------------------------
 * STM32H7 I2C register base addresses
 */
#define STM32_I2C1_BASE  0x40005400
#define STM32_I2C2_BASE  0x40005800

LOCAL const UW i2c_base[] = { STM32_I2C1_BASE, STM32_I2C2_BASE };

/* Register offsets (RM0468 Section 52.7) */
#define I2C_CR1         0x00    /* Control register 1 */
#define I2C_CR2         0x04    /* Control register 2 */
#define I2C_OAR1        0x08    /* Own address register 1 */
#define I2C_OAR2        0x0C    /* Own address register 2 */
#define I2C_TIMINGR     0x10    /* Timing register */
#define I2C_TIMEOUTR    0x14    /* Timeout register */
#define I2C_ISR         0x18    /* Interrupt and status register */
#define I2C_ICR         0x1C    /* Interrupt clear register */
#define I2C_RXDR        0x24    /* Receive data register */
#define I2C_TXDR        0x28    /* Transmit data register */

/* CR1 bits */
#define I2C_CR1_PE      (1u << 0)       /* Peripheral enable */
#define I2C_CR1_TXIE    (1u << 1)       /* TX interrupt enable */
#define I2C_CR1_RXIE    (1u << 2)       /* RX interrupt enable */
#define I2C_CR1_ADDRIE  (1u << 3)       /* Address match interrupt enable */
#define I2C_CR1_NACKIE  (1u << 4)       /* NACK received interrupt enable */
#define I2C_CR1_STOPIE  (1u << 5)       /* STOP detection interrupt enable */
#define I2C_CR1_ERRIE   (1u << 7)       /* Error interrupts enable */
#define I2C_CR1_SBC     (1u << 16)      /* Slave byte control */

/* OAR1 bits */
#define I2C_OAR1_OA1EN  (1u << 15)     /* Own address 1 enable */

/* ISR bits */
#define I2C_ISR_TXE     (1u << 0)       /* TX data register empty */
#define I2C_ISR_TXIS    (1u << 1)       /* TX interrupt status */
#define I2C_ISR_RXNE    (1u << 2)       /* RX data register not empty */
#define I2C_ISR_ADDR    (1u << 3)       /* Address matched */
#define I2C_ISR_NACKF   (1u << 4)       /* NACK received */
#define I2C_ISR_STOPF   (1u << 5)       /* STOP detected */
#define I2C_ISR_DIR     (1u << 16)      /* Transfer direction (0=write, 1=read) */

/* ICR bits (write 1 to clear) */
#define I2C_ICR_ADDRCF  (1u << 3)       /* Address matched clear */
#define I2C_ICR_NACKCF  (1u << 4)       /* NACK clear */
#define I2C_ICR_STOPCF  (1u << 5)       /* STOP clear */

/* IRQ numbers (STM32H723) */
#define INTNO_I2C1_EV   31
#define INTNO_I2C1_ER   32
#define INTNO_I2C2_EV   33
#define INTNO_I2C2_ER   34

/*----------------------------------------------------------------------
 * I2C Slave Interrupt Handler (Event)
 *
 * STM32H7 I2C スレーブプロトコル:
 *   ADDR → (RXNE × N | TXIS × N) → STOPF
 *   - ADDR: マスターがこのスレーブをアドレス指定した
 *   - RXNE: マスターからデータ受信 (write to slave)
 *   - TXIS: マスターへデータ送信要求 (read from slave)
 *   - STOPF: STOP 条件検出
 */
LOCAL void i2c_slave_ev_inthdr(UINT intno)
{
    (void)intno;
    T_I2CS_CB *cb = &i2cs_cb;
    UW base = i2c_base[cb->unit];
    UW isr = in_w(base + I2C_ISR);

    i2cs_isr_count++;

    /* ADDR: Address matched — determine direction */
    if (isr & I2C_ISR_ADDR) {
        cb->has_reg_set = FALSE;
        /* Clear ADDR flag to release SCL stretch */
        out_w(base + I2C_ICR, I2C_ICR_ADDRCF);

        /* If read direction (master reading), preload first byte */
        if (isr & I2C_ISR_DIR) {
            /* Flush TXDR and load data */
            out_w(base + I2C_ISR, I2C_ISR_TXE);	/* Set TXE to flush */
            if (cb->buffer_address < I2C_SLAVE_REG_COUNT)
                out_w(base + I2C_TXDR, i2c_slave_reg[cb->buffer_address]);
            else
                out_w(base + I2C_TXDR, 0xFF);
            cb->buffer_address++;
            i2cs_rd_req_count++;
        }
    }

    /* RXNE: Master wrote data to slave */
    if (isr & I2C_ISR_RXNE) {
        i2cs_rx_full_count++;
        UB data = (UB)(in_w(base + I2C_RXDR) & 0xFF);

        if (!cb->has_reg_set) {
            /* First byte = register address */
            cb->buffer_address = data;
            if (cb->buffer_address >= I2C_SLAVE_REG_COUNT)
                cb->buffer_address = 0;
            cb->has_reg_set = TRUE;
        } else {
            /* Subsequent bytes = data */
            if (cb->buffer_address < I2C_SLAVE_REG_COUNT) {
                i2c_slave_reg[cb->buffer_address] = data;
                cb->buffer_address++;
            }
        }
    }

    /* TXIS: Master requests more data (read from slave) */
    if (isr & I2C_ISR_TXIS) {
        i2cs_rd_req_count++;
        if (cb->buffer_address < I2C_SLAVE_REG_COUNT)
            out_w(base + I2C_TXDR, i2c_slave_reg[cb->buffer_address]);
        else
            out_w(base + I2C_TXDR, 0xFF);
        cb->buffer_address++;
    }

    /* STOPF: Transaction complete */
    if (isr & I2C_ISR_STOPF) {
        i2cs_stop_det_count++;
        out_w(base + I2C_ICR, I2C_ICR_STOPCF);

        if (cb->has_reg_set) {
            tk_set_flg(cb->flgid, I2CS_EVT_WRITE_COMPLETE);
        }
        cb->has_reg_set = FALSE;

        /* Flush TXDR for next transaction */
        out_w(base + I2C_ISR, I2C_ISR_TXE);
    }

    /* NACKF: NACK received (master ended read) */
    if (isr & I2C_ISR_NACKF) {
        i2cs_tx_abrt_count++;
        out_w(base + I2C_ICR, I2C_ICR_NACKCF);
    }
}

/*----------------------------------------------------------------------
 * STM32H7 I2C slave hardware initialization
 */
EXPORT ER i2c_slave_ll_init(UW unit, UB addr)
{
    if (unit > 1) return E_PAR;

    UW base = i2c_base[unit];
    UINT intno_ev = (unit == 0) ? INTNO_I2C1_EV : INTNO_I2C2_EV;

    /*
     * I2C クロックは hw_setting.c で有効化済み (RCC_APB1LENR.I2C1EN)
     * GPIO ピンも hw_setting.c で I2C AF に設定済み (PB8=SCL, PB9=SDA)
     */

    /* Disable I2C before configuration */
    out_w(base + I2C_CR1, 0);

    /*
     * Timing register (100kHz Standard mode with PCLK1 = 137.5MHz)
     * PRESC=0xD, SCLDEL=0x4, SDADEL=0x2, SCLH=0xAF, SCLL=0xD3
     * (Same as I2C master driver for STM32H7)
     */
    out_w(base + I2C_TIMINGR, 0xD04020AFu | (0xD3u << 0));

    /* Set own address (7-bit, OAR1) */
    out_w(base + I2C_OAR1, I2C_OAR1_OA1EN | ((UW)addr << 1));

    /* Disable own address 2 */
    out_w(base + I2C_OAR2, 0);

    /* Enable I2C + slave interrupts
     *   ADDRIE: Address match
     *   RXIE:   Receive data
     *   TXIE:   Transmit request
     *   STOPIE: Stop detection
     *   NACKIE: NACK detection
     */
    out_w(base + I2C_CR1,
          I2C_CR1_PE | I2C_CR1_ADDRIE | I2C_CR1_RXIE |
          I2C_CR1_TXIE | I2C_CR1_STOPIE | I2C_CR1_NACKIE);

    /* Flush TX data register */
    out_w(base + I2C_ISR, I2C_ISR_TXE);

    /* Register event interrupt handler */
    T_DINT dint;
    dint.intatr = TA_HLNG;
    dint.inthdr = (FP)i2c_slave_ev_inthdr;
    tk_def_int(intno_ev, &dint);
    EnableInt(intno_ev, 5);

    return E_OK;
}

EXPORT ER i2c_slave_ll_deinit(UW unit)
{
    if (unit > 1) return E_PAR;
    UW base = i2c_base[unit];
    out_w(base + I2C_CR1, 0);
    return E_OK;
}

#endif /* CPU_STM32H7 */
