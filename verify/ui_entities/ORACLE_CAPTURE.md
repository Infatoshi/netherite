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
| `xp_orb` | RenderXPOrb billboard | `entity_pin` xp_orb value/age/color + client render pin; `frame_pair` |

## Capture profile

```text
resolution 854x480, guiScale 2, bob off, clouds off, fancy off, RD 8
llvmpipe (LIBGL_ALWAYS_SOFTWARE=1), JAVA_HOME = system OpenJDK 8
exclusive /tmp/qrl_25575.lock
fresh flat seed-0 world; A/B via qrl frame{rerender:true} at partialTicks=1
```

```bash
cd magma
bash ../verify/ui_entities/capture_ui_entities.sh
# hard owned-pixel gate (builds C candidate through frame_capture):
bash ../verify/ui_entities/run_oracle_gate.sh
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

## Gate policy (hard full ROI)

Product parity is **max-channel exactness** across the complete family ROI with
**deterministic Java A/B zero**. Nonzero A/B is never a PASS.

The complete ROI is owned. Earlier color-derived subject masks were rejected
because they alternated between missing dim/dark/extra entity pixels and
owning grass, endstone, or pad. The interactive renderer must match the
composed scene, so those ROI pixels are part of the contract.

`xp_orb` has an additional presence prerequisite: the Java golden must visibly
contain the green/gold orb (pad-only frames are `CAPTURE_BLOCKED`). Capture
uses a client render pin (pose + `xpColor`/`xpOrbAge`/`xpValue` + noGravity)
and atomic `frame_pair` so A/B is same-state. Post-2026-07-24 recapture: orb
visible, full-frame A/B exact (`noise_max==0`); honest C is **RESIDUAL** until
`hard_px==0`.

### Verdicts

| Verdict | When |
|---------|------|
| **PASS** | Full ROI A/B `noise_max == 0`, subject presence valid, and `hard_px == 0` |
| **CAPTURE_BLOCKED** | Any ROI A/B pixel differs, or xp is missing its visible orb. **Never PASS**, even for `C == Java_A` or mid-envelope C. |
| **RESIDUAL** | Capture OK (zero A/B) but full-ROI `hard_px > 0` |
| **FAIL** | Missing files, candidate fail, or A/B mean noise over family ceiling |

Deleted: `owned_within_per_pixel_ab` PASS (per-pixel A/B envelope is not product
parity).

**Mutations** (`test_ui_entities_mutations.py`):

- Real-golden Java_A control: nonzero A/B → CAPTURE_BLOCKED; zero A/B + capable → PASS
- Synthetic zero-noise control (A=B=C painted subject) → PASS
- Corruptions of synthetic must not PASS (erase/blank/+1ch/missing dark/transparent/
  shift/recolor/extra black/midgray/midchroma/bright/vivid)
- Hole/extras counterexamples across the full ROI: dim fire, dark/purple/green
  fringes, outside black/midgray/midchroma/bright/vivid extras, xp no-orb
  blocked, and nonzero A/B controls blocked

Honest current C candidates are expected **RESIDUAL** or **CAPTURE_BLOCKED** on
all 16 until the renderer closes residual / recapture freezes A/B.

Post-capture, `validate_ui_entities_goldens.py` must PASS before any commit:
presence + A/B stability **and** inter-state geometry (squish taller/thinner
than size2, size pairs distinct, dragon stages pairwise distinct with
body/ray subject, reject near-empty 190). Approval is Pillow/numpy ROI stats,
never filesize alone. Never commit empty sky frames.

Do **not** modify production renderer paths or replace genuine Java goldens to
close this gate. C-vs-J residual stays open until `run_oracle_gate.sh` reports
PASS with `hard_px==0` and zero A/B for real.
