/*
 * btstack_tlv_flash.c — 独自 TLV Flash ストレージ for RP2040
 *
 * BTstack の btstack_tlv_t インターフェースを直接実装。
 * btstack_tlv_flash_bank.c (BTstack ライブラリ) は使用しない。
 *
 * Flash 構成: 2 バンク × 4KB (Flash 末尾 8KB)
 *   Bank 0: 0x1FE000  Bank 1: 0x1FF000
 *
 * バンクヘッダ: [magic:4 "TKtv"][seq:4]
 * エントリ: [tag:4][len:4][data:len (4byte align)]
 *   tag=0xFFFFFFFF → 空き (終端)
 *   tag=0x00000000 → 削除済み
 *
 * 書込みはページ単位 (256B, ~2ms 割込禁止)。
 * バンク切替 (セクター消去 ~25ms) はバンク満杯時のみ。
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>
#include <tk/syslib.h>
#include <sys/sysdef.h>
#include <tm/tmonitor.h>

#include "btstack_config.h"
#include "btstack_tlv.h"
#include "btstack_tlv_flash.h"

/*----------------------------------------------------------------------
 * Flash 定数
 */
#define XIP_BASE            0x10000000
#define FLASH_PAGE_SIZE     256
#define FLASH_SECTOR_SIZE   4096
#define FLASH_BANK_SIZE     FLASH_SECTOR_SIZE
#define FLASH_BANK_OFFSET   (0x200000 - FLASH_BANK_SIZE * 2)
#define BANK_HEADER_SIZE    8       /* magic(4) + seq(4) */
#define TLV_MAGIC           0x76744B54  /* "TKtv" little-endian */
#define ENTRY_ALIGN         4

/*----------------------------------------------------------------------
 * RP2040 bootrom Flash ヘルパー
 */
typedef void (*rom_void_fn)(void);
typedef void (*rom_flash_erase_fn)(uint32_t, uint32_t, uint32_t, uint8_t);
typedef void (*rom_flash_program_fn)(uint32_t, const uint8_t *, uint32_t);

__attribute__((noinline, section(".data")))
LOCAL void *rom_func_lookup(uint32_t code)
{
    typedef void *(*rom_table_lookup_fn)(uint16_t *, uint32_t);
    uint16_t *ft = (uint16_t *)(uint32_t)*(uint16_t *)0x14;
    rom_table_lookup_fn lk = (rom_table_lookup_fn)(uint32_t)*(uint16_t *)0x18;
    return lk(ft, code);
}

#define BOOT2_SIZE_WORDS 64
LOCAL uint32_t boot2_copyout[BOOT2_SIZE_WORDS];
LOCAL BOOL boot2_copied = FALSE;
LOCAL rom_void_fn rom_connect, rom_exit_xip, rom_flush_cache;
LOCAL rom_flash_erase_fn rom_erase;
LOCAL rom_flash_program_fn rom_program;
LOCAL void (*rom_boot2)(void);

LOCAL void flash_hw_init(void)
{
    if (boot2_copied) return;
    const uint32_t *b2 = (const uint32_t *)XIP_BASE;
    for (int i = 0; i < BOOT2_SIZE_WORDS; i++) boot2_copyout[i] = b2[i];
    boot2_copied = TRUE;
    rom_connect    = (rom_void_fn)rom_func_lookup('I' | ('F' << 8));
    rom_exit_xip   = (rom_void_fn)rom_func_lookup('E' | ('X' << 8));
    rom_erase      = (rom_flash_erase_fn)rom_func_lookup('R' | ('E' << 8));
    rom_program    = (rom_flash_program_fn)rom_func_lookup('R' | ('P' << 8));
    rom_flush_cache = (rom_void_fn)rom_func_lookup('F' | ('C' << 8));
    rom_boot2      = (void (*)(void))((uint32_t)boot2_copyout + 1);
}

/*----------------------------------------------------------------------
 * 低レベル Flash 操作 (RAM 上で実行)
 */
__attribute__((noinline, section(".data")))
LOCAL void flash_erase_raw(uint32_t offset)
{
    uint32_t pm;
    __asm volatile ("mrs %0, primask" : "=r" (pm));
    __asm volatile ("cpsid i");
    rom_connect(); rom_exit_xip();
    rom_erase(offset, FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE, 0x20);
    rom_flush_cache(); rom_boot2();
    __asm volatile ("msr primask, %0" :: "r" (pm));
}

__attribute__((noinline, section(".data")))
LOCAL void flash_write_page_raw(uint32_t offset, const uint8_t *data)
{
    uint32_t pm;
    __asm volatile ("mrs %0, primask" : "=r" (pm));
    __asm volatile ("cpsid i");
    rom_connect(); rom_exit_xip();
    rom_program(offset, data, FLASH_PAGE_SIZE);
    rom_flush_cache(); rom_boot2();
    __asm volatile ("msr primask, %0" :: "r" (pm));
}

