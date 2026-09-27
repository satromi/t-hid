/*
 *----------------------------------------------------------------------
 *    W5100S レジスタ定義 — ioLibrary w5100s.h + wizchip_conf.h の完全置き換え
 *
 *    ioLibrary w5100s.h (3500行, 全チップ条件分岐) と
 *    wizchip_conf.h (1000行, 全チップ対応) から、
 *    W5100S SPI モードで使用する定義のみを抽出 (~300行)。
 *
 *    これにより ioLibrary ヘッダへの依存が完全になくなる。
 *----------------------------------------------------------------------
 */

#ifndef __W5100S_REG_H__
#define __W5100S_REG_H__

#include <stdint.h>
#include <tm/tmonitor.h>

/*======================================================================
 * デバッグ出力
 *   W5100S_DEBUG: 通常デバッグ (DHCP フロー、初期化結果)
 *   W5100S_DEBUG_SPI: SPI 診断 (ビットバング、ループバック等)
 *====================================================================*/
#define W5100S_DEBUG		1
#define W5100S_DEBUG_SPI	1

#if W5100S_DEBUG
#define W5DBG(fmt, ...)		tm_printf((UB *)(fmt), ##__VA_ARGS__)
#else
#define W5DBG(fmt, ...)		((void)0)
#endif

#if W5100S_DEBUG_SPI
#define W5DBG_SPI(fmt, ...)	tm_printf((UB *)(fmt), ##__VA_ARGS__)
#else
#define W5DBG_SPI(fmt, ...)	((void)0)
#endif

/*======================================================================
 * チップ構成
 *====================================================================*/

#define W5100S			W5100S
#define _WIZCHIP_		W5100S
#define _WIZCHIP_IO_MODE_SPI_	0x0200
#define _WIZCHIP_IO_MODE_	_WIZCHIP_IO_MODE_SPI_
#define _WIZCHIP_ID_		"W5100S\0"
#define _WIZCHIP_SOCK_NUM_	4
#define _W5100S_IO_BASE_	0x0000

/*======================================================================
 * WIZCHIP 構造体 — SPI コールバック管理
 *====================================================================*/

typedef uint8_t iodata_t;

typedef struct __WIZCHIP {
    uint16_t if_mode;
    uint8_t  id[8];
    struct { void (*_enter)(void); void (*_exit)(void); } CRIS;
    struct { void (*_select)(void); void (*_deselect)(void); } CS;
    union {
        struct {
            iodata_t (*_read_data)(uint32_t);
            void     (*_write_data)(uint32_t, iodata_t);
            void     (*_read_data_buf)(uint32_t, iodata_t*, int16_t, uint8_t);
            void     (*_write_data_buf)(uint32_t, iodata_t*, int16_t, uint8_t);
        } BUS;
        struct {
            uint8_t (*_read_byte)(void);
            void    (*_write_byte)(uint8_t);
            void    (*_read_burst)(uint8_t*, uint16_t);
            void    (*_write_burst)(uint8_t*, uint16_t);
        } SPI;
    } IF;
} _WIZCHIP;

extern _WIZCHIP WIZCHIP;

#define WIZCHIP_CRITICAL_ENTER()  WIZCHIP.CRIS._enter()
#define WIZCHIP_CRITICAL_EXIT()   WIZCHIP.CRIS._exit()

/*======================================================================
 * ネットワーク情報
 *====================================================================*/

typedef enum { NETINFO_STATIC = 1, NETINFO_DHCP } dhcp_mode;

typedef struct {
    uint8_t   mac[6];
    uint8_t   ip[4];
    uint8_t   sn[4];
    uint8_t   gw[4];
    uint8_t   dns[4];
    dhcp_mode dhcp;
} wiz_NetInfo;

#define PHY_LINK_OFF	0
#define PHY_LINK_ON	1

/*======================================================================
 * レジスタ I/O 関数宣言
 *====================================================================*/

uint8_t WIZCHIP_READ(uint32_t AddrSel);
void    WIZCHIP_WRITE(uint32_t AddrSel, uint8_t wb);
void    WIZCHIP_READ_BUF(uint32_t AddrSel, uint8_t *pBuf, uint16_t len);
void    WIZCHIP_WRITE_BUF(uint32_t AddrSel, uint8_t *pBuf, uint16_t len);

