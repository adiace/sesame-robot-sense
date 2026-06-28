#!/usr/bin/env python3
"""
robot_gui.py — desktop control panel for the quadruped.

A single window with:
  - a live serial/TCP monitor (what the robot is saying)
  - buttons for the basic movements and named moves
  - a text box to type any raw firmware command
  - a voice-input toggle (offline Vosk, same engine as voice_control.py)
  - a center animation showing what the bot is doing

Transport is shared with the CLI tools (robot_link): WiFi first
(quadruped.local), falling back to USB serial. Pick Auto/WiFi/Serial in the
top bar, optionally set a host/IP, and hit Connect.

Run:  python3 robot_gui.py
Deps: tkinter (bundled with Python), plus vosk + sounddevice for voice.
"""

import math
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, scrolledtext

import robot_link
from voice_control import Intents, Player, VoiceListener, load_actions

# action-name (or raw first word) -> animation mode
ANIM = {
    "stand": "stand", "rest": "sit", "neutral": "stand",
    "forward": "walk_fwd", "walk": "walk_fwd", "wander": "walk_fwd",
    "backward": "walk_back", "back": "walk_back",
    "left": "turn_left", "right": "turn_right",
    "circle": "turn_left", "circle right": "turn_right",
    "figure eight": "walk_fwd",
    "spin": "spin", "spin right": "spin",
    "wave": "wave", "dance": "dance", "bow": "bow", "pushup": "pushup",
    "wiggle": "wiggle", "stop": "stand", "gait": "walk_fwd",
}


