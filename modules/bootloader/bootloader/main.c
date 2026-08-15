/*
 * PIC18F4550 Secure Bootloader
 *
 * Occupies flash 0x0000–0x07FF (2 KB).
 * Fosc: 8 MHz internal HFINTOSC.
 *
 * Boot sequence:
 *   1. On POR / MCLR: check for bootloader trigger (RB0 held low).
 *      If not triggered AND a valid application exists → jump to app.
 *      If not triggered AND no valid application → wait for trigger.
 *
 *   2. Trigger detected (RB0 low at startup):
 *      a. Receive Intel HEX over UART (31 250 baud) via ihex_receive().
 *      b. Verify: SHA-256 the application image, read metadata block,
 *         verify ECDSA-P256 signature over the hash using the stored key.
 *      c. If signature valid → send OK, jump to app.
 *         If signature invalid → send ERR, erase application, halt.
 *
 * Application validity check (used when no trigger):
 *   Read META_START and check that app_start == APP_START, app_size > 0,
 *   app_size <= APP_MAX_SIZE and that the ECDSA signature verifies.
 *   The hash is recomputed from flash each time — no separate "valid" flag.
 *
 * UART: RC6 TX, RC7 RX, 31 250 baud, 8N1.
 *   SPBRG = (8 000 000 / (16 × 31 250)) - 1 = 15
 *
 * Trigger pin: RB0, internal pull-up enabled, active low (tie to GND to
 * enter update mode).
 */

#include <xc.h>
#include <stdint.h>
#include <string.h>

#include "boot_shared.h"
#include "flash.h"
#include "sha256.h"
#include "p256.h"
#include "ihex.h"

#define _XTAL_FREQ  8000000UL

/* Two-level stringize: expand the macro argument, then quote it. */
#define BOOT_XSTR_(s)  #s
#define BOOT_XSTR(s)   BOOT_XSTR_(s)

/* ---- Configuration bits -------------------------------------------------- */
#pragma config PLLDIV  = 1
#pragma config CPUDIV  = OSC1_PLL2
#pragma config FOSC    = INTOSCIO_EC
#pragma config FCMEN   = OFF
#pragma config IESO    = OFF
#pragma config PWRT    = ON
#pragma config BOR     = OFF
#pragma config BORV    = 3
#pragma config WDT     = OFF
#pragma config WDTPS   = 32768
#pragma config PBADEN  = OFF
#pragma config LVP     = OFF
#pragma config XINST   = OFF
#pragma config STVREN  = ON
#pragma config MCLRE   = ON
#pragma config CP0=OFF, CP1=OFF, CP2=OFF, CP3=OFF
#pragma config CPB=OFF, CPD=OFF
#pragma config WRT0=OFF, WRT1=OFF, WRT2=OFF, WRT3=OFF
#pragma config WRTB=OFF, WRTC=OFF, WRTD=OFF
#pragma config EBTR0=OFF, EBTR1=OFF, EBTR2=OFF, EBTR3=OFF, EBTRB=OFF

/* ---- UART init at 31 250 baud (SPBRG = 15, BRGH = 1) -------------------- */

static void uart_init(void)
{
    TRISCbits.TRISC6 = 0;   /* TX output */
    TRISCbits.TRISC7 = 1;   /* RX input  */
    /* RC7/RB0 digital: 18F4550 has no ANSELx; ADCON1 = 0x0F in main() */

    SPBRG  = 15u;
    TXSTA  = 0x24u;   /* BRGH = 1, SYNC = 0, TXEN = 1 */
    RCSTA  = 0x90u;   /* SPEN = 1, CREN = 1           */
}

static void uart_putc(uint8_t c)
{
    while (!TXSTAbits.TRMT)
        ;
    TXREG = c;
}

/* ---- Trigger pin --------------------------------------------------------- */

static uint8_t trigger_active(void)
{
    /* RB0 with internal pull-up: low = trigger requested. */
    INTCON2bits.nRBPU = 0u;   /* enable PORTB pull-ups */
    TRISBbits.TRISB0  = 1u;
    __delay_ms(1);
    return (PORTBbits.RB0 == 0u) ? 1u : 0u;
}

/* ---- Application validity check ----------------------------------------- */

