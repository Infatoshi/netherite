#!/bin/bash
# harvest.sh (lane/starts; make -C csrc/runtime starts-harvest): good episodes of a recording into the library of starts
# (train/starts.c has the layout). For each episode I of NREC (ntloop --rec's episodes.nrec):
#   1. ntrec tape: the episode as the oracle's script, its world, its seed and FROM (a library start's checkpoint)
#   2. its rows: the oracle's replay of the script (out/java/try/harv-TAG-I), or a stored one (-r I=ROWS ...)
#   3. ntstarts stages: the tick each new stage was reached (held to a quiet row)
#   4. a stage's checkpoint: the script cut there with a save (ntstarts cut), played by the oracle: LIB/ID/cp
#   5. its start: the oracle's Snapshot of the checkpoint's join (snap1.jsonl, FROM=LIB/ID/cp): LIB/ID/start
#   6. the checkpoint's pre/ snapshot and copied tape dropped (the cut script kept gzipped in cp/), the index made
# An entry already in LIB is kept (nothing redone). Java runs J at a time (out/native/par, the memory budget's).
#
#   harvest.sh -l LIB -n NREC -e "I J ..." [-t TAG] [-r I=ROWS ...] [-j J] [-s STAGES] [-1] [--add-ref ID PATH PROV]
#   harvest.sh -l LIB --stored SCRIPT WORLD SEED ROWS FROM|- --events FILE [-t TAG] [--pack] [--no-index]
#   harvest.sh -l LIB --pack-all | --unpack ID | --unpack-all
# -s: the stages to keep (a comma list of the event names stages prints; default every one). -1: at most one start a
# (seed, stage) (the disk's cap: 9 to 43 MB a start, measured on the kills' library, lane/killlib: the checkpoint's
# save 8 to 29 MB, the start's snapshot 2 to 12; the rows of an episode are deleted once its stages are read).
# --add-ref: an entry that is a stored snapshot by path (the golden chain's, a human session's start), no Java run.
# Prints the index's summary line.
# lane/killlib: --stored takes an episode the oracle already played and checked (its script, world, seed, the Java
# rows, FROM or -) in place of steps 1 and 2; --events adds the late stages (test_snapshots --stage-events of the
# checked C replay: ntstarts stages --events). --pack keeps each checkpoint's save as LIB/ID/cp/save.tar.zst (about half
# the bytes): an ARCHIVE step only. A packed entry does not load: the start reads its checkpoint's save too (every
# chunk the snapshot does not hold comes from cp/save's region files), and since lane/divlib the C engine refuses a
# checkpoint whose cp/save is missing (engine/cpsave.c; before it, it generated those chunks and seed 42's End start
# left the Java client at t 95). --pack-all packs every entry already made; --unpack ID (--unpack-all: every entry)
# restores LIB/ID/cp/save, the archive removed only once the files on disk match its listing (count and bytes).
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
NTS=$ROOT/out/runtime/train/ntstarts; NTR=$ROOT/out/runtime/train/ntrec; PAR=$ROOT/out/native/par
SNAP1=$ROOT/csrc/runtime/train/snap1.jsonl
LIB=; NREC=; EPS=; TAG=e; J=4; KEEP=; CAP=; STORED=; STORED_WORLD=; STORED_SEED=; STORED_FROM=; EVENTS=; INDEX=1; PACK=; declare -A ROWS
pack() {   # LIB/ID: its checkpoint's save into cp/save.tar.zst (checked by listing it back), the directory dropped
  local D=$1
  [ -d "$D/cp/save" ] || return 0
  tar -C "$D/cp" -cf - save | zstd -q -10 -T2 -f -o "$D/cp/save.tar.zst.tmp" &&
    zstd -dcq "$D/cp/save.tar.zst.tmp" | tar -tf - > /dev/null &&
    mv "$D/cp/save.tar.zst.tmp" "$D/cp/save.tar.zst" && rm -rf "$D/cp/save" || { rm -f "$D/cp/save.tar.zst.tmp"; return 1; }
}
unpack() { # LIB/ID: cp/save.tar.zst back to cp/save (lane/divlib): unpacked beside it, held to the archive's listing (the
  # regular files' count and bytes), moved into place, then the archive removed. One line: unpack ID files bytes
  local D=$1 U="$1/cp/.unpack" n0 b0 n1 b1
  [ -d "$D/cp/save" ] && [ ! -e "$D/cp/save.tar.zst" ] && return 0
  [ -d "$D/cp/save" ] && { echo "unpack $D: both cp/save and cp/save.tar.zst (left as they are)"; return 1; }
  [ -s "$D/cp/save.tar.zst" ] || { echo "unpack $D: cp has no save.tar.zst"; return 1; }
  read -r n0 b0 < <(zstd -dcq "$D/cp/save.tar.zst" | tar -tvf - | awk '$1 ~ /^-/ {n++; b += $3} END {print n + 0, b + 0}')
  rm -rf "$U" && mkdir -p "$U" && zstd -dcq "$D/cp/save.tar.zst" | tar -C "$U" -xf - ||
    { rm -rf "$U"; echo "unpack $D: extract failed"; return 1; }
  read -r n1 b1 < <(find "$U/save" -type f -printf '%s\n' | awk '{n++; b += $1} END {print n + 0, b + 0}')
  [ "$n0" -gt 0 ] && [ "$n0" = "$n1" ] && [ "$b0" = "$b1" ] ||
    { rm -rf "$U"; echo "unpack $D: listing $n0 files $b0 bytes, unpacked $n1 files $b1 bytes"; return 1; }
  mv "$U/save" "$D/cp/save" && rmdir "$U" && rm -f "$D/cp/save.tar.zst" || { echo "unpack $D: move failed"; return 1; }
  echo "unpack $(basename "$D") $n1 $b1"
}
while [ $# -gt 0 ]; do
  case $1 in
    -l) LIB=$(realpath -m "$2"); shift 2 ;;
    -n) NREC=$(realpath "$2"); shift 2 ;;
    -e) EPS=$2; shift 2 ;;
    -t) TAG=$2; shift 2 ;;
    -j) J=$2; shift 2 ;;
    -s) KEEP=",$2,"; shift 2 ;;
    --events) EVENTS=$(realpath "$2"); shift 2 ;;
    --no-index) INDEX=; shift ;;
    --pack) PACK=1; shift ;;
    --pack-all) [ -n "$LIB" ] || { echo "harvest.sh: -l LIB first"; exit 2; }
      bad=0; for d in "$LIB"/*/; do [ -s "$d/start/manifest.json" ] && { pack "${d%/}" || { echo "pack ${d%/}: failed"; bad=1; }; }; done
      exit $bad ;;
    --unpack) [ -n "$LIB" ] || { echo "harvest.sh: -l LIB first"; exit 2; }
      unpack "$LIB/$2"; exit ;;
    --unpack-all) [ -n "$LIB" ] || { echo "harvest.sh: -l LIB first"; exit 2; }
      bad=0; for d in "$LIB"/*/; do [ -e "${d}cp/save.tar.zst" ] && { unpack "${d%/}" || bad=1; }; done
      exit $bad ;;
    -1) CAP=1; shift ;;
    -r) ROWS[${2%%=*}]=$(realpath "${2#*=}"); shift 2 ;;
    --stored) STORED=$(realpath "$2"); STORED_WORLD=$(realpath "$3"); STORED_SEED=$4
      ROWS[0]=$(realpath "$5"); STORED_FROM=$6; [ "$STORED_FROM" = - ] || STORED_FROM=$(realpath "$STORED_FROM")
      EPS=0; shift 6 ;;
    --add-ref) mkdir -p "$LIB/$2"; realpath "$3" > "$LIB/$2/ref"; echo "$4" > "$LIB/$2/prov"; shift 4 ;;
    *) echo "harvest.sh: $1?"; exit 2 ;;
  esac
