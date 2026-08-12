/**
 * trace_hooks.c
 *
 * FreeRTOS trace instrumentation.
 *
 * Drop trace_hooks.c and trace_hooks.h into your FreeRTOS project and add
 * the following to FreeRTOSConfig.h:
 *
 *   #include "trace_hooks.h"
 *
 *   #define traceTASK_SWITCHED_IN()         Trace_TaskSwitchedIn()
 *   #define traceTASK_SWITCHED_OUT()        Trace_TaskSwitchedOut()
 *   #define traceTASK_CREATE(pxTask)        Trace_TaskCreated(pxTask)
 *   #define traceTASK_DELETE(pxTask)        Trace_TaskDeleted(pxTask)
 *   #define traceTASK_DELAY()               Trace_TaskBlocked(TRACE_REASON_BLOCK)
 *   #define traceGIVE_MUTEX_RECURSIVE(m)    Trace_MutexGiven((uint8_t)(uintptr_t)(m))
 *   #define traceTAKE_MUTEX_RECURSIVE(m)    Trace_MutexTaken((uint8_t)(uintptr_t)(m))
 *   #define traceBLOCKING_ON_MUTEX(m)       Trace_MutexBlocked((uint8_t)(uintptr_t)(m))
 *
 * The implementation is platform-agnostic: it calls Trace_UART_Send() which
 * you implement for your specific MCU UART peripheral.
 *
 */

#include "trace_hooks.h"
#include "trace_protocol.h"
#include <string.h>

/* ── ring buffer ──────────────────────────────────────────────────── */

#define RING_SIZE 512u   /* must be power of two */
#define RING_MASK (RING_SIZE - 1u)

static volatile uint8_t  s_ring[RING_SIZE];
static volatile uint32_t s_head = 0;
static volatile uint32_t s_tail = 0;

static void ring_push(const uint8_t *buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        uint32_t next = (s_head + 1u) & RING_MASK;
        if (next != s_tail) {       /* drop on overflow — never stall the ISR */
            s_ring[s_head] = buf[i];
            s_head = next;
        }
    }
}

/* Called from your idle task or DMA completion callback */
void Trace_Flush(void)
{
    while (s_tail != s_head) {
        Trace_UART_Send(s_ring[s_tail]);
        s_tail = (s_tail + 1u) & RING_MASK;
    }
}

/* ── task slot registry ───────────────────────────────────────────── */

static void *s_task_handles[TRACE_MAX_TASKS];
static uint8_t s_next_slot = 0;

static uint8_t get_or_assign_slot(void *handle)
{
    for (uint8_t i = 0; i < TRACE_MAX_TASKS; i++) {
        if (s_task_handles[i] == handle)
            return i;
    }
    uint8_t slot = s_next_slot % TRACE_MAX_TASKS;
    s_task_handles[slot] = handle;
    s_next_slot++;
    return slot;
}

/* ── frame emitter ───────────────────────────────────────────────── */

static void emit(uint8_t type, uint32_t ts, uint8_t task_id, uint8_t param)
{
    TraceFrame f;
    f.magic     = TRACE_MAGIC;
    f.type      = type;
    f.timestamp = ts;
    f.task_id   = task_id;
    f.param     = param;
    ring_push((const uint8_t *)&f, TRACE_FRAME_SIZE);
}

/* ── public API ───────────────────────────────────────────────────── */

void Trace_Init(void)
{
    memset((void *)s_ring, 0, sizeof(s_ring));
    memset(s_task_handles, 0, sizeof(s_task_handles));
    s_head = s_tail = 0;
    s_next_slot = 0;
}

void Trace_TaskSwitchedIn(void)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    uint8_t prio = (uint8_t)uxTaskPriorityGet(NULL);
    emit(TRACE_EVT_TASK_SWITCHED_IN, xTaskGetTickCount(), slot, prio);
}

void Trace_TaskSwitchedOut(void)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_TASK_SWITCHED_OUT, xTaskGetTickCount(), slot, TRACE_REASON_PREEMPT);
}

void Trace_TaskCreated(void *handle)
{
    uint8_t slot = get_or_assign_slot(handle);
    uint8_t prio = (uint8_t)uxTaskPriorityGet(handle);
    emit(TRACE_EVT_TASK_CREATED, xTaskGetTickCount(), slot, prio);

    /* Transmit task name in 4-byte chunks encoded into the timestamp field */
    char name[configMAX_TASK_NAME_LEN + 1];
    vTaskGetTaskName(handle, name, sizeof(name));
    uint32_t len = strlen(name);
    for (uint32_t seq = 0; seq * 4u < len; seq++) {
        uint32_t chunk = 0;
        for (uint32_t b = 0; b < 4u && (seq * 4u + b) < len; b++)
            chunk |= ((uint32_t)(uint8_t)name[seq * 4u + b]) << (b * 8u);
        emit(TRACE_EVT_TASK_NAME, chunk, slot, (uint8_t)seq);
    }
}

void Trace_TaskDeleted(void *handle)
{
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_TASK_DELETED, xTaskGetTickCount(), slot, 0);
    s_task_handles[slot] = NULL;
}

void Trace_TaskBlocked(uint8_t reason)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_TASK_BLOCKED, xTaskGetTickCount(), slot, reason);
}

void Trace_MutexTaken(uint8_t mutex_id)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_MUTEX_TAKEN, xTaskGetTickCount(), slot, mutex_id);
}

void Trace_MutexGiven(uint8_t mutex_id)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_MUTEX_GIVEN, xTaskGetTickCount(), slot, mutex_id);
}

void Trace_MutexBlocked(uint8_t mutex_id)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_MUTEX_BLOCKED, xTaskGetTickCount(), slot, mutex_id);
}

void Trace_PriorityInherited(uint8_t new_prio)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_PRIORITY_INHERIT, xTaskGetTickCount(), slot, new_prio);
}

void Trace_PriorityRestored(uint8_t orig_prio)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_PRIORITY_RESTORE, xTaskGetTickCount(), slot, orig_prio);
}

void Trace_DeadlineMiss(uint8_t overrun_ticks)
{
    void *handle = (void *)pxCurrentTCB;
    uint8_t slot = get_or_assign_slot(handle);
    emit(TRACE_EVT_DEADLINE_MISS, xTaskGetTickCount(), slot, overrun_ticks);
}

void Trace_Tick(void)
{
    emit(TRACE_EVT_TICK, xTaskGetTickCount(), 0, 0);
}
