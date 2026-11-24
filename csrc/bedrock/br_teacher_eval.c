#define _POSIX_C_SOURCE 200809L

/* Lane D (br-distill-bridge): Standalone Authentic Verification & Zero-Shot CPU
 * libnetherite.a Teacher Replay + distill.cu ABI Check Tool.
 *
 * Usage:
 *   br_teacher_eval                                        (runs Authentic Tests 1, 2, 3)
 *   br_teacher_eval --self-test                            (runs Authentic Tests 1, 2, 3)
 *   br_teacher_eval --teacher FILE.bin --seeds FILE.bin [--episodes 16] [--ticks 400] [--temp 0.0] [--seed 42]
 */

#include "br_obs_bridge.h"
#include "engine/world.h"
#include "engine/entity.h"
#include "engine/blocks.h"
#include "runtime/train/net.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Compile-time cross-check between br_contract.h and csrc/runtime/train/net.h */
_Static_assert(NETH_NET_CAM == 2, "net.h camera count must be 2");
_Static_assert(NETH_NET_BINS == BRC_CAM_BINS, "net.h camera bins must match BRC_CAM_BINS");
_Static_assert(NETH_NET_KEYS == BRC_KEYS, "net.h keys count must match BRC_KEYS");
_Static_assert(NETH_NET_HOT == BRC_HOTBAR_CATS, "net.h hotbar cats must match BRC_HOTBAR_CATS");
_Static_assert(NETH_NET_CAM + NETH_NET_KEYS + 1 == BRC_S1_ACT_DIM,
               "s1 hotbar action dim must match BRC_S1_ACT_DIM (17)");

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

static uint64_t sm64(uint64_t *s) {
    uint64_t z = (*s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static double rand_u01(uint64_t *s) {
    return (double)(sm64(s) >> 11) * (1.0 / 9007199254740992.0);
}

static inline int floor_div16(int x) {
    return (x >= 0) ? (x / 16) : ((x - 15) / 16);
}

static inline int brc_idx(int x, int y, int z) {
    return ((y << 12) | (z << 6) | x);
}

static void bubble_set_blk(brc_seed_entry_t *seed, int x, int y, int z, uint8_t id, uint8_t meta) {
    if ((unsigned)x >= (unsigned)BRC_WX ||
        (unsigned)y >= (unsigned)BRC_WY ||
        (unsigned)z >= (unsigned)BRC_WZ) return;
    int idx = brc_idx(x, y, z);
    seed->blocks[idx] = id;
    seed->meta[idx] = meta;
    if (brc_blk_is_solid(id)) {
        seed->solid_bits[idx >> 5] |= (1u << (idx & 31));
    } else {
        seed->solid_bits[idx >> 5] &= ~(1u << (idx & 31));
    }
}

/* Populate a synthetic lava-pool + obsidian + portal test bubble */
static void build_synthetic_portal_seed(brc_seed_entry_t *seed, int32_t ox, int32_t oy, int32_t oz) {
    memset(seed, 0, sizeof(*seed));
    seed->origin_x = ox;
    seed->origin_y = oy;
    seed->origin_z = oz;
    seed->spawn_x = 24.5f;
    seed->spawn_y = 9.0f;
    seed->spawn_z = 24.5f;
    seed->spawn_yaw = -45.0f; /* roughly facing (+X, +Z) toward lava at (34, 8, 34) */
    seed->spawn_pitch = 10.0f;
    seed->lava_x = 34.5f;
    seed->lava_y = 8.0f;
    seed->lava_z = 34.5f;
    snprintf(seed->source_name, sizeof(seed->source_name), "synthetic_portal_bubble");

    /* Default speedrunner hotbar */
    seed->hotbar_item[0] = BRC_ITEM_IRON_PICK;    seed->hotbar_cnt[0] = 1;
    seed->hotbar_item[1] = BRC_ITEM_BUCKET_WATER; seed->hotbar_cnt[1] = 1;
    seed->hotbar_item[2] = BRC_ITEM_BUCKET_LAVA;  seed->hotbar_cnt[2] = 1;
    seed->hotbar_item[3] = BRC_ITEM_BUCKET_EMPTY; seed->hotbar_cnt[3] = 1;
    seed->hotbar_item[4] = BRC_ITEM_FLINT_STEEL;  seed->hotbar_cnt[4] = 1;
    seed->hotbar_item[5] = BRC_ITEM_COBBLE;       seed->hotbar_cnt[5] = 32;
    seed->hotbar_item[6] = BRC_ITEM_DIRT;         seed->hotbar_cnt[6] = 16;
    seed->sel_slot = 1;

    /* Rolling hills terrain */
    for (int z = 0; z < BRC_WZ; z++) {
        for (int x = 0; x < BRC_WX; x++) {
            double dx = (double)x - 34.0;
            double dz = (double)z - 34.0;
            double r = sqrt(dx * dx + dz * dz);
            int top_y = 8;
            if (r > 7.0) {
                double hill = 2.5 * sin((double)x * 0.17) * cos((double)z * 0.19)
                            + 1.5 * sin((double)(x + z) * 0.11);
                top_y = 8 + (int)llround(hill);
                if (top_y < 5) top_y = 5;
                if (top_y > 14) top_y = 14;
            }
            for (int y = 0; y <= top_y; y++) {
                uint8_t blk = BRC_BLK_STONE;
                if (y == top_y) blk = (r < 9.0) ? BRC_BLK_SAND : BRC_BLK_GRASS;
                else if (y >= top_y - 2) blk = BRC_BLK_DIRT;
                bubble_set_blk(seed, x, y, z, blk, 0);
            }
        }
    }

    /* Surface lava pool centered at (34, 8, 34), 6x6 depression */
    int lava_src = 0;
    for (int z = 31; z <= 36; z++) {
        for (int x = 31; x <= 36; x++) {
            bubble_set_blk(seed, x, 7, z, BRC_BLK_STONE, 0);
            uint8_t lid = ((x == 31 || z == 31) ? BRC_BLK_FLOW_LAVA : BRC_BLK_LAVA);
            bubble_set_blk(seed, x, 8, z, lid, (lid == BRC_BLK_LAVA) ? 0 : 2);
            if (lid == BRC_BLK_LAVA) lava_src++;
            for (int y = 9; y < BRC_WY; y++) {
                bubble_set_blk(seed, x, y, z, BRC_BLK_AIR, 0);
            }
        }
    }
    seed->lava_sources = lava_src;

    /* Adjacent water pocket + small cascade for full block-class ray coverage */
    for (int z = 27; z <= 30; z++) {
        for (int x = 30; x <= 33; x++) {
            bubble_set_blk(seed, x, 8, z, BRC_BLK_WATER, 0);
            bubble_set_blk(seed, x, 9, z, BRC_BLK_AIR, 0);
        }
    }
    for (int y = 9; y <= 12; y++) {
        bubble_set_blk(seed, 31, y, 28, BRC_BLK_FLOW_WATER, 0);
        bubble_set_blk(seed, 32, y, 28, BRC_BLK_WATER, 0);
    }

    /* Partial obsidian portal frame + lit portal blocks for class-code coverage */
    for (int y = 9; y <= 13; y++) {
        bubble_set_blk(seed, 38, y, 32, BRC_BLK_OBSIDIAN, 0);
        bubble_set_blk(seed, 38, y, 35, BRC_BLK_OBSIDIAN, 0);
    }
    for (int z = 32; z <= 35; z++) {
        bubble_set_blk(seed, 38, 9,  z, BRC_BLK_OBSIDIAN, 0);
        bubble_set_blk(seed, 38, 13, z, BRC_BLK_OBSIDIAN, 0);
    }
    for (int y = 10; y <= 12; y++) {
        for (int z = 33; z <= 34; z++) {
            bubble_set_blk(seed, 38, y, z, BRC_BLK_PORTAL, 0);
        }
    }
    /* A cobble pillar + oak tree + fire block */
    for (int y = 9; y <= 14; y++) {
        bubble_set_blk(seed, 28, y, 30, BRC_BLK_COBBLE, 0);
        bubble_set_blk(seed, 20, y, 28, BRC_BLK_LOG, 0);
    }
    bubble_set_blk(seed, 37, 9, 33, BRC_BLK_FIRE, 0);
}

/* Load a brc_seed_entry_t + optional deltas into a real libnetherite.a struct world
 * using empty air chunks (world_insert_chunk) around the bubble so exterior is clean air. */
static void load_seed_into_world(struct world *w,
                                 const brc_seed_entry_t *seed,
                                 const br_delta_entry_t *deltas,
                                 int n_deltas) {
    world_init(w, 1);
    int cx0 = floor_div16(seed->origin_x) - 1;
    int cx1 = floor_div16(seed->origin_x + BRC_WX - 1) + 1;
    int cz0 = floor_div16(seed->origin_z) - 1;
    int cz1 = floor_div16(seed->origin_z + BRC_WZ - 1) + 1;
    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            world_insert_chunk(w, cx, cz, NULL);
        }
    }

    for (int y = 0; y < BRC_WY; y++) {
        int wy = seed->origin_y + y;
        if (wy < 0 || wy >= 256) continue;
        for (int z = 0; z < BRC_WZ; z++) {
            int wz = seed->origin_z + z;
            for (int x = 0; x < BRC_WX; x++) {
                int idx = brc_idx(x, y, z);
                uint8_t id = seed->blocks[idx];
                if (id != BRC_BLK_AIR) {
                    world_set_block(w, seed->origin_x + x, wy, wz, (int)id, (int)seed->meta[idx], 0);
                }
            }
        }
    }

    for (int i = 0; i < n_deltas; i++) {
        int wy = seed->origin_y + (int)deltas[i].y;
        if (wy < 0 || wy >= 256) continue;
        world_set_block(w,
                        seed->origin_x + (int)deltas[i].x,
                        wy,
                        seed->origin_z + (int)deltas[i].z,
                        (int)deltas[i].block_id,
                        (int)deltas[i].meta,
                        0);
    }
}

