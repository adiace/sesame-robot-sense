/*
 * Sesame Robot Firmware — Distro Board V3 / ESP32-S3
 *
 * Based on dorianborian/sesame-robot. Changes from original:
 *
 * 1. SERVO DRIVER — ESP32Servo replaced with arduino-esp32 3.x ledcAttach/ledcWrite.
 *    On ESP32-S3, ESP32PWM::allocateTimer() shares LEDC channels between servo pairs
 *    (ch0↔ch2, ch1↔ch3), so writing R1 also drives L1. Pin-based ledcAttach gives
 *    each pin its own independent LEDC channel with no coupling.
 *
 * 2. PULSE RANGE — corrected to MG90S spec: 500µs (0°) → 2400µs (180°).
 *    Original used 732–2929µs (wider than the servo's physical travel), which caused
 *    servos to stall against their internal hard stop at high angles.
 *
 * 3. SERVO INIT — eager, staggered to 90° in setup() before WiFi starts.
 *    Original did lazy init on first command; servos floated (unpowered) during the
 *    10-second WiFi connection window and snapped to random positions on first use.
 *
 * 4. SUBTRIM PERSISTENCE — subtrims saved to NVS (Preferences) on each change and
 *    loaded at boot. Original required re-entering trims after every reboot.
 *
 * 5. OTA — ArduinoOTA added so the firmware can be flashed wirelessly. Password: "sesame".
 *
 * 6. BROWNOUT OVERRIDE — esp_brownout_init() stubbed out to prevent BOD resets caused
 *    by voltage sag when multiple servos draw current simultaneously.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "wifi_log.h"         // dlog() → USB serial + TCP port 8890 (include first)
#include "face-bitmaps.h"
#include "movement-sequences.h"
#include "captive-portal.h"
#include "audio_handler.h"    // I2S duplex: speaker + INMP441 mic
#include "wakeword_handler.h" // ESP-SR WakeNet: "Hi ESP" on-device detection
#include "voice_handler.h"    // TCP client → companion app on laptop

// ── WiFi credentials ──────────────────────────────────────────────────────────

// AP the robot always creates (for direct connection via captive portal)
#define AP_SSID  "Sesame-Controller"
#define AP_PASS  "12345678"          // must be ≥ 8 chars

// Optional: home/office network. Credentials live in the gitignored
// wifi_credentials.h — copy wifi_credentials.h.example to create it.
#include "wifi_credentials.h"
#define ENABLE_NETWORK_MODE true

// ── Hardware ──────────────────────────────────────────────────────────────────

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_I2C_ADDR  0x3C

// I2C pins — Distro Board V3
#define I2C_SDA 8
#define I2C_SCL 9
// I2C pins — Distro Board V1:  SDA=21, SCL=22
// I2C pins — S2 Mini:          SDA=33, SCL=35

// Servo GPIO pins — Distro Board V3
// Channel order: R1, R2, L1, L2, R4, R3, L3, L4
const int servoPins[8] = {4, 5, 6, 7, 10, 11, 12, 13};
// Distro Board V2 (legacy): {4, 5, 6, 7, 15, 16, 17, 18}
// Distro Board V1 (legacy): {15, 2, 23, 19, 4, 16, 17, 18}
// Lolin S2 Mini:            {1, 2, 4, 6, 8, 10, 13, 14}

// MG90S pulse range: 500µs = 0°, 2400µs = 180°.
// The original sesame code used 732–2929µs (too wide), which caused servos to stall
// against their internal hard stop when commanded past ~120°.
#define SERVO_MIN_US 500
#define SERVO_MAX_US 2400

// ── Servo state ───────────────────────────────────────────────────────────────

Preferences prefs;

// Per-servo reversal flags. movement-sequences.h already uses mirrored angles for
// bilateral geometry (e.g. L1=45 mirrors R1=135), so these should all be false
// unless a specific servo horn is physically on the wrong shaft side.
bool servoRev[8]          = {false, false, false, false, false, false, false, false};
//                            R1     R2     L1     L2     R4     R3     L3     L4

// Software trim offsets (degrees). Loaded from NVS at boot; updated via /subtrim.
int8_t servoSubtrim[8]    = {0, 0, 0, 0, 0, 0, 0, 0};

int servoCurrentAngle[8]  = {90, 90, 90, 90, 90, 90, 90, 90};
bool servosAttached        = false;

// ── Voice state ───────────────────────────────────────────────────────────────

static float        _voiceAvg    = 500.0f;
static unsigned long _voiceStart = 0;      // set in setup(); mute for 3s after boot
static uint8_t*     _voicePcmBuf = nullptr;
static size_t       _voicePcmMax = 0;

// ── Motion / animation tunables ───────────────────────────────────────────────

int frameDelay       = 100;  // ms between gait frames
int walkCycles       = 10;   // repetitions per walk command
int motorCurrentDelay = 20;  // ms between sequential servo writes (original: 50ms)

// One-shot step count for the next movement command ("walk 5" → 5 gait cycles
// then stop). 0 = continuous (walk until "stop"), the default behavior.
int gStepLimit = 0;

// TCP (voice) commands are bounded: while a command runs the wake-word
// listener is off, so an unbounded "dance" made the robot deaf until someone
// clicked stop in the GUI. Tricks run once; gaits get a default step cap.
// The captive portal's press-and-hold behavior (HTTP) stays continuous.
bool gOneShotPose = false;

// ── OLED / face state ─────────────────────────────────────────────────────────

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

String currentCommand   = "";
String currentFaceName  = "default";
const unsigned char* const* currentFaceFrames = nullptr;
uint8_t currentFaceFrameCount  = 0;
uint8_t currentFaceFrameIndex  = 0;
unsigned long lastFaceFrameMs  = 0;
int     faceFps                = 8;
FaceAnimMode currentFaceMode   = FACE_ANIM_LOOP;
int8_t  faceFrameDirection     = 1;
bool    faceAnimFinished       = false;
int     currentFaceFps         = 0;
bool    idleActive             = false;
bool    idleBlinkActive        = false;
unsigned long nextIdleBlinkMs  = 0;
uint8_t idleBlinkRepeatsLeft   = 0;

// ── Network state ─────────────────────────────────────────────────────────────

DNSServer dnsServer;
const byte DNS_PORT = 53;
WebServer server(80);

// TCP command server — persistent line-protocol connection used by the
// companion app (~5ms/command vs ~200ms for HTTP). One client at a time.
#define TCP_CMD_PORT 8888
WiFiServer tcpServer(TCP_CMD_PORT);
WiFiClient tcpClient;

bool      networkConnected = false;
IPAddress networkIP;
String    deviceHostname   = "sesame-robot";

// WiFi info scroll (shown on OLED before first user input)
unsigned long lastInputTime      = 0;
bool          firstInputReceived = false;
bool          showingWifiInfo    = false;
int           wifiScrollPos      = 0;
unsigned long lastWifiScrollMs   = 0;
String        wifiInfoText       = "";

// ── Face table ────────────────────────────────────────────────────────────────

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

struct FaceFpsEntry { const char* name; uint8_t fps; };
const FaceFpsEntry faceFpsEntries[] = {
  { "walk", 1 }, { "rest", 1 }, { "swim", 1 }, { "dance", 1 }, { "wave", 1 },
  { "point", 5 }, { "stand", 1 }, { "cute", 1 }, { "pushup", 1 }, { "freaky", 1 },
  { "bow", 1 }, { "worm", 1 }, { "shake", 1 }, { "shrug", 1 }, { "dead", 2 },
  { "crab", 1 }, { "idle", 1 }, { "idle_blink", 7 }, { "default", 1 },
  { "happy", 1 }, { "talk_happy", 1 }, { "sad", 1 }, { "talk_sad", 1 },
  { "angry", 1 }, { "talk_angry", 1 }, { "surprised", 1 }, { "talk_surprised", 1 },
  { "sleepy", 1 }, { "talk_sleepy", 1 }, { "love", 1 }, { "talk_love", 1 },
  { "excited", 1 }, { "talk_excited", 1 }, { "confused", 1 }, { "talk_confused", 1 },
  { "thinking", 1 }, { "talk_thinking", 1 },
};

// ── Forward declarations ──────────────────────────────────────────────────────

static void attachServos();
void detachServos();
static inline void _servoWriteRaw(uint8_t channel, int angle);
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
int  getFaceFpsForName(const String& faceName);
bool pressingCheck(String cmd, int ms);
void handleSubtrim();
void handleGetSettings();
void handleSetSettings();
void handleGetStatus();
void handleApiCommand();
void updateWifiInfoScroll();
void recordInput();
void serviceTcpCommands();

// ── HTTP handlers ─────────────────────────────────────────────────────────────

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleCommandWeb() {
  // Send 200 OK immediately — commands are long-running animations so we don't
  // wait for them to finish before responding to the browser.
  if (server.hasArg("pose")) {
    currentCommand = server.arg("pose");
    recordInput();
    exitIdle();
    server.send(200, "text/plain", "OK");
  } else if (server.hasArg("go")) {
    currentCommand = server.arg("go");
    recordInput();
    exitIdle();
    server.send(200, "text/plain", "OK");
  } else if (server.hasArg("stop")) {
    currentCommand = "";
    recordInput();
    server.send(200, "text/plain", "OK");
  } else if (server.hasArg("motor") && server.hasArg("value")) {
    int motorNum = server.arg("motor").toInt();
    int servoIdx = servoNameToIndex(server.arg("motor"));
    int angle    = server.arg("value").toInt();
    if (motorNum >= 1 && motorNum <= 8 && angle >= 0 && angle <= 180) {
      // Respond before moving — setServoAngle calls delayWithFace which calls
      // handleClient re-entrantly; sending first avoids a double-response.
      server.send(200, "text/plain", "OK");
      recordInput();
      setServoAngle(motorNum - 1, angle);
    } else if (servoIdx != -1 && angle >= 0 && angle <= 180) {
      server.send(200, "text/plain", "OK");
      recordInput();
      setServoAngle(servoIdx, angle);
    } else {
      server.send(400, "text/plain", "Invalid motor or angle");
    }
  } else {
    server.send(400, "text/plain", "Bad Args");
  }
}

void handleSubtrim() {
  // Set:  GET /subtrim?motor=N&value=V  (N: 0–7, V: −90 to +90)
  // Get:  GET /subtrim  → {"trims":[...8 values...]}
  if (server.hasArg("motor") && server.hasArg("value")) {
    int m = server.arg("motor").toInt();
    int v = server.arg("value").toInt();
    if (m >= 0 && m < 8 && v >= -90 && v <= 90) {
      servoSubtrim[m] = (int8_t)v;
      // Immediately re-apply so the user sees the servo move while trimming
      if (servosAttached) {
        int adjusted = constrain(servoCurrentAngle[m] + servoSubtrim[m], 0, 180);
        _servoWriteRaw(m, adjusted);
      }
      // Persist so trims survive reboot
      prefs.begin("sesame", false);
      char key[6]; snprintf(key, sizeof(key), "st%d", m);
      prefs.putChar(key, (int8_t)v);
      prefs.end();
    }
  }
  String json = "{\"trims\":[";
  for (int i = 0; i < 8; i++) {
    json += String(servoSubtrim[i]);
    if (i < 7) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleGetSettings() {
  String json = "{";
  json += "\"frameDelay\":"  + String(frameDelay)        + ",";
  json += "\"walkCycles\":"  + String(walkCycles)         + ",";
  json += "\"motorDelay\":"  + String(motorCurrentDelay)  + ",";
  json += "\"faceFps\":"     + String(faceFps);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetSettings() {
  if (server.hasArg("frameDelay")) frameDelay       = constrain(server.arg("frameDelay").toInt(), 1, 1000);
  if (server.hasArg("walkCycles")) walkCycles        = constrain(server.arg("walkCycles").toInt(), 1, 50);
  if (server.hasArg("motorDelay")) motorCurrentDelay = constrain(server.arg("motorDelay").toInt(), 5, 100);
  if (server.hasArg("faceFps"))    faceFps           = (int)max(1L, server.arg("faceFps").toInt());
  // Persist so settings survive reboot (loaded in setup() next to subtrims)
  prefs.begin("sesame", false);
  prefs.putInt("frameDelay", frameDelay);
  prefs.putInt("walkCycles", walkCycles);
  prefs.putInt("motorDelay", motorCurrentDelay);
  prefs.putInt("faceFps",    faceFps);
  prefs.end();
  server.send(200, "text/plain", "OK");
}

void handleGetStatus() {
  String json = "{";
  json += "\"currentCommand\":\""  + currentCommand + "\",";
  json += "\"currentFace\":\""     + currentFaceName + "\",";
  json += "\"networkConnected\":"  + String(networkConnected ? "true" : "false") + ",";
  json += "\"apIP\":\""            + WiFi.softAPIP().toString() + "\",";
  json += "\"servosAttached\":"    + String(servosAttached ? "true" : "false") + ",";
  // Voice diagnostics — checkable without USB: curl http://sesame-robot.local/api/status
  json += "\"wakeReady\":"         + String(gWakeReady ? "true" : "false") + ",";
  json += "\"wakeChunksFed\":"     + String(gWakeChunksFed) + ",";
  json += "\"micRms\":"            + String(gWakeMicRms, 0) + ",";
  json += "\"wakeDetections\":"    + String(gWakeDetections) + ",";
  json += "\"psramBytes\":"        + String(ESP.getPsramSize()) + ",";
  json += "\"voiceBufSecs\":"      + String(_voicePcmMax / 32000.0f, 1);
  if (networkConnected) json += ",\"networkIP\":\"" + networkIP.toString() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }

  String body = server.arg("plain");
  Serial.println("API Command: " + body);

  // Detect face-only payload (has "face" key but no "command" key)
  int faceStart = body.indexOf("\"face\":\"");
  if (faceStart == -1) faceStart = body.indexOf("\"face\": \"");
  bool faceOnly = (faceStart > 0 &&
                   body.indexOf("\"command\":") == -1 &&
                   body.indexOf("\"command\": ") == -1);

  String command = "", face = "";

  if (faceStart > 0) {
    int vs = body.indexOf("\"", faceStart + 6) + 1;
    int ve = body.indexOf("\"", vs);
    if (ve > vs) face = body.substring(vs, ve);
  }

  if (!faceOnly) {
    int cs = body.indexOf("\"command\":\"");
    if (cs == -1) cs = body.indexOf("\"command\": \"");
    if (cs == -1) {
      server.send(400, "application/json", "{\"error\":\"Missing command field\"}");
      return;
    }
    cs = body.indexOf("\"", cs + 10) + 1;
    int ce = body.indexOf("\"", cs);
    if (ce <= cs) {
      server.send(400, "application/json", "{\"error\":\"Invalid command format\"}");
      return;
    }
    command = body.substring(cs, ce);
  }

  if (face.length() > 0) setFace(face);

  if (faceOnly) {
    recordInput();
    server.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Face updated\"}");
    return;
  }

  if (command == "stop") {
    currentCommand = "";
  } else {
    currentCommand = command;
    exitIdle();
  }
  recordInput();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// ── TCP command server ────────────────────────────────────────────────────────
// Newline-terminated lines from the companion app (SesameRobotController):
//   <pose>        e.g. "wave", "forward", "stop" — same names as HTTP /cmd
//   face <name>   set OLED face
//   vision start  acknowledged but ignored (no camera on this robot)
// Called from loop() and delayWithFace() so "stop" interrupts a running gait.

static bool _isKnownPose(const char* s) {
  static const char* poses[] = {
    "forward", "backward", "left", "right", "rest", "stand",
    "wave", "dance", "swim", "point", "pushup", "bow", "cute",
    "freaky", "worm", "shake", "shrug", "dead", "crab", "box",
    "sleep", "wake"
  };
  for (auto p : poses) if (!strcmp(s, p)) return true;
  return false;
}

void serviceTcpCommands() {
  if (!tcpClient || !tcpClient.connected()) {
    WiFiClient incoming = tcpServer.available();
    if (incoming) {
      tcpClient = incoming;
      tcpClient.setNoDelay(true);
      tcpClient.println(F("sesame-robot-voice connected"));
    }
    if (!tcpClient || !tcpClient.connected()) return;
  }

  static char buf[48];
  static uint8_t len = 0;
  while (tcpClient.available() > 0) {
    char ch = (char)tcpClient.read();
    if (ch != '\r' && ch != '\n') {
      if (len < sizeof(buf) - 1) buf[len++] = ch;
      continue;
    }
    if (len == 0) continue;
    buf[len] = '\0';
    len = 0;
    for (char* p = buf; *p; ++p) *p = tolower(*p);

    // Optional step count on movement commands: "walk 5", "left 2" →
    // gStepLimit bounds the next gait to N cycles (loop() clears it after).
    char verb[24];
    int steps = 0;
    if (sscanf(buf, "%23s %d", verb, &steps) == 2 && steps > 0) {
      strcpy(buf, verb);
      gStepLimit = min(steps, 50);
    }

    // Companion-app vocabulary → firmware pose names
    if (!strcmp(buf, "walk")) strcpy(buf, "forward");
    if (!strcmp(buf, "back")) strcpy(buf, "backward");

    // Bound TCP commands so the robot returns to wake-word listening on its
    // own: gaits without an explicit count get a default cap, tricks run once.
    bool isGaitVerb = !strcmp(buf, "forward") || !strcmp(buf, "backward") ||
                      !strcmp(buf, "left")    || !strcmp(buf, "right");
    if (isGaitVerb && gStepLimit == 0)
      gStepLimit = (!strcmp(buf, "left") || !strcmp(buf, "right")) ? 4 : 8;
    if (!isGaitVerb && _isKnownPose(buf))
      gOneShotPose = true;

    recordInput();
    if (!strcmp(buf, "stop") || !strcmp(buf, "halt")) {
      currentCommand = "";
    } else if (!strncmp(buf, "face ", 5)) {
      setFace(String(buf + 5));
    } else if (!strncmp(buf, "vision", 6)) {
      tcpClient.println(F("no camera"));
    } else if (_isKnownPose(buf)) {
      currentCommand = String(buf);
      exitIdle();
    } else {
      tcpClient.print(F("unknown: "));
      tcpClient.println(buf);
    }
  }
}

// ── Brownout override ─────────────────────────────────────────────────────────
// Servo inrush on battery/USB causes voltage sag that triggers the ESP32-S3 BOD
// at ~262ms. Stubbing out the IDF init prevents it from ever being armed.
extern "C" void esp_brownout_init(void) {}

// ── setup() ───────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  wifiLogSetup();  // hook vprintf so all Serial output also goes to TCP port 8890
  randomSeed(micros());

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("SSD1306 init failed"));
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Setting up WiFi..."));
  display.display();

  // Audio: I2S duplex (speaker + mic). Boot beep confirms speaker alive.
  if (audioSetup()) {
    playBeep(880, 100); delay(60); playBeep(1200, 100);
    Serial.println(F("Audio: I2S ready"));
  } else {
    Serial.println(F("Audio: I2S FAILED — voice disabled"));
  }
  // WakeNet: load "Hi ESP" model from model partition
  wakewordSetup();
  // Allocate PCM recording buffer — try PSRAM (4s) then internal RAM (2s)
  _voicePcmBuf = (uint8_t*)ps_malloc(AUDIO_SAMPLE_RATE * 2 * 4);
  if (_voicePcmBuf) {
      _voicePcmMax = AUDIO_SAMPLE_RATE * 2 * 4;
      Serial.println(F("Voice: 4s PSRAM buffer"));
  } else {
      _voicePcmBuf = (uint8_t*)malloc(AUDIO_SAMPLE_RATE * 2 * 2);
      if (_voicePcmBuf) { _voicePcmMax = AUDIO_SAMPLE_RATE * 2 * 2; Serial.println(F("Voice: 2s RAM buffer")); }
      else               { Serial.println(F("Voice: buffer alloc failed")); }
  }

  // Load saved subtrims from NVS (written by handleSubtrim on each UI change)
  prefs.begin("sesame", true);
  for (int i = 0; i < 8; i++) {
    char key[6]; snprintf(key, sizeof(key), "st%d", i);
    servoSubtrim[i] = prefs.getChar(key, 0);
  }
  frameDelay        = prefs.getInt("frameDelay", frameDelay);
  walkCycles        = prefs.getInt("walkCycles", walkCycles);
  motorCurrentDelay = prefs.getInt("motorDelay", motorCurrentDelay);
  faceFps           = prefs.getInt("faceFps",    faceFps);
  prefs.end();

  // Initialise all 8 servos to the STAND pose before WiFi starts so they're
  // controlled (not floating) during the connection window. Staggered 100ms to
  // limit inrush current. Trims are applied (loaded from NVS above) — all-90°
  // is the rest pose, which made the robot slump on every boot.
  // Angles match runStandPose(): R1,R2,L1,L2,R4,R3,L3,L4
  const int bootPose[8] = {135, 45, 45, 135, 0, 180, 0, 180};
  for (int i = 0; i < 8; i++) {
    ledcAttach(servoPins[i], 50, 14);
    servoCurrentAngle[i] = bootPose[i];
    _servoWriteRaw(i, constrain(bootPose[i] + servoSubtrim[i], 0, 180));
    delay(100);
  }
  servosAttached = true;

  // WiFi: try STA first, fall back to AP-only
  if (ENABLE_NETWORK_MODE && String(NETWORK_SSID).length() > 0) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setHostname(deviceHostname.c_str());
    WiFi.begin(NETWORK_SSID, NETWORK_PASS);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500); Serial.print("."); attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);  // OPI PSRAM + WiFi sleep = TX silently drops packets
      networkConnected = true;
      networkIP = WiFi.localIP();
      Serial.println("\nNetwork IP: " + networkIP.toString());
    } else {
      Serial.println("\nSTA failed — AP only");
      WiFi.mode(WIFI_AP);
    }
  } else {
    WiFi.mode(WIFI_AP);
  }

  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress apIP = WiFi.softAPIP();
  Serial.println("AP IP: " + apIP.toString());

  if (networkConnected) {
    wifiInfoText = "AP: " + String(AP_SSID) + " (" + apIP.toString() + ")  |  "
                 + "Network: " + String(NETWORK_SSID) + " (" + networkIP.toString()
                 + ") or " + deviceHostname + ".local  |  ";
  } else {
    wifiInfoText = "WiFi: " + String(AP_SSID) + "  Pass: " + String(AP_PASS)
                 + "  IP: " + apIP.toString() + "  |  ";
  }

  lastInputTime        = millis();
  firstInputReceived   = false;
  showingWifiInfo      = false;

  if (MDNS.begin(deviceHostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://" + deviceHostname + ".local");
  }

  dnsServer.start(DNS_PORT, "*", apIP);

  server.on("/",            handleRoot);
  server.on("/cmd",         handleCommandWeb);
  server.on("/getSettings", handleGetSettings);
  server.on("/setSettings", handleSetSettings);
  server.on("/api/status",  handleGetStatus);
  server.on("/api/command", handleApiCommand);
  server.on("/subtrim",     handleSubtrim);
  server.onNotFound(handleRoot);
  server.begin();
  tcpServer.begin();
  tcpServer.setNoDelay(true);
  wifiLogServerBegin();

  ArduinoOTA.setHostname(deviceHostname.c_str());
  ArduinoOTA.setPassword("sesame");
  ArduinoOTA.begin();
  Serial.println(F("OTA ready (password: sesame)"));

  _voiceStart = millis();

  enterIdle();   // standing + idle face (boot used to slump into rest)
  Serial.println(F("Ready."));
}

// ── loop() ────────────────────────────────────────────────────────────────────

void loop() {
  ArduinoOTA.handle();
  dnsServer.processNextRequest();
  server.handleClient();
  serviceTcpCommands();
  wifiLogServerHandle();
  updateAnimatedFace();
  updateIdleBlink();
  updateWifiInfoScroll();

  if (currentCommand != "") {
    String cmd = currentCommand;
    bool isGait = (cmd == "forward" || cmd == "backward" ||
                   cmd == "left"    || cmd == "right");
    // Bounded move: "walk 5" set gStepLimit — run exactly that many gait
    // cycles once, then stop, instead of looping until "stop" arrives.
    int savedCycles = walkCycles;
    if (isGait && gStepLimit > 0) walkCycles = gStepLimit;

    if      (cmd == "forward")  runWalkPose();
    else if (cmd == "backward") runWalkBackward();
    else if (cmd == "left")     runTurnLeft();
    else if (cmd == "right")    runTurnRight();
    else if (cmd == "rest")     { runRestPose();   if (currentCommand == "rest")  currentCommand = ""; }
    else if (cmd == "stand")    { runStandPose(1); if (currentCommand == "stand") currentCommand = ""; }
    else if (cmd == "wave")     runWavePose();
    else if (cmd == "dance")    runDancePose();
    else if (cmd == "swim")     runSwimPose();
    else if (cmd == "point")    runPointPose();
    else if (cmd == "pushup")   runPushupPose();
    else if (cmd == "bow")      runBowPose();
    else if (cmd == "cute")     runCutePose();
    else if (cmd == "freaky")   runFreakyPose();
    else if (cmd == "worm")     runWormPose();
    else if (cmd == "shake")    runShakePose();
    else if (cmd == "shrug")    runShrugPose();
    else if (cmd == "dead")     runDeadPose();
    else if (cmd == "crab")     runCrabPose();
    else if (cmd == "box")      runBoxPose();
    // sleep/wake — motor protection, mirrors sesame-robot-sense behavior:
    // sleep = rest pose then detach (servos unpowered, no wear); wake = stand.
    else if (cmd == "sleep") {
      runRestPose();
      delayWithFace(1000);   // let loaded servos settle at 90° before power cut
      detachServos();
      exitIdle();            // stop idle blink from overriding the sleepy face
      setFace("sleepy");
      if (currentCommand == "sleep") currentCommand = "";
    }
    else if (cmd == "wake") {
      runStandPose(1);       // setServoAngle re-attaches automatically
      setFace("happy");
      if (currentCommand == "wake") currentCommand = "";
    }
    else currentCommand = "";  // unknown command — drop it instead of spinning

    if (isGait && gStepLimit > 0) {
      walkCycles = savedCycles;
      gStepLimit = 0;
      if (currentCommand == cmd) currentCommand = "";  // bounded move done
      runStandPose(1);
    }
    if (gOneShotPose) {
      gOneShotPose = false;
      if (currentCommand == cmd) currentCommand = "";  // trick ran once — done
    }
  }

  // ── Voice wake detection — WakeNet "Hi ESP" ──────────────────────────────────
  if (_voicePcmBuf && currentCommand == "" && millis() - _voiceStart > 3000) {
    // Feed WakeNet (16-bit mono, left channel). Drain the whole DMA backlog each
    // pass: OLED face redraws block the loop for ~25ms and a single small read
    // couldn't keep up — the ring overflowed and WakeNet heard gaps mid-phrase.
    static int16_t _wkBuf[256];
    static int16_t _stereo[512];
    bool wakeHit = false;
    for (int rd = 0; rd < 10 && !wakeHit; rd++) {
      size_t got = 0;
      i2s_channel_read(_aud_rx, _stereo, sizeof(_stereo), &got,
                       pdMS_TO_TICKS(rd == 0 ? 20 : 0));
      int pairs = got / 4;
      if (pairs == 0) break;
      int64_t sq = 0;
      for (int i = 0; i < pairs; i++) {
        _wkBuf[i] = micApplyGain(_stereo[i * 2]);  // left channel
        sq += (int64_t)_wkBuf[i] * _wkBuf[i];
      }
      gWakeMicRms = sqrtf((float)(sq / pairs));   // live level for /api/status
      // Slow EMA of ambient level. Loud transients (speech, taps) adapt 100×
      // slower rather than being skipped outright — a hard skip deadlocked the
      // estimate when the mic got physically resealed and true ambient jumped
      // >3× (every chunk read as "transient", floor stayed stale-low, silence
      // never registered, recordings ran to the 4s cap).
      float a = (gWakeMicRms < gAmbientRms * 3.0f) ? 0.05f : 0.0005f;
      gAmbientRms += a * (gWakeMicRms - gAmbientRms);
      wakeHit = wakewordFeed(_wkBuf, pairs);
      if (got < sizeof(_stereo)) break;   // backlog drained
    }

    if (wakeHit) {
      setFace("excited");
      // Wake acknowledgment beep. Balance: full-volume 880Hz rang the enclosure,
      // armed the VAD and got transcribed as "2."; amplitude 3000 was inaudible
      // outside the body. Fade edges (in playBeep) stop the enclosure ringing.
      playBeep(1500, 70, 9000);
      delay(120);   // beep decay before micRecord's flush + noise calibration

      size_t pcmLen = micRecord(_voicePcmBuf, _voicePcmMax);
      if (pcmLen > 0) {
        setFace("thinking");
        bool ok = voiceStreamToServer(_voicePcmBuf, pcmLen);
        if (!ok) Serial.println("[Voice] stream failed");
      }

      setFace("rest");
      enterIdle();
      _voiceStart = millis();  // 3s cooldown
    }
  }

  // Serial CLI — useful for diagnosing servo wiring and trim calibration
  if (Serial.available()) {
    static char buf[32];
    static byte pos = 0;
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (pos > 0) {
        buf[pos] = '\0';
        int motorNum, angle;
        recordInput();
        if      (!strcmp(buf, "run walk") || !strcmp(buf, "rn wf")) { currentCommand = "forward";  runWalkPose();     currentCommand = ""; }
        else if (!strcmp(buf, "rn wb"))                              { currentCommand = "backward"; runWalkBackward(); currentCommand = ""; }
        else if (!strcmp(buf, "rn tl"))                              { currentCommand = "left";     runTurnLeft();     currentCommand = ""; }
        else if (!strcmp(buf, "rn tr"))                              { currentCommand = "right";    runTurnRight();    currentCommand = ""; }
        else if (!strcmp(buf, "run rest") || !strcmp(buf, "rn rs"))  runRestPose();
        else if (!strcmp(buf, "run stand") || !strcmp(buf, "rn st")) runStandPose(1);
        else if (!strcmp(buf, "rn wv")) { currentCommand = "wave";   runWavePose();   }
        else if (!strcmp(buf, "rn dn")) { currentCommand = "dance";  runDancePose();  }
        else if (!strcmp(buf, "rn sw")) { currentCommand = "swim";   runSwimPose();   }
        else if (!strcmp(buf, "rn pt")) { currentCommand = "point";  runPointPose();  }
        else if (!strcmp(buf, "rn pu")) { currentCommand = "pushup"; runPushupPose(); }
        else if (!strcmp(buf, "rn bw")) { currentCommand = "bow";    runBowPose();    }
        else if (!strcmp(buf, "rn ct")) { currentCommand = "cute";   runCutePose();   }
        else if (!strcmp(buf, "rn fk")) { currentCommand = "freaky"; runFreakyPose(); }
        else if (!strcmp(buf, "rn wm")) { currentCommand = "worm";   runWormPose();   }
        else if (!strcmp(buf, "rn sk")) { currentCommand = "shake";  runShakePose();  }
        else if (!strcmp(buf, "rn sg")) { currentCommand = "shrug";  runShrugPose();  }
        else if (!strcmp(buf, "rn dd")) { currentCommand = "dead";   runDeadPose();   }
        else if (!strcmp(buf, "rn cb")) { currentCommand = "crab";   runCrabPose();   }
        else if (!strcmp(buf, "subtrim") || !strcmp(buf, "st")) {
          for (int i = 0; i < 8; i++) {
            Serial.print("Motor "); Serial.print(i); Serial.print(": ");
            if (servoSubtrim[i] >= 0) Serial.print("+");
            Serial.println(servoSubtrim[i]);
          }
        }
        else if (!strcmp(buf, "subtrim save") || !strcmp(buf, "st save")) {
          Serial.print("int8_t servoSubtrim[8] = {");
          for (int i = 0; i < 8; i++) { Serial.print(servoSubtrim[i]); if (i < 7) Serial.print(", "); }
          Serial.println("};");
        }
        else if (!strncmp(buf, "subtrim reset", 13) || !strncmp(buf, "st reset", 8)) {
          for (int i = 0; i < 8; i++) servoSubtrim[i] = 0;
          Serial.println("Subtrims reset to 0");
        }
        else if (!strncmp(buf, "subtrim ", 8) || !strncmp(buf, "st ", 3)) {
          const char* p = (buf[1] == 't') ? buf + 3 : buf + 8;
          int m, v;
          if (sscanf(p, "%d %d", &m, &v) == 2 && m >= 0 && m < 8 && v >= -90 && v <= 90) {
            servoSubtrim[m] = v;
            Serial.print("Motor "); Serial.print(m); Serial.print(" subtrim = ");
            if (v >= 0) Serial.print("+");
            Serial.println(v);
          }
        }
        else if (!strncmp(buf, "all ", 4)) {
          if (sscanf(buf + 4, "%d", &angle) == 1)
            for (int i = 0; i < 8; i++) setServoAngle(i, angle);
        }
        else if (sscanf(buf, "%d %d", &motorNum, &angle) == 2 && motorNum >= 0 && motorNum < 8) {
          setServoAngle(motorNum, angle);
        }
        pos = 0;
      }
    } else if (pos < sizeof(buf) - 1) {
      buf[pos++] = c;
    }
  }
}

// ── Servo helpers ─────────────────────────────────────────────────────────────

// Guard so movement-sequences.h calls to attachServos() compile without effect.
// All servo init happens eagerly in setup().
static void attachServos() { if (servosAttached) return; }

void detachServos() {
  if (!servosAttached) return;
  for (int i = 0; i < 8; i++) { ledcWrite(servoPins[i], 0); ledcDetach(servoPins[i]); }
  servosAttached = false;
}

// Writes the PWM pulse for one channel. Each pin has its own LEDC channel via
// pin-based ledcAttach (arduino-esp32 3.x), so servos are fully independent.
static inline void _servoWriteRaw(uint8_t channel, int angle) {
  int physical = constrain(servoRev[channel] ? (180 - angle) : angle, 0, 180);
  uint32_t pulseUs = map(physical, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  uint32_t duty = (uint32_t)((float)pulseUs / 20000.0f * 16384.0f);
  ledcWrite(servoPins[channel], duty);
}

void setServoAngle(uint8_t channel, int angle) {
  if (channel >= 8) return;
  attachServos();
  servoCurrentAngle[channel] = angle;
  int adjusted = constrain(angle + servoSubtrim[channel], 0, 180);
  _servoWriteRaw(channel, adjusted);
  delayWithFace(motorCurrentDelay);
}

// ── Face / OLED helpers ───────────────────────────────────────────────────────

void updateFaceBitmap(const unsigned char* bitmap) {
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, 128, 64, SSD1306_WHITE);
  display.display();
}

uint8_t countFrames(const unsigned char* const* frames, uint8_t maxFrames) {
  if (!frames || !frames[0]) return 0;
  uint8_t n = 0;
  while (n < maxFrames && frames[n]) n++;
  return n;
}

void setFace(const String& faceName) {
  if (faceName == currentFaceName && currentFaceFrames) return;
  currentFaceName        = faceName;
  currentFaceFrameIndex  = 0;
  lastFaceFrameMs        = 0;
  faceFrameDirection     = 1;
  faceAnimFinished       = false;
  currentFaceFps         = getFaceFpsForName(faceName);
  currentFaceFrames      = face_defualt_frames;
  currentFaceFrameCount  = countFrames(face_defualt_frames, MAX_FACE_FRAMES);
  for (size_t i = 0; i < sizeof(faceEntries) / sizeof(faceEntries[0]); i++) {
    if (faceName.equalsIgnoreCase(faceEntries[i].name)) {
      currentFaceFrames     = faceEntries[i].frames;
      currentFaceFrameCount = countFrames(faceEntries[i].frames, faceEntries[i].maxFrames);
      break;
    }
  }
  if (currentFaceFrameCount == 0) {
    currentFaceFrames     = face_defualt_frames;
    currentFaceFrameCount = countFrames(face_defualt_frames, MAX_FACE_FRAMES);
    currentFaceName       = "default";
    currentFaceFps        = getFaceFpsForName("default");
  }
  if (currentFaceFrameCount > 0 && currentFaceFrames[0])
    updateFaceBitmap(currentFaceFrames[0]);
}

void setFaceMode(FaceAnimMode mode) {
  currentFaceMode  = mode;
  faceFrameDirection = 1;
  faceAnimFinished   = false;
}

void setFaceWithMode(const String& faceName, FaceAnimMode mode) {
  setFaceMode(mode);
  setFace(faceName);
}

int getFaceFpsForName(const String& faceName) {
  for (size_t i = 0; i < sizeof(faceFpsEntries) / sizeof(faceFpsEntries[0]); i++)
    if (faceName.equalsIgnoreCase(faceFpsEntries[i].name)) return faceFpsEntries[i].fps;
  return faceFps;
}

void updateAnimatedFace() {
  if (!currentFaceFrames || currentFaceFrameCount <= 1) return;
  if (currentFaceMode == FACE_ANIM_ONCE && faceAnimFinished) return;
  unsigned long now = millis();
  int fps = max(1, currentFaceFps > 0 ? currentFaceFps : faceFps);
  if (now - lastFaceFrameMs < (unsigned long)(1000 / fps)) return;
  lastFaceFrameMs = now;
  if (currentFaceMode == FACE_ANIM_LOOP) {
    currentFaceFrameIndex = (currentFaceFrameIndex + 1) % currentFaceFrameCount;
  } else if (currentFaceMode == FACE_ANIM_ONCE) {
    if (currentFaceFrameIndex + 1 >= currentFaceFrameCount) { faceAnimFinished = true; }
    else currentFaceFrameIndex++;
  } else { // BOOMERANG
    if (faceFrameDirection > 0) {
      if (currentFaceFrameIndex + 1 >= currentFaceFrameCount) { faceFrameDirection = -1; if (currentFaceFrameIndex > 0) currentFaceFrameIndex--; }
      else currentFaceFrameIndex++;
    } else {
      if (currentFaceFrameIndex == 0) { faceFrameDirection = 1; if (currentFaceFrameCount > 1) currentFaceFrameIndex++; }
      else currentFaceFrameIndex--;
    }
  }
  updateFaceBitmap(currentFaceFrames[currentFaceFrameIndex]);
}

void delayWithFace(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    updateAnimatedFace();
    server.handleClient();
    serviceTcpCommands();
    dnsServer.processNextRequest();
    delay(5);
  }
}

void scheduleNextIdleBlink(unsigned long minMs, unsigned long maxMs) {
  nextIdleBlinkMs = millis() + (unsigned long)random(minMs, maxMs);
}

void enterIdle() {
  idleActive          = true;
  idleBlinkActive     = false;
  idleBlinkRepeatsLeft = 0;
  setFaceWithMode("idle", FACE_ANIM_BOOMERANG);
  scheduleNextIdleBlink(3000, 7000);
}

void exitIdle() {
  idleActive      = false;
  idleBlinkActive = false;
}

void updateIdleBlink() {
  if (!idleActive) return;
  if (!idleBlinkActive) {
    if (millis() >= nextIdleBlinkMs) {
      idleBlinkActive = true;
      if (idleBlinkRepeatsLeft == 0 && random(0, 100) < 30) idleBlinkRepeatsLeft = 1;
      setFaceWithMode("idle_blink", FACE_ANIM_ONCE);
    }
    return;
  }
  if (currentFaceMode == FACE_ANIM_ONCE && faceAnimFinished) {
    idleBlinkActive = false;
    setFaceWithMode("idle", FACE_ANIM_BOOMERANG);
    if (idleBlinkRepeatsLeft > 0) { idleBlinkRepeatsLeft--; scheduleNextIdleBlink(120, 220); }
    else scheduleNextIdleBlink(3000, 7000);
  }
}

// ── Gait helpers ──────────────────────────────────────────────────────────────

bool pressingCheck(String cmd, int ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    server.handleClient();
    dnsServer.processNextRequest();
    updateAnimatedFace();
    if (currentCommand != cmd) { runStandPose(1); return false; }
    yield();
  }
  return true;
}

// ── WiFi info scroll ──────────────────────────────────────────────────────────

void recordInput() {
  lastInputTime = millis();
  if (!firstInputReceived) { firstInputReceived = true; showingWifiInfo = false; }
}

void updateWifiInfoScroll() {
  if (firstInputReceived) {
    if (showingWifiInfo) {
      showingWifiInfo = false;
      if (currentFaceFrames && currentFaceFrameCount > 0)
        updateFaceBitmap(currentFaceFrames[currentFaceFrameIndex]);
    }
    return;
  }
  unsigned long now = millis();
  if (!showingWifiInfo && (now - lastInputTime >= 30000)) {
    showingWifiInfo = true; wifiScrollPos = 0; lastWifiScrollMs = now;
  }
  if (!showingWifiInfo) return;
  if (now - lastWifiScrollMs >= 150) {
    lastWifiScrollMs = now;
    display.clearDisplay();
    if (currentFaceFrames && currentFaceFrameCount > 0)
      display.drawBitmap(0, 0, currentFaceFrames[currentFaceFrameIndex], 128, 64, SSD1306_WHITE);
    display.fillRect(0, 0, 128, 10, SSD1306_BLACK);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);
    display.setCursor(-wifiScrollPos, 1);
    display.print(wifiInfoText);
    display.setTextWrap(true);
    display.display();
    wifiScrollPos += 2;
    if (wifiScrollPos >= (int)(wifiInfoText.length() * 6)) wifiScrollPos = 0;
  }
}
