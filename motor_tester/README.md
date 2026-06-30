# Motor Tester

Standalone Arduino sketch for bringing up and calibrating the 8 MG90S servos on the Sesame robot via PCA9685 I2C PWM driver. Use this **before** flashing the main firmware.

## Hardware

| Component | Connection |
|-----------|------------|
| XIAO ESP32-S3 SDA | D9 / GPIO8 |
| XIAO ESP32-S3 SCL | D5 / GPIO6 |
| PCA9685 VCC | XIAO 3.3V |
| PCA9685 GND | XIAO GND |
| PCA9685 V+ | UBEC 5V (3A minimum) |
| PCA9685 V+ GND | UBEC GND (shared with XIAO GND) |

> **Note:** SDA is on D9 (GPIO8), not D4. D4 (GPIO5) is shared with I2S LRC on the AI Pin PCB and corrupts the I2C bus if used for SDA.

## Channel Map

| PCA9685 Ch | Servo | Joint |
|------------|-------|-------|
| 0 | R1 | Right front hip |
| 1 | R2 | Right rear hip |
| 2 | L1 | Left front hip |
| 3 | L2 | Left rear hip |
| 4 | R4 | Right front knee |
| 5 | R3 | Right rear knee |
| 6 | L3 | Left front knee |
| 7 | L4 | Left rear knee |

## PWM Settings

- Frequency: **50Hz** (standard analog servo)
- Pulse range: **500µs–2400µs** (SERVOMIN=102, SERVOMAX=491 at 25MHz oscillator)
- No `setOscillatorFrequency()` call — uses PCA9685 default 25MHz

> The original Sesame firmware used 732–2929µs (SERVOMIN=150, SERVOMAX=600 with 27MHz). That range exceeds MG90S physical limits and causes motor stall/failure. The 500–2400µs range matches `myservo.attach(pin, 500, 2400)`.

---

## Step-by-Step Bring-Up

### 1. Prerequisites
- Adafruit PWM Servo Driver Library installed in Arduino IDE
- SDA wire physically moved from D4 → D9 on the robot
- UBEC (3A+) connected to PCA9685 V+ and GND

### 2. Flash the tester
Open `motor_tester.ino` in Arduino IDE, select **XIAO ESP32S3**, flash.

### 3. Verify I2C
Open Serial Monitor at **115200 baud**. On boot you should see:
```
Scanning I2C bus...
  Found: 0x3C (OLED)
  Found: 0x40 (PCA9685)
  Found: 0x68 (IMU)
  Found: 0x70 (PCA9685)
PCA9685: OK
```
If PCA9685 is missing: check VCC (3.3V), SDA (D9), SCL (D5) wiring.

### 4. Test each servo before installing
Use a spare PCA9685 channel (e.g. ch9) as a test bench:
```
test
```
Plug each servo in — it should rock 90→60→120→90 smoothly. Send any key to stop. Discard any servo that doesn't move or twitches continuously.

### 5. Connect all 8 servos
Plug each servo into its channel (ch0–ch7). Send:
```
all,90
```
All 8 should snap to center.

### 6. Attach servo horns
With all servos at 90°, press each horn onto the shaft at the mechanically neutral position (horizontal for hips, vertical for knees). Do **not** screw down yet.

### 7. Find trim centers
For each servo, nudge the angle until it sits with no mechanical stress. These are your trim centers:

```
0,85    # nudge until R1 hip is neutral
1,92    # R2 hip
2,98    # L1 hip
3,86    # L2 hip
4,85    # R4 knee
5,92    # R3 knee
6,90    # L3 knee
7,94    # L4 knee
```

Once centered, tighten the horn screw.

**Calibrated centers for this build:**

| Ch | Servo | Center |
|----|-------|--------|
| 0 | R1 hip | 85° |
| 1 | R2 hip | 92° |
| 2 | L1 hip | 98° |
| 3 | L2 hip | 86° |
| 4 | R4 knee | 85° |
| 5 | R3 knee | 92° |
| 6 | L3 knee | 90° |
| 7 | L4 knee | 94° |

### 8. Find per-channel limits
For each servo, nudge toward 0° and 180° until it strains or hits the frame. Set the limit:
```
limit 0 80 135
```

**Calibrated limits for this build:**

| Ch | Servo | Min | Max |
|----|-------|-----|-----|
| 0 | R1 hip | 80° | 135° |
| 1 | R2 hip | 40° | 100° |
| 2 | L1 hip | 40° | 100° |
| 3 | L2 hip | 80° | 135° |
| 4 | R4 knee | 40° | 140° |
| 5 | R3 knee | 40° | 135° |
| 6 | L3 knee | 42° | 140° |
| 7 | L4 knee | 40° | 140° |

These are baked into the sketch and enforced on every command.

### 9. Determine servo directions
For each servo, command min and max and note which direction each moves. Servos that move backwards need `rev` set in the main firmware.

**Directions for this build:**

| Ch | Servo | Down/Forward angle |
|----|-------|-------------------|
| 0 | R1 hip | 120° = forward |
| 1 | R2 hip | 40° = forward (reversed) |
| 2 | L1 hip | 40° = forward (reversed) |
| 3 | L2 hip | 120° = forward |
| 4 | R4 knee | 40° = down |
| 5 | R3 knee | 135° = down (reversed) |
| 6 | L3 knee | 40° = down |
| 7 | L4 knee | 135° = down (reversed) |

Reversed servos (R2/ch1, L1/ch2, R3/ch5, L4/ch7) need `rev` in the main firmware calibration.

### 10. Verify stand and rest
```
stand    # robot stands up
rest     # robot relaxes to neutral
```

**Stand pose angles for this build:**
- Hips: R1=120, R2=55, L1=55, L2=120
- Knees: R4=45, R3=135, L3=45, L4=135

---

## Commands Reference

| Command | Description |
|---------|-------------|
| `id,angle` | Move single channel, e.g. `0,90` |
| `all,angle` | Move all 8 channels |
| `stop` | Cut signal to all channels (motors go limp) |
| `sweep` | Toggle non-blocking sweep 60°↔120° on all channels |
| `test` | Cycle ch9 for testing replacement servos |
| `stand` | Stand pose |
| `rest` | Rest pose (trimmed centers) |
| `limit ch min max` | Set per-channel safe range, e.g. `limit 0 80 135` |
| `limits` | Print all channel limits and current angles |
| `?` | Print help |

---

## Troubleshooting

**PCA9685 not found:** Check VCC (3.3V to PCA9685), SDA on D9, SCL on D5.

**Motors not responding after boot:** The PCA9685 needs an active PWM cycle. Send `all,90` or `rest` first. The tester auto-refreshes every 500ms to keep channels alive.

**Motor twitches continuously:** Wrong pulse width or frequency. Confirm SERVO_FREQ_HZ=50 and SERVOMIN/SERVOMAX match values above. Do not use SERVOMAX=600 — exceeds MG90S limits.

**Motor hums in stand:** Normal for analog servos holding position under load. Should stop when `rest` is sent.

**Motor stalls/strips gears:** Limit exceeded. Never command past the per-channel limits. Replace with a new servo and run `test` on ch9 before installing.

**Power issues (random dropouts, twitching under load):** UBEC must be 3A minimum. A 1A supply cannot drive 8 servos simultaneously. Ensure UBEC GND is shared with XIAO GND.
