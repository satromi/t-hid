/*
 *----------------------------------------------------------------------
 *    WIZnet Ethernet mSDI Device Driver for μT-Kernel 3.0 BSP
 *
 *    mSDI デバイスドライバ — TCP/IP ソケット操作を
 *    μT-Kernel デバイスインターフェースで公開する
 *    (現状は RP2040 + W5100S ビルドでのみ使用)
 *
 *    RTOS 機能の活用:
 *    - イベントフラグ: WIZnet INTn 割り込み → タスクへの非同期通知
 *    - ブロッキング受信: tk_wai_flg() でデータ到着まで CPU 解放
 *    - FastLock: SPI アクセスのタスク間排他制御
 *    - サブデバイス: WIZNET_SOCK_NUM ソケットを独立したデバイスとして公開
 *
 *    デバイス名: "neta" (nsub=WIZNET_SOCK_NUM)
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(CPU_RP2040) || defined(MTKBSP_CPU_STM32H5)

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>

#include "../include/dev_wiznet.h"
#include "../common/drvif/msdrvif.h"

/* チップ依存ヘッダ (レジスタ定義 + SPI HAL) */
#if defined(WIZCHIP_W5500)
#include "wiznet_drv.h"
#include "sysdepend/w5500/w5500_reg.h"
#else
#include "w5100s_reg.h"
#include "sysdepend/w5100s/w5100s_spi.h"
#endif

/* μT-Kernel ネイティブソケット */
#include "tk_socket.h"

/* DHCP クライアント */
#include "tk_dhcp.h"

/*----------------------------------------------------------------------
 * ドライバ制御ブロック
 */
typedef struct {
    UW    unit;
    UINT  omode;
} T_WIZNET_DCB;

LOCAL T_WIZNET_DCB dev_wiznet_cb;
LOCAL wiz_NetInfo net_info;

/*----------------------------------------------------------------------
 * サブデバイス番号 → ソケット番号変換
 *
 * mSDI のサブデバイスは物理デバイス番号から求める。
 * mSDI は devid = 物理ID + (subno+1) で管理している。
 * T_DEVREQ.devid にはオープン時の devid が入る。
 * ここでは req->start の値で判定するのではなく、
 * open 時に devid からサブユニット番号を取得する方式にする。
 *
 * 簡易的にサブデバイス番号を devreq の exinf 経由で渡す。
 * → mSDI の仕組みでは直接取れないので、open/close で記録する。
 *
 * 代替案: start パラメータでソケット番号を指定する方式
 *   tk_swri_dev(dd, socket_no, data, len, &asz)
 *   → start = ソケット番号 (0-3) でデータ送受信
 */

/*----------------------------------------------------------------------
 * mSDI コールバック
 */
LOCAL ER dev_wiznet_openfn(ID devid, UINT omode, T_MSDI *p_msdi)
{
    (void)devid; (void)omode; (void)p_msdi;
    return E_OK;
}

LOCAL ER dev_wiznet_closefn(ID devid, UINT omode, T_MSDI *p_msdi)
{
    (void)devid; (void)omode; (void)p_msdi;
    return E_OK;
}

/*----------------------------------------------------------------------
 * ソケット番号の決定
 *
 * start パラメータの使い方:
 *   start >= 0 : データ送受信 (start = ソケット番号 0-3)
 *   start < 0  : 属性操作 (TDN_NET_IP, TDN_SOC_CONNECT 等)
 *
 * ソケット属性操作 (TDN_SOC_*) では req->size の上位バイトに
 * ソケット番号をエンコードする代わりに、TDN_SOC_CONNECT 等に
 * ソケット番号 offset を加算する方式:
 *   TDN_SOC_CONNECT + 0 = ソケット0, +1 = ソケット1, ...
 */
#define SOC_NUM_FROM_ATR(atr, base)  ((INT)(atr) - (INT)(base))

/*----------------------------------------------------------------------
 * データ送信 — start = ソケット番号 (0-3)
 *
 * tk_sock_send() が SENDOK 割り込み + tk_wai_flg() でブロッキング送信を行う。
 * TX バッファ空き待ち・送信完了待ちの間、タスクは CPU を他に譲る。
 */
