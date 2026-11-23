/* The privileged world scan (lane/iron): per env at its step's end, on the
 * worker, what a teacher policy and a scripted expert know of the world
 * that no frame shows: the blocks around the player's feet and the nearest
 * block of each of up to 8 classes (logs, stone, iron ore, ...) within a box
 * around the eye. It reads the client world (what the server sent), like
 * the semantic camera, and changes nothing (rows and frames stay the same).
 *
 * Each env's answer (struct rlscan_out):
 *   patch[y][z][x]  the block ids from (fx - 2, fy - 2, fz - 2) to
 *                   (fx + 2, fy + 3, fz + 2), (fx, fy, fz) the cell of the
 *                   player's feet; -1 where no chunk is loaded
 *   near[c]         class c's nearest block (squared distance from the eye
 *                   cell to the block's cell, a height over one cell above
 *                   the eye weighted by up_weight; ties to the first found
 *                   in x, z, y order): its offset from the eye cell and its id,
 *                   found 0 when the box has none
 */
#ifndef NETHERITE_RUNTIME_RLSCAN_H
#define NETHERITE_RUNTIME_RLSCAN_H

#include <stdint.h>

struct session;

#define RLSCAN_CLASSES 8
enum { RLSCAN_PX = 5, RLSCAN_PY = 6 };

struct rlscan_spec {
    int radius;                 /* the box's half width in x and z (blocks) */
    int below, above;           /* its extent under and over the eye cell */
    int up_weight;              /* a block more than one cell above the eye cell counts its height this many
                                 * times over in the squared distance (1: plain distance; a policy reaches
                                 * what is low) */
    uint8_t cls[4096];          /* block id -> bit c: the id is of class c */
};

struct rlscan_near { int8_t dx, dy, dz, found; int16_t id, pad; };

struct rlscan_out {
    int16_t patch[RLSCAN_PY][RLSCAN_PX][RLSCAN_PX];
    struct rlscan_near near[RLSCAN_CLASSES];
    int32_t fx, fy, fz, pad;    /* the feet cell the patch is around */
    float x, y, z, yaw, pitch;  /* the client player (what the camera and the crosshair use), y its feet (the eye
                                 * 1.62 over them) */
    int32_t pad2[3];
};

/* env's client world into out (neth_env the env's); -1 without a client world */
int rlscan_run(const struct rlscan_spec *sp, struct session *ss, struct rlscan_out *out);

#endif
