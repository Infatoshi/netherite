#define _POSIX_C_SOURCE 200809L
/* Lane B (br-portal-sim): Authentic Rung 3 CPU libnetherite.a vs GPU br_portal_sim.cu Parity Harness
 *
 * Tests:
 *   1. Step-up (0.5-block step_height) & Sneak edge-clamping + lava damage parity vs libnetherite.a
 *   2. Full scripted 4x5 speedrun bucket-cast + flint-and-steel portal ignition parity vs libnetherite.a
 *   3. Pure GPU throughput benchmark at N=16,384 worlds on GPU 1
 */
#include "br_portal_sim.h"
#include "engine/world.h"
#include "engine/entity.h"
#include "engine/blocks.h"
#include "engine/raytrace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failures = 0;

static void check_cond(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failures++;
    }
}

static void load_seed_into_cpu_world(struct world *w, const brc_seed_entry_t *seed) {
    world_init(w, 1);
    for (int cz = 0; cz < 4; cz++) {
        for (int cx = 0; cx < 4; cx++) {
            world_load_chunk(w, cx, cz);
        }
    }
    for (int y = 0; y < BRC_WY; y++) {
        for (int z = 0; z < BRC_WZ; z++) {
            for (int x = 0; x < BRC_WX; x++) {
                int idx = brc_blk_idx(x, y, z);
                world_set_block(w, x, y, z, seed->blocks[idx], seed->meta[idx], 0);
            }
        }
    }
}

static void set_seed_block(brc_seed_entry_t *s, int x, int y, int z, uint8_t id, uint8_t meta) {
    if ((unsigned)x >= BRC_WX || (unsigned)y >= BRC_WY || (unsigned)z >= BRC_WZ) return;
    int idx = brc_blk_idx(x, y, z);
    s->blocks[idx] = id;
    s->meta[idx]   = meta;
    if (brc_blk_is_solid(id)) s->solid_bits[idx >> 5] |= (1u << (idx & 31));
    else                      s->solid_bits[idx >> 5] &= ~(1u << (idx & 31));
}

static int cpu_is_water(int id) { return id == BRC_BLK_WATER || id == BRC_BLK_FLOW_WATER; }
static int cpu_is_lava(int id)  { return id == BRC_BLK_LAVA  || id == BRC_BLK_FLOW_LAVA; }
static int cpu_is_build(uint16_t item) {
    return item == BRC_ITEM_COBBLE || item == BRC_ITEM_DIRT || item == BRC_ITEM_STONE;
}
static int cpu_is_replaceable(int id) {
    return id == BRC_BLK_AIR || cpu_is_water(id) || cpu_is_lava(id) || id == BRC_BLK_FIRE;
}

