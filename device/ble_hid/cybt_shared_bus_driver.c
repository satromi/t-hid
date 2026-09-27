/*
 * cybt_shared_bus_driver.c -- CYW43 BT shared bus low-level driver
 *
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CYW43439 BT コントローラとのバックプレーン共有メモリ通信を実装する。
 */

#include <sys/machine.h>
#ifdef CPU_RP2040

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PROHIBIT_DEF_SIZE_T
#include <tk/tkernel.h>

#include "cyw43_ll.h"
#include "cybt_shared_bus_driver.h"
#include "cybt_logging.h"

/*----------------------------------------------------------------------
 * 定数
 */
#define BTFW_MEM_OFFSET             (0x19000000)

/* BIT0 => WLAN Power UP, BIT1 => WLAN Wake */
#define BT2WLAN_PWRUP_WAKE          (0x03)
#define BT2WLAN_PWRUP_ADDR          (0x640894)

#define BTSDIO_OFFSET_HOST2BT_IN    (0x00002000)
#define BTSDIO_OFFSET_HOST2BT_OUT   (0x00002004)
#define BTSDIO_OFFSET_BT2HOST_IN    (0x00002008)
#define BTSDIO_OFFSET_BT2HOST_OUT   (0x0000200C)

#define H2B_BUF_ADDR                (buf_info.host2bt_buf_addr)
#define H2B_BUF_IN_ADDR             (buf_info.host2bt_in_addr)
#define H2B_BUF_OUT_ADDR            (buf_info.host2bt_out_addr)
#define B2H_BUF_ADDR                (buf_info.bt2host_buf_addr)
#define B2H_BUF_IN_ADDR             (buf_info.bt2host_in_addr)
#define B2H_BUF_OUT_ADDR            (buf_info.bt2host_out_addr)

static uint32_t wlan_ram_base_addr;
volatile uint32_t host_ctrl_cache_reg = 0;
#define WLAN_RAM_BASE_ADDR          (wlan_ram_base_addr)

#define BT_CTRL_REG_ADDR            ((uint32_t)0x18000c7c)
#define HOST_CTRL_REG_ADDR          ((uint32_t)0x18000d6c)
#define WLAN_RAM_BASE_REG_ADDR      ((uint32_t)0x18000d68)

typedef struct {
    uint32_t host2bt_buf_addr;
    uint32_t host2bt_in_addr;
    uint32_t host2bt_out_addr;
    uint32_t bt2host_buf_addr;
    uint32_t bt2host_in_addr;
    uint32_t bt2host_out_addr;
} cybt_fw_membuf_info_t;

static cybt_fw_membuf_info_t buf_info;

#define BTSDIO_REG_DATA_VALID_BITMASK   (1 << 1)
#define BTSDIO_REG_WAKE_BT_BITMASK     (1 << 17)
#define BTSDIO_REG_SW_RDY_BITMASK      (1 << 24)

#define BTSDIO_REG_BT_AWAKE_BITMASK    (1 << 8)
#define BTSDIO_REG_FW_RDY_BITMASK      (1 << 24)

#define BTSDIO_OFFSET_HOST_WRITE_BUF   (0)
#define BTSDIO_OFFSET_HOST_READ_BUF    BTSDIO_FWBUF_SIZE

#define ROUNDUP(x, a)      ((((x) + ((a) - 1)) / (a)) * (a))
#define ROUNDDN(x, a)      ((x) & ~((a) - 1))
#define ISALIGNED(a, x)    (((uint32_t)(a) & ((x) - 1)) == 0)

/*----------------------------------------------------------------------
 * FW ダウンロード用構造体
 */
#define BTFW_ADDR_MODE_UNKNOWN      (0)
#define BTFW_ADDR_MODE_EXTENDED     (1)
#define BTFW_ADDR_MODE_SEGMENT      (2)
#define BTFW_ADDR_MODE_LINEAR32     (3)

