#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "pins.h"
#include "face-bitmaps.h"
#include "movement-sequences.h"
#include "captive-portal.h"
#include "imu_handler.h"
#include "audio_handler.h"
#include "mic_handler.h"
#include "voice_handler.h"
#include "voice_config.h"
#include "wifi_log.h"

// --- Access Point Configuration ---
// This is the network the Robot will create
#define AP_SSID  "Sesame-Controller"
#define AP_PASS  "12345678" // Must be at least 8 characters

// --- Station Mode Configuration ---
// Joined on boot so the robot is reachable at quadruped.local on the LAN, which
// is how the Python host tools (robot_link.py / robot.py / voice_control.py /
// robot_gui.py) find it via mDNS. The Sesame AP below stays up at the same time
// (WIFI_AP_STA), so the captive-portal web UI also keeps working.
// >>> Set these to YOUR network before flashing. <<<
#define NETWORK_SSID "your-wifi-ssid"    // Your WiFi network name
#define NETWORK_PASS "your-wifi-password" // Your WiFi password
#define ENABLE_NETWORK_MODE true         // STA join enabled (needed for quadruped.local)

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// I2C pins, device addresses, servo channel map and the TCP port all live in
// pins.h now (XIAO ESP32-S3 Sense + PCA9685). I2C_SDA/I2C_SCL/OLED_I2C_ADDR
// come from there.


// DNS Server for Captive Portal
DNSServer dnsServer;
const byte DNS_PORT = 53;

// clkDuring/clkAfter=400000 prevents SSD1306 from dropping Wire clock after each transaction
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET, 400000UL, 400000UL);
WebServer server(80);

// TCP command server (Albert line-protocol, see RobotNet.h). One client at a
// time, serviced on Core 0 by networkTask(). quadruped.local:8888.
WiFiServer tcpServer(TCP_CMD_PORT);
WiFiClient tcpClient;

// WiFi serial log server — streams debug output from Core 1 to any TCP client.
// Connect with: nc quadruped.local 8890  (or software/serial_monitor.py)
WiFiServer logServer(TCP_LOG_PORT);
WiFiClient logClient;

// Global state for animations
String currentCommand = "";
String currentFaceName = "default";
const unsigned char* const* currentFaceFrames = nullptr;
uint8_t currentFaceFrameCount = 0;
uint8_t currentFaceFrameIndex = 0;
unsigned long lastFaceFrameMs = 0;
int faceFps = 8;
FaceAnimMode currentFaceMode = FACE_ANIM_LOOP;
int8_t faceFrameDirection = 1;
bool faceAnimFinished = false;
int currentFaceFps = 0;
bool idleActive = false;
bool idleBlinkActive = false;
unsigned long nextIdleBlinkMs = 0;
uint8_t idleBlinkRepeatsLeft = 0;

// Speaking animation: while audio plays, alternate base face ↔ talk_<base> at ~5 fps.
static String        _speakingBaseFace = "";
static bool          _speakingToggle   = false;
static unsigned long _speakingLastMs   = 0;

// WiFi Info Scrolling
unsigned long lastInputTime = 0;
bool firstInputReceived = false;
bool showingWifiInfo = false;
int wifiScrollPos = 0;
unsigned long lastWifiScrollMs = 0;
String wifiInfoText = "";

// Network Mode
bool networkConnected = false;
IPAddress networkIP;
String deviceHostname = "quadruped";  // -> quadruped.local (matches robot_link.py)

// Servo driver — PCA9685 (16-ch PWM over I2C). Replaces the old per-GPIO
// ESP32Servo drive; channel map + pulse calibration live in pins.h.
Adafruit_PWMServoDriver pwm(PCA9685_ADDR);

// ---------------------------------------------------------------------------
// Per-servo calibration state (Core 1 only)
// ---------------------------------------------------------------------------
// trim[]     : runtime angle offset in degrees, range -45..45.
//              Applied on top of the static servoSubtrim[] baked in source.
//              Persisted to NVS on 'save', restored on boot via 'load'.
// reversed[] : runtime direction flip.
//              Left-side servos are mirror-mounted, so their physical 0->180
//              needs to be flipped. Set these during bring-up with 'rev <id>'
//              without recompiling. Persisted alongside trims.
// Both arrays are written/read on Core 1 only; no cross-core locking needed.
// Calibrated during physical bring-up with motor_tester.ino.
// servoSubtrim: offset from 90° to mechanical center for each servo.
// servoRev: true for mirror-mounted servos where increasing angle moves opposite direction.
int8_t servoSubtrim[8] = {-5, 2, 8, -4, -5, 2, 0, 4};  // R1 R2 L1 L2 R4 R3 L3 L4
int8_t servoTrim[8]    = {0, 0, 0, 0, 0, 0, 0, 0};      // NVS-backed runtime trim
bool   servoRev[8]     = {false, true, true, false,       // R1 R2 L1 L2
                           false, true, false, true};      // R4 R3 L3 L4

// ---------------------------------------------------------------------------
// NVS calibration persistence (Core 1 only, called from applyCommandLine)
// ---------------------------------------------------------------------------
#include <Preferences.h>
static const char* kNvsNamespace = "servo-cal";

void calSave() {
  Preferences p; p.begin(kNvsNamespace, false);
  for (int i = 0; i < 8; i++) {
    char kt[4], kr[4];
    snprintf(kt, sizeof(kt), "t%d", i);
    snprintf(kr, sizeof(kr), "r%d", i);
    p.putChar(kt, servoTrim[i]);
    p.putBool(kr, servoRev[i]);
  }
  p.end();
  Serial.println(F("cal: saved to NVS"));
}

void calLoad() {
  Preferences p; p.begin(kNvsNamespace, true);
  for (int i = 0; i < 8; i++) {
    char kt[4], kr[4];
    snprintf(kt, sizeof(kt), "t%d", i);
    snprintf(kr, sizeof(kr), "r%d", i);
    servoTrim[i] = p.getChar(kt, 0);
    servoRev[i]  = p.getBool(kr, false);
  }
  p.end();
}

void calClear() {
  Preferences p; p.begin(kNvsNamespace, false);
  p.clear(); p.end();
  for (int i = 0; i < 8; i++) { servoTrim[i] = 0; servoRev[i] = false; }
  Serial.println(F("cal: cleared"));
}

