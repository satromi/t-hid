/*
 *----------------------------------------------------------------------
 *    WIZnet (W5100S/W5500) μT-Kernel Native Socket Implementation
 *
 *    ioLibrary socket.c (880行, 23箇所のビジーウェイト) を
 *    μT-Kernel ネイティブに書き直したもの (~350行)。
 *
 *    RTOS 機能:
 *    - tk_wai_flg(): 全ソケットイベント (CON/RECV/SENDOK/DISCON/TIMEOUT) を
 *      割り込み駆動で待機。ビジーウェイト完全排除。
 *    - FastLock: IMR レジスタの排他制御
 *    - ソケット制御ブロック: ioLibrary のグローバル変数を構造体化
 *
 *    レジスタアクセスは チップ依存ヘッダ (w5100s_reg.h / w5500_reg.h) で抽象化。
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <string.h>

#if defined(WIZCHIP_W5500)
#include "wiznet_drv.h"
#else
#include "w5100s_reg.h"
#endif
#include "tk_socket.h"
#include "../include/dev_wiznet.h"
#if defined(WIZCHIP_W5500)
#include "wiznet_sock.h"
#else
#include "sysdepend/w5100s/w5100s_spi.h"
#endif

/*----------------------------------------------------------------------
 * ソケット制御ブロック
 */
typedef struct {
    BOOL    in_use;         /* ソケット使用中 */
    UB      protocol;       /* Sn_MR_TCP / Sn_MR_UDP */
    TMO     rcv_tmout;      /* 受信タイムアウト (ms), 0=デフォルト */
    TMO     snd_tmout;      /* 送信タイムアウト (ms), 0=デフォルト */
    ER      last_error;     /* 保留中エラー */
} T_SOCK_CB;

LOCAL T_SOCK_CB sock_cb[WIZNET_SOCK_NUM];

/* 自動ポート割り当てカウンタ */
LOCAL UH auto_port = 49152;

/* IMR 排他制御用 (spi_lock は SPI HAL 内で管理) */
LOCAL FastLock imr_lock;

/* ホスト名 (A-3) */
#define TK_HOSTNAME_MAX  64
LOCAL char hostname[TK_HOSTNAME_MAX];
LOCAL INT  hostname_len = 0;

/* select 用統合イベントフラグ (B-1) — SPI HAL からも参照 */
ID select_flgid = 0;

/*----------------------------------------------------------------------
 * コマンド発行 + HW 完了待ち
 *
 * W5100S のコマンドレジスタ (Sn_CR) はコマンド実行後に HW が自動で 0 にクリアする。
 * クリアまで数μs — tk_dly_tsk() の最小粒度 (1ms) より遥かに短いため
 * 意図的なビジーウェイトとして許容する。(kb_matrix.c の GPIO settling と同様)
 */
LOCAL void wiznet_issue_cmd(UB sn, UB cmd)
{
    setSn_CR(sn, cmd);
    while (getSn_CR(sn)) {}  /* ~数μs, HW コマンド実行完了 */
}

/*----------------------------------------------------------------------
 * ソケット割り込みマスク設定 (IMR 排他制御付き)
 */
LOCAL void wiznet_enable_sock_int(UB sn, UB mask)
{
    Lock(&imr_lock);
    setSn_IMR(sn, mask);
    setIMR(getIMR() | (1 << sn));
    Unlock(&imr_lock);
}

LOCAL void wiznet_disable_sock_int(UB sn)
{
    Lock(&imr_lock);
    setIMR(getIMR() & ~(1 << sn));
    setSn_IMR(sn, 0);
    Unlock(&imr_lock);
}

/*----------------------------------------------------------------------
 * ソケットイベント待ち (共通ヘルパー)
 */
