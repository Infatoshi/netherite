#!/usr/bin/env bash
# The selective self-check's history proof (lane/selfselect), from the
# trees tests/selhist.sh ran: for each merge, the recordings whose full
# self-check outcome differs between the merge's base and the merge (the
# ones a trial must replay), the ones SelfSelect picks from the base's
# coverage, and the misses (changed, not picked).
#
#   bash tests/selhist-eval.sh HISTDIR MERGE:BASETREE:MERGETREE ...
#
# HISTDIR/TREE is selhist.sh's output for TREE (outcome.tsv, cov/). An
# outcome is the self-check line without its seconds (OK and the rows, or
# the first divergence, or the failure). The pick is SelfSelect select
# with BASETREE's coverage against MERGETREE's fingerprints over every
# recording of the list, tests/selfselect.racy as this tree has it, and a
# fallback to every recording when the two trees' replay inputs differ
# (tests/selinputs.sh), as make selfcheck SELECT=1 does. Prints one table
# row per merge and writes HISTDIR/eval/MERGE.{changed,selected,select.txt}.
# The map columns are what the mapping alone picks, the fallback set aside
# (SelfSelect select ... nofallback), and its misses.
# The seconds are the fresh coverage replays' own (J at a time on a loaded
# host): their sums compare the two sets, not a trial's wall clock.
set -u
export LC_ALL=C
H=$1; shift
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/.." && pwd)
CP="$ROOT/out/java/covtool:$ROOT/out/java/jacoco-0.8.12/lib/jacococli.jar:$ROOT/out/java/lib/*"
mkdir -p "$H/eval"
printf '%-9s %-9s %-9s %5s %5s %6s %7s %6s %5s %6s %9s %9s  %s\n' merge base tree recs sel frac changed misses map map_ms sec_full sec_sel "fallback / changed recordings"
tm=0; ts=0; tc=0; tx=0; tsf=0; tss=0; ts2=0; tx2=0
for spec in "$@"; do
    IFS=: read -r m b t <<< "$spec"
    B=$H/$b T=$H/$t E=$H/eval/$m
    for d in "$B" "$T"; do [ -f "$d/done" ] || { echo "$m: $d is not finished"; continue 2; }; done
    # the recordings this merge's trial ran: the base tree's list (tests/selhist.sh LIST;
    # the merge tree's may hold later ones)
    cut -f1 "$B/outcome.tsv" > "$E.recs"
    fb=
    d=$(diff "$B/cov/inputs.tsv" "$T/cov/inputs.tsv" | grep '^[<>]' | cut -d' ' -f2 | sort -u | tr '\n' ' ')
    [ -z "$d" ] || fb="inputs: $d"
    java -cp "$CP" SelfSelect select "$B/cov" "$T/cov/fingerprints.tsv" "$E.recs" "$E.selected" "$HERE/tests/selfselect.racy" > "$E.select.txt" 2>&1 || { echo "$m: select failed"; tail -3 "$E.select.txt"; continue; }
    grep -q '^FALLBACK' "$E.select.txt" && fb="$fb$(grep '^FALLBACK' "$E.select.txt" | head -2 | cut -c10- | tr '\n' ' ')"
    [ -n "$fb" ] && cp "$E.recs" "$E.selected"
    # what the mapping alone picks (the fallback set aside): its misses too
    java -cp "$CP" SelfSelect select "$B/cov" "$T/cov/fingerprints.tsv" "$E.recs" "$E.mapped" "$HERE/tests/selfselect.racy" nofallback > "$E.mapped.txt" 2>&1
    # outcome without the seconds: every column but the last
    join -t$'\t' <(awk -F'\t' '{ o = $2; for (i = 3; i < NF; ++i) o = o " " $i; print $1 "\t" o }' "$B/outcome.tsv" | sort) \
                 <(awk -F'\t' '{ o = $2; for (i = 3; i < NF; ++i) o = o " " $i; print $1 "\t" o }' "$T/outcome.tsv" | sort) |
        awk -F'\t' '$2 != $3 { print $1 "\t" $2 "\t" $3 }' > "$E.changed"
    n=$(wc -l < "$E.recs"); s=$(wc -l < "$E.selected"); c=$(wc -l < "$E.changed")
    x=$(cut -f1 "$E.changed" | sort | comm -23 - <(sort "$E.selected") | wc -l)
    cut -f1 "$E.changed" | sort | comm -23 - <(sort "$E.selected") > "$E.misses"
    s2=$(wc -l < "$E.mapped"); x2=$(cut -f1 "$E.changed" | sort | comm -23 - <(sort "$E.mapped") | wc -l)
    sf=$(awk -F'\t' 'NR == FNR { r[$1] = 1; next } ($1 in r) { s += $NF } END { print s + 0 }' "$E.recs" "$T/outcome.tsv")
    ss=$(awk -F'\t' 'NR == FNR { sel[$1] = 1; next } ($1 in sel) { s += $NF } END { print s + 0 }' "$E.selected" "$T/outcome.tsv")
    ch=$(cut -f1 "$E.changed" | head -6 | tr '\n' ' ')
    printf '%-9s %-9s %-9s %5d %5d %5.1f%% %7d %6d %5d %6d %9d %9d  %s\n' "$m" "$b" "$t" "$n" "$s" "$(awk -v s=$s -v n=$n 'BEGIN { print 100 * s / n }')" "$c" "$x" "$s2" "$x2" "$sf" "$ss" "${fb:+FALLBACK $fb; }$ch"
    ts2=$((ts2 + s2)); tx2=$((tx2 + x2))
    tm=$((tm + n)); ts=$((ts + s)); tc=$((tc + c)); tx=$((tx + x)); tsf=$((tsf + sf)); tss=$((tss + ss))
done
printf '%-29s %5d %5d %5.1f%% %7d %6d %5d %6d %9d %9d\n' "all" "$tm" "$ts" "$(awk -v s=$ts -v n=$tm 'BEGIN { print (n ? 100 * s / n : 0) }')" "$tc" "$tx" "$ts2" "$tx2" "$tsf" "$tss"