/*----------------------------------------------------------------------
 * Flash 読み書きヘルパー
 */
LOCAL const uint8_t *bank_ptr(int bank, uint32_t offset)
{
    return (const uint8_t *)(XIP_BASE + FLASH_BANK_OFFSET +
                             FLASH_BANK_SIZE * bank + offset);
}

LOCAL void flash_erase_bank(int bank)
{
    flash_erase_raw(FLASH_BANK_OFFSET + FLASH_BANK_SIZE * bank);
}

LOCAL void flash_write(int bank, uint32_t offset,
                       const uint8_t *data, uint32_t size)
{
    static uint8_t page_buf[FLASH_PAGE_SIZE];
    uint32_t base = FLASH_BANK_OFFSET + FLASH_BANK_SIZE * bank;
    uint32_t pos = 0;
    while (pos < size) {
        uint32_t page = (base + offset + pos) & ~(FLASH_PAGE_SIZE - 1);
        uint32_t off  = (base + offset + pos) - page;
        uint32_t chunk = FLASH_PAGE_SIZE - off;
        if (chunk > size - pos) chunk = size - pos;
        memcpy(page_buf, (const uint8_t *)(XIP_BASE + page), FLASH_PAGE_SIZE);
        memcpy(page_buf + off, data + pos, chunk);
        flash_write_page_raw(page, page_buf);
        pos += chunk;
    }
}

/*----------------------------------------------------------------------
 * バンク管理
 */
LOCAL int current_bank = -1;
LOCAL uint32_t current_seq = 0;
LOCAL uint32_t write_pos = 0;  /* 現在バンク内の書込み位置 */

LOCAL uint32_t align4(uint32_t v) { return (v + 3) & ~3u; }

LOCAL void bank_read_header(int bank, uint32_t *magic, uint32_t *seq)
{
    memcpy(magic, bank_ptr(bank, 0), 4);
    memcpy(seq, bank_ptr(bank, 4), 4);
}

LOCAL void bank_write_header(int bank, uint32_t seq)
{
    uint8_t hdr[BANK_HEADER_SIZE];
    uint32_t m = TLV_MAGIC;
    memcpy(hdr, &m, 4);
    memcpy(hdr + 4, &seq, 4);
    flash_write(bank, 0, hdr, BANK_HEADER_SIZE);
}

LOCAL void tlv_init_banks(void)
{
    uint32_t magic0, seq0, magic1, seq1;
    bank_read_header(0, &magic0, &seq0);
    bank_read_header(1, &magic1, &seq1);

    BOOL valid0 = (magic0 == TLV_MAGIC);
    BOOL valid1 = (magic1 == TLV_MAGIC);

    if (valid0 && valid1) {
        /* 両方有効: seq が大きい方が最新 */
        current_bank = (seq1 > seq0) ? 1 : 0;
        current_seq = (seq1 > seq0) ? seq1 : seq0;
    } else if (valid0) {
        current_bank = 0;
        current_seq = seq0;
    } else if (valid1) {
        current_bank = 1;
        current_seq = seq1;
    } else {
        /* どちらも無効: bank 0 を初期化 */
        flash_erase_bank(0);
        flash_erase_bank(1);
        current_bank = 0;
        current_seq = 1;
        bank_write_header(0, current_seq);
    }

    /* 書込み位置を検索 (ヘッダの後から) */
    write_pos = BANK_HEADER_SIZE;
    while (write_pos + 8 <= FLASH_BANK_SIZE) {
        uint32_t t, l;
        memcpy(&t, bank_ptr(current_bank, write_pos), 4);
        memcpy(&l, bank_ptr(current_bank, write_pos + 4), 4);
        if (t == 0xFFFFFFFF) break;
        if (l > FLASH_BANK_SIZE) break;
        write_pos += 8 + align4(l);
    }
}

/*----------------------------------------------------------------------
 * バンク切替 (マイグレーション)
 */
