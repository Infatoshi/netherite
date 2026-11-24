#define _POSIX_C_SOURCE 200809L

#include "br_obs_bridge.h"
#include "engine/world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double BR_DEG2RAD = 0.017453292519943295;
static const double BR_EYE_STAND = 1.62;
static const double BR_EYE_SNEAK = 1.54;
static const double BR_RAY_MAX_DIST = 12.0;
static const double BR_CROSSHAIR_DIST = 4.5;

static inline float clampf(float x, float lo, float hi) {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

static inline int clampi(int x, int lo, int hi) {
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

static inline uint64_t splitmix64_next(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline float rng_uniform01(uint64_t *state) {
    uint64_t r = splitmix64_next(state);
    /* Top 24 bits -> [0, 1) float32 */
    return (float)(r >> 40) * (1.0f / 16777216.0f);
}

/* ============================================================================
 * 1. Shared 3D Amanatides-Woo Voxel DDA & 128-Dim Observation Extractor
 * ============================================================================ */

typedef uint8_t (*br_get_blk_fn)(const void *ctx, int x, int y, int z);

typedef struct {
    const brc_seed_entry_t *seed;
    const br_delta_entry_t *deltas;
    int n_deltas;
} br_bubble_ctx_t;

static uint8_t world_get_blk_cb(const void *ctx, int x, int y, int z) {
    struct world *w = (struct world *)ctx;
    int id = world_get_block(w, x, y, z) & 4095;
    return (id >= 0 && id <= 255) ? (uint8_t)id : (uint8_t)BRC_BLK_AIR;
}

static uint8_t bubble_get_blk_cb(const void *ctx, int x, int y, int z) {
    const br_bubble_ctx_t *bc = (const br_bubble_ctx_t *)ctx;
    if ((unsigned)x >= (unsigned)BRC_WX ||
        (unsigned)y >= (unsigned)BRC_WY ||
        (unsigned)z >= (unsigned)BRC_WZ) {
        return BRC_BLK_AIR;
    }
    /* Latest delta override wins */
    for (int i = bc->n_deltas - 1; i >= 0; i--) {
        if ((int)bc->deltas[i].x == x &&
            (int)bc->deltas[i].y == y &&
            (int)bc->deltas[i].z == z) {
            return bc->deltas[i].block_id;
        }
    }
    int idx = ((y << 12) | (z << 6) | x);
    return bc->seed->blocks[idx];
}

/* Exact 3D Amanatides-Woo DDA voxel traversal from (ox,oy,oz) along (yaw_deg,pitch_deg).
 * Checks starting voxel at t=0 and then steps exact voxel face crossings up to max_dist. */
static float raycast_dda_common(br_get_blk_fn get_blk, const void *ctx,
                                double ox, double oy, double oz,
                                float yaw_deg, float pitch_deg,
                                double max_dist,
                                uint8_t *out_blk,
                                int *hit_x, int *hit_y, int *hit_z,
                                int *prev_x, int *prev_y, int *prev_z) {
    double yr = (double)yaw_deg * BR_DEG2RAD;
    double pr = (double)pitch_deg * BR_DEG2RAD;
    double cp = cos(pr);
    double rx = -sin(yr) * cp;
    double ry = -sin(pr);
    double rz =  cos(yr) * cp;

    int vx = (int)floor(ox);
    int vy = (int)floor(oy);
    int vz = (int)floor(oz);
    int px = vx, py = vy, pz = vz;

    uint8_t b0 = get_blk(ctx, vx, vy, vz);
    if (b0 != BRC_BLK_AIR) {
        if (out_blk) *out_blk = b0;
        if (hit_x) *hit_x = vx;
        if (hit_y) *hit_y = vy;
        if (hit_z) *hit_z = vz;
        if (prev_x) *prev_x = px;
        if (prev_y) *prev_y = py;
        if (prev_z) *prev_z = pz;
        return 0.0f;
    }

    const double eps = 1e-12;
    const double inf = 1e30;

    int sx = (rx > 0.0) ? 1 : -1;
    int sy = (ry > 0.0) ? 1 : -1;
    int sz = (rz > 0.0) ? 1 : -1;

    double t_delta_x = (fabs(rx) > eps) ? fabs(1.0 / rx) : inf;
    double t_delta_y = (fabs(ry) > eps) ? fabs(1.0 / ry) : inf;
    double t_delta_z = (fabs(rz) > eps) ? fabs(1.0 / rz) : inf;

    double t_max_x = (fabs(rx) > eps)
                         ? ((rx > 0.0 ? ((double)(vx + 1) - ox) : (ox - (double)vx)) / fabs(rx))
                         : inf;
    double t_max_y = (fabs(ry) > eps)
                         ? ((ry > 0.0 ? ((double)(vy + 1) - oy) : (oy - (double)vy)) / fabs(ry))
                         : inf;
    double t_max_z = (fabs(rz) > eps)
                         ? ((rz > 0.0 ? ((double)(vz + 1) - oz) : (oz - (double)vz)) / fabs(rz))
                         : inf;

    double t = 0.0;
    /* Upper bound on voxel boundary crossings within max_dist: 3 * ceil(max_dist) + 8 */
    const int max_steps = (int)ceil(max_dist * 3.0) + 16;
    for (int step = 0; step < max_steps; step++) {
        px = vx;
        py = vy;
        pz = vz;
        if (t_max_x <= t_max_y && t_max_x <= t_max_z) {
            t = t_max_x;
            t_max_x += t_delta_x;
            vx += sx;
        } else if (t_max_y <= t_max_z) {
            t = t_max_y;
            t_max_y += t_delta_y;
            vy += sy;
        } else {
            t = t_max_z;
            t_max_z += t_delta_z;
            vz += sz;
        }
        if (t > max_dist) break;
        uint8_t blk = get_blk(ctx, vx, vy, vz);
        if (blk != BRC_BLK_AIR) {
            if (out_blk) *out_blk = blk;
            if (hit_x) *hit_x = vx;
            if (hit_y) *hit_y = vy;
            if (hit_z) *hit_z = vz;
            if (prev_x) *prev_x = px;
            if (prev_y) *prev_y = py;
            if (prev_z) *prev_z = pz;
            return (float)t;
        }
    }

    if (out_blk) *out_blk = BRC_BLK_AIR;
    if (hit_x) *hit_x = vx;
    if (hit_y) *hit_y = vy;
    if (hit_z) *hit_z = vz;
    if (prev_x) *prev_x = px;
    if (prev_y) *prev_y = py;
    if (prev_z) *prev_z = pz;
    return (float)max_dist;
}

uint8_t br_world_raycast(struct world *w,
                         double ox, double oy, double oz,
                         float yaw_deg, float pitch_deg,
                         double max_dist,
                         int *hit_x, int *hit_y, int *hit_z,
                         int *prev_x, int *prev_y, int *prev_z,
                         float *out_dist) {
    uint8_t blk = BRC_BLK_AIR;
    float d = raycast_dda_common(world_get_blk_cb, w, ox, oy, oz,
                                 yaw_deg, pitch_deg, max_dist,
                                 &blk, hit_x, hit_y, hit_z, prev_x, prev_y, prev_z);
    if (out_dist) *out_dist = d;
    return blk;
}

static inline int is_build_block_item(uint16_t item_id) {
    return item_id == BRC_ITEM_COBBLE ||
           item_id == BRC_ITEM_DIRT ||
           item_id == BRC_ITEM_STONE;
}

static void extract_obs_common(br_get_blk_fn get_blk, const void *ctx,
                               const br_obs_state_t *st,
                               float obs[BRC_OBS_DIM]) {
    static const float dy_rows[4] = { -24.0f, -8.0f, +8.0f, +24.0f };

    double eye_y = st->py + (st->sneaking ? BR_EYE_SNEAK : BR_EYE_STAND);

    /* obs[0..63]: 4 pitch rows x 16 yaw cols normalized DDA hit distances */
    /* obs[64..79]: 16 block-class codes along row 1 (pitch - 8 deg) */
    for (int r = 0; r < 4; r++) {
        float ray_pitch = clampf(st->pitch_deg + dy_rows[r], -89.9f, 89.9f);
        for (int c = 0; c < 16; c++) {
            float ray_yaw = st->yaw_deg + ((float)c - 7.5f) * 8.0f;
            uint8_t hit_blk = BRC_BLK_AIR;
            float dist = raycast_dda_common(get_blk, ctx,
                                            st->px, eye_y, st->pz,
                                            ray_yaw, ray_pitch,
                                            BR_RAY_MAX_DIST,
                                            &hit_blk,
                                            NULL, NULL, NULL, NULL, NULL, NULL);
            obs[r * 16 + c] = clampf(dist / (float)BR_RAY_MAX_DIST, 0.0f, 1.0f);
            if (r == 1) {
                obs[64 + c] = brc_blk_class_code(hit_blk);
            }
        }
    }

    /* obs[80..103]: exact 24 scalars matching br_contract.h lines 168-191 */
    double yr = (double)st->yaw_deg * BR_DEG2RAD;
    double sy = sin(yr);
    double cy = cos(yr);

    /* Minecraft horizontal basis:
     *   fwd_xz   = (-sin(yaw),  cos(yaw))
     *   right_xz = (-cos(yaw), -sin(yaw))  (cross(fwd_xz, +Y); matches +yaw turning right) */
    double fwd_x = -sy, fwd_z = cy;
    double right_x = -cy, right_z = -sy;

    double dlx = st->lava_x - st->px;
    double dlz = st->lava_z - st->pz;
    double dist_xz = sqrt(dlx * dlx + dlz * dlz);
    double dir_x = (dist_xz > 1e-6) ? (dlx / dist_xz) : 0.0;
    double dir_z = (dist_xz > 1e-6) ? (dlz / dist_xz) : 0.0;

    obs[80] = (float)(fwd_x * dir_x + fwd_z * dir_z);
    obs[81] = (float)(right_x * dir_x + right_z * dir_z);
    obs[82] = clampf((float)(dist_xz / 16.0), 0.0f, 1.0f);
    obs[83] = clampf((float)((st->lava_y - st->py) / 8.0), -1.0f, 1.0f);
    obs[84] = (float)sy;
    obs[85] = (float)cy;
    obs[86] = clampf(st->pitch_deg / 90.0f, -1.0f, 1.0f);
    obs[87] = clampf((float)st->vx, -1.0f, 1.0f);
    obs[88] = clampf((float)st->vy, -1.0f, 1.0f);
    obs[89] = clampf((float)st->vz, -1.0f, 1.0f);
    obs[90] = st->on_ground ? 1.0f : 0.0f;
    obs[91] = st->sneaking ? 1.0f : 0.0f;
    obs[92] = clampf(st->hp / 20.0f, 0.0f, 1.0f);

    int slot = clampi((int)st->sel_slot, 0, 8);
    obs[93] = (float)slot / 8.0f;

    uint16_t held = (st->hotbar_cnt[slot] > 0) ? st->hotbar_item[slot] : (uint16_t)BRC_ITEM_NONE;
    obs[94] = (held == BRC_ITEM_BUCKET_WATER) ? 1.0f : 0.0f;
    obs[95] = (held == BRC_ITEM_BUCKET_LAVA)  ? 1.0f : 0.0f;
    obs[96] = (held == BRC_ITEM_BUCKET_EMPTY) ? 1.0f : 0.0f;
    obs[97] = (held == BRC_ITEM_FLINT_STEEL)  ? 1.0f : 0.0f;
    obs[98] = is_build_block_item(held)       ? 1.0f : 0.0f;

    /* Total building blocks (cobble/dirt/stone) across hotbar slots 0..8 */
    int build_cnt = 0;
    for (int s = 0; s < 9; s++) {
        if (st->hotbar_cnt[s] > 0 && is_build_block_item(st->hotbar_item[s])) {
            build_cnt += (int)st->hotbar_cnt[s];
        }
    }
    obs[99]  = clampf((float)build_cnt / 64.0f, 0.0f, 1.0f);
    obs[100] = clampf((float)st->obsidian_placed / 10.0f, 0.0f, 1.0f);
    obs[101] = st->portal_lit ? 1.0f : 0.0f;

    uint8_t cross_blk = BRC_BLK_AIR;
    (void)raycast_dda_common(get_blk, ctx,
                             st->px, eye_y, st->pz,
                             st->yaw_deg, clampf(st->pitch_deg, -89.9f, 89.9f),
                             BR_CROSSHAIR_DIST,
                             &cross_blk,
                             NULL, NULL, NULL, NULL, NULL, NULL);
    obs[102] = brc_blk_class_code(cross_blk);
    obs[103] = (st->max_ticks > 0)
                   ? clampf((float)st->ep_tick / (float)st->max_ticks, 0.0f, 1.0f)
                   : 0.0f;

    /* obs[104..127]: 0.0f pad for K=128 NVFP4 Tensor Core alignment */
    for (int k = 104; k < BRC_OBS_DIM; k++) {
        obs[k] = 0.0f;
    }
}

void br_extract_obs_from_world(struct world *w,
                               const br_obs_state_t *st,
                               float obs[BRC_OBS_DIM]) {
    extract_obs_common(world_get_blk_cb, w, st, obs);
}

void br_extract_obs_from_bubble(const brc_seed_entry_t *seed,
                                const br_delta_entry_t *deltas,
                                int n_deltas,
                                const br_obs_state_t *st,
                                float obs[BRC_OBS_DIM]) {
    br_bubble_ctx_t ctx;
    ctx.seed = seed;
    ctx.deltas = deltas;
    ctx.n_deltas = (deltas && n_deltas > 0) ? n_deltas : 0;
    extract_obs_common(bubble_get_blk_cb, &ctx, st, obs);
}

/* ============================================================================
 * 2. CPU BRT1 Teacher Checkpoint Loader & Forward Inference
 * ============================================================================ */

static void set_err(char *err, size_t err_sz, const char *msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg);
    }
}

size_t br_teacher_num_floats(const brc_teacher_hdr_t *hdr) {
    size_t H = hdr->hidden;
    size_t D = hdr->obs_dim;
    size_t C = (size_t)2 * hdr->cam_bins;
    size_t K = hdr->n_keys;
    size_t B = hdr->n_hotbar;
    return H * D + H * H +
           C * H + C +
           K * H + K +
           B * H + B +
           H + 1;
}

static void teacher_bind_ptrs(br_teacher_t *t) {
    size_t H = t->hdr.hidden;
    size_t D = t->hdr.obs_dim;
    size_t C = (size_t)2 * t->hdr.cam_bins;
    size_t K = t->hdr.n_keys;
    size_t B = t->hdr.n_hotbar;
    float *p = t->weights;

    t->w1     = p; p += H * D;
    t->w2     = p; p += H * H;
    t->w_cam  = p; p += C * H;
    t->b_cam  = p; p += C;
    t->w_keys = p; p += K * H;
    t->b_keys = p; p += K;
    t->w_hot  = p; p += B * H;
    t->b_hot  = p; p += B;
    t->w_val  = p; p += H;
    t->b_val  = p; p += 1;
}

br_teacher_t *br_teacher_alloc(uint32_t hidden) {
    if (hidden == 0 || hidden > 8192) return NULL;
    br_teacher_t *t = (br_teacher_t *)calloc(1, sizeof(*t));
    if (!t) return NULL;

    t->hdr.magic    = BRC_CKPT_MAGIC;
    t->hdr.version  = 1;
    t->hdr.obs_dim  = BRC_OBS_DIM;
    t->hdr.hidden   = hidden;
    t->hdr.cam_bins = BRC_CAM_BINS;
    t->hdr.n_keys   = BRC_KEYS;
    t->hdr.n_hotbar = BRC_HOTBAR_CATS;
    t->hdr.reserved = 0;

    t->n_weights = br_teacher_num_floats(&t->hdr);
    t->weights = (float *)calloc(t->n_weights, sizeof(float));
    t->h1 = (float *)calloc(hidden, sizeof(float));
    t->h2 = (float *)calloc(hidden, sizeof(float));
    if (!t->weights || !t->h1 || !t->h2) {
        br_teacher_free(t);
        return NULL;
    }
    teacher_bind_ptrs(t);
    return t;
}

br_teacher_t *br_teacher_load(const char *path, char *err, size_t err_sz) {
    if (!path) {
        set_err(err, err_sz, "br_teacher_load: NULL path");
        return NULL;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (err && err_sz > 0) snprintf(err, err_sz, "br_teacher_load: failed to open %s", path);
        return NULL;
    }

    brc_teacher_hdr_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        set_err(err, err_sz, "br_teacher_load: truncated header");
        fclose(fp);
        return NULL;
    }
    if (hdr.magic != BRC_CKPT_MAGIC) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "br_teacher_load: bad magic 0x%08x (expected 0x%08x)",
                     hdr.magic, BRC_CKPT_MAGIC);
        }
        fclose(fp);
        return NULL;
    }
    if (hdr.version != 1 ||
        hdr.obs_dim != BRC_OBS_DIM ||
        hdr.cam_bins != BRC_CAM_BINS ||
        hdr.n_keys != BRC_KEYS ||
        hdr.n_hotbar != BRC_HOTBAR_CATS ||
        hdr.hidden == 0 || hdr.hidden > 8192) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz,
                     "br_teacher_load: unsupported header (v=%u obs=%u H=%u cam=%u keys=%u hot=%u)",
                     hdr.version, hdr.obs_dim, hdr.hidden, hdr.cam_bins, hdr.n_keys, hdr.n_hotbar);
        }
        fclose(fp);
        return NULL;
    }

    br_teacher_t *t = br_teacher_alloc(hdr.hidden);
    if (!t) {
        set_err(err, err_sz, "br_teacher_load: out of memory");
        fclose(fp);
        return NULL;
    }
    t->hdr = hdr;

    size_t got = fread(t->weights, sizeof(float), t->n_weights, fp);
    fclose(fp);
    if (got != t->n_weights) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "br_teacher_load: truncated weights (%zu / %zu floats)",
                     got, t->n_weights);
        }
        br_teacher_free(t);
        return NULL;
    }
    return t;
}

