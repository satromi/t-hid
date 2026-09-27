/*
 *----------------------------------------------------------------------
 *    W5500 SPI I/O — BSB (Block Select Bits) プロトコル実装
 *
 *    W5500 SPI フレーム:
 *      [ADDR_H][ADDR_L][CONTROL][DATA...]
 *      CONTROL = [BSB(5bit)][R/W(1bit)][OM(2bit)]
 *
 *    AddrSel エンコーディング (32bit):
 *      [23:16] = オフセット上位バイト
 *      [15:8]  = オフセット下位バイト
 *      [7:3]   = BSB (ブロック選択)
 *      [2]     = R/W (0=Read, 1=Write)
 *      [1:0]   = OM (00=VDM 可変長)
 *
 *    BSB ブロック:
 *      0       = 共通レジスタ
 *      1+4*N   = ソケット N レジスタ
 *      2+4*N   = ソケット N TX バッファ
 *      3+4*N   = ソケット N RX バッファ
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <string.h>

#include "w5500_reg.h"

/* 参照: wiznet_drv.h の _WIZCHIP 構造体 */
#include "../../wiznet_drv.h"

/* W5500 SPI 制御ビット */
#define W5500_SPI_READ   0x00
#define W5500_SPI_WRITE  0x04
#define W5500_SPI_VDM    0x00

/*----------------------------------------------------------------------
 * 1byte Read
 */
uint8_t WIZCHIP_READ(uint32_t AddrSel)
{
    uint8_t ret;
    uint8_t spi_data[3];

    AddrSel |= (W5500_SPI_READ | W5500_SPI_VDM);

    WIZCHIP_CRITICAL_ENTER();
    WIZCHIP.CS._select();

    spi_data[0] = (uint8_t)((AddrSel >> 16) & 0xFF);  /* offset high */
    spi_data[1] = (uint8_t)((AddrSel >> 8)  & 0xFF);  /* offset low */
    spi_data[2] = (uint8_t)((AddrSel)       & 0xFF);  /* control (BSB|R/W|OM) */
    WIZCHIP.IF.SPI._write_burst(spi_data, 3);
    ret = WIZCHIP.IF.SPI._read_byte();

    WIZCHIP.CS._deselect();
    WIZCHIP_CRITICAL_EXIT();
    return ret;
}

/*----------------------------------------------------------------------
 * 1byte Write
 */
void WIZCHIP_WRITE(uint32_t AddrSel, uint8_t wb)
{
    uint8_t spi_data[4];

    AddrSel |= (W5500_SPI_WRITE | W5500_SPI_VDM);

    WIZCHIP_CRITICAL_ENTER();
    WIZCHIP.CS._select();

    spi_data[0] = (uint8_t)((AddrSel >> 16) & 0xFF);
    spi_data[1] = (uint8_t)((AddrSel >> 8)  & 0xFF);
    spi_data[2] = (uint8_t)((AddrSel)       & 0xFF);
    spi_data[3] = wb;
    WIZCHIP.IF.SPI._write_burst(spi_data, 4);

    WIZCHIP.CS._deselect();
    WIZCHIP_CRITICAL_EXIT();
}

/*----------------------------------------------------------------------
 * バースト Read
 */
void WIZCHIP_READ_BUF(uint32_t AddrSel, uint8_t *pBuf, uint16_t len)
{
    uint8_t spi_data[3];

    AddrSel |= (W5500_SPI_READ | W5500_SPI_VDM);

    WIZCHIP_CRITICAL_ENTER();
    WIZCHIP.CS._select();

    spi_data[0] = (uint8_t)((AddrSel >> 16) & 0xFF);
    spi_data[1] = (uint8_t)((AddrSel >> 8)  & 0xFF);
    spi_data[2] = (uint8_t)((AddrSel)       & 0xFF);
    WIZCHIP.IF.SPI._write_burst(spi_data, 3);
    WIZCHIP.IF.SPI._read_burst(pBuf, len);

    WIZCHIP.CS._deselect();
    WIZCHIP_CRITICAL_EXIT();
}

/*----------------------------------------------------------------------
 * バースト Write
 */
