/*
 *----------------------------------------------------------------------
 *    Device Driver for μT-Kernel 3.0 BSP
 *
 *    I2C Slave Device Driver — Common code
 *
 *    Implements a register array model (QMK i2c_slave.c equivalent).
 *    Hardware-specific code is in sysdepend/<cpu>/i2c_slave_<cpu>.c
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(CPU_STM32H7)

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "i2c_slave.h"

/*----------------------------------------------------------------------
 * Shared register array — the core of the slave model
 */
volatile UB i2c_slave_reg[I2C_SLAVE_REG_COUNT];

/*----------------------------------------------------------------------
 * Driver state
 */
T_I2CS_CB i2cs_cb;

/* Debug counters */
volatile UW i2cs_isr_count = 0;
volatile UW i2cs_rd_req_count = 0;
volatile UW i2cs_rx_full_count = 0;
volatile UW i2cs_stop_det_count = 0;
volatile UW i2cs_tx_abrt_count = 0;

/*----------------------------------------------------------------------
 * Interrupt-safe register array access (for local task use)
 */
EXPORT void i2c_slave_reg_write(UW offset, const void *data, UW size)
{
    if (offset + size > I2C_SLAVE_REG_COUNT) return;
    UINT imask;
    DI(imask);
    memcpy((void *)&i2c_slave_reg[offset], data, size);
    EI(imask);
}

EXPORT void i2c_slave_reg_read(UW offset, void *data, UW size)
{
    if (offset + size > I2C_SLAVE_REG_COUNT) return;
    UINT imask;
    DI(imask);
    memcpy(data, (const void *)&i2c_slave_reg[offset], size);
    EI(imask);
}

EXPORT ID i2c_slave_get_flgid(void)
{
    return i2cs_cb.flgid;
}

EXPORT void i2c_slave_get_stats(T_I2CS_STATS *stats)
{
    if (stats == NULL) return;
    stats->isr_count      = i2cs_isr_count;
    stats->rd_req_count   = i2cs_rd_req_count;
    stats->rx_full_count  = i2cs_rx_full_count;
    stats->stop_det_count = i2cs_stop_det_count;
    stats->tx_abrt_count  = i2cs_tx_abrt_count;
}

/*----------------------------------------------------------------------
 * Initialize I2C slave device
 *
 * Common initialization + HAL-specific hardware setup
 */
EXPORT ER dev_init_i2c_slave(UW unit)
{
    T_I2CS_CB *cb = &i2cs_cb;
    ER err;

    /* Initialize control block */
    cb->unit = unit;
    cb->buffer_address = 0;
    cb->has_reg_set = FALSE;

    /* Clear register array */
    memset((void *)i2c_slave_reg, 0, I2C_SLAVE_REG_COUNT);

    /* Create event flag for write notification */
    T_CFLG cflg;
    cflg.exinf  = NULL;
    cflg.flgatr = TA_TPRI | TA_WMUL;
    cflg.iflgptn = 0;
    cb->flgid = tk_cre_flg(&cflg);
    if (cb->flgid <= 0) return E_NOMEM;

    /* Hardware-specific initialization (sysdepend) */
    err = i2c_slave_ll_init(unit, I2C_SLAVE_ADDRESS);
    if (err != E_OK) {
        tk_del_flg(cb->flgid);
        return err;
    }

    tm_printf((UB *)"I2C Slave: unit=%d addr=0x%02x regs=%d bytes\n",
              unit, I2C_SLAVE_ADDRESS, I2C_SLAVE_REG_COUNT);

    return E_OK;
}

#endif /* CPU_RP2040 || CPU_STM32H7 */
