/*
 * kb_matrix.h — Key matrix scanner API
 */

#ifndef __KB_MATRIX_H__
#define __KB_MATRIX_H__

#include <tk/tkernel.h>
#include "kb_config.h"

/* Raw matrix state: 1 bit per key per row */
typedef struct {
    UB rows[MATRIX_ROWS_PER_HAND];   /* bit 0..3 = col 0..3 */
} T_MATRIX_STATE;

/* Debounce state */
typedef struct {
    UB counter[MATRIX_ROWS_PER_HAND][MATRIX_COLS_PER_HAND];
    T_MATRIX_STATE current;    /* Debounced state */
    T_MATRIX_STATE previous;   /* Previous debounced (for edge detection) */
} T_DEBOUNCE_STATE;

/* Initialize GPIO pins for matrix scanning */
void matrix_init(void);

/* Scan the local matrix (fills raw state) */
void matrix_scan(T_MATRIX_STATE *raw);

/* Apply debounce filter */
void matrix_debounce(T_DEBOUNCE_STATE *db, const T_MATRIX_STATE *raw);

/* Check if debounced state changed since last scan */
BOOL matrix_changed(const T_DEBOUNCE_STATE *db);

/* Check if a specific key is pressed in matrix state */
static inline BOOL matrix_is_pressed(const T_MATRIX_STATE *m, UINT row, UINT col) {
    return (m->rows[row] >> col) & 1;
}

#endif /* __KB_MATRIX_H__ */
