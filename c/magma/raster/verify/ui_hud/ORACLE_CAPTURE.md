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

**Gate verdicts:** `PASS` = hard HUD C-vs-J within noise+margin (parity claim).
`RESIDUAL` = hard capture OK but C residual (**nonzero exit**, no parity claim).
`CAPTURE_OK` = soft state capture integrity only. `FAIL` = missing/noise/empty.
Gray C backdrop is composition isolation only — not a live-world claim.

## Required captures (missing evidence)

| ID | State | How to produce | Region of interest |
|----|--------|----------------|--------------------|
| `hud_armor_iron.png` | Full iron armor (15 pts), full hearts/food | `/give @p iron_*` full set; stand still, clear sky | Bottom-left: armor row at GUI y=`sh-49` + hearts at `sh-39` |
| `hud_absorption_armor.png` | Absorption 20 (golden apples) + armor | `/effect @p absorption 30 4` with iron set | Armor lifted by second heart row |
| `hud_hurt_flash_on.png` / `hud_hurt_flash_off.png` | Health just dropped, `healthUpdateCounter` blink | Summon zombie, take 1 hit; capture two consecutive client ticks during the 20-tick flash window | Hearts row only |
| `hud_hunger_poison.png` | Food 8 + HUNGER potion | `/effect @p hunger 30 0` | Hunger haunches (right of hotbar) |
| `hud_air_partial.png` | Eye in water, air ~123 | Glass pool, submerge ~9s | Bubbles at `sh-49` right |
| `hud_xp_half.png` | `experience=0.5`, level 7 | `/xp` to known fraction | XP bar fill width = 91/182 GUI px + level outline text centered `(sw-w)/2` |
| `hud_durability_half.png` | Wood pick damage 30/59 in hotbar slot 0 | `/give` + anvil or scripted damage | Slot 0 durability strip (13x2 at icon +2,+13) |
| `hud_boss_half.png` | Ender dragon bar at 50% | End fight or boss bar packet | Top center pink bar + "Ender Dragon" |
| `hud_death.png` | Dead player, deaths≥1 | Die to mob; hold death screen | Full-frame red wash + banner |
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

- **Hard HUD (closed at A/B noise floor):** `hud_hurt_flash_on/off` (flash phase
  sprites + last-health white hearts), `hud_air_partial` (pixels correspond to
  air 121–122: four full + one partial), and `hud_durability_half` (exact 13x2
  feature ROI).
- **Hand viewmodels (capture closed):** `hand_bow_pull20` / `hand_eat_mid` /
  `hand_block_shield` Java A/B frames now show distinct lower-right viewmodels
  (bow drawn / bread mid-eat / shield block). Root cause of wall-only goldens
  was Malmo `hideGUI=true` (F1) suppressing `ItemRenderer` while `frame{}`
  still force-painted HUD. Fix: `frame{}` + `hud_pin` clear `hideGUI` and
  pin equip/`itemStackMainHand`/active use. Presence rejects empty-hand
  baseline clones and cross-state-identical ROIs. Hard C residual remains
  (registration/lighting; bow ~55, eat ~46, shield ~50 at A/B noise 0) —
  no budgets/masking; RESIDUAL nonzero, not parity.
- **Drawn-bow registration (#29):** geometry follows ItemRenderer BOW branch at
  partialTicks=1; still-draw A/B against genuine `hand_bow_pull20.png` now
  available for further tuning (C residual ~55/ch on non-hotbar ROI).
- **Inside-block live path:** `gm_overlay_block_in_hand_live` uses
  `Block.causesSuffocation` + INVISIBLE skip, BlockModelShapes particle texture
  (chest/missing-model fallbacks), and maxU-on-left UV; pixel proof still needs
  `overlay_inside_stone.png` / `overlay_inside_grass.png`.
- **Low-health heart jitter:** vanilla `rand(updateCounter*312871)` not taped;
  numerical gates keep the stable baseline deliberately.
- **Absorption gold hearts:** row/armor placement includes
  `GmPlayerView.absorption`; yellow heart sprites are not yet drawn (layout only).

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
