/*
 * audio_mic_test.ino — INMP441 mic + MAX98357A speaker test
 *
 * Wiring (Sesame Distro V3 header):
 *   GPIO1  → BCLK  (INMP441 SCK  + MAX98357 BCLK)
 *   GPIO2  → WS    (INMP441 WS   + MAX98357 LRC)
 *   GPIO3  ← MIC   (INMP441 SD)
 *   GPIO14 → AMP   (MAX98357 DIN)
 *   3v3    → INMP441 VDD + MAX98357 VIN
 *   GND    → INMP441 GND + MAX98357 GND + INMP441 L/R
 *
 * Architecture:
 *   Single duplex I2S_NUM_0 MASTER — one port owns BCLK/WS, drives both
 *   speaker (DOUT→GPIO14) and mic (DIN←GPIO3) simultaneously.
 *   Stereo 16-bit slot. INMP441 drives left channel only (L/R=GND).
 *
 * Boot: two beeps = speaker ok, three beeps = clap to test mic.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include "driver/i2s_std.h"

#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASS "your-wifi-password"

#define I2S_BCLK   GPIO_NUM_1
#define I2S_WS     GPIO_NUM_2
#define I2S_MIC_SD GPIO_NUM_3
#define I2S_AMP_DI GPIO_NUM_14

#define SAMPLE_RATE      16000
#define CLAP_COOLDOWN_MS 800

static i2s_chan_handle_t _tx         = nullptr;
static i2s_chan_handle_t _rx         = nullptr;
static unsigned long     _lastClapMs = 0;
static unsigned long     _startMs    = 0;
static float             _avgRms     = 500.0f;

// ── Duplex I2S_NUM_0 — master TX + RX, stereo 16-bit ─────────────────────────
// Single port owns BCLK/WS. TX→speaker, RX←mic. No GPIO conflict.

static bool i2sSetup() {
    i2s_chan_config_t cc;
    memset(&cc, 0, sizeof(cc));
    cc.id            = I2S_NUM_0;
    cc.role          = I2S_ROLE_MASTER;
    cc.dma_desc_num  = 8;
    cc.dma_frame_num = 256;
    cc.auto_clear    = true;
    if (i2s_new_channel(&cc, &_tx, &_rx) != ESP_OK) {
        Serial.println("[I2S] channel alloc failed"); return false;
    }

    i2s_std_clk_config_t  clk  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    i2s_std_gpio_config_t gpio;
    memset(&gpio, 0, sizeof(gpio));
    gpio.mclk = GPIO_NUM_NC;
    gpio.bclk = I2S_BCLK;
    gpio.ws   = I2S_WS;
    gpio.dout = I2S_AMP_DI;
    gpio.din  = I2S_MIC_SD;

    i2s_std_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.clk_cfg  = clk;
    cfg.slot_cfg = slot;
    cfg.gpio_cfg = gpio;

    // TX init sets up clock + GPIO; RX init sets up its slot config independently
    esp_err_t e;
    e = i2s_channel_init_std_mode(_tx, &cfg);
    if (e != ESP_OK) { Serial.printf("[I2S] TX init failed: %d\n", e); return false; }
    e = i2s_channel_init_std_mode(_rx, &cfg);
    if (e != ESP_OK) { Serial.printf("[I2S] RX init failed: %d\n", e); return false; }
    // Enable RX before TX — required ordering for duplex in IDF 5.x
    e = i2s_channel_enable(_rx);
    if (e != ESP_OK) { Serial.printf("[I2S] RX enable failed: %d\n", e); return false; }
    e = i2s_channel_enable(_tx);
    if (e != ESP_OK) { Serial.printf("[I2S] TX enable failed: %d\n", e); return false; }
    return true;
}

// ── Audio helpers ─────────────────────────────────────────────────────────────

static void playBeep(float freq, int ms) {
    if (!_tx) return;
    const int n = min(SAMPLE_RATE * ms / 1000, SAMPLE_RATE / 5);
    static int16_t buf[SAMPLE_RATE / 5 * 2];
    float phase = 0;
    for (int i = 0; i < n; i++) {
        phase += 2.0f * M_PI * freq / SAMPLE_RATE;
        int16_t s = (int16_t)(12000 * sinf(phase));
        buf[i*2] = s; buf[i*2+1] = s;
    }
    size_t written = 0;
    i2s_channel_write(_tx, buf, n * 4, &written, pdMS_TO_TICKS(500));
}

// Read stereo 16-bit; INMP441 drives only the left channel (L/R=GND).
// Compute RMS over left-channel samples only (even indices).
static float micRms() {
    if (!_rx) return 0;
    static int16_t buf[512];  // 256 stereo pairs
    size_t got = 0;
    i2s_channel_read(_rx, buf, sizeof(buf), &got, pdMS_TO_TICKS(50));
    int pairs = got / 4;  // 4 bytes per stereo pair
    if (pairs == 0) return 0;
    int64_t sum = 0;
    for (int i = 0; i < pairs; i++) {
        int16_t left = buf[i * 2];  // left channel = INMP441
        sum += (int64_t)left * left;
    }
    return sqrtf((float)(sum / pairs));
}

// ── setup() ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[Boot] starting");

    if (!i2sSetup()) {
        Serial.println("[Boot] I2S setup FAILED — halting");
        while (1) delay(1000);
    }

    // Two beeps = speaker alive
    delay(300);
    playBeep(800, 150);
    delay(80);
    playBeep(1200, 200);
    Serial.println("[Boot] speaker OK");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500); Serial.print(".");
    }
    Serial.printf("\n[Boot] WiFi %s  IP %s\n",
        WiFi.status() == WL_CONNECTED ? "OK" : "FAIL",
        WiFi.localIP().toString().c_str());
    ArduinoOTA.setHostname("sesame-robot");
    ArduinoOTA.begin();

    _startMs = millis();

    // Three beeps = "clap now"
    delay(300);
    for (int i = 0; i < 3; i++) { playBeep(1000, 80); delay(80); }
    Serial.println("[Boot] ready — listening for claps");
}

// ── loop() ───────────────────────────────────────────────────────────────────

void loop() {
    ArduinoOTA.handle();

    float rms = micRms();
    unsigned long now = millis();

    // Always update the running average — calibrates to ambient during mute window too
    _avgRms = _avgRms * 0.95f + rms * 0.05f;

    // 3s mute window after boot — update avg for calibration but don't trigger
    if (now - _startMs < 3000) return;

    static unsigned long _lastPrint = 0;
    if (now - _lastPrint >= 200) {
        _lastPrint = now;
        Serial.printf("RMS: %6.0f  avg: %6.0f  thr: %6.0f\n", rms, _avgRms, _avgRms * 4.0f);
    }

    // Require both 4× relative spike AND absolute minimum of 4000
    // Real claps: 6000–30000+. Room sounds: 200–2000.
    if (rms > _avgRms * 4.0f && rms > 4000 && now - _lastClapMs > CLAP_COOLDOWN_MS) {
        _lastClapMs = now;
        Serial.printf(">>> CLAP  rms=%.0f\n", rms);
        playBeep(1200, 120);
    }
}
