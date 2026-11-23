/* The semantic camera (lane/semteach): a cheap observation from the
 * engine's own state, for a policy that is trained without the renderer (a
 * privileged teacher; v1's "semantic camera", paper/main.tex). Per env and
 * step a W x H grid of rays from the client player's eye through the client
 * world's blocks (what the player has been sent, never the server's), each
 * returning the first block it meets: its id and metadata, the distance to
 * where the ray enters it and the face it enters through.
 *
 * The rays are the frame's pixels' centres: a pinhole camera at the eye
 * (the client player's position, the renderer's yOffset - 1.62 camera
 * offset), vertical field of view 70 degrees, aspect W / H, its axes from
 * the player's yaw and pitch through MathHelper's sin table (the look vector
 * vanilla's getLook makes), so a cast is the same on every machine. Each ray
 * walks the grid cell by cell (Amanatides and Woo) from the eye's cell out
 * to RANGE blocks:
 *   - a cell outside a loaded chunk, or in a missing section, is air (the
 *     renderer draws nothing there either);
 *   - the eye's own cell is passed, and while the eye is in water (or lava)
 *     the rays pass through water (lava) too: the player sees through it;
 *   - below y 0 the ray ends (SC_VOID), above y 255 or past RANGE it misses
 *     (SC_MISS); the first other block is the hit.
 * Read only: nothing in the env changes but the client world's near cache
 * of chunk lookups (a memo, no game state), so a cast never changes a row.
 *
 * Two casts that give the same bytes: semcam_cast_ref, the reference (each
 * cell through world_chunk and chunk_get_block, the way any engine read
 * goes), and semcam_cast, the fast path the runtime runs (the chunk and
 * band of the last cell kept, and a ray that rises above every loaded
 * section within RANGE ends there as the miss it is). pool_gate --semcam
 * holds them equal on every tick of its recordings.
 *
 * Two instances (lane/castf32, raycast.h "The two instances"), one source (semcam_inst.h) in two scalars, by the
 * spec's fp: FP64, the reference, the cast as it was (double from the eye's world position); FP32, production:
 * every operation a float's, in this order (fl(): one rounding): the eye's cell e = floor(w) and offset o = fl(w -
 * e) per axis (o == 1: e + 1, o 0), w the world position as above; the camera's f, r, u and aspect as raycast.h's;
 * ray (i, j): sy = (1 - 2 (j + 0.5f) / H) T, sx = ((2 (i + 0.5f) / W - 1) T) aspect, T = 0.700207531f, d = (f + sx
 * r) + sy u, len = sqrtf((dx dx + dy dy) + dz dz), d / len per component; the walk: step s = sign(d), td = |1 /
 * d|, the first boundary t = ((i - e) + 1 - o) / d going + and ((i - e) - o) / d going -, each step t += td
 * (accumulated, as the reference), the least t taken, ties x, then y, then z; dist = floor(t 256) in float
 * (exact: a power of two), held to 65534. */
#ifndef NETHERITE_RUNTIME_SEMCAM_H
#define NETHERITE_RUNTIME_SEMCAM_H

#include <stdint.h>

struct session;

/* a ray's answer: 8 bytes */
struct semcam_cell {
    uint16_t block;             /* id | meta << 12 (0: none, a miss or the void) */
    uint16_t dist;              /* blocks x 256 to where the ray enters the cell, floor; 0xffff: a miss */
    uint8_t face;               /* 0..5 the face entered (DOWN, UP, NORTH, SOUTH, WEST, EAST), SC_MISS, SC_VOID */
    uint8_t flags;              /* SCF_* */
    uint16_t pad;
};
enum { SC_MISS = 6, SC_VOID = 7 };
enum { SCF_EYE_LIQUID = 1 };    /* the eye was in water or lava (set on every ray of the cast) */

struct semcam_spec {
    int w, h;                   /* rays across and down (client.semantic) */
    int range;                  /* blocks (client.semantic_range) */
    int fp;                     /* client.cast_precision: 32 the FP32 instance, else (0, 64) the FP64 reference */
};

/* env's client world from its client player into out[h * w] (row-major, row 0
 * at the top), with neth_env the env's. 0, or -1 when the session has no
 * client world (out is then all misses). */
int semcam_cast(const struct semcam_spec *sp, struct session *ss, struct semcam_cell *out);
int semcam_cast_ref(const struct semcam_spec *sp, struct session *ss, struct semcam_cell *out);

/* the rays' directions for a yaw and pitch (degrees), unit length, into
 * dir[h * w][3] (both casts use these) */
void semcam_rays(const struct semcam_spec *sp, float yaw, float pitch, double *dir);

#endif