// ======================================================================
// Dual-core plumbing (XIAO ESP32-S3, 2x LX7 cores)
// ----------------------------------------------------------------------
// Core 1 (the Arduino loop) owns ALL I2C devices (PCA9685, OLED, IMU), the
// `currentCommand` String, the face state and the serial CLI. Core 0 runs a
// dedicated networkTask that owns WiFi/AP/DNS/HTTP/TCP and nothing else.
//
// The ONLY things crossing between cores are:
//   * cmdQueue       — Core 0 pushes parsed command lines; Core 1 drains them
//                      and is the sole writer of currentCommand / servos / face.
//   * gStopRequested — atomic safety reflex: Core 0 sets it, pressingCheck()
//                      on Core 1 polls it so "stop" aborts a pose instantly.
//   * gServoAngle[]  — single-writer (Core 1) snapshot of last commanded angles,
//                      read by Core 0 only to answer the "pose" query.
// Keeping I2C and every String on one core is what makes this race-free.
// ======================================================================
static const uint8_t  CMD_QUEUE_LEN = 8;
static const uint8_t  CMD_LINE_MAX  = 48;
static const uint8_t  IMU_QUEUE_LEN = 4;
static const uint8_t  IMU_JSON_MAX  = 128;
QueueHandle_t cmdQueue      = nullptr;
QueueHandle_t imuEventQueue = nullptr;
QueueHandle_t logQueue      = nullptr;   // Core 1 → Core 0 log forwarding (wifi_log.h)
volatile bool    gStopRequested = false;
volatile int     gServoAngle[8] = {90, 90, 90, 90, 90, 90, 90, 90};
TaskHandle_t     networkTaskHandle = nullptr;



// Animation constants
int frameDelay = 100;
int walkCycles = 10;
int motorCurrentDelay = 20; // ms delay between motor movements to prevent over-current

struct FaceEntry {
  const char* name;
  const unsigned char* const* frames;
  uint8_t maxFrames;
};

static const uint8_t MAX_FACE_FRAMES = 6;

#define MAKE_FACE_FRAMES(name) \
  const unsigned char* const face_##name##_frames[] = { \
    epd_bitmap_##name, epd_bitmap_##name##_1, epd_bitmap_##name##_2, \
    epd_bitmap_##name##_3, epd_bitmap_##name##_4, epd_bitmap_##name##_5 \
  };

#define X(name) MAKE_FACE_FRAMES(name)
FACE_LIST
#undef X
#undef MAKE_FACE_FRAMES

const FaceEntry faceEntries[] = {
#define X(name) { #name, face_##name##_frames, MAX_FACE_FRAMES },
  FACE_LIST
#undef X
  { "default", face_defualt_frames, MAX_FACE_FRAMES }
};

struct FaceFpsEntry {
  const char* name;
  uint8_t fps;
};

const FaceFpsEntry faceFpsEntries[] = {
  { "walk", 1 },
  { "rest", 1 },
  { "swim", 1 },
  { "dance", 1 },
  { "wave", 1 },
  { "point", 5 },
  { "stand", 1 },
  { "cute", 1 },
  { "pushup", 1 },
  { "freaky", 1 },
  { "bow", 1 },
  { "worm", 1 },
  { "shake", 1 },
  { "shrug", 1 },
  { "dead", 2 },
  { "crab", 1 },
  { "idle", 1 },
  { "idle_blink", 7 },
  { "default", 1 },
  // Conversational faces (manually controlled by Python - no auto-animation)
  { "happy", 1 },
  { "talk_happy", 1 },
  { "sad", 1 },
  { "talk_sad", 1 },
  { "angry", 1 },
  { "talk_angry", 1 },
  { "surprised", 1 },
  { "talk_surprised", 1 },
  { "sleepy", 1 },
  { "talk_sleepy", 1 },
  { "love", 1 },
  { "talk_love", 1 },
  { "excited", 1 },
  { "talk_excited", 1 },
  { "confused", 1 },
  { "talk_confused", 1 },
  { "thinking", 1 },
  { "talk_thinking", 1 },
};


// Prototypes
void setServoAngle(uint8_t channel, int angle);
void updateFaceBitmap(const unsigned char* bitmap);
void setFace(const String& faceName);
void setFaceMode(FaceAnimMode mode);
void setFaceWithMode(const String& faceName, FaceAnimMode mode);
void updateAnimatedFace();
void delayWithFace(unsigned long ms);
void enterIdle();
void exitIdle();
void updateIdleBlink();
int getFaceFpsForName(const String& faceName);
bool pressingCheck(String cmd, int ms);
void handleGetSettings();
void handleSetSettings();
void handleGetStatus();
void handleApiCommand();
void updateWifiInfoScroll();
void recordInput();

// Dual-core / TCP command plumbing
void networkTask(void* arg);
void serviceTcpClient();
void routeTcpLine(const char* rawLine);
void enqueueCommandLine(const char* line);
void drainCommandQueue();
void applyCommandLine(const char* line);
void setCurrentCommand(const String& cmd);
void handleImuReaction(ImuEvent ev);

// Published snapshots: written only on Core 1, read by Core 0 for status
// replies. char[] (not String) so a cross-core read can't corrupt the heap.
char gPubFace[24] = "rest";
char gPubCmd[24]  = "";

void handleRoot() {
  server.send(200, "text/html", index_html);
}

// Runs on Core 0 (inside server.handleClient). Must not touch I2C / Strings /
// currentCommand directly — everything is marshaled to Core 1 via the queue.
void handleCommandWeb() {
  // We send 200 OK immediately so the web browser doesn't hang waiting for animation to finish
  if (server.hasArg("pose")) {
    enqueueCommandLine(server.arg("pose").c_str());
    server.send(200, "text/plain", "OK");
  }
  else if (server.hasArg("go")) {
    enqueueCommandLine(server.arg("go").c_str());
    server.send(200, "text/plain", "OK");
  }
  else if (server.hasArg("stop")) {
    gStopRequested = true;            // instant reflex; Core 1 polls this
    enqueueCommandLine("stop");
    server.send(200, "text/plain", "OK");
  }
  else if (server.hasArg("motor") && server.hasArg("value")) {
    int motorNum = server.arg("motor").toInt();
    int servoIdx = servoNameToIndex(server.arg("motor"));
    int angle = server.arg("value").toInt();
    char line[CMD_LINE_MAX];
    if (motorNum >= 1 && motorNum <= 8 && angle >= 0 && angle <= 180) {
      snprintf(line, sizeof(line), "servo %d %d", motorNum - 1, angle); // 1-based -> 0-based
      enqueueCommandLine(line);
      server.send(200, "text/plain", "OK");
    } else if (servoIdx != -1 && angle >= 0 && angle <= 180) {
      snprintf(line, sizeof(line), "servo %d %d", servoIdx, angle);
      enqueueCommandLine(line);
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Invalid motor or angle");
    }
  }
  else {
    server.send(400, "text/plain", "Bad Args");
  }
}

