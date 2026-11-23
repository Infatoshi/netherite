#!/usr/bin/env bash
# The oracle result cache: a make script or make replay run whose inputs were
# run before returns the stored outputs instead of starting a JVM.
#
#   bash tests/oraclecache.sh ROOT CAPMB HARNESS JAVA OUTJ KIND RUNDIR [--key TEXT]... [--lookup] RERUN... -- ORACLE_ARGS...
#
# The Makefile calls it (script and replay, unless CACHE=0). KIND is script
# or replay, RUNDIR the run's game directory (out/java/run/NAME), RERUN the
# command that runs it fresh (make KIND CACHE=0 ...), ORACLE_ARGS the run's
# arguments as make built them (SCRIPT_ARGS, REPLAY_ARGS). The oracle is
# deterministic: the same harness, JVM, inputs and output-shaping arguments
# write the same rows, frames and snapshots. So the key is a hash of
#   the harness hash, the JVM's version, the libraries, the Mesa version,
#   this script, the kind, and every argument with each input file replaced
#   by its content hash (the script, the replayed tape, config.yaml, a raw GUI
#   file, any existing file a script names; a --from checkpoint by its path,
#   which the tape header records, and its manifest's sha256; a replayed
#   tape's checkpoint start as the oracle resolves it) and each output path
#   replaced by its role (TAPE, FRAMES, SNAPDIR, SAVEEND, TRACE, COVFILE: not
#   the path), and each --key TEXT (what else shapes the output: a coverage
#   run's JaCoCo version and agent options),
# and the value is every file the run wrote (found as files newer than a
# marker under the run's output roots, the roots its log names, and the
# absolute paths its script names) plus its log. Paths of the output roots
# inside stored text (tape rows name their frames, the log names its
# snapshots) are kept as role tokens and written back as the new run's paths.
#
# A hit restores the files (copies: a later run may rewrite them in place)
# and prints the log, without a JVM or a memory reservation. With --lookup
# a miss exits 125 at once (make coverage's first pass: the hits restore
# many at a time, then only the misses run). Otherwise a miss runs
# RERUN under a per-key lock (a concurrent identical miss waits, then hits)
# and stores the result if its rc is 0. A hit whose checkpoint directory
# already exists runs fresh, as the oracle refuses to overwrite one.
# Entries live in ROOT/entries, least recently used evicted past CAPMB;
# ROOT/stats.tsv logs every lookup: epoch, kind, result, key, seconds, the
# original run's seconds, cwd.
set -uo pipefail
# byte order: under a locale sort -u takes DIM-1 and DIM1 for one name; the
# oracle itself runs in the caller's locale
lc=${LC_ALL-unset}
export LC_ALL=C
root=$1 cap=$2 harness=$3 java=$4 outj=$5 kind=$6 rundir=$7
shift 7
extra=() lookup=
while [[ $# -gt 1 && $1 == --key ]]; do extra+=("key $2"); shift 2; done
[[ ${1:-} == --lookup ]] && { lookup=1; shift; }
rerun=()
while [[ $# -gt 0 && $1 != -- ]]; do rerun+=("$1"); shift; done
shift
args=("$@")
t0=$EPOCHREALTIME key=
self=${BASH_SOURCE[0]}

runfresh() { if [[ $lc == unset ]]; then env -u LC_ALL "${rerun[@]}"; else LC_ALL=$lc "${rerun[@]}"; fi; }
fresh() { runfresh; exit; }
stat_line() { printf '%s\t%s\t%s\t%s\t%.2f\t%s\t%s\n' "$(date +%s)" "$kind" "$1" "${key:0:16}" "$(bc <<< "$EPOCHREALTIME - $t0")" "${2:-}" "$PWD" >> "$root/stats.tsv" 2>/dev/null; }
bypass() { echo "oracle cache: not cached ($1)" >&2; mkdir -p "$root"; stat_line bypass; fresh; }
sha() { sha256sum "$1" 2>/dev/null | cut -c1-64; }

mkdir -p "$root/entries" "$root/locks" "$root/tmp" "$root/env" || bypass "no cache root $root"
# the key holds no environment: a debug switch read with getenv (NW_*, as lanes
# add while they bisect) could change the output under the same key
nwenv=$(env | grep -o "^NW_[A-Za-z0-9_]*" | head -3 | tr "\n" " ")
[[ -n $nwenv ]] && bypass "environment ${nwenv% }"

# ---------------------------------------------------------------- the key
# the JVM's version, the libraries and Mesa (the frames' rasterizer),
# memoized by the times of the JVM binary, the library directories and the
# package database
jv=$(readlink -f "$java")
emem=$root/env/$(printf '%s ' "$jv" $(stat -Lc %Y "$jv" "$outj/lib" "$outj/client" /var/lib/dpkg/status 2>/dev/null) | sha256sum | cut -c1-16)
if [[ ! -s $emem ]]; then
  { echo "java $("$java" -version 2>&1 | grep -v JAVA_TOOL_OPTIONS | tr '\n' ' ')"
    echo "libs $(ls -lL "$outj/lib" "$outj/client" 2>/dev/null | awk '{print $5, $NF}' | sha256sum | cut -c1-16)"
    echo "mesa $(dpkg-query -W -f '${Version}' libgl1-mesa-dri 2>/dev/null)"; } > "$emem.$$" && mv "$emem.$$" "$emem"
fi
mapfile -t K < "$emem"
K+=("oraclecache v1 $(sha "$self")" "kind $kind" "harness $harness" "${extra[@]}")
declare -A dest   # output role -> the path this run writes it to
prot=()           # paths the key fixes (they are never tokenized)
dest[RUN]=$rundir
script= ref= conf=
n=${#args[@]}
for ((i = 0; i < n; ++i)); do
  a=${args[i]} v=${args[i+1]:-}
  case $a in
    --agent|--dev|--detail|--enbt|--chat|--toasts|--tooltips|--snap-plain|--save-no-pre) K+=("$a") ;;
    --set|--rd|--particles|--option|--frame-every|--frame-ticks|--frame-pt|--frame-pts|--snap-at|--detail-from|--until|--nbt|--width|--height) K+=("$a $v"); i=$((i + 1)) ;;
    --conf) conf=$v; K+=("$a $(sha "$v")"); i=$((i + 1)) ;;
    --script) script=$v; K+=("$a $(sha "$v")"); i=$((i + 1)) ;;
    --raw-gui) K+=("$a $(sha "$v")"); i=$((i + 1)) ;;
    --replay) ref=$v; K+=("$a $(sha "$v")"); i=$((i + 1)) ;;
    --from) K+=("$a $v $(sha "$v/manifest.json")"); prot+=("$v"); i=$((i + 1)) ;;
    --tape) [[ $kind == script ]] && { dest[TAPE]=$v; dest[TAPEDIR]=$(dirname "$v"); }; K+=("$a"); i=$((i + 1)) ;;
    --frames) dest[FRAMES]=$v; K+=("$a"); i=$((i + 1)) ;;
    --trace) dest[TRACE]=$v; K+=("$a"); i=$((i + 1)) ;;
    # the render-state recorder (frame judges: the drops each frame drew)
    --renderstate) dest[RSTATE]=$v; K+=("$a"); i=$((i + 1)) ;;
    --snap-dir) dest[SNAPDIR]=$v; K+=("$a"); i=$((i + 1)) ;;
    # a coverage run's JaCoCo execution data (make coverage; --key names the agent)
    --coverage) dest[COVFILE]=$v; K+=("$a"); i=$((i + 1)) ;;
    # the checkpoint's name is its directory's name
    --save-at-end) dest[SAVEEND]=$v; K+=("$a $(basename "$v")"); i=$((i + 1)) ;;
    *) bypass "argument $a" ;;
  esac
