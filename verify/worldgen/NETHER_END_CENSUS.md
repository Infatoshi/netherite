# Nether + End terrain census (blaze CPU / CUDA / magma)

Measurement lane on `wt/nethertick` @ parent `2500c77`. Diagnostic only; does **not**
touch `wrapper_gate.sh` / `known_divergences.json`.

Date (UTC): 2026-08-03T03:02:21Z  
Repro: `bash verify/worldgen/nether_census.sh`  
Artifacts (local, not committed bulk dumps): `verify/worldgen/out_nether/`

## Regions

| Region | Chunk box | World xz (approx) |
|--------|-----------|-------------------|
| Nether origin | `(cx,cz) in [-2,1]^2` (4x4) | `[-32,32) x [-32,32)` |
| Nether fortress | 3x3 centered on first `ft_can_spawn` in `[-48,48]^2` | per seed (table below) |
| End main island | `(cx,cz) in [-4,3]^2` (8x8) | `[-64,64) x [-64,64)` |

**In scope:** nether `nf_run` (prepareHeights + buildSurfaces + hell caves + fortress);
end `cpe_provide_chunk` (main island density / end stone).

**Out of scope:** nether populate (fire / glowstone / quartz / springs / mushrooms);
`MapGenEndSpike` (not in provideChunk). Fixed pillar crystal xz lives in
`blaze/core/ender_dragon.h` `ed_pillar_crystal_pos` (not seed-dependent terrain gen).

## Paths under test

| Side | Entry |
|------|--------|
| blaze CPU | `verify/worldgen/dim_region_dump.c` -> `nf_run` / `cpe_provide_chunk` |
| blaze CUDA | `verify/worldgen/dim_region_dump.cu` (same headers, single-thread kernels) |
| magma | `magma/trace/world_dump --world-type 2\|3 --states` -> `light.c` gen_chunk (`nf_run` / `cpe_provide_chunk`) |

GPU: GPU0, `CUDA_VISIBLE_DEVICES=0`, `SM=sm_120`, `flock /home/infatoshi/dev/nw/.tmp/gpu0.lock`.

## Injected-divergence harness proof

Synthetic one-cell edit (87 netherrack -> 88 soul_sand at `(1,64,0)`):

- `diff_cells=1`, `first_diff=(1,64,0)`, `harness_detects_injected=true`
- sha256 of sorted diff lines: `5ed3c24e2c1dc8d6b6ca1af5d98b04109a61caed1f0698c314e0c81c898a2f44`

Zero-diff results below are therefore trustworthy (harness is not a silent no-op).

## Fortress starts (scan radius +/-48)

| seed | fortress cx | fortress cz | fort dump window (cx0,cz0)+3x3 |
|------|-------------|-------------|--------------------------------|
| 0 | -26 | -42 | (-27,-43) |
| 2 | 7 | -41 | (6,-42) |
| 3 | -42 | -42 | (-43,-43) |
| 7 | -39 | -44 | (-40,-45) |
| 9 | -6 | -25 | (-7,-26) |
| 10 | -8 | -43 | (-9,-44) |
| 19 | 20 | -42 | (19,-43) |

## Nether: blaze CPU vs CUDA

| seed | region | non_air cells | diff_cells | first_diff | classes |
|------|--------|---------------|------------|------------|---------|
| 0 | origin | 431855 | 0 | - | {} |
| 0 | fortress | 127343 | 0 | - | {} |
| 2 | origin | 457979 | 0 | - | {} |
| 2 | fortress | 166719 | 0 | - | {} |
| 3 | origin | 462612 | 0 | - | {} |
| 3 | fortress | 245687 | 0 | - | {} |
| 7 | origin | 281825 | 0 | - | {} |
| 7 | fortress | 170800 | 0 | - | {} |
| 9 | origin | 468390 | 0 | - | {} |
| 9 | fortress | 204087 | 0 | - | {} |
| 10 | origin | 307341 | 0 | - | {} |
| 10 | fortress | 158463 | 0 | - | {} |
| 19 | origin | 323338 | 0 | - | {} |
| 19 | fortress | 189020 | 0 | - | {} |

## Nether: blaze CPU vs magma

Same seeds/regions/cell counts as above; **all `diff_cells=0`**. Magma reuses
identical `nf_run` (`magma/world/light.c` ~315-317, world_type==2).

## End: blaze CPU vs CUDA

| seed | non_air cells | diff_cells | first_diff | classes |
|------|---------------|------------|------------|---------|
| 0 | 777805 | 0 | - | {} |
| 2 | 744818 | 0 | - | {} |
| 3 | 716772 | 0 | - | {} |
| 7 | 524037 | 0 | - | {} |
| 9 | 618214 | 0 | - | {} |
| 10 | 670622 | 0 | - | {} |
| 19 | 711105 | 0 | - | {} |

## End: blaze CPU vs magma

Same seeds/cell counts; **all `diff_cells=0`**. Magma reuses identical
`cpe_provide_chunk` (`magma/world/light.c` ~319-321, world_type==3) with
CE_END_STONE -> vanilla 121.

## Live-sim tick probe (CPU vs CUDA hashes)

**SKIPPED with evidence.** Batched blaze env is overworld-only:

- File: `blaze/env/blaze_core.h` (comment at nether water-vaporize branch)
- Snippet: `dimension is always 0 here and id 51 edits are unreachable`
- No dimension-selection API on `VecBlaze`; cannot spawn N=256 envs in a nether
  region without sim changes (out of scope for this measurement lane).

## Ranked divergences

**None.** All compared regions are sparse-cell identical across backends for all
seven seeds. Seed-dependence: non_air counts vary (terrain/caves), but residual
diff is uniformly zero (no seed-dependent backend skew).

## Repro commands

```bash
export UV_CACHE_DIR=/home/infatoshi/.cache/uv
export TMPDIR=/home/infatoshi/dev/nw/.tmp
export CUDA_VISIBLE_DEVICES=0 SM=sm_120 MC_SM=sm_120

# Full census (builds dump tools, inject proof, all seeds)
bash verify/worldgen/nether_census.sh

# CPU-only
bash verify/worldgen/nether_census.sh --skip-cuda

# Single-seed manual
./verify/worldgen/dim_region_dump find-fortress 0 48
./verify/worldgen/dim_region_dump nether 0 -2 -2 4 4 -o /tmp/n_cpu.txt
flock /home/infatoshi/dev/nw/.tmp/gpu0.lock \
  ./verify/worldgen/dim_region_dump_cuda nether 0 -2 -2 4 4 -o /tmp/n_cuda.txt
magma/trace/world_dump --seed 0 --cx0 -2 --cz0 -2 --ncx 4 --ncz 4 \
  --world-type 2 --states --out /tmp/n_magma.crws
```

## Files added (this lane)

| File | Role |
|------|------|
| `verify/worldgen/nether_census.sh` | shell driver (gpu preflight, UV env) |
| `verify/worldgen/nether_census.py` | census + inject proof + live-sim skip |
| `verify/worldgen/dim_region_dump.c` | multi-chunk CPU dump (nf_run / cpe) |
| `verify/worldgen/dim_region_dump.cu` | multi-chunk CUDA dump twin |
| `verify/worldgen/NETHER_END_CENSUS.md` | this report |

Frozen files not modified: `wrapper_gate.sh`, `known_divergences.json`,
`pixel_gate.py`, `delegate_gate.sh`, `regression_pins.txt`,
`parity_manifest.json`, tape sidecars. No sim code changes.
