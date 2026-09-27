/*
 *----------------------------------------------------------------------
 *    W5100S チップ管理 — wizchip_conf.c の W5100S 専用置き換え
 *
 *    ioLibrary の wizchip_conf.c (700行+, 全チップ対応) を
 *    W5100S 専用に縮小 (~150行)。
 *
 *    提供するもの:
 *    - WIZCHIP グローバル構造体 (w5100s.h のレジスタマクロが参照)
 *    - reg_wizchip_*_cbfunc() — SPI コールバック登録
 *    - wizchip_init() — ソケットバッファサイズ設定
 *    - wizchip_setnetinfo() / wizchip_getnetinfo()
 *    - wizchip_sw_reset() / wizphy_getphylink()
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <string.h>

#include "w5100s_reg.h"

/*----------------------------------------------------------------------
 * デフォルトコールバック (何もしない)
 *
 * w5100s_spi.c にも同名の LOCAL 関数があるため、
 * こちらは _default サフィックスで名前衝突を回避する。
 * WIZCHIP 構造体の初期値としてのみ使用される。
 */
LOCAL void wizchip_cris_enter_default(void) {}
LOCAL void wizchip_cris_exit_default(void) {}
LOCAL void wizchip_cs_select_default(void) {}
LOCAL void wizchip_cs_deselect_default(void) {}

LOCAL iodata_t wizchip_bus_readdata_default(uint32_t AddrSel) {
    return *((volatile iodata_t *)((ptrdiff_t)AddrSel));
}
LOCAL void wizchip_bus_writedata_default(uint32_t AddrSel, iodata_t wb) {
    *((volatile iodata_t *)((ptrdiff_t)AddrSel)) = wb;
}

LOCAL uint8_t wizchip_spi_readbyte_default(void) { return 0; }
LOCAL void wizchip_spi_writebyte_default(uint8_t wb) { (void)wb; }
LOCAL void wizchip_spi_readburst_default(uint8_t *pBuf, uint16_t len) { (void)pBuf; (void)len; }
LOCAL void wizchip_spi_writeburst_default(uint8_t *pBuf, uint16_t len) { (void)pBuf; (void)len; }

/*----------------------------------------------------------------------
 * WIZCHIP グローバル構造体
 * w5100s.h の全マクロ (getSn_SR, setSn_CR 等) が参照する。
 */
_WIZCHIP WIZCHIP = {
    _WIZCHIP_IO_MODE_,
    _WIZCHIP_ID_,
    { wizchip_cris_enter_default, wizchip_cris_exit_default },
    { wizchip_cs_select_default, wizchip_cs_deselect_default },
    { { wizchip_bus_readdata_default, wizchip_bus_writedata_default } }
};

/*----------------------------------------------------------------------
 * コールバック登録
 */
void reg_wizchip_cris_cbfunc(void(*cris_en)(void), void(*cris_ex)(void))
{
    WIZCHIP.CRIS._enter = cris_en ? cris_en : wizchip_cris_enter_default;
    WIZCHIP.CRIS._exit  = cris_ex ? cris_ex : wizchip_cris_exit_default;
}

void reg_wizchip_cs_cbfunc(void(*cs_sel)(void), void(*cs_desel)(void))
{
    WIZCHIP.CS._select   = cs_sel   ? cs_sel   : wizchip_cs_select_default;
    WIZCHIP.CS._deselect = cs_desel ? cs_desel : wizchip_cs_deselect_default;
}

void reg_wizchip_spi_cbfunc(uint8_t(*spi_rb)(void), void(*spi_wb)(uint8_t wb))
{
    WIZCHIP.IF.SPI._read_byte  = spi_rb ? spi_rb : wizchip_spi_readbyte_default;
    WIZCHIP.IF.SPI._write_byte = spi_wb ? spi_wb : wizchip_spi_writebyte_default;
}

void reg_wizchip_spiburst_cbfunc(void(*spi_rb)(uint8_t*, uint16_t),
                                  void(*spi_wb)(uint8_t*, uint16_t))
{
    WIZCHIP.IF.SPI._read_burst  = spi_rb ? spi_rb : wizchip_spi_readburst_default;
    WIZCHIP.IF.SPI._write_burst = spi_wb ? spi_wb : wizchip_spi_writeburst_default;
}

/*----------------------------------------------------------------------
 * ソフトウェアリセット (W5100S 専用)
 */
void wizchip_sw_reset(void)
{
    uint8_t mac[6], gw[4], sn[4], sip[4];

    getSHAR(mac);
    getGAR(gw);
    getSUBR(sn);
    getSIPR(sip);

    setMR(MR_RST);
    getMR();  /* delay */

    setSHAR(mac);
    setGAR(gw);
    setSUBR(sn);
    setSIPR(sip);
}

/*----------------------------------------------------------------------
 * チップ初期化 — ソケットバッファサイズ設定
 *
 * W5100S: 全 8KB TX + 8KB RX。各ソケットの合計が 8 以下であること。
 * バッファサイズはログ2値で書き込む (W5100/W5100S 仕様)。
 *   0→0KB, 1→1KB(val=0), 2→2KB(val=1), 4→4KB(val=2), 8→8KB(val=3)
 */
int8_t wizchip_init(uint8_t *txsize, uint8_t *rxsize)
{
    int8_t i, tmp;

    wizchip_sw_reset();

    if (txsize) {
        tmp = 0;
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
            tmp += txsize[i];
            if (tmp > 8) return -1;
        }
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
            /*
             * log2 変換: 0→0(※), 1→0, 2→1, 4→2, 8→3
             * ※ W5100S は TMSR/RMSR の 2bit 値 0 を 1KB として扱うため
             *   0KB バッファは実現できない。txsize=0 は 1KB として扱う。
             */
            uint8_t val = 0;
            uint8_t sz = txsize[i];
            while (sz > 1) { sz >>= 1; val++; }
            setSn_TXBUF_SIZE(i, val);
        }
    }
    if (rxsize) {
        tmp = 0;
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
            tmp += rxsize[i];
            if (tmp > 8) return -1;
        }
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
            uint8_t val = 0;
            uint8_t sz = rxsize[i];
            while (sz > 1) { sz >>= 1; val++; }
            setSn_RXBUF_SIZE(i, val);
        }
    }
    return 0;
}

/*----------------------------------------------------------------------
 * ネットワーク情報 設定/取得
 */
void wizchip_setnetinfo(wiz_NetInfo *pnetinfo)
{
    setSHAR(pnetinfo->mac);
    setGAR(pnetinfo->gw);
    setSUBR(pnetinfo->sn);
    setSIPR(pnetinfo->ip);
}

void wizchip_getnetinfo(wiz_NetInfo *pnetinfo)
{
    getSHAR(pnetinfo->mac);
    getGAR(pnetinfo->gw);
    getSUBR(pnetinfo->sn);
    getSIPR(pnetinfo->ip);
}

/*----------------------------------------------------------------------
 * PHY リンク状態取得 (W5100S: PHYSR レジスタ)
 */
int8_t wizphy_getphylink(void)
{
    if (getPHYSR() & PHYSR_LNK) {
        return PHY_LINK_ON;
    }
    return PHY_LINK_OFF;
}

#endif /* CPU_RP2040 */