done
# a script's save without "out" goes under config.yaml's directory (configs/'s parent for a
# named config, WorldConf.root); a replay's under the tree's
if [[ -n $conf ]]; then
  ckr=$(dirname "$conf"); [[ $(basename "$ckr") == configs ]] && ckr=$(dirname "$ckr")
  dest[CKROOT]=$ckr/out/java/checkpoints
else dest[CKROOT]=$outj/checkpoints; fi
if [[ -n $script ]]; then
  mapfile -t -O ${#prot[@]} prot < <(grep -ao '"/[^"]*"' "$script" | tr -d '"' | sort -u)
  # a file the script names under any key but an output one (out, frame,
  # blobs, post) is an input; its path is in the script's bytes already
  while IFS= read -r p; do
    [[ -f $p ]] && K+=("names $p $(sha "$p")")
  done < <(grep -ao '"[A-Za-z_]*":"/[^"]*"' "$script" | grep -v '^"\(out\|frame\|blobs\|post\)":' | sed 's/^"[A-Za-z_]*":"//; s/"$//' | sort -u)
fi
if [[ -n $ref ]]; then
  # the checkpoint a replayed tape starts on, as Main.configure resolves it
  hdr=$(head -1 "$ref")
  IFS=$'\t' read -r skind rec want seed < <(jq -r '[.start.kind // "-", .start.dir // "-", .start.manifestSha256 // "-", .seed] | @tsv' <<< "$hdr" 2>/dev/null)
  if [[ $skind == checkpoint ]]; then
    [[ $want == - ]] && want=
    ck=
    # the recorded path (in the tape's bytes), or the copy under the tree's
    # checkpoints, which the oracle names through the run directory (a token)
    prot+=("$rec")
    if [[ -f $rec/manifest.json && -f $rec/save/level.dat ]]; then ck=$rec how=recorded
    else
      ck=${dest[CKROOT]}/$seed/${rec##*/} how=local
      [[ -n $want && -f $ck/manifest.json && -f $ck/save/level.dat && $(sha "$ck/manifest.json") == "$want" ]] || ck= how=none
    fi
    K+=("start $how $([[ -n $ck ]] && sha "$ck/manifest.json")")
  fi
fi
# two roles at one path would share a token
for r in "${!dest[@]}"; do for q in "${!dest[@]}"; do
  [[ $r < $q && ${dest[$r]} == "${dest[$q]}" ]] && K+=("same $r $q")
done; done
key=$(printf '%s\n' "${K[@]}" | sha256sum | cut -c1-40)
E=$root/entries/${key:0:2}/$key


# ---------------------------------------------------------------- a hit
# An entry: key.txt, roles.tsv (each role's path when stored), secs (the
# run's seconds), size, log, files.tsv (ROLE, the path under it or @ for the
# role's own path, 1 if stored with tokens), files/ROLE/PATH and one/ROLE
# (plain copies), rw/ROLE/PATH (with tokens), edirs.tsv (empty directories), snapdirs.tsv and ckdirs.tsv
# (the snapshot and checkpoint directories it wrote, ROLE and PATH), done.
# ABS is the role of a path under no output root (a path the script names).
#
# the roles, longest path first: a nested root is tokenized before its parent
byLen=$(for r in "${!dest[@]}"; do printf '%s\t%s\n' "${#dest[$r]}" "$r"; done | sort -rn | cut -f2)
roles_by_len() { printf '%s\n' $byLen; }
# the rewriter's pairs FROM TO, NUL separated for its environment: in, each
# output root's path to its token and each key-determined path to itself (a
# --from checkpoint under an output root stays as it is); out, the tokens to
# this run's paths
tokpairs() { # tokpairs in|out
  local r p i=0
  for r in "${!dest[@]}"; do
    if [[ $1 == in ]]; then printf '%s\0' "OCF$i=${dest[$r]}" "OCT$i=@@OC:$r@@"; else printf '%s\0' "OCF$i=@@OC:$r@@" "OCT$i=${dest[$r]}"; fi
    i=$((i + 1))
  done
  if [[ $1 == in ]]; then for p in "${prot[@]}"; do printf '%s\0' "OCF$i=$p" "OCT$i=$p"; i=$((i + 1)); done; fi
  printf '%s\0' "OCN=$i"
}
# one pass, the longest match first: a nested root is tokenized before its parent
rewrite() { # rewrite in|out < from > to
  local -a e; mapfile -d '' e < <(tokpairs "$1")
  env "${e[@]}" perl -pe 'BEGIN { for $i (0 .. $ENV{OCN} - 1) { $m{$ENV{"OCF$i"}} = $ENV{"OCT$i"} }
    $re = join "|", map { quotemeta } sort { length($b) <=> length($a) } keys %m; $re = qr/($re)/ } s/$re/$m{$1}/g'
}
# RP: the path of ROLE's PATH (@: the role's own path)
rpath() { if [[ $1 == ABS ]]; then RP=$2; elif [[ $2 == @ ]]; then RP=${dest[$1]}; else RP=${dest[$1]}/$2; fi; }

hit() {
  [[ -f $E/done ]] || return 1
  local role rel rw p f
  # the oracle never overwrites a checkpoint: a run into an existing one fails, so run it
  while IFS=$'\t' read -r role rel; do
    [[ $role == ABS || -n ${dest[$role]:-} ]] || return 1
    rpath "$role" "$rel"; p=$RP; [[ -e $p ]] && { echo "oracle cache: $p exists, running fresh" >&2; return 1; }
  done < "$E/ckdirs.tsv"
  touch "$E/done"
  # a snapshot deletes these from its directory before it writes (Snapshot.dump)
  while IFS=$'\t' read -r role rel; do
    rpath "$role" "$rel"
    for f in chunks.bin.gz chunks.idx saved-chunks.bin.gz saved-chunks.idx chunkstate.jsonl chunkstate.jsonl.gz \
      saved-chunkstate.jsonl saved-chunkstate.jsonl.gz entities.jsonl entities.jsonl.gz clientents.jsonl clientents.jsonl.gz \
      ghosts.jsonl ghosts.jsonl.gz; do printf '%s\0' "$RP/$f"; done
  done < "$E/snapdirs.tsv" | xargs -0 -r rm -f
  while IFS=$'\t' read -r role rel; do rpath "$role" "$rel"; printf '%s\0' "$RP"; done < "$E/edirs.tsv" | xargs -0 -r mkdir -p
  for p in "$E"/files/*; do
    [[ -d $p ]] || continue
    role=${p##*/}
    if [[ $role == ABS ]]; then cp -rT --remove-destination "$p" / || return 1
    else mkdir -p "${dest[$role]}" && cp -rT --remove-destination "$p" "${dest[$role]}" || return 1; fi
  done
  for p in "$E"/one/*; do
    [[ -f $p ]] || continue
    role=${p##*/}
    mkdir -p "$(dirname "${dest[$role]}")" && cp --remove-destination "$p" "${dest[$role]}" || return 1
  done
  while IFS=$'\t' read -r role rel rw; do
    [[ $rw == 1 ]] || continue
    rpath "$role" "$rel"; p=$RP
    mkdir -p "$(dirname "$p")" && rm -f "$p" && rewrite out < "$E/rw/$role/$rel" > "$p" || return 1
  done < "$E/files.tsv"
  rewrite out < "$E/log"
  echo "oracle cache: hit ${key:0:12}, $(cat "$E/secs") s of oracle time saved" >&2
  stat_line hit "$(cat "$E/secs")"
  exit 0
}
hit
[[ -z $lookup ]] || { stat_line miss; exit 125; }

