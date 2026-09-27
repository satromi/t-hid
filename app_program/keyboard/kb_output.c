/*
 * kb_output.c — HID output transport auto-switching
 *
 * 出力選択ロジック:
 *   BLE ビルド (KB_OUTPUT_BLE):
 *     1. 常に BLE を初期化 (CYW43 + LED + ペアリング管理)
 *     2. BLE 初期化成功 → BLE を HID 出力に使用
 *     3. BLE 初期化失敗 → USB にフォールバック
 *     4. USB フォールバック時も XMK (ペアリング等) は BLE 側で処理
 *   非 BLE ビルド:
 *     USB のみ試行
 *
 * トランスポート喪失検知:
 *   kb_output_send() は is_ready() をチェックし、トランスポートが
 *   切断された場合は送信をスキップ (ログは抑制して CPU 浪費を防ぐ)
 */

#include <stddef.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>

#include "kb_output.h"

/* 現在アクティブなトランスポート */
LOCAL const T_HID_OUTPUT *active_output = NULL;

/* エラーログ抑制 (連続エラーを毎回ログしない) */
LOCAL BOOL transport_lost_logged = FALSE;

/*----------------------------------------------------------------------
 * 出力初期化 (自動切替)
 */
EXPORT ER kb_output_init(void)
{
    ER err;

    /* まず USB を試行 (即座に動作可能にする) */
    err = kb_output_usb.init();
    if (err == E_OK) {
        active_output = &kb_output_usb;
        tm_printf((UB *)"KB: output = %s\n", active_output->name);
        /*
         * BLE ビルド: USB 成功後も BLE を非同期で初期化開始。
         * CYW43 (LED) + ペアリング管理は BLE タスクで初期化される。
         * ble_deferred_init() → BLE タスク起動 → タスク内で CYW43/GATT 初期化。
         */
#if defined(KB_OUTPUT_BLE)
        ble_deferred_init();
#endif
        return E_OK;
    }

    /* USB 失敗 → BLE をフォールバックとして同期初期化 */
#if defined(KB_OUTPUT_BLE)
    tm_printf((UB *)"KB: USB unavailable, trying BLE...\n");
    err = kb_output_ble.init();
    if (err == E_OK) {
        active_output = &kb_output_ble;
        tm_printf((UB *)"KB: output = %s\n", active_output->name);
        return E_OK;
    }
#endif

    tm_printf((UB *)"KB: no HID output available\n");
    return E_IO;
}

/*----------------------------------------------------------------------
 * レポート送信 (アクティブなトランスポートに送信)
 *
 * トランスポートが切断された場合:
 *   - 初回のみログ出力 (連続ログ抑制)
 *   - E_IO を返す (呼び出し側でハンドリング可能)
 *
 * トランスポートが回復した場合 (BLE 再接続等):
 *   - 自動的に送信再開
 *   - ログ抑制フラグをリセット
 */
EXPORT ER kb_output_send(const T_HID_KBD_REPORT *report)
{
    if (active_output == NULL) return E_IO;

    /* トランスポート喪失チェック */
    if (!active_output->is_ready()) {
        if (!transport_lost_logged) {
            tm_printf((UB *)"KB: %s transport lost\n", active_output->name);
            transport_lost_logged = TRUE;
        }
        return E_IO;
    }

    /* トランスポート回復検知 */
    if (transport_lost_logged) {
        tm_printf((UB *)"KB: %s transport recovered\n", active_output->name);
        transport_lost_logged = FALSE;
    }

    return active_output->send_report(report);
}

/*----------------------------------------------------------------------
 * ゲームパッドレポート送信
 *
 * トランスポートの喪失・回復のログは kb_output_send() に任せる。
 */
EXPORT ER kb_output_send_pad(const T_HID_PAD_REPORT *report)
{
    if (active_output == NULL) return E_IO;
    if (active_output->send_pad == NULL) return E_NOSPT;
    if (!active_output->is_ready()) return E_IO;
    return active_output->send_pad(report);
}

/*----------------------------------------------------------------------
 * 送信可能チェック
 */
EXPORT BOOL kb_output_is_ready(void)
{
    if (active_output == NULL) return FALSE;
    return active_output->is_ready();
}

EXPORT const char *kb_output_active_name(void)
{
    if (active_output == NULL) return "none";
    return active_output->name;
}

/*----------------------------------------------------------------------
 * XMK アクション実行
 *
 * BLE ビルドでは USB フォールバック中でも BLE 側に XMK をルーティング。
 * ペアリングモード等の BLE 固有アクションを常に処理可能にする。
 */
EXPORT void kb_output_xmk_action(UH xmk_code)
{
#if defined(KB_OUTPUT_BLE)
    /* BLE 側で処理 (USB 使用中でも非同期初期化完了後は BLE にルーティング) */
    if (kb_output_ble.xmk_action != NULL) {
        kb_output_ble.xmk_action(xmk_code);
        return;
    }
#endif
    if (active_output != NULL && active_output->xmk_action != NULL) {
        active_output->xmk_action(xmk_code);
    }
}
