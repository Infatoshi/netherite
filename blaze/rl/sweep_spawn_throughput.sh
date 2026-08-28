#!/usr/bin/env bash
# Spawn-t0 CUDA throughput sweep. Writes blaze/rl/figures/spawn_throughput.json.
# Plot with plot_spawn_throughput.py.
#
# Axes (intentionally small):
#   1. worlds N            — sim occupancy vs trainer VRAM
#   2. trainer lockstep    — pack / policy / env H2D+D2H / PPO
#   3. action_repeat       — ticks per policy decision (the amortize knob that exists)
# Not swept: nether/dragon/biome, async queues, curriculum stages.
#
# Sim-only numbers are k_tick+k_obs+k_final from ktime (device kernels).
# Trainer e2e includes host pack, policy, H2D/D2H, PPO. Do not mix Torch
# ctypes + a second CUDA context for this bench (that hung gpu0).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

export PATH="/usr/local/cuda/bin:${HOME}/.local/bin:${PATH}"
export CUDA_HOME="${CUDA_HOME:-/usr/local/cuda}"
export CUDA_VISIBLE_DEVICES="${CUDA_VISIBLE_DEVICES:-0}"
export TMPDIR="${TMPDIR:-$ROOT/out/blaze/rl/tput/tmp}"
mkdir -p "$TMPDIR" "$ROOT/out/blaze/rl/tput" "$ROOT/blaze/rl/figures"

FIXTURE="${FIXTURE:-verify/fixtures/port/s10_t0_r64_no_liquid.bsnp}"
SO="${SO:-out/blaze/env/blaze_cuda.so}"
PPO="${PPO:-out/blaze/rl/ppo}"
# With CUDA_VISIBLE_DEVICES set, ppo device=0 is that visible GPU.
DEVICE="${DEVICE:-0}"
SMI_INDEX="${SMI_INDEX:-${CUDA_VISIBLE_DEVICES%%,*}}"
AGENT="${AGENT:-netherite-nn-fable}"
NS="${NS:-128,256,512,1024,2048}"
REPEATS_N="${REPEATS_N:-256}"
REPEATS="${REPEATS:-1,4,8}"
T_ROLLOUT="${T_ROLLOUT:-8}"
MAX_CHUNKS="${MAX_CHUNKS:-3}"
RUN_TIMEOUT="${RUN_TIMEOUT:-600}"
HEARTBEAT_TTL="${HEARTBEAT_TTL:-7h}"
JSON_OUT="${JSON_OUT:-blaze/rl/figures/spawn_throughput.json}"

if [[ ! -x "$PPO" ]]; then
  echo "missing ppo binary: $PPO" >&2
  exit 1
fi
if [[ ! -f "$SO" ]]; then
  echo "missing cuda so: $SO" >&2
  exit 1
fi
if [[ ! -f "$FIXTURE" ]]; then
  echo "missing fixture: $FIXTURE" >&2
  exit 1
fi

nvidia-smi -i "$SMI_INDEX" --query-gpu=index,name,memory.used,utilization.gpu --format=csv
overnight-compute heartbeat --agent "$AGENT" --ttl "$HEARTBEAT_TTL" >/dev/null || true

GPU_NAME="$(nvidia-smi -i "$SMI_INDEX" --query-gpu=name --format=csv,noheader | head -1 | tr -d '\r')"
HOST_NAME="$(hostname -s)"
STAMP="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

IFS=',' read -r -a N_LIST <<<"$NS"
IFS=',' read -r -a R_LIST <<<"$REPEATS"