void handleGetSettings() {
  String json = "{";
  json += "\"frameDelay\":" + String(frameDelay) + ",";
  json += "\"walkCycles\":" + String(walkCycles) + ",";
  json += "\"motorCurrentDelay\":" + String(motorCurrentDelay) + ",";
  json += "\"faceFps\":" + String(faceFps);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetSettings() {
  if (server.hasArg("frameDelay")) frameDelay = server.arg("frameDelay").toInt();
  if (server.hasArg("walkCycles")) walkCycles = server.arg("walkCycles").toInt();
  if (server.hasArg("motorCurrentDelay")) motorCurrentDelay = server.arg("motorCurrentDelay").toInt();
  if (server.hasArg("faceFps")) faceFps = (int)max(1L, server.arg("faceFps").toInt());
  server.send(200, "text/plain", "OK");
}

// API endpoint for network clients to get robot status
void handleGetStatus() {
  String json = "{";
  json += "\"currentCommand\":\"" + String(gPubCmd) + "\",";
  json += "\"currentFace\":\"" + String(gPubFace) + "\",";
  json += "\"networkConnected\":" + String(networkConnected ? "true" : "false") + ",";
  json += "\"apIP\":\"" + WiFi.softAPIP().toString() + "\"";
  if (networkConnected) {
    json += ",\"networkIP\":\"" + networkIP.toString() + "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

// API endpoint for network clients to send commands (JSON-based)
void handleApiCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = server.arg("plain");
  
  Serial.println("API Command received:");
  Serial.println(body);
  
  // Check for face-only command (no movement)
  int faceOnlyStart = body.indexOf("\"face\":\"");
  if (faceOnlyStart == -1) {
    faceOnlyStart = body.indexOf("\"face\": \"");
  }
  
  // If we have a face but no command field, it's face-only
  bool faceOnly = (faceOnlyStart > 0 && body.indexOf("\"command\":") == -1 && body.indexOf("\"command\": ") == -1);
  
  String command = "";
  String face = "";
  
  // Parse face
  if (faceOnlyStart > 0) {
    faceOnlyStart = body.indexOf("\"", faceOnlyStart + 6) + 1;
    int faceEnd = body.indexOf("\"", faceOnlyStart);
    if (faceEnd > faceOnlyStart) {
      face = body.substring(faceOnlyStart, faceEnd);
      Serial.print("Parsed face: ");
      Serial.println(face);
    }
  }
  
  // Parse command (if not face-only)
  if (!faceOnly) {
    int cmdStart = body.indexOf("\"command\":\"");
    if (cmdStart == -1) {
      cmdStart = body.indexOf("\"command\": \"");
    }
    
    if (cmdStart == -1) {
      Serial.println("Error: command field not found");
      server.send(400, "application/json", "{\"error\":\"Missing command field\"}");
      return;
    }
    
    cmdStart = body.indexOf("\"", cmdStart + 10) + 1;
    int cmdEnd = body.indexOf("\"", cmdStart);
    
    if (cmdEnd <= cmdStart) {
      Serial.println("Error: invalid command format");
      server.send(400, "application/json", "{\"error\":\"Invalid command format\"}");
      return;
    }
    
    command = body.substring(cmdStart, cmdEnd);
    Serial.print("Parsed command: ");
    Serial.println(command);
  }
  
  // Set face if provided — marshaled to Core 1 (OLED/I2C owner) via the queue.
  if (face.length() > 0) {
    char line[CMD_LINE_MAX];
    snprintf(line, sizeof(line), "face %s", face.c_str());
    enqueueCommandLine(line);
  }

  // If face-only, just acknowledge
  if (faceOnly) {
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Face updated\"}");
    return;
  }

  // Execute command
  if (command == "stop") {
    gStopRequested = true;
    enqueueCommandLine("stop");
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Command stopped\"}");
  } else {
    enqueueCommandLine(command.c_str());
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Command executed\"}");
  }
}

void setup() {
  Serial.begin(115200);
  randomSeed(micros());

  // I2C — explicit pins required (Wire.begin() default no longer maps to GPIO5/6
  // on current Seeed XIAO ESP32-S3 board package). 100kHz for stable MPU-6050 comms.
  delay(500);    // MPU-6050 needs ~100ms after Vcc stable; 500ms gives margin
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);
  imuSetup();
  audioSetup();
  micSetup();

  // OLED Init
  // periphBegin=false: we own Wire.begin(), SSD1306 must not re-init it
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR, false, false)) {
    Serial.println(F("SSD1306 allocation failed."));
    while (1);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println(F("Setting up WiFi..."));
  display.display();

  // --- WIFI CONFIGURATION ---
  // Try to connect to network first if configured
  if (ENABLE_NETWORK_MODE && String(NETWORK_SSID).length() > 0) {
    Serial.println("Attempting to connect to network: " + String(NETWORK_SSID));
    WiFi.mode(WIFI_AP_STA); // Enable both AP and Station modes
    WiFi.setHostname(deviceHostname.c_str());
    WiFi.begin(NETWORK_SSID, NETWORK_PASS);
    
    // Wait up to 10 seconds for connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      networkConnected = true;
      networkIP = WiFi.localIP();
      Serial.println();
      Serial.print("Connected to network! IP: ");
      Serial.println(networkIP);
    } else {
      Serial.println();
      Serial.println("Failed to connect to network. Running in AP-only mode.");
      WiFi.mode(WIFI_AP); // Fall back to AP-only
    }
  } else {
    WiFi.mode(WIFI_AP);
    Serial.println("Network mode disabled. Running in AP-only mode.");
  }
  
  // --- ACCESS POINT CONFIGURATION ---
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress myIP = WiFi.softAPIP();
  
  Serial.print("AP Created. IP: ");
  Serial.println(myIP);

  // Build WiFi info text for scrolling
  if (networkConnected) {
    wifiInfoText = "AP: " + String(AP_SSID) + " (" + myIP.toString() + ")  |  Network: " + String(NETWORK_SSID) + " (" + networkIP.toString() + ") or " + deviceHostname + ".local  |  ";
  } else {
    wifiInfoText = "Connect to WiFi: " + String(AP_SSID) + "  |  Pass: " + String(AP_PASS) + "  |  IP: " + myIP.toString() + "  |  Captive Portal will auto-open!  |  ";
  }
  
  // Initialize input tracking
  lastInputTime = millis();
  firstInputReceived = false;
  showingWifiInfo = false;

  // Start mDNS responder for local network discovery
  if (MDNS.begin(deviceHostname.c_str())) {
    Serial.println("mDNS responder started");
    Serial.print("Access controller at: http://");
    Serial.print(deviceHostname);
    Serial.println(".local");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Error setting up mDNS responder!");
  }

  // Start DNS Server for Captive Portal
  // This redirects ALL domain requests to the ESP32's IP
  dnsServer.start(DNS_PORT, "*", myIP);

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/cmd", handleCommandWeb);
  server.on("/getSettings", handleGetSettings);
  server.on("/setSettings", handleSetSettings);
  
  // API endpoints for network communication
  server.on("/api/status", handleGetStatus);
  server.on("/api/command", handleApiCommand);
  
  // Catch-all route for captive portal
  // This ensures any URL redirects to the controller page
  server.onNotFound(handleRoot);
  
  server.begin();

  // Servo driver init — PCA9685 over the shared I2C bus (Wire already begun).
  pwm.begin();
  pwm.setPWMFreq(SERVO_PWM_FREQ_HZ); // 50Hz, uses default 25MHz PCA9685 oscillator
  delay(10);
  calLoad();   // restore runtime trims + reversal flags from NVS

  // TCP command server (Albert line-protocol) + mDNS service advert.
  tcpServer.begin();
  tcpServer.setNoDelay(true);
  logServer.begin();
  logServer.setNoDelay(true);
  MDNS.addService("robot", "tcp", TCP_CMD_PORT);
  Serial.print(F("TCP command server on port "));
  Serial.println(TCP_CMD_PORT);

  // Cross-core command queue, then pin the network stack to Core 0. The Arduino
  // loop() keeps running on Core 1 and owns all servo/OLED/IMU/I2C work.
  cmdQueue      = xQueueCreate(CMD_QUEUE_LEN, CMD_LINE_MAX);
  imuEventQueue = xQueueCreate(IMU_QUEUE_LEN, IMU_JSON_MAX);
  logQueue      = xQueueCreate(LOG_QUEUE_LEN, LOG_MSG_MAX);
  xTaskCreatePinnedToCore(networkTask, "networkTask", 8192, nullptr, 1,
                          &networkTaskHandle, 0);

  // Show rest face on startup without moving motors
  setFace("rest");

  Serial.println(F("HTTP server & Captive Portal started."));
}

