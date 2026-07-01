# Architecture

## Overview

The firmware splits into two cores that must never share I2C or String state:

```
┌─────────────────────────────────────────────────────────────────┐
│  Core 0  (networkTask — pinned)                                 │
│                                                                 │
│  WiFi STA + AP  ·  DNS  ·  HTTP :80  ·  TCP :8888              │
│                                                                 │
│  never touches I2C / String / currentCommand                    │
└────────────────────────┬────────────────────────────────────────┘
                         │  cmdQueue  (FreeRTOS, 8 × 48 chars)
                         │  volatile gStopRequested
                         │  volatile gServoAngle[8]  (read-only on Core 0)
                         │  imuEventQueue  (JSON strings, Core 1 → Core 0)
┌────────────────────────┴────────────────────────────────────────┐
│  Core 1  (Arduino loop())                                       │
│                                                                 │
│  PCA9685 · SSD1306 · MPU-6050  (all I2C)                       │
│  MAX98357A I2S speaker · PDM microphone                         │
│  currentCommand · face state · serial CLI                       │
│  all pose functions and movement sequences                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## Core 0 — networking

`networkTask()` runs in a tight loop and services:

1. **DNS server** — captive-portal redirect for devices on the Sesame AP
2. **HTTP WebServer** — original Sesame web UI on port 80; translates HTTP requests into queue entries
3. **TCP server** — Albert line-protocol on port 8888; buffers newline-framed commands and routes through `routeTcpLine()`
4. **IMU event relay** — drains `imuEventQueue` and pushes JSON over open TCP connections

Core 0 never calls `setServoAngle()`, `setFace()`, or writes any `String` shared with Core 1.

---

## Core 1 — motion

`loop()` runs on Core 1 and does, in order every iteration:

1. `drainCommandQueue()` — pop items Core 0 pushed and call `applyCommandLine()`
2. Stop reflex — check `gStopRequested`
3. OLED animation — `updateAnimatedFace()` / `updateIdleBlink()`
4. Audio pump — `audioPump()` streams the next 256 bytes of the active WAV to I2S
5. IMU poll — `imuPoll()` runs tap/pickup/flip/freefall detection; emits events
6. IMU reaction dispatch — `handleImuReaction()` triggers face + movement on events
7. Wake word check — if ESP-SR WakeNet fired, record 4s PCM and stream to companion app
8. Pose dispatch — continue whichever `currentCommand` is running
9. Serial CLI — read and route USB serial input

All blocking pose functions also drain the queue and check `gStopRequested`, so a stop arrives in ≤ one frame delay (~20 ms).

---

## Cross-core boundary

| Object | Direction | Type | Purpose |
|---|---|---|---|
| `cmdQueue` | Core 0 → Core 1 | FreeRTOS queue (8 × 48 chars) | Command lines |
| `gStopRequested` | Core 0 → Core 1 | `volatile bool` | Instant stop reflex |
| `gServoAngle[8]` | Core 1 → Core 0 | `volatile int[8]` | Current angles for `pose` query |
| `imuEventQueue` | Core 1 → Core 0 | FreeRTOS queue (JSON strings) | IMU events for TCP push |

`String`, `currentCommand`, `currentFaceName`, and all I2C calls are Core 1 only.

---

## I2C bus

SDA: **GPIO8 (D9)** · SCL: **GPIO6 (D5)**

`Wire.begin(8, 6)` is called once in `setup()`. GPIO5 (D4) would conflict with the I2S LRCLK trace on the AI Pin PCB, so SDA was moved to GPIO8. All three devices share the bus; no locking needed since I2C is Core 1 only.

| Device | Address |
|---|---|
| PCA9685 servo driver | 0x40 |
| SSD1306 OLED | 0x3C |
| MPU-6050 IMU | 0x68 |

---

## Servo write path

```
applyCommandLine("servo R1 110")
    │
    ▼