/*======================================================================
 * レジスタアドレス — 共通レジスタ
 *====================================================================*/

#define MR		(_W5100S_IO_BASE_ + 0x0000)
#define GAR		(_W5100S_IO_BASE_ + 0x0001)
#define SUBR		(_W5100S_IO_BASE_ + 0x0005)
#define SHAR		(_W5100S_IO_BASE_ + 0x0009)
#define SIPR		(_W5100S_IO_BASE_ + 0x000F)
#define IR		(_W5100S_IO_BASE_ + 0x0015)
#define _IMR_		(_W5100S_IO_BASE_ + 0x0016)
#define RMSR		(_W5100S_IO_BASE_ + 0x001A)
#define TMSR		(_W5100S_IO_BASE_ + 0x001B)
#define PHYSR		(_W5100S_IO_BASE_ + 0x003C)
/*
 * PHY 管理レジスタ (W5100S データシート Table 2.2 参照)
 *   PHYRAR = 0x003F (PHY Register Address)
 *   PHYDIR = 0x0040 (PHY Data Input, 16bit: 0x0040-0x0041)
 *   PHYACR = 0x0044 (PHY Action Control)
 */
#define PHYRAR		(_W5100S_IO_BASE_ + 0x003F)
#define PHYDIR		(_W5100S_IO_BASE_ + 0x0040)
#define PHYACR		(_W5100S_IO_BASE_ + 0x0044)

/* モードレジスタビット */
#define MR_RST		0x80
#define MR_AI		0x02

/* PHY ステータス */
#define PHYSR_LNK	(1 << 0)

/* PHY アクション */
#define PHYACR_WRITE	0x01

/*======================================================================
 * レジスタアドレス — ソケットレジスタ
 *====================================================================*/

#define _WIZCHIP_SN_BASE_	0x0400
#define _WIZCHIP_SN_SIZE_	0x0100
#define _WIZCHIP_IO_TXBUF_	0x4000
#define _WIZCHIP_IO_RXBUF_	0x6000

#define WIZCHIP_SREG_BLOCK(sn)	(_WIZCHIP_SN_BASE_ + _WIZCHIP_SN_SIZE_ * (sn))
#define WIZCHIP_OFFSET_INC(addr, n) ((addr) + (n))

#define Sn_MR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0000)
#define Sn_CR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0001)
#define Sn_IR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0002)
#define Sn_SR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0003)
#define Sn_PORT(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0004)
#define Sn_DIPR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x000C)
#define Sn_DPORT(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0010)
#define Sn_TX_FSR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0020)
#define Sn_TX_RD(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0022)
#define Sn_TX_WR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0024)
#define Sn_RX_RSR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0026)
#define Sn_RX_RD(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0028)
#define Sn_IMR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x002C)
#define Sn_KPALVTR(sn)	(_W5100S_IO_BASE_ + WIZCHIP_SREG_BLOCK(sn) + 0x0030)

/*======================================================================
 * ソケットモード (Sn_MR)
 *====================================================================*/

#define Sn_MR_TCP	0x01
#define Sn_MR_UDP	0x02
#define Sn_MR_MACRAW	0x04

/*======================================================================
 * ソケットコマンド (Sn_CR)
 *====================================================================*/

#define Sn_CR_OPEN	0x01
#define Sn_CR_LISTEN	0x02
#define Sn_CR_CONNECT	0x04
#define Sn_CR_DISCON	0x08
#define Sn_CR_CLOSE	0x10
#define Sn_CR_SEND	0x20
#define Sn_CR_RECV	0x40

/*======================================================================
 * ソケット割り込み (Sn_IR)
 *====================================================================*/

#define Sn_IR_CON	0x01
#define Sn_IR_DISCON	0x02
#define Sn_IR_RECV	0x04
#define Sn_IR_TIMEOUT	0x08
#define Sn_IR_SENDOK	0x10

/*======================================================================
 * ソケット状態 (Sn_SR)
 *====================================================================*/

#define SOCK_CLOSED	0x00
#define SOCK_INIT	0x13
#define SOCK_LISTEN	0x14
#define SOCK_ESTABLISHED 0x17
#define SOCK_CLOSE_WAIT	0x1C
#define SOCK_UDP	0x22
#define SOCK_MACRAW	0x42

