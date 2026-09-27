/*
 *----------------------------------------------------------------------
 *    W5100S SPI I/O + バッファ管理 — w5100s.c の完全置き換え
 *
 *    ioLibrary w5100s.c (500行, 全チップ条件分岐) を
 *    W5100S SPI モード専用に縮小 (~130行)。
 *
 *    提供するもの:
 *    - WIZCHIP_READ / WIZCHIP_WRITE — 1byte レジスタ Read/Write
 *    - WIZCHIP_READ_BUF / WIZCHIP_WRITE_BUF — バースト Read/Write
 *    - wiz_send_data / wiz_recv_data — TX/RX 循環バッファ管理
 *    - wiz_recv_ignore — RX データ読み捨て
 *
 *    W5100S SPI プロトコル:
 *      Read:  CS=L → [0x0F][ADDR_H][ADDR_L] → [DATA] → CS=H
 *      Write: CS=L → [0xF0][ADDR_H][ADDR_L][DATA] → CS=H
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>

#include "w5100s_reg.h"

/* W5100S SPI コマンドバイト */
#define W5100S_SPI_READ_CMD   0x0F
#define W5100S_SPI_WRITE_CMD  0xF0

/*----------------------------------------------------------------------
 * 1byte レジスタ Read
 */
uint8_t WIZCHIP_READ(uint32_t AddrSel)
{
    uint8_t ret;
    uint8_t spi_data[3];

    WIZCHIP_CRITICAL_ENTER();
    WIZCHIP.CS._select();

    spi_data[0] = W5100S_SPI_READ_CMD;
    spi_data[1] = (uint8_t)((AddrSel >> 8) & 0xFF);
    spi_data[2] = (uint8_t)(AddrSel & 0xFF);
    WIZCHIP.IF.SPI._write_burst(spi_data, 3);
    ret = WIZCHIP.IF.SPI._read_byte();

    WIZCHIP.CS._deselect();
    WIZCHIP_CRITICAL_EXIT();
    return ret;
}

/*----------------------------------------------------------------------
 * 1byte レジスタ Write
 */
void WIZCHIP_WRITE(uint32_t AddrSel, uint8_t wb)
{
    uint8_t spi_data[4];

    WIZCHIP_CRITICAL_ENTER();
    WIZCHIP.CS._select();

    spi_data[0] = W5100S_SPI_WRITE_CMD;
    spi_data[1] = (uint8_t)((AddrSel >> 8) & 0xFF);
    spi_data[2] = (uint8_t)(AddrSel & 0xFF);
    spi_data[3] = wb;
    WIZCHIP.IF.SPI._write_burst(spi_data, 4);

    WIZCHIP.CS._deselect();
    WIZCHIP_CRITICAL_EXIT();
}

/*----------------------------------------------------------------------
 * バースト Read (連続アドレス)
 */
void WIZCHIP_READ_BUF(uint32_t AddrSel, uint8_t *pBuf, uint16_t len)
{
    uint8_t spi_data[3];

    WIZCHIP_CRITICAL_ENTER();
    WIZCHIP.CS._select();

    spi_data[0] = W5100S_SPI_READ_CMD;
    spi_data[1] = (uint8_t)((AddrSel >> 8) & 0xFF);
    spi_data[2] = (uint8_t)(AddrSel & 0xFF);
    WIZCHIP.IF.SPI._write_burst(spi_data, 3);
    WIZCHIP.IF.SPI._read_burst(pBuf, len);

    WIZCHIP.CS._deselect();
    WIZCHIP_CRITICAL_EXIT();
}

/*----------------------------------------------------------------------
 * バースト Write (連続アドレス)
 */
void WIZCHIP_WRITE_BUF(uint32_t AddrSel, uint8_t *pBuf, uint16_t len)
{
    uint8_t spi_data[3];

    WIZCHIP_CRITICAL_ENTER();
    WIZCHIP.CS._select();

    spi_data[0] = W5100S_SPI_WRITE_CMD;
    spi_data[1] = (uint8_t)((AddrSel >> 8) & 0xFF);
    spi_data[2] = (uint8_t)(AddrSel & 0xFF);
    WIZCHIP.IF.SPI._write_burst(spi_data, 3);
    WIZCHIP.IF.SPI._write_burst(pBuf, len);

    WIZCHIP.CS._deselect();
    WIZCHIP_CRITICAL_EXIT();
}

