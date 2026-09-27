/*
 * stm32h5_flash.h — STM32H533 内蔵 Flash の消去・書き込み
 *
 * 512KB = 256KB × 2 バンク、8KB セクタ、書き込み単位 128bit (16 バイト)。
 * 非セキュア側 (TZEN=0) の NS レジスタで操作する。
 *
 * 上位 64KB はデータ領域として予約し、リンカスクリプトでコードを
 * 配置しない (nucleo_h533.ld)。
 *
 *   0x08070000 - 0x08077FFF  32KB  キー処理 wasm モジュール
 *   0x08078000 - 0x08079FFF   8KB  FIDO U2F 鍵・証明書
 *   0x0807A000 - 0x0807DFFF  16KB  FIDO U2F カウンタ (8KB × 2 を交互に使う)
 *   0x0807E000 - 0x0807FFFF   8KB  MCP flash_write ストレージ
 */

#ifndef __STM32H5_FLASH_H__
#define __STM32H5_FLASH_H__

#include <tk/tkernel.h>

#if defined(U2F_SECURE)
#define H5_FLASH_BASE		0x0C000000UL	/* TrustZone のセキュア側から見た Flash */
#else
#define H5_FLASH_BASE		0x08000000UL
#endif
#define H5_FLASH_SIZE		(512 * 1024)
#define H5_FLASH_BANK_SIZE	(256 * 1024)
#define H5_FLASH_SECTOR_SIZE	(8 * 1024)
#define H5_FLASH_QWORD		16

#define H5_FLASH_DATA_OFFSET	0x70000UL		/* データ領域先頭 */
#define H5_FLASH_KBWASM_OFFSET	0x70000UL		/* 32KB */
#define H5_FLASH_KBWASM_SIZE	(32 * 1024)
#define H5_FLASH_U2F_KEYS_OFFSET 0x78000UL		/* 8KB */
#define H5_FLASH_U2F_CNT_A_OFFSET 0x7A000UL		/* 8KB */
#define H5_FLASH_U2F_CNT_B_OFFSET 0x7C000UL		/* 8KB */
#define H5_FLASH_MCP_OFFSET	0x7E000UL		/* 8KB */

/* offset からのセクタを len バイト分消去 (セクタ境界単位) */
ER h5_flash_erase(UW offset, UW len);

/*
 * offset (16 バイト境界) から data を書き込む。len が 16 の倍数でなければ
 * 末尾を 0xFF で埋める。書き込み先は消去済みであること。
 */
ER h5_flash_program(UW offset, const void *data, UW len);

/* 消去 + 書き込み */
ER h5_flash_write(UW offset, const void *data, UW len);

/* ICACHE を無効化する (Flash の内容や読み出し権限が変わった後に呼ぶ) */
void h5_flash_icache_invalidate(void);

/* Flash 上のアドレス (PC 上の検証では差し替える) */
#ifndef H5_FLASH_PTR
#define H5_FLASH_PTR(offset)	((const UB *)(H5_FLASH_BASE + (offset)))
#endif

#endif /* __STM32H5_FLASH_H__ */
