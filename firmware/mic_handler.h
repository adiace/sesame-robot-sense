#pragma once

#include <Arduino.h>
#include "esp_idf_version.h"
#include "audio_handler.h"   // for stopAudio(), audioUninstall(), audioReinstall()
#include "wifi_log.h"        // dlog() / dlogs() — Serial + WiFi log

// ── PDM microphone (XIAO ESP32-S3 Sense internal mic) ────────────────────────
// PDM only works on I2S_NUM_0 on ESP32-S3, so we share it with the speaker.
// micRecordWithVAD() calls audioUninstall(), opens PDM, records, then
// calls audioReinstall() to restore the speaker.
//
// IDF 5.x (Arduino 3.x): uses driver/i2s_pdm.h new channel API directly.
//   ESP_I2S.h is NOT used — it redefines i2s_mode_t and conflicts with
//   driver/i2s.h (legacy) that audio_handler.h uses for the speaker.
// IDF 4.x (Arduino 2.x): uses legacy driver with non-blocking poll workaround
//   for the ESP32-S3 PDM DMA bug.

#define MIC_SAMPLE_RATE 16000
#define MIC_PDM_CLK     42
#define MIC_PDM_DATA    41

#define MIC_MAX_BYTES      (MIC_SAMPLE_RATE * 2 * 10)   // 320 KB — needs PSRAM
#define MIC_FALLBACK_BYTES (MIC_SAMPLE_RATE * 2 * 3)    //  96 KB — internal RAM

static uint8_t* _micBuf    = nullptr;
static size_t   _micMaxLen = 0;

// ── IDF 5.x: raw new channel API (driver/i2s_pdm.h) ─────────────────────────
// Do NOT use ESP_I2S.h — it redefines i2s_mode_t and conflicts with
// driver/i2s.h (legacy) included by audio_handler.h.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/i2s_pdm.h"  // i2s_new_channel, i2s_channel_init_pdm_rx_mode, etc.

static i2s_chan_handle_t _mic_rx_chan = nullptr;

static bool _micInstallPDM() {
    // Use I2S_CHANNEL_DEFAULT_CONFIG macro (flat struct — C++17 compatible).
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, NULL, &_mic_rx_chan) != ESP_OK) {
        Serial.println(F("Mic: new channel failed")); return false;
    }

    // Build PDM config field-by-field to avoid C++17 nested designated-initializer issues.
    i2s_pdm_rx_config_t pdm_cfg = {};  // zero-init
    pdm_cfg.clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE);   // flat macro — OK
    pdm_cfg.slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    pdm_cfg.gpio_cfg.clk = (gpio_num_t)MIC_PDM_CLK;
    pdm_cfg.gpio_cfg.din = (gpio_num_t)MIC_PDM_DATA;
    // invert_flags stays zero (no clock or data inversion needed)

    if (i2s_channel_init_pdm_rx_mode(_mic_rx_chan, &pdm_cfg) != ESP_OK) {
        Serial.println(F("Mic: PDM init failed"));
        i2s_del_channel(_mic_rx_chan); _mic_rx_chan = nullptr; return false;
    }
    if (i2s_channel_enable(_mic_rx_chan) != ESP_OK) {
        Serial.println(F("Mic: channel enable failed"));
        i2s_del_channel(_mic_rx_chan); _mic_rx_chan = nullptr; return false;
    }
    return true;
}

static void _micUninstallPDM() {
    if (_mic_rx_chan) {
        i2s_channel_disable(_mic_rx_chan);
        i2s_del_channel(_mic_rx_chan);
        _mic_rx_chan = nullptr;
    }
}

static size_t _micReadChunk(uint8_t* buf, size_t size) {
    size_t got = 0;
    i2s_channel_read(_mic_rx_chan, buf, size, &got, pdMS_TO_TICKS(100));
    return got;
}

// ── IDF 4.x: legacy driver with non-blocking poll ────────────────────────────
#else
#include "driver/i2s.h"
#define MIC_I2S_PORT I2S_NUM_0

static bool _micInstallPDM() {
    i2s_config_t cfg         = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate          = MIC_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 64;
    cfg.use_apll             = false;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    if (i2s_driver_install(MIC_I2S_PORT, &cfg, 0, NULL) != ESP_OK) return false;
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = I2S_PIN_NO_CHANGE;
    pins.ws_io_num    = MIC_PDM_CLK;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = MIC_PDM_DATA;
    i2s_set_pin(MIC_I2S_PORT, &pins);
    i2s_start(MIC_I2S_PORT);
    return true;
}

static void _micUninstallPDM() {
    i2s_driver_uninstall(MIC_I2S_PORT);
}

static size_t _micReadChunk(uint8_t* buf, size_t size) {
    // Non-blocking poll — workaround for ESP32-S3 legacy PDM DMA timeout bug.
    size_t got = 0;
    for (int t = 0; t < 20 && got == 0; t++) {
        i2s_read(MIC_I2S_PORT, buf, size, &got, 0);
        if (got == 0) delay(5);
    }
    return got;
}
#endif