#define BTFW_HEX_LINE_TYPE_DATA                      (0)
#define BTFW_HEX_LINE_TYPE_END_OF_DATA               (1)
#define BTFW_HEX_LINE_TYPE_EXTENDED_SEGMENT_ADDRESS   (2)
#define BTFW_HEX_LINE_TYPE_EXTENDED_ADDRESS           (4)
#define BTFW_HEX_LINE_TYPE_ABSOLUTE_32BIT_ADDRESS     (5)

typedef struct cybt_fw_cb {
    const uint8_t *p_fw_mem_start;
    uint32_t fw_len;
    const uint8_t *p_next_line_start;
} cybt_fw_cb_t;

typedef struct hex_file_data {
    int addr_mode;
    uint16_t hi_addr;
    uint32_t dest_addr;
    uint8_t *p_ds;
} hex_file_data_t;

static cyw43_ll_t *cyw43_ll = NULL;

/*----------------------------------------------------------------------
 * 内部ヘルパー関数
 */
static cybt_result_t cybt_reg_write(uint32_t reg_addr, uint32_t value)
{
    cybt_debug("cybt_reg_write 0x%08lx 0x%08lx\n", reg_addr, value);
    cyw43_ll_write_backplane_reg(cyw43_ll, reg_addr, value);
    if (reg_addr == HOST_CTRL_REG_ADDR) {
        host_ctrl_cache_reg = value;
    }
    return CYBT_SUCCESS;
}

static cybt_result_t cybt_reg_read(uint32_t reg_addr, uint32_t *p_value)
{
    if (reg_addr == HOST_CTRL_REG_ADDR) {
        *p_value = host_ctrl_cache_reg;
        return CYBT_SUCCESS;
    }
    *p_value = cyw43_ll_read_backplane_reg(cyw43_ll, reg_addr);
    cybt_debug("cybt_reg_read 0x%08lx == 0x%08lx\n", reg_addr, *p_value);
    return CYBT_SUCCESS;
}

static cybt_result_t cybt_mem_write(uint32_t mem_addr,
                                    const uint8_t *p_data, uint32_t data_len)
{
    cybt_debug("cybt_mem_write addr 0x%08lx len %ld\n", mem_addr, data_len);
    do {
        uint32_t transfer_size = (data_len > CYW43_BUS_MAX_BLOCK_SIZE)
                                 ? CYW43_BUS_MAX_BLOCK_SIZE : data_len;
        if ((mem_addr & 0xFFF) + transfer_size > 0x1000) {
            transfer_size = 0x1000 - (mem_addr & 0xFFF);
        }
        cyw43_ll_write_backplane_mem(cyw43_ll, mem_addr, transfer_size, p_data);
        data_len -= transfer_size;
        p_data   += transfer_size;
        mem_addr += transfer_size;
    } while (data_len > 0);
    return CYBT_SUCCESS;
}

static cybt_result_t cybt_mem_read(uint32_t mem_addr,
                                   uint8_t *p_data, uint32_t data_len)
{
    cybt_debug("cybt_mem_read addr 0x%08lx len %ld\n", mem_addr, data_len);
    do {
        uint32_t transfer_size = (data_len > CYW43_BUS_MAX_BLOCK_SIZE)
                                 ? CYW43_BUS_MAX_BLOCK_SIZE : data_len;
        if ((mem_addr & 0xFFF) + transfer_size > 0x1000) {
            transfer_size = 0x1000 - (mem_addr & 0xFFF);
        }
        cyw43_ll_read_backplane_mem(cyw43_ll, mem_addr, transfer_size, p_data);
        data_len -= transfer_size;
        p_data   += transfer_size;
        mem_addr += transfer_size;
    } while (data_len > 0);
    return CYBT_SUCCESS;
}

static uint32_t cybt_get_addr(cybt_addr_idx_t addr_idx)
{
    switch (addr_idx) {
    case H2B_BUF_ADDR_IDX:     return H2B_BUF_ADDR;
    case H2B_BUF_IN_ADDR_IDX:  return H2B_BUF_IN_ADDR;
    case H2B_BUF_OUT_ADDR_IDX: return H2B_BUF_OUT_ADDR;
    case B2H_BUF_ADDR_IDX:     return B2H_BUF_ADDR;
    case B2H_BUF_IN_ADDR_IDX:  return B2H_BUF_IN_ADDR;
    case B2H_BUF_OUT_ADDR_IDX: return B2H_BUF_OUT_ADDR;
    default:                   return 0;
    }
}

