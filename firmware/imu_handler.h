#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "pins.h"

// MPU-6050 / MPU-6500 clone direct I2C driver.
// Phase 1 logic with minimal Phase 2 changes:
//   - Wire.endTransmission(true) throughout (ESP32-S3 repeated-start bug)
//   - imuEmit() writes JSON to Serial + pushes to imuEventQueue (Core 0 TCP)
//   - FLIP uses raw-accel flipAngle = atan2(horiz, az) (0–180°) instead of
//     slow cfPitch/cfRoll so it detects inversion in < 100ms
//   - goto check_level after every event so LEVEL always runs
//   - flipRate blocks TAPPED during flip-back motion

// ── MPU register addresses ──────────────────────────────────────────────────
#define MPU_REG_SMPLRT_DIV    0x19
#define MPU_REG_CONFIG        0x1A
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_FF_THR        0x1D
#define MPU_REG_FF_DUR        0x1E
#define MPU_REG_INT_ENABLE    0x38
#define MPU_REG_INT_STATUS    0x3A
#define MPU_REG_ACCEL_XOUT_H  0x3B
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_WHO_AM_I      0x75

// ── Thresholds ──────────────────────────────────────────────────────────────
// FLIPPED: raw-accel flipAngle (0° upright → 180° inverted) > 100°, 5 stable samples
#define IMU_FLIP_ANGLE_DEG    100.0f
#define IMU_FLIP_SAMPLES        5      // 5 × 20ms = 100ms

// TAPPED: jerk spike; blocked when device is rotating (flip/flip-back)
#define IMU_TAP_JERK          120.0f  // m/s²/s — 60 caught noise; 120 still sensitive for petting
#define IMU_TAP_LOCKOUT_MS    250
#define IMU_TAP_RATE_MAX     150.0f   // °/s of flipAngle change — blocks tap during flip

// PICKUP: EMA residual (Phase 1 values)
#define IMU_EMA_ALPHA         0.005f
#define IMU_PICKUP_RESIDUAL   1.2f    // m/s²
#define IMU_PICKUP_MS         120

// FREEFALL: hardware registers (Phase 1 values)
#define IMU_FF_THR_VAL        10
#define IMU_FF_DUR_VAL        80

// LEVEL: complementary filter < 15° sustained 400ms (Phase 1 values)
#define IMU_LEVEL_ANGLE_DEG   15.0f
#define IMU_LEVEL_SUSTAIN_MS  400

// ── Event enum ──────────────────────────────────────────────────────────────
enum ImuEvent : uint8_t {
  IMU_NONE = 0,
  IMU_PICKUP,
  IMU_FLIPPED,
  IMU_TAPPED,
  IMU_FREEFALL,
  IMU_LEVEL,
};

// ── State ───────────────────────────────────────────────────────────────────
static bool     imuReady  = false;
static ImuEvent lastEvent = IMU_NONE;

static unsigned long imuLastPollMs = 0;

// flipAngle tracking (raw accel, 0–180°)
static float prevFlipAngle = 0.0f;
static int   flipSamples   = 0;

// Complementary filter (slow alpha, used for LEVEL only)
static float cfPitch = 0.0f;
static float cfRoll  = 0.0f;
static bool  cfSeeded = false;

// Level sustain
static unsigned long imuLevelStart = 0;

// Tap
static float         prevMag       = 9.81f;
static unsigned long tapLockoutEnd = 0;

// Pickup
static float         pickupEma   = 9.81f;
static unsigned long pickupStart = 0;

// ── I2C helpers ──────────────────────────────────────────────────────────────
static bool imuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

static bool imuReadAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(true) != 0) return false;
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)14);
  if (Wire.available() < 14) return false;
  int16_t axr = ((int16_t)Wire.read() << 8) | Wire.read();
  int16_t ayr = ((int16_t)Wire.read() << 8) | Wire.read();
  int16_t azr = ((int16_t)Wire.read() << 8) | Wire.read();
  for (int i = 0; i < 8; i++) Wire.read();
  const float scale = 9.81f / 4096.0f;
  ax = axr * scale; ay = ayr * scale; az = azr * scale;
  return true;
}

