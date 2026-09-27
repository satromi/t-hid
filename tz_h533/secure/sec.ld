/*
 * sec.ld — TrustZone のセキュア側イメージ (STM32H533)
 *   Flash 0x0C000000 から 60KB がコード、続く 4KB が非セキュア側からの呼び出し口 (NSC)。
 *   RAM は SRAM3 の上位 32KB。オプションバイトの SECWM1 (セクタ 0-7) と合わせる。
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x0C000000, LENGTH = 60K
    NSC   (rx)  : ORIGIN = 0x0C00F000, LENGTH = 4K
    RAM   (rwx) : ORIGIN = 0x3003C000, LENGTH = 32K
}

ENTRY(Reset_Handler)

_stack_size = 8K;	/* 起動時だけ使う。タスクのセキュアスタックは sec_task.c */
_estack = ORIGIN(RAM) + LENGTH(RAM);
_sstack = _estack - _stack_size;

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } >FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text) *(.text*) *(.rodata) *(.rodata*)
        *(.glue_7) *(.glue_7t) *(.eh_frame)
        . = ALIGN(4);
    } >FLASH

    .ARM.exidx : { *(.ARM.exidx*) } >FLASH

    /* 非セキュア側から呼べる入口 (SG 命令の veneer) */
    .gnu.sgstubs : ALIGN(32)
    {
        *(.gnu.sgstubs*)
        . = ALIGN(32);
    } >NSC

    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data) *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss) *(.bss*) *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >RAM

    PROVIDE(end = _ebss);
    PROVIDE(_end = _ebss);
    ASSERT(_ebss <= _sstack, "secure RAM overflow")
}
