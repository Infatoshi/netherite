/* end_portal: BlockEndPortalFrame + ItemEnderEye eye-insertion state machine.
 * PORT: BlockEndPortalFrame.getOrCreatePortalShape/match, ItemEnderEye.onItemUse.
 * Small fixed grid; deterministic eye order from mc_hash_rng(seed). CPU==CUDA. */
#ifndef MC_END_PORTAL_H
#define MC_END_PORTAL_H

#include "mc.h"
#include "mc_rng.h"
#include "mc_world.h"

enum {
    EP_BLK_END_PORTAL       = 119,
    EP_BLK_END_PORTAL_FRAME = 120,
    EP_FACE_SOUTH = 0,
    EP_FACE_WEST  = 1,
    EP_FACE_NORTH = 2,
    EP_FACE_EAST  = 3
};

#define EP_W 11
#define EP_H 4
#define EP_D 11
#define EP_VOL (EP_W * EP_H * EP_D)
#define EP_NFRAMES 12

/* BlockPattern 5x5 single aisle (?vvv? / >???< / >???< / >???< / ?^^^?). */
enum {
    EP_CELL_ANY = 0,
    EP_CELL_FN  = 1, /* frame eye, facing north */
    EP_CELL_FS  = 2,
    EP_CELL_FW  = 3,
    EP_CELL_FE  = 4
};

typedef struct { int dx, dy, dz; } EpVec3;

MC_HD MC_NOINLINE static u8 ep_pattern_cell(int row, int col) {
    static const u8 p[5][5] = {
        { EP_CELL_ANY, EP_CELL_FN, EP_CELL_FN, EP_CELL_FN, EP_CELL_ANY },
        { EP_CELL_FW,  EP_CELL_ANY, EP_CELL_ANY, EP_CELL_ANY, EP_CELL_FE },
        { EP_CELL_FW,  EP_CELL_ANY, EP_CELL_ANY, EP_CELL_ANY, EP_CELL_FE },
        { EP_CELL_FW,  EP_CELL_ANY, EP_CELL_ANY, EP_CELL_ANY, EP_CELL_FE },
        { EP_CELL_ANY, EP_CELL_FS, EP_CELL_FS, EP_CELL_FS, EP_CELL_ANY }
    };
    return p[row][col];
}

MC_HD MC_NOINLINE static EpVec3 ep_dir(int f) {
    switch (f) {
        case 0: return (EpVec3){ 0, -1, 0 };
        case 1: return (EpVec3){ 0,  1, 0 };
        case 2: return (EpVec3){ 0,  0,-1 };
        case 3: return (EpVec3){ 0,  0, 1 };
        case 4: return (EpVec3){-1,  0, 0 };
        default: return (EpVec3){ 1,  0, 0 };
    }
}

typedef struct {
    u16 blocks[EP_VOL];
    u64 seed;
    i32 eyes_left;
    i32 insert_count;
    i32 activated_step; /* -1 until portal fills */
    int frame_x[EP_NFRAMES];
    int frame_y[EP_NFRAMES];
    int frame_z[EP_NFRAMES];
    u8  frame_face[EP_NFRAMES];
} EpWorld;

MC_HD MC_NOINLINE static int ep_idx(int x, int y, int z) {
    return (y * EP_D + z) * EP_W + x;
}

MC_HD MC_NOINLINE static u16 ep_get(const EpWorld *w, int x, int y, int z) {
    if (x < 0 || x >= EP_W || y < 0 || y >= EP_H || z < 0 || z >= EP_D)
        return mc_state(0, 0);
    return w->blocks[ep_idx(x, y, z)];
}

MC_HD MC_NOINLINE static void ep_set(EpWorld *w, int x, int y, int z, u16 s) {
    if (x < 0 || x >= EP_W || y < 0 || y >= EP_H || z < 0 || z >= EP_D) return;
    w->blocks[ep_idx(x, y, z)] = s;
}

MC_HD MC_NOINLINE static u16 ep_frame_state(int face, int eye) {
    int meta = face & 3;
    if (eye) meta |= 4;
    return mc_state(EP_BLK_END_PORTAL_FRAME, meta);
}

MC_HD MC_NOINLINE static int ep_frame_face(u16 s) {
    if (mc_state_id(s) != EP_BLK_END_PORTAL_FRAME) return -1;
    return mc_state_meta(s) & 3;
}

MC_HD MC_NOINLINE static int ep_frame_eye(u16 s) {
    if (mc_state_id(s) != EP_BLK_END_PORTAL_FRAME) return 0;
    return (mc_state_meta(s) & 4) != 0;
}

