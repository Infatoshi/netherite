# The launch zoom video: how it was made

The tweet-10 asset (`10_zoom_8192.mp4` in the thread folder, built as
`demos/zoom_8192_story.mp4`). One take, ~18 s: the trained chain agent
playing through the exact renderer, a slider wipe into the SAME world and
ticks through the batched sim's semantic camera, then a pure zoom out over
a recorded live GPU batch of 7,200 environments, each pane its own world
stepping in lockstep. This file documents the intent, the seven failed
cuts it took to get there, and the exact reproduction steps.

## Original intent

The first thread draft (2026-07-24) had a zoom video whose final frame
showed ~24,000 tiles - a config that no longer exists - and whose tiles
were posed scenic stills. The goal for the launch version, refined over
several review rounds:

1. Open on the agent actually PLAYING (a trained policy, not a posed
   camera), through renderers we can defend.
2. Show the conversion that makes the project interesting: the real game
   -> the stripped-down parallel renderer the policy actually sees.
3. Zoom out - pure scale, no drift, no jitter - into the batch farm, with
   every pane genuinely alive.
4. The final frame must fill the video exactly (no letterbox), and the
   pane count must be honest.

## The cuts, and what each one taught

**v1 - semantic tiles from a live batch.** Correct by construction
(VecBlaze obs, real batch) but read as "a Walmart version of rendering":
without narrative framing, the cheap renderer looks like a cheap video.
Learning: the semantic camera is only compelling AFTER you show what it
is a compression OF.

