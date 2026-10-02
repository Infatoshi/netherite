/* The key of a section's mesh (raster.c's kept meshes, lane/remesh). A
 * section pass's mesh is a function of what rb_mesh_section reads through
 * render_blocks.c's world accessors, the camera cell and the renderer's own
 * tables (fixed for its life). The accessors read the cells within 3 blocks
 * of the section in x and z (BlockPortal.shouldSideBeRendered's two past
 * the face's neighbour) and 2 in y (a slab's light below a face's corner,
 * a liquid's above), their chunks' section masks and height maps (the sky
 * light of a section that is not there), the biomes and a flower pot's
 * tile entity. meshkey_section hashes exactly those bytes, as the window's
 * records hold them, so an equal key is the same mesh: a section marked
 * stale whose key did not move keeps its mesh (and its version, so a device
 * that holds it uploads nothing and meshes nothing). A pass none of the
 * section's cells is drawn in has the empty mesh, whatever the key.
 *
 * The hash is 128 bits over the bytes in a fixed order. */
#ifndef NETHERITE_RASTER_MESHKEY_H
#define NETHERITE_RASTER_MESHKEY_H

#include <stdint.h>

struct rb_world;

struct meshkey { uint64_t h[2]; };

enum {
    MESHKEY_NONE = 0,   /* no key: mesh it (a flower pot's plant is a tile entity; a scene's world) */
    MESHKEY_EMPTY = 1,  /* the pass's mesh is empty: none of the section's cells is drawn in it */
    MESHKEY_OK = 2      /* *k is the key */
};

/* The bands' part of the keys, kept by band: a band a window record holds
 * is never written (the engine copies a shared band before a write,
 * world.h chunk_sec_own, and a record only shares), so the hash of the bytes
 * a key reads from it (by where the band sits around the section: 27
 * kinds) is kept by the band's pool slot and generation (arena.h) and made
 * once while records hold it. A band written while no record held it can
 * come back to one with other bytes: whoever puts a band in a record that
 * did not hold it names it to meshkey_cache_taken, which drops its hashes.
 * VERIFY: every kept hash is made again from the bytes and compared; BAD
 * counts those that differed. */
struct meshkey_cache;
struct chunk_sec;
struct meshkey_cache *meshkey_cache_new(void);
void meshkey_cache_free(struct meshkey_cache *mc);
void meshkey_cache_taken(struct meshkey_cache *mc, const struct chunk_sec *b);
/* B went into a record in place of FROM, whose bytes it holds (compared by
 * the caller): FROM's hashes are B's (FROM still a live band) */
void meshkey_cache_same(struct meshkey_cache *mc, const struct chunk_sec *from, const struct chunk_sec *b);
/* B's content serial as MC knows it: two bands (or one band twice) with
 * one serial hold the same bytes, if records held them all the while (a
 * new serial when a band is first seen in its slot's generation and when
 * it is taken; a band taken with another's bytes keeps that one's); 0 for a
 * band outside the environment's pool. */
uint64_t meshkey_band_id(struct meshkey_cache *mc, const struct chunk_sec *b);
/* Under VERIFY, a caller that took a band's identity as proof of its bytes
 * and compared them anyway counts it: BAD when they differed. */
void meshkey_cache_count(struct meshkey_cache *mc, int bad);
int meshkey_cache_verifying(const struct meshkey_cache *mc);
void meshkey_cache_verify(struct meshkey_cache *mc, int on);
void meshkey_cache_stats(const struct meshkey_cache *mc, uint64_t *checked, uint64_t *bad);

/* Section S of chunk (CX, CZ) in W's window, the camera block (PX, PY, PZ)
 * (rb_mesher's, the cell drawn twice from inside); PASS[id] has bit p set
 * when the mesher draws block id in pass p (render_blocks.h rb_block_pass,
 * 0 for air); MC the band hashes (NULL: every band hashed here). *DRAWN:
 * the passes some cell of the section is drawn in (both for MESHKEY_NONE);
 * MESHKEY_EMPTY when none. */
/* meshkey_section's *DRAWN alone, without the key (its first steps): 0 when
 * no cell of the section is drawn (MESHKEY_EMPTY) */
int meshkey_drawn(const struct rb_world *w, const uint8_t *pass, struct meshkey_cache *mc, int cx, int s, int cz);
int meshkey_section(const struct rb_world *w, const uint8_t *pass, struct meshkey_cache *mc, int cx, int s, int cz,
                    int px, int py, int pz, struct meshkey *k, int *drawn);

/* The band of section S of chunk (CX, CZ) in W's window of records (NULL:
 * every cell reads air) */
const struct chunk_sec *meshkey_band(const struct rb_world *w, int cx, int s, int cz);

#endif
