#pragma once
//
// pins.h — sesame-robot-sense hardware map
// =============================================================================
// Target board: Seeed XIAO ESP32-S3 Sense on the techiesms "AI Pin" PCB.
//
// This fork replaces the original Sesame direct-GPIO servo drive (ESP32Servo)
// with a PCA9685 16-channel PWM driver on I2C. All angle->pulse conversion now
// happens through Adafruit_PWMServoDriver; the firmware no longer drives any
// servo PWM pin directly.
//
// The three I2C devices (PCA9685, SSD1306 OLED, MPU-6050) share one bus on
// D4/D5 with 4.7k pull-ups to 3V3. Wire.begin(I2C_SDA, I2C_SCL) is called
// exactly once in setup().
//
// I2S pins to the MAX98357A amplifier (D8/D9/D10) are wired on the AI Pin PCB
// and are LOCKED — they are listed here for documentation only and must not be
// reused for anything else.
// =============================================================================

#include <Arduino.h>

// ---------------------------------------------------------------------------
// I2C bus (shared: PCA9685 + OLED + IMU)
// D4/GPIO5 is physically shared with I2S LRC on the AI Pin PCB — SDA moved
// to D9/GPIO8 to avoid the conflict. Move the physical SDA wire to D9.
// ---------------------------------------------------------------------------
#define I2C_SDA            8      // D9 / GPIO8
#define I2C_SCL            6      // D5 / GPIO6

// ---------------------------------------------------------------------------
// I2C device addresses
// ---------------------------------------------------------------------------
#define PCA9685_ADDR       0x40   // servo driver
#define OLED_I2C_ADDR      0x3C   // SSD1306 128x64
#define MPU6050_ADDR       0x68   // IMU (mounted later)

// ---------------------------------------------------------------------------
// I2S -> MAX98357A audio amp (LOCKED on the AI Pin PCB — do not reuse)
// PCB traces confirmed from wiring diagram: LRC=D4, BCLK=D6, DIN=D8
// D4=GPIO5 is also I2C SDA — audioSetup() install/uninstall I2S around
// playback so GPIO5 returns to Wire control when audio is idle.
// ---------------------------------------------------------------------------
#define I2S_BCLK_PIN       43     // D6  / GPIO43
#define I2S_LRCLK_PIN      5      // D4  / GPIO5  (shared with I2C SDA)
#define I2S_DIN_PIN        7      // D8  / GPIO7

// ---------------------------------------------------------------------------
// Remaining XIAO ESP32-S3 GPIO
// ---------------------------------------------------------------------------
#define BATTERY_ADC_PIN    1      // D0 / GPIO1 (battery sense via resistor divider)
#define MODE_BUTTON_PIN    2      // D1 / GPIO2
#define SPARE_PIN          3      // D2 / GPIO3 (spare)
#define IMU_INT_PIN        4      // D3 / GPIO4 (MPU-6050 INT)

// UART on the strapping pins. GPIO43/44 are boot/strapping pins: they are safe
// for UART *after* boot — never drive them LOW at boot time.
#define UART_TX_PIN        43     // D6 / GPIO43
#define UART_RX_PIN        44     // D7 / GPIO44

// ---------------------------------------------------------------------------
// PCA9685 servo pulse calibration
// ---------------------------------------------------------------------------
// 50 Hz => 20000 us period, 4096 ticks => ~4.88 us/tick.
//   SERVOMIN 150 ticks ~= 732 us   (logical angle   0)
//   SERVOMAX 600 ticks ~= 2929 us  (logical angle 180)
// This is deliberately the SAME range the original Sesame firmware fed to
// ESP32Servo (servos[i].attach(pin, 732, 2929)), so every angle baked into
// movement-sequences.h reproduces the identical physical position with no
// re-calibration. Tune per-servo during bring-up if a horn binds.
#define SERVO_PWM_FREQ_HZ  50
#define SERVOMIN           150    // tick count at logical 0 deg
#define SERVOMAX           600    // tick count at logical 180 deg

// PCA9685 on-board oscillator nominal frequency, used by
// pwm.setOscillatorFrequency() so setPWMFreq(50) lands accurately.
#define PCA9685_OSC_HZ     27000000UL

// ---------------------------------------------------------------------------
// Servo channel map (Sesame nomenclature preserved)
// ---------------------------------------------------------------------------
// movement-sequences.h defines the logical servo indices:
//   R1=0 R2=1 L1=2 L2=3 R4=4 R3=5 L3=6 L4=7
// On this hardware those map 1:1 onto PCA9685 output channels 0..7, so the
// index IS the channel. servoChannel[] is kept as an explicit indirection so a
// mis-wire can be corrected here without editing any motion code.
//
//   ch0: R1 (right front hip)     ch4: R4 (right front knee)
//   ch1: R2 (right rear  hip)     ch5: R3 (right rear  knee)
//   ch2: L1 (left  front hip)     ch6: L3 (left  front knee)
//   ch3: L2 (left  rear  hip)     ch7: L4 (left  rear  knee)
static const uint8_t servoChannel[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

// ---------------------------------------------------------------------------
// TCP command server (Albert line-protocol — see RobotNet.h / CommandRouter.h)
// ---------------------------------------------------------------------------
// Matches robot_link.py: clients reach the robot at quadruped.local:8888.
#define TCP_CMD_PORT       8888
#define TCP_LOG_PORT       8890   // WiFi serial monitor: nc quadruped.local 8890
