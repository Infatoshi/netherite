#!/usr/bin/env bash
# The machine-wide warm pool: oracle pool members (Pool.java) that every tree
# on this machine shares, keyed by the harness hash, started on demand.
#
#   bash tests/poolauto.sh run HOME HARNESS CWD JAVA "JVMFLAGS" CLASSES LIB NATIVES CLIENTJAR ASSETS SLOTS MAX IDLE MAXMB COV -- oracle-args...
#   bash tests/poolauto.sh warm HOME HARNESS - JAVA "JVMFLAGS" CLASSES LIB NATIVES CLIENTJAR ASSETS SLOTS MAX IDLE MAXMB COV
#   bash tests/poolauto.sh status [HOME]
#   bash tests/poolauto.sh stop [HOME [KEY]]
#
# run (the Makefile's script and replay, after the tree's own pool): hands
# the run to a free member of HOME/KEY/m through pool.sh, KEY being the
# harness hash and a hash of what else a member's JVM is made of (the JVM,
# its flags, the libraries, natives, client jar and assets, each by its
# real path). A tree whose harness equals a live member's shares it, so the
# lanes that change only native code share members built from master's
# harness; a tree that edits oracle/ gets members of its own the same way.
# With no free member, unless a member refused the run (a run the pool
# never takes), it starts one in the background (while fewer than STARTS of
# the key are starting), then, if a member of the key is up, waits up to
# WAIT seconds for one to free, and else exits 125 (the caller runs a fresh
# JVM, as without a pool). The start is bounded: at most MAX members on the machine,
# and together they reserve at most half of the memory budget
# (csrc/tests/memslot.sh's oracle half).
#
# A member is its own JVM in its own directory HOME/KEY/m/N (Pool.java
# writes harness, pid and port there; pool.sh takes its lock for a run):
# its game directory is N/game, its classes a frozen copy of the starting
# tree's (HOME/KEY/classes), the libraries, natives and assets by real path,
# so it does not depend on the tree that started it (a lane worktree is
# removed after its merge). What a run resolves stays the calling tree's:
# its paths are absolute, its checkpoint root comes from its run directory
# (CWD, Main.configure), its outputs are written where a fresh JVM in that
# tree writes them. HOME is on disk ($HOME/dev/nw/oraclepool by default),
# not /tmp's RAM disk: the members' game directories keep every job's save.
#
# Each member runs in a systemd scope of its own under nv2.slice (where the
# user manager is reachable), not in the scope of the lane whose run started
# it: stopping that lane leaves it up, and its memory is not charged to that
# lane. Its reservation is memslot's (key oracle, name pool-member: held
# while it lives, sized by the last members' peaks). Beside it a watchdog
# (in the same scope) retires it, between runs (it takes the member's lock,
# removes port and pid, then stops it by its pid file), after IDLE seconds
# without a run or past MAXMB resident; it then moves the member's directory
# to HOME/KEY/retired with its log (ORACLE POOL RETIRE: why it ended). The
# member also ends itself after a run that leaves it over MAXMB (Pool.java
# --pool-max-mb, checked after every run; the watchdog's check catches growth
# between runs).
#
# COV is - or, for a coverage run (make replay COVERAGE=FILE), JAR=OPTIONS:
# JaCoCo's agent jar and its options. Its members carry that agent ahead of
# PoolAgent (Coverage.java, CovInit.java: each job's data cut from the
# member's), their key is HARNESS-cov plus the hash with the jar's sha1 and
# the options, so they serve only coverage runs and normal members never
# do; the jar is copied beside the frozen classes.
#
# warm waits (up to 10 minutes) for a member of the key, starting one if
# none is up. status prints every member (key, pid, state, jobs, idle seconds, rss);
# stop stops them by their pid files (never by pattern). HOME/events.log
# has one line per start, refusal to start and retirement.
set -uo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cmd=${1:-}; shift || true

