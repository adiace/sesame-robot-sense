#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "esp_netif.h"
#include "voice_config.h"

// ── Voice: stream PCM to companion app, receive WAV response, play it ─────────
//
// Robot → server:  [uint32 LE pcm_len][PCM bytes]
// Server → robot:  [uint32 LE wav_len][WAV bytes]  (0 = no response)
//
// Run on laptop:  python3 software/voice_service.py

static bool _voiceSendAll(WiFiClient& c, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    uint32_t deadline = millis() + 25000;
    while (sent < len) {
        if (millis() > deadline) {
            dlog("[Voice] send timeout (%zu/%zu)", sent, len);
            return false;
        }
        size_t n = c.write(buf + sent, min(len - sent, (size_t)1024));
        if (n > 0) sent += n;
        else       delay(2);
    }
    c.flush();
    return true;
}

static bool _voiceRecvAll(WiFiClient& c, uint8_t* buf, size_t len) {
    size_t got = 0;
    uint32_t deadline = millis() + 10000;
    while (got < len && millis() < deadline) {
        if (c.available()) got += c.readBytes(buf + got, len - got);
        else               delay(10);
    }
    return got == len;
}

// Send PCM clip to companion app, wait for WAV response, play it back.
// playWavFromMemory() is defined in audio_handler.h (included before this file).
bool voiceStreamToServer(const uint8_t* pcm, size_t len) {
    if (WiFi.status() != WL_CONNECTED) {
        dlog("[Voice] no WiFi");
        return false;
    }
    dlog("[Voice] STA IP=%s  sending %zu bytes (%.1fs) → %s:%d",
                  WiFi.localIP().toString().c_str(),
                  len, len / 32000.0f, VOICE_SERVER_IP, AUDIO_RX_PORT);

    for (int attempt = 1; attempt <= 3; attempt++) {
        // In AP+STA dual mode the ESP32 default netif may be the AP interface.
        // Force STA so outbound TCP goes out the right interface.
        esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        dlog("[Voice] sta netif=%p", (void*)sta);
        if (sta) esp_netif_set_default_netif(sta);

        WiFiClient client;
        client.setTimeout(25);

        if (!client.connect(VOICE_SERVER_IP, AUDIO_RX_PORT)) {
            dlog("[Voice] connect failed (attempt %d)", attempt);
            if (attempt < 3) delay(500);
            continue;
        }

        // Send PCM length header + data
        uint8_t hdr[4] = {
            (uint8_t)(len), (uint8_t)(len >> 8),
            (uint8_t)(len >> 16), (uint8_t)(len >> 24)
        };
        client.write(hdr, 4);
        if (!_voiceSendAll(client, pcm, len)) {
            dlog("[Voice] PCM send failed (attempt %d)", attempt);
            client.stop();
            if (attempt < 3) delay(500);
            continue;
        }
        dlog("[Voice] %zu bytes sent — waiting for WAV...", len);

        // Wait up to 15s for WAV length header (server replies in 1-3s warm,
        // ~9s on first request; a longer wait just wedges loop() — HTTP, TCP
        // and wake are all dead while this runs)
        uint32_t deadline = millis() + 15000;
        while (client.available() < 4 && millis() < deadline) {
            delay(20);
            if ((millis() % 5000) < 20)
                dlog("[Voice] waiting... %lus",
                              (unsigned long)((millis() - (deadline - 15000)) / 1000));
        }

        if (client.available() < 4) {
            dlog("[Voice] timeout — no WAV received");
            client.stop();
            return true;
        }

        uint8_t rhdr[4];
        client.readBytes(rhdr, 4);
        uint32_t wavLen = (uint32_t)rhdr[0] | ((uint32_t)rhdr[1] << 8)
                        | ((uint32_t)rhdr[2] << 16) | ((uint32_t)rhdr[3] << 24);

        if (wavLen == 0) {
            dlog("[Voice] no speech detected — server silent");
            client.stop();
            return true;
        }
        if (wavLen > 4u * 1024u * 1024u) {
            dlog("[Voice] WAV too large (%u) — skipping", wavLen);
            client.stop();
            return true;
        }

        dlog("[Voice] streaming %u byte WAV", wavLen);
        // Stream directly from TCP to I2S — no heap buffer needed.
        playWavFromClient(client, wavLen);
        client.stop();
        return true;

    }

    dlog("[Voice] all attempts failed");
    return false;
}
