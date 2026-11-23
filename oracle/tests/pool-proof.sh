#!/usr/bin/env bash
# The warm pool's byte-identity proof. Re-records recordings from their
# scripts, or replays their tapes, either in fresh JVMs or through this tree's
# pool (make pool-start) or the machine-wide shared pool (tests/poolauto.sh),
# and compares the results. Run from oracle/.
#
#   bash tests/pool-proof.sh record fresh|pool|shared ROOT NAME...   re-record NAME into ROOT/NAME/tape.jsonl
#   bash tests/pool-proof.sh replay fresh|pool|shared ROOT NAME...   replay ../out/java/snapshots/NAME/tape.jsonl
#                                                     (rt-NAME, hs-NAME, rs-NAME: rendertick, hudscreens, render)
#   bash tests/pool-proof.sh render ROOT [NAME...]           the render recordings' frames, fresh against pooled
#   bash tests/pool-proof.sh saveend fresh|pool|shared ROOT TAPE...  replay TAPE with SAVEEND (a played session's check)
#   bash tests/pool-proof.sh compare ROOT NAME...             ROOT's recording against the recorded one, every file
#   bash tests/pool-proof.sh same ROOT_A ROOT_B NAME...       two roots' recordings (every file) and replay verdicts
#
# A render recording's replay (rt-, hs-, rs-: out/java/rendertick, hudscreens
# and render, named as coverage.sh and selfcheck.sh name them) writes every
# frame its tape carries a digest for into ROOT/NAME/frames, and same
# compares those PNGs byte for byte (frames:same(N)). render ROOT replays
# them all but the two that enter the End (te_portal_end, te_portal_sh: the
# End's entities after a dimension change vary, ledger te_portal_end-rec),
# or the NAMEs given, in fresh JVMs into ROOT/fresh, then one after
# another on one member of this tree's pool into ROOT/pool, air scenes
# before underwater ones and the fog, sky and GUI scenes interleaved (the
# order that showed lane/poolleak's fog leak), and runs same on the two:
# the verdicts, the replay rows and every frame must match. It stops and
# starts this tree's members (make pool-stop, pool-start POOLN=1, and a
# POOLTAG=mip- member with the options of the first scene that has mipmaps,
# which a client reads at start); POOLARGS in
# the environment goes to the member: POOLARGS="--pool-skip-reset gl" is the
# negative control (the member keeps each job's GL state), which must FAIL.
#
# compare and same exit 1 when any recording differs. A snapshot recording is
# compared file by file, sub-snapshots (end/) and the save overlay (save/)
# included: tape.jsonl by its rows and header (hdr), manifest.json without its
# harness hash, every other file byte for byte; a file on one side only
# differs. The run's logs (log, *.log, log.txt), replay outputs (replay.jsonl,
# tape.replay.jsonl) and traces (oracle.trace) are not the recording's. Two
# roots re-recording one NAME at once share a game directory (the Makefile's
# runname is the tape's folder and file name): record the roots one after
# the other.
#
# saveend replays each TAPE (any tape path; NAME is its file name without
# .jsonl) as csrc/play/session.sh checks a session: SAVEEND=ROOT/NAME/cp,
# SAVEPRE=0 (SAVEPRE=1 in the environment keeps the pre/ Snapshot). Then the
# same tape again without SAVEEND into ROOT/NAME.next, a second later, so in
# a pool it is the member's next job and takes the world the member
# prelaunched from the saving run's arguments: it must replay as a fresh JVM
# does and write no checkpoint (a line "NAME.next: LEAK" otherwise). same
# ROOT_A ROOT_B NAME NAME.next compares them, the checkpoint file by file.
# Every root's run of NAME has one run name (the tape goes to
# ROOT/../saveend/NAME.jsonl, the run to out/java/run/saveend-NAME.replay,
# whose path the replay's header records): run the roots one after the other.
#
# NAME is a snapshot under ../out/java/snapshots (its script is
# tests/NAME.jsonl, or the name without its -sN suffix; seed, DEV, --rd and
# the checkpoint start come from the recorded tape's header), or a -pre
# script (tests/NAME.jsonl) that saves a checkpoint. Saves go under
# ROOT/root/out/java/checkpoints (a copy of config.yaml sits at ROOT/root), and
# a checkpoint start takes ROOT's own checkpoint when this proof made one, so
# the golden chain runs G1 -> G2 -> ... inside the root. fresh runs JOBS at
# once (POOL=0); pool runs one at a time, in the order given.
# Pool runs never go through the oracle result cache (CACHE=0: they must
# run in the pool); fresh runs do unless CACHE=0 is set (in the environment,
# or make rerecord CACHE=0), which a proof against fresh JVMs needs.
# MAKEX adds make arguments to every run: MAKEX="CLASSES=DIR -o DIR/.stamp" runs
# a frozen copy of the classes while the tree is rebuilt.
# shared runs go only to the shared pool (POOLAUTO, never the tree's own
# members; make pool-warm first, or the first run is fresh), one at a time;
# ALT=DIR (another checkout on this harness, its out/java set up) makes every
# second of them run from that tree: the cross-tree case, one member taking
# runs from two trees alternately.
set -uo pipefail
cd "$(dirname "$0")/.."
SNAP=../out/java/snapshots
CK=../out/java/checkpoints
cmd=$1; shift

