#pragma once

#include <Arduino.h>
#include "esp_idf_version.h"
#include "wifi_log.h"

// ── PDM mic + ESP-SR WakeNet on-device wake word detection ───────────────────
// Architecture:
//   - I2S_NUM_0 PDM stays open permanently (speaker on I2S_NUM_1 is independent)
//   - _micWakeTask (Core 1) feeds 480-sample chunks to WakeNet continuously
//   - On "Hey Willow" detection: task self-suspends, sets _micWakeDetected
//   - Main loop: play ding, call micRecord4s(), stream PCM to audio_receiver.py
//   - micWakeClear(): resume monitoring with 2s cooldown
//
// Wake model: wn9_hiesp  ("Hi ESP" — the only model bundled with arduino-esp32 3.x)
// Custom "Hey Sesame" model: train at https://wake-word.espressif.com
//
// Mic pins (XIAO ESP32-S3 Sense expansion board schematic):
//   CLK → GPIO42,  DATA → GPIO41
//
// Requires 'model' partition in partitions.csv flashed with the model binary.
// See firmware/partitions.csv and SETUP.md for flashing instructions.

#define MIC_SAMPLE_RATE   16000
#define MIC_PDM_CLK       42
#define MIC_PDM_DATA      41
#define MIC_RECORD_BYTES  (MIC_SAMPLE_RATE * 2 * 4)    // 4s fixed = 128KB
#define MIC_PSRAM_BYTES   (MIC_SAMPLE_RATE * 2 * 10)   // 10s = 320KB (PSRAM)

#define WAKE_MODEL_NAME   "wn9_hiesp"

static uint8_t*      _micBuf           = nullptr;
static size_t        _micMaxLen        = 0;
static bool          _micReady         = false;

static volatile bool     _micWakeDetected  = false;
static volatile uint32_t _micWakeCooldown  = 0;
static TaskHandle_t      _micWakeTaskHandle = nullptr;

// ── PDM driver ────────────────────────────────────────────────────────────────
// dma_frame_num=480: each DMA descriptor holds exactly 480 samples (960 bytes),
// matching WakeNet's required chunk size so reads align with the model's window.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "driver/i2s_pdm.h"
static i2s_chan_handle_t _mic_rx_chan = nullptr;

static bool _micOpenPDM() {
    if (_mic_rx_chan) return true;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)I2S_NUM_0, I2S_ROLE_MASTER);
    cc.auto_clear    = true;
    cc.dma_desc_num  = 4;     // 4 × 480 samples ≈ 120ms ring buffer
    cc.dma_frame_num = 480;   // one WakeNet chunk per DMA descriptor
    if (i2s_new_channel(&cc, NULL, &_mic_rx_chan) != ESP_OK) {
        Serial.println(F("Mic: channel alloc failed")); return false;
    }
    i2s_pdm_rx_config_t pdm = {};
    pdm.clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE);
    pdm.slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    pdm.gpio_cfg.clk = (gpio_num_t)MIC_PDM_CLK;
    pdm.gpio_cfg.din = (gpio_num_t)MIC_PDM_DATA;
    if (i2s_channel_init_pdm_rx_mode(_mic_rx_chan, &pdm) != ESP_OK) {
        Serial.println(F("Mic: PDM init failed"));
        i2s_del_channel(_mic_rx_chan); _mic_rx_chan = nullptr; return false;
    }
    if (i2s_channel_enable(_mic_rx_chan) != ESP_OK) {
        Serial.println(F("Mic: enable failed"));
        i2s_del_channel(_mic_rx_chan); _mic_rx_chan = nullptr; return false;
    }
    return true;
}
static size_t _micReadChunk(uint8_t* buf, size_t size) {
    if (!_mic_rx_chan) return 0;
    size_t got = 0;
    i2s_channel_read(_mic_rx_chan, buf, size, &got, pdMS_TO_TICKS(100));
    return got;
}

