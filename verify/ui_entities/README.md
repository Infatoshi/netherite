# ui_entities — focused entity/particle geometry gates

Owner: entity-render path (`game/entity_render.*`, `item_render`, `frame_capture`,
`game_main`, shade/raster dissolve + additive blend).

## What these gates prove

Deterministic **geometry / UV / topology / blend-input** contracts transcribed
from Java 1.11.2. They are **not** framebuffer pixel gates and must never be
described as such.

| Gate | Contract |
|------|----------|
| `test_geom_gates.c` | slime/magma size + squish field, LayerSlimeGel living α=0.1, large vs small fireball fire extents (width 1.0 vs 0.3125), death-ray 9-vert fans, dissolve markers, portal particles.png + EXPLOSION explosion.png UVs |
| `test_entity_render.sh` | full entity model suite + fireball/rays/particles/dissolve cases |
| `test_item_render.sh` | billboard scales + fire overlay extents |

## Pixel gates (Java goldens)

Real MC 1.11.2 frames live under `goldens/` (never fabricate). Capture + hard
owned-pixel gate:

```bash
cd magma
bash ../verify/ui_entities/capture_ui_entities.sh   # llvmpipe, lock, A/B
bash ../verify/ui_entities/run_oracle_gate.sh       # frame_capture C vs Java
# mutation self-tests (after C frames exist under /tmp/magma_ui_entities_c):
uv run --no-project --with pillow --with numpy \
  python ../verify/ui_entities/test_ui_entities_mutations.py \
  --goldens ../verify/ui_entities/goldens \
  --c-frames /tmp/magma_ui_entities_c
```

See `ORACLE_CAPTURE.md` for state table and hard-gate policy: the complete
family ROI is owned, and PASS requires zero Java A/B across that ROI plus
`hard_px==0`. Nonzero A/B or xp-missing-orb is `CAPTURE_BLOCKED` (never
mid-envelope PASS). `RESIDUAL`/`CAPTURE_BLOCKED` are nonzero exit. C path is
`entity_oracle_candidate.c` through `gm_frame_capture_write` (CPU raster), not
a hand-painted candidate.

| Feature | Pixel gate | Notes |
|---------|------------|-------|
| Slime/magma size + LayerSlimeGel | `slime_*` / `magma_*` | entity_pin size/squish |
| Dragon death rays + explode | `dragon_death_{50,100,190}` | deathTicks pin; particles recon may residual |
| Dig ParticleDigging | `dig_stone` / `dig_grass` | entity_pin dig_hit |
| Small / dragon fireball | `fireball_*` | entity_pin |
| XP orb | `xp_orb` | entity_pin value/age/color |

### Chest blocker (precise)

Chest is **not fixed** on this branch. `mesh_mc` still meshes a static closed
ModelChest-ish box from the terrain atlas (oak-plank stand-in). A real fix needs:

1. Pack `entity/chest/normal.png` into an atlas (terrain or entity).
2. Per-frame TESR remesh of lid hinge from `ChestLive.te.lid_angle` in both
   `game_main` and `frame_capture` (chunk mesh is static; TE angle already ticks).

Leave chest code as-is until that path lands; do not treat closed proportions
as a pixel-gated chest fix.

## Run

```bash
export MC_JAR=.../minecraft-1.11.2.jar   # if not in gradle cache
cd magma
bash game/test_entity_render.sh
bash game/test_item_render.sh
bash ../verify/ui_entities/run_gates.sh
make -C . game
```
