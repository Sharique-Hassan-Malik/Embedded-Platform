/**
 * main.c — Demo FreeRTOS application for the RTOS Scheduler Visualizer
 *
 * Demonstrates three instrumented scenarios on an STM32 or QEMU-M3 target:
 *
 *   1. Normal periodic tasks at different priorities (High/Med/Low)
 *   2. Priority inversion: LowTask holds shared_mutex, HighTask blocks on it,
 *      MedTask preempts LowTask — showing the classic unbounded inversion.
 *      FreeRTOS priority inheritance then resolves it.
 *   3. Deadline miss: HighTask exceeds its 50-tick deadline once every 500 ticks.
 *
 * Tick rate: 1000 Hz (1 ms per tick).
 *
 * Build with:
 *   arm-none-eabi-gcc -DSTM32F4xx -mcpu=cortex-m4 -mthumb \
 *     -IFreeRTOS/include -IFreeRTOS/portable/GCC/ARM_CM4F \
 *     -Ifirmware/include \
 *     firmware/src/trace_hooks.c firmware/demo/main.c \
 *     FreeRTOS/tasks.c FreeRTOS/list.c FreeRTOS/queue.c \
 *     FreeRTOS/timers.c FreeRTOS/portable/GCC/ARM_CM4F/port.c \
 *     FreeRTOS/portable/MemMang/heap_4.c \
 *     -o demo.elf
 *
 */

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "trace_hooks.h"

/* ── configuration ────────────────────────────────────────────────── */

#define HIGH_PRIO   (configMAX_PRIORITIES - 1)
#define MED_PRIO    (configMAX_PRIORITIES - 2)
#define LOW_PRIO    (configMAX_PRIORITIES - 3)

#define TICK_RATE_HZ    configTICK_RATE_HZ
#define DEADLINE_TICKS  50u     /* HighTask must complete within 50 ms */

/* ── shared state ─────────────────────────────────────────────────── */

static SemaphoreHandle_t shared_mutex;
static volatile uint32_t s_inversion_cycle = 0;

/* ── simulated work ───────────────────────────────────────────────── */

static void busy_delay(uint32_t ticks)
{
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < ticks)
        ;   /* busy wait — intentional for demo */
}

/* ── tasks ────────────────────────────────────────────────────────── */

static void HighTask(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    uint32_t cycle = 0;

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(100));
        TickType_t start = xTaskGetTickCount();

        /* Every 5th cycle: try to take the shared mutex */
        if (cycle % 5u == 0u) {
            if (xSemaphoreTake(shared_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                busy_delay(pdMS_TO_TICKS(10));
                xSemaphoreGive(shared_mutex);
            }
        }

        /* Every 10th cycle: simulate deadline miss by doing extra work */
        if (cycle % 10u == 9u)
            busy_delay(DEADLINE_TICKS + 10u);
        else
            busy_delay(pdMS_TO_TICKS(5));

        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed > DEADLINE_TICKS)
            Trace_DeadlineMiss((uint8_t)((elapsed - DEADLINE_TICKS) > 255u
                                         ? 255u : elapsed - DEADLINE_TICKS));
        cycle++;
    }
}

static void MedTask(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(70));
        busy_delay(pdMS_TO_TICKS(20));
    }
}

static void LowTask(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(150));

        /* Hold shared_mutex for a long time to trigger priority inversion */
        if (xSemaphoreTake(shared_mutex, 0) == pdTRUE) {
            busy_delay(pdMS_TO_TICKS(60));   /* holds lock for 60 ms */
            xSemaphoreGive(shared_mutex);
        }
        s_inversion_cycle++;
    }
}

static void IdleHook(void *arg)
{
    (void)arg;
    for (;;) {
        Trace_Flush();
        vTaskDelay(1);
    }
}

/* ── main ─────────────────────────────────────────────────────────── */

int main(void)
{
    /* Platform init (clock setup, UART init) would go here */

    Trace_Init();

    shared_mutex = xSemaphoreCreateMutex();
    configASSERT(shared_mutex != NULL);

    xTaskCreate(HighTask, "HighTask", 256, NULL, HIGH_PRIO, NULL);
    xTaskCreate(MedTask,  "MedTask",  256, NULL, MED_PRIO,  NULL);
    xTaskCreate(LowTask,  "LowTask",  256, NULL, LOW_PRIO,  NULL);
    xTaskCreate(IdleHook, "TraceFlush", 128, NULL, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();

    /* Never reached */
    for (;;) {}
}
