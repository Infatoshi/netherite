#!/usr/bin/env bash
# The round-trip costs of make phase-kernel-cost as a table, from each log's
# "phase-kernel tsv" line (tests/phasekernel.h: rows, runs, the whole image's
# pages, then mean p50 p99 max of pages down, pages up, the host export's
# user and kernel instructions, the host import's; steady rows, the first
# row aside).
#   bash tests/phase_kernel_table.sh OUT PHASES NAME...
set -uo pipefail
out=$1
phases=$2
shift 2
echo "round trip per row with $phases on the device (steady rows): pages of 4 KB; instructions in thousands"
echo
echo "| recording | rows | image MB | down mean | down p50 | down p99 | up mean | up p50 | up p99 | export user k | export kernel k (p50) | import user k | import kernel k (p50) | MB moved per row |"
echo "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"
for r in "$@"; do
  f=$out/phase-kernel-cost.$r.$phases.txt
  line=$(grep -m1 $'^phase-kernel tsv\t' "$f" 2>/dev/null)
  if [ -z "$line" ]; then echo "| $r | (no run: $f) |"; continue; fi
  echo "$line" | awk -F'\t' -v r="$r" '{
    printf "| %s | %d | %.1f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f | %.0f (%.0f) | %.0f | %.0f (%.0f) | %.2f |\n",
      r, $2, $4 * 4096 / 1e6, $5, $6, $7, $9, $10, $11, $13 / 1e3, $17 / 1e3, $18 / 1e3, $21 / 1e3, $25 / 1e3, $26 / 1e3,
      ($5 + $9) * 4096 / 1e6 }'
done
