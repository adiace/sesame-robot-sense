#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// ======================================================================
// --- CONFIGURATION ---
// ======================================================================

// I2C pins (XIAO ESP32-S3 — SDA moved to D9 to avoid I2S conflict)
#define I2C_SDA  8   // D9 / GPIO8
#define I2C_SCL  6   // D5 / GPIO6

// PCA9685
#define PCA9685_ADDR     0x40
#define PCA9685_OSC_HZ   27000000UL
#define SERVO_FREQ_HZ    50

// MG90S/SG90 standard range: 500µs–2400µs (matches myservo.attach(pin, 500, 2400))
// At 50Hz with 25MHz oscillator: tick = µs / 4.88
#define SERVOMIN  102   // 500µs  → logical 0°
#define SERVOMAX  491   // 2400µs → logical 180°

// Safe angle limits — prevent stalling against mechanical stops
#define ANGLE_MIN  10
#define ANGLE_MAX  170

// Channel map (matches pins.h in main firmware)
// ch0: R1 right front hip    ch4: R4 right front knee
// ch1: R2 right rear  hip    ch5: R3 right rear  knee
// ch2: L1 left  front hip    ch6: L3 left  front knee
// ch3: L2 left  rear  hip    ch7: L4 left  rear  knee
#define NUM_SERVOS  8

Adafruit_PWMServoDriver pwm(PCA9685_ADDR);
bool powered[NUM_SERVOS] = {false};
int currentAngle[16];
int chanMin[16];
int chanMax[16];
unsigned long lastRefreshMs = 0;
#define REFRESH_INTERVAL_MS 500

// Sweep state (non-blocking)
bool sweeping = false;
int sweepAngle = 90;
int sweepDir = 1;
unsigned long lastSweepMs = 0;
#define SWEEP_INTERVAL_MS 20
#define SWEEP_MIN 60
#define SWEEP_MAX 120

// ======================================================================
// --- FORWARD DECLARATIONS ---
// ======================================================================

void moveMotor(int id, int angle);
void moveAll(int angle);
void stopMotors();
void restPose();
void standPose();
void sweepAll();
void printHelp();

// ======================================================================
// --- SETUP ---
// ======================================================================

void printHelp() {
  Serial.println("-----------------------------------");
  Serial.println("  Sesame Motor Tester (PCA9685)   ");
  Serial.println("-----------------------------------");
  Serial.println("Commands:");
  Serial.println("  id,angle  -> e.g. '0,90'");
  Serial.println("  all,angle -> e.g. 'all,90'");
  Serial.println("  stop      -> Power down all motors");
  Serial.println("  sweep     -> All channels sweep 60<->120 (any key to stop)");
  Serial.println("  test      -> Test single servo on ch9 (any key to stop)");
  Serial.println("  stand     -> Stand pose");
  Serial.println("  rest      -> Rest pose (all 90)");
  Serial.println("  limit <ch> <min> <max> -> Set per-channel safe range");
  Serial.println("  limits    -> Show all channel limits");
  Serial.println("  ?         -> Print this help");
  Serial.println("-----------------------------------");
  Serial.println("Channel map:");
  Serial.println("  0=R1(RF hip)  1=R2(RR hip)  2=L1(LF hip)  3=L2(LR hip)");
  Serial.println("  4=R4(RF knee) 5=R3(RR knee) 6=L3(LF knee) 7=L4(LR knee)");
  Serial.println("-----------------------------------");
  Serial.println("Status: All channels at 90° and ready.");
}

