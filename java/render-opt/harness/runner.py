#!/usr/bin/env python3
"""KernelBench-style runner for MC render kernels.

For a kernel dir with a kernel.json, it:
  1. generates a deterministic input stream,
  2. builds + runs the GOLDEN (verbatim decompiled MC code) on that input,
  3. builds + runs the CANDIDATE (your C/CUDA port) on the same input,
  4. checks they match (bitwise for discrete output, atol/rtol for float),
  5. reports correctness + wall-time for both.

Golden truth comes from real Minecraft code, never from a hand-port - that is the
whole point (see ../README.md). Pure kernels run the decompiled Java standalone here;
state-dependent kernels get their golden from a live-game capture (capture_mode "live-hook").

Usage:  uv run --no-project python harness/runner.py kernels/00_fast_inv_sqrt
"""
import json
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
import time


def gen_inputs(spec, kdir):
    g = spec["gen"]
    if g == "doubles":
        rnd = random.Random(spec.get("seed", 0))
        lo, hi = spec.get("lo", 1e-6), spec.get("hi", 1e6)
        llo, lhi = math.log(lo), math.log(hi)
        return "".join(f"{math.exp(rnd.uniform(llo, lhi)):.17g}\n" for _ in range(spec["n"]))
    if g == "script":
        # kernel-provided input generator: prints the input stream to stdout.
        # Lets each kernel define its own input format (ints, matrices, ARGB, ...)
        # without touching this runner. args (if any) are passed as one JSON argv.
        cmd = ["python3", os.path.join(kdir, spec["file"])]
        if "args" in spec:
            cmd.append(json.dumps(spec["args"]))
        return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    raise SystemExit(f"unknown input gen: {g}")


def build_and_run(kind, kdir, spec, stdin_data, tmp):
    lang = spec["lang"]
    if lang == "java":
        src = os.path.join(kdir, spec["file"])
        subprocess.run(["javac", "-d", tmp, src], check=True)
        cmd = ["java", "-cp", tmp, spec["class"]]
    elif lang == "c":
        out = os.path.join(tmp, kind)
        # -ffp-contract=off: forbid FMA fusion so float ops round identically to the JVM (bit-exact)
        subprocess.run(["cc", "-O2", "-ffp-contract=off", "-o", out,
                        os.path.join(kdir, spec["file"]), "-lm"], check=True)
        cmd = [out]
    elif lang == "cuda":
        # build on a GPU box (anvil sm_120 / gamer sm_86); explicit arch, no fat binaries.
        # --fmad=false is the CUDA analog of -ffp-contract=off (no fused multiply-add).
        out = os.path.join(tmp, kind)
        arch = os.environ.get("QRL_SM", "sm_120")
        subprocess.run(["nvcc", f"-arch={arch}", "-O3", "--fmad=false", "-o", out,
                        os.path.join(kdir, spec["file"])], check=True)
        cmd = [out]
    else:
        raise SystemExit(f"unknown lang: {lang}")
    t0 = time.perf_counter()
    p = subprocess.run(cmd, input=stdin_data, capture_output=True, text=True, check=True)
    return p.stdout.splitlines(), time.perf_counter() - t0


def compare(golden, cand, cmp):
    if len(golden) != len(cand):
        return False, f"line count {len(golden)} vs {len(cand)}", 0
    mode = cmp.get("mode", "bitwise")
    mismatch = 0
    first = None
    for i, (a, b) in enumerate(zip(golden, cand)):
        ok = (a == b)
        if not ok and mode == "tol":
            try:
                ok = abs(float(a) - float(b)) <= cmp.get("atol", 0) + cmp.get("rtol", 0) * abs(float(a))
            except ValueError:
                ok = False
        if not ok:
            mismatch += 1
            if first is None:
                first = f"line {i}: golden={a!r} candidate={b!r}"
    return mismatch == 0, first, mismatch


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: runner.py <kernel_dir>")
    kdir = sys.argv[1].rstrip("/")
    spec = json.load(open(os.path.join(kdir, "kernel.json")))
    print(f"== {spec['name']}  (tier {spec['tier']}, {spec['capture_mode']}) ==")
    with tempfile.TemporaryDirectory() as tmp:
        if spec["capture_mode"] == "live-hook":
            # Golden = captured real-game output; candidate is fed the captured INPUTS
            # (not a generated stream), so it sees the exact world state the method read.
            gpath = os.path.join(kdir, "golden", "golden.txt")
            ipath = os.path.join(kdir, "golden", "inputs.txt")
            if not os.path.exists(gpath) or not os.path.exists(ipath):
                raise SystemExit("live-hook kernel needs captured golden/inputs.txt + golden/golden.txt (run the NetheriteMod capture first)")
            stdin_data = open(ipath).read()
            golden, gt = open(gpath).read().splitlines(), 0.0
        else:
            stdin_data = gen_inputs(spec["inputs"], kdir)
            golden, gt = build_and_run("golden", kdir, spec["golden"], stdin_data, tmp)
        cand, ct = build_and_run("candidate", kdir, spec["candidate"], stdin_data, tmp)
    ok, first, n = compare(golden, cand, spec["compare"])
    print(f"golden {len(golden)} lines in {gt*1e3:.0f}ms | candidate {ct*1e3:.0f}ms"
          f" | speedup {gt/ct:.2f}x" if gt else f"candidate {ct*1e3:.0f}ms")
    if ok:
        print(f"PASS  ({len(cand)} outputs match, mode={spec['compare'].get('mode','bitwise')})")
    else:
        print(f"FAIL  {n} mismatches; {first}")
        sys.exit(1)


if __name__ == "__main__":
    main()
