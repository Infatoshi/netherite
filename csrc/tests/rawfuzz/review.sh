#!/usr/bin/env bash
# The frame review before a fuzz session's budget lines are accepted: every
# frame rawjudge.sh measured for NAME (out/native/rawjudge/NAME/s_*.png
# against the goldens in DIR) through pxdiff survey (out/native/pxdiff), one block per
# frame with its clusters and causes; the triptychs are in
# out/native/rawjudge/NAME/review/TICK/ (look at every cluster pxdiff calls
# content, registration or unresolved before accepting).
#   bash csrc/tests/rawfuzz/review.sh [--accept] DIR [DIR...]
# --accept (after the review) appends each session's measured, unbudgeted
# frames from DIR/judge.log to out/java/rawfuzz/budget.txt, as rawjudge.sh
# --accept would.
set -uo pipefail
accept=0
[[ ${1:-} == --accept ]] && { accept=1; shift; }
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
make -s -C csrc "$root/out/native/pxdiff" > /dev/null || { echo "review: pxdiff build failed"; exit 1; }
for dir in "$@"; do
    dir=${dir%/}
    name=$(basename "$dir")
    work=out/native/rawjudge/$name
    if [[ $accept == 1 ]]; then
        sed -n 's/^  measured, not budgeted: //p' "$dir/judge.log" | tee -a out/java/rawfuzz/budget.txt | sed 's/^/accepted: /'
        continue
    fi
    for s in "$work"/s_*.p1000.png; do
        [[ -f $s ]] || continue
        t=$(basename "$s" | sed -E 's/^s_0*([0-9]+)\.p1000\.png/\1/')
        g=$dir/f_$(printf %06d "$t").png
        [[ -f $g ]] || continue
        out=$work/review/$t
        mkdir -p "$out"
        echo "== $name $t"
        out/native/pxdiff survey --a "$g" --b "$s" -o "$out" 2>&1 |
            sed -n '2,$p' | grep -v '^overview\|^  #' | sed 's|  */home/.*/zoom_|  zoom_|'
    done
done