MC_HD MC_NOINLINE static int ep_comparator(u16 s) {
    return ep_frame_eye(s) ? 15 : 0;
}

MC_HD MC_NOINLINE static int ep_opposite(int f) {
    switch (f) {
        case 0: return 1; case 1: return 0;
        case 2: return 3; case 3: return 2;
        case 4: return 5; case 5: return 4;
        default: return f;
    }
}

MC_HD MC_NOINLINE static EpVec3 ep_cross(EpVec3 a, EpVec3 b) {
    EpVec3 r;
    r.dx = a.dy * b.dz - a.dz * b.dy;
    r.dy = a.dz * b.dx - a.dx * b.dz;
    r.dz = a.dx * b.dy - a.dy * b.dx;
    return r;
}

MC_HD MC_NOINLINE static void ep_world_at(const EpWorld *w, int ox, int oy, int oz,
                                     int finger, int thumb, int palm, int thumb_off, int finger_off,
                                     int *wx, int *wy, int *wz) {
    EpVec3 f = ep_dir(finger);
    EpVec3 t = ep_dir(thumb);
    EpVec3 p = ep_cross(f, t);
    *wx = ox + t.dx * (-thumb_off) + p.dx * palm + f.dx * finger_off;
    *wy = oy + t.dy * (-thumb_off) + p.dy * palm + f.dy * finger_off;
    *wz = oz + t.dz * (-thumb_off) + p.dz * palm + f.dz * finger_off;
}

MC_HD MC_NOINLINE static int ep_cell_matches(const EpWorld *w, int x, int y, int z, u8 cell) {
    u16 s = ep_get(w, x, y, z);
    if (cell == EP_CELL_ANY) return 1;
    if (mc_state_id(s) != EP_BLK_END_PORTAL_FRAME || !ep_frame_eye(s)) return 0;
    switch (cell) {
        case EP_CELL_FN: return ep_frame_face(s) == EP_FACE_NORTH;
        case EP_CELL_FS: return ep_frame_face(s) == EP_FACE_SOUTH;
        case EP_CELL_FW: return ep_frame_face(s) == EP_FACE_WEST;
        case EP_CELL_FE: return ep_frame_face(s) == EP_FACE_EAST;
        default: return 0;
    }
}

MC_HD MC_NOINLINE static int ep_check_pattern_at(const EpWorld *w, int ox, int oy, int oz,
                                            int finger, int thumb, int *ftl_x, int *ftl_y, int *ftl_z) {
    if (thumb == finger || thumb == ep_opposite(finger)) return 0;
    for (int j = 0; j < 5; ++j) {
        for (int i = 0; i < 5; ++i) {
            int wx, wy, wz;
            ep_world_at(w, ox, oy, oz, finger, thumb, i, j, 0, &wx, &wy, &wz);
            if (!ep_cell_matches(w, wx, wy, wz, ep_pattern_cell(j, i))) return 0;
        }
    }
    *ftl_x = ox; *ftl_y = oy; *ftl_z = oz;
    return 1;
}

MC_HD MC_NOINLINE static int ep_match_pattern(const EpWorld *w, int px, int py, int pz,
                                         int *ftl_x, int *ftl_y, int *ftl_z) {
    int span = 5;
    for (int dz = 0; dz < span; ++dz) {
        for (int dy = 0; dy < span; ++dy) {
            for (int dx = 0; dx < span; ++dx) {
                int ax = px - dx, ay = py - dy, az = pz - dz;
                for (int finger = 0; finger < 6; ++finger) {
                    for (int thumb = 0; thumb < 6; ++thumb) {
                        if (thumb == finger || thumb == ep_opposite(finger)) continue;
                        if (ep_check_pattern_at(w, ax, ay, az, finger, thumb, ftl_x, ftl_y, ftl_z))
                            return 1;
                    }
                }
            }
        }
    }
    return 0;
}

MC_HD MC_NOINLINE static void ep_fill_portal(EpWorld *w, int ox, int oy, int oz) {
    u16 portal = mc_state(EP_BLK_END_PORTAL, 0);
    for (int j = 0; j < 3; ++j)
        for (int k = 0; k < 3; ++k)
            ep_set(w, ox + j, oy, oz + k, portal);
}