LOCAL void tlv_migrate(void)
{
    int new_bank = 1 - current_bank;
    flash_erase_bank(new_bank);

    uint32_t new_seq = current_seq + 1;
    bank_write_header(new_bank, new_seq);

    /* 現在バンクの有効エントリを新バンクにコピー */
    uint32_t src = BANK_HEADER_SIZE;
    uint32_t dst = BANK_HEADER_SIZE;
    while (src + 8 <= FLASH_BANK_SIZE) {
        uint32_t t, l;
        memcpy(&t, bank_ptr(current_bank, src), 4);
        memcpy(&l, bank_ptr(current_bank, src + 4), 4);
        if (t == 0xFFFFFFFF) break;
        if (l > FLASH_BANK_SIZE) break;
        uint32_t entry_size = 8 + align4(l);
        if (t != 0x00000000 && l > 0) {
            /* 有効エントリ: 最後の同一タグのみコピー */
            BOOL has_newer = FALSE;
            uint32_t chk = src + entry_size;
            while (chk + 8 <= FLASH_BANK_SIZE) {
                uint32_t ct, cl;
                memcpy(&ct, bank_ptr(current_bank, chk), 4);
                memcpy(&cl, bank_ptr(current_bank, chk + 4), 4);
                if (ct == 0xFFFFFFFF) break;
                if (cl > FLASH_BANK_SIZE) break;
                if (ct == t) { has_newer = TRUE; break; }
                chk += 8 + align4(cl);
            }
            if (!has_newer && dst + entry_size <= FLASH_BANK_SIZE) {
                uint8_t buf[8];
                memcpy(buf, bank_ptr(current_bank, src), 8);
                flash_write(new_bank, dst, buf, 8);
                if (l > 0) {
                    flash_write(new_bank, dst + 8,
                                bank_ptr(current_bank, src + 8), align4(l));
                }
                dst += entry_size;
            }
        }
        src += entry_size;
    }

    current_bank = new_bank;
    current_seq = new_seq;
    write_pos = dst;
}

/*----------------------------------------------------------------------
 * btstack_tlv_t 実装
 */
LOCAL int tlv_get_tag(void *context, uint32_t tag,
                      uint8_t *buffer, uint32_t buffer_size)
{
    (void)context;
    if (current_bank < 0) return 0;

    /* 最後に出現するエントリを返す (上書きセマンティクス) */
    uint32_t pos = BANK_HEADER_SIZE;
    uint32_t found_offset = 0;
    uint32_t found_len = 0;
    while (pos + 8 <= FLASH_BANK_SIZE) {
        uint32_t t, l;
        memcpy(&t, bank_ptr(current_bank, pos), 4);
        memcpy(&l, bank_ptr(current_bank, pos + 4), 4);
        if (t == 0xFFFFFFFF) break;
        if (l > FLASH_BANK_SIZE) break;
        if (t == tag && l > 0) {
            found_offset = pos + 8;
            found_len = l;
        }
        pos += 8 + align4(l);
    }
    if (found_len == 0) return 0;
    uint32_t copy = (found_len < buffer_size) ? found_len : buffer_size;
    memcpy(buffer, bank_ptr(current_bank, found_offset), copy);
    return (int)copy;
}

LOCAL int tlv_store_tag(void *context, uint32_t tag,
                        const uint8_t *data, uint32_t data_size)
{
    (void)context;
    if (current_bank < 0) return 0;

    uint32_t entry_size = 8 + align4(data_size);

    /* 空き不足ならマイグレーション */
    if (write_pos + entry_size > FLASH_BANK_SIZE) {
        tlv_migrate();
        if (write_pos + entry_size > FLASH_BANK_SIZE) return 0;
    }

    /* エントリヘッダ + データを書込み */
    uint8_t hdr[8];
    memcpy(hdr, &tag, 4);
    memcpy(hdr + 4, &data_size, 4);
    flash_write(current_bank, write_pos, hdr, 8);
    if (data_size > 0) {
        /* align4 分のパディングは Flash が 0xFF のまま */
        flash_write(current_bank, write_pos + 8, data, data_size);
    }
    write_pos += entry_size;
    return 1;
}

LOCAL void tlv_delete_tag(void *context, uint32_t tag)
{
    (void)context;
    if (current_bank < 0) return;

    /* tag=0 の削除マーカーを追記 (既存エントリは上書きしない) */
    uint32_t zero_tag = 0x00000000;
    uint32_t zero_len = 0;
    uint32_t entry_size = 8;
    if (write_pos + entry_size > FLASH_BANK_SIZE) {
        tlv_migrate();
        /* マイグレーション後は tag が既に除去されている */
        return;
    }
    /* 削除マーカー: [0x00000000][元のtag値を len に格納] で検索不要 */
    uint8_t hdr[8];
    memcpy(hdr, &zero_tag, 4);
    memcpy(hdr + 4, &zero_len, 4);
    flash_write(current_bank, write_pos, hdr, 8);
    write_pos += entry_size;
}

/*----------------------------------------------------------------------
 * 公開 API
 */
LOCAL const btstack_tlv_t tlv_impl = {
    .get_tag    = tlv_get_tag,
    .store_tag  = tlv_store_tag,
    .delete_tag = tlv_delete_tag,
};

EXPORT const btstack_tlv_t *btstack_tlv_flash_instance(void)
{
    return &tlv_impl;
}

EXPORT void *btstack_tlv_flash_context(void)
{
    return NULL;
}

EXPORT void btstack_tlv_flash_init(void)
{
    flash_hw_init();
    tlv_init_banks();
    tm_printf((UB *)"TLV: bank%d seq=%lu pos=%lu\n",
              current_bank, current_seq, write_pos);
}

#endif /* CPU_RP2040 */
