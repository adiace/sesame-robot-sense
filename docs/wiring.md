# Wiring — Voice Edition (ESP32-S3 on Distro Board V3)

The base robot is wired per the original
[Distro Board V3 guide](https://github.com/dorianborian/sesame-robot/tree/main/docs/wiring-guide).
This page covers everything the voice edition adds or that the firmware depends on.

## I2S audio — one full-duplex bus

The INMP441 (mic) and MAX98357A (amp) share the same I2S clock pair. The ESP32-S3 is the
I2S master on `I2S_NUM_0`; TX (speaker) and RX (mic) run simultaneously — the robot can
play audio and listen at the same time. 16 kHz, 16-bit.

```
                       ESP32-S3
                  ┌────────────────┐
   INMP441        │                │        MAX98357A
  ┌────────┐      │                │      ┌───────────┐
  │  SCK ●─┼──────┤ GPIO 1 ├───────┼──────┼─● BCLK    │
  │   WS ●─┼──────┤ GPIO 2 ├───────┼──────┼─● LRC     │
  │   SD ●─┼──────┤ GPIO 3         │      │           │
  │  L/R ●─┼──GND │        GPIO 14 ├──────┼─● DIN     │
  │  VDD ●─┼──3V3 │                │  GND─┼─● GAIN    │   + ●──[Speaker +]
  │  GND ●─┼──GND │                │  3V3─┼─● VIN     │   − ●──[Speaker −]
  └────────┘      │                │  GND─┼─● GND     │
                  └────────────────┘      └───────────┘
```

| Signal | GPIO | Notes |
|---|---|---|
| BCLK (shared) | 1 | to INMP441 SCK **and** MAX98357A BCLK |
| WS / LRC (shared) | 2 | to INMP441 WS **and** MAX98357A LRC |
| Mic data in | 3 | INMP441 SD |
| Amp data out | 14 | MAX98357A DIN |

- INMP441 **L/R → GND**: mic outputs on the left channel (the firmware reads left).
- Both modules run on **3V3** (do not put the MAX98357A on 5V — noisy rail = garbled audio).

## MAX98357A GAIN pin

| GAIN wiring | Gain |
|---|---|
| 100 kΩ resistor to GND | 15 dB (max) |
| **direct to GND** | **12 dB — recommended** |
| unconnected | 9 dB |
| direct to 3V3 | 6 dB |
| 100 kΩ resistor to 3V3 | 3 dB (min) |

Counter-intuitive: tying GAIN high makes it *quieter*. If the robot is too soft and GAIN
is on 3V3, moving that one wire to GND doubles perceived loudness.

## Microphone placement

The INMP441's sound port is the small hole on the board. It must sit **flush against a
hole in the body shell**, sealed with foam tape or blu-tack so sound can't leak into the
body cavity. An air gap acts as a low-pass filter — measured on this build, a badly
sealed mic passed only 3% of speech energy above 4 kHz, which deletes consonants
("stand" transcribes as "and"). Speech recognition quality is decided here, not in
software.

## Servos

Direct GPIO drive (arduino-esp32 `ledcAttach`, 50 Hz / 14-bit), channel order
R1, R2, L1, L2, R4, R3, L3, L4:

| Servo | GPIO | | Servo | GPIO |
|---|---|---|---|---|
| R1 (front right hip) | 4 | | R4 (front right knee) | 10 |
| R2 (rear right hip) | 5 | | R3 (rear right knee) | 11 |
| L1 (front left hip) | 6 | | L3 (front left knee) | 12 |
| L2 (rear left hip) | 7 | | L4 (rear left knee) | 13 |

Pulse range **500–2400 µs** (MG90S spec). The original firmware used 732–2929 µs, which
drives the servo past its physical travel into the internal hard stop.

## I2C (OLED)

| Signal | GPIO |
|---|---|
| SDA | 8 |
| SCL | 9 |

SSD1306 128×64 at address 0x3C.

## Power notes

- Servo rail comes from the battery through the V3 board, as in the original build.
- **USB alone cannot power the robot with servos attached** — flashing over USB with
  servos connected crashes the board. First flash with servos unplugged; after that,
  flash OTA ([setup.md](setup.md)).
- The firmware also mitigates electrical abuse in software: brownout detector disabled,
  staggered servo attach at boot, motor sleep after idle (see README → "Changes from
  the original firmware").
