#pragma once
#include <Arduino.h>
#include <WiFi.h>

// WiFi debug log — port 8890.
//
//   nc sesame-robot.local 8890
//
// dlog(fmt, ...) writes to USB serial AND to the TCP client, and keeps the
// last WIFI_LOG_BUF bytes in a ring that is replayed when a client connects —
// so you see recent history (boot messages, the last voice interaction) even
// if you connect after the fact.
//
// Note: esp_log_set_vprintf only captures ESP-IDF logs, NOT Arduino
// Serial.print — that's why plain Serial prints never show on 8890. Use dlog
// for anything you want visible over WiFi.

#define WIFI_LOG_PORT 8890
#define WIFI_LOG_BUF  4096

static WiFiServer   _logServer(WIFI_LOG_PORT);
static WiFiClient   _logClient;
static char         _logRing[WIFI_LOG_BUF];
static int          _logHead    = 0;      // next write position
static bool         _logWrapped = false;

static void _logRingWrite(const char* s, int n) {
    for (int i = 0; i < n; i++) {
        _logRing[_logHead] = s[i];
        _logHead = (_logHead + 1) % WIFI_LOG_BUF;
        if (_logHead == 0) _logWrapped = true;
    }
}

// printf-style; appends "\n". Safe to call before WiFi is up (ring buffers it).
void dlog(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 2, fmt, args);
    va_end(args);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 2) n = sizeof(buf) - 2;
    buf[n]     = '\n';
    buf[n + 1] = '\0';
    Serial.print(buf);
    _logRingWrite(buf, n + 1);
    if (_logClient && _logClient.connected()) _logClient.print(buf);
}

// IDF log hook — routes esp_log output through the same path.
static int _wifiLogVprintf(const char* fmt, va_list args) {
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n > 0) {
        Serial.print(buf);
        _logRingWrite(buf, min(n, (int)sizeof(buf) - 1));
        if (_logClient && _logClient.connected()) _logClient.print(buf);
    }
    return n;
}

void wifiLogSetup() {
    esp_log_set_vprintf(_wifiLogVprintf);
}

void wifiLogServerBegin() {
    _logServer.begin();
}

void wifiLogServerHandle() {
    if (_logServer.hasClient()) {
        if (_logClient && _logClient.connected()) _logClient.stop();
        _logClient = _logServer.available();
        // Replay ring history so the client sees what led up to now
        _logClient.printf("=== sesame-robot-voice log  uptime=%lus ===\r\n",
                          millis() / 1000);
        if (_logWrapped)
            _logClient.write((const uint8_t*)_logRing + _logHead,
                             WIFI_LOG_BUF - _logHead);
        if (_logHead > 0)
            _logClient.write((const uint8_t*)_logRing, _logHead);
        _logClient.println("=== live ===");
    }
}
