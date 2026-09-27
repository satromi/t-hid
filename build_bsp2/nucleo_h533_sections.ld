/*
 *  NUCLEO-H533RE の共通セクション定義 (MEMORY は nucleo_h533.ld / nucleo_h533_ns.ld)
 */

/* エントリポイント */
ENTRY(Reset_Handler)

/* 初期スタック (RAM 上端) */
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } >FLASH

    _sidata = LOADADDR(.data);

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >RAM

    .noinit (NOLOAD) :
    {
        *(.noinit)
    } >RAM

    /* μT-Kernel 例外ハンドラテーブル (RAM, 1024 バイト境界) */
    .mtk_exctbl (NOLOAD) :
    {
        . = ALIGN(1024);
        KEEP(*(.mtk_exctbl))
        . = ALIGN(4);
    } >RAM

    /* μT-Kernel 静的システムメモリ (USE_STATIC_SYS_MEM 有効時) */
    .mtk_sysmem (NOLOAD) :
    {
        KEEP(*(.mtk_sysmem))
    } >RAM

    /* ヒープ (bss の後ろからスタックまで) */
    . = ALIGN(8);
    __end = .;
    _end = .;
    PROVIDE(end = .);

    /* スタック */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        . = . + 0x400;  /* ヒープ最小 1KB */
        . = . + 0x800;  /* スタック最小 2KB */
        . = ALIGN(8);
    } >RAM
}
