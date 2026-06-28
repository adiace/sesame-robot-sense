# Software Guide

Python host tools for controlling the robot from your Mac or Linux machine. All four tools live in the `software/` directory and share a single transport layer (`robot_link.py`). They connect over WiFi by default — the robot must be on the same network and reachable at `quadruped.local:8888`.

---

## Installation

### Python version

Python 3.8 or newer. Check with:

```bash
python3 --version
```

### Required packages

```bash
pip install pyserial vosk sounddevice
```

| Package | Used by | Purpose |
|---|---|---|
| `pyserial` | all tools | USB serial fallback transport |
| `vosk` | `voice_control.py`, `robot_gui.py` | Offline speech recognition |
| `sounddevice` | `voice_control.py`, `robot_gui.py` | Microphone input |

`tkinter` is also required by `robot_gui.py`. It ships with Python on macOS. On Linux, install it separately:

```bash
sudo apt install python3-tk   # Debian/Ubuntu
```

### Vosk language model

Voice recognition runs entirely offline using the Vosk small English model (~50 MB). You only need this if you plan to use voice control.

1. Download the model from https://alphacephei.com/vosk/models — look for `vosk-model-small-en-us-*`
2. Unzip it and rename the folder to `vosk-model`
3. Place it inside the `software/` directory:

```
software/
├── vosk-model/          ← here
│   ├── am/
│   ├── conf/
│   └── ...
├── voice_control.py
└── ...
```

The tools expect the model at this exact path relative to their own location. Voice features will fail to start with a clear error if the folder is missing.

---

## Connecting to the robot

All tools default to WiFi. They resolve the robot via mDNS at `quadruped.local:8888` and fall back to USB serial automatically if WiFi isn't available.

**Prerequisites for WiFi:**
- Robot is flashed and running (see [Setup & Calibration](calibration.md))
- Robot and your computer are on the same WiFi network
- `NETWORK_SSID` / `NETWORK_PASS` are set correctly in the firmware

**USB serial fallback:**
- Connect the robot via USB-C
- The tool will find the port automatically on macOS (`/dev/cu.usbmodem*`)

**Connection flags** (available on all tools):

| Flag | Behaviour |
|---|---|
| *(none)* | Try `quadruped.local:8888`, fall back to USB serial |
| `--net` | WiFi only, no serial fallback |
| `--serial` | USB serial only, no WiFi attempt |
| `--host <address>` | Use a specific hostname or IP instead of `quadruped.local` |

---

## `robot.py` — command-line bridge

Send a single command and print the reply, or run a timed sequence from stdin.

### Single command

```bash
cd software
python3 robot.py "stand"
python3 robot.py "forward"
python3 robot.py "servo 2 110"
python3 robot.py "pose"
```

### Force a transport

```bash
python3 robot.py --serial "stand"              # USB serial only
python3 robot.py --net "stand"                 # WiFi only
python3 robot.py --host 192.168.1.42 "pose"   # connect by IP
```

### Timed sequence (`--seq`)

Reads commands from stdin, one per line. A line is either a firmware command (sent verbatim) or `wait <seconds>` (pause). Blank lines and `# comments` are ignored.

```bash
python3 robot.py --seq <<'EOF'
stand
wait 1
forward
wait 3
left
wait 2
stop
EOF
```

Sequences let you script choreography without keeping a connection open between commands.

---

## `robot_gui.py` — desktop control panel

A Tkinter window with movement buttons, a text command entry, a live robot monitor, and a side-view animation.

```bash
cd software
python3 robot_gui.py
```

### Connecting

Use the **Transport** dropdown and **Host** field at the top of the window, then click **Connect**. The robot sends `stand` automatically on a successful connection.

| Transport option | Behaviour |
|---|---|
| Auto | Try WiFi, fall back to serial |
| WiFi | WiFi only |
| Serial | USB serial only |

### Movement buttons

**Movement** section: `Stand`, `Rest`, `Forward`, `Backward`, `Left`, `Right`

**Moves** section: `Circle`, `Spin`, `Dance`, `Wave`, `Bow`, `Wiggle`, `Pushup`, `Wander`

The large **■ STOP** button sends `stop` immediately and interrupts any running motion.

### Raw command entry

Type any firmware command in the text box and press **Enter** or **Send**. The command is sent directly to the robot. See the [Command Reference](commands.md) for the full vocabulary.

### Robot monitor

The right panel shows everything the robot sends back — command echoes, pose data, error messages, and calibration output. Useful for watching calibration feedback. Click **Clear** to wipe it.

### Animation

The centre panel shows a side-view animation that tracks the current action — walking, turning, waving, etc. It updates automatically when you send commands.

### Voice input toggle

Check **🎤 Voice input** to enable the microphone. The GUI reuses the same Vosk recognition engine as `voice_control.py`. Say any voice trigger phrase from `moves.json` to fire the corresponding action, or say "stop" to halt. Requires `vosk` and `sounddevice` installed and the Vosk model in place.

