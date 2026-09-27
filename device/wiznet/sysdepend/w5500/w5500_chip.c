/*
 *----------------------------------------------------------------------
 *    W5500 チップ制御 — 初期化、PHY、ネットワーク情報
 *----------------------------------------------------------------------
 */

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <string.h>

#include "w5500_reg.h"
#include "../../wiznet_drv.h"

/* コールバックデフォルト */
LOCAL void wizchip_cris_enter_default(void) {}
LOCAL void wizchip_cris_exit_default(void) {}
LOCAL void wizchip_cs_select_default(void) {}
LOCAL void wizchip_cs_deselect_default(void) {}
LOCAL uint8_t wizchip_spi_readbyte_default(void) { return 0; }
LOCAL void wizchip_spi_writebyte_default(uint8_t wb) { (void)wb; }
LOCAL void wizchip_spi_readburst_default(uint8_t *pBuf, uint16_t len) { (void)pBuf; (void)len; }
LOCAL void wizchip_spi_writeburst_default(uint8_t *pBuf, uint16_t len) { (void)pBuf; (void)len; }

/* グローバル WIZCHIP 構造体 */
_WIZCHIP WIZCHIP = {
    { wizchip_cris_enter_default, wizchip_cris_exit_default },
    { wizchip_cs_select_default,  wizchip_cs_deselect_default },
    { .SPI = {
        wizchip_spi_readbyte_default, wizchip_spi_writebyte_default,
        wizchip_spi_readburst_default, wizchip_spi_writeburst_default
    }}
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

void reg_wizchip_spi_cbfunc(uint8_t(*spi_rb)(void), void(*spi_wb)(uint8_t))
{
    WIZCHIP.IF.SPI._read_byte  = spi_rb ? spi_rb : wizchip_spi_readbyte_default;
    WIZCHIP.IF.SPI._write_byte = spi_wb ? spi_wb : wizchip_spi_writebyte_default;
}

void reg_wizchip_spiburst_cbfunc(void(*spi_rb)(uint8_t*,uint16_t),
                                  void(*spi_wb)(uint8_t*,uint16_t))
{
    WIZCHIP.IF.SPI._read_burst  = spi_rb ? spi_rb : wizchip_spi_readburst_default;
    WIZCHIP.IF.SPI._write_burst = spi_wb ? spi_wb : wizchip_spi_writeburst_default;
}

/*----------------------------------------------------------------------
 * ソケットレジスタアクセスマクロ (w5500_io.c と共通)
 */
#define Sn_ADDR(sn, off)  (((uint32_t)(off) << 8) | (((1 + 4*(sn)) & 0x1F) << 3))
#define CR_ADDR(off)       ((uint32_t)(off) << 8)

/*----------------------------------------------------------------------
 * ソフトリセット
 */
void wizchip_sw_reset(void)
{
    uint8_t mac[6], gw[4], sn[4], sip[4];

    /* ネットワーク設定を退避 */
    getSHAR(mac); getGAR(gw); getSUBR(sn); getSIPR(sip);

    /* リセット */
    WIZCHIP_WRITE(CR_ADDR(MR), MR_RST);
    WIZCHIP_READ(CR_ADDR(MR));  /* 安定化待ち */

    /* 復元 */
    setSHAR(mac); setGAR(gw); setSUBR(sn); setSIPR(sip);
}

/*----------------------------------------------------------------------
 * チップ初期化 (ソケットバッファサイズ設定)
 *
 * W5500: 各ソケットに個別の Sn_TXBUF_SIZE / Sn_RXBUF_SIZE レジスタ。
 *        値は直接 KB 単位 (0=0KB, 1=1KB, 2=2KB, ..., 16=16KB)。
 *        合計は TX 16KB, RX 16KB まで。
 */
int8_t wizchip_init(uint8_t *txsize, uint8_t *rxsize)
{
    uint8_t i;
    uint16_t tmp;

    wizchip_sw_reset();

    if (txsize) {
        tmp = 0;
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
            tmp += txsize[i];
            if (tmp > 16) return -1;  /* TX 合計 16KB 超過 */
        }
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
            WIZCHIP_WRITE(Sn_ADDR(i, Sn_TXBUF_SIZE), txsize[i]);
        }
    }

    if (rxsize) {
        tmp = 0;
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
            tmp += rxsize[i];
            if (tmp > 16) return -1;  /* RX 合計 16KB 超過 */
        }
        for (i = 0; i < _WIZCHIP_SOCK_NUM_; i++) {
            WIZCHIP_WRITE(Sn_ADDR(i, Sn_RXBUF_SIZE), rxsize[i]);
        }
    }

    return 0;
}

