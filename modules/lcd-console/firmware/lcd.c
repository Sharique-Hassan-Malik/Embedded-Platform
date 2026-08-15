#include "lcd.h"

#ifndef _XTAL_FREQ
#define _XTAL_FREQ 8000000UL    /* 8 MHz — must match system_init() for __delay_*() */
#endif

#include <xc.h>
#include <stdint.h>
#include <string.h>

/* ---- pin aliases --------------------------------------------------------- */
#define DATA_PORT   LATD
#define DATA_TRIS   TRISD

#define RS_PIN      LATEbits.LATE0
#define RW_PIN      LATEbits.LATE1
#define E_PIN       LATEbits.LATE2

#define CS1_PIN     LATCbits.LATC0
#define CS2_PIN     LATCbits.LATC1

/* ---- KS0108 commands ----------------------------------------------------- */
#define CMD_DISPLAY_ON   0x3F
#define CMD_DISPLAY_OFF  0x3E
#define CMD_SET_COL      0x40   /* OR with column 0–63     */
#define CMD_SET_PAGE     0xB8   /* OR with page  0–7       */
#define CMD_START_LINE   0xC0   /* OR with line  0–63      */

/* ---- timing helpers ------------------------------------------------------ */
/* At 8 MHz one instruction cycle = 500 ns.
 * KS0108 requires E high ≥ 450 ns and E low ≥ 450 ns.
 * Four NOPs = 2 µs comfortably meets both margins. */
#define LCD_DELAY()  do { \
    NOP(); NOP(); NOP(); NOP(); \
} while (0)

/* ---- framebuffer --------------------------------------------------------- */
uint8_t lcd_fb[8][LCD_W];

/* ---- internal helpers ---------------------------------------------------- */
static void e_pulse(void)
{
    E_PIN = 1;
    LCD_DELAY();
    E_PIN = 0;
    LCD_DELAY();
}

static void write_cmd(uint8_t cmd)
{
    RS_PIN    = 0;   /* command */
    RW_PIN    = 0;   /* write   */
    DATA_PORT = cmd;
    e_pulse();
}

static void write_data(uint8_t data)
{
    RS_PIN    = 1;   /* data  */
    RW_PIN    = 0;   /* write */
    DATA_PORT = data;
    e_pulse();
}

/* Select CS1, CS2 or both (for broadcast commands). */
static void select(uint8_t cs1, uint8_t cs2)
{
    CS1_PIN = cs1;
    CS2_PIN = cs2;
    LCD_DELAY();
}

static void init_controller(void)
{
    write_cmd(CMD_DISPLAY_ON);
    write_cmd(CMD_START_LINE | 0);
}

/* ---- public API ---------------------------------------------------------- */
void lcd_init(void)
{
    /* Data bus as output. */
    DATA_TRIS  = 0x00;
    DATA_PORT  = 0x00;

    /* PORTD is digital: the PIC18F4550 has no ANSELD register; analogue
       inputs are disabled globally via ADCON1 (= 0x0F) in system_init(). */

    /* Control pins as output. */
    TRISEbits.TRISE0 = 0;   /* RS  */
    TRISEbits.TRISE1 = 0;   /* R/W */
    TRISEbits.TRISE2 = 0;   /* E   */
    TRISCbits.TRISC0 = 0;   /* CS1 */
    TRISCbits.TRISC1 = 0;   /* CS2 */

    /* PORTE is digital: no ANSELE on the PIC18F4550; ADCON1 (= 0x0F) in
       system_init() disables all analogue inputs. */

    /* Deselect, E low. */
    CS1_PIN = 0;
    CS2_PIN = 0;
    E_PIN   = 0;
    RW_PIN  = 0;

    /* Short power-on delay. */
    __delay_ms(20);

    /* Initialise both controllers independently. */
    select(1, 0);
    init_controller();
    select(0, 1);
    init_controller();
    select(0, 0);

    lcd_clear_fb();
    lcd_flush();
}

void lcd_clear_fb(void)
{
    memset(lcd_fb, 0, sizeof(lcd_fb));
}

void lcd_flush(void)
{
    uint8_t page, col;

    for (page = 0; page < 8; page++) {
        /* ---- left half (CS1, columns 0–63) ---- */
        select(1, 0);
        write_cmd(CMD_SET_PAGE | page);
        write_cmd(CMD_SET_COL  | 0);
        for (col = 0; col < 64; col++)
            write_data(lcd_fb[page][col]);

        /* ---- right half (CS2, columns 64–127, mapped to chip col 0–63) ---- */
        select(0, 1);
        write_cmd(CMD_SET_PAGE | page);
        write_cmd(CMD_SET_COL  | 0);
        for (col = 64; col < 128; col++)
            write_data(lcd_fb[page][col]);
    }

    select(0, 0);
}
