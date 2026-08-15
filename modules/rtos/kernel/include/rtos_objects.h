/*
 * rtos_objects.h — internal struct definitions for synchronisation objects.
 *
 * This header is included by kernel .c files, by host unit tests that need to
 * inspect object state, and by the demo application in app/, which allocates
 * synchronisation objects statically and therefore needs their size.
 *
 * Application code *outside this repository* should not include it: use the
 * opaque handles from rtos_types.h and let the kernel allocate. The demo is
 * part of the kernel and is held to a different line.
 */

#ifndef RTOS_OBJECTS_H
#define RTOS_OBJECTS_H

#include "rtos_types.h"

struct rtos_mutex {
    rtos_task_t *owner;
    rtos_task_t *wait_head;
};

struct rtos_sem {
    volatile uint32_t count;
    uint32_t          max_count;
    rtos_task_t      *wait_head;
};

struct rtos_pool {
    void   *free_head;
    size_t  block_size;
    size_t  total_blocks;
    size_t  free_blocks;
};

#endif /* RTOS_OBJECTS_H */
