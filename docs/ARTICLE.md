# netherite: rebuilding Minecraft in C/CUDA, bit-verified against the real game

*The long version: what it is, why the verifier is the whole project, what
failed before it worked, and what you can do with 8,192 Minecrafts on one
GPU. Everything below is measured; commands and gates ship in the repo.*

Repo: https://github.com/Infatoshi/netherite

---

## What this actually is

netherite is Minecraft 1.11.2's engine, rewritten from scratch in C and
CUDA. Not a "Minecraft-like". The same worldgen from the same seeds, the
same physics to 1e-9 over full recorded sessions, the same mob behavior on
the tested paths, and the same pixels: the software renderer is gated
frame-by-frame against screenshots of the real Java client. When it
diverges, the divergence is either fixed or written down in a public
ledger (`OPEN_DIVERGENCES.md`) with the exact pixel counts and the
suspected mechanism.

It has two run modes, and they are different products:

- **Exact mode**: every pixel through the verified rasterizer. 35.9 fps at
  1080p on an RTX PRO 6000, 4.5 fps on a Ryzen 9950X3D, same bits either
  way. This is for verification and for world-model training data.
- **Training mode**: no rasterizer at all. Each environment raycasts a
  64x36 semantic camera (block ids + depth + edges) straight out of its
  world state. 8,192 environments step in lockstep on one GPU at 3.0
  million env-ticks per second.

The viral clip is the second mode: one agent's view, wiped from the real
game to the exact renderer to the semantic camera, then a pure zoom out
over a live recording of 7,200 worlds stepping together.

## Why Minecraft, and why this obsession with exactness

Minecraft is the best open-world RL benchmark that exists: procedurally
infinite, long-horizon, hierarchical (punch tree, make planks, make
pickaxe, mine coal...), partially observable, and there is endless human
prior on the internet for it. The problem has always been that the game
itself is a Java client running one world at ~20 ticks per second. You
cannot do serious RL on that, so everyone trains in a substitute sim, and
then eats the sim2real gap when the policy meets the real game.

The whole bet of netherite is: make the substitute exact, and the gap is
zero by construction. Train in mine, replay the same action sequence in
the real Java game, get the same trajectory. There is no adapter, no
domain randomization, no fine-tuning-on-the-real-thing step. A policy
good in the sim IS good in actual Minecraft, because the sim is actual
Minecraft in every way the policy can perceive.

That bet only pays if you can PROVE exactness, which brings us to the part
that killed every previous attempt.

## The graveyard: Rust, and the pure-CUDA megakernel

This is my third serious run at this project.

**Attempt 1: rewrite it in Rust.** Clean-room engine, nice architecture,
completely unverifiable. Six weeks in, "looks like Minecraft" and "is
Minecraft" had quietly diverged in a hundred places (mob pathing, item
physics, chunk decoration order) and I had no instrument that could even
enumerate the differences, let alone drive them to zero. The code was
fine. The epistemology was broken.

**Attempt 2: the pure-CUDA megakernel.** The seductive version: put the
entire game tick inside one giant CUDA kernel, thousands of worlds, no CPU
in the loop. It was too ambitious in exactly one way, and it is not the
way people guess. The GPU part was fine. The problem was that a megakernel
is a black box: when world 3,117 diverges from vanilla on tick 40,212, you
have no per-subsystem seam to bisect against, no scalar reference to diff,
no way to say WHERE inside the fused tick it went wrong. Debugging
correctness through a megakernel is archaeology.

Both attempts died of the same disease: **the bottleneck is not the
engine, it is the verifier.** You have to match worldgen, physics, mob AI,
textures, lighting, and the renderer, simultaneously, with zero physics
divergence, and you also have to answer an uncomfortable scoping question:
which of the game's hundreds of blocks, items, and mobs are in, and how do
you know the ones that are in actually behave? Every included feature
needs to be tested in the weird, exploitable ways real gameplay hits it.
Elytra wings through a waterfall. A blaze's aggro flag driving its fire
engulfment. A nether portal entered four ticks after the recording
started. You do not get to enumerate these cases up front. You discover
them by playing, recording, and diffing.

So the third attempt inverted the build order: **the oracle came first.**

## The Oracle: making the real game testify

The real 1.11.2 client runs with a small instrumentation mod. It can:

- record a **tape**: every input, every tick's post-tick player state
  (position, velocity to full float precision, health, inventory), the
  entity stream around the player (poses, health, hurt/death timers,
  status flags), a full world save snapshot, and **golden frames**:
  lossless screenshots at a fixed cadence, up to every tick;