LOCAL INT dev_wiznet_writefn(T_DEVREQ *req, T_MSDI *p_msdi)
{
    (void)p_msdi;

    if (req->start >= 0 && req->start < WIZNET_SOCK_NUM) {
        /* データ送信 (tk_socket: 割り込み駆動) */
        INT sn = (INT)req->start;

        if (req->size <= 0 || req->buf == NULL) {
            req->asize = 0;
            return E_PAR;
        }

        W sent = tk_sock_send(sn, (const UB *)req->buf, (UH)req->size, 30000);
        if (sent < 0) {
            req->asize = 0;
            return (sent == E_TMOUT) ? E_TMOUT : E_IO;
        }
        req->asize = sent;
        return E_OK;
    }

    /* 属性書き込み (start < 0) */

    /* --- ネットワークアダプタ属性 --- */
    switch (req->start) {
    case TDN_NET_IP:
        if (req->size < 4) return E_PAR;
        memcpy(net_info.ip, req->buf, 4);
        setSIPR(net_info.ip);
        req->asize = 4;
        return E_OK;

    case TDN_NET_MASK:
        if (req->size < 4) return E_PAR;
        memcpy(net_info.sn, req->buf, 4);
        setSUBR(net_info.sn);
        req->asize = 4;
        return E_OK;

    case TDN_NET_GW:
        if (req->size < 4) return E_PAR;
        memcpy(net_info.gw, req->buf, 4);
        setGAR(net_info.gw);
        req->asize = 4;
        return E_OK;

    case TDN_NET_MAC:
        if (req->size < 6) return E_PAR;
        memcpy(net_info.mac, req->buf, 6);
        setSHAR(net_info.mac);
        req->asize = 6;
        return E_OK;
    }

    /* --- ソケット属性 (範囲チェック方式: WIZNET_SOCK_NUM ソケット対応) --- */
    /*
     * 各 TDN_SOC_* は base + sn (0..N-1) で指定。
     * TDN_SOC_CONNECT=-110, +0..+7 → -110..-103
     * TDN_SOC_LISTEN =-120, +0..+7 → -120..-113
     * TDN_SOC_STATUS =-130, +0..+7 → -130..-123
     * TDN_SOC_CLOSE  =-140, +0..+7 → -140..-133
     * TDN_SOC_UDP    =-150, +0..+7 → -150..-143
     * 各 base は 10 ずつ離れており、N=8 までなら重複しない。
     */
    {
        INT off;

        /* TCP 接続 */
        off = SOC_NUM_FROM_ATR(req->start, TDN_SOC_CONNECT);
        if (off >= 0 && off < WIZNET_SOCK_NUM) {
            INT sn = off;
            T_NET_ADDR *addr = (T_NET_ADDR *)req->buf;
            if (req->size < (W)sizeof(T_NET_ADDR)) return E_PAR;

            ER err = tk_sock_open(sn, Sn_MR_TCP, 0);
            if (err != E_OK) return err;

            err = tk_sock_connect(sn, addr->ip, addr->port, 10000);
            if (err != E_OK) return err;

            req->asize = sizeof(T_NET_ADDR);
            return E_OK;
        }

        /* TCP リッスン + 接続受付 */
        off = SOC_NUM_FROM_ATR(req->start, TDN_SOC_LISTEN);
        if (off >= 0 && off < WIZNET_SOCK_NUM) {
            INT sn = off;
            if (req->size < 2) return E_PAR;
            UH port = *(UH *)req->buf;

            ER err = tk_sock_open(sn, Sn_MR_TCP, port);
            if (err != E_OK) return err;

            err = tk_sock_listen(sn);
            if (err != E_OK) return err;

            err = tk_sock_accept(sn, TMO_FEVR);
            if (err != E_OK) { tk_sock_close(sn); return err; }

            req->asize = 2;
            return E_OK;
        }

        /* ソケットクローズ */
        off = SOC_NUM_FROM_ATR(req->start, TDN_SOC_CLOSE);
        if (off >= 0 && off < WIZNET_SOCK_NUM) {
            tk_sock_close((INT)off);
            req->asize = 0;
            return E_OK;
        }

        /* UDP ソケット */
        off = SOC_NUM_FROM_ATR(req->start, TDN_SOC_UDP);
        if (off >= 0 && off < WIZNET_SOCK_NUM) {
            INT sn = off;
            if (req->size < 2) return E_PAR;
            UH port = *(UH *)req->buf;

            ER err = tk_sock_open(sn, Sn_MR_UDP, port);
            if (err != E_OK) return err;

            req->asize = 2;
            return E_OK;
        }
    }

    return E_NOSPT;
}

