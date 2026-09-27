/*
 *----------------------------------------------------------------------
 *    USB HID Keyboard Output Driver
 *
 *    kb_output.c の出力トランスポート実装 (USB HID)
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <tm/tmonitor.h>

#include "../../device/include/dev_usb_hid.h"
#include "kb_hid_keycodes.h"
#include "kb_output.h"

LOCAL ID usb_dd = -1;
LOCAL INT usb_error_count = 0;
LOCAL BOOL usb_disconnected = FALSE;

/*----------------------------------------------------------------------
 * USB HID 送信エラーしきい値
 * 連続エラーが続いたら USB を「切断」と判定し、BLE に切替
 */
#define USB_ERROR_THRESHOLD  10

/*----------------------------------------------------------------------
 * USB HID 出力初期化
 */
LOCAL ER usb_output_init(void)
{
    /*
     * USB ホスト接続判定:
     *   RP2040 USB SIE_STATUS レジスタ (0x50110050) bit 16 = CONNECTED
     *   CONNECTED=0 (電源のみ) なら即座に BLE フォールバック
     *   CONNECTED=1 なら USB デバイスをオープン
     *
     * CONFIGURED イベント待ちは行わない:
     *   dev_init_usb_hid() (カーネル初期化) で D+ プルアップ有効化後、
     *   PC が SET_CONFIGURATION を送るまでの時間は環境依存 (1-15秒)。
     *   タイムアウトベースの判定は不安定なため、デバイスオープン成功で
     *   USB 使用可能と判断し、実際のレポート送信エラーで BLE に切替。
     */
#define USB_SIE_STATUS  0x50110050
#define USB_SIE_CONNECTED (1u << 16)
    {
        INT i;
        for (i = 0; i < 10; i++) {
            if (in_w(USB_SIE_STATUS) & USB_SIE_CONNECTED) break;
            tk_dly_tsk(100);
        }

        if (!(in_w(USB_SIE_STATUS) & USB_SIE_CONNECTED)) {
            tm_printf((UB *)"KB: USB not connected (power only)\n");
            return E_TMOUT;
        }
        tm_printf((UB *)"KB: USB host detected\n");
    }

    usb_dd = tk_opn_dev((UB *)USB_HID_DEVNM, TD_UPDATE);
    if (usb_dd < E_OK) {
        tm_printf((UB *)"KB: USB open failed: %d\n", usb_dd);
        return E_IO;
    }

    usb_error_count = 0;
    usb_disconnected = FALSE;
    tm_printf((UB *)"KB: USB HID output ready\n");
    return E_OK;
}

/*----------------------------------------------------------------------
 * CONFIGURED 前のレポート送信を防止
 */
LOCAL BOOL usb_configured(void)
{
    ID cfg_flg = usb_hid_get_cfg_flgid();
    if (cfg_flg > 0) {
        UINT flgptn = 0;
        ER chk = tk_wai_flg(cfg_flg, USB_HID_EVT_CONFIGURED,
                            TWF_ORW, &flgptn, 0);
        if (chk != E_OK) return FALSE;
    }
    return TRUE;
}

/*----------------------------------------------------------------------
 * レポート送信
 */
LOCAL ER usb_output_send(const T_HID_KBD_REPORT *report)
{
    if (usb_dd < 0 || usb_disconnected) return E_IO;
    if (!usb_configured()) return E_IO;

    /* E_BUSY リトライ: 前回送信の EP1 IN 完了を待つ (最大 50ms) */
    SZ asize;
    ER err;
    INT retry;
    for (retry = 0; retry < 5; retry++) {
        err = tk_swri_dev(usb_dd, USB_HID_DN_KBD, (void *)report, sizeof(*report), &asize);
        if (err != E_BUSY) break;
        tk_dly_tsk(10);  /* 10ms 待ってリトライ */
    }
    if (err != E_OK) {
        usb_error_count++;
        if (usb_error_count >= USB_ERROR_THRESHOLD) {
            usb_disconnected = TRUE;
            tm_printf((UB *)"KB: USB disconnected (errors=%d)\n", usb_error_count);
        }
        return err;
    }
    usb_error_count = 0;
    return E_OK;
}

#if defined(USB_HID_GAMEPAD)
/*----------------------------------------------------------------------
 * ゲームパッドレポート送信
 *
 * 待たずに 1 回だけ書く。E_BUSY は前のレポートがホストに読まれていない
 * だけなので切断とは数えず、呼び出し側が次のスキャンで送り直す。
 * ゲームパッドのインタフェースを読まないホストでもキーボードを遅らせない。
 */
LOCAL ER usb_output_send_pad(const T_HID_PAD_REPORT *report)
{
    SZ asize;

    if (usb_dd < 0 || usb_disconnected) return E_IO;
    if (!usb_configured()) return E_IO;
    return tk_swri_dev(usb_dd, USB_HID_DN_PAD, (void *)report, sizeof(*report), &asize);
}
#endif

/*----------------------------------------------------------------------
 * USB 接続状態確認
 */
LOCAL BOOL usb_output_is_ready(void)
{
    if (usb_dd < 0 || usb_disconnected) return FALSE;
    return TRUE;
}

/*----------------------------------------------------------------------
 * USB 出力トランスポート定義
 */
EXPORT const T_HID_OUTPUT kb_output_usb = {
    .name        = "USB",
    .init        = usb_output_init,
    .send_report = usb_output_send,
#if defined(USB_HID_GAMEPAD)
    .send_pad    = usb_output_send_pad,
#endif
    .is_ready    = usb_output_is_ready,
};

#endif /* CPU_RP2040 */
