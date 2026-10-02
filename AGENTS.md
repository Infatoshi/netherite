# netherite (AGENTS.md)

## If you are an agent new to this repo

What this is: Minecraft 1.7.10 reimplemented in C (`csrc/engine`, the engine) and CUDA (`csrc/cuda`), checked
bit for bit against the real Java client (`oracle/`, the oracle: Mojang's client decompiled on your machine, plus a
small harness in `oracle/harness`), with a batched RL pipeline on top (`csrc/runtime`). The Java side is the
reference for every behaviour; the C engine is right when its rows equal Java's for every tick.

Setup, once: `make` builds `out/bin/netherite` (the CLI; `netherite help`, `netherite VERB --help`), then
`out/bin/netherite setup` finds the user's Minecraft 1.7.10 install (`--mc DIR`), copies its libraries and jar,
rebuilds the oracle's source (MCP 9.08's decompile in Docker, once, then `oracle/decomp/oracle.patch`) and the
textures (`out/assets`) from it, and builds everything. It needs JDK 8, make, a C compiler, jq, unzip, perl,
git, curl and Docker; SDL3 for the playable client; Linux, CUDA and clang for the GPU paths.

Checking a change: `netherite gate --quick` on a fresh clone (the oracle's own gates, then recordings the Java
client makes now, each replayed by the C engine row for row); with stored recordings, `make -s -C csrc test`
(the whole suite), and the narrowest check for what you touched (Commands below). A divergence is reported as
the first differing row and field (`out/native/diverge NAME`). Report to your user what you ran and its last
lines, not your impression.

Never: commit Mojang's code or assets (`oracle/src` is generated and ignored; `out/` holds the jar, the textures and
every recording), push decompiled source anywhere, add Python to the tree, or change vanilla behaviour in the
oracle except by a documented pin (SPEC.md). Edit `oracle/src` only to add a hook call into `netherite.oracle`, then
run `make -C oracle oracle-patch` and commit `oracle/decomp/oracle.patch`. `tools/export.sh DIR` makes and checks the
public copy of the tree.

The rest of this file is the project's internal working method: the dev host (the canonical checkout, its
merge queue and lanes) and the GPU host (four GPUs for the GPU and performance lanes), the fan-out of work to
parallel agents, and every command.

## The project's own rules

Home: **the dev host** - canonical at `devhost:~/dev/netherite-v2`
since 2026-09-26 11:59 pm (the GPU host before). Lanes, gates, the merge queue and
long runs happen there (`ssh devhost`), and so does the orchestrator (since
2026-09-27 1:30 am). GPU work runs on the dev host's RTX 3090 (sm_86, 24 GB,
device 0; Elliot, 2026-09-27), once the C engine is one-to-one. Since
2026-09-29 (Elliot) GPU and performance lanes also run on the GPU host, one lane
per RTX PRO 6000 (sm_120, GPUs 0 to 3), from a clone that mirrors the dev host's
master; `out/flywheel/bin/toanvil.sh` (untracked) ships a finished GPU host lane to the dev host's
merge queue (the rules for working there: "Compiling and measuring fast"). The Mac clone at
`~/dev/netherite-v2` (origin = the dev host) is for reading the source, editing it,
building it and playing; pushes from the Mac update the dev host's working tree.
csrc/ must build
on macOS (arm64, clang) as well as Linux, because the human plays there:
put Linux-only calls (prctl, perf_event, /proc) behind `#if
defined(__linux__)` (lane/gpuarena's prctl broke the Mac build until
a755efd; the Mac's replays match the GPU host's exactly).

From-scratch rebuild of netherite on **Minecraft 1.7.10** (decided 2026-09-22;
v1 is `github.com/Infatoshi/netherite` on 1.11.2, frozen as reference). Read
`SPEC.md` for the architecture and the next milestone, `DEVLOG.md` for
history. These three files, README.md (the public front page) and
`divergences.md` (generated, below) are the only project markdown.

## Layout

`oracle/` is the whole Java oracle, built with plain javac by `oracle/Makefile`:

- `oracle/src`: the Minecraft 1.7.10 client source and the source of truth, one
  folder per package (`src/block` is `net.minecraft.block`; javac does not
  need the folders to match). Generated, never committed (lane/polish):
  `oracle/decomp/src.sh` (`make -C oracle src`, which every build runs) makes it
  from the pristine MCP 9.08 decompile (cached in
  ~/.cache/netherite-oraclesrc/pristine; from git tag `mcp-pristine` where the
  private history has it, else oracle-src.sh's Docker route, the same bytes),
  `oracle/decomp/determinize.sh` (mechanical RNG and math pass, 107 files) and
  `oracle/decomp/oracle.patch` (the hook edits that call into `harness/`, and the
  deleted screens). It refuses to replace edits not yet in the patch.
  `make pristine-diff` (FULL=1 for the patch) is every change ever made to vanilla.
- `oracle/harness`: our code, the `netherite.oracle` harness.
- `oracle/decomp`: how `src` was made from the owned jar (provenance), and how
  to make it again from public inputs: `make oracle-src` (oracle-src.sh:
  Mojang's jar and libraries, MCP 9.08 by URL, each checked by hash, the
  decompile in Docker, determinize.sh, then `oracle.patch`, every later edit
  with no deleted vanilla text). `make oracle-src-check` proves a fresh
  directory builds a tree whose git hash is the working oracle/src's (QUICK=1:
  only that the patch is current for oracle/src).
- `oracle/tests`: the gate suite (`gate.sh`), its helpers and its input.
- Everything built, downloaded or recorded goes to `out/java/` (ignored). The
  output directories kept their names through the 2026-10-01 rename (java/ to
  oracle/, native/ to csrc/, src/ to engine/, mods/ to harness/): `out/java`
  holds the oracle's builds and every recording, `out/native` the engine's builds.

`csrc/` is the C reimplementation, checked stage by stage against oracle
chunk dumps in `out/java/chunks/` (never against another native tree).
By role: `csrc/engine` the C engine (one library), `csrc/cuda` all device
code, one folder per part, each with its own `cuda/PART/PART.mk` (which
csrc/Makefile includes) and its make targets `gpu-PART` (the build),
`gpu-PART-check` (the check) and `gpu-PART-gate` where a gate script exists:
`cuda/worldgen` terrain, biomes, caves and ravines of the three dimensions
and generation served ahead, `cuda/lighting` the light engine (the
server's and the client world's), `cuda/meshing` sections of blocks into
triangles, `cuda/render` the frame and the frame judges' plugin and gate,
`cuda/tick` the game tick compiled for the device (S6, a correctness tool),
`cuda/include` the C library stand-ins of the clang device builds;
`cuda/mirror.tsv` and `mirror.c` (out/native/mirror) the mirror rule. Inside a folder files
are named by stage or role (worldgen/density.cu, render/raster.cu,
lighting/host.cu), the host API is `PART_verb` (worldgen_*, lighting_*,
meshing_*, render_*, tick_*), objects are `out/native/obj/cuda_PART_FILE.o`,
and every kernel name is unique across csrc/cuda (by stage: density,
raster, carve_walk; the folder's word in front where a bare name would
clash: mesh_draw, light_replay, tick_scatter). DEVLOG 2026-09-29
lane/cudanames has the table from the old names. `csrc/runtime` the scheduler of
the batched product (the env pool, the pipeline, the trainer's API; its own
Makefile with one fragment per part; the dev host only, outside `all` and
clangcheck), `csrc/play` the human client, `csrc/tests` the tests.
The renderer's textures are `out/assets`, made by `make -C csrc assets`
(part of `all`) from the client jar and play's hud scene as `csrc/assets.tsv`
lists them; no texture is committed. `make` at the root builds the CLI,
`out/bin/netherite` (`csrc/cli/netherite.c`), which wraps these make targets.
`csrc/engine/biomes.h` is generated from the live biome objects by
`make -C oracle biomes-h`; do not edit it by hand.