/* Host-side exact 3D Amanatides-Woo DDA on real CPU struct world (matching raytrace face normals) */
static int cpu_dda_world(struct world *w, double ox, double oy, double oz,
                         float yaw_deg, float pitch_deg, double max_dist, int mode,
                         int *hx, int *hy, int *hz, int *nx, int *ny, int *nz,
                         int *out_id, int *out_meta) {
    const double rad = 0.017453292519943295;
    double yr = (double)yaw_deg * rad;
    double pr = (double)pitch_deg * rad;
    double cp = cos(pr);
    double rx = -sin(yr) * cp;
    double ry = -sin(pr);
    double rz =  cos(yr) * cp;

    int ix = (int)floor(ox), iy = (int)floor(oy), iz = (int)floor(oz);
    int stepx = (rx > 1e-8) ? 1 : ((rx < -1e-8) ? -1 : 0);
    int stepy = (ry > 1e-8) ? 1 : ((ry < -1e-8) ? -1 : 0);
    int stepz = (rz > 1e-8) ? 1 : ((rz < -1e-8) ? -1 : 0);

    double tdx = (stepx != 0) ? fabs(1.0 / rx) : 1e30;
    double tdy = (stepy != 0) ? fabs(1.0 / ry) : 1e30;
    double tdz = (stepz != 0) ? fabs(1.0 / rz) : 1e30;

    double tmax_x = (stepx > 0) ? ((double)(ix + 1) - ox) * tdx : ((stepx < 0) ? (ox - (double)ix) * tdx : 1e30);
    double tmax_y = (stepy > 0) ? ((double)(iy + 1) - oy) * tdy : ((stepy < 0) ? (oy - (double)iy) * tdy : 1e30);
    double tmax_z = (stepz > 0) ? ((double)(iz + 1) - oz) * tdz : ((stepz < 0) ? (oz - (double)iz) * tdz : 1e30);

    int fnx = 0, fny = 0, fnz = 0;
    for (int s = 0; s < 32; s++) {
        double t;
        if (tmax_x <= tmax_y && tmax_x <= tmax_z) {
            t = tmax_x; ix += stepx; tmax_x += tdx; fnx = -stepx; fny = 0; fnz = 0;
        } else if (tmax_y <= tmax_z) {
            t = tmax_y; iy += stepy; tmax_y += tdy; fnx = 0; fny = -stepy; fnz = 0;
        } else {
            t = tmax_z; iz += stepz; tmax_z += tdz; fnx = 0; fny = 0; fnz = -stepz;
        }
        if (t > max_dist) break;
        int id = world_get_block(w, ix, iy, iz) & 4095;
        int mt = world_get_meta(w, ix, iy, iz);
        int hit = 0;
        if (mode == 1) hit = brc_blk_is_solid((uint8_t)id);
        else           hit = brc_blk_is_solid((uint8_t)id) || ((cpu_is_water(id) || cpu_is_lava(id)) && mt == 0);
        if (hit) {
            if (hx) *hx = ix;
            if (hy) *hy = iy;
            if (hz) *hz = iz;
            if (nx) *nx = fnx;
            if (ny) *ny = fny;
            if (nz) *nz = fnz;
            if (out_id) *out_id = id;
            if (out_meta) *out_meta = mt;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    int in_portal_cb_count;
} cpu_portal_cb_ctx_t;

static void on_cpu_enter_portal(void *self) {
    cpu_portal_cb_ctx_t *ctx = (cpu_portal_cb_ctx_t *)self;
    ctx->in_portal_cb_count++;
}

/* Authentic CPU 1.7.10 tick calling real libnetherite.a entity_move_ex + world_set_block (flags=3 callbacks!) */
static void cpu_step_portal_tick(struct world *w, struct entity *e, br_portal_state_t *ps,
                                 const brc_s1_action_t *act) {
    /* 1) Camera */
    ps->pitch += brc_cam_deg(act->a[0]);
    if (ps->pitch > 90.0f) ps->pitch = 90.0f;
    if (ps->pitch < -90.0f) ps->pitch = -90.0f;
    ps->yaw += brc_cam_deg(act->a[1]);
    if (ps->yaw >= 180.0f) ps->yaw -= 360.0f;
    else if (ps->yaw < -180.0f) ps->yaw += 360.0f;

    /* 2) Hotbar */
    int hb = act->a[16];
    if (hb >= 1 && hb <= 9) ps->sel_slot = (uint8_t)(hb - 1);

    int k_fwd    = act->a[2 + BRC_KEY_FORWARD] != 0;
    int k_back   = act->a[2 + BRC_KEY_BACK]    != 0;
    int k_left   = act->a[2 + BRC_KEY_LEFT]    != 0;
    int k_right  = act->a[2 + BRC_KEY_RIGHT]   != 0;
    int k_sprint = act->a[2 + BRC_KEY_SPRINT]  != 0;
    int k_jump   = act->a[2 + BRC_KEY_JUMP]    != 0;
    int k_sneak  = act->a[2 + BRC_KEY_SNEAK]   != 0;
    int k_mine   = act->a[2 + BRC_KEY_MINE]    != 0;
    int k_use    = act->a[2 + BRC_KEY_USE]     != 0;

    ps->sneaking = (uint8_t)k_sneak;
    e->is_sneaking = (uint8_t)k_sneak;
    int eff_sprint = (k_sprint && !k_sneak && k_fwd) ? 1 : 0;

    if (fabs(e->motion_x) < 0.005) e->motion_x = 0.0;
    if (fabs(e->motion_y) < 0.005) e->motion_y = 0.0;
    if (fabs(e->motion_z) < 0.005) e->motion_z = 0.0;

    /* Check lava/water */
    int in_lava = 0, in_water = 0;
    {
        int x0 = (int)floor(e->bounding_box.min_x + 0.1), x1 = (int)floor(e->bounding_box.max_x - 0.1);
        int y0 = (int)floor(e->bounding_box.min_y + 0.01), y1 = (int)floor(e->bounding_box.max_y - 0.4);
        int z0 = (int)floor(e->bounding_box.min_z + 0.1), z1 = (int)floor(e->bounding_box.max_z - 0.1);
        for (int x = x0; x <= x1; x++)
            for (int y = y0; y <= y1; y++)
                for (int z = z0; z <= z1; z++) {
                    int bid = world_get_block(w, x, y, z) & 4095;
                    if (cpu_is_lava(bid))  in_lava = 1;
                    if (cpu_is_water(bid)) in_water = 1;
                }
    }

    if (ps->jump_ticks > 0) ps->jump_ticks--;
    if (k_jump) {
        if (in_lava || in_water) {
            e->motion_y += 0.03999999910593033;
        } else if (e->on_ground && ps->jump_ticks == 0) {
            e->motion_y = 0.41999998688697815;
            if (eff_sprint) {
                float yr = ps->yaw * 0.017453292519943295f;
                e->motion_x -= (double)(sinf(yr) * 0.2f);
                e->motion_z += (double)(cosf(yr) * 0.2f);
            }
            ps->jump_ticks = 10;
        }
    } else {
        ps->jump_ticks = 0;
    }

    float strafe = (float)(k_left - k_right);
    float fwd    = (float)(k_fwd - k_back);
    if (k_sneak) { strafe *= 0.3f; fwd *= 0.3f; }
    strafe *= 0.98f;
    fwd    *= 0.98f;

    float v3 = 0.91f;
    if (e->on_ground) {
        int bx = (int)floor(e->pos_x);
        int by = (int)floor(e->bounding_box.min_y) - 1;
        int bz = (int)floor(e->pos_z);
        v3 = BLOCKS[world_get_block(w, bx, by, bz) & 4095].slipperiness * 0.91f;
    }
    float v5;
    if (in_lava || in_water) {
        v5 = 0.02f;
    } else {
        float v4 = 0.16277136f / (v3 * v3 * v3);
        float base_spd = eff_sprint ? 0.13000001f : 0.1f;
        v5 = e->on_ground ? (base_spd * v4) : (eff_sprint ? 0.026f : 0.02f);
    }

    float dist2 = strafe * strafe + fwd * fwd;
    if (dist2 >= 1.0e-4f) {
        float d = sqrtf(dist2);
        if (d < 1.0f) d = 1.0f;
        float sc = v5 / d;
        float s = strafe * sc, f = fwd * sc;
        float yr = ps->yaw * 0.017453292519943295f;
        float sy = sinf(yr), cy = cosf(yr);
        e->motion_x += (double)(s * cy - f * sy);
        e->motion_z += (double)(f * cy + s * sy);
    }

    /* Call real libnetherite.a entity_move_ex with sneak_player flag & step_height=0.5f! */
    entity_move_ex(e, e->motion_x, e->motion_y, e->motion_z, k_sneak, NULL);

    if (in_lava) {
        e->motion_x *= 0.5; e->motion_y *= 0.5; e->motion_z *= 0.5;
        e->motion_y -= 0.02;
    } else if (in_water) {
        e->motion_x *= 0.8; e->motion_y *= 0.8; e->motion_z *= 0.8;
        e->motion_y -= 0.02;
    } else {
        e->motion_y -= 0.08;
        e->motion_y *= 0.9800000190734863;
        e->motion_x *= (double)v3;
        e->motion_z *= (double)v3;
    }

    /* MINE */
    if (ps->mine_cd > 0) ps->mine_cd--;
    if (!k_mine) {
        ps->mine_cd = 0;
    } else if (ps->mine_cd == 0) {
        int hx = 0, hy = 0, hz = 0;
        if (cpu_dda_world(w, e->pos_x, e->pos_y + 1.62, e->pos_z, ps->yaw, ps->pitch, 4.5, 1,
                          &hx, &hy, &hz, NULL, NULL, NULL, NULL, NULL)) {
            if (hx > 0 && hx < BRC_WX - 1 && hy > 0 && hy < BRC_WY - 1 && hz > 0 && hz < BRC_WZ - 1) {
                world_set_block(w, hx, hy, hz, 0, 0, 3);
                ps->mine_cd = 4;
            }
        }
    }

    /* USE: delegates liquid quench & portal frame ignition directly to real libnetherite.a world_set_block(..., 3)! */
    if (ps->use_cd > 0) ps->use_cd--;
    if (!k_use) {
        ps->use_cd = 0;
    } else if (ps->use_cd == 0) {
        uint16_t item = ps->hotbar_item[ps->sel_slot];
        uint8_t  cnt  = ps->hotbar_cnt[ps->sel_slot];
        if (item == BRC_ITEM_BUCKET_EMPTY && cnt > 0) {
            int hx = 0, hy = 0, hz = 0, hid = 0, hm = 0;
            if (cpu_dda_world(w, e->pos_x, e->pos_y + 1.62, e->pos_z, ps->yaw, ps->pitch, 4.5, 2,
                              &hx, &hy, &hz, NULL, NULL, NULL, &hid, &hm)) {
                if (cpu_is_water(hid) && hm == 0) {
                    world_set_block(w, hx, hy, hz, 0, 0, 3);
                    ps->hotbar_item[ps->sel_slot] = BRC_ITEM_BUCKET_WATER;
                    ps->hotbar_cnt[ps->sel_slot]  = 1;
                    ps->use_cd = 4;
                } else if (cpu_is_lava(hid) && hm == 0) {
                    world_set_block(w, hx, hy, hz, 0, 0, 3);
                    ps->hotbar_item[ps->sel_slot] = BRC_ITEM_BUCKET_LAVA;
                    ps->hotbar_cnt[ps->sel_slot]  = 1;
                    ps->use_cd = 4;
                }
            }
        } else if (item != BRC_ITEM_NONE && cnt > 0) {
            int hx = 0, hy = 0, hz = 0, nx = 0, ny = 0, nz = 0;
            if (cpu_dda_world(w, e->pos_x, e->pos_y + 1.62, e->pos_z, ps->yaw, ps->pitch, 4.5, 1,
                              &hx, &hy, &hz, &nx, &ny, &nz, NULL, NULL)) {
                int tx = hx + nx, ty = hy + ny, tz = hz + nz;
                int tid = world_get_block(w, tx, ty, tz) & 4095;
                if (cpu_is_build(item)) {
                    int overlap = (e->pos_x + 0.3 > (double)tx && e->pos_x - 0.3 < (double)(tx + 1) &&
                                   e->pos_y + 1.8 > (double)ty && e->pos_y       < (double)(ty + 1) &&
                                   e->pos_z + 0.3 > (double)tz && e->pos_z - 0.3 < (double)(tz + 1));
                    if (cpu_is_replaceable(tid) && !overlap) {
                        world_set_block(w, tx, ty, tz, item, 0, 3);
                        ps->hotbar_cnt[ps->sel_slot] = (uint8_t)(cnt - 1);
                        if (ps->hotbar_cnt[ps->sel_slot] == 0) ps->hotbar_item[ps->sel_slot] = BRC_ITEM_NONE;
                        ps->use_cd = 4;
                    }
                } else if (item == BRC_ITEM_BUCKET_WATER) {
                    if (cpu_is_replaceable(tid)) {
                        /* Real libnetherite.a blockcb.c lava_water_check runs inside world_set_block! */
                        world_set_block(w, tx, ty, tz, BRC_BLK_WATER, 0, 3);
                        ps->hotbar_item[ps->sel_slot] = BRC_ITEM_BUCKET_EMPTY;
                        ps->hotbar_cnt[ps->sel_slot]  = 1;
                        ps->use_cd = 4;
                    }
                } else if (item == BRC_ITEM_BUCKET_LAVA) {
                    if (cpu_is_replaceable(tid)) {
                        /* Real libnetherite.a blockcb.c lava_water_check converts to ID_OBSIDIAN (49) if touching water! */
                        world_set_block(w, tx, ty, tz, BRC_BLK_LAVA, 0, 3);
                        ps->hotbar_item[ps->sel_slot] = BRC_ITEM_BUCKET_EMPTY;
                        ps->hotbar_cnt[ps->sel_slot]  = 1;
                        ps->use_cd = 4;
                    }
                } else if (item == BRC_ITEM_FLINT_STEEL) {
                    if (tid == BRC_BLK_AIR) {
                        /* Real libnetherite.a BlockFire.onBlockAdded -> portal_try_create fills 2x3 ID_PORTAL (90)! */
                        world_set_block(w, tx, ty, tz, BRC_BLK_FIRE, 0, 3);
                        ps->use_cd = 4;
                    }
                }
            }
        }
    }
}

/* ============================================================================
 * TEST 1: Step-Up (0.5 step_height) + Sneak Edge-Clamping + Lava Damage Parity
 * ============================================================================ */
static void run_test1_stepup_and_sneak(void) {
    printf("=== TEST 1: Step-Up (0.5 step_height) & Sneak Edge-Clamping Parity ===\n");
    brc_seed_entry_t seed;
    memset(&seed, 0, sizeof(seed));

    /* Build terrain with:
     * - Solid perimeter
     * - Platform y=0..7 on x=10..20, z=10..20
     * - 1-block ledge (y=8) at x=10..15, z=18..20 (for step-up testing)
     * - 2-block ledge (y=9) at x=10..15, z=20
     * - Air drop-off at x=16..20, z=21..26 (y=0..7 air) for sneak-over-air test
     * - Lava pool at x=21..26, y=7, z=14..20 (stone at y=0..6) for sneak-over-lava & lava damage test
     */
    for (int z = 0; z < BRC_WZ; z++) {
        for (int x = 0; x < BRC_WX; x++) {
            if (x == 0 || x == BRC_WX - 1 || z == 0 || z == BRC_WZ - 1) {
                for (int y = 0; y < BRC_WY; y++) set_seed_block(&seed, x, y, z, BRC_BLK_STONE, 0);
                continue;
            }
            if (x >= 16 && x <= 20 && z >= 21 && z <= 26) {
                /* Deep air drop-off (only floor at y=0) */
                set_seed_block(&seed, x, 0, z, BRC_BLK_STONE, 0);
                continue;
            }
            for (int y = 0; y < 8; y++) {
                set_seed_block(&seed, x, y, z, (y == 7) ? BRC_BLK_GRASS : BRC_BLK_STONE, 0);
            }
            if (x >= 21 && x <= 26 && z >= 14 && z <= 20) {
                set_seed_block(&seed, x, 7, z, BRC_BLK_LAVA, 0);
            }
            if (x >= 10 && x <= 15 && z >= 18 && z <= 42) {
                set_seed_block(&seed, x, 8, z, BRC_BLK_STONE, 0);
            }
            if (x >= 10 && x <= 15 && z >= 21 && z <= 42) {
                set_seed_block(&seed, x, 9, z, BRC_BLK_STONE, 0);
            }
        }
    }
    seed.spawn_x = 12.5f; seed.spawn_y = 8.0f; seed.spawn_z = 16.5f;
    seed.lava_x = 23.0f; seed.lava_y = 7.5f; seed.lava_z = 17.0f;

    const int N = 16;
    const int TICKS = 80;
    br_portal_state_t *init_st = (br_portal_state_t *)calloc((size_t)N, sizeof(br_portal_state_t));
    br_portal_state_t *gpu_st  = (br_portal_state_t *)calloc((size_t)N, sizeof(br_portal_state_t));
    br_portal_state_t *cpu_st  = (br_portal_state_t *)calloc((size_t)N, sizeof(br_portal_state_t));
    brc_s1_action_t   *acts    = (brc_s1_action_t *)calloc((size_t)TICKS * (size_t)N, sizeof(brc_s1_action_t));

    for (int t = 0; t < TICKS; t++) {
        for (int i = 0; i < N; i++) {
            acts[(size_t)t * N + i].a[0] = 20;
            acts[(size_t)t * N + i].a[1] = 20;
        }
    }

    for (int i = 0; i < N; i++) {
        init_st[i].hp = 20.0f;
        init_st[i].on_ground = 1;
        if (i < 6) {
            /* Group A: 0.5-block step-up over 1-block ledges (x=12.5, walking +Z onto y=9, y=10) */
            init_st[i].x = 11.5 + 0.5 * (double)i;
            init_st[i].z = 16.2;
            init_st[i].yaw = 0.0f; /* +Z */
            if (i < 2) {
                /* Direct mid-descent 0.5-block step-up: y=8.52, vy=-0.55 against z=18 ledge
                 * Sweep 1 hits ground y=8.0 (var37=1) & wall z=18; 0.5 step_height raises y=8.52->9.02 -> climbs! */
                init_st[i].y = 8.52;
                init_st[i].z = 17.68;
                init_st[i].vy = -0.55;
                init_st[i].vz = 0.20;
                init_st[i].on_ground = 0;
                for (int t = 0; t < TICKS; t++) {
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_FORWARD] = 1;
                }
            } else if (i < 4) {
                /* Jump-walk over two consecutive 1-block ledges (y=8 -> y=9 -> y=10) */
                init_st[i].y = 8.0;
                for (int t = 0; t < TICKS; t++) {
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_FORWARD] = 1;
                    if (t == 5 || t == 20) acts[(size_t)t * N + i].a[2 + BRC_KEY_JUMP] = 1;
                }
            } else {
                /* Walk straight into 1-block wall from flat y=8.0 without jumping (must remain blocked!) */
                init_st[i].y = 8.0;
                for (int t = 0; t < TICKS; t++) {
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_FORWARD] = 1;
                }
            }
        } else if (i < 14) {
            /* Group B: Sneak edge-clamping over air drop-off (+Z), lava pool (+X), and corner (+X/+Z) */
            init_st[i].y = 8.0;
            if (i < 9) {
                /* Sneak toward air drop-off at z=21 */
                init_st[i].x = 17.0 + 0.8 * (double)(i - 6);
                init_st[i].z = 19.8;
                init_st[i].yaw = 0.0f; /* +Z */
                for (int t = 0; t < TICKS; t++) {
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_SNEAK] = 1;
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_FORWARD] = 1;
                }
            } else if (i < 12) {
                /* Sneak toward lava pool at x=21 */
                init_st[i].x = 19.8;
                init_st[i].z = 15.5 + 1.0 * (double)(i - 9);
                init_st[i].yaw = -90.0f; /* +X */
                for (int t = 0; t < TICKS; t++) {
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_SNEAK] = 1;
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_FORWARD] = 1;
                }
            } else {
                /* Diagonal sneak into corner of air + lava */
                init_st[i].x = 19.8;
                init_st[i].z = 19.8;
                init_st[i].yaw = -45.0f; /* +X, +Z */
                for (int t = 0; t < TICKS; t++) {
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_SNEAK] = 1;
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_FORWARD] = 1;
                    acts[(size_t)t * N + i].a[2 + BRC_KEY_LEFT] = (i == 13) ? 1 : 0;
                }
            }
        } else {
            /* Group C: Walk into lava without sneaking (lava damage + slowdown + death) */
            init_st[i].x = 20.5;
            init_st[i].y = 8.0;
            init_st[i].z = 16.5 + (double)(i - 14);
            init_st[i].yaw = -90.0f; /* +X into lava pool */
            for (int t = 0; t < TICKS; t++) {
                acts[(size_t)t * N + i].a[2 + BRC_KEY_FORWARD] = 1;
            }
        }
    }

    /* Run GPU */
    br_portal_sim_t *sim = br_portal_sim_create(N, &seed, 1, 400);
    br_portal_sim_upload_states(sim, init_st, N);
    br_portal_sim_replay(sim, acts, TICKS, 0);
    br_portal_sim_download_states(sim, gpu_st, N);
    br_portal_sim_destroy(sim);

    /* Run real CPU libnetherite.a */
    struct world *w = (struct world *)calloc(1, sizeof(struct world));
    double max_pos_err_step = 0.0, max_pos_err_sneak = 0.0, max_vel_err = 0.0;
    int gnd_match = 0, stepup_climbed = 0, sneak_safe = 0, lava_dead_gpu = 0, lava_fire_cpu = 0;

    for (int i = 0; i < N; i++) {
        load_seed_into_cpu_world(w, &seed);
        struct entity e;
        entity_init(&e, w);
        entity_set_size(&e, 0.6f, 1.8f);
        e.step_height = 0.5f;
        entity_set_position(&e, init_st[i].x, init_st[i].y, init_st[i].z);
        e.motion_x = init_st[i].vx;
        e.motion_y = init_st[i].vy;
        e.motion_z = init_st[i].vz;
        e.on_ground = init_st[i].on_ground;

        br_portal_state_t ps = init_st[i];
        for (int t = 0; t < TICKS; t++) {
            cpu_step_portal_tick(w, &e, &ps, &acts[(size_t)t * N + i]);
        }
        ps.x = e.pos_x; ps.y = e.pos_y; ps.z = e.pos_z;
        ps.vx = e.motion_x; ps.vy = e.motion_y; ps.vz = e.motion_z;
        ps.on_ground = e.on_ground;
        cpu_st[i] = ps;

        double dx = gpu_st[i].x - cpu_st[i].x;
        double dy = gpu_st[i].y - cpu_st[i].y;
        double dz = gpu_st[i].z - cpu_st[i].z;
        double pe = sqrt(dx*dx + dy*dy + dz*dz);
        double dvx = gpu_st[i].vx - cpu_st[i].vx;
        double dvy = gpu_st[i].vy - cpu_st[i].vy;
        double dvz = gpu_st[i].vz - cpu_st[i].vz;
        double ve = sqrt(dvx*dvx + dvy*dvy + dvz*dvz);

        if (i < 6) {
            if (pe > max_pos_err_step) max_pos_err_step = pe;
            if (ve > max_vel_err) max_vel_err = ve;
            if (gpu_st[i].on_ground == cpu_st[i].on_ground) gnd_match++;
            if (i < 4 && gpu_st[i].y >= 8.999 && cpu_st[i].y >= 8.999) stepup_climbed++;
            if (i >= 4) check_cond(gpu_st[i].y < 8.01 && cpu_st[i].y < 8.01 && gpu_st[i].z < 17.75,
                                   "Flat walk into 1-block wall stays blocked without jump");
        } else if (i < 14) {
            if (pe > max_pos_err_sneak) max_pos_err_sneak = pe;
            if (ve > max_vel_err) max_vel_err = ve;
            if (gpu_st[i].on_ground == cpu_st[i].on_ground) gnd_match++;
            if (fabs(gpu_st[i].y - 8.0) < 1e-4 && fabs(cpu_st[i].y - 8.0) < 1e-4 &&
                gpu_st[i].on_ground == 1 && gpu_st[i].hp == 20.0f) {
                sneak_safe++;
            }
        } else {
            if (gpu_st[i].dead == 1 && gpu_st[i].hp == 0.0f) lava_dead_gpu++;
            if (e.fire > 0) lava_fire_cpu++;
        }
        world_free(w);
    }
    free(w);

    printf("  Step-up (6 worlds, 80 ticks): max_pos_err=%.6e, climbed_ledges=%d/4\n",
           max_pos_err_step, stepup_climbed);
    printf("  Sneak edge-clamp (8 worlds, 80 ticks): max_pos_err=%.6e, edge_clamped_safe=%d/8\n",
           max_pos_err_sneak, sneak_safe);
    printf("  Ground match (14 non-lava worlds): %d/14 (%.1f%%), max_vel_err=%.6e\n",
           gnd_match, 100.0 * (double)gnd_match / 14.0, max_vel_err);
    printf("  Lava hazard (2 worlds): gpu_dead=%d/2 (hp=%.1f), cpu_fire_contact=%d/2\n",
           lava_dead_gpu, gpu_st[14].hp, lava_fire_cpu);

    check_cond(max_pos_err_step < 1e-4, "Step-up max_pos_err < 1e-4");
    check_cond(stepup_climbed == 4, "All 4 step-up worlds climbed 1-block ledges");
    check_cond(max_pos_err_sneak < 1e-4, "Sneak edge-clamp max_pos_err < 1e-4");
    check_cond(sneak_safe == 8, "All 8 sneak worlds stayed clamped at ledge/lava edge");
    check_cond(gnd_match == 14, "100% on_ground match across step-up & sneak");
    check_cond(lava_dead_gpu == 2 && lava_fire_cpu == 2, "Lava damage kills player on GPU & ignites on CPU");

    free(init_st); free(gpu_st); free(cpu_st); free(acts);
}

