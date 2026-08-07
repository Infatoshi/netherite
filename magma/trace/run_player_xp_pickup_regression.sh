#!/usr/bin/env bash
# Prove that a restored nonzero XP state remains causal when a saved orb is
# collected, including Java's fractional-bar update and hidden total.
set -euo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(cd "$MAGMA/.." && pwd)"
OUT="${OUT:-$MAGMA/trace/out/player_xp_pickup_regression}"
QRL_HOST="${QRL_HOST:-127.0.0.1}"
QRL_PORT="${QRL_PORT:-25575}"
export UV_CACHE_DIR="${UV_CACHE_DIR:-/home/jawaugh/.cache/uv}"
export TMPDIR="${TMPDIR:-$REPO/.tmp}"
mkdir -p "$TMPDIR"

if ! timeout 2 bash -c 'exec 3<>"/dev/tcp/$1/$2"' \
        _ "$QRL_HOST" "$QRL_PORT" 2>/dev/null; then
    echo "player XP regression requires the real Java qrl oracle at $QRL_HOST:$QRL_PORT" >&2
    exit 2
fi

export OUT QRL_HOST QRL_PORT
export TICKS=20 TAPE_PROFILE=idle FRESH=1
export PLAYER_XP_FIXTURE='7 0.33333334 98'
export PLAYER_COMBAT_FIXTURE='2 0 0 0'
export XP_ORB_FIXTURE='0 0 0 5'
export INITIAL_FIRE=-20 INITIAL_FOOD=17 CLEAR_HURT=1
export BLOCK_STRICT=1 REQUIRE_BLOCK_MUTATION=0

bash "$MAGMA/trace/run_oracle.sh"

jq -e '
    .schema == "netherite.state_capsule" and .version == 2 and
    .state.player.xp_level == 7 and
    .state.player.xp_frac == 0.33333334 and
    .state.player.xp_total == 98 and
    ([.state.entities[] | select(
        .type == "EntityXPOrb" and .value == 5)] | length) == 1
' "$OUT/state_capsule/manifest.json" >/dev/null

for state in "$OUT/java_state.jsonl" "$OUT/c_state.jsonl"; do
    jq -s -e '
        .[0].player.xp_level == 7 and
        (.[0].player.xp_frac - 0.5714286 | fabs) < 0.000001 and
        .[0].player.xp_total == 103 and
        .[-1].player.xp_total == 103
    ' "$state" >/dev/null
done
cmp "$OUT/java_blocks.bin" "$OUT/c_runtime_blocks.bin"

echo "player XP pickup regression PASS: restored total 98 + orb 5 -> level 7, fraction 0.5714286, total 103 for 20 exact ticks"
