#!/usr/bin/env python3
"""Export ChainPolicy FP32 parameters to the cgraph/native container."""

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import torch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "blaze" / "env"))
from ppo_chain_cu import ChainPolicy  # noqa: E402


MAGIC = b"NBF16CK1"


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--pt", type=Path)
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    model = ChainPolicy()
    if args.pt:
        model.load_state_dict(
            torch.load(args.pt, map_location="cpu", weights_only=True),
            strict=True,
        )
    state = dict(model.named_parameters())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("wb") as stream:
        stream.write(MAGIC)
        stream.write(struct.pack("<I", len(state)))
        for name, value in state.items():
            tensor = value.detach().to(torch.float32).cpu().contiguous()
            encoded = name.encode()
            stream.write(struct.pack("<I", len(encoded)))
            stream.write(encoded)
            stream.write(struct.pack("<I", tensor.ndim))
            stream.write(struct.pack("<" + "q" * tensor.ndim, *tensor.shape))
            raw = tensor.numpy().tobytes()
            stream.write(struct.pack("<Q", len(raw)))
            stream.write(raw)

    receipt = {
        "schema": "netherite.cgraph-weights.v1",
        "output": str(args.output),
        "sha256": sha256(args.output),
        "torch_seed": args.seed,
        "source_pt": str(args.pt) if args.pt else None,
        "parameters": len(state),
        "values": sum(t.numel() for t in state.values()),
        "dtype": "float32",
    }
    rendered = json.dumps(receipt, indent=2, sort_keys=True) + "\n"
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
