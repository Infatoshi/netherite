# netherite launch post - updated claims (2026-07-27)

The original draft lives on the macbook; this file is the verified-claims
refresh to merge into it. Every number below was measured today on anvil at
master `7287095`; nothing is from memory.

## Headline

netherite: Minecraft 1.11.2 rebuilt from scratch in C and CUDA, verified
pixel-by-pixel against the real Java game, with a batched GPU RL environment
that steps 3 million env-ticks per second.

## The verified claims

**Fidelity.** The renderer and simulation are checked against "the oracle" -
the actual 1.11.2 Java client - by recording input tapes from real play and
replaying them tick-exact through the C engine. 23-tape suite: 16 replay
pixel-gate clean; the other 7 fail on small residuals that are each
individually diagnosed with a mechanism in `OPEN_DIVERGENCES.md` (no mystery
failures - two of them are provably the *oracle's* renderer quirk: llvmpipe
under-fogs distance by exactly 1.05x). Physics replays are clean to 1e-9.

**GPU transfer.** The CPU raster and the CUDA raster are bit-identical
(every layer, exact depth), and the full 23-tape sweep produces identical
gate results on both backends.

**Throughput** (blaze batched env, RTX PRO 6000 Blackwell 96GB, full action
decode, repeat 4, semantic camera every decision, 1000-decision timed runs):

| batch | env-ticks/s | decisions/s |
|---|---|---|
| 1024 | 0.79M | 198k |
| 4096 | 2.22M | 554k |
| 8192 | 3.02M | 756k |

CPU reference (Ryzen 9950X3D, 32 threads, same loop and actions): 0.29M
env-ticks/s - the GPU runs ~10x the whole CPU at matched settings. Batch
16384 exceeds the card (region pool alone is 137 GB).

**Honest edges.** First-person hand use poses, the inventory player preview
(max channel 1), portal/underwater full-frame, and a handful of sub-frame
texel residuals are open and documented. The divergence ledger ships with
the repo; "do what you want with it" includes auditing us.

## Assets (regenerated 2026-07-27 from current binaries)

- `demos/pixel_match_sbs.mp4` - oracle | magma side-by-side over the gated
  pixel scenes; gates re-verified PASS during the encode (4.3 MB, 1708x516).
- `demos/combat_sbs.mp4` - blaze bow fight + ender dragon scenario tapes,
  oracle left / magma right, title copy updated to the 23-tape claim
  (11 MB, 735 frames).
- Both scp'd to macbook:~/Downloads for the post.

## Numbers NOT to claim

- "Pixel-perfect" unqualified (16/23 gate-clean is the honest phrasing).
- The 250-decision bench reads ~5% high; use the 1000-decision figures.
- No rays/s figure was re-measured this pass; drop the old ~396M line or
  re-run `obs_camera.cu` before using it.
