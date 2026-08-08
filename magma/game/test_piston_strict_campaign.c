#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int dx[6] = {0, 0, 0, 0, -1, 1};
static const int dy[6] = {-1, 1, 0, 0, 0, 0};
static const int dz[6] = {0, 0, -1, 1, 0, 0};

/* A connected spiral in the plane perpendicular to piston travel.  Every
 * prefix is connected, so sizes 1..12 cover the complete vanilla push-limit
 * ladder while the later prefixes force recursive slime branching. */
static const int spiral[12][2] = {
    {0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0},
    {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {2, 0}, {2, 1},
};

static void perpendicular_axes(int facing, int p[3], int q[3]) {
    memset(p, 0, 3 * sizeof *p);
    memset(q, 0, 3 * sizeof *q);
    if (facing < 2) { p[0] = 1; q[2] = 1; }
    else if (facing < 4) { p[0] = 1; q[1] = 1; }
    else { p[1] = 1; q[2] = 1; }
}

static int files_equal(const char *a, const char *b) {
    FILE *left = fopen(a, "rb"), *right = fopen(b, "rb");
    int equal = left && right;
    while (equal) {
        unsigned char x[65536], y[65536];
        size_t nx = fread(x, 1, sizeof x, left);
        size_t ny = fread(y, 1, sizeof y, right);
        if (nx != ny || memcmp(x, y, nx)) equal = 0;
        if (nx < sizeof x || ny < sizeof y) {
            if (ferror(left) || ferror(right)) equal = 0;
            break;
        }
    }
    if (left) fclose(left);
    if (right) fclose(right);
    return equal;
}

static void base_for(int facing, int size, int base[3]) {
    int p[3], q[3];
    int column = (size - 1) & 3;
    int row = (size - 1) >> 2;
    perpendicular_axes(facing, p, q);
    base[0] = (column * 9 - 13) * p[0] + (row * 9 - 9) * q[0];
    base[1] = 96 + (column * 9 - 13) * p[1] + (row * 9 - 9) * q[1];
    base[2] = (column * 9 - 13) * p[2] + (row * 9 - 9) * q[2];
}

static int fixture(GmRuntime *r, int facing, int sticky) {
    GmConfig config;
    char error[256] = {0};
    int p[3], q[3];
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 2;
    config.mobs = 0;
    config.weather = 0;
    config.daylight = 0;
    if (!gm_runtime_init(r, &config, error, sizeof error)) {
        fprintf(stderr, "init: %s\n", error);
        return 0;
    }
    gm_world_ensure(r->world, 0, 0, 2);
    /* Superflat terrain ends far below these fixtures. Avoid hundreds of
     * thousands of redundant air writes: each invokes lighting callbacks. */
    perpendicular_axes(facing, p, q);
    for (int size = 1; size <= 12; ++size) {
        int base[3];
        base_for(facing, size, base);
        gm_world_set_block_meta(r->world, base[0], base[1], base[2],
                                sticky ? 29 : 33, facing);
        for (int index = 0; index < size; ++index) {
            int x = base[0] + dx[facing]
                + spiral[index][0] * p[0] + spiral[index][1] * q[0];
            int y = base[1] + dy[facing]
                + spiral[index][0] * p[1] + spiral[index][1] * q[1];
            int z = base[2] + dz[facing]
                + spiral[index][0] * p[2] + spiral[index][1] * q[2];
            gm_world_set_block_meta(r->world, x, y, z, 165, 0);
        }
        /* A stationary item intersects each leading moving plane. */
        if (!gm_runtime_spawn_item_fixture(
                r, 800000 + facing * 32 + sticky * 16 + size,
                base[0] + 0.5 + dx[facing] * 1.45,
                base[1] + 0.5 + dy[facing] * 1.45,
                base[2] + 0.5 + dz[facing] * 1.45,
                0.0, 0.0, 0.0, 1, 1, 0, 0, 32767, 1))
            return 0;
    }
    return gm_runtime_set_world_random_seed48(
        r, UINT64_C(0x123456789abc));
}

static int run_batch(int facing, int sticky,
        int reload, const char *out) {
    GmRuntime *r = calloc(1, sizeof *r);
    GmAction idle = {.hotbar_sel = -1};
    int expected = 0;
    if (!r || !fixture(r, facing, sticky)) {
        free(r);
        return 0;
    }
    for (int size = 1; size <= 12; ++size) {
        int base[3];
        base_for(facing, size, base);
        expected += size + 1;
        if (!gm_runtime_set_block(r,
                base[0] - dx[facing], base[1] - dy[facing],
                base[2] - dz[facing], 152, 0)
                || r->piston_count != expected) {
            fprintf(stderr,
                "extension start face=%d sticky=%d size=%d moving=%d expected=%d\n",
                facing, sticky, size, r->piston_count, expected);
            gm_runtime_destroy(r); free(r); return 0;
        }
    }
    gm_runtime_tick(r, idle);
    if (reload && (!gm_runtime_write_checkpoint(
                r, ".red04-strict-mid.bin")
            || !gm_runtime_load_checkpoint(
                r, ".red04-strict-mid.bin"))) {
        fprintf(stderr, "extension reload face=%d sticky=%d\n",
                facing, sticky);
        gm_runtime_destroy(r); free(r); return 0;
    }
    for (int tick = 0; tick < 3; ++tick) gm_runtime_tick(r, idle);
    if (r->piston_count != 0) {
        fprintf(stderr, "extension settle face=%d sticky=%d count=%d\n",
                facing, sticky, r->piston_count);
        gm_runtime_destroy(r); free(r); return 0;
    }
    if (sticky) {
        expected = 0;
        for (int size = 1; size <= 12; ++size) {
            int base[3];
            base_for(facing, size, base);
            expected += size + 1;
            if (!gm_runtime_set_block(r,
                    base[0] - dx[facing], base[1] - dy[facing],
                    base[2] - dz[facing], 0, 0)
                    || r->piston_count != expected) {
                fprintf(stderr,
                    "pull start face=%d size=%d moving=%d expected=%d\n",
                    facing, size, r->piston_count, expected);
                gm_runtime_destroy(r); free(r); return 0;
            }
        }
        gm_runtime_tick(r, idle);
        if (reload && (!gm_runtime_write_checkpoint(
                    r, ".red04-strict-mid.bin")
                || !gm_runtime_load_checkpoint(
                    r, ".red04-strict-mid.bin"))) {
            fprintf(stderr, "pull reload face=%d\n", facing);
            gm_runtime_destroy(r); free(r); return 0;
        }
        for (int tick = 0; tick < 3; ++tick) gm_runtime_tick(r, idle);
        if (r->piston_count != 0) {
            fprintf(stderr, "pull settle face=%d count=%d\n",
                    facing, r->piston_count);
            gm_runtime_destroy(r); free(r); return 0;
        }
    }
    if (!gm_runtime_write_checkpoint(r, out)) {
        gm_runtime_destroy(r); free(r); return 0;
    }
    gm_runtime_destroy(r);
    free(r);
    return 1;
}

int main(void) {
    int cases = 0;
    for (int facing = 0; facing < 6; ++facing)
        for (int sticky = 0; sticky < 2; ++sticky)
            {
                const char *continuous = ".red04-strict-continuous.bin";
                const char *reloaded = ".red04-strict-reloaded.bin";
                if (!run_batch(facing, sticky, 0, continuous)
                        || !run_batch(facing, sticky, 1, reloaded)
                        || !files_equal(continuous, reloaded)) {
                    fprintf(stderr,
                        "FAIL batch face=%d sticky=%d continuation\n",
                        facing, sticky);
                    return 1;
                }
                cases += 12;
            }
    remove(".red04-strict-mid.bin");
    remove(".red04-strict-mid.bin.tmp");
    remove(".red04-strict-continuous.bin");
    remove(".red04-strict-reloaded.bin");
    printf("piston_strict_campaign: PASS %d generated topologies, "
           "sizes 1..12, six faces, normal/sticky, entity collision, "
           "extension/pull and mid-motion reload\n", cases);
    return 0;
}