int br_teacher_save(const br_teacher_t *t, const char *path, char *err, size_t err_sz) {
    if (!t || !path) {
        set_err(err, err_sz, "br_teacher_save: NULL arg");
        return -1;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        if (err && err_sz > 0) snprintf(err, err_sz, "br_teacher_save: failed to open %s", path);
        return -1;
    }
    if (fwrite(&t->hdr, sizeof(t->hdr), 1, fp) != 1 ||
        fwrite(t->weights, sizeof(float), t->n_weights, fp) != t->n_weights) {
        set_err(err, err_sz, "br_teacher_save: write error");
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0) {
        set_err(err, err_sz, "br_teacher_save: close error");
        return -1;
    }
    return 0;
}

void br_teacher_free(br_teacher_t *t) {
    if (!t) return;
    free(t->weights);
    free(t->h1);
    free(t->h2);
    free(t);
}

static void matvec_relu(const float *W, const float *x, float *y, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        const float *wr = W + (size_t)r * (size_t)cols;
        float acc = 0.0f;
        for (int c = 0; c < cols; c++) {
            acc += wr[c] * x[c];
        }
        y[r] = (acc > 0.0f) ? acc : 0.0f;
    }
}

static void matvec_bias(const float *W, const float *b, const float *x, float *y, int rows, int cols) {
    for (int r = 0; r < rows; r++) {
        const float *wr = W + (size_t)r * (size_t)cols;
        float acc = b[r];
        for (int c = 0; c < cols; c++) {
            acc += wr[c] * x[c];
        }
        y[r] = acc;
    }
}