LOCAL ER wiznet_wait_event(UB sn, UINT wait_evt, TMO tmout)
{
    UINT flgptn;
    /* BREAK イベントも常に待つ (tk_sock_break による中止対応) */
    ER err = tk_wai_flg(wiznet_sock_flgid[sn],
                        wait_evt | WIZNET_SOCK_EVT_BREAK,
                        TWF_ORW | TWF_BITCLR, &flgptn, tmout);
    if (err == E_TMOUT) return E_TMOUT;
    if (err != E_OK) return E_IO;
    /* 優先度順にチェック: BREAK > TIMEOUT > 要求イベント > DISCON */
    if (flgptn & WIZNET_SOCK_EVT_BREAK) return E_ABORT;
    if (flgptn & WIZNET_SOCK_EVT_TIMEOUT) return E_TMOUT;
    if (flgptn & wait_evt) return E_OK;  /* 要求イベント成立 */
    if (flgptn & WIZNET_SOCK_EVT_DISCON) return E_IO;  /* 予期せぬ切断 */
    return E_OK;
}

/*======================================================================
 * ソケット API
 *====================================================================*/

/*----------------------------------------------------------------------
 * ソケットオープン
 */
EXPORT ER tk_sock_open(UB sn, UB protocol, UH port)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;

    /* 自動ポート割り当て (排他制御: 複数タスクからの同時呼び出し対策) */
    if (port == 0) {
        Lock(&imr_lock);
        port = auto_port++;
        if (auto_port == 0) auto_port = 49152;
        Unlock(&imr_lock);
    }

    /* クローズ状態でなければ先にクローズ */
    if (getSn_SR(sn) != SOCK_CLOSED) {
        tk_sock_close(sn);
    }

    /* ソケットモード + ポート設定 */
    setSn_MR(sn, protocol);
    setSn_PORTR(sn, port);

    /* OPEN コマンド発行 */
    wiznet_issue_cmd(sn, Sn_CR_OPEN);

    /* 状態遷移確認 */
    UB sr = getSn_SR(sn);
    if (protocol == Sn_MR_TCP && sr != SOCK_INIT) {
        tk_sock_close(sn);
        return E_IO;
    }
    if (protocol == Sn_MR_UDP && sr != SOCK_UDP) {
        tk_sock_close(sn);
        return E_IO;
    }

    /* 制御ブロック初期化 */
    sock_cb[sn].in_use = TRUE;
    sock_cb[sn].protocol = protocol;

    /* 前セッションの残留イベントを除去してから割り込みを有効化 */
    setSn_IR(sn, 0xFF);                    /* HW 割り込みフラグクリア */
    if (wiznet_sock_flgid[sn] > 0) {
        tk_clr_flg(wiznet_sock_flgid[sn], 0);  /* SW イベントフラグクリア */
    }

    /* 割り込み有効化 */
    wiznet_enable_sock_int(sn,
        Sn_IR_CON | Sn_IR_RECV | Sn_IR_DISCON |
        Sn_IR_TIMEOUT | Sn_IR_SENDOK);

    return E_OK;
}

/*----------------------------------------------------------------------
 * ソケットクローズ
 */
EXPORT ER tk_sock_close(UB sn)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;

    /* 割り込み無効化 */
    wiznet_disable_sock_int(sn);

    /* CLOSE コマンド */
    wiznet_issue_cmd(sn, Sn_CR_CLOSE);

    /* 割り込みフラグクリア */
    setSn_IR(sn, 0xFF);

    /* イベントフラグもクリア (残留イベント除去) */
    if (wiznet_sock_flgid[sn] > 0) {
        tk_clr_flg(wiznet_sock_flgid[sn], 0);
    }

    sock_cb[sn].in_use = FALSE;

    return E_OK;
}

/*----------------------------------------------------------------------
 * TCP リッスン
 * LISTEN 状態にするだけ。接続受付待ちは tk_sock_accept() で行う。
 */
EXPORT ER tk_sock_listen(UB sn)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (getSn_SR(sn) != SOCK_INIT) return E_IO;

    wiznet_issue_cmd(sn, Sn_CR_LISTEN);

    if (getSn_SR(sn) != SOCK_LISTEN) {
        tk_sock_close(sn);
        return E_IO;
    }

    return E_OK;
}

/*----------------------------------------------------------------------
 * TCP 接続受付待ち (サーバー側)
 * ESTABLISHED まで割り込みベースで待機。タスクは CPU を解放する。
 */
