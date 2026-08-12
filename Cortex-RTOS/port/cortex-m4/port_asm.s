/*
 * port_asm.s — Cortex-M4 context switch (ARM Thumb-2).
 *
 * Two entry points:
 *
 *   PendSV_Handler   — preemptive context switch, triggered by setting
 *                      PENDSVSET in SCB->ICSR.  Runs at the lowest
 *                      exception priority so it fires after all other ISRs.
 *
 *   port_start_first_task — loads the first task's context without saving
 *                           anything (nothing to save on first entry).
 *
 * Cortex-M hardware automatically saves {r0-r3, r12, lr, pc, xpsr} on the
 * PSP when an exception is taken and restores them on return.  We save and
 * restore the remaining callee-saved registers {r4-r11} manually.
 *
 * Stack frame layout after PendSV saves context (grows downward):
 *
 *   High addr (old PSP)
 *     xpsr      ← hw-saved (exception entry)
 *     pc        (return address)
 *     lr        (EXC_RETURN or LR at exception entry)
 *     r12
 *     r3
 *     r2
 *     r1
 *     r0
 *     r11       ← sw-saved by PendSV_Handler
 *     r10
 *     r9
 *     r8
 *     r7
 *     r6
 *     r5
 *     r4        ← new PSP (stored in TCB->sp, which is at offset 0)
 *   Low addr
 *
 * TCB layout assumption: the very first field of rtos_task_t is `sp`
 * (a uint32_t*).  This file accesses it at offset 0 without including
 * the C header to avoid pulling in the entire kernel.
 *
 * Globals used:
 *   _current_task — pointer to the running task's TCB
 *   _next_task    — pointer to the task elected by the scheduler
 */

    .syntax unified
    .thumb

    .extern _current_task
    .extern _next_task

/* ── PendSV_Handler ──────────────────────────────────────────────────────────── */

    .section .text.PendSV_Handler
    .type PendSV_Handler, %function
    .global PendSV_Handler
    .thumb_func

PendSV_Handler:
    /* Disable interrupts for the critical section */
    cpsid   i

    /* Load _current_task and _next_task pointers */
    ldr     r2, =_current_task          /* r2 = &_current_task */
    ldr     r3, =_next_task             /* r3 = &_next_task    */
    ldr     r0, [r2]                    /* r0 = _current_task  */
    ldr     r1, [r3]                    /* r1 = _next_task     */

    /* If current == next, nothing to switch — early exit */
    cmp     r0, r1
    beq     .L_pendsv_exit

    /* Save current task context ───────────────────────────────────────────── */

    /* Read the current PSP */
    mrs     r12, psp

    /* Push callee-saved registers onto the process stack */
    stmdb   r12!, {r4-r11}

    /* Save the updated SP into TCB->sp (offset 0) */
    str     r12, [r0, #0]

    /* Update _current_task = _next_task */
    str     r1, [r2]

    /* Restore next task context ────────────────────────────────────────────── */

    /* Load new SP from TCB->sp */
    ldr     r12, [r1, #0]

    /* Pop callee-saved registers from the new process stack */
    ldmia   r12!, {r4-r11}

    /* Write new PSP */
    msr     psp, r12

.L_pendsv_exit:
    cpsie   i

    /*
     * Return using EXC_RETURN = 0xFFFFFFFD:
     *   • Return to Thread mode
     *   • Use PSP
     *   • No FP state (basic frame)
     * The hardware automatically pops {r0-r3, r12, lr, pc, xpsr} from PSP.
     */
    bx      lr

    .size PendSV_Handler, . - PendSV_Handler


/* ── port_start_first_task ───────────────────────────────────────────────────── */

    .section .text.port_start_first_task
    .type port_start_first_task, %function
    .global port_start_first_task
    .thumb_func

port_start_first_task:
    /*
     * Switch the CPU to use PSP and jump to the first task.
     * No context to save — this is the cold start.
     */

    /* Set PendSV to the lowest priority (0xFF) so it runs after all ISRs */
    ldr     r0, =0xE000ED22             /* SCB->SHPR3 byte for PendSV */
    mov     r1, #0xFF
    strb    r1, [r0]

    /* Configure SysTick */
    ldr     r0, =0xE000E010             /* SysTick base */
    ldr     r1, =SYSTICK_RELOAD
    str     r1, [r0, #4]               /* SysTick->LOAD */
    mov     r1, #0
    str     r1, [r0, #8]               /* SysTick->VAL = 0 (clear) */
    mov     r1, #0x7                   /* CLKSOURCE=1, TICKINT=1, ENABLE=1 */
    str     r1, [r0]                   /* SysTick->CTRL */

    /* Load _next_task (= first task elected by sched_elect) */
    ldr     r2, =_next_task
    ldr     r0, [r2]

    /* Update _current_task */
    ldr     r3, =_current_task
    str     r0, [r3]

    /* Load new PSP from TCB->sp */
    ldr     r1, [r0, #0]
    ldmia   r1!, {r4-r11}             /* pop sw-saved registers */
    msr     psp, r1

    /* Switch to PSP: set CONTROL.SPSEL = 1 */
    mov     r0, #2
    msr     CONTROL, r0
    isb

    /* Enable interrupts */
    cpsie   i

    /* Return to Thread mode using PSP.  Hardware will pop the initial frame. */
    bx      lr

    .size port_start_first_task, . - port_start_first_task


/* ── SysTick_Handler ─────────────────────────────────────────────────────────── */

    .section .text.SysTick_Handler
    .type SysTick_Handler, %function
    .global SysTick_Handler
    .thumb_func

SysTick_Handler:
    .extern rtos_tick
    push    {lr}
    bl      rtos_tick
    pop     {pc}

    .size SysTick_Handler, . - SysTick_Handler


/* ── Literal pool ────────────────────────────────────────────────────────────── */

    /* SYSTICK_RELOAD is defined by the build system:
     *   RTOS_CORE_CLOCK_HZ / RTOS_TICK_HZ - 1
     * Default: 84000000 / 1000 - 1 = 83999
     */
    .equ SYSTICK_RELOAD, 83999

    .end