/*----------------------------------------------------------------------
 * FW ダウンロード — バイナリ形式 (cyw43_btfw_43439.h)
 */
static uint32_t cybt_fw_get_data(cybt_fw_cb_t *p_btfw_cb,
                                 hex_file_data_t *hfd)
{
    uint32_t abs_base_addr32 = 0;
    while (1) {
        /* 4 byte ヘッダ */
        uint8_t num_bytes = *(p_btfw_cb->p_next_line_start)++;
        uint16_t addr     = *(p_btfw_cb->p_next_line_start)++ << 8;
        addr             |= *(p_btfw_cb->p_next_line_start)++;
        uint8_t type      = *(p_btfw_cb->p_next_line_start)++;

        if (num_bytes == 0) break;

        memcpy(hfd->p_ds, p_btfw_cb->p_next_line_start, num_bytes);
        p_btfw_cb->p_next_line_start += num_bytes;

        if (type == BTFW_HEX_LINE_TYPE_EXTENDED_ADDRESS) {
            hfd->hi_addr = (hfd->p_ds[0] << 8) | hfd->p_ds[1];
            hfd->addr_mode = BTFW_ADDR_MODE_EXTENDED;
        } else if (type == BTFW_HEX_LINE_TYPE_EXTENDED_SEGMENT_ADDRESS) {
            hfd->hi_addr = (hfd->p_ds[0] << 8) | hfd->p_ds[1];
            hfd->addr_mode = BTFW_ADDR_MODE_SEGMENT;
        } else if (type == BTFW_HEX_LINE_TYPE_ABSOLUTE_32BIT_ADDRESS) {
            abs_base_addr32 = (hfd->p_ds[0] << 24) | (hfd->p_ds[1] << 16) |
                              (hfd->p_ds[2] << 8) | hfd->p_ds[3];
            hfd->addr_mode = BTFW_ADDR_MODE_LINEAR32;
        } else if (type == BTFW_HEX_LINE_TYPE_DATA) {
            hfd->dest_addr = addr;
            if (hfd->addr_mode == BTFW_ADDR_MODE_EXTENDED) {
                hfd->dest_addr += (hfd->hi_addr << 16);
            } else if (hfd->addr_mode == BTFW_ADDR_MODE_SEGMENT) {
                hfd->dest_addr += (hfd->hi_addr << 4);
            } else if (hfd->addr_mode == BTFW_ADDR_MODE_LINEAR32) {
                hfd->dest_addr += abs_base_addr32;
            }
            return num_bytes;
        }
    }
    return 0;
}

/*----------------------------------------------------------------------
 * 公開 API
 */

void cybt_sharedbus_driver_init(cyw43_ll_t *driver)
{
    cyw43_ll = driver;
}