`index/` is the codebase index: one JSON record per source file
(`index/java/<package>/<File>.java.json`, `index/csrc/engine/<file>.json`,
`index/oracle/<File>.java.json`, `index/assets.json`) mapping vanilla to
the C engine and back: each Java file's scope and status, every method and
field under its MCP or `func_`/`field_` name with its C `file:symbol`, the
recordings and tests that prove it, and what is missing; each C file's Java
sources, structs, entry points and GPU hazards by line. `index/schema.json`
defines the fields, `tools/index_check.c` (`make -C csrc index-check`, or
`out/native/index_check` from the root) validates records against the tree
(cited symbols must exist), and `index_check --summary` (`ARGS=--summary`) writes
`index/summary.json` (coverage, counts, every open in-scope item, the GPU
hazards). Start a task from the records of the files it touches; a lane that
changes what a file ports updates its record and runs the check. The merge
trial refuses a lane whose merged tree has more index check errors than
master (a renamed C symbol that a record still cites counts).

`tools/` holds the index checker (above), `export.sh` (the public copy of the tree and its
checks: no decompiled text, no jar file, no host name, path or key, a clean clone's setup and
quick gate) and the pixel-diff CLI (`pxdiff.c`, built as `out/native/pxdiff`): given a golden (oracle) and a
candidate (native) frame it clusters the differing pixels and names the cause
(texel-selection, shading-offset, registration, content, edge, cutout-sky).
The render/pixel path uses it by default; never hand-scan pixels or hand-roll
numpy. Run its `selftest` (must PASS), then `survey` first. Its header documents every command.

## Commands (from oracle/)

```bash
make deps                  # 1.7.10 libraries, client jar (resources only), natives, assets link
make                       # compile src + harness, a few seconds
make gate                  # all gates, ~45 s on the dev host (Xvfb, xdotool, jq)
make play [SEED=1]         # play; then the tape is replayed (on the dev host from the Mac, if its tree matches)
                           # any run: CONF=../configs/NAME.yaml, SET="key=value ..." (config.yaml keys:
                           # section.key, or a bare world key: SET="difficulty=peaceful render_distance=6")
make check TAPE=...        # replay a finished tape where the gates live
make script SCRIPT=tests/smoke.jsonl TAPE=../out/java/tapes/smoke.jsonl FRAMES=../out/java/frames/smoke
make replay REF=../out/java/tapes/smoke.jsonl [EVERY=1 FRAMES=dir]   # stops at the first differing field
make script ... FROM=../out/java/checkpoints/1/cp-mobs-s1   # start on a checkpoint; a script's {"cmd":"save","name":N} writes one
make serve PORT=25590      # TCP control port; bash tests/tcp_smoke.sh PORT DIR drives it
make pool-status           # the shared warm oracle JVMs (started on demand; POOL=0 skips them, pool-start adds tree-local ones)
make cache-stats           # the oracle result cache: a repeated script or replay run returns its stored outputs (CACHE=0 runs fresh)
make pristine-diff         # every change to vanilla vs the pristine decompile (FULL=1: patch)
make oracle-src [OSRC=dir]  # oracle/src rebuilt from public downloads + decomp/oracle.patch (Docker; ~25 s warm, ~50 s cold)
make src                   # oracle/src from the pristine decompile + decomp/oracle.patch (every build runs it; 0.2 s)
make oracle-patch          # regenerate decomp/oracle.patch from oracle/src after an edit there (commit the patch)
make oracle-src-check [QUICK=1] [COLD=1]   # a fresh directory builds it, tree hash = oracle/src's, javac builds it
make render-scenes         # play's two recorded scenes (tests/scenes) where missing; csrc/assets.tsv cuts two textures from them
make coverage [RECS=...] [J=N]   # every recording replayed with JaCoCo (Main --coverage), and the frame judges' (clientframes/NAME, rawjudge/NAME: cf-NAME, rj-NAME) with frames drawn at their goldens' ticks: cached per recording (COVROOT), misses on coverage pool members, J from the load: ../out/java/coverage/summary.txt and unexecuted.tsv (the in-scope methods no recording runs, by package and class, with the index's statuses)
make selfcheck [J=8] [SELECT=1 [COV=dir]]   # every stored recording replayed fresh on this oracle, one line each; SELECT=1: only the ones that ran a method changed since the coverage data in COV was made (default ../out/java/coverage; tests/SelfSelect.java), or all with a FALLBACK line saying why
make -C ../csrc coverage [RECS=...]   # the csrc/engine functions no recording runs (the fncov build over test_snapshots and play_check), crossed with index/csrc: out/native/coverage
bash decomp/decompile.sh   # provenance: regenerate the pristine decompile (Docker, the GPU host)
make chunks SEED=2 X0=-400 Z0=-400 X1=400 Z1=400 STEP=50 DIR=../out/java/chunks/wide2   # worldgen stage dump
make biomes-h              # regenerate ../csrc/engine/biomes.h
make -C ../csrc test     # every native stage against every dump in out/java/chunks, ~1 s
make keyframes [RECS=...]  # snapshots every KEVERY=1000 ticks: native --from-row N, diverge, make -C ../csrc test SEGMENTS=1
make -C ../csrc globals  # the tick's writable globals against csrc/tests/globals.allow (make test runs it)
make -C ../csrc callgraph   # the tick's call graph cycles (worldgen aside must be none; POINTERS=1 resolves pointer calls)
make -C ../csrc index-check [ARGS=--summary]   # the codebase index's records against the tree (tools/index_check.c)
../out/native/pxdiff survey --a GOLDEN.png --b NATIVE.png -o DIR   # a frame pair's differing clusters and their causes (tools/pxdiff.c; selftest, clusters, probe, zoom, frames, stats)
make -C ../csrc phase-check [RECS="a b"]   # every phase's declared writes (csrc/engine/phase.c) held over the snapshot recordings; test_snapshots --phase-trace FILE writes each row's phase digests, make -C ../csrc test PHASETRACE=1 runs the suite with the tracer on
make -C ../csrc gpu-tick            # S6 on the GPU (cuda/tick/tick.mk, Linux + clang 21 + CUDA): the engine's own C compiled for nvptx64, host and device builds sharing names
make -C ../csrc gpu-tick-check [RECS=...] [S6_GROUP=8]   # every recording with S6 on the device (batches of S6_GROUP in one launch), each row's S6 digests against C's trace; --phase-kernel-cuda-diff also compares every page the device wrote with C's
make -C ../csrc phase-profile [RECS="a b"] [ROWS=1]   # per phase and sub-mark: user instructions (rank by these: load does not move them), time, allocations, per-tick p50/p99/max, the worst ticks and what they spent it in, envmem; default the GPU audit's twelve recordings
make -C ../csrc worklist-depth   # the deepest point of each explicit stack that replaced a recursion, over every test's data
make -C ../csrc stack    # every tick frame at most 4 KB, each tick root's worst chain under 32 KB (-fstack-usage over the call graph)
bash ../csrc/play/check.sh TAPE [SNAPSHOT]   # check a session played on out/native/play: Java replays its inputs, the native check compares every row (chain.sh: the seed-42 golden chain played this way)
make -C ../csrc gpu-render-judge    # the dev host: the judges' device frames (out/native/cuda/libframedev.so, which make test builds): play --shots draws the world frames on the 3090, every 16th again in C (PASS/FAIL device frame); a busy device falls back to C (play/framedev.h)
make -C ../csrc gpu-render-judge-check   # every frame judge's shot at 854x480, render distance 8, device against the C renderer byte for byte (JUDGE_ARGS="NAME...")
make -C ../csrc gpu-PART [gpu-PART-check]   # PART worldgen, lighting, meshing, render or tick: the part's build and its check (cuda/PART/PART.mk has its narrower targets)
make -C ../csrc/runtime pipe-gate [SIZE=WxH] [MESH=1|2] [DRAW=1] [PLAY=DIR]   # the RL pipeline's gate (pipeline_gate.list: every row and every device frame, bit for bit); SIZE the observation: 64x64, 96x96, 128x128 (the default), 192x192 and 256x256 are gated (SPEC.md's observation has their costs); PLAY= play's own frames at that size, made by bash ../csrc/runtime/pipe_play.sh -s WxH DIR NAME...
make -C ../csrc/runtime pipe-bench [SIZE=WxH] ARGS="--kinds village --ns 128 --device-mesh 1"   # env-steps/s with observations at that size (pipe_bench --size)
make -C ../csrc/runtime rl-move-gate [MOVERECS=...]   # the action compiler (moveact.h: hold_attack, look_target) through libnwrl: each lever's recording (oracle/tests/moveact.list) compiled again, script and rows equal to the oracle's; rl-move-record NAME=... re-records one
```