void setup() {
  Serial.begin(115200);
  // Wait up to 3s for USB-CDC serial monitor to connect (XIAO ESP32-S3)
  unsigned long t = millis();
  while (!Serial && millis() - t < 3000);

  Wire.begin(I2C_SDA, I2C_SCL);

  // Scan I2C bus and report all devices
  Serial.println("Scanning I2C bus...");
  bool foundPCA = false;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found: 0x%02X", addr);
      if (addr == 0x40 || addr == 0x70) { Serial.print(" (PCA9685)"); foundPCA = true; }
      if (addr == 0x3C)                  Serial.print(" (OLED)");
      if (addr == 0x68)                  Serial.print(" (IMU)");
      Serial.println();
    }
  }
  if (!foundPCA) {
    Serial.println("ERROR: PCA9685 not found at 0x40 — check wiring and power.");
    while (true) {
      if (!Serial) { unsigned long t2 = millis(); while (!Serial && millis()-t2 < 3000); }
      Serial.println("ERROR: PCA9685 not found at 0x40 — check wiring and power.");
      delay(2000);
    }
  }

  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ_HZ);
  delay(10);

  // Per-channel limits (determined during physical bring-up)
  for (int i = 0; i < 16; i++) { currentAngle[i] = 90; chanMin[i] = 40; chanMax[i] = 140; }
  chanMin[0] = 80;  chanMax[0] = 135; // R1 hip
  chanMin[1] = 40;  chanMax[1] = 100; // R2 hip
  chanMin[2] = 40;  chanMax[2] = 100; // L1 hip
  chanMin[3] = 80;  chanMax[3] = 135; // L2 hip
  chanMin[4] = 40;  chanMax[4] = 140; // R4 knee
  chanMin[5] = 40;  chanMax[5] = 135; // R3 knee
  chanMin[6] = 42;  chanMax[6] = 140; // L3 knee
  chanMin[7] = 40;  chanMax[7] = 140; // L4 knee
  uint16_t center = map(90, 0, 180, SERVOMIN, SERVOMAX);
  for (int i = 0; i < 16; i++) pwm.setPWM(i, 0, center);

  Serial.println("PCA9685: OK");
  printHelp();
}

// ======================================================================
// --- MAIN LOOP ---
// ======================================================================

void loop() {
  unsigned long now = millis();

  // Non-blocking sweep
  if (sweeping && now - lastSweepMs >= SWEEP_INTERVAL_MS) {
    lastSweepMs = now;
    sweepAngle += sweepDir;
    if (sweepAngle >= SWEEP_MAX) { sweepAngle = SWEEP_MAX; sweepDir = -1; }
    if (sweepAngle <= SWEEP_MIN) { sweepAngle = SWEEP_MIN; sweepDir =  1; }
    int channels[] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < NUM_SERVOS; i++) {
      int a = constrain(sweepAngle, chanMin[channels[i]], chanMax[channels[i]]);
      uint16_t ticks = map(a, 0, 180, SERVOMIN, SERVOMAX);
      pwm.setPWM(channels[i], 0, ticks);
      currentAngle[channels[i]] = a;
    }
  }

  // Periodically refresh all channel PWM to prevent channels going inactive
  if (!sweeping && now - lastRefreshMs >= REFRESH_INTERVAL_MS) {
    for (int i = 0; i < 16; i++) {
      uint16_t ticks = map(currentAngle[i], 0, 180, SERVOMIN, SERVOMAX);
      pwm.setPWM(i, 0, ticks);
    }
    lastRefreshMs = now;
  }

  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();
  if (input.length() == 0) return;

  if (input.equalsIgnoreCase("stop")) {
    sweeping = false;
    stopMotors();
    return;
  }

  if (input.equalsIgnoreCase("sweep")) {
    sweepAll();
    return;
  }

  if (input.equalsIgnoreCase("test")) {
    testServo(9);
    return;
  }

  if (input.equalsIgnoreCase("stand")) {
    sweeping = false;
    standPose();
    return;
  }

  if (input.equalsIgnoreCase("rest")) {
    restPose();
    return;
  }

  if (input.startsWith("limit ")) {
    // limit <ch> <min> <max>  e.g. "limit 0 50 130"
    int ch, mn, mx;
    if (sscanf(input.c_str(), "limit %d %d %d", &ch, &mn, &mx) == 3 && ch >= 0 && ch <= 15) {
      chanMin[ch] = mn;
      chanMax[ch] = mx;
      Serial.printf("ch%d limits set: %d-%d\n", ch, mn, mx);
    } else {
      Serial.println("Usage: limit <ch> <min> <max>  e.g. 'limit 0 50 130'");
    }
    return;
  }

  if (input.startsWith("limits")) {
    Serial.println("Per-channel limits:");
    for (int i = 0; i < 8; i++)
      Serial.printf("  ch%d: %d-%d  current:%d\n", i, chanMin[i], chanMax[i], currentAngle[i]);
    return;
  }

  if (input == "?") {
    printHelp();
    return;
  }

  int commaIndex = input.indexOf(',');
  if (commaIndex == -1) {
    Serial.println("Error: use 'id,angle', 'all,angle', or 'stop'.");
    return;
  }

  String cmd   = input.substring(0, commaIndex);
  int    angle = constrain(input.substring(commaIndex + 1).toInt(), ANGLE_MIN, ANGLE_MAX);

  if (cmd.equalsIgnoreCase("all")) {
    moveAll(angle);
  } else {
    int id = cmd.toInt();
    if (id == 0 && cmd.charAt(0) != '0') {
      Serial.println("Error: invalid motor ID.");
    } else {
      moveMotor(id, angle);
    }
  }
}

