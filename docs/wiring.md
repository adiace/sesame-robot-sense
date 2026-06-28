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

Servos draw their current from the PCA9685 V+ rail, which is powered independently from the XIAO logic supply. Common ground throughout.

---

## XIAO ESP32-S3 Sense pin assignments

| XIAO pin | GPIO | Connected to | Notes |
|---|---|---|---|
| D4 | GPIO5 | I2C SDA | Shared bus: PCA9685 + OLED + IMU |
| D5 | GPIO6 | I2C SCL | Shared bus: PCA9685 + OLED + IMU |
| D8 | GPIO7 | MAX98357A BCLK | **Locked** — I2S on AI Pin PCB |
| D9 | GPIO8 | MAX98357A LRCLK | **Locked** — I2S on AI Pin PCB |
| D10 | GPIO9 | MAX98357A DIN | **Locked** — I2S on AI Pin PCB |
| D0 | GPIO1 | Battery ADC | Resistor divider; reads ~0–3.1V |
| D1 | GPIO2 | Mode button | Pull-up, active low |
| D2 | GPIO3 | Spare | — |
| D3 | GPIO4 | MPU-6050 INT | IMU interrupt |
| D6 | GPIO43 | UART TX | Strapping pin — safe after boot |
| D7 | GPIO44 | UART RX | Strapping pin — safe after boot |

> **Note on GPIO43/44:** These are boot-strapping pins. Do not drive them LOW at power-on. They are safe for UART use after the ESP32 has booted.

---

## I2C bus

All three I2C peripherals share the same SDA/SCL lines. `Wire.begin()` is called once in `setup()`; all libraries share the bus.

Add **4.7 kΩ pull-up resistors** from SDA and SCL to 3.3V if they are not already on your breakout boards.

| Device | Address | Connected via |
|---|---|---|
| PCA9685 servo driver | 0x40 | D4 (SDA) / D5 (SCL) |
| SSD1306 OLED 128×64 | 0x3C | D4 (SDA) / D5 (SCL) |
| MPU-6050 IMU | 0x68 | D4 (SDA) / D5 (SCL) |

---

## Servo driver — PCA9685

I2C address: **0x40**

Connect PCA9685 to the I2C bus (SDA/SCL) and power the V+ rail from the UBEC. Each servo plugs into a numbered output channel on the PCA9685.

### Servo channel map

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

Servo signal wires (orange/yellow) go to the PCA9685 channel outputs. Power (red) and ground (brown/black) are supplied by the V+ rail on the PCA9685.

### Pulse calibration

| Parameter | Value |
|---|---|
| PWM frequency | 50 Hz |
| Pulse at 0° | ~732 µs (tick 150) |
| Pulse at 180° | ~2929 µs (tick 600) |

These match the original Sesame firmware's pulse range so movement-sequence angles are unchanged.

---

## Display — SSD1306 OLED

128×64 pixels, I2C address **0x3C**

Connects to the shared I2C bus. Same library and usage as the original Sesame project.

---

## IMU — MPU-6050

I2C address **0x68**

Wired to the shared I2C bus via the IMU INT pin on D3/GPIO4. Driver not yet implemented — pin is reserved.

---

## Audio — MAX98357A

I2S amplifier, wired directly on the AI Pin PCB. Pins are fixed by the PCB layout and **must not be reused**.

| Signal | XIAO pin | GPIO |
|---|---|---|
| BCLK | D8 | GPIO7 |
| LRCLK (WS) | D9 | GPIO8 |
| DIN (data) | D10 | GPIO9 |

Driver not yet implemented.

---

## Camera — OV2640

Flex-cable connector on the XIAO ESP32-S3 Sense, front-mounted. Pins are managed by the `esp32-camera` library; no external wiring required. Driver not yet implemented.