/*----------------------------------------------------------------------
 * データ受信 — start = ソケット番号 (0-3)
 *
 * tk_sock_recv() が RECV 割り込み + tk_wai_flg() でブロッキング受信を行う。
 * データ到着まで、タスクはスリープし CPU を他タスクに明け渡す。
 */
LOCAL INT dev_wiznet_readfn(T_DEVREQ *req, T_MSDI *p_msdi)
{
    (void)p_msdi;

    if (req->start >= 0 && req->start < WIZNET_SOCK_NUM) {
        /* データ受信 (tk_socket: 割り込み駆動ブロッキング) */
        INT sn = (INT)req->start;

        if (req->size <= 0 || req->buf == NULL) {
            req->asize = 0;
            return E_PAR;
        }

        W rcvd = tk_sock_recv(sn, (UB *)req->buf, (UH)req->size, 30000);
        if (rcvd < 0) {
            req->asize = 0;
            return (rcvd == E_TMOUT) ? E_TMOUT : E_IO;
        }
        req->asize = rcvd;
        return E_OK;
    }

    /* 属性読み出し (start < 0) */
    switch (req->start) {
    case TDN_NET_IP:
        if (req->size < 4) return E_PAR;
        getSIPR((uint8_t *)req->buf);
        req->asize = 4;
        return E_OK;

    case TDN_NET_MASK:
        if (req->size < 4) return E_PAR;
        getSUBR((uint8_t *)req->buf);
        req->asize = 4;
        return E_OK;

    case TDN_NET_GW:
        if (req->size < 4) return E_PAR;
        getGAR((uint8_t *)req->buf);
        req->asize = 4;
        return E_OK;

    case TDN_NET_MAC:
        if (req->size < 6) return E_PAR;
        getSHAR((uint8_t *)req->buf);
        req->asize = 6;
        return E_OK;

    case TDN_NET_STATUS: {
        if (req->size < 4) return E_PAR;
        /* PHY リンク状態 (WIZnet PHYSR レジスタ) */
        UW status = (wizphy_getphylink() == PHY_LINK_ON) ? 1 : 0;
        *(UW *)req->buf = status;
        req->asize = 4;
        return E_OK;
    }
    }

    /* --- ソケット状態 (範囲チェック方式) --- */
    {
        INT off = SOC_NUM_FROM_ATR(req->start, TDN_SOC_STATUS);
        if (off >= 0 && off < WIZNET_SOCK_NUM) {
            if (req->size < 1) return E_PAR;
            *(UB *)req->buf = getSn_SR((INT)off);
            req->asize = 1;
            return E_OK;
        }
    }

    return E_NOSPT;
}

LOCAL INT dev_wiznet_eventfn(INT evttyp, void *evtinf, T_MSDI *p_msdi)
{
    (void)evttyp; (void)evtinf; (void)p_msdi;
    return E_NOSPT;
}

/*----------------------------------------------------------------------
 * WIZnet デバイスドライバ初期化
 */