/*======================================================================
 * レジスタアクセスマクロ — 共通
 *====================================================================*/

#define setMR(mr)		WIZCHIP_WRITE(MR, (mr))
#define getMR()			WIZCHIP_READ(MR)
#define setGAR(gar)		WIZCHIP_WRITE_BUF(GAR, (gar), 4)
#define getGAR(gar)		WIZCHIP_READ_BUF(GAR, (gar), 4)
#define setSUBR(subr)		WIZCHIP_WRITE_BUF(SUBR, (subr), 4)
#define getSUBR(subr)		WIZCHIP_READ_BUF(SUBR, (subr), 4)
#define setSHAR(shar)		WIZCHIP_WRITE_BUF(SHAR, (shar), 6)
#define getSHAR(shar)		WIZCHIP_READ_BUF(SHAR, (shar), 6)
#define setSIPR(sipr)		WIZCHIP_WRITE_BUF(SIPR, (sipr), 4)
#define getSIPR(sipr)		WIZCHIP_READ_BUF(SIPR, (sipr), 4)
#define setIR(ir)		WIZCHIP_WRITE(IR, ((ir) & 0xE0))
#define getIR()			WIZCHIP_READ(IR)
#define setIMR(imr)		WIZCHIP_WRITE(_IMR_, (imr))
#define getIMR()		WIZCHIP_READ(_IMR_)
#define getPHYSR()		WIZCHIP_READ(PHYSR)

/*======================================================================
 * レジスタアクセスマクロ — ソケット
 *====================================================================*/

#define setSn_MR(sn, mr)	WIZCHIP_WRITE(Sn_MR(sn), (mr))
#define getSn_MR(sn)		WIZCHIP_READ(Sn_MR(sn))
#define setSn_CR(sn, cr)	WIZCHIP_WRITE(Sn_CR(sn), (cr))
#define getSn_CR(sn)		WIZCHIP_READ(Sn_CR(sn))
#define setSn_IR(sn, ir)	WIZCHIP_WRITE(Sn_IR(sn), (ir))
#define getSn_IR(sn)		WIZCHIP_READ(Sn_IR(sn))
#define getSn_SR(sn)		WIZCHIP_READ(Sn_SR(sn))
#define setSn_IMR(sn, imr)	WIZCHIP_WRITE(Sn_IMR(sn), (imr))
#define getSn_IMR(sn)		WIZCHIP_READ(Sn_IMR(sn))

/* TCP キープアライブ (Sn_KPALVTR: 単位 5秒, 0=無効) */
#define setSn_KPALVTR(sn, val)	WIZCHIP_WRITE(Sn_KPALVTR(sn), (val))
#define getSn_KPALVTR(sn)	WIZCHIP_READ(Sn_KPALVTR(sn))

/* ポート (16bit big-endian) */
#define setSn_PORTR(sn, port) \
    do { WIZCHIP_WRITE(Sn_PORT(sn), (uint8_t)((port) >> 8)); \
         WIZCHIP_WRITE(WIZCHIP_OFFSET_INC(Sn_PORT(sn),1), (uint8_t)(port)); } while(0)
#define getSn_PORTR(sn) \
    (((uint16_t)WIZCHIP_READ(Sn_PORT(sn)) << 8) | WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_PORT(sn),1)))

/* 宛先 IP / ポート */
#define setSn_DIPR(sn, dipr)	WIZCHIP_WRITE_BUF(Sn_DIPR(sn), (dipr), 4)
#define getSn_DIPR(sn, dipr)	WIZCHIP_READ_BUF(Sn_DIPR(sn), (dipr), 4)
#define setSn_DPORTR(sn, dport) \
    do { WIZCHIP_WRITE(Sn_DPORT(sn), (uint8_t)((dport) >> 8)); \
         WIZCHIP_WRITE(WIZCHIP_OFFSET_INC(Sn_DPORT(sn),1), (uint8_t)(dport)); } while(0)
#define getSn_DPORTR(sn) \
    (((uint16_t)WIZCHIP_READ(Sn_DPORT(sn)) << 8) | WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_DPORT(sn),1)))

/* TX/RX ポインタ (16bit) */
#define getSn_TX_WR(sn) \
    (((uint16_t)WIZCHIP_READ(Sn_TX_WR(sn)) << 8) | WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_TX_WR(sn),1)))
