#!/usr/bin/env bash
# lane/macromod: the macros' digest matrix (macrodig.tsv's header). From the tree's root:
#   bash csrc/runtime/macrodig.sh [-j J] [-p] [NAME...]
# runs each row (NAME: only those; J at a time, default 2) through out/runtime/vec_bench and prints one line a row,
# NAME: PASS or FAIL with the digests that differ, then MACRO-DIGESTS PASS or FAIL. -p prints each row as a tsv line
# with the tree's digests instead (the pin: the reason column to fill). A pinned row with no digests yet ("-") prints
# its line as with -p and fails. Logs in out/runtime/macrodig/NAME.log.
set -u
J=2 PIN=0
while getopts "j:p" o; do case $o in j) J=$OPTARG ;; p) PIN=1 ;; *) exit 2 ;; esac; done
shift $((OPTIND - 1))
TSV=csrc/runtime/macrodig.tsv
OUT=out/runtime/macrodig
BIN=out/runtime/vec_bench
mkdir -p "$OUT"
[ -x "$BIN" ] || { echo "macrodig: $BIN wanted (make -C csrc/runtime vec_bench)"; exit 1; }
# sh-all and sh-opts start from out/java/try/s1sh, which the canonical tree keeps with its tape as s1sh.jsonl (the pool
# reads START/tape.jsonl: "no tape header", 2 of 22 rows in every fresh lane). A lane's out/java/try/s1sh is a link to
# the canonical directory (lane-share), which is read only: make it a directory of links in this tree's own out/, with
# tape.jsonl linking the same tape (the digests are the pinned ones: the same bytes). A canonical tree (a real
# directory) is left as it is.
S=out/java/try/s1sh
if [ -L "$S" ] && [ ! -e "$S/tape.jsonl" ] && [ -f "$S/s1sh.jsonl" ]; then
    T=$(readlink -f "$S"); rm -f "$S"; mkdir "$S"
    for f in "$T"/*; do ln -s "$f" "$S/"; done
    ln -s "$T/s1sh.jsonl" "$S/tape.jsonl"
fi
COMMON="--ns 32 --groups 2 --threads 8 --warm 0 --policy 1 --seed 1 --digest --label-digest"
names=()
while IFS=$'\t' read -r name cfg args rest; do
    [ -z "$name" ] || [ "${name:0:1}" = "#" ] && continue
    if [ $# -gt 0 ]; then
        keep=0
        for w in "$@"; do [ "$w" = "$name" ] && keep=1; done
        [ $keep = 1 ] || continue
    fi
    names+=("$name")
    (CUDA_VISIBLE_DEVICES= $BIN --config "$cfg" $args $COMMON > "$OUT/$name.log" 2>&1; echo $? > "$OUT/$name.rc") &
    while [ "$(jobs -rp | wc -l)" -ge "$J" ]; do wait -n; done
done < "$TSV"
wait
fail=0
for name in "${names[@]}"; do
    line=$(awk -F'\t' -v n="$name" '$1 == n' "$TSV")
    cfg=$(cut -f2 <<< "$line"); args=$(cut -f3 <<< "$line")
    want="$(cut -f4-7 <<< "$line")"
    L="$OUT/$name.log"
    got="$(awk '/^trajectory digest/{t=$3} /^label digest/{l=$3} /^episode digest/{e=$3} /^books digest/{b=$3}
               END {printf "%s\t%s\t%s\t%s", t, l, e, b}' "$L")"
    rate=$(grep -o '^N [0-9]*, [0-9]* groups: [0-9]* env-steps/s' "$L" | sed 's/.*: //')
    if [ "$(cat "$OUT/$name.rc")" != 0 ] || [ "$got" = $'\t\t\t' ]; then
        echo "$name: FAIL (vec_bench: $(tail -1 "$L"))"; fail=1; continue
    fi
    if [ $PIN = 1 ] || [ "$want" = $'-\t-\t-\t-' ]; then
        printf '%s\t%s\t%s\t%s\t%s\n' "$name" "$cfg" "$args" "$got" "REASON"
        [ $PIN = 1 ] || fail=1
        continue
    fi
    if [ "$got" = "$want" ]; then echo "$name: PASS ($rate)"
    else
        d=""
        for k in 1 2 3 4; do
            g=$(cut -f$k <<< "$got"); w=$(cut -f$k <<< "$want")
            [ "$g" = "$w" ] || d="$d $(sed -n ${k}p <<< $'trajectory\nlabel\nepisode\nbooks') $w -> $g;"
        done
        echo "$name: FAIL$d"; fail=1
    fi
done
[ $PIN = 1 ] && exit 0
[ $fail = 0 ] && echo "MACRO-DIGESTS PASS" || { echo "MACRO-DIGESTS FAIL"; exit 1; }
