#!/usr/bin/env bash
# par: run one command template over several items at once, one line each.
#
#   out/native/par [-j N] [-g REGEX] 'TEMPLATE' ITEM...
#
# Every {} in TEMPLATE becomes the item; each command runs through bash -c
# from the tree's root, J at a time (default 4), and prints in the order
# given:
#   ITEM  rc=0  31.9s  <the last output line, or the last matching REGEX>
# then "par: P/N rc 0"; the exit status is 1 when any failed. Each run's
# whole output is in out/par/PID/ITEM.log (the directory is printed).
# Oracle runs (make -C oracle script, replay, record) reserve their memory
# through csrc/tests/memslot.sh already, so J only bounds the CPU; for
# native replays use out/native/recs. Example, twelve staging variants:
#   out/native/par 'make -s -C oracle script DEV=1 SEED=1 SCRIPT=../out/java/try/ws-{}.jsonl
#     TAPE=../out/java/try/ws-{}/tape.jsonl' 4 5 6 7 8 9 10 11 12 13 14 15
# Lanes wrote such loops one run at a time 65 times by 2026-09-26 (66
# minutes), against 2 parallel ones.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
J=4; G=
while [ $# -gt 0 ]; do
    case $1 in
        -j) J=$2; shift 2;;
        -g) G=$2; shift 2;;
        *) break;;
    esac
done
[ $# -ge 2 ] || { sed -n '3,19p' "$0"; exit 2; }
T=$1; shift
LOG=$ROOT/out/par/$$
mkdir -p "$LOG"
export ROOT T G LOG
for i in $(seq 0 $(($# - 1))); do echo "$i"; done | nice -n 10 xargs -P "$J" -I{} bash -c '
    i=$1; shift; item=${@:i+1:1}
    cmd=${T//\{\}/$item}
    s=$(date +%s%N)
    (cd "$ROOT" && bash -c "$cmd") > "$LOG/$item.log" 2>&1; rc=$?
    e=$(date +%s%N); ds=$(( (e - s) / 100000000 ))
    if [ -n "$G" ]; then last=$(grep -aE -- "$G" "$LOG/$item.log" | tail -1)
    else last=$(grep -av "^[[:space:]]*$" "$LOG/$item.log" | tail -1); fi
    printf "rc=%s\t%s.%ss\t%s\n" "$rc" "$((ds / 10))" "$((ds % 10))" "$(printf %s "$last" | cut -c1-160)" > "$LOG/$item.res"' _ {} "$@"

ok=0; w=0
for a in "$@"; do [ ${#a} -gt $w ] && w=${#a}; done
for a in "$@"; do
    r=$(cat "$LOG/$a.res" 2>/dev/null || printf 'rc=?\t-\tno result')
    printf '%-*s  %s\n' "$w" "$a" "$(printf %s "$r" | tr '\t' ' ')"
    case $r in rc=0*) ok=$((ok + 1));; esac
done
echo "par: $ok/$# rc 0 (logs in ${LOG#$ROOT/})"
[ "$ok" = "$#" ]
