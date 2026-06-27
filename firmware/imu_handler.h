#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "pins.h"

// MPU-6050 / MPU-6500 clone direct I2C driver.
// This is the Phase 1 code verbatim with three Phase 2 changes only:
//   1. Wire.endTransmission(false) → (true)  — repeated start is buggy on ESP32-S3
//   2. imuEmit() outputs JSON + pushes to imuEventQueue instead of Serial plain text
//   3. FLIPPED no longer returns early so LEVEL can fire on flip-back.
//      tiltRate > 200°/s added to tap check to prevent false TAPPED on flip-back.

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

// ── Thresholds (Phase 1 values — do not change) ──────────────────────────────
#define IMU_FLIP_ANGLE_DEG    120.0f
#define IMU_FLIP_SUSTAIN_MS   200

#define IMU_SHAKE_WIN_SIZE    20
#define IMU_SHAKE_VARIANCE    0.40f    // g²

#define IMU_TAP_JERK          60.0f   // m/s²/s
#define IMU_TAP_LOCKOUT_MS    250
#define IMU_TAP_RATE_MAX      200.0f  // °/s — Phase 2 addition: blocks tap during flip-back

#define IMU_EMA_ALPHA         0.005f
#define IMU_PICKUP_RESIDUAL   1.2f    // m/s²
#define IMU_PICKUP_MS         120

#define IMU_FF_THR_VAL        10
#define IMU_FF_DUR_VAL        80

#define IMU_LEVEL_ANGLE_DEG   15.0f
#define IMU_LEVEL_SUSTAIN_MS  400

// ── Event enum ──────────────────────────────────────────────────────────────
enum ImuEvent : uint8_t {
  IMU_NONE = 0,
  IMU_PICKUP,
  IMU_FLIPPED,
  IMU_SHAKEN,
  IMU_TAPPED,
  IMU_FREEFALL,
  IMU_LEVEL,
};

// ── State ───────────────────────────────────────────────────────────────────
static bool     imuReady  = false;
static ImuEvent lastEvent = IMU_NONE;

static unsigned long imuLastPollMs = 0;

// Complementary filter angles (degrees)
static float cfPitch = 0.0f;
static float cfRoll  = 0.0f;
static float prevTilt = 0.0f;  // for tiltRate (Phase 2 tap guard)

// Flip / Level sustain
static unsigned long imuFlipStart  = 0;
static unsigned long imuLevelStart = 0;

// Shake — sliding window
static float   shakeWin[IMU_SHAKE_WIN_SIZE] = {};
static uint8_t shakeIdx  = 0;
static float   shakeSum  = 0.0f;
static float   shakeSumSq = 0.0f;

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
  return Wire.endTransmission(true) == 0;  // Phase 2: true not false (ESP32-S3 bug)
}

static bool imuReadAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(true) != 0) return false;  // Phase 2: true
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
  if (Wire.endTransmission(true) != 0) return false;  // Phase 2: true
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1);
  if (!Wire.available()) return false;
  status = Wire.read();
  return true;
}

// ── Phase 2: JSON emit via FreeRTOS queue ────────────────────────────────────
// Core 0 drains imuEventQueue in serviceTcpClient() and sends via tcpClient.
// imuPoll() on Core 1 never touches WiFi — no blocking, dt stays accurate.
extern QueueHandle_t imuEventQueue;

static void imuEmit(ImuEvent ev, float accel = 0.0f, float pitch = 0.0f, float roll = 0.0f) {
  if (ev == lastEvent) return;
  lastEvent = ev;
  const char* n;
  switch (ev) {
    case IMU_PICKUP:   n = "PICKUP";   break;
    case IMU_FLIPPED:  n = "FLIPPED";  break;
    case IMU_SHAKEN:   n = "SHAKEN";   break;
    case IMU_TAPPED:   n = "TAPPED";   break;
    case IMU_FREEFALL: n = "FREEFALL"; break;
    case IMU_LEVEL:    n = "LEVEL";    break;
    default:           n = "UNKNOWN";  break;
  }
  char json[128];
  snprintf(json, sizeof(json),
    "{\"type\":\"imu_event\",\"event\":\"%s\",\"accel\":%.2f,\"pitch\":%.1f,\"roll\":%.1f}",
    n, accel, pitch, roll);
  Serial.println(json);
  if (imuEventQueue) xQueueSend(imuEventQueue, json, 0);
}

