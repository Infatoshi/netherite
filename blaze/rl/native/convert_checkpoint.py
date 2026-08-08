#!/usr/bin/env python3
"""Convert the native-v1 FP32 parameter container to a Python state_dict."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "blaze" / "env"))
from ppo_chain_cu import ChainPolicy

MAGIC = b"NBF16CK1"


def read_exact(stream, size: int) -> bytes:
    value = stream.read(size)
    if len(value) != size:
        raise ValueError("truncated native checkpoint")
    return value


def unpack(stream, fmt: str):
    return struct.unpack("<" + fmt, read_exact(stream, struct.calcsize(fmt)))


def read_native(path: Path) -> dict[str, torch.Tensor]:
    state: dict[str, torch.Tensor] = {}
    with path.open("rb") as stream:
        if read_exact(stream, len(MAGIC)) != MAGIC:
            raise ValueError("invalid native checkpoint magic")
        (count,) = unpack(stream, "I")
        for _ in range(count):
            (name_size,) = unpack(stream, "I")
            name = read_exact(stream, name_size).decode()
            (rank,) = unpack(stream, "I")
            shape = unpack(stream, "q" * rank)
            (byte_count,) = unpack(stream, "Q")
            expected = 4 * int(torch.tensor(shape).prod())
            if byte_count != expected:
                raise ValueError(f"invalid byte count for {name}")
            raw = bytearray(read_exact(stream, byte_count))
            tensor = torch.frombuffer(raw, dtype=torch.float32).clone()
            state[name] = tensor.reshape(shape)
        if stream.read(1):
            raise ValueError("trailing bytes in native checkpoint")
    return state


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("native", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()

    native_state = read_native(args.native)
    model = ChainPolicy()
    incompatible = model.load_state_dict(native_state, strict=True)
    if incompatible.missing_keys or incompatible.unexpected_keys:
        raise AssertionError(str(incompatible))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.save({key: value.cpu() for key, value in model.state_dict().items()},
               args.output)

    loaded = torch.load(args.output, map_location="cpu", weights_only=True)
    expected = model.state_dict()
    if list(loaded) != list(expected):
        raise AssertionError("state_dict key order changed during conversion")
    for key in expected:
        if loaded[key].shape != expected[key].shape:
            raise AssertionError(f"shape mismatch after round trip: {key}")
        if not torch.equal(loaded[key], native_state[key]):
            raise AssertionError(f"value mismatch after round trip: {key}")

    receipt = {
        "schema": "netherite.native-checkpoint-conversion.v1",
        "native": str(args.native),
        "native_sha256": sha256(args.native),
        "output": str(args.output),
        "output_sha256": sha256(args.output),
        "parameter_keys": len(loaded),
        "parameter_values": sum(t.numel() for t in loaded.values()),
        "dtype": "float32",
        "strict_key_shape_value_roundtrip": True,
    }
    rendered = json.dumps(receipt, indent=2, sort_keys=True)
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(rendered + "\n")
    print(rendered)


if __name__ == "__main__":
    main()