`ARGS=--detail` on any run adds per-entity forensics to every row (server
entity id:class:nbt-hash, entity-ID counters, particle count, call sites of
client entity constructions). Use it to find what a divergence is made of.

## Playing a full game

A whole game (fresh seed-1 world to the dragon's death and the exit
portal) is many sittings. `csrc/play/session.sh` plays one: it starts
where the last checked session ended, opens the native client, and when
you quit the window it checks the session on the Java oracle (Java replays
your inputs, the native engine replays Java's rows and compares every
field; 2 minutes of play checked in 22 s) and chains it: on PASS, Java's
Save and Quit at your last tick is the next session's start. The run's
state is `out/human/NAME/` on the dev host (`next`, `sessions.tsv`, the start
snapshots, tapes, logs, checkpoints); NAME defaults to `game`.

- Build, once. The dev host: `make -C oracle deps` (the canonical tree has it) and
  `make -C csrc play`. Mac: clone the dev host's repo, `brew install sdl3
  pkg-config`, `make -C oracle deps` (needs a 1.7.10 install from any
  launcher), same commit as the dev host (session.sh warns otherwise). The first
  session on the Mac copies play's scene assets from the dev host.
- Play: `csrc/play/session.sh` starts a new world (a new run,
  world-DATE-TIME, from fresh-play-s1); `--continue` plays the next session
  of the run you played last, `--run NAME` a given run (`game` is the first
  one), `--list` lists your runs. Quit (Cmd-Q or close the window) to end a
  session. On the Mac play runs locally and the check runs on the dev host over ssh.
- Your own keys, fov, brightness and mouse speed: copy a Minecraft
  options.txt (1.7.10's or a later version's, e.g. from a Prism instance)
  to `out/human/options.txt` on the machine you play on; session.sh passes
  it to play (`--options`). None of it is game state: the tape records the
  actions the keys make. Render distance and graphics stay the recording's.
- Status: `csrc/play/session.sh --status` (the run you played last, or
  `--run NAME`): each session's result, rows,
  the last checkpoint, whether you reached the End, the dragon, the exit
  portal.
- PASS: nothing to do; the next run continues from there.
- FAIL: the engine and Java disagreed (a bug in the native engine, not in
  your play). The session does not count: the next run starts that session
  again from the same place. The output names the first differing row and
  field and writes `out/human/NAME/play/NAME-sN-A.diverge`; send that file,
  the `.log` and the tape `NAME-sN-A.jsonl` beside it (all on the dev host).
  `csrc/tests/session-proof/run.sh` is the headless proof of the chain.

## A Java session as the judge (speedruns)

The other direction: play the real Java client and let the recording judge
the native client. `make -C oracle play SEED=1` (any machine with a display)
records, beside its tape `out/java/tapes/NAME.jsonl`, every keyboard and
mouse event the Java client took: `NAME.raw.jsonl` (java RawRec: each event
as it entered LWJGL's queues, the tick that took it, each frame's ticks and
camera motion). Close the window to finish. Then, on the dev host (from the Mac it
copies the two files there itself):

    csrc/play/rawjudge.sh out/java/tapes/NAME.jsonl

copies them to `out/java/rawjudge/NAME/`, plays the raw events through
out/native/play's own SDL input path from the same start (the world's join
snapshot; fresh-play-s1 for seed 1), and prints one PASS or FAIL line each
for the rows (every act the client's input made, then every state field,
against Java's row of the tick; and the engine on Java's acts), the screen
ops (the clicks the container screens made) and the frames (a Java replay
makes goldens every `--every N` ticks, 500 by default; the first run lists
the measured frames, `--accept` after a pxdiff review writes their budget
lines to csrc/tests/rawjudge_budget.txt). A directory there with
`tape.raw.jsonl`, `start` and its goldens runs in `make -C csrc test`.
Not judged: the cinematic camera (F8), keys the native client does not map
(chat, the player list, F-keys), a Mac recording's keys beyond `--mac-keys`
(Command as control), Escape in game (the oracle only releases the mouse,
the native client pauses), window focus changes. `oracle/tests/rawplay.sh
DRIVE TAPE [SEED [FROM]]` plays a session with a scripted stand-in for the
person (java RawDrive, LWJGL-level events); `csrc/tests/rawjudge_plant.sh
DIR` proves a planted mapping bug fails the judge at the right tick.
`csrc/tests/rawfuzz/run.sh [-j N] START:SEED...` generates stand-in
sessions (rawfuzz_gen, csrc/tests/rawfuzz/gen.c: walking, looking, the hotbar, drops, mining, fights, the
F-keys, one-frame chords, every container screen gesture), plays each on
the Java client from a fresh (fresh, fresh42) or checkpoint (SN, GN) start
and judges it (out/java/rawfuzz; review.sh surveys the frames with pxdiff,
keep.sh keeps a session as a rawjudge directory).

## Dev ops

Record a staged agent scenario with `make -C oracle script DEV=1 SEED=1
SCRIPT=tests/dev-give-s1.jsonl TAPE=../out/java/snapshots/dev-give-s1/tape.jsonl`.
Use `{"cmd":"run","class":"MobFree"}` and then `Snapshot` before any
`{"cmd":"dev","op":"give","item":267,"count":1}` entries. Dev entries run
in header order at the head of the next server tick. Allow two ticks for
packets to reach the client before input depends on a changed block, position,
or inventory (the client's mouseover runs before its packet pump, so a press
one tick after a dev tp aims from the old position). The snapshot must contain only players for whole-server replay.
Check with `make -C oracle replay REF=../out/java/snapshots/dev-give-s1/tape.jsonl`
and `out/native/test_snapshots out/java/snapshots/dev-give-s1`.

`--dev` is agent only. It writes `dev:true` in the tape and snapshot manifest;
the product `out/native/play` refuses either. `out/native/play-dev` accepts
`--dev-ops PATH`, a JSONL sidecar of ordered header entries such as
`{"tick":18,"class":"Dev","cmd":{"op":"time","value":13000}}`; it folds
them into the output tape header before tick 0. Java and native refuse a Dev
entry without `dev:true`. Legacy MobFree and MobMove entries are accepted in
old non-dev tapes. The supported gamerules are `doDaylightCycle` and
`keepInventory`; dimension travel and effects remain follow-ups.
`{"cmd":"dev","op":"orb","x":165.5,"y":90.5,"z":248.5,"value":11}` spawns
an EntityXPOrb; `summon` builds hostiles through their onSpawnWithEgg at the
local difficulty; `"child":1` works on Zombie, PigZombie and the ageables
(Pig, Cow, Sheep, Chicken, MushroomCow, Villager). A dev setblock or fill
over a container spills its items as the block's breakBlock does. A `slot` entry may carry `"name"` (setStackDisplayName;
a named name tag is the only kind that names a mob) besides `"ench"`.

Agent input is what a person at the vanilla client can do (Act.java's class
comment). Under the inventory screen an act's holds, presses and hotbar are
ignored; a gui op runs only under its screen (`click` and `close` on a
container screen, `respawn` on game over or credits, `wake` asleep, `trsel`
in the merchant) and is otherwise refused; a `hotbar` outside 0..8 or a
pitch outside -90..90 refuses the step. Each refusal prints `ORACLE REFUSE`
and is not recorded; a tape row carrying one fails on both sides
(csrc/tests/agent_values.sh). An attack press on the tick a screen closes
is dropped in vanilla too (leftClickCounter 10000): press it a tick later.

## Rules

- The oracle is vanilla 1.7.10 behavior plus the pins in SPEC.md and the
  `config.yaml` switches. Deleting vanilla code is fine; changing behavior is
  not, unless it is a documented pin or a switch.
  After any edit under `oracle/src`: `make gate` on the dev host, all PASS, and
  `make oracle-patch` (commit oracle/decomp/oracle.patch with the edit) before commit.
- No mutable globals in the native tick (two environments share one process):
  state goes in struct env (`csrc/engine/env.h`, reached through `nw_env`), a
  world, or the replay's per-dimension state (`sr->d`); a table is built
  before main and only read after. `make -C csrc globals` fails on any
  writable global that `csrc/tests/globals.allow` does not name.
- No allocation in the native tick and no entity addresses kept across
  ticks: livings, list entries, item-world and falling/hanging entities,
  paths, village doors and chunk bands come from the env's arena pools
  (`csrc/engine/arena.h`); what one entity keeps of another is a handle
  (`lref`, `ieref`, `fhref`, a pool slot, a pathref), never a pointer; tick
  scratch too large for a frame is the env's (`nw_env->scratch`, the scoped
  COLLIDE_/ACT_/EXPL_SCRATCH stacks; a pool worker's scratch serves every
  env it steps, so nothing in it outlives its call: no slab block, no
  address in an env, lane/slabfix); an array that grows is a
  `fixed_array` made with its owner. Check a change by counting malloc per
  in-tick call stack (an LD_PRELOAD hook, DEVLOG 2026-09-26 lane/gpuarena).
- A tick frame is at most 4 KB: a bigger local is declared `ENV_LOCAL` and
  taken from the env's scratch stack (`csrc/engine/envstack.h`), and a
  packet is filled in its queue's slot (`s2c_add`). `make -C csrc stack`
  fails on a frame over 4 KB or a tick root whose worst chain reaches 32 KB.
- No recursion in the native tick: a block callback (blockcb.c, the liquids,
  the schedule) emits its nested writes, flag-1 metadata writes and
  notifications to the block worklist (csrc/engine/blockwl.h) instead of
  calling world_set_block and the rest, and stops reading the world once it
  has emitted. `make -C csrc callgraph` fails on a cycle outside worldgen.
- Iteration order is data in the native tick: a JDK container whose order is
  game state is kept as keys plus what fixes their order (insertion order,
  list stamps, a capacity) and the order is computed (csrc/engine/jorder.h,
  chunkset.h, potion_map_order), never walked off hash chains;
  `make -C csrc order` checks each against the emulation it replaced. A
  chunk enters a server world, and generation runs, only through
  world_chunk_request (world.h), which records the request and generates
  through the world's chunk_backend.
- No Forge, no Mixin, no Gradle. Hooks stay small and call into
  `netherite.oracle`; logic lives in the harness.
- This repo contains decompiled Mojang source. It must never be pushed to a
  public remote.
- What a world is lives in one YAML file: `config.yaml` at the root, or a
  named one in `configs/` (`configs/headline.yaml`, the RL headline world:
  peaceful, render distance 6). Its sections: `world` (seed, difficulty,
  render_distance, the game rules daylight_cycle and keep_inventory, the
  feature switches), `client` (graphics, particles, the pipeline's
  observation size, ticks_per_step, hud), `engine` (device_mesh,
  device_generation, device_light: runtime choices that never change a row)
  and `pipeline` (kind and start: the snapshot the pipeline's envs start
  from, recorded from that file; worlds, groups, cards and episode_seconds,
  an RL run's envs), and `actions` (the engine-side action compiler's
  switches: anything but their defaults is refused as not implemented yet,
  csrc/engine/worldconf.c wconf_unimplemented). One RL run is one run file
  (configs/speedrun-grpo.yaml) with two parts at column 0: `sim` (those
  sections, two spaces in, entries four) and `policy` (the trainer's model,
  placement, heads, GRPO, reward and out, read by mcsr-speedrun's
  mcsr/runconf.py; `nw_grpo.py` and `nw_grpo_async.py --run FILE`). A file
  without parts is sim.
  The grammar is a strict YAML subset
  written in config.yaml's header; the Java oracle (`WorldConf.java`, the
  key registry) and C (`csrc/engine/worldconf.c`, the same table, checked by
  `make -C csrc worldconf-check`) parse it exactly, no library, and read
  sim only (policy is held to the grammar and skipped). Java:
  CONF=path, SET="key=value" ([sim.]section.key or a bare world key); the runtime:
  `pipe_bench`/`pipe_gate --config FILE` (the flags override it),
  `pool_gate --world FILE`, `make -C csrc/runtime pipe-gate CONFIG=FILE`,
  and every start is held to the file's world. A tape records the resolved
  world: the header's `world` (seed, switches, a game rule only when not
  vanilla's) and `options.difficulty`/`options.rd` (the client options they
  are in 1.7.10). A feature the native engine lacks stays in
  `WorldConf.UNSUPPORTED`: forced off in the oracle too, with a "not
  supported yet" notice. Other runtime behavior is set by flags to
  `netherite.oracle.Main` (via make variables). Never environment
  variables.
- No Python in the repo (Elliot, 2026-10-01): the tools are C, built by
  csrc/Makefile into out/native (index_check, pxdiff, mirror, callgraph,
  smoke_select, testmap, testsel_history, fncov_recs, play_check_sample,
  playfuzz_gen, playfuzz_ends, playfuzz_agentgen, playfuzz_coverage,
  rawfuzz_gen; csrc/tests/tool.h holds what they share). `uv run
  --no-project --with ...` only for throwaway analysis outside the tree.
  `jq` for tapes.

## Fan-out (the working method - parallelize by default)

This project is built by many agents at once, not one at a time. Serializing
independent work is the main way to waste a night (learned 2026-09-24: the
render passes fog / water / HUD / sun-moon were run one after another when they
were independent and should have run together). Before starting any multi-part
task, run this loop:

1. **Find the isolatable pieces.** Split the task into sub-tasks that can be
   *verified independently* - each has its own acceptance and does not need
   another's output to check. Mob ports, render passes, worldgen features,
   scene recordings, and per-dimension checks are all isolatable. If two pieces
   only touch each other through one shared file, they are still isolatable
   (see collisions below).

2. **Sandbox and brief each; fan out as wide as the pieces allow.** Every piece
   gets its own lane (`make lane NAME=x`, a git worktree), a brief with its own
   gate command, and its own model, launched in parallel - as many as make
   sense, not one at a time. Collisions on a shared file (raster.c's dispatch,
   living.h's kind enum, hostiles.c's per-kind switch) are handled the way the
   mob and render fan-outs do: each piece owns its OWN new file and adds a
   single dispatch/table/enum line to the shared file, and the merge queue
   resolves those one-line conflicts (keep every piece's line). Pre-declare the
   shared slots (enum kinds, a pass list) on master first when you can, so the
   lanes do not touch the shared file at all.

3. **Route the one hard track to a strong model and let it iterate.** When a
   piece is a genuinely tough problem (a subtle Random-order bug, matching
   llvmpipe's raster), give it to Opus 5.5 at high effort, or GPT-6 Sol at
   high effort when codex has quota, and let it run for as long as it takes.
   Do not split a hard problem across lanes to parallelize it; it needs the
   long single context. (2026-09-24: Sol high solved the world-tick LCG bug
   and the renderer bring-up, where cheaper models could not.)

4. **Unroll the tedious track.** A long, easy, repetitive track (many similar
   ports, many scenes) is unrolled into independent lanes that run at once:
   Opus 5.5 at medium, or GPT-6 Sol medium and Luna when codex has quota
   (see Model routing). Where a real
   dependency exists, order the steps into a pipeline so each finishing step
   unblocks the next, rather than blocking the whole batch on one. A
   verification-only lane (record scenes, run the check, report the gaps) never
   edits shared code, so it always parallelizes freely and its report becomes
   the next fan-out's target list.

5. **Everything is testable.** Every lane carries an acceptance command
   (`make -C csrc test`, a pxdiff gate, a byte-identical recording). A lane
   with no gate is not a lane. The merge queue's trial re-runs the full suite
   before anything reaches master, so a wide fan-out stays safe. Which model
   takes which lane is the next section.

6. **Every divergence goes in the ledger.** A lane that finds or fixes a
   divergence adds its row to `csrc/tests/divergences/LANE.tsv` (the
   header and the fields are in `csrc/tests/divergences.sh`: id, found, by,
   area, field, cause, status open, fixed, accepted or superseded, commit,
   pin, repro; one row per cause, the pin recording's name as the id).
   Java -> C (the C engine, csrc/engine, against the Java oracle): area one
   of worldgen, light, blocks, liquids, mob-ai, mob-spawn, mob-other, physics,
   items, containers, combat, survival, dimension-travel, save-reload,
   client-sync, client-state, render, input, gui, network, oracle, other.
   C -> CUDA (a device kernel in csrc/cuda, or the pool's result from
   csrc/runtime, against the C engine): area `cuda`. A fuzz lane adds its
   rounds to `csrc/tests/divergences_rounds.tsv`. The merge regenerates
   `divergences.md` from them (`bash csrc/tests/divergences.sh`,
   out/flywheel/bin/divgen-post.sh): what is fixed, what is open with its
   repro, what is accepted by design, what no recording or fuzzer reaches,
   and the find rate. Never edit divergences.md by hand.

## GPU kernel lanes: the loop (Elliot, 2026-09-29)

C is the GPU's oracle; Java only checks C. A CUDA lane never runs Java.
Correct naive kernels first, performance once at the end, hill climbing
after that against the same gates. Every GPU lane runs this loop:

1. One unit per lane: a kernel, or one family of a kernel (the mesher's
   liquids, say), in its own file under `csrc/cuda/PART/` with its own
   `PART.mk` (csrc/Makefile includes `cuda/*/*.mk`), so parallel lanes
   never edit a shared file; a dispatch table takes one line per unit.
   Name the file by its stage (no part prefix: the folder says it), the
   kernel by its stage, unique across csrc/cuda, and its targets
   `gpu-PART...` (Layout).
2. Naive and correct: port the C reference functions (csrc/engine), or
   compile the C itself for the device (L17's route). No tuning.
3. (During a port; the default build is -O3 since 2026-09-29, `PERF=0` for
   this.) Build the device code at `-Xcicc -O1`, the correctness build: the light
   engine's unit compiles in 4.4 s against 20.7 s at -O3 (cicc 8.7 s and
   ptxas 8.9 s on one inlined 79,501-line PTX kernel), with every light call
   byte-equal to C either way (2026-09-29). -O3 is the performance build
   only. Keep `-rdc=true` (without it ptxas took 34.8 s). Units stay small
   so an edit recompiles one unit; unchanged units come from ccache.
   csrc/Makefile does this for every device build (NVOPT; `PERF=1` builds
   at -O3, and switching back comes from ccache); the units compile in
   parallel under the memory budget (NVJOB). Never include cub in a unit
   you edit: its headers cost about 4 s a unit (the light engine has its own
   block scan, the renderer keeps cub in render/sort.cu).
4. Quick check, seconds: one recording's stored C side through the kernel,
   compared byte for byte: the kernel's `edit` line in `mirror.tsv`. C's
   side of every device check is stored once per recording and C build and
   replayed on the device alone (lane/cudasplit), so the check never reruns
   C for a device edit: gpu-lighting-check keeps each recording's capture
   (`test_snapshots --light-cap save:FILE`, zstd) in
   ~/.cache/netherite-devcap/light and `out/native/cuda/lighting_check` replays
   them, a stream each, in one context; gpu-worldgen-overworld-check and
   gpu-worldgen-dim-check keep C's stage results as hashes and the seed worlds'
   spawn lists (C runs only for a chunk the device gets wrong, to name the
   cell); the render gate keeps play's dumps once C redraws them
   (`render_check --stored`). The keys are the C binaries' hashes and the
   recordings' files, so a C change makes them again. `FRESH=1` redoes C
   (the nightly); `LIVE=1` runs gpu-lighting-check the old way (the device beside
   each C replay). The judges' gate (gpu-render-judge-check) stays live: it is the
   renderer inside play.
5. Repeat 3 and 4 until equal. An iteration is about 10 s: never rebuild or
   rerun more than the unit and its check.
6. Quick gate, about 20 s: the kernel's line in `csrc/cuda/mirror.tsv`
   (a few recordings), byte-equal.
7. Commit. The full gate (every recording the kernel covers) is not the
   lane's: while `out/flywheel/defergates` exists (Elliot, 2026-09-29:
   land the kernels, run the gates once at the very end, then
   whack-a-mole), `out/flywheel/bin/batchgates.sh` runs every kernel's
   full gates and the full suite on master at the batch's end and names
   each failed kernel with the merges that touched it.
8. Register the kernel in `mirror.tsv` (the C functions it mirrors, its
   build, quick and full gates): the merge trial builds every kernel's
   `build` line in a clean tree, a C change that reaches it runs its quick
   gate once the deferral ends, and the batch run and the nightly run
   every full gate.
9. Finish: merge master, `make -s -C csrc clangcheck`, `make -s -C
   native test` if you changed csrc/engine or csrc/play (test selection
   reruns only the jobs whose code changed), and build and run your quick
   gate in a fresh scratch worktree of your branch (a tree without your
   out/: the merge trial's is one, and twice on 2026-09-28 a GPU lane's
   build only worked in its own tree: a missing mkdir, a missing rule),
   then report and merge.msg. The merge trial of a lane that changes only
   csrc/cuda and csrc/runtime (and documents) builds and lands, the
   suite deferred with the gates; a lane that changes no oracle/ rides a
   C-only merge train (no Java gate, no self-check).
10. Performance only at the end, for everything together: one -O3 build,
    measured once; then kernel hill climbing, gated by the same checks.

Measured costs to beat, not to accept (2026-09-29, load about 180): a C
edit rebuilds in 0.3 s (one test program) to 2.7 s (everything); an
unchanged CUDA build is 0.1 s; gpu-lighting-check's quick gate replays 6.16 M
calls in 18.9 s. If a step in this loop takes longer, time it
(`nvcc --time FILE`, `/usr/bin/time`) and fix the step before going on.
After lane/cudasplit (2026-09-29, before and after at the same load): one
light-engine function edited, rebuilt and checked (`edit` line) took 14.0
to 14.6 s before (10.3 s rebuilding lighting/replay.cu whole at -O3, 3.7 s for the
live check) and 6.8 s after (0.6 s, 6.2 s of -O1 device replay) at load 4;
one renderer function 30.5 to 32.8 s before (16 s, 15 s) and 8.9 to 10.2 s
after (1.7 s, 7 to 8.5 s) at load 20. A fresh worktree builds every kernel
from ccache in 8.4 s (270 of 270 hits). Every device unit compiles in 0.4
to 2.5 s at -O1 (cub's render/sort.cu 5.7 s, once); the check is now the
device's replay, and -O1 replays the light engine 20 to 40% slower than
-O3 (the split itself costs nothing measurable on the device).

## Compiling and measuring fast (learned 2026-09-29, every number measured)

A performance lane spent more of its hour building and benchmarking than
thinking (37% "benchmarks", mostly builds inside them; 35% the model; 17%
gates). What made it faster, and what did not:

- An attempt is about a minute (Elliot, 2026-09-29 7:30 pm: 10x less time).
  Measured that day, an attempt took 5 to 14 minutes (meshgpu 5.4, envmem 5.1,
  popspeed 7.3, genphase 10.4, tickmem 13.6), of which the model was 18 to 35%,
  waiting in `until` loops for queued benchmarks 13 to 30%, benchmarks 6 to 30%
  and gates 11 to 35%. So an attempt is: edit, build the one unit (seconds),
  the unit's quick check (one recording, the `edit` line), and a measurement
  that needs no interleaving: the unit's microbenchmark on stored inputs (a
  kernel's device time on captured dumps, as lane/meshgpu's render_check
  --seq on its mgdumps) or user instructions on one replay (--counters; load
  does not move them). Not in an attempt: a 128-env run (the GPU host's one timing
  CPU set holds one at a time: 54 minutes waited against 31 run in one hour),
  re-measuring master's baseline, the full recording set or all 40 pool
  references (test selection reruns the jobs your change reaches), a full
  phase-check, nsys unless the question is where the time goes. env-steps/s
  at 32 envs for a kept change; at 128 once, at the lane's end, one run per
  tbench.sh call (the big-run queue is first come, first served). When the
  model's turn is the cost, write several variants at once and measure them
  together (the idle GPUs and the build cores take them in parallel).
- Find the limiter before optimizing anything: measure env-steps/s end to end
  (pipe_bench), where the host and the device spend each env-step, and aim at
  the part that bounds it. A kernel 30% faster moved env-steps/s 2 to 5% while
  the host bounded (lane/meshkern, lane/meshsplit); the upload path and the
  host each doubled it when they were the limit.
- The default build is -O3 (PERF=1 is the default since 2026-09-29, Elliot):
  the kernels a benchmark measures are the ones already built and cached, so
  a measurement never rebuilds anything. PERF=0 is the -O1 build, only for a
  long correctness-only iteration on one unit. Never switch levels inside an
  attempt: switching rebuilds every unit (NVSTAMP), and a lane that built -O1,
  checked, rebuilt -O3 and timed paid 100 to 163 s an attempt.
- nvcc runs with --split-compile=16 (csrc/Makefile NVSPLITN): a unit's
  optimizer and assembler split by function, render_kern.cu at -O3 30.3 s to
  17.7 s. 32 and 64 threads measured the same as 16: the largest single kernel
  is the floor. So keep kernels small and one family to a unit, and do not
  compile one huge kernel twice (render's count and write templates, over
  4,000 SASS instructions each, set that floor until lane/renderbuild split
  render_kern.cu by family, gave each pass one clip site and made the count
  passes clip clip coordinates alone: each unit's -O3 edit 1.8 to 2.6 s).
  render/raster.cu compiles unsplit (split, pixel() leaves raster's
  64-register bound at -O1).
- Not the cost: -rdc (22.2 s with it, 25.2 s without), the device links (1.6
  and 3.3 s), --fmad=false and the precise division and square root (they
  shape the output, not the build time; they stay).
- A constant you sweep goes in the unit that uses it, not a header every unit
  includes (an edit to render/dev.cuh rebuilds every render unit).
- Gates: a quick gate each attempt (pipe-gate over three recordings,
  `PRECS="cov-villagerep-s42 nether-trip-s1 end-dragon-s1"`, and the unit's
  quick check); the full gates once for a change you keep. Independent gates
  run at once (`tbench.sh GPU -gate ... &` then `wait`): in series behind the
  GPU lock a full gate set took 217 s. Make the references first
  (`make -C csrc/runtime pipe-refs`): gates started together on a fresh
  tree make the same references at once and print `ref NAME: FAIL` as
  their temporary files collide.
- Timing (lane/benchfix, 2026-09-29): an A/B is a fixed corpus, the
  default mode: `pipe_bench --kinds village --ns 128 --device-mesh 1`
  runs every env 100 steps unmeasured and 1000 measured (`--warm W
  --steps S`) of the same trajectory whatever the build's speed, and prints
  its `trajectory digest`: two builds are compared only on equal digests.
  `--seconds T [--warm-seconds U]` is the capacity mode (a trainer's steady
  state): a wall-time window is not the same work for two builds (a faster
  one reaches other game states), so never an A/B. Each result line names
  its mode, `--policy` (2, symmetric look deltas, the default since
  lane/benchfix; 1 the walk before, which drifted to look up) and global
  env ids (`--env-base E`: two processes of 64 with E 0 and 64 run the
  envs of one of 128). A run with an error or a missing frame prints FAIL
  and exits 1. `--profile` adds the hardware counters (instructions,
  cycles, the view hooks' share: off by default, they are syscalls in the
  hot path) and the device's busy time as the union of its kernels and
  copies on every stream (CUPTI); without it the renders' summed CUDA
  events are printed as what they are (overlapping, with the host's
  staging in them), never as utilization. Before and after interleaved in
  one session on the same GPU and CPU set; a number from another session
  or partition is not a baseline.
- CPU contention: a render_kern.cu -O3 compile took 22 s quiet and 75 s beside
  a 96-thread benchmark, and the builds disturb the benchmark as much. On
  the GPU host timings run on cores 0-31 and their siblings (64-95), the agents,
  builds and gates on cores 32-63 (96-127), set by CPU affinity (tbench.sh;
  claudelane.sh; user systemd has no cpuset controller there). A 16-core
  build side was too few (a -O3 render build took 219 s).
- Memory: every benchmark, profile and gate runs through
  `out/flywheel/bin/tbench.sh GPU [-big|-gate] -- CMD` (its own scope outside
  the agents' slice, first to die, no swap): two 128-env runs beside four
  agents once filled the slice and the OOM killer took two agents. -big (over
  32 envs) holds the machine-wide big lock and may use 40 GB (a 128-env run
  with a seed per env takes 30.7 GB). Lane agents are capped at 16 GB; run
  the native suite at TESTJ=6 inside it (a play replay is 0.4 to 0.6 GB).
- On the GPU host do not run the full native suite at the finish: the dev host's merge
  trial runs the selected suite before anything lands (a GPU host suite took 10
  minutes). The GPU host's copies of four retired recordings (combat-pig-s1 and the
  three replay2-*) fail there on master too.
- Profilers: Linux perf for host code (instructions:u, cycles:u, DRAM fills,
  call graphs; rank by instructions, load does not move them); nsys for
  timelines (TMPDIR=$HOME/dev/nw/.tmp); Nsight Compute for kernels (the GPU host
  lets any user read the counters; on the dev host only through
  `out/flywheel/bin/ncuwrap.sh`, a sandboxed sudo). Symbolize perf samples by
  unique address, not one addr2line call per sample (a lane waited 10
  minutes on that).
- The orchestrator reads every lane every 5 minutes
  (`out/flywheel/bin/lanepulse.sh`: the running command and its age, tool
  against model time, the slowest command, the latest measured row) and
  fixes what slows it; a GPU host lane's end and any OOM kill are watched
  separately (the dev host's events.sh sees only the dev host's lanes).

## Model routing (pick by task complexity, then keep re-checking)

Only two model families work on this project (Elliot, 2026-09-25): Opus 5.5
through headless Claude Code, and GPT-6 Sol and Luna through codex. No other
model or provider. The launchers live on the dev host (and the GPU host, for GPU lanes) in `out/flywheel/bin/`
(untracked); each writes `out/codex.{start,end,rc}` and `out/last.md` in the
lane, runs inside `nv2.slice` with a per-lane cap (36G on the dev host, 16G on the GPU host with LANEMEM, benchmarks outside it), puts the lane's
`kill`/`pkill`/`killall` shims (`out/flywheel/shim`, which act only inside
the lane's own scope) on its PATH, passes the brief as a file rather than on
the command line, and pins `TESTJ=16`. Agent transcripts go to
`out/flywheel/traces/<lane>.jsonl`, outside the lane tree, and are for
`lanetrace.py` only. A lane keeps its working notes in its own `out/` as it
goes, so a restarted lane resumes from git and those notes.

- **Opus 5.5** (`claudelane.sh LANE medium`, `high` for the hardest). The
  default for implementation: divergences with an unknown cause, a system
  ported into the loop (a dimension, the dragon), cross-cutting work
  (snapshots, the playthrough bot, the GPU refactors), audits, merges that
  need judgment, and the codebase index. Evidence 2026-09-25: it resolved
  the audit fixes, the stronghold, the 22 parked re-recordings, the light
  memo and the first CUDA phase each in a few hours, clean at review. Draws
  on Elliot's Claude usage: when he says he is near a limit, stop launching
  it.
- **GPT-6 Sol** (`codexlane.sh LANE gpt-6-sol medium`, `high` for the
  hardest), when codex has quota: the same class of work as Opus; Sol high
  solved the world-tick LCG bug and the renderer bring-up. **GPT-6 Luna**
  (`codexlane.sh LANE gpt-6-luna low`): high-volume reading, extraction and
  classification (index records, trace scans, pixel-diff sorting). Codex is
  out until 2026-09-28 9:28 pm MT.

Move a lane to the other model family over its own work (stop its scope,
confirm the worker is dead, relaunch with the brief plus a continuation note
and the lane's git state) when it has no commit after about 3 hours on a
hard item, repeats one command (lanetrace flags `REPEAT`), or ends
unfinished twice. Every lane still needs the reviewer: recordings under
out/java/snapshots, no debug getenv, no writes outside the lane.

Keep routing honest by reading the traces, not by impression.
`uv run --no-project python out/flywheel/bin/lanetrace.py [LANE...]` prints
each running lane's model, hours, steps, seconds per step, model-wait share,
tool calls per step, edits, steps per edit, commits and any repeated command.
Every merge appends the lane's line to `out/flywheel/routing.tsv`. Run it on
each lane end and every couple of hours; when a model's hours per merged
lane drifts, change the brief or the route, and record the decision and its
numbers in DEVLOG.md.

## Flywheel speed (Elliot, 2026-09-25: no step should take over a second)

A lane's wall clock is mostly tool runs, not the model (7 to 23% model time
on 2026-09-25), so every slow command is a flywheel bug. Measure, then fix
the slow thing rather than waiting on it.

- Lane agents run with `out/flywheel/lane-settings.json`: a hook
  (`bin/lanehook.py`) times every Bash command into
  `out/flywheel/tooltime/<lane>.tsv` and tells the agent the time, the lane's
  age and the last command's length every 2 minutes, and at once after a
  command over a minute.
- The orchestrator watches `bin/status.sh` (every 2 minutes: memory, load,
  live lanes, the merge stage, commands running or finished over a minute,
  lanes idle over 10 minutes), and every 30 minutes reads `bin/prof.py 30`
  (tool time by command class, exact time per make target from the `make`
  shim's `tooltime/<lane>.make.tsv`) and turns the top class into tooling.
- A fresh checkout builds from caches (lane/trialcache): every C compile goes
  through ccache (~/.cache/ccache; `CCACHE=` turns it off) and objects no
  longer name their checkout, so any tree's compile serves any other's, byte
  for byte; a Java build whose exact sources were fully built before unpacks
  the classes from the class store (oracle/tests/classtore.sh,
  ~/.cache/netherite-classes) instead of running javac. A merge trial's
  worktree builds java, native and play in 1 to 13 s by load where it took
  30 to 80 s.
- The oracle result cache (oracle/tests/oraclecache.sh, lane/oraclecache) sits
  under `make script` and `make replay` and so under everything built on
  them: a run whose harness, JVM, input bytes (script, tape, config.yaml,
  checkpoint) and output-shaping arguments were run before restores its
  outputs and prints its log in about a second, without a JVM; a miss runs
  and is stored if its rc is 0. Change only C code and every Java run you
  repeat is a hit. `CACHE=0` runs fresh (the gate and the pool's proofs
  do; any byte-identity or determinism check must). `make -C oracle
  cache-stats` prints entries, hits and the oracle time saved.
- A warm pool (Pool.java) takes `make script` and `make replay` runs in a
  long-lived JVM that rewinds its heap between runs: no JVM or client start,
  a warm JIT, and the last run's world prelaunched, so a repeated seed-1
  script reaches its first command in about 0.3 s (lane/warmoracle). Nobody
  starts it: the members are machine-wide (oracle/tests/poolauto.sh, under
  ~/dev/nw/oraclepool), keyed by the harness hash, so every tree on one
  harness (most lanes: master's) shares them; a run that finds none free runs
  fresh and starts one in the background, at most POOLMAX (half the memslot
  budget over POOLMAXMB: 12 on the dev host) and half the memory budget together;
  a member retires after POOLIDLE (600 s) idle or past POOLMAXMB (2500)
  resident. `make -C oracle pool-status` lists them (lane/autopool). A tree
  that edits oracle/ gets members of its own the same way. Members run the
  session checks' SAVEEND replays and diverge's SNAPAT ones (lane/poolsave).
  A pooled run that throws (its command or launch, any thread, an
  OutOfMemoryError: every oracle JVM runs -XX:+ExitOnOutOfMemoryError) is
  never answered as a pass: the member names the error and ends, and
  pool.sh runs the job again in a fresh JVM (lane/poolfix).
- Memory: every suite job and every oracle JVM reserves its last measured
  peak RSS from one machine-wide budget (csrc/tests/memslot.sh,
  /tmp/netherite-mem/budget in MB, peaks in /tmp/netherite-mem/peaks), so
  work queues for memory instead of being OOM-killed. A job is admitted
  while nv2.slice's real use (memory.current less its page cache) plus what
  the live jobs may still grow by (a reservation less its resident size,
  while the job ramps) plus its own reservation fits under the budget less
  3000 MB (/tmp/netherite-mem/margin overrides; lane/memslot2). Oracle JVMs
  together take at most half the budget (one alone may take more), so a
  lane running many at once cannot starve the suites and merge trials.
  Oracle JVMs run the serial collector (JVMGC in oracle/Makefile: 0.6 to 1.5
  GB each). `bash csrc/tests/memsample.sh OUT` samples the slice against
  the reservations (a row a second, a row per job) to re-measure the rule.
- The merge trial's oracle self-check replays only the recordings that ran
  code the merge changed (`make -C oracle selfcheck SELECT=1 COV=...`,
  oracle/tests/SelfSelect.java, lane/selfselect): master's coverage data
  (out/java/coverage, refreshed after every merge by out/flywheel/bin/covmap.sh,
  lane/selfselect's patch to the merge scripts)
  names the methods each recording runs, fingerprints of the classes
  (code and structure per member) name what changed, and anything it cannot
  map (the pool's own classes, tests/selfselect.racy, the replay recipe)
  runs every recording with a FALLBACK line. SELFCHECK=full runs them all.
- On any recording failure run `out/native/diverge NAME [ROW]` first (one
  screen: the field, the rows before it, the whole native state against the
  oracle's at that tick, the first differing entity's history), and re-check
  one late row with `test_snapshots --from-row N DIR` (keyframes: `make -C oracle keyframes RECS=NAME`).
- Several recordings at once: `out/native/recs NAME 'gold-g2*' ... [-- flags]`
  replays them in parallel under the memory budget, one line each (OK with
  rows and seconds, or the first FAIL), never a serial for loop.
- Several oracle runs (script variants, replays, records) at once:
  `out/native/par [-j 4] [-g REGEX] 'make -s -C oracle script ... TAPE=../out/java/try/{}/tape.jsonl' A B C`
  runs the template per item in parallel (the JVMs share the memory budget),
  one line each with rc, seconds and the last (or REGEX-matching) line.
- Work with the narrowest check (the recording being changed, one test);
  the merge trial runs the full suite in the background beside its other
  gates.
- A refactor lane (a change every recording must survive unchanged) runs
  `make -s -C csrc smoke` after each edit and the full suite
  (`make -s -C csrc test`) before each commit. Smoke is under 15 s: the
  globals and order checks and csrc/tests/smoke.list's jobs (every test
  binary, every (phase, state class) pair any recording writes, then the
  most src/ lines per second the suite runs); `make -C csrc smoke-select`
  rewrites the list after recordings or tests are added.
- `make -s -C csrc test` replays a job whose executed functions, read
  files and recording are unchanged since a recorded pass (TESTSELECT,
  lane/testselect): a one-function fix reruns only the jobs that ran that
  function. The records come from `make -C csrc test-map` (every job on
  a -finstrument-functions-once build under strace, into
  ~/.cache/netherite-testcache/sel; incremental: only jobs whose record
  no longer holds, 7 min for all of them at load 200); run it after a
  merge on master, or in a lane after merging master. A change to a
  struct every program touches (env.h) reruns everything, correctly.
  `TESTCACHE=` (the merge trial) still runs every job.
- Everything but GPU work runs on the dev host (since 2026-09-26 11:59 pm): lanes,
  master, the merge queue, the Java gate. Render goldens were captured with
  the GPU host's Mesa 26.0.3; the dev host's 26.0.8 gives byte-identical oracle frames and
  rows (fight-zombie-s1, frames every 100 ticks, checked at the move). GPU
  lanes run on the dev host's RTX 3090 (csrc/Makefile CUDA_DEV defaults to 0,
  CUDA_ARCH follows the device).
  nv2.slice is 48 GB on the dev host with a 32000 MB job budget; the GPU host is shared
  with another project's data jobs (its nv2.slice 44 GB, the job budget
  30000 MB, dropped to 5000 by out/flywheel/bin/memguard.sh when the GPU host has
  under 8 GB free). Measured baselines: native build 2 s, Java build 3 s, the
  full suite 78 s on an idle 32-thread host, a native replay about 2,200 rows/s
  (night-s1, lane/replayspeed), the Java oracle about 1,200 ticks/s after a
  3.6 s start (lane/oraclespeed; it was 113, held by options.txt's maxFps
  120 in agent mode). A short recording is now mostly JVM start and world
  load.

## Gotchas (each cost real time on 2026-09-22)

- Porting to C: a decompiled double literal like 0.20000000298023224D is a
  widened float ((double)0.2f); copy it as printed, never as 0.2 (one float
  ulp off otherwise). Java evaluates operands left to right; split any C
  expression that draws twice from one Random. Build with -ffp-contract=off.
- A frame or a tick that depends on how many frames were rendered is a bug.
  Several vanilla client paths did this and are pinned in agent mode (DEVLOG
  2026-09-22). If a replay diverges only in `px`, look for another per-frame
  smoother, cache or counter before suspecting state.
- `Entity.nextEntityID` is one JVM-global counter in vanilla. The oracle gives
  each thread role its own range; do not reintroduce a shared counter.
- The static RNG names in `determinize.sh` ("./net/...:field") seed streams;
  changing them changes every tape.
- Concurrent runs need separate game dirs (make does this per tape name). Two
  runs with the same TAPE basename clobber each other's save.
- `pgrep -f pattern` also matches the shell that runs it. Match the java
  binary (`^[^ ]*/bin/java `) or wait on PIDs.
- The dev host's and the GPU host's login shells are zsh: `/dev/tcp` needs bash, and `set -- $var` does
  not word-split in an `ssh devhost '...'` one-liner. Put multi-step remote
  logic in a bash script under `oracle/tests/`.
- Ubuntu OpenJDK 8 needs `-Djavax.accessibility.assistive_technologies=` or
  AWT dies at startup looking for AtkWrapper (the Makefile passes it).
- The Mac has no arm64 LWJGL 2.9.1; `make deps` there takes the 2.9.4 jars,
  arm64 natives and Zulu JDK 8 from the v1 checkout (`V1=~/dev/netherite`).
- The GPU host `/home` was 94% full at start; another agent owns the storage fix.
  Keep `out/` small (tapes with `--detail` grow fast).
