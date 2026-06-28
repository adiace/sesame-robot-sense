#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include "driver/i2s_std.h"   // new IDF 5.x API — do NOT include driver/i2s.h (legacy conflict)
#include "pins.h"

// ── MAX98357A I2S audio driver (IDF 5.x new channel API) ─────────────────────
// Must use ONLY new API everywhere in the firmware. Mixing legacy i2s_driver_*
// with new i2s_new_channel causes an IDF component init abort before setup().
//
// audioUninstall() releases I2S_NUM_0 so mic_handler can open it as PDM RX.
// audioReinstall() recreates the TX channel after mic recording finishes.

#define AUDIO_I2S_PORT   I2S_NUM_0
#define AUDIO_PUMP_BYTES 256

// Master volume: 0.0–1.0. Reduce if audio is distorted/clipping.
// MAX98357A gain is 12 dB (GAIN pin floating) or 15 dB (GAIN→VDD) — both are high,
// so full-scale PCM overdives the amp. 0.35 = clear voice at normal speaking distance.
#define AUDIO_VOLUME     0.35f

static File              _audioFile;
static bool              _audioPlaying  = false;
static uint32_t          _audioLeft     = 0;
static i2s_chan_handle_t _audio_tx_chan = nullptr;

// ── WAV parser ────────────────────────────────────────────────────────────────
static uint32_t _parseWAV(File &f, uint32_t &dataSize) {
    char id[4];
    if (f.read((uint8_t*)id, 4) != 4) { Serial.println(F("WAV: short read")); return 0; }
    if (memcmp(id, "RIFF", 4))         { Serial.println(F("WAV: no RIFF"));   return 0; }
    f.seek(8);
    if (f.read((uint8_t*)id, 4) != 4) { Serial.println(F("WAV: seek fail")); return 0; }
    if (memcmp(id, "WAVE", 4))         { Serial.println(F("WAV: no WAVE"));   return 0; }
    uint32_t sampleRate = 0; dataSize = 0;
    while (f.available() >= 8) {
        char tag[4]; uint32_t sz = 0;
        if (f.read((uint8_t*)tag, 4) != 4) break;
        if (f.read((uint8_t*)&sz,  4) != 4) break;
        uint32_t chunkStart = f.position();
        if (!memcmp(tag, "fmt ", 4) && sz >= 16) {
            uint8_t fmt[16]; f.read(fmt, 16);
            sampleRate = (uint32_t)fmt[4] | ((uint32_t)fmt[5]<<8)
                       | ((uint32_t)fmt[6]<<16) | ((uint32_t)fmt[7]<<24);
        } else if (!memcmp(tag, "data", 4)) {
            dataSize = sz; return sampleRate;
        }
        if (!f.seek(chunkStart + sz + (sz & 1))) break;
    }
    Serial.println(F("WAV: no data chunk")); return 0;
}

// ── Channel open / close ──────────────────────────────────────────────────────

static bool _audioInstallSpeaker() {
    // Build channel config with plain assignment — no designated initializer issues.
    i2s_chan_config_t chan_cfg;
    memset(&chan_cfg, 0, sizeof(chan_cfg));
    chan_cfg.id           = (i2s_port_t)AUDIO_I2S_PORT;
    chan_cfg.role         = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear   = true;   // alias for auto_clear_after_cb; TX sends zeros when empty

    if (i2s_new_channel(&chan_cfg, &_audio_tx_chan, NULL) != ESP_OK) {
        Serial.println(F("Audio: channel alloc failed")); return false;
    }

    // Slot config: Philips stereo 16-bit.
    // I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG is a flat-struct macro — safe in gnu++17.
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);

    // Clock config: flat-struct macro, safe in gnu++17.
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);

    // GPIO config: build manually so we never write nested designated initializers
    // (invert_flags is an anonymous struct inside gpio_config — nested init is C++20 only).
    i2s_std_gpio_config_t gpio_cfg;
    memset(&gpio_cfg, 0, sizeof(gpio_cfg));
    gpio_cfg.mclk = GPIO_NUM_NC;
    gpio_cfg.bclk = (gpio_num_t)I2S_BCLK_PIN;
    gpio_cfg.ws   = (gpio_num_t)I2S_LRCLK_PIN;
    gpio_cfg.dout = (gpio_num_t)I2S_DIN_PIN;
    gpio_cfg.din  = GPIO_NUM_NC;
    // invert_flags bitfields stay 0 (memset) — no signal inversions needed

    i2s_std_config_t std_cfg;
    std_cfg.clk_cfg  = clk_cfg;
    std_cfg.slot_cfg = slot_cfg;
    std_cfg.gpio_cfg = gpio_cfg;

    if (i2s_channel_init_std_mode(_audio_tx_chan, &std_cfg) != ESP_OK) {
        Serial.println(F("Audio: std mode init failed"));
        i2s_del_channel(_audio_tx_chan); _audio_tx_chan = nullptr; return false;
    }
    if (i2s_channel_enable(_audio_tx_chan) != ESP_OK) {
        Serial.println(F("Audio: enable failed"));
        i2s_del_channel(_audio_tx_chan); _audio_tx_chan = nullptr; return false;
    }
    return true;
}