cybt_result_t cybt_fw_download(const uint8_t *p_bt_firmware,
                               uint32_t bt_firmware_len,
                               uint8_t *p_write_buf,
                               uint8_t *p_hex_buf)
{
    cybt_fw_cb_t btfw_cb;
    hex_file_data_t hfd = { BTFW_ADDR_MODE_EXTENDED, 0, 0, NULL };
    uint8_t *p_mem_ptr;
    uint32_t data_len;

    if (cyw43_ll == NULL) return CYBT_ERR_BADARG;
    if (p_bt_firmware == NULL || bt_firmware_len == 0 ||
        p_write_buf == NULL || p_hex_buf == NULL)
        return CYBT_ERR_BADARG;

    /* BT FW はバージョン文字列 + レコードから始まる */
    {
        uint8_t version_len = *p_bt_firmware;
        cybt_info("BT FW download, version = %s\n", p_bt_firmware + 1);
        p_bt_firmware += version_len + 1;   /* バージョン文字列スキップ */
        p_bt_firmware += 1;                  /* レコード数スキップ */
    }

    p_mem_ptr = p_write_buf;
    if ((uint32_t)(uintptr_t)p_mem_ptr % BTFW_SD_ALIGN) {
        p_mem_ptr += (BTFW_SD_ALIGN - ((uint32_t)(uintptr_t)p_mem_ptr % BTFW_SD_ALIGN));
    }

    hfd.p_ds = p_hex_buf;

    btfw_cb.p_fw_mem_start  = p_bt_firmware;
    btfw_cb.fw_len          = bt_firmware_len;
    btfw_cb.p_next_line_start = p_bt_firmware;

    cybt_reg_write(BTFW_MEM_OFFSET + BT2WLAN_PWRUP_ADDR, BT2WLAN_PWRUP_WAKE);

    while ((data_len = cybt_fw_get_data(&btfw_cb, &hfd)) > 0) {
        uint32_t fwmem_start_addr, fwmem_start_data, fwmem_end_addr, fwmem_end_data;
        uint32_t write_data_len, idx, pad;

        fwmem_start_addr = BTFW_MEM_OFFSET + hfd.dest_addr;
        write_data_len = 0;

        /* 開始アドレスを 4 バイトアライン */
        if (!ISALIGNED(fwmem_start_addr, 4)) {
            pad = fwmem_start_addr % 4;
            fwmem_start_addr = ROUNDDN(fwmem_start_addr, 4);
            cybt_mem_read(fwmem_start_addr, (uint8_t *)&fwmem_start_data,
                          sizeof(uint32_t));
            for (idx = 0; idx < pad; idx++, write_data_len++) {
                p_mem_ptr[write_data_len] = ((uint8_t *)&fwmem_start_data)[idx];
            }
        }
        memcpy(&p_mem_ptr[write_data_len], hfd.p_ds, data_len);
        write_data_len += data_len;

        /* 長さを 4 バイトの倍数に */
        fwmem_end_addr = fwmem_start_addr + write_data_len;
        if (!ISALIGNED(fwmem_end_addr, 4)) {
            cybt_mem_read(ROUNDDN(fwmem_end_addr, 4), (uint8_t *)&fwmem_end_data,
                          sizeof(uint32_t));
            for (idx = (fwmem_end_addr % 4); idx < 4; idx++, write_data_len++) {
                p_mem_ptr[write_data_len] = ((uint8_t *)&fwmem_end_data)[idx];
            }
        }

        /* バックプレーン経由で FW データ書込み */
        if (((fwmem_start_addr & 0xFFF) + write_data_len) <= 0x1000) {
            cybt_mem_write(fwmem_start_addr, p_mem_ptr, write_data_len);
        } else {
            uint32_t first_write_len = 0x1000 - (fwmem_start_addr & 0xFFF);
            cybt_mem_write(fwmem_start_addr, p_mem_ptr, first_write_len);
            cybt_mem_write(fwmem_start_addr + first_write_len,
                           p_mem_ptr + first_write_len,
                           write_data_len - first_write_len);
        }
    }

    return CYBT_SUCCESS;
}

cybt_result_t cybt_init_buffer(void)
{
    int result;
    result = cybt_reg_read(WLAN_RAM_BASE_REG_ADDR, &WLAN_RAM_BASE_ADDR);
    if (CYBT_SUCCESS != result) return result;

    cybt_info("hci_open(): btfw ram base = 0x%lx\n", WLAN_RAM_BASE_ADDR);

    /* データバッファアドレス */
    H2B_BUF_ADDR = WLAN_RAM_BASE_ADDR + BTSDIO_OFFSET_HOST_WRITE_BUF;
    B2H_BUF_ADDR = WLAN_RAM_BASE_ADDR + BTSDIO_OFFSET_HOST_READ_BUF;

    /* 循環バッファインデックスアドレス */
    H2B_BUF_IN_ADDR  = WLAN_RAM_BASE_ADDR + BTSDIO_OFFSET_HOST2BT_IN;
    H2B_BUF_OUT_ADDR = WLAN_RAM_BASE_ADDR + BTSDIO_OFFSET_HOST2BT_OUT;
    B2H_BUF_IN_ADDR  = WLAN_RAM_BASE_ADDR + BTSDIO_OFFSET_BT2HOST_IN;
    B2H_BUF_OUT_ADDR = WLAN_RAM_BASE_ADDR + BTSDIO_OFFSET_BT2HOST_OUT;

    /* 循環バッファインデックスをゼロ初期化 */
    uint32_t reg_val = 0;
    cybt_reg_write(H2B_BUF_IN_ADDR,  reg_val);
    cybt_reg_write(H2B_BUF_OUT_ADDR, reg_val);
    cybt_reg_write(B2H_BUF_IN_ADDR,  reg_val);
    cybt_reg_write(B2H_BUF_OUT_ADDR, reg_val);

    return CYBT_SUCCESS;
}

