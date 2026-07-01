# Software Guide

All companion software has moved to a dedicated repository:

**[sesame-companion-app-sense](https://github.com/adiace/sesame-companion-app-sense)**

Clone it alongside this repo:

```bash
cd ~/Documents
git clone https://github.com/adiace/sesame-companion-app-sense.git
```

---

## Quick start

```bash
cd sesame-companion-app-sense
./run.sh          # creates venv, installs deps, launches GUI on first run
```

On first run `run.sh` creates `.env` from the template. Set `SESAME_ROBOT_IP` to the robot's address (`quadruped.local` or an IP) and `LOCAL_LLM_MODEL` to your Ollama model (default: `llama3.2`).

---

## What's in the companion app

| File | Purpose |
|---|---|
| `sesame_companion.py` | Core library — LLM, STT, TTS, robot HTTP controller, robot voice receiver, IMU tracker |
| `sesame_gui.py` | Tkinter GUI — chat, quick actions, settings |
| `robot_link.py` | Low-level TCP + serial transport (debugging / scripted testing) |
| `robot.py` | CLI bridge — send single commands or timed sequences |
| `imu_receiver.py` | Print IMU events from the robot in real time |
| `serial_monitor.py` | Stream the robot's debug log (TCP port 8890) to the terminal |
| `moves.json` | Named action library with trigger phrases and step sequences |
| `run.sh` | Launch script — venv setup + `sesame_gui.py` |
| `.env.example` | Config template (copy to `.env`) |

---

## Architecture

The companion app is the sole software interface to the robot. It communicates
with the firmware over two channels:

| Channel | Direction | Purpose |
|---|---|---|
| TCP :8888 | laptop → robot | Commands and face changes (persistent, newline-framed) |
| HTTP `GET /api/status` | laptop → robot | Poll current face and command |
| TCP port 8889 | robot → laptop | Robot streams 4-second PCM clips after on-device wake word |
| TCP port 8890 | robot → laptop | Robot pushes debug log lines (serial monitor, IMU events) |

### Voice flow (on-device wake word)

1. Robot detects "Hi ESP" via ESP-SR WakeNet (on-device, no streaming)
2. Robot records 4 s of audio and sends it to the companion app on port 8889
3. `RobotVoiceReceiver` transcribes with `faster_whisper` (local STT)
4. Sends transcript to Ollama → gets `{command, face, response}`
5. Generates TTS WAV with macOS `say` + `afconvert` (16 kHz mono 16-bit)
6. Sends WAV back to robot on the same TCP connection → robot plays it directly from PSRAM on its speaker
7. Sends `{command, face}` to robot via TCP :8888
8. Conversation appears in the GUI chat log

### Laptop voice mode (GUI)

If **Voice Mode** is enabled in the GUI the laptop mic is also active. Speech is captured by `SpeechRecognition` and transcribed locally by `faster_whisper`. The LLM response is spoken by macOS `say` on the laptop speaker and the robot's face animates in sync.

Both modes run simultaneously — they share the same `LocalLLMInterface` and `SesameRobotController`.

---

## `robot.py` — CLI bridge

Send a single command or run a timed sequence. Connects over WiFi by default.

```bash
cd sesame-companion-app-sense
source .venv/bin/activate

python3 robot.py "stand"
python3 robot.py "forward"
python3 robot.py --host 192.168.68.100 "pose"

# Timed sequence
python3 robot.py --seq <<'EOF'
stand
wait 1
forward
wait 3
stop
EOF
```

---

## `imu_receiver.py` — IMU event monitor

Prints IMU events (pickup, flip, tap, freefall) as they arrive from the robot.

```bash
source .venv/bin/activate
python3 imu_receiver.py
python3 imu_receiver.py --host 192.168.68.100
```

---

## `serial_monitor.py` — debug log stream

Mirrors the robot's `wifi_log.h` debug output (same content as Arduino Serial Monitor, over WiFi).

```bash
source .venv/bin/activate
python3 serial_monitor.py
python3 serial_monitor.py 192.168.68.100   # explicit IP
```

---

## `moves.json` — named action library

Defines trigger phrases and step sequences for named actions. Used as a reference and can be integrated into custom scripts via `robot.py --seq`.

---

## Ollama setup

The companion app requires [Ollama](https://ollama.com) running locally:

```bash
brew install ollama
ollama serve         # runs in background
ollama pull llama3.2
# optional: ollama pull granite3.1-dense:8b
```

Set `LOCAL_LLM_MODEL` in `.env` to match the pulled model name.

---

## Troubleshooting

**Robot not found** — confirm `SESAME_ROBOT_IP` in `.env` matches the robot's actual address. Try `ping quadruped.local` first.

**Port 8889 in use** — another process (old `audio_receiver.py`) may be running. Kill it: `lsof -ti:8889 | xargs kill`

**Whisper model slow on first run** — `faster_whisper` downloads the `base` model (~145 MB) on first transcription. Subsequent runs use the cache.

**No audio on robot speaker** — ensure the MAX98357A Vcc is on the 3.3 V pin (not 5 V) and the robot is on battery or a clean USB power source. See [wiring.md](wiring.md).