void WIZCHIP_WRITE_BUF(uint32_t AddrSel, uint8_t *pBuf, uint16_t len)
{
    uint8_t spi_data[3];

    AddrSel |= (W5500_SPI_WRITE | W5500_SPI_VDM);

    WIZCHIP_CRITICAL_ENTER();
    WIZCHIP.CS._select();

    spi_data[0] = (uint8_t)((AddrSel >> 16) & 0xFF);
    spi_data[1] = (uint8_t)((AddrSel >> 8)  & 0xFF);
    spi_data[2] = (uint8_t)((AddrSel)       & 0xFF);
    WIZCHIP.IF.SPI._write_burst(spi_data, 3);
    WIZCHIP.IF.SPI._write_burst(pBuf, len);

    WIZCHIP.CS._deselect();
    WIZCHIP_CRITICAL_EXIT();
}

/*----------------------------------------------------------------------
 * 16bit レジスタ安全読み取り (連続 2 回一致するまでリトライ)
 */
LOCAL uint16_t read_reg16_safe(uint32_t addr)
{
    uint16_t val, prev;
    do {
        prev = ((uint16_t)WIZCHIP_READ(addr) << 8) |
               WIZCHIP_READ(addr + (1 << 8));  /* offset+1 */
        val  = ((uint16_t)WIZCHIP_READ(addr) << 8) |
               WIZCHIP_READ(addr + (1 << 8));
    } while (val != prev);
    return val;
}

/*----------------------------------------------------------------------
 * ソケットレジスタヘルパー — BSB アドレス生成
 *
 * W5500 のソケットレジスタアドレス:
 *   AddrSel = (offset << 8) | (BSB << 3)
 *   BSB = WIZCHIP_SREG_BLOCK(sn) = 1 + 4*sn
 */
#define Sn_ADDR(sn, off)  (((uint32_t)(off) << 8) | (((1 + 4*(sn)) & 0x1F) << 3))
#define TX_ADDR(sn, off)  (((uint32_t)(off) << 8) | (((2 + 4*(sn)) & 0x1F) << 3))
#define RX_ADDR(sn, off)  (((uint32_t)(off) << 8) | (((3 + 4*(sn)) & 0x1F) << 3))

/* 共通レジスタアドレス */
#define CR_ADDR(off)  ((uint32_t)(off) << 8)

/*----------------------------------------------------------------------
 * TX フリーサイズ (16bit 安全読み取り)
 */
uint16_t getSn_TX_FSR(uint8_t sn)
{
    return read_reg16_safe(Sn_ADDR(sn, Sn_TX_FSR));
}

/*----------------------------------------------------------------------
 * RX 受信サイズ (16bit 安全読み取り)
 */
uint16_t getSn_RX_RSR(uint8_t sn)
{
    return read_reg16_safe(Sn_ADDR(sn, Sn_RX_RSR));
}

/*----------------------------------------------------------------------
 * TX バッファへのデータコピー
 *
 * W5500 はハードウェアが循環バッファのラップを自動処理するため、
 * W5100S のような手動ラップ処理は不要。
 */
void wiz_send_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
    uint16_t ptr;
    if (len == 0) return;

    ptr = ((uint16_t)WIZCHIP_READ(Sn_ADDR(sn, Sn_TX_WR)) << 8) |
          WIZCHIP_READ(Sn_ADDR(sn, Sn_TX_WR + 1));

    WIZCHIP_WRITE_BUF(TX_ADDR(sn, ptr), wizdata, len);

    ptr += len;
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_TX_WR), (uint8_t)(ptr >> 8));
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_TX_WR + 1), (uint8_t)(ptr));
}

/*----------------------------------------------------------------------
 * RX バッファからのデータコピー
 */
void wiz_recv_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
    uint16_t ptr;
    if (len == 0) return;

    ptr = ((uint16_t)WIZCHIP_READ(Sn_ADDR(sn, Sn_RX_RD)) << 8) |
          WIZCHIP_READ(Sn_ADDR(sn, Sn_RX_RD + 1));

    WIZCHIP_READ_BUF(RX_ADDR(sn, ptr), wizdata, len);

    ptr += len;
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_RX_RD), (uint8_t)(ptr >> 8));
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_RX_RD + 1), (uint8_t)(ptr));
}

/*----------------------------------------------------------------------
 * RX データ読み捨て
 */
void wiz_recv_ignore(uint8_t sn, uint16_t len)
{
    uint16_t ptr;
    ptr = ((uint16_t)WIZCHIP_READ(Sn_ADDR(sn, Sn_RX_RD)) << 8) |
          WIZCHIP_READ(Sn_ADDR(sn, Sn_RX_RD + 1));
    ptr += len;
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_RX_RD), (uint8_t)(ptr >> 8));
    WIZCHIP_WRITE(Sn_ADDR(sn, Sn_RX_RD + 1), (uint8_t)(ptr));
}