done
[ -n "$LIB" ] || { echo "usage: harvest.sh -l LIB [-n NREC -e \"I ...\" | --stored SCRIPT WORLD SEED ROWS FROM] [-t TAG] [-r I=ROWS] [-j J] [-s STAGES]"; exit 2; }
mkdir -p "$LIB"; W=$LIB/.work; mkdir -p "$W"
fail=0
cuts=(); rows=()
# 1, 2: the scripts, then every episode's rows at once (J Java runs at a time)
p1() {
  local E=$W/$1; local SEED FROM   # the episode's tag (par names its log by the item)
  { read -r SEED; read -r FROM; } < "$E/args"
  make -s -C "$ROOT/oracle" script SEED="$SEED" CONF="$E/world.yaml" SCRIPT="$E/script.jsonl" \
    TAPE="../out/java/try/harv-$(basename "$E")/tape.jsonl" ${FROM:+FROM=$FROM} > "$E/rows.log" 2>&1 \
    || { echo "FAIL $(tail -1 "$E/rows.log")"; return 1; }
  touch "$E/rows.ok"; echo "OK rows"
}
for I in $EPS; do
  E=$W/$TAG$I; mkdir -p "$E"
  if [ ! -s "$E/replay.txt" ]; then
    if [ -n "$STORED" ]; then
      ln -sfn "$STORED" "$E/script.jsonl"; ln -sfn "$STORED_WORLD" "$E/world.yaml"
      printf 'replay: SEED=%s FROM=%s\n' "$STORED_SEED" "${STORED_FROM/#-/}" > "$E/replay.txt"
    else
      "$NTR" tape "$NREC" "$I" -o "$E/script.jsonl" --world "$E/world.yaml" > "$E/tape.log" 2>&1 || { echo "$TAG$I: ntrec tape: $(tail -1 "$E/tape.log")"; fail=1; continue; }
      grep '^replay:' "$E/tape.log" > "$E/replay.txt"
    fi
  fi
  SEED=$(sed -n 's/.*SEED=\([0-9-]*\).*/\1/p' "$E/replay.txt")
  FROM=$(sed -n 's/.* FROM=\([^ ]*\).*/\1/p' "$E/replay.txt")
  printf '%s\n' "$SEED" "$FROM" > "$E/args"
  [ -z "${ROWS[$I]:-}" ] && [ ! -f "$E/rows.ok" ] && rows+=("$TAG$I")
