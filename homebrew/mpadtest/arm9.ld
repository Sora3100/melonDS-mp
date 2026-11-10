OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
    ram : ORIGIN = 0x02000000, LENGTH = 3M
}

SECTIONS
{
    .text : { *(.text.start) *(.text*) } > ram
    .rodata : { *(.rodata*) } > ram
    .data : { *(.data*) } > ram
    . = ALIGN(4);
    .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > ram
    /DISCARD/ : { *(.text.arm7) *(.ARM.attributes) *(.comment) }
    __stack_top = 0x02300000;
}
