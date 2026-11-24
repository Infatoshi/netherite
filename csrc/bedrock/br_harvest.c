#define _POSIX_C_SOURCE 200809L
/* Lane A (br-harvester): Harvest real 1.7.10 surface lava-pool 64x32x64 bubbles
 * directly from libnetherite.a snapshots into the frozen BRS1 binary pack format
 * defined in csrc/bedrock/br_contract.h.
 */

#include "br_contract.h"
#include "engine/snapshot.h"
#include "engine/world.h"
#include "engine/nbtjson.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_IN_DIRS   64
#define MAX_CANDS     16384
#define MAX_PATH_LEN  1024
#define MAX_LAVA_PTS  65536

typedef struct {
    char path[MAX_PATH_LEN];
    char source_name[64];
    int  in_idx;
    int  priority; /* 0 = speedrun/bucket, 1 = flint_and_steel, 2 = other */
    int  has_seed_hint;
    int64_t seed_hint;
} cand_t;

typedef struct {
    int x, y, z;
    uint8_t sky_open;
    uint8_t headroom; /* vertical air cells above (x, y, z), capped at 16 */
} lava_pt_t;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int ends_with(const char *s, const char *suf) {
    size_t sl = strlen(s), sufl = strlen(suf);
    return sl >= sufl && strcmp(s + sl - sufl, suf) == 0;
}

static int parse_seed_hint(const char *name, int64_t *out) {
    if (name[0] != 's') return 0;
    const char *p = name + 1;
    if (*p == '-') p++;
    if (*p < '0' || *p > '9') return 0;
    char *end = NULL;
    errno = 0;
    long long v = strtoll(name + 1, &end, 10);
    if (errno != 0 || end == name + 1) return 0;
    if (*end != '\0' && *end != '-' && *end != '_') return 0;
    *out = (int64_t)v;
    return 1;
}

static void derive_source_name(const char *snap_dir, char out[64]) {
    char tmp[MAX_PATH_LEN];
    snprintf(tmp, sizeof(tmp), "%s", snap_dir);
    size_t n = strlen(tmp);
    while (n > 1 && tmp[n - 1] == '/') tmp[--n] = '\0';
    char *slash = strrchr(tmp, '/');
    const char *base = slash ? slash + 1 : tmp;
    if (strcmp(base, "start") == 0 && slash && slash > tmp) {
        *slash = '\0';
        char *pslash = strrchr(tmp, '/');
        base = pslash ? pslash + 1 : tmp;
    }
    snprintf(out, 64, "%.63s", base);
}

static int cand_priority(const char *name) {
    if (ends_with(name, "-bucket")) return 0;
    if (ends_with(name, "-flint_and_steel")) return 1;
    if (ends_with(name, "-iron_pickaxe")) return 2;
    if (ends_with(name, "-diamond_pickaxe")) return 3;
    return 4;
}

static int cmp_cand(const void *a, const void *b) {
    const cand_t *ca = (const cand_t *)a;
    const cand_t *cb = (const cand_t *)b;
    if (ca->in_idx != cb->in_idx) return ca->in_idx - cb->in_idx;
    if (ca->priority != cb->priority) return ca->priority - cb->priority;
    return strcmp(ca->source_name, cb->source_name);
}

