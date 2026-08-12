#include "gestures.h"
#include "touch.h"
#include "touch_hw.h"

#include <stdint.h>
#include <string.h>

/* ---- Event queue ---------------------------------------------------------- */

#define QUEUE_DEPTH  4u

static gesture_event_t queue[QUEUE_DEPTH];
static uint8_t         q_head;   /* index of next slot to read  */
static uint8_t         q_tail;   /* index of next slot to write */
static uint8_t         q_count;

static void enqueue(gesture_t type, uint8_t ch)
{
    queue[q_tail].type    = type;
    queue[q_tail].channel = ch;
    q_tail = (uint8_t)((q_tail + 1u) % QUEUE_DEPTH);
    if (q_count < QUEUE_DEPTH) {
        q_count++;
    } else {
        /* Overwrite: advance head to discard oldest event. */
        q_head = (uint8_t)((q_head + 1u) % QUEUE_DEPTH);
    }
}

gesture_event_t gestures_get(void)
{
    gesture_event_t ev = { GESTURE_NONE, 0u };
    if (q_count == 0u) return ev;
    ev      = queue[q_head];
    q_head  = (uint8_t)((q_head + 1u) % QUEUE_DEPTH);
    q_count--;
    return ev;
}

/* ---- Per-channel tap/hold state ------------------------------------------ */

typedef enum {
    CS_IDLE = 0,
    CS_DOWN,       /* active, timer running          */
    CS_HOLD_FIRED  /* hold gesture already emitted   */
} ch_state_t;

static ch_state_t ch_state[TOUCH_CH_COUNT];
static uint8_t    ch_timer[TOUCH_CH_COUNT];   /* ticks since channel activated */

/* ---- Swipe detection state ------------------------------------------------ */

/*
 * Swipe is detected by tracking the most recently activated channel and the
 * tick at which it became active.  If the next channel to activate is adjacent
 * and the interval is within SWIPE_MAX_TICKS, a swipe is emitted.
 */
static uint8_t swipe_last_ch;    /* channel that activated most recently */
static uint8_t swipe_timer;      /* ticks since swipe_last_ch activated  */
static uint8_t swipe_tracking;   /* 1 if a potential swipe is in progress */

/* ---- Public API ----------------------------------------------------------- */

void gestures_init(void)
{
    memset(ch_state, 0, sizeof(ch_state));
    memset(ch_timer, 0, sizeof(ch_timer));

    q_head         = 0u;
    q_tail         = 0u;
    q_count        = 0u;
    swipe_last_ch  = 0xFFu;
    swipe_timer    = 0u;
    swipe_tracking = 0u;
}

void gestures_update(void)
{
    uint8_t ch;

    /* Advance swipe inter-channel timer. */
    if (swipe_tracking && swipe_timer < 0xFFu)
        swipe_timer++;

    for (ch = 0u; ch < TOUCH_CH_COUNT; ch++) {
        uint8_t active = touch_active(ch);

        switch (ch_state[ch]) {

        case CS_IDLE:
            if (active) {
                ch_state[ch] = CS_DOWN;
                ch_timer[ch] = 0u;

                /* Swipe entry: check if this channel is adjacent to the
                 * previously activated one and within the time window. */
                if (swipe_tracking && swipe_timer <= SWIPE_MAX_TICKS) {
                    int8_t diff = (int8_t)ch - (int8_t)swipe_last_ch;
                    if (diff == 1) {
                        enqueue(GESTURE_SWIPE_RIGHT, swipe_last_ch);
                        swipe_tracking = 0u;
                    } else if (diff == -1) {
                        enqueue(GESTURE_SWIPE_LEFT, ch);
                        swipe_tracking = 0u;
                    }
                }

                /* Start new swipe window from this channel. */
                swipe_last_ch  = ch;
                swipe_timer    = 0u;
                swipe_tracking = 1u;
            }
            break;

        case CS_DOWN:
            if (active) {
                if (ch_timer[ch] < 0xFFu)
                    ch_timer[ch]++;

                /* Hold: threshold exceeded while still active. */
                if (ch_timer[ch] >= HOLD_MIN_TICKS) {
                    enqueue(GESTURE_HOLD, ch);
                    ch_state[ch] = CS_HOLD_FIRED;
                }
            } else {
                /* Released before hold threshold — emit tap if within window. */
                if (ch_timer[ch] <= TAP_MAX_TICKS)
                    enqueue(GESTURE_TAP, ch);
                ch_state[ch] = CS_IDLE;
            }
            break;

        case CS_HOLD_FIRED:
            /* Hold already emitted.  Wait for release; no further events. */
            if (!active)
                ch_state[ch] = CS_IDLE;
            break;
        }
    }

    /* Expire swipe window if it has been open too long without a follow-up. */
    if (swipe_tracking && swipe_timer > SWIPE_MAX_TICKS + 2u)
        swipe_tracking = 0u;
}
