#!/usr/bin/env python3
"""
voice_control.py — always-on, offline voice control for the quadruped.

Real-time: holds the link open (WiFi by default, so the robot runs on
battery), listens continuously with Vosk (offline, on-device), and maps
spoken phrases to robot commands / authored move routines from moves.json.
No cloud, no API key, no Claude in the loop.

  python3 voice_control.py                 # start listening (WiFi, then serial)
  python3 voice_control.py --serial        # force USB serial
  python3 voice_control.py --host 1.2.3.4  # connect by IP
  python3 voice_control.py --list          # list mic + serial devices
  python3 voice_control.py --once "spin"    # fire one action by text (no mic)

Say "stop" (or "halt"/"freeze") any time — it interrupts whatever is running
immediately, even mid-word, even mid-routine. That's the safety reflex.

To add new moves: edit moves.json (or ask Claude to author one). New trigger
words are picked up automatically next launch.
"""

import argparse
import glob
import json
import os
import queue
import sys
import threading

import robot_link

HERE = os.path.dirname(os.path.abspath(__file__))
MOVES_PATH = os.path.join(HERE, "moves.json")
MODEL_PATH = os.path.join(HERE, "vosk-model")
SAMPLE_RATE = 16000

STOP_WORDS = {"stop", "halt", "freeze"}


# ---------------------------------------------------------------------------
# Player — runs a move's steps in a worker thread; any new action or a stop
# cancels the one in flight immediately.
# ---------------------------------------------------------------------------
class Player:
    def __init__(self, link):
        self.link = link
        self._cancel = threading.Event()
        self._thread = None

    def _run(self, steps, cancel):
        for step in steps:
            if cancel.is_set():
                return
            if "wait" in step:
                remaining = float(step["wait"])
                while remaining > 0 and not cancel.is_set():
                    slice_s = min(0.05, remaining)
                    cancel.wait(slice_s)
                    remaining -= slice_s
            elif "cmd" in step:
                self.link.send(step["cmd"])

    def play(self, steps):
        self.cancel()
        self._cancel = threading.Event()
        self._thread = threading.Thread(
            target=self._run, args=(steps, self._cancel), daemon=True
        )
        self._thread.start()

    def cancel(self):
        self._cancel.set()
        t = self._thread
        if t and t.is_alive():
            t.join(timeout=0.5)

    def emergency_stop(self):
        self._cancel.set()
        self.link.send("stop")


# ---------------------------------------------------------------------------
# Intent matching — phrase -> action, longest trigger wins.
# ---------------------------------------------------------------------------
class Intents:
    def __init__(self, actions):
        self.actions = actions
        self.index = []
        for a in actions:
            for trig in a.get("triggers", []):
                self.index.append((trig.lower(), a))
        self.index.sort(key=lambda kv: len(kv[0]), reverse=True)

    def vocabulary(self):
        words = set()
        for trig, _ in self.index:
            words.update(trig.split())
        return sorted(words)

    def match(self, text):
        text = " " + text.lower().strip() + " "
        for trig, action in self.index:
            if " " + trig + " " in text:
                return action
        return None

    def has_stop(self, text):
        return any(w in STOP_WORDS for w in text.lower().split())

    def stop_action(self):
        for a in self.actions:
            if a.get("priority"):
                return a
        return None


def load_actions():
    with open(MOVES_PATH) as f:
        return json.load(f)["actions"]