// ======================================================================
// --- HELPERS ---
// ======================================================================

void testServo(int ch) {
  Serial.printf("Testing servo on ch%d: 90 -> 60 -> 120 -> 90. Send anything to stop.\n", ch);
  uint16_t ticks;
  while (true) {
    int angles[] = {90, 60, 120, 90};
    for (int i = 0; i < 4; i++) {
      ticks = map(angles[i], 0, 180, SERVOMIN, SERVOMAX);
      pwm.setPWM(ch, 0, ticks);
      Serial.printf("ch%d -> %d\n", ch, angles[i]);
      unsigned long t = millis();
      while (millis() - t < 600) {
        if (Serial.available()) {
          Serial.readStringUntil('\n');
          pwm.setPWM(ch, 0, 0);
          Serial.println("Test stopped.");
          return;
        }
      }
    }
  }
}

void sweepAll() {
  sweeping = !sweeping;
  if (sweeping) {
    sweepAngle = 90;
    sweepDir = 1;
    Serial.println("Sweep started — send 'sweep' again to stop.");
  } else {
    Serial.println("Sweep stopped.");
    restPose();
  }
}

void restPose() {
  Serial.println("Rest pose");
  moveMotor(0, 85);  // R1 hip center
  moveMotor(1, 92);  // R2 hip center
  moveMotor(2, 98);  // L1 hip center
  moveMotor(3, 86);  // L2 hip center
  moveMotor(4, 85);  // R4 knee center
  moveMotor(5, 92);  // R3 knee center
  moveMotor(6, 90);  // L3 knee center
  moveMotor(7, 94);  // L4 knee center
}

void standPose() {
  Serial.println("Stand pose");
  moveMotor(0, 120); // R1 right front hip
  moveMotor(1, 55);  // R2 right rear  hip
  moveMotor(2, 55);  // L1 left  front hip
  moveMotor(3, 120); // L2 left  rear  hip
  moveMotor(4, 45);  // R4 right front knee (down)
  moveMotor(5, 135); // R3 right rear  knee (down)
  moveMotor(6, 45);  // L3 left  front knee (down)
  moveMotor(7, 135); // L4 left  rear  knee (down)
}

void moveMotor(int id, int angle) {
  if (id < 0 || id > 15) {
    Serial.println("Error: channel must be 0-15.");
    return;
  }
  int safe = constrain(angle, chanMin[id], chanMax[id]);
  if (safe != angle) {
    Serial.printf("Warning: ch%d angle %d clamped to %d (limit %d-%d)\n", id, angle, safe, chanMin[id], chanMax[id]);
    angle = safe;
  }
  uint16_t ticks = map(angle, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(id, 0, ticks);
  currentAngle[id] = angle;
  powered[id] = true;
  Serial.print("OK: Motor ");
  Serial.print(id);
  Serial.print(" -> ");
  Serial.println(angle);
}

void moveAll(int angle) {
  Serial.print("Moving ALL to ");
  Serial.println(angle);
  int channels[] = {0, 1, 2, 3, 4, 5, 6, 7};
  for (int i = 0; i < NUM_SERVOS; i++) {
    moveMotor(channels[i], angle);
  }
}

void stopMotors() {
  for (int i = 0; i < 16; i++) {
    pwm.setPWM(i, 0, 4096); // full-off bit — truly cuts signal
  }
  for (int i = 0; i < NUM_SERVOS; i++) powered[i] = false;
  Serial.println("Motors OFF.");
}
