/* nether_portal_make: Teleporter.makePortal search + obsidian/portal placement on nether terrain.
 *
 * PORT TARGET: net/minecraft/world/Teleporter.java makePortal (MC 1.11.2)
 * INTERNAL verify (CPU==CUDA). Builds a 3x3 nether chunk slice via READ-ONLY chunk_provider_nether.h,
 * then runs makePortal for fixed entity positions. READ-ONLY deps: nether_portal.h (portal axis meta),
 * chunk_provider_nether.h, block_props_table.h. CUT: destinationCoordinateCache, notifyNeighbors,
 * pigman spawn, end-dimension exit platform. */
#ifndef MC_NETHER_PORTAL_MAKE_H
#define MC_NETHER_PORTAL_MAKE_H

#include <string.h>
#include "mc.h"
#include "mc_rng.h"
#include "mc_math.h"
#include "mc_blocks.h"
#include "block_props_table.h"
#include "chunk_provider_nether.h"
#include "nether_portal.h"

#define NPM_WX 48
#define NPM_WY 128
#define NPM_WZ 48
#define NPM_VOL (NPM_WX * NPM_WY * NPM_WZ)
#define NPM_ACTUAL_HEIGHT 256
#define NPM_NUM_SCENARIOS 6

typedef struct {
    u16 blocks[NPM_VOL];
} NpmWorld;

typedef struct {
    int found;
    int ax, ay, az;
    int orient;
    int portal_count;
    int obsidian_count;
    int rand_orient_base;
} NpmResult;

typedef struct {
    i64 terrain_seed;
    double ex, ey, ez;
} NpmScenario;

MC_HD static inline u16 npm_cpn_to_vanilla(u16 id) {
    switch (id) {
        case CPN_AIR: return 0;
        case CPN_STONE: return 1;
        case CPN_GRASS: return 2;
        case CPN_DIRT: return 3;
        case CPN_BEDROCK: return 7;
        case CPN_LAVA: return 10;
        case CPN_FLOWING_LAVA: return 11;
        case CPN_GRAVEL: return 13;
        case CPN_NETHERRACK: return 87;
        case CPN_SOUL_SAND: return 88;
        default: return id;
    }
}

MC_HD static inline int npm_idx(int x, int y, int z) {
    return (y * NPM_WZ + z) * NPM_WX + x;
}

MC_HD static inline int npm_in(int x, int y, int z) {
    return x >= 0 && x < NPM_WX && y >= 0 && y < NPM_WY && z >= 0 && z < NPM_WZ;
}

MC_HD static inline int npm_block_id(const NpmWorld *w, int x, int y, int z) {
    if (!npm_in(x, y, z)) return BLK_AIR;
    return mc_state_id(w->blocks[npm_idx(x, y, z)]);
}

MC_HD static inline void npm_set(NpmWorld *w, int x, int y, int z, u16 id) {
    if (npm_in(x, y, z)) w->blocks[npm_idx(x, y, z)] = id;
}

MC_HD static inline int npm_floor(double v) {
    int i = (int)v;
    return v < (double)i ? i - 1 : i;
}

MC_HD static inline int npm_clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

MC_HD static inline int npm_is_air(const NpmWorld *w, int x, int y, int z) {
    if (!npm_in(x, y, z)) return 0;
    return npm_block_id(w, x, y, z) == BLK_AIR;
}

MC_HD static inline int npm_is_solid(const NpmWorld *w, int x, int y, int z) {
    int id = npm_block_id(w, x, y, z);
    return (mc_bpt_props(id).flags & BF_SOLID) != 0;
}

MC_HD static inline void npm_build_terrain(NpmWorld *w, CpnHellScratch *sc, const McSinTable *st,
                                          CpnHellNoise *noise, i64 seed) {
    CpnPrimer primer;
    int cx, cz, lx, ly, lz, wx, wz, i;
    u16 air = mc_state(BLK_AIR, 0);

    for (i = 0; i < NPM_VOL; ++i) w->blocks[i] = air;
    for (cx = -1; cx <= 1; ++cx) {
        for (cz = -1; cz <= 1; ++cz) {
            cpn_provide_chunk(&primer, sc, st, noise, seed, cx, cz);
            for (lx = 0; lx < 16; ++lx) {
                wx = (cx + 1) * 16 + lx;
                for (lz = 0; lz < 16; ++lz) {
                    wz = (cz + 1) * 16 + lz;
                    for (ly = 0; ly < NPM_WY; ++ly) {
                        int vid = (int)npm_cpn_to_vanilla((u16)cpn_get(&primer, lx, ly, lz));
                        npm_set(w, wx, ly, wz, mc_state(vid, 0));
                    }
                }
            }
        }
    }
}

