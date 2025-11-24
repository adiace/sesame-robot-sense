# Sesame: The Quadruped for Everyone!

<img width="963" height="618" alt="sesamehi" src="https://github.com/user-attachments/assets/b6bd7157-87c9-4e10-ac91-b82226e93337" />

**Greetings, from your new best friend.**

Sesame is an innovative and accessible Open-Source robotics project meticulously crafted for makers and engineers of all skill levels. From absolute beginners to seasoned professionals, Sesame offers a dynamic platform designed to fuel innovation and encourage hands-on learning.

This repository contains the CAD files for the body and the base firmware for the Arduino-based controller.

## Features

*   **4-Legged Design:** Uses 8 servo motors (2 per leg) to achieve roughly 2.5 degrees of freedom per limb.
*   **Emotive Display:** Features a 128x64 OLED screen acting as a reactive face that syncs with movement.
*   **Fully Printable:** Designed entirely for 3D printing in PLA with minimal supports.
*   **Serial CLI:** Control the robot and trigger animations via a Serial Command Line Interface.
*   **Pre-programmed Emotes:** Includes animations for Walking, Waving, Dancing, Swimming, Pointing, Resting, and Standing.

## Hardware Requirements

To build a Sesame, you will need the following core components:

*   **Microcontroller:** Arduino-compatible AVR board (Uno/Nano/Mega).
*   **Actuators:** 8x MG90 Micro Metal Gear Servo Motors.
*   **Driver:** PCA9685 16-Channel PWM/Servo Driver.
*   **Display:** 0.96" I2C OLED Display (SSD1306 driver, 128x64).
*   **Power:** 5V Power Supply suitable for 8 servos (approx 2-3A).
*   **Body:** 3D Printed Parts (Files available in the `/CAD` directory).

## Fabrication

Sesame is designed to be printed in **PLA**. Most parts are designed to be printed without supports, though specific orientation guidelines are provided in the Build Guide Wiki.

*   **Material:** PLA / PLA+
*   **Infill:** 15-20%
*   **Supports:** Minimal (Check individual file notes)

## Firmware

The firmware provided in this repository controls the servo kinematics and the OLED face updates.

> **Note from the Creator:** I am a hardware and design specialist, not a software wizard. This firmware works and provides a great platform, but it is basic. The community is highly encouraged to contribute, refactor, and improve the code logic.

### Dependencies

You will need the following libraries installed in your Arduino IDE:
*   `Wire.h` (Standard)
*   `Adafruit_PWMServoDriver.h`
*   `Adafruit_GFX.h`
*   `Adafruit_SSD1306.h`

### Setup

1.  Connect your servos to the PCA9685 board (Channels 0-7).
2.  Connect the OLED via I2C (Address `0x3C`).
3.  Upload `firmware.c` (rename to `.ino` for Arduino IDE) to your board.
4.  Open the Serial Monitor at **9600 baud**.

### Serial Commands

Sesame features a built-in Command Line Interface (CLI). Type these commands into the Serial Monitor to control the robot:

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `run` | `rest` | Robot goes to sleep/rest mode. |
| `run` | `stand` | Robot stands up. |
| `run` | `walk` | Initiates the walking cycle. |
| `run` | `wave` | Robot waves hello. |
| `run` | `dance` | Robot performs a dance. |
| `run` | `swim` | Robot performs a swimming motion. |
| `run` | `point` | Robot points with a limb. |
| `set` | `<ch> <angle>` | Set specific servo (0-15) to angle (0-180). |
| `set` | `all <angle>` | Set **all** servos to a specific angle. |
| `set` | `seq <angle>` | Set servos sequentially (good for testing). |

## Customizing Faces

The faces are stored as byte arrays in `PROGMEM` within the firmware file. You can create your own faces using any standard 128x64 bitmap converter (or MS Paint exported to C-array).

1.  Create your image (128x64 pixels, monochrome).
2.  Convert image to a hex array.
3.  Paste the array into the top of the firmware file.
4.  Call `updateFaceBitmap(your_new_bitmap_name)` in the code.

## Kits & Pre-Orders

If you do not have a 3D printer or prefer to get all the hardware in one box, limited pre-orders for full Sesame Build Kits are available. These kits include all the proper servos, drivers, fasteners, and printed parts to build your own Sesame.

*   **Shipping:** Q1 2026
*   **Store Link:** [Link to your store here]

## Contributing
<img width="2464" height="1728" alt="sesame-customize" src="https://github.com/user-attachments/assets/b3b51c94-483d-457e-9645-a0548705dbc7" />

This robot is a platform for building new features and ideas. Since the current firmware is a basic implementation, pull requests are very welcome for:
*   Kinematics improvements
*   New animations
*   Sensor integration (Ultrasonic, Gyro, etc.)

---

*Created by [Dorian Todd]. Watch the build videos on [https://www.youtube.com/@DorianToddYT/](https://www.youtube.com/@DorianToddYT/).*
