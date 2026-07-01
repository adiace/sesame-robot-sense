#pragma once

// ── Voice assistant server ────────────────────────────────────────────────────
// 1. Run: bash software/setup_voice.sh
// 2. Run: python software/voice_service.py
//    It will print your laptop's IP on startup.
// 3. Set VOICE_SERVER_IP below to that IP, then flash.
//
//    macOS quick IP check: ipconfig getifaddr en0

#define VOICE_SERVER_IP    "YOUR_LAPTOP_IP"   // ← set to your laptop's IP (ipconfig getifaddr en0)
#define AUDIO_RX_PORT      8889              // audio_receiver.py — receives 4s PCM clip

