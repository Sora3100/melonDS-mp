@ ARM9 startup for the bare-metal 8-pad test ROM
    .section .text.start, "ax"
    .arm
    .global _start
_start:
    mrs     r0, cpsr
    orr     r0, r0, #0xC0           @ IRQ/FIQ off
    msr     cpsr_c, r0
    ldr     sp, =__stack_top

    @ clear .bss
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b

    bl      main
2:  b       2b
