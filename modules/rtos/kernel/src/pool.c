/*
 * pool.c — fixed-size block memory pool allocator.
 *
 * The pool manages a caller-supplied buffer divided into equal-sized blocks.
 * Free blocks are threaded into a singly-linked free list embedded inside
 * the blocks themselves (the first word of each free block is a pointer to
 * the next free block).  alloc and free are therefore O(1) with no metadata
 * overhead beyond the single free-list head pointer.
 *
 * Block size is rounded up to sizeof(void*) so the embedded pointer fits.
 * The buffer is assumed to be suitably aligned (e.g. __attribute__((aligned(8)))).
 */

#include "rtos.h"
#include "rtos_objects.h"

void rtos_pool_init(rtos_pool_t *pool, void *buf, size_t buf_bytes, size_t block_size)
{
    /* Round block_size up to pointer alignment */
    size_t align = sizeof(void *);
    block_size   = (block_size + align - 1u) & ~(align - 1u);
    if (block_size < sizeof(void *)) block_size = sizeof(void *);

    size_t n = buf_bytes / block_size;

    pool->block_size   = block_size;
    pool->total_blocks = n;
    pool->free_blocks  = n;
    pool->free_head    = NULL;

    /* Thread all blocks into the free list */
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < n; i++) {
        void **link = (void **)p;
        *link       = pool->free_head;
        pool->free_head = p;
        p += block_size;
    }
}

void *rtos_pool_alloc(rtos_pool_t *pool)
{
    uint32_t primask = rtos_enter_critical();

    if (pool->free_head == NULL) {
        rtos_exit_critical(primask);
        return NULL;
    }

    void *block         = pool->free_head;
    pool->free_head     = *(void **)block;   /* advance free list */
    pool->free_blocks--;

    rtos_exit_critical(primask);
    return block;
}

void rtos_pool_free(rtos_pool_t *pool, void *block)
{
    if (block == NULL) return;

    uint32_t primask = rtos_enter_critical();

    *(void **)block = pool->free_head;
    pool->free_head = block;
    pool->free_blocks++;

    rtos_exit_critical(primask);
}

size_t rtos_pool_free_count(const rtos_pool_t *pool)
{
    return pool->free_blocks;
}
