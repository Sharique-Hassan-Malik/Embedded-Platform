/**
 * trace_protocol.h
 *
 * Binary trace protocol for the RTOS Scheduler Visualizer.
 *
 * Every event is a fixed 8-byte frame transmitted over UART:
 *
 *   Offset  Size  Field
 *   0       1     MAGIC     = 0xA5 (sync byte)
 *   1       1     TYPE      event type (see TRACE_EVT_*)
 *   2       4     TIMESTAMP tick count (little-endian uint32)
 *   6       1     TASK_ID   0–15 (handle slot index)
 *   7       1     PARAM     context-dependent (priority, mutex_id, etc.)
 *
 * The magic byte allows re-synchronisation after UART noise or buffer
 * overflow. If a receiver sees a byte other than 0xA5 at the expected
 * frame start it discards bytes until it finds 0xA5 again.
 *
 * Tick-count wrap-around is handled by the host decoder using a 64-bit
 * extended counter that detects the transition from 0xFFFFFFFF to 0.
 *
 */

#ifndef TRACE_PROTOCOL_H
#define TRACE_PROTOCOL_H

#include <stdint.h>

/* ── frame layout ─────────────────────────────────────────────────── */

#define TRACE_MAGIC        0xA5u
#define TRACE_FRAME_SIZE   8u

typedef struct {
    uint8_t  magic;       /* always TRACE_MAGIC                        */
    uint8_t  type;        /* TRACE_EVT_*                               */
    uint32_t timestamp;   /* FreeRTOS tick count (little-endian)       */
    uint8_t  task_id;     /* slot index 0–15                           */
    uint8_t  param;       /* type-specific (priority, mutex id, flags) */
} __attribute__((packed)) TraceFrame;

/* ── event types ─────────────────────────────────────────────────── */

#define TRACE_EVT_TASK_SWITCHED_IN   0x01u  /* param = current priority      */
#define TRACE_EVT_TASK_SWITCHED_OUT  0x02u  /* param = reason (0=preempt,    */
                                            /*   1=yield, 2=block, 3=delete) */
#define TRACE_EVT_TASK_CREATED       0x03u  /* param = initial priority       */
#define TRACE_EVT_TASK_DELETED       0x04u  /* param = 0                      */
#define TRACE_EVT_TASK_READY         0x05u  /* param = priority               */
#define TRACE_EVT_TASK_BLOCKED       0x06u  /* param = block reason           */
#define TRACE_EVT_MUTEX_TAKEN        0x07u  /* param = mutex id (0–15)        */
#define TRACE_EVT_MUTEX_GIVEN        0x08u  /* param = mutex id               */
#define TRACE_EVT_MUTEX_BLOCKED      0x09u  /* param = mutex id               */
#define TRACE_EVT_DEADLINE_MISS      0x0Au  /* param = overrun ticks (sat. 255)*/
#define TRACE_EVT_TICK               0x0Bu  /* param = 0 (heartbeat)          */
#define TRACE_EVT_PRIORITY_INHERIT   0x0Cu  /* param = inherited priority     */
#define TRACE_EVT_PRIORITY_RESTORE   0x0Du  /* param = restored priority      */
#define TRACE_EVT_TASK_NAME          0x0Eu  /* special: 8-byte ASCII chunk    */
                                            /* task_id = slot, param = seq#   */
                                            /* timestamp bytes used as chars  */

/* ── switch-out reasons (param for TASK_SWITCHED_OUT) ───────────── */

#define TRACE_REASON_PREEMPT   0u
#define TRACE_REASON_YIELD     1u
#define TRACE_REASON_BLOCK     2u
#define TRACE_REASON_DELETE    3u

/* ── utility ──────────────────────────────────────────────────────── */

#define TRACE_MAX_TASKS   16u
#define TRACE_MAX_MUTEXES 16u

#endif /* TRACE_PROTOCOL_H */
