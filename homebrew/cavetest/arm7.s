@ ARM7 binary for cavetest, loaded at 0x037F8000.
@ Enables the VBlank IRQ: melonDS runs Action Replay codes when the ARM7
@ takes a VBlank interrupt, so the cheat that allocates memory needs this.
    .arm
    .global _start
_start:
    msr     cpsr_c, #0xD2           @ IRQ mode
    ldr     sp, =0x0380FF80
    msr     cpsr_c, #0xDF           @ system mode, IRQs still masked
    ldr     sp, =0x0380FD80

    ldr     r0, =0x0380FFFC         @ BIOS jumps to the handler stored here
    ldr     r1, =irq_handler
    str     r1, [r0]

    mov     r0, #0x04000000
    ldrh    r1, [r0, #4]            @ DISPSTAT: VBlank IRQ enable
    orr     r1, r1, #0x8
    strh    r1, [r0, #4]
    add     r2, r0, #0x200
    mov     r1, #1
    str     r1, [r2, #0x10]         @ IE = VBlank
    mvn     r1, #0
    str     r1, [r2, #0x14]         @ clear IF
    mov     r1, #1
    str     r1, [r2, #0x08]         @ IME = 1

    msr     cpsr_c, #0x5F           @ system mode, IRQ enabled
1:  b       1b

irq_handler:
    mov     r0, #0x04000000
    add     r0, r0, #0x200
    ldr     r1, [r0, #0x14]         @ acknowledge everything pending
    str     r1, [r0, #0x14]
    ldr     r0, =0x0380FFF8         @ BIOS IntrWait flags
    ldr     r2, [r0]
    orr     r2, r2, r1
    str     r2, [r0]
    bx      lr
    .ltorg
