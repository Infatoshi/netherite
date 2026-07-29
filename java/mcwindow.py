#!/usr/bin/env python3
"""Mac viewer for the MineRL-style human-play window.

One small local window showing the game's own framebuffer (JPEG over TCP from
java/mcwindow_server.py on anvil); mouse and keyboard in the window are sent
back and injected into the game. Click to capture the mouse, Ctrl+Alt+Shift+Z
to release, close the window to end the session.

Run: uv run --no-project --with pyglet --with pillow python mcwindow.py <host>
"""
import io
import json
import socket
import struct
import sys
import threading
import time

import pyglet
from PIL import Image

MAGIC = 0x51484631
PORT = 25581

PYGLET_TO_XKEYSYM = {
    pyglet.window.key.SPACE: "space", pyglet.window.key.LSHIFT: "Shift_L",
    pyglet.window.key.RSHIFT: "Shift_R", pyglet.window.key.LCTRL: "Control_L",
    pyglet.window.key.RCTRL: "Control_R", pyglet.window.key.LALT: "Alt_L",
    pyglet.window.key.RALT: "Alt_R", pyglet.window.key.TAB: "Tab",
    pyglet.window.key.ENTER: "Return", pyglet.window.key.ESCAPE: "Escape",
    pyglet.window.key.BACKSPACE: "BackSpace", pyglet.window.key.SLASH: "slash",
    pyglet.window.key.COMMA: "comma", pyglet.window.key.PERIOD: "period",
    pyglet.window.key.MINUS: "minus", pyglet.window.key.EQUAL: "equal",
    pyglet.window.key.APOSTROPHE: "apostrophe",
    pyglet.window.key.SEMICOLON: "semicolon",
    pyglet.window.key.BRACKETLEFT: "bracketleft",
    pyglet.window.key.BRACKETRIGHT: "bracketright",
    pyglet.window.key.BACKSLASH: "backslash", pyglet.window.key.GRAVE: "grave",
    pyglet.window.key.UP: "Up", pyglet.window.key.DOWN: "Down",
    pyglet.window.key.LEFT: "Left", pyglet.window.key.RIGHT: "Right",
    pyglet.window.key.PAGEUP: "Page_Up", pyglet.window.key.PAGEDOWN: "Page_Down",
    # Mac Cmd -> Ctrl so Cmd+key does not leak odd syms; MC ignores Super anyway.
    pyglet.window.key.LCOMMAND: "Control_L", pyglet.window.key.RCOMMAND: "Control_R",
}
for _i in range(1, 13):
    PYGLET_TO_XKEYSYM[getattr(pyglet.window.key, f"F{_i}")] = f"F{_i}"
for _c in "abcdefghijklmnopqrstuvwxyz":
    PYGLET_TO_XKEYSYM[getattr(pyglet.window.key, _c.upper())] = _c
for _n in "0123456789":
    PYGLET_TO_XKEYSYM[getattr(pyglet.window.key, f"_{_n}")] = _n

