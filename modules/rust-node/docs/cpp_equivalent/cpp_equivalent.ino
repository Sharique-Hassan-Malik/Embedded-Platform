// docs/cpp_equivalent/cpp_equivalent.ino
//
// Arduino C++ equivalent of the Rust sensor node.
// Requires: DHT sensor library (Adafruit), Adafruit SSD1306, Adafruit GFX.
// Compare binary size with the Rust build using avr-size.

#include <DHT.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

static constexpr uint8_t DHT_PIN   = 2;
static constexpr uint8_t DHT_TYPE  = DHT22;
static constexpr uint8_t OLED_ADDR = 0x3C;

DHT             dht(DHT_PIN, DHT_TYPE);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);

static uint32_t read_count  = 0;
static uint32_t error_count = 0;
static float    last_temp   = NAN;
static float    last_hum    = NAN;

void setup() {
    Serial.begin(9600);
    dht.begin();
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        while (true) {}
    }
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 0);
    oled.println("C++ Sensor Node");
    oled.display();
    delay(1500);
}

void loop() {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    read_count++;

    bool ok = !isnan(h) && !isnan(t);
    if (ok) {
        last_temp = t;
        last_hum  = h;
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        Serial.print("READ ok   T=");
        Serial.print(t, 1);
        Serial.print(" H=");
        Serial.println(h, 1);
    } else {
        error_count++;
        Serial.println("READ err");
    }

    oled.clearDisplay();
    oled.setCursor(0, 0);
    oled.println("C++ Sensor Node");
    oled.println("----------------");
    oled.print("Temp: "); oled.print(isnan(last_temp) ? 0 : last_temp, 1); oled.println(" C");
    oled.print("Hum:  "); oled.print(isnan(last_hum)  ? 0 : last_hum,  1); oled.println(" %");
    oled.println();
    oled.print("Reads: ");  oled.println(read_count);
    oled.print("Errors: "); oled.println(error_count);
    oled.println(ok ? "OK" : "err");
    oled.display();

    delay(2000);
}
