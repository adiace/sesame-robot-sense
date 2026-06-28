# Firmware

Arduino firmware for sesame-robot-sense — the XIAO ESP32-S3 Sense + PCA9685 fork of [sesame-robot](https://github.com/dorianborian/sesame-robot).

## Files

| File | Purpose |
|---|---|
| `sesame-firmware-main.ino` | Main entry point: `setup()`, `loop()`, all function definitions |
| `pins.h` | Pin assignments, I2C addresses, PCA9685 channel map, pulse calibration constants |
| `movement-sequences.h` | All pose and gait functions; `ServoName` enum (`R1`–`L4`) |
| `face-bitmaps.h` | PROGMEM bitmap data for all OLED face animations |
| `captive-portal.h` | Inline HTML for the Sesame AP web UI |

## Setup

See the [Setup & Calibration guide](../docs/calibration.md) for Arduino IDE prerequisites, board selection (`XIAO_ESP32S3`), required libraries, and the bring-up sequence.

**Required libraries (replace `ESP32Servo` with these):**
- `Adafruit PWM Servo Driver`
- `Adafruit SSD1306`
- `Adafruit GFX Library`

## Commands

Full vocabulary: [Command Reference](../docs/commands.md)

Quick start over USB serial (115200 baud):
```
neutral         → center all servos (fit horns here)
servo 0 110     → test R1 hip
trim 0 -5       → correct horn offset
rev 2           → flip L1 direction if backwards
save            → persist to NVS flash
stand           → stand pose
forward         → walk (loops until stop)
stop
```

## Architecture

See [Architecture](../docs/architecture.md) for the dual-core design, I2C bus topology, servo write path, and cross-core safety model.
