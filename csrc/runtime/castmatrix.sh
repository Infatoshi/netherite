#!/bin/bash
# The cast's precision matrix (lane/cxmatrix, castmatrix.mk): for every observation size and every instance of the
# textured cast (client.cast_precision 64, 32, 16, fixed), the device kernel's time a launch on a stored cast, the
# host cast's cycles an env-step, env-steps/s end to end (vec_bench with the host cast; the trainer where a net
# exists: 16x16 w1 with the host and the device cast, 64x64 s1 with the device cast) and what differs from FP64
# (pool_gate --cast-cmp-with), then one line a size: each speedup against FP64's and how much of the kernel's (the
# host cast's) reaches env-steps/s.
#
#   bash csrc/runtime/castmatrix.sh [-s "16 32 64 128"] [-p "64 32 16 fixed"] [-o DIR] [-d DUMPDIR] [-q GPUQ]
#                                   [-r "RECS"] [-n "W1.ntw S1.ntw"] [-t STEPS] [STAGE...]
#   STAGE (default all, in this order): cfg probe dumps gpu host train cmp table
#
# Run from the tree's root, everything built (castmatrix.mk does both). Every stage writes under DIR
# (out/runtime/castmatrix); `table` reads only what the others wrote, so `STAGE=table` prints it again.
#   cfg    out/runtime/train/cfg/cm-{h,d}SIZE-FP.yaml: the trainer's 16x16 host (host.yaml) and device (dev.yaml)
#          configs at each size and instance
#   probe  each binary small before anything at size (a new build's first run: 16 envs, a few hundred steps): vec_bench
#          with the host cast at each size, the three trainer arms; a probe that fails is a cell that says why (an
#          instance the tree does not have yet is refused by its config: "missing")
#   dumps  vec_bench's random actions on dev.yaml at each size and instance (64 envs, one group), group 0's cast 600
#          stored once in DUMPDIR (keyed by the dump format's and the device cast's sources): random actions read no
#          frame, so every instance at a size casts the same worlds
#   gpu    rcbench on each dump in one gpu window (abrun, 3 rounds): ms a launch, the frames and seen records the dump's
#   host   vec_bench with the host cast (128 envs, 2 groups, 16 threads, STEPS from the size) under perf record in one
#          bench window (abrun, 2 rounds): the cast's cycles an env-step (the functions of raycast*.o) and env-steps/s
#   train  ntloop --policy actor (the learner on) in one bench window (abrun, 2 rounds): t16h (16x16 w1, host cast,
#          512 envs), t16d (the same, device cast, served), t64d (64x64 s1, device cast, 256 envs, served)
#   cmp    pool_gate --cast-cmp-with over RECS at each size, FP64 against each other instance: the textured cast's
#          pixels per million that differ; and once --cast-cmp --semcam 64x36:64, the rays that differ (pool_gate counts
#          rays for the semantic camera alone, which has FP64 and FP32 only); cached by the pool_gate and libneth
#          binaries and the recordings
#   table  the matrix and the lines a size
set -u
SIZES="16 32 64 128"
FPS="64 32 16 fixed"
O=out/runtime/castmatrix
D=${NETHER_CACHE:-.cache}/native/cmdumps
Q=
RECS="sr-speedrun-s1 swim-s42 phys-lavaswim-s1 nether-trip-s1 end-dragon-s1 cov-villagerep-s42"
NETS="${NETHER_CACHE:-.cache}/native/actor-a05-070/weights.ntw ${NETHER_CACHE:-.cache}/native/actor-fresh-s1/weights.ntw"
TSTEPS=2000
RT=out/runtime
NT=$RT/train
CFG=$NT/cfg