static int cmp_str_ptr(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

static int cmp_int32(const void *a, const void *b) {
    int32_t ia = *(const int32_t *)a;
    int32_t ib = *(const int32_t *)b;
    return (ia > ib) - (ia < ib);
}

static int collect_candidates(const char **in_dirs, int n_in, cand_t *cands, int max_cands) {
    int nc = 0;
    for (int d = 0; d < n_in; d++) {
        const char *root = in_dirs[d];
        char mpath[MAX_PATH_LEN];
        snprintf(mpath, sizeof(mpath), "%s/manifest.json", root);
        if (file_exists(mpath)) {
            if (nc < max_cands) {
                cand_t *c = &cands[nc++];
                memset(c, 0, sizeof(*c));
                snprintf(c->path, sizeof(c->path), "%s", root);
                derive_source_name(root, c->source_name);
                c->in_idx = d;
                c->priority = cand_priority(c->source_name);
                c->has_seed_hint = parse_seed_hint(c->source_name, &c->seed_hint);
            }
            continue;
        }
        DIR *dp = opendir(root);
        if (!dp) {
            fprintf(stderr, "br_harvest: warning: cannot open dir '%s': %s\n", root, strerror(errno));
            continue;
        }
        char **names = NULL;
        size_t n_names = 0, cap_names = 0;
        struct dirent *de;
        while ((de = readdir(dp)) != NULL) {
            if (de->d_name[0] == '.') continue;
            if (n_names == cap_names) {
                cap_names = cap_names ? cap_names * 2 : 256;
                char **nn = (char **)realloc(names, cap_names * sizeof(char *));
                if (!nn) { closedir(dp); free(names); return nc; }
                names = nn;
            }
            names[n_names++] = strdup(de->d_name);
        }
        closedir(dp);
        qsort(names, n_names, sizeof(char *), cmp_str_ptr);

        for (size_t i = 0; i < n_names && nc < max_cands; i++) {
            char sub[MAX_PATH_LEN];
            int sub_n = snprintf(sub, sizeof(sub), "%s/%s", root, names[i]);
            if (sub_n < 0 || sub_n >= (int)sizeof(sub) - 32) continue;
            if (!dir_exists(sub)) continue;

            char m1[MAX_PATH_LEN], m2[MAX_PATH_LEN];
            snprintf(m1, sizeof(m1), "%.980s/manifest.json", sub);
            snprintf(m2, sizeof(m2), "%.980s/start/manifest.json", sub);

            char chosen[MAX_PATH_LEN];
            int found = 0;
            if (file_exists(m1)) {
                snprintf(chosen, sizeof(chosen), "%.1023s", sub);
                found = 1;
            } else if (file_exists(m2)) {
                snprintf(chosen, sizeof(chosen), "%.1000s/start", sub);
                found = 1;
            }
            if (found) {
                cand_t *c = &cands[nc++];
                memset(c, 0, sizeof(*c));
                snprintf(c->path, sizeof(c->path), "%.1023s", chosen);
                snprintf(c->source_name, sizeof(c->source_name), "%.63s", names[i]);
                c->in_idx = d;
                c->priority = cand_priority(c->source_name);
                c->has_seed_hint = parse_seed_hint(c->source_name, &c->seed_hint);
            }
        }
        for (size_t i = 0; i < n_names; i++) free(names[i]);
        free(names);
    }
    qsort(cands, (size_t)nc, sizeof(cand_t), cmp_cand);
    return nc;
}

static inline int blk_idx(int x, int y, int z) {
    return ((y & 31) << 12) | ((z & 63) << 6) | (x & 63);
}

static int get_player_pos(const struct snapshot *s, double *px, double *py, double *pz, int *dim) {
    *px = 0.0; *py = 64.0; *pz = 0.0; *dim = 0;
    if (!s->player_server) return 0;
    const nbt *f = nbt_get(s->player_server, "fields");
    if (!f) return 0;
    const nbt *nx = nbt_get(f, "posX");
    const nbt *ny = nbt_get(f, "posY");
    const nbt *nz = nbt_get(f, "posZ");
    const nbt *nd = nbt_get(f, "dimension");
    if (nd) *dim = (int)nbt_int_value(nd);
    if (!nx || !ny || !nz) return 0;
    uint64_t bx = nbt_double_bits(nx);
    uint64_t by = nbt_double_bits(ny);
    uint64_t bz = nbt_double_bits(nz);
    memcpy(px, &bx, sizeof(double));
    memcpy(py, &by, sizeof(double));
    memcpy(pz, &bz, sizeof(double));
    return 1;
}

static int loaded_block(struct world *w, int wx, int wy, int wz, uint8_t *out_meta) {
    if (wy < 0 || wy >= 256) {
        if (out_meta) *out_meta = 0;
        return 0;
    }
    int cx = wx >> 4, cz = wz >> 4;
    struct chunk *c = world_chunk(w, cx, cz);
    if (!c) return -1;
    int lx = wx & 15, lz = wz & 15;
    if (out_meta) *out_meta = (uint8_t)(chunk_get_meta(c, lx, wy, lz) & 0x0F);
    return chunk_get_block(c, lx, wy, lz) & 0xFF;
}

static int bubble_chunks_loaded(struct world *w, int ox, int oy, int oz) {
    if (oy < 0 || oy + BRC_WY > 256) return 0;
    int cx0 = ox >> 4;
    int cx1 = (ox + BRC_WX - 1) >> 4;
    int cz0 = oz >> 4;
    int cz1 = (oz + BRC_WZ - 1) >> 4;
    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            if (!world_chunk(w, cx, cz)) return 0;
        }
    }
    return 1;
}

static int extract_bubble(struct world *w, int ox, int oy, int oz,
                          uint8_t *blocks, uint8_t *meta, uint32_t *solid_bits,
                          int32_t *out_lava_sources) {
    if (!bubble_chunks_loaded(w, ox, oy, oz)) return 0;
    memset(solid_bits, 0, BRC_BIT_WORDS * sizeof(uint32_t));
    int32_t lava_cnt = 0;

    int cx0 = ox >> 4, cz0 = oz >> 4;
    int cx1 = (ox + BRC_WX - 1) >> 4, cz1 = (oz + BRC_WZ - 1) >> 4;
    struct chunk *cmap[5][5];
    memset(cmap, 0, sizeof(cmap));
    for (int cz = cz0; cz <= cz1; cz++) {
        for (int cx = cx0; cx <= cx1; cx++) {
            cmap[cz - cz0][cx - cx0] = world_chunk(w, cx, cz);
        }
    }

    for (int y = 0; y < BRC_WY; y++) {
        int wy = oy + y;
        for (int z = 0; z < BRC_WZ; z++) {
            int wz = oz + z;
            int cz = (wz >> 4) - cz0;
            int lz = wz & 15;
            for (int x = 0; x < BRC_WX; x++) {
                int wx = ox + x;
                int cx = (wx >> 4) - cx0;
                int lx = wx & 15;
                const struct chunk *c = cmap[cz][cx];
                uint8_t b = (uint8_t)(chunk_get_block(c, lx, wy, lz) & 0xFF);
                uint8_t m = (uint8_t)(chunk_get_meta(c, lx, wy, lz) & 0x0F);
                int idx = blk_idx(x, y, z);
                blocks[idx] = b;
                meta[idx] = m;
                if (brc_blk_is_solid(b)) {
                    solid_bits[idx >> 5] |= (1u << (idx & 31));
                }
                if (b == BRC_BLK_LAVA && m == 0) {
                    lava_cnt++;
                }
            }
        }
    }
    *out_lava_sources = lava_cnt;
    return 1;
}

