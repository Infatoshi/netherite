#!/usr/bin/env python3
"""anvil-side server for the MineRL-style human-play window.

Bridges the mod's local frame stream (HumanStream.java, 127.0.0.1:25580) to one
remote viewer (java/mcwindow.py on the Mac) and injects the viewer's mouse and
keyboard into the game via XTEST on :0. No screen capture anywhere: frames come
straight from the MC framebuffer, inputs go straight into the X server the game
reads from.

Run: DISPLAY=:0 uv run --no-project --with python-xlib python mcwindow_server.py
Viewer protocol, single TCP connection on :25581:
  down: raw QHF1 frames relayed unmodified (drop-to-latest when the link lags)
  up:   newline JSON  {"t":"mm","dx":..,"dy":..} | {"t":"mb","b":1|2|3,"p":0|1}
                      {"t":"key","sym":"<X keysym name>","p":0|1}
                      {"t":"scroll","d":<clicks, +up>}
"""
import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import threading
import time

from Xlib import X, XK, display
from Xlib.ext import xtest

FRAME_PORT = 25580
VIEWER_PORT = 25581  # overridden in main() via --port
REPO = os.path.expanduser("~/dev/minecraft/mc-1.11.2-env")
MAGIC = 0x51484631


class Injector:
    """XTEST input on :0, pointer clamped to the game window so ungrabbed
    (menu) clicks can never land on the desktop."""

    def __init__(self, width=1280, height=720):
        self.d = display.Display(os.environ.get("DISPLAY", ":0"))
        self.win = None
        self.geom = None  # (x, y, w, h) root-absolute
        self.keycode_cache = {}
        self.down_keys = set()
        self.down_buttons = set()
        self.target_w = width
        self.target_h = height
        # Flat pointer response: X acceleration would otherwise distort both
        # menu cursor tracking and grabbed mouse-look (LWJGL measures the
        # accelerated pointer, not raw deltas).
        subprocess.run(["xset", "m", "0", "0"], capture_output=True,
                       env={**os.environ, "DISPLAY": os.environ.get("DISPLAY", ":0")})

    def find_window(self):
        try:
            out = subprocess.run(
                ["xdotool", "search", "--name", "^Minecraft 1.11.2$"],
                capture_output=True, text=True, timeout=5).stdout.split()
            if not out:
                return False
            # Mutter reparents: the frame AND the client both match the name.
            # wmctrl (EWMH moveresize) on the frame is what Mutter honors;
            # xdotool windowsize is ignored. Heal any accidental drag/resize
            # (stuck-modifier incidents) and pin the game at the target size.
            tw = self.target_w
            th = self.target_h
            subprocess.run(["wmctrl", "-i", "-r", out[0], "-e",
                            f"0,60,60,{tw},{th}"], capture_output=True, timeout=5)
            time.sleep(0.5)
            # Clamp/focus target = the real client window (smallest match).
            def area(w):
                g = self.d.create_resource_object("window", int(w)).get_geometry()
                return g.width * g.height
            wid = int(min(out, key=area))
            # Real activation (not just X focus): GNOME ignores plain
            # set_input_focus for keyboard delivery / MC's Display.isActive.
            subprocess.run(["wmctrl", "-i", "-a", out[0]],
                           capture_output=True, timeout=5)
            self.win = self.d.create_resource_object("window", wid)
            g = self.win.get_geometry()
            abs_pos = self.win.translate_coords(self.d.screen().root, 0, 0)
            # translate_coords(root,0,0) gives window-relative of root origin;
            # invert to get root-absolute of the window origin.
            self.geom = (-abs_pos.x, -abs_pos.y, g.width, g.height)
            self.d.set_input_focus(self.win, X.RevertToParent, X.CurrentTime)
            self.d.sync()
            print(f"[mcw] game window id={wid} geom={self.geom}", flush=True)
            return True
        except Exception as e:
            print(f"[mcw] find_window: {e}", flush=True)
            return False

    def release_all(self):
        """Key/button hygiene at session end: nothing may stay held on :0."""
        for kc in list(self.down_keys):
            xtest.fake_input(self.d, X.KeyRelease, kc)
        for b in list(self.down_buttons):
            xtest.fake_input(self.d, X.ButtonRelease, b)
        self.down_keys.clear()
        self.down_buttons.clear()
        self.d.sync()

    def keycode(self, sym_name):
        kc = self.keycode_cache.get(sym_name)
        if kc is None:
            sym = XK.string_to_keysym(sym_name)
            kc = self.d.keysym_to_keycode(sym) if sym else 0
            self.keycode_cache[sym_name] = kc
        return kc

    def clamp_pointer(self):
        if not self.geom:
            return
        x0, y0, w, h = self.geom
        p = self.d.screen().root.query_pointer()
        cx = min(max(p.root_x, x0 + 2), x0 + w - 3)
        cy = min(max(p.root_y, y0 + 2), y0 + h - 3)
        if (cx, cy) != (p.root_x, p.root_y):
            self.d.screen().root.warp_pointer(cx, cy)

    def handle(self, ev):
        t = ev.get("t")
        if t == "mm":
            xtest.fake_input(self.d, X.MotionNotify, True,
                             x=int(ev["dx"]), y=int(ev["dy"]))
            self.clamp_pointer()
        elif t == "ma":
            # Absolute move in game-client coords (menu/cursor mode).
            if self.geom:
                x0, y0, w, h = self.geom
                cx = x0 + min(max(int(ev["x"]), 0), w - 1)
                cy = y0 + min(max(int(ev["y"]), 0), h - 1)
                self.d.screen().root.warp_pointer(cx, cy)
        elif t == "mb":
            b = int(ev["b"])
            xtest.fake_input(self.d, X.ButtonPress if ev["p"] else X.ButtonRelease, b)
            (self.down_buttons.add if ev["p"] else self.down_buttons.discard)(b)
        elif t == "scroll":
            d = int(ev["d"])
            btn = 4 if d > 0 else 5
            for _ in range(min(abs(d), 5)):
                xtest.fake_input(self.d, X.ButtonPress, btn)
                xtest.fake_input(self.d, X.ButtonRelease, btn)
        elif t == "key":
            kc = self.keycode(str(ev["sym"]))
            if kc:
                xtest.fake_input(self.d, X.KeyPress if ev["p"] else X.KeyRelease, kc)
                (self.down_keys.add if ev["p"] else self.down_keys.discard)(kc)
        self.d.sync()


