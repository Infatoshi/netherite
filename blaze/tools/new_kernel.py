#!/usr/bin/env python3
"""Scaffold a new blaze kernel so a subagent can start porting immediately.

  uv run --no-project python tools/new_kernel.py <name> --vanilla --ref world/gen/layer/GenLayer.java
  uv run --no-project python tools/new_kernel.py <name> --internal   # CPU==CUDA only (runtime sim)

Creates:
  core/<name>.h            (shared __host__ __device__ port - WRITE THE PORT HERE)
  cpu/<name>.c             (reference driver: argv seed -> hex dump on stdout)
  cuda/<name>.cu           (batch driver: same core, same output format)
  oracle/goldens/<name>/Golden.java   (only with --vanilla: paste verbatim decompiled MC here)

Then: implement, and loop `uv run --no-project python oracle/runner.py <name>` until PASS.
Conventions: drivers print one value per line as raw bits hex (%016llx for u64/double) so the
oracle compares bitwise. See cpu/noise.c + oracle/goldens/noise/Golden.java as the worked example.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CORE_H = '''/* {name}: PORT TARGET {ref}
 * Exact data-oriented C port; __host__ __device__; no OOP/STL on the hot path.
 * Traps: ordered temporaries around side-effecting calls; no a[i]=i++; -ffp-contract/--fmad off.
 * See SPEC.md porting gotchas. */
#ifndef MC_{up}_H
#define MC_{up}_H

#include "mc.h"
#include "mc_rng.h"

/* TODO: port {ref} here as MC_HD static inline functions / POD structs. */

#endif
'''

CPU_C = '''/* CPU reference driver for {name}. Prints raw-bits hex, one value per line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/{name}.h"

int main(int argc, char **argv) {{
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    (void)seed;
    /* TODO: drive the port, emit deterministic output:
     *   u64 bits; memcpy(&bits, &val, 8); printf("%016llx\\n", (unsigned long long)bits);
     */
    return 0;
}}
'''

CUDA_CU = '''/* CUDA driver for {name} - SAME core/{name}.h as the CPU path. Output format must match. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../core/{name}.h"

__global__ void run_{name}(i64 seed /*, outputs */) {{
    if (threadIdx.x || blockIdx.x) return;
    (void)seed;
    /* TODO: call the same core functions as cpu/{name}.c */
}}

int main(int argc, char **argv) {{
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    /* TODO: launch, copy back, print identical format to cpu/{name}.c */
    (void)seed;
    return 0;
}}
'''

GOLDEN_JAVA = '''// Verbatim MC 1.11.2 {ref} (vanilla ground truth, eval-pure). Paste the DECOMPILED method(s)
// here unchanged, inline any tiny helpers, add a main(args) driver that prints the SAME raw-bits
// hex format as cpu/{name}.c. Goldens come from real MC only - never a hand-port. See
// oracle/goldens/noise/Golden.java for the worked example.
public class Golden {{
    public static void main(String[] args) {{
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 12345L;
        // TODO
    }}
}}
'''


def w(path, text):
    if os.path.exists(path):
        print(f"  skip (exists): {os.path.relpath(path, ROOT)}")
        return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)
    print(f"  wrote: {os.path.relpath(path, ROOT)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--ref", default="TODO (world/...)", help="Java source ref file:line")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--vanilla", action="store_true", help="bitwise vs verbatim-Java golden (default)")
    g.add_argument("--internal", action="store_true", help="CPU==CUDA only (runtime/stateful)")
    a = ap.parse_args()
    name = a.name
    vanilla = not a.internal
    fmt = dict(name=name, up=name.upper(), ref=a.ref)
    print(f"scaffolding kernel '{name}' ({'vanilla-bitwise' if vanilla else 'internal CPU==CUDA'})")
    w(os.path.join(ROOT, "core", f"{name}.h"), CORE_H.format(**fmt))
    w(os.path.join(ROOT, "cpu", f"{name}.c"), CPU_C.format(**fmt))
    w(os.path.join(ROOT, "cuda", f"{name}.cu"), CUDA_CU.format(**fmt))
    if vanilla:
        w(os.path.join(ROOT, "oracle", "goldens", name, "Golden.java"), GOLDEN_JAVA.format(**fmt))
    print(f"next: implement, then `uv run --no-project python oracle/runner.py {name}`")


if __name__ == "__main__":
    main()
