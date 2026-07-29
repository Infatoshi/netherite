# Why can't you just rewrite Minecraft in Rust, bro?

> [MEDIA: upload `demos/article/header_three_panel.png`]
> Caption: One game became an answer key for thousands.

"Why can't you just rewrite it in Rust?"

It is a good meme because it sounds so reasonable. Minecraft is old. Java is
not famous for being small or fast. Surely you can take the same game, write it
in a lower-level language, and run thousands of copies at once.

I tried that.

The problem was not writing the code. The problem was proving that the new game
was still Minecraft.

If your player falls a tiny bit faster, a jump can land differently. If one
random number is pulled in the wrong order, a tree can appear somewhere else.
If a mob turns one tick too early, the rest of the fight changes. You can build
something that looks right for hours while these mistakes pile up underneath
it.

A faster wrong answer is still a wrong answer.

## The first two attempts failed for the same reason

This is my third serious attempt at the problem.

The first was a clean Rust rewrite. After six weeks, it looked like Minecraft.
That was exactly the trap. I had no good way to list all the places where it
was not Minecraft.

The second attempt went in the opposite direction. I put far too much of the
game into one giant CUDA kernel. It could run many worlds, but it turned the
whole simulation into a black box. When world 3,117 went wrong on tick 40,212,
there was no clean seam where I could stop and ask which part had failed.

Those projects looked very different, but they died of the same disease: I
could build faster than I could verify.

So on the third attempt I changed the order. I built the judge first.

If you want to rewrite any serious piece of software, the tests are the
project. The code is just whatever passes them. Once that clicks, the question
stops being "how do I write a fast Minecraft" and becomes "how do I write
tests so strict that anything passing them IS Minecraft," and then, "how fast
can I make the code while those tests still pass."

> [MEDIA: upload `demos/article/timeline_three_attempts.png`]
> Caption: Three attempts. The third one started with the judge.

## Minecraft became the answer key

In this project, the original Java game is called the **oracle**.

An oracle is just an answer key. I perform an action in the real game, record
what happened, and ask my rewrite to produce the same result.

The recorder saves a **tape**. Think of it as a flight recorder for a Minecraft
session. It contains the buttons pressed on every tick, the player's position
and velocity, health, inventory, nearby entities, a snapshot of the world, and
lossless screenshots from the real game.

Then the C game replays the same buttons against the same world.

Now I can compare the two games without saying "they look close enough."

> [MEDIA: upload `demos/article/diagram_oracle_loop.png`]
> Caption: The oracle loop. Every fix ends where it started: replay the tape
> again.

## One rewrite, several kinds of proof

There is no single "is this Minecraft?" test. Different parts of the game need
different judges.

### Physics

Physics is the cleanest one.

On each tick, compare the player's position and velocity in both games. If the
numbers split, report the first tick where it happened. The canonical physics
tape currently replays all 3,617 ticks with no divergence at the project's
1e-9 tolerance.

That turns "movement feels slightly off" into something useful:

> Tick 428. Jump velocity is correct. Collision response is not.

That is a bug an agent can actually work on.

> [MEDIA: upload `demos/nether_elytra_sbs.mp4`]
> Caption: Elytra flight across the Nether lava sea. Java oracle left,
> netherite right, same buttons on every tick.

### World generation

World generation is more like comparing two enormous spreadsheets.

Start both games from the same seed. At the same coordinates, the same blocks
should exist. Repeat that in the Overworld, Nether, and End.

This test found a bug that screenshots alone could not explain. A tape entered
the Nether after recording had already started. The recorder had saved the
Overworld, but the Nether did not exist on disk yet, so it was never copied.
During replay, my engine generated a fresh Nether. The broad terrain looked
right, but fires and lava pools were missing because those depend on the order
chunks were loaded in the original game.

The fix was not "tune the lava generator." The fix was to make the recorder
capture a dimension when it first appears.

That distinction matters. A weak test would have pushed me toward the wrong
code.

> [MEDIA: upload `demos/article/portal_sbs.mp4`]
> Caption: Walking through a Nether portal mid-tape. After the recorder fix,
> the arrival dimension has its fires and lava pools in the right places.

### Pixels

Pixels are the most visual test, but "compare two screenshots" is only the
beginning.

The useful tool ranks every frame by how wrong it is. Then it groups nearby bad
pixels into clusters. Then it asks what kind of difference each cluster looks
like:

- Did the renderer choose the wrong texture pixel?
- Is the color slightly too bright or dark?
- Is the object shifted?
- Is something present in one game and missing in the other?
- Is this merely the noisy edge of an otherwise correct shape?

When the cause is still unclear, I can zoom into one cluster and inspect the
exact RGB values, geometry, depth, and surrounding pixels.

Instead of telling an agent "the scene looks weird," I can tell it:

> On tape X, tick 152, this 34-by-19 patch of the waterfall is choosing the
> wrong animated water frame. Find the function responsible and make the gate
> pass without moving any other pixels.

