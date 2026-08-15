#pragma once
#include <Arduino.h>
#include "config.h"

// Lock-free ring buffer for transferring samples from the Timer1 ISR
// (producer) to the main loop serial sender (consumer).
//
// Safety model: RING_BUF_SIZE is a power of two; head and tail are uint16_t.
// The ISR writes head; the main loop reads tail. On AVR, reading/writing a
// uint16_t is not atomic (two 8-bit operations). We use volatile and
// mask-based wrap so the consumer never sees a partial update of the index.
// The ISR increments head only after writing the sample; the consumer reads
// head and tail with interrupts disabled for the index comparison.

struct Sample {
    int32_t  current_0p1mA;    // signed: negative if current flows backwards
    uint8_t  ann_mask;          // annotation pin bitmask at sample time
};

// The buffer is the largest thing this firmware allocates, so it is the thing
// most likely to be sized for a part it is not running on.
static_assert(sizeof(Sample) * RING_BUF_SIZE <= RING_BUF_BUDGET,
              "ring buffer does not fit in the RAM budget — reduce RING_BUF_SIZE");
static_assert((RING_BUF_SIZE & (RING_BUF_SIZE - 1)) == 0,
              "RING_BUF_SIZE must be a power of two: the wrap is a mask, not a modulo");

class RingBuffer {
public:
    void clear() {
        cli();
        _head = _tail = 0;
        sei();
    }

    // ISR context: push a sample. Drops if full (overflow flag set).
    void push(const Sample &s) {
        uint16_t next = (_head + 1) & (RING_BUF_SIZE - 1);
        if (next == _tail) {
            _overflow = true;
            return;
        }
        _buf[_head] = s;
        _head       = next;
    }

    // Main loop context: pop one sample into *out. Returns false if empty.
    bool pop(Sample *out) {
        uint16_t h;
        cli();
        h = _head;
        sei();
        if (_tail == h) return false;
        *out  = _buf[_tail];
        _tail = (_tail + 1) & (RING_BUF_SIZE - 1);
        return true;
    }

    uint16_t available() const {
        uint16_t h;
        cli();
        h = _head;
        sei();
        return (h - _tail) & (RING_BUF_SIZE - 1);
    }

    bool overflow() const        { return _overflow; }
    void clearOverflow()         { _overflow = false; }

private:
    volatile uint16_t _head = 0;
    volatile uint16_t _tail = 0;
    volatile bool     _overflow = false;
    Sample            _buf[RING_BUF_SIZE];
};
