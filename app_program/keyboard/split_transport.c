/*
 * split_transport.c — Split keyboard transport layer
 *
 * QMK transport.c equivalent.
 *
 * 3 つのトランスポートモードをサポート:
 *   SPLIT_TRANSPORT_I2C  — I2C ケーブル接続 (既存, デフォルト)
 *   SPLIT_TRANSPORT_BLE  — BLE ワイヤレス (Pico W)
 *   SPLIT_TRANSPORT_AUTO — I2C を試行し、失敗したら BLE にフォールバック
 *
 * ビルド時に makefile から -DSPLIT_TRANSPORT_xxx で選択。
 * 未定義の場合は I2C がデフォルト。
 */

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "split_transport.h"
#include "kb_config.h"

/*----------------------------------------------------------------------
 * CRC8 (polynomial 0x31, initial value 0xFF)
 */
UB crc8(const void *data, UW len)
{
    const UB *d = (const UB *)data;
    UB crc = 0xFF;
    UW i, j;

    for (i = 0; i < len; i++) {
        crc ^= d[i];
        for (j = 0; j < 8; j++) {
            if (crc & 0x80)
                crc = (UB)((crc << 1) ^ 0x31);
            else
                crc <<= 1;
        }
    }
    return crc;
}

/*======================================================================
 * I2C トランスポート関数 (Master/Slave 共通部品)
 * AUTO モードでも使うため、SPLIT_TRANSPORT_BLE 以外でコンパイル。
 *====================================================================*/
#if !defined(SPLIT_TRANSPORT_BLE)

/* ---- I2C Master ---- */
#if KB_IS_MASTER
#include "dev_i2c.h"

LOCAL ID i2c_dd = -1;

LOCAL ER i2c_transport_init(void)
{
    i2c_dd = tk_opn_dev((UB *)SPLIT_I2C_DEVNM, TD_UPDATE);
    if (i2c_dd < E_OK) {
        tm_printf((UB *)"Transport: I2C open failed: %d\n", i2c_dd);
        return E_IO;
    }
    tm_printf((UB *)"Transport: I2C master init OK\n");
    return E_OK;
}

LOCAL ER i2c_transport_read_matrix(matrix_row_t *slave_matrix)
{
    T_I2C_EXEC exec;
    SZ asz;
    ER err;
    UB reg_addr;
    UB checksum_remote;
    UB buf[MATRIX_ROWS_PER_HAND];

    if (i2c_dd < E_OK) return E_IO;

    reg_addr = SHMEM_OFF_CHECKSUM;
    exec.sadr     = SPLIT_I2C_ADDRESS;
    exec.snd_size = 1;
    exec.snd_data = &reg_addr;
    exec.rcv_size = 1;
    exec.rcv_data = &checksum_remote;

    err = tk_swri_dev(i2c_dd, TDN_I2C_EXEC, &exec, sizeof(T_I2C_EXEC), &asz);
    if (err < E_OK) return err;

    reg_addr = SHMEM_OFF_SMATRIX;
    exec.snd_data = &reg_addr;
    exec.rcv_size = MATRIX_ROWS_PER_HAND;
    exec.rcv_data = buf;

    err = tk_swri_dev(i2c_dd, TDN_I2C_EXEC, &exec, sizeof(T_I2C_EXEC), &asz);
    if (err < E_OK) return err;

    UB checksum_calc = crc8(buf, MATRIX_ROWS_PER_HAND);
    if (checksum_calc != checksum_remote) return E_IO;

    memcpy(slave_matrix, buf, MATRIX_ROWS_PER_HAND);
    return E_OK;
}

#else /* ---- I2C Slave ---- */
#include "dev_i2c_slave.h"

#define split_shmem  ((volatile split_shared_memory_t *)i2c_slave_reg)

LOCAL ER i2c_transport_slave_init(void)
{
    ER err = dev_init_i2c_slave(0);
    if (err < E_OK) {
        tm_printf((UB *)"Transport: I2C slave init failed: %d\n", err);
        return err;
    }
    tm_printf((UB *)"Transport: I2C slave init OK\n");
    return E_OK;
}

LOCAL void i2c_transport_slave_update(const matrix_row_t *matrix)
{
    UB checksum = crc8(matrix, MATRIX_ROWS_PER_HAND);
    i2c_slave_reg_write(SHMEM_OFF_CHECKSUM, &checksum, 1);
    i2c_slave_reg_write(SHMEM_OFF_SMATRIX, matrix, MATRIX_ROWS_PER_HAND);

    UB alive = split_shmem->alive_counter + 1;
    i2c_slave_reg_write(SHMEM_OFF_ALIVE, &alive, 1);
}

#endif /* KB_IS_MASTER */
#endif /* !SPLIT_TRANSPORT_BLE */