That is the difference between asking a model to understand a monolithic game
and giving it one small, measurable problem.

> [MEDIA: upload `demos/article/pxdiff_microscope.png`]
> Caption: The pixel microscope on a real divergence: the frame, the diff
> mask, and a 10x zoom into one cluster.

### Mobs

Mob behavior is harder because it is not one formula. A creeper sees you,
chooses a path, turns, starts its fuse, gets pushed, and may change the world
when it explodes. One early difference can change everything after it.

The practical approach is to stage small encounters.

Spawn one mob in a known place. Walk toward it with recorded inputs. Compare
its position, pose, health, flags, and pixels at every step. Find the first
thing that splits. Fix that one thing. Repeat.

Once one mob establishes a pattern in the code, the next one becomes much
easier. Blazes, zombies, pigmen, wither skeletons, and the dragon share pieces
of rendering, damage, interpolation, and animation logic. A good fix can close
an entire family of failures.

There is an important boundary here: recorded mob scenes can verify rendering,
damage, poses, and replayed behavior today, but the batched training simulator
does not yet contain every live vanilla mob brain. "Verified on these tapes"
is not the same claim as "every mob is finished."

> [MEDIA: upload `demos/article/mob_reel.mp4`]
> Caption: The ender dragon fight. Java oracle left, netherite right. This
> scene passes the pixel gate over all 201 frames with zero unexplained
> clusters, the first entity-death scene to do so.

## Why this makes AI agents useful

An LLM cannot hold an entire Minecraft implementation in its head and
personally guarantee that every line is correct. Neither can I.

What it can do is solve a sharply bounded problem with a test at the end.

That changed how I use agents. I do not fan out ten agents with "make
Minecraft better." I give each one a tape, a tick range, a measured cluster,
and a gate it must not regress. They investigate independently. I review the
mechanism and rerun the gate before keeping anything.

In one overnight pass, that loop fixed nine separate problems: Nether lava
IDs, a one-tick elytra delay, double-height plants, load-order-dependent
vegetation, blaze death rotation, dragon death rays, the dragon's tail timing,
a missing-dimension snapshot bug, and a test that could accidentally pass
after checking zero frames.

The last one is my favorite. Moving an old tape broke the paths to its
screenshots. The gate checked no images and announced a perfect pass. It now
fails hard if a tape promises screenshots but resolves none.

Even the judge needs tests.

> [MEDIA: upload `demos/article/diagram_agent_fanout.png`]
> Caption: Every branch comes back to the same judge.

## Eight months, three models

I have been on this third attempt for about eight months, since Claude Opus
4.5 came out and agent models became good enough to hold a debugging loop
without wandering off.

Most of the code in this repo was written by models. That sentence makes some
people uncomfortable, so let me be precise about the division of labor.

One strong model runs the main loop: reading gate reports, deciding which
divergence matters most, designing the fix, and reviewing everything that
comes back. I keep a second strong model from a different lab grinding on the
same repo in parallel, because different models make different mistakes and
disagree in useful places. A third, faster model gets fanned out for the
bounded jobs: rerun this gate, bisect this tape, write this test scenario,
regenerate these reference frames.

The delegation decision is always the same question: does this task end in a
test the agent cannot fool? If yes, it can be handed off, and the only real
cost is wall-clock time, so it goes to whichever model is fastest at that
difficulty. If no, it is a judgment call, and it stays in the main loop.

Nothing lands on trust. Every diff an agent returns gets read, and its
acceptance test gets rerun from the main tree before the commit. Some of the
best deliverables have been refutations: an agent sent to fix a "texture bug"
came back with measurements showing there was no texture bug, just a plant
rendered at point-blank range. A verifier strong enough to prove an agent
right is also strong enough to let it prove me wrong.

This is the honest answer to "how can you trust AI-written code": I mostly do
not. I trust the judge, and I spent most of the eight months building it.

## Why C and CUDA, not Rust

Rust was not the villain. The first Rust attempt failed because I had weak
verification, not because Rust cannot make games.

I chose C for this version because it fits the shape of the project and the
way I want models to write code.

The engine is mostly arrays of plain data moving through small procedural
functions. C keeps that visible. Allocation and ownership are explicit. There
are no class hierarchies for an agent to extend in three different directions.
When the same math must run on the CPU and the GPU, C also sits naturally next
to CUDA.

Most importantly, the language choice is downstream of the test choice. Once
the oracle tells me whether a change is right, I can optimize aggressively
without arguing about whether I changed the game.

> [MEDIA: upload `demos/article/diagram_language.png`]
> Caption: A project constraint, not a universal language ranking.

## Why a GPU changes the scale

A CPU has a small number of powerful cores. A GPU has a huge number of simpler
workers.

One Minecraft world is an awkward GPU job. It branches constantly and much of
it happens in sequence.

Thousands of independent Minecraft worlds are a very good GPU job.

Every world can advance the same small piece of logic at the same time. Then
every world can cast its camera rays at the same time. The individual worlds
do not need to talk to each other.