# ---------------------------------------------------------------- a miss
exec 7> "$root/locks/$key"
flock 7
hit   # a concurrent identical run stored it while this one waited

W=$root/tmp/$key.$$
rm -rf "$W"; mkdir -p "$W/files" "$W/one" "$W/rw"
trap 'rm -rf "$W"' EXIT
marker=$W/marker; : > "$marker"
# blobs a snapshot reuses are not rewritten, so a run into existing blobs cannot be captured
noblob=
for b in "${dest[SNAPDIR]:-}/blobs" $( [[ -n $script ]] && grep -ao '"/[^"]*blobs"' "$script" | tr -d '"'); do
  [[ -n $(ls -A "$b" 2>/dev/null) ]] && noblob=1
done
# what the caller's own shell writes (the log it redirects this run to) is not the run's
own=$(for fd in 1 2; do readlink -f /proc/$$/fd/$fd 2>/dev/null; done)
s0=$EPOCHREALTIME
runfresh 2>&1 | tee "$W/log.raw"
rc=${PIPESTATUS[0]}
secs=$(printf '%.1f' "$(bc <<< "$EPOCHREALTIME - $s0")")
[[ $rc == 0 ]] || { stat_line fail "$secs"; exit $rc; }
nostore() { stat_line nostore "$secs"; echo "oracle cache: not stored ($1)" >&2; exit 0; }
[[ -z $noblob ]] || nostore "the run reused existing snapshot blobs"
# a coverage run whose agent wrote nothing (ORACLE COVERAGE FAIL) would hit without data
[[ -z ${dest[COVFILE]:-} || -s ${dest[COVFILE]} ]] || nostore "no coverage data"