/* ============================================================================
 * Scripted Action-Tape Builder Helpers for Test 2 (using exact brc_cam_deg bins)
 * ============================================================================ */
typedef struct {
    brc_s1_action_t *buf;
    int len;
    int cap;
    float yaw;
    float pitch;
    int sel_slot;
} tape_builder_t;

static void tb_init(tape_builder_t *tb, float init_yaw, float init_pitch, int init_slot) {
    tb->cap = 1024;
    tb->buf = (brc_s1_action_t *)calloc((size_t)tb->cap, sizeof(brc_s1_action_t));
    tb->len = 0;
    tb->yaw = init_yaw;
    tb->pitch = init_pitch;
    tb->sel_slot = init_slot;
}

static int best_cam_bin(float delta_deg) {
    int best = 20;
    float best_err = fabsf(delta_deg - brc_cam_deg(20));
    for (int b = 0; b < BRC_CAM_BINS; b++) {
        float err = fabsf(delta_deg - brc_cam_deg(b));
        if (err < best_err) { best_err = err; best = b; }
    }
    return best;
}

static float wrap_deg(float a) {
    while (a >= 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

static void tb_push(tape_builder_t *tb, int pbin, int ybin, int key_idx, int hb_cat) {
    if (tb->len >= tb->cap) return;
    brc_s1_action_t *a = &tb->buf[tb->len++];
    memset(a, 0, sizeof(*a));
    a->a[0] = pbin;
    a->a[1] = ybin;
    if (key_idx >= 0) a->a[2 + key_idx] = 1;
    a->a[16] = hb_cat;

    tb->pitch += brc_cam_deg(pbin);
    if (tb->pitch > 90.0f) tb->pitch = 90.0f;
    if (tb->pitch < -90.0f) tb->pitch = -90.0f;
    tb->yaw = wrap_deg(tb->yaw + brc_cam_deg(ybin));
    if (hb_cat >= 1 && hb_cat <= 9) tb->sel_slot = hb_cat - 1;
}

/* Rotate camera via exact mu-law bins until aimed at world point (tx, ty, tz) from eye (ex, ey, ez) */
static void tb_aim_at(tape_builder_t *tb, double ex, double ey, double ez,
                      double tx, double ty, double tz) {
    double dx = tx - ex, dy = ty - ey, dz = tz - ez;
    double horiz = sqrt(dx * dx + dz * dz);
    float tgt_yaw   = (float)(atan2(-dx, dz) * (180.0 / 3.14159265358979323846));
    float tgt_pitch = (float)(atan2(-dy, horiz) * (180.0 / 3.14159265358979323846));

    for (int step = 0; step < 8; step++) {
        float dyaw   = wrap_deg(tgt_yaw - tb->yaw);
        float dpitch = tgt_pitch - tb->pitch;
        if (fabsf(dyaw) < 0.4f && fabsf(dpitch) < 0.4f) break;
        int yb = best_cam_bin(dyaw);
        int pb = best_cam_bin(dpitch);
        if (yb == 20 && pb == 20) break;
        tb_push(tb, pb, yb, -1, 0);
    }
}

/* Aim at exact face point, switch hotbar slot (0..8), pulse key (USE or MINE), then release 1 tick */
static void tb_aim_and_act(tape_builder_t *tb, double ex, double ey, double ez,
                           double tx, double ty, double tz, int slot, int key_idx) {
    tb_aim_at(tb, ex, ey, ez, tx, ty, tz);
    int hb_cat = (slot >= 0 && slot != tb->sel_slot) ? (slot + 1) : 0;
    tb_push(tb, 20, 20, key_idx, hb_cat);
    tb_push(tb, 20, 20, -1, 0); /* release key so cooldown clears cleanly */
}

/* Jump-pillar up 1 block by looking straight down, jumping, and placing cobble at peak */
static void tb_pillar_up(tape_builder_t *tb, int cobble_slot) {
    /* Look straight down (pitch = +90) */
    for (int s = 0; s < 6; s++) {
        float dp = 89.0f - tb->pitch;
        if (fabsf(dp) < 0.5f) break;
        int pb = best_cam_bin(dp);
        if (pb == 20) break;
        tb_push(tb, pb, 20, -1, 0);
    }
    int hb_cat = (cobble_slot != tb->sel_slot) ? (cobble_slot + 1) : 0;
    tb_push(tb, 20, 20, BRC_KEY_JUMP, hb_cat); /* tick 0: jump (vy=0.42) */
    for (int k = 0; k < 4; k++) tb_push(tb, 20, 20, -1, 0); /* ticks 1..4: rise to y+1.21 */
    tb_push(tb, 20, 20, BRC_KEY_USE, 0); /* tick 5: place cobble beneath feet (no AABB overlap!) */
    for (int k = 0; k < 7; k++) tb_push(tb, 20, 20, -1, 0); /* ticks 6..12: land cleanly on new cobble */
}

/* Mine 1 cobble block straight beneath feet and land cleanly 1 block lower */
static void tb_unpillar_down(tape_builder_t *tb, int pick_slot) {
    for (int s = 0; s < 6; s++) {
        float dp = 89.0f - tb->pitch;
        if (fabsf(dp) < 0.5f) break;
        int pb = best_cam_bin(dp);
        if (pb == 20) break;
        tb_push(tb, pb, 20, -1, 0);
    }
    int hb_cat = (pick_slot != tb->sel_slot) ? (pick_slot + 1) : 0;
    tb_push(tb, 20, 20, BRC_KEY_MINE, hb_cat);
    for (int k = 0; k < 10; k++) tb_push(tb, 20, 20, -1, 0); /* fall 1 block and settle */
}

/* ============================================================================
 * TEST 2: Scripted 4x5 Speedrun Bucket-Cast + Flint-and-Steel Portal Ignition
 * ============================================================================ */
static void run_test2_bucket_cast_portal(void) {
    printf("=== TEST 2: Scripted 4x5 Bucket-Cast + Portal Ignition Parity ===\n");
    brc_seed_entry_t seed;
    br_portal_make_test_seed(&seed, 0xBEEF1710ULL);
    /* Position player on dry grass edge at (21.5, 8.0, 21.4) right in front of the lava pool (z=22..27) */
    seed.spawn_x = 21.5f;
    seed.spawn_y = 8.0f;
    seed.spawn_z = 21.4f;
    seed.spawn_yaw = 0.0f;
    seed.spawn_pitch = 0.0f;

    const double ex = 21.5, ez = 21.4;
    double ey = 8.0 + 1.62;
    const int SLOT_COBBLE = 0;
    const int SLOT_BUCKET = 1;
    const int SLOT_FLINT  = 2;
    const int SLOT_PICK   = 3;

    tape_builder_t tb;
    tb_init(&tb, seed.spawn_yaw, seed.spawn_pitch, seed.sel_slot);

    /* Phase 1: Place bottom-left & bottom-right cobble pillar bases at (20,7,22) and (23,7,22)
     * Both rays pass cleanly through the liquid lava pool to hit the stone floor (20,6,22) and (23,6,22)! */
    tb_aim_and_act(&tb, ex, ey, ez, 20.5, 7.0, 22.5, SLOT_COBBLE, BRC_KEY_USE);
    tb_aim_and_act(&tb, ex, ey, ez, 23.5, 7.0, 22.5, SLOT_COBBLE, BRC_KEY_USE);

    /* Lava sources ordered with guaranteed unobstructed line-of-sight at every elevation:
     *   py=8: (20,7,23) & (23,7,23) before pillars exist
     *   py=9: (21,7,25) & (22,7,25) through open center corridor x=21..22
     *   py=10: (21,7,24) & (22,7,24) through open center corridor x=21..22
     *   py=11: (22,7,23) while right half is open, then mine (21,10,22), then (21,7,23) while left half is open! */
    const double lava_src[8][3] = {
        {20.5, 7.9, 23.3}, {23.5, 7.9, 23.3},   /* py=8 left/right -> (20,7,23), (23,7,23) */
        {21.5, 7.98, 25.3}, {22.5, 7.98, 25.3}, /* py=9 left/right -> (21,7,25), (22,7,25) */
        {21.5, 7.95, 24.3}, {22.5, 7.95, 24.3}, /* py=10 left/right -> (21,7,24), (22,7,24) */
        {22.5, 7.95, 23.2}, {21.5, 7.95, 23.2}  /* py=11 left (via right), right (via left) */
    };
    int lidx = 0;

    /* Phase 2: Cast y=8 and y=9 of Left (x=20) & Right (x=23) Pillars from y=8.0 (eye=9.62)
     * Water mold sits directly in front at (20, py, 21) / (23, py, 21) -> dead-center aiming, zero occlusion! */
    for (int py = 8; py <= 9; py++) {
        /* Left pillar (20, py, 22) using water at (20, py, 21) */
        tb_aim_and_act(&tb, ex, ey, ez, 20.5, (double)py, 21.5, SLOT_BUCKET, BRC_KEY_USE);
        tb_aim_and_act(&tb, ex, ey, ez, lava_src[lidx][0], lava_src[lidx][1], lava_src[lidx][2], SLOT_BUCKET, BRC_KEY_USE);
        lidx++;
        tb_aim_and_act(&tb, ex, ey, ez, 20.5, (double)py, 22.5, SLOT_BUCKET, BRC_KEY_USE);
        tb_aim_and_act(&tb, ex, ey, ez, 20.5, (double)py + 0.5, 21.5, SLOT_BUCKET, BRC_KEY_USE);
        tb_aim_and_act(&tb, ex, ey, ez, 20.5, (double)py, 21.5, SLOT_COBBLE, BRC_KEY_USE);

        /* Right pillar (23, py, 22) using water at (23, py, 21) */
        tb_aim_and_act(&tb, ex, ey, ez, 23.5, (double)py, 21.5, SLOT_BUCKET, BRC_KEY_USE);
        tb_aim_and_act(&tb, ex, ey, ez, lava_src[lidx][0], lava_src[lidx][1], lava_src[lidx][2], SLOT_BUCKET, BRC_KEY_USE);
        lidx++;
        tb_aim_and_act(&tb, ex, ey, ez, 23.5, (double)py, 22.5, SLOT_BUCKET, BRC_KEY_USE);
        tb_aim_and_act(&tb, ex, ey, ez, 23.5, (double)py + 0.5, 21.5, SLOT_BUCKET, BRC_KEY_USE);
        tb_aim_and_act(&tb, ex, ey, ez, 23.5, (double)py, 21.5, SLOT_COBBLE, BRC_KEY_USE);
    }

    /* Phase 3: Jump-pillar up 1 block onto (21, 8, 21) -> player at y=9.0, eye=10.62! */
    tb_pillar_up(&tb, SLOT_COBBLE);
    ey = 9.0 + 1.62;

    /* Cast y=10 of Left (20, 10, 22) & Right (23, 10, 22) Pillars using water at (20,10,21) / (23,10,21) */
    tb_aim_and_act(&tb, ex, ey, ez, 20.5, 10.0, 21.5, SLOT_BUCKET, BRC_KEY_USE);
    tb_aim_and_act(&tb, ex, ey, ez, lava_src[lidx][0], lava_src[lidx][1], lava_src[lidx][2], SLOT_BUCKET, BRC_KEY_USE);
    lidx++;
    tb_aim_and_act(&tb, ex, ey, ez, 20.5, 10.0, 22.5, SLOT_BUCKET, BRC_KEY_USE);
    tb_aim_and_act(&tb, ex, ey, ez, 20.5, 10.5, 21.5, SLOT_BUCKET, BRC_KEY_USE);

    tb_aim_and_act(&tb, ex, ey, ez, 23.5, 10.0, 21.5, SLOT_BUCKET, BRC_KEY_USE);
    tb_aim_and_act(&tb, ex, ey, ez, lava_src[lidx][0], lava_src[lidx][1], lava_src[lidx][2], SLOT_BUCKET, BRC_KEY_USE);
    lidx++;
    tb_aim_and_act(&tb, ex, ey, ez, 23.5, 10.0, 22.5, SLOT_BUCKET, BRC_KEY_USE);
    tb_aim_and_act(&tb, ex, ey, ez, 23.5, 10.5, 21.5, SLOT_BUCKET, BRC_KEY_USE);

    /* Phase 4: Jump-pillar up 1 more block onto (21, 9, 21) -> player at y=10.0, eye=11.62! */
    tb_pillar_up(&tb, SLOT_COBBLE);
    ey = 10.0 + 1.62;

    /* Cast Top 2 Obsidian (21, 11, 22) and (22, 11, 22):
     * Scoop lava BEFORE placing each temporary cobble scaffold so line-of-sight is 100% open! */
    /* Water at (20, 11, 22) via +Y face of (20, 10, 22) */
    tb_aim_and_act(&tb, ex, ey, ez, 20.5, 11.0, 22.5, SLOT_BUCKET, BRC_KEY_USE);
    /* Scoop lava 7 from (21, 7, 23) while interior is completely open */
    tb_aim_and_act(&tb, ex, ey, ez, 21.5, 7.95, 23.3, SLOT_BUCKET, BRC_KEY_USE);
    /* Temporary cobble at (21, 10, 22) via +X face of (20, 10, 22) */
    tb_aim_and_act(&tb, ex, ey, ez, 21.0, 10.5, 22.5, SLOT_COBBLE, BRC_KEY_USE);
    /* Pour lava at (21, 11, 22) via +Y face of cobble (21, 10, 22) -> OBSIDIAN! */
    tb_aim_and_act(&tb, ex, ey, ez, 21.5, 11.0, 22.5, SLOT_BUCKET, BRC_KEY_USE);
    /* Scoop water back from (20, 11, 22) */
    tb_aim_and_act(&tb, ex, ey, ez, 20.5, 11.5, 22.5, SLOT_BUCKET, BRC_KEY_USE);
    /* Mine temporary cobble (21, 10, 22) */
    tb_aim_and_act(&tb, ex, ey, ez, 21.5, 10.5, 22.0, SLOT_PICK, BRC_KEY_MINE);

    /* Water at (23, 11, 22) via +Y face of (23, 10, 22) */
    tb_aim_and_act(&tb, ex, ey, ez, 23.5, 11.0, 22.5, SLOT_BUCKET, BRC_KEY_USE);
    /* Scoop lava 8 from (22, 7, 23) while interior is completely open */
    tb_aim_and_act(&tb, ex, ey, ez, 22.5, 7.95, 23.3, SLOT_BUCKET, BRC_KEY_USE);
    /* Temporary cobble at (22, 10, 22) via -X face of (23, 10, 22) */
    tb_aim_and_act(&tb, ex, ey, ez, 23.0, 10.5, 22.5, SLOT_COBBLE, BRC_KEY_USE);
    /* Pour lava at (22, 11, 22) via +Y face of cobble (22, 10, 22) -> OBSIDIAN! */
    tb_aim_and_act(&tb, ex, ey, ez, 22.5, 11.0, 22.5, SLOT_BUCKET, BRC_KEY_USE);
    /* Scoop water back from (23, 11, 22) via its front -Z face (clears (22,11,22)!) */
    tb_aim_and_act(&tb, ex, ey, ez, 23.5, 11.5, 22.12, SLOT_BUCKET, BRC_KEY_USE);
    /* Mine temporary interior cobble scaffold (22, 10, 22) */
    tb_aim_and_act(&tb, ex, ey, ez, 22.5, 10.5, 22.0, SLOT_PICK, BRC_KEY_MINE);

    /* Descend back to ground y=8.0 (eye=9.62) by mining the 2 pillar cobble blocks beneath feet */
    tb_unpillar_down(&tb, SLOT_PICK);
    tb_unpillar_down(&tb, SLOT_PICK);
    ey = 8.0 + 1.62;

    /* Phase 5: Cast bottom 2 obsidian (21,7,22) & (22,7,22) by pouring water into (21,8,22) & (22,8,22) */
    tb_aim_and_act(&tb, ex, ey, ez, 21.0, 8.5, 22.5, SLOT_BUCKET, BRC_KEY_USE); /* +X of (20,8,22) -> water (21,8,22) */
    tb_aim_and_act(&tb, ex, ey, ez, 21.5, 8.5, 22.5, SLOT_BUCKET, BRC_KEY_USE); /* scoop water from (21,8,22) */
    tb_aim_and_act(&tb, ex, ey, ez, 23.0, 8.5, 22.5, SLOT_BUCKET, BRC_KEY_USE); /* -X of (23,8,22) -> water (22,8,22) */
    tb_aim_and_act(&tb, ex, ey, ez, 22.5, 8.5, 22.5, SLOT_BUCKET, BRC_KEY_USE); /* scoop water from (22,8,22) */

    /* Phase 6: Ignite 4x5 Nether Portal with Flint & Steel on +Y face of bottom obsidian (21, 7, 22)! */
    tb_aim_and_act(&tb, ex, ey, ez, 21.5, 8.0, 22.5, SLOT_FLINT, BRC_KEY_USE);

    /* Phase 7: Look straight +Z and walk directly into the lit portal! */
    tb_aim_at(&tb, ex, ey, ez, 21.5, ey, 24.5);
    for (int t = 0; t < 10; t++) {
        tb_push(&tb, 20, 20, BRC_KEY_FORWARD, 0);
    }

    int TICKS = tb.len;
    printf("  Scripted action tape length: %d ticks (brc_s1_action_t)\n", TICKS);

    /* Run on GPU br_portal_sim.cu */
    br_portal_sim_t *sim = br_portal_sim_create(1, &seed, 1, 1000);
    br_portal_sim_replay(sim, tb.buf, TICKS, 0);
    br_portal_state_t gpu_st;
    float gpu_obs[BRC_OBS_DIM];
    br_portal_sim_download_states(sim, &gpu_st, 1);
    br_portal_sim_download_obs(sim, gpu_obs, 1);
    br_portal_sim_destroy(sim);

    /* Run on real CPU libnetherite.a */
    struct world *w = (struct world *)calloc(1, sizeof(struct world));
    load_seed_into_cpu_world(w, &seed);
    struct entity e;
    entity_init(&e, w);
    entity_set_size(&e, 0.6f, 1.8f);
    e.step_height = 0.5f;
    cpu_portal_cb_ctx_t pctx = {0};
    e.self = &pctx;
    e.set_in_portal = on_cpu_enter_portal;
    entity_set_position(&e, seed.spawn_x, seed.spawn_y, seed.spawn_z);
    e.on_ground = 1;

    br_portal_state_t cpu_ps;
    memset(&cpu_ps, 0, sizeof(cpu_ps));
    cpu_ps.yaw = seed.spawn_yaw;
    cpu_ps.pitch = seed.spawn_pitch;
    cpu_ps.hp = 20.0f;
    cpu_ps.sel_slot = seed.sel_slot;
    for (int s = 0; s < 9; s++) {
        cpu_ps.hotbar_item[s] = seed.hotbar_item[s];
        cpu_ps.hotbar_cnt[s]  = seed.hotbar_cnt[s];
    }

    for (int t = 0; t < TICKS; t++) {
        cpu_step_portal_tick(w, &e, &cpu_ps, &tb.buf[t]);
    }

    /* Verify all 10 obsidian frame blocks match GPU vs CPU libnetherite.a */
    const int obs_coords[10][3] = {
        {21, 7, 22}, {22, 7, 22},               /* bottom 2 */
        {20, 8, 22}, {20, 9, 22}, {20, 10, 22}, /* left pillar 3 */
        {23, 8, 22}, {23, 9, 22}, {23, 10, 22}, /* right pillar 3 */
        {21, 11, 22}, {22, 11, 22}              /* top 2 */
    };
    int obs_gpu = 0, obs_cpu = 0, obs_match = 0;
    for (int k = 0; k < 10; k++) {
        int x = obs_coords[k][0], y = obs_coords[k][1], z = obs_coords[k][2];
        uint8_t gid = br_portal_query_block(&seed, &gpu_st, x, y, z, NULL);
        int cid = world_get_block(w, x, y, z) & 4095;
        if (gid == BRC_BLK_OBSIDIAN) obs_gpu++;
        if (cid == BRC_BLK_OBSIDIAN) obs_cpu++;
        if (gid == BRC_BLK_OBSIDIAN && cid == BRC_BLK_OBSIDIAN) obs_match++;
        else printf("    MISMATCH obsidian (%d,%d,%d): gpu=%d cpu=%d\n", x, y, z, (int)gid, cid);
    }

    /* Verify all 6 interior BRC_BLK_PORTAL (90) blocks match GPU vs CPU libnetherite.a */
    const int portal_coords[6][3] = {
        {21, 8, 22}, {22, 8, 22},
        {21, 9, 22}, {22, 9, 22},
        {21, 10, 22}, {22, 10, 22}
    };
    int ptl_gpu = 0, ptl_cpu = 0, ptl_match = 0;
    for (int k = 0; k < 6; k++) {
        int x = portal_coords[k][0], y = portal_coords[k][1], z = portal_coords[k][2];
        uint8_t gm = 0;
        uint8_t gid = br_portal_query_block(&seed, &gpu_st, x, y, z, &gm);
        int cid = world_get_block(w, x, y, z) & 4095;
        int cm  = world_get_meta(w, x, y, z);
        if (gid == BRC_BLK_PORTAL) ptl_gpu++;
        if (cid == BRC_BLK_PORTAL) ptl_cpu++;
        if (gid == BRC_BLK_PORTAL && cid == BRC_BLK_PORTAL && (int)gm == cm) ptl_match++;
        else printf("    MISMATCH portal (%d,%d,%d): gpu=(%d,m=%d) cpu=(%d,m=%d)\n",
                    x, y, z, (int)gid, (int)gm, cid, cm);
    }

    /* Verify obs[0..127] finite, bounded, zero NaNs, zero pad */
    int obs_nan = 0, obs_oob = 0, pad_nonzero = 0;
    for (int k = 0; k < 104; k++) {
        if (!isfinite(gpu_obs[k])) obs_nan++;
        if (gpu_obs[k] < -1.0001f || gpu_obs[k] > 1.0001f) obs_oob++;
    }
    for (int k = 104; k < BRC_OBS_DIM; k++) {
        if (gpu_obs[k] != 0.0f) pad_nonzero++;
    }

    double final_pos_err = sqrt((gpu_st.x - e.pos_x) * (gpu_st.x - e.pos_x) +
                                (gpu_st.y - e.pos_y) * (gpu_st.y - e.pos_y) +
                                (gpu_st.z - e.pos_z) * (gpu_st.z - e.pos_z));

    printf("  10 Obsidian frame coords: gpu=%d/10, cpu=%d/10, exact_match=%d/10\n",
           obs_gpu, obs_cpu, obs_match);
    printf("  6 Portal (90) interior blocks: gpu=%d/6, cpu=%d/6, exact_id+meta_match=%d/6\n",
           ptl_gpu, ptl_cpu, ptl_match);
    printf("  Portal lit & entry: gpu_lit=%d gpu_in_portal=%d cpu_set_in_portal_calls=%d pos_err=%.6e delta_used=%d/%d\n",
           (int)gpu_st.portal_lit, (int)gpu_st.in_portal, pctx.in_portal_cb_count,
           final_pos_err, (int)gpu_st.ndelta, BRC_DELTA_CAP);
    printf("  obs[0..127]: nan=%d oob=%d pad_nz=%d obs[100](obs_frac)=%.2f obs[101](lit)=%.1f\n",
           obs_nan, obs_oob, pad_nonzero, gpu_obs[100], gpu_obs[101]);

    check_cond(obs_match == 10, "All 10 obsidian frame blocks match GPU vs CPU libnetherite.a");
    check_cond(ptl_match == 6,  "All 6 portal (90) interior blocks + meta match GPU vs CPU libnetherite.a");
    check_cond(gpu_st.portal_lit == 1 && gpu_st.portal_blocks == 6, "GPU portal_lit==1 and portal_blocks==6");
    check_cond(gpu_st.in_portal == 1 && pctx.in_portal_cb_count > 0, "Player walked into lit portal on both GPU & CPU");
    check_cond(final_pos_err < 1e-4, "Final player position matches GPU vs CPU < 1e-4");
    check_cond(obs_nan == 0 && obs_oob == 0 && pad_nonzero == 0, "obs[0..127] finite, bounded [-1,1], pad==0");
    check_cond(gpu_obs[100] >= 0.99f && gpu_obs[101] == 1.0f, "obs[100]==1.0 and obs[101]==1.0");

    world_free(w);
    free(w);
    free(tb.buf);
}

/* ============================================================================
 * TEST 3: Pure GPU Throughput Benchmark at N = 16,384 Worlds
 * ============================================================================ */
static void run_test3_throughput(void) {
    printf("=== TEST 3: GPU Throughput Benchmark (N=16,384 worlds, 64 CoW seeds) ===\n");
    const int N = 16384;
    const int NUM_SEEDS = 64;
    brc_seed_entry_t *seeds = (brc_seed_entry_t *)malloc((size_t)NUM_SEEDS * sizeof(brc_seed_entry_t));
    for (int s = 0; s < NUM_SEEDS; s++) {
        br_portal_make_test_seed(&seeds[s], 0x100000ULL + (uint64_t)s);
    }

    br_portal_sim_t *sim = br_portal_sim_create(N, seeds, NUM_SEEDS, 600);
    double pure_mticks = br_portal_sim_bench(sim, 4, 250, 0);
    double obs_mticks  = br_portal_sim_bench(sim, 4, 250, 1);

    /* Verify obs after 16,384-world RL rollout has zero NaNs */
    float *h_obs = (float *)malloc((size_t)N * BRC_OBS_DIM * sizeof(float));
    br_portal_sim_download_obs(sim, h_obs, N);
    int nans = 0;
    for (size_t k = 0; k < (size_t)N * BRC_OBS_DIM; k++) {
        if (!isfinite(h_obs[k])) nans++;
    }
    free(h_obs);
    br_portal_sim_destroy(sim);
    free(seeds);

    printf("  Pure sim (K=4, 250 iters, no obs): %.2f Mticks/s\n", pure_mticks);
    printf("  Sim + obs v2 (K=4, 250 iters, 4x16 3D DDA + 24 scalars): %.2f Mticks/s (obs NaNs=%d)\n",
           obs_mticks, nans);

    check_cond(pure_mticks > 10.0, "Pure sim throughput > 10 Mticks/s");
    check_cond(nans == 0, "Zero NaNs across 16,384 x 128 obs floats");
}

int main(void) {
    run_test1_stepup_and_sneak();
    run_test2_bucket_cast_portal();
    run_test3_throughput();

    if (g_failures == 0) {
        printf("\nALL RUNG 3 PORTAL PARITY & THROUGHPUT TESTS PASSED (0 failures).\n");
        return 0;
    } else {
        fprintf(stderr, "\nRUNG 3 FAILED with %d failure(s).\n", g_failures);
        return 1;
    }
}