# ---------------------------------------------------------------------------
# Animated quadruped (side view)
# ---------------------------------------------------------------------------
class RobotCanvas(tk.Canvas):
    def __init__(self, parent, w=420, h=340):
        super().__init__(parent, width=w, height=h, bg="#0f141a",
                         highlightthickness=0)
        self.w, self.h = w, h
        self.mode = "stand"
        self.action_name = "stand"
        self.phase = 0.0
        self._tick()

    def set_action(self, name, mode):
        self.action_name = name
        self.mode = mode
        self.phase = 0.0

    def _tick(self):
        speed = {
            "walk_fwd": 0.22, "walk_back": 0.22, "turn_left": 0.22,
            "turn_right": 0.22, "spin": 0.32, "wave": 0.18, "dance": 0.30,
            "pushup": 0.16, "bow": 0.10, "sit": 0.06, "wiggle": 0.20,
            "stand": 0.05,
        }.get(self.mode, 0.05)
        self.phase += speed
        self._draw()
        self.after(33, self._tick)

    def _leg(self, hipx, hipy, footdx, footdy, leglen, color, width):
        footx, footy = hipx + footdx, hipy + leglen + footdy
        kneex = (hipx + footx) / 2 + 5
        kneey = (hipy + footy) / 2
        self.create_line(hipx, hipy, kneex, kneey, fill=color,
                         width=width, capstyle="round")
        self.create_line(kneex, kneey, footx, footy, fill=color,
                         width=width, capstyle="round")
        self.create_oval(footx - 3, footy - 3, footx + 3, footy + 3,
                         fill=color, outline="")

    def _draw(self):
        self.delete("all")
        W, H = self.w, self.h
        cx = W / 2
        baseY = H * 0.40
        bodyW, bodyH = 150, 34
        p = self.phase
        m = self.mode

        bodyDX = bodyDY = frontDY = backDY = 0.0
        A = 0.0          # leg swing amplitude
        ddir = 1
        arrow = None

        if m == "walk_fwd":
            A, bodyDY = 14, -2 * math.sin(2 * p)
        elif m == "walk_back":
            A, ddir, bodyDY = 14, -1, -2 * math.sin(2 * p)
        elif m == "turn_left":
            A, bodyDY, arrow = 11, -2 * math.sin(2 * p), "left"
        elif m == "turn_right":
            A, bodyDY, arrow = 11, -2 * math.sin(2 * p), "right"
        elif m == "spin":
            A, arrow = 13, "spin"
        elif m == "dance":
            bodyDY = -16 * abs(math.sin(p))
            A = 8
        elif m == "pushup":
            bodyDY = 18 * (0.5 + 0.5 * math.sin(p))
        elif m == "bow":
            frontDY = 24 * (0.5 + 0.5 * math.sin(p))
        elif m == "sit":
            backDY = 26
        elif m == "wiggle":
            bodyDX = 13 * math.sin(p * 1.4)
        elif m == "wave":
            A = 0
        else:  # stand — gentle breathing
            bodyDY = -1.5 * math.sin(p * 0.6)

        # ground
        self.create_line(20, baseY + bodyH + 58, W - 20, baseY + bodyH + 58,
                         fill="#22303c", width=2)

        frontBotY = baseY + bodyH + frontDY + bodyDY
        backBotY = baseY + bodyH + backDY + bodyDY
        fx, bx = cx + 45 + bodyDX, cx - 45 + bodyDX
        leglen = 44

        # leg phase offsets (near front, near back, far front, far back)
        offs = {"nf": 0.0, "nb": math.pi, "ff": math.pi, "fb": 0.0}

        def swing(off):
            return A * math.sin(p + off) * ddir, -12 * max(0.0, math.sin(p + off))

        # far legs (behind body, dimmer + offset for depth)
        for key, lx, hy in (("ff", fx, frontBotY), ("fb", bx, backBotY)):
            dx, dy = swing(offs[key])
            self._leg(lx - 12, hy - 8, dx, dy, leglen, "#3a5566", 6)

        # body
        self.create_polygon(
            bx - 25, baseY + bodyDY + backDY,
            fx + 25, baseY + bodyDY + frontDY,
            fx + 25, baseY + bodyH + bodyDY + frontDY,
            bx - 25, baseY + bodyH + bodyDY + backDY,
            fill="#3f8fcf", outline="#6fb4e8", width=2, smooth=True,
        )
        # head (front = right)
        hx = fx + 30
        hy = baseY + bodyDY + frontDY + 6
        self.create_oval(hx, hy, hx + 26, hy + 24, fill="#3f8fcf",
                         outline="#6fb4e8", width=2)
        self.create_oval(hx + 17, hy + 7, hx + 23, hy + 13, fill="#0f141a",
                         outline="")  # eye

        # near legs (solid)
        for key, lx, hyy in (("nf", fx, frontBotY), ("nb", bx, backBotY)):
            if m == "wave" and key == "nf":
                continue
            dx, dy = swing(offs[key])
            self._leg(lx, hyy, dx, dy, leglen, "#d6e2ee", 7)

        # wave: front-near leg raised and waving
        if m == "wave":
            wx = fx
            wy = baseY + bodyDY + frontDY
            footdx = 22 * math.sin(p * 2.2)
            self.create_line(wx, wy + bodyH, wx + 10, wy + 6, fill="#d6e2ee",
                             width=7, capstyle="round")
            self.create_line(wx + 10, wy + 6, wx + 10 + footdx, wy - 24,
                             fill="#d6e2ee", width=7, capstyle="round")

        # direction arrows
        if arrow == "left":
            self.create_text(cx, 28, text="⟲  turning left", fill="#7fd6a0",
                             font=("Helvetica", 13, "bold"))
        elif arrow == "right":
            self.create_text(cx, 28, text="turning right  ⟳", fill="#7fd6a0",
                             font=("Helvetica", 13, "bold"))
        elif arrow == "spin":
            self.create_text(cx, 28, text="⟳ spinning", fill="#7fd6a0",
                             font=("Helvetica", 13, "bold"))

        # action label
        self.create_text(cx, H - 16, text=self.action_name.upper(),
                         fill="#8aa0b2", font=("Helvetica", 14, "bold"))


