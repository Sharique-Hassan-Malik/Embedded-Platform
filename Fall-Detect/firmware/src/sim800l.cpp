#include "sim800l.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// SoftwareSerial exists only on AVR (Uno/Nano). On the Nano 33 BLE (mbed,
// Cortex-M4) it is unavailable, so the SIM800L uses the hardware UART Serial1
// (pins D0/D1) instead of the D3/D4 SoftwareSerial pair.
#if defined(__AVR__)
  #include <SoftwareSerial.h>
  static SoftwareSerial gsm(3, 4);   /* RX=D3, TX=D4 */
#else
  #define gsm Serial1                /* hardware UART: RX=D0, TX=D1 */
#endif

/* Send an AT command and wait for an expected response substring.
 * Returns true if resp is found within timeout_ms. */
static bool at_wait(const char *cmd, const char *resp, uint32_t timeout_ms)
{
    if (cmd) {
        gsm.println(cmd);
    }

    uint32_t deadline = millis() + timeout_ms;
    char buf[128];
    uint8_t idx = 0;

    while (millis() < deadline) {
        while (gsm.available() && idx < sizeof(buf) - 1) {
            char c = (char)gsm.read();
            buf[idx++] = c;
            buf[idx]   = '\0';
            if (strstr(buf, resp))
                return true;
        }
    }
    return false;
}

static bool at_ok(const char *cmd, uint32_t timeout_ms = 1000)
{
    return at_wait(cmd, "OK", timeout_ms);
}

void sim800_reset(void)
{
    pinMode(SIM800_RST_PIN, OUTPUT);
    digitalWrite(SIM800_RST_PIN, LOW);
    delay(200);
    digitalWrite(SIM800_RST_PIN, HIGH);
    delay(3000);   /* boot time */
}

Sim800Status sim800_init(void)
{
    gsm.begin(SIM800_BAUD);
    sim800_reset();

    /* Wait for "RDY" from module boot sequence. */
    if (!at_wait(nullptr, "RDY", 10000))
        return SIM800_TIMEOUT;

    /* Disable echo. */
    at_ok("ATE0");

    /* SMS text mode. */
    if (!at_ok("AT+CMGF=1"))
        return SIM800_ERROR;

    /* Set SMS character set to GSM (avoids encoding issues with ASCII text). */
    at_ok("AT+CSCS=\"GSM\"");

    /* Check SIM card present. */
    if (!at_ok("AT+CPIN?"))
        return SIM800_NO_SIM;

    return SIM800_OK;
}

Sim800Status sim800_check_network(void)
{
    gsm.println("AT+CREG?");
    uint32_t deadline = millis() + 10000;
    char buf[64];
    uint8_t idx = 0;

    while (millis() < deadline) {
        while (gsm.available() && idx < sizeof(buf) - 1) {
            char c = (char)gsm.read();
            buf[idx++] = c;
            buf[idx]   = '\0';
        }
        /* +CREG: 0,1 (home) or +CREG: 0,5 (roaming) */
        if (strstr(buf, ",1") || strstr(buf, ",5"))
            return SIM800_OK;
        if (strstr(buf, "ERROR"))
            return SIM800_ERROR;
    }
    return SIM800_NO_NET;
}

Sim800Status sim800_send_sms(const char *number, const char *body)
{
    char cmd[SIM800_NUM_LEN + 16];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
    gsm.println(cmd);

    /* Wait for the '>' prompt before sending the message body. */
    if (!at_wait(nullptr, ">", 3000))
        return SIM800_TIMEOUT;

    gsm.print(body);
    gsm.write(0x1A);   /* Ctrl+Z — terminates the SMS body */

    /* Module responds with +CMGS: <mr> followed by OK. */
    if (!at_wait(nullptr, "+CMGS:", 5000))
        return SIM800_TIMEOUT;

    return SIM800_OK;
}

int sim800_signal_dbm(void)
{
    gsm.println("AT+CSQ");
    uint32_t deadline = millis() + 1000;
    char buf[32];
    uint8_t idx = 0;

    while (millis() < deadline) {
        while (gsm.available() && idx < sizeof(buf) - 1) {
            char c = (char)gsm.read();
            buf[idx++] = c;
            buf[idx]   = '\0';
        }
        const char *p = strstr(buf, "+CSQ: ");
        if (p) {
            int rssi = atoi(p + 6);
            if (rssi == 99) return 0;          /* not known / not detectable */
            return rssi * (-2) - 109;
        }
    }
    return 0;
}
