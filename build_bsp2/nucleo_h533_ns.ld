/*
 *  Linker script for NUCLEO-H533RE (STM32H533RET6)
 *  TrustZone の非セキュア側イメージ (make TZ=1)
 *  Flash: 0x08010000 から 384KB (先頭 64KB はセキュア側、上位 64KB はデータ領域)
 *  SRAM:  0x20000000 から 240KB (上位 32KB はセキュア側)
 *
 *  NOTE: STM32CubeMX が生成するリンカスクリプトを使う場合は
 *        このファイルを置き換えること。
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08010000, LENGTH = 384K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 240K
}

INCLUDE nucleo_h533_sections.ld