- answer state queries over a socket (the same channel scripted scenarios
  use to stage worlds: fill blocks, summon mobs, hop dimensions);
- write session facts into the tape header that replay cannot derive:
  fog smoother state at recording start, rain strength, which HUD
  overlays the capture stripped.

Replay takes a tape and drives the C engine tick-by-tick with the
recorded inputs. Then the gates fire:

- **Physics gate**: the replayed trajectory must match the recorded one.
  Divergence tolerance is 1e-9; in practice the suite replays clean, i.e.
  the first diverging tick is "never".
- **State gate**: inventory, health, world edits, entity counts.
- **Pixel gate**: rendered frames vs golden frames, clustered and
  classified (HUD, viewmodel, particles, unexplained), with per-class
  budgets. "Unexplained" pixels above threshold fail the tape.
- **Geometry gate**: for articulated mobs (the ender dragon), the
  recorder dumps per-part transforms; the replay must reproduce them.
  After this week's fixes, the dragon's 1,668 recorded part poses match
  byte-exact.

Twenty-plus tapes cover scenario families: combat (blaze bow fight,
melee, wither skeletons, pigmen aggro), traversal (elytra through a
waterfall and over lava, soul sand + ice, cobwebs, fence collisions),
fluids (diving, flowing water conversion), dimension transits (nether
portal roundtrip, entering the End), the ender dragon fight, and long
free-play sessions. A nightly sweep replays all of them on both the CPU
and CUDA backends and diffs every gate against committed baselines.

Everything that does not pass is filed in `OPEN_DIVERGENCES.md`. Not
"known issues" hand-waving: each entry has the tape, the tick, the pixel
count, the suspected mechanism, and often the vanilla source line. The
ledger is adversarial by design. It is the project's actual product as
much as the code is.

### What the verifier catches that you would never guess

A few stories from the ledger, because the flavor matters:

- **The oracle itself can be wrong.** Our fog never matched at the
  horizon; weeks of suspicion landed on our rasterizer. The final
  measurement: the capture client's GL stack (llvmpipe) under-fogs
  radial distance by exactly a factor of 1.05, uniformly, against the
  formula in Minecraft's own source. The reference implementation has a
  renderer bug. We match the formula, documented the reference's
  deviation, and moved on.
- **Vanilla's dragon-death starburst is an integer overflow.** The alpha
  of the death rays is computed as `255*(1-f1)`, which goes negative past
  death tick 200 and reaches the vertex buffer through a Java byte cast:
  it wraps to ~250. The dramatic final flash every player knows is a
  numeric accident. Our port had "fixed" it by clamping, and therefore
  failed the gate. Exactness means porting the overflow.
- **The elytra flag arrives one tick late.** The client asks the server
  to start gliding, and the pose change comes back as entity metadata a
  tick later. Our sim set it inline and was one tick early, which showed
  up as a single frame of wrong camera height aimed at a waterfall.
- **The recorder snapshots the save at recording start**, so a dimension
  you first enter mid-recording (walking into a nether portal) does not
  exist in the snapshot, and replay silently regenerated its own nether:
  right terrain, no fire, no lava springs, because vanilla's nether
  decoration depends on chunk load ORDER, not just the seed.
- **A retired tape once measured as a perfect pass**, because golden
  paths were absolute, moving the tape orphaned them, and the gate
  reported "PASS over 0 frames". Three independent agents found this the
  same night. A gate that checks zero frames is now a fatal error, and
  "if a gate reports 0 frames checked, that is a harness failure" is
  written into the contributor docs.

Every one of these is the verifier doing its job: converting vibes into
mechanisms.

## The exact renderer

`magma` is a software rasterizer written to be bit-stable rather than
fast: fixed function, `-ffp-contract=off`, no driver in the loop. There
is a CPU implementation and a CUDA implementation of the same raster
stage, and a parity test requires them to agree **bitwise, on every
pixel, on every layer, including depth**. That contract is what lets the
project say "the CPU path is the oracle for the GPU path" and mean it
literally.

Performance today, measured with the in-tree stage timers (600 frames,
1080p, view distance 8): 4.5 fps CPU, 35.9 fps on the RTX PRO 6000. The
GPU raster stage is ~17.5 ms; nobody has seriously optimized it because
exactness work has always been the priority. There is a documented path
to 60+ (raster kernel is ~1.6x short, HUD caching, present overlap).

