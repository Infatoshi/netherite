#!/bin/bash
# dbench.sh (lane/dbench): the dragon benchmark, `make -C csrc/runtime dragon-bench W=MODEL.ntw` (loop.mk).
#   dbench.sh run    -m MAKE -w W -v V -r RUNFILE -o DIR -n RUNS -t MINUTES -s "SPEC ..." -L LINKRUNS -l "STAGE:MIN ..." -B LIB -p PRE [-f]
#   dbench.sh report -o DIR -w W [-B LIB]
# run: the frozen eval (make train-score, ntloop --score) from spawn for each SPEC (spawn: seed 42's spawn; A-B,C:
# the captured starts of those seeds, starts.seeds) with --score-books, then each library stage S (the start of the
# stage, --library --stages S) for its MIN minutes: a directory each under DIR, skipped where its score.json is there
# (-f redoes it). PRE prefixes each job (the 3090's queue: gpuq.sh check --mem 12000 --).
# report: the tables from those directories' files (episodes.jsonl, books.jsonl, score.json) to DIR/dragon-bench.txt
# and dragon-bench.tsv. Nothing here is counted by hand: every row is a jq program over the runs' own output.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
JQ="$HERE/dbench.jq"
mode=${1:-}; shift || true
MAKE=make W= V= RUN=configs/tests/dbench.yaml OUT= RUNS=256 MIN=120 SPECS=spawn LRUNS=128 LINKS= PRE= FORCE=0 SEED=1000 MINFREE=25 LIB=out/library-train
SET=
while getopts "m:w:v:r:o:n:t:s:L:l:p:k:S:B:f" o; do
    case $o in
        S) SET=$OPTARG ;;
        B) LIB=$OPTARG ;;
        m) MAKE=$OPTARG ;; w) W=$OPTARG ;; v) V=$OPTARG ;; r) RUN=$OPTARG ;; o) OUT=$OPTARG ;; n) RUNS=$OPTARG ;;
        t) MIN=$OPTARG ;; s) SPECS=$OPTARG ;; L) LRUNS=$OPTARG ;; l) LINKS=$OPTARG ;; p) PRE=$OPTARG ;; k) SEED=$OPTARG ;;
        f) FORCE=1 ;;
        *) exit 2 ;;
    esac
done
[ -n "$OUT" ] || { echo "dbench.sh: -o DIR"; exit 2; }

score() {   # DIR MINUTES RUNS ARGS...: one train-score into DIR (skipped when done)
    local d=$1 min=$2 runs=$3; shift 3
    if [ -f "$d/score.json" ] && [ "$FORCE" = 0 ]; then echo "dbench: $d done"; return 0; fi
    mkdir -p "$d"
    # a job starts with MINFREE GB available (the host is shared); its peak resident is kept in DIR.rss (kB)
    while [ "$(awk '/MemAvailable/ {print int($2 / 1048576)}' /proc/meminfo)" -lt "$MINFREE" ]; do sleep 30; done
    echo "dbench: $(date +%T) $d: $runs runs x $min min"
    /usr/bin/time -f %M -o "$d.rss" $PRE $MAKE -s train-score RUN="$RUN" W="$W" V="$V" RUNS="$runs" MIN="$min" SEED="$SEED" SCORE_DIR="$d" \
        SCORE_ARGS="--score-books $d/books.jsonl $*" > "$d.out" 2>&1 || { tail -3 "$d.out"; echo "dbench: $d FAILED"; return 1; }
    tail -1 "$d.out" | cut -c1-120
}

spec_dir() {   # SPEC -> NAME (the directory) and ARGS
    case $1 in
        spawn|42) echo "spawn-s42" ;;
        *) echo "spawn-h$1" ;;
    esac
}

