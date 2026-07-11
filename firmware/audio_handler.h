#pragma once

#include <Arduino.h>
#include "driver/i2s_std.h"

// ── I2S duplex on I2S_NUM_0 ───────────────────────────────────────────────────
// Single master port owns BCLK/WS — no GPIO conflict between speaker and mic.
//
// Wiring (Sesame Distro V3 header):
//   GPIO1  ↔ BCLK  (INMP441 SCK + MAX98357 BCLK)
//   GPIO2  ↔ WS    (INMP441 WS  + MAX98357 LRC)
//   GPIO3  ← MIC   (INMP441 SD)
//   GPIO14 → AMP   (MAX98357 DIN)

#define AUDIO_SAMPLE_RATE  16000

// Software mic gain — compensates for the mic being enclosed in the robot body
// (small port hole attenuates sound significantly). Applied to both the WakeNet
// feed and VAD recording; VAD thresholds are scaled by the same factor.
#define MIC_GAIN 4
static inline int16_t micApplyGain(int32_t s) {
    s *= MIC_GAIN;
    if (s >  32767) s =  32767;
    if (s < -32768) s = -32768;
    return (int16_t)s;
}
#define AUDIO_BCLK         GPIO_NUM_1
#define AUDIO_WS           GPIO_NUM_2
#define AUDIO_MIC_SD       GPIO_NUM_3
#define AUDIO_AMP_DIN      GPIO_NUM_14

static i2s_chan_handle_t _aud_tx = nullptr;
static i2s_chan_handle_t _aud_rx = nullptr;

bool audioSetup() {
    i2s_chan_config_t cc;
    memset(&cc, 0, sizeof(cc));
    cc.id            = I2S_NUM_0;
    cc.role          = I2S_ROLE_MASTER;
    cc.dma_desc_num  = 8;
    cc.dma_frame_num = 256;
    cc.auto_clear    = true;
    if (i2s_new_channel(&cc, &_aud_tx, &_aud_rx) != ESP_OK) {
        Serial.println("[Audio] channel alloc failed"); return false;
    }

    i2s_std_clk_config_t  clk  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE);
    i2s_std_slot_config_t slot  = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                                      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    i2s_std_gpio_config_t gpio;
    memset(&gpio, 0, sizeof(gpio));
    gpio.mclk = GPIO_NUM_NC;
    gpio.bclk = AUDIO_BCLK;
    gpio.ws   = AUDIO_WS;
    gpio.dout = AUDIO_AMP_DIN;
    gpio.din  = AUDIO_MIC_SD;

    i2s_std_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.clk_cfg  = clk;
    cfg.slot_cfg = slot;
    cfg.gpio_cfg = gpio;

    esp_err_t e;
    e = i2s_channel_init_std_mode(_aud_tx, &cfg);
    if (e != ESP_OK) { Serial.printf("[Audio] TX init failed: %d\n", e); return false; }
    e = i2s_channel_init_std_mode(_aud_rx, &cfg);
    if (e != ESP_OK) { Serial.printf("[Audio] RX init failed: %d\n", e); return false; }
    e = i2s_channel_enable(_aud_rx);
    if (e != ESP_OK) { Serial.printf("[Audio] RX enable failed: %d\n", e); return false; }
    e = i2s_channel_enable(_aud_tx);
    if (e != ESP_OK) { Serial.printf("[Audio] TX enable failed: %d\n", e); return false; }
    return true;
}

// Blocking sine-wave beep on the speaker. Optional amplitude override —
// the wake acknowledgment uses a quiet beep so the enclosed speaker doesn't
// couple into the mic and contaminate the recording that follows.
void playBeep(float freq, int ms, int amplitude = 10000) {
    if (!_aud_tx) return;
    const int n = min((int)(AUDIO_SAMPLE_RATE * ms / 1000), AUDIO_SAMPLE_RATE / 5);
    static int16_t buf[AUDIO_SAMPLE_RATE / 5 * 2];
    float phase = 0;
    for (int i = 0; i < n; i++) {
        phase += 2.0f * M_PI * freq / AUDIO_SAMPLE_RATE;
        // 5ms fade-in/out — a hard-edged sine rings the enclosure much longer
        float env = 1.0f;
        int fade = AUDIO_SAMPLE_RATE / 200;
        if (i < fade)          env = (float)i / fade;
        else if (i > n - fade) env = (float)(n - i) / fade;
        int16_t s = (int16_t)(amplitude * env * sinf(phase));
        buf[i*2] = s; buf[i*2+1] = s;
    }
    size_t written = 0;
    i2s_channel_write(_aud_tx, buf, n * 4, &written, pdMS_TO_TICKS(500));
}

// Read one stereo buffer from mic; return RMS of left channel (INMP441 = left).
static float _micRms() {
    if (!_aud_rx) return 0;
    static int16_t buf[512];  // 256 stereo pairs
    size_t got = 0;
    i2s_channel_read(_aud_rx, buf, sizeof(buf), &got, pdMS_TO_TICKS(50));
    int pairs = got / 4;
    if (pairs == 0) return 0;
    int64_t sum = 0;
    for (int i = 0; i < pairs; i++) { int16_t s = buf[i * 2]; sum += (int64_t)s * s; }
    return sqrtf((float)(sum / pairs));
}

