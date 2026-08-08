#!/usr/bin/env bash
# setup_and_verify.sh - one entry for a clean Linux box:
#   bootstrap Mojang-derived inputs -> build -> verification pyramid.
#
# Usage:
#   bash scripts/setup_and_verify.sh            # bootstrap + --quick sweep
#   bash scripts/setup_and_verify.sh --full     # + CUDA oracles / tape / parity
#   bash scripts/setup_and_verify.sh --demo     # after setup: pixel SBS MP4 demo
#   bash scripts/setup_and_verify.sh --bootstrap-only
#   bash scripts/setup_and_verify.sh --skip-bootstrap   # already bootstrapped
#
# Requirements (Linux x86_64, the only full-stack platform):
#   - JDK 8 (JAVA_HOME or /usr/lib/jvm/java-8-openjdk-amd64)
#   - uv, make, gcc/g++, git, network (first bootstrap)
#   - NVIDIA GPU + CUDA toolkit for --full (and for blaze CUDA steps)
#   - You own Minecraft; ForgeGradle downloads 1.11.2 from Mojang
#
# Prism/MultiMC is optional: only used if you already have a 1.11.2 jar and
# skip bootstrap_oracle. Otherwise bootstrap_oracle populates the jar cache.
#
# Does NOT run multi-hour RL training or live JVM policy demos. Those are
# gate-2 steps after this script is green (see docs/GATES.md).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

MODE=quick
DO_BOOTSTRAP=1
DO_DEMO=0
for arg in "$@"; do
  case "$arg" in
    --full) MODE=full ;;
    --quick) MODE=quick ;;
    --bootstrap-only) MODE=bootstrap_only ;;
    --skip-bootstrap) DO_BOOTSTRAP=0 ;;
    --demo) DO_DEMO=1 ;;
    -h|--help)
      sed -n '2,30p' "$0" | sed 's/^# \?//'
      exit 0
      ;;
    *)
      echo "unknown arg: $arg (try --help)" >&2
      exit 2
      ;;
  esac
done

if [ -z "${JAVA_HOME:-}" ]; then
  for j in /usr/lib/jvm/java-8-openjdk-amd64 \
           /usr/lib/jvm/temurin-8-jdk-amd64 \
           /usr/lib/jvm/java-1.8.0-openjdk; do
    [ -d "$j" ] && export JAVA_HOME="$j" && break
  done
fi
[ -n "${JAVA_HOME:-}" ] || {
  echo "ERROR: JDK 8 required. Set JAVA_HOME to a Java 8 install." >&2
  exit 2
}
"$JAVA_HOME/bin/java" -version 2>&1 | grep -q '1\.8\.' || {
  echo "ERROR: JAVA_HOME=$JAVA_HOME is not JDK 8" >&2
  exit 2
}

command -v uv >/dev/null || {
  echo "ERROR: uv not found (https://docs.astral.sh/uv/)" >&2
  exit 2
}
command -v make >/dev/null || {
  echo "ERROR: make not found" >&2
  exit 2
}

echo "== host =="
uname -a
echo "JAVA_HOME=$JAVA_HOME"
"$JAVA_HOME/bin/java" -version 2>&1 | head -1
if command -v nvidia-smi >/dev/null; then
  nvidia-smi -L || true
else
  echo "nvidia-smi: missing (CUDA steps will SKIP or fail under --full)"
fi

if [ "$DO_BOOTSTRAP" -eq 1 ]; then
  echo "== bootstrap oracle (ForgeGradle decomp MC 1.11.2) =="
  bash scripts/bootstrap_oracle.sh
  echo "== bootstrap assets (texture headers from jar) =="
  bash scripts/bootstrap_assets.sh
else
  echo "== skip bootstrap (caller promised oracle-src + asset headers) =="
  [ -d java/oracle-src/net/minecraft ] || {
    echo "ERROR: java/oracle-src missing; drop --skip-bootstrap" >&2
    exit 1
  }
fi

if [ "$MODE" = bootstrap_only ]; then
  echo "bootstrap complete; not running sweep"
  exit 0
fi

echo "== build magma =="
make -C magma game

# RL gate artifacts: snapshots for blaze CPU/CUDA steps (committed refs alone
# are not enough; .bsnp files are regenerable and gitignored).
if [ ! -d blaze/rl/out/snaps ] || [ -z "$(find blaze/rl/out/snaps -name '*.bsnp' 2>/dev/null | head -1)" ]; then
  echo "== make blaze snapshots (t0 + curriculum) =="
  make -C magma blaze_so || make -C magma game
  T0=1 uv run --no-project --with numpy,torch python blaze/env/make_snapshots.py
  uv run --no-project --with numpy,torch python blaze/env/make_snapshots.py
else
  echo "== blaze snapshots already present =="
fi

echo "== netherite_sweep --$MODE =="
bash netherite_sweep.sh --"$MODE"

if [ "$DO_DEMO" -eq 1 ]; then
  echo "== pixel fidelity demo (tape replay + SBS MP4) =="
  command -v ffmpeg >/dev/null || {
    echo "installing ffmpeg..."
    sudo apt-get update -qq && sudo apt-get install -y -qq ffmpeg || true
  }
  bash scripts/demo_pixel_sbs.sh --skip-build || bash scripts/demo_pixel_sbs.sh
fi

echo "== setup_and_verify done =="
