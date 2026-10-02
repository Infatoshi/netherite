/* The environment's arena: one reserved mapping made with the environment,
 * carved into its fixed-capacity pools (a GPU environment is a block of
 * bytes of a size known before it runs, with no allocation while it ticks).
 *
 * A slot pool hands out zeroed slots of one size and takes them back on its
 * own free chain, bumping the slot's generation; a slot's index names it
 * (pool_index, pool_at), so what refers to an entity can store the index and
 * generation instead of an address. The capacities are the constants below:
 * reserved as address space and committed as written, so a small run costs
 * what it touches; a pool that runs out stops the program. */
#ifndef NETHERITE_ARENA_H
#define NETHERITE_ARENA_H

#include <stddef.h>
#include <stdint.h>

#include "pathfind.h"

struct arena {
    unsigned char *base;
    size_t size, used;
    int own_mapping;        /* the arena mapped itself (arena_env_init) */
};

struct slot_pool {
    unsigned char *base;
    size_t size;            /* bytes per slot */
    int32_t cap, top, free_head, live;
    int32_t freed;          /* slots given back since arena_trim last read the pool */
    int32_t *next;          /* the free chain, -1 ends it */
    uint32_t *gen;          /* bumped each time the slot is given back */
    const char *what;
};

/* The capacities. PathEntity objects (living.h pathref) are shared between
 * holders by count; the entity pools hold every living (with its list
 * entry), item-world entity (items, orbs, projectiles, TNT) and falling or
 * hanging entity the environment has not released. */
#define ARENA_PATHS (1 << 16)
#define ARENA_LIVINGS (1 << 16)
#define ARENA_AN_ENTS (1 << 17)
#define ARENA_IE_ENTS (1 << 17)
#define ARENA_FH_ENTS (1 << 16)
#define ARENA_DOORS (1 << 16)
/* chunk bands (world.h struct chunk_sec, 4 KB and a header): every loaded
 * chunk's sections in every world of the environment; their nibble arrays
 * (2 KB) that are not one value, at most three a band, after the sixteen
 * constant arrays (slot v every nibble v, made with the pool) */
#define ARENA_BANDS (1 << 17)
#define ARENA_NIBS (3 * ARENA_BANDS + 16)
/* chunks (world.h struct chunk, about 2.9 KB): every chunk of the
 * environment, loaded, parked in a region store, the client's and the
 * snapshot's */
#define ARENA_CHUNKS (1 << 17)

/* The list slab: blocks of 64 bytes to 4 GB in power-of-two classes, each
 * class with its own free chain (through a block's first eight bytes),
 * named by their byte offset from the slab's base. The per-chunk entity
 * section lists, the chunk tables, the pending tick sets, the paths' points
 * and what else the tick used to malloc live here. */
#define ARENA_SLAB_BYTES ((size_t)4 << 30)
#define SLAB_CLASSES 40

/* A freed block of this class (1 MB) or more gives its pages past its first
 * back to the kernel (image_give_pages): the pending tick sets' runs, heap
 * and tables grow by doubling to a backlog's size (a village snapshot's
 * 16 MB), and each outgrown block would otherwise stay resident on its
 * chain. Smaller blocks churn every tick (section lists, zlib's window) and
 * stay: their pages would be faulted in again at each reuse. */
#define SLAB_GIVE_CLASS 20

struct slab {
    unsigned char *base;
    size_t size, used;
    int64_t free_head[SLAB_CLASSES];
};

struct living_ai;
struct living_villager;
struct living_player;

struct arena_env {
    struct arena a;
    struct slab slab;
    /* a living's parts (living.h): each its own pool, a slot taken the
     * first time the living reaches the part (a bat never takes an AI part,
     * only a villager a trading part, only the probe's player an inventory) */
    struct slot_pool ai_parts, villager_parts, player_parts;
    struct slot_pool paths;
    struct slot_pool livings, an_ents, ie_ents, fh_ents, doors, bands, nibs, chunks;
    struct pf_store pf;
};

