# Voice Pipeline — internals, tuning, debugging

## The pipeline

```
 ┌────────────────────────── robot (ESP32-S3) ──────────────────────────┐
 │                                                                      │
 │  INMP441 mic ──► wake feed loop ──► WakeNet "Hi ESP" (on-device)     │
 │                       │ ambient-level EMA        │ detected          │
 │                       ▼                          ▼                   │
 │                  noise floor              beep + record with VAD     │
 │                                            (stops ~0.8s after you    │
 │                                             finish; max 4s)          │
 └──────────────────────────────│───────────────────────────────────────┘
                                │ raw PCM over TCP :8889
 ┌────────────────────────── laptop (companion app) ────────────────────┐
 │  faster-whisper STT ─► quick-response layer ─► LLM (Ollama, local)   │
 │        │                                            │                │
 │        └── saves clip to ~/.sesame/last_heard.wav   ▼                │
 │                                       {command, face, response}      │
 │   TTS (say) ─► WAV ──────────────────── commands over TCP :8888 ─────┼──►
 └──────────────────────────────│───────────────────────────────────────┘
                                │ WAV streamed back on the same socket
 ┌──────────────────────────────▼───────────────────────────────────────┐
 │  robot streams WAV → I2S → MAX98357A → speaker (no RAM buffering)    │
 └──────────────────────────────────────────────────────────────────────┘
```

Nothing leaves your network. Whisper, the LLM, and TTS all run on the laptop.

## Wake word (robot side)

- **Model**: ESP-SR WakeNet `wn9_hiesp` ("Hi ESP"), loaded from the `model` flash
  partition at 0xC10000. Requires PSRAM (the firmware disables wake cleanly if missing).
- **Mode**: `DET_MODE_95` (permissive). The stricter `DET_MODE_90` missed the phrase
  through the body enclosure. If you get false wakes, switch back in
  `firmware/wakeword_handler.h`.
- The feed loop drains the whole mic DMA backlog every pass — OLED face animation blocks
  the main loop long enough that a single small read would drop audio mid-phrase.
- `MIC_GAIN 4` software gain (`firmware/audio_handler.h`) compensates for the enclosed
  mic. All VAD thresholds scale with it automatically.

## Recording (VAD)

After the wake beep (quiet + faded so the speaker doesn't trigger the mic through the
body), the robot records with a voice-activity detector:

- Noise floor = rolling ambient EMA maintained by the wake loop (no calibration pause —
  a calibration pass would eat the first word of prompt speakers).
- Speech must exceed `noise × 1.5` for 3 consecutive 30 ms chunks to arm.
- You can pause up to **5 s** after the beep before speaking; leading silence is trimmed.
- Recording stops **~0.8 s** after you stop talking (max 4 s), and nothing is uploaded
  if no speech was ever detected.

## What makes recognition good or bad

1. **Mic seal** (dominant factor). See [wiring.md](wiring.md#microphone-placement).
   A leaky seal low-passes speech and deletes consonants.
2. **Whisper model** — `WHISPER_MODEL` in the companion app's `.env`: `small` is the
   tested default; `medium` is better with accents (~2–3 s slower per clip).
3. **Phrases beat single words** — "dance for me" is far more reliable than "dance"
   (more acoustic context). The app also fuzzy-rescues short mishears
   ("done?" → `dance`).
4. Speak at normal volume from ~0.5 m. Shouting into the mic hole clips the gained
   signal and makes things worse.

## Debugging tools

| Tool | What it tells you |
|---|---|
| `curl http://sesame-robot.local/api/status` | wake health: `wakeReady`, `wakeChunksFed` (~32/s = listening), `micRms`, `wakeDetections` |
| `nc sesame-robot.local 8890` | live firmware log with history replay: `[Wake]`, `[Mic] recorded … peak=… thresh=…`, `[Voice]` connect/stream events |
| `afplay ~/.sesame/last_heard.wav` (on the laptop) | **listen to exactly what the robot recorded** — instantly separates mic problems from STT problems |
| `printf 'stop\n' \| nc sesame-robot.local 8888` | unstick a wedged robot (HTTP dead but ping alive) |
| `audio_mic_test/` sketch | standalone speaker + mic hardware test, no robot logic |

### Reading `[Mic]` log lines

```
[Mic] noise=800 thresh=2000
[Mic] recorded 44160 bytes (1.4s) peak=9500 thresh=2000
```

- `peak` well above `thresh` → healthy speech capture.
- `peak` barely above `thresh` → too quiet/far, or mic muffled.
- `(no speech detected)` suffix → VAD never armed; nothing was sent.
- Recording always hitting 4.0 s → silence isn't being detected; check ambient noise
  and the seal.

### Spectrum check for the mic seal

Record any command, then on the laptop:

```python
import wave, numpy as np
w = wave.open("~/.sesame/last_heard.wav".replace("~", __import__("os").path.expanduser("~")))
x = np.frombuffer(w.readframes(w.getnframes()), np.int16).astype(np.float32)
s = np.abs(np.fft.rfft(x * np.hanning(len(x))))**2
f = np.fft.rfftfreq(len(x), 1/16000)
hi = s[(f>4000)].sum() / s[(f>50)].sum()
print(f"energy above 4kHz: {100*hi:.1f}%  (well-sealed mic: >10%; muffled: <5%)")
```
