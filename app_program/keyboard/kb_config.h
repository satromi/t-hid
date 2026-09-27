/*
 * kb_config.h — Keyboard hardware configuration
 *
 * ★ User-editable file: Change pin assignments, I2C address,
 *   debounce settings here to match your keyboard hardware.
 *
 * ターゲット依存の定義 (ピン割当、LED、I2C) は
 * sysdepend/<target>/kb_config_hw.h に分離されている
 */

#ifndef __KB_CONFIG_H__
#define __KB_CONFIG_H__

#include <sys/machine.h>

/*--------------------------------------------------------------------
 * USB Device Identity (usb_descriptors.c から参照される)
 *--------------------------------------------------------------------*/
#define KB_VENDOR_ID        0xCafe  /* WARNING: 0xCafe is not USB-IF registered. Development use only. */
#define KB_PRODUCT_ID       0x4004
#define KB_MANUFACTURER     "satromi works"
#define KB_PRODUCT          "TL Split using uT-Kernel"

/*--------------------------------------------------------------------
 * Matrix dimensions (mintlsplit: 10 rows x 4 cols per hand)
 *--------------------------------------------------------------------*/
#define MATRIX_ROWS_PER_HAND  10
#define MATRIX_COLS_PER_HAND  4
#define MATRIX_ROWS           (MATRIX_ROWS_PER_HAND * 2)  /* 20 total */
#define MATRIX_COLS           MATRIX_COLS_PER_HAND         /* 4 */

/*--------------------------------------------------------------------
 * Diode direction
 *   COL2ROW: Column → Diode → Row (drive row low, read col)
 *   ROW2COL: Row → Diode → Column (drive col low, read row)
 *--------------------------------------------------------------------*/
#define COL2ROW  0
#define ROW2COL  1
#define DIODE_DIRECTION  COL2ROW

/*--------------------------------------------------------------------
 * Debounce
 *--------------------------------------------------------------------*/
#define DEBOUNCE_MS          5    /* Debounce time in milliseconds */

/*--------------------------------------------------------------------
 * Scan interval (depends on CNF_TIMER_PERIOD in config.h)
 * With CNF_TIMER_PERIOD=1, tk_dly_tsk(1) waits ~2ms
 *--------------------------------------------------------------------*/
#define SCAN_INTERVAL_MS     1    /* Argument to tk_dly_tsk() */
#define SCAN_PERIOD_MS       2    /* Actual scan period (~CNF_TIMER_PERIOD+1) */

/*--------------------------------------------------------------------
 * I2C Split keyboard configuration
 *--------------------------------------------------------------------*/
#ifndef SPLIT_ENABLED
#define SPLIT_ENABLED        1
#endif
#define SPLIT_I2C_DEVNM      "iica"          /* I2C device name (unit 0) */
#define SPLIT_I2C_ADDRESS    0x32            /* Right-hand slave I2C address */

/*--------------------------------------------------------------------
 * Master / Slave selection (compile-time)
 *   Define KB_IS_MASTER=1 for left hand (USB + I2C master)
 *   Define KB_IS_MASTER=0 for right hand (I2C slave only)
 *   Can be overridden from makefile: -DKB_IS_MASTER=0
 *--------------------------------------------------------------------*/
#ifndef KB_IS_MASTER
#define KB_IS_MASTER         1
#endif

/*--------------------------------------------------------------------
 * Matrix hand selection (compile-time)
 *   0 = 左手のピン割当 (ROW_PINS_L / COL_PINS_L) でスキャンし、
 *       マトリクス行 0〜9 を担当する
 *   1 = 右手のピン割当 (ROW_PINS_R / COL_PINS_R) でスキャンし、
 *       マトリクス行 10〜19 を担当する
 *
 * 既定は master=左手 / slave=右手。明示指定すれば master ビルド
 * (USB HID 有効) のまま右手基板を単体のキーボードとして動かせる。
 *--------------------------------------------------------------------*/
#ifndef KB_MATRIX_HAND_R
#define KB_MATRIX_HAND_R     (!KB_IS_MASTER)
#endif

/*--------------------------------------------------------------------
 * LED heartbeat (1 = enable 100ms ON / 1900ms OFF blink)
 *--------------------------------------------------------------------*/
#define KB_LED_HEARTBEAT     1

/*--------------------------------------------------------------------
 * Task configuration
 *--------------------------------------------------------------------*/
#define KB_SCAN_TASK_PRI     8
#if defined(KB_OUTPUT_BLE)
#define KB_SCAN_TASK_STKSZ   8192    /* BLE: CYW43 FW ロード + BTstack HCI init が scanner 上で実行 */
#else
#define KB_SCAN_TASK_STKSZ   1024
#endif

/*--------------------------------------------------------------------
 * I2C Slave register definitions (for split protocol)
 *--------------------------------------------------------------------*/
#define SPLIT_REG_MATRIX     0x00   /* Register address for matrix data */
#define SPLIT_MATRIX_SIZE    MATRIX_ROWS_PER_HAND  /* 10 bytes */

/*--------------------------------------------------------------------
 * Target-dependent hardware definitions
 *   ROW_PINS_L/R, COL_PINS_L/R  — GPIO ピン割当
 *   KB_LED_ON(), KB_LED_OFF()    — LED 制御マクロ
 *   KB_COL_INIT(pin)             — カラムピン初期化マクロ
 *   I2C0_BASE_ADDR, INTNO_I2C0  — I2C ハードウェアアドレス
 *--------------------------------------------------------------------*/
#define KB_CONFIG_HW_PATH_(a)	#a
#define KB_CONFIG_HW_PATH(a)	KB_CONFIG_HW_PATH_(a)
#define KB_CONFIG_HW_FILE()	KB_CONFIG_HW_PATH(sysdepend/TARGET_CPU_DIR/kb_config_hw.h)
#include KB_CONFIG_HW_FILE()

#endif /* __KB_CONFIG_H__ */
