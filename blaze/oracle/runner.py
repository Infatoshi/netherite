#!/usr/bin/env python3
"""CPU-vs-CUDA oracle for blaze.

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

# Kernels whose default (no-argv) output is legitimately empty. Census of all 85 ALL_KERNELS
# (2026-08-02): every driver either prints a fixed number of lines (W_N / N-const / fixed
# printf) or has a multi-seed default that is nonzero. populate_light_shim was the only
# vacuous case (default seed 12345 emitted 0 diffs); fixed to seeds 9/19. No entries.
# Add an entry here only with a one-line why if a future kernel truly has empty oracle output.
EMPTY_OUTPUT_ALLOWLIST = frozenset({
    # (none; see census comment above)
})


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


def nvcc_present():
    return subprocess.run(["which", "nvcc"], capture_output=True).returncode == 0


def require_nonempty(name, label_a, a, label_b, b):
    """FATAL if both sides are empty unless name is explicitly allowlisted.

    Same rule as the 2026-07-29 pixel-gate fix: PASS over 0 comparisons is a
    harness failure, not a green gate.
    """
    if len(a) == 0 and len(b) == 0:
        if name in EMPTY_OUTPUT_ALLOWLIST:
            return
        print(
            f"FATAL  {name}: empty comparison ({label_a} and {label_b} both 0 lines). "
            f"Oracle emitted nothing on both sides - not a PASS. "
            f"If this kernel legitimately has empty output, add it to "
            f"EMPTY_OUTPUT_ALLOWLIST with a comment why."
        )
        sys.exit(1)


def require_cpu_output(name, cpu_out):
    """FATAL if the CPU oracle emitted nothing (unless allowlisted)."""
    if len(cpu_out) == 0 and name not in EMPTY_OUTPUT_ALLOWLIST:
        print(
            f"FATAL  {name}: empty CPU output (0 lines). "
            f"Oracle emitted nothing - not a PASS. "
            f"If this kernel legitimately has empty output, add it to "
            f"EMPTY_OUTPUT_ALLOWLIST with a comment why."
        )
        sys.exit(1)


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


def nvcc_status_note(cpu_only, have_nvcc, cuda_out):
    """Label why CUDA is absent from the comparison (never conflate empty emit with missing nvcc)."""
    if cpu_only:
        return "  [cpu-only]"
    if not have_nvcc:
        return "  [nvcc missing, CPU only]"
    if cuda_out is None:
        return "  [cuda build/run skipped]"
    if len(cuda_out) == 0:
        return "  [cuda ran, 0 lines]"
    return ""


def run_py_gym_env_smoke(args, cpu_only=False):
    """CPU==CUDA + determinism replay + optional pybind11 schema oracle."""
    name = "py_gym_env_smoke"
    have_nvcc = nvcc_present()
    have_cuda = (not cpu_only) and have_nvcc
    with tempfile.TemporaryDirectory() as tmp:
        cpu_bin = build_cpu(name, tmp)
        cpu_out = run(cpu_bin, args)
        cpu_out2 = run(cpu_bin, args)
        cuda_out = run(build_cuda(name, tmp), args) if have_cuda else None

    print(f"== {name} (arch {SM}) ==  {len(cpu_out)} lines"
          f"{nvcc_status_note(cpu_only, have_nvcc, cuda_out)}")
    require_cpu_output(name, cpu_out)
    require_nonempty(name, "cpu_run1", cpu_out, "cpu_run2", cpu_out2)
    diff("cpu_run1", cpu_out, "cpu_run2", cpu_out2)
    if cuda_out is not None:
        require_nonempty(name, "cpu", cpu_out, "cuda", cuda_out)
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
    have_nvcc = nvcc_present()
    have_cuda = (not cpu_only) and have_nvcc
    with tempfile.TemporaryDirectory() as tmp:
        cpu_out = run(build_cpu(name, tmp), args)
        gcmd = build_golden(name, tmp)
        golden_out = run(gcmd, args) if gcmd else None
        cuda_out = run(build_cuda(name, tmp), args) if have_cuda else None

    print(f"== {name} (arch {SM}) ==  {len(cpu_out)} lines"
          f"{'' if golden_out else '  [no Java golden]'}"
          f"{nvcc_status_note(cpu_only, have_nvcc, cuda_out)}")
    require_cpu_output(name, cpu_out)
    # Vanilla faithfulness (worldgen only): CPU must match the verbatim-Java golden.
    if golden_out is not None:
        require_nonempty(name, "java", golden_out, "cpu", cpu_out)
        diff("java", golden_out, "cpu", cpu_out)
    # Internal consistency (always): CPU must match CUDA bitwise.
    if cuda_out is not None:
        require_nonempty(name, "cpu", cpu_out, "cuda", cuda_out)
        diff("cpu", cpu_out, "cuda", cuda_out)
    print("PASS")


if __name__ == "__main__":
    main()
