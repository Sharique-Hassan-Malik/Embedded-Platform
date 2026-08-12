/**
 * trace_hooks.h
 *
 * Public API for the FreeRTOS trace instrumentation module.
 *
 */

#ifndef TRACE_HOOKS_H
#define TRACE_HOOKS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the trace subsystem.
 * Call once before the scheduler starts.
 */
void Trace_Init(void);

/**
 * Flush buffered trace frames to UART.
 * Call periodically from the idle task or a low-priority background task.
 */
void Trace_Flush(void);

/* Hook functions — called from FreeRTOS trace macros */
void Trace_TaskSwitchedIn(void);
void Trace_TaskSwitchedOut(void);
void Trace_TaskCreated(void *handle);
void Trace_TaskDeleted(void *handle);
void Trace_TaskBlocked(uint8_t reason);
void Trace_MutexTaken(uint8_t mutex_id);
void Trace_MutexGiven(uint8_t mutex_id);
void Trace_MutexBlocked(uint8_t mutex_id);
void Trace_PriorityInherited(uint8_t new_prio);
void Trace_PriorityRestored(uint8_t orig_prio);
void Trace_DeadlineMiss(uint8_t overrun_ticks);
void Trace_Tick(void);

/**
 * Platform-specific UART transmit — implement this for your MCU.
 * Must be safe to call from an interrupt-disabled context.
 *
 * Example (STM32 HAL):
 *   void Trace_UART_Send(uint8_t byte) {
 *       HAL_UART_Transmit(&huart2, &byte, 1, HAL_MAX_DELAY);
 *   }
 */
void Trace_UART_Send(uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* TRACE_HOOKS_H */
