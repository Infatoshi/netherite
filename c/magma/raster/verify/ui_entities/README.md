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

## Pixel gates (status)

Honest status: **no Java frame goldens** for these interactive paths on this
branch. Geometry/topology/UV gates pass; pixel match vs live MC remains open.

| Feature | Pixel gate | Notes |
|---------|------------|-------|
| Fireball fire overlay extents | geometry only | Needs a live fireball frame golden |
| Dragon death rays | topology + RH rotate + blend inputs | Needs death-window Java frames |
| Dragon dissolve (per-texel) | marker + shade path | Pixel gate needs End death frames |
| Portal (particles.png) | UV + count recon | **UNVERIFIED** vs Java pixels; not a live Particle list |
| EXPLOSION_LARGE/HUGE | explosion.png 4x4, life 6+nextInt(4), size/color, no motion | Geometry/UV transcribed from Java; recon not a live FXLayer-3 list; **UNVERIFIED** pixels |
| Dig ParticleDigging | model particle icon + hit-face spawn/scale | Progress-stage recon only (no per-tick age stream); **UNVERIFIED** pixels |
| LayerSlimeGel | geometry + tint.a=255 + blend=4 + α_ref=0.1 | **UNVERIFIED** vs Java pixels |
| Chest TESR lid | **not claimed** | See blocker below |

### Capture commands (when qrl client + oracle frames available)

```bash
# 1. Live client on :25575 with focused scene (summon enderman / kill dragon / dig)
# 2. Pose-pin tape with frames_every=1
# 3. Diff magma frame_capture PPMs against oracle frames — never invent goldens
cd c/magma
# example: route e2e already records End death; re-gate with:
#   uv run --no-project python raster/verify/trace/regate.py --tape ... --npy out/.../magma_frames.npy
```

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
cd c/magma
bash game/test_entity_render.sh
bash game/test_item_render.sh
bash raster/verify/ui_entities/run_gates.sh
make -C . game
```