static void softmax_temp(const float *logits, float *probs, int n, float inv_temp) {
    float mx = logits[0] * inv_temp;
    for (int i = 1; i < n; i++) {
        float v = logits[i] * inv_temp;
        if (v > mx) mx = v;
    }
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float e = expf(logits[i] * inv_temp - mx);
        probs[i] = e;
        sum += e;
    }
    float inv_sum = (sum > 0.0f) ? (1.0f / sum) : (1.0f / (float)n);
    for (int i = 0; i < n; i++) {
        probs[i] *= inv_sum;
    }
}

static inline float sigmoidf_stable(float x) {
    if (x >= 0.0f) {
        float z = expf(-x);
        return 1.0f / (1.0f + z);
    } else {
        float z = expf(x);
        return z / (1.0f + z);
    }
}

static int argmax_f(const float *v, int n) {
    int best = 0;
    float best_v = v[0];
    for (int i = 1; i < n; i++) {
        if (v[i] > best_v) {
            best_v = v[i];
            best = i;
        }
    }
    return best;
}

static int sample_categorical(const float *probs, int n, uint64_t *rng) {
    float u = rng_uniform01(rng);
    float cdf = 0.0f;
    for (int i = 0; i < n - 1; i++) {
        cdf += probs[i];
        if (u < cdf) return i;
    }
    return n - 1;
}

