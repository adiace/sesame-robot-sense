#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "voice_config.h"
#include "wifi_log.h"

// ── Voice: stream PCM to companion app, receive WAV response, play it ────────
//
// Protocol (robot → laptop):  [uint32 LE pcm_len][PCM bytes]
// Protocol (laptop → robot):  [uint32 LE wav_len][WAV bytes]  (0 = silent)
//
// Companion app flow:
//   1. faster_whisper STT → Ollama LLM → macOS say+afconvert TTS
//   2. Sends WAV + {command, face} to robot via TCP port 8888
//
// WAV is played directly from PSRAM via playWavFromMemory() — no SPIFFS write.

static bool _voiceSendAll(WiFiClient& c, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    uint32_t deadline = millis() + 25000;
    while (sent < len) {
        if (millis() > deadline) {
            dlog("Voice: send timeout (%zu/%zu)", sent, len);
            return false;
        }
        size_t n = c.write(buf + sent, min(len - sent, (size_t)1024));
        if (n > 0) {
            sent += n;
        } else {
            delay(2);   // TCP tx buffer full — yield and retry
        }
    }
    c.flush();
    return true;
}

static bool _voiceRecvAll(WiFiClient& c, uint8_t* buf, size_t len) {
    size_t got = 0;
    uint32_t deadline = millis() + 10000;
    while (got < len && millis() < deadline) {
        if (c.available()) {
            got += c.readBytes(buf + got, len - got);
        } else {
            delay(10);
        }
    }
    return got == len;
}

// Fire: sends PCM clip to companion app, waits for WAV response, plays it.
// Returns true if PCM was sent successfully (WAV is best-effort).
bool voiceStreamToServer(const uint8_t* pcm, size_t len) {
    if (WiFi.status() != WL_CONNECTED) {
        dlogs("Voice: no WiFi — cannot send clip");
        return false;
    }
    dlog("Voice: sending %zu bytes (%.1fs) → %s:%d",
         len, len / 32000.0f, VOICE_SERVER_IP, AUDIO_RX_PORT);

    for (int attempt = 1; attempt <= 3; attempt++) {
        WiFiClient client;
        client.setTimeout(25);   // 25s total — covers STT + LLM + TTS latency

        if (!client.connect(VOICE_SERVER_IP, AUDIO_RX_PORT)) {
            dlog("Voice: connect failed (attempt %d)", attempt);
            if (attempt < 3) delay(500);
            continue;
        }

        // ── Send PCM ──────────────────────────────────────────────────────────
        uint8_t hdr[4] = {
            (uint8_t)(len),       (uint8_t)(len >> 8),
            (uint8_t)(len >> 16), (uint8_t)(len >> 24)
        };
        client.write(hdr, 4);
        if (!_voiceSendAll(client, pcm, len)) {
            dlog("Voice: PCM send failed (attempt %d)", attempt);
            client.stop();
            if (attempt < 3) delay(500);
            continue;
        }
        dlog("Voice: %zu bytes sent, waiting for WAV...", len);

        // ── Wait for WAV length header (4 bytes) ──────────────────────────────
        uint32_t deadline = millis() + 40000;   // 40s for STT+LLM+TTS
        while (client.available() < 4 && millis() < deadline) {
            delay(20);
            if ((millis() % 5000) < 20)
                dlog("Voice: waiting for WAV... %lus elapsed",
                     (unsigned long)((millis() - (deadline - 40000)) / 1000));
        }

        if (client.available() < 4) {
            dlogs("Voice: response timeout — no WAV header received");
            client.stop();
            return true;   // PCM was sent; just no response came back
        }

        uint8_t rhdr[4];
        client.readBytes(rhdr, 4);
        uint32_t wavLen = (uint32_t)rhdr[0] | ((uint32_t)rhdr[1] << 8)
                        | ((uint32_t)rhdr[2] << 16) | ((uint32_t)rhdr[3] << 24);

        if (wavLen == 0 || wavLen > 1024u * 1024u) {
            dlog("Voice: empty/invalid WAV len=%u — done", wavLen);
            client.stop();
            return true;
        }

        dlog("Voice: receiving %u byte WAV response", wavLen);

        // ── Receive WAV into PSRAM ────────────────────────────────────────────
        uint8_t* wavBuf = (uint8_t*)ps_malloc(wavLen);
        if (!wavBuf) {
            dlogs("Voice: ps_malloc failed for WAV buffer");
            client.stop();
            return true;
        }

        bool ok = _voiceRecvAll(client, wavBuf, wavLen);
        client.stop();

        if (!ok) {
            dlogs("Voice: WAV receive incomplete");
            free(wavBuf);
            return true;
        }

        // ── Play directly from PSRAM — no SPIFFS write needed ────────────────
        // playWavFromMemory() takes ownership of wavBuf and frees it when done.
        dlog("Voice: playing WAV (%u bytes) from RAM", wavLen);
        playWavFromMemory(wavBuf, wavLen);
        return true;
    }

    dlogs("Voice: all send attempts failed");
    return false;
}