EXPORT ER tk_sock_accept(UB sn, TMO tmout)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;

    if (getSn_SR(sn) == SOCK_ESTABLISHED) return E_OK;

    return wiznet_wait_event(sn,
        WIZNET_SOCK_EVT_CON | WIZNET_SOCK_EVT_TIMEOUT, tmout);
}

/*----------------------------------------------------------------------
 * TCP 接続 (クライアント側)
 * ESTABLISHED まで割り込みベースで待機。
 */
EXPORT ER tk_sock_connect(UB sn, UB *ip, UH port, TMO tmout)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (ip == NULL || port == 0) return E_PAR;
    if (getSn_SR(sn) != SOCK_INIT) return E_IO;

    /* 接続先設定 */
    setSn_DIPR(sn, ip);
    setSn_DPORTR(sn, port);

    /* CONNECT コマンド */
    wiznet_issue_cmd(sn, Sn_CR_CONNECT);

    /* 既に接続完了していればすぐリターン */
    if (getSn_SR(sn) == SOCK_ESTABLISHED) return E_OK;

    /* 割り込みで接続完了 or タイムアウトを待つ */
    ER err = wiznet_wait_event(sn,
        WIZNET_SOCK_EVT_CON | WIZNET_SOCK_EVT_TIMEOUT, tmout);

    if (err != E_OK) {
        tk_sock_close(sn);
        return err;
    }

    return E_OK;
}

/*----------------------------------------------------------------------
 * TCP 切断
 */
EXPORT ER tk_sock_disconnect(UB sn, TMO tmout)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;

    UB sr = getSn_SR(sn);
    if (sr == SOCK_CLOSED) return E_OK;

    /* UDP/MACRAW は DISCON 不要 — 直接クローズ */
    if (sr == SOCK_UDP || sr == SOCK_MACRAW) {
        return tk_sock_close(sn);
    }

    wiznet_issue_cmd(sn, Sn_CR_DISCON);

    /* CLOSED まで割り込みで待つ */
    ER err = wiznet_wait_event(sn,
        WIZNET_SOCK_EVT_DISCON | WIZNET_SOCK_EVT_TIMEOUT, tmout);

    tk_sock_close(sn);
    return err;
}

/*----------------------------------------------------------------------
 * データ送信
 *
 * TX バッファ空きを SENDOK 割り込みで待つ。
 * ioLibrary の send() にあった TX_FSR ポーリングループを排除。
 */
EXPORT W tk_sock_send(UB sn, const UB *buf, UH len, TMO tmout)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (buf == NULL || len == 0) return E_PAR;

    UB sr = getSn_SR(sn);
    if (sr != SOCK_ESTABLISHED && sr != SOCK_CLOSE_WAIT) return E_IO;

    /* TX バッファサイズで制限 */
    UH maxsz = getSn_TxMAX(sn);
    if (len > maxsz) len = maxsz;

    /* TX バッファ空き待ち (割り込み駆動) */
    while (getSn_TX_FSR(sn) < len) {
        sr = getSn_SR(sn);
        if (sr != SOCK_ESTABLISHED && sr != SOCK_CLOSE_WAIT) return E_IO;

        ER err = wiznet_wait_event(sn,
            WIZNET_SOCK_EVT_SENDOK | WIZNET_SOCK_EVT_DISCON |
            WIZNET_SOCK_EVT_TIMEOUT, tmout);
        if (err != E_OK) return err;
    }

    /* データを WIZnet TX バッファにコピー */
    wiz_send_data(sn, (uint8_t *)buf, len);

    /* SEND コマンド発行 */
    wiznet_issue_cmd(sn, Sn_CR_SEND);

    /* SENDOK 割り込みで送信完了を待つ */
    ER err = wiznet_wait_event(sn,
        WIZNET_SOCK_EVT_SENDOK | WIZNET_SOCK_EVT_TIMEOUT, tmout);
    if (err != E_OK) return err;

    return (W)len;
}

/*----------------------------------------------------------------------
 * データ受信
 *
 * RX データ到着を RECV 割り込みで待つ。
 * ioLibrary の recv() にあった RX_RSR ポーリングループを排除。
 */