void br_teacher_infer(br_teacher_t *t,
                      const float obs[BRC_OBS_DIM],
                      float temperature,
                      uint64_t *rng_state,
                      brc_s1_action_t *out_act,
                      br_teacher_out_t *out_probs) {
    int H = (int)t->hdr.hidden;
    int D = (int)t->hdr.obs_dim;

    /* Hidden layers: ReLU(W1 * obs), ReLU(W2 * h1) */
    matvec_relu(t->w1, obs, t->h1, H, D);
    matvec_relu(t->w2, t->h1, t->h2, H, H);

    /* Policy & value heads */
    br_teacher_out_t local_out;
    br_teacher_out_t *o = out_probs ? out_probs : &local_out;

    matvec_bias(t->w_cam,  t->b_cam,  t->h2, o->cam_logits, 2 * BRC_CAM_BINS, H);
    matvec_bias(t->w_keys, t->b_keys, t->h2, o->key_logits, BRC_KEYS, H);
    matvec_bias(t->w_hot,  t->b_hot,  t->h2, o->hot_logits, BRC_HOTBAR_CATS, H);
    matvec_bias(t->w_val,  t->b_val,  t->h2, &o->value,     1, H);

    float inv_temp = (temperature > 0.0f) ? (1.0f / temperature) : 1.0f;
    softmax_temp(o->cam_logits,                o->pitch_probs, BRC_CAM_BINS,    inv_temp);
    softmax_temp(o->cam_logits + BRC_CAM_BINS, o->yaw_probs,   BRC_CAM_BINS,    inv_temp);
    softmax_temp(o->hot_logits,                o->hot_probs,   BRC_HOTBAR_CATS, inv_temp);
    for (int k = 0; k < BRC_KEYS; k++) {
        o->key_probs[k] = sigmoidf_stable(o->key_logits[k] * inv_temp);
    }

    if (out_act) {
        if (temperature <= 0.0f) {
            out_act->a[0] = argmax_f(o->cam_logits, BRC_CAM_BINS);
            out_act->a[1] = argmax_f(o->cam_logits + BRC_CAM_BINS, BRC_CAM_BINS);
            for (int k = 0; k < BRC_KEYS; k++) {
                out_act->a[2 + k] = (o->key_probs[k] >= 0.5f) ? 1 : 0;
            }
            out_act->a[16] = argmax_f(o->hot_logits, BRC_HOTBAR_CATS);
        } else {
            uint64_t fallback_rng = 0x243f6a8885a308d3ULL;
            uint64_t *rng = rng_state ? rng_state : &fallback_rng;
            out_act->a[0] = sample_categorical(o->pitch_probs, BRC_CAM_BINS, rng);
            out_act->a[1] = sample_categorical(o->yaw_probs,   BRC_CAM_BINS, rng);
            for (int k = 0; k < BRC_KEYS; k++) {
                out_act->a[2 + k] = (rng_uniform01(rng) < o->key_probs[k]) ? 1 : 0;
            }
            out_act->a[16] = sample_categorical(o->hot_probs, BRC_HOTBAR_CATS, rng);
        }
    }
}

