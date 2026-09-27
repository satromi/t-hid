/*
 *----------------------------------------------------------------------
 *    WIZnet 共通ソケットイベント定義
 *
 *    W5100S / W5500 で共通のソケットイベントフラグとパラメータ。
 *    チップ依存の SPI HAL から参照される。
 *----------------------------------------------------------------------
 */

#ifndef __WIZNET_SOCK_H__
#define __WIZNET_SOCK_H__

#include <tk/tkernel.h>

/* ソケットイベントフラグビット (tk_wai_flg 用) */
#define WIZNET_SOCK_EVT_RECV     (1u << 0)
#define WIZNET_SOCK_EVT_CON      (1u << 1)
#define WIZNET_SOCK_EVT_DISCON   (1u << 2)
#define WIZNET_SOCK_EVT_TIMEOUT  (1u << 3)
#define WIZNET_SOCK_EVT_SENDOK   (1u << 4)
#define WIZNET_SOCK_EVT_BREAK    (1u << 5)

/* (旧 W5100S_SOCK_EVT_* は廃止 → WIZNET_SOCK_EVT_* に統一) */

/* IRQ イベント */
#define WIZNET_EVT_IRQ           (1u << 0)

/* 最大ソケット数 (チップ依存) */
#if defined(WIZCHIP_W5500)
  #define WIZNET_SOCK_NUM  8
#else
  #define WIZNET_SOCK_NUM  4
#endif

/* SPI HAL が提供する外部変数 (各チップの spi.c で定義) */
extern ID wiznet_sock_flgid[];   /* ソケット別イベントフラグ */

/* (旧 w5100s_sock_flgid は廃止 → wiznet_sock_flgid に統一) */

/* SPI HAL 公開 API (チップ依存の spi.c が実装) */
void wiznet_spi_init(void);
void wiznet_int_init(void);
void wiznet_hw_reset(void);

#endif /* __WIZNET_SOCK_H__ */
