#!/usr/bin/env bash
# The warm pool's GL-state proof (PoolGl). RenderSlime enables GL_NORMALIZE
# and nothing disables it, so a member that drew a slime would light the next
# job's scaled draws (the sign board of tests/poolgl-sign.jsonl) otherwise
# than a fresh JVM. This records poolgl-sign in a fresh JVM, then on one
# member of this tree's pool poolgl-slime (the same scene with a slime in
# view) and poolgl-sign after it, and compares the two sign recordings: every
# golden frame byte for byte and the tape's rows. Run from oracle/.
#
#   bash tests/pool-glproof.sh ROOT        PASS when the pooled sign equals the fresh one
#   bash tests/pool-glproof.sh ROOT gl     the negative control: the member keeps the
#                                          last job's GL state (--pool-skip-reset gl), so
#                                          the frames must differ
#
# It stops and starts this tree's own members (make pool-stop, pool-start).
set -uo pipefail
cd "$(dirname "$0")/.."
root=$(realpath -m "$1") ctl=${2:-}
rm -rf "$root"; mkdir -p "$root"
run() {
    local mode=$1 n=$2 m
    case $mode in fresh) m=(POOL=0 CACHE=0) ;; pool) m=(POOL=1 CACHE=0 POOLAUTO=0) ;; esac
    make -s script "${m[@]}" DEV=1 SEED=1 SCRIPT=tests/poolgl-$n.jsonl TAPE="$root/$mode-$n/tape.jsonl" FRAMES="$root/$mode-$n/frames" > "$root/$mode-$n.log" 2>&1
    echo "$mode $n rc=$? $(grep -a '^pool: member' "$root/$mode-$n.log" | tail -1)"
}
run fresh sign &
make -s pool-stop > /dev/null 2>&1
# pool-start gives up after 60 s, but a member may wait longer than that for a display slot (headless.sh)
make -s pool-start POOLN=1 ${ctl:+POOLARGS="--pool-skip-reset $ctl"} > /dev/null 2>&1
for i in $(seq 1 1200); do [ -f ../out/java/pool/1/port ] && break; grep -q "ORACLE POOL END" ../out/java/pool/1/log 2>/dev/null && break; sleep 0.5; done
[ -f ../out/java/pool/1/port ] && echo "member 1 ready" || { echo "FAIL member 1 did not start"; exit 1; }
run pool slime
run pool sign
wait
grep -a "ORACLE POOL gl\|ORACLE POOL negative\|ORACLE POOL END" ../out/java/pool/1/log | sed 's/^/member: /'
make -s pool-stop > /dev/null 2>&1
rc=0
for s in slime sign; do grep -q "member 1 takes the run" "$root/pool-$s.log" || { echo "pool $s: NOT RUN BY THE POOL"; rc=1; }; done
n=0 d=0
for f in "$root"/fresh-sign/frames/*.png; do
    n=$((n + 1))
    cmp -s "$f" "$root/pool-sign/frames/$(basename "$f")" || { d=$((d + 1)); echo "DIFFER $(basename "$f")"; }
done
# rows without their frame paths (each run writes its own)
cmp -s <(tail -n +2 "$root/fresh-sign/tape.jsonl" | jq -c "del(.frame)") <(tail -n +2 "$root/pool-sign/tape.jsonl" | jq -c "del(.frame)") && r=rows-same || r=ROWS-DIFFER
echo "frames: $((n - d)) of $n byte-identical; $r"
if [ -n "$ctl" ]; then
    [ $d -gt 0 ] && echo "PASS negative control: the kept GL state changes the frames" || { echo "FAIL negative control: no frame differs"; rc=1; }
else
    [ $d = 0 ] && [ $n -gt 0 ] && [ $r = rows-same ] && [ $rc = 0 ] && echo "PASS pooled after a slime = fresh" || { echo "FAIL"; rc=1; }
fi
exit $rc
