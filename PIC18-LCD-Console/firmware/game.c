#include "game.h"
#include "gfx.h"
#include "lcd.h"
#include "buttons.h"
#include "eeprom.h"
#include "font.h"

#include <stdint.h>

/* ---- layout constants ---------------------------------------------------- */
#define SCORE_H       8    /* height of status bar in pixels    */
#define PLAY_TOP      9    /* first pixel row of the play field */

#define BRICK_COLS    8
#define BRICK_ROWS    5
#define BRICK_W      14
#define BRICK_H       4
#define BRICK_GAP_X   2
#define BRICK_GAP_Y   1
#define BRICK_OFF_X   1    /* left margin of first column       */

#define PADDLE_W     20
#define PADDLE_H      3
#define PADDLE_Y     56
#define PADDLE_SPEED  3
#define PADDLE_MIN    1
#define PADDLE_MAX   (LCD_W - PADDLE_W - 1)

#define BALL_SIZE     3
#define BALL_INIT_DX  2
#define BALL_INIT_DY  2    /* positive = downward; negated on launch */

#define LIVES_START   3
#define DEAD_FRAMES  30    /* ~1 s pause after losing a life   */
#define LEVEL_FRAMES 45    /* ~1.5 s pause between levels      */
#define TITLE_BLINK  15    /* "PRESS FIRE" blink half-period   */

/* ---- brick row y-position ------------------------------------------------ */
#define BRICK_Y(row)  ((int8_t)(PLAY_TOP + (row) * (BRICK_H + BRICK_GAP_Y)))
/* ---- brick col x-position ------------------------------------------------ */
#define BRICK_X(col)  ((int8_t)(BRICK_OFF_X + (col) * (BRICK_W + BRICK_GAP_X)))

/* ---- game state ----------------------------------------------------------- */
typedef enum {
    GS_TITLE    = 0,
    GS_LAUNCH,       /* ball resting on paddle, awaiting FIRE */
    GS_PLAYING,
    GS_DEAD,         /* brief pause after ball lost           */
    GS_LEVEL_UP,     /* brief pause after level cleared       */
    GS_GAME_OVER
} game_state_t;

/* ---- module state --------------------------------------------------------- */
static game_state_t state;

static int8_t  ball_x, ball_y;
static int8_t  ball_dx, ball_dy;
static int8_t  paddle_x;

static uint8_t  bricks[BRICK_ROWS][BRICK_COLS];
static uint8_t  bricks_left;

static uint16_t score;
static uint16_t hi_score;
static uint8_t  lives;
static uint8_t  level;

static uint8_t  timer;        /* countdown for DEAD / LEVEL_UP states */
static uint8_t  blink_timer;  /* for flashing "PRESS FIRE"            */
static uint8_t  blink_on;

/* ---- helpers ------------------------------------------------------------- */
static void reset_bricks(void)
{
    uint8_t r, c;
    for (r = 0; r < BRICK_ROWS; r++)
        for (c = 0; c < BRICK_COLS; c++)
            bricks[r][c] = 1;
    bricks_left = BRICK_ROWS * BRICK_COLS;
}

static void spawn_ball(void)
{
    /* Place ball just above the paddle centre. */
    ball_x  = paddle_x + PADDLE_W / 2 - BALL_SIZE / 2;
    ball_y  = PADDLE_Y - BALL_SIZE - 1;
    /* Angle chosen so the ball goes up-right on every spawn. */
    ball_dx =  BALL_INIT_DX;
    ball_dy = -BALL_INIT_DY;
    state   = GS_LAUNCH;
}

/* ---- brick collision ------------------------------------------------------ */
static void check_bricks(void)
{
    uint8_t r, c;

    for (r = 0; r < BRICK_ROWS; r++) {
        for (c = 0; c < BRICK_COLS; c++) {
            int8_t bx, by, ox, oy;

            if (!bricks[r][c]) continue;

            bx = BRICK_X(c);
            by = BRICK_Y(r);

            /* AABB overlap test */
            if (ball_x + BALL_SIZE <= bx) continue;
            if (ball_x >= bx + BRICK_W)   continue;
            if (ball_y + BALL_SIZE <= by)  continue;
            if (ball_y >= by + BRICK_H)    continue;

            /* Resolve: penetration depth on each axis determines bounce axis.
             * Use the smaller overlap depth to select the nearest face. */
            ox = ball_x + BALL_SIZE - bx;
            if (bx + BRICK_W - ball_x < ox)
                ox = bx + BRICK_W - ball_x;

            oy = ball_y + BALL_SIZE - by;
            if (by + BRICK_H - ball_y < oy)
                oy = by + BRICK_H - ball_y;

            if (oy <= ox)
                ball_dy = -ball_dy;
            else
                ball_dx = -ball_dx;

            bricks[r][c] = 0;
            bricks_left--;
            score += (uint16_t)level * 10u;

            if (bricks_left == 0) {
                timer = LEVEL_FRAMES;
                state = GS_LEVEL_UP;
            }

            return;   /* one brick per frame avoids double-flip */
        }
    }
}

