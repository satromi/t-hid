/*
 * split_transport.h — Split keyboard transport layer
 *
 * QMK transport.h equivalent.
 * Defines the shared memory structure between master and slave halves.
 * The structure is overlaid on the I2C slave register array.
 */

#ifndef __SPLIT_TRANSPORT_H__
#define __SPLIT_TRANSPORT_H__

#include <stddef.h>     /* offsetof() */
#include <tk/typedef.h>
#include "kb_config.h"

/*----------------------------------------------------------------------
 * Matrix row type: one byte per row, bits represent columns
 */
typedef UB matrix_row_t;

/*----------------------------------------------------------------------
 * Shared memory structure (QMK split_shared_memory_t equivalent)
 *
 * This structure is directly mapped to i2c_slave_reg[].
 * Master reads/writes specific offsets via I2C.
 */
typedef struct {
    /* --- slave→master: sub side writes, main side reads --- */
    UB  checksum;                                    /* CRC8 of smatrix[] */
    matrix_row_t smatrix[MATRIX_ROWS_PER_HAND];      /* Sub-side key matrix */

    /* --- master→slave: main side writes, sub side reads --- */
    UB  led_state;                                   /* LED sync (Caps/Num Lock) */

    /* --- control --- */
    UB  alive_counter;                               /* Incremented by sub, read by main */
} split_shared_memory_t;

/* Verify it fits in the I2C slave register array */
_Static_assert(sizeof(split_shared_memory_t) <= 64,
               "split_shared_memory_t exceeds I2C_SLAVE_REG_COUNT");

/*----------------------------------------------------------------------
 * Convenience offset macros for I2C register access
 */
#define SHMEM_OFF_CHECKSUM    offsetof(split_shared_memory_t, checksum)
#define SHMEM_OFF_SMATRIX     offsetof(split_shared_memory_t, smatrix)
#define SHMEM_OFF_LED_STATE   offsetof(split_shared_memory_t, led_state)
#define SHMEM_OFF_ALIVE       offsetof(split_shared_memory_t, alive_counter)

#define SHMEM_SIZE_SMATRIX    sizeof(((split_shared_memory_t*)0)->smatrix)

/*----------------------------------------------------------------------
 * Transport API
 *
 * Master/slave 両方の宣言を常に公開する。
 * 実体は split_transport.c 内で #if KB_IS_MASTER により片方だけコンパイルされる。
 */

/* Initialize transport (called once at startup) */
void transport_init(void);

/* Master: read slave matrix from I2C (returns E_OK on success) */
ER transport_master_read_matrix(matrix_row_t *slave_matrix);

/* Slave: update local matrix in shared memory */
void transport_slave_update_matrix(const matrix_row_t *matrix);

/*----------------------------------------------------------------------
 * CRC8 checksum (QMK crc.c equivalent)
 */
UB crc8(const void *data, UW len);

#endif /* __SPLIT_TRANSPORT_H__ */