The renderer is verified against golden frames at multiple levels below
full tapes too: single-scene captures with exact camera state, GUI
screens diffed to the pixel (our inventory screen minus the real one:
442 of 112,796 pixels differ, every one by 1/255), entity model gates,
HUD compositing gates.

## The pivot that makes RL possible: throw the renderer away

Here is the part the thread compressed into one sentence, and the part
that deserves the most explanation: **training mode does not render.**

The insight is that a policy does not need the game's pixels. It needs a
faithful observation of the game's STATE. So the batched environment
(`blaze`) keeps each world's state resident on the GPU and gives the
policy a semantic camera instead of a rendered frame:

- Each environment owns a compact world slab: a dense region tensor of
  block ids around the agent (about 1 MB), a 3x3-chunk physics window,
  and a small player/inventory struct. Roughly a few MB per world, all
  resident in VRAM. That is the trick that lets thousands of worlds
  live on one card at once: 8,192 environments fit in the RTX PRO
  6000's 96 GB with the observation buffers beside them.
- The observation is a 64x36 grid of raycasts from the agent's eye:
  each ray returns the block id it hits, the distance, and an edge
  flag. Three tiny planes. That is the entire visual input, and it is
  why the farm zoom video looks like chunky pixel art: you are looking
  at what the policy looks at.
- Every simulation op (movement, digging with real block hardness,
  item drops, crafting, fluid interaction, reward) is a small kernel
  or a per-env serial function, NOT a megakernel. This is the direct
  lesson from attempt 2. Because each op has a scalar CPU twin
  compiled from the same headers, the whole batched sim has a bitwise
  CPU==CUDA contract, verified by replaying a 2,058-action episode
  across 16 CUDA lanes against the CPU, byte-exact on every tick.

### CUDA cores vs CPU cores, stated plainly

A CPU has dozens of powerful cores; a GPU has tens of thousands of weak
ones. One Minecraft world is a terrible fit for a GPU if you think of it
as one program: branchy, serial, latency-sensitive. But 8,192 worlds,
each advanced by one small step at a time, is a perfect fit: the
per-step work of ONE world maps to one thread (or one thread per ray for
the camera: 8,192 envs x 2,304 rays = ~19M rays per observation batch,
which is exactly the shape GPUs were built for). The batch dimension
supplies the parallelism that a single world lacks.

Concretely, per step the GPU runs: one kernel over envs for the tick
(movement/physics/dig state machines against the resident region
tensor), then one kernel over (env, pixel) for the camera. The CPU's
job shrinks to feeding action tensors and reading observation tensors,
already in torch layout, zero copies in the loop.

Measured throughput on one RTX PRO 6000 (1,000-decision runs, full
action decode, camera every decision, repeat 4):

| batch | env-ticks/s | agent decisions/s |
|---|---|---|
| 1,024 | 0.79M | 198k |
| 4,096 | 2.22M | 554k |
| 8,192 | 3.02M | 756k |

The same loop on the CPU backend does ~0.29M env-ticks/s using the whole
9950X3D. The GPU is ~10x the entire CPU, and the ratio grows with batch
size because the fixed costs amortize. For calibration against the real
thing: vanilla Java Minecraft is one world at 20 ticks/s. This is
150,000 vanilla-clients-worth of simulation on one card, with a bitwise
CPU reference and a pixel-verified lineage back to the game.

## What it looks like (the videos)

The follow-up media set (same pipeline as the launch thread, all
oracle-left / magma-right, replayed tick-exact from recorded inputs):

- **Nether elytra flight.** An agent runs off a netherrack perch inside
  a real seed-0 lava cavern, deploys wings mid-fall, glides 115 blocks
  over the lava sea at ~18 m/s, and slams into the far cavern wall at
  speed, standing up at half health. Nothing staged except the perch
  and pad; the terrain is generated, the route was read out of the
  world snapshot. Both panes render the same cave, the same lavafalls,
  the same flight.
- **Nether portal roundtrip.** Walk into a portal, 4-second transit
  with the purple overlay frame-matched, arrive in a nether cave whose
  fire and lava pools now replay correctly (this tape found the
  mid-recording-dimension snapshot bug and drove the fix).
- **Blaze fortress melee.** Portal, fortress walkway (fortress
  placement is seed-deterministic, so the whole route is scripted),
  diamond sword, blaze at 20 HP going down in visible 7-damage hits,
  death keel and hurt tint matching vanilla.