EXPORT W tk_sock_recv(UB sn, UB *buf, UH len, TMO tmout)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (buf == NULL || len == 0) return E_PAR;

    /* RX データ待ち (割り込み駆動) */
    while (getSn_RX_RSR(sn) == 0) {
        UB sr = getSn_SR(sn);
        if (sr == SOCK_CLOSE_WAIT) {
            /* 相手が切断済み + データなし */
            return 0;
        }
        if (sr != SOCK_ESTABLISHED) return E_IO;

        ER err = wiznet_wait_event(sn,
            WIZNET_SOCK_EVT_RECV | WIZNET_SOCK_EVT_DISCON |
            WIZNET_SOCK_EVT_TIMEOUT, tmout);
        if (err == E_TMOUT) return E_TMOUT;
        if (err != E_OK) return E_IO;
    }

    /* 受信可能サイズ */
    UH rxsz = getSn_RX_RSR(sn);
    if (len > rxsz) len = rxsz;

    /* WIZnet RX バッファからデータコピー */
    wiz_recv_data(sn, buf, len);

    /* RECV コマンド (RX ポインタ更新) */
    wiznet_issue_cmd(sn, Sn_CR_RECV);

    return (W)len;
}

/*----------------------------------------------------------------------
 * UDP データグラム送信
 *
 * WIZnet の UDP 送信: 宛先 IP/Port をレジスタに設定 → データコピー
 * → SEND コマンド → SENDOK 割り込みで完了待ち
 */
EXPORT W tk_sock_sendto(UB sn, const UB *buf, UH len, UB *addr, UH port, TMO tmout)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (buf == NULL || len == 0 || addr == NULL || port == 0) return E_PAR;

    UB sr = getSn_SR(sn);
    if (sr != SOCK_UDP && sr != SOCK_MACRAW) {
        W5DBG("sendto: E1 sr=0x%02x\n", sr);
        return E_IO;
    }

    UH maxsz = getSn_TxMAX(sn);
    if (len > maxsz) return E_PAR;

    /* TX バッファ空き待ち */
    UH fsr = getSn_TX_FSR(sn);
    while (fsr < len) {
        sr = getSn_SR(sn);
        if (sr == SOCK_CLOSED) {
            W5DBG("sendto: E2 closed\n");
            return E_IO;
        }
        ER err = wiznet_wait_event(sn,
            WIZNET_SOCK_EVT_SENDOK | WIZNET_SOCK_EVT_TIMEOUT, tmout);
        if (err != E_OK) {
            W5DBG("sendto: E3 wait=%d\n", err);
            return err;
        }
        fsr = getSn_TX_FSR(sn);
    }

    /* 宛先・データ・SEND */
    setSn_DIPR(sn, addr);
    setSn_DPORTR(sn, port);
    wiz_send_data(sn, (uint8_t *)buf, len);
    wiznet_issue_cmd(sn, Sn_CR_SEND);

    /* SENDOK 待ち */
    ER err = wiznet_wait_event(sn,
        WIZNET_SOCK_EVT_SENDOK | WIZNET_SOCK_EVT_TIMEOUT, tmout);
    if (err != E_OK) {
        W5DBG("sendto: E4 wait=%d sr=0x%02x\n", err, getSn_SR(sn));
        return err;
    }

    return (W)len;
}

/*----------------------------------------------------------------------
 * UDP データグラム受信
 *
 * WIZnet の UDP RX バッファ先頭には 8byte ヘッダが付く:
 *   [0-3] 送信元 IP (4byte, big-endian)
 *   [4-5] 送信元ポート (2byte, big-endian)
 *   [6-7] データ長 (2byte, big-endian)
 * この後にペイロードが続く。
 */
