/*
 *  Linker script for NUCLEO-H533RE (STM32H533RET6)
 *  Flash: 512KB @ 0x08000000 (上位 64KB はデータ領域: device/flash/stm32h5_flash.h)
 *  SRAM:  272KB @ 0x20000000
 *
 *  NOTE: STM32CubeMX が生成するリンカスクリプトを使う場合は
 *        このファイルを置き換えること。
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 448K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 272K
}

INCLUDE nucleo_h533_sections.ld
