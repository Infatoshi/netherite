#!/usr/bin/env bash
# A played session with no person at the keyboard: make play (the play-mode
# client at wall-clock pace, vanilla's own input loops, the raw recorder)
# with DRIVE= (RawDrive.java: LWJGL-level keyboard and mouse events standing
# in for a person) on its own Xvfb display, the window focused as a
# person's would be. Writes TAPE and TAPE's NAME.raw.jsonl beside it. FROM
# (a checkpoint directory) starts the session on that checkpoint's save.
#   bash tests/rawplay.sh DRIVE TAPE [SEED [FROM]]
set -uo pipefail
cd "$(dirname "$0")/.."
if [ "${1:-}" != --in ]; then exec bash tests/headless.sh "${SLOTS:-24}" bash "$0" --in "$@"; fi
shift
drive=$(realpath "$1"); tape=$(realpath -m "$2"); seed=${3:-1}; from=${4:+$(realpath "$4")}
extra=(); [ -n "$from" ] && extra=(ARGS="--from $from")
mkdir -p "$(dirname "$tape")"
rm -f "$tape" "${tape%.jsonl}.raw.jsonl"
make -s play CHECK=0 SEED="$seed" TAPE="$tape" DRIVE="$drive" "${extra[@]}" > "${tape%.jsonl}.make.log" 2>&1 & mk=$!
w=
for _ in $(seq 1 600); do
  w=$(xdotool search --name "Minecraft" 2>/dev/null | head -1)
  [ -n "$w" ] && break
  kill -0 $mk 2>/dev/null || break
  sleep 0.1
done
[ -n "$w" ] || { echo "rawplay: no client window"; wait $mk; exit 1; }
xdotool windowfocus --sync "$w" 2>/dev/null || xdotool windowfocus "$w"
wait $mk
grep -a "ORACLE" "${tape%.jsonl}.log" | tail -3
echo "rows: $(( $(wc -l < "$tape") - 1 )), raw lines: $(wc -l < "${tape%.jsonl}.raw.jsonl")"
