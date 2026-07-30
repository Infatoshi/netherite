"""Shared helpers for residual board row harnesses."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", required=True)
    ap.add_argument("--repo", required=True)
    return ap.parse_args()


def load_board(path: str) -> dict:
    return json.loads(Path(path).read_text())


def pack_paths(board: dict, repo: Path) -> dict:
    d = repo / board["capture_pack"]["dir"]
    return {
        "dir": d,
        "golden": d / board["capture_pack"]["golden"],
        "camera": d / board["capture_pack"]["camera"],
        "options": d / board["capture_pack"]["options"],
        "hard_scene": d / board["capture_pack"]["hard_scene"],
        "pack": board["capture_pack"],
    }


def finish(ok: bool, **kwargs):
    payload = {"ok": bool(ok), **kwargs}
    print(json.dumps(payload))
    sys.exit(0 if ok else 1)