MC_HD static inline int npm_volume_ok(const NpmWorld *w, int j2, int j3, int l2,
                                      int l3, int i4, int j4, int k4, int l4) {
    int i5 = j2 + (k4 - 1) * l3 + j4 * i4;
    int j5 = j3 + l4;
    int k5 = l2 + (k4 - 1) * i4 - j4 * l3;
    if (l4 < 0 && !npm_is_solid(w, i5, j5, k5)) return 0;
    if (l4 >= 0 && !npm_is_air(w, i5, j5, k5)) return 0;
    return 1;
}

/* Teleporter.makePortal - search best air column + build 4x4 obsidian frame with portal interior. */
MC_HD static inline int npm_make_portal(NpmWorld *w, JavaRandom *rand, double ex, double ey, double ez,
                                        NpmResult *out) {
    double d0 = -1.0;
    int j = npm_floor(ex);
    int k = npm_floor(ey);
    int l = npm_floor(ez);
    int i1 = j, j1 = k, k1 = l;
    int l1 = 0;
    int i2 = jrand_int_bound(rand, 4);
    int j2, l2, j3, k3, l3, i4, j4, k4, l4;
    int l5, j6, i7, k7, j8, j9, j10, j11, j12, i13, j13;
    int i6, k2, k6, l6, i3;
    int i8, l8, l9, l10, l11, k12;
    int i9, i10, i11, i12;
    int j7, l7, k8;
    u16 portal_state, obs_state;
    int found_before_fallback;

    out->rand_orient_base = i2;

    for (j2 = j - 16; j2 <= j + 16; ++j2) {
        double d1 = (double)j2 + 0.5 - ex;
        for (l2 = l - 16; l2 <= l + 16; ++l2) {
            double d2 = (double)l2 + 0.5 - ez;
            for (j3 = NPM_WY - 1; j3 >= 0; --j3) {
                if (!npm_is_air(w, j2, j3, l2)) continue;
                while (j3 > 0 && npm_is_air(w, j2, j3 - 1, l2)) --j3;

                for (k3 = i2; k3 < i2 + 4; ++k3) {
                    l3 = k3 % 2;
                    i4 = 1 - l3;
                    if (k3 % 4 >= 2) {
                        l3 = -l3;
                        i4 = -i4;
                    }
                    for (j4 = 0; j4 < 3; ++j4) {
                        for (k4 = 0; k4 < 4; ++k4) {
                            int ok = 1;
                            for (l4 = -1; l4 < 4; ++l4) {
                                if (!npm_volume_ok(w, j2, j3, l2, l3, i4, j4, k4, l4)) {
                                    ok = 0;
                                    break;
                                }
                            }
                            if (!ok) continue;

                            {
                                double d5 = (double)j3 + 0.5 - ey;
                                double d7 = d1 * d1 + d5 * d5 + d2 * d2;
                                if (d0 < 0.0 || d7 < d0) {
                                    d0 = d7;
                                    i1 = j2;
                                    j1 = j3;
                                    k1 = l2;
                                    l1 = k3 % 4;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (d0 < 0.0) {
        for (l5 = j - 16; l5 <= j + 16; ++l5) {
            double d3 = (double)l5 + 0.5 - ex;
            for (j6 = l - 16; j6 <= l + 16; ++j6) {
                double d4 = (double)j6 + 0.5 - ez;
                for (i7 = NPM_WY - 1; i7 >= 0; --i7) {
                    if (!npm_is_air(w, l5, i7, j6)) continue;
                    while (i7 > 0 && npm_is_air(w, l5, i7 - 1, j6)) --i7;

                    for (k7 = i2; k7 < i2 + 2; ++k7) {
                        j8 = k7 % 2;
                        j9 = 1 - j8;
                        for (j10 = 0; j10 < 4; ++j10) {
                            for (j11 = -1; j11 < 4; ++j11) {
                                j12 = l5 + (j10 - 1) * j8;
                                i13 = i7 + j11;
                                j13 = j6 + (j10 - 1) * j9;
                                if (j11 < 0 && !npm_is_solid(w, j12, i13, j13)) continue;
                                if (j11 >= 0 && !npm_is_air(w, j12, i13, j13)) continue;

                                {
                                    double d6 = (double)i7 + 0.5 - ey;
                                    double d8 = d3 * d3 + d6 * d6 + d4 * d4;
                                    if (d0 < 0.0 || d8 < d0) {
                                        d0 = d8;
                                        i1 = l5;
                                        j1 = i7;
                                        k1 = j6;
                                        l1 = k7 % 2;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    found_before_fallback = (d0 >= 0.0) ? 1 : 0;

    i6 = i1;
    k2 = j1;
    k6 = k1;
    l6 = l1 % 2;
    i3 = 1 - l6;
    if (l1 % 4 >= 2) {
        l6 = -l6;
        i3 = -i3;
    }

    if (d0 < 0.0) {
        j1 = npm_clamp(j1, 70, NPM_ACTUAL_HEIGHT - 10);
        k2 = j1;
        for (j7 = -1; j7 <= 1; ++j7) {
            for (l7 = 1; l7 < 3; ++l7) {
                for (k8 = -1; k8 < 3; ++k8) {
                    int k9 = i6 + (l7 - 1) * l6 + j7 * i3;
                    int k10 = k2 + k8;
                    int k11 = k6 + (l7 - 1) * i3 - j7 * l6;
                    npm_set(w, k9, k10, k11, mc_state(k8 < 0 ? BLK_OBSIDIAN : BLK_AIR, 0));
                }
            }
        }
    }

    portal_state = mc_state(NP_BLK_PORTAL, l6 == 0 ? 2 : 1);
    obs_state = mc_state(BLK_OBSIDIAN, 0);

    for (i8 = 0; i8 < 4; ++i8) {
        for (l8 = 0; l8 < 4; ++l8) {
            for (l9 = -1; l9 < 4; ++l9) {
                l10 = i6 + (l8 - 1) * l6;
                l11 = k2 + l9;
                k12 = k6 + (l8 - 1) * i3;
                npm_set(w, l10, l11, k12,
                        (l8 == 0 || l8 == 3 || l9 == -1 || l9 == 3) ? obs_state : portal_state);
            }
        }
    }

    out->found = found_before_fallback;
    out->ax = i6;
    out->ay = k2;
    out->az = k6;
    out->orient = l1;
    out->portal_count = 0;
    out->obsidian_count = 0;
    for (i9 = 0; i9 < 4; ++i9) {
        for (i10 = 0; i10 < 4; ++i10) {
            for (i11 = -1; i11 < 4; ++i11) {
                i12 = npm_block_id(w, i6 + (i10 - 1) * l6, k2 + i11, k6 + (i10 - 1) * i3);
                if (i12 == NP_BLK_PORTAL) out->portal_count++;
                else if (i12 == BLK_OBSIDIAN) out->obsidian_count++;
            }
        }
    }
    return 1;
}

MC_HD static inline void npm_scenario_params(int idx, NpmScenario *sc) {
    switch (idx) {
    case 0: sc->terrain_seed = 12345LL; sc->ex = 24.5; sc->ey = 70.0; sc->ez = 24.5; break;
    case 1: sc->terrain_seed = 0LL;     sc->ex = 24.5; sc->ey = 65.0; sc->ez = 24.5; break;
    case 2: sc->terrain_seed = 7LL;     sc->ex = 8.5;  sc->ey = 72.0; sc->ez = 8.5;  break;
    case 3: sc->terrain_seed = 999LL;   sc->ex = 40.5; sc->ey = 68.0; sc->ez = 16.5; break;
    case 4: sc->terrain_seed = 424242LL; sc->ex = 24.5; sc->ey = 80.0; sc->ez = 24.5; break;
    case 5: sc->terrain_seed = 12345LL; sc->ex = 32.5; sc->ey = 70.0; sc->ez = 32.5; break;
    default: sc->terrain_seed = 12345LL; sc->ex = 24.5; sc->ey = 70.0; sc->ez = 24.5; break;
    }
}

MC_HD static inline void npm_run_scenario(int idx, NpmWorld *w, CpnHellScratch *sc,
                                          const McSinTable *st, CpnHellNoise *noise,
                                          NpmResult *r) {
    NpmScenario scn;
    JavaRandom rand;

    npm_scenario_params(idx, &scn);
    cpn_noise_init(noise, scn.terrain_seed);
    npm_build_terrain(w, sc, st, noise, scn.terrain_seed);
    jrand_set(&rand, scn.terrain_seed);
    npm_make_portal(w, &rand, scn.ex, scn.ey, scn.ez, r);
}

MC_HD static inline int npm_emit_result(const NpmResult *r,
                                        void (*emit_u32)(u32, void *), void *ctx) {
    emit_u32((u32)r->found, ctx);
    emit_u32((u32)r->ax, ctx);
    emit_u32((u32)r->ay, ctx);
    emit_u32((u32)r->az, ctx);
    emit_u32((u32)r->orient, ctx);
    emit_u32((u32)r->portal_count, ctx);
    emit_u32((u32)r->obsidian_count, ctx);
    emit_u32((u32)r->rand_orient_base, ctx);
    return 8;
}

#endif /* MC_NETHER_PORTAL_MAKE_H */
