# ui_entities — focused entity/particle geometry gates

Owner: entity-render path (`game/entity_render.*`, `frame_capture`, `mesh_mc`).

## What these gates prove

Deterministic **geometry / UV / scale** contracts transcribed from Java 1.11.2
oracle models/renderers. They do **not** loosen tape pixel thresholds or invent
frame goldens.

| Gate | Contract |
|------|----------|
| `test_geom_gates.c` | slime/magma size ratios, large vs small fireball scale patch, death-ray count at deathTicks=100, enderman portal particle count |
| `test_entity_render.sh` | full entity model suite (parts/UV/winding + new I/J cases) |

## Pixel capture blocker

Oracle-backed **pixel** gates for particles and chest TESR are blocked until:

1. **`particles.png`** is packed into a magma atlas (terrain/item/mob). Current
   particle emit uses stand-in UVs on the enderman/dragon skin so geometry is
   testable without a fabricated golden.
2. **`entity/chest/normal.png`** is in the terrain atlas. `mesh_mc` now emits
   ModelChest closed proportions (body 10 + lid 4 + facing knob) but still
   samples oak planks (`PB_CHEST` / `build_atlas.py` out of scope here).
3. **Animated lid** needs per-frame TESR remesh from `ChestLive.te.lid_angle`
   (chunk mesh is static; TE angle already ticks in sim).

Capture recipe (when atlas + live client available):

```bash
# 1. Summon focused scenes on the live 1.11.2 client (qrl :25575)
# 2. Pose pin + frames_every=1 tape
# 3. Diff magma frame_capture PPMs against oracle frames in this directory
#    (never invent goldens offline)
```

## Run

```bash
cd c/magma
bash game/test_entity_render.sh
bash raster/verify/ui_entities/run_gates.sh
make -C . game   # interactive binary links the same entity_render.o
```