static int is_safe_stand_ground(uint8_t b) {
    if (!brc_blk_is_solid(b)) return 0;
    /* Avoid leaves, cactus, ice, magma-like hazards or fragile blocks as spawn pad */
    if (b == 18 || b == 81 || b == 79 || b == 161 || b == 85 || b == 113) return 0;
    return 1;
}

static int has_clear_los(const uint8_t *blocks, double ex, double ey, double ez,
                         double tx, double ty, double tz) {
    double dx = tx - ex, dy = ty - ey, dz = tz - ez;
    double dist = sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 0.25) return 1;
    int steps = (int)ceil(dist * 4.0);
    for (int s = 1; s < steps; s++) {
        double t = (double)s / (double)steps;
        int qx = (int)floor(ex + dx * t);
        int qy = (int)floor(ey + dy * t);
        int qz = (int)floor(ez + dz * t);
        if (qx < 0 || qx >= BRC_WX || qy < 0 || qy >= BRC_WY || qz < 0 || qz >= BRC_WZ) return 0;
        uint8_t b = blocks[blk_idx(qx, qy, qz)];
        if (brc_blk_is_solid(b)) return 0;
    }
    return 1;
}

static int find_spawn_near_lava(const uint8_t *blocks, float lava_x, float lava_y, float lava_z,
                                float *out_sx, float *out_sy, float *out_sz,
                                float *out_yaw, float *out_pitch) {
    int lxi = (int)floorf(lava_x);
    int lyi = (int)floorf(lava_y);
    int lzi = (int)floorf(lava_z);

    int best_found = 0;
    double best_score = -1e18;
    float best_sx = 0.0f, best_sy = 0.0f, best_sz = 0.0f;

    for (int dz = -7; dz <= 7; dz++) {
        for (int dx = -7; dx <= 7; dx++) {
            int bx = lxi + dx;
            int bz = lzi + dz;
            if (bx < 4 || bx >= BRC_WX - 4 || bz < 4 || bz >= BRC_WZ - 4) continue;

            for (int dy = -3; dy <= 4; dy++) {
                int by = lyi + dy;
                if (by < 2 || by >= BRC_WY - 4) continue;

                double sx = (double)bx + 0.5;
                double sy = (double)by;
                double sz = (double)bz + 0.5;
                double ddx = sx - (double)lava_x;
                double ddy = sy - (double)lava_y;
                double ddz = sz - (double)lava_z;
                double d3 = sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
                double dh = sqrt(ddx * ddx + ddz * ddz);
                if (d3 < 3.0 || d3 > 6.0 || dh < 2.5) continue;

                uint8_t ground = blocks[blk_idx(bx, by - 1, bz)];
                if (!is_safe_stand_ground(ground)) continue;
                if (blocks[blk_idx(bx, by, bz)] != BRC_BLK_AIR) continue;
                if (blocks[blk_idx(bx, by + 1, bz)] != BRC_BLK_AIR) continue;

                /* Ensure immediate 4-neighbors around feet and ground are not lava */
                int lava_adj = 0;
                const int offs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                for (int k = 0; k < 4; k++) {
                    uint8_t nb0 = blocks[blk_idx(bx + offs[k][0], by, bz + offs[k][1])];
                    uint8_t nb1 = blocks[blk_idx(bx + offs[k][0], by - 1, bz + offs[k][1])];
                    if (nb0 == BRC_BLK_LAVA || nb0 == BRC_BLK_FLOW_LAVA ||
                        nb1 == BRC_BLK_LAVA || nb1 == BRC_BLK_FLOW_LAVA) {
                        lava_adj++;
                    }
                }
                if (lava_adj > 0) continue;

                int jump_air = (blocks[blk_idx(bx, by + 2, bz)] == BRC_BLK_AIR) ? 1 : 0;
                int los = has_clear_los(blocks, sx, sy + 1.62, sz,
                                        (double)lava_x, (double)lava_y + 0.5, (double)lava_z);

                /* Prefer flat bank 1 block above lava, ~4.2 blocks away, clear LOS & jump clearance */
                double score = 0.0;
                score += los ? 50.0 : -50.0;
                score += jump_air ? 10.0 : 0.0;
                score -= fabs(d3 - 4.2) * 5.0;
                score -= fabs((double)(by - (lyi + 1))) * 8.0;

                if (!best_found || score > best_score) {
                    best_found = 1;
                    best_score = score;
                    best_sx = (float)sx;
                    best_sy = (float)sy;
                    best_sz = (float)sz;
                }
            }
        }
    }

    if (!best_found) return 0;

    double dx = (double)lava_x - (double)best_sx;
    double dy = ((double)lava_y + 0.5) - ((double)best_sy + 1.62);
    double dz = (double)lava_z - (double)best_sz;
    double horiz = sqrt(dx * dx + dz * dz);
    float yaw = (float)(atan2(-dx, dz) * (180.0 / M_PI));
    float pitch = (float)(atan2(-dy, horiz) * (180.0 / M_PI));

    *out_sx = best_sx;
    *out_sy = best_sy;
    *out_sz = best_sz;
    *out_yaw = yaw;
    *out_pitch = pitch;
    return 1;
}