train_one() {
  local n="$1" repeat="$2" t="$3"
  local log="out/blaze/rl/tput/train_n${n}_r${repeat}_t${t}.log"
  local chunks="$MAX_CHUNKS"
  echo "TRAIN n=$n repeat=$repeat T=$t chunks=$chunks timeout=${RUN_TIMEOUT}s"
  overnight-compute heartbeat --agent "$AGENT" --ttl "$HEARTBEAT_TTL" >/dev/null || true
  set +e
  timeout "$RUN_TIMEOUT" "$PPO" --conf blaze/rl/ppo.conf \
    --set backend=cuda \
    --set device="$DEVICE" \
    --set n_envs="$n" \
    --set rollout_steps="$t" \
    --set action_repeat="$repeat" \
    --set max_chunks="$chunks" \
    --set ckpt_ticks=999999999 \
    --set ktime=1 \
    --set warp_tick=1 \
    --set train_seeds=fixture \
    --set fixture="$FIXTURE" \
    --set checkpoint=out/blaze/rl/tput/bench_ckpt.bin \
    --set epochs=1 \
    --set mb=0 \
    >"$log" 2>&1
  local rc=$?
  set -e
  echo "  rc=$rc log=$log"
  if grep -q "phases pack" "$log" 2>/dev/null; then
    grep "phases pack\|blaze_cuda ktime\|ppo: chunk=" "$log" | tail -20
  else
    echo "  (no phases line; tail follows)"
    tail -15 "$log" || true
  fi
}

echo "== trainer N sweep T=${T_ROLLOUT} repeat=4 =="
for n in "${N_LIST[@]}"; do
  train_one "$n" 4 "$T_ROLLOUT"
done

echo "== action_repeat at N=${REPEATS_N} T=${T_ROLLOUT} =="
for r in "${R_LIST[@]}"; do
  if [[ "$r" != "4" ]]; then
    train_one "$REPEATS_N" "$r" "$T_ROLLOUT"
  fi
done

python3 - "$JSON_OUT" "$HOST_NAME" "$GPU_NAME" "$STAMP" "$FIXTURE" <<'PY'
import json, os, re, sys, glob
out, host, gpu, stamp, fixture = sys.argv[1:6]
runs = []

def parse_ktime(text):
    m = re.search(
        r"blaze_cuda ktime: (\d+) steps\s+k_tick ([\d.]+) ms \(([\d.]+) ms/step\)\s+"
        r"k_obs ([\d.]+) ms \(([\d.]+) ms/step\)\s+"
        r"k_final ([\d.]+) ms \(([\d.]+) ms/step\)",
        text,
    )
    if not m:
        return {}
    return {
        "k_steps": int(m.group(1)),
        "ms_ktick": float(m.group(2)),
        "ms_ktick_step": float(m.group(3)),
        "ms_kobs": float(m.group(4)),
        "ms_kobs_step": float(m.group(5)),
        "ms_kfinal": float(m.group(6)),
        "ms_kfinal_step": float(m.group(7)),
    }

phase_re = re.compile(
    r"ppo: phases pack=([\d.]+)ms nn=([\d.]+)ms env=([\d.]+)ms "
    r"host=([\d.]+)ms upd=([\d.]+)ms"
)

