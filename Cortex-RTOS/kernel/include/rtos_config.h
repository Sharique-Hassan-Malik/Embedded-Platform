/*
 * rtos_config.h — compile-time configuration knobs.
 *
 * All values here can be overridden by defining them before including
 * this header, or by passing -DRTOS_MAX_TASKS=N etc. on the compiler
 * command line.
 */

#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

/* Maximum number of tasks (including idle) */
#ifndef RTOS_MAX_TASKS
#define RTOS_MAX_TASKS          8
#endif

/* Tick rate in Hz — determines SysTick period */
#ifndef RTOS_TICK_HZ
#define RTOS_TICK_HZ            1000
#endif

/* System core clock in Hz — used to program SysTick reload */
#ifndef RTOS_CORE_CLOCK_HZ
#define RTOS_CORE_CLOCK_HZ      84000000UL   /* STM32F401 default */
#endif

/* Stack size in bytes for tasks that don't specify one */
#ifndef RTOS_DEFAULT_STACK_BYTES
#define RTOS_DEFAULT_STACK_BYTES 512
#endif

/* Memory pool: total heap size in bytes */
#ifndef RTOS_HEAP_BYTES
#define RTOS_HEAP_BYTES         4096
#endif

/* Number of priority levels (0 = highest, RTOS_PRIORITY_LEVELS-1 = lowest) */
#ifndef RTOS_PRIORITY_LEVELS
#define RTOS_PRIORITY_LEVELS    8
#endif

/* Idle task priority (always the lowest) */
#define RTOS_IDLE_PRIORITY      (RTOS_PRIORITY_LEVELS - 1)

/* Compile-time assertion helper */
#define RTOS_STATIC_ASSERT(cond, msg) \
    typedef char _rtos_static_assert_##msg[(cond) ? 1 : -1]

RTOS_STATIC_ASSERT(RTOS_MAX_TASKS >= 2, max_tasks_must_be_at_least_2);
RTOS_STATIC_ASSERT(RTOS_PRIORITY_LEVELS <= 32, priority_levels_must_not_exceed_32);

#endif /* RTOS_CONFIG_H */
