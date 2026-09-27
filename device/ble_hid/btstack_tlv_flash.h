/*
 * btstack_tlv_flash.h — 独自 TLV Flash ストレージ for RP2040
 *
 * 2 バンク × 4KB (Flash 末尾 8KB) でボンディングデータを永続保存。
 * btstack_tlv_t インターフェースを直接実装。
 */

#ifndef BTSTACK_TLV_FLASH_H
#define BTSTACK_TLV_FLASH_H

#include "btstack_tlv.h"

const btstack_tlv_t *btstack_tlv_flash_instance(void);
void *btstack_tlv_flash_context(void);
void btstack_tlv_flash_init(void);

#endif /* BTSTACK_TLV_FLASH_H */