// Play a WAV file from a memory buffer (mono 16-bit 16kHz from voice_service.py).
// Scans for the "data" chunk, then streams stereo (L=R) to the speaker.
// Takes ownership: caller must free() the buffer after this returns.
void playWavFromMemory(uint8_t* buf, uint32_t len) {
    if (!_aud_tx || !buf || len < 44) { free(buf); return; }

    uint32_t dataOff = 44;
    for (uint32_t i = 12; i + 8 < min(len, (uint32_t)200); i++) {
        if (buf[i]=='d' && buf[i+1]=='a' && buf[i+2]=='t' && buf[i+3]=='a') {
            dataOff = i + 8; break;
        }
    }

    static int16_t stereo[512];
    uint32_t pos = dataOff;
    while (pos + 1 < len) {
        int n = (int)min((uint32_t)256, (len - pos) / 2);
        for (int i = 0; i < n; i++) {
            int16_t s = (int16_t)((uint16_t)buf[pos] | ((uint16_t)buf[pos+1] << 8));
            pos += 2;
            stereo[i*2] = s; stereo[i*2+1] = s;
        }
        size_t written = 0;
        i2s_channel_write(_aud_tx, stereo, n * 4, &written, pdMS_TO_TICKS(500));
    }
    free(buf);
}

// Stream a mono 16-bit WAV directly from a WiFiClient — no heap allocation needed.
// Reads the WAV header to find the data chunk, then pipes audio to I2S as stereo.
void playWavFromClient(WiFiClient& client, uint32_t wavLen) {
    if (!_aud_tx || wavLen < 44) return;

    // Read up to 200 bytes to find the "data" chunk offset
    static uint8_t hdr[200];
    uint32_t hdrRead = min(wavLen, (uint32_t)sizeof(hdr));
    size_t got = 0;
    uint32_t deadline = millis() + 5000;
    while (got < hdrRead && millis() < deadline) {
        if (client.available()) got += client.readBytes(hdr + got, hdrRead - got);
        else delay(2);
    }

    uint32_t dataOff = 44;
    for (uint32_t i = 12; i + 8 < got; i++) {
        if (hdr[i]=='d' && hdr[i+1]=='a' && hdr[i+2]=='t' && hdr[i+3]=='a') {
            dataOff = i + 8; break;
        }
    }

    // Skip any header bytes already read past dataOff
    uint32_t remaining = wavLen - (uint32_t)got;
    if (dataOff > got) {
        // data chunk starts beyond what we read — drain the gap
        uint32_t gap = dataOff - got;
        uint8_t tmp[64];
        while (gap > 0) {
            uint32_t n = min(gap, (uint32_t)sizeof(tmp));
            size_t r = 0;
            uint32_t dl = millis() + 2000;
            while (r < n && millis() < dl) {
                if (client.available()) r += client.readBytes(tmp + r, n - r);
                else delay(2);
            }
            gap -= r;
            remaining -= r;
            if (r < n) break;
        }
    } else {
        // dataOff is inside the header bytes we already read — replay from hdr[dataOff]
        // push those samples out first
        uint32_t preBytes = got - dataOff;
        static int16_t stereo[512];
        uint32_t pos = dataOff;
        while (pos + 1 < got) {
            int n = (int)min((uint32_t)256, (got - pos) / 2);
            for (int i = 0; i < n; i++) {
                int16_t s = (int16_t)((uint16_t)hdr[pos] | ((uint16_t)hdr[pos+1] << 8));
                pos += 2;
                stereo[i*2] = s; stereo[i*2+1] = s;
            }
            size_t written = 0;
            i2s_channel_write(_aud_tx, stereo, n * 4, &written, pdMS_TO_TICKS(500));
        }
    }

    // Stream remaining audio from TCP → I2S in 512-byte mono chunks.
    // 30s cap: loop() is blocked while this streams (HTTP/TCP/wake all dead),
    // so a stalled connection must not wedge the robot for long.
    static uint8_t mono[512];
    static int16_t stereo2[512];
    deadline = millis() + 30000;
    while (remaining >= 2 && millis() < deadline) {
        uint32_t want = min(remaining, (uint32_t)sizeof(mono));
        // align to even bytes
        want &= ~1u;
        size_t r = 0;
        uint32_t dl = millis() + 3000;
        while (r < want && millis() < dl) {
            if (client.available()) r += client.readBytes(mono + r, want - r);
            else delay(2);
        }
        if (r < 2) break;
        r &= ~1u;
        int pairs = r / 2;
        for (int i = 0; i < pairs; i++) {
            int16_t s = (int16_t)((uint16_t)mono[i*2] | ((uint16_t)mono[i*2+1] << 8));
            stereo2[i*2] = s; stereo2[i*2+1] = s;
        }
        size_t written = 0;
        i2s_channel_write(_aud_tx, stereo2, pairs * 4, &written, pdMS_TO_TICKS(500));
        remaining -= r;
    }
}

