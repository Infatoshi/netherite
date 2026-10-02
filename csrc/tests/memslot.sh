#!/usr/bin/env bash
# Run one native check inside a machine-wide memory budget.
#   bash tests/memslot.sh [--t FILE] [--key NAME] DATA_DIR command args...
# (--key names the job's kind instead of the command's basename: oracle/tests/
# headless.sh passes --key oracle so every oracle JVM shares the budget too.)
# The checks that generate or load regions of chunks hold them all in memory,
# every lane runs the whole suite at once, and the GPU host's 60 GB are shared with
# other projects; data size does not predict it. Peak RSS per job measured
# on the dev host 2026-09-26 (lane/replaymem, every directory of each test, GiB):
# projectiles 1.87, sblocks and portals 1.66; snapshots 0.87 (swamp-night-s2;
# 14 of 198 over 0.5), populate 0.52, seedworld 0.45. Everything else peaks
# under 0.4 (tileticks 0.36, spawning 0.35, dig 0.32, servertick 0.21).
#
# Budget mode, when /tmp/netherite-mem/budget holds a number of MB: each job
# reserves the peak RSS its binary and directory last measured
# (/tmp/netherite-mem/peaks, appended after every run through GNU time; a
# job never measured reserves a worst case for its binary, below) and waits
# while the budget's cgroup's real use plus what the live jobs may still
# grow by would pass the budget less a margin (below; the sum of every
# checkout's reservations where the cgroup cannot be read). A job that
# last peaked under 100 MB runs without a reservation. Slot counts sized for
# the worst job of a pool made small jobs queue behind big ones: the
# clientstate merge trial (2026-09-26) ran 987 job-seconds and waited 2,776.
# Without the budget file (the Mac, a fresh machine) the old slot pools apply:
# big (sblocks, portals, projectiles) and mid (populate, seedworld,
# snapshots) take one of 8 and 16 flock slots under /tmp/netherite-slots-*.
set -uo pipefail
# The cgroup the budget measures: the outermost one above this process with
# a memory limit (nv2.slice for a lane, whose own scope's cap sits inside
# it), else the user's nv2.slice (the orchestrator's session runs outside
# it), else none (the Mac): then only the reservations count.
cgroup_dir() {
  local c= l d v best=
  while read -r l; do case $l in 0::*) c=${l#0::};; esac; done 2>/dev/null < /proc/self/cgroup
  d=/sys/fs/cgroup$c
  while [ -n "$c" ] && [ "$d" != /sys/fs/cgroup ]; do
    for v in max high; do
      l=max; read -r l 2>/dev/null < "$d/memory.$v"
      [ "$l" != max ] && { best=$d; break; }
    done
    d=${d%/*}
  done
  d=/sys/fs/cgroup/user.slice/user-$UID.slice/user@$UID.service/nv2.slice
  [ -z "$best" ] && [ -r "$d/memory.stat" ] && best=$d
  [ -n "$best" ] && [ -r "$best/memory.stat" ] && echo "$best"
}
[ "${1:-}" = --cgroup ] && { cgroup_dir; exit 0; }
# --t FILE: write the time the job was admitted (date +%s%N) to FILE, so
# make test can split a job's time into waiting and running.
tfile=
key=
[ "$1" = --t ] && { tfile=$2; shift 2; }
[ "$1" = --key ] && { key=$2; shift 2; }
data=$1; shift
bin=${key:-${1##*/}}
M=/tmp/netherite-mem
if [ -f "$M/budget" ] && command -v flock >/dev/null; then
  name=$data; while [ "${name%/}" != "$name" ]; do name=${name%/}; done; name=${name##*/}
  # the largest of its last three measurements: checkouts on older code can
  # peak higher than the newest (lane/worldmem cut the world's memory 6-9x).
  # A pool member's, of its last twenty: its peak follows how long it lived
  # and what it ran (793 members to 2026-09-28: 531 to 3,329 MB, median
  # 2,539), so three short-lived ones in a row reserved a fraction of what
  # the next one could grow to (lane/poolfix)
  # (grep -F first: awk alone over the 574,000-line peaks file took 0.1 s of
  # CPU per job, 2026-09-28)
  k=3; [ "$name" = pool-member ] && k=20
  need=$(grep -F -- " $bin $name" "$M/peaks" 2>/dev/null | awk -v b="$bin" -v n="$name" -v k="$k" '$2 == b && $3 == n {v[++c] = $1} END {m = 0; for (i = c - k + 1; i <= c; ++i) if (i >= 1 && v[i] + 0 > m) m = v[i] + 0; print m}')
  if [ "${need:-0}" = 0 ]; then
    case $bin in
      test_sblocks|test_portals|test_projectiles) need=1900;;
      test_populate|test_seedworld) need=900;;
      # since lane/worldmem a replay's peak no longer grows with its
      # recording (gold-g15 2.7 GB before, 289 MB after, 2026-09-26)
      test_snapshots) need=1000;;
      # an oracle JVM runs with -Xmx3G: a run never measured (every fuzz case,
      # every new recording) can reach 3.2 to 3.9 GB RSS from a late-game
      # start; at 2000 six of them overran the slice (two OOM kills, 5:55 pm
      # 2026-09-26). Measured runs reserve their own peaks (most 1.1 GB).
      # A new name takes the largest of the last 5 measured peaks of runs
      # whose names share its prefix, cutting trailing numbers one at a time
      # (play-pf-g10-4001-s1-1.replay -> play-pf-g10-4001-s1-, ..., play-pf-g10-),
      # plus a quarter: 3000 for every new fuzz case held 43 GB of the budget
      # for JVMs peaking near 1.1 GB, and other lanes' single runs waited 40
      # to 60 s behind them (2026-09-27 3:30 am).
      oracle) need=$(grep -F ' oracle ' "$M/peaks" 2>/dev/null | awk -v name="$name" '$2 == "oracle" { nm[++c] = $3; pk[c] = $1 }
          END { p = name
                while (1) { q = p; sub(/[0-9]+[^0-9]*$/, "", q); if (q == p || length(q) < 6) break
                  p = q; m = 0; k = 0
                  for (i = c; i >= 1 && k < 5; --i) if (index(nm[i], p) == 1) { ++k; if (pk[i] + 0 > m) m = pk[i] + 0 }
                  if (k) { print int(m * 1.25) + 1; exit } }
                print 0 }')
              [ "${need:-0}" -gt 0 ] || need=3000;;
      *) need=400;;
    esac
  fi
  # an oracle JVM's reservation is named held.oracle.PID: the contents stay a
  # bare number, which every checkout's memslot sums, and the name lets this
  # one cap the oracle share (below)
  tok="$M/held.$$"; [ "$bin" = oracle ] && tok="$M/held.oracle.$$"
  # Admission by real use (lane/memslot2) where the budget's cgroup is
  # readable: the cgroup's committed memory (memstate.awk: memory.current
  # less its file cache: what every process in it holds, agents included)
  # plus what each live job may still grow by (its reservation less its
  # resident size while it ramps: 10 s for a native check, 30 s for an
  # oracle run, 60 s for a pool member and then at most 400 MB more;
  # memstate.awk's allow()) plus this job's reservation must fit under the
  # budget (or the cgroup's limit, if lower) less MARGIN MB. Over an hour
  # of every lane's jobs (2026-09-28) real use never passed what the rule
  # projected (777 MB under at worst); MARGIN covers what that hour did
  # not show: a measured job's run past its reservation (the largest in a
  # day of peaks, 1,029 MB, an oracle run) and an older copy's admission
  # overlapping this one's (below). The sum of reservations counted none of
  # the 1.9 GB (median, 4 GB p99) the agents, builds and helpers in the
  # slice hold outside any job. $M/margin, when it holds a number,
  # replaces MARGIN. Elsewhere (no cgroup v2, no gawk,
  # a snapshot every refresh of which failed) the sum of reservations must
  # fit under the budget, as before.
  MARGIN=3000
  cg=; [ -n "${EPOCHREALTIME:-}" ] && command -v gawk > /dev/null && cg=$(cgroup_dir)
  st=${BASH_SOURCE[0]%/*}/memstate.awk; [ "$st" != "${BASH_SOURCE[0]}/memstate.awk" ] || st=./memstate.awk
  [ -f "$st" ] || cg=
  if [ "$need" -ge 100 ]; then
    trap 'rm -f "$tok" "$M"/want.*."$$"' EXIT
    t0=${EPOCHSECONDS:-$(date +%s)}; wrote=
    declare -A grow
    while :; do
      # the budget is read on every pass, so raising it helps jobs already waiting
      budget=4500; read -r budget 2>/dev/null < "$M/budget"; case $budget in ''|*[!0-9]*) budget=4500;; esac
      n=$need; [ "$n" -gt "$budget" ] && n=$budget
      real=0
      if [ -n "$cg" ]; then
        # one snapshot a second serves every waiting job of every checkout
        sT=0; read -r sT scom slim _ 2>/dev/null < "$M/snap"
        now=${EPOCHREALTIME/./}; now=${now:0:13}; age=$((now - ${sT:-0}))
        if [ $age -gt 1000 ] || [ $age -lt 0 ]; then
          { flock -n 7 && gawk -f "$st" -v M="$M" -v CG="$cg" -v mode=snap; } 7> "$M/snap.lock"
          sT=0; read -r sT scom slim _ 2>/dev/null < "$M/snap"
          now=${EPOCHREALTIME/./}; now=${now:0:13}; age=$((now - ${sT:-0}))
        fi
        # a snapshot over 5 s old (every refresh failing) is not trusted
        if [ $age -ge 0 ] && [ $age -le 5000 ]; then
          real=1; grow=()
          { read -r _; while read -r sp sg; do grow[$sp]=$sg; done; } 2>/dev/null < "$M/snap"
        fi
      fi
      # Nothing under the lock forks. Each job took it with a cat per
      # reservation and ls, sort and date under load 50 to 75 on 32 threads
      # (nv2.slice at CPUWeight 20): about 160 ms a hold, and 48 jobs of one
      # suite queued 7 to 12 s on the lock alone, which make test counted as
      # waiting for memory (2026-09-28, lane/memslot2). Older copies (every
      # checkout that has not merged lane/memslot2) still hold $M/lock that
      # way, 58% of a minute at 3:04 am, and a suite of this one's waited
      # 19 s a job behind them; so this one takes $M/lock.r. An older copy's
      # decision can then overlap this one's: the two may admit past the
      # budget by that one older job's reservation, which MARGIN covers
      # (without the real-use measure this one takes $M/lock, as before).
      lk=$M/lock.r; [ $real = 1 ] || lk=$M/lock
      exec 8>"$lk"; flock 8
      used=0; oused=0; extra=0
      for h in "$M"/held.*; do
        [ -f "$h" ] || continue
        # a reservation whose job died without its trap (kill -9) frees itself
        p=${h##*.}
        if kill -0 "$p" 2>/dev/null; then
          hn=0; read -r hn 2>/dev/null < "$h"; case $hn in ''|*[!0-9]*) hn=0;; esac
          used=$((used + hn))
          case $h in "$M"/held.oracle.*) oused=$((oused + hn));; esac
          # a job the snapshot has not seen yet may still take all of it
          [ $real = 1 ] && extra=$((extra + ${grow[$p]:-$hn}))
        else rm -f "$h"; fi
      done
      if [ $real = 1 ]; then
        cap=$budget; [ "${slim:-0}" -gt 0 ] && [ "$slim" -lt "$cap" ] && cap=$slim
        m=$MARGIN; read -r m2 2>/dev/null < "$M/margin" && case $m2 in ''|*[!0-9]*) ;; *) m=$m2;; esac
        cap=$((cap - m)); load=$((scom + extra))
      else
        cap=$budget; load=$used
      fi
      # oracle JVMs take at most half the budget (one alone may take more)
      # while other jobs wait: a lane recording or fuzzing many at once left
      # clientui's merge trial 2,654 job-seconds of work and 32,020 of
      # waiting (2026-09-26 6:20 pm)
      # fairness: the oldest job that has waited over 60 s keeps its room; a
      # later job gets in only if it still leaves that room (a 2.8 GB replay
      # waited 7 minutes behind back-to-back 2 GB oracle runs, 2026-09-26).
      # 10 s was too short: under steady contention the head was always a
      # 2 GB oracle run and every smaller job queued behind it (a lane's
      # suite waited 7 minutes behind six parallel Java replays)
      # want.T.PID is an older memslot's place in the queue (it admits by the
      # sum of reservations alone), want.T.r.PID this one's; T has ten
      # digits, so the glob's order is the queue's
      first=; firstneed=0
      for w in "$M"/want.*; do
        [ -f "$w" ] || continue
        if kill -0 "${w##*.}" 2>/dev/null; then first=$w; read -r firstneed 2>/dev/null < "$w"; case $firstneed in ''|*[!0-9]*) firstneed=0;; esac; break; else rm -f "$w"; fi
      done
      mine=0; case $first in *."$$") mine=1;; esac
      room=1
      if [ -n "$first" ] && [ $mine = 0 ]; then
        [ $((load + n + firstneed)) -le "$cap" ] || room=0
        # an older memslot at the head admits itself by the sum of
        # reservations: leave it that room in those terms too, or it starves
        case $first in "$M"/want.*.r.*) ;; *) [ $((used + n + firstneed)) -le "$budget" ] || room=0;; esac
      fi
      # the cap applies only while a job that is not an oracle JVM has waited
      # over 60 s (a want file: oracle jobs never write one); with none
      # waiting, oracles may fill the budget (three oracle-heavy lanes ran
      # one JVM at a time under the half cap with nothing else queued,
      # replays at 190 s against 30, 2026-09-26 8:16 pm)
      ocap=1
      if [ "$bin" = oracle ] && [ -n "$first" ] && [ "$oused" -gt 0 ] && [ $((oused + n)) -gt $((budget / 2)) ]; then ocap=0; fi
      # with no reservation held, a job runs whatever else the cgroup holds
      if [ $ocap = 1 ] && [ $room = 1 ] && { [ $((load + n)) -le "$cap" ] || [ "$used" = 0 ]; }; then
        echo "$n" > "$tok"; exec 8>&-
        [ -z "$wrote" ] || rm -f "$wrote"
        break
      fi
      # an oracle JVM never holds a place in the fair queue: held back by its
      # share, it would make every other job leave room it cannot use
      if [ "$bin" != oracle ] && [ -z "$wrote" ] && [ $(( ${EPOCHSECONDS:-$(date +%s)} - t0 )) -ge 60 ]; then
        wrote="$M/want.${EPOCHSECONDS:-$(date +%s)}.r.$$"; echo "$n" > "$wrote"
      fi
      exec 8>&-
      sleep 0.2
    done
  fi
  [ -z "$tfile" ] || date +%s%N > "$tfile"
  if [ -x /usr/bin/time ] && /usr/bin/time -f %M -o /dev/null true 2>/dev/null; then
    /usr/bin/time -f %M -o "$M/rss.$$" "$@"; rc=$?
    kb=$(tail -1 "$M/rss.$$" 2>/dev/null); rm -f "$M/rss.$$"
    case $kb in ''|*[!0-9]*) ;; *) echo "$(( (kb + 1023) / 1024 )) $bin $name" >> "$M/peaks";; esac
  else
    "$@"; rc=$?
  fi
  rm -f "$tok"
  exit $rc
fi
case "$bin" in
  test_sblocks|test_portals|test_projectiles) pool=big; slots=8;;
  test_populate|test_seedworld|test_snapshots) pool=mid; slots=16;;
  *) pool=;;
esac
if [ -n "$pool" ] && command -v flock >/dev/null; then
  dir=/tmp/netherite-slots-$pool
  mkdir -p "$dir"
  while :; do
    for i in $(seq 1 "$slots"); do
      exec 9>"$dir/$i"
      flock -n 9 && break 2
    done
    exec 9>&-
    sleep 0.2
  done
fi
[ -z "$tfile" ] || date +%s%N > "$tfile"
"$@"