def ensure_game():
    if subprocess.run(["pgrep", "-f", "[G]radleStart --username"],
                      capture_output=True).returncode != 0:
        print("[mcw] game not running, launching", flush=True)
        subprocess.Popen(["nohup", f"{REPO}/java/sunshine_launch_mc.sh"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         start_new_session=True)


def connect_frames():
    while True:
        try:
            s = socket.create_connection(("127.0.0.1", FRAME_PORT), timeout=2)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            print("[mcw] connected to mod frame stream", flush=True)
            return s
        except OSError:
            time.sleep(1)


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("frame stream closed")
        buf += chunk
    return buf


def main():
    global VIEWER_PORT
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=25581,
                    help="viewer TCP port (default 25581)")
    ap.add_argument("--width", type=int, default=1280,
                    help="pin game window width (default 1280)")
    ap.add_argument("--height", type=int, default=720,
                    help="pin game window height (default 720)")
    args = ap.parse_args()
    VIEWER_PORT = args.port
    ensure_game()
    inj = Injector(width=args.width, height=args.height)

    latest = {"frame": None, "modsock": None}
    cond = threading.Condition()

    def frame_pump():
        while True:
            fs = connect_frames()
            latest["modsock"] = fs
            try:
                while True:
                    hdr = read_exact(fs, 16)
                    magic, w, h, ln = struct.unpack(">iiii", hdr)
                    if magic != MAGIC or ln <= 0 or ln > 32 << 20:
                        raise ConnectionError(f"bad frame header {magic:#x} {ln}")
                    jpg = read_exact(fs, ln)
                    with cond:
                        latest["frame"] = hdr + jpg
                        cond.notify()
            except (ConnectionError, OSError) as e:
                print(f"[mcw] frame stream lost ({e}); reconnecting", flush=True)
                latest["modsock"] = None
                try:
                    fs.close()
                except OSError:
                    pass
                time.sleep(1)

    threading.Thread(target=frame_pump, daemon=True).start()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", VIEWER_PORT))
    srv.listen(1)
    print(f"[mcw] viewer port {VIEWER_PORT} ready", flush=True)

    while True:
        conn, addr = srv.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print(f"[mcw] viewer {addr} connected", flush=True)
        while not inj.find_window():
            time.sleep(2)
        stop = threading.Event()

        def sender():
            sent, t0 = 0, time.time()
            try:
                while not stop.is_set():
                    with cond:
                        if not cond.wait(timeout=1.0):
                            continue
                        frame = latest["frame"]
                    if frame is None:
                        continue
                    conn.sendall(frame)
                    sent += 1
                    if time.time() - t0 > 5:
                        print(f"[mcw] relay {sent / (time.time() - t0):.1f} fps",
                              flush=True)
                        sent, t0 = 0, time.time()
            except OSError:
                pass
            stop.set()

        threading.Thread(target=sender, daemon=True).start()
        try:
            buf = b""
            while not stop.is_set():
                chunk = conn.recv(4096)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    if not line.strip():
                        continue
                    try:
                        ev = json.loads(line)
                        if ev.get("t") == "look":
                            # mouse-look goes to the mod (in-game camera path),
                            # not XTEST — LWJGL grabs are unreliable headless.
                            ms = latest["modsock"]
                            if ms is not None:
                                ms.sendall(line + b"\n")
                        else:
                            inj.handle(ev)
                    except Exception as e:
                        print(f"[mcw] bad input event: {e}", flush=True)
        except OSError:
            pass
        stop.set()
        inj.release_all()
        try:
            conn.close()
        except OSError:
            pass
        print("[mcw] viewer disconnected", flush=True)


if __name__ == "__main__":
    sys.exit(main())
