# Oracle capture instructions (ui_hud)

Focused gates: numerical formulas (`test_ui_hud_numerical.c`), end-to-end
frame composition (`test_ui_hud_compose.c`), live inventory/armor +
`overlay_live` 8-sample path (`test_ui_hud_live.c`), and unit scripts
`game/test_{hud,hand,overlay}.sh`. Composition proves live plumbing (armor
from equipped inventory via `GmPlayerView`, hand use poses via `gm_hand_draw`,
block-in-hand overlay order, absorption heart-row placement, XP centering)
but **does not claim pixel parity with Java**.

Full **pixel** gates against Java 1.11.2 frames need goldens that are **not**
in this tree (repo policy: do not fabricate goldens). Record them with a live
Forge 1.11.2 client at **854x480, GUI scale 2**, then drop PNGs under this
directory and wire a diff harness.

## Oracle frames (landed)

Java PNGs live under `goldens/` as `<id>_a.png` / `<id>_b.png` (twin captures
for the A/B noise floor). Capture: `bash raster/verify/ui_hud/capture_ui_hud.sh`
(uses qrl `hud_pin` + `frame` at partialTicks=1, llvmpipe, lock
`/tmp/qrl_25575.lock`). ROI compare: `compare_ui_hud_oracle.py` via
`run_ui_hud_gates.sh` when goldens are present.

**Capture integrity (enforced):** every state starts from an asserted clean
living player; `base_scene` fails on any server-command failure; `clear_effects`
runs before requested effects; death uses real `respawnPlayer` + close
`GuiGameOver`; A/B noise ceilings are tight (no 40 loophole); feature-presence
checks cover death / shield / bow / eat / fire / inside-block / portal /
underwater. Contaminated legacy `hand_block_sword` is rejected (1.11.2 blocks
with **shield** item 442).

**Gate verdicts:** `PASS` = hard C parity claim. `RESIDUAL` = hard capture OK but
C residual (**nonzero exit**, no parity claim). `CAPTURE_OK` = soft state capture
integrity only (portal / fire / underwater / death). `FAIL` = missing/noise/empty.
Gray C backdrop is composition isolation only — not a live-world claim.

**Inside-block fullscreen hard gate** (`overlay_inside_stone` /
`overlay_inside_grass`): blend-off full-frame particle replace, so the compare is
**strict full ROI on A/B-stable pixels** (Java HUD flicker excluded), not a
painted-only mean. Explicit A/B noise (mean + max). `hard_thr = ceil(noise_max)`
on the stable set: when `noise_max==0` every stable pixel must be **exact**
(thr 0; any maxch>0 is residual). **No** thr floor of 1 (that allowed a +1
single-channel mutation to pass). **No** diluted mean sole gate. Mutation suite
(`test_ui_hud_mutations.py`) rejects erase-90%, blank-to-one, **+1 single-channel**,
x/y shifts 2–4 px, recolor +20, and sparse extra pixels. Overlays are not
marked PASS until hard_px==0.

## Required captures (missing evidence)

