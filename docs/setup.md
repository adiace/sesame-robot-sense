# Setup & Flashing

Every step, in order, assuming a fresh machine. Wiring first: [wiring.md](wiring.md).

## 1. Arduino IDE + board package

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. **Settings/Preferences → Additional Boards Manager URLs**, paste:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. **Tools → Board → Boards Manager** → search `esp32` → install **esp32 by Espressif
   Systems** (3.x).
4. **Sketch → Include Library → Manage Libraries** → install:
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`

## 2. Board settings (Tools menu)

| Setting | Value | Why |
|---|---|---|
| Board | **ESP32S3 Dev Module** | |
| Flash Size | **16MB (128Mb)** | wake-word model lives at flash offset 0xC10000 |
| Partition Scheme | **Custom** | reads `firmware/partitions.csv` from the sketch folder |
| PSRAM | **OPI PSRAM** | WakeNet crashes without PSRAM (guarded, but no wake word) |
| Flash Mode | **QIO 80MHz** | the flash chip is QIO; only the PSRAM is OPI |

Everything else stays default.

## 3. Configure WiFi and the companion app address

```bash
cd firmware
cp wifi_credentials.h.example wifi_credentials.h
```

Edit `wifi_credentials.h` with your network name and password. It's gitignored — your
credentials never end up in a commit.

Edit `firmware/voice_config.h` → `VOICE_SERVER_IP` = your laptop's IP
(`ipconfig getifaddr en0` on macOS, `hostname -I` on Linux).

## 4. First flash — USB, servos disconnected

> ⚠️ **Unplug the servos before any USB flash.** The servo rail draws more than USB 5V
> can supply; with servos attached the board browns out and the flash fails or the
> robot crashes mid-write. This is a one-time inconvenience — after this flash, all
> updates go over WiFi.

1. Connect USB, **Tools → Port** → select the serial port, click **Upload** (→).
2. Flash the ESP-SR wake-word model to its partition (one time; firmware updates never
   touch it):

   ```bash
   ~/Library/Arduino15/packages/esp32/tools/esptool_py/*/esptool \
     --chip esp32s3 -p /dev/cu.usbmodem101 write-flash 0xC10000 \
     ~/Library/Arduino15/packages/esp32/tools/esp32s3-libs/*/esp_sr/srmodels.bin
   ```

   (Adjust the port name — check Tools → Port. On Windows the tools live under
   `%LOCALAPPDATA%\Arduino15\...`.)

3. Open **Serial Monitor** at 115200 and press the board's reset button. Expected boot:

   ```
   Audio: I2S ready            ← plus two beeps from the speaker
   [Wake] Loading model: wn9_hiesp
   [Wake] Hi ESP ready — chunk=512 samples @ 16000Hz
   Voice: 4s PSRAM buffer
   Network IP: 192.168.x.x
   OTA ready (password: sesame)
   Ready.
   ```

   If you see `[Wake] No PSRAM` → the PSRAM setting in step 2 is wrong.
   If `[Wake] No models found` → step 4.2 didn't run or used the wrong offset.

## 5. All later flashes — OTA over WiFi

Reconnect the servos. From now on:

- **Arduino IDE**: Tools → Port → under "network ports", pick
  `sesame-robot at sesame-robot.local`. Password when prompted: `sesame`.
- **Command line**:
  ```bash
  # compile to a folder
  arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=custom,PSRAM=opi,FlashMode=qio" \
    --output-dir /tmp/build firmware
  # push it to the robot
  python3 ~/Library/Arduino15/packages/esp32/tools/esp32-arduino-libs/../espota.py \
    -i sesame-robot.local -p 3232 --auth=sesame -f /tmp/build/firmware.ino.bin
  ```
  (`find ~/Library/Arduino15 -name espota.py` if the path differs.)

## 6. Health check without USB

```bash
curl http://sesame-robot.local/api/status
```

Key fields: `wakeReady` (model loaded), `wakeChunksFed` (climbs ~32/s while listening),
`micRms` (~500–2500 ambient; 0 = mic wiring problem), `wakeDetections`.

Live firmware log over WiFi:

```bash
nc sesame-robot.local 8890
```

## 7. Companion app

Install [sesame-companion-app-sense](https://github.com/adiace/sesame-companion-app-sense)
on the laptop whose IP you put in `voice_config.h`. Then: say **"Hi ESP"** → beep →
speak a command.
