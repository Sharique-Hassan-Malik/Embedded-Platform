#include "rtos.h"
#include "scheduler.h"

#include <string.h>
#include <stddef.h>

/* ── Idle task ───────────────────────────────────────────────────────────────── */

#define IDLE_STACK_WORDS 64
static uint32_t _idle_stack[IDLE_STACK_WORDS];

static void _idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* WFI: stop the core clock until the next interrupt (saves power). */
#ifdef CORTEX_M
        __asm volatile ("wfi" ::: "memory");
#endif
    }
}

/* ── Blocked-task list (delay queue) ────────────────────────────────────────── */
/*
 * A singly-linked list of tasks blocked by rtos_delay().
 * The list is unsorted; the tick handler scans the whole list each tick.
 * For RTOS_MAX_TASKS ≤ 32 this is negligible overhead.
 */
rtos_task_t *_delay_list = NULL;

/* ── Port stack initialisation (declared in port/cortex-m4/port.c) ─────────── */
extern uint32_t *port_stack_init(uint32_t *stack_top, rtos_task_fn_t fn, void *arg);

/* ── Heap for default stacks (simple bump allocator from a static buffer) ───── */
static uint8_t  _heap[RTOS_HEAP_BYTES] __attribute__((aligned(8)));
static size_t   _heap_used = 0;

static void *_heap_alloc(size_t bytes)
{
    /* Round up to 8-byte alignment */
    bytes = (bytes + 7u) & ~7u;
    if (_heap_used + bytes > RTOS_HEAP_BYTES) return NULL;
    void *p = &_heap[_heap_used];
    _heap_used += bytes;
    return p;
}

/* ── rtos_init ───────────────────────────────────────────────────────────────── */

void rtos_init(void)
{
    sched_init();

    /* Create the idle task — it must succeed, so we assert on failure */
    rtos_task_t *idle = rtos_task_create(
        "idle", _idle_task, NULL,
        _idle_stack, sizeof(_idle_stack),
        RTOS_IDLE_PRIORITY
    );
    (void)idle;   /* idle is always task 0 */
}

/* ── rtos_start ──────────────────────────────────────────────────────────────── */

/* Declared in port/cortex-m4/port.c */
extern void port_start_first_task(void);

void rtos_start(void)
{
    uint32_t primask = rtos_enter_critical();
    _next_task = sched_elect();
    rtos_exit_critical(primask);

    port_start_first_task();   /* never returns */
}

/* ── rtos_tick ───────────────────────────────────────────────────────────────── */

void rtos_tick(void)
{
    uint32_t primask = rtos_enter_critical();
    _tick_count++;

    /* Scan the delay list and wake tasks whose deadline has arrived */
    rtos_task_t *prev = NULL;
    rtos_task_t *t    = _delay_list;
    bool yield_needed = false;

    while (t != NULL) {
        rtos_task_t *next = t->next;
        if ((rtos_tick_t)(_tick_count - t->wake_tick) < 0x80000000UL) {
            /* Deadline reached or passed */
            if (prev == NULL) _delay_list = next;
            else              prev->next  = next;
            t->next         = NULL;
            t->block_reason = BLOCK_NONE;
            sched_ready(t);
            if (t->priority < _current_task->priority) yield_needed = true;
        } else {
            prev = t;
        }
        t = next;
    }

    /* Round-robin tick: if any task at the same priority is ready, rotate */
    if (!yield_needed && _ready_mask & (1u << _current_task->priority)) {
        yield_needed = true;
    }

    rtos_exit_critical(primask);

    if (yield_needed) sched_yield_needed();
}

/* ── rtos_tick_count ─────────────────────────────────────────────────────────── */

rtos_tick_t rtos_tick_count(void)
{
    return _tick_count;
}

/* ── Task creation ───────────────────────────────────────────────────────────── */

