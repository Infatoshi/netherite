#!/bin/bash
# rawdrive.sh NAME START DRIVE: rawfuzz run.sh's one() with a kept drive instead of rawfuzz_gen's (throwaway re-check)
set -u
name=$1 st=$2 drive=$3 seed=1 cp=
case $st in fresh) ;; fresh42) seed=42;; S*) cp=out/java/checkpoints/1/$st;; G*) cp=out/java/checkpoints/42/$st seed=42;; esac
dir=out/java/rawfuzz/$name; rm -rf "$dir"; mkdir -p "$dir"; cp "$drive" "$dir/drive.jsonl"
(cd oracle && bash tests/rawplay.sh "../$dir/drive.jsonl" "../$dir/tape.jsonl" "$seed" ${cp:+"../$cp"}) > "$dir/play.log" 2>&1
[[ -n $cp ]] && echo "rawfuzz/starts/$st" > "$dir/start"
bash csrc/play/rawjudge.sh "$dir" --every 250 --budgets out/java/rawfuzz/budget.txt > "$dir/judge.log" 2>&1
grep -E '^(PASS|FAIL) rawjudge' "$dir/judge.log" | cut -c1-160; grep -h 'measured, not budgeted' "$dir/judge.log" out/native/rawjudge/$name/*.log 2>/dev/null | sort -u