derive() {   # -S "key=value ...": RUNFILE with those world keys (difficulty=easy, keep_inventory=on) set in sim's world
    [ -n "$SET" ] || return 0
    # beside the other run files: the run file's relative paths (the cast's atlas) are read from its own directory
    local f="$HERE/../../../configs/zz-dbench-$(basename "$OUT").yaml" src="$RUN"
    case $RUN in /*) ;; *) src="$HERE/../../../$RUN" ;; esac
    mkdir -p "$OUT"
    # start=NAME replaces pipeline.start (the engine holds a start to the file's world: a start is recorded at one
    # difficulty and one keep_inventory, so these arms play starts recorded at theirs, out/java/snapshots/dbench-*)
    awk -v kv="$SET" 'BEGIN { n = split(kv, a, " "); for (i = 1; i <= n; ++i) { split(a[i], p, "="); key[p[1]] = p[2]; order[i] = p[1] } }
        /^  world:/ { print; for (i = 1; i <= n; ++i) if (order[i] != "start") printf "    %s: %s\n", order[i], key[order[i]]; next }
        /^    start:/ && ("start" in key) { printf "    start: %s\n", key["start"]; next }
        { for (i = 1; i <= n; ++i) if (order[i] != "start" && $0 ~ "^    " order[i] ":") next; print }' "$src" > "$f" || return 1
    RUN=$f
}

run() {
    bad=0
    derive || { echo "dbench: -S $SET: no world section in $RUN"; exit 2; }
    for s in $SPECS; do
        n=$(spec_dir "${s%/*}")
        case $s in
            spawn|42) score "$OUT/$n" "$MIN" "$RUNS" || bad=1 ;;
            */*)   # A-B/K: the seeds in K processes of RUNS / K runs each (a process holds every start it plays: 64 held 16.7 GB)
                r=${s%/*}; k=${s#*/}; lo=${r%-*}; hi=${r#*-}; sz=$(( (hi - lo + k) / k ))
                for ((i = 0; i < k; ++i)); do
                    a=$((lo + i * sz)); b=$((a + sz - 1)); [ $b -gt $hi ] && b=$hi
                    score "$OUT/$n.c$((i + 1))" "$MIN" "$((RUNS / k))" --seeds "$a-$b" --curriculum uniform || bad=1
                done ;;
            *) score "$OUT/$n" "$MIN" "$RUNS" --seeds "$s" --curriculum uniform || bad=1 ;;
        esac
    done
    for sm in $LINKS; do
        st=${sm%%:*}; min=${sm##*:}
        score "$OUT/link-$st" "$min" "$LRUNS" --library "$LIB" --stages "$st" --curriculum uniform || bad=1
    done
    [ -n "$SET" ] && cp "$RUN" "$OUT/run.yaml" && rm -f "$RUN"     # (kept in DIR, not in configs/)
    [ $bad = 0 ] && echo "DRAGON-BENCH RUN OK $OUT" || { echo "DRAGON-BENCH RUN FAILED"; exit 1; }
}

jqd() {   # DIR MODE [--arg a X]: dbench.jq over DIR's files
    local d=$1 m=$2; shift 2
    jq -n -r -f "$JQ" --slurpfile books "$d/books.jsonl" --slurpfile eps "$d/episodes.jsonl" --arg mode "$m" "$@"
}

merge_chunks() {   # DIR.c1 DIR.c2 ... -> DIR: the chunks' runs as one set (env ids made apart, runs summed)
    local base
    for base in $(ls -d "$OUT"/spawn-*.c1 2>/dev/null | sed 's/\.c1$//'); do
        [ -f "$base/score.json" ] && [ "$base/score.json" -nt "${base}.c1/score.json" ] && continue
        mkdir -p "$base"; : > "$base/books.jsonl"; : > "$base/episodes.jsonl"
        local i=0 c
        for c in "$base".c[0-9]*; do
            [ -f "$c/score.json" ] || continue
            [ $i = 0 ] && head -1 "$c/books.jsonl" >> "$base/books.jsonl"
            jq -c --argjson o $((i * 1000)) 'select(.env != null) | .env += $o' "$c/books.jsonl" >> "$base/books.jsonl"
            jq -c --argjson o $((i * 1000)) '.env += $o' "$c/episodes.jsonl" >> "$base/episodes.jsonl"
            i=$((i + 1))
        done
        jq -s -c '(map(.runs) | add) as $r | .[0] + {runs: $r, env_steps_per_s: ((map(.env_steps_per_s) | add) / length | round), wall: (map(.wall) | add), depth: ((map(.depth * .runs) | add) / $r), depth_se: null}' \
            "$base".c[0-9]*/score.json > "$base/score.json"
    done
}

