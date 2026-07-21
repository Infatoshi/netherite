# ROUTE.md - scripted seed-0 route: spawn to dragon kill, verified in magma

Goal: one scripted, repeatable playthrough of the REAL game (oracle, qrl bridge,
seed 0) covering every mechanic class from spawn to the ender dragon dying,
taped segment by segment, each segment replayed through magma and
pixel-matched before the next is added. Deliverable: side-by-side MP4 of the
dragon death (oracle vs magma).

Design rules (from progression_bot.py):
- Cheats allowed for STAGING (fill arenas, give gear, tp aim pins); everything
  the tape replays must be organic inputs (movement, block place/break, portal
  transits, bow draws, item throws).
- The qrl action space has no GUI clicks, so crafting-table/inventory GUIs are
  out of scope; "crafting" milestones are covered by /give + the world-side
  mechanics they gate (tool use, block place). Everything else is playable.
- One segment = one tape = one report. A segment is DONE when physics is clean
  at 1e-9 and pixels have no unexplained cluster diffs (sky-tolerance policy).

## Milestone ladder

| # | segment    | mechanics exercised                                   | status |
|---|-----------|--------------------------------------------------------|--------|
| 1 | overworld | walk/sprint/jump/strafe/look at spawn terrain           | DONE (tapes 0921/0928) |
| 2 | mine      | hold-to-break: dig logs + stone staircase, drops, pickup | DONE (120328Z, div 32/33) |
| 3 | build     | block placement: bridge forward, pillar-up tower         | DONE (124101Z, div 35/36, euclid 0) |
| 4 | nether    | portal light + natural transit + x8 return               | DONE (1017/1029) |
| 5 | pearl     | ender pearl throw + teleport, eye of ender flight        | DONE (130300Z, euclid 0) |
| 6 | endportal | end_portal pad entry, End island arrival                 | DONE (1017 route) |
| 7 | crystal   | End: pillar-up tower, bow an end crystal until it blows  | DONE (134124Z: staged pad, 1-shot kill; physics clean, pre-blast 0.46-1.0/ch; fixed div 37/38/39, filed 40-42) |
| 8 | dragon    | velocity-lead bow loop until dragon health<=0, death anim | DONE (144207Z: 32651 ticks, kill at t32129, deathTicks 0->196; physics CLEAN at 1e-9 incl hp; fight pixels 2.5-4.5/ch, death window dominated by oracle-only particles - div 40/46/47) |
| 9 | e2e       | run 1-8 back to back on one save, single tape            | DONE (175629Z: 20045 ticks, spawn -> mine -> build -> pearl -> nether roundtrip -> organic End entry -> crystal 1-shot -> dragon kill at t19555 (hp 0, 130-tick death anim, XP shower to t19684); replay full-length, euclid 0, first div 0.0235 self-healing) |
|10 | mp4       | side-by-side oracle/magma MP4 of the dragon death      | DONE (dragon_death_sbs.mp4 t31700-32651 + dragon_fight_sbs.mp4 t29000+, scp'd to macbook:~/Downloads 2026-07-13) |

## Per-segment notes

2 mine: give iron pickaxe+axe via replaceitem (deterministic hotbar slots);
  arena = fill a stone hill + oak logs at the sky pad. Hold attack on a log
  (known playtest divergence class: hold-to-break), then dig a 1x2 staircase
  3-4 steps down. Verifies break-progress overlay, block removal, item drops
  (EntityItem ghosts), pickup.
3 build: replaceitem hotbar cobblestone 64; walk to pad edge, place a 4-block
  bridge (sneak at edge, use), then pitch 90-down jump-place tower x6, look
  around from the top. Verifies placement raycast, sneak edge-guard, jump-place
  rhythm.
5 pearl: throw 2 pearls (use), tp-follow of the teleport; throw eye of ender
  (rises + hovers). Verifies EntityEnderPearl / EntityEnderEye ghosts + fov.
7 crystal: End arena; pillar-up 8 blocks on the obsidian platform, face_point
  the nearest EnderCrystal, bow until getentities loses the crystal (explosion,
  fire pillar gone). Crystal is not living (health -1): death = disappearance.
8 dragon: Power V bow (give NBT), velocity-lead loop capped at ~40 shots,
  re-lead every shot from two getentities samples; success = dragon health<=0,
  then hold 200 ticks so the tape captures the full death animation (beams,
  XP orbs, portal spawn). Perched dragon resists arrows: if health stalls
  while perched, wait out the perch (or melee head with a sword as fallback).
9 e2e: chain the segment drives on one save without world resets. Landed at
  20k ticks (dragon fight length is spawn-luck); replay stayed tractable.
  Unblocked by three fixes: qrl ServerTickEvent watchdog (vanilla skips
  spawnEntity on any End-exit transfer), qrl EntityTravelToDimensionEvent
  guard (3x3 end-portal pad double-fires changeDimension -> second call hits
  the credits path and setDead()s the just-arrived player; headless never
  sends the credits respawn), and script.c accepting air down to -20
  (vanilla drowning counts below zero). Kill detection caveat: a dying
  dragon filters out of getentities while hp>0 is still the last sample -
  judge the kill from the tape (hp 0 rows + XP orb burst), not the drive's
  final poll.
10 mp4: ffmpeg hstack oracle f_*.png vs magma_frames.npy frames, scp to Mac
  (anvil is headless; demos go to the Mac).

## Known render-divergence surface each segment probes
- mine: break-progress decal, EntityItem ghosts (have model), particles (out of scope)
- build: block state updates, AO recompute at placed edges
- pearl/eye: new projectile ghost types (need entity_render mapping like arrows)
- crystal: EnderCrystal model (bedrock base + spinning cube + beam), explosion flash
- dragon death: death beams/rays, dragon fade, XP orbs, exit portal spawn-in
  (expect several NEW divergence classes here; file in OPEN_DIVERGENCES.md)
