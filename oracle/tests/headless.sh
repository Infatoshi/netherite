#!/usr/bin/env bash
# Run one command on its own Xvfb display, at most SLOTS at a time per machine.
#   bash tests/headless.sh SLOTS command args...
# Xvfb picks a free display itself (-displayfd), so parallel runs cannot
# collide; with `xvfb-run -a` 6 of 32 parallel oracles crashed on a display
# another run had taken. A run that finds every slot busy waits for one, so a
# burst of jobs from many lanes queues instead of running the box out of RAM.
set -uo pipefail
# --name NAME: the name memslot keeps the run's peaks under, instead of its
# out/java/run directory's (tests/poolauto.sh: every pool member is pool-member).
name=
[ "$1" = --name ] && { name=$2; shift 2; }
# Under the machine-wide memory budget (csrc/tests/memslot.sh, when
# /tmp/netherite-mem/budget exists) the run first reserves the peak RSS its
# run directory last measured (1.5 GB when new), like a native check: an
# oracle JVM takes 0.6-1.5 GB and the suites and JVMs of every lane share
# one 8 GB slice on the GPU host.
# ORACLE QUEUE (stderr) says how long the run waited for its memory and for
# a display slot before the JVM started (the oracle's --tick-profile covers
# the rest of the run).
if [ "$1" = --in-budget ]; then shift; q0=$1; shift
else
  q0=$(date +%s%N)
  m=$(cd "$(dirname "$0")/../../csrc/tests" 2>/dev/null && pwd)/memslot.sh
  if [ -f /tmp/netherite-mem/budget ] && [ -f "$m" ]; then
    run=$(printf '%s\n' "$@" | grep -ao 'out/java/run/[^ /]*' | head -1)
    run=${name:-${run##*/}}
    exec bash "$m" --key oracle "${run:-oracle-run}" bash "$0" --in-budget "$q0" "$@"
  fi
fi
q1=$(date +%s%N)
slots=$1; shift
dir=/tmp/netherite-oracle-slots
mkdir -p "$dir"
while :; do
  for i in $(seq 1 "$slots"); do
    exec 9>"$dir/$i"
    flock -n 9 && break 2
  done
  exec 9>&-
  sleep 0.5
done
q2=$(date +%s%N)
printf 'ORACLE QUEUE memory=%d.%03ds display=%d.%03ds\n' $(( (q1 - q0) / 1000000000 )) $(( (q1 - q0) / 1000000 % 1000 )) \
  $(( (q2 - q1) / 1000000000 )) $(( (q2 - q1) / 1000000 % 1000 )) >&2
fifo=$(mktemp -u /tmp/netherite-xvfb.XXXXXX)
mkfifo "$fifo"
Xvfb -displayfd 3 -screen 0 1280x800x24 -nolisten tcp 3>"$fifo" 9>&- >/dev/null 2>&1 &
xv=$!
read -r d < "$fifo" || true
rm -f "$fifo"
[ -n "${d:-}" ] || { echo "headless: Xvfb did not start" >&2; exit 1; }
DISPLAY=:$d "$@"
rc=$?
kill "$xv" 2>/dev/null
wait "$xv" 2>/dev/null
exit $rc