# a replay log's verdict: its divergence, else its REPLAY OK (a divergence in the last rows is
# found when the tape closes, after the REPLAY OK line)
verdict() { local v; v=$(grep -ao "ORACLE DIVERGE.*" "$1" 2>/dev/null | head -1); [ -n "$v" ] || v=$(grep -ao "ORACLE REPLAY OK ticks=[0-9]*" "$1" 2>/dev/null | head -1); echo "$v"; }

# the make arguments of a mode, and the oracle/ directory a run starts make in
modeargs() { case $1 in fresh) echo POOL=0 ;; pool) echo POOL=1 CACHE=0 POOLAUTO=0 ;; shared) echo POOL=1 CACHE=0 POOLAUTO=1 POOLDIR=/nonexistent ;; esac; }
nth=0
tree_of() { if [ "$1" = shared ] && [ -n "${ALT:-}" ] && [ $((nth % 2)) = 1 ]; then echo "$ALT/oracle"; else echo .; fi; }

script_of() {
  local n=$1
  # a render recording keeps its script beside its tape; its frame and probe paths are moved into ROOT
  if [[ $n == rt-* ]]; then echo "../out/java/rendertick/${n#rt-}/script.jsonl"; return; fi
  if [[ $n == hs-* ]]; then echo "tests/${n#hs-}.jsonl"; return; fi
  for s in tests/$n.jsonl "tests/${n%-s[0-9]*}.jsonl"; do [ -f "$s" ] && { echo "$s"; return; }; done
}