for path in sorted(glob.glob("out/blaze/rl/tput/train_n*.log")):
    text = open(path, encoding="utf-8", errors="replace").read()
    nm = re.search(r"train_n(\d+)_r(\d+)_t(\d+)", os.path.basename(path))
    rec = {"kind": "train", "log": path, "ok": False}
    if nm:
        rec["n_envs"] = int(nm.group(1))
        rec["repeat"] = int(nm.group(2))
        rec["rollout_steps"] = int(nm.group(3))
    phases = [(float(a), float(b), float(c), float(d), float(e))
              for a, b, c, d, e in phase_re.findall(text)]
    rec.update(parse_ktime(text))
    low = text.lower()
    rec["oom"] = ("out of memory" in low or "cudamalloc" in low
                  or "oom" in low)
    rec["timeout"] = "terminated" in low or bool(re.search(r"timeout: ", low))
    if len(phases) >= 2:
        p = phases[1:]
        nph = len(p)
        pack = sum(x[0] for x in p) / nph
        nn = sum(x[1] for x in p) / nph
        env = sum(x[2] for x in p) / nph
        host_ms = sum(x[3] for x in p) / nph
        upd = sum(x[4] for x in p) / nph
        wall_ms = pack + nn + env + host_ms + upd
        n = rec["n_envs"]
        T = rec["rollout_steps"]
        R = rec["repeat"]
        ticks_chunk = n * T * R
        rec.update({
            "ok": True,
            "ms_pack": pack,
            "ms_nn": nn,
            "ms_env": env,
            "ms_host": host_ms,
            "ms_upd": upd,
            "ms_chunk": wall_ms,
            "n_chunks_avg": nph,
            "ticks_per_s": ticks_chunk / (wall_ms / 1000.0) if wall_ms > 0 else 0.0,
            "decisions_per_s": (n * T) / (wall_ms / 1000.0) if wall_ms > 0 else 0.0,
        })
        p1 = phases[1]
        wall_c1 = sum(p1)
        rec.update({
            "ms_pack_c1": p1[0],
            "ms_nn_c1": p1[1],
            "ms_env_c1": p1[2],
            "ms_host_c1": p1[3],
            "ms_upd_c1": p1[4],
            "ms_chunk_c1": wall_c1,
            "ticks_per_s_c1": ticks_chunk / (wall_c1 / 1000.0) if wall_c1 > 0 else 0.0,
            "decisions_per_s_c1": (n * T) / (wall_c1 / 1000.0) if wall_c1 > 0 else 0.0,
        })
        kstep = (rec.get("ms_ktick_step", 0.0) + rec.get("ms_kobs_step", 0.0)
                 + rec.get("ms_kfinal_step", 0.0))
        if kstep > 0:
            rec["kernel_ticks_per_s"] = n * R / (kstep / 1000.0)
            rec["kernel_decisions_per_s"] = n / (kstep / 1000.0)
            if wall_c1 > 0:
                rec["kernel_ticks_per_s_c1"] = rec["kernel_ticks_per_s"]
                rec["kernel_decisions_per_s_c1"] = rec["kernel_decisions_per_s"]
            # Fake a sim-only row so the plot's sim series works.
            runs.append({
                "kind": "sim",
                "ok": True,
                "n_envs": n,
                "repeat": R,
                "rollout_steps": 0,
                "ticks_per_s": rec["kernel_ticks_per_s"],
                "decisions_per_s": rec["kernel_decisions_per_s"],
                "source": "ktime kernels during trainer",
                "ms_ktick_step": rec.get("ms_ktick_step"),
                "ms_kobs_step": rec.get("ms_kobs_step"),
                "ms_kfinal_step": rec.get("ms_kfinal_step"),
            })
    else:
        rec["error"] = text[-2000:]
        rec["n_phases"] = len(phases)
    runs.append(rec)

doc = {
    "host": host,
    "gpu": gpu,
    "measured_utc": stamp,
    "fixture": fixture,
    "backend": "cuda",
    "warp_tick": 1,
    "notes": {
        "panel_a": "T=8 first-steady-chunk (phases[1]). k_tick wall is occupancy, not GPU-full. M3 4.06M is sim-only at N=8192.",
        "scope": "spawn t0 fixture only. not nether/dragon/biome.",
        "nn": "Fable NHWC cuDNN-graph conv + cublasLt TF32. host-copy trainer (not gpu-resident-ppo).",
        "metric": "trainer e2e env-ticks/s first-steady T=8 R=4. kernel t/s from ktime during trainer.",
    },
    "runs": runs,
}
os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
with open(out, "w", encoding="utf-8") as f:
    json.dump(doc, f, indent=2)
    f.write("\n")
print("wrote", out, "runs", len(runs))
print("ok", sum(1 for r in runs if r.get("ok")), "/", len(runs))
PY

echo "done $JSON_OUT"