/* ============================================================================
 * Authentic Test 1: World vs brc_seed_entry_t (+ deltas) obs[128] parity
 * ============================================================================ */
static int test_obs_parity(void) {
    printf("[Test 1] World vs brc_seed_entry_t obs[128] parity (64 random poses)...\n");

    brc_seed_entry_t *seed = (brc_seed_entry_t *)calloc(1, sizeof(*seed));
    struct world *w = (struct world *)calloc(1, sizeof(*w));
    if (!seed || !w) {
        fprintf(stderr, "  FAIL: OOM\n");
        free(seed);
        free(w);
        return 1;
    }

    /* Non-zero world origin to prove coordinate-shift invariance */
    build_synthetic_portal_seed(seed, 16, 48, -32);

    /* Live delta overlay including double-overwrite to test last-write-wins */
    br_delta_entry_t deltas[10] = {
        { 32, 8,  32, BRC_BLK_OBSIDIAN, 0 },
        { 33, 8,  32, BRC_BLK_COBBLE,   0 },
        { 33, 8,  32, BRC_BLK_OBSIDIAN, 0 }, /* overrides cobble -> obsidian */
        { 34, 9,  33, BRC_BLK_WATER,    0 },
        { 35, 9,  35, BRC_BLK_COBBLE,   0 },
        { 35, 10, 35, BRC_BLK_COBBLE,   0 },
        { 30, 9,  34, BRC_BLK_OBSIDIAN, 0 },
        { 30, 10, 34, BRC_BLK_PORTAL,   0 },
        { 28, 10, 30, BRC_BLK_AIR,      0 }, /* punch hole in cobble pillar */
        { 26, 9,  26, BRC_BLK_LAVA,     0 }
    };

    load_seed_into_world(w, seed, deltas, 10);

    uint64_t rng = 0x123456789abcdef0ULL;
    float max_abs_diff = 0.0f;
    int worst_pose = -1, worst_idx = -1;
    int nan_count = 0;
    int seen_class_air = 0, seen_class_solid = 0, seen_class_lava = 0;
    int seen_class_water = 0, seen_class_obs = 0, seen_class_portal = 0;

    for (int p = 0; p < 64; p++) {
        br_obs_state_t st_b;
        memset(&st_b, 0, sizeof(st_b));
        /* Keep poses around the lava/portal region (16..46, 9..16, 16..46) */
        st_b.px = 20.0 + rand_u01(&rng) * 24.0;
        st_b.py = 9.0  + rand_u01(&rng) * 7.0;
        st_b.pz = 20.0 + rand_u01(&rng) * 24.0;
        st_b.vx = (rand_u01(&rng) - 0.5) * 0.8;
        st_b.vy = (rand_u01(&rng) - 0.5) * 0.8;
        st_b.vz = (rand_u01(&rng) - 0.5) * 0.8;
        st_b.yaw_deg   = (float)(rand_u01(&rng) * 360.0 - 180.0);
        st_b.pitch_deg = (float)(rand_u01(&rng) * 140.0 - 70.0);
        st_b.on_ground = (uint8_t)(sm64(&rng) & 1);
        st_b.sneaking  = (uint8_t)(sm64(&rng) & 1);
        st_b.hp        = (float)(1.0 + rand_u01(&rng) * 19.0);
        memcpy(st_b.hotbar_item, seed->hotbar_item, sizeof(st_b.hotbar_item));
        memcpy(st_b.hotbar_cnt,  seed->hotbar_cnt,  sizeof(st_b.hotbar_cnt));
        st_b.sel_slot        = (uint8_t)(sm64(&rng) % 9);
        st_b.lava_x          = seed->lava_x;
        st_b.lava_y          = seed->lava_y;
        st_b.lava_z          = seed->lava_z;
        st_b.obsidian_placed = (int32_t)(sm64(&rng) % 12);
        st_b.portal_lit      = (uint8_t)(sm64(&rng) & 1);
        st_b.ep_tick         = (int32_t)(sm64(&rng) % 400);
        st_b.max_ticks       = 400;

        /* Corresponding world-space state shifted by (origin_x, origin_y, origin_z) */
        br_obs_state_t st_w = st_b;
        st_w.px     += (double)seed->origin_x;
        st_w.py     += (double)seed->origin_y;
        st_w.pz     += (double)seed->origin_z;
        st_w.lava_x += (double)seed->origin_x;
        st_w.lava_y += (double)seed->origin_y;
        st_w.lava_z += (double)seed->origin_z;

        float obs_w[BRC_OBS_DIM];
        float obs_b[BRC_OBS_DIM];
        br_extract_obs_from_world(w, &st_w, obs_w);
        br_extract_obs_from_bubble(seed, deltas, 10, &st_b, obs_b);

        for (int i = 0; i < BRC_OBS_DIM; i++) {
            if (!isfinite(obs_w[i]) || !isfinite(obs_b[i])) nan_count++;
            float diff = fabsf(obs_w[i] - obs_b[i]);
            if (diff > max_abs_diff) {
                max_abs_diff = diff;
                worst_pose = p;
                worst_idx = i;
            }
            if (i >= 104 && (obs_w[i] != 0.0f || obs_b[i] != 0.0f)) {
                fprintf(stderr, "  FAIL: non-zero pad at obs[%d]\n", i);
                world_free(w); free(w); free(seed);
                return 1;
            }
        }

        /* Track class code diversity across row-1 rays + crosshair */
        for (int c = 64; c <= 79; c++) {
            float v = obs_w[c];
            if (v == 0.00f) seen_class_air++;
            else if (fabsf(v - 0.25f) < 1e-4f) seen_class_solid++;
            else if (fabsf(v - 0.50f) < 1e-4f) seen_class_lava++;
            else if (fabsf(v - 0.75f) < 1e-4f) seen_class_water++;
            else if (fabsf(v - 0.90f) < 1e-4f) seen_class_obs++;
            else if (fabsf(v - 1.00f) < 1e-4f) seen_class_portal++;
        }
    }

    world_free(w);
    free(w);
    free(seed);

    printf("  max_abs_diff=%.3e (worst pose=%d idx=%d) nans=%d\n",
           max_abs_diff, worst_pose, worst_idx, nan_count);
    printf("  row1 class hits: air=%d solid=%d lava=%d water=%d obsidian=%d portal/fire=%d\n",
           seen_class_air, seen_class_solid, seen_class_lava,
           seen_class_water, seen_class_obs, seen_class_portal);

    if (nan_count > 0 || max_abs_diff > 1e-5f) {
        fprintf(stderr, "  FAIL: parity error %.3e > 1e-5 or nans=%d\n", max_abs_diff, nan_count);
        return 1;
    }
    if (seen_class_solid == 0 || seen_class_lava == 0 || seen_class_water == 0 ||
        seen_class_obs == 0 || seen_class_portal == 0) {
        fprintf(stderr, "  FAIL: insufficient block class diversity in raycasts\n");
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ============================================================================
 * Synthetic deterministic BRT1 teacher builder (for Tests 2 and 3)
 * ============================================================================ */
static br_teacher_t *make_synthetic_teacher(uint32_t hidden, uint64_t seed) {
    br_teacher_t *t = br_teacher_alloc(hidden);
    if (!t) return NULL;
    uint64_t rng = seed;
    float s1 = 1.0f / sqrtf((float)BRC_OBS_DIM);
    float s2 = 1.0f / sqrtf((float)hidden);

    for (size_t i = 0; i < (size_t)hidden * BRC_OBS_DIM; i++) {
        t->w1[i] = (float)(rand_u01(&rng) * 2.0 - 1.0) * s1 * 0.5f;
    }
    for (size_t i = 0; i < (size_t)hidden * hidden; i++) {
        t->w2[i] = (float)(rand_u01(&rng) * 2.0 - 1.0) * s2 * 0.5f;
    }
    for (size_t i = 0; i < (size_t)(2 * BRC_CAM_BINS) * hidden; i++) {
        t->w_cam[i] = (float)(rand_u01(&rng) * 2.0 - 1.0) * s2 * 0.25f;
    }
    for (int i = 0; i < 2 * BRC_CAM_BINS; i++) {
        t->b_cam[i] = (float)(rand_u01(&rng) * 0.4 - 0.2);
    }
    /* Slight forward-walking + centered-camera prior for realistic replay */
    t->b_cam[20] += 1.2f;                 /* pitch bin 20 = 0 deg */
    t->b_cam[BRC_CAM_BINS + 20] += 1.0f;  /* yaw bin 20 = 0 deg */

    for (size_t i = 0; i < (size_t)BRC_KEYS * hidden; i++) {
        t->w_keys[i] = (float)(rand_u01(&rng) * 2.0 - 1.0) * s2 * 0.25f;
    }
    for (int i = 0; i < BRC_KEYS; i++) {
        t->b_keys[i] = -0.8f;
    }
    t->b_keys[BRC_KEY_FORWARD] = 1.1f;
    t->b_keys[BRC_KEY_SPRINT]  = 0.4f;

    for (size_t i = 0; i < (size_t)BRC_HOTBAR_CATS * hidden; i++) {
        t->w_hot[i] = (float)(rand_u01(&rng) * 2.0 - 1.0) * s2 * 0.25f;
    }
    for (int i = 0; i < BRC_HOTBAR_CATS; i++) {
        t->b_hot[i] = (i == 0) ? 1.5f : 0.0f;
    }
    for (size_t i = 0; i < (size_t)hidden; i++) {
        t->w_val[i] = (float)(rand_u01(&rng) * 2.0 - 1.0) * s2 * 0.25f;
    }
    t->b_val[0] = 0.25f;
    return t;
}

/* ============================================================================
 * Authentic Test 2: BRT1 Checkpoint Round-Trip, FP32 Reference Check, distill.cu ABI
 * ============================================================================ */
static int test_teacher_and_distill_abi(void) {
    printf("[Test 2] BRT1 checkpoint load/infer + distill.cu ABI verification...\n");

    const char *tmp_ckpt = "/tmp/br_lane_d_test_brt1.bin";
    const uint32_t H = 1024;
    br_teacher_t *orig = make_synthetic_teacher(H, 0xfeedfacecafebeefULL);
    if (!orig) {
        fprintf(stderr, "  FAIL: alloc synthetic teacher\n");
        return 1;
    }

    char err[256] = {0};
    if (br_teacher_save(orig, tmp_ckpt, err, sizeof(err)) != 0) {
        fprintf(stderr, "  FAIL: save BRT1: %s\n", err);
        br_teacher_free(orig);
        return 1;
    }

    br_teacher_t *loaded = br_teacher_load(tmp_ckpt, err, sizeof(err));
    remove(tmp_ckpt);
    if (!loaded) {
        fprintf(stderr, "  FAIL: load BRT1: %s\n", err);
        br_teacher_free(orig);
        return 1;
    }

    /* Build deterministic non-zero obs[128] */
    float obs[BRC_OBS_DIM];
    uint64_t rng = 0x9988776655443322ULL;
    for (int i = 0; i < 104; i++) obs[i] = (float)(rand_u01(&rng) * 2.0 - 1.0);
    for (int i = 104; i < BRC_OBS_DIM; i++) obs[i] = 0.0f;

    /* Run loaded teacher greedy */
    brc_s1_action_t act_greedy;
    br_teacher_out_t out_greedy;
    br_teacher_infer(loaded, obs, 0.0f, NULL, &act_greedy, &out_greedy);

    /* Independent FP64 reference MLP calculation directly from orig weights */
    double *ref_h1 = (double *)calloc(H, sizeof(double));
    double *ref_h2 = (double *)calloc(H, sizeof(double));
    for (uint32_t r = 0; r < H; r++) {
        double acc = 0.0;
        for (int c = 0; c < BRC_OBS_DIM; c++) acc += (double)orig->w1[r * BRC_OBS_DIM + c] * (double)obs[c];
        ref_h1[r] = acc > 0.0 ? acc : 0.0;
    }
    for (uint32_t r = 0; r < H; r++) {
        double acc = 0.0;
        for (uint32_t c = 0; c < H; c++) acc += (double)orig->w2[r * H + c] * ref_h1[c];
        ref_h2[r] = acc > 0.0 ? acc : 0.0;
    }

    double max_logit_err = 0.0;
    for (int r = 0; r < 2 * BRC_CAM_BINS; r++) {
        double acc = (double)orig->b_cam[r];
        for (uint32_t c = 0; c < H; c++) acc += (double)orig->w_cam[(size_t)r * H + c] * ref_h2[c];
        double e = fabs(acc - (double)out_greedy.cam_logits[r]);
        if (e > max_logit_err) max_logit_err = e;
    }
    for (int r = 0; r < BRC_KEYS; r++) {
        double acc = (double)orig->b_keys[r];
        for (uint32_t c = 0; c < H; c++) acc += (double)orig->w_keys[(size_t)r * H + c] * ref_h2[c];
        double e = fabs(acc - (double)out_greedy.key_logits[r]);
        if (e > max_logit_err) max_logit_err = e;
    }
    for (int r = 0; r < BRC_HOTBAR_CATS; r++) {
        double acc = (double)orig->b_hot[r];
        for (uint32_t c = 0; c < H; c++) acc += (double)orig->w_hot[(size_t)r * H + c] * ref_h2[c];
        double e = fabs(acc - (double)out_greedy.hot_logits[r]);
        if (e > max_logit_err) max_logit_err = e;
    }
    double ref_val = (double)orig->b_val[0];
    for (uint32_t c = 0; c < H; c++) ref_val += (double)orig->w_val[c] * ref_h2[c];
    double val_err = fabs(ref_val - (double)out_greedy.value);

    free(ref_h1);
    free(ref_h2);

    /* Check softmax normalization & key probability bounds */
    float sp = 0.0f, sy = 0.0f, sh = 0.0f;
    for (int i = 0; i < BRC_CAM_BINS; i++) {
        sp += out_greedy.pitch_probs[i];
        sy += out_greedy.yaw_probs[i];
    }
    for (int i = 0; i < BRC_HOTBAR_CATS; i++) sh += out_greedy.hot_probs[i];
    int prob_ok = (fabsf(sp - 1.0f) < 1e-5f) && (fabsf(sy - 1.0f) < 1e-5f) && (fabsf(sh - 1.0f) < 1e-5f);
    for (int k = 0; k < BRC_KEYS; k++) {
        if (!(out_greedy.key_probs[k] >= 0.0f && out_greedy.key_probs[k] <= 1.0f)) prob_ok = 0;
    }

    /* Check 64 stochastic draws for valid action ranges */
    int range_ok = 1;
    uint64_t sample_rng = 0xabcdef0123456789ULL;
    for (int s = 0; s < 64; s++) {
        brc_s1_action_t sa;
        br_teacher_infer(loaded, obs, (s == 0) ? 0.0f : 0.85f, &sample_rng, &sa, NULL);
        if (sa.a[0] < 0 || sa.a[0] >= BRC_CAM_BINS) range_ok = 0;
        if (sa.a[1] < 0 || sa.a[1] >= BRC_CAM_BINS) range_ok = 0;
        for (int k = 0; k < BRC_KEYS; k++) {
            if (sa.a[2 + k] != 0 && sa.a[2 + k] != 1) range_ok = 0;
        }
        if (sa.a[16] < 0 || sa.a[16] >= BRC_HOTBAR_CATS) range_ok = 0;
    }

    /* Check distill.cu record emission & ABI offsets */
    brc_s1_action_t prev_a;
    memset(&prev_a, 0, sizeof(prev_a));
    prev_a.a[0] = 17;
    prev_a.a[1] = 24;
    prev_a.a[2 + BRC_KEY_FORWARD] = 1;
    prev_a.a[2 + BRC_KEY_MINE]    = 1;
    prev_a.a[2 + BRC_KEY_USE]     = 1;
    prev_a.a[16] = 2;

    brc_distill_rec_t rec0, rec1, rec_inv;
    brc_distill_rec_nohot_t rec_nh;
    br_emit_distill_rec(&rec0, &act_greedy, NULL, 0, 1.0f, 1, 1, 0);
    br_emit_distill_rec(&rec1, &act_greedy, &prev_a, 3, 1.0f, 1, 1, 1);
    br_emit_distill_rec(&rec_inv, &act_greedy, &prev_a, 0, 1.0f, 0, 1, 0);
    br_emit_distill_rec_nohot(&rec_nh, &act_greedy, &prev_a, 3, 1.0f, 1, 1, 0);

    int32_t expected_mask = (1 << BRC_KEY_FORWARD) | (1 << BRC_KEY_MINE) | (1 << BRC_KEY_USE);
    int rec_ok = 1;
    if (rec0.p[0] != 20 || rec0.p[1] != 20 || rec0.p[2] != 0 || rec0.p[3] != 0) rec_ok = 0;
    if (rec1.p[0] != 17 || rec1.p[1] != 24 || rec1.p[2] != expected_mask || rec1.p[3] != 3) rec_ok = 0;
    if (rec1.w != 1.0f || rec1.valid != 1 || rec1.soft != 1 || rec1.hold != 1) rec_ok = 0;
    if (rec_inv.valid != 0 || rec_inv.w != 0.0f || rec_inv.a[0] != 20 || rec_inv.a[1] != 20) rec_ok = 0;
    if (rec_nh.p[2] != expected_mask || rec_nh.valid != 1 || rec_nh.soft != 1) rec_ok = 0;

    br_teacher_free(orig);
    br_teacher_free(loaded);

    /* Also verify BRS1 seed pack round-trip via br_load_seed_pack() */
    const char *tmp_seeds = "/tmp/br_lane_d_test_brs1.bin";
    brc_seed_entry_t *pack_in = (brc_seed_entry_t *)calloc(2, sizeof(*pack_in));
    build_synthetic_portal_seed(&pack_in[0], 0, 64, 0);
    build_synthetic_portal_seed(&pack_in[1], -64, 56, 32);
    brc_seed_file_hdr_t shdr = { BRC_SEED_MAGIC, 1u, 2u, 0u };
    FILE *sfp = fopen(tmp_seeds, "wb");
    if (!sfp ||
        fwrite(&shdr, sizeof(shdr), 1, sfp) != 1 ||
        fwrite(pack_in, sizeof(*pack_in), 2, sfp) != 2) {
        if (sfp) fclose(sfp);
        free(pack_in);
        remove(tmp_seeds);
        fprintf(stderr, "  FAIL: write temp BRS1 pack\n");
        return 1;
    }
    fclose(sfp);
    brc_seed_entry_t *pack_out = NULL;
    uint32_t pack_cnt = 0;
    int brs_rc = br_load_seed_pack(tmp_seeds, &pack_out, &pack_cnt, err, sizeof(err));
    remove(tmp_seeds);
    int brs_ok = (brs_rc == 0 && pack_cnt == 2 &&
                  memcmp(pack_in, pack_out, 2 * sizeof(*pack_in)) == 0);
    free(pack_in);
    free(pack_out);

    printf("  H=%u n_floats=%zu max_logit_err=%.3e val_err=%.3e brs1_roundtrip=%s\n",
           H, br_teacher_num_floats(&(brc_teacher_hdr_t){.obs_dim=128,.hidden=H,.cam_bins=41,.n_keys=14,.n_hotbar=10}),
           max_logit_err, val_err, brs_ok ? "ok" : "FAIL");
    printf("  probs: sum_pitch=%.6f sum_yaw=%.6f sum_hot=%.6f greedy_act=[p=%d y=%d fwd=%d hot=%d]\n",
           sp, sy, sh, act_greedy.a[0], act_greedy.a[1], act_greedy.a[2 + BRC_KEY_FORWARD], act_greedy.a[16]);
    printf("  distill.cu ABI: sizeof(hotbar=1)=%zu (p@%zu w@%zu valid@%zu soft@%zu hold@%zu), sizeof(hotbar=0)=%zu\n",
           sizeof(brc_distill_rec_t),
           offsetof(brc_distill_rec_t, p),
           offsetof(brc_distill_rec_t, w),
           offsetof(brc_distill_rec_t, valid),
           offsetof(brc_distill_rec_t, soft),
           offsetof(brc_distill_rec_t, hold),
           sizeof(brc_distill_rec_nohot_t));

    if (max_logit_err > 1e-4 || val_err > 1e-4 || !prob_ok || !range_ok || !rec_ok || !brs_ok) {
        fprintf(stderr, "  FAIL: logit_err=%.3e val_err=%.3e prob_ok=%d range_ok=%d rec_ok=%d brs_ok=%d\n",
                max_logit_err, val_err, prob_ok, range_ok, rec_ok, brs_ok);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ============================================================================
 * Shared CPU libnetherite.a Episode Runner (used by Test 3 and CLI Eval)
 * ============================================================================ */

typedef struct {
    double start_x, start_y, start_z;
    double end_x, end_y, end_z;
    double dist_traveled;
    double min_lava_dist;
    int    ticks_run;
    int    obsidian_placed;
    int    portal_lit;
    int    hotbar_switches;
    int    blocks_placed;
    int    blocks_broken;
    int    distill_recs;
    int    nan_events;
    double elapsed_ms;
} br_ep_result_t;

static float wrap_yaw_deg(float yaw) {
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    return yaw;
}

static float clamp_pitch_deg(float pitch) {
    if (pitch < -90.0f) return -90.0f;
    if (pitch > 90.0f)  return 90.0f;
    return pitch;
}

/* Execute one CPU 1.7.10 tick on real libnetherite.a struct world + struct entity */
static void step_cpu_world_tick(struct world *w,
                                struct entity *e,
                                br_obs_state_t *st,
                                int *jump_ticks,
                                const brc_s1_action_t *act,
                                br_ep_result_t *res) {
    /* 1. Camera delta using exact 41-bin mu-law table from br_contract.h */
    float dpitch = brc_cam_deg(act->a[0]);
    float dyaw   = brc_cam_deg(act->a[1]);
    st->pitch_deg = clamp_pitch_deg(st->pitch_deg + dpitch);
    st->yaw_deg   = wrap_yaw_deg(st->yaw_deg + dyaw);

    /* 2. Hotbar switch (0 = keep, 1..9 = slot 0..8) */
    if (act->a[16] >= 1 && act->a[16] <= 9) {
        uint8_t ns = (uint8_t)(act->a[16] - 1);
        if (ns != st->sel_slot) {
            st->sel_slot = ns;
            res->hotbar_switches++;
        }
    }

    int k_fwd    = act->a[2 + BRC_KEY_FORWARD] > 0;
    int k_back   = act->a[2 + BRC_KEY_BACK] > 0;
    int k_left   = act->a[2 + BRC_KEY_LEFT] > 0;
    int k_right  = act->a[2 + BRC_KEY_RIGHT] > 0;
    int k_sprint = act->a[2 + BRC_KEY_SPRINT] > 0;
    int k_jump   = act->a[2 + BRC_KEY_JUMP] > 0;
    int k_sneak  = act->a[2 + BRC_KEY_SNEAK] > 0;
    int k_mine   = act->a[2 + BRC_KEY_MINE] > 0;
    int k_use    = act->a[2 + BRC_KEY_USE] > 0;

    st->sneaking = (uint8_t)k_sneak;

    /* 3. Exact Minecraft 1.7.10 living_move_with_heading + entity_move */
    if (fabs(e->motion_x) < 0.005) e->motion_x = 0.0;
    if (fabs(e->motion_y) < 0.005) e->motion_y = 0.0;
    if (fabs(e->motion_z) < 0.005) e->motion_z = 0.0;

    if (*jump_ticks > 0) (*jump_ticks)--;
    if (k_jump) {
        if (e->on_ground && *jump_ticks == 0) {
            e->motion_y = 0.41999998688697815;
            if (k_sprint) {
                float yr = st->yaw_deg * 0.017453292519943295f;
                e->motion_x -= (double)(sinf(yr) * 0.2f);
                e->motion_z += (double)(cosf(yr) * 0.2f);
            }
            *jump_ticks = 10;
        }
    } else {
        *jump_ticks = 0;
    }

    float strafe = ((k_left ? 1.0f : 0.0f) - (k_right ? 1.0f : 0.0f)) * 0.98f;
    float fwd    = ((k_fwd  ? 1.0f : 0.0f) - (k_back  ? 1.0f : 0.0f)) * 0.98f;
    if (k_sneak) {
        strafe *= 0.3f;
        fwd    *= 0.3f;
    }

    float v3 = 0.91f;
    if (e->on_ground) {
        int bx = (int)floor(e->pos_x);
        int by = (int)floor(e->bounding_box.min_y) - 1;
        int bz = (int)floor(e->pos_z);
        v3 = BLOCKS[world_get_block(w, bx, by, bz) & 4095].slipperiness * 0.91f;
    }
    float v4 = 0.16277136f / (v3 * v3 * v3);
    float base_spd = (k_sprint && !k_sneak) ? 0.13000001f : 0.1f;
    float v5 = e->on_ground ? (base_spd * v4) : ((k_sprint && !k_sneak) ? 0.026f : 0.02f);

    float dist2 = strafe * strafe + fwd * fwd;
    if (dist2 >= 1.0e-4f) {
        float d = sqrtf(dist2);
        if (d < 1.0f) d = 1.0f;
        float sc = v5 / d;
        float s = strafe * sc, f = fwd * sc;
        float yr = st->yaw_deg * 0.017453292519943295f;
        float sy = sinf(yr), cy = cosf(yr);
        e->motion_x += (double)(s * cy - f * sy);
        e->motion_z += (double)(f * cy + s * sy);
    }

    double old_x = e->pos_x, old_y = e->pos_y, old_z = e->pos_z;
    entity_move_ex(e, e->motion_x, e->motion_y, e->motion_z, k_sneak, NULL);
    double dx = e->pos_x - old_x, dy = e->pos_y - old_y, dz = e->pos_z - old_z;
    res->dist_traveled += sqrt(dx * dx + dy * dy + dz * dz);

    e->motion_y -= 0.08;
    e->motion_y *= 0.9800000190734863;
    e->motion_x *= (double)v3;
    e->motion_z *= (double)v3;

    /* Sync position & velocity back into obs state */
    st->px = e->pos_x;
    st->py = e->pos_y;
    st->pz = e->pos_z;
    st->vx = e->motion_x;
    st->vy = e->motion_y;
    st->vz = e->motion_z;
    st->on_ground = e->on_ground ? 1 : 0;

    /* 4. Survival-reach USE / MINE interaction on live struct world */
    double eye_y = st->py + (st->sneaking ? 1.54 : 1.62);
    if (k_use || k_mine) {
        int hx = 0, hy = 0, hz = 0, px = 0, py = 0, pz = 0;
        uint8_t hit = br_world_raycast(w, st->px, eye_y, st->pz,
                                       st->yaw_deg, st->pitch_deg, 4.5,
                                       &hx, &hy, &hz, &px, &py, &pz, NULL);
        if (k_use && hit != BRC_BLK_AIR) {
            int slot = st->sel_slot < 9 ? (int)st->sel_slot : 0;
            uint16_t item = (st->hotbar_cnt[slot] > 0) ? st->hotbar_item[slot] : BRC_ITEM_NONE;
            if (item == BRC_ITEM_BUCKET_WATER) {
                if (hit == BRC_BLK_LAVA || hit == BRC_BLK_FLOW_LAVA) {
                    world_set_block(w, hx, hy, hz, BRC_BLK_OBSIDIAN, 0, 0);
                    st->obsidian_placed++;
                    res->obsidian_placed++;
                } else if (py >= 0 && py < 256) {
                    world_set_block(w, px, py, pz, BRC_BLK_WATER, 0, 0);
                    st->hotbar_item[slot] = BRC_ITEM_BUCKET_EMPTY;
                    res->blocks_placed++;
                }
            } else if (item == BRC_ITEM_BUCKET_EMPTY) {
                if (hit == BRC_BLK_LAVA) {
                    world_set_block(w, hx, hy, hz, BRC_BLK_AIR, 0, 0);
                    st->hotbar_item[slot] = BRC_ITEM_BUCKET_LAVA;
                } else if (hit == BRC_BLK_WATER) {
                    world_set_block(w, hx, hy, hz, BRC_BLK_AIR, 0, 0);
                    st->hotbar_item[slot] = BRC_ITEM_BUCKET_WATER;
                }
            } else if (item == BRC_ITEM_BUCKET_LAVA && py >= 0 && py < 256) {
                world_set_block(w, px, py, pz, BRC_BLK_LAVA, 0, 0);
                st->hotbar_item[slot] = BRC_ITEM_BUCKET_EMPTY;
                res->blocks_placed++;
            } else if (item == BRC_ITEM_FLINT_STEEL && hit == BRC_BLK_OBSIDIAN && py >= 0 && py < 256) {
                if (st->obsidian_placed >= 10) {
                    world_set_block(w, px, py, pz, BRC_BLK_PORTAL, 0, 0);
                    st->portal_lit = 1;
                    res->portal_lit = 1;
                } else {
                    world_set_block(w, px, py, pz, BRC_BLK_FIRE, 0, 0);
                }
                res->blocks_placed++;
            } else if ((item == BRC_ITEM_COBBLE || item == BRC_ITEM_DIRT || item == BRC_ITEM_STONE) &&
                       py >= 0 && py < 256) {
                /* Avoid placing a block inside the player's AABB */
                int in_player = (px == (int)floor(st->px) && pz == (int)floor(st->pz) &&
                                 (py == (int)floor(st->py) || py == (int)floor(st->py + 1.0)));
                if (!in_player) {
                    world_set_block(w, px, py, pz, (int)item, 0, 0);
                    st->hotbar_cnt[slot]--;
                    if (st->hotbar_cnt[slot] == 0) st->hotbar_item[slot] = BRC_ITEM_NONE;
                    res->blocks_placed++;
                }
            }
        } else if (k_mine && hit != BRC_BLK_AIR && hit != BRC_BLK_LAVA && hit != BRC_BLK_WATER &&
                   hit != BRC_BLK_OBSIDIAN && hit != BRC_BLK_PORTAL) {
            world_set_block(w, hx, hy, hz, BRC_BLK_AIR, 0, 0);
            res->blocks_broken++;
        }
    }
}

static int run_cpu_episode(br_teacher_t *teacher,
                           const brc_seed_entry_t *seed,
                           int max_ticks,
                           float temperature,
                           uint64_t *rng_state,
                           FILE *rec_dump_fp,
                           br_ep_result_t *out_res) {
    memset(out_res, 0, sizeof(*out_res));
    struct world *w = (struct world *)calloc(1, sizeof(*w));
    if (!w) return -1;

    double t0 = now_ms();
    load_seed_into_world(w, seed, NULL, 0);

    br_obs_state_t st;
    memset(&st, 0, sizeof(st));
    st.px = (double)seed->origin_x + (double)seed->spawn_x;
    st.py = (double)seed->origin_y + (double)seed->spawn_y;
    st.pz = (double)seed->origin_z + (double)seed->spawn_z;
    st.yaw_deg   = seed->spawn_yaw;
    st.pitch_deg = seed->spawn_pitch;
    st.hp        = 20.0f;
    memcpy(st.hotbar_item, seed->hotbar_item, sizeof(st.hotbar_item));
    memcpy(st.hotbar_cnt,  seed->hotbar_cnt,  sizeof(st.hotbar_cnt));
    st.sel_slot  = seed->sel_slot < 9 ? seed->sel_slot : 0;
    st.lava_x    = (double)seed->origin_x + (double)seed->lava_x;
    st.lava_y    = (double)seed->origin_y + (double)seed->lava_y;
    st.lava_z    = (double)seed->origin_z + (double)seed->lava_z;
    st.max_ticks = max_ticks;

    struct entity e;
    entity_init(&e, w);
    entity_set_size(&e, 0.6f, 1.8f);
    e.step_height = 0.5f;
    entity_set_position(&e, st.px, st.py, st.pz);

    out_res->start_x = st.px;
    out_res->start_y = st.py;
    out_res->start_z = st.pz;
    out_res->min_lava_dist = 1e9;

    int jump_ticks = 0;
    int has_prev = 0;
    brc_s1_action_t prev_act;
    memset(&prev_act, 0, sizeof(prev_act));

    for (int tick = 0; tick < max_ticks; tick++) {
        st.ep_tick = tick;

        float obs[BRC_OBS_DIM];
        br_extract_obs_from_world(w, &st, obs);
        for (int i = 0; i < BRC_OBS_DIM; i++) {
            if (!isfinite(obs[i])) out_res->nan_events++;
        }

        brc_s1_action_t act;
        br_teacher_out_t probs;
        br_teacher_infer(teacher, obs, temperature, rng_state, &act, &probs);
        if (!isfinite(probs.value)) out_res->nan_events++;
        if (act.a[0] < 0 || act.a[0] >= BRC_CAM_BINS ||
            act.a[1] < 0 || act.a[1] >= BRC_CAM_BINS ||
            act.a[16] < 0 || act.a[16] >= BRC_HOTBAR_CATS) {
            out_res->nan_events++;
        }

        brc_distill_rec_t rec;
        br_emit_distill_rec(&rec, &act, has_prev ? &prev_act : NULL, 0, 1.0f, 1, 1, 0);
        out_res->distill_recs++;
        if (rec_dump_fp) {
            (void)fwrite(&rec, sizeof(rec), 1, rec_dump_fp);
        }

        step_cpu_world_tick(w, &e, &st, &jump_ticks, &act, out_res);
        if (!isfinite(st.px) || !isfinite(st.py) || !isfinite(st.pz) ||
            !isfinite(st.vx) || !isfinite(st.vy) || !isfinite(st.vz)) {
            out_res->nan_events++;
        }

        double dlx = st.lava_x - st.px;
        double dlz = st.lava_z - st.pz;
        double ld = sqrt(dlx * dlx + dlz * dlz);
        if (ld < out_res->min_lava_dist) out_res->min_lava_dist = ld;

        prev_act = act;
        has_prev = 1;
        out_res->ticks_run = tick + 1;
        if (st.portal_lit) break;
    }

    out_res->end_x = st.px;
    out_res->end_y = st.py;
    out_res->end_z = st.pz;
    out_res->elapsed_ms = now_ms() - t0;

    world_free(w);
    free(w);
    return 0;
}

/* ============================================================================
 * Authentic Test 3: Zero-shot 120-tick CPU libnetherite.a Replay Loop
 * ============================================================================ */
static int test_cpu_replay_loop(void) {
    printf("[Test 3] Zero-shot CPU libnetherite.a 120-tick teacher replay...\n");

    brc_seed_entry_t *seed = (brc_seed_entry_t *)calloc(1, sizeof(*seed));
    br_teacher_t *teacher = make_synthetic_teacher(1024, 0x3141592653589793ULL);
    if (!seed || !teacher) {
        fprintf(stderr, "  FAIL: OOM\n");
        free(seed);
        br_teacher_free(teacher);
        return 1;
    }

    build_synthetic_portal_seed(seed, 0, 64, 0);
    uint64_t rng = 0xdeadbeef12345678ULL;
    br_ep_result_t res;
    int rc = run_cpu_episode(teacher, seed, 120, 0.5f, &rng, NULL, &res);

    free(seed);
    br_teacher_free(teacher);

    printf("  ticks=%d start=(%.2f,%.2f,%.2f) end=(%.2f,%.2f,%.2f) dist=%.2fb min_lava=%.2fb\n",
           res.ticks_run,
           res.start_x, res.start_y, res.start_z,
           res.end_x, res.end_y, res.end_z,
           res.dist_traveled, res.min_lava_dist);
    printf("  hotbar_sw=%d placed=%d broken=%d obsidian=%d portal=%d recs=%d nans=%d time=%.2fms (%.2f ms/tick)\n",
           res.hotbar_switches, res.blocks_placed, res.blocks_broken,
           res.obsidian_placed, res.portal_lit, res.distill_recs,
           res.nan_events, res.elapsed_ms,
           res.ticks_run > 0 ? res.elapsed_ms / (double)res.ticks_run : 0.0);

    if (rc != 0 || res.ticks_run != 120 || res.nan_events != 0 || res.distill_recs != 120 || res.dist_traveled <= 0.0) {
        fprintf(stderr, "  FAIL: rc=%d ticks=%d nans=%d recs=%d dist=%.3f\n",
                rc, res.ticks_run, res.nan_events, res.distill_recs, res.dist_traveled);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ============================================================================
 * CLI Entry Point
 * ============================================================================ */
static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s [--self-test]\n"
            "  %s --teacher FILE.bin --seeds portal_seeds.bin [--episodes 16] [--ticks 400] [--temp 0.0] [--seed 42] [--dump-rec FILE.bin]\n",
            prog, prog);
}

int main(int argc, char **argv) {
    const char *teacher_path = NULL;
    const char *seeds_path = NULL;
    const char *dump_rec_path = NULL;
    int run_self_test = 0;
    int episodes = 16;
    int max_ticks = 400;
    float temperature = 0.0f;
    uint64_t seed = 42ULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--self-test") == 0) {
            run_self_test = 1;
        } else if (strcmp(argv[i], "--teacher") == 0 && i + 1 < argc) {
            teacher_path = argv[++i];
        } else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
            seeds_path = argv[++i];
        } else if (strcmp(argv[i], "--episodes") == 0 && i + 1 < argc) {
            episodes = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            max_ticks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--temp") == 0 && i + 1 < argc) {
            temperature = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--dump-rec") == 0 && i + 1 < argc) {
            dump_rec_path = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!teacher_path && !seeds_path) {
        run_self_test = 1;
    }

    int fails = 0;
    if (run_self_test) {
        fails += test_obs_parity();
        fails += test_teacher_and_distill_abi();
        fails += test_cpu_replay_loop();
        if (fails > 0) {
            fprintf(stderr, "SELF-TEST FAILED (%d failure(s))\n", fails);
            return 1;
        }
        printf("ALL 3 AUTHENTIC SELF-TESTS PASSED.\n");
        if (!teacher_path && !seeds_path) return 0;
    }

    if (!teacher_path || !seeds_path) {
        fprintf(stderr, "Both --teacher and --seeds are required for zero-shot evaluation.\n");
        return 2;
    }
    if (episodes <= 0) episodes = 1;
    if (max_ticks <= 0) max_ticks = 400;

    char err[256] = {0};
    br_teacher_t *teacher = br_teacher_load(teacher_path, err, sizeof(err));
    if (!teacher) {
        fprintf(stderr, "Error: %s\n", err);
        return 1;
    }

    brc_seed_entry_t *seeds = NULL;
    uint32_t num_seeds = 0;
    if (br_load_seed_pack(seeds_path, &seeds, &num_seeds, err, sizeof(err)) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        br_teacher_free(teacher);
        return 1;
    }

    FILE *rec_fp = NULL;
    if (dump_rec_path) {
        rec_fp = fopen(dump_rec_path, "wb");
        if (!rec_fp) {
            fprintf(stderr, "Error: cannot open --dump-rec %s\n", dump_rec_path);
            free(seeds);
            br_teacher_free(teacher);
            return 1;
        }
    }

    printf("Zero-shot CPU libnetherite.a eval: teacher=%s (H=%u) seeds=%s (N=%u) episodes=%d ticks=%d temp=%.2f\n",
           teacher_path, teacher->hdr.hidden, seeds_path, num_seeds, episodes, max_ticks, temperature);

    int total_wins = 0, total_obs = 0, total_ticks = 0, total_nans = 0;
    double sum_min_lava = 0.0, sum_dist = 0.0, total_ms = 0.0;
    uint64_t rng = seed ? seed : 42ULL;

    for (int ep = 0; ep < episodes; ep++) {
        const brc_seed_entry_t *s = &seeds[(uint32_t)ep % num_seeds];
        br_ep_result_t res;
        if (run_cpu_episode(teacher, s, max_ticks, temperature, &rng, rec_fp, &res) != 0) {
            fprintf(stderr, "Episode %d failed to initialize\n", ep);
            fails++;
            continue;
        }
        total_wins   += res.portal_lit;
        total_obs    += res.obsidian_placed;
        total_ticks  += res.ticks_run;
        total_nans   += res.nan_events;
        sum_min_lava += res.min_lava_dist;
        sum_dist     += res.dist_traveled;
        total_ms     += res.elapsed_ms;

        printf("  ep=%02d seed=%.32s ticks=%3d dist=%.2fb min_lava=%.2fb obs=%d portal=%d sw=%d ms=%.1f\n",
               ep, s->source_name, res.ticks_run, res.dist_traveled, res.min_lava_dist,
               res.obsidian_placed, res.portal_lit, res.hotbar_switches, res.elapsed_ms);
    }

    if (rec_fp) fclose(rec_fp);
    free(seeds);
    br_teacher_free(teacher);

    double ticks_per_s = (total_ms > 0.0) ? ((double)total_ticks / (total_ms * 1e-3)) : 0.0;
    printf("SUMMARY: episodes=%d wins=%d (%.1f%%) mean_obsidian=%.2f mean_min_lava=%.2fb mean_dist=%.2fb total_ticks=%d nans=%d throughput=%.1f ticks/s\n",
           episodes, total_wins, 100.0 * (double)total_wins / (double)episodes,
           (double)total_obs / (double)episodes,
           sum_min_lava / (double)episodes,
           sum_dist / (double)episodes,
           total_ticks, total_nans, ticks_per_s);

    return (fails > 0 || total_nans > 0) ? 1 : 0;
}
