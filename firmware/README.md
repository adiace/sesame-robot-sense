# Sesame Robot Firmware

This document provides technical information on the firmware architecture, control logic, and hardware abstraction layers used in the Sesame Robot.

>[!NOTE]
> There are two firmware versions, one for the S2 Mini board, and one for the ESP32-DevKitC-32E for the Sesame Distro Board. You can also just change the pin numbers at the start of the firmware to match your board of choice.

## Technical Implementation Overview

The firmware is built on the Arduino-ESP32 framework. Currently the firmware is running on a single-core event loop, and hardware-based PWM timers for precise motor control, but may see improvement from using both cores on the microcontroller.

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
- **Memory Management (`PROGMEM`)**: Large 128x64 bitmap arrays (1024 bytes per frame) are stored in Flash memory using the `PROGMEM` attribute. This is critical for ESP32 variants with limited SRAM, as it prevents the heap from being exhausted by static assets.
- **Rendering Pipeline**: The `updateFaceBitmap()` function performs a `clearDisplay()` -> `drawBitmap()` -> `display()` sequence. This is optimized to only trigger on state changes to reduce I2C congestion.

## Prerequisites & Development Environment

- **Board Support**: ESP32 by Espressif Systems (v2.0.0+ recommended). Lolin S2 Mini and ESP32-WROOM32 DevKitC are best supported.
- **Libraries**:
  - `ESP32Servo`: Low-level PWM timer management.
  - `Adafruit_SSD1306` & `Adafruit_GFX`: Buffer-based OLED rendering.
- **Tooling**: Arduino IDE.

## Hardware Abstraction Layer (HAL)

The firmware abstracts pin definitions via the `servoPins` array. The default configuration is optimized for the **Sesame Distro Board** and **Lolin S2 Mini**, but is easily portable to any ESP32 with WiFi capability (e.g., S3, C3, or DevKit V1).

| Component | Default GPIO | Notes |
|-----------|--------------|-------|
| SDA       | 33           | SSD1306 Data (Hardware I2C) |
| SCL       | 35           | SSD1306 Clock (Hardware I2C) |
| Servos    | 1, 2, 4, 6...| Hardware PWM / ESP32PWM Timers |

To port this to a different ESP32 variant, modify the `servoPins` and `I2C_` defines in the header of [sesame-firmware.ino](sesame-firmware.ino). Ensure the chosen pins are PWM-capable and not "input-only".

## Asset Pipeline & Face Customization

To maintain a clean main source file and optimize the IDE's performance, face bitmaps are decoupled from the primary `.ino` logic. To see the defualt set of faces for the included motions, paste the entirety of **[faces.json](faces.json)** into the space for face bitmaps in the firmware.

Reference **[faces.json](faces.json)** for pre-generated expressions or uses **[placeholders.json](placeholders.json)** for debugging.

### Workflow:
3.  **Image Creation**: Find faces by searching Kaomoji or using [Emojicombos](https://emojicombos.com/kaomoji). Then paste your text into a 128x64 [jsPaint](https://jspaint.app/) document, make any edits or tweaks you want, then export as a PNG file.
1.  **Bitmap Conversion**: Use [image2cpp](https://javl.github.io/image2cpp/) with `Horizontal` scaling, `128x64` resolution, and `Arduino Code` output.
2.  **Implementation**: Paste the generated arrays into the `const unsigned char` declarations at the top of the firmware.

## Execution & Deployment

1.  **Toolchain**: Configure your IDE for `ESP32 Dev Module` or `Lolin S2 Mini`.
2.  **Calibration**: Use the Serial Monitor (115200) to send manual step commands (e.g., `rn wf`).
3.  **Power Management**: If the robot brownouts during movement, increase `motorCurrentDelay` in the web settings to further stagger servo bursts.

