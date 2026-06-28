# Architecture

## Overview

The firmware is structured around two concerns that must never interfere with each other: **networking** (which the ESP32 WiFi stack wants to own without interruption) and **servo/display control** (which must be the sole user of the I2C bus). The XIAO ESP32-S3's dual LX7 cores map cleanly to this split.

```
┌─────────────────────────────────────────────────────────────────┐
│  Core 0  (networkTask — pinned)                                 │
│                                                                 │
│  WiFi STA + AP  ·  DNS  ·  HTTP :80  ·  TCP :8888              │
│                                                                 │
│  never touches I2C / String / currentCommand                    │
└────────────────────────┬────────────────────────────────────────┘
                         │ FreeRTOS queue (8 × 48 chars)
                         │ volatile gStopRequested
                         │ volatile gServoAngle[8]  (read-only from Core 0)
┌────────────────────────┴────────────────────────────────────────┐
│  Core 1  (Arduino loop())                                       │
│                                                                 │
│  PCA9685 · SSD1306 · MPU-6050  (all I2C)                       │
│  currentCommand · face state · serial CLI                       │
│  all pose functions and movement sequences                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## Core 0 — networking

`networkTask()` runs in a tight `vTaskDelay(1)` loop and services three things:

1. **DNS server** — captive-portal redirect for devices connected to the Sesame AP
2. **HTTP WebServer** — original Sesame web UI on port 80; `handleCommandWeb()` and `handleApiCommand()` translate HTTP requests into queue entries
3. **TCP server** — Albert line-protocol on port 8888; `serviceTcpClient()` buffers newline-framed commands and routes them through `routeTcpLine()`

Core 0 never calls `setServoAngle()`, `setFace()`, or writes any `String` shared with Core 1. Every incoming command becomes a `char[48]` queue item or, for stop, a single flag write.

---

## Core 1 — motion

The Arduino `loop()` runs on Core 1 and does, in order every iteration:

1. `drainCommandQueue()` — pop any items Core 0 pushed and call `applyCommandLine()`
2. Stop reflex — check `gStopRequested`; clear `currentCommand` if set
3. `updateAnimatedFace()` / `updateIdleBlink()` / `updateWifiInfoScroll()` — OLED work
4. Pose dispatch — run whichever `currentCommand` is set (`runWalkPose()`, etc.)
5. Serial CLI — read and route any USB serial input through `applyCommandLine()`

All blocking pose functions (`runWalkPose`, `delayWithFace`, `pressingCheck`) also drain the queue and check `gStopRequested` on each iteration, so a stop command arrives in ≤ one `motorCurrentDelay` (~20 ms).

---

## Cross-core boundary

Three objects cross cores — all deliberately minimal:

| Object | Direction | Type | Purpose |
|---|---|---|---|
| `cmdQueue` | Core 0 → Core 1 | `QueueHandle_t` (FreeRTOS) | Command lines as `char[48]` |
| `gStopRequested` | Core 0 → Core 1 | `volatile bool` | Instant stop reflex |
| `gServoAngle[8]` | Core 1 → Core 0 | `volatile int[8]` | Current angles for `pose` query |

`String`, `currentCommand`, `currentFaceName`, and all I2C calls are Core 1 only. This is what makes the design race-free without mutex overhead on the hot path.

---

## I2C bus

All three peripherals share one bus on GPIO5 (SDA) and GPIO6 (SCL). `Wire.begin()` is called once in `setup()` before any device is initialized; the Adafruit libraries then share the bus transparently.

| Device | Address | Purpose |
|---|---|---|
| PCA9685 | 0x40 | Servo PWM driver |
| SSD1306 | 0x3C | 128×64 OLED display |
| MPU-6050 | 0x68 | IMU (wired, driver not yet implemented) |

I2C is only accessed from Core 1. No locking is needed.

---

## Servo write path

```
applyCommandLine("servo 2 110")
    │
    ▼
setServoAngle(channel=2, angle=110)
    │
    ├─ trimmed = constrain(110 + servoTrim[2] + servoSubtrim[2], 0, 180)
    ├─ physical = servoRev[2] ? 180 - trimmed : trimmed
    ├─ ticks = map(physical, 0, 180, 150, 600)   // SERVOMIN/SERVOMAX
    └─ pwm.setPWM(servoChannel[2], 0, ticks)      // I2C to PCA9685
```

**Pulse calibration:** `SERVOMIN=150 / SERVOMAX=600` at 50 Hz equals 732–2929 µs, which is identical to the pulse range the original Sesame firmware used with `ESP32Servo`. Every angle in `movement-sequences.h` reproduces the same physical position with no re-tuning.

**Stagger:** `setServoAngle` calls `delayWithFace(motorCurrentDelay)` (default 20 ms) after each write. This is the original Sesame mechanism to avoid an inrush current spike when multiple servos move in sequence.

---

## WiFi topology

The robot runs in `WIFI_AP_STA` mode simultaneously:

- **Station** — joins your home network, registers mDNS hostname `quadruped.local`. This is how the Python host tools reach it on port 8888.
- **Access Point** — creates the `Sesame-Controller` hotspot. The captive-portal web UI (port 80) works whether or not the home network join succeeded.

mDNS service advertisements:
- `_http._tcp` on port 80 (original Sesame)
- `_robot._tcp` on port 8888 (Albert TCP protocol)

---

## Control transport summary

| Transport | Port | Protocol | Handled by |
|---|---|---|---|
| Browser | 80 | HTTP GET/POST | Core 0 `server.handleClient()` |
| Python tools / TCP | 8888 | Newline-framed text | Core 0 `serviceTcpClient()` |
| USB serial | — | Same text as TCP | Core 1 `loop()` serial reader |

All three ultimately call `applyCommandLine()` on Core 1 with the same command vocabulary.

---

## File map

```
firmware/
├── sesame-firmware-main.ino   — setup(), loop(), all function definitions
├── pins.h                     — pin assignments, I2C addresses, servo channel map, pulse calibration
├── movement-sequences.h       — all pose and gait functions; declares ServoName enum
├── face-bitmaps.h             — PROGMEM bitmap data for OLED faces
└── captive-portal.h           — inline HTML for the Sesame web UI
```
