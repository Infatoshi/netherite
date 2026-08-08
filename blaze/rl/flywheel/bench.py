"""flywheelopt benchmark harness: measure M, decide keep/revert, receipt it.

M (minimize) = median `chunk_wall_ms` over --reps independent processes, each
running BENCH_WARMUP_CHUNKS post-init warm-up chunks and then timing ONE
complete training chunk (rollout + GAE + PPO update) between two device
syncs. The pinned config is identical to the ppo-native-bf16 lane so the two
scoreboards are directly comparable:

    N_ENVS=6144 T_CHUNK=32 REPEAT=4 EPOCHS=2 MB=8192, GPU0 exclusive.

Reciprocal reported as env-ticks/s = N_ENVS*T_CHUNK*REPEAT / wall_s.

Every config named on the command line is measured back-to-back inside ONE
hold of the GPU0 flock, so an A/B pair cannot be split by another tenant.
The run aborts before measuring anything if either GPU is busy.

Keep rule (from the lane contract): correctness passes AND relative gain
>= EPS (0.02) AND absolute gain >= MDD = noise_k * stderr(baseline).

Usage:
  python blaze/rl/flywheel/bench.py --reps 5 \
      --cfg baseline: --cfg act_cache:ACT_CACHE=1
"""
import argparse
import fcntl
import json
import os
import re
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
# Lane directory under optloop_runs/. Default unchanged (flywheelopt-v1);
# override via --lane or OPTLOOP_LANE for sibling A/B lanes (e.g. chanlast-v1).
DEFAULT_LANE = os.environ.get("OPTLOOP_LANE", "flywheelopt-v1")
LOCK = "/home/infatoshi/dev/nw/.tmp/gpu0.lock"

EPS = 0.02
NOISE_K = 2.0

PINNED = {
    "N_ENVS": "6144",
    "T_CHUNK": "32",
    "EPOCHS": "2",
    "MB": "8192",
    "MAX_TICKS": "1e12",
    "MAX_WALL": "3600",
    "PYTHONHASHSEED": "0",
    "RNG_SEED": "0",
    "CUDA_VISIBLE_DEVICES": "0",
    "UV_CACHE_DIR": "/home/infatoshi/.cache/uv",
    "TMPDIR": "/home/infatoshi/dev/nw/.tmp",
}
UV = ["uv", "run", "--no-project", "--with", "numpy==2.5.1",
      "--with", "torch==2.13.0", "python"]
BENCH_RE = re.compile(r"^BENCH chunk=(\d+) wall_ms=([0-9.]+) "
                      r"env_ticks_per_s=([0-9.]+)")


def smi(query, extra=()):
    out = subprocess.run(
        ["nvidia-smi", f"--query-{query}", "--format=csv,noheader,nounits",
         *extra], capture_output=True, text=True, check=True).stdout
    return [[c.strip() for c in ln.split(",")]
            for ln in out.strip().splitlines() if ln.strip()]