setServoAngle(channel=0, angle=110)
    │
    ├─ trimmed  = constrain(110 + servoTrim[0] + servoSubtrim[0], 0, 180)
    ├─ physical = servoRev[0] ? 180 - trimmed : trimmed
    ├─ ticks    = map(physical, 0, 180, 150, 600)
    └─ pwm.setPWM(0, 0, ticks)   // I2C to PCA9685
```

Servo names (R1, R2, L1, L2, R3, R4, L3, L4) and integer indices (0–7) are both accepted by `servo`, `nudge`, `trim`, and `rev` commands.

---

## Audio

**Speaker:** MAX98357A on I2S_NUM_1 (standard TX, IDF 5.x new-channel API).

`playWavFromSPIFFS(path)` — plays built-in sound effects from SPIFFS. Non-blocking: sets up file state and returns. `audioPump()` in the main loop streams 256 bytes per iteration.

`playWavFromMemory(buf, len)` — plays voice responses directly from PSRAM without any SPIFFS write. Takes ownership of the buffer; `stopAudio()` frees it. Used for the robot voice pipeline because TTS WAVs can exceed SPIFFS capacity.

**Microphone:** PDM on I2S_NUM_0 (GPIO42 CLK / GPIO41 DATA). A FreeRTOS task on Core 1 feeds samples to ESP-SR WakeNet continuously. On "Hi ESP" detection the task self-suspends; the main loop records 4 s of audio and streams it to the companion app.

---

## IMU reactions

`imuPoll()` runs at ~50 Hz and detects:

| Event | Trigger | Reaction |
|---|---|---|
| TAPPED | jerk spike > 38 m/s²/s, 2s peak window quiet | `love` face + small wiggle |
| PICKUP | sustained upward accel > 3.2 m/s² for 160 ms | `scared` face + wiggle |
| FLIPPED | flipAngle > 100° for 5 consecutive samples | `dizzy` face |
| FREEFALL | mag < 3.9 m/s² for 120 ms | `scared` face |
| LEVEL | tilt < 15° for 3 s | return to idle |

Tap detection uses a 100-sample (2-second) peak window: the max `|jerk|` seen in the last 2 seconds must be below 18 before a tap can fire. The peak is checked from history *before* the current sample is stored, so the tap spike doesn't block its own detection.

---

## WiFi topology

`WIFI_AP_STA` mode simultaneously:

- **Station** — joins home network, registers mDNS `quadruped.local`. Used by Python tools and the companion app.
- **Access Point** — `Sesame-Controller` hotspot; captive-portal web UI on port 80 always available.

---

## Control transport summary

| Transport | Port | Protocol |
|---|---|---|
| Browser | 80 | HTTP GET/POST |
| Companion app / Python | 8888 | Newline-framed text (persistent TCP) |
| USB serial | — | Same vocabulary as TCP |
| Robot voice (listen) | 8889 | [uint32 PCM len][PCM bytes] → [uint32 WAV len][WAV bytes] |
| Debug log / IMU events | 8890 | Text lines pushed by firmware |

All command transports ultimately call `applyCommandLine()` on Core 1.

---

## File map

```
firmware/
├── firmware.ino              — setup(), loop(), networkTask(), applyCommandLine()
├── pins.h                    — pin assignments, I2C addresses, servo channel map
├── movement-sequences.h      — all pose and gait functions; ServoName enum
├── imu_handler.h             — MPU-6050 driver; tap/pickup/flip/freefall detection
├── audio_handler.h           — MAX98357A I2S driver; SPIFFS + RAM playback
├── mic_handler.h             — PDM mic driver; ESP-SR WakeNet integration; VAD recording
├── voice_handler.h           — PCM → companion app streaming; WAV response playback
├── voice_config.h            — companion app IP and port
├── wifi_log.h                — dlog() macro; dual Serial + WiFi TCP log output
├── face-bitmaps.h            — PROGMEM OLED bitmap data
└── captive-portal.h          — inline HTML for Sesame web UI
```