void loop() {
  // Core 1: motion + face + serial only. DNS / HTTP / TCP are serviced by
  // networkTask on Core 0. Incoming network commands arrive via cmdQueue.
  drainCommandQueue();

  // Detect when audio finishes and suppress tap briefly — speaker resonance
  // after playback ends creates jerk spikes that look like real taps.
  // Must snapshot state BEFORE audioPump() so we catch the exact tick it stops.
  static bool _prevAudioPlaying = false;
  bool _wasPlaying = isAudioPlaying();
  audioPump();
  bool _isPlaying = isAudioPlaying();
  if (_wasPlaying && !_isPlaying) imuSuppressTap(800);
  _prevAudioPlaying = _isPlaying;

  imuPoll();
  handleImuReaction(imuConsumeReaction());


  // Safety reflex: a "stop" from any transport sets gStopRequested on Core 0.
  // Clear the running pose here; pressingCheck() also polls it mid-pose.
  if (gStopRequested) {
    gStopRequested = false;
    currentCommand = "";
  }

  updateAnimatedFace();
  updateIdleBlink();
  updateSpeakingFace();
  updateWifiInfoScroll();

  // Keep the network-readable command snapshot in sync (single writer = Core 1).
  strncpy(gPubCmd, currentCommand.c_str(), sizeof(gPubCmd) - 1);
  gPubCmd[sizeof(gPubCmd) - 1] = '\0';

  if (currentCommand != "") {
    String cmd = currentCommand;
    if (cmd == "forward") runWalkPose();
    else if (cmd == "backward") runWalkBackward();
    else if (cmd == "left") runTurnLeft();
    else if (cmd == "right") runTurnRight();
    else if (cmd == "rest") { runRestPose(); if (currentCommand == "rest") currentCommand = ""; }
    else if (cmd == "stand") { runStandPose(1); if (currentCommand == "stand") currentCommand = ""; }
    else if (cmd == "wave") runWavePose();
    else if (cmd == "dance") runDancePose();
    else if (cmd == "swim") runSwimPose();
    else if (cmd == "point") runPointPose();
    else if (cmd == "pushup") runPushupPose();
    else if (cmd == "bow") runBowPose();
    else if (cmd == "cute") runCutePose();
    else if (cmd == "freaky") runFreakyPose();
    else if (cmd == "worm") runWormPose();
    else if (cmd == "shake") runShakePose();
    else if (cmd == "shrug") runShrugPose();
    else if (cmd == "dead") runDeadPose();
    else if (cmd == "crab") runCrabPose();
  }
  
  // Serial CLI — same vocabulary as TCP (applyCommandLine handles both).
  // '?' is an alias for 'help'. Legacy "rn xx" shortcuts are gone; use the
  // full names (forward, wave, etc.) or the calibration commands below.
  if (Serial.available()) {
    static char serialBuf[CMD_LINE_MAX];
    static uint8_t serialLen = 0;
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLen > 0) {
        serialBuf[serialLen] = '\0';
        recordInput();
        if (strcmp(serialBuf, "?") == 0) {
          applyCommandLine("help");
        } else {
          applyCommandLine(serialBuf);
        }
        serialLen = 0;
      }
    } else if (serialLen < CMD_LINE_MAX - 1) {
      serialBuf[serialLen++] = c;
    }
  }
}

// Function to update the robot's face
void updateFaceBitmap(const unsigned char* bitmap) {
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, 128, 64, SSD1306_WHITE);
  display.display();
}

uint8_t countFrames(const unsigned char* const* frames, uint8_t maxFrames) {
  if (frames == nullptr || frames[0] == nullptr) return 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < maxFrames; i++) {
    if (frames[i] == nullptr) break;
    count++;
  }
  return count;
}

void setFace(const String& faceName) {
  if (faceName == currentFaceName && currentFaceFrames != nullptr) return;

  currentFaceName = faceName;
  currentFaceFrameIndex = 0;
  lastFaceFrameMs = 0;
  faceFrameDirection = 1;
  faceAnimFinished = false;
  currentFaceFps = getFaceFpsForName(faceName);

  currentFaceFrames = face_defualt_frames;
  currentFaceFrameCount = countFrames(face_defualt_frames, MAX_FACE_FRAMES);

  for (size_t i = 0; i < (sizeof(faceEntries) / sizeof(faceEntries[0])); i++) {
    if (faceName.equalsIgnoreCase(faceEntries[i].name)) {
      currentFaceFrames = faceEntries[i].frames;
      currentFaceFrameCount = countFrames(faceEntries[i].frames, faceEntries[i].maxFrames);
      break;
    }
  }

  if (currentFaceFrameCount == 0) {
    currentFaceFrames = face_defualt_frames;
    currentFaceFrameCount = countFrames(face_defualt_frames, MAX_FACE_FRAMES);
    currentFaceName = "default";
    currentFaceFps = getFaceFpsForName(currentFaceName);
  }

  if (currentFaceFrameCount > 0 && currentFaceFrames[0] != nullptr) {
    updateFaceBitmap(currentFaceFrames[0]);
  }

  // Publish for the network status endpoint (Core 1 is the sole writer).
  strncpy(gPubFace, currentFaceName.c_str(), sizeof(gPubFace) - 1);
  gPubFace[sizeof(gPubFace) - 1] = '\0';
}

void setFaceMode(FaceAnimMode mode) {
  currentFaceMode = mode;
  faceFrameDirection = 1;
  faceAnimFinished = false;
}

void setFaceWithMode(const String& faceName, FaceAnimMode mode) {
  setFaceMode(mode);
  setFace(faceName);
}

int getFaceFpsForName(const String& faceName) {
  for (size_t i = 0; i < (sizeof(faceFpsEntries) / sizeof(faceFpsEntries[0])); i++) {
    if (faceName.equalsIgnoreCase(faceFpsEntries[i].name)) {
      return faceFpsEntries[i].fps;
    }
  }
  return faceFps;
}

