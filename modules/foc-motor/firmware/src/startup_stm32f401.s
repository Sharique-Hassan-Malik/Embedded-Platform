/*
 * startup_stm32f401.s - startup file for STM32F401 (FOC motor controller).
 *
 * Provides the Cortex-M4 vector table (system exceptions + STM32F401
 * peripheral IRQs), a Reset_Handler that initialises .data/.bss and calls
 * main(), and weak Default_Handler aliases for every interrupt. The strong
 * ADC/TIM2/USART2/DMA1_Stream7 handlers in hal.c override the weak defaults.
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

/* -- Vector table ---------------------------------------------------------- */
    .section .isr_vector, "a", %progbits
    .type _vectors, %object
    .global _vectors
_vectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word DebugMon_Handler
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    .word Default_Handler    /* IRQ 0 */
    .word Default_Handler    /* IRQ 1 */
    .word Default_Handler    /* IRQ 2 */
    .word Default_Handler    /* IRQ 3 */
    .word Default_Handler    /* IRQ 4 */
    .word Default_Handler    /* IRQ 5 */
    .word Default_Handler    /* IRQ 6 */
    .word Default_Handler    /* IRQ 7 */
    .word Default_Handler    /* IRQ 8 */
    .word Default_Handler    /* IRQ 9 */
    .word Default_Handler    /* IRQ 10 */
    .word Default_Handler    /* IRQ 11 */
    .word Default_Handler    /* IRQ 12 */
    .word Default_Handler    /* IRQ 13 */
    .word Default_Handler    /* IRQ 14 */
    .word Default_Handler    /* IRQ 15 */
    .word Default_Handler    /* IRQ 16 */
    .word Default_Handler    /* IRQ 17 */
    .word ADC_IRQHandler    /* IRQ 18 */
    .word Default_Handler    /* IRQ 19 */
    .word Default_Handler    /* IRQ 20 */
    .word Default_Handler    /* IRQ 21 */
    .word Default_Handler    /* IRQ 22 */
    .word Default_Handler    /* IRQ 23 */
    .word Default_Handler    /* IRQ 24 */
    .word Default_Handler    /* IRQ 25 */
    .word Default_Handler    /* IRQ 26 */
    .word Default_Handler    /* IRQ 27 */
    .word TIM2_IRQHandler    /* IRQ 28 */
    .word Default_Handler    /* IRQ 29 */
    .word Default_Handler    /* IRQ 30 */
    .word Default_Handler    /* IRQ 31 */
    .word Default_Handler    /* IRQ 32 */
    .word Default_Handler    /* IRQ 33 */
    .word Default_Handler    /* IRQ 34 */
    .word Default_Handler    /* IRQ 35 */
    .word Default_Handler    /* IRQ 36 */
    .word Default_Handler    /* IRQ 37 */
    .word USART2_IRQHandler    /* IRQ 38 */
    .word Default_Handler    /* IRQ 39 */
    .word Default_Handler    /* IRQ 40 */
    .word Default_Handler    /* IRQ 41 */
    .word Default_Handler    /* IRQ 42 */
    .word Default_Handler    /* IRQ 43 */
    .word Default_Handler    /* IRQ 44 */
    .word Default_Handler    /* IRQ 45 */
    .word Default_Handler    /* IRQ 46 */
    .word DMA1_Stream7_IRQHandler    /* IRQ 47 */
    .word Default_Handler    /* IRQ 48 */
    .word Default_Handler    /* IRQ 49 */
    .word Default_Handler    /* IRQ 50 */
    .word Default_Handler    /* IRQ 51 */
    .word Default_Handler    /* IRQ 52 */
    .word Default_Handler    /* IRQ 53 */
    .word Default_Handler    /* IRQ 54 */
    .word Default_Handler    /* IRQ 55 */
    .word Default_Handler    /* IRQ 56 */
    .word Default_Handler    /* IRQ 57 */
    .word Default_Handler    /* IRQ 58 */
    .word Default_Handler    /* IRQ 59 */
    .word Default_Handler    /* IRQ 60 */
    .word Default_Handler    /* IRQ 61 */
    .word Default_Handler    /* IRQ 62 */
    .word Default_Handler    /* IRQ 63 */
    .word Default_Handler    /* IRQ 64 */
    .word Default_Handler    /* IRQ 65 */
    .word Default_Handler    /* IRQ 66 */
    .word Default_Handler    /* IRQ 67 */
    .word Default_Handler    /* IRQ 68 */
    .word Default_Handler    /* IRQ 69 */
    .word Default_Handler    /* IRQ 70 */
    .word Default_Handler    /* IRQ 71 */
    .word Default_Handler    /* IRQ 72 */
    .word Default_Handler    /* IRQ 73 */
    .word Default_Handler    /* IRQ 74 */
    .word Default_Handler    /* IRQ 75 */
    .word Default_Handler    /* IRQ 76 */
    .word Default_Handler    /* IRQ 77 */
    .word Default_Handler    /* IRQ 78 */
    .word Default_Handler    /* IRQ 79 */
    .word Default_Handler    /* IRQ 80 */
    .word Default_Handler    /* IRQ 81 */
    .word Default_Handler    /* IRQ 82 */
    .word Default_Handler    /* IRQ 83 */
    .word Default_Handler    /* IRQ 84 */

/* -- Reset handler --------------------------------------------------------- */
    .section .text.Reset_Handler
    .type Reset_Handler, %function
    .global Reset_Handler
Reset_Handler:
    /* Copy .data from flash (_sidata) to SRAM (_sdata.._edata) */
    ldr  r0, =_sidata
    ldr  r1, =_sdata
    ldr  r2, =_edata
1:
    cmp  r1, r2
    ittt lo
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo  1b
    /* Zero .bss (_sbss.._ebss) */
    ldr  r0, =_sbss
    ldr  r1, =_ebss
    movs r2, #0
2:
    cmp  r0, r1
    it   lo
    strlo r2, [r0], #4
    blo  2b
    /* Call main() */
    bl   main
3:  b 3b   /* trap if main returns */

/* -- Default handler ------------------------------------------------------- */
    .section .text.Default_Handler
    .type Default_Handler, %function
    .global Default_Handler
Default_Handler:
    b .

/* Weak aliases: any handler not defined in C falls back to Default_Handler. */
    .weak NMI_Handler
    .thumb_set NMI_Handler, Default_Handler
    .weak HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler
    .weak MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler
    .weak BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler
    .weak UsageFault_Handler
    .thumb_set UsageFault_Handler, Default_Handler
    .weak SVC_Handler
    .thumb_set SVC_Handler, Default_Handler
    .weak DebugMon_Handler
    .thumb_set DebugMon_Handler, Default_Handler
    .weak PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler
    .weak SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler
    .weak ADC_IRQHandler
    .thumb_set ADC_IRQHandler, Default_Handler
    .weak DMA1_Stream7_IRQHandler
    .thumb_set DMA1_Stream7_IRQHandler, Default_Handler
    .weak TIM2_IRQHandler
    .thumb_set TIM2_IRQHandler, Default_Handler
    .weak USART2_IRQHandler
    .thumb_set USART2_IRQHandler, Default_Handler
