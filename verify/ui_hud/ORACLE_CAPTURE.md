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
for the A/B noise floor). Capture: `bash ../verify/ui_hud/capture_ui_hud.sh`
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

**Gate verdicts:** `PASS` = hard C parity claim only when `noise_max==0` and
`hard_px==0` (bit-exact C vs Java_a on A/B-stable ROI). `CAPTURE_BLOCKED` =
A/B stable maxch residual > 0 (**nonzero exit**; **no** C may PASS, including
C=Java_a / Java_b / midpoint / Java_a+1). `RESIDUAL` = A/B bit-exact but C
residual (**nonzero exit**). `CAPTURE_OK` = soft state capture integrity only
(fire / underwater / death). `FAIL` = missing/noise/empty/unstable.
Gray C backdrop is composition isolation only — not a live-world claim.
Portal is hard full-frame A/B-stable hard_px (not soft CAPTURE_OK).
**Never** use `ceil(noise_max)` as a PASS tolerance.

**Inside-block fullscreen hard gate** (`overlay_inside_stone` /
`overlay_inside_grass`): blend-off full-frame particle replace, so the compare is
**strict full ROI on A/B-stable pixels** (Java HUD flicker excluded), not a
painted-only mean. Explicit A/B noise (mean + max). `hard_thr` is always **0**
(bit-exact C vs Java_a). PASS only if `noise_max==0` AND `hard_px==0`.
**No** `ceil(noise_max)` tolerance (that allowed C=Java_a / Java_a+1 to PASS
when A/B still had maxch=1). **No** diluted mean sole gate. Mutation suite
(`test_ui_hud_mutations.py`) rejects erase-90%, blank-to-one, **+1 single-channel**,
x/y shifts 2–4 px, recolor +20, and sparse extra pixels; plus explicit
controls: synthetic zero-noise PASS, real portal A/B CAPTURE_BLOCKED under
any C, Java_a+1 blocked.

## Required captures (missing evidence)

| ID | State | How to produce | Region of interest |
|----|--------|----------------|--------------------|
| `hud_armor_iron.png` | Full iron armor (15 pts), full hearts/food | `/give @p iron_*` full set; stand still, clear sky | Bottom-left: armor row at GUI y=`sh-49` + hearts at `sh-39` |
| `hud_absorption_armor.png` | Absorption 20 (golden apples) + armor | `/effect @p absorption 30 4` with iron set | Armor lifted by second heart row |
| `hud_hurt_flash_on.png` / `hud_hurt_flash_off.png` | Health just dropped, `healthUpdateCounter` blink | Summon zombie, take 1 hit; capture two consecutive client ticks during the 20-tick flash window | Hearts row only |
| `hud_hunger_poison.png` | Food 8 + HUNGER potion | `/effect @p hunger 30 0` | Hunger haunches (right of hotbar) |
| `hud_air_partial.png` | Eye in water; **pin/reply air=123** (historical freeze) but **pixels** are 4 full + 1 partial ⇒ `effective_pixel_air_range=121..122` | Glass pool; future recapture pin air=121 | Bubbles at `sh-49` right |
| `hud_xp_half.png` | `experience=0.5`, level 7 | `/xp` to known fraction | XP bar fill width = 91/182 GUI px + level outline text centered `(sw-w)/2` |
| `hud_durability_half.png` | Wood pick damage 30/59 in hotbar slot 0 | `/give` + anvil or scripted damage | Slot 0 full owned icon (16x16) + 13x2 durability strip |
| `hud_boss_half.png` | Ender dragon bar at 50% | End fight or boss bar packet | Top center pink bar + "Ender Dragon" |
| `hud_death.png` | Dead player, GuiGameOver | Die to mob; hold death screen | Opaque chrome: "You died!" + Score (body+shadow) + Respawn/Title buttons; full-frame gradient/world soft residual only |
| `hand_bow_pull20.png` | Bow drawn 20 ticks, fixed yaw/pitch 0, wall backdrop | Hold use 20 ticks against plain wall | Lower-right viewmodel |
| `hand_eat_mid.png` | Bread, use remaining 16/32 | Hold right-click mid-eat | Lower-right viewmodel |
| `hand_block_shield.png` | Shield blocking (1.11.2; swords do not block) | Hold right-click with shield (id 442) | Lower-right viewmodel |
| `overlay_inside_stone.png` | Eye inside solid stone | `/tp` into stone (suffocation) | Full frame near-black **particle** texture, U mirrored (maxU left) |
| `overlay_inside_grass.png` | Eye inside grass (particle=dirt not top) | `/tp` into grass block | Full frame dirt particle darken |
| `overlay_portal_050.png` | `timeInPortal=0.5`, `portal_phase=0` | Outdoor pad; texture anim pinned | Full-frame portal swirl (hard_px) |
| `overlay_fire.png` | Player on fire | Lava edge / flame | First-person fire quads |
| `overlay_underwater.png` | Fully submerged, yaw 0 pitch 0 | Glass pool floor | Full-frame underwater.png |

