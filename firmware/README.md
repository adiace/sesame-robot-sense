# Firmware — Voice Edition

Main sketch: `firmware.ino`. Open the `firmware/` folder as the sketch in Arduino IDE.
Setup and flashing: [../docs/setup.md](../docs/setup.md).

## Files

| File | Purpose |
|---|---|
| `firmware.ino` | `setup()`/`loop()`: WiFi (STA+AP), web server, TCP command server (:8888), servo control, faces, wake-word feed, command dispatch |
| `audio_handler.h` | I2S full-duplex driver — MAX98357A speaker + INMP441 mic on one bus; beep, WAV streaming (TCP→I2S, zero-copy), VAD recording |
| `wakeword_handler.h` | ESP-SR WakeNet "Hi ESP" detector; loads `wn9_hiesp` from the model partition; health counters for `/api/status` |
| `voice_handler.h` | Client for the companion app: streams recorded PCM to :8889, plays the WAV reply |
| `voice_config.h` | **Edit me**: companion app (laptop) IP + port |
| `wifi_credentials.h.example` | Copy to `wifi_credentials.h` (gitignored) and fill in your WiFi |
| `wifi_log.h` | `dlog()` — debug log mirrored to TCP :8890 with history replay |
| `movement-sequences.h` | All poses and gaits (from upstream, plus `box` pose and bounded-gait support) |
| `face-bitmaps.h` | OLED face bitmaps (upstream, unchanged) |
| `captive-portal.h` | Embedded web UI HTML (upstream + settings persistence fixes) |
| `partitions.csv` | 16 MB flash layout — OTA app slots, SPIFFS, `model` partition at 0xC10000 for WakeNet |

## Required libraries

- **esp32 by Espressif** board package 3.x (Boards Manager)
- **Adafruit GFX** + **Adafruit SSD1306** (Library Manager)

Everything else (I2S driver, ESP-SR/WakeNet, OTA, mDNS) ships inside the board package.

## Board settings

ESP32S3 Dev Module · Flash 16MB · Partition Scheme **Custom** · PSRAM **OPI** ·
Flash Mode **QIO 80MHz**. Details and the one-time model flash:
[../docs/setup.md](../docs/setup.md).

## Quick test after flashing

```bash
curl http://sesame-robot.local/api/status      # health JSON
nc sesame-robot.local 8888                     # then type: wave
nc sesame-robot.local 8890                     # live debug log
```

Command vocabulary: [../docs/commands.md](../docs/commands.md).
Voice pipeline internals: [../docs/voice.md](../docs/voice.md).
