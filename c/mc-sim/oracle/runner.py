#!/usr/bin/env python3
"""CPU-vs-CUDA oracle for mc-sim.

The fidelity contract (SPEC.md) is internal consistency: the CPU scalar path and the CUDA batch
path are the SAME __host__ __device__ source compiled two ways, and must produce BITWISE-identical
output on the same seed. This runner builds both, runs them on identical args, and diffs.

  uv run --no-project python oracle/runner.py smoke [seed] [n]
  uv run --no-project python oracle/runner.py --cpu-only cuda_batch_worldgen   # fast dev loop

Float discipline: CPU built with -ffp-contract=off, CUDA with --fmad=false (SPEC rule 4), so the
two agree to the last bit. CUDA arch defaults to sm_120 (this anvil); override with MC_SM.
Use --cpu-only (or MC_CPU_ONLY=1) to skip CUDA during iteration; run full oracle before commit.
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SM = os.environ.get("MC_SM", "sm_120")
NVCC = ["nvcc", f"-arch={SM}", "-O3", "--fmad=false"]
# Heavy worldgen device functions are MC_NOINLINE (see core/mc.h), so every kernel now compiles
# standalone in seconds - no cached-object / link workaround needed.


def build_cpu(name, tmp):
    out = os.path.join(tmp, name + "_cpu")
    subprocess.run(["cc", "-O2", "-ffp-contract=off", "-o", out,
                    os.path.join(ROOT, "cpu", name + ".c"), "-lm"], check=True)
    return out


def build_cuda(name, tmp):
    out = os.path.join(tmp, name + "_cuda")
    subprocess.run([*NVCC, "-o", out,
                    os.path.join(ROOT, "cuda", name + ".cu")], check=True)
    return out


def build_golden(name, tmp):
    """Verbatim-Java golden (vanilla ground truth) at oracle/goldens/<name>/Golden.java, if present."""
    src = os.path.join(ROOT, "oracle", "goldens", name, "Golden.java")
    if not os.path.exists(src):
        return None
    subprocess.run(["javac", "-d", tmp, src], check=True)
    return ["java", "-cp", tmp, "Golden"]


def run(cmd, args):
    cmd = cmd if isinstance(cmd, list) else [cmd]
    p = subprocess.run([*cmd, *args], capture_output=True, text=True, check=True)
    return p.stdout.splitlines()


def diff(label_a, a, label_b, b):
    if len(a) != len(b):
        print(f"FAIL  {label_a}=={label_b}: line count {len(a)} vs {len(b)}")
        sys.exit(1)
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            print(f"FAIL  {label_a}=={label_b}: line {i}: {label_a}={x!r} {label_b}={y!r}")
            sys.exit(1)
    print(f"  ok  {label_a} == {label_b}  ({len(a)} lines)")


def parse_args(argv):
    cpu_only = os.environ.get("MC_CPU_ONLY", "") not in ("", "0", "false")
    rest = list(argv)
    while rest and rest[0].startswith("-"):
        if rest[0] == "--cpu-only":
            cpu_only = True
            rest = rest[1:]
        else:
            raise SystemExit(f"unknown flag: {rest[0]}")
    if len(rest) < 1:
        raise SystemExit("usage: runner.py [--cpu-only] <name> [args...]")
    return cpu_only, rest[0], rest[1:]


def run_py_gym_env_smoke(args, cpu_only=False):
    """CPU==CUDA + determinism replay + optional pybind11 schema oracle."""
    name = "py_gym_env_smoke"
    have_cuda = (not cpu_only
                 and subprocess.run(["which", "nvcc"], capture_output=True).returncode == 0)
    with tempfile.TemporaryDirectory() as tmp:
        cpu_bin = build_cpu(name, tmp)
        cpu_out = run(cpu_bin, args)
        cpu_out2 = run(cpu_bin, args)
        cuda_out = run(build_cuda(name, tmp), args) if have_cuda else None

    skip = "  [cpu-only]" if cpu_only else ""
    print(f"== {name} (arch {SM}) ==  {len(cpu_out)} lines"
          f"{skip}"
          f"{'' if cuda_out else '  [nvcc skipped]' if cpu_only else '  [nvcc missing, CPU only]'}")
    diff("cpu_run1", cpu_out, "cpu_run2", cpu_out2)
    if cuda_out is not None:
        diff("cpu", cpu_out, "cuda", cuda_out)

    oracle = os.path.join(ROOT, "oracle", "py_gym_env_smoke.py")
    subprocess.run(
        [sys.executable, oracle, *args],
        check=True,
        cwd=ROOT,
    )
    print("PASS")


def main():
    cpu_only, name, args = parse_args(sys.argv[1:])
    if name == "py_gym_env_smoke":
        run_py_gym_env_smoke(args, cpu_only=cpu_only)
        return
    have_cuda = (not cpu_only
                 and subprocess.run(["which", "nvcc"], capture_output=True).returncode == 0)
    with tempfile.TemporaryDirectory() as tmp:
        cpu_out = run(build_cpu(name, tmp), args)
        gcmd = build_golden(name, tmp)
        golden_out = run(gcmd, args) if gcmd else None
        cuda_out = run(build_cuda(name, tmp), args) if have_cuda else None

    skip_note = "  [cpu-only]" if cpu_only else ""
    if cuda_out:
        nvcc_note = ""
    elif cpu_only:
        nvcc_note = "  [nvcc skipped]"
    else:
        nvcc_note = "  [nvcc missing, CPU only]"
    print(f"== {name} (arch {SM}) ==  {len(cpu_out)} lines"
          f"{'' if golden_out else '  [no Java golden]'}"
          f"{skip_note}{nvcc_note}")
    # Vanilla faithfulness (worldgen only): CPU must match the verbatim-Java golden.
    if golden_out is not None:
        diff("java", golden_out, "cpu", cpu_out)
    # Internal consistency (always): CPU must match CUDA bitwise.
    if cuda_out is not None:
        diff("cpu", cpu_out, "cuda", cuda_out)
    print("PASS")


if __name__ == "__main__":
    main()