now() { date +%s; }
ev() { printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$home/events.log"; }
alive() { [ -n "${1:-}" ] && kill -0 "$1" 2>/dev/null; }
# memslot's need for a member: the largest of the last twenty member peaks (memslot.sh), 3000 MB when none
need_mb() {
  local n
  n=$(awk '$2 == "oracle" && $3 == "pool-member" {v[++c] = $1} END {m = 0; for (i = c - 19; i <= c; ++i) if (i >= 1 && v[i] + 0 > m) m = v[i] + 0; print m}' /tmp/netherite-mem/peaks 2>/dev/null)
  [ "${n:-0}" = 0 ] && n=3000
  echo "$n"
}
rss_mb() { awk '/^VmRSS:/ {print int($2 / 1024)}' "/proc/$1/status" 2>/dev/null; }
# Members of one key starting at once: at most STARTS, each named by a file KD/starting, starting2,
# starting3 holding its launcher's pid. One at a time filled an empty pool one member per 25 to 70 s
# under load, and a selfcheck's first 39 runs went to fresh JVMs meanwhile (lane/replaylat).
STARTS=3
nstarting() { local f c=0; for f in "$1"/starting "$1"/starting2 "$1"/starting3; do [ -f "$f" ] && alive "$(cat "$f" 2>/dev/null)" && c=$((c + 1)); done; echo "$c"; }
freeslot() { local f; for f in starting starting2 starting3; do if ! [ -f "$1/$f" ] || ! alive "$(cat "$1/$f" 2>/dev/null)"; then echo "$f"; return; fi; done; }
# a live member of the key (up, busy or free)
anylive() { local m; for m in "$1"/m/*/; do [ -f "$m/port" ] && alive "$(cat "$m/pid" 2>/dev/null)" && return 0; done; return 1; }
# How long a run waits for a busy member of its key before it goes to a fresh JVM: a member frees
# in seconds, a fresh JVM costs 20 to 90 s at load 100 to 150 (its memory and display queue, the
# JVM and client start, a cold JIT; master's selfcheck: 88 fresh runs, 6,985 of 16,791 s).
WAIT=15

case $cmd in
run|warm)
  home=$1 harness=$2 cwd=$3 java=$4 jflags=$5 classes=$6 lib=$7 natives=$8 cjar=$9 assets=${10} slots=${11} max=${12} idle=${13} maxmb=${14} cov=${15:--}
  shift 15; [ "${1:-}" = -- ] && shift
  rj=$(readlink -f "$java") rl=$(readlink -f "$lib") rn=$(readlink -f "$natives") rc=$(readlink -f "$cjar") ra=$(readlink -f "$assets")
  if [ "$cov" = - ]; then
    key=$harness-$(printf '%s\n' "$rj" "$jflags" "$rl" "$rn" "$rc" "$ra" | sha1sum | cut -c1-8)
  else
    # a coverage member: the agent by content, so every tree with the same JaCoCo shares it
    cj=$(readlink -f "${cov%%=*}")
    [ -f "$cj" ] || exit 125
    key=$harness-cov$(printf '%s\n' "$rj" "$jflags" "$rl" "$rn" "$rc" "$ra" "$(sha1sum < "$cj" | cut -c1-40)" "${cov#*=}" | sha1sum | cut -c1-8)
    cov=$cj=${cov#*=}
  fi
  kd=$home/$key
  if [ $cmd = warm ]; then
    # a member of this key up and free (make pool-warm, the proofs), started if none is
    for t in $(seq 1 1200); do
      for m in "$kd"/m/*/; do
        [ -f "$m/port" ] && alive "$(cat "$m/pid" 2>/dev/null)" && { echo "pool: shared member $key/$(basename "$m") is up, pid $(cat "$m/pid")"; exit 0; }
      done
      if [ $t = 1 ] || { [ $((t % 20)) = 0 ] && [ "$(nstarting "$kd")" = 0 ]; }; then
        mkdir -p "$kd/m"
        setsid -f bash "$here/poolauto.sh" start "$home" "$key" "$harness" "$rj" "$jflags" "$classes" "$rl" "$rn" "$rc" "$ra" "$slots" "$max" "$idle" "$maxmb" "$cov" > /dev/null 2>&1 < /dev/null
      fi
      sleep 0.5
    done
    echo "pool: no shared member of $key after 10 minutes; see $home/events.log"; exit 1
  fi
  # the flags the pool refuses before anything runs (pool.sh --never): no member for them
  bash "$here/pool.sh" --never "$@" && exit 125
  st=$kd/state.$$
  # one try at the key's free members: returns (exits) unless none was free
  try() {
    [ -d "$kd/m" ] || return 0
    bash "$here/pool.sh" --state "$st" "$kd/m" "$harness" "$cwd" "$@"
    r=$?; s=$(cat "$st" 2>/dev/null); rm -f "$st"
    # 124: a member failed the run (pool.sh), the caller runs it fresh
    [ "$r" = 125 ] || exit "$r"
    # a member refused it: a run of a kind the pool never takes, another member would not either
    [ "$s" = refused ] && exit 125
    return 0
  }
  try "$@"
  mkdir -p "$kd/m" || exit 125
  # up to STARTS starts at a time per key; the caller does not wait for them
  if [ "$(nstarting "$kd")" -lt $STARTS ]; then
    echo "pool: no free shared member for harness $harness; one starts in the background" >&2
    setsid -f bash "$here/poolauto.sh" start "$home" "$key" "$harness" "$rj" "$jflags" "$classes" "$rl" "$rn" "$rc" "$ra" "$slots" "$max" "$idle" "$maxmb" "$cov" \
      > /dev/null 2>&1 < /dev/null
  fi
  # every live member busy: wait up to WAIT seconds for one to free (or a starting one to come up)
  if anylive "$kd"; then
    w0=${EPOCHSECONDS:-$(now)}
    while [ $(( ${EPOCHSECONDS:-$(now)} - w0 )) -lt $WAIT ]; do
      sleep 0.5
      try "$@"
    done
    echo "pool: no shared member for harness $harness freed in $WAIT s: fresh JVM" >&2
  else
    echo "pool: no live shared member for harness $harness: fresh JVM" >&2
  fi
  exit 125 ;;

start)
  # detached: decide under the machine-wide lock, then launch the member in its own scope
  home=$1 key=$2 harness=$3 java=$4 jflags=$5 classes=$6 lib=$7 natives=$8 cjar=$9 assets=${10} slots=${11} max=${12} idle=${13} maxmb=${14} cov=${15:--}
  kd=$home/$key
  exec 8> "$home/lock"
  flock -w 30 8 || exit 0
  slot=$(freeslot "$kd")
  [ -n "$slot" ] || exit 0
  # members whose launcher died without its watchdog (kill -9, a stopped scope) go to retired/
  for m in "$home"/*/m/*/; do
    [ -d "$m" ] || continue
    [ -f "$m/mpid" ] && alive "$(cat "$m/mpid")" && continue
    [ -f "$m/mpid" ] || [ "$(( $(now) - $(stat -c %Y "$m") ))" -gt 120 ] || continue
    k=$(basename "$(dirname "$(dirname "$m")")")
    mkdir -p "$home/$k/retired" && mv "$m" "$home/$k/retired/$(basename "$m")-$(now)" && ev "$k/$(basename "$m") swept (its launcher is gone)"
  done
  # a retired member keeps its log a day and never its game directory, and a harness with no
  # member, none starting and nothing new for a day goes whole (its classes are copied again
  # if it comes back): retired saves of dead harnesses had grown HOME to 25 GB over 163
  # harnesses by 2026-09-28, since only a member of the same harness pruned its retired/
  for r in "$home"/*/retired/*/; do [ -d "$r/game" ] && rm -rf "$r/game"; done
  find "$home"/*/retired -mindepth 1 -maxdepth 1 -mmin +1440 -exec rm -rf {} + 2>/dev/null
  for k in "$home"/*/; do
    [ "${k%/}" = "$kd" ] && continue
    ls "$k/m" 2>/dev/null | grep -q . && continue
    ls "$k"/starting* > /dev/null 2>&1 && continue
    find "$k" -maxdepth 2 -mmin -1440 | grep -q . && continue
    rm -rf "$k"
  done
  n=0 held=0 need=$(need_mb)
  for m in "$home"/*/m/*/; do
    [ -f "$m/mpid" ] || continue
    p=$(cat "$m/mpid"); alive "$p" || continue
    n=$((n + 1))
    h=$(cat "/tmp/netherite-mem/held.oracle.$p" 2>/dev/null || echo "$need")
    held=$((held + h))
  done
  if [ "$n" -ge "$max" ]; then ev "$key not started: $n members, the most (POOLMAX $max)"; exit 0; fi
  budget=$(cat /tmp/netherite-mem/budget 2>/dev/null || echo 0)
  if [ "$budget" -gt 0 ] && [ $((held + need)) -gt $((budget / 2)) ]; then
    ev "$key not started: members hold $held MB, one more needs $need, the oracle half of the budget is $((budget / 2))"; exit 0
  fi
  # the classes, frozen: the tree that starts it may rebuild or be removed
  if [ ! -f "$kd/classes/.complete" ]; then
    rm -rf "$kd/classes.$$" && cp -a "$classes" "$kd/classes.$$" && touch "$kd/classes.$$/.complete" &&
      rm -rf "$kd/classes" && mv "$kd/classes.$$" "$kd/classes" || { rm -rf "$kd/classes.$$"; ev "$key not started: cannot copy $classes"; exit 0; }
  fi
  # a coverage member's JaCoCo agent, frozen the same way
  if [ "$cov" != - ] && [ ! -f "$kd/jacocoagent.jar" ]; then
    cp "${cov%%=*}" "$kd/jacocoagent.jar.$$" && mv "$kd/jacocoagent.jar.$$" "$kd/jacocoagent.jar" || { ev "$key not started: cannot copy ${cov%%=*}"; exit 0; }
  fi
  i=$(( $(cat "$kd/next" 2>/dev/null || echo 0) + 1 )); echo "$i" > "$kd/next"
  d=$kd/m/$i
  mkdir -p "$d/game"
  echo $$ > "$kd/$slot"
  ev "$key/$i starting ($n members up, $held MB held)"
  exec 8>&-
  # its own scope under nv2.slice when the user manager is there; else here
  sc=()
  if command -v systemd-run > /dev/null && systemd-run --user --scope --quiet --slice=nv2.slice true 2>/dev/null; then
    sc=(systemd-run --user --scope --quiet --slice=nv2.slice -p MemoryMax=6G --)
  fi
  exec "${sc[@]}" bash "$here/poolauto.sh" member "$home" "$key" "$i" "$harness" "$java" "$jflags" "$lib" "$natives" "$cjar" "$assets" "$slots" "$idle" "$maxmb" "$cov" "$slot" ;;

member)
  # the member and its watchdog
  home=$1 key=$2 i=$3 harness=$4 java=$5 jflags=$6 lib=$7 natives=$8 cjar=$9 assets=${10} slots=${11} idle=${12} maxmb=${13} cov=${14:--} slot=${15:-starting}
  kd=$home/$key d=$home/$key/m/$i
  echo $$ > "$kd/$slot"
  # the agent that tells det.nbt which split owners a run touched (PoolAgent.java); a
  # coverage member's JaCoCo agent goes first, so JaCoCo sees (and names) the classes' own bytes
  agent=; [ -f "$kd/classes/poolagent.jar" ] && agent=-javaagent:$kd/classes/poolagent.jar
  [ "$cov" != - ] && agent="-javaagent:$kd/jacocoagent.jar=${cov#*=} $agent"
  # the JVM runs in its game directory; headless.sh reserves its memory (memslot, as pool-member) and its display
  # shellcheck disable=SC2086
  bash "$here/headless.sh" --name pool-member "$slots" bash -c 'cd "$1" && shift && exec "$@"' - "$d/game" \
    "$java" -Xmx3G $jflags $agent -Djavax.accessibility.assistive_technologies= -Djava.library.path="$natives" \
    -cp "$kd/classes:$lib/*:$cjar" netherite.oracle.Main --game-dir . --assets "$assets" --harness "$harness" \
    --agent --pool "$d" --pool-max-mb "$maxmb" >> "$d/log" 2>&1 < /dev/null &
  lp=$!
  echo "$lp" > "$d/mpid"
  why=
  retire() { # retire WHY: between runs, under the member's lock
    local p; p=$(cat "$d/pid" 2>/dev/null)
    rm -f "$d/port" "$d/pid"
    why=$1
    echo "ORACLE POOL RETIRE $why after $(grep -ac '^ORACLE POOL JOB ' "$d/log") jobs" >> "$d/log"
    ev "$key/$i retired: $why"
    if alive "$p"; then
      kill "$p"
      for _ in $(seq 1 300); do alive "$p" || break; sleep 0.2; done
      alive "$p" && kill -9 "$p"
    fi
  }
  started=0
  while alive "$lp"; do
    sleep 5
    [ -f "$d/port" ] && [ -f "$d/pid" ] || continue
    if [ $started = 0 ]; then started=1; [ "$(cat "$kd/$slot" 2>/dev/null)" = $$ ] && rm -f "$kd/$slot"; ev "$key/$i ready, pid $(cat "$d/pid")"; fi
    p=$(cat "$d/pid")
    last=$(stat -c %Y "$d/used" 2>/dev/null || stat -c %Y "$d/port")
    rss=$(rss_mb "$p"); rss=${rss:-0}
    if [ $(( $(now) - last )) -ge "$idle" ]; then
      exec 9> "$d/lock"
      if flock -n 9; then
        last=$(stat -c %Y "$d/used" 2>/dev/null || stat -c %Y "$d/port")
        [ $(( $(now) - last )) -ge "$idle" ] && retire "idle $(( $(now) - last )) s (POOLIDLE $idle), rss $rss MB"
      fi
      exec 9>&-
    elif [ "$rss" -gt "$maxmb" ]; then
      # past the cap: wait for the run in progress, then end
      exec 9> "$d/lock"
      flock -w 600 9 && [ -f "$d/pid" ] && retire "rss $(rss_mb "$p") MB over POOLMAXMB $maxmb"
      exec 9>&-
    fi
  done
  wait "$lp" 2>/dev/null
  # the JVM ended by itself: --pool-max-mb after a job (Pool.retire), a failed teardown, a crash
  [ -n "$why" ] || ev "$key/$i ended: $(grep -a 'ORACLE POOL END\|Exception in\|Terminating due to\|^#' "$d/log" | tail -1)"
  rm -f "$d/port" "$d/pid"
  [ "$(cat "$kd/$slot" 2>/dev/null)" = $$ ] && rm -f "$kd/$slot"
  rm -rf "$d/game"
  mkdir -p "$kd/retired" && mv "$d" "$kd/retired/$i-$(now)"
  # retired members' logs are kept a day
  find "$kd/retired" -mindepth 1 -maxdepth 1 -mmin +1440 -exec rm -rf {} + 2>/dev/null
  exit 0 ;;