/* ============================================================================
 * 3. distill.cu Label Record Adapter
 * ============================================================================ */

static void pack_prev_fields(int32_t p[4], const brc_s1_action_t *prev_act, int32_t prev_macro_kind) {
    if (!prev_act) {
        p[0] = BRC_CAM_BINS / 2;
        p[1] = BRC_CAM_BINS / 2;
        p[2] = 0;
        p[3] = prev_macro_kind;
        return;
    }
    p[0] = clampi(prev_act->a[0], 0, BRC_CAM_BINS - 1);
    p[1] = clampi(prev_act->a[1], 0, BRC_CAM_BINS - 1);
    int32_t kbits = 0;
    for (int k = 0; k < BRC_KEYS; k++) {
        if (prev_act->a[2 + k] > 0) {
            kbits |= (1 << k);
        }
    }
    p[2] = kbits;
    p[3] = prev_macro_kind;
}

void br_emit_distill_rec(brc_distill_rec_t *rec,
                         const brc_s1_action_t *act,
                         const brc_s1_action_t *prev_act,
                         int32_t prev_macro_kind,
                         float weight,
                         int32_t valid,
                         int32_t soft,
                         int32_t hold) {
    memset(rec, 0, sizeof(*rec));
    pack_prev_fields(rec->p, prev_act, prev_macro_kind);

    if (!valid || !act) {
        rec->a[0] = BRC_CAM_BINS / 2;
        rec->a[1] = BRC_CAM_BINS / 2;
        rec->w = 0.0f;
        rec->valid = 0;
        rec->soft = 0;
        rec->hold = 0;
        return;
    }

    rec->a[0] = clampi(act->a[0], 0, BRC_CAM_BINS - 1);
    rec->a[1] = clampi(act->a[1], 0, BRC_CAM_BINS - 1);
    for (int k = 0; k < BRC_KEYS; k++) {
        rec->a[2 + k] = (act->a[2 + k] > 0) ? 1 : 0;
    }
    rec->a[16] = clampi(act->a[16], 0, BRC_HOTBAR_CATS - 1);
    rec->w     = weight;
    rec->valid = 1;
    rec->soft  = soft ? 1 : 0;
    rec->hold  = hold ? 1 : 0;
}