# the files the run wrote: newer than the marker under the run's own files,
# the declared outputs, every path (three levels deep or more) the log's
# ORACLE lines name and every path the script names
found=$( {
  printf '%s\n' "$rundir/replay.jsonl" "$rundir/logs/latest.log"
  for r in TAPE FRAMES TRACE SNAPDIR SAVEEND RSTATE COVFILE; do [[ -n ${dest[$r]:-} ]] && printf '%s\n' "${dest[$r]}"; done
  grep -a '^ORACLE ' "$W/log.raw" | grep -ao '/[^/[:space:]"=,;()]*/[^/[:space:]"=,;()]*/[^[:space:]"=,;()]*'
  [[ -n $script ]] && grep -ao '"/[^"]*"' "$script" | tr -d '"'
} | sed 's#/*$##' | sort -u | while IFS= read -r c; do
  [[ -e $c ]] || continue
  # the run's game directory holds its world and settings: only its rows and log are outputs
  case $c in "$rundir"|"$rundir"/*) [[ $c == "$rundir/replay.jsonl" || $c == "$rundir/logs/latest.log" ]] || continue ;; esac
  find "$c" \( -type f -o -type d -empty \) -newer "$marker" -printf '%y\t%p\n' 2>/dev/null
done | sort -u)
files=$(printf '%s\n' "$found" | sed -n 's/^f\t//p' | grep -vxF -f <(printf '%s\n' "$own" | grep . || echo /dev/null))
# the empty directories it made (a checkpoint's save keeps DIM-1 and DIM1 even when empty)
edirs=$(printf '%s\n' "$found" | sed -n 's/^d\t//p')
snapdirs=$(grep -ao '^ORACLE SNAPSHOT /[^ ]*' "$W/log.raw" | cut -d' ' -f3 | sort -u)
ckdirs=$(grep -ao '^ORACLE CHECKPOINT /[^ ]*' "$W/log.raw" | cut -d' ' -f3 | sort -u)
# a shared directory of tapes may hold other runs' files
for d in $snapdirs $ckdirs; do [[ $d == "$outj/tapes" || $d == "$outj" ]] && nostore "a snapshot in $d"; done
# each path as ROLE, the path under it (@ for the role's own), the path: the longest role that holds it
roleof() {
  local pairs; pairs=$(for r in $(roles_by_len); do printf '%s\t%s\n' "$r" "${dest[$r]}"; done)
  awk -F'\t' -v pairs="$pairs" 'BEGIN { n = split(pairs, L, "\n"); for (i = 1; i <= n; ++i) { split(L[i], a, "\t"); R[i] = a[1]; P[i] = a[2] } }
    { for (i = 1; i <= n; ++i) {
        if ($0 == P[i]) { print R[i] "\t@\t" $0; next }
        if (substr($0, 1, length(P[i]) + 1) == P[i] "/") { print R[i] "\t" substr($0, length(P[i]) + 2) "\t" $0; next } }
      print "ABS\t" $0 "\t" $0 }'
}
printf '%s\n' $snapdirs | grep . | roleof | cut -f1,2 > "$W/snapdirs.tsv"
printf '%s\n' $ckdirs | grep . | roleof | cut -f1,2 > "$W/ckdirs.tsv"
printf '%s\n' "$edirs" | grep . | roleof | cut -f1,2 > "$W/edirs.tsv"
# the output roots' paths inside stored text become tokens; a binary file
# naming one could not be written back for another destination
pats=$W/pats; for r in $(roles_by_len); do printf '%s\n' "${dest[$r]}"; done > "$pats"
naming=$(printf '%s\n' "$files" | grep . | xargs -r -d '\n' grep -lF -f "$pats" --)
text=$(printf '%s\n' "$naming" | grep . | xargs -r -d '\n' grep -lIF -f "$pats" --)
[[ $naming == "$text" ]] || nostore "a binary output names an output path: $(comm -23 <(printf '%s\n' "$naming") <(printf '%s\n' "$text") | head -1)"
printf '%s\n' "$naming" > "$W/naming"
printf '%s\n' "$files" | grep . | roleof | awk -F'\t' 'NR == FNR { n[$0] = 1; next } { print $1 "\t" $2 "\t" (($3 in n) ? 1 : 0) "\t" $3 }' "$W/naming" - > "$W/map"
cut -f1-3 "$W/map" > "$W/files.tsv"
while IFS=$'\t' read -r role rel rw p; do
  [[ $rw == 1 ]] || continue
  mkdir -p "$(dirname "$W/rw/$role/$rel")" && rewrite in < "$p" > "$W/rw/$role/$rel" || nostore "cannot store $p"
done < "$W/map"
# the plain files, copied a role at a time
for role in $(awk -F'\t' '$3 == 0 { print $1 }' "$W/map" | sort -u); do
  if [[ $role == ABS ]]; then src=/; else src=${dest[$role]}; fi
  awk -F'\t' -v r="$role" '$1 == r && $3 == 0 { print $2 }' "$W/map" > "$W/sel"
  if grep -qx @ "$W/sel"; then cp "$src" "$W/one/$role" || nostore "cannot copy $src"; fi
  grep -qvx @ "$W/sel" || continue
  mkdir -p "$W/files/$role"
  grep -vx @ "$W/sel" | { cd "$src" && xargs -r -d '\n' cp --parents -t "$W/files/$role" --; } || nostore "cannot copy under $src"
done
rm -f "$W/map" "$W/sel" "$W/naming" "$pats" "$marker"
rewrite in < "$W/log.raw" > "$W/log"; rm -f "$W/log.raw"
printf '%s\n' "${K[@]}" > "$W/key.txt"
for r in "${!dest[@]}"; do printf '%s\t%s\n' "$r" "${dest[$r]}"; done > "$W/roles.tsv"
echo "$secs" > "$W/secs"; du -sb "$W" | cut -f1 > "$W/size"
mkdir -p "$(dirname "$E")"
exec 8> "$root/lock"; flock 8
rm -rf "$E"; mv "$W" "$E" && touch "$E/done"
# least recently used out past the cap
total=$(cat "$root"/entries/*/*/size 2>/dev/null | awk '{s += $1} END {print s + 0}')
if [[ $total -gt $((cap * 1048576)) ]]; then
  ls -tr "$root"/entries/*/*/done 2>/dev/null | while IFS= read -r d; do
    [[ $total -gt $((cap * 1048576)) ]] || break
    d=$(dirname "$d"); [[ $d == "$E" ]] && continue
    total=$((total - $(cat "$d/size" 2>/dev/null || echo 0))); rm -rf "$d"
  done
fi
exec 8>&-
trap - EXIT
stat_line store "$secs"
exit 0
