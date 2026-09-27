/*
 *----------------------------------------------------------------------
 *    Device Driver for μT-Kernel 3.0 BSP
 *
 *    I2C Slave — Internal driver header
 *    Defines the HAL interface between common code and sysdepend code.
 *----------------------------------------------------------------------
 */

#ifndef __I2C_SLAVE_INT_H__
#define __I2C_SLAVE_INT_H__

#include <tk/tkernel.h>
#include "../include/dev_i2c_slave.h"

/*----------------------------------------------------------------------
 * Driver state (shared between common and sysdepend code)
 */
typedef struct {
    UW   unit;              /* I2C unit number */
    UB   buffer_address;    /* Current register offset (auto-increment) */
    BOOL has_reg_set;       /* First byte (register address) received? */
    ID   flgid;             /* Event flag ID for write notification */
} T_I2CS_CB;

extern T_I2CS_CB i2cs_cb;

/* Debug counters */
extern volatile UW i2cs_isr_count;
extern volatile UW i2cs_rd_req_count;
extern volatile UW i2cs_rx_full_count;
extern volatile UW i2cs_stop_det_count;
extern volatile UW i2cs_tx_abrt_count;

/*----------------------------------------------------------------------
 * HAL functions (implemented in sysdepend/<cpu>/i2c_slave_<cpu>.c)
 */

/* Configure I2C hardware in slave mode and register ISR
 *   unit:  I2C peripheral unit number
 *   addr:  7-bit slave address
 *   return E_OK on success
 */
IMPORT ER i2c_slave_ll_init(UW unit, UB addr);

/* Disable I2C slave hardware */
IMPORT ER i2c_slave_ll_deinit(UW unit);

#endif /* __I2C_SLAVE_INT_H__ */