/* Map and carve e's pools; release the mapping. arena_env_init_in carves
 * them from memory the caller reserved (an environment's image, image.h):
 * arena_env_bytes of it, page aligned. */
void arena_env_init(struct arena_env *e);
void arena_env_init_in(struct arena_env *e, void *mem, size_t size);
size_t arena_env_bytes(void);
void arena_env_free(struct arena_env *e);
/* The live arena was moved to another address (image_relocate). */
void arena_live_replace(struct arena_env *old, struct arena_env *moved);
/* bytes (zeroed, 64-byte aligned) from the arena, at environment creation */
void *arena_carve(struct arena *a, size_t bytes);

void *pool_take(struct slot_pool *p);
/* pool_take without clearing the slot: for a caller that writes every byte
 * of it at once (a band copied whole) */
void *pool_take_raw(struct slot_pool *p);
void pool_give(struct slot_pool *p, void *obj);
/* pool_give without clearing the record: its bytes stay until the slot is
 * taken again (an item-world entity's count a stack link still names) */
void pool_give_keep(struct slot_pool *p, void *obj);
static inline int32_t pool_index(const struct slot_pool *p, const void *obj)
{
    return (int32_t)((size_t)((const unsigned char *)obj - p->base) / p->size);
}
static inline void *pool_at(const struct slot_pool *p, int32_t i)
{
    return p->base + (size_t)i * p->size;
}
static inline int pool_holds(const struct slot_pool *p, const void *obj)
{
    return (const unsigned char *)obj >= p->base && (const unsigned char *)obj < p->base + (size_t)p->cap * p->size;
}

/* The current environment's arena: struct env starts with it (env.c checks). */
struct env;
/* local-exec: every program links the environment into its executable, so
 * the pointer is one %fs-relative load (NW_ENV_TLS_MODEL: a library
 * loaded into such a program, csrc/runtime's play view, takes
 * initial-exec) */
#if defined(__NVPTX__)
/* the device build (cuda/tick/dev.c): each thread runs one environment
 * (a block holds up to 32), named in the block's shared memory by thread */
extern struct env *__attribute__((address_space(3))) nwdev_envs[32];
#define nw_env (nwdev_envs[__nvvm_read_ptx_sreg_tid_x()])
#else
#ifndef NW_ENV_TLS_MODEL
#define NW_ENV_TLS_MODEL "local-exec"
#endif
extern _Thread_local struct env *nw_env __attribute__((tls_model(NW_ENV_TLS_MODEL)));
#endif
static inline struct arena_env *nw_arena(void)
{
    return (struct arena_env *)(void *)nw_env;
}

/* bytes from the slab (zeroed when zero), and back; the pointer an offset
 * names (valid until the block is freed) */
int64_t slab_alloc(struct slab *s, size_t bytes, int zero);
void slab_free(struct slab *s, int64_t off, size_t bytes);
/* the slab's bytes in blocks taken and on the free chains (a walk of the
 * chains: for the harness, envmem.h); the block a request of bytes takes */
void slab_census(const struct slab *s, size_t *live, size_t *free_bytes);
size_t slab_block_bytes(size_t bytes);
static inline void *slab_ptr(const struct slab *s, int64_t off)
{
    return s->base + off;
}

/* The slab by address, for what the tick used to malloc: slab_take's bytes
 * (not cleared, as malloc's), slab_give's back (bytes as taken), and
 * slab_grow as realloc (NULL or 0 old bytes: none yet); a block keeps its
 * address while the size stays in its power-of-two class. */
void *slab_take(size_t bytes);
void slab_give(void *p, size_t bytes);
void *slab_grow(void *p, size_t old_bytes, size_t new_bytes);

/* One chunk section's entity list (Chunk.entityLists[y]): the pool slot
 * indices of its entities in insertion order, in a slab block of cap. */
struct sec_list {
    int64_t off;
    int32_t n, cap;
};

static inline int32_t *sec_items(const struct sec_list *l)
{
    return (int32_t *)slab_ptr(&nw_arena()->slab, l->off);
}
/* A list Java grows past what its fixed array holds: say which and stop
 * (never a silent drop). */
