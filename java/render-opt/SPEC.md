# Kernel catalog (MC 1.11.2 rendering)

~35 kernels found by scanning `../src/net/minecraft/client/renderer/`, `.../texture/`, `.../culling/`,
`.../chunk/`, `block/`, `world/` (lighting), `world/biome/`, `util/math/`. Status: `verified` (PASS in
harness), `todo` (not started). `cap` = capture mode (`pure` = verbatim Java golden; `hook` = live-game
capture). `tier`: 0 scalar / 1 per-element parallel / 2 structured. Implement roughly top-to-bottom
(pure+simple first, structured meshing last).

# >>> STATUS — lab closed (keep for catalog; product work is magma)
# 39/40 kernels VERIFIED bitwise vs real MC (C only). k26 closed-by-integration: standalone
# bitwise impossible (non-deterministic atlas UVs); sub-kernels + drop-in column pixel-diffs.
# Drop-ins: sin, lightmap, biome, AO. Whole-frame stripcheck: 0 px native vs vanilla on pinned
# course. Not ported by design: atlas UV stitch + GL raster. Optional: full rebuildChunk VBO.
# History: ../../docs/DEVLOG.md. Day-to-day game fidelity: ../magma/VERIFY.md.
# <<< END STATUS

## Math primitives (tier-0, cap=pure) - plumbing smoke tests, bit-exact sensitive
| id | kernel | source (file:line, method) | status |
|----|--------|----------------------------|--------|
| 00 | fast_inv_sqrt | util/math/MathHelper.java:539 fastInvSqrt | verified |
| 01 | sin_cos_table | util/math/MathHelper.java:29 sin/cos (+:615 SIN_TABLE init) | verified |
| 02 | atan2_lut | util/math/MathHelper.java:475 atan2 (ASINE_TAB/COS_TAB, FRAC_BIAS) | verified (tol) |
| 03 | log2_debruijn | util/math/MathHelper.java:331 smallestEncompassingPowerOfTwo / :355 log2DeBruijn | verified |
| 04 | color_pack | util/math/MathHelper.java:408 rgb / :426 multiplyColor | verified |

## Frustum / culling (tier-1, cap=pure) - pure matrix/vector math
| id | kernel | source | status |
|----|--------|--------|--------|
| 05 | frustum_plane_extract | client/renderer/culling/ClippingHelperImpl.java:36 init (matmul + normalize 6 planes) | verified |
| 06 | aabb_frustum_test | client/renderer/culling/ClippingHelper.java:22 isBoxInFrustum (6 planes x 8 corners) | verified |

## Color / texture (tier-1, cap=pure except where noted) - per-pixel, gamma math
| id | kernel | source | status |
|----|--------|--------|--------|
| 07 | mipmap_blend_gamma | client/renderer/texture/TextureUtil.java:108 blendColors / :158 blendColorComponent (pow gamma) | verified |
| 08 | mipmap_gen_chain | client/renderer/texture/TextureUtil.java:57 generateMipmapData (2x2 downsample) | verified |
| 09 | tex_frame_interp | client/renderer/texture/TextureAtlasSprite.java:242 interpolateColor | verified |
| 10 | atlas_stitch_pack | client/renderer/texture/Stitcher.java:110 allocateSlot/expandAndAllocateSlot (bin pack) | verified |

## Lighting / color-of-world (tier-1/2, cap=hook) - read world light/biome arrays
| id | kernel | source | tier | status |
|----|--------|--------|------|--------|
| 11 | lightmap_texture | client/renderer/EntityRenderer.java:883 updateLightmap (256 texels) | 1 | verified |
| 12 | ao_vertex_brightness | client/renderer/BlockModelRenderer.java:368 AmbientOcclusionFace.updateVertexBrightness | 2 | verified |
| 13 | ao_pack_helpers | BlockModelRenderer.java:519 getAoBrightness / :539 getVertexBrightness (pure helpers) | 0 | verified |
| 14 | light_query | world/chunk/Chunk.java:685 getLightFor / world/World.java:852 getLightFromNeighborsFor | 1 | verified |
| 15 | light_combine_pack | world/World.java:955 getCombinedLight / block/Block.java:453 getPackedLightmapCoords | 0 | verified |
| 16 | light_propagation | world/World.java:3025 checkLightFor (BFS, sequential) + :2967 getRawLight | 2 | verified |
| 17 | skylight_gen | world/chunk/Chunk.java:238 generateSkylightMap (per-column) | 1 | verified |
| 18 | biome_color_blend | world/biome/BiomeColorHelper.java:33 getColorAtPos (3x3 blend) | 1 | verified |

## Fluid (tier-1/2, cap=hook)
| id | kernel | source | tier | status |
|----|--------|--------|------|--------|
| 19 | fluid_height | client/renderer/BlockFluidRenderer.java:273 getFluidHeight (4-neighbor avg) | 1 | verified |
| 20 | fluid_quad_gen | client/renderer/BlockFluidRenderer.java:43 renderFluid (slope angle, sin/cos, quads) | 2 | verified (bitwise) - enriched live capture of 1710 water blocks -> 39060 ints PASS; covers flow/still-UP, UP back-face, 4 sides (front+back), DOWN, water-overlay branch. Lava code-path-identical (not in capture scene) |