EXPORT W tk_sock_recvfrom(UB sn, UB *buf, UH len, UB *addr, UH *port, TMO tmout)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (buf == NULL || len == 0) return E_PAR;

    /* RX データ待ち (割り込み駆動) */
    while (getSn_RX_RSR(sn) == 0) {
        UB sr = getSn_SR(sn);
        if (sr != SOCK_UDP && sr != SOCK_MACRAW) return E_IO;

        ER err = wiznet_wait_event(sn,
            WIZNET_SOCK_EVT_RECV | WIZNET_SOCK_EVT_TIMEOUT, tmout);
        if (err == E_TMOUT) return E_TMOUT;
        if (err != E_OK) return E_IO;
    }

    /*
     * WIZnet UDP RX バッファ: [IP(4)][Port(2)][Len(2)][Payload...]
     *
     * RX_RD の中間更新を避けるため、ヘッダはバイト単位で直接読み、
     * RX_RD はフレーム全体を読み終えた後に 1 回だけ更新する。
     */
    uint16_t ptr = getSn_RX_RD(sn);
    uint16_t rx_mask = getSn_RxMASK(sn);
    uint16_t rx_base = getSn_RxBASE(sn);
    uint16_t rx_max  = getSn_RxMAX(sn);

    /*
     * UDP ヘッダ (8 byte) をバイト単位で読み取り — RX_RD 更新なし.
     *
     * wiz_recv_data() + RECV コマンドではなくバイト単位で読む理由:
     * setSn_RX_RD() で中間ポインタを更新し RECV コマンドを発行すると、
     * WIZnet がまだ読み終えていないペイロード部分を解放してしまう。
     * ヘッダとペイロードを不可分に読むため、RX_RD は最後に 1 回だけ更新する。
     */
    UB head[8];
    {
        INT i;
        for (i = 0; i < 8; i++) {
            uint16_t a = rx_base + ((ptr + i) & rx_mask);
            head[i] = WIZCHIP_READ(a);
        }
    }

    /* 送信元情報 */
    if (addr != NULL) {
        addr[0] = head[0]; addr[1] = head[1];
        addr[2] = head[2]; addr[3] = head[3];
    }
    if (port != NULL) {
        *port = ((UH)head[4] << 8) | head[5];
    }
    UH data_len = ((UH)head[6] << 8) | head[7];
    UH read_len = (data_len > len) ? len : data_len;

    /*
     * ペイロード読み取り (ptr+8 から、循環バッファ対応).
     *
     * WIZnet の RX バッファは固定長の循環バッファ (rx_base ~ rx_base+rx_max)。
     * オフセット p_off からの読み取りがバッファ末尾を跨ぐ場合、
     * 前半 (p_off ~ rx_max) と後半 (rx_base ~ 残り) の 2 回に分けて読む。
     */
    if (read_len > 0) {
        uint16_t p_off = (ptr + 8) & rx_mask;
        uint16_t p_addr = rx_base + p_off;
        if (p_off + read_len > rx_max) {
            /* 循環バッファ境界を跨ぐ: 末尾まで + 先頭から残り */
            uint16_t first = rx_max - p_off;
            WIZCHIP_READ_BUF(p_addr, buf, first);
            WIZCHIP_READ_BUF(rx_base, buf + first, read_len - first);
        } else {
            WIZCHIP_READ_BUF(p_addr, buf, read_len);
        }
    }

    /*
     * RX_RD をフレーム全体分 (8 + data_len) 進める -- 1 回だけ更新。
     * ヘッダ + ペイロードを全て読み終えてから更新することで、
     * RECV コマンドが未読データを解放してしまう問題を防ぐ。
     */
    setSn_RX_RD(sn, ptr + 8 + data_len);

    /* RECV コマンド発行 (RX バッファ解放) */
    wiznet_issue_cmd(sn, Sn_CR_RECV);

    return (W)read_len;
}

/*----------------------------------------------------------------------
 * TCP キープアライブ設定
 *
 * WIZnet の Sn_KPALVTR レジスタは 5 秒単位。
 * sec=30 → val=6 (30/5), sec=3 → val=1 (切り上げ)
 */
EXPORT ER tk_sock_set_keepalive(UB sn, UH sec)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;

    UB val = 0;
    if (sec > 0) {
        val = (UB)((sec + 4) / 5);  /* 5秒単位に切り上げ */
        if (val == 0) val = 1;
    }
    setSn_KPALVTR(sn, val);
    return E_OK;
}

/*======================================================================
 * Phase 1: T2EX 仕様準拠 API
 *====================================================================*/

/*----------------------------------------------------------------------
 * A-1: 接続先アドレス/ポート取得 (so_getpeername 相当)
 */
