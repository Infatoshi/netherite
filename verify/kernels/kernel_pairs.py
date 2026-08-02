#!/usr/bin/env python3
"""Kernel-pair lockstep guard: CUDA and Metal raster kernels must change together.

The two backends implement the same six kernels (same math, device-appropriate
indexing):

    cuda/raster_cuda.cu        __global__ void <name>(...)
    metal/raster_kernels.metal kernel void <name>(...)

This tool extracts each kernel's source block (signature line through its
matching closing brace) from both files, hashes them, and compares against the
recorded pair manifest. Everything outside the kernel blocks in each file is
hashed as the "<side>_helpers" entry, because kernel math also lives in shared
device helpers.

If EITHER side of a pair drifts from the manifest, the check fails and names
the kernel: edit the twin implementation to match semantics, prove it with
`scripts/kernel_parity_gate.sh` on BOTH machines (anvil: cpu-vs-cuda,
macbook: cpu-vs-metal; the CPU rasterizer is the shared reference), then
re-record with `--update`. `--update` always re-records both sides of every
pair - the discipline is that you never run it without having reviewed the
twin.

Usage:
    uv run --no-project python verify/kernels/kernel_pairs.py            # check
    uv run --no-project python verify/kernels/kernel_pairs.py --update   # re-record
"""

import hashlib
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CUDA_SRC = REPO / "magma/cuda/raster_cuda.cu"
METAL_SRC = REPO / "magma/metal/raster_kernels.metal"
MANIFEST = Path(__file__).resolve().parent / "parity_manifest.json"

SIG = {
    "cuda": re.compile(r"^(?:static )?__global__ void ([a-z_0-9]+)\b"),
    "metal": re.compile(r"^(?:static )?kernel void ([a-z_0-9]+)\b"),
}


def extract_kernels(path, side):
    """Return ({kernel_name: block_text}, helpers_text)."""
    text = path.read_text()
    lines = text.splitlines(keepends=True)
    kernels = {}
    helper_lines = []
    i = 0
    while i < len(lines):
        m = SIG[side].match(lines[i])
        if not m:
            helper_lines.append(lines[i])
            i += 1
            continue
        name = m.group(1)
        depth = 0
        seen_open = False
        block = []
        while i < len(lines):
            block.append(lines[i])
            depth += lines[i].count("{") - lines[i].count("}")
            if "{" in lines[i]:
                seen_open = True
            i += 1
            if seen_open and depth == 0:
                break
        kernels[name] = "".join(block)
    return kernels, "".join(helper_lines)


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()[:16]


def current_pairs():
    cuda_k, cuda_helpers = extract_kernels(CUDA_SRC, "cuda")
    metal_k, metal_helpers = extract_kernels(METAL_SRC, "metal")
    if set(cuda_k) != set(metal_k):
        only_cuda = sorted(set(cuda_k) - set(metal_k))
        only_metal = sorted(set(metal_k) - set(cuda_k))
        sys.exit(
            "FAIL: kernel sets differ between backends\n"
            f"  only in {CUDA_SRC.name}: {only_cuda}\n"
            f"  only in {METAL_SRC.name}: {only_metal}\n"
            "Every kernel must exist in both backends with the same name."
        )
    pairs = {
        name: {"cuda": sha(cuda_k[name]), "metal": sha(metal_k[name])}
        for name in sorted(cuda_k)
    }
    pairs["_helpers"] = {"cuda": sha(cuda_helpers), "metal": sha(metal_helpers)}
    return pairs


def main():
    update = "--update" in sys.argv[1:]
    pairs = current_pairs()
    if update:
        MANIFEST.write_text(
            json.dumps(
                {
                    "comment": "Paired kernel-source hashes. Re-record ONLY "
                    "after updating both backends and passing "
                    "scripts/kernel_parity_gate.sh on both machines.",
                    "pairs": pairs,
                },
                indent=2,
            )
            + "\n"
        )
        print(f"recorded {len(pairs)} pairs -> {MANIFEST}")
        return
    if not MANIFEST.exists():
        sys.exit(f"FAIL: no manifest at {MANIFEST}; run with --update to seed it")
    recorded = json.loads(MANIFEST.read_text())["pairs"]
    bad = []
    for name in sorted(set(recorded) | set(pairs)):
        want, got = recorded.get(name), pairs.get(name)
        if want == got:
            continue
        for side in ("cuda", "metal"):
            w = (want or {}).get(side)
            g = (got or {}).get(side)
            if w != g:
                bad.append((name, side, w, g))
    if bad:
        print("FAIL: kernel pair(s) drifted from the recorded manifest:")
        for name, side, w, g in bad:
            print(f"  {name} [{side}]: manifest {w} != current {g}")
        print(
            "\nA kernel changed on one backend. Port the same change to its "
            "twin\n(cuda/raster_cuda.cu <-> metal/raster_kernels.metal), run "
            "scripts/kernel_parity_gate.sh\non BOTH machines, then re-record: "
            "uv run --no-project python verify/kernels/kernel_pairs.py --update"
        )
        sys.exit(1)
    print(f"OK: {len(pairs)} kernel pairs match the manifest")


if __name__ == "__main__":
    main()
