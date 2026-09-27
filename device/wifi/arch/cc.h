/*
 *	arch/cc.h — lwIP architecture-dependent compiler settings
 *
 *	RP2040 (ARM Cortex-M0+) + GCC 用
 */

#ifndef __LWIP_ARCH_CC_H__
#define __LWIP_ARCH_CC_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/* バイトオーダー */
#define BYTE_ORDER LITTLE_ENDIAN

/* printf フォーマット指定子 */
#define X8_F  "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"

/* パック構造体 (GCC) */
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/* プラットフォーム診断 */
#define LWIP_PLATFORM_DIAG(x)   do { /* tm_printf x; */ } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { /* assert fail */ while(1){} } while(0)

/* rand は cyw43_hal_ticks_us ベース (lwipopts.h で LWIP_RAND 定義済み) */

#endif /* __LWIP_ARCH_CC_H__ */
