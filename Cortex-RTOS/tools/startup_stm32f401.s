/*
 * startup_stm32f401.s — minimal startup file for STM32F401.
 *
 * Provides:
 *   • Default exception/interrupt vector table in .isr_vector section.
 *   • Reset_Handler: copies .data from flash to SRAM, zeros .bss, calls main.
 *   • Weak default handlers for all Cortex-M system exceptions.
 *
 * The PendSV_Handler and SysTick_Handler are defined in port_asm.s and
 * will override these weak defaults at link time.
 */

    .syntax unified
    .thumb

    .extern _estack
    .extern _sidata
    .extern _sdata
    .extern _edata
    .extern _sbss
    .extern _ebss
    .extern main

/* ── Vector table ────────────────────────────────────────────────────────────── */

    .section .isr_vector, "a", %progbits
    .type _vectors, %object
    .global _vectors

_vectors:
    .word _estack                /* Initial MSP                    */
    .word Reset_Handler          /* Reset                          */
    .word NMI_Handler            /* NMI                            */
    .word HardFault_Handler      /* Hard Fault                     */
    .word MemManage_Handler      /* MPU Fault                      */
    .word BusFault_Handler       /* Bus Fault                      */
    .word UsageFault_Handler     /* Usage Fault                    */
    .word 0                      /* Reserved                       */
    .word 0                      /* Reserved                       */
    .word 0                      /* Reserved                       */
    .word 0                      /* Reserved                       */
    .word SVC_Handler            /* SVCall                         */
    .word DebugMon_Handler       /* Debug Monitor                  */
    .word 0                      /* Reserved                       */
    .word PendSV_Handler         /* PendSV — context switch        */
    .word SysTick_Handler        /* SysTick — RTOS tick            */
    /* External interrupts: all default to Default_Handler */
    .rept 85
    .word Default_Handler
    .endr

/* ── Reset_Handler ───────────────────────────────────────────────────────────── */

    .section .text.Reset_Handler
    .type Reset_Handler, %function
    .global Reset_Handler
    .thumb_func

Reset_Handler:
    /* Copy .data section from flash to SRAM */
    ldr  r0, =_sidata
    ldr  r1, =_sdata
    ldr  r2, =_edata
.copy_loop:
    cmp  r1, r2
    bge  .copy_done
    ldr  r3, [r0], #4
    str  r3, [r1], #4
    b    .copy_loop
.copy_done:

    /* Zero .bss section */
    ldr  r0, =_sbss
    ldr  r1, =_ebss
    mov  r2, #0
.zero_loop:
    cmp  r0, r1
    bge  .zero_done
    str  r2, [r0], #4
    b    .zero_loop
.zero_done:

    /* Jump to main */
    bl   main

    /* main should never return; trap here if it does */
.hang:
    b    .hang

    .size Reset_Handler, . - Reset_Handler

/* ── Default weak handlers ───────────────────────────────────────────────────── */

    .section .text.Default_Handler
    .type Default_Handler, %function
    .global Default_Handler
    .thumb_func
Default_Handler:
    bkpt #0
    b    Default_Handler
    .size Default_Handler, . - Default_Handler

    .macro weak_alias name
    .weak \name
    .thumb_set \name, Default_Handler
    .endm

    weak_alias NMI_Handler
    weak_alias HardFault_Handler
    weak_alias MemManage_Handler
    weak_alias BusFault_Handler
    weak_alias UsageFault_Handler
    weak_alias SVC_Handler
    weak_alias DebugMon_Handler
    weak_alias PendSV_Handler
    weak_alias SysTick_Handler

    .end
