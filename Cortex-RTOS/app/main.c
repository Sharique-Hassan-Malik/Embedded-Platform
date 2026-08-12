/*
 * main.c — CortexRTOS demo application.
 *
 * Four tasks demonstrate every kernel feature:
 *
 *   led_task      (priority 2) — blinks the LED every 500 ms using rtos_delay.
 *
 *   producer_task (priority 1) — acquires a mutex, writes a message to a
 *                                shared buffer, releases the mutex, then
 *                                signals a semaphore.
 *
 *   consumer_task (priority 1) — waits on the semaphore, acquires the mutex,
 *                                reads the buffer and prints it over UART.
 *
 *   stats_task    (priority 3) — prints kernel statistics every 2 seconds
 *                                using pool-allocated buffers.
 *
 * Priority inheritance is implicitly exercised: if led_task preempts while
 * producer_task holds the mutex, consumer_task (same priority as producer)
 * blocks and producer's effective priority is raised to consumer's level
 * if consumer has a higher priority in a modified configuration.
 *
 * Output format (UART2, 115200 baud, 3.3 V, PA2):
 *   [  1234] producer: sent msg #5
 *   [  1234] consumer: got "msg #5"
 *   [  2000] stats: tasks=4 tick=2000 heap_free=2048
 */

#include "rtos.h"
#include "hal.h"

#include <string.h>
#include <stdio.h>

/* ── Shared objects ──────────────────────────────────────────────────────────── */

static rtos_mutex_t g_mutex;
static rtos_sem_t   g_sem;

#define MSG_LEN 32
static char g_msg[MSG_LEN];

/* ── Memory pool for stats buffers ──────────────────────────────────────────── */

#define POOL_BLOCK_SIZE  64
#define POOL_BLOCKS      4
static uint8_t      g_pool_buf[POOL_BLOCK_SIZE * POOL_BLOCKS] __attribute__((aligned(8)));
static rtos_pool_t  g_pool;

/* ── Task stacks (static, no dynamic allocation) ─────────────────────────────── */

#define STACK_WORDS 128
static uint32_t led_stack[STACK_WORDS];
static uint32_t prod_stack[STACK_WORDS];
static uint32_t cons_stack[STACK_WORDS];
static uint32_t stats_stack[STACK_WORDS];

/* ── Tasks ───────────────────────────────────────────────────────────────────── */

static void led_task(void *arg)
{
    (void)arg;
    for (;;) {
        hal_led_toggle();
        rtos_delay(500);
    }
}

static void producer_task(void *arg)
{
    (void)arg;
    static uint32_t msg_id = 0;

    for (;;) {
        rtos_delay(300);

        rtos_mutex_lock(&g_mutex, RTOS_WAIT_FOREVER);
        snprintf(g_msg, MSG_LEN, "msg #%lu", (unsigned long)msg_id++);
        rtos_mutex_unlock(&g_mutex);

        hal_uart_printf("[%6u] producer: sent %s\r\n",
                        (unsigned)rtos_tick_count(), g_msg);

        rtos_sem_signal(&g_sem);
    }
}

static void consumer_task(void *arg)
{
    (void)arg;
    char local[MSG_LEN];

    for (;;) {
        rtos_sem_wait(&g_sem, RTOS_WAIT_FOREVER);

        rtos_mutex_lock(&g_mutex, RTOS_WAIT_FOREVER);
        memcpy(local, g_msg, MSG_LEN);
        rtos_mutex_unlock(&g_mutex);

        hal_uart_printf("[%6u] consumer: got \"%s\"\r\n",
                        (unsigned)rtos_tick_count(), local);
    }
}

static void stats_task(void *arg)
{
    (void)arg;
    extern rtos_task_t _task_table[];
    extern uint8_t     _task_count;

    for (;;) {
        rtos_delay(2000);

        /* Allocate a buffer from the pool to format the stats line */
        char *buf = (char *)rtos_pool_alloc(&g_pool);
        if (buf == NULL) {
            hal_uart_puts("[stats] pool exhausted\r\n");
            continue;
        }

        snprintf(buf, POOL_BLOCK_SIZE,
                 "[%6u] stats: tasks=%u tick=%u pool_free=%u\r\n",
                 (unsigned)rtos_tick_count(),
                 (unsigned)_task_count,
                 (unsigned)rtos_tick_count(),
                 (unsigned)rtos_pool_free_count(&g_pool));

        hal_uart_puts(buf);

        /* Per-task details */
        for (uint8_t i = 0; i < _task_count; i++) {
            rtos_task_t *t = &_task_table[i];
            hal_uart_printf("  %-12s prio=%u state=%u runs=%lu\r\n",
                            t->name ? t->name : "?",
                            (unsigned)t->priority,
                            (unsigned)t->state,
                            (unsigned long)t->run_count);
        }

        rtos_pool_free(&g_pool, buf);
    }
}

/* ── main ────────────────────────────────────────────────────────────────────── */

int main(void)
{
    hal_clock_init();
    hal_uart_init(115200);
    hal_led_init();

    hal_uart_puts("\r\nCortexRTOS booting...\r\n");

    rtos_init();

    rtos_mutex_init(&g_mutex);
    rtos_sem_init(&g_sem, 0, 8);
    rtos_pool_init(&g_pool, g_pool_buf, sizeof(g_pool_buf), POOL_BLOCK_SIZE);

    rtos_task_create("led",      led_task,      NULL, led_stack,   sizeof(led_stack),   2);
    rtos_task_create("producer", producer_task, NULL, prod_stack,  sizeof(prod_stack),  1);
    rtos_task_create("consumer", consumer_task, NULL, cons_stack,  sizeof(cons_stack),  1);
    rtos_task_create("stats",    stats_task,    NULL, stats_stack, sizeof(stats_stack), 3);

    hal_uart_puts("Tasks created. Starting scheduler.\r\n");

    rtos_start();   /* never returns */

    return 0;       /* unreachable */
}
