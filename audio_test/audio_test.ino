// audio_test.ino — plays all WAV files from SPIFFS in a loop.
// Board: XIAO ESP32-S3 Sense.  No other hardware needed.
// I2S pins match AI Pin PCB: BCLK=D6/GPIO43, LRC=D4/GPIO5, DIN=D8/GPIO7

#include <SPIFFS.h>
#include "driver/i2s.h"

#define I2S_PORT    I2S_NUM_0
#define PIN_BCLK    43   // D6
#define PIN_LRC      5   // D4
#define PIN_DIN      7   // D8

const char* WAVS[] = {
    "/hehe.wav",
    "/woah_flying.wav",
    "/upside_down.wav",
    "/falling.wav",
};
const int NUM_WAVS = sizeof(WAVS) / sizeof(WAVS[0]);

// ── WAV parser ───────────────────────────────────────────────────────────────
uint32_t parseWAV(File &f, uint32_t &dataSize) {
    char id[4];
    f.read((uint8_t*)id, 4);
    if (memcmp(id, "RIFF", 4)) { Serial.println("no RIFF"); return 0; }
    f.seek(8);
    f.read((uint8_t*)id, 4);
    if (memcmp(id, "WAVE", 4)) { Serial.println("no WAVE"); return 0; }

    uint32_t rate = 0; dataSize = 0;
    while (f.available() >= 8) {
        char tag[4]; uint32_t sz = 0;
        f.read((uint8_t*)tag, 4);
        f.read((uint8_t*)&sz, 4);
        uint32_t start = f.position();
        if (!memcmp(tag, "fmt ", 4) && sz >= 16) {
            uint8_t b[16]; f.read(b, 16);
            rate = b[4]|(b[5]<<8)|(b[6]<<16)|(b[7]<<24);
            Serial.printf("  fmt: rate=%u\n", rate);
        } else if (!memcmp(tag, "data", 4)) {
            dataSize = sz;
            return rate;
        }
        f.seek(start + sz + (sz & 1));
    }
    return 0;
}

// ── Play one WAV synchronously ────────────────────────────────────────────────
void playWAV(const char* path) {
    Serial.printf("Playing %s ...\n", path);
    File f = SPIFFS.open(path, "r");
    if (!f) { Serial.printf("  open failed: %s\n", path); return; }

    uint32_t dataSize = 0;
    uint32_t rate = parseWAV(f, dataSize);
    if (!rate || !dataSize) { Serial.println("  bad WAV"); f.close(); return; }

    i2s_set_sample_rates(I2S_PORT, rate);
    Serial.printf("  %u bytes @ %u Hz\n", dataSize, rate);

    uint8_t mono[512], stereo[1024];
    while (dataSize > 0) {
        int got = f.read(mono, (int)min((uint32_t)512, dataSize));
        if (got <= 0) break;
        for (int i = 0; i < got; i += 2) {
            stereo[i*2+0]=mono[i];   stereo[i*2+1]=mono[i+1];  // L
            stereo[i*2+2]=mono[i];   stereo[i*2+3]=mono[i+1];  // R
        }
        size_t written;
        i2s_write(I2S_PORT, stereo, (size_t)got*2, &written, portMAX_DELAY);
        dataSize -= (uint32_t)got;
    }
    i2s_zero_dma_buffer(I2S_PORT);
    f.close();
    Serial.println("  done.");
}

// ── setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== audio_test ===");

    if (!SPIFFS.begin(true)) { Serial.println("SPIFFS failed"); while(1); }

    i2s_config_t cfg         = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = 16000;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count        = 4;
    cfg.dma_buf_len          = 1024;
    cfg.use_apll             = true;   // dedicated audio PLL — less sensitive to USB noise
    cfg.tx_desc_auto_clear   = true;
    i2s_driver_install(I2S_PORT, &cfg, 0, NULL);

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = PIN_BCLK;
    pins.ws_io_num    = PIN_LRC;
    pins.data_out_num = PIN_DIN;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_PORT, &pins);
    i2s_start(I2S_PORT);
    delay(50);

    Serial.println("I2S ready. Starting playback loop...\n");
}

// ── loop ─────────────────────────────────────────────────────────────────────
void loop() {
    for (int i = 0; i < NUM_WAVS; i++) {
        playWAV(WAVS[i]);
        delay(800);   // gap between files
    }
    Serial.println("--- loop ---\n");
    delay(1000);
}
