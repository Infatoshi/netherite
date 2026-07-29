/* tests/test_mesh_mc.c - smoke test for the MC-faithful chunk mesher.
 *
 * Meshes chunk (0,0) of a seed-0 world and asserts structural invariants:
 * nonzero output, per-layer counts multiple of 3, SOLID the largest layer,
 * finite positions within the chunk world column (+/-1), and uv in [0,1].
 */
#include "world/mesh_mc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const char *LAYER_NAME[4] = { "SOLID", "CUTOUT_MIPPED", "CUTOUT", "TRANSLUCENT" };

int main(void) {
    int fail = 0;

    CrWorldMC *w = worldmc_create(0);
    if (!w) { printf("FAIL: worldmc_create returned NULL\n"); return 1; }
    worldmc_ensure(w, 0, 0, 1);

    CrChunkMeshMC m;
    int total = worldmc_mesh_chunk(w, 0, 0, &m);

    printf("chunk (0,0) per-layer vertex counts:\n");
    int sum = 0;
    for (int l = 0; l < 4; ++l) {
        printf("  [%d] %-14s %d verts (%d tris)\n", l, LAYER_NAME[l], m.nverts[l], m.nverts[l] / 3);
        sum += m.nverts[l];
        if (m.nverts[l] < 0 || m.nverts[l] % 3 != 0) {
            printf("FAIL: layer %d nverts %d not a non-negative multiple of 3\n", l, m.nverts[l]);
            fail = 1;
        }
    }
    printf("  total %d verts (return value %d)\n", sum, total);

    if (total <= 0) { printf("FAIL: total verts must be > 0\n"); fail = 1; }
    if (sum != total) { printf("FAIL: return value %d != sum of nverts %d\n", total, sum); fail = 1; }

    /* SOLID must be the largest layer */
    for (int l = 1; l < 4; ++l) {
        if (m.nverts[0] < m.nverts[l]) {
            printf("FAIL: SOLID (%d) not the largest layer; layer %d has %d\n",
                   m.nverts[0], l, m.nverts[l]);
            fail = 1;
        }
    }

    /* geometry + uv invariants */
    const float lo = -1.0f, hix = 16.0f + 1.0f, hiy = 256.0f + 1.0f;
    for (int l = 0; l < 4; ++l) {
        for (int i = 0; i < m.nverts[l]; ++i) {
            CrVertex *v = &m.verts[l][i];
            if (!isfinite(v->pos.x) || !isfinite(v->pos.y) || !isfinite(v->pos.z)) {
                printf("FAIL: layer %d vert %d has non-finite position\n", l, i);
                fail = 1; break;
            }
            if (v->pos.x < lo || v->pos.x > hix ||
                v->pos.z < lo || v->pos.z > hix ||
                v->pos.y < lo || v->pos.y > hiy) {
                printf("FAIL: layer %d vert %d pos (%f,%f,%f) outside chunk column\n",
                       l, i, v->pos.x, v->pos.y, v->pos.z);
                fail = 1; break;
            }
            if (!isfinite(v->uv.x) || !isfinite(v->uv.y) ||
                v->uv.x < 0.0f || v->uv.x > 1.0f || v->uv.y < 0.0f || v->uv.y > 1.0f) {
                printf("FAIL: layer %d vert %d uv (%f,%f) outside [0,1]\n",
                       l, i, v->uv.x, v->uv.y);
                fail = 1; break;
            }
            if (!isfinite(v->light) || !isfinite(v->ao)) {
                printf("FAIL: layer %d vert %d non-finite light/ao\n", l, i);
                fail = 1; break;
            }
        }
    }

    worldmc_free_mesh(&m);
    worldmc_destroy(w);

    if (fail) { printf("FAIL\n"); return 1; }
    printf("PASS\n");
    return 0;
}
