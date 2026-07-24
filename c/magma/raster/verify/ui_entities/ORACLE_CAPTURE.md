# Oracle capture (ui_entities)

Real Minecraft 1.11.2 frames for interactive entity paths. Never synthesize
PNGs. Geometry gates in `test_geom_gates.c` stay separate.

## States

| ID | Feature | How |
|----|---------|-----|
| `slime_size1` / `slime_size2` / `slime_size4` | EntitySlime getSlimeSize + LayerSlimeGel | `entity_pin` kind=slime size N squish=0 |
| `slime_squish` | client squishFactor=prev=amount=1.0 on size 2 | `entity_pin` + frame render pin |
| `magma_size1` / `magma_size2` / `magma_size4` | EntityMagmaCube size | `entity_pin` kind=magma_cube |
| `magma_squish` | client squishFactor=prev=amount=1.0 size 2 | `entity_pin` + frame render pin |
| `dragon_death_50` / `100` / `190` | client deathTicks; server stays alive | `entity_pin` + frame render pin |
| `dig_stone` / `dig_grass` | ParticleDigging hit spray | `entity_pin` dig_hit on stone/grass face UP |
| `fireball_small` | RenderFireball scale 0.3125 + fire | `entity_pin` small_fireball |
| `fireball_dragon` | RenderDragonFireball 2x | `entity_pin` dragon_fireball |
| `xp_orb` | RenderXPOrb billboard | `entity_pin` xp_orb value/age/color |

## Capture profile

```text
resolution 854x480, guiScale 2, bob off, clouds off, fancy off, RD 8
llvmpipe (LIBGL_ALWAYS_SOFTWARE=1), JAVA_HOME = system OpenJDK 8
exclusive /tmp/qrl_25575.lock
fresh flat seed-0 world; A/B via qrl frame{rerender:true} at partialTicks=1
```

```bash
cd c/magma
bash raster/verify/ui_entities/capture_ui_entities.sh
# then compare (builds C candidate through frame_capture):
bash raster/verify/ui_entities/run_oracle_gate.sh
```

## Capture order (required)

1. Rules + `difficulty easy` (not peaceful — slimes/magma cubes must not despawn).
2. `set_pose` onto the pad/camera and settle so server chunks load (flat spawn is
   far from origin).
3. `setblocks` pad / dig targets (never `/fill` into unloaded columns).
4. `entity_pin` + settle for client entity packets, then A/B `frame{rerender:true}`.
5. Dragon: pose to high air, place end-stone shelf, then pin death_ticks.
6. Render pin: slime/magma `squishFactor`/`prevSquishFactor`/`squishAmount` and
   dragon `deathTicks` are applied on the **client** entity (UUID/eid/pos/type
   match). `frame{}` re-applies the pending pin immediately before
   `renderWorld(1.0)` and restores afterward so no intervening client tick
   overwrites the freeze. Server dragon stays alive (health full, deathTicks=0)
   while client render state is 50/100/190.

Reuse the project MalmoMod jar only (no `qrl_bridge.jar` in mods). Preserve
existing non-empty goldens unless `FORCE_RECAPTURE=1`. Optional:
`ONLY_STATES="slime_squish magma_squish dragon_death_50"`.

## Gate policy

Feature-specific ROIs. Hard gate: `c_vs_j <= noise + MARGIN` with MARGIN at
the raster noise floor (default 0.5 mean abs RGB). Zero unexplained residual
budget outside noise. Presence assert: Java A ROI must differ from a blank
sky sample (feature not empty).

Post-capture, `validate_ui_entities_goldens.py` must PASS before any commit:
presence + A/B stability **and** inter-state geometry (squish taller/thinner
than size2, size pairs distinct, dragon stages pairwise distinct with
body/ray subject, reject near-empty 190). Approval is Pillow/numpy ROI stats,
never filesize alone. Never commit empty sky frames.

If C exactness is not reached, the ROI compare stays FAIL and prints clusters;
Java goldens remain usable as the oracle baseline when validation PASSes.
C-vs-J residual stays open until `run_oracle_gate.sh` is closed separately.