_Noreturn void list_full(const char *what, int cap);
/* append v; a list past max (its world's list capacity: a section cannot
 * hold more entities than its world) stops the program */
void sec_push(struct sec_list *l, int32_t v, int max);
/* the first v out, the rest moved up; 1 when it was there */
int sec_remove_first(struct sec_list *l, int32_t v);
void sec_release(struct sec_list *l);

/* A growable table in the slab (the entity worlds' chunk tables): *p grows
 * to hold n + 1 elements of size bytes (doubling from 32), *cap follows. */
void *slab_table_room(void *p, int n, int *cap, size_t size);
void slab_table_free(void *p, int cap, size_t size);

/* the slot index of an entity in its pool, and the entity at an index */
struct an_ent;
struct ie_ent;
struct fh_ent;
static inline int32_t an_ent_index(const struct an_ent *en) { return en ? pool_index(&nw_arena()->an_ents, en) : -1; }
/* the entity at a slot, NULL for a negative one (a list's cleared entry) */
static inline struct an_ent *an_ent_at(int32_t i) { return i < 0 ? NULL : (struct an_ent *)pool_at(&nw_arena()->an_ents, i); }
static inline int32_t ie_ent_index(const struct ie_ent *en) { return en ? pool_index(&nw_arena()->ie_ents, en) : -1; }
static inline struct ie_ent *ie_ent_at(int32_t i) { return i < 0 ? NULL : (struct ie_ent *)pool_at(&nw_arena()->ie_ents, i); }
static inline int32_t fh_ent_index(const struct fh_ent *en) { return en ? pool_index(&nw_arena()->fh_ents, en) : -1; }
static inline struct fh_ent *fh_ent_at(int32_t i) { return i < 0 ? NULL : (struct fh_ent *)pool_at(&nw_arena()->fh_ents, i); }

/* a list entry named by its slot + 1 (0 for none) */
static inline int32_t an_ref(const struct an_ent *en) { return en ? an_ent_index(en) + 1 : 0; }
static inline struct an_ent *an_deref(int32_t h) { return h ? an_ent_at(h - 1) : NULL; }

/* A fixed-capacity array made with its owner (a world, a spawner) and never
 * moved or grown: cap elements of size bytes, zeroed, reserved as address
 * space and committed as written. fixed_array_free takes it back. Its
 * mapping is named nw-fixed, which the grave's scan skips: a fixed array
 * must not hold a living's address or lref. */
void *fixed_array(size_t cap, size_t size);
void fixed_array_free(void *p, size_t cap, size_t size);
/* the bytes of a fixed array (or any page-aligned mapping) its writes have
 * committed so far (/proc/self/pagemap, else mincore): a measure for the
 * harness (envmem.h) */
size_t fixed_array_resident(const void *p, size_t cap, size_t size);

/* The current environment's freed pool slots' pages handed back to the
 * kernel: for each pool that has freed ARENA_TRIM_BYTES since its last trim,
 * the whole pages under each run of free slots (image_give_pages). A slot
 * taken again is written whole (pool_take clears it, a band is copied into),
 * so it reads what it did. Between rows (session.c), beside the grave's
 * sweep; not the item-world entities, whose freed bytes are still read
 * (pool_give_keep). */
#define ARENA_TRIM_BYTES ((size_t)2 << 20)
void arena_trim(void);

/* A fixed array whose first used elements are live and which once held *hi:
 * the whole pages past the used ones back to the kernel once they pass
 * FIXED_TRIM_BYTES, and *hi becomes used (a list refilled from 0 each tick
 * keeps the pages of the largest tick otherwise). */
#define FIXED_TRIM_BYTES ((size_t)256 << 10)
void fixed_array_trim(void *p, size_t used, int *hi, size_t size);

/* Every arena alive in the process (the grave's scan reads their pools'
 * used slots instead of the whole reservation): up to ARENA_MAX_LIVE. */
#define ARENA_MAX_LIVE 16
int arena_live(struct arena_env **out);

#endif