- **Ender dragon kill.** The dragon circles, gets pulled on camera,
  dies on a chosen tick (health written to zero by an in-world command
  block wired to the camera pitch, so the REAL death animation plays),
  and runs the full 200-tick death: rise, spin, dissolve, rays,
  byte-overflow starburst. After this week's fixes this scene passes
  the full pixel gate: zero unexplained clusters over 201 frames. It is
  the first entity-death scene to gate clean.
- **The farm zoom.** A live recording, not a mosaic of stills: 7,200
  environments stepping in lockstep at approximately real time, every
  pane its own world with a wandering agent. (7,200 not 8,192 because
  the recording process shares VRAM with the batch; 8,192 is the
  benched training configuration.)

## Actually training in it

The proof-of-life result: a policy trained with PPO in the batched env
runs a 2,058-action unbroken chain from empty-handed spawn: chop logs,
craft planks, sticks, a crafting table, a wooden pickaxe, then mine
stone and coal with it. No scripted stages, no resets. The episode
replays through the exact renderer for the demo footage, and the same
action sequence replays in the real Java client, which is the sim2real
claim made concrete.

Curriculum comes from snapshots: the env can load per-seed, per-stage
world states (fresh spawn, post-logging, post-crafting...) and assign
thousands of lanes across them, so later stages get dense experience
without replaying the prefix. Reward shaping, action repeat, frame
stacking are all conventional; the point is that nothing about the RL
setup is exotic. The env is just very fast and very true.

What is NOT yet there, honestly: full mob AI parity in the live sim
(replay uses recorded entity streams; the batched sim's blaze does not
yet run vanilla's attack-pattern state machine), redstone, villages,
the End beyond the dragon arena, and most of the item universe beyond
the tool chain. The scoping philosophy is unchanged: features enter
when they can be oracle-tested, not before.

## Generating world-model data

The exact renderer turns the same machinery into a world-model data
factory. Because rendering is deterministic and replay is exact, you
can produce paired sequences (frame, action, next frame) at 1080p with
perfect ground-truth side channels that no video scrape has: depth per
pixel, semantic ids per pixel, exact camera pose, entity poses, and
the full world state behind every frame. You can re-render the same
trajectory from new camera angles, re-light it at a different time of
day, or branch it: rewind ten ticks, take a different action, render
the counterfactual. A dataset where counterfactuals are cheap is the
thing video-scraped world models fundamentally cannot have.

At 35.9 fps per GPU renderer instance this is already practical for
mid-scale datasets; the renderer's CUDA path leaves obvious headroom
(nobody has fused its stages), and render farming parallelizes
trivially across worlds because everything is deterministic from
(seed, action log).

## Where the divergence grind stands

The ledger is short now, and every entry is measured. In one recent
overnight pass, sub-agents closed nine: the dragon death-ray curve
(onset, End-fog, lightmap, the overflow starburst), the dragon's
trail-ring phase (its pose is now byte-exact against the recorder's
geometry oracle), the blaze death keel that the code computed and
discarded, double-plant models, phantom vegetation from load-order
dependent decoration (the replay's world is now bit-identical to the
save), the elytra one-tick flag latency, a nether lava id swap, and
the zero-frame gate hole. What remains open, with repro tapes filed:
first-person hand poses on use animations, some particle families,
fortress structure placement (terrain matches, the structure generator
disagrees on y/z), mob-spawner cage miniatures (renderer built, data
plumbing missing), the flowing-water surface texture family, fire
animation phase, and the live-sim mob AI gap above.

The honest headline number: the nightly suite result is PASS, meaning
every tape either gates clean or matches its committed baseline of
known, documented residuals.

## What this unlocks, and what is next

- **RL at internet scale on single nodes.** 3M env-ticks/s per card,
  linear across cards (worlds share nothing). The obvious next step is
  a multi-GPU farm and longer-horizon curricula toward diamond and
  beyond.
- **World models with ground truth.** Counterfactual-capable, label-
  perfect Minecraft video at scale, from the exact renderer.
- **A methodology.** The transferable artifact is not the engine. It is
  the oracle-first loop: instrument the reference, record adversarial
  sessions, gate every subsystem bitwise or pixelwise, ledger every
  divergence with its mechanism, never let "looks right" stand in for
  "is right". That loop is how a two-attempt graveyard became a
  verified engine, and it applies to any "rewrite the reference
  faster" project, not just games.

Everything here is in the repo, including the gates that would catch me
lying to you, which is rather the point.

https://github.com/Infatoshi/netherite