void updateAnimatedFace() {
  if (currentFaceFrames == nullptr || currentFaceFrameCount <= 1) return;
  if (currentFaceMode == FACE_ANIM_ONCE && faceAnimFinished) return;

  unsigned long now = millis();
  int fps = max(1, (currentFaceFps > 0 ? currentFaceFps : faceFps));
  unsigned long interval = 1000UL / fps;
  if (now - lastFaceFrameMs >= interval) {
    lastFaceFrameMs = now;
    if (currentFaceMode == FACE_ANIM_LOOP) {
      currentFaceFrameIndex = (currentFaceFrameIndex + 1) % currentFaceFrameCount;
    } else if (currentFaceMode == FACE_ANIM_ONCE) {
      if (currentFaceFrameIndex + 1 >= currentFaceFrameCount) {
        currentFaceFrameIndex = currentFaceFrameCount - 1;
        faceAnimFinished = true;
      } else {
        currentFaceFrameIndex++;
      }
    } else {
      if (faceFrameDirection > 0) {
        if (currentFaceFrameIndex + 1 >= currentFaceFrameCount) {
          faceFrameDirection = -1;
          if (currentFaceFrameIndex > 0) currentFaceFrameIndex--;
        } else {
          currentFaceFrameIndex++;
        }
      } else {
        if (currentFaceFrameIndex == 0) {
          faceFrameDirection = 1;
          if (currentFaceFrameCount > 1) currentFaceFrameIndex++;
        } else {
          currentFaceFrameIndex--;
        }
      }
    }
    updateFaceBitmap(currentFaceFrames[currentFaceFrameIndex]);
  }
}

void delayWithFace(unsigned long ms) {
  // Core 1 helper. Networking is serviced on Core 0, so here we only animate the
  // face and pull any queued commands so a new pose/stop is seen promptly.
  unsigned long start = millis();
  while (millis() - start < ms) {
    updateAnimatedFace();
    audioPump();
    drainCommandQueue();
    delay(5);
  }
}

void scheduleNextIdleBlink(unsigned long minMs, unsigned long maxMs) {
  unsigned long now = millis();
  unsigned long interval = (unsigned long)random(minMs, maxMs);
  nextIdleBlinkMs = now + interval;
}

void enterIdle() {
  idleActive = true;
  idleBlinkActive = false;
  idleBlinkRepeatsLeft = 0;
  setFaceWithMode("idle", FACE_ANIM_BOOMERANG);
  scheduleNextIdleBlink(3000, 7000);
}

void exitIdle() {
  idleActive = false;
  idleBlinkActive = false;
}

void startSpeakingFace(const String& baseFace) {
  _speakingBaseFace = baseFace;
  _speakingToggle   = false;
  _speakingLastMs   = millis();
  setFace(baseFace);
}

void updateSpeakingFace() {
  if (!isAudioPlaying()) {
    if (!_speakingBaseFace.isEmpty()) {
      setFace(_speakingBaseFace);   // restore base face when audio ends
      _speakingBaseFace = "";
    }
    return;
  }
  if (_speakingBaseFace.isEmpty()) return;
  if (millis() - _speakingLastMs < 180) return;   // ~5-6 fps mouth flap
  _speakingLastMs = millis();
  _speakingToggle = !_speakingToggle;
  setFace(_speakingToggle ? ("talk_" + _speakingBaseFace) : _speakingBaseFace);
}

void updateIdleBlink() {
  if (!idleActive) return;

  if (!idleBlinkActive) {
    if (millis() >= nextIdleBlinkMs) {
      idleBlinkActive = true;
      if (idleBlinkRepeatsLeft == 0 && random(0, 100) < 30) {
        idleBlinkRepeatsLeft = 1; // double blink
      }
      setFaceWithMode("idle_blink", FACE_ANIM_ONCE);
    }
    return;
  }

  if (currentFaceMode == FACE_ANIM_ONCE && faceAnimFinished) {
    idleBlinkActive = false;
    setFaceWithMode("idle", FACE_ANIM_BOOMERANG);
    if (idleBlinkRepeatsLeft > 0) {
      idleBlinkRepeatsLeft--;
      scheduleNextIdleBlink(120, 220);
    } else {
      scheduleNextIdleBlink(3000, 7000);
    }
  }
}

// ====== HELPERS ======
// Drives one logical servo via the PCA9685. Core 1 only (I2C owner).
// Applies, in order: runtime trim, compile-time subtrim, reversal override.
// The 'angle' stored in gServoAngle is the raw logical value (pre-trim) so
// nudge can compute deltas correctly and 'pose' prints what you commanded.
void setServoAngle(uint8_t channel, int angle) {
  if (channel >= 8) return;
  int trimmed = constrain(angle + servoTrim[channel] + servoSubtrim[channel], 0, 180);
  int physical = servoRev[channel] ? 180 - trimmed : trimmed;
  uint16_t ticks = map(physical, 0, 180, SERVOMIN, SERVOMAX);
  pwm.setPWM(servoChannel[channel], 0, ticks);
  gServoAngle[channel] = angle;   // snapshot for the network "pose" / nudge
  delayWithFace(motorCurrentDelay);
}

// Holds a gait keyframe for `ms`, but bails the instant the held command is
// superseded or a stop arrives. Runs on Core 1; the network lives on Core 0, so
// here we poll the cross-core stop flag and drain queued commands.
bool pressingCheck(String cmd, int ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    updateAnimatedFace();
    drainCommandQueue();
    if (gStopRequested) {
      gStopRequested = false;
      currentCommand = "";
      runStandPose(1);
      return false;
    }
    if (currentCommand != cmd) {
      runStandPose(1);
      return false;
    }
    yield();
  }
  return true;
}

void recordInput() {
  lastInputTime = millis();
  if (!firstInputReceived) {
    firstInputReceived = true;
    showingWifiInfo = false;
  }
}

void updateWifiInfoScroll() {
  // Don't show WiFi info if first input has been received
  if (firstInputReceived) {
    if (showingWifiInfo) {
      showingWifiInfo = false;
      // Restore the current face
      if (currentFaceFrames != nullptr && currentFaceFrameCount > 0) {
        updateFaceBitmap(currentFaceFrames[currentFaceFrameIndex]);
      }
    }
    return;
  }
  
  unsigned long now = millis();
  
  // Check if 30 seconds have passed without input
  if (!showingWifiInfo && (now - lastInputTime >= 30000)) {
    showingWifiInfo = true;
    wifiScrollPos = 0;
    lastWifiScrollMs = now;
  }
  
  if (!showingWifiInfo) return;
  
  // Update scroll every 150ms
  if (now - lastWifiScrollMs >= 150) {
    lastWifiScrollMs = now;
    
    // Clear and redraw with current face in background
    display.clearDisplay();
    
    // Draw the face bitmap in the background
    if (currentFaceFrames != nullptr && currentFaceFrameCount > 0) {
      display.drawBitmap(0, 0, currentFaceFrames[currentFaceFrameIndex], 128, 64, SSD1306_WHITE);
    }
    
    // Draw black bar for text background on top row
    display.fillRect(0, 0, 128, 10, SSD1306_BLACK);
    
    // Draw scrolling text
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    display.setCursor(-wifiScrollPos, 1);
    display.print(wifiInfoText);
    display.setTextWrap(true);
    
    display.display();
    
    // Advance scroll position
    wifiScrollPos += 2;
    if (wifiScrollPos >= (int)(wifiInfoText.length() * 6)) {
      wifiScrollPos = 0;
    }
  }
}

