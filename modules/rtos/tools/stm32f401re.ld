/*
 * stm32f401re.ld — linker script for STM32F401RE (512K flash, 96K SRAM).
 *
 * Memory map (from RM0368 Table 1):
 *   Flash:  0x08000000 – 0x0807FFFF  (512 KB)
 *   SRAM:   0x20000000 – 0x20017FFF  ( 96 KB)
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH =  96K
}

ENTRY(Reset_Handler)

SECTIONS
{
    /* Interrupt vector table — must be at the start of flash */
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    /* Read-only code and data */
    .text :
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Initialised data — stored in flash, copied to SRAM at startup */
    _sidata = LOADADDR(.data);
    .data :
    {
        _sdata = .;
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    /* Zero-initialised data */
    .bss :
    {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    /* newlib's _sbrk grows the heap from `end`. Without this symbol the link
     * fails with "undefined reference to `end'" out of libnosys, which reads
     * as a missing library rather than a missing linker-script definition.
     *
     * This RTOS has its own pool allocator and never calls malloc, but linking
     * against -lc pulls _sbrk in regardless, so the symbol has to exist. */
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    PROVIDE(__heap_start = .);

    /* Stack at the top of SRAM (grows downward) */
    _estack = ORIGIN(SRAM) + LENGTH(SRAM);
    PROVIDE(__heap_end = _estack - 0x400);
}
