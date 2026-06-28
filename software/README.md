# Software

Python host tools for controlling the robot from a Mac or Linux machine over WiFi (or USB serial as fallback). All tools connect to `quadruped.local:8888` by default — no configuration needed as long as the robot is on the same network.

## Setup

See the full **[Software Guide](../docs/software.md)** for step-by-step installation, voice model setup, and troubleshooting.

```bash
pip install pyserial vosk sounddevice
```

For voice control, download the [Vosk small English model](https://alphacephei.com/vosk/models) and place it at `software/vosk-model/`.

---

## Tools

### `robot_link.py` — transport layer

Shared by all other tools. Tries WiFi first (`quadruped.local:8888`), falls back to USB serial automatically.

Not run directly. Import it in your own scripts:

```python
import robot_link
link = robot_link.connect()
link.send("forward")
link.close()
```

### `robot.py` — command-line bridge

Send single commands or timed sequences from a terminal.

```bash
python3 robot.py "forward"                  # single command
python3 robot.py "gait -0.4 -1"            # arc gait
python3 robot.py --serial "stand"          # force USB serial
python3 robot.py --host 192.168.1.42 "pose"

python3 robot.py --seq <<'EOF'             # timed sequence
stand
wait 1
forward
wait 3
stop
EOF
```

### `robot_gui.py` — desktop control panel

Tkinter GUI with movement buttons, a raw command entry, live robot monitor, a side-view animation, and an optional voice-input toggle.

```bash
python3 robot_gui.py
```

Use the **Transport** picker (Auto / WiFi / Serial) and the Host field to configure the connection. Connects and sends `stand` automatically on successful link.

### `voice_control.py` — always-on voice control

Offline voice recognition via [Vosk](https://alphacephei.com/vosk/). Listens continuously, matches spoken phrases to actions defined in `moves.json`, and sends the corresponding firmware commands over the open link.

```bash
python3 voice_control.py                   # WiFi, then serial
python3 voice_control.py --serial          # force USB serial
python3 voice_control.py --list            # list mic + serial devices
python3 voice_control.py --once "wave"     # fire one action without the mic
```

Say **"stop"**, **"halt"**, or **"freeze"** at any time — it interrupts instantly, even mid-routine.

### `moves.json` — action library

Defines the named moves that `voice_control.py` and `robot_gui.py` recognize. Each entry has trigger phrases and a list of steps (`{"cmd": "..."}` or `{"wait": seconds}`).

To add a new move, append an entry — new trigger words are picked up on next launch. Continuous gaits run until `stop`; routines self-stop by ending with a `stop` or `stand` step.

> **Note:** The `circle`, `figure eight`, `spin`, and `wander` routines use discrete `left`/`right` turns rather than smooth arcs — the Sesame step sequencer does not have a sine-gait engine.

---

## Connection modes

| Flag | Behaviour |
|---|---|
| *(default)* | Try `quadruped.local:8888` via WiFi, fall back to USB serial |
| `--net` | WiFi only (no serial fallback) |
| `--serial` | USB serial only |
| `--host <ip>` | Connect to a specific IP instead of `quadruped.local` |
