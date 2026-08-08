# Video-conditioned tape recovery

This harness recovers a valid Netherite input tape that approximately follows
a recorded Minecraft 1.11.2 playthrough. It does not claim to recover the
player's unknowable original input samples, and a compressed video is never an
oracle golden.

## Product

Each run workspace contains:

- `manifest.json`: immutable source identity, timing, seed, version, and
  extraction parameters.
- `targets.jsonl`: reviewable observations sampled from the video. Every value
  has an explicit confidence and may be corrected by a human.
- `segments.jsonl`: accepted contiguous tape spans and their start/end state
  hashes.
- `actions.jsonl`: one full 13-field Netherite action per simulated tick.
- `divergences.jsonl`: the first measured mismatch for every failed native or
  Java replay. Unsupported state is a divergence, never a scoring tolerance.

The finished tape must start from the declared seed, preserve state continuity
between every segment, and reach the declared terminal condition. Independent
per-segment teleports, inventory patches, and RNG resets are prohibited in the
final tape.

## Procedure

1. Run the resumable extractor directly from a source video and seed:

   ```bash
   bash verify/video_replay/extract.sh run.mp4 --seed 123 \
       --workspace verify/video_replay/out/my_run
   ```

   It extracts renderer-agnostic observations and then searches ordinary
   inputs through native checkpoints. Interrupting and rerunning the same
   command resumes it. The source hash and immutable search configuration are
   recorded in `manifest.json`.

   Native checkpoint storage is bounded by default to the latest three
   generations plus every current live beam slot. Set
   `--checkpoint-history 0` only for a short forensic run that genuinely needs
   arbitrary historical rewind; a full video with unbounded snapshots is
   intentionally not the default.
2. Ingest the video and extract candidate target frames at a fixed interval.
3. Establish video-to-game clock anchors. Loading, pause, and menu spans do not
   advance game ticks and must be labeled explicitly.
4. Infer hard observations (dimension, alive/dead, required inventory,
   portal/dragon events) and soft observations (pose, look, vitals, visual
   landmarks) with confidence.
   For a fresh world, first run the Java/C spawn-position golden. The world
   spawn is seed-determined, but the ordinary player's spawn-fuzz RNG is not;
   retain every valid fuzz offset until terrain landmarks disambiguate it.
5. Start from the previous accepted native state. Generate parameterized input
   primitives, not independent keys per tick: held movement, turn curves,
   jumps, attacks, uses, hotbar selection, and visible GUI operations.
6. Roll candidates through the fastest faithful tier:
   - Blaze batches movement/mining/crafting windows whose `.bsnp` state is
     complete for the episode.
   - Magma handles mobs, fluids, projectiles, portals, arbitrary inventory,
     world clock, and other state excluded by `.bsnp`.
7. Rank with hard constraints first, then state trajectory, timing, visual
   structure, and finally action simplicity. Retain a beam across ambiguity.
8. Commit a segment only after replaying it from the prior committed state.
   Save the resulting state hash and an overlap window with the next segment.
9. Periodically replay the accumulated tape in the Java 1.11.2 oracle. Stop at
   the first divergence, record it, and determine whether it is tape ambiguity,
   unsupported state, or an implementation defect.

## Target record

`targets.jsonl` begins with one header and then target records:

```json
{"schema":1,"kind":"header","run_id":"moleyg_3243"}
{"kind":"target","id":12,"video_s":60.0,"frame":1800,
 "game_tick":null,"clock":"unknown","image":"frames/target_000012.jpg",
 "hard":{},"soft":{},"events":[],"confidence":{},"status":"unreviewed"}
```

`clock` is `running`, `paused`, `loading`, or `unknown`. `game_tick` remains
null until anchored; inventing a tick from `video_s * 20` is forbidden.

## Scoring contract

Candidates with a hard mismatch are rejected. Soft costs are normalized by
declared tolerances and weighted by observation confidence. Pixel scores use
masked structural features; skins, F3 text, timers, hardware strings, mod
overlays, and codec damage are excluded. A segment report records every term,
so a low aggregate score cannot conceal a large subsystem error.

## Completion gate

A recovered run is complete only when:

- `actions.jsonl` replays continuously from a fresh declared seed;
- every segment boundary state hash matches the prior segment's output;
- Netherite reaches the requested terminal condition without state patches;
- the full tape has a Java replay report, including its first divergence if it
  does not complete there; and
- the ordinary performance gates remain unchanged.