/*======================================================================
 * BLE トランスポート関数 (Master/Slave)
 * AUTO モードでも使うため、SPLIT_TRANSPORT_I2C 以外でコンパイル。
 *====================================================================*/
#if defined(SPLIT_TRANSPORT_BLE) || defined(SPLIT_TRANSPORT_AUTO)

#if KB_IS_MASTER
#include "ble_split_central.h"

/* BLE Central は kb_output_ble.c で BLE タスク起動前に初期化済み */
LOCAL ER ble_transport_init(void)
{
    tm_printf((UB *)"Transport: BLE split master (pre-initialized)\n");
    return E_OK;
}

LOCAL ER ble_transport_read_matrix(matrix_row_t *slave_matrix)
{
    return ble_split_central_read_matrix(slave_matrix);
}

#else /* Slave */
#include "ble_split_service.h"

LOCAL ER ble_transport_slave_init(void)
{
    tm_printf((UB *)"Transport: BLE split slave\n");
    return E_OK;
}

LOCAL void ble_transport_slave_update(const matrix_row_t *matrix)
{
    ble_split_service_update_matrix(matrix);
}

#endif /* KB_IS_MASTER */
#endif /* SPLIT_TRANSPORT_BLE || SPLIT_TRANSPORT_AUTO */

/*======================================================================
 * 公開 API — トランスポートモードに応じてディスパッチ
 *====================================================================*/

#if KB_IS_MASTER

/*----------------------------------------------------------------------
 * SPLIT_TRANSPORT_I2C (デフォルト)
 */
#if defined(SPLIT_TRANSPORT_I2C) || \
    (!defined(SPLIT_TRANSPORT_BLE) && !defined(SPLIT_TRANSPORT_AUTO))

void transport_init(void)              { i2c_transport_init(); }
ER transport_master_read_matrix(matrix_row_t *m) { return i2c_transport_read_matrix(m); }

/*----------------------------------------------------------------------
 * SPLIT_TRANSPORT_BLE
 */
#elif defined(SPLIT_TRANSPORT_BLE)

void transport_init(void)              { ble_transport_init(); }
ER transport_master_read_matrix(matrix_row_t *m) { return ble_transport_read_matrix(m); }

/*----------------------------------------------------------------------
 * SPLIT_TRANSPORT_AUTO — I2C 優先、失敗時 BLE フォールバック
 */
#elif defined(SPLIT_TRANSPORT_AUTO)

LOCAL BOOL use_ble = FALSE;

void transport_init(void)
{
    /*
     * I2C を先に試行 (probe として 1 回 read を実行)。
     * I2C slave が接続されていれば I2C を使う。
     * 接続されていなければ BLE Central にフォールバック。
     */
    ER err = i2c_transport_init();
    if (err == E_OK) {
        /* I2C probe: checksum を 1 回読んでみる */
        matrix_row_t dummy[MATRIX_ROWS_PER_HAND];
        err = i2c_transport_read_matrix(dummy);
    }

    if (err == E_OK) {
        use_ble = FALSE;
        tm_printf((UB *)"Transport: AUTO → I2C (slave detected)\n");
    } else {
        use_ble = TRUE;
        ble_transport_init();
        tm_printf((UB *)"Transport: AUTO → BLE (I2C slave not found)\n");
    }
}

ER transport_master_read_matrix(matrix_row_t *m)
{
    if (use_ble) {
        return ble_transport_read_matrix(m);
    } else {
        return i2c_transport_read_matrix(m);
    }
}

#endif /* SPLIT_TRANSPORT_xxx */

#else /* !KB_IS_MASTER — Slave */

/*----------------------------------------------------------------------
 * Slave: I2C / BLE / AUTO
 */
#if defined(SPLIT_TRANSPORT_BLE)

void transport_init(void)                               { ble_transport_slave_init(); }
void transport_slave_update_matrix(const matrix_row_t *m) { ble_transport_slave_update(m); }

#elif defined(SPLIT_TRANSPORT_AUTO)
/* AUTO slave: BLE 対応 Pico W なら BLE、そうでなければ I2C */
/* 実質的に slave はハードウェアで決まるので、ビルド時に選択 */
/* Pico W ビルドなら BLE、RP2040 ビルドなら I2C */
#if defined(KB_OUTPUT_BLE)
void transport_init(void)                               { ble_transport_slave_init(); }
void transport_slave_update_matrix(const matrix_row_t *m) { ble_transport_slave_update(m); }
#else
void transport_init(void)                               { i2c_transport_slave_init(); }
void transport_slave_update_matrix(const matrix_row_t *m) { i2c_transport_slave_update(m); }
#endif

#else /* デフォルト: I2C */

void transport_init(void)                               { i2c_transport_slave_init(); }
void transport_slave_update_matrix(const matrix_row_t *m) { i2c_transport_slave_update(m); }

#endif /* SPLIT_TRANSPORT_xxx */

#endif /* KB_IS_MASTER */
