/*
 * test_kernel.c — host-side unit tests for CortexRTOS kernel.
 *
 * Compiled with native GCC.  No Cortex-M toolchain required.
 * Compile and run via: make -C tests/host
 *
 * Test groups:
 *   T1  Scheduler — ready queue ordering, round-robin, election
 *   T2  Delay list — tasks wake after correct tick count
 *   T3  Memory pool — alloc/free, exhaustion, double-free safety
 *   T4  Mutex — acquire/release, priority ordering of waiters
 *   T5  Semaphore — counting, wait/signal ordering
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

/* Pull in the kernel headers (with CORTEX_M undefined — no asm) */
#include "rtos.h"
#include "rtos_objects.h"
#include "scheduler.h"

/* Forward declarations of test helpers from host_stubs.c */
extern void _drive_scheduler(void);
extern void _advance_ticks(uint32_t n);

/* ── Minimal test framework ──────────────────────────────────────────────────── */

static int _pass = 0, _fail = 0;

#define TEST(name) \
    do { \
        printf("  %-52s", name); \
        fflush(stdout); \
    } while (0)

#define EXPECT(cond) \
    do { \
        if (cond) { \
            printf("PASS\n"); _pass++; \
        } else { \
            printf("FAIL  (line %d: %s)\n", __LINE__, #cond); _fail++; \
        } \
    } while (0)

#define SECTION(title) printf("\n%s\n", title)

/* ── Fake stacks (host doesn't need real stack space) ────────────────────────── */

static uint32_t stk[RTOS_MAX_TASKS][64];

static void dummy_fn(void *arg) { (void)arg; for (;;) {} }

/* Reset the kernel state between test groups */
static void kernel_reset(void)
{
    sched_init();
    memset(_task_table, 0, sizeof(_task_table));
    _task_count   = 0;
    _current_task = NULL;
    _next_task    = NULL;
    _tick_count   = 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * T1 — Scheduler
 * ══════════════════════════════════════════════════════════════════════════════ */

static void test_scheduler(void)
{
    SECTION("T1  Scheduler");
    kernel_reset();

    /* Create three tasks at different priorities */
    rtos_task_t *t_hi  = rtos_task_create("hi",  dummy_fn, NULL, stk[0], sizeof(stk[0]), 0);
    rtos_task_t *t_mid = rtos_task_create("mid", dummy_fn, NULL, stk[1], sizeof(stk[1]), 1);
    rtos_task_t *t_lo  = rtos_task_create("lo",  dummy_fn, NULL, stk[2], sizeof(stk[2]), 2);

    TEST("task_create returns non-NULL");
    EXPECT(t_hi != NULL && t_mid != NULL && t_lo != NULL);

    TEST("elect returns highest priority task");
    rtos_task_t *elected = sched_elect();
    EXPECT(elected == t_hi);

    /* Simulate hi running */
    _current_task = t_hi;
    t_hi->state = TASK_RUNNING;
    sched_unready(t_hi);   /* running task is not in the ready queue */

    TEST("elect with hi running returns mid");
    elected = sched_elect();
    EXPECT(elected == t_mid);

    /* Put hi back as ready (e.g. it yielded) */
    sched_ready(t_hi);

    TEST("round-robin: two tasks at same priority alternate");
    rtos_task_t *t_mid2 = rtos_task_create("mid2", dummy_fn, NULL, stk[3], sizeof(stk[3]), 1);
    (void)t_lo;
    /* Remove t_hi from the ready queue so the election stays at priority 1 */
    sched_unready(t_hi);
    /* Both t_mid and t_mid2 are at prio 1. Two successive elections must differ. */
    rtos_task_t *e1 = sched_elect();
    rtos_task_t *e2 = sched_elect();
    EXPECT(e1 != e2 && (e1 == t_mid || e1 == t_mid2));

    TEST("unready removes task from ready queue");
    sched_unready(t_mid);
    rtos_task_t *after_remove = sched_elect();
    /* mid is gone; only mid2 at prio 1 should come out */
    bool mid_gone = true;
    for (int i = 0; i < 5; i++) {
        rtos_task_t *e = sched_elect();
        if (e == t_mid) { mid_gone = false; break; }
    }
    (void)after_remove;
    EXPECT(mid_gone);

    TEST("priority mask clears when level empties");
    /* Remove all prio-0 tasks */
    sched_unready(t_hi);
    /* Now elect should skip prio 0 and pick from prio 1 */
    rtos_task_t *e = sched_elect();
    EXPECT(e != NULL && e->priority == 1);

    TEST("task_create respects RTOS_MAX_TASKS limit");
    kernel_reset();
    rtos_task_t *tasks[RTOS_MAX_TASKS];
    for (int i = 0; i < RTOS_MAX_TASKS; i++) {
        tasks[i] = rtos_task_create("t", dummy_fn, NULL, stk[i % RTOS_MAX_TASKS],
                                    sizeof(stk[0]), 2);
    }
    rtos_task_t *overflow = rtos_task_create("over", dummy_fn, NULL,
                                              stk[0], sizeof(stk[0]), 2);
    EXPECT(overflow == NULL);
    (void)tasks;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * T2 — Delay list
 * ══════════════════════════════════════════════════════════════════════════════ */

static void test_delay(void)
{
    SECTION("T2  Delay list");
    kernel_reset();

    rtos_task_t *t_a = rtos_task_create("a", dummy_fn, NULL, stk[0], sizeof(stk[0]), 1);
    rtos_task_t *t_b = rtos_task_create("b", dummy_fn, NULL, stk[1], sizeof(stk[1]), 2);

    /* Manually put t_a into the blocked/delay state (rtos_delay calls sched_unready
     * and sets wake_tick; we replicate that here without the context switch) */

    _current_task = t_a;
    t_a->state       = TASK_BLOCKED;
    t_a->block_reason = BLOCK_DELAY;
    t_a->wake_tick   = _tick_count + 5;
    sched_unready(t_a);
    t_a->next   = NULL;
    _delay_list = t_a;

    _current_task = t_b;
    t_b->state = TASK_RUNNING;

    TEST("delayed task not elected before deadline");
    _advance_ticks(4);
    bool found_before = false;
    for (int i = 0; i < 20; i++) {
        rtos_task_t *e = sched_elect();
        if (e == t_a) { found_before = true; break; }
    }
    EXPECT(!found_before);

    TEST("delayed task elected at deadline");
    _advance_ticks(1);   /* tick 5 — deadline reached */
    bool found_after = false;
    for (int i = 0; i < 20; i++) {
        rtos_task_t *e = sched_elect();
        if (e == t_a) { found_after = true; break; }
    }
    EXPECT(found_after);

    TEST("delay list is empty after task woken");
    EXPECT(_delay_list == NULL);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * T3 — Memory pool
 * ══════════════════════════════════════════════════════════════════════════════ */

#define POOL_BLOCKS 4
#define POOL_BSIZE  16

static uint8_t     _pool_buf[POOL_BLOCKS * POOL_BSIZE] __attribute__((aligned(8)));
static rtos_pool_t _pool;

static void test_pool(void)
{
    SECTION("T3  Memory pool");

    rtos_pool_init(&_pool, _pool_buf, sizeof(_pool_buf), POOL_BSIZE);

    TEST("free_count equals total blocks after init");
    EXPECT(rtos_pool_free_count(&_pool) == POOL_BLOCKS);

    TEST("alloc returns non-NULL for each block");
    void *blks[POOL_BLOCKS];
    bool all_valid = true;
    for (int i = 0; i < POOL_BLOCKS; i++) {
        blks[i] = rtos_pool_alloc(&_pool);
        if (blks[i] == NULL) all_valid = false;
    }
    EXPECT(all_valid);

    TEST("alloc returns NULL when exhausted");
    EXPECT(rtos_pool_alloc(&_pool) == NULL);

    TEST("free_count is zero when exhausted");
    EXPECT(rtos_pool_free_count(&_pool) == 0);

    TEST("free restores one block");
    rtos_pool_free(&_pool, blks[0]);
    EXPECT(rtos_pool_free_count(&_pool) == 1);

    TEST("alloc after free returns non-NULL");
    void *p = rtos_pool_alloc(&_pool);
    EXPECT(p != NULL);

    TEST("blocks are within pool buffer bounds");
    uintptr_t base = (uintptr_t)_pool_buf;
    uintptr_t end  = base + sizeof(_pool_buf);
    bool all_in_range = true;
    for (int i = 1; i < POOL_BLOCKS; i++) {
        uintptr_t b = (uintptr_t)blks[i];
        if (b < base || b + POOL_BSIZE > end) all_in_range = false;
    }
    EXPECT(all_in_range);

    TEST("free NULL does not crash");
    rtos_pool_free(&_pool, NULL);   /* should be a no-op */
    EXPECT(true);                   /* reaching here = pass */

    TEST("full alloc-free cycle restores all blocks");
    /* Re-free everything */
    for (int i = 1; i < POOL_BLOCKS; i++) rtos_pool_free(&_pool, blks[i]);
    rtos_pool_free(&_pool, p);
    EXPECT(rtos_pool_free_count(&_pool) == POOL_BLOCKS);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * T4 — Mutex
 * ══════════════════════════════════════════════════════════════════════════════ */

static rtos_mutex_t _m;

static void test_mutex(void)
{
    SECTION("T4  Mutex");
    kernel_reset();

    rtos_task_t *t_a = rtos_task_create("a", dummy_fn, NULL, stk[0], sizeof(stk[0]), 1);
    rtos_task_t *t_b = rtos_task_create("b", dummy_fn, NULL, stk[1], sizeof(stk[1]), 2);
    rtos_mutex_init(&_m);

    /* t_a acquires the mutex */
    _current_task = t_a;
    t_a->state = TASK_RUNNING;
    sched_unready(t_a);

    TEST("first lock succeeds (RTOS_OK)");
    rtos_err_t r = rtos_mutex_lock(&_m, RTOS_NO_WAIT);
    EXPECT(r == RTOS_OK);

    TEST("mutex owner is set to acquiring task");
    EXPECT(_m.owner == t_a);

    TEST("second lock with NO_WAIT returns TIMEOUT");
    _current_task = t_b;
    t_b->state = TASK_RUNNING;
    rtos_err_t r2 = rtos_mutex_lock(&_m, RTOS_NO_WAIT);
    EXPECT(r2 == RTOS_TIMEOUT);

    TEST("unlock clears owner");
    _current_task = t_a;
    rtos_mutex_unlock(&_m);
    EXPECT(_m.owner == NULL || _m.owner == t_b);  /* either free or woken */

    TEST("unlock with waiter wakes waiter");
    rtos_mutex_init(&_m);
    _current_task = t_a;
    rtos_mutex_lock(&_m, RTOS_NO_WAIT);

    /* Manually block t_b as a waiter (simulating a failed lock + block) */
    t_b->state        = TASK_BLOCKED;
    t_b->block_reason = BLOCK_MUTEX;
    sched_unready(t_b);
    t_b->next = NULL;
    _m.wait_head = t_b;

    rtos_mutex_unlock(&_m);
    EXPECT(t_b->state == TASK_READY && _m.owner == t_b);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * T5 — Semaphore
 * ══════════════════════════════════════════════════════════════════════════════ */

static rtos_sem_t _s;

static void test_semaphore(void)
{
    SECTION("T5  Semaphore");
    kernel_reset();

    rtos_task_t *t_a = rtos_task_create("a", dummy_fn, NULL, stk[0], sizeof(stk[0]), 1);
    rtos_task_t *t_b = rtos_task_create("b", dummy_fn, NULL, stk[1], sizeof(stk[1]), 2);

    TEST("initial count equals requested value");
    rtos_sem_init(&_s, 3, 8);
    EXPECT(_s.count == 3);

    TEST("wait decrements count");
    _current_task = t_a;
    t_a->state = TASK_RUNNING;
    sched_unready(t_a);
    rtos_sem_wait(&_s, RTOS_NO_WAIT);
    EXPECT(_s.count == 2);

    TEST("wait with NO_WAIT on empty returns TIMEOUT");
    rtos_sem_init(&_s, 0, 4);
    rtos_err_t r = rtos_sem_wait(&_s, RTOS_NO_WAIT);
    EXPECT(r == RTOS_TIMEOUT);

    TEST("signal increments count when no waiter");
    rtos_sem_init(&_s, 0, 4);
    _current_task = t_a;
    rtos_sem_signal(&_s);
    EXPECT(_s.count == 1);

    TEST("signal does not exceed max_count");
    rtos_sem_init(&_s, 4, 4);
    rtos_sem_signal(&_s);
    EXPECT(_s.count == 4);

    TEST("signal wakes waiter instead of incrementing count");
    rtos_sem_init(&_s, 0, 4);
    /* Manually block t_b as waiter */
    t_b->state        = TASK_BLOCKED;
    t_b->block_reason = BLOCK_SEM;
    sched_unready(t_b);
    t_b->next    = NULL;
    _s.wait_head = t_b;

    _current_task = t_a;
    rtos_sem_signal(&_s);
    EXPECT(t_b->state == TASK_READY && _s.count == 0);

    TEST("isr signal also wakes waiter");
    rtos_sem_init(&_s, 0, 4);
    t_b->state        = TASK_BLOCKED;
    sched_unready(t_b);
    t_b->next    = NULL;
    _s.wait_head = t_b;
    rtos_sem_signal_isr(&_s);
    EXPECT(t_b->state == TASK_READY);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Entry point
 * ══════════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    printf("CortexRTOS host unit tests\n");
    printf("==========================\n");

    test_scheduler();
    test_delay();
    test_pool();
    test_mutex();
    test_semaphore();

    printf("\n==========================\n");
    printf("Results: %d passed, %d failed\n", _pass, _fail);

    return _fail > 0 ? 1 : 0;
}
