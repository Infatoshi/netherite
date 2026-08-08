#!/usr/bin/env bash
# Prove that hidden redstone-torch history survives a Java -> magma checkpoint.
#
# The Java run completes seven off/on cycles, then captures observation 27.
# At that boundary the blocks and block light are byte-identical to the
# original fixture, but the world contains seven invisible toggle records.
# Magma loads that checkpoint, receives only the eighth cycle's two edits, and
# must match Java's already-continued suffix exactly.
set -euo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
cd "$MAGMA"

OUT="${OUT:-trace/out/redstone_torch_checkpoint_regression}"
QRL_HOST="${QRL_HOST:-127.0.0.1}"
QRL_PORT="${QRL_PORT:-25600}"
FULL_SEQUENCE="trace/fixtures/redstone_torch_floor_burnout.sequence"
SUFFIX_SEQUENCE="trace/fixtures/redstone_torch_floor_burnout_suffix.sequence"
FIXTURE="trace/fixtures/redstone_torch_floor_on.blocks"
CHECKPOINT_TICK=27
SUFFIX_TICKS=8
SUFFIX="$OUT/checkpoint_suffix"

echo "[1/7] capture Java checkpoint after seven complete torch cycles"
env \
    TICKS=36 \
    SEED=0 \
    TAPE_PROFILE=idle \
    FIXTURE_BLOCKS_FILE="$FIXTURE" \
    FIXTURE_STAGE=final \
    SCHEDULE_CAPTURE_BLOCKS="75 76" \
    BLOCK_EDIT_SEQUENCE="$FULL_SEQUENCE" \
    CHECKPOINT_TICK="$CHECKPOINT_TICK" \
    BLOCK_STRICT=1 \
    REQUIRE_BLOCK_MUTATION=1 \
    QRL_HOST="$QRL_HOST" \
    QRL_PORT="$QRL_PORT" \
    OUT="$OUT" \
    bash trace/run_oracle.sh

read -r -a BLOCK_BOX_ARGS <"$OUT/block_box.txt"
if [ "${#BLOCK_BOX_ARGS[@]}" -ne 6 ]; then
    echo "checkpoint run did not write a valid six-integer block box" >&2
    exit 1
fi

echo "[2/7] prove the checkpoint has unchanged visible blocks and light"
cmp "$OUT/java_blocks_before.bin" "$OUT/java_checkpoint_blocks.bin"
cmp "$OUT/java_block_light_before.bin" \
    "$OUT/java_checkpoint_block_light.bin"

echo "[3/7] package and validate the hidden-history checkpoint"
uv run --no-project python trace/state_capsule.py create \
    --state "$OUT/java_checkpoint_state.json" \
    --blocks "$OUT/java_checkpoint_blocks.bin" \
    --box "${BLOCK_BOX_ARGS[@]}" \
    --seed 0 \
    --source-engine minecraft-java \
    --source-version 1.11.2 \
    --out "$OUT/checkpoint_capsule"
uv run --no-project python trace/state_capsule.py validate \
    --capsule "$OUT/checkpoint_capsule"

mkdir -p "$SUFFIX"
echo "[4/7] extract Java's eight-tick continuation and generate its input tape"
uv run --no-project python trace/slice_state_trace.py \
    --input "$OUT/java_state.jsonl" \
    --output "$SUFFIX/java_state.jsonl" \
    --start $((CHECKPOINT_TICK + 1)) \
    --count "$SUFFIX_TICKS"
uv run --no-project python trace/gen_tape.py \
    --ticks "$SUFFIX_TICKS" \
    --seed 0 \
    --profile idle \
    --out "$SUFFIX/tape.txt"

echo "[5/7] load the checkpoint in magma and apply only the eighth cycle"
uv run --no-project python trace/trace_runtime.py \
    --tape "$SUFFIX/tape.txt" \
    --spawn-file "$OUT/c_spawn.txt" \
    --seed 0 \
    --capsule "$OUT/checkpoint_capsule" \
    --block-edit-sequence "$SUFFIX_SEQUENCE" \
    --script-out "$SUFFIX/c_runtime_script.jsonl" \
    --raw-state "$SUFFIX/c_state_raw.jsonl" \
    --state "$SUFFIX/c_state.jsonl" \
    --blocks-out "$SUFFIX/c_runtime_blocks.bin" \
    --block-light-out "$SUFFIX/c_runtime_block_light.bin" \
    --blocks-box "${BLOCK_BOX_ARGS[@]}" \
    --skip-build

echo "[6/7] compare every canonical state feature in the continuation"
uv run --no-project python trace/diff_trace.py \
    --java "$SUFFIX/java_state.jsonl" \
    --c "$SUFFIX/c_state.jsonl"

echo "[7/7] compare final raw blocks and all sampled block-light cells"
uv run --no-project python trace/block_diff.py \
    --java "$OUT/java_blocks.bin" \
    --c "$SUFFIX/c_runtime_blocks.bin" \
    --java-before "$OUT/java_checkpoint_blocks.bin" \
    --c-before "$OUT/java_checkpoint_blocks.bin" \
    --box "${BLOCK_BOX_ARGS[@]}" \
    --out "$SUFFIX/block_mismatches.csv" \
    --transition-strict \
    --require-mutation
uv run --no-project python trace/light_diff.py \
    --java "$OUT/java_block_light.bin" \
    --c "$SUFFIX/c_runtime_block_light.bin" \
    --box "${BLOCK_BOX_ARGS[@]}" \
    --out "$SUFFIX/block_light_mismatches.csv"

echo "PASS: seven hidden toggles restored; eighth-toggle burnout is exact"
