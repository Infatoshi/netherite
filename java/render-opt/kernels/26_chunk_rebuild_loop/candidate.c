/* CANDIDATE: MC 1.11.2 RenderChunk.rebuildChunk() chunk meshing
 *   (src/net/minecraft/client/renderer/chunk/RenderChunk.java:157).
 *
 * STATUS: BLOCKED at capture scope, not at porting effort. Cannot bitwise-match
 * golden.txt from the captured inputs.txt, and no partial numeric match is possible.
 *
 * What inputs.txt provides:  header "origin X Y Z dim 18" + one line per cell of the
 *   18^3 neighborhood:  rel_x rel_y rel_z stateID   (the 16^3 chunk + a 1-block shell).
 *   That is the ONLY captured data: block-state registry IDs. No light arrays, no baked
 *   model geometry, no atlas, no biome.
 *
 * What golden.txt is:  per render layer (SOLID, CUTOUT_MIPPED, CUTOUT, TRANSLUCENT)
 *   "layer N vertexCount C" then C*7 ints, DefaultVertexFormats.BLOCK (7 ints/vertex):
 *     +0,+1,+2 position floats | +3 packed RGBA color (tint*AO shade)
 *     +4,+5 texture U,V floats  | +6 packed lightmap (BLOCK<<4 | SKY<<20)
 *
 * Why every numeric value in golden.txt is unrecoverable from inputs.txt:
 *   - col +6 lightmap embeds per-vertex SKY light. Skylight comes from canSeeSky / the
 *     column heightmap, i.e. blocks in the FULL column above this region. The capture is
 *     an 18^3 box (16^3 + a one-block shell), so the data that determines skylight is not
 *     present at all. Block light is likewise an uncaptured world array. -> DECISIVE.
 *   - cols +4,+5 texture U,V are coordinates into the runtime-stitched texture atlas
 *     (TextureMap stitch order is not deterministic across runs and is not captured).
 *   - cols +0..+2 positions and the vertex COUNT come from each block's baked model
 *     (ModelManager / IBakedModel quads) + face occlusion (shouldSideBeRendered needs
 *     neighbor opacity), none of which is derivable from a bare stateID in C.
 *   - col +3 color = biome/tint multiplier * smooth-AO shade (needs biome + neighbor AO).
 *
 * The per-block DRIVER (this loop's own logic) IS portable; the kernels it composes
 * (21 occlusion, 22-24 quads, 25 visgraph, 28 vertex-pack) are verified in isolation.
 * But composing them to reproduce golden.txt requires a model+atlas+light capture that
 * inputs.txt does not contain. To actually port this kernel, the NetheriteMod hook must capture,
 * per rendered block: its baked quads, the atlas sprite UV rects, and the BLOCK/SKY light
 * at each vertex. With only stateIDs, the divergence from golden.txt is total.
 *
 * The runner compares line-by-line and fails on the first line-count mismatch, so there
 * is no partial-credit path; this file emits nothing and documents the blocker instead. */
#include <stdio.h>

int main(void) {
    /* Faithful per-block iteration order of rebuildChunk over the 16^3 interior
     * (BlockPos.getAllInBoxMutable, x outer / y / z inner), for reference only.
     * No vertex output is produced: the data needed to mesh is not in inputs.txt. */
    fprintf(stderr,
        "26_chunk_rebuild_loop: BLOCKED. inputs.txt has only 18^3 stateIDs; "
        "golden.txt embeds per-vertex skylight + atlas UVs + baked-model geometry "
        "that are not captured. Needs an extended NetheriteMod capture (models/atlas/light) "
        "before a C port can bitwise-match.\n");
    return 0;
}
