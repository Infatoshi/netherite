#define _POSIX_C_SOURCE 200809L
/* CPU 1.7.10 Ground-Truth Parity Bridge for the Bedrock Fidelity Flywheel.
 * Links directly against out/native/libnetherite.a (real struct world +
 * world_set_block + entity_move + exact living_move_with_heading order).
 */
#include "bedrock.h"
#include "engine/world.h"
#include "engine/entity.h"
#include "engine/blocks.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double cpu_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static uint64_t sm64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static int cmp_d(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

/* Populate a real 1.7.10 struct world from a Bedrock 64x32x64 seed */
static void load_seed_into_world(struct world *w, const uint8_t *blocks) {
    world_init(w, 1);
    for (int cz = 0; cz < 4; cz++) {
        for (int cx = 0; cx < 4; cx++) {
            world_load_chunk(w, cx, cz);
        }
    }
    for (int y = 0; y < BR_WY; y++) {
        for (int z = 0; z < BR_WZ; z++) {
            for (int x = 0; x < BR_WX; x++) {
                int id = blocks[br_blk_idx(x, y, z)];
                world_set_block(w, x, y, z, id, 0, 0);
            }
        }
    }
}

/* Host-side ray pick matching 4.5-block survival reach on real struct world */
static int cpu_pick_block(struct world *w, double ox, double oy, double oz,
                          float yaw_deg, float pitch_deg, int *hx, int *hy, int *hz) {
    const double rad = 0.017453292519943295;
    double yr = (double)yaw_deg * rad, pr = (double)pitch_deg * rad;
    double cp = cos(pr);
    double rx = -sin(yr) * cp;
    double ry = -sin(pr);
    double rz =  cos(yr) * cp;
    for (int s = 1; s <= 45; s++) {
        double t = (double)s * 0.1;
        int qx = (int)floor(ox + rx * t);
        int qy = (int)floor(oy + ry * t);
        int qz = (int)floor(oz + rz * t);
        int id = world_get_block(w, qx, qy, qz) & 4095;
        if (id != 0) { *hx = qx; *hy = qy; *hz = qz; return id; }
    }
    return 0;
}

/* Run exact CPU 1.7.10 tick using real entity_move + world_get_block slipperiness */
static void cpu_step_one(struct world *w, struct entity *e, br_player_state_t *ps,
                         uint8_t a, float yaw) {
    if (fabs(e->motion_x) < 0.005) e->motion_x = 0.0;
    if (fabs(e->motion_y) < 0.005) e->motion_y = 0.0;
    if (fabs(e->motion_z) < 0.005) e->motion_z = 0.0;

    if (ps->jump_ticks > 0) ps->jump_ticks--;
    if (a & BR_ACT_JUMP) {
        if (e->on_ground && ps->jump_ticks == 0) {
            e->motion_y = 0.41999998688697815;
            if (a & BR_ACT_SPRINT) {
                float yr = yaw * 0.017453292519943295f;
                e->motion_x -= (double)(sinf(yr) * 0.2f);
                e->motion_z += (double)(cosf(yr) * 0.2f);
            }
            ps->jump_ticks = 10;
        }
    } else {
        ps->jump_ticks = 0;
    }

    float strafe = (((a & BR_ACT_LEFT) ? 1.0f : 0.0f) - ((a & BR_ACT_RIGHT) ? 1.0f : 0.0f)) * 0.98f;
    float fwd    = (((a & BR_ACT_FORWARD) ? 1.0f : 0.0f) - ((a & BR_ACT_BACK) ? 1.0f : 0.0f)) * 0.98f;

    float v3 = 0.91f;
    if (e->on_ground) {
        int bx = (int)floor(e->pos_x);
        int by = (int)floor(e->bounding_box.min_y) - 1;
        int bz = (int)floor(e->pos_z);
        v3 = BLOCKS[world_get_block(w, bx, by, bz) & 4095].slipperiness * 0.91f;
    }
    float v4 = 0.16277136f / (v3 * v3 * v3);
    float base_spd = (a & BR_ACT_SPRINT) ? 0.13000001f : 0.1f;
    float v5 = e->on_ground ? (base_spd * v4) : ((a & BR_ACT_SPRINT) ? 0.026f : 0.02f);

    float dist2 = strafe * strafe + fwd * fwd;
    if (dist2 >= 1.0e-4f) {
        float d = sqrtf(dist2);
        if (d < 1.0f) d = 1.0f;
        float sc = v5 / d;
        float s = strafe * sc, f = fwd * sc;
        float yr = yaw * 0.017453292519943295f;
        float sy = sinf(yr), cy = cosf(yr);
        e->motion_x += (double)(s * cy - f * sy);
        e->motion_z += (double)(f * cy + s * sy);
    }

    /* Call Netherite's real compiled 1.7.10 entity_move! */
    entity_move(e, e->motion_x, e->motion_y, e->motion_z);

    e->motion_y -= 0.08;
    e->motion_y *= 0.9800000190734863;
    e->motion_x *= (double)v3;
    e->motion_z *= (double)v3;

    if (a & BR_ACT_MINE) {
        int hx = 0, hy = 0, hz = 0;
        int hit = cpu_pick_block(w, e->pos_x, e->pos_y + 1.62, e->pos_z, yaw, ps->pitch, &hx, &hy, &hz);
        if (hit == BR_BLK_LOG) {
            if (hx == ps->dig_x && hy == ps->dig_y && hz == ps->dig_z) ps->dig_ticks++;
            else { ps->dig_x = (int16_t)hx; ps->dig_y = (int16_t)hy; ps->dig_z = (int16_t)hz; ps->dig_ticks = 1; }
            if (ps->dig_ticks >= 60) {
                world_set_block(w, hx, hy, hz, 0, 0, 0);
                ps->logs_mined++;
                ps->won = 1;
                ps->dig_ticks = 0;
            }
        } else ps->dig_ticks = 0;
    } else ps->dig_ticks = 0;
}

