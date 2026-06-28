WAV files for Sesame audio reactions.
Format: mono, 16-bit signed PCM, 16kHz sample rate.

Files needed:
  woah_flying.wav   ~1s   (PICKUP reaction)
  upside_down.wav   ~1.5s (FLIPPED reaction)
  hehe.wav          ~0.5s (TAPPED reaction)
  falling.wav       ~0.5s (FREEFALL reaction)

Source from freesound.org, then convert with ffmpeg:
  ffmpeg -i input.wav -ar 16000 -ac 1 -acodec pcm_s16le output.wav

Upload all files in this folder to the ESP32 using:
  Arduino IDE: Tools > ESP32 Sketch Data Upload
  (requires "ESP32 SPIFFS Data Upload" plugin if not already installed)
