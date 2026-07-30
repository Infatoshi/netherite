/* mc_world.h - the world representation everything writes into. TRUNK: design before fan-out.
 *
 * Layout matches MC: a chunk is 16x16 columns, 256 tall (16 sections of 16^3). Block state is
 * packed u16 = (blockId<<4)|meta (vanilla legacy id; see mc_blocks.h). Light is packed u8 =
 * (skyLight<<4)|blockLight. Biome is u8 per column.
 *
 * Tick uses DOUBLE BUFFERING (SPEC rule 3): read the 'now' buffer for tick N, write the 'next'
 * buffer for N+1, then swap. No in-place mutation a neighbor can observe mid-tick -> parallel
 * (CUDA, any thread order) ticking is deterministic and CPU==CUDA holds. Worldgen is a one-shot
 * fill (no double buffer needed); it writes a Chunk directly. */
#ifndef MC_WORLD_H
#define MC_WORLD_H

#include "mc.h"

#define MC_CX 16          /* chunk x */
#define MC_CZ 16          /* chunk z */
#define MC_CY 256         /* world height */
#define MC_SEC 16         /* section height */
#define MC_NSEC (MC_CY / MC_SEC)
#define MC_CHUNK_VOL (MC_CX * MC_CZ * MC_CY)   /* 65536 */
#define MC_COL_AREA (MC_CX * MC_CZ)            /* 256 */

/* In-chunk linear index. Local coords x,z in [0,16), y in [0,256). */
MC_HD static inline int mc_idx(int x, int y, int z) { return (y * MC_CZ + z) * MC_CX + x; }

/* block-state packing (vanilla legacy id<<4|meta). The global-palette encoding used by chunk
 * storage is built from the registry at oracle 1c; default states (meta 0) coincide. */
MC_HD static inline u16 mc_state(int id, int meta) { return (u16)(((id & 0xFFF) << 4) | (meta & 0xF)); }
MC_HD static inline int mc_state_id(u16 s)   { return s >> 4; }
MC_HD static inline int mc_state_meta(u16 s) { return s & 0xF; }

/* light packing */
MC_HD static inline u8 mc_light(int sky, int block) { return (u8)(((sky & 0xF) << 4) | (block & 0xF)); }
MC_HD static inline int mc_light_sky(u8 l)   { return l >> 4; }
MC_HD static inline int mc_light_block(u8 l) { return l & 0xF; }

typedef struct {
    u16 blocks[MC_CHUNK_VOL];   /* 128 KB */
    u8  light[MC_CHUNK_VOL];    /*  64 KB */
    u8  biome[MC_COL_AREA];     /* per column */
    i32 cx, cz;                 /* chunk coords (in chunk units) */
} Chunk;

MC_HD static inline u16  mc_get(const Chunk *c, int x, int y, int z) { return c->blocks[mc_idx(x, y, z)]; }
MC_HD static inline void mc_set(Chunk *c, int x, int y, int z, u16 s) { c->blocks[mc_idx(x, y, z)] = s; }

/* A fixed-radius world for one env (small for batched RL). Chunks laid out (2R+1)^2 around origin.
 * Tick double-buffers the block arrays; see mc_tick.h. */
#ifndef MC_WORLD_R
#define MC_WORLD_R 2                       /* 5x5 chunks default; tune per memory budget */
#endif
#define MC_WORLD_DIM (2 * MC_WORLD_R + 1)
#define MC_WORLD_CHUNKS (MC_WORLD_DIM * MC_WORLD_DIM)

/* named tag: mc_rng.h forward-declares `struct World` (C++ rejects the mismatch). */
typedef struct World {
    Chunk chunk[MC_WORLD_CHUNKS];
    u64   seed;
    i64   tick;
} World;

#endif /* MC_WORLD_H */
