# Oracle capture instructions (ui_hud)

Focused gates: numerical formulas (`test_ui_hud_numerical.c`) plus end-to-end
frame composition (`test_ui_hud_compose.c`) and unit scripts
`game/test_{hud,hand,overlay}.sh`. Composition proves live plumbing (armor
from `GmPlayerView`, hand use poses via `gm_hand_draw`, block-in-hand overlay
order) but **does not claim pixel parity with Java**.

Full **pixel** gates against Java 1.11.2 frames need goldens that are **not**
in this tree (repo policy: do not fabricate goldens). Record them with a live
Forge 1.11.2 client at **854x480, GUI scale 2**, then drop PNGs under this
directory and wire a diff harness.

## Remaining oracle-frame blocker

No Java PNGs exist under `raster/verify/ui_hud/`. Until those captures land,
pixel parity vs 1.11.2 is **blocked**. Composition + formula gates are the
release bar for owned modules; they intentionally stop short of image A/B.

## Required captures (missing evidence)

| ID | State | How to produce | Region of interest |
|----|--------|----------------|--------------------|
| `hud_armor_iron.png` | Full iron armor (15 pts), full hearts/food | `/give @p iron_*` full set; stand still, clear sky | Bottom-left: armor row at GUI y=`sh-49` + hearts at `sh-39` |
| `hud_hurt_flash_on.png` / `hud_hurt_flash_off.png` | Health just dropped, `healthUpdateCounter` blink | Summon zombie, take 1 hit; capture two consecutive client ticks during the 20-tick flash window | Hearts row only |
| `hud_hunger_poison.png` | Food 8 + HUNGER potion | `/effect @p hunger 30 0` | Hunger haunches (right of hotbar) |
| `hud_air_partial.png` | Eye in water, air ~123 | Glass pool, submerge ~9s | Bubbles at `sh-49` right |
| `hud_xp_half.png` | `experience=0.5`, level 7 | `/xp` to known fraction | XP bar fill width = 91/182 GUI px + level outline text |
| `hud_durability_half.png` | Wood pick damage 30/59 in hotbar slot 0 | `/give` + anvil or scripted damage | Slot 0 durability strip (13x2 at icon +2,+13) |
| `hud_boss_half.png` | Ender dragon bar at 50% | End fight or boss bar packet | Top center pink bar + "Ender Dragon" |
| `hud_death.png` | Dead player, deaths≥1 | Die to mob; hold death screen | Full-frame red wash + banner |
| `hand_bow_pull20.png` | Bow drawn 20 ticks, fixed yaw/pitch 0, wall backdrop | Hold use 20 ticks against plain wall | Lower-right viewmodel |
| `hand_eat_mid.png` | Bread, use remaining 16/32 | Hold right-click mid-eat | Lower-right viewmodel |
| `hand_block_sword.png` | Iron sword blocking | Hold right-click | Lower-right viewmodel |
| `overlay_inside_stone.png` | Eye inside solid stone | `/tp` into stone (suffocation) | Full frame near-black block texture |
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

- **Drawn-bow registration (#29):** geometry follows ItemRenderer BOW branch at
  partialTicks=1; remaining 3–4/ch silhouette error needs still-draw A/B against
  `hand_bow_pull20.png` before further tuning.
- **Inside-block live path:** `gm_overlay_block_in_hand_live` samples the eye
  block and real atlas UVs on interactive + frame_capture paths; pixel proof
  still needs `overlay_inside_stone.png`.
- **Low-health heart jitter:** vanilla `rand(updateCounter*312871)` not taped;
  numerical gates keep the stable baseline deliberately.

## What is gated without goldens

`run_ui_hud_gates.sh` + `game/test_{hud,hand,overlay}.sh` cover:

- healthUpdateCounter blink phase
- XP fill columns, durability (incl. fishing rod), armor from `GmPlayerView`,
  boss half-fill, multi-row heart displacement of the armor row
- stack counts, death wash, hunger-poison sprite swap
- equip/swing/eat/block/bow viewmodel offsets (emit + `gm_hand_draw` path)
- selection/crack geometry, portal alpha formula, block-in-hand darken,
  loading full-frame fill, underwater constants
- end-to-end frame composition (hand + block overlay + HUD on one framebuffer)