cybt_result_t cybt_set_host_ready(void)
{
    uint32_t reg_val;
    cybt_reg_read(HOST_CTRL_REG_ADDR, &reg_val);
    reg_val |= BTSDIO_REG_SW_RDY_BITMASK;
    cybt_reg_write(HOST_CTRL_REG_ADDR, reg_val);
    return CYBT_SUCCESS;
}

cybt_result_t cybt_toggle_bt_intr(void)
{
    uint32_t reg_val, new_val;
    cybt_reg_read(HOST_CTRL_REG_ADDR, &reg_val);

    if ((reg_val & ~(BTSDIO_REG_SW_RDY_BITMASK |
                     BTSDIO_REG_WAKE_BT_BITMASK |
                     BTSDIO_REG_DATA_VALID_BITMASK)) != 0) {
        cybt_error("cybt_toggle_bt_intr: HOST_CTRL_REG corruption 0x%08lx\n",
                   reg_val);
        /* レジスタ破損検出 — 停止せずに続行する */
    }

    new_val = reg_val ^ BTSDIO_REG_DATA_VALID_BITMASK;
    cybt_reg_write(HOST_CTRL_REG_ADDR, new_val);
    return CYBT_SUCCESS;
}

cybt_result_t cybt_set_bt_intr(int value)
{
    uint32_t reg_val, new_val;
    cybt_reg_read(HOST_CTRL_REG_ADDR, &reg_val);
    if (value)
        new_val = reg_val | BTSDIO_REG_DATA_VALID_BITMASK;
    else
        new_val = reg_val & ~BTSDIO_REG_DATA_VALID_BITMASK;
    cybt_reg_write(HOST_CTRL_REG_ADDR, new_val);
    return CYBT_SUCCESS;
}

int cybt_ready(void)
{
    uint32_t reg_val;
    cybt_reg_read(BT_CTRL_REG_ADDR, &reg_val);
    return (reg_val & BTSDIO_REG_FW_RDY_BITMASK) ? 1 : 0;
}

int cybt_awake(void)
{
    uint32_t reg_val;
    cybt_reg_read(BT_CTRL_REG_ADDR, &reg_val);
    return (reg_val & BTSDIO_REG_BT_AWAKE_BITMASK) ? 1 : 0;
}

cybt_result_t cybt_set_bt_awake(int value)
{
    uint32_t reg_val_before;
    cybt_reg_read(HOST_CTRL_REG_ADDR, &reg_val_before);

    uint32_t reg_val_after = reg_val_before;
    if (value)
        reg_val_after |= BTSDIO_REG_WAKE_BT_BITMASK;
    else
        reg_val_after &= ~BTSDIO_REG_WAKE_BT_BITMASK;

    if (reg_val_before != reg_val_after) {
        cybt_reg_write(HOST_CTRL_REG_ADDR, reg_val_after);
    }
    return CYBT_SUCCESS;
}