status)
  home=${1:-$HOME/dev/nw/oraclepool}
  printf '%-22s %-4s %-8s %-8s %5s %7s %7s\n' key m pid state jobs idle_s rss_mb
  for m in "$home"/*/m/*/; do
    [ -d "$m" ] || continue
    m=${m%/}; k=$(basename "$(dirname "$(dirname "$m")")")
    p=$(cat "$m/pid" 2>/dev/null); s=starting
    if [ -f "$m/port" ] && alive "$p"; then
      s=free; exec 9> "$m/lock"; flock -n 9 || s=busy; exec 9>&-
    fi
    last=$(stat -c %Y "$m/used" 2>/dev/null || stat -c %Y "$m/port" 2>/dev/null || echo "$(now)")
    printf '%-22s %-4s %-8s %-8s %5s %7s %7s\n' "$k" "${m##*/}" "${p:--}" "$s" "$(grep -ac '^ORACLE POOL JOB ' "$m/log" 2>/dev/null)" \
      $(( $(now) - last )) "$( [ -n "$p" ] && rss_mb "$p" || echo -)"
  done ;;

stop)
  home=${1:-$HOME/dev/nw/oraclepool} only=${2:-}
  for m in "$home"/*/m/*/; do
    [ -d "$m" ] || continue
    m=${m%/}; k=$(basename "$(dirname "$(dirname "$m")")")
    [ -z "$only" ] || [ "$k" = "$only" ] || continue
    p=$(cat "$m/pid" 2>/dev/null)
    alive "$p" || continue
    # after the run in progress
    exec 9> "$m/lock"; flock -w 600 9
    rm -f "$m/port" "$m/pid"
    kill "$p"; for _ in $(seq 1 300); do alive "$p" || break; sleep 0.2; done
    echo "pool: shared member $k/${m##*/} stopped (pid $p)"
    exec 9>&-
  done ;;

*) sed -n '2,48p' "$0"; exit 2 ;;
esac
