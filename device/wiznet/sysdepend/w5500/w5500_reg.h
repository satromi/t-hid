/*
 *----------------------------------------------------------------------
 *    W5500 レジスタ定義 — WIZnet W5500 SPI モード専用
 *
 *    W5500 SPI プロトコル (BSB: Block Select Bits):
 *      [addr_hi][addr_lo][control][data...]
 *      control = [BSB(5bit)][R/W(1bit)][OM(2bit)]
 *      BSB: 00000=共通, 00001=Sn0_REG, 00010=Sn0_TX, 00011=Sn0_RX, ...
 *      R/W: 0=Read, 1=Write
 *      OM:  00=VDM (可変長), 01=FDM1, 10=FDM2, 11=FDM4
 *----------------------------------------------------------------------
 */

#ifndef __W5500_REG_H__
#define __W5500_REG_H__

#include <stdint.h>
#include <tm/tmonitor.h>

/*======================================================================
 * デバッグ出力
 *====================================================================*/
#define W5DBG(fmt, ...)		tm_printf((UB *)(fmt), ##__VA_ARGS__)

/*======================================================================
 * W5500 チップ構成
 *====================================================================*/

#define _WIZCHIP_SOCK_NUM_	8
/* WIZNET_SOCK_NUM は dev_wiznet.h で WIZCHIP_W5500 に応じて定義 */
#ifndef WIZNET_SOCK_NUM
#define WIZNET_SOCK_NUM		_WIZCHIP_SOCK_NUM_
#endif

/*======================================================================
 * W5500 SPI BSB (Block Select Bits)
 *====================================================================*/

#define W5500_BSB_COMMON	(0x00 << 3)	/* 共通レジスタブロック */
#define W5500_BSB_Sn_REG(sn)	(((sn)*4 + 1) << 3)	/* ソケット N レジスタ */
#define W5500_BSB_Sn_TX(sn)	(((sn)*4 + 2) << 3)	/* ソケット N TX バッファ */
#define W5500_BSB_Sn_RX(sn)	(((sn)*4 + 3) << 3)	/* ソケット N RX バッファ */

#define W5500_CTRL_READ		0x00
#define W5500_CTRL_WRITE	0x04
#define W5500_CTRL_VDM		0x00	/* 可変長モード */
#define W5500_CTRL_FDM1		0x01	/* 固定 1byte */
#define W5500_CTRL_FDM2		0x02	/* 固定 2byte */
#define W5500_CTRL_FDM4		0x03	/* 固定 4byte */

/*======================================================================
 * 共通レジスタ (BSB=00000)
 *====================================================================*/

#define MR		0x0000	/* モードレジスタ */
#define GAR		0x0001	/* ゲートウェイ IP (4 bytes) */
#define SUBR		0x0005	/* サブネットマスク (4 bytes) */
#define SHAR		0x0009	/* ソースハードウェアアドレス (6 bytes) */
#define SIPR		0x000F	/* ソース IP (4 bytes) */
#define IR		0x0015	/* 割り込みレジスタ */
#define _IMR_		0x0016	/* 割り込みマスクレジスタ */
#define SIR		0x0017	/* ソケット割り込みレジスタ */
#define SIMR		0x0018	/* ソケット割り込みマスクレジスタ */
#define INTLEVEL	0x0013	/* 割り込みレベルタイマー (2 bytes) */
#define PHYCFGR		0x002E	/* PHY 設定レジスタ */
#define VERSIONR	0x0039	/* チップバージョン (0x04 = W5500) */

/* PHYCFGR ビット */
#define PHYCFGR_LNK	(1 << 0)	/* リンク状態 */
#define PHYCFGR_SPD	(1 << 1)	/* 速度 (1=100Mbps) */
#define PHYCFGR_DPX	(1 << 2)	/* 全二重 */
#define PHYCFGR_RST	(1 << 7)	/* PHY リセット */

/* MR ビット */
#define MR_RST		0x80

/*======================================================================
 * ソケットレジスタ (BSB=Sn_REG, オフセット 0x0000-)
 *====================================================================*/

#define Sn_MR		0x0000	/* ソケットモード */
#define Sn_CR		0x0001	/* ソケットコマンド */
#define Sn_IR		0x0002	/* ソケット割り込み */
#define Sn_SR		0x0003	/* ソケットステータス */
#define Sn_PORT		0x0004	/* ソースポート (2 bytes) */
#define Sn_DIPR		0x000C	/* 宛先 IP (4 bytes) */
#define Sn_DPORT	0x0010	/* 宛先ポート (2 bytes) */
#define Sn_RXBUF_SIZE	0x001E	/* RX バッファサイズ (KB) */
#define Sn_TXBUF_SIZE	0x001F	/* TX バッファサイズ (KB) */
#define Sn_TX_FSR	0x0020	/* TX フリーサイズ (2 bytes) */
#define Sn_TX_RD	0x0022	/* TX 読みポインタ (2 bytes) */
#define Sn_TX_WR	0x0024	/* TX 書きポインタ (2 bytes) */
#define Sn_RX_RSR	0x0026	/* RX 受信サイズ (2 bytes) */
#define Sn_RX_RD	0x0028	/* RX 読みポインタ (2 bytes) */
#define Sn_RX_WR	0x002A	/* RX 書きポインタ (2 bytes) */
#define Sn_IMR		0x002C	/* ソケット割り込みマスク */
#define Sn_KPALVTR	0x002F	/* キープアライブタイマー */

/* ソケットコマンド */
#define Sn_CR_OPEN	0x01
#define Sn_CR_LISTEN	0x02
#define Sn_CR_CONNECT	0x04
#define Sn_CR_DISCON	0x08
#define Sn_CR_CLOSE	0x10
#define Sn_CR_SEND	0x20
#define Sn_CR_RECV	0x40

/* ソケット割り込みビット */
#define Sn_IR_CON	0x01
#define Sn_IR_DISCON	0x02
#define Sn_IR_RECV	0x04
#define Sn_IR_TIMEOUT	0x08
#define Sn_IR_SENDOK	0x10

/* ソケットモード */
#define Sn_MR_TCP	0x01
#define Sn_MR_UDP	0x02
#define Sn_MR_MACRAW	0x04

/* ソケットステータス */
#define SOCK_CLOSED		0x00
#define SOCK_INIT		0x13
#define SOCK_LISTEN		0x14
#define SOCK_ESTABLISHED	0x17
#define SOCK_CLOSE_WAIT		0x1C
#define SOCK_UDP		0x22
#define SOCK_MACRAW		0x42

/* PHY */
#define PHY_LINK_ON	1
#define PHY_LINK_OFF	0

#endif /* __W5500_REG_H__ */