/*----------------------------------------------------------------------
 * ネットワーク情報設定/取得
 */
void getSIPR(uint8_t *ip)
{
    WIZCHIP_READ_BUF(CR_ADDR(SIPR), ip, 4);
}

void setSIPR(const uint8_t *ip)
{
    WIZCHIP_WRITE_BUF(CR_ADDR(SIPR), (uint8_t *)ip, 4);
}

void getSUBR(uint8_t *mask)
{
    WIZCHIP_READ_BUF(CR_ADDR(SUBR), mask, 4);
}

void setSUBR(const uint8_t *mask)
{
    WIZCHIP_WRITE_BUF(CR_ADDR(SUBR), (uint8_t *)mask, 4);
}

void getGAR(uint8_t *gw)
{
    WIZCHIP_READ_BUF(CR_ADDR(GAR), gw, 4);
}

void setGAR(const uint8_t *gw)
{
    WIZCHIP_WRITE_BUF(CR_ADDR(GAR), (uint8_t *)gw, 4);
}

void getSHAR(uint8_t *mac)
{
    WIZCHIP_READ_BUF(CR_ADDR(SHAR), mac, 6);
}

void setSHAR(const uint8_t *mac)
{
    WIZCHIP_WRITE_BUF(CR_ADDR(SHAR), (uint8_t *)mac, 6);
}

uint8_t getIMR(void)
{
    return WIZCHIP_READ(CR_ADDR(SIMR));  /* W5500: SIMR = Socket Interrupt Mask */
}

void setIMR(uint8_t mask)
{
    WIZCHIP_WRITE(CR_ADDR(SIMR), mask);
}

void wizchip_setnetinfo(wiz_NetInfo *info)
{
    setSHAR(info->mac);
    setGAR(info->gw);
    setSUBR(info->sn);
    setSIPR(info->ip);
}

void wizchip_getnetinfo(wiz_NetInfo *info)
{
    getSHAR(info->mac);
    getGAR(info->gw);
    getSUBR(info->sn);
    getSIPR(info->ip);
}

/*----------------------------------------------------------------------
 * PHY リンク状態取得
 */
int8_t wizphy_getphylink(void)
{
    if (WIZCHIP_READ(CR_ADDR(PHYCFGR)) & PHYCFGR_LNK) {
        return PHY_LINK_ON;
    }
    return PHY_LINK_OFF;
}

/*----------------------------------------------------------------------
 * ソケットレジスタアクセス (上位層から使用)
 *
 * これらは wiznet_drv.h のマクロ or tk_socket.c から呼ばれる。
 */

/* ソケット IR/IMR */
uint8_t getSn_IR(uint8_t sn)  { return WIZCHIP_READ(Sn_ADDR(sn, Sn_IR)); }
void    setSn_IR(uint8_t sn, uint8_t v) { WIZCHIP_WRITE(Sn_ADDR(sn, Sn_IR), v); }
uint8_t getSn_IMR_reg(uint8_t sn) { return WIZCHIP_READ(Sn_ADDR(sn, Sn_IMR)); }
void    setSn_IMR_reg(uint8_t sn, uint8_t v) { WIZCHIP_WRITE(Sn_ADDR(sn, Sn_IMR), v); }

/* ソケット MR/CR/SR */
uint8_t getSn_SR(uint8_t sn)  { return WIZCHIP_READ(Sn_ADDR(sn, Sn_SR)); }
uint8_t getSn_CR(uint8_t sn)  { return WIZCHIP_READ(Sn_ADDR(sn, Sn_CR)); }
void    setSn_CR(uint8_t sn, uint8_t v) { WIZCHIP_WRITE(Sn_ADDR(sn, Sn_CR), v); }
void    setSn_MR(uint8_t sn, uint8_t v) { WIZCHIP_WRITE(Sn_ADDR(sn, Sn_MR), v); }

