#!/usr/bin/env python3
"""Headless real-input driver for mcwindow.

Unlike the QRL ``step`` command, this sends ordinary keyboard/mouse events to
the live :0 client while continuously draining its framebuffer stream. A JSONL
script is a sequence of timed input states, for example::

    {"seconds":2.0,"keys":["w"]}
    {"seconds":1.0,"keys":["w","Control_L"],"look":[30,-5]}
    {"seconds":0.5,"buttons":[1],"cursor":[282,258]}

Keys/buttons omitted from the next row are released at that row boundary. All
held inputs are released on exit, including exceptions and SIGINT.
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import threading
import time
from pathlib import Path

MAGIC = 0x51484631
PORT = 25581


def read_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("mcwindow server closed")
        data.extend(chunk)
    return bytes(data)


def load_script(path: str | Path) -> list[dict]:
    segments = []
    with Path(path).open(encoding="utf-8") as source:
        for line_no, line in enumerate(source, 1):
            if not line.strip():
                continue
            segment = json.loads(line)
            allowed = {"seconds", "keys", "buttons", "look", "cursor"}
            unknown = set(segment) - allowed
            if unknown:
                raise ValueError(f"line {line_no}: unknown fields {sorted(unknown)}")
            seconds = float(segment.get("seconds", 0.0))
            keys = segment.get("keys", [])
            buttons = segment.get("buttons", [])
            look = segment.get("look")
            cursor = segment.get("cursor")
            if seconds < 0 or not all(isinstance(key, str) for key in keys):
                raise ValueError(f"line {line_no}: invalid seconds/keys")
            if not all(int(button) in (1, 2, 3) for button in buttons):
                raise ValueError(f"line {line_no}: buttons must be 1, 2, or 3")
            if look is not None and (not isinstance(look, list) or len(look) != 2):
                raise ValueError(f"line {line_no}: look must be [dx,dy]")
            if cursor is not None and (
                not isinstance(cursor, list)
                or len(cursor) != 2
                or not all(isinstance(v, (int, float)) for v in cursor)
            ):
                raise ValueError(f"line {line_no}: cursor must be [x,y]")
            segments.append({
                "seconds": seconds,
                "keys": list(dict.fromkeys(keys)),
                "buttons": list(dict.fromkeys(int(button) for button in buttons)),
                "look": look,
                "cursor": cursor,
            })
    return segments


class MCWindowClient:
    def __init__(self, host: str = "127.0.0.1", port: int = PORT):
        self.sock = socket.create_connection((host, port), timeout=15)
        self.sock.settimeout(None)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._send_lock = threading.Lock()
        self._stop = threading.Event()
        self._stream_ready = threading.Event()
        self._error: Exception | None = None
        self._keys: set[str] = set()
        self._buttons: set[int] = set()
        self.frames = 0
        self.width = 0
        self.height = 0
        self._rx = threading.Thread(target=self._drain_frames, daemon=True)
        self._rx.start()

    def _drain_frames(self) -> None:
        try:
            while not self._stop.is_set():
                magic, width, height, size = struct.unpack(
                    ">iiii", read_exact(self.sock, 16)
                )
                if magic != MAGIC or size <= 0 or size > 32 << 20:
                    raise ConnectionError(f"bad frame header {magic:#x} {size}")
                read_exact(self.sock, size)
                self.width, self.height = width, height
                self.frames += 1
                self._stream_ready.set()
        except (ConnectionError, OSError) as exc:
            if not self._stop.is_set():
                self._error = exc
                self._stream_ready.set()

    def _send(self, event: dict) -> None:
        if self._error is not None:
            raise self._error
        payload = (json.dumps(event, separators=(",", ":")) + "\n").encode()
        with self._send_lock:
            self.sock.sendall(payload)

    def wait_stream(self, timeout: float = 60.0) -> None:
        if not self._stream_ready.wait(timeout):
            raise TimeoutError("no mcwindow framebuffer received")
        if self._error is not None:
            raise self._error

    def set_inputs(self, keys: list[str], buttons: list[int]) -> None:
        wanted_keys, wanted_buttons = set(keys), set(buttons)
        for key in sorted(self._keys - wanted_keys):
            self._send({"t": "key", "sym": key, "p": 0})
        for button in sorted(self._buttons - wanted_buttons):
            self._send({"t": "mb", "b": button, "p": 0})
        for key in sorted(wanted_keys - self._keys):
            self._send({"t": "key", "sym": key, "p": 1})
        for button in sorted(wanted_buttons - self._buttons):
            self._send({"t": "mb", "b": button, "p": 1})
        self._keys, self._buttons = wanted_keys, wanted_buttons

    def look(self, dx: float, dy: float) -> None:
        self._send({"t": "look", "dx": float(dx), "dy": float(dy)})

    def move_cursor(self, x: float, y: float) -> None:
        """Move the GUI cursor in game-client coordinates."""
        self._send({"t": "ma", "x": int(x), "y": int(y)})

    def run(self, segments: list[dict]) -> None:
        self.wait_stream()
        for segment in segments:
            if segment["cursor"] is not None:
                self.move_cursor(*segment["cursor"])
            self.set_inputs(segment["keys"], segment["buttons"])
            if segment["look"] is not None:
                self.look(*segment["look"])
            if self._stop.wait(segment["seconds"]):
                break
            if self._error is not None:
                raise self._error
        self.set_inputs([], [])

    def close(self) -> None:
        if self._stop.is_set():
            return
        try:
            self.set_inputs([], [])
        except (ConnectionError, OSError):
            pass
        self._stop.set()
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()
        self._rx.join(timeout=2)

    def __enter__(self) -> "MCWindowClient":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("script")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()
    segments = load_script(args.script)
    started = time.monotonic()
    with MCWindowClient(args.host, args.port) as client:
        client.run(segments)
        elapsed = time.monotonic() - started
        print(
            f"mcwindow script: {len(segments)} segments, {elapsed:.2f}s, "
            f"{client.frames} frames at {client.width}x{client.height}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
