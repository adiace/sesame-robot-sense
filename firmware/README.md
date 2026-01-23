# Sesame Robot Firmware

This document provides technical information on the firmware architecture, control logic, and hardware abstraction layers used in the Sesame Robot.

>[!NOTE]
> The firmware is now organized into a modular structure with a main entry point and specialized header files for bitmaps, movement, and web assets. This makes customization much easier and the codebase cleaner.

## Firmware Architecture

The firmware is split into several key files to keep the logic organized and assets easy to manage:

- **[sesame-firmware-main.ino](sesame-firmware-main.ino)**: The main entry point containing the `setup()`, `loop()`, and core system logic.
- **[face-bitmaps.h](face-bitmaps.h)**: A dedicated header for OLED face macros and raw bitmap data.
- **[movement-sequences.h](movement-sequences.h)**: Definitions for all procedural movement and pose animations.
- **[captive-portal.h](captive-portal.h)**: Contains the HTML, CSS, and JS for the web-based remote control.

## Technical Implementation Overview

The firmware is built on the Arduino-ESP32 framework. Currently the firmware is running on a single-core event loop, and hardware-based PWM timers for precise motor control.

### PWM & Servo Kinematics
- **Timer Allocation**: The firmware uses `ESP32PWM::allocateTimer(n)` to reserve hardware timers 0-3. This prevents conflicts with other peripherals and ensuring high-resolution PWM signals (50Hz frequency). Due to the limited number of timers, you may experience network errors upon adding additional devices or calls to the firmware. For example, in a modded version of the robot, I tried adding two ESCs and their servo controll to the code, and the CPU ran out of internal timers and caused the captive portal to die. If you are experiencing network errors with your custom firmware, check the timer allocation.
- **Pulse Width Mapping**: Leg servos are mapped from degrees (0-180) to microseconds (732us to 2929us). This range is set to the maximum travel on most hobby servos with a 180 degree limit, but can be edited in the `servos[i].attach()` calls. If you are using motors with a larger range of motion, like 270 degree servos, you need to set the PWM mapping to a different length of microseconds. For 270 degree servos specifically I found (833us to 2167us) works.
- **Staggered Activation**: To prevent VCC rail collapse (brownout) caused by simultaneous inductive loads, the `setServoAngle` helper introduces a mandatory `motorCurrentDelay` (default 20ms) between sequential pulses. This delay should be tweaked to your power setup. If you have a strong dedicated power supply you can try setting it to zero. It can also be changed while running through the AP controller settings menu.

### Communication & Networking Stack
- **SoftAP & Captive Portal**: The ESP32 initializes an Access Point using `WiFi.softAP()`. A `DNSServer` listens on UDP Port 53, using a wildcard "*" redirect to map all DNS queries to the internal gateway (`192.168.4.1`).
- **RESTful API Surface**: The `WebServer` handles asynchronous HTTP GET requests:
  - `/cmd?go=[dir]`: Updates motion state.
  - `/cmd?pose=[name]`: Triggers procedural animation sequences.
  - `/getSettings` / `/setSettings`: JSON-based state sync for motion parameters.
- **Non-Blocking Control Flow**: Instead of `delay()`, the firmware uses a custom `pressingCheck(String cmd, int ms)` function. This function polls `server.handleClient()` and `dnsServer.processNextRequest()` during animation frames, allowing for real-time interruptibility (e.g., immediate stop on button release). This pressingCheck protocol can be used for motion commands like walking to play each motion only when the button is held.

### Display & Graphics Subsystem
- **I2C Bus Hardware**: Utilizes the ESP32's hardware I2C controller at 400kHz (Fast Mode) for minimal latency when pushing full-frame buffers to the SSD1306 display.
- **Memory Management (`PROGMEM`)**: Large 128x64 bitmap arrays (1024 bytes per frame) are stored in Flash memory using the `PROGMEM` attribute.
- **Macro-Based Asset Management**: The firmware uses a `FACE_LIST` macro in [face-bitmaps.h](face-bitmaps.h) to automatically register and handle new faces, reducing the boilerplate required when adding animations.
- **Rendering Pipeline**: The `updateAnimatedFace()` function manages frame rates and sequence looping outside of the main movement logic to ensure smooth visual feedback even during complex movements.

## Prerequisites & Development Environment

- **Board Support**: ESP32 by Espressif Systems (v2.0.0+ recommended). Lolin S2 Mini and ESP32-WROOM32 DevKitC are best supported.
- **Libraries**:
  - `ESP32Servo`: Low-level PWM timer management.
  - `Adafruit_SSD1306` & `Adafruit_GFX`: Buffer-based OLED rendering.
- **Tooling**: Arduino IDE.

## Hardware Abstraction Layer (HAL)