MC_HD MC_NOINLINE static int ep_try_activate(EpWorld *w, int px, int py, int pz) {
    int ftl_x, ftl_y, ftl_z;
    if (!ep_match_pattern(w, px, py, pz, &ftl_x, &ftl_y, &ftl_z)) return 0;
    ep_fill_portal(w, ftl_x - 3, ftl_y, ftl_z - 3);
    return 1;
}

MC_HD MC_NOINLINE static int ep_insert_eye(EpWorld *w, int x, int y, int z) {
    u16 s = ep_get(w, x, y, z);
    if (mc_state_id(s) != EP_BLK_END_PORTAL_FRAME || ep_frame_eye(s)) return 0;
    if (w->eyes_left <= 0) return 0;
    ep_set(w, x, y, z, ep_frame_state(ep_frame_face(s), 1));
    w->eyes_left--;
    w->insert_count++;
    (void)ep_comparator(ep_get(w, x, y, z));
    if (ep_try_activate(w, x, y, z)) {
        w->activated_step = w->insert_count;
        return 2;
    }
    return 1;
}

MC_HD MC_NOINLINE static void ep_clear(EpWorld *w) {
    u16 air = mc_state(0, 0);
    for (int i = 0; i < EP_VOL; ++i) w->blocks[i] = air;
}

MC_HD MC_NOINLINE static void ep_place_ring(EpWorld *w) {
    const int y = 1;
    const int fx[EP_NFRAMES] = { 4, 5, 6, 4, 5, 6, 3, 3, 3, 7, 7, 7 };
    const int fz[EP_NFRAMES] = { 3, 3, 3, 7, 7, 7, 4, 5, 6, 4, 5, 6 };
    const u8 ff[EP_NFRAMES] = {
        EP_FACE_NORTH, EP_FACE_NORTH, EP_FACE_NORTH,
        EP_FACE_SOUTH, EP_FACE_SOUTH, EP_FACE_SOUTH,
        EP_FACE_WEST,  EP_FACE_WEST,  EP_FACE_WEST,
        EP_FACE_EAST,  EP_FACE_EAST,  EP_FACE_EAST
    };
    for (int i = 0; i < EP_NFRAMES; ++i) {
        w->frame_x[i] = fx[i];
        w->frame_y[i] = y;
        w->frame_z[i] = fz[i];
        w->frame_face[i] = ff[i];
        ep_set(w, fx[i], y, fz[i], ep_frame_state(ff[i], 0));
    }
}

MC_HD MC_NOINLINE static void ep_shuffle_order(EpWorld *w, int order[EP_NFRAMES]) {
    for (int i = 0; i < EP_NFRAMES; ++i) order[i] = i;
    for (int i = EP_NFRAMES - 1; i > 0; --i) {
        u64 h = mc_hash_seed(w->seed, 0, i, 0, 0, 0xE900u);
        int j = (int)mc_hash_bound(h, (u64)(i + 1));
        int t = order[i]; order[i] = order[j]; order[j] = t;
    }
}

MC_HD MC_NOINLINE static int ep_portal_count(const EpWorld *w) {
    int n = 0;
    for (int i = 0; i < EP_VOL; ++i)
        if (mc_state_id(w->blocks[i]) == EP_BLK_END_PORTAL) n++;
    return n;
}

MC_HD MC_NOINLINE static void ep_init(EpWorld *w, u64 seed) {
    w->seed = seed;
    w->eyes_left = EP_NFRAMES;
    w->insert_count = 0;
    w->activated_step = -1;
    ep_clear(w);
    ep_place_ring(w);
}

MC_HD MC_NOINLINE static void ep_run(EpWorld *w) {
    ep_init(w, w->seed);
    int order[EP_NFRAMES];
    ep_shuffle_order(w, order);
    for (int i = 0; i < EP_NFRAMES; ++i) {
        int fi = order[i];
        ep_insert_eye(w, w->frame_x[fi], w->frame_y[fi], w->frame_z[fi]);
    }
}

#define EP_NOUT (EP_NFRAMES + 4)

MC_HD MC_NOINLINE static void ep_dump(const EpWorld *w, u64 *out) {
    int o = 0;
    for (int i = 0; i < EP_NFRAMES; ++i)
        out[o++] = (u64)ep_get(w, w->frame_x[i], w->frame_y[i], w->frame_z[i]);
    out[o++] = (u64)(u32)w->eyes_left;
    out[o++] = (u64)(u32)ep_portal_count(w);
    out[o++] = (u64)(i32)w->activated_step;
    out[o++] = (u64)(u32)w->insert_count;
}

#endif /* MC_END_PORTAL_H */