#else  // IDF 4.x legacy path
#include "driver/i2s.h"
#define MIC_I2S_PORT I2S_NUM_0
static bool _micOpenPDM() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate          = MIC_SAMPLE_RATE;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ONLY_RIGHT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = 480;
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
static size_t _micReadChunk(uint8_t* buf, size_t size) {
    size_t got = 0;
    for (int t = 0; t < 20 && got == 0; t++) {
        i2s_read(MIC_I2S_PORT, buf, size, &got, 0);
        if (got == 0) delay(5);
    }
    return got;
}
#endif

// ── ESP-SR WakeNet ────────────────────────────────────────────────────────────
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"

static const esp_wn_iface_t *_wakenet  = nullptr;
static model_iface_data_t   *_wn_model = nullptr;
static int                   _wn_chunk = 0;   // samples per WakeNet window

static bool _wakeNetInit() {
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        Serial.println(F("WakeNet: 'model' partition not found"));
        Serial.println(F("  → add partitions.csv + flash model binary (see CLAUDE.md)"));
        return false;
    }
    char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "hiesp");
    if (!wn_name) {
        Serial.println(F("WakeNet: heywillow not found in model partition"));
        return false;
    }
    _wakenet = esp_wn_handle_from_name(wn_name);
    if (!_wakenet) { Serial.println(F("WakeNet: handle failed")); return false; }
    _wn_model = _wakenet->create(wn_name, DET_MODE_90);
    if (!_wn_model) { Serial.println(F("WakeNet: model create failed")); return false; }
    _wn_chunk = _wakenet->get_samp_chunksize(_wn_model);
    Serial.printf("WakeNet: ready  phrase='Hi ESP'  chunk=%d samp\n", _wn_chunk);
    return true;
}

// ── Wake monitor task ─────────────────────────────────────────────────────────
// Core 1, priority 1. Accumulates PDM samples into WakeNet-sized chunks and
// calls detect(). On WAKENET_DETECTED: sets _micWakeDetected, self-suspends.
// Main loop resumes the task (via micWakeClear()) after recording is done.