void br_emit_distill_rec_nohot(brc_distill_rec_nohot_t *rec,
                               const brc_s1_action_t *act,
                               const brc_s1_action_t *prev_act,
                               int32_t prev_macro_kind,
                               float weight,
                               int32_t valid,
                               int32_t soft,
                               int32_t hold) {
    memset(rec, 0, sizeof(*rec));
    pack_prev_fields(rec->p, prev_act, prev_macro_kind);

    if (!valid || !act) {
        rec->a[0] = BRC_CAM_BINS / 2;
        rec->a[1] = BRC_CAM_BINS / 2;
        rec->w = 0.0f;
        rec->valid = 0;
        rec->soft = 0;
        rec->hold = 0;
        return;
    }

    rec->a[0] = clampi(act->a[0], 0, BRC_CAM_BINS - 1);
    rec->a[1] = clampi(act->a[1], 0, BRC_CAM_BINS - 1);
    for (int k = 0; k < BRC_KEYS; k++) {
        rec->a[2 + k] = (act->a[2 + k] > 0) ? 1 : 0;
    }
    rec->w     = weight;
    rec->valid = 1;
    rec->soft  = soft ? 1 : 0;
    rec->hold  = hold ? 1 : 0;
}

/* ============================================================================
 * 4. BRS1 Seed Pack Loader
 * ============================================================================ */