static int find_best_pool_in_snapshot(struct snapshot *s, const char *source_name,
                                      int min_lava, int require_surface,
                                      brc_seed_entry_t *out_entry, int *out_is_surface,
                                      int *out_cluster_size) {
    double px = 0.0, py = 64.0, pz = 0.0;
    int pdim = 0;
    get_player_pos(s, &px, &py, &pz, &pdim);
    if (pdim != 0) return 0;

    lava_pt_t *pts = (lava_pt_t *)malloc(MAX_LAVA_PTS * sizeof(lava_pt_t));
    if (!pts) return 0;
    int npts = 0;

    int min_cx = INT_MAX, max_cx = INT_MIN, min_cz = INT_MAX, max_cz = INT_MIN;

    for (int i = 0; i < s->nchunks; i++) {
        struct snap_chunk *sc = &s->chunks[i];
        if (sc->dim != 0 || sc->saved || !sc->live) continue;
        if (sc->cx < min_cx) min_cx = sc->cx;
        if (sc->cx > max_cx) max_cx = sc->cx;
        if (sc->cz < min_cz) min_cz = sc->cz;
        if (sc->cz > max_cz) max_cz = sc->cz;

        const struct chunk *c = sc->live;
        for (int b = 0; b < 16; b++) {
            const struct chunk_sec *sec = chunk_sec_at(c, b);
            if (!sec) continue;
            for (int cell = 0; cell < SEC_CELLS; cell++) {
                if (sec->ids[cell] != BRC_BLK_LAVA) continue;
                int lx = (cell >> 8) & 15;
                int lz = (cell >> 4) & 15;
                int ly = cell & 15;
                int wy = (b << 4) | ly;
                if (wy < 2 || wy > 245) continue;
                if (require_surface && wy < 48) continue;
                if ((chunk_get_meta(c, lx, wy, lz) & 0x0F) != 0) continue;
                if (chunk_get_block(c, lx, wy + 1, lz) != BRC_BLK_AIR) continue;

                /* Compute headroom and whether sky-open */
                int head = 0;
                for (int hy = wy + 1; hy < 256 && head < 16; hy++) {
                    if (chunk_get_block(c, lx, hy, lz) == BRC_BLK_AIR) head++;
                    else break;
                }
                int sky = 1;
                if (wy < 48) {
                    sky = 0;
                } else {
                    for (int hy = wy + 1; hy < 256; hy++) {
                        uint8_t ab = (uint8_t)(chunk_get_block(c, lx, hy, lz) & 0xFF);
                        if (brc_blk_is_solid(ab) && ab != 18 && ab != 161) {
                            sky = 0;
                            break;
                        }
                    }
                }
                if (require_surface && !sky) continue;

                if (npts < MAX_LAVA_PTS) {
                    pts[npts].x = (sc->cx << 4) + lx;
                    pts[npts].y = wy;
                    pts[npts].z = (sc->cz << 4) + lz;
                    pts[npts].sky_open = (uint8_t)sky;
                    pts[npts].headroom = (uint8_t)head;
                    npts++;
                }
            }
        }
    }

    if (npts < min_lava) {
        free(pts);
        return 0;
    }

    /* Cluster connected/local lava source points (|dx|<=2, |dz|<=2, |dy|<=1) */
    uint8_t *vis = (uint8_t *)calloc((size_t)npts, 1);
    int *queue = (int *)malloc((size_t)npts * sizeof(int));
    int *members = (int *)malloc((size_t)npts * sizeof(int));
    if (!vis || !queue || !members) {
        free(pts); free(vis); free(queue); free(members);
        return 0;
    }

    int best_ok = 0;
    double best_pool_score = -1e18;
    brc_seed_entry_t best_entry;
    int best_surface = 0;
    int best_cluster = 0;
    memset(&best_entry, 0, sizeof(best_entry));

    brc_seed_entry_t *trial = (brc_seed_entry_t *)malloc(sizeof(brc_seed_entry_t));
    if (!trial) {
        free(pts); free(vis); free(queue); free(members);
        return 0;
    }

    for (int i = 0; i < npts; i++) {
        if (vis[i]) continue;
        int qh = 0, qt = 0, nm = 0;
        vis[i] = 1;
        queue[qt++] = i;
        while (qh < qt) {
            int u = queue[qh++];
            members[nm++] = u;
            int ux = pts[u].x, uy = pts[u].y, uz = pts[u].z;
            for (int v = 0; v < npts; v++) {
                if (vis[v]) continue;
                int dx = pts[v].x - ux;
                int dy = pts[v].y - uy;
                int dz = pts[v].z - uz;
                if (dx >= -2 && dx <= 2 && dz >= -2 && dz <= 2 && dy >= -1 && dy <= 1) {
                    vis[v] = 1;
                    queue[qt++] = v;
                }
            }
        }

        if (nm < min_lava) continue;

        double sx = 0.0, sy = 0.0, sz = 0.0;
        int sky_cnt = 0, head_sum = 0;
        for (int k = 0; k < nm; k++) {
            const lava_pt_t *p = &pts[members[k]];
            sx += p->x; sy += p->y; sz += p->z;
            sky_cnt += p->sky_open;
            head_sum += p->headroom;
        }
        double cx = sx / nm, cy = sy / nm, cz = sz / nm;

        /* Pick representative actual lava source block closest to centroid */
        int rep_idx = members[0];
        double rep_d2 = 1e30;
        for (int k = 0; k < nm; k++) {
            const lava_pt_t *p = &pts[members[k]];
            double ddx = p->x - cx, ddy = p->y - cy, ddz = p->z - cz;
            double d2 = ddx * ddx + ddy * ddy + ddz * ddz;
            if (d2 < rep_d2) { rep_d2 = d2; rep_idx = members[k]; }
        }
        int rx = pts[rep_idx].x, ry = pts[rep_idx].y, rz = pts[rep_idx].z;

        /* Determine bubble origin centered on representative lava block, shifted if needed to stay loaded */
        int ox = rx - 32;
        int oz = rz - 32;
        int oy = ry - 10;
        if (oy < 0) oy = 0;
        if (oy > 256 - BRC_WY) oy = 256 - BRC_WY;

        /* Clamp origin inside loaded chunk interior while keeping lava within [12..52] */
        int min_wx = min_cx << 4;
        int max_wx = ((max_cx + 1) << 4) - BRC_WX;
        int min_wz = min_cz << 4;
        int max_wz = ((max_cz + 1) << 4) - BRC_WZ;
        if (ox < min_wx) ox = min_wx;
        if (ox > max_wx) ox = max_wx;
        if (oz < min_wz) oz = min_wz;
        if (oz > max_wz) oz = max_wz;

        int local_lx = rx - ox;
        int local_ly = ry - oy;
        int local_lz = rz - oz;
        if (local_lx < 10 || local_lx > BRC_WX - 11 ||
            local_lz < 10 || local_lz > BRC_WZ - 11 ||
            local_ly < 3 || local_ly > BRC_WY - 10) {
            continue;
        }

        if (!bubble_chunks_loaded(&s->world, ox, oy, oz)) continue;

        memset(trial, 0, sizeof(*trial));
        int32_t total_lava = 0;
        if (!extract_bubble(&s->world, ox, oy, oz, trial->blocks, trial->meta, trial->solid_bits, &total_lava)) {
            continue;
        }
        if (total_lava < min_lava) continue;

        float lava_fx = (float)local_lx + 0.5f;
        float lava_fy = (float)local_ly;
        float lava_fz = (float)local_lz + 0.5f;

        float spx = 0.0f, spy = 0.0f, spz = 0.0f, yaw = 0.0f, pitch = 0.0f;
        if (!find_spawn_near_lava(trial->blocks, lava_fx, lava_fy, lava_fz,
                                  &spx, &spy, &spz, &yaw, &pitch)) {
            continue;
        }

        int is_surf = (sky_cnt * 2 >= nm && ry >= 48);
        double avg_head = (double)head_sum / (double)nm;
        double dp = sqrt((rx - px) * (rx - px) + (rz - pz) * (rz - pz));

        double score = 0.0;
        if (is_surf) score += 1000.0;
        if (nm >= 10) score += 200.0;
        if (avg_head >= 6.0) score += 100.0;
        score += (double)(nm > 40 ? 40 : nm) * 3.0;
        score -= dp * 0.5;

        if (!best_ok || score > best_pool_score) {
            best_ok = 1;
            best_pool_score = score;
            best_surface = is_surf;
            best_cluster = nm;

            trial->origin_x = ox;
            trial->origin_y = oy;
            trial->origin_z = oz;
            trial->spawn_x = spx;
            trial->spawn_y = spy;
            trial->spawn_z = spz;
            trial->spawn_yaw = yaw;
            trial->spawn_pitch = pitch;
            trial->lava_x = lava_fx;
            trial->lava_y = lava_fy;
            trial->lava_z = lava_fz;
            trial->lava_sources = total_lava;

            /* Populate speedrun portal hotbar kit */
            trial->hotbar_item[0] = BRC_ITEM_BUCKET_WATER; trial->hotbar_cnt[0] = 1;
            trial->hotbar_item[1] = BRC_ITEM_BUCKET_EMPTY; trial->hotbar_cnt[1] = 1;
            trial->hotbar_item[2] = BRC_ITEM_COBBLE;       trial->hotbar_cnt[2] = 64;
            trial->hotbar_item[3] = BRC_ITEM_FLINT_STEEL;  trial->hotbar_cnt[3] = 1;
            trial->hotbar_item[4] = BRC_ITEM_IRON_PICK;    trial->hotbar_cnt[4] = 1;
            for (int sl = 5; sl < 9; sl++) {
                trial->hotbar_item[sl] = BRC_ITEM_NONE;
                trial->hotbar_cnt[sl] = 0;
            }
            trial->sel_slot = 0;
            snprintf(trial->source_name, sizeof(trial->source_name), "%.63s", source_name);

            best_entry = *trial;
        }
    }

    free(trial);
    free(pts);
    free(vis);
    free(queue);
    free(members);

    if (best_ok) {
        *out_entry = best_entry;
        *out_is_surface = best_surface;
        *out_cluster_size = best_cluster;
    }
    return best_ok;
}

