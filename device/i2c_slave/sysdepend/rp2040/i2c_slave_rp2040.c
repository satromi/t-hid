/*
 *----------------------------------------------------------------------
 *    Device Driver for μT-Kernel 3.0 BSP
 *
 *    I2C Slave — RP2040 hardware-specific code
 *    RP2040 Datasheet Section 4.3 I2C
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <sys/sysdef.h>
#include <string.h>

#include "../../i2c_slave.h"

/*----------------------------------------------------------------------
 * RP2040 I2C register offsets
 */
#define I2C0_BASE   0x40044000
#define I2C1_BASE   0x40048000

LOCAL const UW i2c_base[] = { I2C0_BASE, I2C1_BASE };

#define REG_CON         0x00
#define REG_SAR         0x08
#define REG_DATA_CMD    0x10
#define REG_INTR_STAT   0x2C
#define REG_INTR_MASK   0x30
#define REG_RX_TL       0x38
#define REG_TX_TL       0x3C
#define REG_CLR_INTR    0x40
#define REG_CLR_RD_REQ  0x50
#define REG_CLR_TX_ABRT 0x54
#define REG_CLR_RX_DONE 0x58
#define REG_CLR_STOP_DET 0x60
#define REG_CLR_START_DET 0x64
#define REG_ENABLE      0x6C
#define REG_STATUS      0x70
#define REG_SDA_HOLD    0x7C

/* I2C_CON bits */
#define CON_TX_EMPTY_CTRL       (1 << 8)
#define CON_STOP_DET_IFADDR     (1 << 7)
#define CON_SPEED_FAST          (2 << 1)
#define CON_7BIT_ADR            (0 << 4)

/* Interrupt bits */
#define INT_RD_REQ         (1 << 5)
#define INT_RX_FULL        (1 << 2)
#define INT_STOP_DET       (1 << 9)
#define INT_TX_ABRT        (1 << 6)

/* IRQ numbers */
#define INTNO_I2C0_RP      23
#define INTNO_I2C1_RP      24

/*----------------------------------------------------------------------
 * I2C Slave Interrupt Handler
 */
LOCAL void i2c_slave_inthdr(UINT intno)
{
    (void)intno;
    T_I2CS_CB *cb = &i2cs_cb;
    UW base = i2c_base[cb->unit];
    UW stat = in_w(base + REG_INTR_STAT);

    i2cs_isr_count++;

    /* RX_FULL: Master sent data */
    if (stat & INT_RX_FULL) {
        i2cs_rx_full_count++;
        UW data = in_w(base + REG_DATA_CMD) & 0xFF;

        if (!cb->has_reg_set) {
            cb->buffer_address = (UB)data;
            if (cb->buffer_address >= I2C_SLAVE_REG_COUNT)
                cb->buffer_address = 0;
            cb->has_reg_set = TRUE;
        } else {
            if (cb->buffer_address < I2C_SLAVE_REG_COUNT) {
                i2c_slave_reg[cb->buffer_address] = (UB)data;
                cb->buffer_address++;
            }
        }
    }

    /* RD_REQ: Master requests data */
    if (stat & INT_RD_REQ) {
        i2cs_rd_req_count++;
        if (cb->buffer_address < I2C_SLAVE_REG_COUNT)
            out_w(base + REG_DATA_CMD, (UW)i2c_slave_reg[cb->buffer_address]);
        else
            out_w(base + REG_DATA_CMD, 0xFF);
        cb->buffer_address++;
        in_w(base + REG_CLR_RD_REQ);
    }

    /* STOP_DET: Transaction complete */
    if (stat & INT_STOP_DET) {
        i2cs_stop_det_count++;
        if (cb->has_reg_set)
            tk_set_flg(cb->flgid, I2CS_EVT_WRITE_COMPLETE);
        cb->has_reg_set = FALSE;
        in_w(base + REG_CLR_STOP_DET);
    }

    /* TX_ABRT: Clear abort */
    if (stat & INT_TX_ABRT) {
        i2cs_tx_abrt_count++;
        in_w(base + REG_CLR_TX_ABRT);
    }
}

/*----------------------------------------------------------------------
 * RP2040 I2C slave hardware initialization
 */
EXPORT ER i2c_slave_ll_init(UW unit, UB addr)
{
    if (unit > 1) return E_PAR;

    UW base = i2c_base[unit];
    UINT intno = (unit == 0) ? INTNO_I2C0_RP : INTNO_I2C1_RP;

    /* Release I2C peripheral from reset */
    UW reset_bit = (unit == 0) ? RESETS_RESET_I2C0 : RESETS_RESET_I2C1;
    set_w(RESETS_RESET, reset_bit);
    clr_w(RESETS_RESET, reset_bit);
    while ((in_w(RESETS_RESET_DONE) & reset_bit) == 0) {}

    /* Disable I2C before configuration */
    out_w(base + REG_ENABLE, 0);

    /* Configure as SLAVE mode */
    out_w(base + REG_CON,
          CON_SPEED_FAST | CON_7BIT_ADR | CON_TX_EMPTY_CTRL
          | CON_STOP_DET_IFADDR);

    /* Set slave address */
    out_w(base + REG_SAR, addr);

    /* FIFO thresholds */
    out_w(base + REG_RX_TL, 0);
    out_w(base + REG_TX_TL, 0);

    /* SDA hold time */
    out_w(base + REG_SDA_HOLD, 38);

    /* Mask all interrupts before enabling */
    out_w(base + REG_INTR_MASK, 0);

    /* Enable I2C */
    out_w(base + REG_ENABLE, 1);

    /* Clear pending interrupts */
    in_w(base + REG_CLR_INTR);

    /* Set interrupt mask */
    out_w(base + REG_INTR_MASK,
          INT_RD_REQ | INT_RX_FULL | INT_STOP_DET | INT_TX_ABRT);

    /* Register interrupt handler */
    T_DINT dint;
    dint.intatr = TA_HLNG;
    dint.inthdr = (FP)i2c_slave_inthdr;
    tk_def_int(intno, &dint);

    in_w(base + REG_CLR_INTR);
    ClearInt(intno);
    EnableInt(intno, 2);

    return E_OK;
}

EXPORT ER i2c_slave_ll_deinit(UW unit)
{
    if (unit > 1) return E_PAR;
    UW base = i2c_base[unit];
    out_w(base + REG_ENABLE, 0);
    return E_OK;
}

#endif /* CPU_RP2040 */
