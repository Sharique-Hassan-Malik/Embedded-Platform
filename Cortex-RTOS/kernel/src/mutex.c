/*
 * mutex.c — binary mutex with priority inheritance.
 *
 * Priority inheritance: when a high-priority task (H) blocks on a mutex
 * held by a low-priority task (L), L's priority is temporarily raised to
 * H's level so that medium-priority tasks cannot preempt L and extend the
 * wait indefinitely.  The original priority is restored on unlock.
 *
 * The mutex is NOT reentrant and MUST be released by the task that acquired
 * it.  Attempting to lock from an ISR is undefined behaviour.
 */

#include "rtos.h"
#include "rtos_objects.h"
#include "scheduler.h"

void rtos_mutex_init(rtos_mutex_t *m)
{
    m->owner     = NULL;
    m->wait_head = NULL;
}

rtos_err_t rtos_mutex_lock(rtos_mutex_t *m, rtos_tick_t timeout_ticks)
{
    rtos_tick_t deadline = rtos_tick_count() + timeout_ticks;

    for (;;) {
        uint32_t primask = rtos_enter_critical();

        if (m->owner == NULL) {
            /* Mutex is free — take it */
            m->owner = _current_task;
            rtos_exit_critical(primask);
            return RTOS_OK;
        }

        if (timeout_ticks == RTOS_NO_WAIT) {
            rtos_exit_critical(primask);
            return RTOS_TIMEOUT;
        }

        /* Check deadline before blocking */
        if (timeout_ticks != RTOS_WAIT_FOREVER &&
            (rtos_tick_t)(rtos_tick_count() - deadline) < 0x80000000UL) {
            rtos_exit_critical(primask);
            return RTOS_TIMEOUT;
        }

        /* Priority inheritance: raise owner's priority if we're higher */
        rtos_task_t *owner = m->owner;
        if (_current_task->priority < owner->priority) {
            /* Temporarily raise owner — move it in the ready queue */
            if (owner->state == TASK_READY) sched_unready(owner);
            owner->priority = _current_task->priority;
            if (owner->state == TASK_READY) sched_ready(owner);
        }

        /* Block self, insert into wait list ordered by priority */
        rtos_task_t *self = _current_task;
        self->state        = TASK_BLOCKED;
        self->block_reason = BLOCK_MUTEX;
        sched_unready(self);

        /* Insert ordered by priority (lower value = higher priority first) */
        rtos_task_t **pp = &m->wait_head;
        while (*pp != NULL && (*pp)->priority <= self->priority) {
            pp = &(*pp)->next;
        }
        self->next = *pp;
        *pp        = self;

        _next_task = sched_elect();
        rtos_exit_critical(primask);
        sched_yield_needed();

        /* Resume here after woken — loop back to try the lock again */
    }
}

void rtos_mutex_unlock(rtos_mutex_t *m)
{
    uint32_t primask = rtos_enter_critical();

    /* Restore original priority if it was elevated */
    rtos_task_t *owner = m->owner;
    if (owner != NULL && owner->priority != owner->base_priority) {
        if (owner->state == TASK_READY || owner->state == TASK_RUNNING) {
            sched_unready(owner);
            owner->priority = owner->base_priority;
            sched_ready(owner);
        } else {
            owner->priority = owner->base_priority;
        }
    }

    /* Wake the highest-priority waiter */
    if (m->wait_head != NULL) {
        rtos_task_t *waiter = m->wait_head;
        m->wait_head        = waiter->next;
        waiter->next        = NULL;
        m->owner            = waiter;
        waiter->block_reason = BLOCK_NONE;
        sched_ready(waiter);
        if (waiter->priority < _current_task->priority) {
            _next_task = waiter;
            rtos_exit_critical(primask);
            sched_yield_needed();
            return;
        }
    } else {
        m->owner = NULL;
    }

    rtos_exit_critical(primask);
}
