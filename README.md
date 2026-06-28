# sesame-robot-sense

![License](https://img.shields.io/badge/License-APACHE2.0-yellow)
![Microcontroller](https://img.shields.io/badge/Microcontroller-ESP32--S3-blue)
![Firmware](https://img.shields.io/badge/Firmware-C%2B%2B-blue?logo=c%2B%2B)
![IDE](https://img.shields.io/badge/IDE-Arduino-00979D?logo=arduino&logoColor=white)

A hardware upgrade and firmware fork of [dorianborian/sesame-robot](https://github.com/dorianborian/sesame-robot), replacing the original ESP32 + direct-GPIO servo drive with a more capable platform built around the **Seeed XIAO ESP32-S3 Sense** and a **PCA9685 I2C servo driver**.

The result is the same expressive quadruped with a new spine: more headroom for audio, vision, and IMU sensing; cleaner servo timing; and a dual-transport control stack that lets you drive the robot from a browser, a Python script, or voice commands — all at the same time.

---

## What this fork adds over the original Sesame

| | sesame-robot (original) | sesame-robot-sense (this fork) |
|---|---|---|
| **MCU** | Lolin S2 Mini / Distro Board | Seeed XIAO ESP32-S3 Sense |
| **Servo drive** | Direct GPIO PWM (ESP32Servo) | PCA9685 I2C PWM driver — no jitter, no strapping-pin conflicts |
| **Audio** | None | MAX98357A I2S amp on AI Pin PCB |
| **IMU** | None | MPU-6050 (I2C, pin-reserved) |
| **Camera** | None | OV2640 flex-cable mount (pin-reserved) |
| **Control transports** | Browser AP / HTTP only | Browser AP + TCP line-protocol (port 8888) + USB serial |
| **Python host stack** | Sesame Companion App | Albert-compatible: `robot_link.py`, `robot_gui.py`, `voice_control.py` |
| **Calibration** | Compile-time subtrim | Runtime `trim` / `rev` / `nudge` — saved to NVS flash, no reflash needed |
| **Architecture** | Single-core blocking loop | Dual-core: Core 0 owns WiFi/networking, Core 1 owns servos/OLED/IMU |

All original Sesame movement sequences, OLED faces, and web UI are preserved unchanged.

---

## Key features

**Servo hardware**
- PCA9685 16-channel PWM driver over I2C; all 8 MG90S servos on channels 0–7
- Pulse range tuned to match the original Sesame firmware exactly — no re-calibration of angles in `movement-sequences.h`
- Per-servo runtime trim, direction flip, and NVS persistence

**Dual-mode WiFi**
- Joins your home network as a station (`quadruped.local` via mDNS) — for Python host tools over TCP
- Also runs the original Sesame access point + captive-portal web UI simultaneously

**Three control surfaces — all live at the same time**
1. **Browser** — original Sesame web UI on port 80, unchanged
2. **TCP line protocol** on port 8888 — drives `robot_link.py`, `robot_gui.py`, `voice_control.py` with zero changes to those scripts
3. **USB serial** — identical command vocabulary to TCP; use the Arduino Serial Monitor for bring-up and calibration

**Calibration workflow**
- `servo`, `nudge`, `trim`, `rev`, `save/load/clear` — tune horns and direction live, persist to flash, dump a copy-pasteable `servoSubtrim[]` when done

**Dual-core architecture**
- Core 0: WiFi, DNS, HTTP, TCP — never touches I2C
- Core 1: PCA9685, OLED, IMU, servo logic, serial CLI
- FreeRTOS queue between cores; one atomic stop flag for instant mid-pose interrupt

**Sensor headroom (wired, not yet coded)**
- MPU-6050 IMU on I2C 0x68
- OV2640 camera on flex connector
- MAX98357A I2S audio amp — pins locked on the AI Pin PCB

---

## Hardware

**Board:** Seeed XIAO ESP32-S3 Sense on the techiesms AI Pin PCB
**Servo driver:** Adafruit PCA9685 breakout (I2C 0x40)
**Display:** SSD1306 128×64 OLED (I2C 0x3C) — same as original Sesame
**Power:** 2S LiPo → TPS565201 5V UBEC → PCA9685 V+ rail and XIAO 5V pin

Full pin assignments and servo channel map: [`firmware/pins.h`](firmware/pins.h)

---

## Getting started

### 1. Libraries (Arduino IDE)

Install via Library Manager:

| Library | Replaces |
|---|---|
| `Adafruit PWM Servo Driver` | `ESP32Servo` — remove that if present |
| `Adafruit SSD1306` | (same as original Sesame) |
| `Adafruit GFX Library` | (same as original Sesame) |

Board package: **esp32 by Espressif Systems** — board `XIAO_ESP32S3`.

### 2. Wire the hardware

See the [Wiring guide](docs/wiring.md) for the full schematic: I2C bus, PCA9685 servo channel map, power topology, and locked I2S pins.

### 3. Configure WiFi credentials

Open `firmware/sesame-firmware-main.ino` and set:

```cpp
#define NETWORK_SSID "your-network"
#define NETWORK_PASS "your-password"
```

The robot joins this network on boot and advertises itself as `quadruped.local` — which is how all the Python host tools find it.

### 4. Flash

Connect XIAO via USB-C, select the correct port, upload. Open Serial Monitor at 115200 baud to confirm WiFi connection and TCP server startup.

### 5. Calibrate servos

With USB still connected, use the serial CLI to center all horns and dial in trims before assembling the legs. See the [Calibration guide](docs/calibration.md).

---

## Documentation

| Document | Contents |
|---|---|
| [Architecture](docs/architecture.md) | Dual-core design, I2C bus, servo write path, cross-core safety |
| [Wiring](docs/wiring.md) | Pin assignments, I2C bus, servo channel map, power topology |
| [Setup & Calibration](docs/calibration.md) | Flashing, WiFi config, bring-up sequence, trim/rev/save workflow |
| [Command Reference](docs/commands.md) | Full vocabulary — all transports, with examples |
| [Software guide](docs/software.md) | Installation, tool usage, voice setup, moves.json, troubleshooting |
| [Software](software/README.md) | Python host tools: GUI, CLI, voice control, moves library |

---

## Credits

Original Sesame Robot by [Dorian Todd](https://www.doriantodd.com/) — [`dorianborian/sesame-robot`](https://github.com/dorianborian/sesame-robot).
This fork adapts the firmware for the XIAO ESP32-S3 Sense platform and adds the Albert-protocol TCP stack.