# ---------------------------------------------------------------------------
# Main application
# ---------------------------------------------------------------------------
class RobotGUI:
    BASIC = ["stand", "rest", "forward", "backward", "left", "right"]
    MOVES = ["circle", "spin", "dance", "wave", "bow", "wiggle", "pushup", "wander"]

    def __init__(self, root):
        self.root = root
        root.title("Quadruped Control Panel")
        root.configure(bg="#161b22")

        self.actions = load_actions()
        self.intents = Intents(self.actions)
        self.by_name = {a["name"]: a for a in self.actions}

        self.link = None
        self.player = None
        self.reader_stop = threading.Event()
        self.reader_thread = None
        self.listener = None
        self.log_q = queue.Queue()
        self.voice_q = queue.Queue()
        self.action_token = 0

        self._build_ui()
        self.root.after(80, self._poll)

    # ---- UI construction -------------------------------------------------
    def _build_ui(self):
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        top = tk.Frame(self.root, bg="#161b22")
        top.pack(side="top", fill="x", padx=10, pady=8)

        self.status_dot = tk.Label(top, text="●", fg="#e05561",
                                   bg="#161b22", font=("Helvetica", 14))
        self.status_dot.pack(side="left")
        self.status_lbl = tk.Label(top, text="disconnected", fg="#c9d1d9",
                                   bg="#161b22", font=("Helvetica", 11))
        self.status_lbl.pack(side="left", padx=(4, 14))

        tk.Label(top, text="Transport:", fg="#8b949e", bg="#161b22").pack(side="left")
        self.transport = tk.StringVar(value="Auto")
        ttk.OptionMenu(top, self.transport, "Auto", "Auto", "WiFi", "Serial").pack(side="left", padx=4)
        tk.Label(top, text="Host:", fg="#8b949e", bg="#161b22").pack(side="left")
        self.host_var = tk.StringVar(value=robot_link.DEFAULT_HOST)
        tk.Entry(top, textvariable=self.host_var, width=18).pack(side="left", padx=4)
        self.connect_btn = ttk.Button(top, text="Connect", command=self.connect)
        self.connect_btn.pack(side="left", padx=6)

        body = tk.Frame(self.root, bg="#161b22")
        body.pack(side="top", fill="both", expand=True, padx=10, pady=(0, 10))

        # left: controls
        left = tk.Frame(body, bg="#161b22")
        left.pack(side="left", fill="y", padx=(0, 10))

        tk.Label(left, text="Movement", fg="#c9d1d9", bg="#161b22",
                 font=("Helvetica", 11, "bold")).pack(anchor="w")
        grid = tk.Frame(left, bg="#161b22")
        grid.pack(pady=4)
        for i, name in enumerate(self.BASIC):
            b = ttk.Button(grid, text=name.capitalize(), width=11,
                           command=lambda n=name: self.fire(n))
            b.grid(row=i // 2, column=i % 2, padx=3, pady=3)

        self.stop_btn = tk.Button(left, text="■  STOP", bg="#e05561",
                                  fg="white", font=("Helvetica", 12, "bold"),
                                  activebackground="#c0392b",
                                  command=lambda: self.fire("stop"))
        self.stop_btn.pack(fill="x", pady=6)

        tk.Label(left, text="Moves", fg="#c9d1d9", bg="#161b22",
                 font=("Helvetica", 11, "bold")).pack(anchor="w", pady=(8, 0))
        mgrid = tk.Frame(left, bg="#161b22")
        mgrid.pack(pady=4)
        for i, name in enumerate(self.MOVES):
            ttk.Button(mgrid, text=name.capitalize(), width=11,
                       command=lambda n=name: self.fire(n)).grid(
                row=i // 2, column=i % 2, padx=3, pady=3)

        # text command
        tk.Label(left, text="Command", fg="#c9d1d9", bg="#161b22",
                 font=("Helvetica", 11, "bold")).pack(anchor="w", pady=(10, 0))
        cmd_row = tk.Frame(left, bg="#161b22")
        cmd_row.pack(fill="x", pady=4)
        self.cmd_var = tk.StringVar()
        entry = tk.Entry(cmd_row, textvariable=self.cmd_var)
        entry.pack(side="left", fill="x", expand=True)
        entry.bind("<Return>", lambda e: self._send_text())
        ttk.Button(cmd_row, text="Send", command=self._send_text).pack(side="left", padx=4)

        # voice toggle
        self.voice_on = tk.BooleanVar(value=False)
        self.voice_chk = ttk.Checkbutton(left, text="\U0001f3a4  Voice input",
                                         variable=self.voice_on,
                                         command=self._toggle_voice)
        self.voice_chk.pack(anchor="w", pady=(10, 0))

        # center: animation
        center = tk.Frame(body, bg="#0f141a")
        center.pack(side="left", fill="both", expand=True)
        self.canvas = RobotCanvas(center)
        self.canvas.pack(fill="both", expand=True, padx=6, pady=6)

        # right: monitor
        right = tk.Frame(body, bg="#161b22")
        right.pack(side="left", fill="both", expand=True, padx=(10, 0))
        tk.Label(right, text="Robot monitor", fg="#c9d1d9", bg="#161b22",
                 font=("Helvetica", 11, "bold")).pack(anchor="w")
        self.monitor = scrolledtext.ScrolledText(
            right, width=40, height=24, bg="#0d1117", fg="#9ece6a",
            insertbackground="#9ece6a", font=("Menlo", 10), state="disabled")
        self.monitor.pack(fill="both", expand=True, pady=4)
        ttk.Button(right, text="Clear", command=self._clear_monitor).pack(anchor="e")

        self._set_controls(False)

    # ---- connection ------------------------------------------------------
    def connect(self):
        if self.link:
            self._teardown_link()
        self.connect_btn.config(state="disabled")
        self.status_lbl.config(text="connecting…")
        prefer = {"Auto": "net", "WiFi": "net-only", "Serial": "serial-only"}[self.transport.get()]
        host = self.host_var.get().strip() or robot_link.DEFAULT_HOST

        def worker():
            try:
                link = robot_link.connect(prefer=prefer, host=host, verbose=False)
                self.log_q.put(("__connected__", link))
            except Exception as e:
                self.log_q.put(("__error__", str(e)))

        threading.Thread(target=worker, daemon=True).start()

    def _on_connected(self, link):
        self.link = link
        self.player = Player(link)
        self.status_dot.config(fg="#3fb950")
        self.status_lbl.config(text=link.describe())
        self.connect_btn.config(state="normal", text="Reconnect")
        self._set_controls(True)
        self._log(f"[connected via {link.describe()}]")
        self.reader_stop.clear()
        self.reader_thread = threading.Thread(target=self._reader, daemon=True)
        self.reader_thread.start()
        self.fire("stand")

    def _on_conn_error(self, msg):
        self.status_dot.config(fg="#e05561")
        self.status_lbl.config(text="disconnected")
        self.connect_btn.config(state="normal", text="Connect")
        self._log(f"[connection failed] {msg}")

    def _teardown_link(self):
        self.reader_stop.set()
        if self.listener:
            self.listener.stop()
            self.listener = None
            self.voice_on.set(False)
        if self.link:
            try:
                self.link.close()
            except Exception:
                pass
        self.link = None
        self.player = None

    def _reader(self):
        while not self.reader_stop.is_set():
            try:
                text = self.link.read_reply(timeout=0.3)
            except Exception as e:
                self.log_q.put(("__line__", f"[link lost] {e}"))
                self.log_q.put(("__down__", None))
                return
            if text:
                self.log_q.put(("__line__", text))

    # ---- actions ---------------------------------------------------------
    def fire(self, name):
        """Run a named action/move (from a button or voice)."""
        action = self.by_name.get(name)
        if not action:
            self._send_raw(name)
            return
        self._run_action(action)

    def _run_action(self, action):
        if not self.player:
            self._log("[not connected]")
            return
        name = action["name"]
        self._log(f"→ {name}")
        self.canvas.set_action(name, ANIM.get(name, "stand"))
        if action.get("priority"):
            self.player.emergency_stop()
            return
        self.player.play(action["steps"])
        self._schedule_idle(action["steps"])

    def _schedule_idle(self, steps):
        # If a routine self-stops, return the animation to 'stand' when done.
        last = next((s["cmd"] for s in reversed(steps) if "cmd" in s), "")
        if last in ("stop", "stand"):
            total = sum(float(s["wait"]) for s in steps if "wait" in s)
            self.action_token += 1
            token = self.action_token
            self.root.after(int(total * 1000) + 200,
                            lambda: self._idle_if(token))

    def _idle_if(self, token):
        if token == self.action_token:
            self.canvas.set_action("stand", "stand")

    def _send_text(self):
        text = self.cmd_var.get().strip()
        if not text:
            return
        self.cmd_var.set("")
        action = self.intents.match(text)
        if action:
            self._run_action(action)
        else:
            self._send_raw(text)

    def _send_raw(self, cmd):
        if not self.link:
            self._log("[not connected]")
            return
        self.link.send(cmd)
        self._log(f"> {cmd}")
        mode = ANIM.get(cmd.split()[0]) if cmd else None
        if mode:
            self.canvas.set_action(cmd, mode)

    # ---- voice -----------------------------------------------------------
    def _toggle_voice(self):
        if self.voice_on.get():
            if not self.link:
                self._log("[connect before enabling voice]")
                self.voice_on.set(False)
                return
            self.listener = VoiceListener(
                self.intents,
                on_action=lambda a: self.voice_q.put(a),
                on_text=lambda t: self.log_q.put(("__line__", f"\U0001f3a4 {t}")),
            )
            self.listener.start()
            self._log("[voice ON — say a command; 'stop' halts]")
        else:
            if self.listener:
                self.listener.stop()
                self.listener = None
            self._log("[voice OFF]")

    # ---- polling (main thread) ------------------------------------------
    def _poll(self):
        # drain robot output + connection events
        while not self.log_q.empty():
            kind, payload = self.log_q.get()
            if kind == "__line__":
                self._log(payload)
            elif kind == "__connected__":
                self._on_connected(payload)
            elif kind == "__error__":
                self._on_conn_error(payload)
            elif kind == "__down__":
                self._on_link_down()
        # drain voice actions
        while not self.voice_q.empty():
            self._run_action(self.voice_q.get())
        # surface a voice-thread crash
        if self.listener and self.listener.error:
            self._log(f"[voice error] {self.listener.error}")
            self.listener = None
            self.voice_on.set(False)
        self.root.after(80, self._poll)

    def _on_link_down(self):
        self.status_dot.config(fg="#e05561")
        self.status_lbl.config(text="disconnected")
        self.connect_btn.config(text="Connect", state="normal")
        self._set_controls(False)
        self._teardown_link()

    # ---- helpers ---------------------------------------------------------
    def _set_controls(self, on):
        state = "normal" if on else "disabled"
        for frame in (self.stop_btn,):
            frame.config(state=state)

    def _log(self, text):
        self.monitor.config(state="normal")
        self.monitor.insert("end", text.rstrip() + "\n")
        self.monitor.see("end")
        self.monitor.config(state="disabled")

    def _clear_monitor(self):
        self.monitor.config(state="normal")
        self.monitor.delete("1.0", "end")
        self.monitor.config(state="disabled")


def main():
    root = tk.Tk()
    app = RobotGUI(root)

    def on_close():
        app._teardown_link()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", on_close)
    root.minsize(1000, 560)
    root.mainloop()


if __name__ == "__main__":
    main()
