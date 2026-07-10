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

MAX98357A GAIN → **currently wired to 3V3 = 6dB, nearly the QUIETEST setting**.
Datasheet gain table: 100kΩ-to-GND=15dB, GND=12dB, floating=9dB, VDD=6dB,
100kΩ-to-VDD=3dB. **Pending hardware fix: move GAIN wire from 3V3 to GND for a
free +6dB** (do it together with the mic port seal). No digital gain on the speaker
path — loudness comes from the companion app's Python maximizer (compression +
peak normalize) on each TTS clip.

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

**TCP commands are bounded** (voice must return to wake listening — the wake feed is
OFF while a command runs): gaits take an optional count (`walk 5`, `left 2` — runs N
cycles via gStepLimit then stands and clears) and default to 8 steps / 4 turn cycles
when uncounted; tricks (`dance`…) run once (gOneShotPose). Aliases: walk→forward,
back→backward. HTTP portal press-and-hold stays continuous. Chained voice commands
("walk 5 steps then turn left") are sequenced by the companion app (`_run_chain`),
which polls /api/status until idle between steps.

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

- **`dlog()` → TCP port 8890** (`nc sesame-robot.local 8890`) with a 4KB replay ring —
  you see recent history on connect. Plain `Serial.print` does NOT reach 8890
  (esp_log_set_vprintf only hooks IDF logs); use dlog for anything WiFi-visible.
- **`/api/status` voice diagnostics** (no USB needed): `wakeReady` (model+PSRAM ok),
  `wakeChunksFed` (climbs 32/s when listening — 0 or stalled = feed dead/gated),
  `micRms` (~1000-2000 gained ambient; ~0 = mic wiring), `wakeDetections`.
- **A wedged robot** (HTTP dead, ping alive): loop() is blocked in a voice session or
  endless pose. `printf 'stop\n' | nc <ip> 8888` unsticks it. Waits are bounded
  (15s WAV wait, 30s stream) so it self-recovers.
- OTA from CLI: compile with `--output-dir`, then
  `python3 espota.py -i <ip> -p 3232 --auth=sesame -f firmware.ino.bin`
- mDNS collision warning: sesame-robot-sense also broadcasts `sesame-robot.local` — don't
  run both robots at once, or change one hostname. macOS Python resolves .local flakily;
  the companion app caches the robot IP at ~/.sesame/robot_ip and seeds it from
  incoming voice connections.

## Voice tuning state (what's been tried)

- Wake: DET_MODE_95 (90 missed through the enclosure), MIC_GAIN 4, backlog-draining
  feed. Beep: 1500Hz/70ms/amp 9000 with 5ms fades (full volume rang the enclosure and
  poisoned the VAD; amp 3000 was inaudible outside the body).
- VAD noise floor = rolling ambient EMA from the wake feed (a local calibration pass
  ate ~180ms of audio and chopped first words: "is it Monday" → "Even Monday").
- STT (companion app): Whisper small + beam search + peak-normalize + pre-emphasis
  (+6dB/oct — measured clip had 90% energy <1kHz, 3% >4kHz; consonants gone) +
  phrase-biased initial_prompt + difflib fuzzy rescue on 1-2 word utterances.
- **Known limit**: the mic port hole low-passes speech; "stand" arrives as "and".
  Pending physical fix: seal INMP441 port flush against the case hole. Verify with
  the spectrum: `~/.sesame/last_heard.wav` — 4-8kHz band should rise well above 3%.

## Compile from CLI

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=custom,PSRAM=opi,FlashMode=qio,USBMode=hwcdc,CDCOnBoot=cdc" firmware
```
