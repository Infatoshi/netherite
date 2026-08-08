# ui_entities — focused entity/particle geometry gates

Owner: entity-render path (`game/entity_render.*`, `item_render`, `frame_capture`,
`game_main`, shade/raster dissolve + additive blend).

## What these gates prove

The three source-level tests below are deterministic **geometry / UV /
topology / blend-input** contracts transcribed from Java 1.11.2. They are
**not** framebuffer pixel gates; the separate real-Java lanes below are.

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
# strict Wither subject/layer gate with same-scene background controls:
ENTITY_GATE_WITHER_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# pixel-exact Ender Chest inventory panel (116720 owned pixels):
ENTITY_GATE_ENDER_CHEST_GUI_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# closed/open Ender Chest TESR, zero hard model pixels vs real Java:
ENTITY_GATE_ENDER_CHEST_WORLD_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# ordinary/trapped single/double Chest TESRs against six stable Java states:
ENTITY_GATE_CHEST_WORLD_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# Slime/Magma sizes 1/2/4 and a pinned nonuniform squish keyframe:
ENTITY_GATE_SLIME_MAGMA_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# Glowing outline post-process, exact opaque fixture plus Slime stress:
ENTITY_GATE_GLOWING_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# TNT minecart unprimed, two lit/dark fuse boundaries, scale, and flash:
ENTITY_GATE_MINECART_TNT_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# empty/chest/furnace/hopper/spawner/command cart models and display blocks:
ENTITY_GATE_MINECART_VARIANTS_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# all six Boat wood textures, idle paddles, and same-scene ownership:
ENTITY_GATE_BOAT_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# all four llama coats, all 16 decor layers, chest/child/gait, and spit:
ENTITY_GATE_LLAMA_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# Bat flying/hanging hierarchy, animation phase, and RenderBat offsets:
ENTITY_GATE_BAT_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# Squid swimming/dry rotations and live tentacle angle:
ENTITY_GATE_SQUID_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# Crafting, Anvil, Merchant, and Enchanting populated screens, pixel-exact:
ENTITY_GATE_STANDARD_CONTAINER_GUI_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# Enchanting alone, including the live seven-part book and generated runes:
ENTITY_GATE_ENCHANTING_GUI_ONLY=1 bash ../verify/ui_entities/run_oracle_gate.sh
# mutation self-tests (after C frames exist under /tmp/magma_ui_entities_c):
uv run --no-project --with pillow --with numpy \
  python ../verify/ui_entities/test_ui_entities_mutations.py \
  --goldens ../verify/ui_entities/goldens \
  --c-frames /tmp/magma_ui_entities_c
```

The Bat lane requires byte-stable Java A/B captures and exact subject
ownership in both poses. Flying permits no hard residual. Hanging permits the
single measured body-back/tail-top 24-bit depth-tie pixel above max-channel
25; subject erasure must still fail the negative control.

The Squid lane pins `renderYawOffset`, `squidPitch`, `squidYaw`, and
`tentacleAngle` at the Java render boundary. Both swimming and dry A/B pairs
are byte-stable. Dry has no hard residual; swimming permits one measured
internal-tentacle 24-bit depth-tie pixel, and an additional hard mutation must
fail.

See `ORACLE_CAPTURE.md` for state table and hard-gate policy: the complete
family ROI is owned, and PASS requires zero Java A/B across that ROI plus
`hard_px==0`. Nonzero A/B or xp-missing-orb is `CAPTURE_BLOCKED` (never
mid-envelope PASS). `RESIDUAL`/`CAPTURE_BLOCKED` are nonzero exit. C path is
`entity_oracle_candidate.c` through `gm_frame_capture_write` (CPU raster), not
a hand-painted candidate.

| Feature | Pixel gate | Notes |
|---------|------------|-------|
| Slime/magma size + LayerSlimeGel | `slime_*` / `magma_*` | exact Java A/B; pinned render yaw/partial tick; same-scene background subtraction; bounded fixed-function tail |
| Glowing outline | `magma_size2_glowing` / `slime_size2_glowing` | exact opaque final effect; cull-disabled Slime gel stress with classified bounded shading tail |
| Dragon death rays + explode | `dragon_death_{50,100,190}` | deathTicks pin; particles recon may residual |
| Dig ParticleDigging | `dig_stone` / `dig_grass` | entity_pin dig_hit |
| Small / dragon fireball | `fireball_*` | entity_pin |
| XP orb | `xp_orb` | entity_pin value/age/color |
| Squid model | `squid_{swim,dry}_pose` | exact RenderSquid transform chain and pinned tentacle angle; one bounded swimming depth tie |
| TNT minecart | `minecart_tnt_{fuse80_flash,fuse79_dark,fuse4_flash,fuse5_dark,unprimed}` | exact entity-id jitter, FaceBakery UV, quartic swell, opaque 1.11.2 flash |
| Other minecarts | `minecart_{empty,chest,furnace,hopper,spawner,command}_model` | exact cart shell; ModelChest transform; baked hopper elements/UVs; directional furnace/command faces |
| Boat variants | `boat_{oak,spruce,birch,jungle,acacia,darkoak}_model` | exact model ownership and idle paddle phase; seven bounded minification tie samples across all six textures |

### Chest status

Ender Chest uses `entity/chest/ender.png` in a per-frame TESR pass in both
`game_main` and `frame_capture`; its lid follows the live tile's interpolated
angle. Ordinary and trapped chests use their real single/double textures and
ModelChest geometry in the same pass. The focused wooden gate covers closed and
open single variants plus X- and Z-oriented doubles. Java A/B is exact; four
single views have no pixel over four channels, the X double has one bounded
pixel, and the Z double's two measured minified edge/texel ties are locked by
coordinate and value budgets.

## Run

```bash
export MC_JAR=.../minecraft-1.11.2.jar   # if not in gradle cache
cd magma
bash game/test_entity_render.sh
bash game/test_item_render.sh
bash ../verify/ui_entities/run_gates.sh
make -C . game
```