done
if [ ${#rows[@]} -gt 0 ]; then
  export -f p1; export ROOT W
  "$PAR" -j "$J" 'p1 {}' "${rows[@]}" || fail=1
fi
# 3: the stages; at most one start a (seed, stage) in the library (-1 none: the disk's cap)
for I in $EPS; do
  E=$W/$TAG$I
  [ -f "$E/args" ] || continue
  { read -r SEED; read -r FROM; } < "$E/args"
  R=${ROWS[$I]:-$ROOT/out/java/try/harv-$TAG$I/tape.jsonl}
  [ -n "${ROWS[$I]:-}" ] || [ -f "$E/rows.ok" ] || continue
  if [ -n "$EVENTS" ]; then
    "$NTS" stages "$R" --events "$EVENTS" > "$E/stages.tsv" || { echo "$TAG$I: ntstarts stages failed"; fail=1; continue; }
  elif [ ! -s "$E/stages.tsv" ]; then
    "$NTS" stages "$R" > "$E/stages.tsv" || { echo "$TAG$I: ntstarts stages failed"; fail=1; continue; }
  fi
  [ -n "${ROWS[$I]:-}" ] || rm -rf "$ROOT/out/java/try/harv-$TAG$I"   # the rows (about 50 MB a 30-minute episode)
  while IFS=$'\t' read -r T ST MS DIM ITEMS T0; do
    [ -z "$KEEP" ] || [[ $KEEP == *",$ST,"* ]] || continue
    ID=s$SEED-$TAG$I-$ST
    [ -s "$LIB/$ID/start/manifest.json" ] && continue
    if [ -n "$CAP" ] && compgen -G "$LIB/s$SEED-*-$ST/prov" > /dev/null; then continue; fi
    rm -rf "${LIB:?}/$ID"; mkdir -p "$LIB/$ID"
    printf '%s\n' "$ST" > "$LIB/$ID/stage"
    "$NTS" cut "$E/script.jsonl" "$T" "$LIB/$ID/cut.jsonl" "$LIB/$ID/cp" || { fail=1; continue; }
    if [ -n "$STORED" ]; then
      echo "harvested $TAG@$T ($ST, reached at $T0) from stored Java rows" > "$LIB/$ID/prov"
    else
      echo "harvested $(basename "$NREC"):$I@$T ($ST, reached at $T0)" > "$LIB/$ID/prov"
    fi
    printf '%s\n' "$SEED" "$E/world.yaml" "$FROM" > "$LIB/$ID/args"
    cuts+=("$ID")
  done < "$E/stages.tsv"
done
# a job: the save run (4), then the start (5) and the prune (6)
job() {
  local ID=$1 D=$LIB/$1; local SEED CONF FROM
  { read -r SEED; read -r CONF; read -r FROM; } < "$D/args"
  make -s -C "$ROOT/oracle" script SEED="$SEED" CONF="$CONF" SCRIPT="$D/cut.jsonl" CACHE=0 \
    TAPE="../out/java/try/lib-$ID/tape.jsonl" ${FROM:+FROM=$FROM} > "$D/cp.log" 2>&1 || { echo "FAIL cp $(tail -1 "$D/cp.log")"; return 1; }
  rm -rf "$ROOT/out/java/try/lib-$ID"
  # (lane/statebank) the tape named by the entry: the oracle names a run's game directory after the tape's folder and
  # file (oracle/Makefile runname), so LIB/ID/start/tape.jsonl gave every job the one game directory "start-tape" and
  # two jobs at once (-j over 1, or two harvests) wrote each other's save: a start with another world's seed
  rm -rf "$D/start"; mkdir -p "$D/start"
  make -s -C "$ROOT/oracle" script SEED="$SEED" CONF="$CONF" FROM="$D/cp" SCRIPT="$SNAP1" CACHE=0 \
    TAPE="$D/start/$ID.jsonl" > "$D/start.log" 2>&1 || { echo "FAIL start $(tail -1 "$D/start.log")"; return 1; }
  mv "$D/start/$ID.jsonl" "$D/start/tape.jsonl" || { echo "FAIL start: no tape"; return 1; }
  rm -rf "$D/cp/pre" "$D/cp/tape.jsonl"; gzip -f "$D/cut.jsonl"; mv "$D/cut.jsonl.gz" "$D/cp/script.jsonl.gz"
  [ -z "$PACK" ] || pack "$D" || { echo "FAIL pack $ID"; return 1; }
  echo "OK $ID $(du -sb "$D" | cut -f1) bytes"
}
if [ ${#cuts[@]} -gt 0 ]; then
  export -f job pack; export LIB ROOT SNAP1 PACK
  "$PAR" -j "$J" 'job {}' "${cuts[@]}" || fail=1
fi
[ -z "$INDEX" ] || "$NTS" index "$LIB" || fail=1
exit $fail
