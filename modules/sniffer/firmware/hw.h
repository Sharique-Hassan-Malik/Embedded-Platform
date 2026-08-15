#ifndef HW_H
#define HW_H

/*
 * hw.h — Pin assignments and constants for the PIC24FJ64GA002 protocol
 *         sniffer.
 *
 * Target : PIC24FJ64GA002, 16 MHz (FRC × 4 PLL → Fcy = 16 MHz)
 *
 * ── UART sniffer (passive, input only) ──────────────────────────────────
 *   RP6 / RB6   SNIFF_UART_RX    connect to target TX line
 *   (sniffer only captures TX side; add second input for bidirectional)
 *
 * ── I2C sniffer (passive, input only) ───────────────────────────────────
 *   RB8         SNIFF_I2C_SCL    connect to target SCL
 *   RB9         SNIFF_I2C_SDA    connect to target SDA
 *   (change-notification interrupts on both pins; bit-bang capture)
 *
 * ── SPI sniffer (passive, input only) ───────────────────────────────────
 *   RP10 / RB10  SNIFF_SPI_SCK   connect to target SCK
 *   RP11 / RB11  SNIFF_SPI_MOSI  connect to target MOSI
 *   RP12 / RB12  SNIFF_SPI_MISO  connect to target MISO
 *   RB13         SNIFF_SPI_CS    connect to target /CS (active low)
 *
 * ── SD card (SPI2) ────────────────────────────────────────────────────
 *   RP14 / RB14  SD_SCK
 *   RP15 / RB15  SD_MOSI (SDO2)
 *   RB4           SD_MISO (SDI2)
 *   RB5           SD_CS
 *
 * ── Host UART (output only, to USB-serial adapter) ──────────────────────
 *   RP2 / RB2   HOST_TX  (115 200 baud)
 *
 * ── Status LED ──────────────────────────────────────────────────────────
 *   RA0          LED (active high)
 */

#include <xc.h>
#include <stdint.h>

/* Peripheral bus clock */
#define FCY  16000000UL

/* ---- Host UART --------------------------------------------------------- */
#define HOST_BAUD        115200UL
#define HOST_TX_RP       2u          /* RP2 = RB2 */

/* ---- UART sniffer ------------------------------------------------------ */
#define SNIFF_UART_RX_RP  6u         /* RP6 = RB6 */

/* ---- I2C sniffer pins (CN-capable, no remappable peripheral needed) ---- */
#define I2C_SCL_BIT   8u             /* RB8  CN6 */
#define I2C_SDA_BIT   9u             /* RB9  CN7 */
#define I2C_SCL_PORT  PORTBbits.RB8
#define I2C_SDA_PORT  PORTBbits.RB9

/* ---- SPI sniffer ------------------------------------------------------- */
#define SPI_SCK_RP    10u            /* RP10 = RB10 */
#define SPI_MOSI_RP   11u            /* RP11 = RB11 */
#define SPI_MISO_RP   12u            /* RP12 = RB12 */
#define SPI_CS_BIT    13u            /* RB13  (not remappable, GPIO only) */
#define SPI_CS_PORT   PORTBbits.RB13

/* ---- SD card ----------------------------------------------------------- */
#define SD_SCK_RP     14u
#define SD_MOSI_RP    15u
#define SD_CS_LAT     LATBbits.LATB5
#define SD_CS_TRIS    TRISBbits.TRISB5
#define SD_CS_LOW()   (SD_CS_LAT = 0)
#define SD_CS_HIGH()  (SD_CS_LAT = 1)

/* ---- Status LED -------------------------------------------------------- */
#define LED_LAT       LATAbits.LATA0
#define LED_TRIS      TRISAbits.TRISA0
#define LED_ON()      (LED_LAT = 1)
#define LED_OFF()     (LED_LAT = 0)
#define LED_TOGGLE()  (LED_LAT ^= 1)

/* ---- Frame buffer for host output ------------------------------------- */
/*
 * All captured bytes are queued in a circular buffer and streamed to the
 * host UART by the main loop.  This decouples the capture ISRs from the
 * relatively slow 115 200 baud output.
 *
 * Each entry is a 32-bit word:
 *   bits [31:28]  protocol  (PROTO_*)
 *   bits [27:24]  flags     (FLAG_*)
 *   bits [23:8]   metadata  (I2C address / SPI CS / framing info)
 *   bits  [7:0]   data byte
 */
#define PROTO_UART   0x1u
#define PROTO_I2C    0x2u
#define PROTO_SPI    0x3u
#define PROTO_ERR    0xFu

/* I2C flags */
#define FLAG_I2C_START  0x1u
#define FLAG_I2C_STOP   0x2u
#define FLAG_I2C_ACK    0x4u
#define FLAG_I2C_NACK   0x8u
#define FLAG_I2C_ADDR   0x2u   /* meta byte is address, bit 0 = R/W */

/* SPI flags */
#define FLAG_SPI_CS_ASSERT   0x1u
#define FLAG_SPI_CS_DEASSERT 0x2u
#define FLAG_SPI_MOSI        0x4u
#define FLAG_SPI_MISO        0x8u

/* UART flags */
#define FLAG_UART_FRAMING_ERR  0x1u

/* Circular buffer depth (power of two) */
#define CBUF_SIZE   256u
#define CBUF_MASK   (CBUF_SIZE - 1u)

typedef struct {
    uint32_t buf[CBUF_SIZE];
    uint16_t head;
    uint16_t tail;
} cbuf_t;

extern cbuf_t capture_buf;

static inline void cbuf_push(uint32_t word)
{
    capture_buf.buf[capture_buf.tail] = word;
    capture_buf.tail = (capture_buf.tail + 1u) & CBUF_MASK;
    /* On overflow, advance head to discard oldest (never stall an ISR). */
    if (capture_buf.tail == capture_buf.head)
        capture_buf.head = (capture_buf.head + 1u) & CBUF_MASK;
}

static inline int cbuf_empty(void)
{
    return capture_buf.head == capture_buf.tail;
}

static inline uint32_t cbuf_pop(void)
{
    uint32_t w = capture_buf.buf[capture_buf.head];
    capture_buf.head = (capture_buf.head + 1u) & CBUF_MASK;
    return w;
}

#endif /* HW_H */