rtos_task_t *rtos_task_create(
    const char    *name,
    rtos_task_fn_t fn,
    void          *arg,
    uint32_t      *stack_buf,
    size_t         stack_bytes,
    uint8_t        priority
) {
    uint32_t primask = rtos_enter_critical();

    if (_task_count >= RTOS_MAX_TASKS || priority >= RTOS_PRIORITY_LEVELS) {
        rtos_exit_critical(primask);
        return NULL;
    }

    rtos_task_t *task = &_task_table[_task_count++];
    memset(task, 0, sizeof(*task));

    /* Allocate stack from heap if caller didn't supply one */
    if (stack_buf == NULL) {
        if (stack_bytes == 0) stack_bytes = RTOS_DEFAULT_STACK_BYTES;
        stack_buf = (uint32_t *)_heap_alloc(stack_bytes);
        if (stack_buf == NULL) {
            _task_count--;
            rtos_exit_critical(primask);
            return NULL;
        }
    }

    task->name          = name;
    task->priority      = priority;
    task->base_priority = priority;
    task->state         = TASK_READY;
    task->block_reason  = BLOCK_NONE;
    task->stack_base    = stack_buf;
    task->stack_top     = stack_buf + (stack_bytes / sizeof(uint32_t));
    task->stack_bytes   = stack_bytes;

    /* Initialise the stack frame so the first context restore works */
    uint32_t *top = task->stack_top;
    task->sp = port_stack_init(top, fn, arg);

    sched_ready(task);

    rtos_exit_critical(primask);
    return task;
}

/* ── rtos_yield ──────────────────────────────────────────────────────────────── */

void rtos_yield(void)
{
    sched_yield_needed();
}

/* ── rtos_delay ──────────────────────────────────────────────────────────────── */

void rtos_delay(rtos_tick_t ticks)
{
    if (ticks == 0) return;

    uint32_t primask = rtos_enter_critical();

    rtos_task_t *self     = _current_task;
    self->state           = TASK_BLOCKED;
    self->block_reason    = BLOCK_DELAY;
    self->wake_tick       = _tick_count + ticks;
    self->next            = NULL;

    sched_unready(self);

    /* Insert into the delay list (prepend for simplicity) */
    self->next  = _delay_list;
    _delay_list = self;

    _next_task = sched_elect();

    rtos_exit_critical(primask);
    sched_yield_needed();

    /* Execution resumes here after the task is woken */
}

/* ── rtos_task_suspend / resume ──────────────────────────────────────────────── */

void rtos_task_suspend(rtos_task_t *task)
{
    uint32_t primask = rtos_enter_critical();
    if (task->state == TASK_READY || task->state == TASK_RUNNING) {
        sched_unready(task);
        task->state = TASK_SUSPENDED;
    }
    rtos_exit_critical(primask);
    if (task == _current_task) sched_yield_needed();
}

void rtos_task_resume(rtos_task_t *task)
{
    uint32_t primask = rtos_enter_critical();
    if (task->state == TASK_SUSPENDED) {
        sched_ready(task);
        if (task->priority < _current_task->priority) {
            _next_task = task;
            rtos_exit_critical(primask);
            sched_yield_needed();
            return;
        }
    }
    rtos_exit_critical(primask);
}

/* ── rtos_current_task ───────────────────────────────────────────────────────── */

rtos_task_t *rtos_current_task(void)
{
    return _current_task;
}

/* ── Critical section ────────────────────────────────────────────────────────── */

uint32_t rtos_enter_critical(void)
{
    uint32_t primask;
#ifdef CORTEX_M
    __asm volatile (
        "mrs %0, PRIMASK\n"
        "cpsid i\n"
        : "=r"(primask) :: "memory"
    );
#else
    primask = 0;
#endif
    return primask;
}

void rtos_exit_critical(uint32_t saved_primask)
{
#ifdef CORTEX_M
    __asm volatile (
        "msr PRIMASK, %0\n"
        :: "r"(saved_primask) : "memory"
    );
#else
    (void)saved_primask;
#endif
}
