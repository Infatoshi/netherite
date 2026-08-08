#!/usr/bin/env bash
# Build the native candidate once, then run every focused Java-pixel owner.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUNNER="$ROOT/verify/ui_entities/run_oracle_gate.sh"
OUT_ROOT="${ENTITY_STRICT_OUT:-$ROOT/.tmp/entity_strict_focused}"
CANDIDATE="$OUT_ROOT/candidate"
mkdir -p "$OUT_ROOT"

ENTITY_GATE_CANDIDATE="$CANDIDATE" \
ENTITY_GATE_BUILD_ONLY=1 \
ENTITY_GATE_C_OUT="$OUT_ROOT/build" \
  bash "$RUNNER"

modes=(
  ENTITY_GATE_ENDER_CHEST_GUI_ONLY
  ENTITY_GATE_LARGE_CHEST_GUI_ONLY
  ENTITY_GATE_STATIC_CONTAINER_GUI_ONLY
  ENTITY_GATE_PROCESSING_CONTAINER_GUI_ONLY
  ENTITY_GATE_STANDARD_CONTAINER_GUI_ONLY
  ENTITY_GATE_BEACON_GUI_ONLY
  ENTITY_GATE_BEACON_WORLD_ONLY
  ENTITY_GATE_SPAWNER_WORLD_ONLY
  ENTITY_GATE_SLIME_MAGMA_ONLY
  ENTITY_GATE_VISUAL_TAIL_ONLY
  ENTITY_GATE_BAT_ONLY
  ENTITY_GATE_SQUID_ONLY
  ENTITY_GATE_MOOSHROOM_ONLY
  ENTITY_GATE_MINECART_TNT_ONLY
  ENTITY_GATE_MINECART_VARIANTS_ONLY
  ENTITY_GATE_BOAT_ONLY
  ENTITY_GATE_SHULKER_BOX_GUI_ONLY
  ENTITY_GATE_ENDER_CHEST_WORLD_ONLY
  ENTITY_GATE_CHEST_WORLD_ONLY
  ENTITY_GATE_SHULKER_BOX_WORLD_ONLY
  ENTITY_GATE_HORSE_GUI_ONLY
  ENTITY_GATE_HORSE_PARTICLES_ONLY
  ENTITY_GATE_HORSE_MODEL_ONLY
  ENTITY_GATE_LLAMA_ONLY
  ENTITY_GATE_HANGING_ONLY
  ENTITY_GATE_GLOWING_ONLY
  ENTITY_GATE_WITHER_ONLY
)

failed=()
for mode in "${modes[@]}"; do
  lane="${mode#ENTITY_GATE_}"
  lane="${lane%_ONLY}"
  lane="${lane,,}"
  lane_out="$OUT_ROOT/$lane"
  mkdir -p "$lane_out"
  echo "== focused pixel lane: $lane =="
  if env "$mode=1" \
      ENTITY_GATE_REUSE_CANDIDATE=1 \
      ENTITY_GATE_CANDIDATE="$CANDIDATE" \
      ENTITY_GATE_C_OUT="$lane_out" \
      bash "$RUNNER" >"$lane_out/gate.log" 2>&1; then
    tail -n 1 "$lane_out/gate.log"
  else
    failed+=("$lane")
    tail -n 20 "$lane_out/gate.log"
  fi
done

if [ "${#failed[@]}" -ne 0 ]; then
  echo "FAIL focused entity pixel suite: ${failed[*]}" >&2
  exit 1
fi
echo "PASS focused entity pixel suite: ${#modes[@]} lanes"
