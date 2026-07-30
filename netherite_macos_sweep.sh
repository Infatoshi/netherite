#!/usr/bin/env bash
# Native Apple Silicon verification pyramid.  This is intentionally separate
# from netherite_sweep.sh so Linux/CUDA gates and their performance pins remain
# untouched.
#
#   bash netherite_macos_sweep.sh --quick
#   bash netherite_macos_sweep.sh --full
#   bash netherite_macos_sweep.sh --full --skip-build
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MAGMA="$ROOT/c/magma"
MCSIM="$ROOT/c/mc-sim"
SNAPS="$MAGMA/rl/out/snaps"

MODE=quick
DO_BUILD=1
while [ $# -gt 0 ]; do
  case "$1" in
    --quick) MODE=quick ;;
    --full) MODE=full ;;
    --skip-build) DO_BUILD=0 ;;
    -h|--help)
      sed -n '2,8p' "$0" | sed 's/^#[[:space:]]*//'
      exit 0
      ;;
    *)
      echo "usage: $0 [--quick|--full] [--skip-build]" >&2
      exit 2
      ;;
  esac
  shift
done

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
LOGDIR="${NETHERITE_LOG_DIR:-${TMPDIR:-/tmp}/netherite_macos_sweep_$STAMP}"
mkdir -p "$LOGDIR"

TIMEOUT_BIN=""
if command -v gtimeout >/dev/null 2>&1; then
  TIMEOUT_BIN="$(command -v gtimeout)"
elif command -v timeout >/dev/null 2>&1; then
  TIMEOUT_BIN="$(command -v timeout)"
fi

# `/usr/bin/java[c]` can exist as a launcher stub on macOS even when no JDK is
# installed.  Probe execution, then explicitly suppress only Java-backed
# goldens in the native sweep.  The runner remains strict by default so Linux
# and developer-invoked Java comparisons cannot silently disappear.
HAVE_JAVA_GOLDENS=0
if command -v javac >/dev/null 2>&1 && \
   command -v java >/dev/null 2>&1 && \
   javac -version >/dev/null 2>&1 && java -version >/dev/null 2>&1; then
  HAVE_JAVA_GOLDENS=1
fi
JAVA_GOLDEN_ENV=(env)
if [ "$HAVE_JAVA_GOLDENS" -eq 0 ]; then
  JAVA_GOLDEN_ENV=(env MC_SKIP_JAVA_GOLDENS=1)
fi

NAMES=()
STATUSES=()
DETAILS=()
SECS=()
NFAIL=0

record() {
  NAMES+=("$1")
  STATUSES+=("$2")
  DETAILS+=("$3")
  SECS+=("$4")
  case "$2" in
    PASS) printf '[PASS] %-27s (%ss)\n' "$1" "$4" ;;
    SKIP) printf '[SKIP] %-27s %s\n' "$1" "$3" ;;
    FAIL)
      printf '[FAIL] %-27s %s (%ss, log: %s)\n' \
        "$1" "$3" "$4" "$LOGDIR/$1.log"
      NFAIL=$((NFAIL + 1))
      ;;
  esac
}

skip() { record "$1" SKIP "$2" 0; }