---

## `voice_control.py` — always-on voice control

Keeps the robot link open and listens continuously for spoken commands. No cloud, no API key — everything runs locally via Vosk.

```bash
cd software
python3 voice_control.py
```

### Flags

```bash
python3 voice_control.py                    # WiFi then serial, listen continuously
python3 voice_control.py --serial           # force USB serial
python3 voice_control.py --net              # force WiFi
python3 voice_control.py --host 1.2.3.4    # connect by IP
python3 voice_control.py --list             # list available mics and serial ports
python3 voice_control.py --once "wave"      # fire one action by text, no mic
```

### Usage

Once connected the tool prints the vocabulary and listens indefinitely. Say a trigger phrase from `moves.json` and the corresponding steps are sent to the robot.

**Stop reflex:** saying "stop", "halt", or "freeze" interrupts instantly — even on a partial recognition result, even mid-routine. This is always active.

Press **Ctrl-C** to quit. The tool sends `stop` to the robot before closing.

### Choosing a microphone

If recognition is poor or the wrong device is selected:

```bash
python3 voice_control.py --list
```

This prints all available audio input devices with their index numbers. To use a specific device, set the `sounddevice` default in your environment, or pass the device index by editing the `sd.RawInputStream` call in `voice_control.py`.

---

## `moves.json` — action library

Defines every named action that `voice_control.py` and `robot_gui.py` recognise. Editing this file is how you add new moves — no code changes required.

### Structure

```json
{
  "actions": [
    {
      "name": "wave",
      "triggers": ["wave", "say hi", "hello"],
      "steps": [
        { "cmd": "wave" }
      ]
    },
    {
      "name": "circle",
      "triggers": ["circle", "walk in a circle"],
      "steps": [
        { "cmd": "left" },
        { "wait": 7 },
        { "cmd": "stop" }
      ]
    }
  ]
}
```

Each action has:

| Field | Type | Description |
|---|---|---|
| `name` | string | Unique identifier, shown in logs and the GUI |
| `triggers` | array of strings | Any matching phrase fires this action; longest match wins |
| `steps` | array | Ordered list of `{"cmd": "..."}` or `{"wait": seconds}` |
| `priority` | bool (optional) | If `true`, this action interrupts the current one instantly (used for `stop`) |

### Step types

```json
{ "cmd": "forward" }      sends a firmware command
{ "wait": 2.5 }           pauses for 2.5 seconds
```

### Adding a new move

Append an entry to the `"actions"` array. New trigger words are picked up the next time the tool starts — no restart needed if you use `--once`.

```json
{
  "name": "patrol",
  "triggers": ["patrol", "guard", "keep watch"],
  "steps": [
    { "cmd": "forward" }, { "wait": 2 },
    { "cmd": "left" },    { "wait": 1 },
    { "cmd": "forward" }, { "wait": 2 },
    { "cmd": "right" },   { "wait": 1 },
    { "cmd": "stop" }
  ]
}
```

### Notes

The `circle`, `figure eight`, `spin`, and `wander` routines use discrete `left`/`right` turns. The Sesame step sequencer does not have a sine-gait engine, so these are sharp turns rather than smooth arcs.

---

## `robot_link.py` — transport API

Not run directly. Import it to build your own scripts:

```python
import robot_link

# Auto-connect (WiFi then serial)
link = robot_link.connect()

# Send a command
link.send("forward")

# Read whatever the robot echoes back
reply = link.read_reply(timeout=0.4)
print(reply)

# Discard buffered input before sending a new command
link.drain()

# Close when done
link.close()
```

### `connect()` parameters

```python
robot_link.connect(
    prefer="net",              # "net", "serial", "net-only", "serial-only"
    host="quadruped.local",    # hostname or IP
    tcp_port=8888,
    serial_port=None,          # auto-detect if None
    verbose=True               # print "Connected via ..." on success
)
```

Returns a `TcpLink` or `SerialLink` object; both expose the same `.send()` / `.read_reply()` / `.drain()` / `.close()` / `.describe()` interface.

---

## Troubleshooting

**`quadruped.local` doesn't resolve**
- Confirm the robot serial monitor shows `WiFi: connected` and the mDNS line
- Try connecting by IP: `--host 192.168.x.y`
- mDNS (Bonjour) must be working on your network — it's disabled on some managed/enterprise WiFi

**No serial port found**
- On macOS: check System Settings → Privacy & Security → USB; the XIAO may need a driver
- Try `python3 voice_control.py --list` to see what ports are visible

**Voice recognition doesn't start**
- Check that `software/vosk-model/` exists and contains `am/`, `conf/`, etc.
- Run `python3 voice_control.py --list` to confirm your mic is visible
- On macOS, grant microphone permission to Terminal when prompted

**Commands fire but servos don't move**
- Confirm PCA9685 is wired correctly and V+ rail is powered from the UBEC
- Run `pose` to check the robot is reporting angles
- Run `servo 0 90` directly to test a single channel
