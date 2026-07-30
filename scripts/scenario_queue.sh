#!/usr/bin/env bash
# scenario_queue.sh LIST_FILE - record scenarios serially through the oracle.
#
# LIST_FILE: one scenario yaml path per line (# comments ok). Each entry runs
# through scenarios/run_scenario.sh (which flocks /tmp/qrl_25575.lock, boots
# the oracle, records, replays --cpu, gates). One queue process owns the
# night; delegates never call this.
#
# Board: trace/report/overnight_board.jsonl, one row per attempt:
#   {"scenario": ..., "tape": ..., "rc": N, "phase": "record+gate",
#    "started": ..., "finished": ...}
# rc semantics are run_scenario.sh's: 0 pass, 3 pixel gate fail, 4 physics
# fail, 2 spec error. The queue continues past failures - failures are the
# fix fan-out's work list.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERIFY="$HERE/verify"
BOARD="$VERIFY/trace/report/overnight_board.jsonl"
LIST="${1:?usage: scenario_queue.sh LIST_FILE}"
mkdir -p "$(dirname "$BOARD")"

while IFS= read -r spec; do
    case "$spec" in ''|\#*) continue;; esac
    [ -f "$spec" ] || spec="$VERIFY/scenarios/$spec"
    if [ ! -f "$spec" ]; then
        echo "{\"scenario\": \"$spec\", \"rc\": 2, \"error\": \"spec not found\"}" >> "$BOARD"
        continue
    fi
    name=$(basename "$spec" .yaml)
    t0=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    echo "[queue] recording $name"
    log="$VERIFY/trace/report/queue_${name}.log"
    bash "$VERIFY/scenarios/run_scenario.sh" "$spec" >"$log" 2>&1
    rc=$?
    t1=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    tape=$(ls -t "$VERIFY/tapes/scenario_${name}_"*.jsonl 2>/dev/null \
           | rg -v 'geom|known_divergences' | head -1 || true)
    printf '{"scenario": "%s", "tape": "%s", "rc": %d, "started": "%s", "finished": "%s", "log": "%s"}\n' \
        "$name" "${tape:-}" "$rc" "$t0" "$t1" "$log" >> "$BOARD"
    echo "[queue] $name rc=$rc"
done < "$LIST"
echo "[queue] done: $(rg -c . "$BOARD") board rows total"
