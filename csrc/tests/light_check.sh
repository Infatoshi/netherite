#!/usr/bin/env bash
# light_check.sh [-j N] [-d] [--fresh] [--store DIR] [--live] NAME...: each
# snapshot recording NAME's light engine calls replayed on the device and
# compared with C's (GPU plan L16, engine/lightcap.h, cuda/lighting/lighting.h); -d adds
# --light-drivers (func_150809_p's column checks and the relight checks
# replayed whole). One line per recording (OK or the first FAIL, the calls,
# the bytes compared, the queue's largest length), then the totals. Exit 1
# when any recording fails. `make -C csrc gpu-lighting-check [DRIVERS=1]` runs it.
#
# C's side is stored once per recording and C build (lane/cudasplit): the
# capture (out/native/test_snapshots --light-cap save:FILE, every call with
# C's result, zstd) goes to DIR/NAME/KEY.lc.zst, KEY the capture binary's
# hash and the recording's files (names, sizes, times), so a C change makes
# them again and a device change does not. A recording without a capture is
# captured N at a time under tests/memslot.sh, and every capture, stored or
# new, goes as it is ready to one out/native/cuda/lighting_check, which replays N
# at a time on the device alone (one context, a stream each). --fresh makes
# every capture again (the nightly); DIR defaults to
# ~/.cache/netherite-devcap/light, and after each run the store keeps its
# newest captures within MB (--store-mb, default 12000: the full gate's
# captures took 8.6 GB). --live runs the old way instead: each replay with
# out/native/cuda/lighting/test_snapshots --light-cap cuda, the device in a forked
# child beside C (the end-to-end path).
set -u
J=8 DRV= SFX= FRESH=0 LIVE=0 STORE=$HOME/.cache/netherite-devcap/light STORE_MB=12000
while [ $# -gt 0 ]; do
    case $1 in
        -j) J=$2; shift 2;;
        -d) DRV=--light-drivers; SFX=-drivers; shift;;
        --fresh) FRESH=1; shift;;
        --live) LIVE=1; shift;;
        --store) STORE=$2; shift 2;;
        --store-mb) STORE_MB=$2; shift 2;;
        *) break;;
    esac
