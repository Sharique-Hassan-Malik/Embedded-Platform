#ifndef GESTURES_H
#define GESTURES_H

#include <stdint.h>

/*
 * Gesture recognition layer built on top of touch_scan().
 *
 * Detects three gesture types across the TOUCH_CH_COUNT electrode array:
 *
 *   TAP   — a channel activates then releases within TAP_MAX_TICKS.
 *   HOLD  — a channel stays active for at least HOLD_MIN_TICKS.
 *   SWIPE — two adjacent channels activate in sequence within SWIPE_MAX_TICKS,
 *            giving a direction (LEFT or RIGHT along the channel axis).
 *
 * All timing is in scan ticks.  At 30 scans/s:
 *   TAP_MAX_TICKS  = 15 →  500 ms maximum tap duration
 *   HOLD_MIN_TICKS = 30 → 1 000 ms minimum hold duration
 *   SWIPE_MAX_TICKS = 8 →  267 ms maximum inter-channel transition time
 *
 * Adjust the constants below to suit electrode size, scan rate and
 * application responsiveness requirements.
 *
 * Usage:
 *   gestures_init();
 *   loop:
 *     touch_scan();
 *     gestures_update();   ← call after every touch_scan()
 *     e = gestures_get();  ← returns GESTURE_NONE until an event fires
 */

#define TAP_MAX_TICKS    15u
#define HOLD_MIN_TICKS   30u
#define SWIPE_MAX_TICKS   8u

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP,           /* brief touch on one channel         */
    GESTURE_HOLD,          /* sustained touch on one channel     */
    GESTURE_SWIPE_LEFT,    /* higher-index to lower-index        */
    GESTURE_SWIPE_RIGHT    /* lower-index to higher-index        */
} gesture_t;

typedef struct {
    gesture_t   type;
    uint8_t     channel;   /* channel that triggered the gesture */
} gesture_event_t;

/* Initialise gesture state.  Call once before the scan loop. */
void gestures_init(void);

/*
 * Update gesture state machine.  Must be called once per scan cycle,
 * immediately after touch_scan().
 */
void gestures_update(void);

/*
 * Return the oldest pending gesture event and remove it from the queue.
 * Returns a GESTURE_NONE event when there are no pending gestures.
 * The internal queue depth is 4 events; older events are overwritten if
 * the caller does not drain the queue.
 */
gesture_event_t gestures_get(void);

#endif /* GESTURES_H */
