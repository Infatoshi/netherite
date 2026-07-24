# ui_entities — focused entity/particle geometry gates

Owner: entity-render path (`game/entity_render.*`, `item_render`, `frame_capture`,
`game_main`, shade/raster dissolve + additive blend).

## What these gates prove

Deterministic **geometry / UV / topology / blend-input** contracts transcribed
from Java 1.11.2. They are **not** framebuffer pixel gates and must never be
described as such.

| Gate | Contract |
|------|----------|
| `test_geom_gates.c` | slime/magma size + squish field, LayerSlimeGel, large vs small fireball fire extents (width 1.0 vs 0.3125), death-ray 9-vert fans, dissolve markers, particles.png UVs |
| `test_entity_render.sh` | full entity model suite + fireball/rays/particles/dissolve cases |
| `test_item_render.sh` | billboard scales + fire overlay extents |

## Pixel gates (status)

| Feature | Pixel gate | Notes |
|---------|------------|-------|
| Fireball fire overlay extents | geometry only | Needs a live fireball frame golden |
| Dragon death rays | topology + blend inputs only | Needs death-window Java frames |
| Dragon dissolve (per-texel) | marker + shade path | Pixel gate needs End death frames |
| particles.png portal/dust | UV + counts only | **UNVERIFIED** vs Java pixels |
| LayerSlimeGel | geometry only | **UNVERIFIED** vs Java pixels |
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
