/*
 * PIC32MX270F256B — Standalone FM Synthesizer Voice
 *
 * System clock : 80 MHz  (internal FRC × PLL)
 * Peripheral bus: 40 MHz
 * Audio output  : MCP4921 12-bit SPI DAC on SPI1, 22 050 Hz sample rate
 * MIDI input    : UART1 at 31 250 baud (standard DIN-5 MIDI)
 *
 * Pin map:
 *   RB0  /CS for MCP4921 (GPIO output)
 *   RB3  SCK1
 *   RB5  SDO1 (MOSI)
 *   RB13 U1RX (MIDI input — optocoupler output connects here)
 *   RA0  Activity LED
 *
 * MIDI CC assignments:
 *   CC  1  Modulation wheel — FM modulation depth (mod index)
 *   CC  5  Attack time (0 ms to 4 000 ms)
 *   CC  6  Decay time  (0 ms to 2 000 ms)
 *   CC  7  Sustain level (Q15)
 *   CC  8  Release time (0 ms to 4 000 ms)
 *   CC  9  LFO rate (0.1 Hz to 20 Hz)
 *   CC 10  LFO depth (Q15)
 *   CC 11  Operator ratio (modulator/carrier, Q8: 0 = 0.5x, 127 = 8x)
 *   CC 80  LFO target (< 64 = pitch vibrato, ≥ 64 = amplitude tremolo)
 */

#include <xc.h>
#include <sys/attribs.h>
#include <stdint.h>

#include "wavetable.h"
#include "dac.h"
#include "midi.h"
#include "fm_synth.h"

/* ---------------------------------------------------------------------------
 * Configuration bits
 * --------------------------------------------------------------------------- */
#pragma config FNOSC    = FRCPLL    /* FRC oscillator with PLL */
#pragma config FPLLIDIV = DIV_2     /* PLL input: 8 MHz / 2 = 4 MHz        */
#pragma config FPLLMUL  = MUL_20    /* VCO: 4 MHz × 20 = 80 MHz            */
#pragma config FPLLODIV = DIV_1     /* System clock: 80 MHz                 */
#pragma config FPBDIV   = DIV_2     /* Peripheral bus: 40 MHz               */
#pragma config FWDTEN   = OFF
#pragma config ICESEL   = ICS_PGx1
#pragma config JTAGEN   = OFF
#pragma config FSOSCEN  = OFF
#pragma config POSCMOD  = OFF       /* Primary oscillator disabled          */

#define PBCLK_HZ  40000000UL

/* ---------------------------------------------------------------------------
 * Globals shared between ISRs and main loop
 * --------------------------------------------------------------------------- */
static fm_voice_t voice;

/* Single-byte mailbox from UART ISR to main loop.
 * Byte-size writes and reads are atomic on MIPS so no critical section
 * is required here.  The ready flag is checked after each ISR write. */
static volatile uint8_t midi_rx_byte;
static volatile uint8_t midi_rx_ready;

/* Current ADSR timing values (updated by CC and read by handle_cc). */
static struct {
    uint32_t attack_ms;
    uint32_t decay_ms;
    int32_t  sustain_level;
    uint32_t release_ms;
} adsr_cfg = {8u, 150u, 19660, 400u};

/* ---------------------------------------------------------------------------
 * Peripheral initialization
 * --------------------------------------------------------------------------- */
static void uart1_init(void)
{
    /* Disable while configuring. */
    U1MODEbits.ON = 0;

    /* Map U1RX to RB13 via peripheral pin select. */
    U1RXRbits.U1RXR = 0b0011;   /* RPB13 = U1RX */

    /* 31 250 baud, 8N1. */
    U1BRG           = (uint16_t)(PBCLK_HZ / (16u * 31250u) - 1u);   /* 79 */
    U1MODEbits.PDSEL = 0;    /* 8-bit, no parity */
    U1MODEbits.STSEL = 0;    /* 1 stop bit */
    U1STAbits.URXEN  = 1;
    U1MODEbits.ON    = 1;

    /* RX interrupt at priority 3. */
    IPC8bits.U1IP   = 3;
    IPC8bits.U1IS   = 1;
    IFS1bits.U1RXIF = 0;
    IEC1bits.U1RXIE = 1;
}

static void timer2_init(void)
{
    T2CON           = 0;
    TMR2            = 0;
    /* PR2 = PBCLK / SAMPLE_RATE - 1 = 40 000 000 / 22 050 - 1 = 1 813 - 1 */
    PR2             = (uint16_t)(PBCLK_HZ / SAMPLE_RATE - 1u);
    T2CONbits.TCKPS = 0;    /* 1:1 prescaler */
    T2CONbits.ON    = 1;

    /* Priority 6 — higher than MIDI UART so audio timing is not disturbed. */
    IPC2bits.T2IP  = 6;
    IPC2bits.T2IS  = 0;
    IFS0bits.T2IF  = 0;
    IEC0bits.T2IE  = 1;
}

/* ---------------------------------------------------------------------------
 * UART1 RX ISR — receives one MIDI byte per interrupt
 * --------------------------------------------------------------------------- */