// ===========================================================================
// IMU reaction handler — Core 1 only, called from loop() after imuPoll().
// Drives face and movement directly; no queue needed (same core).
// ---------------------------------------------------------------------------
void handleImuReaction(ImuEvent ev) {
  switch (ev) {
    case IMU_PICKUP:
      if (isAudioPlaying()) { imuResetLevelTimer(); break; }  // don't cut voice response
      currentFaceName = "";
      setFace("scared");      // show scared face immediately alongside the sound
      playWavFromSPIFFS("/woah_flying.wav");
      imuResetLevelTimer();
      runWiggle();
      exitIdle();
      imuResetLevelTimer();
      break;
    case IMU_FLIPPED:
      if (isAudioPlaying()) break;  // don't cut voice response
      playWavFromSPIFFS("/upside_down.wav");
      setFace("dizzy");
      exitIdle();
      break;
    case IMU_TAPPED:
      // Ignore taps while audio is playing — speaker vibration creates jerk
      // spikes that auto-retrigger and cut the response mid-play.
      if (isAudioPlaying()) break;
      dlogs("Voice: tap received");
      stopAudio();
      currentCommand = "";
      setFace("excited");
      audioActivationChirp();
      dlogs("Voice: starting mic");
      {
        size_t bytes = micRecordWithVAD();
        dlog("Voice: captured %zu bytes (%.1fs)", bytes, bytes / 32000.0f);
        if (bytes > 0) {
          audioGotItChirp();           // immediate "got it" feedback before slow server call
          setFace("love");             // "thinking" face while waiting for server
          dlog("Voice: sending %zu bytes to server", bytes);
          if (voiceRequest(micBuffer(), bytes)) {
            dlogs("Voice: playing response");
            startSpeakingFace("excited");
            playWavFromSPIFFS(VOICE_RESPONSE_PATH);
          } else {
            dlogs("Voice: server error");
          }
        } else {
          dlogs("Voice: no speech detected");
        }
      }
      // Suppress tap for 2s after pipeline — prevents speaker acoustic feedback
      // or residual vibration from immediately re-triggering.
      imuSuppressTap(2000);
      enterIdle();
      break;
    case IMU_FREEFALL:
      if (isAudioPlaying()) break;
      playWavFromSPIFFS("/falling.wav");
      setFace("scared");
      exitIdle();
      break;
    case IMU_LEVEL:
      // Don't stopAudio() here — would cut voice responses mid-play.
      // Suppress tap for 2s: the set-down jolt creates a jerk spike that
      // would immediately re-trigger voice recording.
      imuSuppressTap(2000);
      currentCommand = "";
      enterIdle();
      break;
    default:
      break;
  }
}

// Cross-core command pipeline
// ===========================================================================
// enqueueCommandLine : called on EITHER core; hands a command line to Core 1.
// drainCommandQueue  : Core 1 only; pops lines and applies them.
// applyCommandLine   : Core 1 only; the single place that mutates currentCommand
//                      / servos / face from a text command.
// ---------------------------------------------------------------------------

void enqueueCommandLine(const char* line) {
  if (cmdQueue == nullptr || line == nullptr) return;
  char buf[CMD_LINE_MAX];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  // Non-blocking: motion commands are idempotent enough that dropping one under
  // a full queue is safer than stalling a core.
  xQueueSend(cmdQueue, buf, 0);
}

void drainCommandQueue() {
  if (cmdQueue == nullptr) return;
  char buf[CMD_LINE_MAX];
  while (xQueueReceive(cmdQueue, buf, 0) == pdTRUE) {
    applyCommandLine(buf);
  }
}

// Set the active movement command (Core 1). Mirrors the web path: wake from
// idle and mark input so the WiFi-info screen stays dismissed.
void setCurrentCommand(const String& cmd) {
  currentCommand = cmd;
  exitIdle();
  recordInput();
  strncpy(gPubCmd, currentCommand.c_str(), sizeof(gPubCmd) - 1);
  gPubCmd[sizeof(gPubCmd) - 1] = '\0';
}

