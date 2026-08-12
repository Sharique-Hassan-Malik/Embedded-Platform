/*
 * semaphore.c — counting semaphore.
 *
 * rtos_sem_signal_isr is safe to call from interrupt context:
 * it only marks a task ready and triggers PendSV; it does not
 * context-switch inline.
 */

#include "rtos.h"
#include "rtos_objects.h"
#include "scheduler.h"

void rtos_sem_init(rtos_sem_t *s, uint32_t initial, uint32_t max_count)
{
    s->count     = (initial <= max_count) ? initial : max_count;
    s->max_count = max_count;
    s->wait_head = NULL;
}

rtos_err_t rtos_sem_wait(rtos_sem_t *s, rtos_tick_t timeout_ticks)
{
    rtos_tick_t deadline = rtos_tick_count() + timeout_ticks;

    for (;;) {
        uint32_t primask = rtos_enter_critical();

        if (s->count > 0) {
            s->count--;
            rtos_exit_critical(primask);
            return RTOS_OK;
        }

        if (timeout_ticks == RTOS_NO_WAIT) {
            rtos_exit_critical(primask);
            return RTOS_TIMEOUT;
        }

        if (timeout_ticks != RTOS_WAIT_FOREVER &&
            (rtos_tick_t)(rtos_tick_count() - deadline) < 0x80000000UL) {
            rtos_exit_critical(primask);
            return RTOS_TIMEOUT;
        }

        /* Block and enqueue ordered by priority */
        rtos_task_t *self  = _current_task;
        self->state        = TASK_BLOCKED;
        self->block_reason = BLOCK_SEM;
        sched_unready(self);

        rtos_task_t **pp = &s->wait_head;
        while (*pp != NULL && (*pp)->priority <= self->priority) {
            pp = &(*pp)->next;
        }
        self->next = *pp;
        *pp        = self;

        _next_task = sched_elect();
        rtos_exit_critical(primask);
        sched_yield_needed();
        /* Resume here after woken — loop to retry */
    }
}

static void _sem_signal_locked(rtos_sem_t *s)
{
    if (s->wait_head != NULL) {
        rtos_task_t *waiter = s->wait_head;
        s->wait_head        = waiter->next;
        waiter->next        = NULL;
        waiter->block_reason = BLOCK_NONE;
        sched_ready(waiter);
    } else if (s->count < s->max_count) {
        s->count++;
    }
}

void rtos_sem_signal(rtos_sem_t *s)
{
    uint32_t primask = rtos_enter_critical();
    bool yield = false;

    if (s->wait_head != NULL && s->wait_head->priority < _current_task->priority) {
        _next_task = s->wait_head;
        yield = true;
    }
    _sem_signal_locked(s);

    rtos_exit_critical(primask);
    if (yield) sched_yield_needed();
}

void rtos_sem_signal_isr(rtos_sem_t *s)
{
    uint32_t primask = rtos_enter_critical();
    _sem_signal_locked(s);
    rtos_exit_critical(primask);
    sched_yield_needed();
}