/* ---- ball movement ------------------------------------------------------- */
static void move_ball(void)
{
    int8_t nx = ball_x + ball_dx;
    int8_t ny = ball_y + ball_dy;

    /* Side walls */
    if (nx < 1) {
        nx     = 1;
        ball_dx = -ball_dx;
    } else if (nx + BALL_SIZE > LCD_W - 1) {
        nx     = LCD_W - 1 - BALL_SIZE;
        ball_dx = -ball_dx;
    }

    /* Top wall (below status bar) */
    if (ny < PLAY_TOP) {
        ny     = PLAY_TOP;
        ball_dy = -ball_dy;
    }

    /* Ball lost off the bottom */
    if (ny + BALL_SIZE > LCD_H) {
        lives--;
        timer = DEAD_FRAMES;
        state = GS_DEAD;
        return;
    }

    ball_x = nx;
    ball_y = ny;

    /* Paddle collision — only when ball is moving downward */
    if (ball_dy > 0 &&
        ball_y + BALL_SIZE >= PADDLE_Y &&
        ball_y             <  PADDLE_Y + PADDLE_H &&
        ball_x + BALL_SIZE >  paddle_x &&
        ball_x             <  paddle_x + PADDLE_W)
    {
        ball_dy  = -ball_dy;
        ball_y   =  PADDLE_Y - BALL_SIZE;

        /* Vary horizontal angle based on hit position relative to paddle centre.
         * Left third: force left,  right third: force right,  centre: keep dx. */
        {
            int8_t rel = ball_x + (BALL_SIZE / 2) - paddle_x;
            if      (rel < PADDLE_W / 3)
                ball_dx = -BALL_INIT_DX;
            else if (rel > (2 * PADDLE_W) / 3)
                ball_dx =  BALL_INIT_DX;
        }
    }

    check_bricks();
}

/* ---- paddle movement ----------------------------------------------------- */
static void move_paddle(void)
{
    if (btn_held(BTN_LEFT)) {
        paddle_x -= PADDLE_SPEED;
        if (paddle_x < PADDLE_MIN) paddle_x = PADDLE_MIN;
    }
    if (btn_held(BTN_RIGHT)) {
        paddle_x += PADDLE_SPEED;
        if (paddle_x > PADDLE_MAX) paddle_x = PADDLE_MAX;
    }
}

/* ---- public API ---------------------------------------------------------- */
void game_init(void)
{
    hi_score = eeprom_load_hiscore();
    score    = 0;
    lives    = LIVES_START;
    level    = 1;
    paddle_x = (LCD_W - PADDLE_W) / 2;

    reset_bricks();
    spawn_ball();

    blink_timer = 0;
    blink_on    = 1;
    state       = GS_TITLE;
}

