/*
 * dup_table.h — recent-packet cache for duplicate suppression.
 *
 * Each received packet is identified by (origin, seq, type).  The table
 * stores the last DUP_TABLE_SIZE such tuples in a circular buffer.
 * A packet is a duplicate if the same tuple appears anywhere in the buffer.
 *
 * HELLO packets from the same source but with a newer seq are NOT
 * duplicates and must be re-processed to update routing state.
 */

#ifndef DUP_TABLE_H
#define DUP_TABLE_H

#include <stdint.h>
#include <stdbool.h>
#include "packet.h"

typedef struct {
    uint8_t origin;
    uint8_t seq;
    uint8_t type;
} DupEntry;

typedef struct {
    DupEntry entries[DUP_TABLE_SIZE];
    uint8_t  head;   /* next write position (circular) */
} DupTable;

static inline void dup_init(DupTable *dt)
{
    dt->head = 0;
    for (uint8_t i = 0; i < DUP_TABLE_SIZE; i++)
        dt->entries[i].origin = ADDR_NONE;
}

/* Returns true if (origin, seq, type) was seen recently; adds it if not. */
static inline bool dup_check_and_add(DupTable *dt,
                                      uint8_t origin, uint8_t seq, uint8_t type)
{
    for (uint8_t i = 0; i < DUP_TABLE_SIZE; i++) {
        const DupEntry *e = &dt->entries[i];
        if (e->origin == origin && e->seq == seq && e->type == type)
            return true;   /* duplicate */
    }
    dt->entries[dt->head] = (DupEntry){ origin, seq, type };
    dt->head = (dt->head + 1) % DUP_TABLE_SIZE;
    return false;
}

#endif /* DUP_TABLE_H */
