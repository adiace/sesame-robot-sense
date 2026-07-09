#pragma once
#include <Arduino.h>
extern "C" {
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
}

// ── WakeNet "Hi ESP" detector ─────────────────────────────────────────────────
// Call wakewordSetup() once after boot. Then call wakewordFeed() with every
// mono 16-bit mic chunk — it returns true once when "Hi ESP" is detected.
//
// Before flashing firmware, write the model binary to the model partition:
//   esptool --port /dev/cu.usbmodem101 write-flash 0xC10000 \
//     ~/Library/Arduino15/packages/esp32/tools/esp32s3-libs/3.3.10/esp_sr/srmodels.bin

static srmodel_list_t*      _sr_models  = nullptr;
static const esp_wn_iface_t* _wn_iface  = nullptr;
static model_iface_data_t*   _wn_data   = nullptr;
static int                   _wn_chunk  = 0;

// Accumulation buffer — WakeNet may want a different chunk size than our mic read size
static int16_t* _wn_buf     = nullptr;
static int      _wn_buf_pos = 0;

bool wakewordSetup() {
    // WakeNet9 allocates its working memory in PSRAM and crashes (StoreProhibited,
    // NULL write) if there is none. Bail out cleanly instead.
    if (ESP.getPsramSize() == 0) {
        Serial.println("[Wake] No PSRAM — WakeNet disabled (enable OPI PSRAM in Tools menu)");
        return false;
    }

    // Mount model SPIFFS partition (label "model" from partitions.csv)
    _sr_models = esp_srmodel_init("model");
    if (!_sr_models || _sr_models->num == 0) {
        Serial.println("[Wake] No models found — flash srmodels.bin to 0xC10000");
        return false;
    }

    // Find the Hi ESP wakenet model
    char *model_name = esp_srmodel_filter(_sr_models, ESP_WN_PREFIX, "hiesp");
    if (!model_name) {
        Serial.println("[Wake] wn9_hiesp not found in model partition");
        esp_srmodel_deinit(_sr_models);
        _sr_models = nullptr;
        return false;
    }
    Serial.printf("[Wake] Loading model: %s\n", model_name);

    _wn_iface = esp_wn_handle_from_name(model_name);
    if (!_wn_iface) {
        Serial.println("[Wake] Failed to get WakeNet handle");
        return false;
    }

    _wn_data = _wn_iface->create(model_name, DET_MODE_90);
    if (!_wn_data) {
        Serial.println("[Wake] Failed to create model instance");
        return false;
    }

    _wn_chunk = _wn_iface->get_samp_chunksize(_wn_data);
    int rate  = _wn_iface->get_samp_rate(_wn_data);
    Serial.printf("[Wake] Hi ESP ready — chunk=%d samples @ %dHz\n", _wn_chunk, rate);

    _wn_buf = (int16_t*)malloc(_wn_chunk * sizeof(int16_t));
    if (!_wn_buf) {
        Serial.println("[Wake] Buffer alloc failed");
        return false;
    }
    _wn_buf_pos = 0;
    return true;
}

// Feed mono 16-bit samples (any count). Returns true when wake word fires.
bool wakewordFeed(const int16_t* samples, int n) {
    if (!_wn_iface || !_wn_data || !_wn_buf) return false;

    int i = 0;
    while (i < n) {
        // Fill internal chunk buffer
        int space = _wn_chunk - _wn_buf_pos;
        int copy  = min(n - i, space);
        memcpy(_wn_buf + _wn_buf_pos, samples + i, copy * sizeof(int16_t));
        _wn_buf_pos += copy;
        i           += copy;

        if (_wn_buf_pos == _wn_chunk) {
            _wn_buf_pos = 0;
            wakenet_state_t state = (wakenet_state_t)_wn_iface->detect(_wn_data, _wn_buf);
            if (state == WAKENET_DETECTED) {
                Serial.println("[Wake] Hi ESP detected!");
                return true;
            }
        }
    }
    return false;
}
