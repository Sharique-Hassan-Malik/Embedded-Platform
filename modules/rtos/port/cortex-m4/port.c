/*
 * port.c — Cortex-M4 C-side port glue.
 *
 * port_stack_init builds the initial stack frame for a new task so that the
 * first context restore (in PendSV_Handler or port_start_first_task) lands
 * correctly in the task function.
 *
 * Initial stack layout (addresses decrease downward):
 *
 *   stack_top - 1  : xPSR  = 0x01000000  (Thumb bit set, no FP)
 *   stack_top - 2  : PC    = fn           (task entry point, LSB cleared)
 *   stack_top - 3  : LR    = _task_exit_hook (called if task returns)
 *   stack_top - 4  : R12   = 0
 *   stack_top - 5  : R3    = 0
 *   stack_top - 6  : R2    = 0
 *   stack_top - 7  : R1    = 0
 *   stack_top - 8  : R0    = arg          (first argument to task fn)
 *   stack_top - 9  : R11   = 0            ┐
 *   stack_top - 10 : R10   = 0            │
 *   stack_top - 11 : R9    = 0            │  sw-saved callee regs
 *   stack_top - 12 : R8    = 0            │
 *   stack_top - 13 : R7    = 0            │
 *   stack_top - 14 : R6    = 0            │
 *   stack_top - 15 : R5    = 0            │
 *   stack_top - 16 : R4    = 0            ┘  ← initial SP stored in TCB
 */

#include "rtos_types.h"

/* Called if a task function returns instead of looping forever.
 * Marks the task as dead and yields.  The idle task will continue. */
static void _task_exit_hook(void)
{
    /* Declared before use: C needs the extern above the first reference, and
     * with it below, this file simply does not compile. */
    extern rtos_task_t *volatile _current_task;

    rtos_task_t *self = _current_task;
    self->state = TASK_DEAD;
    for (;;) {
        extern void sched_yield_needed(void);
        sched_yield_needed();
    }
}

/* Forward declaration — _current_task is in scheduler.c */
extern rtos_task_t *volatile _current_task;

uint32_t *port_stack_init(uint32_t *stack_top, rtos_task_fn_t fn, void *arg)
{
    /* Cortex-M requires 8-byte aligned SP on exception entry */
    uint32_t *sp = stack_top;

    /* Hardware-saved exception frame (popped automatically by CPU) */
    *(--sp) = 0x01000000u;                     /* xPSR: Thumb bit set      */
    *(--sp) = (uint32_t)fn & ~1u;              /* PC: clear Thumb bit      */
    *(--sp) = (uint32_t)_task_exit_hook | 1u;  /* LR: Thumb return address */
    *(--sp) = 0u;                              /* R12                      */
    *(--sp) = 0u;                              /* R3                       */
    *(--sp) = 0u;                              /* R2                       */
    *(--sp) = 0u;                              /* R1                       */
    *(--sp) = (uint32_t)arg;                   /* R0: task argument        */

    /* Software-saved callee registers (R4–R11) */
    *(--sp) = 0u;  /* R11 */
    *(--sp) = 0u;  /* R10 */
    *(--sp) = 0u;  /* R9  */
    *(--sp) = 0u;  /* R8  */
    *(--sp) = 0u;  /* R7  */
    *(--sp) = 0u;  /* R6  */
    *(--sp) = 0u;  /* R5  */
    *(--sp) = 0u;  /* R4  */

    return sp;
}
