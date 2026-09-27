ENTRY(Reset_Handler)

MEMORY {
	ROM (rx)	: ORIGIN = 0x10000000, LENGTH = 2048K
	RAM (xrw)	: ORIGIN = 0x20000000, LENGTH = 256K
}

SECTIONS {
	boot2 : {
		__boot2_start__ = .;
		KEEP (*(.boot2))
		__boot2_end__ = .;
	} > ROM
	ASSERT(__boot2_end__ - __boot2_start__ == 256,
		"ERROR: Pico second stage bootloader must be 256 bytes in size")

	.text : {
		__vector_org = .;
		KEEP (*(.vector))

		*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)

		__start = .;
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata.*)
	} >ROM

	.ARM.extab :
	{
		*(.ARM.extab* .gnu.linkonce.armextab.*)
	} > ROM

	__exidx_start = .;
	.ARM.exidx :
	{
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
	} > ROM
	__exidx_end = .;

	__data_org = .;
	/* wasm 永続化領域 (0x1DF000-0x1FEFFF, 128KB) と FLASH_STORAGE (0x1FF000-0x1FFFFF)
	 * に firmware .text が侵食していないか検査。.data は RAM へ展開されるので
	 * ROM 実サイズは __data_org がそのまま firmware end となる */
	ASSERT(__data_org <= 0x101DF000,
		"ERROR: firmware .text exceeds 1916KB; overlaps wasm Flash slot region @ 0x1DF000")
	vector 0x20000000 (NOLOAD) : {
		__vector_start = .;
		*(.data_vector)
		__vector_end = .;
	} >RAM
	.data : AT(__data_org) {
		__data_start = .;
		*(.data)
		*(.data.*)
		. = ALIGN(4);
		__data_end = .;
	} >RAM
	.bss ALIGN(4) (NOLOAD) : {
		__bss_start = .;
		PROVIDE (__noinit_start = .);
		*(.noinit)
		PROVIDE (__noinit_end = .);
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(4);
		__bss_end = .;
		__end = .;
		end = .;     /* Required by newlib _sbrk */
	} >RAM
}