int br_run_fidelity_gate(int rung, int prec, int n_worlds, int ticks, br_fidelity_t *out) {
    const int NUM_SEEDS = 8;
    uint8_t  *seed_blocks = (uint8_t *)malloc((size_t)NUM_SEEDS * BR_BLOCKS);
    uint32_t *seed_bits   = (uint32_t *)malloc((size_t)NUM_SEEDS * BR_BIT_WORDS * sizeof(uint32_t));
    br_gen_seeds(seed_blocks, seed_bits, NUM_SEEDS, 0x20261123ULL + (uint64_t)rung);

    br_player_state_t *init_st = (br_player_state_t *)calloc((size_t)n_worlds, sizeof(br_player_state_t));
    br_player_state_t *cpu_st  = (br_player_state_t *)calloc((size_t)n_worlds, sizeof(br_player_state_t));
    br_player_state_t *gpu_st  = (br_player_state_t *)calloc((size_t)n_worlds, sizeof(br_player_state_t));
    uint8_t *acts = (uint8_t *)malloc((size_t)ticks * (size_t)n_worlds);
    float   *yaws = (float *)malloc((size_t)ticks * (size_t)n_worlds * sizeof(float));

    uint64_t rng = 0x55aa0000ULL + (uint64_t)rung;
    for (int i = 0; i < n_worlds; i++) {
        if (rung == 2) {
            /* Wood-punch Rung 2: spawn facing an oak log trunk (x=16, z=18) at 2.5 blocks */
            init_st[i].x = 16.5;
            init_st[i].y = 9.0;
            init_st[i].z = 15.5;
            init_st[i].yaw = 0.0f; /* +Z looks directly at (16, 9..11, 18) */
            init_st[i].pitch = 0.0f;
            init_st[i].on_ground = 0;
        } else {
            init_st[i].x = 14.5 + (double)(sm64(&rng) & 31);
            init_st[i].y = (rung == 0) ? 14.0 : 9.5 + (double)(sm64(&rng) & 3) * 0.5;
            init_st[i].z = 14.5 + (double)(sm64(&rng) & 31);
            init_st[i].yaw = (float)(sm64(&rng) % 360);
            init_st[i].pitch = 0.0f;
            init_st[i].on_ground = 0;
        }
    }

    for (int t = 0; t < ticks; t++) {
        for (int i = 0; i < n_worlds; i++) {
            if (rung == 0) {
                /* Free-fall + straight walk + periodic jump */
                uint8_t a = BR_ACT_FORWARD | ((t % 25 == 0) ? BR_ACT_JUMP : 0);
                acts[(size_t)t * n_worlds + i] = a;
                yaws[(size_t)t * n_worlds + i] = init_st[i].yaw;
            } else if (rung == 1) {
                /* Full 3D movement + sprinting + yaw turning + obstacle/tree collisions */
                uint8_t a = 0;
                if ((t / 15 + i) & 1) a |= BR_ACT_FORWARD;
                if ((t / 20 + i) & 2) a |= BR_ACT_LEFT;
                if ((t % 18) == 0)    a |= BR_ACT_JUMP;
                if ((t / 30) & 1)     a |= BR_ACT_SPRINT;
                acts[(size_t)t * n_worlds + i] = a;
                yaws[(size_t)t * n_worlds + i] = fmodf(init_st[i].yaw + (float)(t * 3), 360.0f);
            } else {
                /* Rung 2: walk up to log and hold mine */
                uint8_t a = (t < 10) ? BR_ACT_FORWARD : BR_ACT_MINE;
                acts[(size_t)t * n_worlds + i] = a;
                yaws[(size_t)t * n_worlds + i] = 0.0f;
            }
        }
    }

    /* 1) Run GPU Bedrock fleet */
    br_fleet_t *f = br_fleet_create(n_worlds, NUM_SEEDS, prec, seed_blocks, seed_bits);
    br_fleet_upload_players(f, init_st, n_worlds);
    double gpu_us = br_fleet_run_parity(f, acts, yaws, ticks);
    br_fleet_download_players(f, gpu_st, n_worlds);
    br_fleet_destroy(f);

    /* 2) Run real CPU 1.7.10 engine ground truth */
    struct world *w = (struct world *)calloc(1, sizeof(struct world));
    double cpu_t0 = cpu_now_ns();
    for (int i = 0; i < n_worlds; i++) {
        load_seed_into_world(w, seed_blocks + (size_t)(i % NUM_SEEDS) * BR_BLOCKS);
        struct entity e;
        entity_init(&e, w);
        entity_set_size(&e, 0.6f, 1.8f);
        e.step_height = 0.0f; /* Rung 0-2 matches block collision before step-up rung */
        entity_set_position(&e, init_st[i].x, init_st[i].y, init_st[i].z);
        br_player_state_t ps = init_st[i];

        for (int t = 0; t < ticks; t++) {
            cpu_step_one(w, &e, &ps, acts[(size_t)t * n_worlds + i], yaws[(size_t)t * n_worlds + i]);
        }
        ps.x = e.pos_x; ps.y = e.pos_y; ps.z = e.pos_z;
        ps.vx = e.motion_x; ps.vy = e.motion_y; ps.vz = e.motion_z;
        ps.on_ground = e.on_ground;
        cpu_st[i] = ps;
        world_free(w);
    }
    double cpu_us = (cpu_now_ns() - cpu_t0) / 1000.0;
    free(w);

    /* 3) Compute fidelity metrics */
    double *perr = (double *)malloc((size_t)n_worlds * sizeof(double));
    double *verr = (double *)malloc((size_t)n_worlds * sizeof(double));
    int gmatch = 0, wmatch = 0, cwins = 0, gwins = 0;
    double max_pe = 0.0;
    for (int i = 0; i < n_worlds; i++) {
        double dx = gpu_st[i].x - cpu_st[i].x;
        double dy = gpu_st[i].y - cpu_st[i].y;
        double dz = gpu_st[i].z - cpu_st[i].z;
        double pe = sqrt(dx*dx + dy*dy + dz*dz);
        double dvx = gpu_st[i].vx - cpu_st[i].vx;
        double dvy = gpu_st[i].vy - cpu_st[i].vy;
        double dvz = gpu_st[i].vz - cpu_st[i].vz;
        perr[i] = pe;
        verr[i] = sqrt(dvx*dvx + dvy*dvy + dvz*dvz);
        if (pe > max_pe) max_pe = pe;
        if (gpu_st[i].on_ground == cpu_st[i].on_ground) gmatch++;
        if (gpu_st[i].won == cpu_st[i].won && gpu_st[i].logs_mined == cpu_st[i].logs_mined) wmatch++;
        cwins += cpu_st[i].won;
        gwins += gpu_st[i].won;
    }
    qsort(perr, (size_t)n_worlds, sizeof(double), cmp_d);
    qsort(verr, (size_t)n_worlds, sizeof(double), cmp_d);

    memset(out, 0, sizeof(*out));
    out->rung = rung;
    out->prec = prec;
    out->worlds = n_worlds;
    out->ticks = ticks;
    out->p50_pos_err = perr[n_worlds / 2];
    out->p90_pos_err = perr[(n_worlds * 9) / 10];
    out->max_pos_err = max_pe;
    out->p50_vel_err = verr[n_worlds / 2];
    out->ground_match_pct = 100.0 * (double)gmatch / (double)n_worlds;
    out->win_match_pct = 100.0 * (double)wmatch / (double)n_worlds;
    out->cpu_wins = cwins;
    out->gpu_wins = gwins;
    out->gpu_msteps_s = ((double)n_worlds * ticks) / gpu_us;
    out->cpu_msteps_s = ((double)n_worlds * ticks) / cpu_us;

    free(perr); free(verr);
    free(seed_blocks); free(seed_bits);
    free(init_st); free(cpu_st); free(gpu_st);
    free(acts); free(yaws);
    return 0;
}