EXPORT ER tk_sock_getpeer(UB sn, UB *ip, UH *port)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (ip != NULL) getSn_DIPR(sn, ip);
    if (port != NULL) *port = getSn_DPORTR(sn);
    return E_OK;
}

/*----------------------------------------------------------------------
 * A-1: ローカルアドレス/ポート取得 (so_getsockname 相当)
 */
EXPORT ER tk_sock_getlocal(UB sn, UB *ip, UH *port)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (ip != NULL) getSIPR(ip);
    if (port != NULL) *port = getSn_PORTR(sn);
    return E_OK;
}

/*----------------------------------------------------------------------
 * A-2: ソケット操作中止 (so_break 相当)
 *
 * 指定ソケットで tk_wai_flg() 中のタスクを E_ABORT で起床させる。
 * wiznet_wait_event() は BREAK イベントを検出して E_ABORT を返す。
 */
EXPORT ER tk_sock_break(UB sn)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    if (wiznet_sock_flgid[sn] <= 0) return E_NOEXS;

    tk_set_flg(wiznet_sock_flgid[sn], WIZNET_SOCK_EVT_BREAK);
    return E_OK;
}

/*----------------------------------------------------------------------
 * A-3: ホスト名設定 (so_sethostname 相当)
 */
EXPORT ER tk_net_sethostname(const char *name, INT len)
{
    if (name == NULL || len <= 0 || len >= TK_HOSTNAME_MAX) return E_PAR;

    Lock(&imr_lock);
    memcpy(hostname, name, len);
    hostname[len] = '\0';
    hostname_len = len;
    Unlock(&imr_lock);

    return E_OK;
}

/*----------------------------------------------------------------------
 * A-3: ホスト名取得 (so_gethostname 相当)
 */
EXPORT ER tk_net_gethostname(char *name, INT len)
{
    if (name == NULL || len <= 0) return E_PAR;

    Lock(&imr_lock);
    INT copy_len = (hostname_len < len - 1) ? hostname_len : len - 1;
    memcpy(name, hostname, copy_len);
    name[copy_len] = '\0';
    Unlock(&imr_lock);

    return E_OK;
}

/*----------------------------------------------------------------------
 * B-4: 受信可能バイト数取得 (so_ioctl FIONREAD 相当)
 */
EXPORT W tk_sock_available(UB sn)
{
    if (sn >= WIZNET_SOCK_NUM) return E_PAR;
    return (W)getSn_RX_RSR(sn);
}

/*======================================================================
 * Phase 2: T2EX 仕様準拠 API
 *====================================================================*/

/*----------------------------------------------------------------------
 * B-1: 簡易 I/O 多重化 (so_select 相当)
 *
 * select 用統合フラグを使い、複数ソケットの OR 待ちを 1 回の
 * tk_wai_flg() で実現する。
 *
 * 統合フラグのビットレイアウト:
 *   [7:0]   ソケット 0 のイベント (EVT_RECV..EVT_BREAK)
 *   [15:8]  ソケット 1 のイベント
 *   [23:16] ソケット 2 のイベント
 *   [31:24] ソケット 3 のイベント
 */
EXPORT ER tk_sock_select(UB sn_mask, UINT evt_mask, UINT *result, TMO tmout)
{
    if (result == NULL) return E_PAR;
    if (select_flgid <= 0) return E_NOEXS;

    /* 待ちビットパターンを構築 */
    UINT wait_ptn = 0;
    INT sn;
    for (sn = 0; sn < WIZNET_SOCK_NUM; sn++) {
        if (sn_mask & (1 << sn)) {
            wait_ptn |= (evt_mask & 0xFF) << (sn * 8);
        }
    }
    if (wait_ptn == 0) return E_PAR;

    /* 統合フラグで OR 待ち */
    UINT flgptn;
    ER err = tk_wai_flg(select_flgid, wait_ptn,
                        TWF_ORW | TWF_BITCLR, &flgptn, tmout);
    if (err == E_TMOUT) return E_TMOUT;
    if (err != E_OK) return E_IO;

    *result = flgptn;
    return E_OK;
}

