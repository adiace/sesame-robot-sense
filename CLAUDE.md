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

### Stubbed — pins reserved, no driver code yet
- **MAX98357A audio** — I2S pins GPIO7/8/9 are locked on the AI Pin PCB, documented in `pins.h`. Do not reuse them.
- **OV2640 camera** — flex connector on the XIAO, no code. Use `esp32-camera` library when ready.

### Phase 1 complete — IMU events (serial only)
- **MPU-6050 IMU** — driver in `firmware/imu_handler.h`. Direct I2C, no library (clone chip returns WHO_AM_I=0x70; Adafruit library rejects it).
- `imuSetup()` called in `setup()` BEFORE `pwm.begin()` — missing PCA9685 corrupts I2C bus otherwise.
- `imuPoll()` called at top of `loop()` after `drainCommandQueue()`. Runs at 50Hz on Core 1.
- Detection algorithms (proven approaches, not naive thresholds):
  - **FLIPPED**: complementary filter pitch+roll > 120°, sustained 200ms
  - **SHAKEN**: variance of magnitude over 20-sample sliding window > 0.12 g²
  - **TAPPED**: jerk (dMag/dt) rising edge > 150 m/s²/s + 300ms lockout. Intentionally sensitive — designed for petting/gentle taps when stationary.
  - **PICKUP**: EMA residual (deviation from slow 4s average) > 2.5 m/s², sustained 150ms
  - **FREEFALL**: hardware FF_THR/FF_DUR registers via INT_STATUS bit 7
  - **LEVEL**: complementary filter angles < 15°, sustained 400ms
- Prints `IMU: <EVENT>` to Serial on state transition only.

### Phase 2 complete — IMU events over TCP
- IMU events pushed as JSON over the existing TCP connection (port 8888).
- **Cross-core**: Core 1 writes `gImuEvent / gImuAccel / gImuPitch / gImuRoll` (volatile globals in firmware.ino); Core 0 reads in `serviceTcpClient()`, sends JSON, clears the flag.
- JSON format: `{"type":"imu_event","event":"PICKUP","accel":1.02,"pitch":-2.1,"roll":0.5}`
- `accel` is in g units; `pitch` and `roll` are complementary-filter degrees.
- **`software/imu_receiver.py`** — connects to robot, reads lines, prints any `imu_event` JSON. Ignores all other TCP traffic (command echoes etc.).
- `robot_link.TcpLink.readline(timeout)` added to support the receiver's line-by-line read loop.
- **Phase 3 not started** — IMU reactions (face + movement) are next.

## Architecture rules — read before editing

### Core ownership (hard rules)
- **Core 0** owns: WiFi, AP, DNS, HTTP WebServer, TCP server. Lives in `networkTask()`.
- **Core 1** owns: PCA9685 (I2C), SSD1306 (I2C), MPU-6050 (I2C), `currentCommand` String, `currentFaceName`, all servo/face/OLED calls, serial CLI.
- **Never** call `setServoAngle()`, `setFace()`, or any I2C function from Core 0 or an HTTP handler.
- **Never** call `Wire.begin()` more than once.

### Cross-core boundary — the only three things that cross
1. `cmdQueue` (FreeRTOS `QueueHandle_t`, 8 × 48 chars) — Core 0 pushes, Core 1 drains via `drainCommandQueue()`
2. `volatile bool gStopRequested` — Core 0 sets, Core 1 clears in `loop()` and `pressingCheck()`
3. `volatile int gServoAngle[8]` — Core 1 writes (single writer), Core 0 reads for `pose` query only

### Command routing
All commands — TCP, HTTP, serial — eventually call `applyCommandLine(const char*)` on Core 1. HTTP handlers use `enqueueCommandLine()` to get there safely. Do not add direct servo/face calls to HTTP handlers.

### Servo write path
```
applyCommandLine() → setServoAngle(channel, angle)
  trimmed  = constrain(angle + servoTrim[ch] + servoSubtrim[ch], 0, 180)
  physical = servoRev[ch] ? 180 - trimmed : trimmed
  ticks    = map(physical, 0, 180, SERVOMIN=150, SERVOMAX=600)
  pwm.setPWM(servoChannel[ch], 0, ticks)
```
`SERVOMIN=150 / SERVOMAX=600` at 50 Hz = 732–2929 µs — matches original Sesame so movement-sequences.h angles need no changes.

## File responsibilities

| File | Purpose |
|---|---|
| `firmware/sesame-firmware-main.ino` | `setup()`, `loop()`, `networkTask()`, all function definitions |
| `firmware/pins.h` | All pin numbers, I2C addresses, PCA9685 channel map, pulse calibration constants |
| `firmware/movement-sequences.h` | All pose/gait functions; `ServoName` enum; `extern` declarations that reference globals in the .ino |
| `firmware/face-bitmaps.h` | PROGMEM bitmap data — do not edit unless adding faces |
| `firmware/captive-portal.h` | Inline HTML for the Sesame web UI — do not edit unless changing the web UI |
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

Left-side servos (L1/L2/L3/L4, channels 2/3/6/7) are mirror-mounted — expect `servoRev[]` to be `true` for those after calibration.

## Known limitations

- **`gait <L> <R>`** collapses to nearest discrete walk by sign — no sine-gait engine in Sesame. `circle`/`spin` moves in `moves.json` use discrete left/right turns as a result.
- **`stance8` / `stance4`** are not implemented in `applyCommandLine()`. If needed, add them following the pattern of `stance`.
- **`delayWithFace()`** no longer pumps the web server (Core 0 does that). It drains `cmdQueue` and animates the OLED. This is correct — do not add `server.handleClient()` back.

## I2C pins

- SDA: GPIO5 (D4)
- SCL: GPIO6 (D5)
- **`Wire.begin(I2C_SDA, I2C_SCL)`** — explicit pins required. `Wire.begin()` with no args no longer maps to GPIO5/6 on the current Seeed board package (confirmed via scan: no-args finds nothing; explicit pins find 0x3C and 0x68).
- `Wire.begin()` is called first in `setup()`, followed by `delay(500)` (MPU-6050 needs ~100ms after Vcc stable) then `imuSetup()` — before OLED or PCA9685 touch the bus.
- `display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR, false, false)` — the last two `false` args prevent the SSD1306 library from calling `Wire.begin()` again internally, which would corrupt bus state.
- `Adafruit_SSD1306` constructed with `clkDuring=400000, clkAfter=400000` to prevent the library from dropping the clock to 100kHz after each display transaction.
- All MPU-6050 I2C reads use `Wire.endTransmission(true)` (STOP+START) not `false` (repeated start) — repeated start is buggy on ESP32-S3 arduino-esp32.

## WiFi config

Credentials are in the `.ino` at the top:
```cpp
#define NETWORK_SSID "..."
#define NETWORK_PASS "..."
```
Hostname: `quadruped` → `quadruped.local:8888`. TCP port: 8888. Matches `robot_link.TCP_PORT`.
