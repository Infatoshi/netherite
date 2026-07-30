#!/usr/bin/env python3
"""Cross-check blaze render_opt_obs_hook vs render-opt kernel 15_light_combine_pack.

Extracts sky/block light from the post-init TLC slice (same path as the hook), pipes
"sky block 0" lines into ref/render-opt/kernels/15_light_combine_pack/candidate.c,
and compares packed outputs to blaze hook hex dump (bitwise on the low 32 bits).

  uv run --no-project python tools/crosscheck_render_opt_obs_hook.py [seed]
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _render_opt_root():
    for rel in ("ref/render-opt", "../mc-1.11.2-env/java/render-opt"):
        p = os.path.normpath(os.path.join(ROOT, rel))
        if os.path.isdir(p):
            return p
    return os.path.join(ROOT, "ref", "render-opt")


RENDER_OPT = _render_opt_root()
CAND = os.path.join(RENDER_OPT, "kernels", "15_light_combine_pack", "candidate.c")
SM = os.environ.get("MC_SM", "sm_120")


def build_cpu_hook(tmp):
    out = os.path.join(tmp, "hook_cpu")
    subprocess.run(
        ["cc", "-O2", "-ffp-contract=off", "-o", out,
         os.path.join(ROOT, "cpu", "render_opt_obs_hook.c"), "-lm"],
        check=True,
    )
    return out


def build_ro15(tmp):
    out = os.path.join(tmp, "ro15")
    subprocess.run(
        ["cc", "-O2", "-ffp-contract=off", "-o", out, CAND],
        check=True,
    )
    return out


def build_input_dumper(tmp):
    src = os.path.join(tmp, "dump_inputs.c")
    src_text = r'''
#include "render_opt_obs_hook.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    u64 seed = (argc > 1) ? strtoull(argv[1], 0, 10) : 12345ULL;
    Env e; TcfAux aux; TcfScratch scratch; ChunkPrimer primer; CpScratch sc; McSinTable st;
    u8 sky[TLC_SLICE_VOL], blk[TLC_SLICE_VOL]; u16 blocks[TLC_SLICE_VOL]; int i;
    mc_sin_table_init(&st);
    tcf_init_env(&e, &aux, seed, &primer, &sc, &st, &scratch);
    tlc_extract_slice(twc_now(&e), sky, blk, blocks);
    for (i = 0; i < ROOH_VOL; ++i)
        printf("%d %d 0\n", (int)sky[i], (int)blk[i]);
    return 0;
}
'''
    with open(src, "w") as f:
        f.write(src_text)
    out = os.path.join(tmp, "dump_inputs")
    subprocess.run(
        ["cc", "-O2", "-ffp-contract=off", "-I", os.path.join(ROOT, "core"),
         "-o", out, src, "-lm"],
        check=True,
    )
    return out


def main():
    seed = sys.argv[1] if len(sys.argv) > 1 else "12345"
    if not os.path.isfile(CAND):
        print(f"SKIP  render-opt candidate missing at {CAND}")
        sys.exit(0)

    with tempfile.TemporaryDirectory() as tmp:
        hook = build_cpu_hook(tmp)
        ro15 = build_ro15(tmp)
        dumper = build_input_dumper(tmp)

        hook_out = subprocess.run([hook, seed], capture_output=True, text=True, check=True)
        hook_lines = hook_out.stdout.splitlines()

        inp = subprocess.run([dumper, seed], capture_output=True, text=True, check=True)
        ro_out = subprocess.run([ro15], input=inp.stdout, capture_output=True, text=True, check=True)
        ro_lines = ro_out.stdout.splitlines()

    if len(hook_lines) != len(ro_lines):
        print(f"FAIL  line count {len(hook_lines)} vs {len(ro_lines)}")
        sys.exit(1)

    for i, (h, r) in enumerate(zip(hook_lines, ro_lines)):
        want = int(r, 10)
        got = int(h, 16)
        if got != want:
            print(f"FAIL  line {i}: hook={h} render-opt={r}")
            sys.exit(1)

    print(f"PASS  render_opt_obs_hook == render-opt kernel 15  ({len(hook_lines)} lines, seed={seed})")


if __name__ == "__main__":
    main()