static int seed_seen(const int64_t *seeds, int n, int64_t val) {
    for (int i = 0; i < n; i++) {
        if (seeds[i] == val) return 1;
    }
    return 0;
}

static int resolve_source_path(const char **in_dirs, int n_in, const char *source_name,
                               char out_path[MAX_PATH_LEN]) {
    for (int d = 0; d < n_in; d++) {
        const char *root = in_dirs[d];
        char cand1[MAX_PATH_LEN], cand2[MAX_PATH_LEN], mpath[MAX_PATH_LEN + 32];

        /* Check root directly */
        char rname[64];
        derive_source_name(root, rname);
        snprintf(mpath, sizeof(mpath), "%s/manifest.json", root);
        if (strcmp(rname, source_name) == 0 && file_exists(mpath)) {
            snprintf(out_path, MAX_PATH_LEN, "%s", root);
            return 1;
        }

        /* Check root/source_name */
        snprintf(cand1, sizeof(cand1), "%s/%s", root, source_name);
        snprintf(mpath, sizeof(mpath), "%s/manifest.json", cand1);
        if (file_exists(mpath)) {
            snprintf(out_path, MAX_PATH_LEN, "%s", cand1);
            return 1;
        }

        /* Check root/source_name/start */
        snprintf(cand2, sizeof(cand2), "%s/%s/start", root, source_name);
        snprintf(mpath, sizeof(mpath), "%s/manifest.json", cand2);
        if (file_exists(mpath)) {
            snprintf(out_path, MAX_PATH_LEN, "%s", cand2);
            return 1;
        }
    }
    return 0;
}

