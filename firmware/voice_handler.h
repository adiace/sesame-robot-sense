#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include "voice_config.h"
#include "wifi_log.h"

// ── Voice round-trip: POST PCM → receive WAV ─────────────────────────────────
// Sends raw mono 16-bit 16kHz PCM to the laptop voice service.
// Retries once on send-payload failure (-3) which happens when WiFi drops
// mid-upload or the server closes an idle keep-alive connection.

#define VOICE_RESPONSE_PATH "/response.wav"

static bool _voiceDoRequest(const uint8_t* pcm, size_t len) {
    char url[64];
    snprintf(url, sizeof(url), "http://%s:%d/listen", VOICE_SERVER_IP, VOICE_SERVER_PORT);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/octet-stream");
    http.setConnectTimeout(5000);   // 5s to establish TCP
    http.setTimeout(60000);         // 60s for full round-trip (LLM + TTS)

    int code = http.POST((uint8_t*)pcm, len);
    if (code != 200) {
        dlog("Voice: HTTP %d", code);
        http.end();
        return false;
    }

    int total = http.getSize();
    dlog("Voice: receiving %d byte WAV", total);

    File f = SPIFFS.open(VOICE_RESPONSE_PATH, "w");
    if (!f) {
        dlogs("Voice: SPIFFS write failed");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[512];
    int remaining = (total > 0) ? total : INT_MAX;
    while (http.connected() && remaining > 0) {
        int avail = stream->available();
        if (avail > 0) {
            int got = stream->read(buf, min(avail, (int)sizeof(buf)));
            f.write(buf, got);
            remaining -= got;
        } else {
            delay(1);
        }
        if (total < 0 && !http.connected()) break;
    }
    f.close();
    http.end();
    return true;
}

bool voiceRequest(const uint8_t* pcm, size_t len) {
    if (WiFi.status() != WL_CONNECTED) {
        dlogs("Voice: no WiFi");
        return false;
    }

    dlog("Voice: POST %zu bytes (%.1fs) → %s:%d",
         len, len / 32000.0f, VOICE_SERVER_IP, VOICE_SERVER_PORT);

    for (int attempt = 1; attempt <= 4; attempt++) {
        if (_voiceDoRequest(pcm, len)) return true;
        if (attempt < 4) {
            dlog("Voice: attempt %d failed, retrying in 1s...", attempt);
            delay(1000);
        }
    }
    dlogs("Voice: all attempts failed");
    return false;
}
