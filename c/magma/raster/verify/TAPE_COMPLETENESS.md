# Tape completeness - what the recorder captures vs what vanilla draws

Audit date 2026-07-12 (full table in the session log; this is the living
checklist). Contract: EVERYTHING that influenced the player's screen is on
tape, in the world snapshot, or in the config sidecar. "when i play and we
replay it, there should be no difference."

## Captured (recorder as of 2026-07-12)
- inputs, pose, velocity, on_ground, hp, food, fall, world_time
- rain/thunder strength; open GUI screen class + scaled-res mouse
- authoritative local-player `SPacketEntityVelocity` values (raw packet shorts)
- HUD + open GUI drawn into tick-boundary goldens
- WHOLE save dir snapshot at recstart (level.dat, playerdata, all DIM
  regions, data/) -> initial inventory/xp/potions + world ground truth
- inventory delta dumps (41 slots id/meta/count) whenever changed
- air (<300), xp level+progress, absorption, saturation, hurtTime/maxHurtTime/
  attackedAtYaw, fire flag, portal ramp, attack-cooldown (<1), active potions
- ents: id, class, pos, yaw + [living: yawHead, pitch, swingProgress,
  hurtTime, deathTime, renderYawOffset, flags(burn/sneak/invis/child)]
  [sheep: sheared, fleece color, exact graze head Y+angle] [items: stack id/meta/count,
  age, hoverStart]

## Known-unrecorded (filed, next recorder pass; all packet-triggered one-shots)
- chat line arrivals (200-tick fade), title/actionbar text, boss bars (+world
  tint), lightning bolt strikes, item-pickup fly animation (3 ticks),
  entity status events (wolf shake, breeding hearts, crit sparks),
  riding state (mount hearts/jump bar), sleep fade, death cam.
- wall-clock-seeded pixels (unrecordable in principle - PIN in the mod
  instead): enchant glint scroll phase, achievement toast slide,
  heart-flash healthUpdateCounter, low-hp/hunger HUD jitter RNG.
- F1/F3/F5/smooth-cam toggles: emit-on-change insurance not yet wired;
  do not touch them while taping.

## Replay-side status (magma)
- DONE: inv rows drive post-tick hand/hotbar/GUI truth, then re-anchor the
  simulation inventory before the next tick (no pre/post action skew). XP and
  air feed the HUD; exact vanilla bubble sprites render below 300 air.
- DONE: open crafting/inventory/furnace rows drive exact visible container
  stacks, carried cursor stack, and furnace flame/cook progress every tick.
  These overrides are render-only and clear before the next tick.
- DONE for new tapes: authoritative player velocity packets replace heuristic
  immediate mob pushes. Legacy tapes retain the old entity-box inference.
- DONE: extended entity rows drive body/head pose, exact hurt state, item stack
  models + hover phase, sheep graze pose, sheared state, and fleece color.
- DONE for every recorded dimension: header/tick rows carry exact dimension,
  replay compares it, and the recstart Anvil snapshot is diffed against the
  matching Overworld/Nether/End generator into dimension-tagged, side-effect-
  free tick-0 snapshot events. A live portal-transition tape remains the gate.
- DONE: player `hurtTime`/`maxHurtTime`/`attackedAtYaw` drive the first-person
  hurt-camera transform; active potion rows drive poison/wither hearts and the
  potion-effect HUD; attack cooldown drives `updateEquippedItem`'s cubed target.
- Remaining: exact local-player swing progress is not recorded. `atk` is held
  key state, so legacy tapes can only infer a swing on its press edge; newer
  tapes also expose actual clicks through cooldown resets. Vanilla's repeated
  block-hit `swingArm` restart after half-progress needs a recorded local
  `swingProgressInt` to reproduce exactly. Death interpolation,
  burning/sneak/child entity transforms, armor/offhand GUI slots, player hurt
  red vignette, and portal transition timing also remain.
- Natural Nether/End transition timing and portal-link placement on a live tape.