/*----------------------------------------------------------------------
 * B-2: ソケットオプション取得 (so_getsockopt 相当)
 */
EXPORT ER tk_sock_getopt(UB sn, INT optname, void *optval, INT *optlen)
{
    if (sn >= WIZNET_SOCK_NUM || optval == NULL || optlen == NULL) return E_PAR;

    switch (optname) {
    case TK_SO_KEEPALIVE: {
        if (*optlen < (INT)sizeof(UH)) return E_PAR;
        UB val = getSn_KPALVTR(sn);
        *(UH *)optval = (UH)val * 5;
        *optlen = sizeof(UH);
        return E_OK;
    }
    case TK_SO_RCVTIMEO: {
        if (*optlen < (INT)sizeof(TMO)) return E_PAR;
        *(TMO *)optval = sock_cb[sn].rcv_tmout;
        *optlen = sizeof(TMO);
        return E_OK;
    }
    case TK_SO_SNDTIMEO: {
        if (*optlen < (INT)sizeof(TMO)) return E_PAR;
        *(TMO *)optval = sock_cb[sn].snd_tmout;
        *optlen = sizeof(TMO);
        return E_OK;
    }
    case TK_SO_ERROR: {
        if (*optlen < (INT)sizeof(ER)) return E_PAR;
        *(ER *)optval = sock_cb[sn].last_error;
        sock_cb[sn].last_error = E_OK;  /* 読み出し後クリア */
        *optlen = sizeof(ER);
        return E_OK;
    }
    case TK_SO_TYPE: {
        if (*optlen < (INT)sizeof(UB)) return E_PAR;
        *(UB *)optval = sock_cb[sn].protocol;
        *optlen = sizeof(UB);
        return E_OK;
    }
    case TK_SO_RCVBUF: {
        if (*optlen < (INT)sizeof(UH)) return E_PAR;
        *(UH *)optval = getSn_RxMAX(sn);
        *optlen = sizeof(UH);
        return E_OK;
    }
    case TK_SO_SNDBUF: {
        if (*optlen < (INT)sizeof(UH)) return E_PAR;
        *(UH *)optval = getSn_TxMAX(sn);
        *optlen = sizeof(UH);
        return E_OK;
    }
    default:
        return E_PAR;
    }
}

/*----------------------------------------------------------------------
 * B-2: ソケットオプション設定 (so_setsockopt 相当)
 */
EXPORT ER tk_sock_setopt(UB sn, INT optname, const void *optval, INT optlen)
{
    if (sn >= WIZNET_SOCK_NUM || optval == NULL) return E_PAR;

    switch (optname) {
    case TK_SO_KEEPALIVE: {
        if (optlen < (INT)sizeof(UH)) return E_PAR;
        return tk_sock_set_keepalive(sn, *(const UH *)optval);
    }
    case TK_SO_RCVTIMEO: {
        if (optlen < (INT)sizeof(TMO)) return E_PAR;
        sock_cb[sn].rcv_tmout = *(const TMO *)optval;
        return E_OK;
    }
    case TK_SO_SNDTIMEO: {
        if (optlen < (INT)sizeof(TMO)) return E_PAR;
        sock_cb[sn].snd_tmout = *(const TMO *)optval;
        return E_OK;
    }
    /* R/O オプション */
    case TK_SO_ERROR:
    case TK_SO_TYPE:
    case TK_SO_RCVBUF:
    case TK_SO_SNDBUF:
        return E_PAR;  /* 読取専用 */
    default:
        return E_PAR;
    }
}

/*----------------------------------------------------------------------
 * ソケット初期化
 */
EXPORT void tk_sock_init(void)
{
    memset(sock_cb, 0, sizeof(sock_cb));
    auto_port = 49152;
    hostname_len = 0;
    hostname[0] = '\0';
    CreateLock(&imr_lock, (CONST UB *)"wimr");

    /* select 用統合イベントフラグ */
    {
        T_CFLG cflg;
        cflg.exinf   = NULL;
        cflg.flgatr  = TA_TPRI | TA_WMUL;
        cflg.iflgptn = 0;
        select_flgid = tk_cre_flg(&cflg);
    }
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