The firmware abstracts pin definitions via the `servoPins` array. The default configuration is optimized for the **Sesame Distro Board** and **Lolin S2 Mini**, but is easily portable to any ESP32 with WiFi capability (e.g., S3, C3, or DevKit V1).

### Pin Configuration Tables

#### Lolin S2 Mini (ESP32-S2)

| Motor/Component | Array Index | GPIO Pin | Notes |
|-----------------|-------------|----------|-------|
| Motor 0 | 0 | 1 | Front Left Hip |
| Motor 1 | 1 | 2 | Front Left Knee |
| Motor 2 | 2 | 4 | Front Right Hip |
| Motor 3 | 3 | 6 | Front Right Knee |
| Motor 4 | 4 | 8 | Back Left Hip |
| Motor 5 | 5 | 10 | Back Left Knee |
| Motor 6 | 6 | 13 | Back Right Hip |
| Motor 7 | 7 | 14 | Back Right Knee |
| **I2C SDA** | - | **33** | SSD1306 Data (Hardware I2C) |
| **I2C SCL** | - | **35** | SSD1306 Clock (Hardware I2C) |

#### Sesame Distro Board (ESP32-WROOM32)

| Motor/Component | Array Index | GPIO Pin | Notes |
|-----------------|-------------|----------|-------|
| Motor 0 | 0 | 15 | Front Left Hip |
| Motor 1 | 1 | 2 | Front Left Knee |
| Motor 2 | 2 | 23 | Front Right Hip |
| Motor 3 | 3 | 19 | Front Right Knee |
| Motor 4 | 4 | 4 | Back Left Hip |
| Motor 5 | 5 | 16 | Back Left Knee |
| Motor 6 | 6 | 17 | Back Right Hip |
| Motor 7 | 7 | 18 | Back Right Knee |
| **I2C SDA** | - | **21** | SSD1306 Data (Hardware I2C) |
| **I2C SCL** | - | **22** | SSD1306 Clock (Hardware I2C) |

### Porting to Other ESP32 Variants

To port this to a different ESP32 variant, modify the `servoPins` and `I2C_` defines in the header of [sesame-firmware-main.ino](sesame-firmware-main.ino). Ensure the chosen pins are PWM-capable and not "input-only".

## Asset Pipeline & Face Customization

To maintain a clean main source file and optimize performance, face bitmaps are decoupled from the primary logic. Faces are managed in [face-bitmaps.h](face-bitmaps.h) using a "Single Source of Truth" macro system.

### Workflow:
1.  **Image Creation**: Find faces using Kaomoji or [Emojicombos](https://emojicombos.com/kaomoji). Create a `128x64` image in a tool like [jsPaint](https://jspaint.app/).
2.  **Bitmap Conversion**: Use [image2cpp](https://javl.github.io/image2cpp/) with `Horizontal` scaling, `128x64` resolution, and `Arduino Code` output.
3.  **Registration**:
    -   Add your face name to the `FACE_LIST` macro in [face-bitmaps.h](face-bitmaps.h).
    -   Paste the generated C array into [face-bitmaps.h](face-bitmaps.h) right after the last bitmap in the list.
    -   (Optional) For animations, add numbered suffixes (e.g., `_1`, `_2`) and register the FPS in the `faceFpsEntries` in [sesame-firmware-main.ino](sesame-firmware-main.ino).

### Animating Faces
For an animation to be recognized by the `MAKE_FACE_FRAMES` macro, your array names in [face-bitmaps.h](face-bitmaps.h) must follow a strict naming convention:
- **Root Frame**: `epd_bitmap_myface` (This is required and acts as frame 0).
- **Subsequent Frames**: `epd_bitmap_myface_1`, `epd_bitmap_myface_2`, etc.
- **Limit**: The default system supports up to 6 frames per face (Root + 5 numbered frames).
- **Animation Modes**: Animations can be configured to play as a `LOOP` (restarts at frame 0), `ONCE` (stops on the final frame), or `BOOMERANG` (plays forward then reverses). These modes are typically defined in [movement-sequences.h](movement-sequences.h) when triggerring a pose.

### Macro System
The `FACE_LIST` macro uses X-Macros to automatically generate variable declarations and registration objects:
```cpp
#define FACE_LIST \
    X(walk) \
    X(rest) \
    X(my_new_face) // Just add this line!
```
This eliminates the need to manually update multiple switch statements or arrays when adding new assets.

## Execution & Deployment

1.  **Toolchain**: Configure your IDE for `ESP32 Dev Module` or `Lolin S2 Mini`.
2.  **Calibration**: Use the Serial Monitor (115200) to send manual step commands (e.g., `rn wf`).
3.  **Power Management**: If the robot brownouts during movement, increase `motorCurrentDelay` in the web settings to further stagger servo bursts.