static bool imuReadIntStatus(uint8_t &status) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_INT_STATUS);
  if (Wire.endTransmission(true) != 0) return false;
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1);
  if (!Wire.available()) return false;
  status = Wire.read();
  return true;
}

// ── TCP push via queue ────────────────────────────────────────────────────────
extern QueueHandle_t imuEventQueue;

static void imuEmit(ImuEvent ev, float accel = 0.0f, float pitch = 0.0f, float roll = 0.0f) {
  if (ev == lastEvent) return;
  lastEvent = ev;
  const char* n;
  switch (ev) {
    case IMU_PICKUP:   n = "PICKUP";   break;
    case IMU_FLIPPED:  n = "FLIPPED";  break;
    case IMU_TAPPED:   n = "TAPPED";   break;
    case IMU_FREEFALL: n = "FREEFALL"; break;
    case IMU_LEVEL:    n = "LEVEL";    break;
    default:           n = "UNKNOWN";  break;
  }
  // Reset LEVEL sustain timer so LEVEL always requires 400ms after any non-LEVEL event
  if (ev != IMU_LEVEL) imuLevelStart = 0;

  char json[128];
  snprintf(json, sizeof(json),
    "{\"type\":\"imu_event\",\"event\":\"%s\",\"accel\":%.2f,\"pitch\":%.1f,\"roll\":%.1f}",
    n, accel, pitch, roll);
  Serial.println(json);
  if (imuEventQueue) xQueueSend(imuEventQueue, json, 0);
}

// ── Public API ───────────────────────────────────────────────────────────────

// I2C bus recovery: clock SCL 9 times to unstick a slave holding SDA low.
// Needed when the ESP32 is reset mid-transaction (e.g. during upload).
static void imuBusRecover() {
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(I2C_SCL, HIGH); delayMicroseconds(5);
    digitalWrite(I2C_SCL, LOW);  delayMicroseconds(5);
  }
  // Send a STOP condition
  pinMode(I2C_SDA, OUTPUT);
  digitalWrite(I2C_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(I2C_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(I2C_SDA, HIGH);
  delayMicroseconds(5);
  // Return pins to Wire control
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);
  delay(10);
}

void imuSetup() {
  // Probe first; if no response, attempt bus recovery and retry once
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_WHO_AM_I);
  Wire.endTransmission(true);
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1);
  if (!Wire.available()) {
    Serial.println("IMU: no response — attempting I2C bus recovery");
    imuBusRecover();
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(50);
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_WHO_AM_I);
    Wire.endTransmission(true);
    Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1);
  }
  if (!Wire.available()) { Serial.println("IMU: no response"); return; }
  uint8_t whoami = Wire.read();
  Serial.print("IMU: WHO_AM_I=0x"); Serial.println(whoami, HEX);

  if (!imuWrite(MPU_REG_PWR_MGMT_1, 0x01)) { Serial.println("IMU: init failed"); return; }
  delay(100);
  imuWrite(MPU_REG_SMPLRT_DIV,  0x09);
  imuWrite(MPU_REG_CONFIG,       0x03);
  imuWrite(MPU_REG_ACCEL_CONFIG, 0x10);
  imuWrite(MPU_REG_GYRO_CONFIG,  0x08);
  imuWrite(MPU_REG_FF_THR,   IMU_FF_THR_VAL);
  imuWrite(MPU_REG_FF_DUR,   IMU_FF_DUR_VAL);
  imuWrite(MPU_REG_INT_ENABLE, 0x80);

  pickupEma = 9.81f;
  imuReady = true;
  Serial.println("IMU: ready");
}

