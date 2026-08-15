/*
 * Wire protocol between Pico firmware and Python host.
 *
 * All multi-byte fields are little-endian.
 *
 * Host → Pico  (1-byte command + optional payload)
 * ───────────────────────────────────────────────
 *   CMD_START        0x01  — begin streaming frames
 *   CMD_STOP         0x02  — stop streaming
 *   CMD_SET_RATE     0x03  + uint32 clkdiv   — set ADC clock divisor
 *   CMD_SET_CHANNEL  0x04  + uint8  channel  — 0=ADC0 1=ADC1 2=ADC2 3=temp
 *   CMD_SET_SAMPLES  0x05  + uint16 count    — samples per frame (≤FRAME_MAX_SAMPLES)
 *   CMD_PING         0x06  — host checks firmware alive
 *
 * Pico → Host  (binary frames)
 * ─────────────────────────────
 *   [4B SYNC][1B channel][1B flags][2B n_samples][n_samples×2B uint16][2B CRC16]
 *
 *   SYNC     = 0xDE 0xAD 0xC0 0xDE
 *   flags    = bit0: overflow (DMA overrun since last frame)
 *   n_samples: actual sample count in this frame
 *   samples  : 12-bit ADC value in bits [15:4], bits [3:0] = 0
 *              → divide by 16 to get raw 12-bit value
 *   CRC16    : CRC-16/CCITT-FALSE over bytes from SYNC through last sample
 *
 * Pong (response to CMD_PING):
 *   [1B 0xAC][4B FIRMWARE_VERSION]
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Command bytes */
#define CMD_START       0x01u
#define CMD_STOP        0x02u
#define CMD_SET_RATE    0x03u
#define CMD_SET_CHANNEL 0x04u
#define CMD_SET_SAMPLES 0x05u
#define CMD_PING        0x06u

/* Frame constants */
#define FRAME_SYNC_0        0xDEu
#define FRAME_SYNC_1        0xADu
#define FRAME_SYNC_2        0xC0u
#define FRAME_SYNC_3        0xDEu
#define FRAME_MAX_SAMPLES   2048u
#define FRAME_FLAG_OVERFLOW 0x01u
#define PONG_BYTE           0xACu
#define FIRMWARE_VERSION    0x00010000u  /* 0.1.0 */

/* ADC reference voltage (3.3 V on Pico) */
#define ADC_VREF_MV   3300u
#define ADC_BITS      12u
#define ADC_FULL_SCALE ((1u << ADC_BITS) - 1u)   /* 4095 */

/* Default configuration */
#define DEFAULT_CLKDIV    960u   /* 48 MHz / 960 = 50 ksps */
#define DEFAULT_CHANNEL   0u
#define DEFAULT_N_SAMPLES 1024u

/* Minimum ADC clock divisor (≥ 96 per datasheet; 96 → 500 ksps) */
#define MIN_CLKDIV  96u
#define MAX_CLKDIV  65535u

#endif /* PROTOCOL_H */
