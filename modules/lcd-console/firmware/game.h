#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/*
 * Breakout for PIC18F4550 / 128×64 KS0108 LCD.
 *
 * Screen layout:
 *
 *   y =  0..7   Status bar: "SC:NNNN HI:NNNN L:N"
 *   y =  8       Separator line
 *   y =  9..33  Brick grid (5 rows × 8 columns)
 *   y = 34..55  Open play field
 *   y = 56..58  Paddle
 *   y = 59..63  Dead zone (ball lost if it passes here)
 *
 * Brick geometry:
 *   BRICK_W 14, BRICK_H 4, BRICK_GAP_X 2, BRICK_GAP_Y 1
 *   8 columns: x = 1, 17, 33, 49, 65, 81, 97, 113
 *   5 rows:    y = 9, 14, 19, 24, 29
 *
 * Paddle: 20 px wide, 3 px tall, y = 56
 * Ball:    3 px wide, 3 px tall
 *
 * Scoring: level × 10 points per brick.
 * Lives:   3 at game start, decremented on each ball loss.
 * Levels:  clearing all bricks advances the level; ball speed increases.
 */

void game_init(void);
void game_tick(void);    /* advance one game frame at 30 Hz */
void game_render(void);  /* draw current state into lcd_fb  */

#endif /* GAME_H */
