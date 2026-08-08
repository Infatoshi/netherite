# Candidate C — RET_NORM=1

## Implementation (exact)

Per-chunk, on valid GAE returns `R`:

1. Compute batch mean `m` and population variance `v` of `R`.
2. EMA update (`RET_NORM_MOMENTUM=0.99` default):
   - first chunk: `mu ← m`, `var ← max(v, 1e-6)`
   - else: `mu ← 0.99*mu + 0.01*m`, `var ← 0.99*var + 0.01*v`
3. `sigma = sqrt(max(var, 1e-6))`
4. Value-loss targets become `(R - mu) / (sigma + 1e-8)`.
5. Advantages still minibatch-normalized as before. GAE still uses raw V (no PopArt denormalize on bootstrap).

This is **target-side popart-lite**, not full PopArt (no output denormalize, no last-layer affine reparam).

Default `RET_NORM=0` preserves oracle parity.

Optional experiment `cand_c2_retnorm_clamp` added target `.clamp(-10, 10)` after normalize: killed the early spike (peak 15.6) but left steady `|V|~11` and hurt reward (0.0169). **Not** the production choice; unclamped C wins on reward + end bound.

## 300-chunk receipt (vs Phase 1 baseline)

| metric | baseline | RET_NORM | delta |
|--------|---------:|---------:|------:|
| max\|V\| at chunk 300 | 38.25 | **2.94** | **-92%** |
| mean\|V\| last 50 chunks | 31.70 | **3.54** | flat band |
| max\|V\| over run | 38.25 | **77** | early spike only |
| \|V\| at chunk 100 | 11.31 | 3.83 | better |
| reward last | 0.0306 | 0.0294 | -0.0012 (match) |
| reward last10 mean | 0.0305 | 0.0279 | close |
| cells last | 39 | 33 | -6 |
| steady wall_ms median | 1322.35 | 1312.58 | -0.74% |
| grad_norm peak (early) | ~18 | **4643** | transient |

Trajectory max\|V\|: 0.07 → **38 (c10)** → 8.1 (c50) → 3.8 (c100) → 6.9 (c200) → **2.94 (c300)**. After ~c30, sits in a small band; not climbing.

## Criteria

1. **|V| bounded + flat at 300 chunks:** **PASS** (2.94 << 50; last-50 mean 3.54, not climbing). **Caveat:** mid-run peak 77 during first ~15 chunks while EMA/stats adapt; recovers.
2. **Reward/curriculum not degraded:** **PASS on reward** (within noise of baseline). Curriculum cells slightly lower (33 vs 39).
3. **Speed within 2%:** **PASS** (-0.74%).

## Rank

**Winner among A/B/C.** Only single flag that breaks the bootstrap ratchet at this horizon while preserving reward and speed. Early grad/value transient is the residual risk (see RECOMMENDATION for production note).