## Capture recipe (mcwindow / qrl)

Pinned profile matches `../verify/mc_capture/capture_gui.sh`:

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
  four full + one partial), `hud_xp_half`, `hud_durability_half` (wood-pick
  flat GUI blit + exact 13x2 strip/fill; every opaque `items/wood_pickaxe.png`
  texel and the strip bit-match Java/PNG; local atlas-alpha ownership + forced
  strip + C-extra = unowned icon pixels not equal to exact HUD_HOTBAR-over-GRAY
  isolation underlay — not thr surgery, not mx/chroma threshold holes, not a
  global `painted_full` drop, not Java world underlay. Widgets hotbar alpha
  composition vs Java world is isolation-only),
  `hud_boss_half` (bar + name chrome).
  Gate scores Java∪C feature masks with `n_hard_px==0` (HARD_THR=2); no painted-
  only holes, no noise/mean budget for parity. Durability PASS also requires
  colored strip fill (black underlay alone fails).
- **Composition note (not a durability residual):** slot-0 hotbar underlay under
  transparent wood-pick texels differs C gray isolation vs Java world; excluded
  only when C matches the exact predicted isolation underlay color per texel.
  Counterexamples that must RESIDUAL: mid-gray `(60,60,60)`, mid-chroma
  `(45,20,20)`, every underlay channel ±1, misplaced underlay RGB, missing/
  shift/recolor body or strip.
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
- **Hand viewmodels (use-pose pin; exact gate):** `hand_bow_pull20` /
  `hand_eat_mid` / `hand_block_shield` require sticky full-use geometry
  (drawn bow / mid-eat / shield block), not idle rest tips. Sticky pin bugs
  fixed in `hud_pin` + `frame{}` (MAIN_HAND only):
  1. `processKeyBinds` wiped `setActiveHand` between pin and re-render → sticky
     `pinUseActive` re-applied at ClientTickEvent END and before `frame{}`
     `renderWorld`, use-key held.
  2. `activeItemStack` / `itemStackMainHand` must be the **same inventory
     reference** as the hotbar stack (`.copy()` breaks `==` predicates).
  Diagnostics: `use_branch`, `model_pulling` / `model_pull` / `model_blocking`,
  `stack_id_eq`, `ir_id_eq`. Driver: `capture_ui_hud_driver.py --self-test-hand-use`.
  **Gate:** complete Java∪C subject ownership on the lower-band ROI (subject =
  maxch distance from per-image ROI-border backdrop > thr; C isolation uses
  GRAY, Java uses wall median). `hard_thr` always **0**. PASS only if
  `noise_max==0` AND `hard_px==0`. No mean budget, no legacy `hard_parity`
  label, no painted-only holes. A/B maxch residual ⇒ CAPTURE_BLOCKED.
  Mutations (from a true bit-exact synthetic control): missing Java-only
  silhouette, C-extra pixels, +1 single-channel, shift, recolor — all reject.
- **Shield golden corrected; C parity RESIDUAL:** sticky-pin full-use
  `hand_block_shield` A/B (`model_blocking=1.0`, A/B noise 0, CAPTURE_OK)
  replaces the mislabeled idle-tip golden. Against the corrected golden,
  C-painted residual mean **1.563** with **15,989/16,737** nonzero and maxch
  **60** is **OPEN** (not PASS). Owned-subject hard residual under thr=0 also
  nonzero (gray isolation vs wall + hand). Do not claim pixel-perfect.
