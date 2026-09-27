/* PC 上の検証用: Flash データ領域を RAM 上の配列で置き換える */
#ifndef SHIM_FAKE_FLASH_H
#define SHIM_FAKE_FLASH_H
#include <stddef.h>
#include <tk/typedef.h>
extern UB fake_flash[512 * 1024];
#define H5_FLASH_PTR(offset)	((const UB *)(fake_flash + (offset)))
void *Kmalloc(size_t size);
#endif