/* ソケットポート */
void setSn_PORTR(uint8_t sn, uint16_t port)
{
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_PORT), (uint8_t)(port >> 8));
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_PORT + 1), (uint8_t)(port));
}

/* 宛先 IP/ポート */
void setSn_DIPR(uint8_t sn, const uint8_t *ip)
{
    WIZCHIP_WRITE_BUF(Sn_ADDR(sn, Sn_DIPR), (uint8_t *)ip, 4);
}

void setSn_DPORTR(uint8_t sn, uint16_t port)
{
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_DPORT), (uint8_t)(port >> 8));
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_DPORT + 1), (uint8_t)(port));
}

/* グローバル割り込み */
uint8_t getIR(void)  { return WIZCHIP_READ(CR_ADDR(SIR)); }
void    setIR(uint8_t v) { WIZCHIP_WRITE(CR_ADDR(SIR), v); }

/* バージョン読み取り */
uint8_t getVERSIONR(void) { return WIZCHIP_READ(CR_ADDR(VERSIONR)); }

/* バッファサイズ取得 */
uint16_t getSn_TxMAX(uint8_t sn)
{
    return (uint16_t)WIZCHIP_READ(Sn_ADDR(sn, Sn_TXBUF_SIZE)) * 1024;
}

uint16_t getSn_RxMAX(uint8_t sn)
{
    return (uint16_t)WIZCHIP_READ(Sn_ADDR(sn, Sn_RXBUF_SIZE)) * 1024;
}

/*----------------------------------------------------------------------
 * 追加ソケットレジスタアクセス (tk_socket.c が使用)
 *
 * W5500 はバッファのラップ処理を HW が自動で行うため、
 * getSn_RxBASE / getSn_RxMASK は tk_socket.c の W5100S 互換コードパスで
 * 使われるが、W5500 では wiz_recv_data() 内で不要 (HW 自動ラップ)。
 * ただし tk_socket.c が参照するため、互換値を返す。
 */

/* ソケット割り込みマスク */
void setSn_IMR(uint8_t sn, uint8_t v) { WIZCHIP_WRITE(Sn_ADDR(sn, Sn_IMR), v); }

/* RX 読みポインタ */
uint16_t getSn_RX_RD(uint8_t sn)
{
    return ((uint16_t)WIZCHIP_READ(Sn_ADDR(sn, Sn_RX_RD)) << 8) |
           WIZCHIP_READ(Sn_ADDR(sn, Sn_RX_RD + 1));
}

void setSn_RX_RD(uint8_t sn, uint16_t val)
{
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_RX_RD), (uint8_t)(val >> 8));
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_RX_RD + 1), (uint8_t)(val));
}

/* バッファマスク・ベース (W5500 は HW 自動ラップだが互換性のため提供) */
uint16_t getSn_RxMASK(uint8_t sn) { return getSn_RxMAX(sn) - 1; }
uint32_t getSn_RxBASE(uint8_t sn) { (void)sn; return 0; }  /* W5500 では未使用 */

/* ソースポート取得 */
uint16_t getSn_PORTR(uint8_t sn)
{
    return ((uint16_t)WIZCHIP_READ(Sn_ADDR(sn, Sn_PORT)) << 8) |
           WIZCHIP_READ(Sn_ADDR(sn, Sn_PORT + 1));
}

/* 宛先 IP/ポート取得 */
void getSn_DIPR(uint8_t sn, uint8_t *ip)
{
    WIZCHIP_READ_BUF(Sn_ADDR(sn, Sn_DIPR), ip, 4);
}

uint16_t getSn_DPORTR(uint8_t sn)
{
    return ((uint16_t)WIZCHIP_READ(Sn_ADDR(sn, Sn_DPORT)) << 8) |
           WIZCHIP_READ(Sn_ADDR(sn, Sn_DPORT + 1));
}

/* TCP キープアライブ */
void setSn_KPALVTR(uint8_t sn, uint8_t val) { WIZCHIP_WRITE(Sn_ADDR(sn, Sn_KPALVTR), val); }
uint8_t getSn_KPALVTR(uint8_t sn) { return WIZCHIP_READ(Sn_ADDR(sn, Sn_KPALVTR)); }
