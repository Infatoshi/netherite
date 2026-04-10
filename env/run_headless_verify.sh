#!/usr/bin/env bash
# Unattended smoke: headless Minecraft (see netherite.headless in YAML) + recorder --headless,
# then state_verify on the recording. Run from repo root: ./env/run_headless_verify.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PYTHONPATH=env

pkill -9 -f 'KnotClient|runClient' 2>/dev/null || true
sleep 2
shopt -s nullglob
for f in /tmp/netherite_*; do rm -f "$f" || true; done
shopt -u nullglob
rm -rf run/saves/netherite_0 2>/dev/null || true
rm -f recordings/smoke_headless.jsonl

if [[ $# -ge 1 ]]; then
  CONFIG="$1"
  shift
else
  CONFIG=config/smoke_headless.yaml
fi

uv run env/pygame_recorder.py --config "$CONFIG" --headless "$@"
# Reuse the save; check later ticks after chunk lighting stabilizes.
uv run env/state_verify.py --config "$CONFIG" --tick-checkpoints 100,200 --reuse-world

echo "Headless verify OK: $CONFIG"