# hostrun CFG NS GROUPS THREADS STEPS SYMS: vec_bench (the host cast, random actions, fixed work) under perf record
# (cycles:u, a sample every 1000003): its own lines, then the cast's cycles an env-step (the functions SYMS names)
# and the process's
if [ "${1:-}" = hostrun ]; then
    shift
    c=$1 n=$2 g=$3 t=$4 s=$5 syms=$6
    tmp=$(mktemp -d /tmp/castmatrix.XXXXXX) || exit 1
    trap 'rm -rf "$tmp"' EXIT
    CUDA_VISIBLE_DEVICES= perf record -q -o "$tmp/p.data" -e cycles:u -c 1000003 -- $RT/vec_bench --config "$c" --ns "$n" \
        --groups "$g" --threads "$t" --warm 100 --steps "$s" --ep-len 3600 --macros 63 --digest > "$tmp/o.txt" 2>&1
    rc=$?
    cat "$tmp/o.txt"
    [ $rc = 0 ] || exit $rc
    grep -q 'env-steps/s' "$tmp/o.txt" || exit 1
    # perf report's samples by symbol (the period is fixed: samples x period = cycles)
    perf report -q -i "$tmp/p.data" --no-children --sort sym -F overhead,sample,sym --stdio 2>/dev/null |
        awk -v syms="$syms" -v steps=$(( n * (s + 100) )) -v per=1000003 '
        BEGIN { while ((getline l < syms) > 0) want[l] = 1 }
        NF >= 3 { k = $2 + 0; all += k; nm = $NF; sub(/\..*$/, "", nm); if (nm in want) c += k }
        END { if (all == 0) exit 1
              printf "cast cycles %.0f an env-step (%.1f%% of the process: process cycles %.0f an env-step)\n",
                     c * per / steps, 100 * c / all, all * per / steps }'
    exit
fi

while getopts "s:p:o:d:q:r:n:t:" a; do
    case $a in
        s) SIZES=$OPTARG ;; p) FPS=$OPTARG ;; o) O=$OPTARG ;; d) D=$OPTARG ;; q) Q=$OPTARG ;; r) RECS=$OPTARG ;;
        n) NETS=$OPTARG ;; t) TSTEPS=$OPTARG ;; *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
STAGES=${*:-cfg probe dumps gpu host train cmp table}
NETW=${NETS%% *}
NETS1=${NETS##* }
mkdir -p "$O" || exit 1

# the GPU queue's check slot and windows (none: run directly)
qcheck() { if [ -n "$Q" ]; then "$Q" check -- "$@"; else "$@"; fi; }
qwin() { local k=$1; shift; if [ -n "$Q" ]; then "$Q" "$k" -- "$@"; else "$@"; fi; }
# pool_gate's name for an instance; vec_bench's steps at a size, 1024000 / size^2 + 12800 / size: a measured window of
# 1 to 4 s at 16 threads (the env's own 250k cycles an env-step and the cast's, which grows with the pixels; 12800 / size
# alone left 16x16 a 0.4 s window, its env-steps/s 5% apart in two runs, 2026-10-31)
tag() { case $1 in 32) echo f32 ;; 16) echo f16 ;; fixed) echo fx ;; *) echo f64 ;; esac; }
hsteps() { echo $(( 1024000 / ($1 * $1) + 12800 / $1 > 20 ? 1024000 / ($1 * $1) + 12800 / $1 : 20 )); }
has() { case " $SIZES " in *" $1 "*) return 0 ;; esac; return 1; }
# state.tsv: KEY<TAB>ok or the reason a cell was not run
state() { awk -F'\t' -v k="$1" '$1 == k {v = $2} END {print v}' "$O/state.tsv" 2>/dev/null; }
setstate() { { grep -v "^$1	" "$O/state.tsv" 2>/dev/null; printf '%s\t%s\n' "$1" "$2"; } > "$O/state.tsv.n"; mv "$O/state.tsv.n" "$O/state.tsv"; }
# a probe's verdict from its log: ok, missing (the config refuses the instance) or the failing line
verdict() {
    local rc=$1 log=$2 want=$3
    if [ "$rc" = 0 ] && grep -q "$want" "$log"; then echo ok; return; fi
    if grep -qiE 'cast_precision|not implemented|not supported' "$log"; then
        echo "missing: $(grep -iE -m1 'cast_precision|not implemented|not supported' "$log" | cut -c1-90)"
    else echo "failed (rc $rc): $(grep -v '^gpuq' "$log" | tail -1 | cut -c1-90)"; fi
}

