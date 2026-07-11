#pragma once

// IP of the laptop running software/voice_service.py
// Find your IP: ipconfig getifaddr en0   (Mac)
//               hostname -I | awk '{print $1}'  (Linux)
#ifndef VOICE_SERVER_IP
#define VOICE_SERVER_IP   "192.168.1.100"   // ← override in wifi_credentials.h
#endif
#ifndef AUDIO_RX_PORT
#define AUDIO_RX_PORT     8889
#endif