| ID | State | How to produce | Region of interest |
|----|--------|----------------|--------------------|
| `hud_armor_iron.png` | Full iron armor (15 pts), full hearts/food | `/give @p iron_*` full set; stand still, clear sky | Bottom-left: armor row at GUI y=`sh-49` + hearts at `sh-39` |
| `hud_absorption_armor.png` | Absorption 20 (golden apples) + armor | `/effect @p absorption 30 4` with iron set | Armor lifted by second heart row |
| `hud_hurt_flash_on.png` / `hud_hurt_flash_off.png` | Health just dropped, `healthUpdateCounter` blink | Summon zombie, take 1 hit; capture two consecutive client ticks during the 20-tick flash window | Hearts row only |
| `hud_hunger_poison.png` | Food 8 + HUNGER potion | `/effect @p hunger 30 0` | Hunger haunches (right of hotbar) |
| `hud_air_partial.png` | Eye in water; **pin/reply air=123** (historical freeze) but **pixels** are 4 full + 1 partial ⇒ `effective_pixel_air_range=121..122` | Glass pool; future recapture pin air=121 | Bubbles at `sh-49` right |
| `hud_xp_half.png` | `experience=0.5`, level 7 | `/xp` to known fraction | XP bar fill width = 91/182 GUI px + level outline text centered `(sw-w)/2` |
| `hud_durability_half.png` | Wood pick damage 30/59 in hotbar slot 0 | `/give` + anvil or scripted damage | Slot 0 durability strip only (13x2 at icon +2,+13) |
| `hud_boss_half.png` | Ender dragon bar at 50% | End fight or boss bar packet | Top center pink bar + "Ender Dragon" |
| `hud_death.png` | Dead player, GuiGameOver | Die to mob; hold death screen | Opaque chrome: "You died!" + Score (body+shadow) + Respawn/Title buttons; full-frame gradient/world soft residual only |
| `hand_bow_pull20.png` | Bow drawn 20 ticks, fixed yaw/pitch 0, wall backdrop | Hold use 20 ticks against plain wall | Lower-right viewmodel |
| `hand_eat_mid.png` | Bread, use remaining 16/32 | Hold right-click mid-eat | Lower-right viewmodel |
| `hand_block_shield.png` | Shield blocking (1.11.2; swords do not block) | Hold right-click with shield (id 442) | Lower-right viewmodel |
| `overlay_inside_stone.png` | Eye inside solid stone | `/tp` into stone (suffocation) | Full frame near-black **particle** texture, U mirrored (maxU left) |
| `overlay_inside_grass.png` | Eye inside grass (particle=dirt not top) | `/tp` into grass block | Full frame dirt particle darken |
| `overlay_portal_050.png` | `timeInPortal=0.5` | Stand in portal ~10 ticks | Full-frame portal swirl alpha |
| `overlay_fire.png` | Player on fire | Lava edge / flame | First-person fire quads |
| `overlay_underwater.png` | Fully submerged, yaw 0 pitch 0 | Glass pool floor | Full-frame underwater.png |

## Capture recipe (mcwindow / qrl)

Pinned profile matches `raster/verify/mc_capture/capture_gui.sh`:

```text
resolution 854x480
options: guiScale 2, fancy graphics, view distance 8, bob off if possible
partialTicks at tick boundary (recorder already does this)
```

Example qrl + mcwindow sketch for armor + hurt flash:

```text
# qrl setup
/gamemode 0
/give @p minecraft:iron_helmet 1
/give @p minecraft:iron_chestplate 1
/give @p minecraft:iron_leggings 1
/give @p minecraft:iron_boots 1
# equip via inventory clicks, then:
# mcwindow: wait 40; screenshot hud_armor_iron.png
# summon zombie in a 1x2 cell; wait until hurt; screenshot two frames 3 ticks apart
```

## Known open pixel residuals (do not mask)

- **Hard core HUD (oracle∪C complete feature masks, A/B noise floor):**
  `hud_armor_iron`, `hud_absorption_armor` (gold abs hearts + lifted armor),
  `hud_hurt_flash_on/off`, `hud_hunger_poison`, `hud_air_partial` (air 121–122:
  four full + one partial), `hud_xp_half`, `hud_boss_half` (bar + name chrome).
  Gate scores Java∪C feature masks with `n_hard_px==0` (HARD_THR=2); no painted-
  only holes, no noise/mean budget for parity.
- **Hard HUD RESIDUAL (truthful; do not weaken masks):**
  - `hud_durability_half`: full owned icon + 13x2 strip; strip/fill match but
    wood-pick icon pixels still diverge vs Java (icon mean residual) — no
    black-underlay-only pass. Production durability atlas-alpha ownership is a
    separate stack (not this merge).
- **GuiGameOver chrome closed; full-frame world tint open:** hard feature ROIs —
  `hud_death_title` / `hud_death_score` use oracle-derived body plus vanilla
  drop-shadow color classes and the Java+C union so missing and extra pixels
  fail. `hud_death_btn_*` compares full button rectangles. The hard
  `hud_death_tint_pair` source-model gate checks pure gradient bands over the
  known gray underlay against 1.11.2 `Gui.drawGradientRect` blend math.
  Full-frame `hud_death` remains soft at ~33 C-vs-J because the Java frame has
  a live stone-pad underlay; same-scene parity needs a world-only companion
  capture. Mutation tests cover missing button faces/shadows, shifted/extra
  glyphs, and a pure-band tint wipe.