row() { local IFS=$'\t'; printf '%s\n' "$*"; }   # a tsv line from its arguments (empty ones kept)
# notrate TRAJ TOP RUNS: the runs are too few distinct trajectories to be a rate: fewer than 8, or one trajectory holds
# over half of them (lane/rod: 118 of the Nether start's 128 runs died at one step; the diamond pickaxe start is two)
notrate() { [ "$1" -lt 8 ] || [ $(( $2 * 2 )) -gt "$3" ]; }
libn() {   # STAGE: the starts the library holds at that stage (index.tsv's stage column)
    local index=${LIBINDEX:-}
    if [ -z "$index" ]; then
        case $LIB in /*) index="$LIB/index.tsv" ;; *) index="$HERE/../../../$LIB/index.tsv" ;; esac
    fi
    awk -F'\t' -v s="$1" '$3 == s' "$index" 2>/dev/null | wc -l
}

report() {
    merge_chunks
    local txt="$OUT/dragon-bench.txt" tsv="$OUT/dragon-bench.tsv"
    : > "$txt"; : > "$tsv"
    row kind set item runs reached share median_min conversion se from_runs food0_end food0_first_min trajectories starts lib_starts top_group >> "$tsv"
    {
        echo "dragon-bench: model $(basename "$W"), $(date '+%F %T'); steps are 1/20 s; a run is one episode from the start; 'reached' = held (or entered) at any time"
        for d in "$OUT"/spawn-*; do
            [ -f "$d/score.json" ] && [ "${d%.c[0-9]}" = "$d" ] || continue
            nm=$(basename "$d")
            echo
            jq -r '"== \(.model | split("/") | last): from spawn \(.runs) runs x \(.minutes) min, eval seed \(.seed), \(.env_steps_per_s) env-steps/s, \(.wall) s; depth \(.depth) (SE \(.depth_se))"' "$d/score.json" | sed "s/from spawn/$nm: from spawn/"
            IFS=$'\t' read -r tj sts top <<< "$(jqd "$d" traj)"
            echo "trajectories: $tj distinct among the $(jq -r '.runs' "$d/score.json") runs (the largest identical group $top), from $sts start(s)$(notrate "$tj" "$top" "$(jq -r '.runs' "$d/score.json")" && echo ' -- NOT A RATE: fewer than 8 distinct, or one trajectory holds over half')"
            row trajectories "$(basename "$d")" all "$(jq -r '.runs' "$d/score.json")" "" "" "" "" "" "" "" "" "$tj" "$sts" "" "$top" >> "$tsv"
            jqd "$d" funnel
            jqd "$d" deaths
            jqd "$d" funnel_tsv | awk -F'\t' -v s="$nm" '{printf "spawn\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", s, $1, ($3 > 0 ? int($2 / $3 + 0.5) : ""), $2, $3, $4, $5, $6, $7, $8, $9}' >> "$tsv"
        done
        echo
        echo "== link by link from the library's starts (a start at each stage; the next milestone's rate within the stage's length)"
        echo "   traj = distinct trajectories behind the runs (a death is its first death: step, dimension, driver, killer; a survivor its"
        echo "   milestones and end), top = the runs the largest identical one holds, starts = distinct starts played of lib in the library"
        printf '%-18s %4s %5s  %-32s %5s %4s %6s %5s %4s %7s  %s\n' stage min runs "link" "start" "hit" "rate" "traj" "top" "starts" ""
        for d in "$OUT"/link-*; do
            [ -f "$d/score.json" ] && [ "${d%.c[0-9]}" = "$d" ] || continue
            st=${d##*/link-}
            min=$(jq -r '.minutes' "$d/score.json")
            for ln in $(jqd "$d" stage_links --arg a "$st"); do
                r=$(jqd "$d" link_rate --arg a "$ln")
                IFS=$'\t' read -r _ n hit rate tj sts top <<< "$r"
                lib=$(libn "$st"); flag=; notrate "$tj" "$top" "$n" && flag="NOT A RATE ($tj distinct, the largest group $top of $n)"
                printf '%-18s %4s %5s  %-32s %5s %4s %6s %5s %4s %4s/%-2s  %s\n' "$st" "$min" "$(jq -r '.runs' "$d/score.json")" "$ln" "$n" "$hit" "$([ "$rate" = null ] && echo - || awk -v x="$rate" 'BEGIN{printf "%.3f", x}')" "$tj" "$top" "$sts" "$lib" "$flag"
                row link "$st" "$ln" "$n" "$hit" "$rate" "" "" "" "" "" "" "$tj" "$sts" "$lib" "$top" >> "$tsv"
            done
        done
        echo
        echo "== the links ranked by what fixing each adds to the from-spawn dragon rate (the product of the library-start links' rates;"
        echo "   a link with no hit at its 95% upper bound 3/runs; 'fixed' = that link raised to 0.5, or left if already over)"
        awk -F'\t' '$1 == "link" && $4 > 0 {
                r = $6 + 0; u = ($5 == 0) ? 3.0 / $4 : r; if (u > 1) u = 1
                if ($5 == 0) mark[$3] = "<"; tjn[$3] = $13; topn[$3] = $16; rate[$3] = r; up[$3] = u; n[$3] = $4; hit[$3] = $5; order[++k] = $3; P = (P == "" ? 1 : P) * u
            } END {
                if (!k) { print "   (no link measured)"; exit }
                printf "   product of the %d links measured %.3g: one dragon in %.3g runs\n", k, P, (P > 0 ? 1 / P : 0)
                printf "   %-34s %6s %6s %9s %12s\n", "link", "runs", "rate", "x if fixed", "new product"
                for (i = 1; i <= k; ++i) { l = order[i]; f = (up[l] < 0.5) ? 0.5 / up[l] : 1; fac[l] = f; srt[i] = l }
                for (i = 1; i <= k; ++i) for (j = i + 1; j <= k; ++j) if (fac[srt[j]] > fac[srt[i]]) { t = srt[i]; srt[i] = srt[j]; srt[j] = t }
                for (i = 1; i <= k; ++i) { l = srt[i]; printf "   %-34s %6d %s%5.3f %9.1f %12.3g  %s\n", l, n[l], (mark[l] == "<" ? "<" : " "), up[l], fac[l], P * fac[l], (tjn[l] + 0 < 8 || 2 * topn[l] > n[l] ? "NOT A RATE (" tjn[l] " distinct, largest group " topn[l] ")" : tjn[l] " distinct") }
            }' "$tsv"
        echo
        echo "== where the runs stop: every link under 0.5 (from the library's start of its stage, then from spawn)"
        for d in "$OUT"/link-*; do
            [ -f "$d/score.json" ] && [ "${d%.c[0-9]}" = "$d" ] || continue
            st=${d##*/link-}
            for ln in $(jqd "$d" stage_links --arg a "$st"); do
                r=$(jqd "$d" link_rate --arg a "$ln")
                IFS=$'\t' read -r _ n hit rate tj sts top <<< "$r"
                [ "$n" -gt 0 ] 2>/dev/null || continue
                awk -v x="$rate" 'BEGIN{exit !(x < 0.5)}' || continue
                echo
                echo "[library start: $st, $(jq -r '.minutes' "$d/score.json") min, $(jq -r '.runs' "$d/score.json") runs; $tj distinct trajectories (largest group $top) from $sts of $(libn "$st") library start(s)$(notrate "$tj" "$top" "$n" && echo ' -- NOT A RATE')]"
                jqd "$d" link_fail --arg a "$ln"
            done
        done
        for d in "$OUT"/spawn-*; do
            [ -f "$d/score.json" ] && [ "${d%.c[0-9]}" = "$d" ] || continue
            nm=$(basename "$d")
            for ln in $(jqd "$d" linknames); do
                r=$(jqd "$d" link_rate --arg a "$ln")
                IFS=$'\t' read -r _ n hit rate tj sts top <<< "$r"
                [ "$n" -gt 0 ] 2>/dev/null || continue
                awk -v x="$rate" 'BEGIN{exit !(x < 0.5)}' || continue
                echo
                echo "[from spawn: $nm; $tj distinct trajectories among these $n runs (largest group $top)$(notrate "$tj" "$top" "$n" && echo ' -- NOT A RATE')]"
                jqd "$d" link_fail --arg a "$ln"
            done
        done
    } >> "$txt"
    cat "$txt"
    echo "DRAGON-BENCH OK $txt $tsv"
}

case $mode in
    run) run ;;
    report) report ;;
    *) echo "usage: dbench.sh run|report -o DIR ..."; exit 2 ;;
esac
