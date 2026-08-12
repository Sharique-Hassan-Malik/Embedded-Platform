/*
 * host_stubs.c — native stubs for the two port functions that have no
 * #ifdef CORTEX_M fallback in the kernel C sources.
 *
 * rtos_enter_critical, rtos_exit_critical and sched_yield_needed are
 * already defined (with #ifdef guards) in kernel.c and scheduler.c,
 * so they are NOT repeated here.
 *
 * Only port_stack_init and port_start_first_task live exclusively in
 * the Cortex-M assembly/port.c files, so we stub them here.
 */

#include <stdint.h>
#include "rtos_types.h"
#include "scheduler.h"

/* port_stack_init: return a dummy SP so task creation succeeds. */
uint32_t *port_stack_init(uint32_t *stack_top, rtos_task_fn_t fn, void *arg)
{
    (void)fn; (void)arg;
    return stack_top - 16;
}

/* port_start_first_task: not called in unit tests. */
void port_start_first_task(void) {}

/* ── Test driver helpers ─────────────────────────────────────────────────── */

/*
 * _drive_scheduler — manually run one scheduling decision.
 * Call after any kernel operation that would normally trigger PendSV.
 */
void _drive_scheduler(void)
{
    rtos_task_t *next = sched_elect();
    if (next != NULL) {
        if (_current_task && _current_task->state == TASK_RUNNING) {
            _current_task->state = TASK_READY;
        }
        _current_task = next;
        _current_task->state = TASK_RUNNING;
    }
}

/*
 * _advance_ticks — simulate elapsed time by calling rtos_tick N times.
 */
void _advance_ticks(uint32_t n)
{
    extern void rtos_tick(void);
    for (uint32_t i = 0; i < n; i++) {
        rtos_tick();
        _drive_scheduler();
    }
}
