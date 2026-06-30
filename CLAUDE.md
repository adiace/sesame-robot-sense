# sesame-robot-sense — Claude context

Fork of dorianborian/sesame-robot adapted for the **Seeed XIAO ESP32-S3 Sense** on the techiesms AI Pin PCB. The original Sesame servo drive (ESP32Servo, direct GPIO) is replaced by a PCA9685 I2C PWM driver. All original Sesame movement sequences, OLED faces, and web UI are preserved.

## What is and isn't implemented

### Done
- PCA9685 servo drive replacing ESP32Servo — `firmware/sesame-firmware-main.ino` + `firmware/pins.h`
- Dual-core architecture: Core 0 = WiFi/DNS/HTTP/TCP, Core 1 = servos/OLED/serial
- STA + AP WiFi simultaneously (`quadruped.local` on home network, `Sesame-Controller` AP)
- TCP command server on port 8888 (Albert line-protocol) — matches `robot_link.py` with zero changes
- Full calibration CLI: `servo`, `nudge`, `trim`, `rev`, `save`/`load`/`clear`, `map`, `pose`, `dump`
- NVS persistence of runtime trims and reversal flags via `Preferences`
- All Sesame poses and gaits intact (forward/back/left/right/stand/rest/wave/dance/etc.)
- Python host stack in `software/`: `robot_link.py`, `robot.py`, `robot_gui.py`, `voice_control.py`, `moves.json`

### Phase 3 complete — IMU reactions (face + movement)
- **MPU-6050 IMU** — driver in `firmware/imu_handler.h`. Direct I2C, no library (clone chip returns WHO_AM_I=0x70; Adafruit library rejects it).
- Events: PICKUP → scared face + wiggle; FLIPPED → dizzy face; TAPPED → love face + small wiggle; FREEFALL → scared face; LEVEL → return to idle
- `imuEmit()` dedup: all events suppress re-fire until a different event, except TAPPED (can repeat within episode)
- Deferred tap: 2-poll confirmation — jerk spike sets `tapPending`; next poll checks mag returned to baseline (prevents pickup jolt triggering a tap)
- TAPPED reaction uses `love` face (44 bytes different from idle — heart icon + changed mouth, clearly visible). `happy` and `excited` are too similar to `idle` to use.
- `setFace()` guard bypass: `currentFaceName = ""` before reaction face calls, because `runWiggle()` → `enterIdle()` resets face to idle first
- IMU events pushed as JSON over TCP: `{"type":"imu_event","event":"PICKUP","accel":1.02,"pitch":-2.1,"roll":0.5}`

### Phase 4 complete — Audio playback (pending hardware fix)
- **MAX98357A I2S amp** — driver in `firmware/audio_handler.h`
- WAV files in SPIFFS (`/data/` folder): `hehe.wav`, `woah_flying.wav`, `upside_down.wav`, `falling.wav` — all mono 16-bit 16kHz PCM
- I2S pins confirmed from PCB wiring diagram: **LRC=D4/GPIO5, BCLK=D6/GPIO43, DIN=D8/GPIO7**
- GPIO5 (D4) is shared between I2S LRC (PCB trace) and I2C SDA. Resolved by moving I2C SDA to D9/GPIO8 in `pins.h`. **Requires physical rewire of SDA wire from D4 to D9 on the Sesame robot hardware.**
- Startup beep on boot (1kHz, ~130ms) confirms I2S hardware path
- `audioPump()` called from `loop()` and `delayWithFace()` — streams mono WAV as stereo (L=R) without blocking
- **Known hardware issue**: audio garbled on USB power (5V rail noise from switch-mode supply). Fix: move MAX98357A Vcc from 5V pin → 3.3V pin. Clear audio confirmed on 3.7V battery.
- Standalone test sketch at `audio_test/audio_test.ino` — plays all WAVs in a loop, no other hardware needed

