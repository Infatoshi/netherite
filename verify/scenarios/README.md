# Scenario tapes

Scenario tapes turn a one-time Java-oracle combat or mechanic setup into the
normal magma physics and structural-pixel gate. Run one from
`verify`:

```bash
bash scenarios/run_scenario.sh scenarios/smoke_zombie.yaml
```

The command boots `java/start_vnc_client.sh` with the resolved `java/fast.yaml`
profile, creates a fresh world, runs setup through qrl `runcmds`, records via
`trace/tape.py`, and sends real keyboard and mouse input through
`java/mcwindow_script.py`. It archives every tape artifact as
`tapes/scenario_<name>_<UTC stamp>.*`, stops only the processes it started,
releases the oracle lock, and then runs `replay_tape.py --cpu --report`.
Exit codes are the replay contract: 0 passes, 3 is a pixel-gate failure, and 4
is a physics failure. Gate-failure side-by-side PNGs remain under
`trace/out/tape_scenario_<name>_<stamp>/gatefail_sbs_*.png`.

## YAML schema

```yaml
name: blaze_fight             # lowercase archive-safe identifier
world:                       # fresh qrl reset before setup
  seed: 0
  mode: survival             # survival | creative
  type: default              # default | flat
  structures: true
setup_commands:              # vanilla server commands, run in one qrl tick
  - /time set 6000
  - /difficulty hard
  - /tp @p 0.5 64 0.5 0 0
  - /give @p minecraft:bow 1
  - /summon Blaze 0.5 64 6.5
duration_ticks: 800           # target wall duration at vanilla 20 TPS
frames_every: 20             # optional; default 20, 0 disables frames
input:
  segments:                   # exact mcwindow_script.py segment fields
    - seconds: 2.0
      keys: [w, Control_L]
      buttons: [1]
      look: [30, -5]
known_divergences:            # optional pixel_gate.py entries
  - ticks: [200, 240]
    open_divergence: 40
    reason: Oracle-only death particles.
    regions: [[45, 0, 383, 853]]
    predicate: {type: non_solid_scene}
```

`setup_commands` fails the run if any command returns failure. Commands use
the vanilla 1.11.2 syntax and execute as the player, so selectors and relative
coordinates follow the player's current dimension. `input.segments` accepts
only `seconds`, `keys`, `buttons`, and `look`, exactly like
`mcwindow_script.py`. Instead of inline segments, use
`input: {file: relative/path.jsonl}`; the path is relative to the spec. A short
input is padded with released controls to `duration_ticks / 20` seconds, and a
long input is rejected. The tape's recorded tick count is authoritative if
wall scheduling makes it differ slightly from the target.

Known divergences are copied verbatim into the archived sibling
`<tape>.known_divergences.json` with sidecar version 1. Predicates and regions
have the same meaning as in `trace/pixel_gate.py`; use them only for an already
filed, tightly bounded render gap.

## Oracle lock and cleanup

`run_scenario.sh` acquires the required lock before touching qrl, Xvfb `:1`, or
the Java client:

```bash
exec 9>/tmp/qrl_25575.lock
flock 9
```

It waits if another recorder owns the lock. After acquiring it, the harness
still refuses to launch if an oracle process, X socket, or reserved port is
present, because `start_vnc_client.sh` has a cleanup preamble. It records the
pre-launch PID set and its new process groups, and cleanup signals only those
new processes. The lock is released immediately after the Java session is
gone; CPU replay does not hold it. Direct use of `scenario.py record` is
rejected so the lock cannot be skipped accidentally.

## Why mob AI replay is deterministic

Explicit summon coordinates make the initial entity placement reproducible;
`NoAI` is not set, so vanilla AI remains active. Its server-side RNG does not
need to be reproduced by magma. At every client tick end,
`Recorder.recordTick` writes the nearby client entity stream into the tape's
`ents` field: stable id, class, position, yaw, health, and the model pose,
flags, and variant fields used for rendering. The recorder emits up to 64
nearby entities, and a scenario summon six blocks away is well inside that
window. During replay, `replay_tape.py:tape_to_script` converts each recorded
entity row into per-tick `ent_view` events (and the recorded player packets
re-anchor mob hits and knockback), rather than rerunning the oracle AI RNG.
Thus the JSONL entity timeline, not magma's spawner or AI, is the replay source
of truth.

## Authoring contract (paid-for gotchas - read before writing a yaml)

Every rule below cost a debugging session. Violating one wastes an oracle
recording slot.

- One system per scenario. A scenario that stages three mechanics produces
  a tape whose failures cannot be assigned to a delegate.
- `setup_commands` run in ONE qrl tick and the run aborts on any command
  failure. `/clear` on an empty inventory and `/fill` that changes zero
  blocks REPORT FAILURE in 1.11.2. Order matters: teleport first, then act
  on the arrival cell only if something is guaranteed to be there.
- Instant potions take their duration argument in TICKS, not seconds:
  `/effect @p minecraft:instant_damage 1 1` is one tick of amplifier 1.
- Reduced player HP needs `gamerule naturalRegeneration false` or the HP
  regenerates before the input segments start.
- An `AttributeModifiers` NBT tag REPLACES an armor piece's default
  attributes; declare `generic.armor` explicitly or the piece protects for
  zero.
- `/replaceitem entity @p slot.armor.<part>` is how armor is worn;
  `/give` only fills the hotbar.
- Summon with explicit coordinates a known offset from the player tp
  point; never rely on natural spawns for the encounter under test.
- Keep `duration_ticks` minimal for the mechanic (200-800). Recording time
  is the serial bottleneck of the whole flywheel; frames_every: 2 only when
  pixel evidence needs tick resolution.
- Validate before queueing:
  `uv run --no-project --with pyyaml --with pytest python -m pytest scenarios/test_scenario.py`
  plus a read of your yaml against the schema block above.
- `known_divergences` in a NEW scenario yaml is almost always wrong: new
  tapes should expect rc 0 or produce an honest failure for the fix
  fan-out. Filing divergences is the merge owner's call, with proof.