## Geometry / meshing (tier-2, cap=hook) - the heavy structured ones, do last
| id | kernel | source | status |
|----|--------|--------|--------|
| 21 | should_side_render | block/Block.java:471 shouldSideBeRendered (occlusion test) | verified |
| 22 | fill_quad_bounds | client/renderer/BlockModelRenderer.java:168 fillQuadBounds (pure) | verified |
| 23 | render_quads_flat | client/renderer/BlockModelRenderer.java:238 renderQuadsFlat | verified |
| 24 | render_quads_smooth | client/renderer/BlockModelRenderer.java:116 renderQuadsSmooth (AO) | verified |
| 25 | visgraph_floodfill | client/renderer/chunk/VisGraph.java:40 computeVisibility (BFS over 16^3 bitset) | verified |
| 26 | chunk_rebuild_loop | client/renderer/chunk/RenderChunk.java:157 rebuildChunk (per-block driver) | closed-by-integration: no standalone bitwise (atlas UVs); sub-kernels + drop-in lightmap/biome columns |
| 27 | translucent_sort | client/renderer/VertexBuffer.java:69 sortVertexData (painter's, per-chunk) | verified |
| 28 | vertex_pack | client/renderer/VertexBuffer.java:434 addVertexData / :262 putBrightness4 / :300 putColorMultiplier | verified |

## Particle (tier-1, cap=hook)
| id | kernel | source | status |
|----|--------|--------|--------|
| 29 | particle_update | client/particle/Particle.java:156 onUpdate (gravity/drag, per-particle) | verified |
| 30 | particle_billboard | client/particle/Particle.java:183 renderParticle (camera-facing quad + rotate) | verified |

## Notes
- Bit-exact-sensitive (magic constants / LUT layout must match): 00, 01, 02, 03, 07. Verify with the
  `-ffp-contract=off` / `--fmad=false` flags the runner sets.
- Sequential / hard-to-parallelize (do as C, not CUDA, unless you redesign the algorithm): 16
  (light BFS), 25 (visgraph BFS), 27 (sort), 10 (bin pack).
- AO (12) shows up in both lighting and meshing scans - same kernel, listed once here.
- Spot-check every `source` line against `../src/` before trusting I/O; decompiled `var1` names can
  mislead a one-pass reader.

# Tier-2 catalog: rest-of-the-renderer (round-3 subsystem scan)

The Tier-0/1 catalog above is the world/chunk compute. A *complete* frame also needs these. They split
cleanly into PORTABLE compute (new kernels, todo) vs GL-BOUND draw/orchestration that stays MC's
(the "(A) port compute, keep MC's GL" decision - these are NOT kernels, do not port).

## PORTABLE - model-baking (JSON model -> BakedQuad; pure, deterministic, upstream of meshing)
| id | kernel | source | status |
|----|--------|--------|--------|
| 31 | facebakery_make_quad | client/renderer/block/model/FaceBakery.java:55 makeBakedQuad | verified (identity modelRotation, uvLock=false; both partRotation branches) |
| 32 | facebakery_fill_vertex | FaceBakery.java:141 fillVertexData / :152 storeVertexData | verified (identity modelRotation; uvs/sprite-bounds as inputs) |
| 33 | facebakery_rotate | FaceBakery.java:163 rotatePart / :212 rotateVertex | verified (rotateVertex matrix as input) |
| 34 | facebakery_facing_normal | FaceBakery.java:242 getFacingFromVertexData / :283 applyFacing | verified |
| 35 | bake_model_loop | client/renderer/block/model/ModelBakery.java:674 bakeModel | verified (assembly only; makeBakedQuad+rotate reduced to inputs) |

## PORTABLE - scattered (sky/entity/gui compute worth porting; rest of those subsystems is GL-bound)
| id | kernel | source | cap | status |
|----|--------|--------|-----|--------|
| 36 | sky_dome_tessellate | client/renderer/RenderGlobal.java:306 generateSky/generateSky2 | pure | verified |
| 37 | entity_limb_anim | client/model + RenderLivingBase setRotationAngles (per-limb trig) | hook | verified |
| 38 | model_box_gen | client/model/ModelBox.java:27 + TexturedQuad.draw (vertex+normal) | pure | verified |
| 39 | font_glyph_quad | client/gui/FontRenderer.java renderDefaultChar (per-glyph quad/UV) | pure | verified |

## NOT PORTED - GL-bound draw + orchestration (stays MC's; the conductor + the rasterizer)
- Render orchestration: `RenderGlobal.setupTerrain` (frustum BFS culling), `renderBlockLayer`,
  `ViewFrustum` - control-flow/GL-bound. (Only `updateChunkPositions` grid-math is pure; minor.)
- Sky/weather GL: `renderSky`, `setupFog`; HYBRID (color math portable, state-coupled): `updateFogColor`,
  `renderRainSnow`.
- Entity/TE/GUI: the actual draws (`Gui.drawRect/drawTexturedModalRect`, `TexturedQuad.draw` GL emit,
  display-list compile), per-frame interpolation, TESR animations (chest lid, banner wave), HUD
  compositing - tiny per-element or GL-bound. Hand-off point: kernels produce vertex/color/light;
  MC's GL still submits + rasterizes.

Scan takeaway: model-baking is the one rich seam of new portable compute (~8 fns under 31-35); sky,
entity, and GUI contribute only a handful each; orchestration + rasterization are intentionally out of
scope. So "complete native rendering" = Tier-0/1 (done bar ~6 hard) + model-baking (31-35) + a few
scattered (36-39) + the heavy-buffer JNI drop-in + whole-frame pixel-diff.
