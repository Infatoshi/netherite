# CENSUS - every 1.11.2 feature vs magma, one row each

Produced 2026-07-30 by the GOAL.md P1 fan-out: four parallel delegate sweeps
of the oracle registries (java/oracle-src) against the magma implementation,
with per-row citations. Domain files hold the rows; this page is the index
and the honest totals.

| domain | rows | implemented | partial | missing | cut | file |
|--------|------|-------------|---------|---------|-----|------|
| blocks | 236 | 44 | 138 | 2 | 52 | census/blocks.md |
| items | 392 | 9 | 235 | 80 | 68 | census/items.md |
| entities | 81 | 23 | 24 | 15 | 19 | census/entities.md |
| mechanics | 102 | 26 | 41 | 23 | 12 | census/mechanics.md |

Reading the numbers honestly:
- "partial" dominates because the bar is total: a block is implemented only
  if it generates, renders, AND collides; an item only if behavior (not just
  the icon) is in; an entity only if simulated, not render-only. Most
  partials are render-or-registry-only.
- "cut" rows cite docs/SCOPE.md section 1; everything else that is absent
  is "missing" and belongs on the flywheel.
- Every implemented/partial row carries a magma file:symbol citation; every
  non-cut uncovered row carries a scenario candidate for phase P2.

Maintenance: rows change status only with a citation change; re-run the P1
sweep after large merges rather than hand-editing counts.