void __ISR(_UART_1_VECTOR, IPL3SRS) uart1_rx_isr(void)
{
    if (U1STAbits.URXDA) {
        midi_rx_byte  = U1RXREG;
        midi_rx_ready = 1;
    }
    /* Clear hardware overrun to re-enable the receiver. */
    if (U1STAbits.OERR)
        U1STAbits.OERR = 0;
    IFS1bits.U1RXIF = 0;
}

/* ---------------------------------------------------------------------------
 * Timer2 ISR — audio sample generation at 22 050 Hz
 *
 * Budget: 80 MHz / 22 050 Hz ≈ 3 628 cycles per sample.
 * Measured worst-case cost of fm_voice_tick + dac_write ≈ 380 cycles,
 * leaving ample headroom for interrupt latency and UART events.
 * --------------------------------------------------------------------------- */
void __ISR(_TIMER_2_VECTOR, IPL6SRS) timer2_isr(void)
{
    int32_t  sample  = fm_voice_tick(&voice);
    uint16_t dac_val = q15_to_dac(sample);
    dac_write(dac_val);
    IFS0bits.T2IF = 0;
}

/* ---------------------------------------------------------------------------
 * MIDI CC handler
 * --------------------------------------------------------------------------- */
static void handle_cc(uint8_t cc_num, uint8_t value)
{
    switch (cc_num) {

    case 1:    /* Modulation wheel — FM mod index (0 = sine, 127 = heavy FM) */
        /* Map 0–127 → mod_index Q8 (0–512). */
        fm_voice_set_mod_index(&voice, (uint16_t)(((uint32_t)value * 512u) / 127u));
        break;

    case 5:    /* Attack time 0–4 000 ms */
        adsr_cfg.attack_ms = midi_cc_to_ms(value, 4000u);
        adsr_set_params(&voice.envelope,
                        adsr_cfg.attack_ms, adsr_cfg.decay_ms,
                        adsr_cfg.sustain_level, adsr_cfg.release_ms);
        break;

    case 6:    /* Decay time 0–2 000 ms */
        adsr_cfg.decay_ms = midi_cc_to_ms(value, 2000u);
        adsr_set_params(&voice.envelope,
                        adsr_cfg.attack_ms, adsr_cfg.decay_ms,
                        adsr_cfg.sustain_level, adsr_cfg.release_ms);
        break;

    case 7:    /* Volume / sustain level */
        adsr_cfg.sustain_level = midi_cc_to_q15(value);
        adsr_set_params(&voice.envelope,
                        adsr_cfg.attack_ms, adsr_cfg.decay_ms,
                        adsr_cfg.sustain_level, adsr_cfg.release_ms);
        break;

    case 8:    /* Release time 0–4 000 ms */
        adsr_cfg.release_ms = midi_cc_to_ms(value, 4000u);
        adsr_set_params(&voice.envelope,
                        adsr_cfg.attack_ms, adsr_cfg.decay_ms,
                        adsr_cfg.sustain_level, adsr_cfg.release_ms);
        break;

    case 9:    /* LFO rate: 0.1 Hz (value=0) to 20 Hz (value=127), in centihz */
        lfo_set_params(&voice.lfo,
                       10u + (uint32_t)value * 156u,
                       voice.lfo.depth,
                       voice.lfo.target);
        break;

    case 10:   /* LFO depth */
        lfo_set_params(&voice.lfo,
                       voice.lfo.rate_centihz,
                       midi_cc_to_q15(value),
                       voice.lfo.target);
        break;

    case 11:   /* Operator ratio Q8: value 0 → ~0.5x, value 64 → 1x, value 127 → 4x */
        fm_voice_set_op_ratio(&voice,
                              (uint16_t)(64u + ((uint32_t)value * 896u) / 127u));
        break;

    case 80:   /* LFO target: < 64 = pitch, ≥ 64 = amplitude */
        lfo_set_params(&voice.lfo,
                       voice.lfo.rate_centihz,
                       voice.lfo.depth,
                       (value >= 64) ? LFO_TARGET_AMPLITUDE : LFO_TARGET_PITCH);
        break;

    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Main
 * --------------------------------------------------------------------------- */
int main(void)
{
    midi_msg_t msg;

    /* Free JTAG pins for GPIO and UART use. */
    DDPCONbits.JTAGEN = 0;

    /* Activity LED on RA0. */
    ANSELAbits.ANSA0 = 0;
    TRISAbits.TRISA0 = 0;
    LATAbits.LATA0   = 0;

    wavetable_init();
    midi_init();
    fm_voice_init(&voice);
    dac_init();

    uart1_init();
    timer2_init();

    /* Enable multi-vector interrupt controller. */
    INTCONbits.MVEC = 1;
    __builtin_enable_interrupts();

    for (;;) {
        if (midi_rx_ready) {
            uint8_t b;
            midi_rx_ready = 0;
            b = midi_rx_byte;

            /* Toggle LED on any incoming MIDI byte. */
            LATAINV = (1u << 0);

            if (midi_parse(b, &msg)) {
                switch (msg.type) {
                case MIDI_MSG_NOTE_ON:
                    fm_voice_note_on(&voice, msg.byte1, msg.byte2);
                    break;
                case MIDI_MSG_NOTE_OFF:
                    fm_voice_note_off(&voice);
                    break;
                case MIDI_MSG_CC:
                    handle_cc(msg.byte1, msg.byte2);
                    break;
                default:
                    break;
                }
            }
        }
    }

    return 0;
}
