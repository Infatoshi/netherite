#!/usr/bin/env bash
# One recording through the per-phase kernel harness (tests/phasekernel.h,
# GPU plan L13): its phase trace first (test_snapshots --phase-trace), then
# the replay with PHASES round-tripped onto the device copy and run there by
# the stand-in kernel (--phase-kernel), each row's digests against the trace.
#   bash tests/phase_kernel_check.sh OUT DIR PHASES [VERIFY]
# Prints "NAME rc=N" with the run's summary; the log is
# OUT/phase-kernel.NAME.txt, and the trace is kept only when the check fails.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
out=$1
dir=${2%/}
phases=$3
verify=${4:-}
name=$(basename "$dir")
log=$out/phase-kernel.$name.txt
ref=$out/phase-kernel.$name.ref
bash tests/memslot.sh "$dir" "$out/test_snapshots" --phase-trace "$ref" "$dir" > "$log" 2>&1
rc=$?
if [ $rc != 0 ]; then
  echo "$name rc=$rc (the trace run; $log)"
  exit 0
fi
bash tests/memslot.sh --key phase-kernel "$dir" "$out/test_snapshots" --phase-kernel "$phases" \
  --phase-kernel-ref "$ref" ${verify:+--phase-kernel-verify "$verify"} "$dir" > "$log" 2>&1
rc=$?
echo "$name rc=$rc: $(grep -m1 -E '^phase-kernel: ([0-9]+ phase runs|FAIL)' "$log" | sed 's/^phase-kernel: //'); $(grep -m1 -E "^phase-kernel: (every row|FAIL [0-9])" "$log" | sed 's/^phase-kernel: //')"
[ $rc = 0 ] && rm -f "$ref"
exit 0
