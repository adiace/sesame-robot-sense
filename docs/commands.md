# Command Reference

Three ways in — all funnel into the same dispatcher:

| Transport | How | Used by |
|---|---|---|
| TCP port **8888** | newline-terminated lines | companion app, `nc`, scripts |
| HTTP port **80** | captive portal + JSON API | web UI, curl |
| USB serial | 115200 baud CLI | bring-up / trim calibration |

## TCP :8888 (line protocol)

```bash
nc sesame-robot.local 8888
wave
walk 5
face happy
stop
```

### Movement (gaits)

| Command | Effect |
|---|---|
| `walk` / `forward` | walk forward — **8 steps** then stand (default cap) |
| `walk 5` | walk exactly 5 gait cycles then stand (1–50) |
| `back` / `backward` | walk backward, same defaults |
| `left` / `right` | turn — **4 cycles** default, `left 2` for exact |
| `stop` / `halt` | abort whatever is running immediately |

Gaits sent over TCP are always bounded so the robot returns to wake-word listening on
its own (the mic is not monitored while a command runs). The web UI's press-and-hold
buttons remain continuous.

### Tricks (run once per command)

`wave` `dance` `swim` `point` `pushup` `bow` `cute` `freaky` `worm` `shake` `shrug`
`dead` `crab` `box` `rest` `stand`

### Faces

`face <name>` — `happy` `sad` `angry` `surprised` `sleepy` `love` `excited` `confused`
`idle` `rest` `thinking` … (see `firmware/face-bitmaps.h` for the full list; most have a
`talk_` variant used while speaking).

### Power management

| Command | Effect |
|---|---|
| `sleep` | rest pose → servos fully detached (no hold current) → sleepy face |
| `wake` | re-attach → stand → happy face |

The companion app sends `sleep` automatically after 5 minutes idle.

### Chained commands

The companion app sequences chains itself — say *"walk 5 steps then turn left"* and it
sends `walk 5`, waits for the robot to finish (polls `/api/status` until
`currentCommand` is empty), then sends `left`.

## HTTP API

| Endpoint | Method | Purpose |
|---|---|---|
| `/` | GET | the captive-portal web UI |
| `/cmd?pose=wave` · `/cmd?go=forward` · `/cmd?stop=1` | GET | web UI buttons |
| `/cmd?motor=N&value=A` | GET | direct servo positioning (N=1–8, A=0–180) |
| `/api/command` | POST | `{"command":"wave","face":"happy"}` |
| `/api/status` | GET | state + voice diagnostics (below) |
| `/subtrim?motor=N&value=V` | GET | set servo trim, persisted to NVS |
| `/getSettings` / `/setSettings` | GET | frameDelay, walkCycles, motorDelay, faceFps (persisted) |

### `/api/status` fields

```json
{
  "currentCommand": "",         // what's running ("" = idle, listening)
  "currentFace": "idle",
  "networkConnected": true,
  "networkIP": "192.168.x.x",
  "servosAttached": true,       // false while sleeping
  "wakeReady": true,            // WakeNet model loaded (needs PSRAM + model flash)
  "wakeChunksFed": 12345,       // climbs ~32/s while listening; stalled = feed blocked
  "micRms": 1200,               // live mic level; ~0 = dead mic wiring
  "wakeDetections": 3,          // "Hi ESP" hits since boot
  "psramBytes": 8388608,
  "voiceBufSecs": 4.0
}
```

## Serial CLI (USB, 115200)

For bring-up and trim calibration:

| Command | Effect |
|---|---|
| `<motor> <angle>` | e.g. `3 90` — set servo 0–7 to angle |
| `all <angle>` | all servos to angle |
| `subtrim <m> <v>` | set trim −90…+90 for servo m |
| `subtrim` / `st` | print all trims |
| `subtrim reset` | zero all trims |
| `rn wf` `rn wb` `rn tl` `rn tr` | walk fwd/back, turn left/right |
| `rn wv` `rn dn` `rn sw` … | tricks (wave, dance, swim, …) |
| `run rest` / `run stand` | poses |

## Debug log — TCP :8890

```bash
nc sesame-robot.local 8890
```

Streams the firmware's diagnostic log (`[Wake]`, `[Mic]`, `[Voice]` lines) and replays
the last ~4 KB of history on connect, so you can check what happened *after* the fact.
