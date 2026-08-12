# Digital Oscilloscope — Raspberry Pi Pico

Turns a Raspberry Pi Pico into a 3-channel digital oscilloscope with a Python GUI.
The firmware captures analog signals via ADC and DMA and streams binary frames over
USB CDC at up to 500 ksps per channel. The host renders waveforms in real time with
edge triggering, voltage measurements and frequency estimation.

## Hardware

| Component | Connection |
|---|---|
| Raspberry Pi Pico | USB to PC |
| Signal input ch 0 | GPIO26 (ADC0), 0–3.3 V |
| Signal input ch 1 | GPIO27 (ADC1), 0–3.3 V |
| Signal input ch 2 | GPIO28 (ADC2), 0–3.3 V |
| Temperature sensor | Internal (no pin required) |

**Input range:** 0–3.3 V. Do not exceed 3.3 V on ADC pins.

## Features

- 3 analog channels + internal temperature sensor
- 5 ksps to 500 ksps sample rate (selectable)
- 256 to 2048 samples per frame
- Edge trigger (rising / falling) with draggable level
- Measurements: Vmin, Vmax, Vpp, Vmean, Vrms, frequency, duty cycle
- DMA overflow detection
- Demo mode — no hardware required

## Project structure

```
digital-oscilloscope/
├── firmware/
│   ├── CMakeLists.txt
│   ├── pico_sdk_import.cmake
│   ├── include/
│   │   ├── protocol.h      # wire protocol constants
│   │   ├── adc_dma.h       # ping-pong DMA driver interface
│   │   └── crc16.h         # CRC-16/CCITT-FALSE (header-only)
│   └── src/
│       ├── main.c           # command parser, frame builder, main loop
│       └── adc_dma.c        # ADC + DMA driver implementation
├── host/
│   ├── oscilloscope.py     # GUI application
│   ├── serial_reader.py    # background frame parser thread
│   ├── signal_proc.py      # trigger, measurements, timebase
│   └── protocol.py         # wire protocol mirror + CRC + command builders
├── tests/
│   └── test_oscilloscope.py
├── docs/
│   └── architecture.md
├── requirements.txt
└── README.md
```

## Building the firmware

```bash
export PICO_SDK_PATH=/path/to/pico-sdk

cd firmware
mkdir build && cd build
cmake ..
make -j4
```

Copy `oscilloscope.uf2` to the Pico while holding BOOTSEL.

## Running the host

```bash
pip install -r requirements.txt

# With hardware
python host/oscilloscope.py --port /dev/ttyACM0

# Demo mode (no hardware)
python host/oscilloscope.py --demo
```

## Running tests

```bash
pytest tests/ -v
```

## Sample rates and timing

| Divider | Sample rate | Time per 1024 samples |
|---|---|---|
| 96 | 500 ksps | 2.0 ms |
| 480 | 100 ksps | 10.2 ms |
| 960 | 50 ksps | 20.5 ms |
| 4800 | 10 ksps | 102 ms |
| 9600 | 5 ksps | 205 ms |

ADC clock: 48 MHz (USB PLL). `sample_rate = 48_000_000 / clkdiv`.

## Wire protocol summary

All fields little-endian.

**Host → Pico commands:**

| Cmd | Byte | Payload |
|---|---|---|
| Start streaming | 0x01 | — |
| Stop | 0x02 | — |
| Set sample rate | 0x03 | uint32 clkdiv |
| Set channel | 0x04 | uint8 (0–3) |
| Set samples/frame | 0x05 | uint16 |
| Ping | 0x06 | — |

**Pico → Host frame:**
`[sync 4B][channel 1B][flags 1B][n_samples 2B][samples n×2B][CRC16 2B]`

Sync word: `0xDE 0xAD 0xC0 0xDE`. CRC: CRC-16/CCITT-FALSE over all preceding bytes.

## References

- RP2040 datasheet — Chapter 4.9: ADC and Temperature Sensor
- Raspberry Pi Pico SDK documentation — `hardware_adc`, `hardware_dma`
- TinyUSB — USB device stack used by the Pico SDK
