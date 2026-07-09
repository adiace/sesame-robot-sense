#pragma once
#include <Arduino.h>
#include <WiFi.h>

// WiFi serial mirror — streams all Serial output to any TCP client on port 8890.
// Hook is installed in setup(); no changes to existing Serial.printf calls needed.
//
// To receive on Mac:
//   nc 192.168.68.58 8890

#define WIFI_LOG_PORT 8890
#define WIFI_LOG_BUF  4096

static WiFiServer   _logServer(WIFI_LOG_PORT);
static WiFiClient   _logClient;
static char         _logRing[WIFI_LOG_BUF];
static int          _logHead = 0;

static int _wifiLogVprintf(const char* fmt, va_list args) {
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    // write to USB serial
    Serial.print(buf);
    // mirror to TCP client if connected
    if (_logClient && _logClient.connected()) {
        _logClient.print(buf);
    }
    return n;
}

// Call from networkTask() after WiFi is up — starts the log server.
void wifiLogServerBegin() {
    _logServer.begin();
}

// Call from networkTask() loop — accepts new log client, drops old one.
void wifiLogServerHandle() {
    if (_logServer.hasClient()) {
        if (_logClient && _logClient.connected()) _logClient.stop();
        _logClient = _logServer.available();
    }
}

// Call once in setup() after Serial.begin() — hooks all printf-family output.
void wifiLogSetup() {
    esp_log_set_vprintf(_wifiLogVprintf);
}