# ---------------------------------------------------------------------------
# VoiceListener — offline Vosk recognition in a background thread. Calls
# on_action(action_dict) whenever a phrase matches (including an instant stop
# on a partial result). Start/stop at will. Shared by the CLI and the GUI.
# ---------------------------------------------------------------------------
class VoiceListener:
    def __init__(self, intents, on_action, on_text=None):
        self.intents = intents
        self.on_action = on_action      # called with an action dict
        self.on_text = on_text          # optional: called with raw heard text
        self._stop = threading.Event()
        self._thread = None
        self.error = None

    def start(self):
        if self._thread and self._thread.is_alive():
            return
        self.error = None
        self._stop.clear()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        self._stop.set()
        t = self._thread
        if t and t.is_alive():
            t.join(timeout=1.0)

    def is_running(self):
        return self._thread is not None and self._thread.is_alive()

    def _run(self):
        try:
            import sounddevice as sd
            from vosk import Model, KaldiRecognizer

            if not os.path.isdir(MODEL_PATH):
                raise RuntimeError(f"Vosk model not found at {MODEL_PATH}")

            model = Model(MODEL_PATH)
            grammar = json.dumps(self.intents.vocabulary() + ["[unk]"])
            rec = KaldiRecognizer(model, SAMPLE_RATE, grammar)
            rec.SetWords(False)
            stop_action = self.intents.stop_action()

            audio_q = queue.Queue()

            def audio_cb(indata, frames, time_info, status):
                audio_q.put(bytes(indata))

            with sd.RawInputStream(samplerate=SAMPLE_RATE, blocksize=8000,
                                   dtype="int16", channels=1, callback=audio_cb):
                last_partial = ""
                while not self._stop.is_set():
                    try:
                        data = audio_q.get(timeout=0.2)
                    except queue.Empty:
                        continue
                    if rec.AcceptWaveform(data):
                        text = json.loads(rec.Result()).get("text", "").strip()
                        last_partial = ""
                        if not text:
                            continue
                        if self.on_text:
                            self.on_text(text)
                        action = self.intents.match(text)
                        if action:
                            self.on_action(action)
                    else:
                        partial = json.loads(rec.PartialResult()).get("partial", "")
                        if partial != last_partial:
                            last_partial = partial
                            if stop_action and self.intents.has_stop(partial):
                                self.on_action(stop_action)
                                rec.Reset()
                                last_partial = ""
        except Exception as e:  # surfaced to the caller via .error
            self.error = e


# ---------------------------------------------------------------------------
# Listening loop
# ---------------------------------------------------------------------------
def listen(link, intents):
    import time
    player = Player(link)

    def on_action(action):
        print(f"→ {action['name']}")
        if action.get("priority"):
            player.emergency_stop()
        else:
            player.play(action["steps"])
        link.drain()

    def on_text(text):
        if not intents.match(text):
            print(f"  (heard '{text}' — no match)")

    listener = VoiceListener(intents, on_action, on_text)
    listener.start()
    print("Listening — say a command. 'stop' halts instantly. Ctrl-C to quit.")
    print("Vocabulary:", ", ".join(a["name"] for a in intents.actions))

    try:
        while listener.is_running():
            time.sleep(0.2)
        if listener.error:
            sys.exit(f"Voice listener error: {listener.error}")
    finally:
        listener.stop()


def list_devices():
    import sounddevice as sd
    print("=== Audio input devices ===")
    for i, d in enumerate(sd.query_devices()):
        if d["max_input_channels"] > 0:
            print(f"  [{i}] {d['name']}")
    print("=== Serial ports ===")
    port = robot_link.find_port()
    print(f"  robot (USB): {port or 'NOT FOUND'}")
    print(f"  robot (WiFi): try {robot_link.DEFAULT_HOST}:{robot_link.TCP_PORT}")


def main():
    ap = argparse.ArgumentParser(description="Always-on voice control for the quadruped.")
    ap.add_argument("--list", action="store_true", help="list mic + serial devices and exit")
    ap.add_argument("--host", help="robot address (default: quadruped.local)")
    ap.add_argument("--serial", action="store_true", help="force USB serial")
    ap.add_argument("--net", action="store_true", help="force WiFi (no serial fallback)")
    ap.add_argument("--once", metavar="TEXT", help="fire one action by text, no mic")
    args = ap.parse_args()

    if args.list:
        list_devices()
        return
    if args.serial and args.net:
        sys.exit("Pick one of --serial / --net.")

    actions = load_actions()
    intents = Intents(actions)

    prefer = "serial-only" if args.serial else "net-only" if args.net else "net"
    try:
        link = robot_link.connect(prefer=prefer, host=args.host or robot_link.DEFAULT_HOST)
    except ConnectionError as e:
        sys.exit(str(e))

    try:
        if args.once:
            action = intents.match(args.once)
            if not action:
                sys.exit(f"No action matched '{args.once}'.")
            print(f"→ {action['name']}")
            Player(link)._run(action["steps"], threading.Event())
        else:
            listen(link, intents)
    except KeyboardInterrupt:
        print("\nstopping…")
        link.send("stop")
    finally:
        link.close()


if __name__ == "__main__":
    main()