# run_step NAME TIMEOUT_SECONDS WORKDIR COMMAND...
run_step() {
  local name="$1" tmo="$2" dir="$3"
  shift 3
  local log="$LOGDIR/$name.log"
  local t0 t1 rc last
  t0="$(date +%s)"
  if [ -n "$TIMEOUT_BIN" ]; then
    (cd "$dir" && "$TIMEOUT_BIN" -k 15 "$tmo" "$@") >"$log" 2>&1
  else
    (cd "$dir" && "$@") >"$log" 2>&1
  fi
  rc=$?
  t1="$(date +%s)"
  if [ "$rc" -eq 0 ]; then
    record "$name" PASS "" $((t1 - t0))
  elif [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
    record "$name" FAIL "timeout after ${tmo}s" $((t1 - t0))
  else
    last="$(tail -1 "$log" 2>/dev/null | cut -c1-100)"
    record "$name" FAIL "rc=$rc: $last" $((t1 - t0))
  fi
}

echo "netherite macOS sweep - mode=$MODE ($STAMP)"
echo "logs: $LOGDIR"
if [ -z "$TIMEOUT_BIN" ]; then
  echo "note: GNU timeout not installed; steps run without a watchdog"
fi
echo

if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
  record host-preflight FAIL "requires a native Apple Silicon macOS process" 0
else
  record host-preflight PASS "" 0
fi
if [ "$HAVE_JAVA_GOLDENS" -eq 0 ]; then
  skip java-golden-runtime \
    "working JDK unavailable; native CPU kernels still run, live Java comparisons are suppressed"
fi
run_step metal-runtime 90 "$ROOT" bash scripts/check_metal.sh

if [ "$DO_BUILD" -eq 1 ]; then
  run_step build-magma-native 1200 "$MAGMA" make \
    demo world game game-dbg game-metal
  run_step build-mcsim-metal 600 "$MCSIM" make metal-all
  run_step build-blaze-native 900 "$MAGMA" make \
    blaze_so blaze_verify blaze_metal_dylib blaze_metal_check
fi

run_step native-architecture 60 "$ROOT" bash -c '
  set -e
  deps=""
  for artifact in \
    c/magma/magma_game \
    c/magma/magma_game_metal \
    c/mc-sim/build/bin/metal/mcsim_metal_oracle \
    c/magma/rl/blaze/blaze_metal.dylib; do
    test -s "$artifact"
    file "$artifact" | grep -q "Mach-O.*arm64"
    deps="$(otool -L "$artifact")"
    if printf "%s\n" "$deps" | grep -Eiq "cuda|nvidia"; then
      echo "$artifact unexpectedly links an NVIDIA/CUDA library" >&2
      exit 1
    fi
    echo "$artifact: arm64"
  done
'

# CPU references stay in the native pyramid: Metal parity is only meaningful
# when the host reference continues to pass unchanged.
run_step mcsim-oracle-smoke 300 "$MCSIM" "${JAVA_GOLDEN_ENV[@]}" \
  env MC_CPU_ONLY=1 \
  uv run --no-project python oracle/runner.py smoke 12345 256
run_step mcsim-cpu-trunk 900 "$MCSIM" "${JAVA_GOLDEN_ENV[@]}" \
  make verify-cpu-trunk \
  PYTHON="uv run --no-project python"
run_step mcsim-metal-parity 600 "$MCSIM" make verify-metal \
  PYTHON="uv run --no-project python"

run_step magma-test-config 300 "$MAGMA" make test-config
run_step magma-block-registry 300 "$MAGMA" make test-block-registry
run_step magma-test-launch-cpu 300 "$MAGMA" make test-launch
run_step magma-metal-parity 900 "$MAGMA" make test-metal

FIRST_SNAP="$(find "$SNAPS" -maxdepth 1 -name '*.bsnp' 2>/dev/null | sort | head -1)"
CHAIN_SNAP="$SNAPS/s10_t0.bsnp"
CHAIN_ACTIONS="$MAGMA/rl/out/chain_actions_s10.json"
HAVE_CHAIN=0
if [ -s "$CHAIN_SNAP" ] && [ -s "$CHAIN_ACTIONS" ]; then
  HAVE_CHAIN=1
fi
METAL_SNAP="$CHAIN_SNAP"
if [ ! -s "$METAL_SNAP" ]; then
  METAL_SNAP="$FIRST_SNAP"
fi
if [ -z "$FIRST_SNAP" ]; then
  skip blaze-c-smoke "no .bsnp snapshot (run T0=1 make_snapshots.py)"
else
  run_step blaze-c-smoke 600 "$MAGMA" ./rl/blaze/blaze_verify \
    "$FIRST_SNAP" 32 50 4
fi
if [ "$HAVE_CHAIN" -eq 1 ]; then
  run_step blaze-cpu-parity 1800 "$MAGMA" uv run --no-project \
    --with numpy python rl/blaze/verify_cpu.py --chain
else
  skip blaze-cpu-parity \
    "canonical s10 snapshot/action tape missing (run T0=1 make_snapshots.py)"
fi

# Keep this array non-empty for Bash 3.2 + `set -u`; N=7 is the verifier's
# normal default and also makes the invoked workload explicit in logs.
VERIFY_ARGS=(--n 7)
if [ -n "$METAL_SNAP" ]; then
  VERIFY_ARGS+=(--snapshot "$METAL_SNAP")
fi
VERIFY_TIMEOUT=1800
if [ "$MODE" = quick ]; then
  VERIFY_ARGS+=(--quick)
  VERIFY_TIMEOUT=900
fi
if [ "$HAVE_CHAIN" -eq 1 ]; then
  run_step blaze-metal-parity "$VERIFY_TIMEOUT" "$MAGMA" \
    uv run --no-project --with numpy python rl/blaze/verify_metal.py \
    "${VERIFY_ARGS[@]}" --tails
else
  # Synthetic/default and mixed streams remain meaningful without generated
  # artifacts.  The committed-chain omission stays visible as an explicit
  # SKIP rather than failing inside --stream all or comparing an arbitrary
  # snapshot as though it were canonical s10.
  run_step blaze-metal-default "$VERIFY_TIMEOUT" "$MAGMA" \
    uv run --no-project --with numpy python rl/blaze/verify_metal.py \
    "${VERIFY_ARGS[@]}" --stream default --tails
  run_step blaze-metal-mixed "$VERIFY_TIMEOUT" "$MAGMA" \
    uv run --no-project --with numpy python rl/blaze/verify_metal.py \
    "${VERIFY_ARGS[@]}" \
    --stream mixed --no-error-paths
  skip blaze-metal-chain \
    "canonical s10 snapshot/action tape missing (run T0=1 make_snapshots.py)"
fi

if [ -n "$METAL_SNAP" ]; then
  run_step blaze-mps-train-smoke 1800 "$MAGMA" env BLAZE_BACKEND=metal \
    BLAZE_SNAPSHOT="$METAL_SNAP" N_ENVS=32 T_CHUNK=2 MB=16 \
    uv run --no-project --with numpy,torch python rl/blaze/mps_smoke.py
else
  run_step blaze-mps-train-smoke 1800 "$MAGMA" env BLAZE_BACKEND=metal \
    N_ENVS=32 T_CHUNK=2 MB=16 uv run --no-project --with numpy,torch \
    python rl/blaze/mps_smoke.py
fi

# Reward arithmetic remains a backend-independent contract. Force this gate
# onto CPU tensors so the MPS smoke above does not conceal a host dependency.
run_step blaze-reward-parity 600 "$MAGMA" env CUDA_VISIBLE_DEVICES="" \
  uv run --no-project --with numpy,torch python rl/blaze/test_reward_chain.py

if [ "$MODE" = full ]; then
  run_step mcsim-cpu-physics 1200 "$MCSIM" "${JAVA_GOLDEN_ENV[@]}" \
    make verify-cpu-physics \
    PYTHON="uv run --no-project python"
  run_step mcsim-cpu-tick 1200 "$MCSIM" "${JAVA_GOLDEN_ENV[@]}" \
    make verify-cpu-tick \
    PYTHON="uv run --no-project python"
  if [ "$HAVE_JAVA_GOLDENS" -eq 1 ]; then
    run_step java-cpu-mathhelper 600 "$MCSIM" make verify-cpu-mathhelper \
      PYTHON="uv run --no-project python"
  else
    skip java-cpu-mathhelper \
      "JDK unavailable (external prerequisite for Java golden comparison)"
  fi
  run_step magma-test-game 1800 "$MAGMA" make test-game
  run_step magma-harsh-structural 900 "$MAGMA" make \
    test-jar-models test-model-oracle test-mesh
  if [ -s "$MAGMA/raster/verify/mc_capture/mc_frame.png" ]; then
    run_step magma-harsh-pixels 1800 "$MAGMA" make \
      hard-scene-verify multi-verify
  else
    skip magma-harsh-pixels \
      "mc_frame.png missing (external real-Minecraft pixel golden)"
  fi
  run_step magma-unit-gates 900 "$MAGMA" uv run --no-project \
    --with pytest --with numpy --with scipy --with pillow --with pyyaml \
    pytest -q raster/verify/trace/test_replay_tape.py \
    raster/verify/scenarios/test_scenario.py

  if [ -s "$MAGMA/rl/out/coal_prefixes.json" ]; then
    run_step blaze-vec-env-parity 1200 "$MAGMA" uv run --no-project \
      --with numpy,torch python rl/test_vec_env.py
  else
    skip blaze-vec-env-parity \
      "coal_prefixes.json missing (external generated verification artifact)"
  fi

  # Non-gating measurements.  Logs record FPS, env-ticks/s, unified-memory
  # sizing, and explicit shared-host-to-MPS copy time on this machine.
  run_step magma-cpu-benchmark 1800 "$MAGMA" make bench-macos-cpu \
    METAL_BENCH_FRAMES=600 METAL_BENCH_WARMUP=120
  run_step magma-metal-benchmark 1200 "$MAGMA" make bench-metal \
    METAL_BENCH_FRAMES=600 METAL_BENCH_WARMUP=120
  if [ -z "$FIRST_SNAP" ]; then
    skip blaze-metal-benchmark "no .bsnp snapshot"
  else
    run_step blaze-metal-benchmark 1800 "$MAGMA" uv run --no-project \
      --with numpy python rl/blaze/verify_metal.py --snapshot "$METAL_SNAP" \
      --bench-only --n 256 --decisions 100
  fi
fi

echo
echo "summary:"
for i in "${!NAMES[@]}"; do
  printf '  %-27s %-4s %ss' "${NAMES[$i]}" "${STATUSES[$i]}" "${SECS[$i]}"
  if [ -n "${DETAILS[$i]}" ]; then
    printf '  %s' "${DETAILS[$i]}"
  fi
  printf '\n'
done
echo "logs: $LOGDIR"

if [ "$NFAIL" -ne 0 ]; then
  echo "FAIL: $NFAIL macOS sweep step(s) failed" >&2
  exit 1
fi
echo "PASS: native Apple Silicon CPU/Metal sweep"
