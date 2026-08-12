/*
 * rtos.h — public API for the CortexRTOS kernel.
 *
 * Include this one header from application code.  Internal kernel headers
 * (scheduler.h, pool.h, etc.) are not part of the public API.
 */

#ifndef RTOS_H
#define RTOS_H

#include "rtos_types.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Kernel lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * rtos_init — must be the first call before creating any task or object.
 * Initialises the scheduler, memory pool and idle task.
 */
void rtos_init(void);

/*
 * rtos_start — hand control to the kernel; never returns.
 * Calls the port-specific function that loads the first task context and
 * enables the SysTick interrupt.
 */
void rtos_start(void);

/*
 * rtos_tick — called from SysTick_Handler every tick period.
 * Advances the tick counter, unblocks delayed tasks and requests a
 * context switch if a higher-priority task is now ready.
 */
void rtos_tick(void);

/* Current tick count (safe to read from tasks; wraps at UINT32_MAX) */
rtos_tick_t rtos_tick_count(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * Task management
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * rtos_task_create — create a new task.
 *
 * stack_buf:   caller-provided static buffer (size >= stack_bytes).
 *              Pass NULL to use default stack from the kernel heap.
 * stack_bytes: size of the stack buffer in bytes.
 * priority:    0 = highest, RTOS_PRIORITY_LEVELS-1 = lowest.
 *
 * Returns a pointer to the TCB on success, NULL on failure.
 */
rtos_task_t *rtos_task_create(
    const char    *name,
    rtos_task_fn_t fn,
    void          *arg,
    uint32_t      *stack_buf,
    size_t         stack_bytes,
    uint8_t        priority
);

/* Voluntarily yield to another ready task of equal or higher priority */
void rtos_yield(void);

/* Block the calling task for at least `ticks` tick periods */
void rtos_delay(rtos_tick_t ticks);

/* Suspend / resume a task by TCB pointer */
void rtos_task_suspend(rtos_task_t *task);
void rtos_task_resume(rtos_task_t *task);

/* Pointer to the TCB of the currently running task */
rtos_task_t *rtos_current_task(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * Mutex
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Binary mutex with priority inheritance.
 * A mutex MUST be released by the same task that acquired it.
 */
void      rtos_mutex_init(rtos_mutex_t *m);
rtos_err_t rtos_mutex_lock(rtos_mutex_t *m, rtos_tick_t timeout_ticks);
void      rtos_mutex_unlock(rtos_mutex_t *m);

/* ═══════════════════════════════════════════════════════════════════════════
 * Counting semaphore
 * ═══════════════════════════════════════════════════════════════════════════ */

void      rtos_sem_init(rtos_sem_t *s, uint32_t initial, uint32_t max_count);
rtos_err_t rtos_sem_wait(rtos_sem_t *s, rtos_tick_t timeout_ticks);
void      rtos_sem_signal(rtos_sem_t *s);
/* Signal from an ISR — does not yield, only marks a task ready */
void      rtos_sem_signal_isr(rtos_sem_t *s);

/* ═══════════════════════════════════════════════════════════════════════════
 * Fixed-size memory pool
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * rtos_pool_init — initialise a pool over a caller-provided buffer.
 *
 * buf:        raw memory buffer
 * buf_bytes:  total size of buf in bytes
 * block_size: size of each allocation block in bytes (will be aligned up)
 */
void    rtos_pool_init(rtos_pool_t *pool, void *buf, size_t buf_bytes, size_t block_size);
void   *rtos_pool_alloc(rtos_pool_t *pool);
void    rtos_pool_free(rtos_pool_t *pool, void *block);
size_t  rtos_pool_free_count(const rtos_pool_t *pool);

/* ═══════════════════════════════════════════════════════════════════════════
 * Critical section (disable / restore interrupts)
 * ═══════════════════════════════════════════════════════════════════════════ */

uint32_t rtos_enter_critical(void);
void     rtos_exit_critical(uint32_t saved_primask);

#endif /* RTOS_H */
