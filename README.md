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
| **Control transports** | Browser AP / HTTP only | Browser AP + TCP (port 8888) + USB serial — same vocabulary on all three |
| **Companion app** | [sesame-companion-app](https://github.com/dorianborian/sesame-companion-app) | [sesame-companion-app-sense](https://github.com/adiace/sesame-companion-app-sense) — fork with local LLM, robot voice pipeline, and raw command bypass |
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
2. **TCP on port 8888** — newline-framed text commands; used by the companion app and `robot_link.py`
3. **USB serial** — identical command vocabulary to TCP; use the Arduino Serial Monitor for bring-up and calibration

**Calibration workflow**
- `servo`, `nudge`, `trim`, `rev`, `save/load/clear` — tune horns and direction live, persist to flash, dump a copy-pasteable `servoSubtrim[]` when done

**Dual-core architecture**
- Core 0: WiFi, DNS, HTTP, TCP — never touches I2C
- Core 1: PCA9685, OLED, IMU, servo logic, serial CLI
- FreeRTOS queue between cores; one atomic stop flag for instant mid-pose interrupt

**IMU reactions**
- MPU-6050 on I2C 0x68 — tap/pickup/flip/freefall detection; triggers face changes and movement responses
- Tap uses a 2-second peak window to reject motion noise before enabling tap recognition

**Audio**
- MAX98357A I2S amp plays WAV sound effects from SPIFFS and TTS voice responses from PSRAM
- Voice responses stream directly from PSRAM — no SPIFFS write, no size limit

**On-device wake word**
- PDM mic on the XIAO Sense module feeds ESP-SR WakeNet continuously ("Hi ESP")
- On trigger: records 4 s, streams to companion app, plays WAV response back on speaker

**Camera (pin-reserved, not yet coded)**
- OV2640 on the XIAO Sense flex connector — pins reserved, no driver yet

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

## Companion app

**[sesame-companion-app-sense](https://github.com/adiace/sesame-companion-app-sense)** is the desktop companion for this robot. It is a fork of [dorianborian/sesame-companion-app](https://github.com/dorianborian/sesame-companion-app) with the following differences:

| | sesame-companion-app (original) | sesame-companion-app-sense (this fork) |
|---|---|---|
| **LLM** | Gemini cloud API | Local Ollama (llama3.2 or any model) — no cloud required |
| **TTS (laptop)** | pyttsx3 | macOS `say` — avoids NSSpeechSynthesizer crash on AppKit thread |
| **Robot voice** | Not supported | Full pipeline: robot mic → Whisper STT → Ollama → `say` TTS → WAV back to robot speaker |
| **Command transport** | HTTP `/api/command` | TCP port 8888 (persistent, ~5ms latency) |
| **Raw command bypass** | None | `/command` prefix in chat skips LLM and sends directly to robot |
| **LLM reliability** | None | `_normalize_llm()` fallbacks + `_infer_command()` keyword scan when LLM misses a command |

Clone it separately and follow its README:

```bash
git clone https://github.com/adiace/sesame-companion-app-sense.git
cd sesame-companion-app-sense
./run.sh
```

---

## Documentation

| Document | Contents |
|---|---|
| [Architecture](docs/architecture.md) | Dual-core design, I2C bus, servo write path, cross-core safety |
| [Wiring](docs/wiring.md) | Pin assignments, I2C bus, servo channel map, power topology |
| [Setup & Calibration](docs/calibration.md) | Flashing, WiFi config, bring-up sequence, trim/rev/save workflow |
| [Command Reference](docs/commands.md) | Full vocabulary — all transports, with examples |
| [Software guide](docs/software.md) | Installation, tool usage, voice setup, moves.json, troubleshooting |

---

## Credits

Original Sesame Robot by [Dorian Todd](https://www.doriantodd.com/) — [`dorianborian/sesame-robot`](https://github.com/dorianborian/sesame-robot).
This fork adapts the firmware for the XIAO ESP32-S3 Sense platform and extends it with dual-core networking, IMU reactions, audio, and an on-device voice pipeline.
