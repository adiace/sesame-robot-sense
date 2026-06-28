#pragma once

// ── Voice assistant server ────────────────────────────────────────────────────
// 1. Run: bash software/setup_voice.sh
// 2. Run: python software/voice_service.py
//    It will print your laptop's IP on startup.
// 3. Set VOICE_SERVER_IP below to that IP, then flash.
//
//    macOS quick IP check: ipconfig getifaddr en0

#define VOICE_SERVER_IP    "192.168.68.57"   // ← set to your laptop's IP
#define VOICE_SERVER_PORT  5005

