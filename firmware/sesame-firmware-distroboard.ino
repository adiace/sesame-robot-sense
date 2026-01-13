#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Access Point Configuration ---
// This is the network the Robot will create
#define AP_SSID  "Sesame-Controller"
#define AP_PASS  "12345678" // Must be at least 8 characters

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_I2C_ADDR 0x3C

// I2C Pins for Distro Board
#define I2C_SDA 21
#define I2C_SCL 22

// DNS Server for Captive Portal
DNSServer dnsServer;
const byte DNS_PORT = 53;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);

// Global state for animations
String currentCommand = "";

// Servo Pins for Distro Board
// ======================================================================
// Pin numbers are coorisponding to the ESP32 GPIO pins and may differ based on which board you use.
// If you are using a different board, please adjust the servoPins array accordingly.
// ======================================================================
Servo servos[8];
// Sesame Distro Board Pinout
const int servoPins[8] = {15, 2, 23, 19, 4, 16, 17, 18};

// Animation constants
int frameDelay = 100;
int walkCycles = 10;
int motorCurrentDelay = 20; // ms delay between motor movements to prevent over-current

// ======================================================================
// --- FACE BITMAPS ---
// Bitmaps for the faces are stored here, can be generated with image2cpp
// ======================================================================
const unsigned char epd_bitmap_walk [] PROGMEM = { 0x00 }; 
const unsigned char epd_bitmap_rest [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_swim [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_dance [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_wave [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_point [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_stand [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_cute [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_pushup [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_freaky [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_bow [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_worm [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_shake [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_shrug [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_dead [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_crab [] PROGMEM = { 0x00 };
const unsigned char epd_bitmap_defualt [] PROGMEM = { 0x00 };

// ======================================================================
// --- WEB INTERFACE HTML ---
// ======================================================================

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Robot AP Controller</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { 
      font-family: 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; 
      text-align: center; 
      background-color: #121212; 
      color: #e0e0e0; 
      touch-action: manipulation; 
      margin: 0;
      padding: 20px;
    }
    h2 { 
      margin-top: 10px;
      margin-bottom: 20px; 
      color: #ffffff;
      font-size: 28px;
    }
    .grid { 
      display: grid; 
      grid-template-columns: repeat(2, 1fr); 
      gap: 15px; 
      max-width: 400px; 
      margin: 0 auto; 
      padding: 10px; 
    }
    .dpad-container { 
      display: flex; 
      justify-content: center; 
      margin: 20px 0; 
    }
    .dpad { 
      display: grid; 
      grid-template-columns: repeat(3, 90px); 
      grid-template-rows: repeat(2, 90px); 
      gap: 12px; 
    }
    .dpad button { 
      font-size: 35px; 
      background-color: #333333; 
      border: 2px solid #555555; 
      box-shadow: 0 4px #111111;
    }
    button { 
      background-color: #333333; 
      border: none; 
      color: #e0e0e0; 
      padding: 15px; 
      font-size: 18px; 
      border-radius: 12px; 
      cursor: pointer; 
      user-select: none; 
      box-shadow: 0 4px #111111;
      transition: all 0.1s;
    }
    button:active { 
      box-shadow: 0 2px #111111;
      transform: translateY(2px); 
    }
    .btn-pose { 
      background-color: #2c3e50; 
      box-shadow: 0 4px #1a252f; 
    }
    .btn-pose:active { 
      background-color: #34495e; 
      box-shadow: 0 2px #1a252f; 
    }
    .btn-stop-all { 
      background-color: #d00000; 
      width: 95%; 
      max-width: 400px; 
      margin: 30px auto; 
      display: block; 
      font-weight: bold; 
      font-size: 24px; 
      padding: 20px; 
      border-radius: 15px; 
      box-shadow: 0 6px #800000;
      border: 2px solid #ff4d4d;
      color: white;
    }
    .btn-stop-all:active { 
      box-shadow: 0 2px #800000; 
      transform: translateY(4px); 
    }
    .btn-settings { 
      background-color: #444444; 
      box-shadow: 0 4px #222222; 
      margin-top: 10px;
      padding: 10px 25px;
    }
    .spacer { visibility: hidden; }
    .settings-panel { 
      display: none; 
      position: fixed; 
      top: 0; 
      left: 0; 
      width: 100%; 
      height: 100%; 
      background: rgba(0,0,0,0.85); 
      z-index: 100; 
      backdrop-filter: blur(5px); 
    }
    .settings-content { 
      background: #1e1e1e; 
      border: 1px solid #444444; 
      max-width: 300px; 
      margin: 50px auto; 
      padding: 25px; 
      border-radius: 20px; 
      text-align: left; 
      box-shadow: 0 10px 30px rgba(0,0,0,0.5); 
    }
    .settings-content h3 { color: #ffffff; margin-top: 0; }
    .settings-content label { display: block; margin-top: 15px; font-weight: bold; color: #cccccc; }
    .settings-content input { 
      width: 100%; 
      padding: 10px; 
      margin-top: 5px; 
      background: #333333; 
      color: #fff; 
      border: 1px solid #555555; 
      border-radius: 8px; 
      box-sizing: border-box;
    }
    .btn-save { background-color: #27ae60; box-shadow: 0 4px #1e8449; width: 100%; margin-top: 25px; color: white; }
    .btn-close { background-color: #c0392b; box-shadow: 0 4px #922b21; width: 100%; margin-top: 12px; color: white; }
  </style>
</head>
<body>
  <h2>Sesame Controller</h2>
  
  <div class="dpad-container">
    <div class="dpad">
      <div class="spacer"></div>
      <button onmousedown="move('forward')" onmouseup="stop()" ontouchstart="move('forward')" ontouchend="stop()">&#9650;</button>
      <div class="spacer"></div>
      
      <button onmousedown="move('left')" onmouseup="stop()" ontouchstart="move('left')" ontouchend="stop()">&#9664;</button>
      <button onmousedown="move('backward')" onmouseup="stop()" ontouchstart="move('backward')" ontouchend="stop()">&#9660;</button>
      <button onmousedown="move('right')" onmouseup="stop()" ontouchstart="move('right')" ontouchend="stop()">&#9654;</button>
    </div>
  </div>

  <div class="grid">
    <button class="btn-pose" onclick="pose('rest')">Rest</button>
    <button class="btn-pose" onclick="pose('stand')">Stand</button>
    <button class="btn-pose" onclick="pose('wave')">Wave</button>
    <button class="btn-pose" onclick="pose('dance')">Dance</button>
    <button class="btn-pose" onclick="pose('swim')">Swim</button>
    <button class="btn-pose" onclick="pose('point')">Point</button>
    <button class="btn-pose" onclick="pose('pushup')">Pushup</button>
    <button class="btn-pose" onclick="pose('bow')">Bow</button>
    <button class="btn-pose" onclick="pose('cute')">Cute</button>
    <button class="btn-pose" onclick="pose('freaky')">Freaky</button>
    <button class="btn-pose" onclick="pose('worm')">Worm</button>
    <button class="btn-pose" onclick="pose('shake')">Shake</button>
    <button class="btn-pose" onclick="pose('shrug')">Shrug</button>
    <button class="btn-pose" onclick="pose('dead')">Dead</button>
    <button class="btn-pose" onclick="pose('crab')">Crab</button>
  </div>

  <button class="btn-stop-all" onclick="stop()">STOP ALL</button>
  <button class="btn-settings" onclick="openSettings()">Settings</button>

  <div id="settingsPanel" class="settings-panel">
    <div class="settings-content">
      <h3>Settings</h3>
      <label>Frame Delay (ms):</label>
      <input type="number" id="frameDelay">
      <label>Walk Cycles:</label>
      <input type="number" id="walkCycles">
      <label>Motor Current Delay (ms):</label>
      <input type="number" id="motorCurrentDelay">
      <button class="btn-save" onclick="saveSettings()">Save Settings</button>
      <button class="btn-close" onclick="closeSettings()">Close</button>
    </div>
  </div>

<script>
function move(dir) { fetch('/cmd?go=' + dir).catch(console.log); }
function stop() { fetch('/cmd?stop=1').catch(console.log); }
function pose(name) { fetch('/cmd?pose=' + name).catch(console.log); }
function openSettings() {
  fetch('/getSettings').then(r => r.json()).then(data => {
    document.getElementById('frameDelay').value = data.frameDelay;
    document.getElementById('walkCycles').value = data.walkCycles;
    document.getElementById('motorCurrentDelay').value = data.motorCurrentDelay;
    document.getElementById('settingsPanel').style.display = 'block';
  });
}
function closeSettings() { document.getElementById('settingsPanel').style.display = 'none'; }
function saveSettings() {
  const fd = document.getElementById('frameDelay').value;
  const wc = document.getElementById('walkCycles').value;
  const mcd = document.getElementById('motorCurrentDelay').value;
  fetch(`/setSettings?frameDelay=${fd}&walkCycles=${wc}&motorCurrentDelay=${mcd}`).then(() => closeSettings());
}
</script>
</body>
</html>
)rawliteral";

// ======================================================================

// Prototypes
void setServoAngle(uint8_t channel, int angle);
void updateFaceBitmap(const unsigned char* bitmap);
bool pressingCheck(String cmd, int ms);
void runRestPose();
void runStandPose(int face = 1);
void runWavePose();
void runDancePose();
void runSwimPose();
void runPointPose();
void runPushupPose();
void runBowPose();
void runCutePose();
void runFreakyPose();
void runWormPose();
void runShakePose();
void runShrugPose();
void runDeadPose();
void runCrabPose();
void runWalkPose();
void runWalkBackward(); 
void runTurnLeft();    
void runTurnRight();   
void handleGetSettings();
void handleSetSettings();

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleCommandWeb() {
  // We send 200 OK immediately so the web browser doesn't hang waiting for animation to finish
  if (server.hasArg("pose")) {
    currentCommand = server.arg("pose");
    server.send(200, "text/plain", "OK"); 
  } 
  else if (server.hasArg("go")) {
    currentCommand = server.arg("go");
    server.send(200, "text/plain", "OK");
  } 
  else if (server.hasArg("stop")) {
    currentCommand = "";
    server.send(200, "text/plain", "OK");
  }
  else {
    server.send(400, "text/plain", "Bad Args");
  }
}

void handleGetSettings() {
  String json = "{";
  json += "\"frameDelay\":" + String(frameDelay) + ",";
  json += "\"walkCycles\":" + String(walkCycles) + ",";
  json += "\"motorCurrentDelay\":" + String(motorCurrentDelay);
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetSettings() {
  if (server.hasArg("frameDelay")) frameDelay = server.arg("frameDelay").toInt();
  if (server.hasArg("walkCycles")) walkCycles = server.arg("walkCycles").toInt();
  if (server.hasArg("motorCurrentDelay")) motorCurrentDelay = server.arg("motorCurrentDelay").toInt();
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  
  // I2C Init for ESP32
  Wire.begin(I2C_SDA, I2C_SCL);

  // OLED Init
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("SSD1306 allocation failed."));
    while (1);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println(F("Creating WiFi..."));
  display.display();

  // --- ACCESS POINT CONFIGURATION ---
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress myIP = WiFi.softAPIP();
  
  Serial.print("AP Created. IP: ");
  Serial.println(myIP);

  // Show Connection Info on OLED
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(F("Connect to WiFi:"));
  display.println(AP_SSID);
  display.println(F(""));
  display.println(F("Captive Portal"));
  display.println(F("will auto-open!"));
  display.display();

  // Start DNS Server for Captive Portal
  // This redirects ALL domain requests to the ESP32's IP
  dnsServer.start(DNS_PORT, "*", myIP);

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/cmd", handleCommandWeb);
  server.on("/getSettings", handleGetSettings);
  server.on("/setSettings", handleSetSettings);
  
  // Catch-all route for captive portal
  // This ensures any URL redirects to the controller page
  server.onNotFound(handleRoot);
  
  server.begin();

  // PWM Init
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  for (int i = 0; i < 8; i++) {
    servos[i].setPeriodHertz(50);
    // Map 0-180 to approx 732-2929us
    servos[i].attach(servoPins[i], 732, 2929);
  }
  delay(10);
  
  // Set default face
  updateFaceBitmap(epd_bitmap_defualt);
  
  Serial.println(F("HTTP server & Captive Portal started."));
}

void loop() {
  // Process DNS requests for captive portal
  dnsServer.processNextRequest();
  
  server.handleClient();

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
  
  // Serial CLI for debugging (can be used to diagnose servo position issues and wiring)
  if (Serial.available()) {
    static char command_buffer[32];
    static byte buffer_pos = 0;
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (buffer_pos > 0) {
        command_buffer[buffer_pos] = '\0';
        int motorNum, angle;
        if(strcmp(command_buffer, "run walk") == 0 || strcmp(command_buffer, "rn wf") == 0) { currentCommand = "forward"; runWalkPose(); currentCommand = ""; }
        else if(strcmp(command_buffer, "rn wb") == 0) { currentCommand = "backward"; runWalkBackward(); currentCommand = ""; }
        else if(strcmp(command_buffer, "rn tl") == 0) { currentCommand = "left"; runTurnLeft(); currentCommand = ""; }
        else if(strcmp(command_buffer, "rn tr") == 0) { currentCommand = "right"; runTurnRight(); currentCommand = ""; }
        else if(strcmp(command_buffer, "run rest") == 0 || strcmp(command_buffer, "rn rs") == 0) runRestPose();
        else if(strcmp(command_buffer, "run stand") == 0 || strcmp(command_buffer, "rn st") == 0) runStandPose(1);
        else if(strcmp(command_buffer, "rn wv") == 0) { currentCommand = "wave"; runWavePose(); }
        else if(strcmp(command_buffer, "rn dn") == 0) { currentCommand = "dance"; runDancePose(); }
        else if(strcmp(command_buffer, "rn sw") == 0) { currentCommand = "swim"; runSwimPose(); }
        else if(strcmp(command_buffer, "rn pt") == 0) { currentCommand = "point"; runPointPose(); }
        else if(strcmp(command_buffer, "rn pu") == 0) { currentCommand = "pushup"; runPushupPose(); }
        else if(strcmp(command_buffer, "rn bw") == 0) { currentCommand = "bow"; runBowPose(); }
        else if(strcmp(command_buffer, "rn ct") == 0) { currentCommand = "cute"; runCutePose(); }
        else if(strcmp(command_buffer, "rn fk") == 0) { currentCommand = "freaky"; runFreakyPose(); }
        else if(strcmp(command_buffer, "rn wm") == 0) { currentCommand = "worm"; runWormPose(); }
        else if(strcmp(command_buffer, "rn sk") == 0) { currentCommand = "shake"; runShakePose(); }
        else if(strcmp(command_buffer, "rn sg") == 0) { currentCommand = "shrug"; runShrugPose(); }
        else if(strcmp(command_buffer, "rn dd") == 0) { currentCommand = "dead"; runDeadPose(); }
        else if(strcmp(command_buffer, "rn cb") == 0) { currentCommand = "crab"; runCrabPose(); }
        else if (strncmp(command_buffer, "all ", 4) == 0) {
             if (sscanf(command_buffer + 4, "%d", &angle) == 1) {
                 for (int i = 0; i < 8; i++) setServoAngle(i, angle);
                 Serial.print("All servos set to "); Serial.println(angle);
             }
        }
        else if (sscanf(command_buffer, "%d %d", &motorNum, &angle) == 2) {
             if (motorNum >= 0 && motorNum < 8) {
                 setServoAngle(motorNum, angle);
                 Serial.print("Servo "); Serial.print(motorNum); Serial.print(" set to "); Serial.println(angle);
             } else {
                 Serial.println("Invalid motor number (0-7)");
             }
        }
        buffer_pos = 0;
      }
    } else if (buffer_pos < sizeof(command_buffer) - 1) {
      command_buffer[buffer_pos++] = c;
    }
  }
}

// Function to update the robot's face
void updateFaceBitmap(const unsigned char* bitmap) {
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, 128, 64, SSD1306_WHITE);
  display.display();
}

// ====== HELPERS ======
void setServoAngle(uint8_t channel, int angle) { 
  if (channel < 8) {
    servos[channel].write(angle);
    delay(motorCurrentDelay);
  }
}

bool pressingCheck(String cmd, int ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    server.handleClient();
    dnsServer.processNextRequest();
    if (currentCommand != cmd) {
      runStandPose(1);
      return false;
    }
    yield();
  }
  return true;
}

// ====== POSES ======
void runRestPose() { 
  Serial.println(F("REST")); 
  updateFaceBitmap(epd_bitmap_rest); 
  for (int i = 0; i < 8; i++) setServoAngle(i, 90); 
}

void runStandPose(int face) { 
  Serial.println(F("STAND")); 
  if (face == 1) updateFaceBitmap(epd_bitmap_stand); 
  setServoAngle(0, 135); 
  setServoAngle(1, 45); 
  setServoAngle(2, 45); 
  setServoAngle(3, 135); 
  setServoAngle(4, 0); 
  setServoAngle(5, 180); 
  setServoAngle(6, 0); 
  setServoAngle(7, 180); 
}

void runWavePose() { 
  Serial.println(F("WAVE")); 
  updateFaceBitmap(epd_bitmap_wave); 
  runStandPose(0); 
  delay(200);
  setServoAngle(4, 80); setServoAngle(6, 180); 
  setServoAngle(3, 60); setServoAngle(0, 100); 
  delay(200);
  setServoAngle(6, 180); 
  delay(300); 
  for (int i = 0; i < 4; i++) { 
    setServoAngle(6, 180); delay(300); 
    setServoAngle(6, 100); delay(300); 
  } 
  runStandPose(1); 
  if (currentCommand == "wave") currentCommand = "";
}

void runDancePose() { 
  Serial.println(F("DANCE")); 
  updateFaceBitmap(epd_bitmap_dance); 
  setServoAngle(0, 90); setServoAngle(1, 90); 
  setServoAngle(2, 90); setServoAngle(3, 90); 
  setServoAngle(4, 160); setServoAngle(5, 160); 
  setServoAngle(6, 10); setServoAngle(7, 10); 
  delay(300); 
  for (int i = 0; i < 5; i++) { 
    setServoAngle(4, 115); setServoAngle(5, 115); 
    setServoAngle(6, 10); setServoAngle(7, 10); 
    delay(300); 
    setServoAngle(4, 160); setServoAngle(5, 160); 
    setServoAngle(6, 65); setServoAngle(7, 65); 
    delay(300); 
  } 
  runStandPose(1); 
  if (currentCommand == "dance") currentCommand = "";
}

void runSwimPose() { 
  Serial.println(F("SWIM")); 
  updateFaceBitmap(epd_bitmap_swim); 
  for (int i = 0; i < 8; i++) setServoAngle(i, 90); 
  for (int i = 0; i < 4; i++) { 
    setServoAngle(0, 135); setServoAngle(1, 45); 
    setServoAngle(2, 45); setServoAngle(3, 135); 
    delay(400); 
    setServoAngle(0, 90); setServoAngle(1, 90); 
    setServoAngle(2, 90); setServoAngle(3, 90); 
    delay(400); 
  } 
  runStandPose(1); 
  if (currentCommand == "swim") currentCommand = "";
}

void runPointPose() { 
  Serial.println(F("POINT")); 
  updateFaceBitmap(epd_bitmap_point); 
  setServoAngle(3, 60); setServoAngle(0, 135); 
  setServoAngle(1, 100); setServoAngle(7, 180); 
  setServoAngle(2, 25); setServoAngle(6, 145);
  setServoAngle(4, 80); setServoAngle(5, 170); 
  delay(2000); 
  runStandPose(1); 
  if (currentCommand == "point") currentCommand = "";
}

void runPushupPose() {
  Serial.println(F("PUSHUP"));
  updateFaceBitmap(epd_bitmap_pushup);
  runStandPose(0); 
  delay(200);
  setServoAngle(2, 0);
  setServoAngle(0, 180);
  setServoAngle(6, 90);
  setServoAngle(5, 90);
  delay(500);
  for (int i = 0; i < 4; i++) {
    setServoAngle(6, 0);
    setServoAngle(5, 180);
    delay(600);
    setServoAngle(6, 90);
    setServoAngle(5, 90);
    delay(500);
  }
  runStandPose(1);
  if (currentCommand == "pushup") currentCommand = "";
}

void runBowPose() {
  Serial.println(F("BOW"));
  updateFaceBitmap(epd_bitmap_bow);
  runStandPose(0); 
  delay(200);
  setServoAngle(2, 0);
  setServoAngle(0, 180);
  setServoAngle(6, 0);
  setServoAngle(5, 180);
  setServoAngle(3, 180);
  setServoAngle(1, 0);
  setServoAngle(4, 0);
  setServoAngle(7, 180);
  delay(600);
  setServoAngle(6, 90);
  setServoAngle(5, 90);
  delay(3000);
  runStandPose(1);
  if (currentCommand == "bow") currentCommand = "";
}

void runCutePose() {
  Serial.println(F("CUTE"));
  updateFaceBitmap(epd_bitmap_cute);
  runStandPose(0); 
  delay(200);
  setServoAngle(3, 160);
  setServoAngle(1, 20);
  setServoAngle(4, 180);
  setServoAngle(7, 0);

  setServoAngle(2, 0);
  setServoAngle(0, 180);
  setServoAngle(6, 180);
  setServoAngle(5, 0);
  delay(200);
  for (int i = 0; i < 5; i++) {
    setServoAngle(4, 180);
    setServoAngle(7, 45);
    delay(300);
    setServoAngle(4, 135);
    setServoAngle(7, 0);
    delay(300);
  }
  runStandPose(1);
  if (currentCommand == "cute") currentCommand = "";
}

void runFreakyPose() {
  Serial.println(F("FREAKY"));
  updateFaceBitmap(epd_bitmap_freaky);
  runStandPose(0); 
  delay(200);
  setServoAngle(2, 0);
  setServoAngle(0, 180);
  setServoAngle(3, 180);
  setServoAngle(1, 0);
  setServoAngle(4, 90);
  setServoAngle(5, 0);
  delay(200);
  for (int i = 0; i < 3; i++) {
    setServoAngle(5, 25);
    delay(400);
    setServoAngle(5, 0);
    delay(400);
  }
  runStandPose(1);
  if (currentCommand == "freaky") currentCommand = "";
}

void runWormPose() {
  Serial.println(F("WORM"));
  updateFaceBitmap(epd_bitmap_worm);
  runStandPose(0);
  delay(200);
  setServoAngle(0, 180); setServoAngle(1, 0); setServoAngle(2, 0); setServoAngle(3, 180);
  setServoAngle(4, 90); setServoAngle(5, 90); setServoAngle(6, 90); setServoAngle(7, 90);
  delay(200);
  for(int i=0; i<5; i++) {
    setServoAngle(5, 45); setServoAngle(6, 135); setServoAngle(4, 45); setServoAngle(7, 135);
    delay(300);
    setServoAngle(5, 135); setServoAngle(6, 45); setServoAngle(4, 135); setServoAngle(7, 45);
    delay(300);
  }
  runStandPose(1);
  if (currentCommand == "worm") currentCommand = "";
}

void runShakePose() {
  Serial.println(F("SHAKE"));
  updateFaceBitmap(epd_bitmap_shake);
  runStandPose(0);
  delay(200);
  setServoAngle(0, 135); setServoAngle(2, 45); setServoAngle(6, 90); setServoAngle(5, 90);
  setServoAngle(3, 90); setServoAngle(1, 90);
  delay(200);
  for(int i=0; i<5; i++) {
    setServoAngle(4, 45); setServoAngle(7, 135);
    delay(300);
    setServoAngle(4, 0); setServoAngle(7, 180);
    delay(300);
  }
  runStandPose(1);
  if (currentCommand == "shake") currentCommand = "";
}

void runShrugPose() {
  Serial.println(F("SHRUG"));
  runStandPose(0);
  updateFaceBitmap(epd_bitmap_dead);
  delay(200);
  setServoAngle(5, 90); setServoAngle(4, 90); setServoAngle(6, 90); setServoAngle(7, 90);
  delay(1000);
  updateFaceBitmap(epd_bitmap_shrug);
  setServoAngle(5, 0); setServoAngle(4, 180); setServoAngle(6, 180); setServoAngle(7, 0);
  delay(1500);
  runStandPose(1);
  if (currentCommand == "shrug") currentCommand = "";
}

void runDeadPose() {
  Serial.println(F("DEAD"));
  runStandPose(0);
  updateFaceBitmap(epd_bitmap_dead);
  delay(200);
  setServoAngle(5, 90); setServoAngle(4, 90); setServoAngle(6, 90); setServoAngle(7, 90);
  if (currentCommand == "dead") currentCommand = "";
}

void runCrabPose() {
  Serial.println(F("CRAB"));
  updateFaceBitmap(epd_bitmap_crab);
  runStandPose(0);
  delay(200);
  setServoAngle(0, 90); setServoAngle(1, 90); setServoAngle(2, 90); setServoAngle(3, 90);
  setServoAngle(4, 0); setServoAngle(5, 180); setServoAngle(6, 45); setServoAngle(7, 135);
  for(int i=0; i<5; i++) {
    setServoAngle(4, 45); setServoAngle(5, 135); setServoAngle(6, 0); setServoAngle(7, 180);
    delay(300);
    setServoAngle(4, 0); setServoAngle(5, 180); setServoAngle(6, 45); setServoAngle(7, 135);
    delay(300);
  }
  runStandPose(1);
  if (currentCommand == "crab") currentCommand = "";
}
// --- MOVEMENT ANIMATIONS ---

void runWalkPose() {
  Serial.println(F("WALK FWD"));
  updateFaceBitmap(epd_bitmap_walk);
  // Initial Step
  setServoAngle(5, 135); setServoAngle(6, 45);
  setServoAngle(1, 100); setServoAngle(2, 25);
  if (!pressingCheck("forward", frameDelay)) return;
  
  for (int i = 0; i < walkCycles; i++) {
    setServoAngle(5, 135); setServoAngle(6, 0);
    if (!pressingCheck("forward", frameDelay)) return;
    setServoAngle(7, 135); setServoAngle(3, 90);
    setServoAngle(4, 0); setServoAngle(0, 180);
    if (!pressingCheck("forward", frameDelay)) return;    
    setServoAngle(1, 45); setServoAngle(2, 90);
    if (!pressingCheck("forward", frameDelay)) return;
    setServoAngle(4, 45); setServoAngle(7, 180);
    if (!pressingCheck("forward", frameDelay)) return;
    setServoAngle(5, 180); setServoAngle(6, 45);
    setServoAngle(1, 100); setServoAngle(2, 25);
    if (!pressingCheck("forward", frameDelay)) return;  
    setServoAngle(3, 135); setServoAngle(0, 90);
    if (!pressingCheck("forward", frameDelay)) return;
  }
  runStandPose(1);
}

// Logic reversed from Walk
void runWalkBackward() {
  Serial.println(F("WALK BACK"));
  updateFaceBitmap(epd_bitmap_walk);
  if (!pressingCheck("backward", frameDelay)) return;
  
  for (int i = 0; i < walkCycles; i++) {
    setServoAngle(2, 0); setServoAngle(1, 90);
    setServoAngle(5, 135); setServoAngle(7, 135);
    if (!pressingCheck("backward", frameDelay)) return;
    setServoAngle(3, 135); setServoAngle(0, 90);
    setServoAngle(6, 45); setServoAngle(4, 45);
    if (!pressingCheck("backward", frameDelay)) return;
    setServoAngle(5, 180); setServoAngle(7, 180);
    setServoAngle(2, 45); setServoAngle(1, 45);
    if (!pressingCheck("backward", frameDelay)) return;
    setServoAngle(4, 0); setServoAngle(6, 0);
    setServoAngle(3, 90); setServoAngle(0, 135);
    if (!pressingCheck("backward", frameDelay)) return;
  }
  runStandPose(1);
}

// Simple turn logic
void runTurnLeft() {
  Serial.println(F("TURN LEFT"));
  updateFaceBitmap(epd_bitmap_walk);
  for (int i = 0; i < walkCycles; i++) {
    //legset 1 (R1 L2)
    setServoAngle(5, 135); setServoAngle(7, 135); 
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(0, 180); setServoAngle(3, 180); 
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(5, 180); setServoAngle(7, 180); 
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(0, 135); setServoAngle(3, 135);
    if (!pressingCheck("left", frameDelay)) return;
      //legset 2 (R2 L1)
    setServoAngle(4, 45); setServoAngle(6, 45); 
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(1, 90); setServoAngle(2, 90); 
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(4, 0); setServoAngle(6, 0); 
    if (!pressingCheck("left", frameDelay)) return;
    setServoAngle(1, 45); setServoAngle(2, 45);
    if (!pressingCheck("left", frameDelay)) return;  
  }
  runStandPose(1);
}

void runTurnRight() {
  Serial.println(F("TURN RIGHT"));
  updateFaceBitmap(epd_bitmap_walk);
  for (int i = 0; i < walkCycles; i++) {
    //legset 2 (R2 L1)
    setServoAngle(4, 45); setServoAngle(6, 45); 
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(1, 0); setServoAngle(2, 0); 
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(4, 0); setServoAngle(6, 0); 
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(1, 45); setServoAngle(2, 45);
    if (!pressingCheck("right", frameDelay)) return;  
    //legset 1 (R1 L2)
    setServoAngle(5, 135); setServoAngle(7, 135); 
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(0, 90); setServoAngle(3, 90); 
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(5, 180); setServoAngle(7, 180); 
    if (!pressingCheck("right", frameDelay)) return;
    setServoAngle(0, 135); setServoAngle(3, 135);
    if (!pressingCheck("right", frameDelay)) return;
  }
  runStandPose(1);
}