### Phase 5a built — voice assistant (not yet tested)
- **Architecture**: hold MODE_BUTTON_PIN (GPIO2) → ESP32 records PDM mic → HTTP POST raw PCM to laptop service → laptop runs STT+LLM+TTS → returns WAV → ESP32 plays it
- **Trigger**: IMU tap (TAPPED event) — no button needed. Replaces the old hehe/love reaction.
- **Recording**: VAD-based — mic auto-stops after ~1.2s of silence. 5s timeout if no speech detected.
- **Mic**: PDM on I2S_NUM_0 (shared with MAX98357A — ESP32-S3 only supports PDM on I2S0). `micRecordWithVAD()` uninstalls the speaker driver, installs PDM, records, then calls `audioReinstall()`. GPIO42=CLK, GPIO41=DATA. If silent, swap these in `mic_handler.h`.
- **Recording buffer**: 10s max, allocated from 8MB PSRAM via `ps_malloc()`
- **Voice service** (`software/voice_service.py`): Flask on port 5005. faster-whisper STT (local) → Ollama llama3.2 LLM (local) → pyttsx3 TTS (macOS system voices). All free, no API keys.
- **Setup**: `bash software/setup_voice.sh` then set `VOICE_SERVER_IP` in `firmware/voice_config.h`
- **Faces during interaction**: excited = listening, idle = thinking, resumes idle after playback

### Stubbed — no driver code yet
- **OV2640 camera** — flex connector on the XIAO Sense module, no code. Phase 5c: use `esp32-camera` library, capture JPEG, send with voice request for vision queries.

## Pending hardware changes

Before the main firmware fully works end-to-end:
1. **Resolder I2C SDA** from XIAO D4 → D9 (one wire on the Sesame robot). Without this, I2S setup corrupts the I2C bus and OLED/IMU stop working after boot.
2. **Move MAX98357A Vcc** from XIAO 5V pin → 3.3V pin. Fixes garbled audio on USB power.

## Architecture rules — read before editing

### Core ownership (hard rules)
- **Core 0** owns: WiFi, AP, DNS, HTTP WebServer, TCP server. Lives in `networkTask()`.
- **Core 1** owns: PCA9685 (I2C), SSD1306 (I2C), MPU-6050 (I2C), `currentCommand` String, `currentFaceName`, all servo/face/OLED calls, serial CLI, audio pump.
- **Never** call `setServoAngle()`, `setFace()`, or any I2C function from Core 0 or an HTTP handler.
- **Never** call `Wire.begin()` more than once.

### Cross-core boundary — the only things that cross
1. `cmdQueue` (FreeRTOS `QueueHandle_t`, 8 × 48 chars) — Core 0 pushes, Core 1 drains via `drainCommandQueue()`
2. `volatile bool gStopRequested` — Core 0 sets, Core 1 clears in `loop()` and `pressingCheck()`
3. `volatile int gServoAngle[8]` — Core 1 writes (single writer), Core 0 reads for `pose` query only
4. `imuEventQueue` (FreeRTOS queue) — Core 1 writes IMU JSON strings, Core 0 reads and sends over TCP

### Command routing
All commands — TCP, HTTP, serial — eventually call `applyCommandLine(const char*)` on Core 1. HTTP handlers use `enqueueCommandLine()` to get there safely. Do not add direct servo/face calls to HTTP handlers.

### Servo write path
```
applyCommandLine() → setServoAngle(channel, angle)
  trimmed  = constrain(angle + servoTrim[ch] + servoSubtrim[ch], 0, 180)
  physical = servoRev[ch] ? 180 - trimmed : trimmed
  ticks    = map(physical, 0, 180, SERVOMIN=102, SERVOMAX=491)
  pwm.setPWM(servoChannel[ch], 0, ticks)
```
`SERVOMIN=102 / SERVOMAX=491` at 50 Hz / 25 MHz = 500–2400 µs (MG90S safe range).
Original Sesame used 150/600 (732–2929 µs) which exceeded MG90S physical limits and caused stall/gear damage.
Movement-sequences.h stand pose angles updated during bring-up; gait/trick angles need physical re-tuning.

## File responsibilities

