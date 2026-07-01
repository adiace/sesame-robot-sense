# Wiring

## Board

**Seeed XIAO ESP32-S3 Sense** on the **techiesms AI Pin PCB**

---

## Power

```
2S LiPo (7.4V)
    └── TPS565201 5V UBEC
            ├── PCA9685 V+  (servo power rail)
            └── XIAO 5V pin (logic power)
```

Servos draw their current from the PCA9685 V+ rail, powered independently from the XIAO logic supply. Common ground throughout.

> **Audio quality note:** The MAX98357A Vcc is on the XIAO 5V pin via a PCB trace. On USB power the 5V rail carries switching noise that garbles audio. Use battery power or a quality USB charger, or add a 100µF electrolytic + 100nF ceramic cap across the amp module's Vcc/GND pads.

---

## XIAO ESP32-S3 Sense pin assignments

| XIAO pin | GPIO | Connected to | Notes |
|---|---|---|---|
| D4 | GPIO5 | I2S LRCLK (WS) | MAX98357A — fixed by AI Pin PCB trace |
| D5 | GPIO6 | I2C SCL | Shared bus: PCA9685 + OLED + IMU |
| D6 | GPIO43 | I2S BCLK | MAX98357A — fixed by AI Pin PCB trace |
| D8 | GPIO7 | I2S DIN (data) | MAX98357A — fixed by AI Pin PCB trace |
| D9 | GPIO8 | I2C SDA | Moved from D4 to avoid I2S LRCLK conflict |
| D0 | GPIO1 | Battery ADC | Resistor divider |
| D1 | GPIO2 | Mode button | Pull-up, active low |
| D3 | GPIO4 | MPU-6050 INT | IMU interrupt (hardware interrupt disabled in firmware — clone chip fires spuriously) |
| D6 | GPIO43 | UART TX / I2S BCLK | Boot-strapping pin — safe after boot |
| D7 | GPIO44 | UART RX | Boot-strapping pin — safe after boot |
| — | GPIO41 | PDM mic DATA | XIAO Sense camera module connector |
| — | GPIO42 | PDM mic CLK | XIAO Sense camera module connector |

> **I2C SDA wire:** The AI Pin PCB routes I2C SDA to D4/GPIO5, which conflicts with the I2S LRCLK trace. A physical wire must be run from the I2C SDA devices to D9/GPIO8 instead. The firmware uses `Wire.begin(8, 6)` to match.

---

## I2C bus

SDA: **GPIO8 (D9)** · SCL: **GPIO6 (D5)**

All three I2C peripherals share the same bus. `Wire.begin(8, 6)` is called once in `setup()`; all libraries share the bus transparently.

Add **4.7 kΩ pull-up resistors** from SDA and SCL to 3.3V if not already present on your breakout boards.

| Device | Address |
|---|---|
| PCA9685 servo driver | 0x40 |
| SSD1306 OLED 128×64 | 0x3C |
| MPU-6050 IMU | 0x68 |

---

## Servo driver — PCA9685

I2C address: **0x40** · PWM frequency: **50 Hz**

| PCA9685 channel | Servo name | Joint |
|---|---|---|
| 0 | R1 | Right front hip |
| 1 | R2 | Right rear hip |
| 2 | L1 | Left front hip |
| 3 | L2 | Left rear hip |
| 4 | R4 | Right front knee |
| 5 | R3 | Right rear knee |
| 6 | L3 | Left front knee |
| 7 | L4 | Left rear knee |

Pulse range: tick 150 (~732 µs) at 0° → tick 600 (~2929 µs) at 180°. Matches the original Sesame firmware exactly.

---

## Display — SSD1306 OLED

128×64 pixels · I2C address **0x3C** · shared bus · same usage as original Sesame.

---

## IMU — MPU-6050

I2C address **0x68** · direct I2C driver (no library — clone returns WHO_AM_I=0x70, rejected by Adafruit)

Detects: **tap** (petting), **pickup**, **flip**, **freefall**. Events trigger face changes and movement reactions. Hardware interrupt pin (D3/GPIO4) is wired but unused — the clone chip fires INT spuriously, so freefall and motion detection use software thresholds instead.

---

## Microphone — PDM (XIAO Sense)

The PDM microphone is on the XIAO ESP32-S3 Sense camera expansion board. Pins are on the camera module flex connector — no external wiring required.

| Signal | GPIO |
|---|---|
| CLK | 42 |
| DATA | 41 |

Used for on-device wake word detection (ESP-SR WakeNet, phrase "Hi ESP") and 4-second voice recording after wake.

---

## Audio — MAX98357A

I2S amplifier wired directly on the AI Pin PCB. Pins are fixed by PCB traces and must not be reused.

| Signal | XIAO pin | GPIO |
|---|---|---|
| LRCLK (WS) | D4 | GPIO5 |
| BCLK | D6 | GPIO43 |
| DIN (data) | D8 | GPIO7 |

Plays WAV files from PSRAM (voice responses) and SPIFFS (built-in sound effects).

---

## Camera — OV2640

Flex-cable connector on the XIAO ESP32-S3 Sense, front-mounted. Not yet implemented in firmware.
