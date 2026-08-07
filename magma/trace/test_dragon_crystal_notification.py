#!/usr/bin/env python3
"""Compare the bounded dragon crystal-destruction transition to real 1.11.2."""

import argparse
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import time


HERE = pathlib.Path(__file__).resolve().parent
MAGMA = HERE.parent
BLAZE = MAGMA.parent / "blaze"


def request(port, cmd, action=None):
    payload = json.dumps({"cmd": cmd, "action": action or {}}) + "\n"
    with socket.create_connection(("127.0.0.1", port), timeout=30) as sock:
        sock.settimeout(30)
        sock.sendall(payload.encode())
        chunks = []
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
            if b"\n" in chunk:
                break
    result = json.loads(b"".join(chunks).splitlines()[0])
    if not result.get("ok"):
        raise RuntimeError(result)
    return result


def c_rows():
    cache_root = pathlib.Path(os.environ.get(
        "TMPDIR", str(MAGMA / ".tmp")))
    cache_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=cache_root) as tmp:
        binary = pathlib.Path(tmp) / "dragon_crystal_oracle"
        subprocess.run([
            os.environ.get("CC", "cc"), "-O2", "-ffp-contract=off",
            "-I", str(BLAZE / "core"),
            str(MAGMA / "game" / "test_dragon_crystal_oracle.c"),
            "-lm", "-o", str(binary),
        ], check=True)
        output = subprocess.check_output([str(binary)], text=True)
    return [line.split() for line in output.splitlines()]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=25575)
    args = parser.parse_args()
    locked = False
    try:
        deadline = time.monotonic() + 120.0
        while True:
            try:
                request(args.port, "obs")
                break
            except (OSError, RuntimeError, ValueError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.5)
        request(args.port, "server_step_lock")
        locked = True
        java = []
        for healing, player_source in ((True, True), (False, True),
                                       (True, False)):
            row = request(args.port, "dragon_crystal_notify_locked", {
                "healing": healing, "player_source": player_source,
            })
            java.append([f"{float(row['health']):.1f}",
                         "1" if row["strafe"] else "0"])
        c = c_rows()
        if java != c:
            raise SystemExit(f"FAIL java={java!r} c={c!r}")
        print(f"PASS java==c: {java}")
    finally:
        if locked:
            request(args.port, "server_step_unlock")


if __name__ == "__main__":
    main()
