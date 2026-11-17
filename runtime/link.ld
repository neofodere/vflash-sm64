ENTRY(_start)

SECTIONS
{
	. = 0x10500000;
	.boot : { *(.boot) }

	. = 0x80500100;
	.romtab : AT(ADDR(.romtab) - 0x70000000) { KEEP(*(.romtab)) }

	. = 0x80501000;
	.text : AT(ADDR(.text) - 0x70000000) { *(.text .text.*) }
	.rodata : AT(ADDR(.rodata) - 0x70000000) { *(.rodata .rodata.*) }
	.data : AT(ADDR(.data) - 0x70000000) { *(.data .data.*) }
	__image_end = LOADADDR(.data) + SIZEOF(.data);
	__image_size = __image_end - 0x10500000;

	. = 0x80200000;
	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss .bss.* COMMON)
		. = ALIGN(16);
		__bss_end = .;
	}
	_engineSegmentStart = __bss_end;
	_framebuffersSegmentNoloadStart = __bss_end;
	_framebuffersSegmentNoloadEnd = __bss_end;

	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.ARM.attributes) *(.comment) *(.note*) }
}
