#include "measurement.h"
#include "config.h"
#include "adc_os.h"
#include <math.h>

namespace Measurement {

static uint16_t g_I0   = 0;
static uint16_t g_dark = 0;
static uint8_t  g_duty = LED_DUTY_DEFAULT;

void begin() {
    pinMode(LED_PIN,      OUTPUT);
    pinMode(CUVETTE_PIN,  INPUT_PULLUP);
    pinMode(LED_EN_PIN,   INPUT_PULLUP);
    pinMode(STATUS_PIN,   OUTPUT);

    analogWrite(LED_PIN, g_duty);
    ADC_OS::begin();
}

void setLEDDuty(uint8_t duty) {
    g_duty = duty;
    analogWrite(LED_PIN, duty);
}

bool cuvettePresent() {
    return digitalRead(CUVETTE_PIN) == LOW;
}

uint16_t getI0()      { return g_I0; }
uint16_t getDark()    { return g_dark; }
uint8_t  getLEDDuty() { return g_duty; }

// ── Dark measurement ──────────────────────────────────────────────────────────
uint16_t doDark() {
    analogWrite(LED_PIN, 0);
    delay(LED_SETTLE_MS);

    uint16_t d = ADC_OS::read(PD_SIGNAL_PIN);
    g_dark = d;

    analogWrite(LED_PIN, g_duty);
    delay(LED_SETTLE_MS);
    return d;
}

// ── Blank (I0 reference) ──────────────────────────────────────────────────────
uint16_t doBlank() {
    delay(LED_SETTLE_MS);
    uint16_t raw = ADC_OS::read(PD_SIGNAL_PIN);

    // Subtract dark offset so I0 represents the net photocurrent.
    uint16_t net = (raw > g_dark) ? (raw - g_dark) : 0;
    g_I0 = net;

    digitalWrite(STATUS_PIN, HIGH);
    delay(200);
    digitalWrite(STATUS_PIN, LOW);

    return net;
}

// ── Sample measurement ────────────────────────────────────────────────────────
void doRead(uint16_t &signal_out, uint16_t &reference_out, float &absorbance) {
    delay(LED_SETTLE_MS);

    uint16_t raw_sig = ADC_OS::read(PD_SIGNAL_PIN);
    uint16_t raw_ref = ADC_OS::read(PD_REF_PIN);

    // Net signal after dark subtraction.
    uint16_t sig_net = (raw_sig > g_dark) ? (raw_sig - g_dark) : 0;

    signal_out    = sig_net;
    reference_out = raw_ref;

    if (g_I0 == 0 || sig_net == 0) {
        absorbance = NAN;
        return;
    }

    // Beer-Lambert: A = -log10(I / I0)
    // I  = sig_net (transmitted intensity through sample, dark-corrected)
    // I0 = g_I0   (transmitted intensity through blank, dark-corrected)
    float ratio = static_cast<float>(sig_net) / static_cast<float>(g_I0);
    absorbance  = -log10f(ratio);
}

} // namespace Measurement