- **Hand viewmodels (capture closed):** `hand_bow_pull20` / `hand_eat_mid` /
  `hand_block_shield` Java A/B frames now show distinct lower-right viewmodels
  (bow drawn / bread mid-eat / shield block). Root cause of wall-only goldens
  was Malmo `hideGUI=true` (F1) suppressing `ItemRenderer` while `frame{}`
  still force-painted HUD. Fix: `frame{}` + `hud_pin` clear `hideGUI` and
  pin equip/`itemStackMainHand`/active use. Presence rejects empty-hand
  baseline clones and cross-state-identical ROIs. Hard C residual remains
  (registration/lighting; no budgets/masking; RESIDUAL nonzero, not parity).
- **Viewmodel ports in flight:** bread (297) is in the item atlas (was silent
  iron_ingot fallback). Shield keeps native 64x64 `shield_base_nopattern` and
  ModelBox box-net UVs + RenderHelper diffuse only (no block face shades).
  Mid-eat ROI is the wider lower band so residual is not a false PASS on
  lower-right edge crumbs. Remaining gap is first-person registration vs Java
  (C silhouettes larger/higher than the genuine tips) plus lighting nuance.
- **Drawn-bow registration (#29):** geometry follows ItemRenderer BOW branch at
  partialTicks=1; still-draw A/B against genuine `hand_bow_pull20.png` now
  available for further tuning (C residual ~55/ch on non-hotbar ROI).
- **Inside-block gate (exact bar):** `gm_overlay_block_in_hand` matches 1.11.2
  `ItemRenderer.renderBlockInHand`: view-space z=-0.5 under hand FOV 70,
  maxU/maxV on left/bottom (U mirrored), **blend off**, replace RGB with
  `round(tex * 0.1)`. Live path: `causesSuffocation` + INVISIBLE skip + particle
  sprite (grass→dirt). Candidate uses black world + real atlas particle UVs.
  Gate: full A/B-stable ROI, `hard_thr=ceil(noise_max)` (noise_max=0 => exact;
  any maxch>0 residual). Do **not** PASS until hard_px==0. Open 1-LSB residuals
  (measured under thr=0): stone hard_px is HUD-band only (non-HUD overlay exact);
  grass hard_px is widespread maxch=1 (dirt particle vs Java — floor(tex*0.1)
  makes stone worse, so not a global rounding flip; left open). Mutations must
  reject erase/blank/+1ch/shift/recolor/extra.
- **Underwater hard residual (improved, not noise-floor):** UV/blend/order
  match `renderWaterOverlayTexture` (4× tile, yaw/pitch/64, color(brightness,0.5),
  src-over, FOV 60). Candidate uses same-scene glass-pool ambient (fogged
  nearby stone, not gray isolation) and water-attenuated eye brightness
  (~light 10 → 1/3). Full-ROI hard gate (same exact hard_px bar as inside-block;
  no painted-vs-gray filter; no noise/mean budget to claim parity). Measured
  C-vs-J **15.25 → ~4.97**/ch on committed A/B. Remaining residual is
  non-uniform pool geometry / hand registration under the translucent overlay
  — needs a full mesh of the glass pool to close further. Air partial stays a
  separate hard HUD gate. Fire/portal left soft for animated-atlas. Mutations
  cover omission wipe and extra block (must not PASS).
- **Low-health heart jitter:** vanilla `rand(updateCounter*312871)` not taped;
  numerical gates keep the stable baseline deliberately.
- **Absorption gold hearts (closed):** `GuiIngame.renderPlayerStats` port —
  high→low icon order, gold full/half (icons.png U=160/169), blink underlay
  via last-health, armor drawn before health, row/gap lifts armor. Hard
  `hud_absorption_armor` at A/B noise with oracle∪C masks (exact C-vs-J).

## What is gated without goldens

`run_ui_hud_gates.sh` + `game/test_{hud,hand,overlay}.sh` cover:

- healthUpdateCounter blink phase
- XP fill columns, durability (incl. fishing rod), armor from `GmPlayerView`,
  boss half-fill, multi-row heart + absorption displacement of the armor row,
  XP level pixel centering via `(sw-w)/2`
- stack counts, death wash, hunger-poison sprite swap
- equip/swing/eat/block/bow viewmodel offsets (emit + `gm_hand_draw` path;
  eat mean change is non-vacuous)
- selection/crack geometry, portal alpha formula, block-in-hand darken + U mirror,
  loading full-frame fill, underwater constants
- end-to-end frame composition (hand + block overlay + HUD on one framebuffer)
- live: real inventory iron set -> `armor_points` + HUD; overlay_live 8-sample
  stone darken / leaves+barrier+chest no-op
