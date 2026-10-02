#!/usr/bin/env bash
# The seed-1 dragon run on all three engines: GoldBot's chain (oracle/tests/
# gold-chain.sh --seed 1: s1chain-S1..S23, the fresh seed-1 world to the
# dragon's death and the respawn) played and checked on the Java oracle, the
# C engine and the GPU, with one report:
#
#   csrc/tests/dragonrun.sh [--stages java,c,gpu] [--j N]
#
# java  the chain recorded from the fresh world in one run, each segment from
#       the one before's own end (gold-chain.sh --seed 1 --root, CACHE=0
#       POOL=0), every tape compared with the tree's (SAME); then each
#       segment's fresh tape replayed with the oracle's frames at the shots
#       below (854x480, the tape's render distance, partial ticks 0.5 and
#       1.0, --toasts --chat as the live client draws them, the drops from
#       the render state). The first run installs them as the frame judges'
#       recordings out/java/clientframes/dragon-s1chain-SN; later runs
#       compare the fresh frames with those byte for byte.
# c     every segment replayed by test_snapshots (out/native/recs, all at
#       once), csrc/play's own client through the chain (csrc/play/
#       chain.sh --seed 1: play takes the tape's acts, Java replays play's
#       tape, test_snapshots checks every row), and play's frames at the
#       shots drawn by the C renderer against Java's (tests/frame_judge.sh
#       --frames c: tests/frame_budget.txt), each ranked by pxdiff's
#       clusters (out/native/pxdiff frames).
# gpu   the chain through the product pipeline with the device meshing and
#       drawing (csrc/runtime pipe_gate --device-mesh 2 --draw: every row
#       against test_snapshots', every 128x128 observation against the C
#       renderer's, byte for byte), then the judges' device frames at the
#       shots: play --frames device --frames-check 1 at the tape's render
#       distance (the strip's frames, each against C byte for byte) and
#       cuda/render/judge_gate.sh at 854x480, render distance 8 (the gate's
#       own path). One device check at a time (the 3090 is shared).
#
# Writes out/perf/dragonrun: the stage logs, dragonrun.txt (the table:
# engine, wall time, ticks/s, rows equal, frames compared and the result),
# and strip-*.png (Java, C and the GPU side by side at the milestones).
# Scratch in out/dragonrun. Exit 1 when any check fails.
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
stages=java,c,gpu
J=4
while [[ $# -gt 0 ]]; do
    case $1 in
        --stages) stages=$2; shift 2;;
        --j) J=$2; shift 2;;
        *) echo "usage: dragonrun.sh [--stages java,c,gpu] [--j N]"; exit 2;;
    esac
done
has() { [[ ,$stages, == *,$1,* ]]; }
NSEG=23
segs=($(seq 1 $NSEG))
names=(); for n in "${segs[@]}"; do names+=("s1chain-S$n"); done
work=$root/out/dragonrun
res=$root/out/perf/dragonrun
mkdir -p "$work" "$res"
export root work
export TMPDIR=${TMPDIR:-$HOME/dev/nw/.tmp}
mkdir -p "$TMPDIR"
cf=out/java/clientframes

# The shots: "SEGMENT TICK LABEL" (LABEL - for a plain shot, else a
# milestone's name for the strips). Two a segment (its middle, near its
# end) and the milestones.
shots() {
    cat <<'EOF'
1 100 start
7 3875 nether-arrival
8 5090 nether-fortress
22 2330 stronghold
23 200 end-arrival
23 4000 end-crystals
23 16600 dragon-death
23 16785 respawn
EOF
    for n in "${segs[@]}"; do
        rows=$(( $(wc -l < out/java/snapshots/s1chain-S$n/tape.jsonl) - 1 ))
        echo "$n $(( rows / 200 * 100 )) -"
        echo "$n $(( (rows - 40) / 10 * 10 )) -"
    done
}
shots | sort -k1,1n -k2,2n -u > "$work/shots.txt"
ticks_of() { awk -v s="$1" '$1 == s {print $2}' "$work/shots.txt" | sort -un | paste -sd,; }

