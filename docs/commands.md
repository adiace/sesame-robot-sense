# Command Reference

All commands work identically over USB serial, TCP (port 8888), and the HTTP API. Commands are case-insensitive. Send `help` or `?` at any time to print a summary.

---

## Movement

Continuous gaits — loop until `stop` is sent.

| Command | Description |
|---|---|
| `forward` | Walk forward |
| `backward` | Walk backward |
| `left` | Turn left |
| `right` | Turn right |
| `stop` | Halt immediately, return to stand |

One-shot poses — execute once then return to stand.

| Command | Description |
|---|---|
| `stand` | Stand pose |
| `rest` | Rest / fold down |
| `box` | Box stance — crouched, legs tucked under frame |
| `wave` | Wave one leg |
| `dance` | Dance sequence |
| `swim` | Swimming motion |
| `point` | Point with front leg |
| `pushup` | Push-up sequence |
| `bow` | Take a bow |
| `cute` | Cute wiggle |
| `freaky` | Freaky shake |
| `worm` | Worm motion |
| `shake` | Shake |
| `shrug` | Shrug |
| `dead` | Play dead |
| `crab` | Crab walk |

`stop` also works mid-pose — it interrupts the current motion and returns to stand. The safety reflex (`gStopRequested`) means this takes effect in ≤ one frame delay (~20 ms).

---

## Direct servo control

These commands stop any running pose before moving servos.

| Command | Description |
|---|---|
| `neutral` | All 8 servos to 90° |
| `all <0-180>` | All servos to one angle |
| `servo <id> <0-180>` | One servo by index or name, e.g. `servo R1 110` or `servo 0 110` |
| `nudge <id> <delta>` | Relative move from current position, e.g. `nudge R1 -10` |
| `hips <0-180>` | All four hip servos |
| `knees <0-180>` | All four knee servos |
| `stance <hip> <knee>` | Hips and knees in one command |

`<id>` accepts either a servo name or its index 0–7:

| ID | Name | Joint |
|---|---|---|
| 0 | R1 | Right front hip |
| 1 | R2 | Right rear hip |
| 2 | L1 | Left front hip |
| 3 | L2 | Left rear hip |
| 4 | R4 | Right front knee |
| 5 | R3 | Right rear knee |
| 6 | L3 | Left front knee |
| 7 | L4 | Left rear knee |

---

## Calibration

| Command | Description |
|---|---|
| `trim <id> <-45..45>` | Apply a runtime angle offset to one servo (name or index); takes effect immediately |
| `rev <id>` | Flip a servo's direction (name or index, toggle); takes effect immediately |
| `rev` | List reversal state for all 8 servos |
| `save` | Persist current `servoTrim[]` and `servoRev[]` to NVS flash |
| `load` | Reload trims and reversal from NVS (also called on boot) |
| `clear` | Wipe NVS; reset all runtime trims and reversals to zero |

Trim stacks on top of the compile-time `servoSubtrim[]` array in source. To bake calibration permanently into the firmware:
1. Run `dump` — prints a `servoSubtrim[]` line to paste into source
2. Paste into `sesame-firmware-main.ino` and reflash
3. Run `clear` so the NVS values don't double-stack

---

## Information

| Command | Description |
|---|---|
| `map` | All servos: index, name, PCA9685 channel, effective trim, reversal state |
| `pose` | Current logical angle of each servo |
| `dump` | Print a copy-pasteable `servoSubtrim[]` line combining compile-time + runtime trims |
| `wifi` | AP IP, station IP, mDNS hostname, TCP port |
| `help` or `?` | Command summary |

---

## Face control

| Command | Description |
|---|---|
| `face <name>` | Set the OLED face without triggering any movement |

Available face names (subset): `idle`, `stand`, `rest`, `walk`, `dance`, `wave`, `point`, `pushup`, `bow`, `worm`, `shake`, `shrug`, `dead`, `crab`, `cute`, `freaky`, `swim`, `happy`, `sad`, `angry`, `surprised`, `sleepy`, `love`, `excited`, `confused`, `thinking`, `talk_happy`, `talk_sad`, `talk_angry` (and variants).

---

## Gait scaling (TCP / Python)

| Command | Description |
|---|---|
| `gait <leftScale> <rightScale>` | Continuous gait with per-side scale (−1..1) |

Scale interpretation:
- `gait -1 -1` → forward
- `gait 1 1` → backward
- `gait 1 -1` → turn left
- `gait -1 1` → turn right

> **Note:** Sesame uses a discrete step engine, not the Albert sine-gait. The `gait` command maps scales to the nearest discrete walk. Arcs and fractional values collapse to forward/back/left/right by sign; `stop` ends the gait.

---

## HTTP API (port 80)

The original Sesame web endpoints are unchanged.

### GET `/cmd`

| Parameter | Value | Effect |
|---|---|---|
| `go=<command>` | movement name | Start movement |
| `pose=<command>` | pose name | Execute pose |
| `stop=1` | — | Stop |
| `motor=<id>&value=<angle>` | id 1–8 or name, angle 0–180 | Direct servo |

### POST `/api/command`

```json
{ "command": "forward" }
{ "command": "wave", "face": "excited" }
{ "face": "happy" }
```

### GET `/api/status`

```json
{
  "currentCommand": "forward",
  "currentFace": "walk",
  "networkConnected": true,
  "apIP": "192.168.4.1",
  "networkIP": "192.168.1.42"
}
```

### GET `/getSettings` / POST `/setSettings`

```json
{ "frameDelay": 100, "walkCycles": 10, "motorCurrentDelay": 20, "faceFps": 8 }
```

---

## TCP line protocol (port 8888)

Connect to `quadruped.local:8888`. On connect the robot sends:

```
quadruped connected — type 'help'
```

Send any command above as a newline-terminated string. The robot echoes the command and replies with `ok` or output data. One client at a time; a new connection is accepted when the previous one drops.

```python
import socket
s = socket.create_connection(("quadruped.local", 8888))
s.sendall(b"forward\n")
print(s.recv(1024).decode())   # -> "> forward\nok\n"
```

See `robot_link.py` for the full reference transport implementation.