for st in $STAGES; do case $st in
cfg)
    for s in $SIZES; do for p in $FPS; do for k in h:host d:dev; do
        f=$CFG/cm-${k%%:*}$s-$p.yaml
        awk -v s="$s" -v p="$p" '/^    cast_precision:/ {next}
            {sub(/observation: 16x16/, "observation: " s "x" s); print}
            /^    camera: raycast$/ {print "    cast_precision: " p}' "$CFG/${k#*:}.yaml" > "$f"
        grep -q "^    cast_precision: $p\$" "$f" && grep -q "observation: ${s}x$s" "$f" || { echo "cfg: $f not made"; exit 1; }
    done; done; done
    echo "cfg: $(ls $CFG/cm-*.yaml | wc -l) configs in $CFG" ;;

probe)
    rm -rf "$O/probe"; mkdir -p "$O/probe"
    A="--groups 2 --threads 4 --warm 20 --steps 200 --ep-len 3600 --macros 63"
    pids=
    for s in $SIZES; do for p in $FPS; do
        ( CUDA_VISIBLE_DEVICES= $RT/vec_bench --config $CFG/cm-h$s-$p.yaml --ns 16 $A --digest > "$O/probe/h$s-$p.log" 2>&1
          echo $? > "$O/probe/h$s-$p.rc" ) & pids="$pids $!"
    done; wait $pids; pids=; done
    for s in $SIZES; do for p in $FPS; do
        setstate "h$s-$p" "$(verdict "$(cat "$O/probe/h$s-$p.rc")" "$O/probe/h$s-$p.log" 'env-steps/s')"
    done; done
    for p in $FPS; do
        has 16 && { qcheck $NT/ntloop --config $CFG/cm-h16-$p.yaml --ns 16 $A --policy actor --init $NETW --depth 0 \
            > "$O/probe/t16h-$p.log" 2>&1; echo $? > "$O/probe/t16h-$p.rc"; } &
        has 16 && { qcheck $NT/ntloop --config $CFG/cm-d16-$p.yaml --ns 16 $A --policy actor --init $NETW --serve 1 \
            > "$O/probe/t16d-$p.log" 2>&1; echo $? > "$O/probe/t16d-$p.rc"; } &
        has 64 && { qcheck $NT/s1/ntloop --config $CFG/cm-d64-$p.yaml --ns 16 $A --policy actor --init $NETS1 --serve 1 \
            > "$O/probe/t64d-$p.log" 2>&1; echo $? > "$O/probe/t64d-$p.rc"; } &
        wait
    done
    for f in "$O"/probe/t*.rc; do
        [ -f "$f" ] || continue
        k=$(basename "$f" .rc)
        setstate "$k" "$(verdict "$(cat "$f")" "$O/probe/$k.log" 'env-steps/s')"
    done
    echo "probe: $(grep -c '	ok$' "$O/state.tsv") ok; $(grep -vc '	ok$' "$O/state.tsv") not:"
    grep -v '	ok$' "$O/state.tsv" | sed 's/^/  /' ;;

dumps)
    mkdir -p "$D" || exit 1
    pids=
    for s in $SIZES; do for p in $FPS; do
        d=$D/c$s-$p.dump
        if [ -s "$d" ]; then setstate "k$s-$p" ok; continue; fi
        ( qcheck $RT/vec_bench --config $CFG/cm-d$s-$p.yaml --ns 64 --groups 1 --threads 16 --warm 650 --steps 10 \
            --ep-len 3600 --macros 63 --dump-cast 600:$d.tmp > "$d.log" 2>&1; rc=$?
          grep -q '^--dump-cast: ' "$d.log" && mv "$d.tmp" "$d"; rm -f "$d.tmp"; echo $rc > "$d.rc" ) & pids="$pids $!"
    done; wait $pids; pids=; done
    for s in $SIZES; do for p in $FPS; do
        d=$D/c$s-$p.dump
        [ -f "$d.rc" ] && setstate "k$s-$p" "$(verdict "$(cat "$d.rc")" "$d.log" '^--dump-cast: ')"
        rm -f "$d.rc"
    done; done
    echo "dumps: $(ls $D/*.dump 2>/dev/null | wc -l) in $D" ;;