**v2 - 8,192 exact-renderer stills (1024 worlds x 16 poses).** Aerial
poses blew the gameplay-sized draw-buffer caps (fixed with a video-only
`MAGMA_CONF`), random origins landed on ocean, and the first "land score"
ranked pitch-black inside-a-hill frames as 100% land. Learnings: aerial
cameras need bigger geometry budgets than gameplay ever does; content
filters must gate on luminance and variance, not one heuristic; cache
keys must include what they depend on (a stale `center_hi/DONE` marker
served the previous run's ocean tile as the zoom opening).

**v3 - trained-policy hero + animated ring.** First cut with the chain
policy's POV as the opening. Three composition bugs surfaced: the hero
window landed on a block-break stare (reads as a freeze - fixed by
scoring every window of the run for camera motion and hard-penalizing
>1.5 s static runs), integer-truncated crops jittered (fixed with
float-box `Image.transform` through a mip pyramid), and easing the
anchor toward the mosaic center read as a lateral slide (fixed: the
anchor never moves).

**v4 - rigid canvas.** Tiles pasted individually still wobbled against
the background by +-1 px. Fix that generalizes: composite everything
into ONE layer at source resolution first, then apply the single zoom
transform - things that cannot move relative to each other in the source
cannot wobble on screen.

**v5 - 8,192 CPU rollout farm.** Every tile a real exact-render rollout
(30-way parallel, ~40 min). Two lessons: my random walker's dig bursts
put half the agents inside dark tunnels by the video's final seconds -
exactly when the full grid is on screen (content gates must weight the
END of a clip); and a 2^13 tile count cannot tile a 2:1 frame with 16:9
tiles at all (grid ratio must be 9:8 - that geometry, not rendering, is
where the black bar came from).

**Final story cut.** The v1 idea returns with the framing it needed:
exact render -> wipe -> semantic camera of the same ticks (dual-captured
from one rl-mode replay), then the zoom over a live batch recording.
Farm fixes: snapshot assignment shuffled (round-robin had lined identical
ocean worlds into the diagonal blue stripes), spawns screened per world
(ocean-facing snapshots measured 58-80% flat panes; the five land-locked
ones 0.8-6.7%), and the bench's +-90-degree random pitch replaced with a
correlated walker whose pitch is sprung to the horizon. Final frame:
3.2% flat tiles. Stepped at repeat=1, so 30 fps playback is ~1.5x real
time. Grid 90x80 = 7,200: the 9:8 ratio makes 16:9 obs tiles fill 2:1
exactly, and 7,200 is what fits on the card next to the obs recorder
(8,192 remains the separately benched perf number; the thread master
notes the distinction).

Also found along the way, filed in `magma/OPEN_DIVERGENCES.md`: the
mesher renders double-height plants (id 175) as solid tinted slabs - the
scenic-walk tape recorded for an earlier cut is kept as the repro.

## Infrastructure incidents worth remembering

- The CUDA runtime wedged box-wide after GPU1 (the co-tenant card) went
  into an error state; `rmmod nvidia_uvm && modprobe nvidia_uvm` fixed it
  without a reboot.
- The farm recorder was OOM-killed at ~150 ticks: float64 colorize
  temporaries were ~1 GB per tick of transient churn. Do obs math in
  float32 with in-place `np.multiply(..., where=)`.
- Background compound commands inherit a stale cwd; use absolute paths
  in anything launched with `run_in_background`.
- `pkill -f <pattern>` kills your own wrapper shell if the pattern
  appears in its command line.

## Tooling used (all committed)

| Piece | What it does |
|---|---|
| `scripts/zoom_hero_clip.py` | Replays `rl/out/chain_actions_s10.json` through `magma_game --rl`, scores every window for camera motion, captures the liveliest 540 ticks twice: exact frames at 1152x1152 and the semantic camera (`cam`/`depth`/`edge` from the same obs stream) into `hero_obs.npz`. |
| `scripts/batch_obs_record.py` | Runs a real VecBlaze batch (90x80 envs, GPU0), gentle correlated walker, repeat=1, colorizes every env's 64x36 camera per tick into one mosaic memmap; prints a flat-tile report. |
| `scripts/make_zoom_story.py` | Cuts the phases: exact POV -> slider wipe to the obs view -> hold -> fixed-anchor exponential zoom over the mosaic (hero pane overrides the center tile for continuity) -> title. Pipes raw frames to ffmpeg/libx264. |
| `magma_game` script mode | `set_pose`/`set_time` JSONL scripts + `--frames-out` for posed captures; `MAGMA_HIDE_GUI=1`; `--conf PATH` for a video-only caps file (never edit the repo's `magma.conf`). |
| `run_scenario.sh` | Oracle-side recording (used for the scenic-walk tape; needs `wmctrl` on the box). |
| PIL `Image.transform(EXTENT)` + `reduce` | Sub-pixel float-box sampling and mip prefiltering - the anti-jitter/anti-shimmer core. |
| ffmpeg rawvideo pipe | `-f rawvideo -pix_fmt rgb24 -s 1920x960 -r 30 -i - -c:v libx264 -crf 18 -pix_fmt yuv420p`. |

## Reproduce

```bash
cd ~/dev/netherite
# 0. once: video-only caps file (bigger draw buffers for posed shots)
sed 's/draw_cutout = 262144/draw_cutout = 1048576/' \
    magma/magma.conf > ~/dev/nw/.tmp/zoom_video.conf

# 1. hero: trained-chain POV, exact + semantic, motion-scored window
(cd magma && uv run --no-project --with numpy python \
    ../../scripts/zoom_hero_clip.py)

# 2. farm: live 7200-env batch recording on GPU0 (check nvidia-smi first)
CUDA_VISIBLE_DEVICES=0 uv run --no-project --with numpy,torch,pillow \
    python scripts/batch_obs_record.py     # expect "flat tiles ... ~3%"

# 3. compose
uv run --no-project --with numpy,pillow python scripts/make_zoom_story.py
# -> demos/zoom_8192_story.mp4; frame-check phase boundaries before use:
ffmpeg -i demos/zoom_8192_story.mp4 -vf \
    "select='eq(n\,30)+eq(n\,100)+eq(n\,300)+eq(n\,527)'" -vsync 0 f_%d.png
```

The single most important process rule from this build: extract and LOOK
at frames after every render. All seven rejected cuts passed their exit
codes; every one failed in the pixels.