# one re-record: MODE ROOT NAME
record_one() {
  local mode=$1 root=$2 n=$3 s seed dev rd from="" ck args=()
  s=$(script_of "$n")
  [ -n "$s" ] || { echo "$n: no script"; return; }
  if [[ $n == *-pre ]] && [ ! -f "$SNAP/$n/tape.jsonl" ]; then
    local save; save=$(grep -o '"cmd":"save","name":"[^"]*"' "$s" | sed 's/.*"name":"//; s/"$//')
    local m; m=$(ls -d $CK/*/"$save" 2>/dev/null | head -1)
    seed=$(jq -r .seed "$m/manifest.json"); dev=$(jq -r '.dev // false' "$m/manifest.json"); rd=4
  elif [[ $n == rt-* || $n == hs-* ]]; then
    local x=${n#??-} area=rendertick h
    [[ $n == hs-* ]] && area=hudscreens
    local src=../out/java/$area/$x
    mkdir -p "$root/$n/frames"
    local from_script=$s
    s=$root/$n/script.jsonl
    sed "s#/[^\"]*/out/java/$area/$x#$(realpath "$root")/$n/frames#g; s#/[^\"]*/out/java/render-src/$x#$(realpath "$root")/$n/frames#g" "$from_script" > "$s"
    h=$(head -1 "$src/tape.jsonl")
    seed=$(jq -r .seed <<<"$h"); dev=$(jq -r '.dev // false' <<<"$h"); rd=$(jq -r .options.rd <<<"$h")
  else
    local h; h=$(head -1 "$SNAP/$n/tape.jsonl")
    seed=$(jq -r .seed <<<"$h"); dev=$(jq -r '.dev // false' <<<"$h"); rd=$(jq -r .options.rd <<<"$h")
    if [ "$(jq -r '.start.kind // ""' <<<"$h")" = checkpoint ]; then
      ck=$(jq -r '.start.dir' <<<"$h"); ck=${ck##*/}
      from=$CK/$seed/$ck
      [ -f "$root/root/out/java/checkpoints/$seed/$ck/manifest.json" ] && from=$root/root/out/java/checkpoints/$seed/$ck
    fi
  fi
  args=(SEED="$seed" SCRIPT="$s" TAPE="$root/$n/tape.jsonl" CONF="$root/root/config.yaml")
  [ "$dev" = true ] && args+=(DEV=1)
  local xa=""
  # a scene recorded with its chat or toasts drawn says so in its header
  if [[ $n == hs-* ]]; then
    [ "$(jq -r '.chat // false' <<<"$(head -1 "$(ref "$n")")")" = true ] && xa="$xa --chat"
    [ "$(jq -r '.toasts // false' <<<"$(head -1 "$(ref "$n")")")" = true ] && xa="$xa --toasts"
  fi
  [ "$rd" != 4 ] && xa="$xa --rd $rd"
  # a tape recorded with --detail carries per-entity rows ("x")
  [ -f "$SNAP/$n/tape.jsonl" ] && sed -n 3p "$SNAP/$n/tape.jsonl" | grep -q '"x":{' && xa="$xa --detail"
  [ -n "$xa" ] && args+=(ARGS="${xa# }")
  [ -n "$from" ] && args+=(FROM="$from")
  mkdir -p "$root/$n"
  local t0=$EPOCHREALTIME tj; tj=$(tree_of "$mode"); nth=$((nth + 1))
  make -s -C "$tj" script ${MAKEX:-} $(modeargs "$mode") "${args[@]}" > "$root/$n/log" 2>&1
  local rc=$?
  [ "$tj" = . ] || echo "$n: run from $tj" >> "$root/times.txt"
  [ "$mode" != fresh ] && ! grep "^pool: member " "$root/$n/log" | tail -n 1 | grep -q "takes the run" && echo "$n: NOT RUN BY THE POOL" | tee -a "$root/times.txt"
  printf '%s rc=%s %.2fs %s\n' "$n" "$rc" "$(bc <<<"$EPOCHREALTIME - $t0")" "$(grep -ac '^' "$root/$n/tape.jsonl" 2>/dev/null) rows" | tee -a "$root/times.txt"
}

replay_one() {
  local mode=$1 root=$2 n=$3
  mkdir -p "$root/$n"
  local t0=$EPOCHREALTIME
  # a copy named after the root: its replay runs in its own out/java/run directory
  local ref="$root/$n/$(basename "$root")-$n.jsonl"
  cp "$(ref "$n")" "$ref"
  local tj; tj=$(tree_of "$mode"); nth=$((nth + 1))
  # SNAPAT=T,T,... (and RARGS, the run's ARGS): the any-tick snapshots, or diverge's replay, into ROOT/NAME/snaps
  local xa=()
  [ -n "${SNAPAT:-}" ] && xa+=(SNAPAT="$SNAPAT" SNAPDIR="$root/$n/snaps")
  [ -n "${RARGS:-}" ] && xa+=(ARGS="$RARGS")
  # a render recording's frames, for same to compare byte for byte
  case $n in rt-*|hs-*|rs-*) rm -rf "$root/$n/frames"; xa+=(FRAMES="$root/$n/frames") ;; esac
  make -s -C "$tj" replay ${MAKEX:-} $(modeargs "$mode") REF="$ref" "${xa[@]}" > "$root/$n/replay.log" 2>&1
  local rc=$?
  [ "$tj" = . ] || echo "$n: replayed from $tj" >> "$root/replays.txt"
  cp "$tj/../out/java/run/$n-$(basename "$ref" .jsonl).replay/replay.jsonl" "$root/$n/replay.jsonl" 2>/dev/null
  rm -f "$ref"
  [ "$mode" != fresh ] && ! grep "^pool: member " "$root/$n/replay.log" | tail -n 1 | grep -q "takes the run" && echo "$n: NOT RUN BY THE POOL" | tee -a "$root/replays.txt"
  local v; v=$(verdict "$root/$n/replay.log")
  printf '%s rc=%s %.2fs %s\n' "$n" "$rc" "$(bc <<<"$EPOCHREALTIME - $t0")" "${v:-no verdict}" | tee -a "$root/replays.txt"
}

# one saving replay and the plain replay after it: MODE ROOT TAPE
saveend_one() {
  local mode=$1 root=$2 src=$3 n x d ref t0 tj rc v
  n=$(basename "$src" .jsonl)
  tj=$(tree_of "$mode"); nth=$((nth + 1))
  for x in "$n" "$n.next"; do
    d=$root/$x
    rm -rf "$d"; mkdir -p "$d"
    # one run name in every root (its directory names the checkpoint start in the replay's header)
    ref="$(dirname "$root")/saveend/$x.jsonl"
    mkdir -p "${ref%/*}"; cp "$src" "$ref"
    t0=$EPOCHREALTIME
    if [ "$x" = "$n" ]; then
      make -s -C "$tj" replay ${MAKEX:-} $(modeargs "$mode") REF="$ref" SAVEEND="$d/cp" SAVEPRE="${SAVEPRE:-0}" > "$d/replay.log" 2>&1
    else
      sleep 1 # past Pool.GRACE_MS: the member prelaunches the saving run's world for this one
      make -s -C "$tj" replay ${MAKEX:-} $(modeargs "$mode") REF="$ref" > "$d/replay.log" 2>&1
    fi
    rc=$?
    cp "$tj/../out/java/run/saveend-$x.replay/replay.jsonl" "$d/replay.jsonl" 2>/dev/null
    [ "$mode" != fresh ] && ! grep "^pool: member " "$d/replay.log" | tail -n 1 | grep -q "takes the run" && echo "$x: NOT RUN BY THE POOL" | tee -a "$root/replays.txt"
    [ "$x" != "$n" ] && { [ -e "$d/cp" ] || grep -aq '^ORACLE CHECKPOINT' "$d/replay.log"; } && echo "$x: LEAK (a checkpoint from a run without SAVEEND)" | tee -a "$root/replays.txt"
    v=$(verdict "$d/replay.log")
    [ "$x" = "$n" ] && v="$v $(grep -ao '^ORACLE CHECKPOINT .*' "$d/replay.log" | sed 's#^ORACLE CHECKPOINT [^ ]* #checkpoint #')"
    printf '%s rc=%s %.2fs %s\n' "$x" "$rc" "$(bc <<<"$EPOCHREALTIME - $t0")" "${v:-no verdict}" | tee -a "$root/replays.txt"
  done
  # only now: a member's prelaunch parses the last run's arguments, its tape included
  rm -f "$(dirname "$root")/saveend/$n.jsonl" "$(dirname "$root")/saveend/$n.next.jsonl"
}

# rows = every line but the header (a render recording's without their frame paths);
# the header without its harness hash and checkpoint location
rows() { case $1 in *rt-*|*/rendertick/*|*hs-*|*/hudscreens/*|*rs-*|*/render/*) tail -n +2 "$1" | jq -c 'del(.frame)' ;; *) tail -n +2 "$1" ;; esac; }
ref() { case $1 in rt-*) echo "../out/java/rendertick/${1#rt-}/tape.jsonl" ;; hs-*) echo "../out/java/hudscreens/${1#hs-}/tape.jsonl" ;; rs-*) echo "../out/java/render/${1#rs-}/tape.jsonl" ;; *) echo "$SNAP/$1/tape.jsonl" ;; esac; }
# two roots' frames of one recording: every PNG either wrote, byte for byte
framesdiff() {
  local a=$1/frames b=$2/frames f n=0 d=0
  [ -d "$a" ] || [ -d "$b" ] || return
  for f in $( { ls "$a" 2>/dev/null; ls "$b" 2>/dev/null; } | grep '\.png$' | sort -u); do
    n=$((n + 1))
    cmp -s "$a/$f" "$b/$f" || d=$((d + 1))
  done
  [ $d = 0 ] && echo "frames:same($n)" || echo "frames:DIFFER($d of $n)"
}
hdr() { head -1 "$1" | jq -cS 'del(.harness) | if .start.dir then .start |= del(.dir, .manifestSha256) else . end'; }
# a snapshot recording's files, relative to its directory
snapfiles() { [ -d "$1" ] && (cd "$1" && find -L . -type f ! -name log ! -name '*.log' ! -name log.txt ! -name replay.jsonl ! -name tape.replay.jsonl ! -name oracle.trace -printf '%P\n'); }
# each file that differs between two recordings A and B, one per line
filesdiff() {
  local a=$1 b=$2 f
  sort -u <(snapfiles "$a") <(snapfiles "$b") | while IFS= read -r f; do
    if [ ! -f "$a/$f" ] || [ ! -f "$b/$f" ]; then echo "$f"; continue; fi
    case $f in
      tape.jsonl|*/tape.jsonl) cmp -s <(rows "$a/$f") <(rows "$b/$f") && [ "$(hdr "$a/$f")" = "$(hdr "$b/$f")" ] || echo "$f" ;;
      manifest.json|*/manifest.json) cmp -s <(jq -cS 'del(.harness)' "$a/$f") <(jq -cS 'del(.harness)' "$b/$f") || echo "$f" ;;
      *) cmp -s "$a/$f" "$b/$f" || echo "$f" ;;
    esac
  done
}
files() { local d; d=$(filesdiff "$1" "$2" | tr '\n' ' '); [ -z "$d" ] && echo files-same || echo "FILES-DIFFER: ${d% }"; }

