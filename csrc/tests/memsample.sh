#!/usr/bin/env bash
# Sample the memory budget's real use: every EVERY seconds (default 1) one row
# of the slice's memory.stat, memory.events and pressure against memslot's
# live reservations, and one row per finished job (its reservation, the
# largest resident size of its process tree, when it reached it).
#   bash tests/memsample.sh OUT [EVERY]
# OUT (a TSV, OUT.jobs beside it) belongs outside /tmp (a RAM disk). Columns:
# committed = memory.current less the file LRUs and reclaimable slab (MB);
# sum_extra = what the live jobs inside the slice may still grow to their
# reservations; sum_over = MB the jobs hold beyond them; nonjob = committed
# less the jobs' anonymous and shmem resident MB; proj = what memslot.sh admits against
# (committed plus each job's allowance, memstate.awk's allow());
# old = the sum of reservations the budget was held to before lane/memslot2.
set -uo pipefail
out=${1:?OUT}; every=${2:-1}
here=$(cd "$(dirname "$0")" && pwd)
cg=$(bash "$here/memslot.sh" --cgroup)
[ -n "$cg" ] || { echo "memsample: no cgroup with a memory limit" >&2; exit 1; }
exec gawk -f "$here/memstate.awk" -v M=/tmp/netherite-mem -v CG="$cg" -v mode=sample -v out="$out" -v every="$every"
