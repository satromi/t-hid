/*
 *----------------------------------------------------------------------
 *    WIZnet チップ共通ドライバ抽象化ヘッダ
 *
 *    W5100S / W5500 の差分を吸収し、上位層 (tk_socket, tk_dhcp 等) に
 *    チップ非依存の API を提供する。
 *
 *    ビルド時に WIZCHIP_W5100S or WIZCHIP_W5500 を定義して切替。
 *----------------------------------------------------------------------
 */

#ifndef __WIZNET_DRV_H__
#define __WIZNET_DRV_H__

#include <tk/tkernel.h>

/*======================================================================
 * チップ選択 (ビルド時マクロ)
 *====================================================================*/

#if defined(WIZCHIP_W5100S)
  #include "sysdepend/w5100s/w5100s_reg.h"
  #define WIZCHIP_NAME  "W5100S"
  #define WIZCHIP_VERR  0x51
  #define WIZCHIP_SOCK_NUM  4
#elif defined(WIZCHIP_W5500)
  #include "sysdepend/w5500/w5500_reg.h"
  #define WIZCHIP_NAME  "W5500"
  #define WIZCHIP_VERR  0x04
  #define WIZCHIP_SOCK_NUM  8
#else
  #error "Define WIZCHIP_W5100S or WIZCHIP_W5500"
#endif

/*======================================================================
 * チップ非依存レジスタ API
 *
 * 実装は各チップの SPI ドライバ (w5100s_spi.c / w5500_spi.c) が提供。
 *====================================================================*/

/* 1byte レジスタ Read/Write */
UB   WIZCHIP_READ(UW addr);
void WIZCHIP_WRITE(UW addr, UB val);

/* バースト Read/Write */
void WIZCHIP_READ_BUF(UW addr, UB *buf, UH len);
void WIZCHIP_WRITE_BUF(UW addr, UB *buf, UH len);

/*======================================================================
 * チップ非依存ネットワークレジスタマクロ
 *
 * W5100S と W5500 で同じレジスタ名・同じオフセットのものは
 * そのまま使用可能 (MR, GAR, SUBR, SHAR, SIPR, IR, IMR 等)。
 *
 * チップ依存のもの (PHY, バッファ管理) は関数で抽象化。
 *====================================================================*/

/* 共通レジスタ操作マクロ (両チップで同一) */
#define getMR()           WIZCHIP_READ(MR)
#define setMR(val)        WIZCHIP_WRITE(MR, val)

/* IP/MAC 操作 — 両チップで同じアドレス */
void getSIPR(UB *ip);
void setSIPR(const UB *ip);
void getSUBR(UB *mask);
void setSUBR(const UB *mask);
void getGAR(UB *gw);
void setGAR(const UB *gw);
void getSHAR(UB *mac);
void setSHAR(const UB *mac);

/* 割り込みマスク */
UB   getIMR(void);
void setIMR(UB mask);

/* PHY リンク状態 (チップ依存部分を関数化) */
int8_t wizphy_getphylink(void);

/* ソケットレジスタ — チップ依存のアドレス計算を関数化 */
#if defined(WIZCHIP_W5100S)
  /* W5100S: フラットアドレス */
  #define getSn_SR(sn)     WIZCHIP_READ(_WIZCHIP_SN_BASE_ + _WIZCHIP_SN_SIZE_ * (sn) + 0x03)
  #define getSn_CR(sn)     WIZCHIP_READ(_WIZCHIP_SN_BASE_ + _WIZCHIP_SN_SIZE_ * (sn) + 0x01)
  #define setSn_CR(sn,v)   WIZCHIP_WRITE(_WIZCHIP_SN_BASE_ + _WIZCHIP_SN_SIZE_ * (sn) + 0x01, v)
  /* ... 他のソケットレジスタも同様 */