gpu)
    : > "$O/gpu.tsv"
    for s in $SIZES; do for p in $FPS; do
        [ "$(state k$s-$p)" = ok ] && printf 'k%s-%s\tout/native/cuda/rcbench %s\n' "$s" "$p" "$D/c$s-$p.dump" >> "$O/gpu.tsv"
    done; done
    rm -rf "$O/log/gpu"; mkdir -p "$O/log/gpu"
    qwin gpu out/native/abrun -r 3 -k -t 120 --lower --no-digest --require 'frames equal, seen equal' \
        -m '([0-9.]+) ms a launch' --log "$O/log/gpu" -f "$O/gpu.tsv" > "$O/gpu.out" 2>&1
    echo "gpu: $(tail -1 "$O/gpu.out")" ;;

host)
    for f in $RT/pic/raycast*.o; do nm --defined-only "$f"; done | awk '$2 ~ /^[tT]$/ {n = $3; sub(/\..*$/, "", n); print n}' |
        sort -u > "$O/castsyms.txt"
    : > "$O/host.tsv"
    for s in $SIZES; do for p in $FPS; do
        [ "$(state h$s-$p)" = ok ] && printf 'h%s-%s\tbash csrc/runtime/castmatrix.sh hostrun %s 128 2 16 %s %s\n' "$s" "$p" \
            "$CFG/cm-h$s-$p.yaml" "$(hsteps "$s")" "$O/castsyms.txt" >> "$O/host.tsv"
    done; done
    rm -rf "$O/log/host"; mkdir -p "$O/log/host"
    qwin bench out/native/abrun -r 2 -k -t 200 --lower --no-digest -m 'cast cycles ([0-9]+)' --log "$O/log/host" \
        -f "$O/host.tsv" > "$O/host.out" 2>&1
    echo "host: $(tail -1 "$O/host.out")" ;;

train)
    : > "$O/train.tsv"
    T="--groups 4 --threads 32 --warm 200 --steps $TSTEPS --ep-len 3600 --macros 63 --policy actor"
    for p in $FPS; do
        [ "$(state t16h-$p)" = ok ] && printf 't16h-%s\t%s/ntloop --config %s --ns 512 %s --init %s --depth 0\n' "$p" $NT \
            $CFG/cm-h16-$p.yaml "$T" $NETW >> "$O/train.tsv"
        [ "$(state t16d-$p)" = ok ] && printf 't16d-%s\t%s/ntloop --config %s --ns 512 %s --init %s --serve 1\n' "$p" $NT \
            $CFG/cm-d16-$p.yaml "$T" $NETW >> "$O/train.tsv"
        [ "$(state t64d-$p)" = ok ] && printf 't64d-%s\t%s/s1/ntloop --config %s --ns 256 %s --init %s --serve 1\n' "$p" $NT \
            $CFG/cm-d64-$p.yaml "$T" $NETS1 >> "$O/train.tsv"
    done
    rm -rf "$O/log/train"; mkdir -p "$O/log/train"
    qwin bench out/native/abrun -r 2 -k -t 200 --no-digest -m 'groups: ([0-9]+) env-steps/s' --log "$O/log/train" \
        -f "$O/train.tsv" > "$O/train.out" 2>&1
    echo "train: $(tail -1 "$O/train.out")" ;;

