/*
 * kb_split.h — Split keyboard control layer (Layer 3)
 *
 * QMK split_util.h equivalent.
 * Manages master/slave detection, connection errors, and initialization.
 */

#ifndef __KB_SPLIT_H__
#define __KB_SPLIT_H__

#include <tk/tkernel.h>
#include "kb_config.h"
#include "kb_matrix.h"
#include "split_transport.h"

#if SPLIT_ENABLED

/*----------------------------------------------------------------------
 * Connection error throttling (QMK split_util.c equivalent)
 */
#ifndef SPLIT_MAX_CONNECTION_ERRORS
#define SPLIT_MAX_CONNECTION_ERRORS   10    /* Errors before marking disconnected */
#endif

#ifndef SPLIT_CONNECTION_CHECK_TIMEOUT
#define SPLIT_CONNECTION_CHECK_TIMEOUT 500  /* ms: retry interval when disconnected */
#endif

/*----------------------------------------------------------------------
 * Split keyboard API
 */

/* Is the transport connected to the slave? */
BOOL split_is_connected(void);

#if KB_IS_MASTER
/* Master: read slave matrix with error handling and throttling */
BOOL split_master_read(matrix_row_t *slave_matrix);
#else
/* Slave: run scan loop + update shared memory */
void split_slave_loop(void);
#endif

/* Initialize split keyboard (transport layer) */
void split_init(void);

#endif /* SPLIT_ENABLED */

#endif /* __KB_SPLIT_H__ */