#define setSn_TX_WR(sn, txwr) \
    do { WIZCHIP_WRITE(Sn_TX_WR(sn), (uint8_t)((txwr) >> 8)); \
         WIZCHIP_WRITE(WIZCHIP_OFFSET_INC(Sn_TX_WR(sn),1), (uint8_t)(txwr)); } while(0)
#define getSn_RX_RD(sn) \
    (((uint16_t)WIZCHIP_READ(Sn_RX_RD(sn)) << 8) | WIZCHIP_READ(WIZCHIP_OFFSET_INC(Sn_RX_RD(sn),1)))
#define setSn_RX_RD(sn, rxrd) \
    do { WIZCHIP_WRITE(Sn_RX_RD(sn), (uint8_t)((rxrd) >> 8)); \
         WIZCHIP_WRITE(WIZCHIP_OFFSET_INC(Sn_RX_RD(sn),1), (uint8_t)(rxrd)); } while(0)

/*
 * TX/RX バッファサイズ (TMSR/RMSR, 2bit per socket, log2 KB)
 *
 * ★注意: setSn_TXBUF_SIZE / setSn_RXBUF_SIZE は read-modify-write のため
 * 複数タスクから同時に呼ぶと競合する。初期化時 (w5100s_chip_init) でのみ使用し、
 * ランタイムでは呼ばないこと。getSn_TxMAX/getSn_RxMAX は読み取り専用なので安全。
 */
#define setSn_TXBUF_SIZE(sn, sz) \
    WIZCHIP_WRITE(TMSR, (WIZCHIP_READ(TMSR) & ~(0x03 << (2*(sn)))) | ((sz) << (2*(sn))))
#define getSn_TXBUF_SIZE(sn) \
    ((WIZCHIP_READ(TMSR) >> (2*(sn))) & 0x03)
#define setSn_RXBUF_SIZE(sn, sz) \
    WIZCHIP_WRITE(RMSR, (WIZCHIP_READ(RMSR) & ~(0x03 << (2*(sn)))) | ((sz) << (2*(sn))))
#define getSn_RXBUF_SIZE(sn) \
    ((WIZCHIP_READ(RMSR) >> (2*(sn))) & 0x03)

/* TX/RX バッファ容量・マスク (バイト単位) */
#define getSn_TxMAX(sn)		((uint16_t)(1 << getSn_TXBUF_SIZE(sn)) << 10)
#define getSn_RxMAX(sn)		((uint16_t)(1 << getSn_RXBUF_SIZE(sn)) << 10)
#define getSn_TxMASK(sn)	(getSn_TxMAX(sn) - 1)
#define getSn_RxMASK(sn)	(getSn_RxMAX(sn) - 1)

/*======================================================================
 * TX/RX バッファ関数宣言
 * (getSn_TX_FSR / getSn_RX_RSR は 16bit 安全読み取りが必要なため関数)
 *====================================================================*/

uint16_t getSn_TX_FSR(uint8_t sn);
uint16_t getSn_RX_RSR(uint8_t sn);
uint32_t getSn_TxBASE(uint8_t sn);
uint32_t getSn_RxBASE(uint8_t sn);

void wiz_send_data(uint8_t sn, uint8_t *wizdata, uint16_t len);
void wiz_recv_data(uint8_t sn, uint8_t *wizdata, uint16_t len);
void wiz_recv_ignore(uint8_t sn, uint16_t len);

/*======================================================================
 * コールバック登録関数宣言
 *====================================================================*/

void reg_wizchip_cris_cbfunc(void(*)(void), void(*)(void));
void reg_wizchip_cs_cbfunc(void(*)(void), void(*)(void));
void reg_wizchip_spi_cbfunc(uint8_t(*)(void), void(*)(uint8_t));
void reg_wizchip_spiburst_cbfunc(void(*)(uint8_t*, uint16_t), void(*)(uint8_t*, uint16_t));

int8_t wizchip_init(uint8_t *txsize, uint8_t *rxsize);
void   wizchip_sw_reset(void);
void   wizchip_setnetinfo(wiz_NetInfo *pnetinfo);
void   wizchip_getnetinfo(wiz_NetInfo *pnetinfo);
int8_t wizphy_getphylink(void);

#endif /* __W5100S_REG_H__ */
