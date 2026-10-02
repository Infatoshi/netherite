#!/usr/bin/env bash
# The device renderer's gate (make -C csrc gpu-render-check): frames
# drawn by the C renderer in play at the observation's shape, dumped with
# their inputs (play --obs-dump), drawn again on the device, byte for byte.
#
#   gate.sh [--size WxH] [--batch K] [--jobs J] [--sample S] [--bench] [--prec P] [NAME...]
#
# The frames: every shot of every frame judge's recording (out/java/
# clientframes/NAME, the ticks and partial ticks csrc/tests/
# frame_budget.txt lists), and with --sample S, S ticks of every recording
# under out/java/snapshots (a fixed draw per name, at partial tick 1.0).
# NAME... narrows either set (clientframes names, or snapshots/NAME). The
# observation: --size (128x128), F1 (--hide-gui) and render distance 4;
# the device draws only the terrain passes, so play dumps exactly those.
# K recordings' frames go to the device in one render (render_check), and
# each one's first frame again with the clouds, the stars, a sunrise and the
# exp fogs forced (render_check --variants: paths the pinned options never
# draw, on both sides); J
# plays dump at once. --bench: render_check --bench 1,16,64,256 over the
# dumps of the first 16 recordings instead. --keep DIR keeps the dumps there. One line per render_check, the
# FAIL lines, then "render_gate: P of N frames bit-exact ...". --store DIR
# keeps each recording's dumps (lane/cudasplit), once C's redraw of every
# dump (render_check --c-check) gives its live frame, under the play
# binary's hash and the recording's files, shots and options: a later run
# draws them on the device alone (render_check --stored), without play or
# C. The store keeps its newest within --store-mb MB (3000); --fresh dumps
# every recording again. --mesh: the
# device mesher's gate (cuda/meshing): play dumps each frame with its mesh feed
# and the host's meshes of it (--obs-mesh 2), and render_check --seq feeds
# each recording's frames in order to one slot, so every section pass the
# frames mesh is meshed on the device from the feeds alone, checked byte for
# byte against the host's, and every frame is drawn with the device's
# meshes; then "mesh_gate: ...". --mesh-tab: the device mesher in its
# table mode (render_check --mesh-tab; cuda/meshing/table.h). --prec P: play draws at that precision
# (--render-prec: exact, the default, fast or fast:STAGE,...; engine/
# raster_prec.h) and the device from its dumps, which carry it.
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
size=128x128 batch=8 jobs=12 sample=0 bench=0 keep= mesh=0 store= store_mb=3000 fresh=0 prec=exact
names=() tab=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --size) size=$2; shift 2;;
        --batch) batch=$2; shift 2;;
        --jobs) jobs=$2; shift 2;;
        --sample) sample=$2; shift 2;;
        --bench) bench=1; shift;;
        --keep) keep=$2; shift 2;;
        --mesh) mesh=1; shift;;
        --mesh-tab) tab=(--mesh-tab); shift;;
        --store) store=$2; shift 2;;
        --store-mb) store_mb=$2; shift 2;;
        --fresh) fresh=1; shift;;
        --prec) prec=$2; shift 2;;
        *) names+=("$1"); shift;;
    esac
done
out=$root/out/native
check=$out/cuda/render_check
[[ -x $check ]] || { echo "render_gate: build $check first (make -C csrc gpu-render)"; exit 2; }
if [[ -n $keep ]]; then tmp=$keep; mkdir -p "$tmp"
else tmp=$(mktemp -d "${TMPDIR:-/tmp}/rendergate.XXXXXX"); trap 'rm -rf "$tmp"' EXIT; fi
budgets=$root/csrc/tests/frame_budget.txt

