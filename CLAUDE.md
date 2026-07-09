# sesame-robot-voice — Claude context

Fork of dorianborian/sesame-robot on an **ESP32-S3 Dev Module** (N16R8: 16MB flash, 8MB OPI PSRAM)
with direct-GPIO servo drive (ledcAttach) and a full voice pipeline: on-device "Hi ESP" wake word
(ESP-SR WakeNet), INMP441 mic, MAX98357A speaker, and a laptop companion app for STT/LLM/TTS.

Sister repos:
- `../sesame-robot-sense` — XIAO ESP32-S3 Sense variant (PCA9685 servos, PDM mic, camera)
- `../sesame-companion-app-sense` — laptop voice/vision server, works with both robots

## Arduino IDE settings (ESP32S3 Dev Module) — all required

| Setting | Value | Why |
|---|---|---|
| Flash Size | 16MB (128Mb) | model partition lives at 0xC10000 |
| Partition Scheme | **Custom** | uses `firmware/partitions.csv` |
| PSRAM | **OPI PSRAM** | WakeNet9 crashes (NULL-write boot loop) without PSRAM |
| Flash Mode | QIO 80MHz | flash is QIO; only the PSRAM is OPI |

**OPI PSRAM + WiFi works fine with these settings.** An earlier session concluded "OPI PSRAM
breaks WiFi TX" — that was wrong (probably Flash Mode was set to OPI). Do not disable PSRAM;
WakeNet requires it, and `wakewordSetup()` now guards with `ESP.getPsramSize()==0` → clean
disable instead of crash.

## Partition layout (`firmware/partitions.csv`)

```
app0    0x010000  3MB   (ota_0)
app1    0x310000  3MB   (ota_1)
spiffs  0x610000  6MB
model   0xC10000  3.9MB (ESP-SR srmodels.bin — wn9_hiesp)
```

One-time model flash (survives firmware uploads; re-flash only after full chip erase):
```bash
~/Library/Arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool \
  --port /dev/cu.usbmodem101 write-flash 0xC10000 \
  ~/Library/Arduino15/packages/esp32/tools/esp32s3-libs/3.3.10/esp_sr/srmodels.bin
```

## Wiring

### I2S — single duplex port (I2S_NUM_0 master), speaker + mic share clocks
| Signal | GPIO | Goes to |
|---|---|---|
| BCLK | 1 | MAX98357A BCLK + INMP441 SCK |
| WS/LRC | 2 | MAX98357A LRC + INMP441 WS |
| MIC SD | 3 | INMP441 SD (L/R → GND = left channel) |
| AMP DIN | 14 | MAX98357A DIN |

MAX98357A GAIN → 3V3 (15dB). No digital gain on the speaker path — loudness comes
from `sox norm -1` normalization on the companion-app side.

Mic path has `MIC_GAIN 4` software gain (`audio_handler.h`) because the mic is
enclosed in the robot body behind a port hole. VAD thresholds scale with it
automatically. If wake word is still deaf: seal mic port to the case hole first,
then try MIC_GAIN 8, then DET_MODE_95 in wakeword_handler.h.

### Servos (direct GPIO, ledcAttach 50Hz/14-bit, 500–2400µs)
Channel order R1,R2,L1,L2,R4,R3,L3,L4 → GPIO {4,5,6,7,10,11,12,13}

### I2C (OLED SSD1306 @0x3C): SDA=8, SCL=9

## Voice pipeline

1. `wakeword_handler.h` — WakeNet wn9_hiesp fed from `loop()`; guards `ESP.getPsramSize()==0`
   (WakeNet9 NULL-writes and boot-loops without PSRAM). The feed **drains the whole mic DMA
   backlog every loop pass** — OLED face redraws block ~25ms and a single small read let the
   ring overflow, making WakeNet miss syllables mid-phrase.
2. On "Hi ESP": beep, then **200ms settle** before recording — the enclosed speaker couples
   into the mic and the beep tone otherwise arms the VAD (Whisper transcribed it as "2.").
3. `audio_handler.h` `micRecord()` — VAD state machine, all constants scale with MIC_GAIN:
   - flush stale DMA audio first (beep tail, the "Hi ESP" itself)
   - noise calibration ~180ms → threshold = noise×1.5 + 150·G, clamp [400·G, 2000·G]
     (1.5× not 2.0×: the enclosure attenuates speech more than the electrical noise floor)
   - speech arms only after 3 consecutive loud chunks (90ms) — single blips don't count
   - user may pause up to **5s** before speaking; leading silence trimmed to 300ms pre-roll
   - stops ~810ms after speech ends; returns 0 if no speech ever seen (nothing uploaded)
4. `voice_handler.h` — raw PCM → TCP to companion app (`voice_config.h`: IP + port 8889);
   forces STA netif via `esp_netif_set_default_netif()` (AP+STA routing bug workaround)
5. Response WAV is **streamed** TCP→I2S in 512-byte chunks (`playWavFromClient`) — never
   buffered in RAM; server WAVs can be arbitrarily large
6. Server replies length 0 = no speech / gibberish → robot stays silent, no retry

First response after companion-app start takes ~9s (Whisper/Ollama warm-up); then fast.

## Command interfaces

All three set the same `currentCommand` dispatched in `loop()`:
- **TCP port 8888** (`serviceTcpCommands`) — newline lines from the companion app:
  pose names, `face <name>`, `stop`, `sleep`, `wake`, `vision start` (answered "no camera").
  Serviced from `loop()` **and** `delayWithFace()` so `stop` interrupts a running gait.
- **HTTP** — captive portal `/cmd`, `/api/command`, `/subtrim`, `/getSettings`, `/setSettings`
- **Serial CLI** — `rn wf`, `subtrim`, etc.

Special commands (mirror sesame-robot-sense):
- `sleep` — rest pose, 1s settle, `detachServos()` (unpowered, no wear), sleepy face
- `wake` — re-attach, stand, happy face
- `box` — wide low boxing stance (35° knees, sense's final tuning)
- unknown commands are dropped (an unrecognized `currentCommand` used to block the
  wake-word listener forever)

## Boot behavior

Boot goes to the **stand pose with NVS trims applied** (staggered 100ms/servo), then idle
face. Do not "init to 90°": all-90° is the rest pose, and raw writes skip trims — that
made the robot slump untrimmed on every boot.

Settings (`frameDelay`, `walkCycles`, `motorDelay`, `faceFps`) persist to NVS on
`/setSettings` and load at boot. WiFi credentials live in gitignored
`firmware/wifi_credentials.h` (copy from `.example`).

## Debugging

- `wifi_log.h` mirrors all Serial output to TCP port 8890: `nc <robot-ip> 8890`
- OTA: ArduinoOTA, password "sesame", hostname sesame-robot.local
- `[Mic] recorded ... peak=X thresh=Y` after each interaction: if peak << thresh the user
  is too quiet/far; if noise calibration reads high right after wake, the beep tail is
  still audible when recording starts (extend the 200ms settle).
- mDNS collision warning: sesame-robot-sense also broadcasts `sesame-robot.local` — don't
  run both robots at once, or change one hostname.

## Compile from CLI

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=custom,PSRAM=opi,FlashMode=qio,USBMode=hwcdc,CDCOnBoot=cdc" firmware
```
