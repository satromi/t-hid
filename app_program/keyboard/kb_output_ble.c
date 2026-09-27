/*
 * kb_output_ble.c — BLE HID output transport
 *
 * Pico W (CYW43439) + BTstack による BLE HID over GATT 出力。
 *
 * 初期化パターン:
 *   A) USB 非接続 → ble_output_init() で同期初期化 (BLE が HID 出力)
 *   B) USB 接続   → ble_deferred_init() で BLE タスクが非同期初期化
 *                    USB が HID 出力。BLE は LED/ペアリングのみ。
 */

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "kb_output.h"

#if defined(KB_OUTPUT_BLE)

/* pico_w.mk の INCPATH に -I"../device/ble_hid" が設定済み */
#include "btstack_port_tkernel.h"
#include "ble_hid_gatt.h"

/* CYW43 アーキテクチャ初期化 (sysdepend) */
extern ER cyw43_arch_tkernel_init(void);

/* BLE 初期化完了フラグ (非同期初期化の完了を示す) */
LOCAL volatile BOOL ble_ready = FALSE;

/*----------------------------------------------------------------------
 * BLE 初期化共通処理
 * CYW43 + BTstack + GATT HID を初期化する。
 * 呼び出し元タスクのスタック上で実行される。
 */
LOCAL ER ble_do_init(void)
{
    ER err;

    err = cyw43_arch_tkernel_init();
    if (err != E_OK) {
        tm_printf((UB *)"BLE: CYW43 init failed: %d\n", err);
        return err;
    }

    err = btstack_tkernel_init();
    if (err != E_OK) {
        tm_printf((UB *)"BLE: BTstack init failed: %d\n", err);
        return err;
    }

    err = ble_hid_gatt_init();
    if (err != E_OK) {
        tm_printf((UB *)"BLE: GATT HID init failed: %d\n", err);
        return err;
    }

#if (defined(SPLIT_TRANSPORT_BLE) || defined(SPLIT_TRANSPORT_AUTO)) && KB_IS_MASTER
    {
        extern ER ble_split_central_init(void);
        err = ble_split_central_init();
        if (err != E_OK) {
            tm_printf((UB *)"BLE: split central init failed: %d\n", err);
        }
    }
#endif

    /*
     * BLE タスクを先に起動してから HCI パワーオンする。
     * hci_power_control(HCI_POWER_ON) は HCI_RESET 送信後 200ms タイマーを
     * 設定するため、BTstack run loop (BLE タスク) が稼働していないと
     * タイムアウト → HCI_RESET 無限ループになる。
     */
    err = btstack_tkernel_start();
    if (err != E_OK) {
        tm_printf((UB *)"BLE: task start failed: %d\n", err);
        return err;
    }

    err = ble_hid_gatt_start();
    if (err != E_OK) {
        tm_printf((UB *)"BLE: HCI start failed: %d\n", err);
        return err;
    }

    /* CYW43 GPIO (LED) を使用可能にする */
    {
        extern void cyw43_arch_set_ready(void);
        cyw43_arch_set_ready();
    }

    ble_ready = TRUE;
    tm_printf((UB *)"BLE: ready (advertising)\n");
    return E_OK;
}

/*----------------------------------------------------------------------
 * BLE 同期初期化 (USB 非接続時、BLE が HID 出力)
 */
LOCAL ER ble_output_init(void)
{
    return ble_do_init();
}

/*----------------------------------------------------------------------
 * 非同期 BLE 初期化タスク
 *
 * USB が HID 出力として動作中に、バックグラウンドで CYW43/BLE を初期化。
 * CYW43 FW ロード等の重い処理が scanner タスクをブロックしない。
 * 初期化完了後は LED 制御とペアリング管理が有効になる。
 */
#define BLE_INIT_TASK_PRI    10
#define BLE_INIT_TASK_STKSZ  8192   /* CYW43 FW ロード + SPI バッファに必要 */

LOCAL void ble_init_task(INT stacd, void *exinf)
{
    (void)stacd; (void)exinf;

    tm_printf((UB *)"BLE: deferred init start\n");
    ER err = ble_do_init();
    if (err != E_OK) {
        tm_printf((UB *)"BLE: deferred init failed: %d\n", err);
    } else {
        tm_printf((UB *)"BLE: deferred init OK\n");
    }
    /* タスク永久スリープ (tk_ext_tsk は BTstack リソース解放前に呼ぶと Hard fault) */
    tk_slp_tsk(TMO_FEVR);
}

EXPORT void ble_deferred_init(void)
{
    T_CTSK ctsk;
    ctsk.exinf   = NULL;
    ctsk.tskatr  = TA_HLNG | TA_RNG3;
    ctsk.task    = (FP)ble_init_task;
    ctsk.itskpri = BLE_INIT_TASK_PRI;
    ctsk.stksz   = BLE_INIT_TASK_STKSZ;
    ctsk.bufptr  = NULL;

    ID tskid = tk_cre_tsk(&ctsk);
    if (tskid > 0) {
        tk_sta_tsk(tskid, 0);
    } else {
        tm_printf((UB *)"BLE: deferred init task failed: %d\n", tskid);
    }
}

/*----------------------------------------------------------------------
 * BLE レポート送信
 */
LOCAL ER ble_output_send(const T_HID_KBD_REPORT *report)
{
    return ble_hid_gatt_send_report(report);
}

/*----------------------------------------------------------------------
 * BLE 接続状態チェック
 */
LOCAL BOOL ble_output_is_ready(void)
{
    return ble_hid_gatt_is_connected();
}

/*----------------------------------------------------------------------
 * XMK アクション処理 (BLE)
 */
LOCAL void ble_output_xmk(UH xmk_code)
{
    if (ble_ready) {
        ble_hid_gatt_xmk_action(xmk_code);
    }
}

/*----------------------------------------------------------------------
 * BLE HID 出力トランスポート定義
 */
const T_HID_OUTPUT kb_output_ble = {
    .init        = ble_output_init,
    .send_report = ble_output_send,
    .is_ready    = ble_output_is_ready,
    .xmk_action  = ble_output_xmk,
    .name        = "BLE",
};

#endif /* KB_OUTPUT_BLE */
