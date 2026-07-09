#pragma once

// IP of the laptop running software/voice_service.py
// Find your IP: ipconfig getifaddr en0   (Mac)
//               hostname -I | awk '{print $1}'  (Linux)
#define VOICE_SERVER_IP   "192.168.68.54"
#define AUDIO_RX_PORT     8889
