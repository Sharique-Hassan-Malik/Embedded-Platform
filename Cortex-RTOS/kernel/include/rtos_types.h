/*
 * rtos_types.h — fundamental types shared across all kernel components.
 *
 * The Task Control Block (TCB) is the central data structure.  Its first
 * field MUST be `sp` (saved stack pointer) because the context-switch
 * assembly reads it at offset 0 without knowing the full struct layout.
 */

#ifndef RTOS_TYPES_H
#define RTOS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rtos_config.h"

/* ── Opaque handle types ────────────────────────────────────────────────────── */
typedef struct rtos_task   rtos_task_t;
typedef struct rtos_mutex  rtos_mutex_t;
typedef struct rtos_sem    rtos_sem_t;
typedef struct rtos_pool   rtos_pool_t;

/* ── Tick counter ───────────────────────────────────────────────────────────── */
typedef uint32_t rtos_tick_t;

/* ── Task function signature ────────────────────────────────────────────────── */
typedef void (*rtos_task_fn_t)(void *arg);

/* ── Task states ────────────────────────────────────────────────────────────── */
typedef enum {
    TASK_READY    = 0,   /* in the run queue, eligible to run */
    TASK_RUNNING  = 1,   /* currently on the CPU              */
    TASK_BLOCKED  = 2,   /* waiting on mutex, semaphore or delay */
    TASK_SUSPENDED= 3,   /* explicitly suspended               */
    TASK_DEAD     = 4,   /* returned from task function        */
} rtos_task_state_t;

/* ── Block reason (used when state == TASK_BLOCKED) ──────────────────────────── */
typedef enum {
    BLOCK_NONE    = 0,
    BLOCK_DELAY   = 1,
    BLOCK_MUTEX   = 2,
    BLOCK_SEM     = 3,
} rtos_block_reason_t;

/*
 * Task Control Block.
 *
 * Layout rules:
 *   • `sp` must be the very first field (offset 0).
 *   • `stack_base` points to the lowest address of the stack buffer;
 *     `stack_top`  points one word past the highest address.
 *   • The TCB is always allocated from the kernel's static task table,
 *     never from the heap, so its address is stable forever.
 */
struct rtos_task {
    uint32_t           *sp;             /* saved stack pointer — MUST BE FIRST */

    const char         *name;
    uint8_t             priority;       /* 0 = highest */
    rtos_task_state_t   state;
    rtos_block_reason_t block_reason;

    rtos_tick_t         wake_tick;      /* tick at which a delayed task wakes  */

    uint32_t            *stack_base;    /* lowest stack address (stack grows ↓)*/
    uint32_t            *stack_top;     /* highest stack address + 1           */
    size_t               stack_bytes;

    /* Intrusive linked-list link — used by scheduler run queues and
     * mutex/semaphore wait lists. */
    rtos_task_t        *next;

    /* Priority-inheritance: original priority before elevation */
    uint8_t             base_priority;

    /* Run-time statistics */
    uint32_t            run_count;      /* number of times scheduled */
    uint32_t            total_ticks;    /* accumulated ticks on CPU  */
};

/* ── Return codes ────────────────────────────────────────────────────────────── */
typedef enum {
    RTOS_OK       =  0,
    RTOS_TIMEOUT  = -1,
    RTOS_ERROR    = -2,
    RTOS_FULL     = -3,
    RTOS_EMPTY    = -4,
} rtos_err_t;

/* Convenience: RTOS_WAIT_FOREVER passed as timeout_ticks */
#define RTOS_WAIT_FOREVER  ((rtos_tick_t)0xFFFFFFFFUL)
#define RTOS_NO_WAIT       ((rtos_tick_t)0)

#endif /* RTOS_TYPES_H */