This is also why "just launch thousands of Java clients" is not the same
solution. One Java client wants hundreds of megabytes to a gigabyte of memory,
and it drags a whole renderer, JVM, and operating system process along with
it. A thousand of those is on the order of half a terabyte of RAM spent
running mostly idle copies of the same machinery. Netherite stores each world
as compact state directly on the GPU, about one megabyte per environment, so
8,192 environments fit in roughly nine gigabytes of VRAM sitting next to the
policy that is learning from them.

On the measured launch configuration, one RTX PRO 6000 holds those 8,192
environments and steps them at 3.02 million environment-ticks per second. The
matching CPU loop reaches 0.29 million on the whole Ryzen 9950X3D.

Those numbers include full action decoding and a 64-by-36 semantic camera
every decision over a 1,000-decision run. I am spelling that out because a
speed number without its workload is marketing, not a benchmark.

> [MEDIA: upload `demos/article/diagram_cpu_vs_gpu.png`]
> Caption: Do not parallelize one Minecraft. Parallelize the batch.

> [MEDIA: upload `demos/zoom_8192_story.mp4`]
> Caption: One environment's camera, zooming out to 7,200 live worlds on one
> GPU. The recording shows 7,200 because the recorder shares VRAM; 8,192 is
> the benchmark configuration.

## The exact renderer and the fast training camera are different things

There are two modes, and mixing them together makes the project sound more
magical than it is.

The exact mode renders normal Minecraft pixels. It is used to compare against
the Java game and to create high-quality training data. At 1080p, the measured
CUDA renderer runs at 35.9 frames per second. It is not yet at the 60 fps
target.

The training mode skips that renderer. A policy does not need clouds,
beautiful water, or a hotbar shadow to learn where a block is. It receives a
tiny 64-by-36 semantic camera containing things like block identity, distance,
and edges.

That is where the millions of ticks per second come from.

The exact renderer proves the world. The small camera lets the policy learn
inside it quickly.

> [MEDIA: upload `demos/pixel_match_sbs.mp4`]
> Caption: The exact renderer against the Java game, frame by frame. This is
> the mode the pixel gates run on.

## Then the verifier itself became the bottleneck

Once the engine became reasonably complete, most of my time was no longer
spent writing blocks or mobs. It was spent waiting to learn whether a change
was correct.

Record a Java scene. Replay it. Render every frame. Compare the images. Rank
the failures. Give one to an agent. Make a change. Run the whole thing again.

That loop is the real build system.

So I started optimizing it with the same question I had asked about the game:
where is the current bottleneck?

The replay pipeline was reduced from 29 seconds to 8.4 seconds for the
documented workload. Tapes can be analyzed in batches. The CPU and CUDA
renderers must agree bit-for-bit, so the faster backend can be used without
changing the answer. Better diff tools point directly to the useful frames, so
an agent does not waste an hour watching a whole video.

Faster verification compounds. If a loop runs three times sooner, every agent
gets three times as many honest attempts in the same wall-clock time.

> [MEDIA: upload `demos/article/diagram_verify_loop.png`]
> Caption: The loop is the build system. Making it faster makes everything
> faster.

## What this is actually for

The immediate goal is a Minecraft environment where a policy can learn long
chains of behavior quickly, then replay those actions in the real game.

That is not hypothetical. A policy trained with PPO inside the batched
environments runs an unbroken 2,058-action chain from an empty-handed spawn:
chop logs, craft planks, sticks, a crafting table, and a wooden pickaxe, then
mine stone and coal with it. No scripted stages, no resets. The same action
sequence replays across 16 CUDA lanes against the CPU backend byte-identically
on every tick, and it replays inside the real Java game. That last replay is
the entire bet made concrete: train in the rewrite, act in the original, with
no sim-to-real gap to apologize for, because the sim is the real rules.

The same system can also produce unusually clean data for world models: video,
actions, depth, block identities, camera pose, entity pose, and the hidden
world state all aligned on the same tick. It can rewind a scene, change one
action, and render the counterfactual.

But the broader lesson is not about Minecraft, Rust, or CUDA.

It is this:

**If you want to rewrite complicated software, do not begin by asking how to
write it faster. Begin by asking how the old program can grade the new one.**

The better that judge becomes, the more work you can safely hand to models,
the more aggressively you can optimize, and the less you have to trust
anyone's claim that two giant systems "look about right."

The code matters.

The test is what makes the code believable.

> [MEDIA: upload `demos/article/final_reel.mp4`]
> Caption: Pixel match, the Nether, the dragon, and 7,200 worlds. The tests
> ship with the code.

Repository: https://github.com/Infatoshi/netherite

---

*Publishing checklist (not article copy): every media slot above names a real
file on anvil, mirrored to the mac under ~/Downloads/netherite_thread_v2/
article/. Upload MP4s natively (H.264 yuv420p, within X's web limits); do not
use the GIF. Every side-by-side clip reads Java oracle left, netherite right;
keep the captions with the uploads since many readers watch muted.*