#elif defined(WIZCHIP_W5500)
  /* W5500: ソケットレジスタアクセスは w5500_chip.c で関数として実装済み。
   * w5500_io.c 内の Sn_ADDR マクロが BSB アドレスを生成する。
   * ここではマクロ定義不要 (関数版が優先される)。 */
  /* ソケットレジスタアクセス関数 (w5500_chip.c 実装) */
  uint8_t getSn_SR(uint8_t sn);
  uint8_t getSn_CR(uint8_t sn);
  void    setSn_CR(uint8_t sn, uint8_t v);
  void    setSn_MR(uint8_t sn, uint8_t v);
  uint8_t getSn_IR(uint8_t sn);
  void    setSn_IR(uint8_t sn, uint8_t v);
  uint8_t getSn_IMR_reg(uint8_t sn);
  void    setSn_IMR_reg(uint8_t sn, uint8_t v);
  void    setSn_IMR(uint8_t sn, uint8_t v);
  void    setSn_PORTR(uint8_t sn, uint16_t port);
  uint16_t getSn_PORTR(uint8_t sn);
  void    setSn_DIPR(uint8_t sn, const uint8_t *ip);
  void    getSn_DIPR(uint8_t sn, uint8_t *ip);
  void    setSn_DPORTR(uint8_t sn, uint16_t port);
  uint16_t getSn_DPORTR(uint8_t sn);
  uint8_t getIR(void);
  void    setIR(uint8_t v);
  uint16_t getSn_TxMAX(uint8_t sn);
  uint16_t getSn_RxMAX(uint8_t sn);
  uint16_t getSn_RxMASK(uint8_t sn);
  uint32_t getSn_RxBASE(uint8_t sn);
  uint16_t getSn_RX_RD(uint8_t sn);
  void     setSn_RX_RD(uint8_t sn, uint16_t val);
  void    setSn_KPALVTR(uint8_t sn, uint8_t val);
  uint8_t getSn_KPALVTR(uint8_t sn);
#endif

/*======================================================================
 * チップ初期化・バッファ管理
 *====================================================================*/

/* チップ初期化 (ソケットバッファサイズ設定含む) */
int8_t wizchip_init(uint8_t *txsize, uint8_t *rxsize);

/* ソフトリセット */
void wizchip_sw_reset(void);

/* TX/RX バッファ操作 */
uint16_t getSn_TX_FSR(uint8_t sn);
uint16_t getSn_RX_RSR(uint8_t sn);
void     wiz_send_data(uint8_t sn, uint8_t *data, uint16_t len);
void     wiz_recv_data(uint8_t sn, uint8_t *data, uint16_t len);
void     wiz_recv_ignore(uint8_t sn, uint16_t len);

/* ネットワーク情報設定 */
typedef struct {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t sn[4];
    uint8_t gw[4];
    uint8_t dns[4];
    uint8_t dhcp;
} wiz_NetInfo;

#define NETINFO_STATIC  1
#define NETINFO_DHCP    2

void wizchip_setnetinfo(wiz_NetInfo *info);
void wizchip_getnetinfo(wiz_NetInfo *info);

/*======================================================================
 * SPI HAL コールバック (チップ共通)
 *====================================================================*/

typedef struct {
    struct {
        void (*_enter)(void);
        void (*_exit)(void);
    } CRIS;
    struct {
        void (*_select)(void);
        void (*_deselect)(void);
    } CS;
    struct {
        struct {
            uint8_t (*_read_byte)(void);
            void    (*_write_byte)(uint8_t);
            void    (*_read_burst)(uint8_t*, uint16_t);
            void    (*_write_burst)(uint8_t*, uint16_t);
        } SPI;
    } IF;
} _WIZCHIP;

extern _WIZCHIP WIZCHIP;

void reg_wizchip_cris_cbfunc(void(*enter)(void), void(*exit)(void));
void reg_wizchip_cs_cbfunc(void(*sel)(void), void(*desel)(void));
void reg_wizchip_spi_cbfunc(uint8_t(*rb)(void), void(*wb)(uint8_t));
void reg_wizchip_spiburst_cbfunc(void(*rb)(uint8_t*,uint16_t), void(*wb)(uint8_t*,uint16_t));

#define WIZCHIP_CRITICAL_ENTER()  WIZCHIP.CRIS._enter()
#define WIZCHIP_CRITICAL_EXIT()   WIZCHIP.CRIS._exit()

#endif /* __WIZNET_DRV_H__ */