// ── Public API ───────────────────────────────────────────────────────────────
void imuSetup() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_WHO_AM_I);
  Wire.endTransmission(true);
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1);
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
  for (int i = 0; i < IMU_SHAKE_WIN_SIZE; i++) {
    shakeWin[i] = 1.0f;
    shakeSum   += 1.0f;
    shakeSumSq += 1.0f;
  }
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

  // ── Complementary filter ─────────────────────────────────────────────────
  float accelPitch = atan2f(-ax, sqrtf(ay*ay + az*az)) * (180.0f / M_PI);
  float accelRoll  = atan2f( ay, az)                    * (180.0f / M_PI);
  const float cfAlpha = 0.85f;  // Phase 1 value
  cfPitch = cfAlpha * cfPitch + (1.0f - cfAlpha) * accelPitch;
  cfRoll  = cfAlpha * cfRoll  + (1.0f - cfAlpha) * accelRoll;

  float tiltAngle = sqrtf(cfPitch*cfPitch + cfRoll*cfRoll);
  float tiltRate  = fabsf(tiltAngle - prevTilt) / dt;  // °/s — Phase 2 tap guard
  prevTilt = tiltAngle;

  // ── Shake window variance ────────────────────────────────────────────────
  float old = shakeWin[shakeIdx];
  shakeSum -= old; shakeSumSq -= old * old;
  shakeWin[shakeIdx] = magG;
  shakeSum += magG; shakeSumSq += magG * magG;
  shakeIdx = (shakeIdx + 1) % IMU_SHAKE_WIN_SIZE;
  float shakeMean = shakeSum / IMU_SHAKE_WIN_SIZE;
  float shakeVar  = (shakeSumSq / IMU_SHAKE_WIN_SIZE) - (shakeMean * shakeMean);

  // ── Jerk ─────────────────────────────────────────────────────────────────
  float jerk = (mag - prevMag) / dt;
  prevMag = mag;
  bool tapRising = (jerk > IMU_TAP_JERK)
                && (tiltRate < IMU_TAP_RATE_MAX)   // Phase 2: block during rotation
                && (now > tapLockoutEnd);

  // ── EMA residual ─────────────────────────────────────────────────────────
  pickupEma = pickupEma * (1.0f - IMU_EMA_ALPHA) + mag * IMU_EMA_ALPHA;
  float pickupResidual = fabsf(mag - pickupEma);

  // ── Freefall ─────────────────────────────────────────────────────────────
  uint8_t intStatus = 0;
  imuReadIntStatus(intStatus);
  if (intStatus & 0x80) { imuEmit(IMU_FREEFALL, magG, cfPitch, cfRoll); return; }

  // ── FLIPPED — tilt > 120° sustained 200ms ───────────────────────────────
  if (tiltAngle > IMU_FLIP_ANGLE_DEG) {
    if (imuFlipStart == 0) imuFlipStart = now;
    if (now - imuFlipStart >= IMU_FLIP_SUSTAIN_MS)
      imuEmit(IMU_FLIPPED, magG, cfPitch, cfRoll);
    // Phase 2: goto instead of return so LEVEL check below still runs
    goto check_level;
  } else {
    imuFlipStart = 0;
  }

  // ── SHAKEN ───────────────────────────────────────────────────────────────
  if (shakeVar > IMU_SHAKE_VARIANCE) {
    imuEmit(IMU_SHAKEN, magG, cfPitch, cfRoll);
    goto check_level;
  }

  // ── PICKUP ───────────────────────────────────────────────────────────────
  if (pickupResidual > IMU_PICKUP_RESIDUAL) {
    if (pickupStart == 0) pickupStart = now;
    if (now - pickupStart >= IMU_PICKUP_MS) {
      pickupEma = mag;
      imuEmit(IMU_PICKUP, magG, cfPitch, cfRoll);
    }
  } else {
    pickupStart = 0;
  }

  // ── TAPPED ───────────────────────────────────────────────────────────────
  if (tapRising) {
    tapLockoutEnd = now + IMU_TAP_LOCKOUT_MS;
    imuEmit(IMU_TAPPED, magG, cfPitch, cfRoll);
  }

check_level:
  // Always checked — Phase 2 fix so LEVEL fires after flip-back
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
