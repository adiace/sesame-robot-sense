#pragma once

// IP of the laptop running software/voice_service.py
// Find your IP: ipconfig getifaddr en0   (Mac)
//               hostname -I | awk '{print $1}'  (Linux)
#define VOICE_SERVER_IP   "192.168.1.100"   // ← CHANGE to your laptop IP (ipconfig getifaddr en0)
#define AUDIO_RX_PORT     8889