void imuPoll() {
  if (!imuReady) return;

  unsigned long now = millis();
  if (now - imuLastPollMs < 20) return;
  float dt = (now - imuLastPollMs) / 1000.0f;
  imuLastPollMs = now;

  float ax, ay, az;
  if (!imuReadAccel(ax, ay, az)) return;

  float mag  = sqrtf(ax*ax + ay*ay + az*az);
  float magG = mag / 9.81f;

  // ── flipAngle: atan2(horiz, az) = 0° upright, 90° sideways, 180° inverted
  // Fast raw-accel computation — no filter lag, detects inversion immediately.
  float horiz     = sqrtf(ax*ax + ay*ay);
  float flipAngle = atan2f(horiz, az) * (180.0f / M_PI);
  float flipRate  = fabsf(flipAngle - prevFlipAngle) / dt;  // °/s
  prevFlipAngle   = flipAngle;

  // ── Complementary filter for LEVEL only (slow — filters out tap jolts) ───
  float accelPitch = atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0f / M_PI);
  float accelRoll  = atan2f( ay, az)                    * (180.0f / M_PI);
  if (!cfSeeded) {
    cfPitch = accelPitch; cfRoll = accelRoll; cfSeeded = true;
  }
  const float cfAlpha = 0.85f;
  cfPitch = cfAlpha * cfPitch + (1.0f - cfAlpha) * accelPitch;
  cfRoll  = cfAlpha * cfRoll  + (1.0f - cfAlpha) * accelRoll;
  float tiltAngle = sqrtf(cfPitch*cfPitch + cfRoll*cfRoll);

  // ── Shake window variance ────────────────────────────────────────────────
  // ── Jerk ─────────────────────────────────────────────────────────────────
  float jerk = (mag - prevMag) / dt;
  prevMag = mag;

  // ── EMA residual ─────────────────────────────────────────────────────────
  pickupEma = pickupEma * (1.0f - IMU_EMA_ALPHA) + mag * IMU_EMA_ALPHA;
  float pickupResidual = mag - pickupEma;  // positive only: pickup = upward accel (mag > EMA)

  // ── FREEFALL ─────────────────────────────────────────────────────────────
  uint8_t intStatus = 0;
  imuReadIntStatus(intStatus);
  if (intStatus & 0x80) { imuEmit(IMU_FREEFALL, magG, cfPitch, cfRoll); goto check_level; }

  // ── FLIPPED: flipAngle > 100°, 5 consecutive stable samples ─────────────
  if (flipAngle > IMU_FLIP_ANGLE_DEG) {
    if (++flipSamples >= IMU_FLIP_SAMPLES)
      imuEmit(IMU_FLIPPED, magG, cfPitch, cfRoll);
    goto check_level;  // suppress TAPPED/PICKUP while inverted
  } else {
    flipSamples = 0;
  }

  // ── PICKUP ───────────────────────────────────────────────────────────────
  if (pickupResidual > IMU_PICKUP_RESIDUAL) {
    if (pickupStart == 0) pickupStart = now;
    if (now - pickupStart >= IMU_PICKUP_MS) {
      pickupEma = mag;
      imuEmit(IMU_PICKUP, magG, cfPitch, cfRoll);
    }
    goto check_level;
  } else {
    pickupStart = 0;
  }

  // ── TAPPED ───────────────────────────────────────────────────────────────
  if (jerk > IMU_TAP_JERK && flipRate < IMU_TAP_RATE_MAX && now > tapLockoutEnd) {
    tapLockoutEnd = now + IMU_TAP_LOCKOUT_MS;
    imuEmit(IMU_TAPPED, magG, cfPitch, cfRoll);
    goto check_level;
  }

check_level:
  if (lastEvent != IMU_NONE && lastEvent != IMU_LEVEL) {
    if (tiltAngle < IMU_LEVEL_ANGLE_DEG) {
      if (imuLevelStart == 0) imuLevelStart = now;
      if (now - imuLevelStart >= IMU_LEVEL_SUSTAIN_MS)
        imuEmit(IMU_LEVEL, magG, cfPitch, cfRoll);
    } else {
      imuLevelStart = 0;
    }
  }
}
