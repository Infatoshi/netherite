#!/usr/bin/env bash
# What a self-check replay runs besides the classes (lane/selfselect), one
# line each, "NAME SHA1": the replay recipe as make would run it (make -n
# of `make replay` with this tree's path, the harness and the tape's path
# replaced by fixed names, runs of blanks squeezed (an empty optional
# argument changes nothing): JVM flags, the pool's arguments, the headless
# wrapper) and the scripts it calls (the pool, the shared pool's starter,
# the display and memory wrapper).
#   bash tests/selinputs.sh [JAVADIR]
# make coverage writes it beside the coverage data (COVDIR/inputs.tsv);
# make selfcheck SELECT=1 compares it with this tree's and runs every
# recording when any line differs.
set -u
J=$(cd "${1:-$(dirname "$0")/..}" && pwd)
R=$(cd "$J/.." && pwd)
printf 'replay %s\n' "$(make -n -s --no-print-directory -C "$J" replay REF=/SELINPUT/x/tape.jsonl CACHE=0 HARNESS=HARNESS -o build 2>&1 | sed "s|$R|ROOT|g" | tr -s " " | sed "s/ \([;']\)/\1/g" | sha1sum | cut -c1-16)"
for f in tests/pool.sh tests/poolauto.sh tests/headless.sh; do
    printf '%s %s\n' "$f" "$( { cat "$J/$f" 2>/dev/null || echo missing; } | sha1sum | cut -c1-16)"
done
