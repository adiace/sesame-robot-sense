# Firmware

Arduino firmware for sesame-robot-sense — XIAO ESP32-S3 Sense + PCA9685 fork of [sesame-robot](https://github.com/dorianborian/sesame-robot).

Companion software lives in **[sesame-companion-app-sense](https://github.com/adiace/sesame-companion-app-sense)** — clone it separately and run `./start.sh` to launch the GUI, serial monitor, and robot voice receiver.

See [Setup & Calibration](../docs/calibration.md) for Arduino IDE prerequisites and bring-up procedure.

## Files

| File | Purpose |
|---|---|
| `firmware.ino` | `setup()`, `loop()`, `networkTask()`, HTTP/TCP handlers, all function definitions |
| `pins.h` | Pin assignments, I2C addresses, PCA9685 channel map, servo pulse calibration |
| `movement-sequences.h` | All pose and gait functions; `ServoName` enum |
| `face-bitmaps.h` | PROGMEM bitmap arrays for every OLED face and animation frame |
| `captive-portal.h` | Inline HTML for the Sesame AP web UI |
| `audio_handler.h` | MAX98357A I2S speaker driver — `audioSetup()`, `playWavFromSPIFFS()`, `audioPump()` |
| `imu_handler.h` | MPU-6050 direct I2C driver — `imuSetup()`, `imuPoll()`, `imuConsumeReaction()` |
| `mic_handler.h` | PDM mic + ESP-SR WakeNet — `micSetup()`, `micRecord4s()`, `micWakeTriggered()` |
| `voice_handler.h` | Voice pipeline — streams PCM to companion app, receives and plays WAV response |
| `voice_config.h` | Companion app IP/port (`VOICE_SERVER_IP`, `AUDIO_RX_PORT`) — edit before flashing |
| `wifi_log.h` | `dlog()` / `dlogs()` — mirrors Serial output to TCP port 8890 |
| `wifi_credentials.h` | `NETWORK_SSID` / `NETWORK_PASS` — gitignored, never commit |
| `partitions.csv` | Custom 8 MB flash layout — required for the ESP-SR WakeNet model partition |

## `data/` — SPIFFS audio assets

Upload via Arduino IDE → Sketch → Upload Filesystem Image:

| File | Played when |
|---|---|
| `woah_flying.wav` | Robot lifted (PICKUP) |
| `upside_down.wav` | Robot flipped upside-down |
| `hehe.wav` | Robot tapped/petted (TAPPED) |
| `falling.wav` | Freefall detected |

## Required libraries

Install via Arduino IDE Library Manager:

- `Adafruit PWM Servo Driver Library`
- `Adafruit SSD1306`
- `Adafruit GFX Library`
- `ESP32` board package (Espressif, v3.x) — includes ESP-SR and I2S driver-ng

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

## Voice pipeline

1. ESP-SR WakeNet detects "Hi ESP" on-device (no audio streaming until triggered)
2. Robot records 4 s of PCM into PSRAM
3. `voice_handler.h` sends `[uint32 len][PCM]` to companion app on port 8889
4. Companion app: faster-whisper STT → Ollama LLM → macOS `say`+`afconvert` TTS → WAV
5. Firmware receives `[uint32 len][WAV]`, loads into PSRAM, plays directly on speaker (no SPIFFS write)
6. TCP command from companion app sets face/movement simultaneously

Flash the ESP-SR WakeNet model once after initial firmware flash:
```bash
esptool.py --chip esp32s3 -p /dev/tty.usbmodem* write_flash 0x480000 \
  ~/.arduino15/packages/esp32/hardware/esp32/3.*/tools/esp_sr/models/wn9_hiesp_tts_v*.bin
```

## Architecture

See [Architecture](../docs/architecture.md) for dual-core design, I2C bus topology, servo write path, and cross-core safety model.
