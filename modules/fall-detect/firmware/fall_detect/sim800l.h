#ifndef SIM800L_H
#define SIM800L_H

#include <stdint.h>
#include <stdbool.h>

/*
 * SIM800L GSM/GPRS module driver — AT command interface over UART.
 *
 * Hardware connections (Arduino Nano / Uno):
 *   SIM800L TX → Arduino pin 3  (SoftwareSerial RX)
 *   SIM800L RX → Arduino pin 4  (SoftwareSerial TX, via 1 kΩ + 2 kΩ divider to 3.3 V)
 *   SIM800L VCC → 3.7–4.2 V (separate LiPo or LDO — NOT the Arduino 5 V rail)
 *   SIM800L GND → Arduino GND
 *   SIM800L RST → Arduino pin 5  (active-low hardware reset)
 *
 * The module draws up to 2 A during GPRS transmission.  The power supply must
 * be capable of supplying this without voltage droop below 3.4 V.
 *
 * AT command timeouts (milliseconds):
 *   General response   : 1000
 *   Network registration: 10 000
 *   SMS send           : 5000
 *   HTTP operations    : 30 000
 */

#define SIM800_BAUD       9600
#define SIM800_RST_PIN    5

/* Maximum length of a phone number string including null terminator. */
#define SIM800_NUM_LEN    20

/* Maximum SMS body length (standard 160-character SMS). */
#define SIM800_SMS_LEN    161

typedef enum {
    SIM800_OK       = 0,
    SIM800_TIMEOUT  = 1,
    SIM800_ERROR    = 2,
    SIM800_NO_SIM   = 3,
    SIM800_NO_NET   = 4,
} Sim800Status;

/*
 * Power-on and initialise the module.
 * Waits for "RDY" and "Call Ready" strings, then configures SMS text mode.
 * Returns SIM800_OK if the module is ready within 10 s.
 */
Sim800Status sim800_init(void);

/*
 * Check network registration (AT+CREG?).
 * Returns SIM800_OK if registered on home or roaming network.
 */
Sim800Status sim800_check_network(void);

/*
 * Send an SMS.
 * number: destination phone number string (e.g. "+923001234567")
 * body:   message text (max 160 chars)
 * Returns SIM800_OK if the module acknowledges the send with "+CMGS:".
 */
Sim800Status sim800_send_sms(const char *number, const char *body);

/*
 * Hardware reset: pull RST low for 200 ms then wait 3 s for the module to boot.
 */
void sim800_reset(void);

/*
 * Query signal quality (AT+CSQ).
 * Returns RSSI in dBm, or 0 if the query fails.
 * RSSI mapping: CSQ value × (−2) − 109 dBm (ITU-T V.42bis table).
 */
int sim800_signal_dbm(void);

#endif /* SIM800L_H */
