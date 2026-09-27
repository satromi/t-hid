/*
 *----------------------------------------------------------------------
 *    Device Driver for μT-Kernel 3.0 BSP
 *
 *    I2C Slave Device Driver — Public API Header
 *
 *    Implements a QMK-style register array model:
 *    A shared memory region (i2c_slave_reg[]) is exposed via I2C.
 *    Master can read/write any offset in this array.
 *----------------------------------------------------------------------
 */

#ifndef __DEV_I2C_SLAVE_H__
#define __DEV_I2C_SLAVE_H__

#include <tk/typedef.h>

/*----------------------------------------------------------------------
 * Configuration
 */
#ifndef I2C_SLAVE_REG_COUNT
#define I2C_SLAVE_REG_COUNT   64    /* Default register array size (bytes) */
#endif

#ifndef I2C_SLAVE_ADDRESS
#define I2C_SLAVE_ADDRESS     0x32  /* Default 7-bit slave address */
#endif

/* Alias for compatibility with kb_config.h naming */
#ifndef SPLIT_I2C_ADDRESS
#define SPLIT_I2C_ADDRESS     I2C_SLAVE_ADDRESS
#endif

/*----------------------------------------------------------------------
 * Device name: "i2cs" (I2C Slave)
 */
#define I2C_SLAVE_DEVNM       "i2cs"

/*----------------------------------------------------------------------
 * Attribute data numbers for tk_swri_dev / tk_srea_dev
 */
typedef enum {
    TDN_I2CS_REGDATA  = -100,  /* Direct register array read/write */
} T_DN_I2CS_ATR;

/*----------------------------------------------------------------------
 * Device initialization
 *   unit: I2C peripheral unit (0=I2C0, 1=I2C1)
 */
IMPORT ER dev_init_i2c_slave(UW unit);

/*----------------------------------------------------------------------
 * Direct access to register array (for local task use)
 *
 * The slave ISR reads/writes this array in response to master requests.
 * Local tasks can also read/write directly (with interrupt protection).
 */
extern volatile UB i2c_slave_reg[I2C_SLAVE_REG_COUNT];

/* Write to register array (interrupt-safe) */
IMPORT void i2c_slave_reg_write(UW offset, const void *data, UW size);

/* Read from register array (interrupt-safe) */
IMPORT void i2c_slave_reg_read(UW offset, void *data, UW size);

/*----------------------------------------------------------------------
 * Event notification
 *
 * When master writes to the register array, an event flag is set.
 * Upper layers can wait on this to detect incoming data.
 */
#define I2CS_EVT_WRITE_COMPLETE   (1 << 0)  /* Master completed a write */

/* Get the event flag ID (valid after dev_init_i2c_slave) */
IMPORT ID i2c_slave_get_flgid(void);

/*----------------------------------------------------------------------
 * Debug statistics
 */
typedef struct {
    UW  isr_count;      /* Total ISR invocations */
    UW  rd_req_count;   /* Master read requests */
    UW  rx_full_count;  /* Master write (data received) */
    UW  stop_det_count; /* STOP condition count */
    UW  tx_abrt_count;  /* TX abort / NACK count */
} T_I2CS_STATS;

/* Get debug statistics snapshot */
IMPORT void i2c_slave_get_stats(T_I2CS_STATS *stats);

#endif /* __DEV_I2C_SLAVE_H__ */
