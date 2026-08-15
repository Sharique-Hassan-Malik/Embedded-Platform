# Wiring Guide

## Bill of Materials (per node)

| Qty | Component | Notes |
|---|---|---|
| 1 | Arduino Uno or Nano | ATmega328P |
| 1 | nRF24L01+ module | 2.4 GHz, SPI interface, 3.3 V |
| 1 | 10 µF electrolytic capacitor | nRF24L01 power decoupling (essential) |
| 1 | 100 nF ceramic capacitor | nRF24L01 power decoupling |
| 1 | DHT22 sensor | Temperature + humidity (sensor nodes only) |
| 1 | 10 kΩ resistor | DHT22 data pull-up (sensor nodes only) |
| 1 | LDR (photoresistor) | Light level (sensor nodes only) |
| 1 | 10 kΩ resistor | LDR voltage divider (sensor nodes only) |
| 1 | HC-SR501 PIR sensor | Motion detection (sensor nodes only) |

---

## nRF24L01 Wiring (all nodes)

The nRF24L01 runs at 3.3 V. Power it from the Arduino's 3.3 V pin, never 5 V.
The module's data lines are 5 V tolerant on most breakout boards, but check
your specific module. Add decoupling capacitors directly at the module's VCC/GND
pins — without them, the module behaves erratically under SPI traffic.

```
nRF24L01 pin   Arduino pin    Notes
─────────────────────────────────────────────────────────
 GND            GND
 VCC            3.3V           NOT 5V
 CE             D9             configurable in config.h
 CSN            D10            configurable in config.h
 SCK            D13            hardware SPI — fixed
 MOSI           D11            hardware SPI — fixed
 MISO           D12            hardware SPI — fixed
 IRQ            (not used)
```

Decoupling: place a 10 µF electrolytic (positive to VCC) and 100 nF ceramic in
parallel across the nRF24L01's VCC and GND pins, as close to the module as
possible. This is the most common cause of nRF24L01 communication failures.

---

## DHT22 (sensor nodes only)

```
5V ──[10kΩ]──┬── DHT22 DATA ── D4
              │
             DHT22 VCC ── 5V
             DHT22 GND ── GND
```

The DHT22 requires a 10 kΩ pull-up on the data line. Without it, readings
return NaN. The firmware reports temperature -999 and humidity 65535 on failure,
which the Python host decodes and marks as `None`.

---

## LDR Voltage Divider (sensor nodes only)

```
5V ──[10kΩ]──┬── A0
              │
             LDR
              │
             GND
```

Higher ambient light → lower LDR resistance → higher ADC reading → higher
`light_pct` value. Swap the LDR and resistor if you want the opposite response.
Adjust the 10 kΩ to the LDR's midpoint resistance for maximum sensitivity.

---

## HC-SR501 PIR (sensor nodes only)

```
HC-SR501 VCC  → 5V  (the HC-SR501 requires 5V, not 3.3V)
HC-SR501 OUT  → D3
HC-SR501 GND  → GND
```

The HC-SR501 has two onboard potentiometers: sensitivity (range) and hold time
(how long OUT stays HIGH after motion is detected). Set hold time to its minimum
(~3 s) and sensitivity to match your deployment area.

---

## Flashing Multiple Nodes

Each node needs a unique `NODE_ID` in `config.h`. Flash them one at a time:

### In the Arduino IDE

1. Open `firmware/node/node.ino`
2. At the top of the file, change `#define NODE_ID 1` to the target node's ID
3. Connect that Arduino, select the port and click Upload
4. Repeat for each node, incrementing NODE_ID

### With arduino-cli

```bash
# Flash base node (NODE_ID=0)
arduino-cli compile --fqbn arduino:avr:uno firmware/node \
    --build-property "build.extra_flags=-DNODE_ID=0"
arduino-cli upload --fqbn arduino:avr:uno --port /dev/ttyACM0 firmware/node

# Flash sensor node 1 (NODE_ID=1)
arduino-cli compile --fqbn arduino:avr:uno firmware/node \
    --build-property "build.extra_flags=-DNODE_ID=1"
arduino-cli upload --fqbn arduino:avr:uno --port /dev/ttyACM1 firmware/node

# Flash sensor node 2 (NODE_ID=2)
arduino-cli compile --fqbn arduino:avr:uno firmware/node \
    --build-property "build.extra_flags=-DNODE_ID=2"
arduino-cli upload --fqbn arduino:avr:uno --port /dev/ttyACM2 firmware/node
```

---

## Required Libraries (Arduino IDE Library Manager)

| Library | Version | Install via |
|---|---|---|
| RF24 | ≥ 1.4.5 | Library Manager → search "RF24" by TMRh20 |
| DHT sensor library | ≥ 1.4.4 | Library Manager → search "DHT sensor library" by Adafruit |

The routing protocol itself has no library dependency — it is implemented from
scratch in `router.h/.cpp`.

---

## Verifying the Network

1. Flash and power on the base node (NODE_ID=0). Open the Serial Monitor at
   115200 baud. You should see `READY`.

2. Power on sensor node 1. Within 10 seconds you should see:
   `DATA,1,1,234,652,512,0` (temperature, humidity, light, motion)

3. Power on sensor node 2 in a different room (out of direct range of base).
   Position it so it can hear node 1. Its packets should appear as:
   `DATA,2,2,...` — two hops via node 1.

4. Use a serial terminal to monitor raw output from the base node before
   launching the dashboard.

5. Launch the dashboard:
   ```bash
   python host/dashboard.py --port /dev/ttyACM0
   ```

---

## Range Notes

- nRF24L01+ at 250 kbps in open air: 50–100 m typical
- Through walls: 10–30 m
- Use RF24_PA_HIGH in config.h for maximum range (requires stable 3.3V supply)
- Each relay hop adds ~80 ms latency (ACK_TIMEOUT_MS)