EXPORT ER dev_init_wiznet(UW unit)
{
    T_DMSDI   dmsdi;
    T_IDEV    idev;
    T_MSDI    *p_msdi;
    ER        err;

    memset(&dev_wiznet_cb, 0, sizeof(dev_wiznet_cb));
    dev_wiznet_cb.unit = unit;

    /*
     *  ソケット制御ブロック初期化
     */
    tk_sock_init();

    /*
     *  SPI 初期化 + タスク + イベントフラグ (割り込みはまだ登録しない)
     */
    wiznet_spi_init();

    /*
     *  WIZnet ハードウェアリセット (tk_dly_tsk 使用 — タスクに CPU 譲る)
     *  リセットにより INTn が HIGH に戻り、スプリアス割り込みを防止
     */
    wiznet_hw_reset();

    /*
     *  GPIO 割り込み登録 (HW リセット後 = INTn が安定してから)
     */
    wiznet_int_init();

    /*
     *  WIZnet チップ初期化
     *  ソケットバッファ: 各 2KB × N
     *    W5100S (4 sockets): 2KB × 4 = 8KB TX + 8KB RX = 16KB (全容量)
     *    W5500  (8 sockets): 2KB × 8 = 16KB TX + 16KB RX = 32KB (全容量)
     */
    {
        uint8_t txsize[WIZNET_SOCK_NUM];
        uint8_t rxsize[WIZNET_SOCK_NUM];
        int i;
        for (i = 0; i < WIZNET_SOCK_NUM; i++) {
            txsize[i] = 2;
            rxsize[i] = 2;
        }
        int8_t ret = wizchip_init(txsize, rxsize);
        if (ret < 0) {
            W5DBG("WIZnet: wizchip_init failed: %d\n", ret);
            return E_IO;
        }
    }

    /*
     *  デフォルトネットワーク設定
     */
    memset(&net_info, 0, sizeof(net_info));
    net_info.mac[0] = 0x00; net_info.mac[1] = 0x08; net_info.mac[2] = 0xDC;
    net_info.mac[3] = 0x12; net_info.mac[4] = 0x34; net_info.mac[5] = 0x56;
    net_info.ip[0] = 192; net_info.ip[1] = 168;
    net_info.ip[2] = 0;   net_info.ip[3] = 100;
    net_info.sn[0] = 255; net_info.sn[1] = 255;
    net_info.sn[2] = 255; net_info.sn[3] = 0;
    net_info.gw[0] = 192; net_info.gw[1] = 168;
    net_info.gw[2] = 0;   net_info.gw[3] = 1;
    net_info.dhcp = NETINFO_STATIC;
    wizchip_setnetinfo(&net_info);

    /* SPI 書き込み検証 (RP2040 + W5100S デバッグ時のみ) */
#if W5100S_DEBUG_SPI && defined(CPU_RP2040)
    {
        /* SPI0 診断 */
        W5DBG_SPI("SPI0: CR0=0x%08x CR1=0x%08x SR=0x%08x CPSR=0x%08x\n",
            in_w(0x4003C000 + 0x00), in_w(0x4003C000 + 0x04),
            in_w(0x4003C000 + 0x0C), in_w(0x4003C000 + 0x10));
        W5DBG_SPI("GP16=%d GP17=%d GP18=%d GP19=%d\n",
            in_w(0x40014000 + 0x04 + 16*8) & 0x1F,
            in_w(0x40014000 + 0x04 + 17*8) & 0x1F,
            in_w(0x40014000 + 0x04 + 18*8) & 0x1F,
            in_w(0x40014000 + 0x04 + 19*8) & 0x1F);
        W5DBG_SPI("RESETS_DONE=0x%08x (SPI0=%d)\n",
            in_w(0x4000C008), (in_w(0x4000C008) >> 16) & 1);

        /* テスト -1: GPIO ビットバング SPI で W5100S VERR 読み出し */
        {
            /* GP16(MISO),17(CS),18(SCLK),19(MOSI) を SIO に切替 */
            out_w(0x40014000 + 0x04 + 16*8, 5); /* GP16 SIO */
            out_w(0x40014000 + 0x04 + 18*8, 5); /* GP18 SIO */
            out_w(0x40014000 + 0x04 + 19*8, 5); /* GP19 SIO */
            out_w(GPIO_OE_SET, (1u<<18)|(1u<<19)); /* SCLK,MOSI output */
            out_w(GPIO_OE_CLR, (1u<<16));           /* MISO input */
            out_w(GPIO(16), (1<<6)|(1<<4)|(1<<1));  /* MISO: IE,4mA,Schmitt */
            out_w(GPIO_OUT_CLR, (1u<<18));           /* SCLK LOW */

            /* CS assert */
            out_w(GPIO_OUT_CLR, (1u<<17));

            /* 送信: 0x0F (read opcode), 0x00, 0x80 (VERR addr) */
            UB tx_data[3] = {0x0F, 0x00, 0x80};
            INT bi, by;
            for (by = 0; by < 3; by++) {
                for (bi = 7; bi >= 0; bi--) {
                    if (tx_data[by] & (1<<bi))
                        out_w(GPIO_OUT_SET, (1u<<19));
                    else
                        out_w(GPIO_OUT_CLR, (1u<<19));
                    for (volatile int d=0; d<10; d++) {}
                    out_w(GPIO_OUT_SET, (1u<<18)); /* SCLK HIGH */
                    for (volatile int d=0; d<10; d++) {}
                    out_w(GPIO_OUT_CLR, (1u<<18)); /* SCLK LOW */
                }
            }
            /* 受信: 8 bits (MISO) */
            out_w(GPIO_OUT_CLR, (1u<<19)); /* MOSI LOW */
            UB rx_val = 0;
            for (bi = 7; bi >= 0; bi--) {
                for (volatile int d=0; d<10; d++) {}
                out_w(GPIO_OUT_SET, (1u<<18));
                for (volatile int d=0; d<10; d++) {}
                if (in_w(GPIO_IN) & (1u<<16)) rx_val |= (1<<bi);
                out_w(GPIO_OUT_CLR, (1u<<18));
            }

            /* CS deassert */
            out_w(GPIO_OUT_SET, (1u<<17));

            W5DBG_SPI("W5100S bitbang VERR=0x%02x (expect 0x51)\n", rx_val);

            /* SPI 機能に戻す */
            out_w(0x40014000 + 0x04 + 16*8, 1); /* GP16 SPI */
            out_w(0x40014000 + 0x04 + 18*8, 1); /* GP18 SPI */
            out_w(0x40014000 + 0x04 + 19*8, 1); /* GP19 SPI */
        }

        /* テスト 0: SPI ループバック (MOSI→MISO 内部接続) */
        {
            /* CR1 LBM=1 (loopback mode) */
            out_w(0x4003C000 + 0x04, 0x03);  /* SSE=1, LBM=1 */

            /* ダミーRX排出 */
            while (in_w(0x4003C000 + 0x0C) & (1<<2))
                (void)in_w(0x4003C000 + 0x08);

            /* TX: 0xAB 送信 */
            out_w(0x4003C000 + 0x08, 0xAB);
            /* RX 待ち */
            {
                INT t = 100000;
                while (!(in_w(0x4003C000 + 0x0C) & (1<<2)) && --t > 0) {}
                UB rx = (t > 0) ? (in_w(0x4003C000 + 0x08) & 0xFF) : 0xEE;
                W5DBG_SPI("SPI0 loopback: TX=AB RX=%02x %s\n",
                    rx, (rx == 0xAB) ? "OK" : "FAIL");
            }

            /* LBM 解除 */
            out_w(0x4003C000 + 0x04, 0x02);  /* SSE=1, LBM=0 */
        }

        /* テスト 1: WIZCHIP_WRITE 経由 */
        UB before = WIZCHIP_READ(0x0001);
        WIZCHIP_WRITE(0x0001, 0xAB);
        UB after = WIZCHIP_READ(0x0001);
        W5DBG_SPI("W5100S: test1 burst: before:%02x write:AB read:%02x %s\n",
            before, after, (after == 0xAB) ? "OK" : "FAIL");

        /* チップ識別: Version Register (0x0080) = 0x51 for W5100S */
        UB ver = WIZCHIP_READ(0x0080);
        W5DBG_SPI("W5100S: VERR=0x%02x (expect 0x51)\n", ver);

        /* テスト 2: 1byte ずつ送信 (_write_byte 経由) */
        WIZCHIP_CRITICAL_ENTER();
        WIZCHIP.CS._select();
        WIZCHIP.IF.SPI._write_byte(0xF0);  /* write opcode */
        WIZCHIP.IF.SPI._write_byte(0x00);  /* addr high */
        WIZCHIP.IF.SPI._write_byte(0x01);  /* addr low (GAR[0]) */
        WIZCHIP.IF.SPI._write_byte(0xCD);  /* data */
        WIZCHIP.CS._deselect();
        WIZCHIP_CRITICAL_EXIT();

        UB after2 = WIZCHIP_READ(0x0001);
        W5DBG_SPI("W5100S: test2 byte: write:CD read:%02x %s\n",
            after2, (after2 == 0xCD) ? "OK" : "FAIL");

        /* netinfo 読み戻し */
        wiz_NetInfo readback;
        wizchip_getnetinfo(&readback);
        W5DBG_SPI("W5100S: verify IP=%d.%d.%d.%d MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
            readback.ip[0], readback.ip[1], readback.ip[2], readback.ip[3],
            readback.mac[0], readback.mac[1], readback.mac[2],
            readback.mac[3], readback.mac[4], readback.mac[5]);
        UB mr = getMR();
        W5DBG_SPI("W5100S: MR=0x%02x\n", mr);
    }
#endif /* W5100S_DEBUG_SPI */

    /*
     *  WIZnet グローバル割り込みマスク設定
     *  (個別ソケットは open 時に有効化)
     */
    setIMR(0);  /* 初期は全ソケット割り込み無効 */

    /*
     *  mSDI デバイス登録
     */
    strcpy((char *)dmsdi.devnm, WIZNET_DEVNM);
    dmsdi.exinf   = &dev_wiznet_cb;
    dmsdi.drvatr  = 0;
    dmsdi.devatr  = TDK_UNDEF;
    dmsdi.nsub    = WIZNET_SOCK_NUM;
    dmsdi.blksz   = 1;
    dmsdi.openfn  = dev_wiznet_openfn;
    dmsdi.closefn = dev_wiznet_closefn;
    dmsdi.readfn  = dev_wiznet_readfn;
    dmsdi.writefn = dev_wiznet_writefn;
    dmsdi.eventfn = dev_wiznet_eventfn;

    err = msdi_def_dev(&dmsdi, &idev, &p_msdi);
    if (err != E_OK) return err;

    W5DBG("WIZnet: ready  IP=%d.%d.%d.%d  MAC=%02x:%02x:%02x:%02x:%02x:%02x\n",
          net_info.ip[0], net_info.ip[1], net_info.ip[2], net_info.ip[3],
          net_info.mac[0], net_info.mac[1], net_info.mac[2],
          net_info.mac[3], net_info.mac[4], net_info.mac[5]);

    /* DHCP クライアント開始 (最終ソケット = WIZNET_SOCK_NUM - 1 を使用) */
    {
        /* DHCP: SIPR はゼロにしない。
         * WIZnet は SIPR=0.0.0.0 だと UDP ブロードキャスト受信を
         * フィルタしてしまい、OFFER を受け取れない。
         * DHCP メッセージ内の ciaddr=0 で正しく動作する。*/
        const UB dhcp_sn = WIZNET_SOCK_NUM - 1;
        ER dhcp_err = tk_dhcp_start(dhcp_sn, NULL, NULL);
        if (dhcp_err == E_OK) {
            W5DBG("WIZnet: DHCP started (socket %d)\n", dhcp_sn);
        } else {
            W5DBG("WIZnet: DHCP start failed (%d), using static IP\n", dhcp_err);
            /* 失敗時は静的 IP に戻す */
            setSIPR(net_info.ip);
        }
    }

    return E_OK;
}

#endif /* CPU_RP2040 || MTKBSP_CPU_STM32H5 */