- **OPEN bow/eat goldens:** committed `hand_bow_pull20` / `hand_eat_mid` still
  idle-tip capture blockers/residual (no stable full-use PNG; bow sticky meta
  CAPTURE_FAIL). C use path source-correct; do not fit offsets to tip goldens.
  Recapture only.
- **Viewmodel ports:** bread (297) in item atlas; shield native 64x64
  `shield_base_nopattern` + ModelBox UV + RenderHelper diffuse; mid-eat ROI is
  the wider lower band.
- **Inside-block gate (exact bar):** `gm_overlay_block_in_hand` matches 1.11.2
  `ItemRenderer.renderBlockInHand`: view-space z=-0.5 under hand FOV 70,
  maxU/maxV on left/bottom (U mirrored), **blend off**, replace RGB with
  `round(tex * 0.1)`. Live path: `causesSuffocation` + INVISIBLE skip + particle
  sprite (grass→dirt). Candidate uses black world + real atlas particle UVs.
  Gate: full A/B-stable ROI, `hard_thr=0` (bit-exact). PASS only if
  `noise_max==0` and hard_px==0. Never `ceil(noise_max)` as PASS tolerance.
  Body: banker's `rintf(tex*0.1)` (c8c9a68). HUD-band 1-LSB closed by textured
  GUI src-over as separate round then add (`hud_blend_px_tex`); solid fills
  (death gradient) keep fused `(s*a+d*ia+127)/255`. Stone/grass hard_px=0.
  Mutations must reject erase/blank/+1ch/shift/recolor/extra.
- **Portal CAPTURE_BLOCKED (source path, not black-fit):** `GuiIngame.renderPortal`
  (not ItemRenderer) is a full-screen blocks-atlas portal sprite with
  fourth-power alpha (`t^4*0.8+0.2` at `timeInPortal=0.5` → 0.25),
  SRC_ALPHA blend, depth off, drawn in the GUI pass after world+hand.
  Nausea skips the texture (camera warp only, rate 7 vs 20). C
  `gm_overlay_portal_screen` matches the alpha/blend; candidate pins
  `portal_frame=0` via `bm_atlas_set_portal_frame`. Camera warp
  (`EntityRenderer.setupCameraTransform` on axis (0,1,1) from
  `rendererUpdateCount`) is frozen with sticky `portal_phase` on
  `hud_pin`/`frame{}`. Sticky `timeInPortal` is also re-applied at
  `frame{}` (free-running ticks decay it when not in a portal block).
  Drivers use atomic `frame_pair` (two re-renders, one client turn, shared
  nanoTime). Hard gate: full A/B-stable ROI, thr=0, Java∪C owned = full
  portal feature. **Reproducible blocker (2026-07-24):** even with sticky
  time+phase, pin_texture_animations, and atomic `frame_pair` under
  llvmpipe, A/B still shows maxch=1 residuals (measured ~6k px on a fresh
  pair; accepted golden still ~865 maxch=1). Product remains
  **CAPTURE_BLOCKED** (never PASS under any C: Java_a, Java_b, midpoint,
  Java_a+1). Do not replace accepted goldens with a noisier pair. **No**
  fitted black cell, **no** color-only purple masks, **no** mean budgets,
  **no** ceil(noise_max) PASS tolerance. Outdoor Java underlay vs gray C
  isolation under translucent alpha is the honest composition residual.
  Mutations + controls: synthetic zero-noise PASS; real A/B blocked; erase/
  blank/+1/shift/recolor/extra black-midgray-midchroma-bright.
- **Underwater hard residual (improved, not noise-floor):** UV/blend/order
  match `renderWaterOverlayTexture` (4× tile, yaw/pitch/64, color(brightness,0.5),
  src-over, FOV 60). Candidate uses same-scene glass-pool ambient (fogged
  nearby stone, not gray isolation) and water-attenuated eye brightness
  (~light 10 → 1/3). Full-ROI hard gate (same exact hard_px bar as inside-block;
  no painted-vs-gray filter; no noise/mean budget to claim parity). Measured
  C-vs-J **15.25 → ~4.97**/ch on committed A/B. Remaining residual is
  non-uniform pool geometry / hand registration under the translucent overlay
  — needs a full mesh of the glass pool to close further. Air partial stays a
  separate hard HUD gate. Fire left soft for animated-atlas. Mutations
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