/*----------------------------------------------------------------------
 * 16bit レジスタ安全読み取り
 *
 * W5100S の TX_FSR / RX_RSR は 16bit だが 8bit ずつ読むため、
 * 読み取り中にカウンタが変化する可能性がある。
 * 2 回連続で同じ値になるまでリトライする。
 */
uint16_t getSn_TX_FSR(uint8_t sn)
{
    uint16_t val, val1;
    do {
        val1 = ((uint16_t)WIZCHIP_READ(Sn_TX_FSR(sn)) << 8) |
               WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_TX_FSR(sn), 1));
        if (val1 == 0) return 0;
        val = ((uint16_t)WIZCHIP_READ(Sn_TX_FSR(sn)) << 8) |
              WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_TX_FSR(sn), 1));
    } while (val != val1);
    return val;
}

uint16_t getSn_RX_RSR(uint8_t sn)
{
    uint16_t val, val1;
    do {
        val1 = ((uint16_t)WIZCHIP_READ(Sn_RX_RSR(sn)) << 8) |
               WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_RX_RSR(sn), 1));
        if (val1 == 0) return 0;
        val = ((uint16_t)WIZCHIP_READ(Sn_RX_RSR(sn)) << 8) |
              WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_RX_RSR(sn), 1));
    } while (val != val1);
    return val;
}

/*----------------------------------------------------------------------
 * TX/RX バッファベースアドレス計算
 *
 * W5100S のバッファは各ソケットに可変サイズで割り当てられる。
 * ソケット N のベースアドレスは、ソケット 0〜(N-1) の
 * バッファサイズの合計をベースに加算して求める。
 */
uint32_t getSn_TxBASE(uint8_t sn)
{
    uint32_t base = _WIZCHIP_IO_TXBUF_;
    int8_t i;
    for (i = 0; i < sn; i++) {
        base += getSn_TxMAX(i);
    }
    return base;
}

uint32_t getSn_RxBASE(uint8_t sn)
{
    uint32_t base = _WIZCHIP_IO_RXBUF_;
    int8_t i;
    for (i = 0; i < sn; i++) {
        base += getSn_RxMAX(i);
    }
    return base;
}

/*----------------------------------------------------------------------
 * TX バッファへのデータコピー (循環バッファ対応)
 *
 * W5100S の TX バッファは固定サイズの循環バッファ。
 * 書き込みポインタ (Sn_TX_WR) がバッファ末尾に達したら先頭に戻る。
 */
void wiz_send_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
    uint16_t ptr = getSn_TX_WR(sn);
    uint16_t dst_mask = ptr & getSn_TxMASK(sn);
    uint16_t dst_ptr = getSn_TxBASE(sn) + dst_mask;

    if (dst_mask + len > getSn_TxMAX(sn)) {
        /* ラップアラウンド: 末尾まで書いて残りを先頭から */
        uint16_t size = getSn_TxMAX(sn) - dst_mask;
        WIZCHIP_WRITE_BUF(dst_ptr, wizdata, size);
        wizdata += size;
        WIZCHIP_WRITE_BUF(getSn_TxBASE(sn), wizdata, len - size);
    } else {
        WIZCHIP_WRITE_BUF(dst_ptr, wizdata, len);
    }

    setSn_TX_WR(sn, ptr + len);
}

/*----------------------------------------------------------------------
 * RX バッファからのデータコピー (循環バッファ対応)
 */
void wiz_recv_data(uint8_t sn, uint8_t *wizdata, uint16_t len)
{
    uint16_t ptr = getSn_RX_RD(sn);
    uint16_t src_mask = ptr & getSn_RxMASK(sn);
    uint16_t src_ptr = getSn_RxBASE(sn) + src_mask;

    if (src_mask + len > getSn_RxMAX(sn)) {
        /* ラップアラウンド: 末尾まで読んで残りを先頭から */
        uint16_t size = getSn_RxMAX(sn) - src_mask;
        WIZCHIP_READ_BUF(src_ptr, wizdata, size);
        wizdata += size;
        WIZCHIP_READ_BUF(getSn_RxBASE(sn), wizdata, len - size);
    } else {
        WIZCHIP_READ_BUF(src_ptr, wizdata, len);
    }

    setSn_RX_RD(sn, ptr + len);
}

/*----------------------------------------------------------------------
 * RX データ読み捨て (ポインタだけ進める)
 */
void wiz_recv_ignore(uint8_t sn, uint16_t len)
{
    uint16_t ptr = getSn_RX_RD(sn);
    setSn_RX_RD(sn, ptr + len);
}

#endif /* CPU_RP2040 */