static int verify_pack(const char *pack_path, const char **in_dirs, int n_in) {
    FILE *fp = fopen(pack_path, "rb");
    if (!fp) {
        fprintf(stderr, "br_harvest --verify: cannot open '%s': %s\n", pack_path, strerror(errno));
        return 1;
    }

    brc_seed_file_hdr_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) {
        fprintf(stderr, "br_harvest --verify: short read on header\n");
        fclose(fp);
        return 1;
    }
    if (hdr.magic != BRC_SEED_MAGIC) {
        fprintf(stderr, "br_harvest --verify: bad magic 0x%08x (expected 0x%08x)\n",
                hdr.magic, BRC_SEED_MAGIC);
        fclose(fp);
        return 1;
    }
    if (hdr.version != 1 || hdr.num_seeds == 0) {
        fprintf(stderr, "br_harvest --verify: invalid version=%u num_seeds=%u\n",
                hdr.version, hdr.num_seeds);
        fclose(fp);
        return 1;
    }

    struct stat st;
    if (fstat(fileno(fp), &st) != 0) {
        fclose(fp);
        return 1;
    }
    size_t expected_sz = sizeof(brc_seed_file_hdr_t) + (size_t)hdr.num_seeds * sizeof(brc_seed_entry_t);
    if ((size_t)st.st_size != expected_sz) {
        fprintf(stderr, "br_harvest --verify: file size %zu != expected %zu\n",
                (size_t)st.st_size, expected_sz);
        fclose(fp);
        return 1;
    }

    printf("VERIFY BRS1 '%s': version=%u num_seeds=%u size=%zu bytes\n",
           pack_path, hdr.version, hdr.num_seeds, (size_t)st.st_size);

    brc_seed_entry_t *entry = (brc_seed_entry_t *)malloc(sizeof(brc_seed_entry_t));
    int32_t *lava_counts = (int32_t *)malloc(hdr.num_seeds * sizeof(int32_t));
    int64_t *unique_seeds = (int64_t *)malloc(hdr.num_seeds * sizeof(int64_t));
    if (!entry || !lava_counts || !unique_seeds) {
        free(entry); free(lava_counts); free(unique_seeds); fclose(fp);
        return 1;
    }
    int n_unique = 0;

    uint64_t total_block_mismatches = 0;
    uint64_t total_meta_mismatches = 0;
    uint64_t total_solid_mismatches = 0;
    uint64_t total_unloaded_cells = 0;
    uint64_t total_lava_count_mismatches = 0;
    uint64_t total_spawn_errors = 0;
    uint32_t seeds_below_10 = 0;
    uint64_t total_blocks_checked = 0;

    for (uint32_t i = 0; i < hdr.num_seeds; i++) {
        if (fread(entry, sizeof(*entry), 1, fp) != 1) {
            fprintf(stderr, "br_harvest --verify: short read on seed %u\n", i);
            free(entry); free(lava_counts); free(unique_seeds); fclose(fp);
            return 1;
        }

        char snap_path[MAX_PATH_LEN];
        if (!resolve_source_path(in_dirs, n_in, entry->source_name, snap_path)) {
            fprintf(stderr, "br_harvest --verify: seed %u source '%s' not found in --in dirs\n",
                    i, entry->source_name);
            free(entry); free(lava_counts); free(unique_seeds); fclose(fp);
            return 1;
        }

        struct snapshot snap;
        if (!snapshot_load(&snap, snap_path)) {
            fprintf(stderr, "br_harvest --verify: snapshot_load failed for '%s'\n", snap_path);
            free(entry); free(lava_counts); free(unique_seeds); fclose(fp);
            return 1;
        }

        if (!seed_seen(unique_seeds, n_unique, snap.seed)) {
            unique_seeds[n_unique++] = snap.seed;
        }

        uint64_t blk_mm = 0, meta_mm = 0, solid_mm = 0, unloaded = 0;
        int32_t recounted_lava = 0;

        for (int y = 0; y < BRC_WY; y++) {
            int wy = entry->origin_y + y;
            for (int z = 0; z < BRC_WZ; z++) {
                int wz = entry->origin_z + z;
                for (int x = 0; x < BRC_WX; x++) {
                    int wx = entry->origin_x + x;
                    int idx = blk_idx(x, y, z);
                    uint8_t want_m = 0;
                    int want_b = loaded_block(&snap.world, wx, wy, wz, &want_m);
                    if (want_b < 0) {
                        unloaded++;
                        blk_mm++;
                        meta_mm++;
                        continue;
                    }
                    uint8_t got_b = entry->blocks[idx];
                    uint8_t got_m = entry->meta[idx];
                    if (got_b != (uint8_t)want_b) blk_mm++;
                    if (got_m != want_m) meta_mm++;
                    uint32_t want_solid = brc_blk_is_solid(got_b) ? 1u : 0u;
                    uint32_t got_solid = (entry->solid_bits[idx >> 5] >> (idx & 31)) & 1u;
                    if (got_solid != want_solid) solid_mm++;
                    if (got_b == BRC_BLK_LAVA && got_m == 0) recounted_lava++;
                    total_blocks_checked++;
                }
            }
        }

        int spawn_err = 0;
        int sbx = (int)floorf(entry->spawn_x);
        int sby = (int)floorf(entry->spawn_y);
        int sbz = (int)floorf(entry->spawn_z);
        if (sbx < 1 || sbx >= BRC_WX - 1 || sby < 1 || sby >= BRC_WY - 2 || sbz < 1 || sbz >= BRC_WZ - 1) {
            spawn_err = 1;
        } else {
            uint8_t g = entry->blocks[blk_idx(sbx, sby - 1, sbz)];
            uint8_t f0 = entry->blocks[blk_idx(sbx, sby, sbz)];
            uint8_t f1 = entry->blocks[blk_idx(sbx, sby + 1, sbz)];
            double ddx = (double)entry->spawn_x - (double)entry->lava_x;
            double ddy = (double)entry->spawn_y - (double)entry->lava_y;
            double ddz = (double)entry->spawn_z - (double)entry->lava_z;
            double dist = sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
            if (!brc_blk_is_solid(g) || brc_blk_is_solid(f0) || brc_blk_is_solid(f1) || dist < 3.0 || dist > 6.0) {
                spawn_err = 1;
            }
        }

        if (recounted_lava != entry->lava_sources) total_lava_count_mismatches++;
        if (entry->lava_sources < 10) seeds_below_10++;
        lava_counts[i] = entry->lava_sources;

        total_block_mismatches += blk_mm;
        total_meta_mismatches += meta_mm;
        total_solid_mismatches += solid_mm;
        total_unloaded_cells += unloaded;
        total_spawn_errors += (uint64_t)spawn_err;

        printf("  [%02u] %-36s seed=%-12lld origin=(%5d,%3d,%5d) lava_src=%4d spawn=(%.1f,%.1f,%.1f) blk_mm=%llu meta_mm=%llu solid_mm=%llu%s\n",
               i, entry->source_name, (long long)snap.seed,
               entry->origin_x, entry->origin_y, entry->origin_z,
               entry->lava_sources,
               entry->spawn_x, entry->spawn_y, entry->spawn_z,
               (unsigned long long)blk_mm, (unsigned long long)meta_mm, (unsigned long long)solid_mm,
               spawn_err ? " SPAWN_ERR" : "");

        snapshot_free(&snap);
    }

    qsort(lava_counts, hdr.num_seeds, sizeof(int32_t), cmp_int32);
    int32_t min_l = lava_counts[0];
    int32_t med_l = lava_counts[hdr.num_seeds / 2];
    int32_t max_l = lava_counts[hdr.num_seeds - 1];

    printf("VERIFY SUMMARY: seeds=%u unique_world_seeds=%d blocks_checked=%llu "
           "block_mismatches=%llu meta_mismatches=%llu solid_mismatches=%llu "
           "unloaded=%llu lava_cnt_mm=%llu spawn_err=%llu "
           "lava_sources(min/med/max)=%d/%d/%d below_10=%u\n",
           hdr.num_seeds, n_unique, (unsigned long long)total_blocks_checked,
           (unsigned long long)total_block_mismatches,
           (unsigned long long)total_meta_mismatches,
           (unsigned long long)total_solid_mismatches,
           (unsigned long long)total_unloaded_cells,
           (unsigned long long)total_lava_count_mismatches,
           (unsigned long long)total_spawn_errors,
           min_l, med_l, max_l, seeds_below_10);

    free(entry);
    free(lava_counts);
    free(unique_seeds);
    fclose(fp);

    return (total_block_mismatches == 0 && total_meta_mismatches == 0 &&
            total_solid_mismatches == 0 && total_unloaded_cells == 0 &&
            total_lava_count_mismatches == 0 && total_spawn_errors == 0) ? 0 : 1;
}

