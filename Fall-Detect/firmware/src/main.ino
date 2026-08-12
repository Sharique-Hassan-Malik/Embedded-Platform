/*
 * Wearable Fall Detection Device
 *
 * Hardware:
 *   Arduino Nano 33 BLE (nRF52840, 64 MHz, 256 KB SRAM, BLE)
 *   — or —
 *   Arduino Nano / Uno + external MPU-6050 breakout
 *
 *   MPU-6050  → I2C (A4=SDA, A5=SCL), INT → pin 2
 *   SIM800L   → SoftwareSerial (pin 3=RX, pin 4=TX), RST → pin 5
 *   Status LED → pin 13 (built-in)
 *   Alert LED  → pin 7
 *
 * Operation:
 *   1. IMU sampled at 100 Hz via DATA_RDY interrupt.
 *   2. Each sample fed to the fall detector (threshold FSM).
 *   3. On a fall candidate the TFLite Micro classifier runs on the
 *      50-sample window.
 *   4. If the classifier confirms a fall and the posture check passes,
 *      an SMS is sent to ALERT_NUMBER via the SIM800L module.
 *   5. The system enters a 30 s cooldown before re-arming.
 *
 * Serial monitor (115200 baud) logs all state transitions and SMV values.
 */

#include <Arduino.h>
#include "mpu6050.h"
#include "sim800l.h"
#include "fall_detect.h"

#define ALERT_NUMBER    "+923001234567"   /* edit before deploying */
#define IMU_ADDR        MPU6050_ADDR_LOW
#define LED_STATUS      13
#define LED_ALERT       7
#define PIN_IMU_INT     2

static FallDetector  g_fd;
static Mpu6050Sample g_sample;
static volatile bool g_imu_ready = false;

static void imu_isr(void) { g_imu_ready = true; }

static const char *state_name(FdState s)
{
    switch (s) {
        case FD_IDLE:        return "IDLE";
        case FD_FREE_FALL:   return "FREE_FALL";
        case FD_IMPACT:      return "IMPACT";
        case FD_CLASSIFYING: return "CLASSIFYING";
        case FD_LYING:       return "LYING";
        case FD_ALERT:       return "ALERT";
        case FD_COOLDOWN:    return "COOLDOWN";
        default:             return "UNKNOWN";
    }
}

static void send_fall_alert(void)
{
    char body[SIM800_SMS_LEN];
    snprintf(body, sizeof(body),
             "FALL DETECTED\n"
             "Confidence: %.0f%%\n"
             "Falls total: %lu\n"
             "Time: %lus",
             (double)(g_fd.fall_prob * 100.0f),
             g_fd.total_alerts,
             (unsigned long)(millis() / 1000));

    Serial.println(F("Sending SMS alert..."));
    digitalWrite(LED_ALERT, HIGH);

    Sim800Status st = sim800_send_sms(ALERT_NUMBER, body);
    if (st == SIM800_OK) {
        Serial.println(F("SMS sent OK"));
    } else {
        Serial.print(F("SMS failed: "));
        Serial.println(st);
    }
}

void setup(void)
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {}
    Serial.println(F("Fall Detection System — init"));

    pinMode(LED_STATUS, OUTPUT);
    pinMode(LED_ALERT,  OUTPUT);
    pinMode(PIN_IMU_INT, INPUT_PULLUP);

    /* IMU init. */
    if (!mpu6050_init(IMU_ADDR)) {
        Serial.println(F("MPU-6050 not found — halting"));
        while (true) {
            digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
            delay(200);
        }
    }
    Serial.println(F("MPU-6050 OK"));

    /* Attach interrupt for DATA_RDY (INT pin is active-low). */
    attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT), imu_isr, FALLING);

    /* GSM init. */
    Serial.println(F("Initialising SIM800L..."));
    Sim800Status st = sim800_init();
    if (st != SIM800_OK) {
        Serial.print(F("SIM800L init failed: "));
        Serial.println(st);
        /* Continue without GSM — fall detection still works, alerts won't send. */
    } else {
        Serial.println(F("SIM800L OK"));
        int rssi = sim800_signal_dbm();
        Serial.print(F("Signal: "));
        Serial.print(rssi);
        Serial.println(F(" dBm"));
    }

    /* Fall detector init (includes TFLite Micro setup). */
    fd_init(&g_fd);
    Serial.println(F("Fall detector ready"));

    digitalWrite(LED_STATUS, HIGH);
}

void loop(void)
{
    if (!g_imu_ready) return;
    g_imu_ready = false;

    if (!mpu6050_read(IMU_ADDR, &g_sample)) return;

    FdState prev_state = g_fd.state;
    bool fall_confirmed = fd_update(&g_fd, &g_sample, millis());

    /* Log state transitions. */
    if (g_fd.state != prev_state) {
        Serial.print(F("State: "));
        Serial.print(state_name(prev_state));
        Serial.print(F(" → "));
        Serial.println(state_name(g_fd.state));

        if (g_fd.state == FD_CLASSIFYING) {
            Serial.print(F("Classifier: fall="));
            Serial.print(g_fd.fall_prob, 3);
            Serial.print(F("  no_fall="));
            Serial.println(g_fd.nfall_prob, 3);
        }
    }

    if (fall_confirmed) {
        Serial.println(F("*** FALL CONFIRMED ***"));
        send_fall_alert();
    }

    /* LED heartbeat: fast blink during free-fall/impact, slow during idle. */
    static uint32_t led_ms = 0;
    uint32_t now = millis();
    uint32_t period = (g_fd.state >= FD_FREE_FALL && g_fd.state <= FD_LYING)
                      ? 100u : 1000u;
    if (now - led_ms >= period) {
        led_ms = now;
        digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
    }

    /* Clear alert LED after 5 s. */
    static uint32_t alert_ms = 0;
    if (fall_confirmed) alert_ms = millis();
    if (digitalRead(LED_ALERT) && (millis() - alert_ms > 5000))
        digitalWrite(LED_ALERT, LOW);
}