// The full command vocabulary, applied on Core 1. Accepts both the Sesame
// movement words and the Albert line-protocol primitives the Python host tools
// emit (servo/all/neutral/hips/knees/stance/gait/face/stop).
void applyCommandLine(const char* rawLine) {
  // Normalize: trim + lowercase into a local buffer.
  char line[CMD_LINE_MAX];
  strncpy(line, rawLine, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  for (char* p = line; *p; ++p) *p = tolower(*p);
  char* s = line;
  while (*s == ' ' || *s == '\t') ++s;
  int end = strlen(s) - 1;
  while (end >= 0 && (s[end] == ' ' || s[end] == '\t' || s[end] == '\r' || s[end] == '\n')) s[end--] = '\0';
  if (*s == '\0') return;

  // First token = verb.
  char verb[16];
  int vi = 0;
  while (s[vi] && s[vi] != ' ' && vi < (int)sizeof(verb) - 1) { verb[vi] = s[vi]; vi++; }
  verb[vi] = '\0';
  const char* args = (s[vi] == ' ') ? s + vi + 1 : s + vi;

  // --- stop / e-stop -------------------------------------------------------
  if (!strcmp(verb, "stop") || !strcmp(verb, "halt") || !strcmp(verb, "freeze")) {
    currentCommand = "";
    gPubCmd[0] = '\0';
    return;
  }

  // --- info / calibration --------------------------------------------------
  if (!strcmp(verb, "help") || !strcmp(verb, "?")) {
    Serial.println(F("--- movement ---"));
    Serial.println(F("forward backward left right stand rest stop"));
    Serial.println(F("wave dance swim point pushup bow cute freaky worm shake shrug dead crab"));
    Serial.println(F("--- direct servo ---"));
    Serial.println(F("neutral             all servos to 90"));
    Serial.println(F("all <0-180>         all servos to one angle"));
    Serial.println(F("servo <id> <0-180>  one servo (id 0-7 or name R1/R2/L1/L2/R3/R4/L3/L4)"));
    Serial.println(F("nudge <id> <delta>  relative tweak, e.g. nudge 2 5"));
    Serial.println(F("hips <a>  knees <a>  stance <hip> <knee>"));
    Serial.println(F("--- calibration ---"));
    Serial.println(F("trim <id> <-45..45> runtime angle offset (stacks on subtrim)"));
    Serial.println(F("rev <id>            flip servo direction (toggle)"));
    Serial.println(F("save / load / clear persist trim+rev to NVS"));
    Serial.println(F("--- info ---"));
    Serial.println(F("map   servo name -> channel + current trim + rev"));
    Serial.println(F("pose  current logical angles"));
    Serial.println(F("dump  copy-pasteable servoSubtrim[] for baking into code"));
    Serial.println(F("wifi  IP and mDNS info"));
    if (tcpClient && tcpClient.connected()) {
      tcpClient.println(F("movement: forward backward left right stand rest stop"));
      tcpClient.println(F("tricks:   wave dance swim point pushup bow cute freaky worm shake shrug dead crab"));
      tcpClient.println(F("servo:    servo <id> <a>, nudge <id> <d>, all <a>, neutral, hips <a>, knees <a>, stance <h> <k>"));
      tcpClient.println(F("cal:      trim <id> <-45..45>, rev <id>, save, load, clear"));
      tcpClient.println(F("info:     map, pose, dump, wifi, help"));
    }
    return;
  }

  if (!strcmp(verb, "map")) {
    for (int i = 0; i < 8; i++) {
      Serial.print(i); Serial.print(F(" "));
      Serial.print(ServoNames[i]); Serial.print(F("  ch="));
      Serial.print(servoChannel[i]); Serial.print(F("  trim="));
      Serial.print(servoTrim[i] + servoSubtrim[i]);
      Serial.print(F("  rev=")); Serial.println(servoRev[i] ? F("yes") : F("no"));
    }
    if (tcpClient && tcpClient.connected()) {
      for (int i = 0; i < 8; i++) {
        tcpClient.print(i); tcpClient.print(F(" ")); tcpClient.print(ServoNames[i]);
        tcpClient.print(F(" ch=")); tcpClient.print(servoChannel[i]);
        tcpClient.print(F(" trim=")); tcpClient.print(servoTrim[i] + servoSubtrim[i]);
        tcpClient.print(F(" rev=")); tcpClient.println(servoRev[i] ? F("yes") : F("no"));
      }
    }
    return;
  }

  if (!strcmp(verb, "pose") || !strcmp(verb, "status")) {
    for (int i = 0; i < 8; i++) {
      Serial.print(ServoNames[i]); Serial.print(F("=")); Serial.print(gServoAngle[i]);
      Serial.print(i < 7 ? F("  ") : F("\n"));
    }
    return;
  }

  if (!strcmp(verb, "dump")) {
    Serial.print(F("int8_t servoSubtrim[8] = {"));
    for (int i = 0; i < 8; i++) {
      Serial.print((int)(servoSubtrim[i] + servoTrim[i]));
      if (i < 7) Serial.print(F(", "));
    }
    Serial.println(F("};  // bake these into source then clear NVS"));
    return;
  }

  if (!strcmp(verb, "wifi")) {
    Serial.print(F("ap:  ")); Serial.println(WiFi.softAPIP());
    Serial.print(F("sta: ")); Serial.println(networkConnected ? WiFi.localIP() : IPAddress(0,0,0,0));
    Serial.print(F("host: ")); Serial.print(deviceHostname); Serial.print(F(".local:")); Serial.println(TCP_CMD_PORT);
    return;
  }

  if (!strcmp(verb, "save"))  { calSave(); return; }
  if (!strcmp(verb, "load"))  { calLoad(); Serial.println(F("cal: loaded")); return; }
  if (!strcmp(verb, "clear")) { calClear(); return; }

  // --- direct servo control (clears any running pose first) ----------------
  if (!strcmp(verb, "neutral")) {
    currentCommand = ""; gPubCmd[0] = '\0';
    for (int i = 0; i < 8; i++) setServoAngle(i, 90);
    Serial.println(F("all -> 90"));
    return;
  }
  if (!strcmp(verb, "all")) {
    int a; if (sscanf(args, "%d", &a) == 1) {
      currentCommand = ""; gPubCmd[0] = '\0';
      a = constrain(a, 0, 180);
      for (int i = 0; i < 8; i++) setServoAngle(i, a);
      Serial.print(F("all -> ")); Serial.println(a);
    }
    return;
  }
  if (!strcmp(verb, "servo")) {
    int id, a; if (sscanf(args, "%d %d", &id, &a) == 2 && id >= 0 && id < 8) {
      currentCommand = ""; gPubCmd[0] = '\0';
      a = constrain(a, 0, 180);
      setServoAngle((uint8_t)id, a);
      Serial.print(ServoNames[id]); Serial.print(F(" -> ")); Serial.println(a);
    }
    return;
  }
  if (!strcmp(verb, "nudge")) {
    int id, delta; if (sscanf(args, "%d %d", &id, &delta) == 2 && id >= 0 && id < 8) {
      currentCommand = ""; gPubCmd[0] = '\0';
      int next = constrain((int)gServoAngle[id] + delta, 0, 180);
      setServoAngle((uint8_t)id, next);
      Serial.print(ServoNames[id]); Serial.print(F(" -> ")); Serial.println(next);
    }
    return;
  }
  if (!strcmp(verb, "trim")) {
    int id, t; if (sscanf(args, "%d %d", &id, &t) == 2 && id >= 0 && id < 8) {
      servoTrim[id] = (int8_t)constrain(t, -45, 45);
      setServoAngle((uint8_t)id, gServoAngle[id]);  // re-drive immediately
      Serial.print(ServoNames[id]); Serial.print(F(" trim=")); Serial.println(servoTrim[id]);
    }
    return;
  }
  if (!strcmp(verb, "rev")) {
    int id; if (sscanf(args, "%d", &id) == 1 && id >= 0 && id < 8) {
      servoRev[id] = !servoRev[id];
      setServoAngle((uint8_t)id, gServoAngle[id]);  // re-drive with new direction
      Serial.print(ServoNames[id]); Serial.print(F(" rev=")); Serial.println(servoRev[id] ? F("yes") : F("no"));
    } else {
      // no id: list all
      for (int i = 0; i < 8; i++) {
        Serial.print(i); Serial.print(F(" ")); Serial.print(ServoNames[i]);
        Serial.print(F(" rev=")); Serial.println(servoRev[i] ? F("yes") : F("no"));
      }
    }
    return;
  }
  if (!strcmp(verb, "hips")) {
    int a; if (sscanf(args, "%d", &a) == 1) {
      currentCommand = ""; gPubCmd[0] = '\0';
      a = constrain(a, 0, 180);
      setServoAngle(R1, a); setServoAngle(R2, a); setServoAngle(L1, a); setServoAngle(L2, a);
    }
    return;
  }
  if (!strcmp(verb, "knees")) {
    int a; if (sscanf(args, "%d", &a) == 1) {
      currentCommand = ""; gPubCmd[0] = '\0';
      a = constrain(a, 0, 180);
      setServoAngle(R4, a); setServoAngle(R3, a); setServoAngle(L3, a); setServoAngle(L4, a);
    }
    return;
  }
  if (!strcmp(verb, "stance")) {
    int hip, knee; if (sscanf(args, "%d %d", &hip, &knee) == 2) {
      currentCommand = ""; gPubCmd[0] = '\0';
      hip = constrain(hip, 0, 180); knee = constrain(knee, 0, 180);
      setServoAngle(R1, hip); setServoAngle(R2, hip); setServoAngle(L1, hip); setServoAngle(L2, hip);
      setServoAngle(R4, knee); setServoAngle(R3, knee); setServoAngle(L3, knee); setServoAngle(L4, knee);
    }
    return;
  }

  // --- gait <leftScale> <rightScale> ---------------------------------------
  // Sesame has no continuous sine engine, so the Albert per-side scales are
  // collapsed to the nearest discrete walk. Arcs/circles won't be faithful, but
  // forward/back/spin map cleanly. (Flagged to the user.)
  if (!strcmp(verb, "gait")) {
    float l, r; if (sscanf(args, "%f %f", &l, &r) == 2) {
      if      (l < 0 && r < 0) setCurrentCommand("forward");
      else if (l > 0 && r > 0) setCurrentCommand("backward");
      else if (l > 0 && r < 0) setCurrentCommand("left");
      else if (l < 0 && r > 0) setCurrentCommand("right");
      else { currentCommand = ""; gPubCmd[0] = '\0'; }
    }
    return;
  }

  // --- face <name> ---------------------------------------------------------
  if (!strcmp(verb, "face")) {
    if (*args) setFace(String(args));
    return;
  }

  // --- aliases -------------------------------------------------------------
  if (!strcmp(verb, "walk")) { setCurrentCommand("forward"); return; }
  if (!strcmp(verb, "back")) { setCurrentCommand("backward"); return; }

  // --- movement / trick poses (handled by the loop dispatcher) -------------
  static const char* kPoses[] = {
    "forward", "backward", "left", "right", "rest", "stand", "wave", "dance",
    "swim", "point", "pushup", "bow", "cute", "freaky", "worm", "shake",
    "shrug", "dead", "crab"
  };
  for (size_t i = 0; i < sizeof(kPoses) / sizeof(kPoses[0]); i++) {
    if (!strcmp(verb, kPoses[i])) { setCurrentCommand(verb); return; }
  }
  // Unknown verbs are ignored (calibration-only Albert verbs like nudge/trim/
  // rev/save/load/map have no Sesame equivalent).
}

// ===========================================================================
// Core 0 — network task
// ===========================================================================
// Owns the captive-portal DNS, the HTTP WebServer and the TCP command server.
// Touches no I2C, no Strings shared with Core 1, and never calls a pose/servo
// directly — incoming commands go onto cmdQueue for Core 1 to apply.

void networkTask(void* arg) {
  for (;;) {
    dnsServer.processNextRequest();
    server.handleClient();
    serviceTcpClient();
    vTaskDelay(1);   // yield ~1ms; keeps WiFi/lwIP fed without busy-spinning
  }
}

// Accepts one TCP client at a time (Albert protocol) and feeds its newline-
// framed commands into the queue. Query verbs that only need data Core 0 can
// see (help/wifi/pose) are answered here directly.
void serviceTcpClient() {
  if (!tcpClient || !tcpClient.connected()) {
    WiFiClient incoming = tcpServer.available();
    if (incoming) {
      tcpClient = incoming;
      tcpClient.setNoDelay(true);
      tcpClient.println(F("quadruped connected — type 'help'"));
    }
  }
  // Accept log monitor client and drain queue — independent of command client.
  if (!logClient || !logClient.connected()) {
    WiFiClient incoming = logServer.available();
    if (incoming) {
      logClient = incoming;
      logClient.printf("=== sesame log  uptime=%lus  sta=%s ===\r\n",
                       millis() / 1000,
                       WiFi.localIP().toString().c_str());
    }
  }
  if (logClient && logClient.connected() && logQueue) {
    static char logMsg[LOG_MSG_MAX];
    while (xQueueReceive(logQueue, logMsg, 0) == pdTRUE) {
      logClient.println(logMsg);
    }
  }

  if (!tcpClient || !tcpClient.connected()) return;

  // Drain IMU events queued by Core 1 (imuEmit) and push over TCP
  if (imuEventQueue) {
    static char imuJson[IMU_JSON_MAX];
    while (xQueueReceive(imuEventQueue, imuJson, 0) == pdTRUE) {
      tcpClient.println(imuJson);
    }
  }

  static char buf[CMD_LINE_MAX];
  static uint8_t len = 0;
  while (tcpClient.available() > 0) {
    char ch = (char)tcpClient.read();
    if (ch == '\r' || ch == '\n') {
      if (len > 0) {
        buf[len] = '\0';
        routeTcpLine(buf);
        len = 0;
      }
    } else if (len < CMD_LINE_MAX - 1) {
      buf[len++] = ch;
    }
  }
}

// Handle one TCP command line: echo it, answer local queries, else enqueue.
void routeTcpLine(const char* rawLine) {
  // Lowercase/trim a copy for verb matching.
  char line[CMD_LINE_MAX];
  strncpy(line, rawLine, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';
  for (char* p = line; *p; ++p) *p = tolower(*p);
  char* s = line;
  while (*s == ' ' || *s == '\t') ++s;
  int e = strlen(s) - 1;
  while (e >= 0 && (s[e] == ' ' || s[e] == '\t')) s[e--] = '\0';
  if (*s == '\0') return;

  tcpClient.print(F("> "));
  tcpClient.println(s);

  if (!strcmp(s, "help")) {
    tcpClient.println(F("movement: forward backward left right stand rest stop"));
    tcpClient.println(F("tricks:   wave dance swim point pushup bow cute freaky worm shake shrug dead crab"));
    tcpClient.println(F("servo:    servo <id> <a>, nudge <id> <d>, all <a>, neutral, hips <a>, knees <a>, stance <h> <k>"));
    tcpClient.println(F("cal:      trim <id> <-45..45>, rev <id>, save, load, clear"));
    tcpClient.println(F("info:     map, pose, dump, wifi, gait <L> <R>, face <name>, help"));
    return;
  }
  if (!strcmp(s, "wifi")) {
    tcpClient.print(F("ap:  ")); tcpClient.println(WiFi.softAPIP());
    tcpClient.print(F("sta: "));
    tcpClient.println(networkConnected ? WiFi.localIP() : IPAddress(0, 0, 0, 0));
    tcpClient.print(F("host: ")); tcpClient.print(deviceHostname); tcpClient.print(F(".local:"));
    tcpClient.println(TCP_CMD_PORT);
    return;
  }
  if (!strcmp(s, "pose") || !strcmp(s, "status")) {
    for (int i = 0; i < 8; i++) {
      tcpClient.print(ServoNames[i]); tcpClient.print(F("="));
      tcpClient.print(gServoAngle[i]); tcpClient.print(i < 7 ? F(" ") : F("\n"));
    }
    return;
  }

  // Everything else is an action — instant stop reflex, then queue for Core 1.
  if (!strncmp(s, "stop", 4) || !strncmp(s, "halt", 4) || !strncmp(s, "freeze", 6)) {
    gStopRequested = true;
  }
  enqueueCommandLine(s);
  tcpClient.println(F("ok"));
}
