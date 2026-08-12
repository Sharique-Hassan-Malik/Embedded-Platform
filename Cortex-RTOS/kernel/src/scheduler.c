#include "scheduler.h"
#include "rtos.h"

#include <string.h>

/* ── Globals read by port assembly ──────────────────────────────────────────── */
rtos_task_t *volatile _current_task = NULL;
rtos_task_t *volatile _next_task    = NULL;
volatile rtos_tick_t  _tick_count   = 0;

/* ── Static task pool ────────────────────────────────────────────────────────── */
rtos_task_t _task_table[RTOS_MAX_TASKS];
uint8_t     _task_count = 0;

/* ── Per-priority ready queues (circular singly-linked lists) ────────────────── */
/*
 * Each level stores a pointer to the *tail* of the circular list so that
 * both enqueue (at tail) and dequeue (from head = tail->next) are O(1).
 * NULL means the level is empty.
 */
static rtos_task_t *_ready_tail[RTOS_PRIORITY_LEVELS];

uint32_t _ready_mask = 0;

/* ── Internal helpers ────────────────────────────────────────────────────────── */

static inline void _mask_set(uint8_t prio)
{
    _ready_mask |= (1u << prio);
}

static inline void _mask_clear(uint8_t prio)
{
    _ready_mask &= ~(1u << prio);
}

/* ── Public API ──────────────────────────────────────────────────────────────── */

void sched_init(void)
{
    memset(_ready_tail, 0, sizeof(_ready_tail));
    _ready_mask  = 0;
    _task_count  = 0;
    _current_task = NULL;
    _next_task    = NULL;
    _tick_count   = 0;
}

void sched_ready(rtos_task_t *task)
{
    uint8_t prio = task->priority;
    task->state  = TASK_READY;

    rtos_task_t *tail = _ready_tail[prio];
    if (tail == NULL) {
        /* First task at this priority: self-referential circular list */
        task->next        = task;
        _ready_tail[prio] = task;
        _mask_set(prio);
    } else {
        /* Insert at tail (head = tail->next) */
        task->next        = tail->next;
        tail->next        = task;
        _ready_tail[prio] = task;
    }
}

void sched_unready(rtos_task_t *task)
{
    uint8_t      prio = task->priority;
    rtos_task_t *tail = _ready_tail[prio];

    if (tail == NULL) return;   /* already empty — nothing to remove */

    /* Walk the circular list to find the predecessor */
    rtos_task_t *prev = tail;
    rtos_task_t *cur  = tail->next;

    do {
        if (cur == task) {
            if (cur == cur->next) {
                /* Only element — empty the level */
                _ready_tail[prio] = NULL;
                _mask_clear(prio);
            } else {
                prev->next = cur->next;
                /* If we removed the tail, move tail back one */
                if (cur == tail) {
                    _ready_tail[prio] = prev;
                }
            }
            task->next = NULL;
            return;
        }
        prev = cur;
        cur  = cur->next;
    } while (cur != tail->next);
}

rtos_task_t *sched_elect(void)
{
    if (_ready_mask == 0) return NULL;

    /*
     * Lowest set bit = lowest priority index = highest-priority ready level.
     * __builtin_ctz compiles to RBIT+CLZ on Cortex-M4 (one instruction pair).
     */
    uint8_t prio = (uint8_t)__builtin_ctz(_ready_mask);

    rtos_task_t *tail = _ready_tail[prio];
    rtos_task_t *head = tail->next;      /* round-robin: head is next to run */

    /* Advance the tail pointer (rotate the list) to implement round-robin */
    _ready_tail[prio] = head;

    return head;
}

void sched_yield_needed(void)
{
    /*
     * Set PendSV pending.  PendSV fires at the lowest exception priority
     * after all other ISRs complete, making it safe to call from any ISR.
     */
#ifdef CORTEX_M
    /* SCB->ICSR: set PENDSVSET (bit 28) */
    *((volatile uint32_t *)0xE000ED04u) = (1u << 28);
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
#endif
}
