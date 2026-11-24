# netherite (AGENTS.md)

How to build, check and report in this tree, and the rules that bind. The long form of this file (the command
ledger and the lane machinery of 2026-09, written for the machines of that month) is in git:
`git show df2a60c5:AGENTS.md`.

## If you are an agent new to this repo

What this is: Minecraft 1.7.10 reimplemented in C (`csrc/engine`, the engine) and CUDA (`csrc/cuda`), checked
bit for bit against the real Java client (`oracle/`, the oracle: Mojang's client decompiled on your machine, plus a
small harness in `oracle/harness`), with a batched RL pipeline on top (`csrc/runtime`). The Java side is the
reference for every behaviour; the C engine is right when its rows equal Java's for every tick.

Setup, once: `make` builds `out/bin/nether` (the CLI; `nether help`, `nether VERB --help`), then
`out/bin/nether setup` finds the user's Minecraft 1.7.10 install (`--mc DIR`), copies its libraries and jar,
rebuilds the oracle's source (MCP 9.08's decompile in Docker, once, then `oracle/decomp/oracle.patch`) and the
textures (`out/assets`) from it, and builds everything. It needs JDK 8, make, a C compiler, jq, unzip, perl,
git, curl and Docker; SDL3 for the playable client; Linux, CUDA and clang for the GPU paths.

Training, from there: `nether train configs/tests/brute16fast.yaml [NTLOOP ARGS]` (the native trainer; it makes the
trainer, the textures, the cast's atlas and the run file's start where they are missing: `make -C csrc/runtime
train-prep RUN=...`), `nether score RUN.yaml W.ntw [RUNS MIN SEED]` (the frozen eval) and `nether replay
EPISODES.nrec EPISODE` (a recorded episode played on the Java client, its end checked, frames and an mp4).

Never: commit Mojang's code or assets (`oracle/src` is generated and ignored; `out/` holds the jar, the textures and
every recording), push decompiled source anywhere, add Python to the tree, or change vanilla behaviour in the
oracle except by a documented pin (SPEC.md). Edit `oracle/src` only to add a hook call into `netherite.oracle`, then
run `make -C oracle oracle-patch` and commit `oracle/decomp/oracle.patch`. `tools/export.sh DIR` makes and checks the
public copy of the tree.

Where the rest is read: `SPEC.md` (the architecture and its decisions), `GOAL.md` (the goal and its hard rules),
`DEVLOG.md` (the history, newest entry first: what was decided, with its numbers), `README.md` (the public front
page), `docs/benchmarks.md` and `docs/scripts.md`. These and this file are the only project markdown: add no other.
`index/` maps every Java source to the C that ports it and back (`make -C csrc index-check`); a lane that changes
what a file ports updates its record and runs the check.

## Layout

- `oracle/` is the Java oracle, built by `oracle/Makefile`: `oracle/src` the decompiled client (generated, never
  committed: `oracle/decomp/src.sh` makes it from the pristine decompile, `determinize.sh` and `oracle.patch`),
  `oracle/harness` our code, `oracle/decomp` how `src` is made from the owned jar, `oracle/tests` the gate suite.
- `csrc/engine` the C engine, one library, checked against the oracle's chunk dumps in `out/java/chunks` (never
  against another C tree). `csrc/cuda` all device code, a folder a part with its own `PART.mk` and `gpu-PART`
  targets; `csrc/cuda/mirror.tsv` is the mirror rule. `csrc/runtime` the batched environment, the pool and the
  trainer's library, the trainer itself in `csrc/runtime/train`. `csrc/mods/macros` the vector env's macros, frozen
  (GOAL.md). `csrc/play` the human client. `csrc/tests` the tests and their tools. `csrc/cli` the `nether` verbs, a
  file each.
- `configs/` the run files (`envs`, `sim`, `algo`, `gyms`, `tests`: `nether list`), the gates (`gates.yaml`) and the
  fidelity file. `tools/` the C tools built into `out/native` (`index_check`, `pxdiff`, `abrun`, `ncustall`) and
  `export.sh`.