secs() { awk -v a="$1" -v b="$(date +%s.%N)" 'BEGIN { printf "%.1f", b - a }'; }
rows_total=0
for n in "${segs[@]}"; do rows_total=$(( rows_total + $(wc -l < out/java/snapshots/s1chain-S$n/tape.jsonl) - 1 )); done
table=$res/dragonrun.tsv
: > "$table.new"
row() { printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$@" >> "$table.new"; }
fail=0

make -s -C csrc all play play-dev > "$res/build.log" 2>&1 || { echo "dragonrun: native build failed ($res/build.log)"; exit 1; }
make -s -C oracle build >> "$res/build.log" 2>&1 || { echo "dragonrun: java build failed ($res/build.log)"; exit 1; }

# ---------------------------------------------------------------- Java
if has java; then
    jroot=$work/javaroot
    rm -rf "$jroot"
    t0=$(date +%s.%N)
    (cd oracle && bash tests/gold-chain.sh --seed 1 --root "$jroot" $NSEG 1) > "$res/java-chain.log" 2>&1
    jrc=$?
    jwall=$(secs "$t0")
    same=$(grep -c ' SAME$' "$res/java-chain.log")
    jrows=0
    for n in "${segs[@]}"; do
        f=$jroot/out/java/snapshots/s1chain-S$n/tape.jsonl
        [[ -s $f ]] && jrows=$(( jrows + $(wc -l < "$f") - 1 ))
    done
    echo "java: chain recorded in $jwall s, $same of $NSEG segments SAME as the tree's, $jrows rows (rc $jrc)"
    [[ $jrc == 0 && $same == $NSEG ]] || fail=1
fi
if has java || has java-frames; then
    # the frames: each segment's fresh tape replayed with its shots
    jframes() {
        local n=$1 ticks=$2
        local d=$work/jf/dragon-s1chain-S$n
        rm -rf "$d"; mkdir -p "$d/frames"
        local tape=$jroot/out/java/snapshots/s1chain-S$n/tape.jsonl
        [[ -s $tape ]] || tape=$root/out/java/snapshots/s1chain-S$n/tape.jsonl
        ln -sfn "$(readlink -f "$tape")" "$d/tape.jsonl"
        make -s --no-print-directory -C oracle replay POOL=0 CACHE=0 REF="$d/tape.jsonl" FRAMES="$d/frames" \
            ARGS="--frame-ticks $ticks --frame-pts 0.5,1.0 --toasts --chat --tooltips --renderstate $d/rs" > "$d/log" 2>&1
        local r=$?
        # the tape's own frame rows draw others: keep the shots'
        local keep=" ${ticks//,/ } " f t
        for f in "$d"/frames/f_??????*.png; do
            t=$(basename "$f"); t=$((10#${t:2:6}))
            [[ $keep == *" $t "* ]] || rm -f "$f"
        done
        # the drops each shot's frame drew (the client's copies it renders)
        jq -c 'select(.pt == 1) | .tick0.t as $t | .ents[] | select((.k == "item" or .k == "orb") and .vis == 1)
            | {t: $t, k, x, y, z}' "$d/rs/frames.jsonl" > "$d/frames/drops.jsonl" 2>/dev/null
        rm -rf "$d/rs"
        if [[ $r == 0 ]] && grep -aq '^ORACLE REPLAY OK' "$d/log"; then
            echo "S$n OK $(ls "$d"/frames/f_*.png | wc -l)"
        else
            echo "S$n FAIL $(grep -a '^ORACLE' "$d/log" | tail -1)"
        fi
    }
    export -f jframes
    export jroot=$work/javaroot
    t0=$(date +%s.%N)
    for n in "${segs[@]}"; do echo "$n $(ticks_of "$n")"; done |
        xargs -P "$J" -L1 bash -c 'jframes "$0" "$1"' > "$res/java-frames.log" 2>&1
    jfwall=$(secs "$t0")
    # the fresh chain (about 1.5 GB) and the frame replays' run directories (0.7 GB) are done with
    rm -rf "$jroot" out/java/run/dragon-s1chain-S*-tape.replay
    jfok=$(grep -c ' OK ' "$res/java-frames.log")
    jnf=0 jsame=0 jnew=0
    for n in "${segs[@]}"; do
        src=$work/jf/dragon-s1chain-S$n/frames
        dst=$cf/dragon-s1chain-S$n
        if [[ ! -d $dst ]]; then
            # the first run: the frame judges' recording
            mkdir -p "$dst"
            cp "$src"/f_*.png "$src/drops.jsonl" "$dst/"
            echo "snapshots/s1chain-S$n" > "$dst/start"
            echo clientrand > "$dst/pins"
            jnew=$(( jnew + $(ls "$src"/f_*.png | wc -l) ))
        fi
        for f in "$src"/f_*.png; do
            jnf=$((jnf + 1))
            cmp -s "$f" "$dst/$(basename "$f")" && jsame=$((jsame + 1))
        done
    done
    echo "java: $jfok of $NSEG frame replays OK in $jfwall s, $jnf frames, $jsame byte-equal to $cf/dragon-s1chain-*$( ((jnew)) && echo " ($jnew installed now)")"
    [[ $jfok == $NSEG && $jsame == $jnf ]] || fail=1
    has java || { jwall=0; jrows=0; same=-; jrc=0; }
    tps=$(awk -v r="$jrows" -v s="$jwall" 'BEGIN { if (s > 0) printf "%.0f", r / s; else print "-" }')
    row "Java oracle (GoldBot records the chain)" "$jwall s" "$tps" "$jrows rows, $same/$NSEG tapes SAME as the tree's" \
        "$jnf frames drawn, $jsame byte-equal to the stored goldens" "$([[ $jrc == 0 && ( $same == - || $same == $NSEG ) && $jsame == $jnf ]] && echo PASS || echo FAIL)"
fi

# ---------------------------------------------------------------- C
if has c; then
    t0=$(date +%s.%N)
    out/native/recs -j $NSEG "${names[@]}" > "$res/c-replay.log" 2>&1
    crc=$?
    cwall=$(secs "$t0")
    cok=$(awk '$2 == "OK"' "$res/c-replay.log" | wc -l)
    crows=$(awk '$2 == "OK" { s += $3 } END { print s + 0 }' "$res/c-replay.log")
    csum=$(awk '$2 == "OK" { sub(/s$/, "", $5); s += $5 } END { printf "%.1f", s }' "$res/c-replay.log")
    echo "c: $cok of $NSEG segments replayed OK by test_snapshots, $crows rows, $cwall s wall ($csum s summed)"
    [[ $crc == 0 ]] || fail=1
    row "C engine (test_snapshots, $NSEG at once)" "$cwall s ($csum s one at a time)" \
        "$(awk -v r="$crows" -v w="$cwall" -v s="$csum" 'BEGIN { printf "%.0f (%.0f one process)", r / w, r / s }')" \
        "$crows rows, $cok/$NSEG OK (every field)" "-" "$([[ $crc == 0 ]] && echo PASS || echo FAIL)"

    t0=$(date +%s.%N)
    J=$J bash csrc/play/chain.sh --seed 1 > "$res/c-play.log" 2>&1
    prc=$?
    pwall=$(secs "$t0")
    pline=$(tail -1 "$res/c-play.log")
    prows=$(awk '/ LINED UP / { s += $11 } END { print s + 0 }' "$res/c-play.log")
    pok=$(grep -c ' LINED UP ' "$res/c-play.log")
    echo "c: play $pline"
    [[ $prc == 0 ]] || fail=1
fi
if has c || has c-frames; then

    # play's frames at the shots, the C renderer, against Java's
    mkdir -p "$work/cshots"
    cjudge() {
        local n=$1 d=$work/cshots/S$1
        rm -rf "$d"; mkdir -p "$d"
        bash csrc/tests/frame_judge.sh "out/java/clientframes/dragon-s1chain-S$n" --frames c --shot-dir "$d" > "$d.log" 2>&1
        echo "S$n rc $?"
    }
    export -f cjudge
    t0=$(date +%s.%N)
    printf '%s\n' "${segs[@]}" | xargs -P "$J" -I{} bash -c 'cjudge {}' > "$res/c-frames.rc" 2>&1
    fwall=$(secs "$t0")
    cat "$work"/cshots/S*.log > "$res/c-frames.log"
    fpass=$(grep -c '^PASS frame judge .* within its budget' "$res/c-frames.log")
    fdrops=$(grep -c '^PASS frame judge .* visible drops drawn' "$res/c-frames.log")
    ffail=$(grep '^FAIL frame judge' "$res/c-frames.log" | grep -vc 'no budget')
    fnob=$(grep -c 'no budget' "$res/c-frames.log")
    # pxdiff's clusters, Java's frame against play's, every shot
    pd=$work/pxdiff; rm -rf "$pd"; mkdir -p "$pd/a" "$pd/b"
    for n in "${segs[@]}"; do
        for f in "$work"/cshots/S$n/s_*.png; do
            [[ -e $f ]] || continue
            b=$(basename "$f" .png); t=${b:2:6}; p=${b:9}
            g=$cf/dragon-s1chain-S$n/f_$t.png
            [[ $p == p500 ]] && g=$cf/dragon-s1chain-S$n/f_$t.p500.png
            [[ $p == p1000 ]] || [[ $p == p500 ]] || continue
            ln -s "$root/$g" "$pd/a/S${n}_${t}_$p.png"
            ln -s "$f" "$pd/b/S${n}_${t}_$p.png"
        done
    done
    out/native/pxdiff frames --dir-a "$pd/a" --dir-b "$pd/b" --json \
        > "$res/c-pxdiff.json" 2> "$res/c-pxdiff.err"
    pxn=$(jq length "$res/c-pxdiff.json" 2>/dev/null || echo 0)
    pxclean=$(jq '[.[] | select(.cluster_px == 0)] | length' "$res/c-pxdiff.json" 2>/dev/null || echo 0)
    pxworst=$(jq -r '.[0] | "\(.frame) \(.cluster_px) px (\(.top_cause))"' "$res/c-pxdiff.json" 2>/dev/null)
    echo "c: frames: $fpass within budget, $ffail over, $fnob without a budget ($fwall s); pxdiff: $pxclean of $pxn with no cluster, worst $pxworst"
    [[ $ffail == 0 && $fnob == 0 ]] || fail=1
    has c || { pwall=-; prows=0; pok=-; prc=0; }
    row "C engine (csrc/play's client + Java's check, J=$J)" "$pwall s + frames $fwall s" \
        "$(awk -v r="$prows" -v s="$pwall" 'BEGIN { if (s > 0) printf "%.0f", r / s; else print "-" }')" \
        "$prows rows, $pok/$NSEG lined up (Java replays play's tape)" \
        "$((fpass + ffail + fnob)) vs Java: $fpass within budget, drops present in $fdrops; pxdiff $pxclean/$pxn no cluster, worst $pxworst" \
        "$([[ $prc == 0 && $ffail == 0 && $fnob == 0 ]] && echo PASS || echo FAIL)"
fi

# ---------------------------------------------------------------- GPU
if has gpu; then
    make -s -C csrc gpu-render-judge > "$res/gpu-build.log" 2>&1 || { echo "dragonrun: gpu build failed"; exit 1; }
    make -s -C csrc/runtime pipeline >> "$res/gpu-build.log" 2>&1 || { echo "dragonrun: pipeline build failed"; exit 1; }
    make -s -C csrc/runtime pipe-refs PRECS="${names[*]}" > "$res/gpu-refs.log" 2>&1
    t0=$(date +%s.%N)
    make -s -C csrc/runtime pipe-gate MESH=2 DRAW=1 N=$NSEG J=16 EPISODES=$NSEG PRECS="${names[*]}" > "$res/gpu-pipe.log" 2>&1
    grc=$?
    gwall=$(secs "$t0")
    gsum=$(grep '^pipe_gate: \(OK\|FAIL\)' "$res/gpu-pipe.log" | head -1)
    gok=$(grep -c '^s1chain-S[0-9]*: OK' "$res/gpu-pipe.log")
    growsn=$(awk '/^s1chain-S[0-9]*: OK/ { s += $5 } END { print s + 0 }' "$res/gpu-pipe.log")
    gframes=$(awk '/^s1chain-S[0-9]*: OK/ { s += $8 } END { print s + 0 }' "$res/gpu-pipe.log")
    gmesh=$(grep '^pipe_gate: device meshing' "$res/gpu-pipe.log" | sed 's/^pipe_gate: device meshing 2: //; s/, 0 frames lost.*//')
    echo "gpu: pipe_gate $gok of $NSEG OK, $growsn rows, $gframes frames bit-exact, $gwall s; $gmesh"
    [[ $grc == 0 ]] || fail=1
    row "GPU (pipeline: device meshing and drawing, $NSEG envs)" "$gwall s" \
        "$(awk -v r="$growsn" -v s="$gwall" 'BEGIN { printf "%.0f", r / s }')" \
        "$growsn rows equal to C's, $gok/$NSEG OK" \
        "$gframes 128x128 observations byte-equal to the C renderer; meshes: $gmesh" \
        "$([[ $grc == 0 && $gok == $NSEG ]] && echo PASS || echo FAIL)"
fi
if has gpu || has gpu-frames; then
    make -s -C csrc gpu-render-judge > "$res/gpu-build.log" 2>&1 || { echo "dragonrun: gpu build failed"; exit 1; }

    # the judges' device frames at the shots: the tape's render distance
    # (the strip, against C byte for byte), one recording at a time
    budgets=csrc/tests/frame_budget.txt
    t0=$(date +%s.%N)
    : > "$res/gpu-frames.log"; rm -f "$res/gpu-frames.retries"
    for n in "${segs[@]}"; do
        d=$work/gshots/S$n; rm -rf "$d"; mkdir -p "$d"
        dir=$cf/dragon-s1chain-S$n
        grep -E "^dragon-s1chain-S$n " "$budgets" | awk '{print $2, $3}' > "$d.shots"
        [[ -s $d.shots ]] || for f in "$dir"/f_??????.png; do t=$((10#$(basename "$f" .png | cut -c3-))); printf '%s 0.5\n%s 1.0\n' "$t" "$t"; done > "$d.shots"
        # the device is shared: a run it turned away (no free judge slot or
        # memory) runs again, up to five times, 20 s apart
        for try in 1 2 3 4 5 6; do
            CUDA_VISIBLE_DEVICES=${CUDA_DEV:-0} SDL_VIDEODRIVER=dummy out/native/play "out/java/snapshots/s1chain-S$n" --threads 2 \
                --replay "out/java/snapshots/s1chain-S$n/tape.jsonl" --tape "$d.tape" --shots "$d.shots" --shot-golden-dir "$dir" \
                --shot-items "$dir/drops.jsonl" --shot-dir "$d" --frames device --frames-check 1 > "$d.log" 2>&1
            prc=$?
            grep -q 'no device renderer\|device renderer stopped' "$d.log" || break
            [[ $try == 6 ]] || { echo "S$n: the device turned it away, again in 20 s" >> "$res/gpu-frames.retries"; sleep 20; }
        done
        echo "S$n rc $prc $(grep -c '^PASS device frame .*: the frame and its world depth equal' "$d.log") $(grep -c '^FAIL device frame' "$d.log") $(wc -l < "$d.shots")" >> "$res/gpu-frames.log"
        rm -f "$d.tape"
    done
    gfwall=$(secs "$t0")
    gfp=$(awk '{ s += $4 } END { print s + 0 }' "$res/gpu-frames.log")
    gff=$(awk '{ s += $5 } END { print s + 0 }' "$res/gpu-frames.log")
    gfs=$(awk '{ s += $6 } END { print s + 0 }' "$res/gpu-frames.log")
    echo "gpu: judge frames at the tape's render distance: $gfp bit-exact, $gff differ, $gfs shots ($gfwall s)"
    # the gate's own path: 854x480, render distance 8
    t0=$(date +%s.%N)
    CUDA_VISIBLE_DEVICES=${CUDA_DEV:-0} bash csrc/cuda/render/judge_gate.sh --size 854x480 --rd 8 --jobs 2 \
        $(printf 'dragon-s1chain-S%s ' "${segs[@]}") > "$res/gpu-judge-rd8.log" 2>&1
    jgrc=$?
    jgwall=$(secs "$t0")
    jgline=$(grep '^judge_gate:' "$res/gpu-judge-rd8.log" | tail -1)
    echo "gpu: $jgline ($jgwall s)"
    [[ $gff == 0 && $gfp == $gfs && $jgrc == 0 ]] || fail=1
    row "GPU (the judges' device frames, 854x480)" "$gfwall s + rd 8 $jgwall s" "-" "-" \
        "$gfp of $gfs device frames byte-equal to C (the tape's rd 4); rd 8: ${jgline#judge_gate: }" \
        "$([[ $gff == 0 && $gfp == $gfs && $jgrc == 0 ]] && echo PASS || echo FAIL)"
fi

# ---------------------------------------------------------------- the strips
# Java | C | GPU at every milestone, pt 1.0, on a dark background
if { has c || has c-frames; } && { has gpu || has gpu-frames; }; then
    rm -f "$res"/strip-*.png
    k=0
    while read -r n t label; do
        [[ $label == - ]] && continue
        k=$((k + 1))
        tt=$(printf '%06d' "$t")
        j=$cf/dragon-s1chain-S$n/f_$tt.png c=$work/cshots/S$n/s_$tt.p1000.png g=$work/gshots/S$n/s_$tt.p1000.png
        [[ -f $j && -f $c && -f $g ]] || { echo "strip $label: a frame is missing"; continue; }
        montage -quiet -background '#101014' -fill '#d0d0d0' -font DejaVu-Sans -pointsize 16 \
            -label "Java oracle" "$j" -label "C engine (play)" "$c" -label "GPU (device renderer)" "$g" \
            -tile 3x1 -geometry +6+6 -title "s1chain-S$n t=$t: $label" "$res/strip-$(printf '%02d' $k)-$label.png"
    done < "$work/shots.txt"
    montage -quiet -background '#101014' "$res"/strip-??-*.png -tile 1x -geometry +0+4 "$res/strips-all.png" 2>/dev/null
    echo "strips: $k milestones in $res/strip-*.png"
fi

# ---------------------------------------------------------------- the table
[[ -s $table.new ]] && mv "$table.new" "$table"
{
    echo "dragonrun $(date '+%Y-%m-%d %H:%M %Z') $(git rev-parse --short HEAD): seed 1, s1chain-S1..S$NSEG, $rows_total rows"
    printf '%-52s | %-24s | %-10s | %-58s | %-s | %s\n' engine "wall time" "ticks/s" "rows" "frames compared" result
    awk -F'\t' '{ printf "%-52s | %-24s | %-10s | %-58s | %s | %s\n", $1, $2, $3, $4, $5, $6 }' "$table"
} | tee "$res/dragonrun.txt"
exit $fail