| File | Purpose |
|---|---|
| `firmware/sesame-firmware-main.ino` | `setup()`, `loop()`, `networkTask()`, all function definitions |
| `firmware/pins.h` | All pin numbers, I2C addresses, PCA9685 channel map, pulse calibration constants |
| `firmware/audio_handler.h` | MAX98357A I2S driver — `audioSetup()`, `playWavFromSPIFFS()`, `audioPump()`, `stopAudio()` |
| `firmware/imu_handler.h` | MPU-6050 driver — `imuSetup()`, `imuPoll()`, `imuConsumeReaction()` |
| `firmware/movement-sequences.h` | All pose/gait functions; `ServoName` enum; `extern` declarations that reference globals in the .ino |
| `firmware/face-bitmaps.h` | PROGMEM bitmap data — do not edit unless adding faces |
| `firmware/captive-portal.h` | Inline HTML for the Sesame web UI — do not edit unless changing the web UI |
| `audio_test/audio_test.ino` | Standalone WAV playback test — no I2C, no servos, just I2S |
| `firmware/mic_handler.h` | PDM mic driver — `micSetup()`, `micRecordWhileHeld()`, `micBuffer()` |
| `firmware/voice_handler.h` | HTTP client — POSTs PCM to laptop service, saves WAV response to SPIFFS |
| `firmware/voice_config.h` | Server IP/port — edit `VOICE_SERVER_IP` before flashing |
| `software/voice_service.py` | Laptop AI pipeline: faster-whisper + Ollama + pyttsx3, Flask on port 5005 |
| `software/setup_voice.sh` | One-time setup: installs Python deps, pulls llama3.2, prints laptop IP |
| `software/robot_link.py` | Transport layer (TCP + serial); used by all other tools |
| `software/moves.json` | Voice/GUI action library; edit here to add new moves |

## Servo channel map

| PCA9685 ch | Name | Joint |
|---|---|---|
| 0 | R1 | Right front hip |
| 1 | R2 | Right rear hip |
| 2 | L1 | Left front hip |
| 3 | L2 | Left rear hip |
| 4 | R4 | Right front knee |
| 5 | R3 | Right rear knee |
| 6 | L3 | Left front knee |
| 7 | L4 | Left rear knee |

Left-side servos (L1/L2/L3/L4, channels 2/3/6/7) are mirror-mounted. Mirror mounting is handled by using mirrored angles in movement-sequences.h (not servoRev flags). servoRev is false for all servos; servoSubtrim holds the per-servo mechanical center offsets.

## I2C pins

- SDA: **GPIO8 (D9)** — moved from D4 to avoid conflict with I2S LRC on GPIO5. Requires physical SDA wire rewire.
- SCL: GPIO6 (D5)
- **`Wire.begin(I2C_SDA, I2C_SCL)`** — explicit pins required. `Wire.begin()` with no args no longer maps correctly on the current Seeed board package.
- `Wire.begin()` is called first in `setup()`, followed by `delay(500)` (MPU-6050 needs ~100ms after Vcc stable) then `imuSetup()` — before OLED or PCA9685 touch the bus.
- `display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR, false, false)` — the last two `false` args prevent the SSD1306 library from calling `Wire.begin()` again internally, which would corrupt bus state.
- `Adafruit_SSD1306` constructed with `clkDuring=400000, clkAfter=400000` to prevent the library from dropping the clock to 100kHz after each display transaction.
- All MPU-6050 I2C reads use `Wire.endTransmission(true)` (STOP+START) not `false` (repeated start) — repeated start is buggy on ESP32-S3 arduino-esp32.

## I2S / Audio pins (AI Pin PCB — do not reuse)

| Signal | XIAO pin | GPIO |
|---|---|---|
| BCLK | D6 | GPIO43 |
| LRC (WS) | D4 | GPIO5 |
| DIN (data to amp) | D8 | GPIO7 |

GPIO5 (D4) is physically traced to both MAX98357A LRC and the I2C bus on the PCB. I2C SDA was moved to GPIO8 to resolve the conflict.

## WiFi config

Credentials are in the `.ino` at the top:
```cpp
#define NETWORK_SSID "..."
#define NETWORK_PASS "..."
```
Hostname: `quadruped` → `quadruped.local:8888`. TCP port: 8888. Matches `robot_link.TCP_PORT`.

## Known limitations

- **Audio garbled on USB power** — USB 5V rail noise. MAX98357A Vcc is on a PCB trace to the XIAO 5V pin; cannot be moved. On LiPo battery the 5V pin is ~3.7V (cleaner) so audio is clear. Fix options in order of ease: (1) use a quality phone charger instead of a laptop USB port; (2) solder a 100µF electrolytic + 100nF ceramic cap across the MAX98357A module's Vcc/GND pads — no trace cutting, just adds decoupling to existing pads.
- **PICKUP won't re-fire within ~1500ms** — `imuEmit()` dedup blocks until LEVEL fires. Acceptable; kept simple.
- **`gait <L> <R>`** collapses to nearest discrete walk by sign — no sine-gait engine in Sesame.
- **`stance8` / `stance4`** not implemented in `applyCommandLine()`.
- **`delayWithFace()`** no longer pumps the web server (Core 0 does that). It drains `cmdQueue` and animates the OLED. Do not add `server.handleClient()` back.