cmp)
    mkdir -p "$O/cmp"
    key=$(cat $RT/pool_gate $RT/libneth.so | cksum | cut -d' ' -f1)-$(echo "$RECS" | cksum | cut -d' ' -f1)
    for s in $SIZES; do for p in $FPS; do
        [ "$p" = 64 ] && continue
        f=$O/cmp/$p-$s-$key.log
        [ -s "$f" ] && grep -q '^cast-cmp total' "$f" && continue
        [ "$(state h$s-$p)" = ok ] || continue
        $RT/pool_gate -n 16 -j 16 --ticks 1 --episodes $(echo $RECS | wc -w) --cast-cmp-with "$(tag $p)" --raycast ${s}x$s \
            $RECS > "$f" 2>&1 || echo "cmp $p at $s: $(tail -1 "$f")"
    done; done
    # the rays: pool_gate counts the rays that differ for the semantic camera alone (its FP64 against its FP32, the one
    # other instance it has); the textured cast's comparison is its pixels
    f=$O/cmp/sem-$key.log
    if ! { [ -s "$f" ] && grep -q '^cast-cmp total' "$f"; }; then
        s=${SIZES%% *}
        $RT/pool_gate -n 16 -j 16 --ticks 1 --episodes $(echo $RECS | wc -w) --cast-cmp --semcam 64x36:64 --raycast ${s}x$s \
            $RECS > "$f" 2>&1 || echo "cmp semcam: $(tail -1 "$f")"
    fi
    echo "$key" > "$O/cmp/key"
    echo "cmp: $(ls "$O"/cmp/*-"$key".log 2>/dev/null | wc -l) runs" ;;