static void _micWakeTask(void* arg) {
    vTaskDelay(pdMS_TO_TICKS(3000));   // let setup() finish

    if (!_wn_chunk || !_wakenet || !_wn_model) {
        Serial.println(F("WakeNet: task aborted — model not initialised"));
        vTaskDelete(NULL); return;
    }

    int16_t *chunk = (int16_t*)malloc((size_t)_wn_chunk * 2);
    uint8_t *raw   = (uint8_t*)malloc((size_t)_wn_chunk * 2);
    if (!chunk || !raw) {
        Serial.println(F("WakeNet: buffer alloc failed"));
        free(chunk); free(raw); vTaskDelete(NULL); return;
    }

    int collected = 0;

    for (;;) {
        if (millis() < _micWakeCooldown) { taskYIELD(); continue; }

        size_t got = _micReadChunk(raw, (size_t)_wn_chunk * 2);
        if (got >= 2) {
            int n      = (int)(got / 2);
            int needed = _wn_chunk - collected;
            int take   = (n < needed) ? n : needed;
            memcpy(chunk + collected, raw, (size_t)take * 2);
            collected += take;

            if (collected >= _wn_chunk) {
                wakenet_state_t st = _wakenet->detect(_wn_model, chunk);
                if (st == WAKENET_DETECTED && !_micWakeDetected) {
                    _micWakeDetected = true;
                    collected = 0;
                    vTaskSuspend(NULL);
                }
                int leftover = n - take;
                if (leftover > 0)
                    memcpy(chunk, (int16_t*)raw + take, (size_t)leftover * 2);
                collected = leftover > 0 ? leftover : 0;
            }
        }

        taskYIELD();
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void micSetup() {
    _micBuf = (uint8_t*)ps_malloc(MIC_PSRAM_BYTES);
    if (_micBuf) {
        _micMaxLen = MIC_PSRAM_BYTES;
        Serial.println(F("Mic: 10s PSRAM buffer"));
    } else {
        _micBuf = (uint8_t*)malloc(MIC_RECORD_BYTES);
        if (_micBuf) {
            _micMaxLen = MIC_RECORD_BYTES;
            Serial.println(F("Mic: 4s internal RAM buffer"));
        } else {
            Serial.println(F("Mic: alloc failed — voice disabled")); return;
        }
    }
    if (!_micOpenPDM()) { Serial.println(F("Mic: PDM open failed")); return; }
    if (!_wakeNetInit()) {
        Serial.println(F("WakeNet: disabled — flash model partition to enable"));
        // PDM stays open so energy-level Serial prints still work for debugging
        return;
    }
    _micReady = true;
    xTaskCreatePinnedToCore(_micWakeTask, "micWake", 8192, nullptr, 1,
                            &_micWakeTaskHandle, 1);
    Serial.println(F("Mic: WakeNet monitor running (3s boot delay)"));
}

bool micWakeTriggered() { return _micWakeDetected; }

void micWakeClear() {
    _micWakeDetected = false;
    _micWakeCooldown = millis() + 2000;
    if (_micWakeTaskHandle) vTaskResume(_micWakeTaskHandle);
}

// Record PCM with VAD: stop when speech ends (silence after speech detected).
// Max 4 seconds. Calls micWakeClear() automatically before returning.
size_t micRecord4s() {
    if (!_micBuf) { micWakeClear(); return 0; }

    const size_t CHUNK        = 960;   // 60ms per chunk at 16kHz
    const int    SILENCE_HOLD = 12;    // 12 × 60ms = 720ms silence → stop
    const size_t MAX_BYTES    = (MIC_RECORD_BYTES < _micMaxLen)
                                ? MIC_RECORD_BYTES : _micMaxLen;

    // Calibrate noise floor from the first 6 chunks (~360ms) before speech starts.
    // Threshold = noise_floor * 2 + 300, clamped to [600, 2500].
    float noiseSum = 0.0f; int noiseChunks = 0;
    {
        uint8_t* calBuf = _micBuf;   // write into start of buffer (overwritten by real record)
        while (noiseChunks < 6) {
            size_t got = _micReadChunk(calBuf, CHUNK);
            if (got < 2) continue;
            int16_t* s = (int16_t*)calBuf;
            int n = (int)(got / 2);
            float sum = 0.0f;
            for (int i = 0; i < n; i++) sum += (float)s[i] * s[i];
            noiseSum += sqrtf(sum / n);
            noiseChunks++;
        }
    }
    float noiseFloor  = noiseSum / noiseChunks;
    float speechThresh = constrain(noiseFloor * 2.0f + 300.0f, 600.0f, 2500.0f);
    dlog("Mic: noise=%.0f thresh=%.0f", noiseFloor, speechThresh);

    size_t captured   = 0;
    int    silenceRun = 0;
    bool   speechSeen = false;

    while (captured < MAX_BYTES) {
        size_t want = (CHUNK < MAX_BYTES - captured) ? CHUNK : MAX_BYTES - captured;
        size_t got  = _micReadChunk(_micBuf + captured, want);
        if (got < 2) continue;

        int16_t* s = (int16_t*)(_micBuf + captured);
        int n = (int)(got / 2);
        float sum = 0.0f;
        for (int i = 0; i < n; i++) sum += (float)s[i] * s[i];
        float rms = sqrtf(sum / n);
        captured += got;

        if (rms >= speechThresh) {
            speechSeen = true;
            silenceRun = 0;
        } else if (speechSeen) {
            if (++silenceRun >= SILENCE_HOLD) break;
        }
    }
    dlog("Mic: recorded %zu bytes (%.1fs)", captured, captured / 32000.0f);
    micWakeClear();
    return captured;
}

uint8_t* micBuffer() { return _micBuf; }