# the items: clientframes/NAME or snapshots/NAME
items=()
if [[ ${#names[@]} -gt 0 ]]; then
    for n in "${names[@]}"; do [[ $n == */* ]] && items+=("$n") || items+=("clientframes/$n"); done
else
    for d in out/java/clientframes/*/; do n=$(basename "$d"); grep -qE "^$n " "$budgets" && items+=("clientframes/$n"); done
    if [[ $sample -gt 0 ]]; then
        for d in out/java/snapshots/*/; do [[ -f $d/tape.jsonl ]] && items+=("snapshots/$(basename "$d")"); done
    fi
fi
[[ $bench == 1 ]] && items=("${items[@]:0:16}")

# one item's dumps into $tmp/ITEM (a line: ITEM rc frames)
dump_one() {
    local it=$1 dir=$root/out/java/$1 key=${1//\//_}
    local start=$dir tape=$dir/tape.jsonl shots=$tmp/$key.shots
    mkdir -p "$tmp/$key"
    if [[ $it == clientframes/* ]]; then
        [[ -f $dir/start ]] && start=$root/out/java/$(cat "$dir/start")
        [[ -f $tape ]] || tape=$start/tape.jsonl
        # (not the raw screen events: window pixels of 854x480; the tape's ops)
        grep -E "^$(basename "$it") " "$budgets" | awk '{print $2, $3}' > "$shots"
    else
        # S ticks of the tape's rows, drawn from the name
        local first last
        first=$(sed -n 2p "$tape" | grep -o '"t":[0-9]*' | head -1 | cut -d: -f2)
        last=$(tail -1 "$tape" | grep -o '"t":[0-9]*' | head -1 | cut -d: -f2)
        # rows before the snapshot's own tick are not replayed from it
        local t0
        t0=$(grep -o '"tick": *[0-9]*' "$dir/manifest.json" 2>/dev/null | head -1 | grep -o '[0-9]*$')
        [[ -n $t0 && -n $first && $t0 -gt $first ]] && first=$t0
        [[ -n $first && -n $last && $last -ge $first ]] || { echo "$it 0 0 (no rows after its start)"; return 0; }
        RANDOM=$(( $(printf '%s' "$it" | cksum | cut -d' ' -f1) % 32768 ))
        for ((k = 0; k < SAMPLE; ++k)); do echo "$(( first + (RANDOM * 32768 + RANDOM) % (last - first + 1) )) 1.0"; done | sort -un > "$shots"
    fi
    local bin=$out/play
    head -1 "$start/tape.jsonl" | grep -q '"dev":true' && bin=$out/play-dev
    # the store: these dumps, made before by this play (its hash) from the
    # same recording files, shots and options, and checked by C then
    local sd=
    if [[ -n $STORE ]]; then
        local rk
        rk=$( { (cd "$dir" && find -L . -type f -printf '%P %s %T@\n'); [[ $start != "$dir" ]] && (cd "$start" && find -L . -type f -printf 'start %P %s %T@\n');
                cat "$shots"; echo "$SIZE ${OBSMESH:-} $PREC"; } 2>/dev/null | LC_ALL=C sort | sha1sum | cut -c1-16)
        sd=$STORE/$key/$(sha1sum "$bin" | cut -c1-16)-$rk
        if [[ $FRESH == 0 && -f $sd/ok ]]; then
            rmdir "$tmp/$key" && ln -s "$sd" "$tmp/$key" && touch "$sd"
            echo "$it 0 $(ls "$sd" | grep -c '\.obs$') stored"
            return 0
        fi
    fi
    SDL_VIDEODRIVER=dummy "$bin" "$start" --threads 2 --replay "$tape" --tape "$tmp/$key.tape" \
        --shots "$shots" --obs-dump "$tmp/$key" --size "$SIZE" --hide-gui --render-distance 4 $OBSMESH --render-prec "$PREC" \
        > "$tmp/$key.log" 2>&1
    local rc=$?
    rm -f "$tmp/$key.tape"
    if [[ -n $sd && ( $rc == 0 || $rc == 3 ) ]] && ls "$tmp/$key"/o_*.obs > /dev/null 2>&1 &&
       "$check" --c-check "$tmp/$key"/o_*.obs > "$tmp/$key.ccheck" 2>&1; then
        mkdir -p "$STORE/$key" && rm -rf "$sd" && touch "$tmp/$key/ok" && mv "$tmp/$key" "$sd" && ln -s "$sd" "$tmp/$key"
    fi
    echo "$it $rc $(ls "$tmp/$key/" | grep -c '\.obs$')"
}
export -f dump_one
OBSMESH=
[[ $mesh == 1 ]] && OBSMESH="--obs-mesh 2"
export root tmp out check budgets SIZE=$size SAMPLE=$sample OBSMESH STORE=$store FRESH=$fresh PREC=$prec

# the store keeps its newest dumps (by last use) within store_mb
store_trim() {
    [[ -n $store && -d $store ]] || return 0
    local t=0 m p
    find "$store" -mindepth 2 -maxdepth 2 -type d -printf '%T@ %p\n' | sort -rn | while read -r _ p; do
        m=$(du -sm "$p" | cut -f1); t=$((t + m))
        [[ $t -gt $store_mb ]] && rm -rf "$p"
    done
}

printf '%s\n' "${items[@]}" | xargs -P "$jobs" -I{} bash -c 'dump_one {}' > "$tmp/dumps.txt"
awk '$2 != 0 && $2 != 3 {print "FAIL render_gate " $1 ": play rc " $2}' "$tmp/dumps.txt"
awk '$2 == 3 || ($2 == 0 && $3 == 0) {print "note render_gate " $1 ": play rc " $2 ", " $3 " frames"}' "$tmp/dumps.txt" | head -20
nrec=$(awk '$3 > 0' "$tmp/dumps.txt" | wc -l)

if [[ $bench == 1 ]]; then
    "$check" --bench 1,16,64,256 --reps 5 --threads 16 "$tmp"/*/o_*.obs
    exit $?
fi

rc=0 pass=0 total=0 vpass=0 vtotal=0
mapfile -t done_items < <(awk '$3 > 0 {print $1}' "$tmp/dumps.txt")
if [[ $mesh == 1 ]]; then
    meq=0 mtot=0 mreq=0
    for ((i = 0; i < ${#done_items[@]}; i += batch)); do
        files=()
        for it in "${done_items[@]:i:batch}"; do files+=("$tmp/${it//\//_}"/o_*.obs); done
        "$check" --seq "${tab[@]}" "${files[@]}" > "$tmp/check.txt" 2>&1 || rc=1
        grep -E '^FAIL|device mesh differs' "$tmp/check.txt" | head -20
        last=$(tail -1 "$tmp/check.txt")
        echo "$last"
        if [[ $last =~ ^render_check:\ ([0-9]+)\ of\ ([0-9]+)\ device\ meshes.*\(([0-9]+)\ section.*,\ ([0-9]+)\ of\ ([0-9]+)\ frames ]]; then
            meq=$((meq + BASH_REMATCH[1])); mtot=$((mtot + BASH_REMATCH[2])); mreq=$((mreq + BASH_REMATCH[3]))
            pass=$((pass + BASH_REMATCH[4])); total=$((total + BASH_REMATCH[5]))
        else rc=1; echo "FAIL render_gate: render_check --seq died: $(echo "$last" | cut -c1-160)"; fi
        [[ -n $keep ]] || for it in "${done_items[@]:i:batch}"; do rm -rf "$tmp/${it//\//_}"; done
    done
    grep -q '^FAIL' <(awk '$2 != 0 && $2 != 3' "$tmp/dumps.txt" | sed 's/^/FAIL /') && rc=1
    store_trim
    echo "mesh_gate: $meq of $mtot device meshes equal to the host's ($mreq section passes meshed on the device) over $nrec recordings; $pass of $total frames bit-exact with the device's meshes at $size"
    exit $rc
fi
for ((i = 0; i < ${#done_items[@]}; i += batch)); do
    files=()
    stored=--stored
    for it in "${done_items[@]:i:batch}"; do files+=("$tmp/${it//\//_}"/o_*.obs); [[ -L $tmp/${it//\//_} ]] || stored=; done
    "$check" $stored --batch 1000000 "${files[@]}" > "$tmp/check.txt" 2>&1
    r=$?
    if ! tail -1 "$tmp/check.txt" | grep -q '^render_check: [0-9]* of [0-9]* frames bit-exact'; then
        # the render died (the device is shared: out of memory): one
        # recording's frames per render instead
        echo "note render_gate: $(tail -1 "$tmp/check.txt"); again one recording a render"
        : > "$tmp/check.txt"; r=0; p1=0; t1=0
        for it in "${done_items[@]:i:batch}"; do
            "$check" $stored "$tmp/${it//\//_}"/o_*.obs > "$tmp/one.txt" 2>&1 || r=1
            grep -E '^FAIL' "$tmp/one.txt" >> "$tmp/check.txt"
            tail -1 "$tmp/one.txt" | grep -q '^render_check: [0-9]* of' ||
                echo "FAIL render_gate $it: render_check died: $(tail -1 "$tmp/one.txt" | cut -c1-160)" >> "$tmp/check.txt"
            p1=$((p1 + $(tail -1 "$tmp/one.txt" | sed -nE 's/^render_check: ([0-9]+) of ([0-9]+) .*/\1/p' | grep . || echo 0)))
            t1=$((t1 + $(tail -1 "$tmp/one.txt" | sed -nE 's/^render_check: ([0-9]+) of ([0-9]+) .*/\2/p' | grep . || ls "$tmp/${it//\//_}" | grep -c obs)))
        done
        echo "render_check: $p1 of $t1 frames bit-exact at $size, one recording a render" >> "$tmp/check.txt"
    fi
    grep -E '^FAIL' "$tmp/check.txt"
    grep -E '^render_check: [0-9]+ frames:' "$tmp/check.txt"
    tail -1 "$tmp/check.txt"
    # the sky's and the fog's other paths over each recording's first frame
    firsts=()
    for it in "${done_items[@]:i:batch}"; do f=("$tmp/${it//\//_}"/o_*.obs); firsts+=("${f[0]}"); done
    "$check" --variants "${firsts[@]}" > "$tmp/var.txt" 2>&1 || { r=1; grep -E '^FAIL' "$tmp/var.txt"; }
    tail -1 "$tmp/var.txt"
    vp=$(tail -1 "$tmp/var.txt" | sed -nE 's/^render_check: ([0-9]+) of ([0-9]+) .*/\1/p')
    vt=$(tail -1 "$tmp/var.txt" | sed -nE 's/^render_check: ([0-9]+) of ([0-9]+) .*/\2/p')
    vpass=$((vpass + ${vp:-0})); vtotal=$((vtotal + ${vt:-1}))
    p=$(tail -1 "$tmp/check.txt" | sed -nE 's/^render_check: ([0-9]+) of ([0-9]+) .*/\1/p')
    t=$(tail -1 "$tmp/check.txt" | sed -nE 's/^render_check: ([0-9]+) of ([0-9]+) .*/\2/p')
    pass=$((pass + ${p:-0})); total=$((total + ${t:-${#files[@]}}))
    [[ $r == 0 ]] || rc=1
    [[ -n $keep ]] || for it in "${done_items[@]:i:batch}"; do rm -rf "$tmp/${it//\//_}"; done
done
grep -q '^FAIL' <(awk '$2 != 0 && $2 != 3' "$tmp/dumps.txt" | sed 's/^/FAIL /') && rc=1
store_trim
echo "render_gate: $pass of $total frames bit-exact at $size ($prec) over $nrec recordings ($(grep -c ' stored$' "$tmp/dumps.txt") from the store), $batch recordings a device render; $vpass of $vtotal variant frames"
exit $rc