// ── Public API ────────────────────────────────────────────────────────────────

void micSetup() {
    _micBuf = (uint8_t*)ps_malloc(MIC_MAX_BYTES);
    if (_micBuf) {
        _micMaxLen = MIC_MAX_BYTES;
        Serial.println(F("Mic: 10s PSRAM buffer"));
    } else {
        _micBuf = (uint8_t*)malloc(MIC_FALLBACK_BYTES);
        if (_micBuf) {
            _micMaxLen = MIC_FALLBACK_BYTES;
            Serial.println(F("Mic: 3s internal RAM buffer (enable OPI PSRAM for 10s)"));
        } else {
            Serial.println(F("Mic: alloc failed — voice disabled"));
        }
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    Serial.println(F("Mic: IDF 5.x — using i2s_pdm channel API"));
#else
    Serial.println(F("Mic: IDF 4.x — using legacy PDM driver"));
#endif
}

// Record with VAD. Swaps I2S_NUM_0 speaker ↔ PDM mic.
// Returns captured byte count, or 0 if no speech within timeout.
size_t micRecordWithVAD() {
    if (!_micBuf) return 0;

    stopAudio();
    audioUninstall();
    delay(50);

    if (!_micInstallPDM()) {
        Serial.println(F("Mic: PDM install failed"));
        audioReinstall();
        return 0;
    }
    delay(100);
    dlogs("Mic: recording...");

    // VAD constants. Observed: ambient 1000–1600 RMS, clear speech 2000–4000+.
    // VAD_SPEECH: onset threshold (low enough to catch soft speech start).
    // VAD_LOUD: threshold to reset the silence timer. Must be above the ambient
    // noise ceiling (~1800 observed) so spurious spikes don't extend recording.
    const float    VAD_SPEECH        = 1600.0f;
    const float    VAD_LOUD          = 1900.0f;  // only real speech resets silence timer
    const int      SPEECH_START      = 5;        // loud chunks to confirm speech start
    const uint32_t SILENCE_END_MS    = 1800;     // ms of no VAD_LOUD chunk to end recording
    const uint32_t IDLE_TIMEOUT_MS   = 2400;     // ms to wait for speech before giving up
    const int      LEAD_IN_CHUNKS    = 8;        // chunks (~128ms) kept before speech onset

    size_t   captured       = 0;
    size_t   speechTrimByte = 0;
    bool     speechTrimSet  = false;
    int      loudChunks     = 0;
    bool     speaking       = false;
    int      totalChunks    = 0;
    uint32_t lastLoudMs     = millis();  // time of last chunk >= VAD_LOUD
    size_t   chunkSize      = 512;

    while (captured < _micMaxLen) {
        size_t want = min(chunkSize, _micMaxLen - captured);
        size_t got  = _micReadChunk(_micBuf + captured, want);

        if (got == 0) {
            totalChunks++;
            if (!speaking && (millis() - lastLoudMs) >= IDLE_TIMEOUT_MS) break;
            continue;
        }

        int16_t* s = (int16_t*)(_micBuf + captured);
        int n = (int)(got / 2);
        float sum = 0.0f;
        for (int i = 0; i < n; i++) sum += (float)s[i] * (float)s[i];
        float rms = sqrtf(sum / n);

        captured += got;
        totalChunks++;

        if (totalChunks % 30 == 1)
            dlog("Mic RMS: %.0f", rms);

        if (rms >= VAD_SPEECH) {
            loudChunks++;
            if (rms >= VAD_LOUD) lastLoudMs = millis();  // only clear speech resets timer
            if (!speaking && loudChunks >= SPEECH_START) {
                speaking = true;
                size_t keepBack = (size_t)(LEAD_IN_CHUNKS + SPEECH_START) * want;
                speechTrimByte = (captured > keepBack) ? captured - keepBack : 0;
                speechTrimSet  = true;
                dlog("Mic: speech onset, recording...");
            }
        } else {
            loudChunks = 0;
            if (!speaking && (millis() - lastLoudMs) >= IDLE_TIMEOUT_MS) break;
            if ( speaking && (millis() - lastLoudMs) >= SILENCE_END_MS)  break;
        }
    }

    _micUninstallPDM();
    audioReinstall();

    if (!speaking) return 0;

    // Shift buffer left to remove pre-speech silence before upload.
    if (speechTrimSet && speechTrimByte > 0) {
        size_t trimmed = captured - speechTrimByte;
        memmove(_micBuf, _micBuf + speechTrimByte, trimmed);
        dlog("Mic: trimmed %zu bytes lead-in, sending %zu bytes", speechTrimByte, trimmed);
        return trimmed;
    }
    return captured;
}

uint8_t* micBuffer() { return _micBuf; }