static void usage(void) {
    fprintf(stderr,
            "usage: br_harvest --in DIR [--in DIR2 ...] [--out FILE] [--max N] [--min-lava N] [--verify]\n");
}

int main(int argc, char **argv) {
    const char *in_dirs[MAX_IN_DIRS];
    int n_in = 0;
    const char *out_path = "out/bedrock/portal_seeds.bin";
    int max_seeds = 64;
    int min_lava = 10;
    int verify_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--in") == 0 && i + 1 < argc) {
            if (n_in < MAX_IN_DIRS) in_dirs[n_in++] = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--max") == 0 && i + 1 < argc) {
            max_seeds = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--min-lava") == 0 && i + 1 < argc) {
            min_lava = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verify") == 0) {
            verify_only = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "br_harvest: unknown arg '%s'\n", argv[i]);
            usage();
            return 2;
        }
    }

    if (n_in == 0) {
        fprintf(stderr, "br_harvest: at least one --in DIR is required\n");
        usage();
        return 2;
    }

    if (verify_only) {
        return verify_pack(out_path, in_dirs, n_in);
    }

    if (max_seeds <= 0) {
        fprintf(stderr, "br_harvest: --max must be > 0\n");
        return 2;
    }

    double t0 = now_sec();
    cand_t *cands = (cand_t *)malloc(MAX_CANDS * sizeof(cand_t));
    brc_seed_entry_t *entries = (brc_seed_entry_t *)malloc((size_t)max_seeds * sizeof(brc_seed_entry_t));
    int64_t *harvested_seeds = (int64_t *)malloc((size_t)max_seeds * sizeof(int64_t));
    int32_t *lava_counts = (int32_t *)malloc((size_t)max_seeds * sizeof(int32_t));
    if (!cands || !entries || !harvested_seeds || !lava_counts) {
        fprintf(stderr, "br_harvest: OOM\n");
        free(cands); free(entries); free(harvested_seeds); free(lava_counts);
        return 1;
    }

    int nc = collect_candidates(in_dirs, n_in, cands, MAX_CANDS);
    printf("br_harvest: discovered %d snapshot candidates across %d --in dir(s); target=%d distinct seeds (min_lava=%d)\n",
           nc, n_in, max_seeds, min_lava);

    int nh = 0;
    int scanned = 0;
    int surface_hits = 0;

    /* Pass 1: Strict surface sky-open lava pools (>= min_lava)
     * Pass 2 (fallback if < max_seeds): Any loaded-chunk lava pools (>= min_lava)
     * Pass 3 (fallback if still < max_seeds): Any loaded-chunk lava pools (>= 6) */
    const int pass_req_surf[3] = {1, 0, 0};
    const int pass_min_lava[3] = {min_lava, min_lava, 6};

    for (int pass = 0; pass < 3 && nh < max_seeds; pass++) {
        printf("br_harvest: pass %d (require_surface=%d, min_lava=%d), harvested=%d/%d\n",
               pass + 1, pass_req_surf[pass], pass_min_lava[pass], nh, max_seeds);
        for (int i = 0; i < nc && nh < max_seeds; i++) {
            /* Fast skip if this candidate's seed hint is already harvested */
            if (cands[i].has_seed_hint && seed_seen(harvested_seeds, nh, cands[i].seed_hint)) {
                continue;
            }
            struct snapshot snap;
            if (!snapshot_load(&snap, cands[i].path)) continue;
            scanned++;

            if (seed_seen(harvested_seeds, nh, snap.seed)) {
                snapshot_free(&snap);
                continue;
            }

            int is_surf = 0, csize = 0;
            if (find_best_pool_in_snapshot(&snap, cands[i].source_name,
                                           pass_min_lava[pass], pass_req_surf[pass],
                                           &entries[nh], &is_surf, &csize)) {
                harvested_seeds[nh] = snap.seed;
                lava_counts[nh] = entries[nh].lava_sources;
                if (is_surf) surface_hits++;
                printf("  [harvest %02d/%02d] %-36s seed=%-12lld %s cluster=%3d bubble_lava=%4d origin=(%5d,%3d,%5d) spawn=(%.1f,%.1f,%.1f)\n",
                       nh + 1, max_seeds, entries[nh].source_name, (long long)snap.seed,
                       is_surf ? "SURFACE" : "CAVE   ",
                       csize, entries[nh].lava_sources,
                       entries[nh].origin_x, entries[nh].origin_y, entries[nh].origin_z,
                       entries[nh].spawn_x, entries[nh].spawn_y, entries[nh].spawn_z);
                fflush(stdout);
                nh++;
            }
            snapshot_free(&snap);
        }
    }

    if (nh == 0) {
        fprintf(stderr, "br_harvest: ERROR harvested 0 seeds!\n");
        free(cands); free(entries); free(harvested_seeds); free(lava_counts);
        return 1;
    }

    /* Write BRS1 binary pack */
    FILE *fp = fopen(out_path, "wb");
    if (!fp) {
        fprintf(stderr, "br_harvest: cannot open '%s' for write: %s\n", out_path, strerror(errno));
        free(cands); free(entries); free(harvested_seeds); free(lava_counts);
        return 1;
    }

    brc_seed_file_hdr_t hdr;
    hdr.magic = BRC_SEED_MAGIC;
    hdr.version = 1;
    hdr.num_seeds = (uint32_t)nh;
    hdr.reserved = 0;

    int write_ok = (fwrite(&hdr, sizeof(hdr), 1, fp) == 1) &&
                   (fwrite(entries, sizeof(brc_seed_entry_t), (size_t)nh, fp) == (size_t)nh);
    fclose(fp);
    if (!write_ok) {
        fprintf(stderr, "br_harvest: write failed for '%s'\n", out_path);
        free(cands); free(entries); free(harvested_seeds); free(lava_counts);
        return 1;
    }

    qsort(lava_counts, (size_t)nh, sizeof(int32_t), cmp_int32);
    size_t total_bytes = sizeof(brc_seed_file_hdr_t) + (size_t)nh * sizeof(brc_seed_entry_t);
    printf("br_harvest: DONE wrote %d distinct real 1.7.10 seeds (%d surface) to '%s' (%zu bytes) "
           "in %.2fs (scanned %d snapshots); lava_sources min/med/max=%d/%d/%d\n",
           nh, surface_hits, out_path, total_bytes, now_sec() - t0, scanned,
           lava_counts[0], lava_counts[nh / 2], lava_counts[nh - 1]);

    free(cands);
    free(entries);
    free(harvested_seeds);
    free(lava_counts);
    return (nh >= max_seeds) ? 0 : 1;
}