int br_load_seed_pack(const char *path,
                      brc_seed_entry_t **out_seeds,
                      uint32_t *out_count,
                      char *err,
                      size_t err_sz) {
    if (!path || !out_seeds || !out_count) {
        set_err(err, err_sz, "br_load_seed_pack: NULL arg");
        return -1;
    }
    *out_seeds = NULL;
    *out_count = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        if (err && err_sz > 0) snprintf(err, err_sz, "br_load_seed_pack: failed to open %s", path);
        return -1;
    }

    brc_seed_file_hdr_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        set_err(err, err_sz, "br_load_seed_pack: truncated header");
        fclose(fp);
        return -1;
    }
    if (hdr.magic != BRC_SEED_MAGIC) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "br_load_seed_pack: bad magic 0x%08x (expected 0x%08x)",
                     hdr.magic, BRC_SEED_MAGIC);
        }
        fclose(fp);
        return -1;
    }
    if (hdr.num_seeds == 0 || hdr.num_seeds > 1000000u) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "br_load_seed_pack: invalid num_seeds %u", hdr.num_seeds);
        }
        fclose(fp);
        return -1;
    }

    brc_seed_entry_t *seeds = (brc_seed_entry_t *)malloc((size_t)hdr.num_seeds * sizeof(*seeds));
    if (!seeds) {
        set_err(err, err_sz, "br_load_seed_pack: out of memory");
        fclose(fp);
        return -1;
    }
    size_t got = fread(seeds, sizeof(*seeds), hdr.num_seeds, fp);
    fclose(fp);
    if (got != hdr.num_seeds) {
        if (err && err_sz > 0) {
            snprintf(err, err_sz, "br_load_seed_pack: truncated entries (%zu / %u)",
                     got, hdr.num_seeds);
        }
        free(seeds);
        return -1;
    }

    *out_seeds = seeds;
    *out_count = hdr.num_seeds;
    return 0;
}