// Record mono 16-bit PCM into outBuf with VAD.
// Calibrates noise floor from first ~360ms, then stops 720ms after speech ends.
// Returns number of bytes written (mono 16-bit samples, 2 bytes each).
size_t micRecord(uint8_t* outBuf, size_t maxLen) {
    if (!_aud_rx || !outBuf || maxLen < 2) return 0;

    // Each chunk: 480 stereo pairs (30ms) = 1920 bytes → 480 mono samples = 960 bytes out
    const int CHUNK_PAIRS  = 480;
    const int CHUNK_STEREO = CHUNK_PAIRS * 4;   // bytes to read from I2S
    const int CHUNK_MONO   = CHUNK_PAIRS * 2;   // bytes to write to outBuf
    const int SILENCE_HOLD = 8;                 // silence chunks after speech → stop (~240ms)
    const int SPEECH_ARM   = 3;                 // consecutive loud chunks to count as speech (90ms)
    const int PREROLL_CHUNKS = 10;              // leading audio kept before speech starts (300ms)

    static int16_t stereoChunk[CHUNK_PAIRS * 2];

    // Flush stale DMA audio (wake beep tail + "Hi ESP" itself) before calibrating.
    // Without this the calibration reads wake-word RMS as the noise floor and
    // sets speechThresh too high, making the actual command appear as silence.
    {
        size_t got;
        for (int i = 0; i < 8; i++) {
            got = 0;
            i2s_channel_read(_aud_rx, stereoChunk, CHUNK_STEREO, &got, 0);
            if (got < (size_t)CHUNK_STEREO) break;
        }
    }

    auto readChunk = [&](int16_t* mono, int* nSamples) -> float {
        size_t got = 0;
        i2s_channel_read(_aud_rx, stereoChunk, CHUNK_STEREO, &got, pdMS_TO_TICKS(100));
        *nSamples = (int)(got / 4);
        int64_t sum = 0;
        for (int i = 0; i < *nSamples; i++) {
            int16_t s = micApplyGain(stereoChunk[i * 2]);  // left channel
            mono[i]   = s;
            sum += (int64_t)s * s;
        }
        return *nSamples > 0 ? sqrtf((float)(sum / *nSamples)) : 0.0f;
    };

    // Calibrate noise floor from the first 6 chunks (~180ms) — same approach as
    // sesame-robot-sense micRecord4s(). The wake task is suspended so the DMA
    // ring only has room-ambient audio. User hasn't spoken yet (they wait for
    // the beep before talking), so this captures true ambient, not wake-word RMS.
    static int16_t monoBuf[CHUNK_PAIRS];
    float noiseSum = 0.0f; int noiseChunks = 0;
    while (noiseChunks < 6) {
        int n; float rms = readChunk(monoBuf, &n);
        if (n > 0) { noiseSum += rms; noiseChunks++; }
    }
    float noiseFloor   = noiseSum / noiseChunks;
    float speechThresh = constrain(noiseFloor * 2.0f + 300.0f, 800.0f, 6000.0f);
    dlog("[Mic] noise=%.0f thresh=%.0f", noiseFloor, speechThresh);

    size_t captured   = 0;
    int    silenceRun = 0;
    int    speechRun  = 0;
    bool   speechSeen = false;
    float  peakRms    = 0;
    unsigned long tStart = millis();

    while (captured + CHUNK_MONO <= maxLen) {
        int n;
        float rms = readChunk(monoBuf, &n);
        if (n <= 0) continue;
        if (rms > peakRms) peakRms = rms;

        memcpy(outBuf + captured, monoBuf, n * 2);
        captured += n * 2;

        if (!speechSeen) {
            // Waiting for the user to start talking. A single loud blip (breath,
            // servo click) doesn't count — require SPEECH_ARM consecutive chunks.
            if (rms >= speechThresh) {
                if (++speechRun >= SPEECH_ARM) speechSeen = true;
            } else {
                speechRun = 0;
            }
            // Keep only a short pre-roll so leading silence isn't uploaded and a
            // long pause before speaking can't fill the buffer.
            if (!speechSeen && captured > (size_t)(PREROLL_CHUNKS * CHUNK_MONO)) {
                memmove(outBuf, outBuf + CHUNK_MONO, captured - CHUNK_MONO);
                captured -= CHUNK_MONO;
            }
            // Generous window: user may pause after the wake beep before speaking.
            if (!speechSeen && millis() - tStart > 5000) break;
        } else {
            if (rms >= speechThresh) silenceRun = 0;
            else if (++silenceRun >= SILENCE_HOLD) break;
        }
    }
    dlog("[Mic] recorded %zu bytes (%.1fs) peak=%.0f thresh=%.0f%s",
                  captured, captured / 32000.0f, peakRms, speechThresh,
                  speechSeen ? "" : " (no speech detected)");
    return speechSeen ? captured : 0;
}
