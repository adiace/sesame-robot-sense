# Setup & Calibration

## Prerequisites

**Arduino IDE 2.x** with the following installed via Library Manager:

| Library | Notes |
|---|---|
| `Adafruit PWM Servo Driver` | Replaces `ESP32Servo` — remove that if present |
| `Adafruit SSD1306` | Same as original Sesame |
| `Adafruit GFX Library` | Same as original Sesame |

**Board package:** esp32 by Espressif Systems (v2.x+)
Board selection: `XIAO_ESP32S3`

---

## Step 1 — Configure WiFi

Open `firmware/sesame-firmware-main.ino` and set your network credentials near the top:

```cpp
#define NETWORK_SSID "your-network-name"
#define NETWORK_PASS "your-password"
```

The robot joins this network on boot and advertises itself as `quadruped.local` — which is how `robot_link.py` and the GUI find it. The Sesame AP (`Sesame-Controller`) also stays up at the same time, so the browser web UI keeps working.

Leave `ENABLE_NETWORK_MODE true`. If your network isn't available the robot falls back to AP-only; the TCP server still starts but won't be reachable at `quadruped.local`.

---

## Step 2 — Flash

1. Connect XIAO via USB-C
2. Open `firmware/sesame-firmware-main.ino` in Arduino IDE (ensure all `.h` files are in the same folder)
3. **Tools → Board → XIAO_ESP32S3**
4. **Tools → USB CDC On Boot → Enabled** (required for serial output)
5. Select the correct port and upload

Open Serial Monitor at **115200 baud**. You should see:

```
WiFi: connecting to your-network...
WiFi: connected, IP = 192.168.x.y
mDNS: quadruped.local:8888
TCP command server on port 8888
HTTP server & Captive Portal started.
```

If you see `WiFi: failed` the robot is still usable — connect to `Sesame-Controller` AP and use the web UI, or connect by IP over TCP.

---

## Step 3 — Bring-up sequence

Do this before assembling the legs. All commands work over USB serial (Arduino Serial Monitor) or TCP.

### 3a. Center all servos

```
neutral
```

All 8 servos move to 90°. Fit servo horns while the servos are in this position. This is the reference point for all calibration.

### 3b. Test each servo individually

```
servo 0 70      → move R1 hip toward 70°
servo 0 110     → move R1 hip toward 110°
servo 0 90      → return to center
```

Verify each servo:
- Moves in the expected direction for its joint (hip swings fore/aft, knee extends/retracts)
- Reaches both ends of travel without binding

Servo index map:

| ID | Name | Joint |
|---|---|---|
| 0 | R1 | Right front hip |
| 1 | R2 | Right rear hip |
| 2 | L1 | Left front hip |
| 3 | L2 | Left rear hip |
| 4 | R4 | Right front knee |
| 5 | R3 | Right rear knee |
| 6 | L3 | Left front knee |
| 7 | L4 | Left rear knee |

### 3c. Fix reversed servos

Left-side servos are mirror-mounted and typically need direction flipped. If a servo moves the wrong way:

```
rev 2           → flip L1 direction (toggle; drives the servo immediately)
rev             → list current reversal state for all 8 servos
```

Reversal flags are not saved automatically — run `save` after you've set them all.

### 3d. Fine-tune trim

If a servo horn is slightly off-center after fitting:

```
trim 2 -8       → apply −8° offset to L1 (re-drives immediately)
trim 5 3        → apply +3° to R3
nudge 0 5       → move R1 +5° from its current position (useful for small tweaks)
```

Trim range is −45 to +45 degrees. It stacks on top of the compile-time `servoSubtrim[]`.

### 3e. Check the full state

```
map             → all servos: channel, effective trim, reversal
pose            → current logical angle of each servo
```

### 3f. Save to flash

```
save            → writes all trim values and reversal flags to NVS flash
```

Values are restored automatically on every boot via `calLoad()` in `setup()`. You don't need to reflash to change calibration.

---

## Step 4 — Verify stand pose

Once legs are assembled:

```
stand
```

Adjust individual trims until the robot stands level:

```
trim 0 3        → tweak R1 hip
trim 4 -5       → tweak R4 knee
save            → persist
```

Repeat with `rest` to check the folded position.

---

## Step 5 — Test gaits

```
forward         → walk forward (loops until you send stop)
stop
left
right
backward
```

If the gait is uneven, use `nudge` or `trim` on the offending hip/knee while the robot is standing, then `save`.

---

## Baking calibration into source (optional)

Once you're happy, run:

```
dump
```

This prints a ready-to-paste line:

```cpp
int8_t servoSubtrim[8] = {3, 0, -8, 2, -5, 0, 1, 0};  // bake these into source then clear NVS
```

Copy that into `sesame-firmware-main.ino`, reflash, then run `clear` to wipe NVS so the values don't double-stack.

---

## Python host tools

With the robot on your network, connect from a Mac/Linux machine (clone the Albert firmware repo for the scripts):

```bash
python3 robot.py "stand"                    # single command
python3 robot.py --seq <<'EOF'              # timed sequence
stand
wait 1
forward
wait 3
stop
EOF

python3 robot_gui.py                        # desktop control panel + voice
python3 voice_control.py                    # always-on offline voice control
```

All tools connect to `quadruped.local:8888` by default. Use `--host <ip>` if mDNS isn't available on your network.

---

## NVS commands quick reference

| Command | Effect |
|---|---|
| `save` | Write current `servoTrim[]` and `servoRev[]` to NVS flash |
| `load` | Reload from NVS (also called automatically on boot) |
| `clear` | Wipe NVS; all runtime trims and reversals reset to zero |
| `dump` | Print a `servoSubtrim[]` line to paste into source |