MOUSE_TO_XBUTTON = {pyglet.window.mouse.LEFT: 1, pyglet.window.mouse.MIDDLE: 2,
                    pyglet.window.mouse.RIGHT: 3}


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("server closed")
        buf += chunk
    return buf


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "anvil"
    sock = socket.create_connection((host, PORT), timeout=10)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    print(f"[mcwindow] connected to {host}:{PORT}, waiting for first frame", flush=True)

    latest = {"rgb": None, "wh": None, "n": 0}
    lock = threading.Lock()
    dead = threading.Event()

    def rx():
        try:
            while not dead.is_set():
                magic, w, h, ln = struct.unpack(">iiii", read_exact(sock, 16))
                if magic != MAGIC:
                    raise ConnectionError("bad magic")
                jpg = read_exact(sock, ln)
                img = Image.open(io.BytesIO(jpg)).convert("RGB")
                with lock:
                    latest["rgb"] = img.tobytes()
                    latest["wh"] = (w, h)
                    latest["n"] += 1
        except (ConnectionError, OSError) as e:
            print(f"[mcwindow] stream ended: {e}", flush=True)
            dead.set()

    threading.Thread(target=rx, daemon=True).start()

    t0 = time.time()
    while latest["wh"] is None:
        if dead.is_set() or time.time() - t0 > 60:
            print("[mcwindow] no frames (is the game still booting?)")
            return 1
        time.sleep(0.05)
    w, h = latest["wh"]
    print(f"[mcwindow] {w}x{h} stream up — cursor mode for menus; "
          "Ctrl+Alt+Shift+Z toggles mouse-look capture", flush=True)

    window = pyglet.window.Window(width=w, height=h, caption="mc",
                                  resizable=False)
    state = {"captured": False, "drawn": 0, "t0": time.time()}
    down_syms = set()

    def send(ev):
        try:
            sock.sendall((json.dumps(ev) + "\n").encode())
        except OSError:
            dead.set()

    def release_capture():
        # Send key-ups for everything still held (incl. the hotkey's own
        # Ctrl/Alt/Shift) or they stay pressed on the host X server, where a
        # stuck Alt turns the next drag into a window move/resize.
        for sym in list(down_syms):
            send({"t": "key", "sym": sym, "p": 0})
        down_syms.clear()
        window.set_exclusive_mouse(False)
        state["captured"] = False
        print("[mcwindow] mouse released (cursor mode: menus click normally)",
              flush=True)

    def capture():
        window.set_exclusive_mouse(True)
        state["captured"] = True
        print("[mcwindow] mouse captured for look (Ctrl+Alt+Shift+Z toggles)",
              flush=True)

    @window.event
    def on_draw():
        nonlocal w, h
        window.clear()
        with lock:
            rgb, wh = latest["rgb"], latest["wh"]
        if rgb and wh != (w, h):
            w, h = wh
            window.set_size(w, h)
            print(f"[mcwindow] stream resized to {w}x{h}", flush=True)
            return
        if rgb:
            pyglet.image.ImageData(w, h, "RGB", rgb, pitch=-w * 3).blit(0, 0)
            state["drawn"] += 1
        if time.time() - state["t0"] > 5:
            print(f"[mcwindow] {state['drawn'] / (time.time() - state['t0']):.1f} fps drawn", flush=True)
            state["drawn"], state["t0"] = 0, time.time()

    @window.event
    def on_mouse_press(x, y, button, modifiers):
        if not state["captured"]:
            send({"t": "ma", "x": int(x), "y": int(h - 1 - y)})
        b = MOUSE_TO_XBUTTON.get(button)
        if b:
            send({"t": "mb", "b": b, "p": 1})

    @window.event
    def on_mouse_release(x, y, button, modifiers):
        b = MOUSE_TO_XBUTTON.get(button)
        if b:
            send({"t": "mb", "b": b, "p": 0})

    def motion(x, y, dx, dy):
        if state["captured"]:
            if dx or dy:
                # look deltas go to the mod's camera path (dy positive = down)
                send({"t": "look", "dx": float(dx), "dy": float(-dy)})
        else:
            # Cursor mode: mirror the local cursor 1:1 into the game window.
            send({"t": "ma", "x": int(x), "y": int(h - 1 - y)})

    @window.event
    def on_mouse_motion(x, y, dx, dy):
        motion(x, y, dx, dy)

    @window.event
    def on_mouse_drag(x, y, dx, dy, buttons, modifiers):
        motion(x, y, dx, dy)

    @window.event
    def on_mouse_scroll(x, y, sx, sy):
        if sy:
            send({"t": "scroll", "d": int(sy) or (1 if sy > 0 else -1)})

    @window.event
    def on_key_press(symbol, modifiers):
        k = pyglet.window.key
        if (symbol == k.Z and modifiers & k.MOD_CTRL and modifiers & k.MOD_ALT
                and modifiers & k.MOD_SHIFT):
            release_capture() if state["captured"] else capture()
            return pyglet.event.EVENT_HANDLED
        sym = PYGLET_TO_XKEYSYM.get(symbol)
        if sym:
            send({"t": "key", "sym": sym, "p": 1})
            down_syms.add(sym)
        return pyglet.event.EVENT_HANDLED  # keep Esc from closing the window

    @window.event
    def on_key_release(symbol, modifiers):
        sym = PYGLET_TO_XKEYSYM.get(symbol)
        if sym and sym in down_syms:
            send({"t": "key", "sym": sym, "p": 0})
            down_syms.discard(sym)
        return pyglet.event.EVENT_HANDLED

    def tick(dt):
        if dead.is_set():
            pyglet.app.exit()

    pyglet.clock.schedule_interval(tick, 0.25)
    pyglet.clock.schedule_interval(lambda dt: None, 1 / 120.0)  # keep on_draw hot
    pyglet.app.run()
    dead.set()
    try:
        sock.close()
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