static uint8_t app_is_valid(void)
{
    uint8_t  buf[4];
    uint32_t app_start, app_size;
    /* Large buffers are static: XC8 free-mode does not overlay locals, and the
       18F4550 has only 2 KB RAM. app_is_valid() runs once at boot and is not
       reentrant, so fixed storage is safe and keeps the compiled stack small. */
    static uint8_t  computed_hash[32];
    static uint8_t  sig_r[META_SIG_SIZE];
    static uint8_t  sig_s[META_SIG_SIZE];
    static sha256_ctx_t ctx;

    /* Two phases that never overlap, sharing one allocation.

       Hashing needs a 64-byte flash window and the stored hash to compare
       against; verification needs the 65-byte public key. The last read of
       `hashing` is the constant-time compare, and `pubkey` is not written
       until after it, so the overlay is safe — and necessary: as separate
       objects these are 161 bytes and XC8 cannot place a 65-byte object in
       any remaining PIC18 bank ("could not find space (65 bytes)"). Overlaid,
       the largest request is the key itself and 96 bytes are freed for it —
       which is what makes a 65-byte hole exist at all.

       The flash window is 32 bytes rather than 64 for the same reason;
       sha256_update() takes any length, so this only costs loop iterations.

       If a later change reads stored_hash after the key is loaded, it reads
       key material. Keep the phases in this order.  */
    static union {
        struct {
            uint8_t tmp[32];          /* flash read window, live in the loop  */
            uint8_t stored_hash[32];  /* live until the compare below         */
        } hashing;
        uint8_t pubkey[PUBKEY_LEN];   /* live only after the compare          */
    } phase;
    uint32_t addr;
    uint16_t chunk;

    /* Read app_start and app_size from metadata block. */
    flash_read_block(META_START + META_APP_START_OFF, buf, 4u);
    app_start = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
              | ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];

    flash_read_block(META_START + META_APP_SIZE_OFF, buf, 4u);
    app_size = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
             | ((uint32_t)buf[2] <<  8) |  (uint32_t)buf[3];

    if (app_start != APP_START) return 0u;
    if (app_size == 0u || app_size > APP_MAX_SIZE) return 0u;

    /* Read stored hash and signature. */
    flash_read_block(META_START + META_SHA256_OFF, phase.hashing.stored_hash, 32u);
    flash_read_block(META_START + META_SIG_R_OFF,  sig_r,       32u);
    flash_read_block(META_START + META_SIG_S_OFF,  sig_s,       32u);

    /* Recompute SHA-256 over the application image in flash. */
    sha256_init(&ctx);
    addr = APP_START;
    while (addr < APP_START + app_size) {
        chunk = (uint16_t)((APP_START + app_size - addr) < sizeof phase.hashing.tmp
                         ? (APP_START + app_size - addr) : sizeof phase.hashing.tmp);
        flash_read_block(addr, phase.hashing.tmp, chunk);
        sha256_update(&ctx, phase.hashing.tmp, chunk);
        addr += chunk;
    }
    sha256_final(&ctx, computed_hash);

    /* Compare computed hash to stored hash (constant-time via XOR accumulate). */
    {
        uint8_t diff = 0u;
        uint8_t i;
        for (i = 0u; i < 32u; i++)
            diff |= (computed_hash[i] ^ phase.hashing.stored_hash[i]);
        if (diff) return 0u;
    }

    /* Read public key from protected key page. */
    /* From here on `phase` holds the key, and the hashing fields are gone. */
    flash_read_block(KEY_PAGE_START + PUBKEY_OFFSET, phase.pubkey, PUBKEY_LEN);

    /* Verify ECDSA signature. */
    return (p256_verify(phase.pubkey, computed_hash, sig_r, sig_s) == 0) ? 1u : 0u;
}

/* ---- Erase entire application area --------------------------------------- */

static void erase_application(void)
{
    uint32_t addr;
    for (addr = APP_START; addr < META_START; addr += 64u)
        flash_erase_row(addr);
}

/* ---- Jump to application ------------------------------------------------- */

static void launch_app(void)
{
    /* Disable all interrupts and peripherals before handing over. */
    INTCONbits.GIE  = 0u;
    INTCONbits.PEIE = 0u;

    /* The application is linked to start at APP_START.
     * A far call to APP_START triggers the application's reset vector.
     * The application must set up its own interrupt vectors at
     * APP_START + 0x08 (high) and APP_START + 0x18 (low). */
    asm("GOTO " BOOT_XSTR(APP_START_ASM));
}

/* ---- Main ---------------------------------------------------------------- */

void main(void)
{
    /* 8 MHz internal oscillator. */
    OSCCON = 0x72u;
    while (!OSCCONbits.IOFS)
        ;

    /* All analogue-capable pins digital (the 18F4550 has no ANSELx). */
    ADCON1 = 0x0Fu;

    uart_init();

    if (!trigger_active()) {
        /* No update requested: validate and launch existing application. */
        if (app_is_valid()) {
            launch_app();
        }
        /* No valid application found — fall through to wait for update. */
    }

    /* ---- Update mode ---- */
    /* Receive firmware image. */
    if (ihex_receive() != IHEX_DONE) {
        uart_putc(UART_ERR);
        erase_application();
        for (;;)   /* halt — require power cycle */
            ;
    }

    /* Verify the newly written image. */
    if (!app_is_valid()) {
        uart_putc(UART_ERR);
        erase_application();
        for (;;)
            ;
    }

    uart_putc(UART_OK);
    __delay_ms(10);   /* allow OK byte to transmit before oscillator changes */
    launch_app();
}
