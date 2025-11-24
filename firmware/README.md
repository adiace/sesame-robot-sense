# Firmware

This directory contains the firmware code for controlling the Sesame Robot.

## Directory Structure

- **arduino/** - Main Arduino firmware code
- **libraries/** - Custom libraries and dependencies

## Dependencies

Required Arduino libraries:
- `Wire.h` (Standard)
- `Adafruit_PWMServoDriver.h`
- `Adafruit_GFX.h`
- `Adafruit_SSD1306.h`

Install these libraries through the Arduino IDE Library Manager.

## Setup Instructions

1. **Board Setup:** Select your Arduino-compatible board in the Arduino IDE Tools menu
2. **Connections:**
   - Connect servos to PCA9685 board (Channels 0-7)
   - Connect PCA9685 SDA/SCL to Arduino I2C pins
   - Connect OLED display via I2C
3. **Upload:** Upload the firmware to your Arduino board
4. **Monitor:** Open Serial Monitor at **115200 baud**

## Features

- Servo kinematics control for 8 servos (2 per leg)
- OLED face/emote display updates
- Serial CLI for control and animation triggers
- Pre-programmed animations: Walking, Waving, Dancing, Swimming, Pointing, Resting, Standing

## Contributing

The firmware is a basic implementation designed as a platform for improvement. Contributions welcome for:
- Kinematics improvements
- New animations
- Sensor integration
- Code optimization
