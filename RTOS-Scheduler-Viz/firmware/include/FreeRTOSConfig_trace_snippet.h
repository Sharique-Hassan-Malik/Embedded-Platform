/**
 * FreeRTOSConfig_trace_snippet.h
 *
 * Add the contents of this file to your FreeRTOSConfig.h to enable the
 * RTOS Scheduler Visualizer trace hooks.
 *
 */

/* ── include the hook declarations ───────────────────────────────── */
#include "trace_hooks.h"

/* ── map FreeRTOS trace macros to hook functions ─────────────────── */

#define traceTASK_SWITCHED_IN() \
    Trace_TaskSwitchedIn()

#define traceTASK_SWITCHED_OUT() \
    Trace_TaskSwitchedOut()

#define traceTASK_CREATE(pxNewTCB) \
    Trace_TaskCreated((void *)(pxNewTCB))

#define traceTASK_DELETE(pxTaskToDelete) \
    Trace_TaskDeleted((void *)(pxTaskToDelete))

#define traceTASK_DELAY() \
    Trace_TaskBlocked(TRACE_REASON_BLOCK)

#define traceTASK_DELAY_UNTIL(xTimeToWake) \
    Trace_TaskBlocked(TRACE_REASON_BLOCK)

#define traceBLOCKING_ON_QUEUE_RECEIVE(pxQueue) \
    Trace_TaskBlocked(TRACE_REASON_BLOCK)

#define traceGIVE_MUTEX_RECURSIVE(pxMutex) \
    Trace_MutexGiven((uint8_t)((uintptr_t)(pxMutex) & 0x0Fu))

#define traceTAKE_MUTEX_RECURSIVE(pxMutex) \
    Trace_MutexTaken((uint8_t)((uintptr_t)(pxMutex) & 0x0Fu))

#define traceBLOCKING_ON_MUTEX_TAKE(pxMutex) \
    Trace_MutexBlocked((uint8_t)((uintptr_t)(pxMutex) & 0x0Fu))

#define traceTASK_PRIORITY_INHERIT(pxTCBOfMutexHolder, uxInheritedPriority) \
    Trace_PriorityInherited((uint8_t)(uxInheritedPriority))

#define traceTASK_PRIORITY_DISINHERIT(pxTCBOfMutexHolder, uxOriginalPriority) \
    Trace_PriorityRestored((uint8_t)(uxOriginalPriority))

#define traceTASK_INCREMENT_TICK(xTickCount) \
    do { if (((xTickCount) & 0x3Fu) == 0u) Trace_Tick(); } while (0)