def hw_stamp():
    cols = ("index,name,uuid,clocks.sm,clocks.mem,memory.total,memory.used,"
            "utilization.gpu,clocks_throttle_reasons.active")
    gpus = []
    for r in smi(f"gpu={cols}"):
        gpus.append(dict(zip(
            ("index", "name", "uuid", "clock_sm_mhz", "clock_mem_mhz",
             "memory_total_mib", "memory_used_mib", "util_pct", "throttle"),
            r)))
    drv = smi("gpu=driver_version")[0][0]
    return {"hostname": os.uname().nodename, "driver": drv, "gpus": gpus,
            "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}


def preflight(max_mem_mib=512, max_util=5):
    """Both GPUs idle, or abort. A co-tenant on GPU1 still perturbs GPU0
    (shared host, shared PCIe), so the contract requires both idle."""
    bad = []
    procs = smi("compute-apps=gpu_uuid,pid,used_memory")
    for g in hw_stamp()["gpus"]:
        if int(g["memory_used_mib"]) > max_mem_mib:
            bad.append(f"gpu{g['index']} mem {g['memory_used_mib']} MiB")
        if int(g["util_pct"]) > max_util:
            bad.append(f"gpu{g['index']} util {g['util_pct']}%")
    if procs:
        bad.append(f"compute apps running: {procs}")
    if bad:
        print("PREFLIGHT ABORT: " + "; ".join(bad), file=sys.stderr)
        sys.exit(2)
    print("preflight ok: both GPUs idle", flush=True)


def parse_cfg(spec):
    name, _, rest = spec.partition(":")
    env = {}
    for kv in filter(None, rest.split(",")):
        k, _, v = kv.partition("=")
        env[k] = v
    return name, env


def one_rep(env_over, warmup, log_path):
    env = dict(os.environ)
    env.update(PINNED)
    env["BENCH_WARMUP_CHUNKS"] = str(warmup)
    env["BENCH_MEASURE_CHUNKS"] = "1"
    env.update(env_over)
    cgraph_bin = env.get("CGRAPH_BIN")
    cmd = ([cgraph_bin] if cgraph_bin else
           UV + [os.path.join(REPO, "blaze", "env", "ppo_chain_cu.py")])
    t0 = time.perf_counter()
    out = subprocess.run(cmd, cwd=REPO, env=env, capture_output=True,
                         text=True, check=False)
    dt = time.perf_counter() - t0
    with open(log_path, "a") as f:
        f.write(f"$ {env_over}\n{out.stdout}\n--- stderr ---\n{out.stderr}\n")
    if out.returncode != 0:
        raise RuntimeError(f"rep failed rc={out.returncode}; see {log_path}")
    vals = [float(m.group(2)) for m in
            (BENCH_RE.match(ln) for ln in out.stdout.splitlines()) if m]
    if len(vals) != 1:
        raise RuntimeError(f"expected 1 BENCH line, got {len(vals)}")
    print(f"    rep wall_ms={vals[0]:.3f} (process {dt:.1f}s)", flush=True)
    return vals[0]


def stats(samples):
    n = len(samples)
    mean = statistics.fmean(samples)
    std = statistics.stdev(samples) if n > 1 else 0.0
    return {"n": n, "mean": mean, "std": std, "min": min(samples),
            "max": max(samples), "median": statistics.median(samples),
            "cv": (std / mean) if mean else 0.0,
            "stderr": (std / (n ** 0.5)) if n else 0.0}


def load_baseline(scoreboard):
    if not os.path.exists(scoreboard):
        return None
    base = None
    with open(scoreboard) as f:
        for ln in f:
            r = json.loads(ln)
            if r.get("phase") == "baseline":
                base = r
    return base


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfg", action="append", required=True,
                    help="NAME:K=V,K=V  (empty tail = unmodified path)")
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=2)
    ap.add_argument("--phase", default="candidate",
                    choices=["baseline", "candidate", "probe"])
    ap.add_argument("--note", default="")
    ap.add_argument("--correctness", default="unrun",
                    choices=["unrun", "pass", "fail"])
    ap.add_argument("--no-record", action="store_true")
    ap.add_argument("--lane", default=DEFAULT_LANE,
                    help="optloop_runs/<lane> directory name "
                         "(default: flywheelopt-v1 or $OPTLOOP_LANE)")
    args = ap.parse_args()

    lane = os.path.join(REPO, "optloop_runs", args.lane)
    scoreboard = os.path.join(lane, "scoreboard.jsonl")
    os.makedirs(lane, exist_ok=True)
    log_path = os.path.join(lane, "bench.log")
    print(f"lane={args.lane}  scoreboard={scoreboard}", flush=True)
    print("waiting for gpu0 lock ...", flush=True)
    with open(LOCK, "a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            preflight()
            hw = hw_stamp()
            base = load_baseline(scoreboard)
            results = []
            for spec in args.cfg:
                name, env_over = parse_cfg(spec)
                print(f"[{name}] {env_over or 'unmodified'}  x{args.reps}",
                      flush=True)
                samples = [one_rep(env_over, args.warmup, log_path)
                           for _ in range(args.reps)]
                st = stats(samples)
                rec = {
                    "hypothesis": name,
                    "note": args.note,
                    "phase": "baseline" if (args.phase == "baseline" and
                                            name == "baseline") else args.phase,
                    "metric_name": "chunk_wall_ms",
                    "metric": st["median"],
                    "env_ticks_per_s": 6144 * 32 * 4 / (st["median"] / 1000.0),
                    "reps": args.reps,
                    "samples": samples,
                    "stats": st,
                    "env_overrides": env_over,
                    "pinned": PINNED,
                    "correctness_ok": args.correctness,
                    "hw": hw,
                    "git_head": subprocess.run(
                        ["git", "rev-parse", "HEAD"], cwd=REPO,
                        capture_output=True, text=True,
                        check=False).stdout.strip(),
                    "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                }
                # In a multi-cfg A/B under one lock hold, the first cfg named
                # "baseline" becomes the in-run reference for later cfgs when
                # no prior scoreboard baseline exists (or phase=baseline).
                if name == "baseline" and base is None:
                    base = rec
                ref = base if (args.phase != "baseline" or
                               name != "baseline") else None
                if ref and name != "baseline":
                    mdd = NOISE_K * ref["stats"]["stderr"]
                    gain = ref["metric"] - st["median"]
                    rec["baseline_metric"] = ref["metric"]
                    rec["mdd"] = {"epsilon": EPS, "noise_k": NOISE_K,
                                  "baseline_stderr": ref["stats"]["stderr"],
                                  "mdd_abs": mdd,
                                  "eps_abs": EPS * ref["metric"]}
                    rec["gain_abs_ms"] = gain
                    rec["gain_rel"] = gain / ref["metric"]
                    keep = (args.correctness == "pass" and
                            gain / ref["metric"] >= EPS and
                            gain >= mdd)
                    rec["keep"] = keep
                    rec["keep_reason"] = (
                        f"rel={gain / ref['metric']:+.4f} abs={gain:+.2f}ms "
                        f"mdd={mdd:.2f}ms eps_abs={EPS * ref['metric']:.2f}ms "
                        f"correctness={args.correctness}")
                    print(f"  -> median {st['median']:.2f} ms  "
                          f"{rec['keep_reason']}  keep={keep}", flush=True)
                else:
                    print(f"  -> median {st['median']:.2f} ms  "
                          f"stderr {st['stderr']:.2f}  cv {st['cv']:.4f}",
                          flush=True)
                results.append(rec)
                if not args.no_record:
                    with open(scoreboard, "a") as f:
                        f.write(json.dumps(rec, sort_keys=True) + "\n")
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)
    return 0


if __name__ == "__main__":
    sys.exit(main())