- `out/` is ignored and holds everything built or recorded (`out/bin`, `out/native`, `out/runtime`, `out/java`). The
  renderer's textures are made from the client jar: none is committed. `csrc/engine/biomes.h` is generated
  (`make -C oracle biomes-h`): never edit it by hand.
- Each make fragment's header says what its targets are for: read it before writing a command of your own.

## Rules

- The oracle is vanilla 1.7.10 behaviour plus the pins in SPEC.md and the `config.yaml` switches. Deleting vanilla
  code is fine; changing behaviour is not, unless it is a documented pin or a switch. After any edit under
  `oracle/src`: every oracle gate PASS, and `make -C oracle oracle-patch` (commit the patch) before commit.
- No mutable globals in the native tick (two environments share one process): state goes in `struct env`
  (`csrc/engine/env.h`, reached through `neth_env`), a world, or the replay's per-dimension state; a table is built
  before main and only read after. `make -C csrc globals` fails on any writable global that `csrc/tests/globals.allow`
  does not name.
- No allocation in the native tick and no entity address kept across ticks: livings, list entries, paths and the
  rest come from the env's arena pools (`csrc/engine/arena.h`); what one entity keeps of another is a handle
  (`lref`, `ieref`, a pool slot, a pathref), never a pointer; scratch too large for a frame is the env's
  (`neth_env->scratch`; nothing in a pool worker's scratch outlives its call); an array that grows is a `fixed_array`
  made with its owner.
- A tick frame is at most 4 KB: a bigger local is `ENV_LOCAL`, taken from the env's scratch stack
  (`csrc/engine/envstack.h`), and a packet is filled in its queue's slot (`s2c_add`). `make -C csrc stack` fails on
  a frame over 4 KB or a tick root whose worst chain reaches 32 KB.
- No recursion in the native tick: a block callback emits its nested writes to the block worklist
  (`csrc/engine/blockwl.h`) instead of calling `world_set_block`. `make -C csrc callgraph` fails on a cycle outside
  worldgen.
- Iteration order is data in the native tick: a JDK container whose order is game state keeps its keys plus what
  fixes their order, and the order is computed (`csrc/engine/jorder.h`, `chunkset.h`), never walked off hash chains;
  `make -C csrc order` checks each. A chunk enters a server world, and generation runs, only through
  `world_chunk_request` (`world.h`).
- No Forge, no Mixin, no Gradle. Hooks stay small and call into `netherite.oracle`; logic lives in the harness.
- This repo contains decompiled Mojang source. It must never be pushed to a public remote.
- What a world is lives in one YAML file: `config.yaml` at the root, or a named one in `configs/`. One RL run is one
  run file with two parts at column 0, `sim` and `policy`; a file without parts is sim. The grammar is the strict
  YAML subset written in config.yaml's header, parsed with no library by the Java oracle (`WorldConf.java`) and by
  C (`csrc/engine/worldconf.c`, the same table, `make -C csrc worldconf-check`), which read sim only. Every start is
  held to the file's world. A feature the native engine lacks stays in `WorldConf.UNSUPPORTED`, forced off in the
  oracle too. Java: CONF=path, SET="key=value". Other runtime behaviour is set by flags to `netherite.oracle.Main`.
  Never environment variables.
- No Python in the repo: the tools are C, built by csrc/Makefile into out/native. `uv run --no-project --with ...`
  only for throwaway analysis outside the tree. `jq` for tapes.
- `csrc/` must build on macOS (arm64, clang) as well as Linux, because the human plays there: Linux-only calls
  (prctl, perf_event, /proc) go behind `#if defined(__linux__)`.
- `csrc/mods/macros/` is frozen (GOAL.md's first hard rule): `nether gate --quick` fails on an edit under it.

## Working method

- Parallelize by default: serializing independent work is the main way to waste a night. Split a task into pieces
  that can be verified on their own, give each its own worktree and branch (`make lane NAME=x`), a brief with its
  own gate command, and launch them together. A piece owns its own new files and adds one line to each shared file (a
  dispatch, a table, an enum); pre-declare the shared slots first when you can. One genuinely hard problem goes to
  the strongest model in one long context, never split across lanes. A verification-only lane never edits shared
  code. A lane with no gate is not a lane, and every lane still needs a reviewer: no debug getenv, no writes outside
  the lane.
- Every divergence goes in the ledger: a lane that finds or fixes one adds its row to `csrc/tests/divergences/` (one
  file a lane; the fields are in `csrc/tests/divergences.sh`); a fuzz lane adds its rounds to
  `csrc/tests/divergences_rounds.tsv`. `divergences.md` is generated from them: never edit it by hand.
- Tools, not scripts. A job a second lane would write again is a tool in the tree (C in `tools/` or `csrc/tests`, a
  make target, a make variable), never a script in a lane's `out/`. Before writing a script, look for the tool:
  `nether ab` and `out/native/abrun` (A against B in alternating rounds, every digest held equal), `nether bench`,
  `out/native/ncustall`, `make -C csrc/runtime gates-env` and `gates-cast`, `VARIANT=NAME` on `rlbind`. A sweep is a
  grid that one window ranks, not one variant a turn.
- Find the limiter before optimizing anything: measure env-steps/s end to end, where the host and the device spend
  an env-step, and aim at the part that bounds it (a kernel 30% faster once moved env-steps/s 2 to 5%).
- An A/B is a fixed corpus: every build runs the same trajectory and prints its digest, and two builds are compared
  only on equal digests. A wall-time window is not an A/B. Before and after are interleaved in one session on the
  same GPU and CPU set; a number from another session is not a baseline. Rank host code by user instructions (perf),
  which load does not move. Profilers: perf for host code, nsys for timelines, Nsight Compute for kernels.
- A CUDA lane never runs Java: C is the GPU's oracle. Correct naive kernel first, one kernel or family a file under
  `csrc/cuda/PART/`, checked in seconds against one recording's stored C side (the kernel's `edit` line in
  `mirror.tsv`), then registered in `mirror.tsv`; performance last, hill climbing against the same checks. Build at
  `PERF=0` (the -O1 correctness build) during a port and never switch levels inside an attempt; the default is -O3.
  A constant you sweep goes in the unit that uses it, not in a header every unit includes, and a unit you edit never
  includes cub (its headers cost about 4 s a unit).
- An attempt is about a minute: edit, build the one unit, its quick check, and a measurement that needs no queue.
  A quick gate each attempt, the full gates once for a change you keep; independent gates run at once. If a step
  is slow, time it and fix the step: every slow command is a bug in the loop, not a wait.
- Keep the Java oracle frozen: an edit under `oracle/` invalidates every cache, capture and pool member. What makes
  a fresh tree fast: every C compile goes through ccache; the oracle result cache (`oracle/tests/oraclecache.sh`)
  answers a repeated run in about a second, and `CACHE=0` runs fresh, as the gates and any byte-identity or
  determinism check must; a warm oracle pool starts on its own (`make -C oracle pool-status`); every suite job and
  oracle JVM reserves its memory from one machine-wide budget (`csrc/tests/memslot.sh`).
- A lane's `out/` is its notebook and is never committed: commit code and DEVLOG.md. A lane ends with a DEVLOG entry
  (`## DATE lane/NAME: what came of it`, newest first), its numbers with how they were measured, and a report of
  what it ran and the last lines.
- Models (Elliot, 2026-09-25): only two families work on this project, Opus 5.5 through headless Claude Code and
  GPT-6 Sol and Luna through codex; no other model or provider. Opus at medium effort for implementation and high for
  the hardest; Luna for high-volume reading. When Elliot says he is near a limit, stop launching that model. A lane
  with no commit after about 3 hours on a hard item, or one that repeats a command or ends unfinished twice, moves
  to the other family with its git state and a continuation note. Record a routing decision and its numbers in
  DEVLOG.md, read from the traces and not from impression.

## Checking and reporting

- The narrowest check for what you touched; the whole suite, `make -s -C csrc test`, before a commit that changes
  `csrc/engine` or `csrc/play`, and `make -s -C csrc clangcheck` before a lane ends. `make -s -C csrc smoke` (under
  15 s) after each edit of a refactor every recording must survive unchanged. `make -s -C csrc test` replays a job
  whose executed functions, read files and recording are unchanged since a recorded pass (`TESTSELECT`;
  `TESTCACHE=` runs every job).
- On any recording failure run `out/native/diverge NAME [ROW]` first (the field, the rows before it, the whole
  native state against the oracle's at that tick), and re-check one late row with `test_snapshots --from-row N DIR`
  (keyframes: `make -C oracle keyframes RECS=NAME`). Several recordings at once: `out/native/recs NAME 'gold-g2*' ...
  [-- flags]`; several oracle runs at once: `out/native/par [-j 4] 'make -s -C oracle script ... TAPE={}/tape.jsonl'
  A B C`; never a serial for loop. `ARGS=--detail` on any run adds per-entity forensics to every row.
- Pixels: `out/native/pxdiff` (its `selftest`, then `survey`) clusters a frame pair's differing pixels and names the
  cause. Never hand-scan pixels.
- A GPU job takes its turn through the queue of the machine that runs lanes (`out/flywheel/bin/gpuq.sh`, untracked):
  checks a few at a time, a bench window alone on the GPU.
- Finish in a fresh worktree of your branch, one without your `out/`: build and run your quick gate there (a lane's
  build has worked only in its own tree: a missing mkdir, a missing rule).
- `nether play` plays the C engine's own client and checks the session against Java when you quit
  (`csrc/play/session.sh`): a PASS chains the next session from Java's save at your last tick; a FAIL is a bug in
  the native engine, not in the play, and the session starts again from the same place. `make -C oracle play SEED=1`
  records a Java session, and `csrc/play/rawjudge.sh TAPE` plays its raw events through the native client and judges
  the rows, the screen ops and the frames (what it does not compare: `divergences.md`). A staged agent
  scenario is `make -C oracle script DEV=1 SCRIPT=... TAPE=...`; `--dev` is agent only (it writes `dev:true` in the
  tape and the snapshot manifest, and `out/native/play` refuses either), and the snapshot must contain only players.
- Agent input is what a person at the vanilla client can do. A refused value prints `ORACLE REFUSE` and is not
  recorded, and a tape row carrying one fails on both sides (`csrc/tests/agent_values.sh`). Allow two ticks for
  packets to reach the client before an input depends on a changed block, position or inventory.
- Report to your user what you ran and its last lines, not your impression.

## Gotchas (each cost real time)

- A lane's `out/java/snapshots/NAME` is a symlink to the canonical tree's recording: `cp -r` copies the link, so
  editing the "copy" edits the canonical. Make a start in the lane's own out/ with `cp -rL`, check the target with
  `readlink -f` before an edit, and never chmod or write anything under out/java/snapshots.
- Porting to C: a decompiled double literal like 0.20000000298023224D is a widened float ((double)0.2f); copy it as
  printed, never as 0.2 (one float ulp off otherwise). Java evaluates operands left to right; split any C expression
  that draws twice from one Random. Build with -ffp-contract=off.
- A frame or a tick that depends on how many frames were rendered is a bug. If a replay diverges only in `px`, look
  for another per-frame smoother, cache or counter before suspecting state.
- `Entity.nextEntityID` is one JVM-global counter in vanilla; the oracle gives each thread role its own range. Do not
  reintroduce a shared counter.
- The static RNG names in `determinize.sh` seed streams; changing them changes every tape.
- Concurrent oracle runs need separate game dirs (make does this per tape name): two runs with the same TAPE
  basename clobber each other's save.
- `pgrep -f pattern` also matches the shell that runs it: match the java binary or wait on PIDs.
- The login shells are zsh: `/dev/tcp` needs bash, and an unquoted variable does not word-split in an ssh one-liner.
  Multi-step remote logic goes in a bash script file.
- Ubuntu OpenJDK 8 needs `-Djavax.accessibility.assistive_technologies=` or AWT dies at startup (the Makefile passes
  it).
- The Mac has no arm64 LWJGL 2.9.1; `make deps` there takes the 2.9.4 jars, arm64 natives and Zulu JDK 8 from the
  v1 checkout (`V1=~/dev/netherite`).
- Keep `out/` small: tapes with `--detail` grow fast.