table)
    key=$(cat "$O/cmp/key" 2>/dev/null)
    # the median of a metric over a window's runs (the runs that passed: the metric found, and rcbench's own check)
    med() {
        local win=$1 name=$2 re=$3
        for f in "$O/log/$win/$name"-r*.log; do
            [ -f "$f" ] || continue
            [ "$win" = gpu ] && ! grep -q 'frames equal, seen equal' "$f" && continue
            # the last match, and in it the first number (each metric's regex has its number first or after words)
            grep -oE "$re" "$f" | tail -1 | grep -oE '[0-9]+(\.[0-9]+)?' | head -1
        done | sort -g | awk '{v[NR] = $1} END {if (NR) {m = NR % 2 ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2
                                                         if (m >= 1000) printf "%.0f\n", m; else printf "%.4g\n", m}}'
    }
    # a cell: the value, or why there is none (a probe's reason, abbreviated)
    # (ok and no value: its window has not run, or its runs failed: the window's .out says which)
    why() { local s; s=$(state "$1"); case $s in ok|"") echo "-" ;; missing*) echo "missing" ;; *) echo "failed" ;; esac; }
    # cast-cmp's total line: its rays' ppm (field 1) or its pixels' (2)
    ppm() {
        [ -s "$2" ] || { echo -; return; }
        awk -v w="$1" '/^cast-cmp total/ {v = "-"; m = 0; for (i = 1; i <= NF; ++i) if ($i == "differ" && ++m == w) v = $(i + 1)}
             END {print (v == "" ? "-" : v)}' "$2"
    }
    # the rays (the semantic camera's, FP32 alone) and the textured cast's pixels at a size
    cmpv() {
        [ "$2" = 64 ] && { echo "0 0"; return; }
        echo "$([ "$2" = 32 ] && ppm 1 "$O/cmp/sem-$key.log" || echo n/a) $(ppm 2 "$O/cmp/$2-$1-$key.log")"
    }
    fpname() { case $1 in fixed) echo fixed ;; *) echo "FP$1" ;; esac; }
    {
    printf '%-7s %-6s %9s %6s %11s %6s %9s %6s %9s %6s %9s %6s %9s %6s %9s %9s\n' size cast kernel_ms x cast_cyc x vec_es/s x \
        t16h_es/s x t16d_es/s x t64d_es/s x rays_ppm px_ppm
    for s in $SIZES; do
        k64=$(med gpu k$s-64 '([0-9.]+) ms a launch'); c64=$(med host h$s-64 'cast cycles ([0-9]+)')
        v64=$(med host h$s-64 '([0-9]+) env-steps/s'); a64=$(med train t16h-64 'groups: ([0-9]+) env-steps/s')
        b64=$(med train t16d-64 'groups: ([0-9]+) env-steps/s'); e64=$(med train t64d-64 'groups: ([0-9]+) env-steps/s')
        for p in $FPS; do
            k=$(med gpu k$s-$p '([0-9.]+) ms a launch'); c=$(med host h$s-$p 'cast cycles ([0-9]+)')
            v=$(med host h$s-$p '([0-9]+) env-steps/s')
            a=; b=; e=
            [ "$s" = 16 ] && a=$(med train t16h-$p 'groups: ([0-9]+) env-steps/s') b=$(med train t16d-$p 'groups: ([0-9]+) env-steps/s')
            [ "$s" = 64 ] && e=$(med train t64d-$p 'groups: ([0-9]+) env-steps/s')
            set -- $(cmpv "$s" "$p")
            awk -v s="${s}x$s" -v p="$(fpname $p)" -v k="${k:-$(why k$s-$p)}" -v k0="$k64" -v c="${c:-$(why h$s-$p)}" -v c0="$c64" \
                -v v="${v:-$(why h$s-$p)}" -v v0="$v64" -v a="${a:-$([ $s = 16 ] && why t16h-$p || echo -)}" -v a0="$a64" \
                -v b="${b:-$([ $s = 16 ] && why t16d-$p || echo -)}" -v b0="$b64" -v e="${e:-$([ $s = 64 ] && why t64d-$p || echo -)}" \
                -v e0="$e64" -v r="$1" -v px="$2" '
                function num(x) { return x ~ /^[0-9.]+$/ }
                function sp(x, x0, lower) { return num(x) && num(x0) && x > 0 && x0 > 0 ? sprintf("%.2f", lower ? x0 / x : x / x0) : "-" }
                BEGIN { printf "%-7s %-6s %9s %6s %11s %6s %9s %6s %9s %6s %9s %6s %9s %6s %9s %9s\n", s, p, k, sp(k, k0, 1),
                        c, sp(c, c0, 1), v, sp(v, v0, 0), a, sp(a, a0, 0), b, sp(b, b0, 0), e, sp(e, e0, 0), r, px }'
        done
    done
    } > "$O/table.txt"
    # one line a size: the kernel's (the host cast's) speedup against FP64 and the env-steps/s speedup it became
    awk 'NR > 1 { s = $1; if (!(s in seen)) { seen[s] = 1; order[++n] = s }
                  if ($2 == "FP64") next
                  if ($5 == "missing" && ($3 == "missing" || $3 == "-")) l = $2 " missing"
                  else { l = $2 ": kernel " x($4)
                         if (n_($14)) l = l " -> t64d " x($14) reach($4, $14)
                         if (n_($12)) l = l " -> t16d " x($12) reach($4, $12)
                         l = l ", host cast " x($6) " -> vec_bench " x($8) reach($6, $8)
                         if (n_($10)) l = l ", t16h " x($10) reach($6, $10) }
                  line[s] = line[s] (line[s] == "" ? "" : " | ") l }
         function n_(v) { return v ~ /^[0-9.]+$/ }
         function x(v) { return n_(v) ? v "x" : "-" }
         # the share of a gain (or a loss) against FP64 that reached env-steps/s
         function reach(g, e) { return (n_(g) && n_(e) && g != 1) ? sprintf(" (%.0f%%)", 100 * (e - 1) / (g - 1)) : "" }
         END { for (i = 1; i <= n; ++i) printf "%s: %s\n", order[i], line[order[i]] }' "$O/table.txt" > "$O/lines.txt"
    cat "$O/table.txt"
    echo
    cat "$O/lines.txt"
    echo "(x: the speedup against FP64 at that size, higher is faster; kernel_ms rcbench's median ms a launch, 64 envs; cast_cyc the"
    echo " host cast's cycles an env-step under perf, vec_es/s its env-steps/s (128 envs, 16 threads); t16h/t16d the 16x16 w1"
    echo " trainer with the host/device cast (512 envs), t64d the 64x64 s1 trainer with the device cast (256 envs); per million"
    echo " that differ from FP64 over RECS: px_ppm the textured cast's pixels, rays_ppm the semantic camera's rays (64x36; it has"
    echo " FP32 alone: n/a for the others); missing: the tree has no such instance; failed: its probe failed ($O/probe);"
    echo " a line's (N%): the share of the kernel's (the host cast's) speedup against FP64 that reached that env-steps/s)" ;;

*) echo "castmatrix: unknown stage $st"; exit 2 ;;
esac; done