void game_tick(void)
{
    /* Blink timer advances every frame regardless of state. */
    blink_timer++;
    if (blink_timer >= TITLE_BLINK) {
        blink_timer = 0;
        blink_on    = !blink_on;
    }

    switch (state) {

    case GS_TITLE:
        if (btn_pressed(BTN_FIRE)) {
            score    = 0;
            lives    = LIVES_START;
            level    = 1;
            paddle_x = (LCD_W - PADDLE_W) / 2;
            reset_bricks();
            spawn_ball();
            /* spawn_ball() sets state = GS_LAUNCH */
        }
        break;

    case GS_LAUNCH:
        move_paddle();
        /* Ball rides the paddle centre. */
        ball_x = paddle_x + PADDLE_W / 2 - BALL_SIZE / 2;
        ball_y = PADDLE_Y - BALL_SIZE - 1;
        if (btn_pressed(BTN_FIRE))
            state = GS_PLAYING;
        break;

    case GS_PLAYING:
        move_paddle();
        move_ball();
        break;

    case GS_DEAD:
        if (--timer == 0) {
            if (lives == 0) {
                if (score > hi_score) {
                    hi_score = score;
                    eeprom_save_hiscore(hi_score);
                }
                state = GS_GAME_OVER;
            } else {
                spawn_ball();
            }
        }
        break;

    case GS_LEVEL_UP:
        if (--timer == 0) {
            level++;
            /* Cap speed increase: after level 3 the ball stays at 3 px/frame. */
            if (level <= 3) {
                ball_dx = (ball_dx > 0) ?  (int8_t)level : -(int8_t)level;
                ball_dy = (ball_dy > 0) ?  (int8_t)level : -(int8_t)level;
            }
            paddle_x = (LCD_W - PADDLE_W) / 2;
            reset_bricks();
            spawn_ball();
        }
        break;

    case GS_GAME_OVER:
        if (btn_pressed(BTN_FIRE)) {
            score    = 0;
            lives    = LIVES_START;
            level    = 1;
            paddle_x = (LCD_W - PADDLE_W) / 2;
            reset_bricks();
            spawn_ball();
        }
        break;
    }
}

/* ---- rendering ----------------------------------------------------------- */

/* Draw the status bar: score, hi-score, lives. */
static void render_status(void)
{
    int8_t x = 0;
    x = gfx_str(x, 0, "SC:", 1);
    x = gfx_uint_pad(x, 0, score, 4, 1);
    x = gfx_str(x, 0, " HI:", 1);
    x = gfx_uint_pad(x, 0, hi_score, 4, 1);
    x = gfx_str(x, 0, " L:", 1);
    gfx_uint(x, 0, lives, 1);

    /* Separator line just below the status bar. */
    gfx_hline(0, SCORE_H, LCD_W, 1);
}

/* Draw all live bricks. */
static void render_bricks(void)
{
    uint8_t r, c;
    for (r = 0; r < BRICK_ROWS; r++) {
        for (c = 0; c < BRICK_COLS; c++) {
            if (!bricks[r][c]) continue;
            gfx_fill_rect(BRICK_X(c), BRICK_Y(r), BRICK_W, BRICK_H, 1);
            /* Single-pixel highlight along the top edge for a raised look. */
            gfx_hline(BRICK_X(c) + 1, BRICK_Y(r), BRICK_W - 2, 0);
        }
    }
}

/* Draw paddle and ball. */
static void render_entities(void)
{
    gfx_fill_rect(paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, 1);
    gfx_fill_rect(ball_x,   ball_y,   BALL_SIZE, BALL_SIZE, 1);
}

/* Centre-align a string in the given row. */
static int8_t centre_x(const char *s)
{
    uint8_t len = 0;
    while (s[len]) len++;
    return (int8_t)((LCD_W - (int8_t)len * (FONT_W + FONT_GAP)) / 2);
}

void game_render(void)
{
    lcd_clear_fb();

    switch (state) {

    case GS_TITLE:
        gfx_str(centre_x("BREAKOUT"), 20, "BREAKOUT", 1);
        if (blink_on)
            gfx_str(centre_x("PRESS FIRE"), 38, "PRESS FIRE", 1);
        break;

    case GS_LAUNCH:
    case GS_PLAYING:
    case GS_DEAD:
        render_status();
        render_bricks();
        render_entities();
        break;

    case GS_LEVEL_UP:
        render_status();
        render_bricks();
        {
            char buf[8] = "LEVEL ";        /* 6 chars + digit + NUL */
            buf[6] = (char)('0' + level);
            buf[7] = '\0';
            gfx_str(centre_x("LEVEL 1"), 28, buf, 1);
        }
        break;

    case GS_GAME_OVER:
        render_status();
        gfx_str(centre_x("GAME OVER"), 20, "GAME OVER", 1);
        gfx_str(2, 30, "SC:", 1);
        gfx_uint_pad(20, 30, score, 4, 1);
        gfx_str(2, 38, "HI:", 1);
        gfx_uint_pad(20, 38, hi_score, 4, 1);
        if (blink_on)
            gfx_str(centre_x("PRESS FIRE"), 52, "PRESS FIRE", 1);
        break;
    }
}
