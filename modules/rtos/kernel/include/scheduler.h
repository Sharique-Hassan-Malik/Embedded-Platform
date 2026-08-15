/*
 * scheduler.h — internal scheduler API (not part of the public RTOS API).
 *
 * The scheduler maintains one ready queue per priority level using a
 * singly-linked circular list.  A 32-bit bitmask (`_ready_mask`) records
 * which priority levels have at least one ready task, allowing O(1) highest-
 * priority selection via CLZ (Count Leading Zeros) on Cortex-M.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "rtos_types.h"

/* Initialise the scheduler data structures; called once by rtos_init(). */
void sched_init(void);

/* Add a task to the tail of its priority level's ready queue. */
void sched_ready(rtos_task_t *task);

/* Remove a task from the ready queue it is currently in. */
void sched_unready(rtos_task_t *task);

/*
 * sched_elect — return the TCB of the highest-priority ready task.
 * Ties within a priority level are resolved round-robin.
 * Must be called with interrupts disabled.
 */
rtos_task_t *sched_elect(void);

/*
 * sched_yield_needed — request a PendSV context switch.
 * Safe to call from both task context and ISR context.
 */
void sched_yield_needed(void);

/* Pointers to the running and next task, read by the port assembly. */
extern rtos_task_t *volatile _current_task;
extern rtos_task_t *volatile _next_task;

/* Ready-queue bitmask: bit N set means priority-N level is non-empty. */
extern uint32_t _ready_mask;

/* Current tick counter */
extern volatile rtos_tick_t  _tick_count;

/* Delay list — tasks blocked by rtos_delay() */
extern rtos_task_t *_delay_list;

/* Static task table */
extern rtos_task_t _task_table[RTOS_MAX_TASKS];
extern uint8_t     _task_count;

#endif /* SCHEDULER_H */