done
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/out/native
SNAP=$ROOT/out/java/snapshots
LOGS=$OUT/gpu-lighting-check$SFX
mkdir -p "${TMPDIR:-$HOME/dev/nw/.tmp}" "$LOGS"
tmp=$(mktemp -d "${TMPDIR:-$HOME/dev/nw/.tmp}/lcheck.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
export ROOT SNAP OUT tmp DRV SFX LOGS STORE FRESH

# one recording's result line to $tmp/NAME.res, from its device line (in
# $tmp/NAME.log) and its capture line (in $2)
verdict() {
    local n=$1 cap=$2 rc=$3 dev calls bytes wrote q cw r
    dev=$(grep -a "^light device: " "$tmp/$n.log" | tail -1)
    cap=$(grep -a "^light capture: " "$cap" | tail -1)
    calls=$(echo "$dev" | sed -n "s/^light device: \([0-9]*\) calls replayed, every one byte-equal.*/\1/p")
    bytes=$(echo "$dev" | sed -n "s/.*; \([0-9]*\) bytes of written chunks compared.*/\1/p")
    wrote=$(echo "$dev" | sed -n "s/.*; \([0-9]*\) records wrote light, .*/\1/p")
    q=$(echo "$cap" | sed -n "s/.*queue max \([0-9]*\), \([0-9]*\) calls hit.*/\1 \2/p")
    cw=$(echo "$cap" | sed -n "s/.*; \([0-9]*\) on the client world).*/\1/p")
    if [ "$rc" = 0 ] && [ -n "$calls" ] && [ -n "$q" ]; then r="OK $calls calls, ${cw:-0} client world, $wrote wrote, $bytes bytes, queue max/cap hits $q"
    else r="FAIL $( (grep -a "^light device: record\|^light device: the\|^light device: not\|^light device: no" "$tmp/$n.log"; grep -a "^FAIL" "$tmp/$n.log") | head -1 | cut -c1-240)"; [ "$r" = "FAIL " ] && r="FAIL rc=$rc"; fi
    echo "$r" > "$tmp/$n.res"
}
export -f verdict

if [ "$LIVE" = 1 ]; then
    BIN=$OUT/cuda/lighting/test_snapshots
    export BIN
    printf '%s\n' "$@" | xargs -P "$J" -I{} bash -c '
        n=$1; d=$SNAP/$n
        # the 3090 is shared: a run the device had no memory for (another
        # process holds it) is run again, up to five times, a little later each
        for a in 1 2 3 4 5; do
            bash "$ROOT/csrc/tests/memslot.sh" --key test_snapshots "$d" "$BIN" --light-cap cuda $DRV "$d" > "$tmp/$n.log" 2>&1; rc=$?
            grep -aq "^lighting: .*out of memory" "$tmp/$n.log" || break
            [ $a = 5 ] || { cp "$tmp/$n.log" "$LOGS/$n.oom$a.log"; sleep $((30 * a)); }
        done
        cp "$tmp/$n.log" "$LOGS/$n.log"
        grep -aq ": OK" "$tmp/$n.log" || rc=1
        verdict "$n" "$tmp/$n.log" "$rc"' _ {}
else
    # the capture binary's hash: every stored capture made by other C is
    # stale (sha1 of the program: its code and its tables)
    KB=$(sha1sum "$OUT/test_snapshots" | cut -c1-16)
    export KB
    # captures: stored ones at once, the rest as they are made; each path to
    # the replay as it is ready
    printf '%s\n' "$@" | xargs -P "$J" -I{} bash -c '
        n=$1; d=$SNAP/$n
        rk=$(cd "$d" 2>/dev/null && find -L . -type f -printf "%P %s %T@\n" | LC_ALL=C sort | sha1sum | cut -c1-16)
        f=$STORE/$n/$KB-$rk$SFX.lc.zst
        mkdir -p "$STORE/$n"
        if [ "$FRESH" = 1 ] || [ ! -f "$f" ] || [ ! -f "${f%.lc.zst}.log" ]; then
            t=$f.tmp.$$
            bash "$ROOT/csrc/tests/memslot.sh" --key test_snapshots "$d" "$OUT/test_snapshots" --light-cap save:$t.zst $DRV "$d" > "$t.log" 2>&1; rc=$?
            if [ $rc = 0 ] && grep -aq ": OK" "$t.log" && grep -aq "^light capture stored: " "$t.log"; then
                mv "$t.zst" "$f" && mv "$t.log" "${f%.lc.zst}.log"
            else
                cp "$t.log" "$tmp/$n.log"; rm -f "$t.zst" "$t.log"; echo "capture rc $rc" > "$tmp/$n.caprc"
                exit 0
            fi
        else touch "$f"
        fi
        echo "$f" > "$tmp/$n.path"
        echo "$f"' _ {} | "$OUT/cuda/lighting_check" -j "$J" - > "$tmp/replay.txt" 2> "$tmp/replay.err"
    # a capture the replay gave no line (the shared 3090 out of memory: 254 of the batch run's
    # driver captures on 2026-09-29, each byte-equal when replayed alone) runs again alone, twice
    for a in 1 2; do
        miss=$(for n in "$@"; do [ -f "$tmp/$n.path" ] || continue; f=$(cat "$tmp/$n.path"); grep -aqF "$f	" "$tmp/replay.txt" || echo "$f"; done)
        [ -n "$miss" ] || break
        echo "gpu-lighting-check: $(echo "$miss" | wc -l) captures without a result, replayed again alone (try $a)" >&2
        sleep $((20 * a))
        echo "$miss" | "$OUT/cuda/lighting_check" -j 1 - >> "$tmp/replay.txt" 2>> "$tmp/replay.err"
    done
    for n in "$@"; do
        if [ -f "$tmp/$n.caprc" ]; then
            cp "$tmp/$n.log" "$LOGS/$n.log"; verdict "$n" "$tmp/$n.log" 1; continue
        fi
        f=$(cat "$tmp/$n.path" 2>/dev/null)
        { cat "${f%.lc.zst}.log" 2>/dev/null; grep -aF "$f	" "$tmp/replay.txt" | cut -f2-; } > "$tmp/$n.log"
        cp "$tmp/$n.log" "$LOGS/$n.log"
        grep -aq "^light device: [0-9]* calls replayed, every one byte-equal" "$tmp/$n.log"; verdict "$n" "$tmp/$n.log" $?
    done
    [ -s "$tmp/replay.err" ] && sed 's/^/gpu-lighting-check: lighting_check: /' "$tmp/replay.err" | head -5
    # the store keeps its newest captures (by last use) within STORE_MB
    find "$STORE" -name '*.lc.zst' -printf '%T@ %s %p\n' 2>/dev/null | sort -rn | awk -v cap=$((STORE_MB * 1048576)) '{ s += $2; if (s > cap) print $3 }' |
        while read -r f; do rm -f "$f" "${f%.lc.zst}.log"; done
fi

ok=0 all=0 C=0 W=0 B=0 Q=0 O=0 R=0
for n in "$@"; do
    r=$(cat "$tmp/$n.res" 2>/dev/null || echo "FAIL no result")
    printf '%-34s %s\n' "$n" "$r"
    all=$((all + 1))
    case $r in OK*)
        ok=$((ok + 1))
        set -- $r
        C=$((C + $2)); R=$((R + $4)); W=$((W + $7)); B=$((B + $9))
        [ "${14}" -gt "$Q" ] && Q=${14}
        O=$((O + ${15}));;
    esac
done
echo "gpu-lighting-check${DRV:+ (drivers)}: $ok/$all recordings OK; $C light calls replayed on the device ($R on the client world), every one byte-equal to C; $W records wrote light; $B bytes of written chunks compared; queue max $Q of 32,768, $O calls hit the cap; logs ${LOGS#$ROOT/}/NAME.log"
[ "$ok" = "$all" ]
