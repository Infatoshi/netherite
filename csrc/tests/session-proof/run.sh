#!/usr/bin/env bash
# The multi-sitting chain proved headless: csrc/play/session.sh drives
# out/native/play through its SDL input path (--input-script, the dummy video
# driver) for four sessions from fresh-play-s1, each from the checkpoint the
# previous check wrote. Session 1 chops a log and picks it up, 2 crafts planks
# in the inventory grid, 3 walks and clicks, 4 walks west (the inputs are
# flow-craft-s1's and flow-walk-s1's recorded play, split at quiet ticks). A
# first attempt at session 4 has a divergence planted in its native check
# (--fault entity@40): it must FAIL and leave session 4 next; the second
# attempt passes. Then --status.
#   bash csrc/tests/session-proof/run.sh [RUN]    (RUN defaults to proof; it must not exist)
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
run=${1:-proof}
[[ -e out/human/$run ]] && { echo "out/human/$run exists: pick another RUN"; exit 2; }
in=csrc/tests/session-proof
s() { local o; o=$(bash csrc/play/session.sh --run "$run" "$@" 2>&1 | grep -v '^play: '); echo "$o"; out=$o; }
ok=1 out=
for n in 1 2 3; do s --input-script $in/s$n.jsonl; grep -q "^PASS session $n:" <<< "$out" || ok=0; done
s --fault entity@40 --input-script $in/s4.jsonl; grep -q '^FAIL session 4 attempt 1:' <<< "$out" || ok=0
[[ $(cat out/human/$run/next) == 4 ]] || { echo "the planted failure advanced the chain"; ok=0; }
s --input-script $in/s4.jsonl; grep -q '^PASS session 4:' <<< "$out" || ok=0
bash csrc/play/session.sh --run "$run" --status
[[ $ok == 1 ]] && echo "session proof: PASS" || { echo "session proof: FAIL"; exit 1; }