case $cmd in
  record|replay)
    mode=$1 root=$(realpath -m "$2"); shift 2
    mkdir -p "$root/root"
    cp ../config.yaml "$root/root/config.yaml"
    if [ "$mode" = fresh ] && [ "$cmd" = record ]; then
      # dependents wait for their checkpoint: -pre scripts and the golden chain run first, in order
      for n in "$@"; do case $n in *-pre|gold-g*) record_one fresh "$root" "$n" ;; esac; done
      for n in "$@"; do case $n in *-pre|gold-g*) ;; *) echo "$n" ;; esac; done |
        xargs -P "${JOBS:-6}" -I{} bash -c "$(declare -f script_of record_one ref modeargs tree_of); nth=0; SNAP=$SNAP CK=$CK; record_one fresh '$root' {}"
    elif [ "$mode" = fresh ]; then
      printf '%s\n' "$@" | xargs -P "${JOBS:-6}" -I{} bash -c "$(declare -f replay_one ref modeargs tree_of verdict); nth=0; SNAP=$SNAP; replay_one fresh '$root' {}"
    else
      for n in "$@"; do "${cmd}_one" "$mode" "$root" "$n"; done
    fi ;;
  saveend)
    mode=$1 root=$(realpath -m "$2"); shift 2
    mkdir -p "$root"
    tapes=(); for t in "$@"; do tapes+=("$(realpath "$t")"); done
    if [ "$mode" = fresh ]; then
      printf '%s\n' "${tapes[@]}" | xargs -P "${JOBS:-6}" -I{} bash -c "$(declare -f saveend_one modeargs tree_of verdict); nth=0; saveend_one fresh '$root' {}"
    else
      for t in "${tapes[@]}"; do saveend_one "$mode" "$root" "$t"; done
    fi
    ! grep -q 'LEAK\|NOT RUN BY THE POOL\| rc=[1-9]' "$root/replays.txt" 2>/dev/null ;;
  compare)
    root=$1; shift
    for n in "$@"; do
      a=$(ref "$n") b=$root/$n/tape.jsonl
      [ -f "$a" ] || { echo "$n: no recorded tape"; rc=1; continue; }
      [ -f "$b" ] || { echo "$n: MISSING"; rc=1; continue; }
      if cmp -s <(rows "$a") <(rows "$b"); then r=rows-same; else r="ROWS-DIFFER at line $(cmp <(rows "$a") <(rows "$b") | awk '{print $NF}')"; fi
      if [ "$(hdr "$a")" = "$(hdr "$b")" ]; then h=header-same; else h=HEADER-DIFFERS; fi
      fl=""
      case $n in rt-*|hs-*) ;; *) fl=" $(files "$(dirname "$a")" "$root/$n")" ;; esac
      echo "$n $r $h$fl"
      case "$r $h$fl" in *DIFFER*) rc=1 ;; esac
    done
    exit ${rc:-0} ;;
  same)
    ra=$1 rb=$2; shift 2
    for n in "$@"; do
      out="$n"
      for f in tape.jsonl replay.jsonl; do
        [ -f "$ra/$n/$f" ] || [ -f "$rb/$n/$f" ] || continue
        if cmp -s <(rows "$ra/$n/$f" 2>/dev/null) <(rows "$rb/$n/$f" 2>/dev/null) && [ "$(hdr "$ra/$n/$f" 2>/dev/null)" = "$(hdr "$rb/$n/$f" 2>/dev/null)" ]; then out+=" $f:same"; else out+=" $f:DIFFER"; rc=1; fi
      done
      case $n in
        rt-*|hs-*|rs-*) fl=$(framesdiff "$ra/$n" "$rb/$n"); [ -n "$fl" ] && { out+=" $fl"; case $fl in *DIFFER*) rc=1 ;; esac; } ;;
        *) fl=$(files "$ra/$n" "$rb/$n"); out+=" $fl"; [ "$fl" = files-same ] || rc=1 ;;
      esac
      va=$(verdict "$ra/$n/replay.log")
      vb=$(verdict "$rb/$n/replay.log")
      [ -n "$va$vb" ] && { [ "$va" = "$vb" ] && out+=" verdict:same" || { out+=" verdict:DIFFER($va | $vb)"; rc=1; }; }
      echo "$out"
    done
    exit ${rc:-0} ;;
  render)
    root=$(realpath -m "$1"); shift
    names=("$@")
    if [ ${#names[@]} = 0 ]; then
      # every render recording that has a tape, in an order that runs air, rain and GUI scenes before
      # underwater and lava ones: a member's leftover state shows in the job after it
      for d in ../out/java/render/*/ ../out/java/rendertick/*/ ../out/java/hudscreens/*/; do
        [ -f "$d/tape.jsonl" ] || continue
        x=$(basename "$d"); a=$(basename "$(dirname "$d")")
        # the scenes that enter the End: its entities after the dimension change are not reproducible
        # (ledger te_portal_end-rec, fresh JVMs too), and a warm member hits that more often
        case $x in te_portal_end*|te_portal_sh*) echo "render: skip $a/$x (the End's entities after a dimension change: ledger te_portal_end-rec)"; continue ;; esac
        case $a in render) names+=("rs-$x") ;; rendertick) names+=("rt-$x") ;; hudscreens) names+=("hs-$x") ;; esac
      done
      under=(); rest=()
      for n in "${names[@]}"; do case $n in *underwater*|*lava*) under+=("$n") ;; *) rest+=("$n") ;; esac; done
      names=()
      # each underwater scene right after an air scene
      i=0
      for n in "${rest[@]}"; do names+=("$n"); [ $i -lt ${#under[@]} ] && [[ $n == rs-worldfx_* ]] && { names+=("${under[$i]}"); i=$((i + 1)); }; done
      while [ $i -lt ${#under[@]} ]; do names+=("${under[$i]}"); i=$((i + 1)); done
    fi
    rm -rf "$root/fresh" "$root/pool"; mkdir -p "$root"
    # one build before the parallel runs (each make would otherwise rebuild a stale tree at once)
    make -s ${MAKEX:-} > "$root/build.txt" 2>&1 || { echo "FAIL build: $root/build.txt"; exit 1; }
    echo "render: ${#names[@]} recordings: ${names[*]}"
    CACHE=0 bash "$0" replay fresh "$root/fresh" "${names[@]}" > "$root/fresh.txt" 2>&1 &
    fpid=$!
    make -s pool-stop > /dev/null 2>&1
    rm -rf ../out/java/pool/mip-1
    make -s pool-start POOLN=1 ${POOLARGS:+POOLARGS="$POOLARGS"} > "$root/pool-start.txt" 2>&1
    # a scene with mipmaps (a client reads them at start) goes to a second member with its options
    mip=
    for n in "${names[@]}"; do [ "$(head -1 "$(ref "$n")" | jq -r '.options.mip // 0')" != 0 ] && { mip=$(realpath "$(ref "$n")"); break; }; done
    [ -n "$mip" ] && make -s pool-start POOLN=1 POOLTAG=mip- POOLARGS="--pool-options $mip ${POOLARGS:-}" >> "$root/pool-start.txt" 2>&1
    for m in 1 ${mip:+mip-1}; do
      for i in $(seq 1 1200); do [ -f ../out/java/pool/$m/port ] && break; grep -q "ORACLE POOL END" ../out/java/pool/$m/log 2>/dev/null && break; sleep 0.5; done
      [ -f ../out/java/pool/$m/port ] || { echo "FAIL member $m did not start"; wait $fpid; exit 1; }
    done
    bash "$0" replay pool "$root/pool" "${names[@]}" > "$root/pool.txt" 2>&1
    wait $fpid
    cp ../out/java/pool/1/log "$root/member.log"
    [ -n "$mip" ] && cp ../out/java/pool/mip-1/log "$root/member-mip.log"
    make -s pool-stop > /dev/null 2>&1
    grep -a "NOT RUN BY THE POOL" "$root/pool.txt" && rc=1
    bash "$0" same "$root/fresh" "$root/pool" "${names[@]}" | tee "$root/same.txt"
    [ "${PIPESTATUS[0]}" = 0 ] && [ "${rc:-0}" = 0 ] && echo "PASS render: ${#names[@]} recordings pooled = fresh (verdicts, rows, frames)" || { echo "FAIL render"; exit 1; } ;;
  *) echo "usage: see the head of $0"; exit 2 ;;
esac