cybt_result_t cybt_get_bt_buf_index(cybt_fw_membuf_index_t *p_buf_index)
{
    uint32_t buf[4];
    static int consecutive_errors = 0;
    int retry;

    /*
     * SPI 間欠エラー対策: 読み出し失敗時に即リトライ (最大 2 回)。
     * PIO SPI のタイミングジッターで稀にビットエラーが発生するが、
     * 即座にリトライすれば成功することが多い。
     */
    for (retry = 0; retry < 3; retry++) {
        cybt_mem_read(H2B_BUF_IN_ADDR, (uint8_t *)buf, sizeof(buf));

        p_buf_index->host2bt_in_val  = buf[0];
        p_buf_index->host2bt_out_val = buf[1];
        p_buf_index->bt2host_in_val  = buf[2];
        p_buf_index->bt2host_out_val = buf[3];

        if (p_buf_index->host2bt_in_val  < BTSDIO_FWBUF_SIZE &&
            p_buf_index->host2bt_out_val < BTSDIO_FWBUF_SIZE &&
            p_buf_index->bt2host_in_val  < BTSDIO_FWBUF_SIZE &&
            p_buf_index->bt2host_out_val < BTSDIO_FWBUF_SIZE) {
            /* 正常 */
            consecutive_errors = 0;
            return CYBT_SUCCESS;
        }
    }

    /* 3 回リトライしても失敗 */
    consecutive_errors++;
    if (consecutive_errors <= 3) {
        cybt_error("cybt_get_bt_buf_index: invalid buffer value\n");
    }
    if (consecutive_errors >= 5) {
        cybt_error("cybt_get_bt_buf_index: reset buffer\n");
        cybt_init_buffer();
        consecutive_errors = 0;
    }
    return CYBT_ERR_GENERIC;

    return CYBT_SUCCESS;
}

cybt_result_t cybt_reg_write_idx(cybt_addr_idx_t reg_idx, uint32_t value)
{
    if ((reg_idx != H2B_BUF_IN_ADDR_IDX &&
         reg_idx != B2H_BUF_OUT_ADDR_IDX) || value >= BTSDIO_FWBUF_SIZE) {
        return CYBT_ERR_BADARG;
    }
    uint32_t reg_addr = cybt_get_addr(reg_idx);
    return cybt_reg_write(reg_addr, value);
}

cybt_result_t cybt_mem_write_idx(cybt_addr_idx_t mem_idx, uint32_t offset,
                                 const uint8_t *p_data, uint32_t data_len)
{
    if (mem_idx != H2B_BUF_ADDR_IDX || (offset + data_len) > BTSDIO_FWBUF_SIZE) {
        return CYBT_ERR_BADARG;
    }
    if (!ISALIGNED(p_data, 4)) {
        return CYBT_ERR_BADARG;
    }
    uint32_t mem_addr = cybt_get_addr(mem_idx) + offset;
    return cybt_mem_write(mem_addr, p_data, data_len);
}

cybt_result_t cybt_mem_read_idx(cybt_addr_idx_t mem_idx, uint32_t offset,
                                uint8_t *p_data, uint32_t data_len)
{
    if (mem_idx != B2H_BUF_ADDR_IDX || (offset + data_len) > BTSDIO_FWBUF_SIZE) {
        return CYBT_ERR_BADARG;
    }
    uint32_t mem_addr = cybt_get_addr(mem_idx) + offset;
    return cybt_mem_read(mem_addr, p_data, data_len);
}

void cybt_debug_dump(void)
{
    uint32_t reg_val = 0;
    cybt_fw_membuf_index_t buf_index;

    cybt_error("WLAN_RAM_BASE_ADDR: 0x%08lx\n", WLAN_RAM_BASE_ADDR);
    cybt_error("H2B_BUF_ADDR: 0x%08lx\n", H2B_BUF_ADDR);
    cybt_error("B2H_BUF_ADDR: 0x%08lx\n", B2H_BUF_ADDR);

    cybt_get_bt_buf_index(&buf_index);

    cybt_reg_read(HOST_CTRL_REG_ADDR, &reg_val);
    cybt_error("HOST_CTRL_REG: 0x%08lx\n", reg_val);

    cybt_reg_read(BT_CTRL_REG_ADDR, &reg_val);
    cybt_error("BT_CTRL_REG: 0x%08lx\n", reg_val);
}

#endif /* CPU_RP2040 */