// Helper: write a single pure tone of `freq` Hz for `ms` milliseconds, blocking.
static void _audioTone(float freq, int ms) {
    if (!_audio_tx_chan) return;
    const int RATE    = 16000;
    const int SAMPLES = RATE * ms / 1000;
    static uint8_t _toneBuf[16000 / 4];  // max 250ms @ 16kHz stereo 16-bit
    int n = min(SAMPLES, (int)(sizeof(_toneBuf) / 4));
    float phase = 0.0f;
    for (int i = 0; i < n; i++) {
        phase += 2.0f * M_PI * freq / RATE;
        int16_t s = (int16_t)(32000 * AUDIO_VOLUME * sinf(phase));
        _toneBuf[i*4+0] = (uint8_t)(s & 0xFF); _toneBuf[i*4+1] = (uint8_t)(s >> 8);
        _toneBuf[i*4+2] = _toneBuf[i*4+0];      _toneBuf[i*4+3] = _toneBuf[i*4+1];
    }
    size_t written = 0;
    i2s_channel_write(_audio_tx_chan, _toneBuf, n * 4, &written, pdMS_TO_TICKS(500));
}

static void _audioSilence(int ms) {
    if (!_audio_tx_chan) return;
    const int RATE    = 16000;
    int n = RATE * ms / 1000;
    static uint8_t _silBuf[16000 / 10 * 4];  // max 100ms
    n = min(n, (int)(sizeof(_silBuf) / 4));
    memset(_silBuf, 0, n * 4);
    size_t written = 0;
    i2s_channel_write(_audio_tx_chan, _silBuf, n * 4, &written, pdMS_TO_TICKS(200));
}

// Three ascending beeps: clearly signals "mic is now active, speak now."
void audioActivationChirp() {
    _audioTone(600, 110);
    _audioSilence(50);
    _audioTone(900, 110);
    _audioSilence(50);
    _audioTone(1200, 150);
}

// Two descending beeps: "got it, sending to server now."
// Call immediately after micRecordWithVAD() returns with speech detected.
void audioGotItChirp() {
    _audioTone(1000, 100);
    _audioSilence(40);
    _audioTone(650, 140);
}

// Release I2S_NUM_0 so mic_handler can open it as PDM RX.
void audioUninstall() {
    if (_audio_tx_chan) {
        i2s_channel_disable(_audio_tx_chan);
        i2s_del_channel(_audio_tx_chan);
        _audio_tx_chan = nullptr;
    }
}

// Restore speaker channel after mic recording.
void audioReinstall() {
    _audioInstallSpeaker();
}

// ── Internal write helpers ────────────────────────────────────────────────────

static void _audioWriteStereo(const uint8_t* data, size_t len) {
    if (!_audio_tx_chan) return;
    size_t written = 0;
    i2s_channel_write(_audio_tx_chan, data, len, &written, pdMS_TO_TICKS(20));
}

// ── Public API ────────────────────────────────────────────────────────────────

void audioSetup() {
    if (!SPIFFS.begin(true)) { Serial.println(F("Audio: SPIFFS failed")); return; }
    if (!_audioInstallSpeaker()) return;
    delay(20);

    // Startup beep: 1 kHz square wave, confirms I2S path at boot.
    {
        const int period = 16;
        uint8_t tone[512];
        for (int i = 0; i < (int)sizeof(tone); i += 4) {
            int16_t s = ((i / 4) % period < period / 2) ? (int16_t)(16000 * AUDIO_VOLUME) : (int16_t)(-16000 * AUDIO_VOLUME);
            tone[i+0] = (uint8_t)(s & 0xFF); tone[i+1] = (uint8_t)(s >> 8);
            tone[i+2] = (uint8_t)(s & 0xFF); tone[i+3] = (uint8_t)(s >> 8);
        }
        for (int rep = 0; rep < 16; rep++) _audioWriteStereo(tone, sizeof(tone));
        delay(200);
    }
    Serial.println(F("Audio: ready"));
}

bool isAudioPlaying() { return _audioPlaying; }

void stopAudio() {
    if (_audioPlaying) {
        _audioFile.close();
        _audioPlaying = false;
        _audioLeft    = 0;
    }
    // auto_clear=true drains DMA with zeros automatically; nothing explicit needed.
}

void playWavFromSPIFFS(const char* path) {
    stopAudio();
    _audioFile = SPIFFS.open(path, "r");
    if (!_audioFile) { Serial.printf("Audio: open failed %s\n", path); return; }
    uint32_t dataSize = 0;
    uint32_t rate     = _parseWAV(_audioFile, dataSize);
    if (!rate || !dataSize) {
        Serial.printf("Audio: bad WAV %s\n", path); _audioFile.close(); return;
    }
    if (rate != 16000 && _audio_tx_chan) {
        i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
        i2s_channel_disable(_audio_tx_chan);
        i2s_channel_reconfig_std_clock(_audio_tx_chan, &clk);
        i2s_channel_enable(_audio_tx_chan);
    }
    _audioLeft    = dataSize;
    _audioPlaying = true;
    Serial.printf("Audio: playing %s  rate=%u  bytes=%u\n", path, rate, dataSize);
}

void audioPump() {
    if (!_audioPlaying || !_audio_tx_chan) return;
    if (_audioLeft == 0) { stopAudio(); return; }
    uint8_t mono[AUDIO_PUMP_BYTES];
    size_t  n   = min((uint32_t)AUDIO_PUMP_BYTES, _audioLeft);
    int     got = _audioFile.read(mono, (int)n);
    if (got <= 0) { stopAudio(); return; }
    uint8_t stereo[AUDIO_PUMP_BYTES * 2];
    for (int i = 0; i < got; i += 2) {
        int16_t s = (int16_t)((uint16_t)mono[i] | ((uint16_t)mono[i+1] << 8));
        s = (int16_t)(s * AUDIO_VOLUME);
        stereo[i*2+0] = (uint8_t)((uint16_t)s & 0xFF);
        stereo[i*2+1] = (uint8_t)((uint16_t)s >> 8);
        stereo[i*2+2] = stereo[i*2+0];   // R = L
        stereo[i*2+3] = stereo[i*2+1];
    }
    _audioWriteStereo(stereo, (size_t)got * 2);
    _audioLeft -= (uint32_t)got;
}
