/* See serverreplay.h. */
#define _POSIX_C_SOURCE 200809L
#include "leash.h"
#include "serverreplay.h"
#include "regionspill.h"
#include "envstack.h"

/* the cells one tick of entity-pass and network writes can defer */
/* a tick's entity-pass and network writes wait for the next flush: as many
 * as the pass may make (a chunk generated inside it) */
#define SR_MAX_LATE ST_MAX_WRITES
/* st_write.pad: an explosion's write (0x10 and the low bits tag another dimension's) */
#define SR_WRITE_S27 0x40
/* st_write.pad: a write without flag 2 (World.markBlockForUpdate not called): the flush does not send it */
#include "grave.h"
#include "arena.h"
#include "snap_runtime.h"
#include "combatench.h"
#include "spill.h"
#include "sleep.h"
#include "tnt.h"
#include "combat.h"
#include "chatmsg.h"
#include "seedworld.h"
#include "ticks.h"
#include "blockcb.h"
#include "randomtick.h"
#include "entity_nbt.h"
#include "hostiles.h"
#include "hostiles_pigman.h"
#include "hostiles_skeleton.h"
#include "hostiles_spider.h"
#include "hostiles_silverfish.h"
#include "hostiles_blaze.h"
#include "hostiles_creeper.h"
#include "hostiles_enderman.h"
#include "hostiles_witch.h"
#include "slimes.h"
#include "ghasts.h"
#include "animals.h"
#include "ghasts.h"
#include "slimes.h"
#include "ai.h"
#include "player.h"
#include "portal.h"
#include "populate_nether.h"
#include "survival.h"
#include "tileticks.h"
#include "villagers.h"
#include "structure_blocks.h"
#include "throw.h"
#include "endfight.h"
#include "explosion.h"
#include "blocks.h"
#include "jorder.h"
#include "features_nether.h"
#include "pickobj.h"
#include "dig.h"
#include "stronghold.h"
#include "env.h"

#include <stddef.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbtjson.h"
#include "place.h"
#include "region.h"
#include "item_entity.h"
#include "projectile.h"
#include "riding.h"
#include "stronghold.h"
#include "regioncache.h"

int sr_living_kind(const char *cls);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FNV_PRIME 0x100000001b3ULL

/* ------------------------------------------------------------ the dimensions */

/* Make a the dimension here. A state entered for the first time starts its
 * pending set with the world values the current one holds (World.rand, the
 * total time, the immediate and fallInstantly flags), as the one set of
 * ticks.c globals the dimensions once swapped did. */
static void sr_use_dim(struct serverreplay *sr, struct sr_dimstate *a)
{
    struct ticks_set *cur = nw_env->ticks.cur;

    if (!a->ticks_ready)
    {
        a->ticks.immediate = cur->immediate;
        a->ticks.fall_instantly = cur->fall_instantly;
        a->ticks.total_time = cur->total_time;
        a->ticks.world_rand = cur->world_rand;
        a->ticks_ready = 1;
    }

    /* the pointers that name the dimension here rather than their own
     * follow it: the player's living twin and the survival module's pools */
    struct sr_dimstate *from = sr->d;

    if (from != NULL)
    {
        if (sr->player_livh != 0 && lv_get(sr->player_livh)->an == &from->anw) lv_get(sr->player_livh)->an = &a->anw;
        if (surv.iew == &from->iew) surv.iew = &a->iew;
        if (surv.anw == &from->anw) surv.anw = &a->anw;
        if (surv.extra_iew == &from->anw.iew) surv.extra_iew = &a->anw.iew;
    }

    ticks_use(&a->ticks);
    sr->d = a;
    sr->here = a->dim;
    a->iew.peer = &a->anw.iew;
    a->anw.iew.peer = &a->iew;
}

/* The replay's dimension states before the load: the overworld here, its
 * pending set the one ticks.c holds now. */
void serverreplay_dims_init(struct serverreplay *sr)
{
    static const int dims[3] = {0, -1, 1};
    struct ticks_set *cur = nw_env->ticks.cur;

    for (int i = 0; i < 3; ++i)
    {
        sr->dims[i].sr = sr;
        sr->dims[i].dim = dims[i];
        /* the pass order and the pass's own writes: fixed, made here */
        sr->dims[i].ents = fixed_array(SR_MAX_ORDER, sizeof *sr->dims[i].ents);
        sr->dims[i].capents = SR_MAX_ORDER;
        sr->dims[i].extra = fixed_array(ST_MAX_WRITES, sizeof *sr->dims[i].extra);
        sr->dims[i].capextra = ST_MAX_WRITES;
        sr->dims[i].late = fixed_array(SR_MAX_LATE, sizeof *sr->dims[i].late);
        sr->dims[i].caplate = SR_MAX_LATE;
    }

    /* moved, not copied: the set may be one a previous load left current */
    struct ticks_set here = *cur;
    memset(cur, 0, sizeof *cur);
    sr->dims[0].live = 1;
    sr->dims[0].ticks = here;
    sr->dims[0].ticks_ready = 1;
    sr_use_dim(sr, &sr->dims[0]);
}

void serverreplay_enter(struct serverreplay *sr, int dim)
{
    if (sr->here == dim) return;

    struct sr_dimstate *a = &sr->dims[sr_dim_index(dim)];

    if (a->live && a->dim == dim) sr_use_dim(sr, a);
}

int serverreplay_dim_nents(const struct serverreplay *sr, int dim)
{
    const struct sr_dimstate *a = &sr->dims[sr_dim_index(dim)];

    return a->live && a->dim == dim ? a->nents : 0;
}

/* Every world's loadedEntityList but the players, which the caller counts. */
static int sr_count_entities(const struct serverreplay *sr)
{
    int n = 0;

    for (int i = 0; i < 3; ++i)
        if (sr->dims[i].live) n += sr->dims[i].iew.n + sr->dims[i].fhw.n + sr->dims[i].anw.n;

    if (sr->end != NULL) n += endfight_count(sr->end);

    return n;
}

int serverreplay_player_dim(const struct serverreplay *sr)
{
    return sr->player != NULL ? sr->player->dimension : 0;
}

jrand *serverreplay_world_rand(struct serverreplay *sr, int dim)
{
    for (int i = 0; i < sr->nworlds; ++i)
        if (sr->w[i].dim == dim) return &ST_RAND(&sr->w[i].st);
    return &ST_RAND(&sr->w[0].st);
}

static void sr_entity_drop(void *ctx, double x, double y, double z, int item, int damage, int count);

void serverreplay_drop_env_begin(struct serverreplay *sr, struct randomtick_env *saved)
{
    struct randomtick_env env = {serverreplay_world_rand(sr, serverreplay_player_dim(sr)), &SR_DET(sr), DET_SERVER,
                                 sr_entity_drop, sr};

    randomtick_tick_save(saved);
    randomtick_tick_load(&env);
}

void serverreplay_drop_env_end(const struct randomtick_env *saved)
{
    randomtick_tick_load(saved);
}

struct dragon_state *serverreplay_dragon(struct serverreplay *sr)
{
    return sr->end != NULL && sr->end->dragon_listed ? &sr->end->d : NULL;
}

/* The world a dimension's chunks live in, and its world server. */
static struct world *sr_dim_world(struct serverreplay *sr, int dim)
{
    return dim == -1 ? &sr->hell.world : dim == 1 ? &sr->sky.world : &sr->pop.world;
}

static struct sr_world *sr_dim_ws(struct serverreplay *sr, int dim)
{
    for (int i = 0; i < sr->nworlds; ++i)
        if (sr->w[i].dim == dim) return &sr->w[i];

    return &sr->w[0];
}

/* The clock func_147446_b schedules against: the Nether's and the End's
 * WorldInfo is a DerivedWorldInfo whose getWorldTotalTime is the
 * overworld's, which the overworld's tick advances before its entity pass,
 * while a no-sky world's own copy (st.total_time) steps at its own tick's
 * head; so a chunk of another dimension loaded inside the overworld's pass
 * (an item or a mob through a portal) schedules at the overworld's time. */
static int64_t sr_shared_clock(struct serverreplay *sr)
{
    return sr_dim_ws(sr, 0)->st.total_time;
}

/* ------------------------------------------------------------- nbt helpers */

static int64_t scalar_long(const nbt *comp, const char *key)
{
    const nbt *v = comp ? nbt_get(comp, key) : NULL;

    if (v == NULL) return 0;

    char *t = nbt_render(v);

    if (t == NULL) return 0;

    char *p = t;

    if (*p == '"') ++p;

    if (p[0] != 0 && p[1] == ':') p += 2;

    int64_t out = strtoll(p, NULL, 10);
    free(t);
    return out;
}

static float scalar_float(const nbt *comp, const char *key)
{
    const nbt *v = comp ? nbt_get(comp, key) : NULL;

    if (v == NULL) return 0.0F;

    char *t = nbt_render(v);

    if (t == NULL) return 0.0F;

    char *p = t;

    if (*p == '"') ++p;

    if (p[0] == 'f' && p[1] == ':') p += 2;

    uint32_t bits = (uint32_t)strtoul(p, NULL, 16);
    float f;

    memcpy(&f, &bits, sizeof f);
    free(t);
    return f;
}

/* The checkpoint entity restore reads region NBT members, so it needs the
 * list and float accessors snapshot_entities.c keeps as its own statics. */
static int cp_len(const nbt *v) { return v ? nbt_list_size(v) : 0; }

static int cp_num(const nbt *o, const char *key)
{
    return (int)nbt_int_value(nbt_get(o, key));
}

static double cp_dbl(const nbt *v)
{
    uint64_t bits = v ? nbt_double_bits(v) : 0;
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static float cp_flt(const nbt *v)
{
    uint32_t bits = v ? nbt_float_bits(v) : 0;
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static float cp_fat(const nbt *o, const char *key)
{
    return cp_flt(nbt_get(o, key));
}

static double cp_dat(const nbt *o, const char *key, int index)
{
    return cp_dbl(nbt_list_get(nbt_get(o, key), index));
}

static float cp_ftat(const nbt *o, const char *key, int index)
{
    return cp_flt(nbt_list_get(nbt_get(o, key), index));
}

/* A canonical int array "ia:1,2,3" (empty "ia:") into out; the count, or -1
 * when the node is not an int array or does not fit. */
static int scalar_int_array(const nbt *v, int *out, int max)
{
    if (v == NULL) return -1;

    char *t = nbt_render(v);

    if (t == NULL) return -1;

    char *p = t;

    if (*p == '"') ++p;

    if (strncmp(p, "ia:", 3) != 0) { free(t); return -1; }

    p += 3;
    int n = 0;

    while (*p != 0 && *p != '"')
    {
        char *end;
        long x = strtol(p, &end, 10);

        if (end == p) break;

        if (n >= max) { free(t); return -1; }

        out[n++] = (int)x;
        p = end;

        if (*p == ',') ++p;
    }

    free(t);
    return n;
}

/* ------------------------------------------------------- the active chunk set */

/* World.setActivePlayerChunksAndCheckLight's HashSet, cleared and filled
 * each tick with the (2 view + 1)^2 chunks around the player: its iteration
 * order (jorder.h). clear() keeps the table, so *cap carries it from fill
 * to fill (0: not filled natively yet, taken as the table the set's size
 * needs, the steady state a snapshot's set is in); NULL: that table for
 * this fill alone. */
int sr_active_set(const struct serverreplay *sr, double px, double pz, int *out, int *cap)
{
    int side = 2 * sr->view + 1;

    if (sr->view > SR_MAX_VIEW) return 0;

    int bins = jord_cap(sr->view >= 0 ? side * side : 0);
    if (cap != NULL && *cap > 0) bins = *cap;

    int n = jord_ccp_window((int)floor(px / 16.0), (int)floor(pz / 16.0), sr->view, &bins, out);

    if (cap != NULL) *cap = bins;
    return n;
}

int sr_active_set_memo(const struct serverreplay *sr, double px, double pz, int *out, int *cap,
                       struct jord_window_memo *memo)
{
    int side = 2 * sr->view + 1;

    if (sr->view > SR_MAX_VIEW) return 0;

    int bins = jord_cap(sr->view >= 0 ? side * side : 0);
    if (cap != NULL && *cap > 0) bins = *cap;

    int n = jord_ccp_window_memo(memo, (int)floor(px / 16.0), (int)floor(pz / 16.0), sr->view, &bins, out);

    if (cap != NULL) *cap = bins;
    return n;
}

/* -------------------------------------------------------------- the load */

static int read_file(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) return 0;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }

    long n = ftell(f);

    if (n < 0) { fclose(f); return 0; }

    rewind(f);
    char *buf = malloc((size_t)n + 1);

    if (buf == NULL) { fclose(f); return 0; }

    size_t got = fread(buf, 1, (size_t)n, f);

    buf[got] = 0;

    /* the oracle's writeFile ends the line; canonical NBT text does not */
    while (got > 0 && (buf[got - 1] == '\n' || buf[got - 1] == '\r')) buf[--got] = 0;

    fclose(f);
    *out = buf;
    return 1;
}

static void set_err(struct serverreplay *sr, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sr->err, sizeof sr->err, fmt, ap);
    va_end(ap);
}

/* VillageSiege.func_75529_b over World.playerEntities (the overworld's one
 * player, when it is there), on the tick's World.rand. */
static int sr_siege_muster(void *ctx, jrand *rand)
{
    struct serverreplay *sr = ctx;
    if (sr->player == NULL || serverreplay_player_dim(sr) != 0) return 0;
    return vs_muster(&sr->vs, &sr->vc, rand, sr->player->e.pos_x, sr->player->e.pos_y, sr->player->e.pos_z);
}

/* fh_env.drop: defined with the entity pass below, pointed at by the load. */
static void sr_fh_hurt(void *ctx, fh_ent *faller, float amount);
static int sr_chunk_in_search(int cx, int cy, int cz, const struct aabb *box);
static void sr_fh_drop(void *ctx, const fh_drop *d);
static void sr_block_env_drop(void *ctx, int id, uint64_t rand_state, int64_t msb, int64_t lsb,
                              double x, double y, double z, float yaw, float hover,
                              double mx, double mz, int item, int damage, int count);

/* The chunkload engine's events: an unload has already taken the chunk out of
 * the world (cl_unload_step calls world_unload_chunk itself), so the only work
 * left is Chunk.onChunkUnload's entity half, the same one Java's
 * World.updateEntities does before its walk. */
static void sr_cl_event(void *ctx, const struct cl_event *e);
static void sr_player_ghost_unload(struct serverreplay *sr, int g);

/* servertick's on_unload_step: ChunkProviderServer.unloadQueuedChunks. */
static void sr_unload_step(void *ctx, int t);
static void sr_on_chunk(void *ctx, int cx, int cz);
static void sr_hell_on_chunk(void *ctx, int cx, int cz);
static struct chunk *sr_hell_provide(void *ctx, int cx, int cz);
static void sr_tiles_loaded(struct world *w, struct chunk *c);
static void sr_dispenser_births(struct serverreplay *sr, const struct chunk *c);
static void sr_arrow_tnt(struct ie_world *iew, struct ie_ent *arrow, int x, int y, int z);
static void sr_arrow_button(struct ie_world *iew, struct ie_ent *arrow, int x, int y, int z);
void sr_constructor_begin(det_state *fake, const det_state *real, int role);
void sr_constructor_end(det_state *real, det_state *fake, int role);
static const struct sp_list *sr_hell_possible_creatures(void *ctx, int type, int x, int y, int z);
static const struct sp_list *sr_possible_creatures(void *ctx, int type, int x, int y, int z);
static int sr_spawn_blocked(void *ctx, const struct aabb *box);
static void sr_hell_ensure_chunk(void *ctx, int cx, int cz);
static void sr_sky_on_chunk(void *ctx, int cx, int cz);
static struct chunk *sr_sky_provide(void *ctx, int cx, int cz);
static void sr_sky_ensure_chunk(void *ctx, int cx, int cz);
static void sr_restore_chunk_entities(struct serverreplay *sr, int cx, int cz);
static void sr_restore_arrow(struct serverreplay *sr, struct sr_saved_entity *saved);
static void sr_restore_saved_entities(struct serverreplay *sr, int cx, int cz);
static void sr_end_track(struct serverreplay *sr, int crystal);
static void sr_end_hooks(struct serverreplay *sr);
/* The replay the End's pool callbacks reach (they are handed the ie_world). */
#define sr_end_ctx (nw_env->serverreplay.end_ctx)
static int sr_end_query_other(struct ie_world *iew, struct aabb box, void **out, struct aabb *boxes, int cap);
static void sr_end_mob_collide(void *ctx, struct living *l, const struct aabb *box);
static int sr_end_mob_collide_before(void *ctx, const struct living *q);
static void sr_end_other_hit(struct ie_world *iew, struct ie_ent *pr, void *other);
static void sr_dragon_shadow_sync(struct serverreplay *sr);
static void sr_transfer_player(struct serverreplay *sr, int to_dim);
static int sr_order_find_player(const struct serverreplay *sr);
static void sr_restore_chunk_ticks(struct serverreplay *sr, int cx, int cz);
static void sr_player_send_chunks(struct serverreplay *sr, struct world *w);
static void sr_player_world_update(struct serverreplay *sr);
static void sr_action_hooks(struct serverreplay *sr);
static void sr_living_portal_travel(void *ctx, struct living *l, int to);
static void sr_living_post_update(void *ctx);
static void sr_ie_portal_travel(struct ie_world *iew, ie_ent *en, int to);
static void sr_ie_post_update(struct ie_world *iew);
static void sr_fh_portal_travel(struct fh_world *fw, struct fh_ent *en, int to);
static void sr_fh_post_update(struct fh_world *fw);
static void sr_action_unhook(struct serverreplay *sr);
static void sr_action_order_new(struct serverreplay *sr);
static void sr_order_remove(struct serverreplay *sr, int k);
static void sr_spawner_ensure_chunk(void *ctx, int cx, int cz);
static struct chunk *sr_provide(void *ctx, int cx, int cz);
static struct chunk *sr_provide_home(struct serverreplay *sr, int dim, int cx, int cz);
static int sr_saved_has(struct serverreplay *sr, int cx, int cz);
static struct chunk *sr_saved_take(struct serverreplay *sr, int cx, int cz);
static void sr_track_spawn(struct an_world *an, struct living *l, void *ctx)
{
    struct serverreplay *sr = ctx;
    /* a silverfish out of a monster egg (the player's break, an ally's
     * summon) joins the loaded entity list: World.countEntities counts it
     * in the monster cap (an IMob without a list row) and its box blocks a
     * natural spawn */
    int sk = sr_spawn_kind(l->kind);
    if (sk >= 0 && an == &sr->d->anw) spawner_register_living(&sr->d->spawner, sk, l->entity_id, &l->e);
    /* a slime's split children (slimes.c) come here for the count only: the
     * tracker takes them in its own pass over the pool (combat.c), the
     * entry timing fight-magmacube-s1 holds */
    if (l->kind == SK_SLIME || l->kind == SK_MAGMA_CUBE) return;
    combat_track_spawn(ctx, l);
}

static void sr_egg_chicken(struct ie_world *iew, struct ie_ent *egg);
static void sr_anw_egg_chicken(struct ie_world *iew, struct ie_ent *egg);
static struct chunk *sr_chunk_load_cb(void *ctx, int cx, int cz);
static int sr_chunk_unload_cb(void *ctx, int cx, int cz);
static ie_ent *sr_item_from_nbt(struct serverreplay *sr, ie_world *iew, const nbt *tag, int kind);
static void sr_save_item(struct serverreplay *sr, const ie_ent *ie, int pool, int cx, int cz);
static void sr_restore_item(struct serverreplay *sr, struct sr_saved_entity *saved);

static void sr_client_chunk(struct serverreplay *sr, char kind, int cx, int cz)
{
    if (!sr->stream_chunks) return;
    if (sr->ncchunks == sr->capcchunks)
    {
        sr->capcchunks = sr->capcchunks ? sr->capcchunks * 2 : 64;
        sr->cchunks = realloc(sr->cchunks, (size_t)sr->capcchunks * sizeof *sr->cchunks);
    }
    sr->cchunks[sr->ncchunks++] = (struct sr_client_chunk){kind, sr->here, cx, cz};
}

/* PlayerInstance.removePlayer's S21 for a ready chunk. */
static void sr_client_unload(void *ctx, int t, int cx, int cz)
{
    (void)t;
    sr_client_chunk(ctx, 'u', cx, cz);
}

/* EntityPlayerMP.onUpdate's chunk send, at the player's place in the entity
 * pass: one S26 bulk of up to S26PacketMapChunkBulk.func_149258_c() = 5. */
static void sr_send_chunks(struct serverreplay *sr, int t)
{
    int out[5][2];
    int n = cl_send_chunks(sr->d->cl, t, out, 5);
    for (int i = 0; i < n; ++i) sr_client_chunk(sr, 'l', out[i][0], out[i][1]);
    /* EntityTracker.func_85172_a per sent chunk, after the bulk */
    for (int i = 0; i < n; ++i) combat_chunk_sent(sr, out[i][0], out[i][1]);
}

/* A snapshot that records the player's send queue (player_server.nbt's
 * sendQueue, every snapshot since the any-tick sweep) resumes the chunk
 * stream where it was, not from a fresh login: the login's modelled sends and
 * unloads are dropped, the queue is EntityPlayerMP.loadedChunks as recorded,
 * and the chunk packets the client had not pumped yet (player_client.nbt's
 * pending S26 bulks and S21s) are queued for its first tick. A partial S21
 * (not ground-up) carries block changes, which the client world already has
 * from the server's copy. Returns 0 for an older snapshot. */
static int sr_resume_stream(struct serverreplay *sr, const struct snapshot *snap)
{
    const nbt *sq = nbt_get(nbt_get(snap->player_server, "fields"), "sendQueue");
    if (sq == NULL) return 0;
    int n = 0;
    const int *v = nbt_int_array(sq, &n);
    cl_set_send_queue(sr->d->cl, v, n / 2);
    sr->ncchunks = 0;
    const nbt *pend = nbt_get(snap->player_client, "pending");
    for (int i = 0; i < (pend ? nbt_list_size(pend) : 0); ++i)
    {
        const nbt *pk = nbt_list_get(pend, i);
        const char *cls = nbt_string_value(nbt_get(pk, "cls"));
        if (cls == NULL) continue;
        if (strcmp(cls, "S26PacketMapChunkBulk") == 0)
        {
            int nx = 0, nz = 0;
            const int *xs = nbt_int_array(nbt_get(pk, "field_149266_a"), &nx);
            const int *zs = nbt_int_array(nbt_get(pk, "field_149264_b"), &nz);
            for (int k = 0; xs && zs && k < nx && k < nz; ++k) sr_client_chunk(sr, 'l', xs[k], zs[k]);
        }
        else if (strcmp(cls, "S21PacketChunkData") == 0)
        {
            int x = (int)nbt_int_value(nbt_get(pk, "field_149284_a"));
            int z = (int)nbt_int_value(nbt_get(pk, "field_149282_b"));
            int ground_up = (int)nbt_int_value(nbt_get(pk, "field_149279_g"));
            int mask = (int)nbt_int_value(nbt_get(pk, "field_149283_c"));
            if (ground_up) sr_client_chunk(sr, mask == 0 ? 'u' : 'l', x, z);
        }
    }
    return 1;
}

/* One "l:"-prefixed long out of the det.nbt canonical tree, as
 * servertick_load_det reads its fields. */
static int64_t sr_det_long(const nbt *v)
{
    if (v == NULL) return 0;
    char *t = nbt_render(v);
    if (t == NULL) return 0;
    char *p = t;
    if (*p == '"') ++p;
    if (p[0] != 0 && p[1] == ':') p += 2;
    int64_t out = strtoll(p, NULL, 10);
    free(t);
    return out;
}

/* A snapshot taken in (or after visiting) another dimension carries that
 * world's loaded chunks: they replace its provider's empty world (the same
 * world_init, the chunks already in their loadedChunks order). The caller
 * sets the provider hooks afterwards. */
static void sr_adopt_world(struct world *dst, struct world *src)
{
    world_free(dst);
    *dst = *src;
    memset(src, 0, sizeof *src);
    chunkgen_init(&dst->gen, dst->seed);
}

/* The rest of a non-overworld dimension's snapshot state, with that
 * dimension entered and its region store made. The region-file chunks the
 * snapshot read at its own time (saved-chunks.bin.gz: not the checkpoint's
 * copy, since the join's unload drain re-saves chunks after they changed) go
 * to the region store, their Entities as records rebuilt at the chunk's load
 * (EntityList.createEntityFromNBT, filed as the checkpoint path files the
 * overworld's) and their TileTicks as the chunk's saved delays
 * (func_147446_b at the load's clock). Then the world's own unload queue,
 * and the login when the player is here: its loads happened before the
 * snapshot (the drain since then emptied part of the ring), so the rebuilt
 * login loads nothing, and the recorded queue replaces its marks. */
static void sr_adopt_dim_snapshot(struct serverreplay *sr, struct snapshot *snap, int dim)
{
    struct world **saved = dim == -1 ? &snap->hell_saved : &snap->end_saved;
    struct sr_world *ws = sr_dim_ws(sr, dim);

    if (*saved != NULL)
    {
        /* world_take_chunk backward-shifts the slot array: the coordinates
         * first */
        struct world *sw = *saved;
        int *coords = malloc(sizeof(int) * 2 * (sw->used + 1));
        int nc = 0;
        for (size_t i = 0; i < sw->cap; ++i)
        {
            const struct chunk *c = chunk_ptr(sw->slot[i]);
            if (c == NULL) continue;
            coords[nc * 2] = c->cx;
            coords[nc * 2 + 1] = c->cz;
            ++nc;
        }
        for (int k = 0; k < nc; ++k)
        {
            struct chunk *taken = world_take_chunk(sw, coords[k * 2], coords[k * 2 + 1]);
            if (taken == NULL) continue;
            world_put_chunk(&sr->d->saved, taken);
            /* parked, as an unload leaves a chunk */
            chunk_pack(taken);
        }
        free(coords);
        /* the moved chunks' tile entities go with them */
        world_te_registry_move(&sr->d->saved, sw);
        world_free(sw);
        free(sw);
        *saved = NULL;

        for (int i = 0; i < snap->nchunks; ++i)
        {
            struct snap_chunk *sc = &snap->chunks[i];
            if (!sc->saved || sc->dim != dim) continue;

            for (int e = 0; e < sc->nents_saved; ++e)
            {
                const char *cls = nbt_string_value(nbt_get(sc->ents[e], "id"));
                sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
                struct sr_saved_entity *sv = &sr->d->saved_ents[sr->d->nsaved_ents++];
                memset(sv, 0, sizeof *sv);
                sv->active.id = -1;
                sv->cx = sc->cx;
                sv->cz = sc->cz;
                sv->seq = (uint64_t)e;
                int section = (int)floor(cp_dat(sc->ents[e], "Pos", 1) / 16.0);
                sv->section = section < 0 ? 0 : section > 15 ? 15 : section;
                sv->tag = sc->ents[e];
                sc->ents[e] = NULL;
                sv->uuid_msb = nbt_int_value(nbt_get(sv->tag, "UUIDMost"));
                sv->uuid_lsb = nbt_int_value(nbt_get(sv->tag, "UUIDLeast"));
                if (cls && (strcmp(cls, "Item") == 0 || strcmp(cls, "XPOrb") == 0))
                {
                    sv->pool = 5;
                    sv->kind = strcmp(cls, "Item") == 0 ? IE_ITEM : IE_ORB;
                }
                else
                {
                    sv->pool = 3;
                    sv->kind = -1;
                    sv->cls = strdup(cls ? cls : "?");
                }
            }

            if (sc->nsaved_ticks == 0) continue;
            if (sr->d->nchunk_ticks == sr->d->capchunk_ticks)
            {
                int cap = sr->d->capchunk_ticks ? sr->d->capchunk_ticks * 2 : 64;
            sr->d->chunk_ticks = slab_grow(sr->d->chunk_ticks, (size_t)sr->d->capchunk_ticks * sizeof *sr->d->chunk_ticks,
                                           (size_t)cap * sizeof *sr->d->chunk_ticks);
            sr->d->capchunk_ticks = cap;
            }
            struct sr_chunk_ticks *ct = &sr->d->chunk_ticks[sr->d->nchunk_ticks++];
            ct->cx = sc->cx;
            ct->cz = sc->cz;
            ct->n = sc->nsaved_ticks;
            ct->ticks = slab_take((size_t)ct->n * sizeof *ct->ticks);
            for (int k = 0; k < ct->n; ++k)
            {
                ct->ticks[k].x = sc->saved_ticks[k].x;
                ct->ticks[k].y = sc->saved_ticks[k].y;
                ct->ticks[k].z = sc->saved_ticks[k].z;
                ct->ticks[k].block = sc->saved_ticks[k].id;
                ct->ticks[k].delay = (int)sc->saved_ticks[k].scheduled;
                ct->ticks[k].priority = sc->saved_ticks[k].priority;
            }
        }
    }

    if (sr->join_dim != dim)
    {
        cl_restore_unload(sr->d->cl, ws->unload, ws->nunload, ws->unload_bins);
        return;
    }

    cl_set_no_load(sr->d->cl, 1);
    sr->stream_chunks = snap->version >= 2;
    cl_on_client_unload(sr->d->cl, sr_client_unload, sr);
    /* the from-run's server is fresh: its managers hold the constructor's
     * view distance (IntegratedPlayerList's 10) until tick 1's tail applies
     * the client's render distance */
    cl_set_radius(sr->d->cl, 10);
    cl_login(sr->d->cl, 0, sr->join_x, sr->join_z);
    int resume = nbt_get(nbt_get(snap->player_server, "fields"), "sendQueue") != NULL;
    if (sr->stream_chunks && !resume) sr_send_chunks(sr, 1);
    cl_view_radius(sr->d->cl, 1, ws->view);
    if (resume) sr_resume_stream(sr, snap);
    cl_restore_unload(sr->d->cl, ws->unload, ws->nunload, ws->unload_bins);
    cl_set_no_load(sr->d->cl, 0);
    for (int k = 0; k + 2 < ws->npm_inst; k += 3)
        cl_set_mark(sr->d->cl, ws->pm_inst[k], ws->pm_inst[k + 1], ws->pm_inst[k + 2]);
}

static struct aabb sr_crystal_box(const struct dragon_crystal_state *c);

/* canPlaceEntityOnSide's entity check on the overworld: the server player, the
 * livings, the falling blocks and primed TNT, the entities that set
 * preventEntitySpawning. The Nether and the End keep no entity list here. */
static int sr_place_entity_in(void *ctx, struct world *w, const struct aabb *box, int skip_placer)
{
    struct serverreplay *sr = ctx;

    /* the world the placement runs in is the one entered (its lists are
     * sr->d's): the Nether's and the End's too */
    if (w != sr_dim_world(sr, sr->here)) return 0;

    /* the placer, unless it is the excluded entity: a player whose client
     * held another slot (func_150936_a checks the client's stack) places an
     * ItemBlock into its own box */
    if (!skip_placer && sr->player != NULL && !sr->player->sv.dead && !sr->player->sv.removed &&
        aabb_intersects(box, &sr->player->e.bounding_box))
        return 1;

    for (int i = 0; i < sr->d->nents; ++i)
    {
        if (sr_ent_p(sr, &sr->d->ents[i]) == NULL) continue;

        if (sr->d->ents[i].pool == 0)
        {
            const ie_ent *ie = sr_ent_p(sr, &sr->d->ents[i]);
            if (ie->kind == IE_TNT && !ie->is_dead && aabb_intersects(box, &ie->e.bounding_box)) return 1;
        }
        else if (sr->d->ents[i].pool == 1)
        {
            const fh_ent *fh = sr_ent_p(sr, &sr->d->ents[i]);
            if (fh->kind == FH_FALLING && !fh->is_dead && aabb_intersects(box, &fh->e.bounding_box)) return 1;
        }
        else if (sr->d->ents[i].pool == 2)
        {
            const struct an_ent *en = sr_ent_p(sr, &sr->d->ents[i]);
            if (en->is_living)
            {
                if (!lv_get(en->livh)->is_dead && aabb_intersects(box, &lv_get(en->livh)->e.bounding_box)) return 1;
            }
            else if (en->ieh != 0 && ie_get(en->ieh)->kind == IE_TNT && !ie_get(en->ieh)->is_dead &&
                     aabb_intersects(box, &ie_get(en->ieh)->e.bounding_box))
                return 1;
        }
    }

    /* the End's own lists: an ender crystal (its constructor sets
     * preventEntitySpawning) and the dragon (EntityLivingBase's; its parts
     * leave it false), live and in the world */
    if (sr->here == 1 && sr->end != NULL)
    {
        const struct dragon_state *d = &sr->end->d;
        for (int i = 0; i < d->n_crystals; ++i)
        {
            const struct dragon_crystal_state *c = &d->crystals[i];
            if (!c->in_world || c->dead) continue;
            struct aabb cb = sr_crystal_box(c);
            if (aabb_intersects(box, &cb)) return 1;
        }
        if (sr->end->dragon_listed && !d->dead)
        {
            struct aabb db = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
            if (aabb_intersects(box, &db)) return 1;
        }
    }

    return 0;
}

static void sr_saved_context(struct serverreplay *sr);

/* A mid-run snapshot's worldTeleporter: the portal positions it has cached
 * (a trip back reuses them) and its Random (makePortal's orientation draw). */
static void sr_teleporter_restore(struct serverreplay *sr, struct teleporter *tp, int dim)
{
    for (int i = 0; i < sr->nworlds; ++i)
    {
        struct sr_world *sw = &sr->w[i];
        if (sw->dim != dim || !sw->has_tp) continue;
        TP_RAND(tp).seed = sw->tp_rand;
        portal_cache_free(&tp->cache);
        tp->cache = sw->tp_cache;
        memset(&sw->tp_cache, 0, sizeof sw->tp_cache);
        sw->has_tp = 0;
    }
}

/* A snapshot older than det.nbt's "shuf" (fresh-play-s1 among them): the
 * shuffle Random is still the one Det.reset installed at the world's launch,
 * new Random(mix(R, 9)) with R the world seed for a seed start and
 * mix(seed, totalTime) for a checkpoint's (Oracle.java's Det.reset call; the
 * tape header's start names which). Nothing shuffles before tick 2. */
/* The tape header's start: 1 for a checkpoint's (its totalTime in *total_time),
 * 0 for a seed's. */
static int sr_start_checkpoint(const char *dir, int64_t *total_time)
{
    char path[1200], *line = NULL;
    size_t cap = 0;
    int cp = 0;
    snprintf(path, sizeof path, "%s/tape.jsonl", dir);
    FILE *f = fopen(path, "r");
    if (f && getline(&line, &cap, f) > 0)
    {
        const char *st = strstr(line, "\"start\":{");
        const char *end = st ? strchr(st, '}') : NULL;
        const char *kind = st ? strstr(st, "\"kind\":\"checkpoint\"") : NULL;
        const char *tt = st ? strstr(st, "\"totalTime\":") : NULL;
        if (kind && kind < end)
        {
            cp = 1;
            if (total_time) *total_time = tt && tt < end ? strtoll(tt + 12, NULL, 10) : 0;
        }
    }
    free(line);
    if (f) fclose(f);
    return cp;
}

static void sr_shuf_from_reset(struct serverreplay *sr, const struct snapshot *snap, const char *dir)
{
    uint64_t reset = (uint64_t)snap->seed;
    int64_t tt = 0;
    if (sr_start_checkpoint(dir, &tt)) reset = det_mix((uint64_t)snap->seed, (uint64_t)tt);
    jr_seed(&SR_SHUF(sr), (int64_t)det_mix(reset, 9));
}

/* seedents.jsonl.gz's chunks by cx, then cz, and the one at cx, cz (or NULL) */
static int sr_seed_chunk_cmp(const void *a, const void *b)
{
    const struct snap_seed_chunk *x = a, *y = b;
    if (x->cx != y->cx) return x->cx < y->cx ? -1 : 1;
    return x->cz < y->cz ? -1 : x->cz > y->cz;
}

static const struct snap_seed_chunk *sr_seed_chunk(const struct serverreplay *sr, int cx, int cz)
{
    struct snap_seed_chunk key = {.cx = cx, .cz = cz};
    return sr->nseed_chunks > 0 ? bsearch(&key, sr->seed_chunks, (size_t)sr->nseed_chunks, sizeof key, sr_seed_chunk_cmp) : NULL;
}

/* The world's tile entity list in the recorded order (x,y,z triples); any
 * the record does not name keep their order behind (load time only). */
static void sr_te_reorder(struct world *w, const int *order, int n)
{
    if (w->te_n < 2) return;
    struct tile_entity **out = malloc((size_t)w->te_n * sizeof *out);
    if (out == NULL) abort();
    int k = 0;
    for (int j = 0; j + 2 < n; j += 3)
        for (int i = 0; i < w->te_n; ++i)
        {
            struct tile_entity *te = w->te_list[i];
            if (te == NULL || te->x != order[j] || te->y != order[j + 1] || te->z != order[j + 2]) continue;
            out[k++] = te;
            w->te_list[i] = NULL;
            break;
        }
    for (int i = 0; i < w->te_n; ++i)
        if (w->te_list[i] != NULL) out[k++] = w->te_list[i];
    memcpy(w->te_list, out, (size_t)k * sizeof *out);
    free(out);
}

int serverreplay_load(struct serverreplay *sr, struct snapshot *snap, const char *dir)
{
    memset(sr, 0, sizeof *sr);
    sr->player_removed_tick = -1;
    /* the replay's streams start as its struct did: zero (env.h struct rng_table) */
    memset(nw_env->rng.world, 0, sizeof nw_env->rng.world);
    memset(&nw_env->rng.shuffle, 0, sizeof nw_env->rng.shuffle);
    memset(nw_env->rng.teleport, 0, sizeof nw_env->rng.teleport);
    serverreplay_dims_init(sr);
    sr->flush = calloc(SR_FLUSH_MAX, sizeof *sr->flush);
    sr->snap = snap;
    place_set_entity_hook(sr_place_entity_in, sr);
    det_init(&SR_DET(sr));
    sr->err[0] = 0;

    /* WorldServer.villageSiegeObj and its village collection. The siege's
     * state is the snapshot's own (see the worlds.nbt read below); the
     * collection stays empty because a snapshot carries no village list, so
     * func_75529_b finds no village and makes no draw. */
    vc_init(&sr->vc, &sr->d->anw);
    sr->d->anw.village_collection = &sr->vc;
    vs_init(&sr->vs, &sr->d->anw);
    sr->d->anw.village_siege = &sr->vs;
    int unload_coords[4096];
    /* the overworld PlayerManager's instance marks, triplets cx, cz, age */
    int *pm_inst = NULL;
    int npm_inst = -1;
    int nunload = -1;
    int unload_bins = 0;

    char path[1200];
    snprintf(path, sizeof path, "%s/worlds.nbt", dir);
    char *text = NULL;

    if (!read_file(path, &text))
    {
        set_err(sr, "no worlds.nbt");
        return 0;
    }

    nbt *root = nbt_parse(text);
    free(text);

    if (root == NULL)
    {
        set_err(sr, "worlds.nbt does not parse");
        return 0;
    }

    const nbt *order = nbt_get(root, "order");
    int nw = order ? nbt_list_size(order) : 0;

    if (nw < 1 || nw > 3)
    {
        set_err(sr, "worlds.nbt carries %d worlds", nw);
        nbt_free(root);
        return 0;
    }

    sr->nworlds = nw;

    populate_init(&sr->pop, snap->seed);
    /* the structure maps: the snapshot's own when it records them, in their
     * HashMap iteration order; else the spawn loop's offers */
    {
        char spath[1200];
        char *stext = NULL;
        snprintf(spath, sizeof spath, "%s/structures.json", dir);
        nbt *st = read_file(spath, &stext) ? nbt_parse(stext) : NULL;
        free(stext);
        const nbt *w0 = st ? nbt_get(st, "w0") : NULL;
        if (w0 != NULL)
        {
            static const char *names[4] = {"Mineshaft", "Village", "Stronghold", "Temple"};
            for (int t = 0; t < 4; ++t)
            {
                int coords[4096];
                int n = scalar_int_array(nbt_get(w0, names[t]), coords, 4096);
                for (int i = 0; i + 1 < n; i += 2) populate_add_start(&sr->pop, t, coords[i], coords[i + 1]);
                /* the stronghold starts in the map, and which of them link
                 * their portal room (a snapshot without the list: all, but
                 * a checkpoint start's, which the Java run read back from
                 * the save's structure data: none) */
                if (t == 2)
                {
                    int linked[64];
                    const nbt *pt = nbt_get(w0, "Stronghold.portal");
                    int np = pt != NULL ? scalar_int_array(pt, linked, 64) : 0;
                    int saved = pt == NULL && sr_start_checkpoint(dir, NULL);
                    for (int i = 0; i + 1 < n && i / 2 < 64; i += 2)
                        stronghold_note_start(&snap->world, coords[i], coords[i + 1],
                                              i / 2 >= np ? !saved : linked[i / 2]);
                }
                /* each start's components as the snapshot holds them (a
                 * snapshot before them has none: the rebuilt ones stand) */
                char pkey[32];
                snprintf(pkey, sizeof pkey, "%s.pieces", names[t]);
                const nbt *pl = nbt_get(w0, pkey);
                char fkey[32];
                snprintf(fkey, sizeof fkey, "%s.flags", names[t]);
                const nbt *fl = nbt_get(w0, fkey);
                for (int i = 0; pl != NULL && i < nbt_list_size(pl) && 2 * i + 1 < n; ++i)
                {
                    int nv = 0, nf = 0;
                    const int *v = nbt_int_array(nbt_list_get(pl, i), &nv);
                    const int *f = fl != NULL && i < nbt_list_size(fl) ? nbt_int_array(nbt_list_get(fl, i), &nf) : NULL;
                    if (!populate_restore_pieces(&sr->pop, t, coords[2 * i], coords[2 * i + 1], v, nv,
                                                 f != NULL && nf == nv / 8 ? f : NULL))
                    {
                        set_err(sr, "structures.json: %s start %d,%d does not match its rebuilt pieces", names[t],
                                coords[2 * i], coords[2 * i + 1]);
                        nbt_free(st);
                        return 0;
                    }
                }
            }
        }
        else populate_initial_chunks(&sr->pop);
        nbt_free(st);
    }
    world_free(&sr->pop.world);
    sr->pop.world = snap->world;
    memset(&snap->world, 0, sizeof snap->world);
    /* the tracker entries the snapshot recorded, applied by combat_bind */
    sr->trk = NULL;
    sr->ntrk = 0;
    for (int i = 0; i < snap->nents; ++i)
    {
        const struct snap_entity *se = &snap->ents[i];
        if (!se->has_trk) continue;
        sr->trk = realloc(sr->trk, (size_t)(sr->ntrk + 1) * sizeof *sr->trk);
        struct sr_trk *k = &sr->trk[sr->ntrk++];
        k->id = se->id;
        memcpy(k->v, se->trk, sizeof k->v);
        memcpy(k->d, se->trkd, sizeof k->d);
    }
    /* the saved pending ticks of the unloaded chunks, verbatim from the
     * region files: a chunk re-adds exactly these at its next load */
    sr->nseed_ticks = snap->nseed_ticks;
    sr->has_seedticks = snap->has_seedticks;
    /* each biome object's WorldGenBigTree.heightLimit, which the JVM keeps
     * from the first big tree of that biome on (decorator.c's bigtree_state) */
    {
        int nbt_n = 0;
        const nbt *btv = nbt_get(snap->worldstate, "bigTree");
        const int *bt = btv ? nbt_int_array(btv, &nbt_n) : NULL;
        sr->has_bigtree = bt != NULL;
        for (int i = 0; bt && i < nbt_n && i < (int)(sizeof nw_env->decorator.bigtree_state / sizeof nw_env->decorator.bigtree_state[0]); ++i)
            nw_env->decorator.bigtree_state[i] = bt[i];
        /* each leaf block's decay search array's centre cell (randomtick.c
         * rt_leaves): 0, a fresh array's, when the snapshot predates it */
        const nbt *ldv = nbt_get(snap->worldstate, "leafDecay");
        const int *ld = ldv ? nbt_int_array(ldv, &nbt_n) : NULL;
        for (int i = 0; i < 2; ++i) nw_env->leaves.decay_centre[i] = ld && i < nbt_n ? ld[i] : 0;
        /* the fences' and the stairs' shared bounds (collide.c): a fence's
         * as they are, a stairs block's octant flag */
        const nbt *bbv = nbt_get(snap->worldstate, "blockBounds");
        const int *bb = bbv ? nbt_int_array(bbv, &nbt_n) : NULL;
        for (int k = 0; bb && k + 6 < nbt_n; k += 7)
        {
            float f[6];
            memcpy(f, &bb[k + 1], sizeof f);
            int id = bb[k];
            if (id == 85 || id == 113) collide_fence_bounds_set(id, f[0], f[1], f[2], f[3], f[4], f[5]);
            else if (id == 65) memcpy(nw_env->collide.ladder_bounds, f, sizeof f);
            else if (id == 117) nw_env->collide.brewing_base = f[4] == 0.125F;
            else if (id == 120) nw_env->collide.item_bounds[120] = f[4] == 0.8125F;
            else if (id == 119) nw_env->collide.item_bounds[119] = f[4] == 0.0625F;
            else if (id > 0 && id < 4096)
                nw_env->collide.stairs_octant[id] = f[0] == 0.5F && f[1] == 0.5F && f[2] == 0.5F &&
                                                    f[3] == 1.0F && f[4] == 1.0F && f[5] == 1.0F;
        }
    }
    sr->has_seedents = snap->has_seedents;
    sr->seed_chunks = NULL;
    sr->nseed_chunks = 0;
    if (snap->nseed_chunks > 0)
    {
        sr->seed_chunks = calloc((size_t)snap->nseed_chunks, sizeof *sr->seed_chunks);
        if (!sr->seed_chunks) { set_err(sr, "out of memory for the seed entities"); return 0; }
        for (int i = 0; i < snap->nseed_chunks; ++i)
        {
            const struct snap_seed_chunk *from = &snap->seed_chunks[i];
            struct snap_seed_chunk *to = &sr->seed_chunks[sr->nseed_chunks++];
            to->cx = from->cx;
            to->cz = from->cz;
            to->nents = from->nents;
            to->ents = from->nents > 0 ? calloc((size_t)from->nents, sizeof *to->ents) : NULL;
            if (from->nents > 0 && !to->ents) { set_err(sr, "out of memory for the seed entities"); return 0; }
            for (int k = 0; k < from->nents; ++k) to->ents[k] = nbt_copy_canonical(from->ents[k]);
        }
        qsort(sr->seed_chunks, (size_t)sr->nseed_chunks, sizeof *sr->seed_chunks, sr_seed_chunk_cmp);
    }
    sr->seed_ticks = NULL;
    if (snap->nseed_ticks > 0)
    {
        sr->seed_ticks = malloc((size_t)snap->nseed_ticks * sizeof *sr->seed_ticks);
        if (!sr->seed_ticks) { set_err(sr, "out of memory for the seed ticks"); return 0; }
        memcpy(sr->seed_ticks, snap->seed_ticks, (size_t)snap->nseed_ticks * sizeof *sr->seed_ticks);
    }
    /* The layer graph stores pointers to nodes inside struct chunkgen. Moving
     * the snapshot world by value leaves those pointers at the old address;
     * rebuild the provider before it generates a chunk outside the snapshot. */
    chunkgen_init(&sr->pop.world.gen, sr->pop.world.seed);
    sr->pop.world.on_chunk = sr_on_chunk;
    sr->pop.world.on_chunk_ctx = sr;

    for (int i = 0; i < nw; ++i)
    {
        const nbt *v = nbt_list_get(order, i);

        if (v == NULL)
        {
            /* a plain JSON number list would read as a list of scalars too */
            set_err(sr, "worlds.nbt order[%d] is not an int", i);
            nbt_free(root);
            return 0;
        }

        char key[16];
        snprintf(key, sizeof key, "w%d", i);
        const nbt *w = nbt_get(root, key);

        if (w == NULL)
        {
            set_err(sr, "worlds.nbt has no %s", key);
            nbt_free(root);
            return 0;
        }

        struct sr_world *sw = &sr->w[i];
        memset(sw, 0, sizeof *sw);
        servertick_reserve(&sw->st);
        sw->dim = (int)scalar_long(w, "dim");
        sw->st.rng_slot = sr_dim_index(sw->dim);
        sw->is_overworld = i == 0;

        /* the overworld's chunks are the snapshot's struct world; the Nether's
         * and the End's are the snapshot's own dim worlds when it has them */
        if (sw->is_overworld) sw->st.w = &sr->pop.world;
        else if (sw->dim == -1) sw->st.w = &sr->hell.world;

        sw->chunks = (int)scalar_long(w, "chunks");
        sw->pending = (int)scalar_long(w, "ticks") + 0;
        sw->pending = 0;
        sw->st.det = &SR_DET(sr);
        ST_RAND(&sw->st).seed = (uint64_t)scalar_long(w, "rand") & ((1ULL << 48) - 1);
        ST_LCG(&sw->st) = (int)scalar_long(w, "updateLCG");
        sw->st.skylight_subtracted = (int)scalar_long(w, "skylightSubtracted");
        sw->st.prev_raining_strength = scalar_float(w, "prevRainingStrength");
        sw->st.raining_strength = scalar_float(w, "rainingStrength");
        sw->st.prev_thundering_strength = scalar_float(w, "prevThunderingStrength");
        sw->st.thundering_strength = scalar_float(w, "thunderingStrength");
        sw->st.all_players_sleeping = (int)scalar_long(w, "allPlayersSleeping");
        sw->st.total_time = scalar_long(w, "total");
        sw->st.world_time = scalar_long(w, "time");
        sw->st.daylight_cycle = 1;
        sw->st.raining = (int)scalar_long(w, "raining");
        sw->st.thundering = (int)scalar_long(w, "thundering");
        sw->st.rain_time = (int)scalar_long(w, "rainTime");
        sw->st.thunder_time = (int)scalar_long(w, "thunderTime");
        sw->st.next_tick_entry = scalar_long(w, "nextTickEntryID");
        sw->st.ambient_tick_countdown = (int)scalar_long(w, "ambientTickCountdown");
        if (nbt_get(w, "tpRand") != NULL)
        {
            const nbt *tc = nbt_get(w, "tpCache");
            sw->has_tp = 1;
            sw->tp_rand = (uint64_t)scalar_long(w, "tpRand") & ((1ULL << 48) - 1);
            portal_cache_init(&sw->tp_cache);
            for (int k = 0; tc && k < nbt_list_size(tc); ++k)
            {
                const nbt *e = nbt_list_get(tc, k);
                portal_cache_put(&sw->tp_cache, scalar_long(e, "k"), (int)scalar_long(e, "x"), (int)scalar_long(e, "y"),
                                 (int)scalar_long(e, "z"), scalar_long(e, "t"));
            }
        }
        {
            int ton = 0;
            const nbt *tov = nbt_get(w, "teOrder");
            const int *to = tov ? nbt_int_array(tov, &ton) : NULL;
            if (to && ton >= 3 && i < 3)
            {
                sr->te_order[i] = malloc((size_t)ton * sizeof *to);
                if (sr->te_order[i] == NULL) abort();
                memcpy(sr->te_order[i], to, (size_t)ton * sizeof *to);
                sr->te_order_n[i] = ton;
            }
        }
        /* the passes this world has already run with no player (a Nether
         * join's overworld counts from the server start) */
        if (nbt_get(w, "updateEntityTick") != NULL)
            sw->update_entity_tick = (int)scalar_long(w, "updateEntityTick");
        sw->st.difficulty = (int)scalar_long(w, "difficulty");

        /* PlayerManager's processChunk mark. A snapshot that predates the
         * record gets the likeliest state: a world past 8,000 ticks swept on
         * its first tick after the load, a younger one has not swept yet. */
        if (nbt_get(w, "pmSweep") != NULL) sw->pm_sweep = scalar_long(w, "pmSweep");
        else sw->pm_sweep = sw->st.total_time > 8000 ? sw->st.total_time : 0;
        if (nbt_get(w, "pmInst") != NULL)
        {
            /* the overworld's marks go to the login below, another world's
             * to its own manager's (sr_adopt_dim_snapshot) */
            int *pi = malloc(sizeof(int) * 3 * 2048);
            int npi = scalar_int_array(nbt_get(w, "pmInst"), pi, 3 * 2048);
            if (npi < 0 || npi % 3 != 0)
            {
                set_err(sr, "worlds.nbt has a malformed pmInst");
                free(pi);
                free(pm_inst);
                nbt_free(root);
                return 0;
            }
            if (i == 0) { pm_inst = pi; npm_inst = npi; }
            else { sw->pm_inst = pi; sw->npm_inst = npi; }
        }

        /* WorldServer.villageSiegeObj's four runtime fields. A snapshot that
         * predates them carries none: it gets field_75536_c = 2, the state the
         * supported recordings were made in (no dusk-window draw, and no
         * village lookup, so no fields move). */
        if (sw->is_overworld)
        {
            int has = nbt_get(w, "siegeC") != NULL;
            sr->vs.field_75536_c = has ? (int)scalar_long(w, "siegeC") : 2;
            sr->vs.field_75535_b = (int)scalar_long(w, "siegeB");
            sr->vs.field_75533_d = (int)scalar_long(w, "siegeD");
            sr->vs.field_75534_e = (int)scalar_long(w, "siegeE");
            /* servertick's roll runs from the same state: a server started
             * at night holds -1 until a daytime tick, so no midnight roll */
            if (has)
            {
                sw->st.siege_phase = sr->vs.field_75536_c;
                sw->st.siege_mustering = sr->vs.field_75535_b;
            }
        }

        /* every world's own unload queue and view distance, and the overworld's
         * active-set model check: the snapshot's playerX/Z is the position the
         * overworld's manager saw, and it is zero when the player is away */
        int nunload_w = -1;
        int coords_w[4096];
        if (snap->version >= 2)
        {
            nunload_w = scalar_int_array(nbt_get(w, "unload"), coords_w, 4096);
            if (nunload_w < 0 || (nunload_w & 1))
            {
                set_err(sr, "world %d has no valid unload queue", i);
                nbt_free(root);
                return 0;
            }
        }
        int bins_w = (int)scalar_long(w, "unloadBins");
        if (i == 0)
        {
            nunload = nunload_w;
            unload_bins = bins_w;
            memcpy(unload_coords, coords_w, sizeof(int) * (size_t)(nunload_w > 0 ? nunload_w : 0));
            if (snap->version >= 2)
            {
                /* The tick each unload save ran at, triplets cx, cz, tick. The
                 * save's clock is tick - 1: unloadQueuedChunks runs before
                 * WorldInfo.incrementTotalWorldTime. Older snapshots have no
                 * record; the seed save keeps their clock-1 approximation. */
                int clocks[4096];
                int nclocks = scalar_int_array(nbt_get(w, "unloadClock"), clocks, 4096);
                if (nclocks >= 0 && (nclocks % 3) == 0)
                {
                    sr->capjoin_clock = nclocks / 3;
                    sr->join_clock = malloc(sizeof(int) * (size_t)(sr->capjoin_clock * 3));
                    sr->njoin_clock = nclocks / 3;
                    memcpy(sr->join_clock, clocks, sizeof(int) * (size_t)nclocks);
                }
                else if (nclocks > 0)
                {
                    set_err(sr, "worlds.nbt has a malformed unloadClock");
                    nbt_free(root);
                    return 0;
                }
            }
            sr->view = (int)scalar_long(w, "viewDistance");
            if (sr->view > SR_MAX_VIEW)
            {
                set_err(sr, "view distance %d past the engine's %d", sr->view, SR_MAX_VIEW);
                nbt_free(root);
                return 0;
            }
            /* the EntityTrackers took their entityViewDistance at the
             * worlds' creation, when IntegratedPlayerList's view distance
             * was still its first 10 (the client's render distance comes
             * later): 144 blocks in every dimension, whatever the load's */
            sr->tracker_view = 10;
            sr->blk_hash = (uint64_t)scalar_long(w, "blkHash");

            /* the active set model against the order the snapshot recorded;
             * the manifest's playerX/Z is the overworld manager's view, which
             * is zero when the player was in another dimension */
            const struct jval *p = json_get(snap->manifest_json, "server");
            const struct jval *px = p ? json_get(p, "playerX") : NULL;
            const struct jval *pz = p ? json_get(p, "playerZ") : NULL;
            uint64_t bx = 0, bz = 0;
            if (px) json_double(px, &bx);
            if (pz) json_double(pz, &bz);
            double x, z;
            memcpy(&x, &bx, 8);
            memcpy(&z, &bz, 8);
            int player_here0 = x != 0.0 || z != 0.0;

            int rec[SR_MAX_ACTIVE * 2];
            int nrec = scalar_int_array(nbt_get(w, "active"), rec, SR_MAX_ACTIVE * 2);
            int mine[SR_MAX_ACTIVE * 2];
            int nmine = sr_active_set(sr, 0.0, 0.0, mine, NULL);

            (void)nmine;

            /* a mid-run snapshot's set was built at the head of the last
             * tick, from where the player stood before that tick moved it;
             * WorldServer.func_147456_g rebuilds it before anything reads
             * it, so only a join snapshot (no send queue) is checked */
            int mid_run = nbt_get(nbt_get(snap->player_server, "fields"), "sendQueue") != NULL;
            if (nrec > 0 && player_here0 && !mid_run)
            {
                int n2 = sr_active_set(sr, x, z, mine, NULL);

                if (n2 != nrec / 2 || memcmp(mine, rec, sizeof(int) * (size_t)nrec) != 0)
                {
                    set_err(sr, "the active chunk set model differs from the snapshot's order");
                    nbt_free(root);
                    return 0;
                }
            }
        }
        else
        {
            /* the Nether's and the End's own queue and view distance, which
             * their player managers restore (sr_adopt_dim_snapshot) */
            sw->nunload = nunload_w > 0 ? nunload_w : 0;
            sw->unload_bins = bins_w;
            sw->unload = malloc(sizeof(int) * (size_t)(sw->nunload + 1));
            memcpy(sw->unload, coords_w, sizeof(int) * (size_t)sw->nunload);
            sw->view = (int)scalar_long(w, "viewDistance");
        }
    }

    /* the manifest's server block, for the per-world pending count and the
     * snapshot's own flag */
    const struct jval *srv = json_get(snap->manifest_json, "server");
    int64_t cal;
    if (json_int(json_get(srv, "calMonth"), &cal)) sr->cal_month = (int)cal;
    if (json_int(json_get(srv, "calDay"), &cal)) sr->cal_day = (int)cal;

    if (srv)
    {
        const struct jval *pp = json_get(srv, "pendingPerWorld");

        for (int i = 0; i < sr->nworlds && pp && i < json_len(pp); ++i)
        {
            int64_t n = 0;
            if (json_int(json_at(pp, i), &n)) sr->w[i].pending = (int)n;
        }
    }

    nbt_free(root);

    /* The Nether has no loaded chunks at this snapshot: its provider
     * generates from the seed on the first transfer (MobFree's
     * generateNether leaves the oracle's Nether provider on), and its region
     * store starts empty. A snapshot taken in (or after visiting) the Nether
     * carries its chunks in the adopted hell_world; the generator is only
     * rebuilt when there is nothing to adopt. */
    populate_init(&sr->hell, snap->seed);
    populate_hell_init(&sr->hell, snap->seed);
    /* the Nether's fortress map as the snapshot recorded it (Fortress.dat's
     * starts come back first, in the map's order); else the offers alone */
    {
        char spath[1200];
        char *stext = NULL;
        snprintf(spath, sizeof spath, "%s/structures.json", dir);
        nbt *st = read_file(spath, &stext) ? nbt_parse(stext) : NULL;
        free(stext);
        const nbt *w1 = st ? nbt_get(st, "w1") : NULL;
        if (w1 != NULL)
        {
            int coords[4096];
            int n = scalar_int_array(nbt_get(w1, "Fortress"), coords, 4096);
            for (int i = 0; i + 1 < n; i += 2) populate_hell_add_start(&sr->hell, coords[i], coords[i + 1]);
        }
        nbt_free(st);
    }
    if (snap->hell_loaded)
    {
        sr_adopt_world(&sr->hell.world, &snap->hell_world);
        sr->hell_live = 1;
    }
    sr->hell.world.dim = -1;
    sr->hell.world.on_chunk = sr_hell_on_chunk;
    sr->hell.world.on_chunk_ctx = sr;
    sr->hell.world.provide = sr_hell_provide;
    sr->hell.world.provide_ctx = sr;
    const struct jval *no_gen_worlds = srv ? json_get(srv, "noChunkGeneration") : NULL;
    int64_t nether_no_gen = 1;
    if (no_gen_worlds && json_len(no_gen_worlds) > 1)
        json_int(json_at(no_gen_worlds, 1), &nether_no_gen);
    sr->hell.world.no_generate = nether_no_gen != 0;
    for (int i = 0; i < sr->nworlds; ++i)
    {
        if (sr->w[i].dim != -1) continue;
        sr->w[i].st.w = &sr->hell.world;
        /* VillageSiege starts at -1 and only a daytime tick zeroes it: the
         * Nether never has one, so it never rolls for a siege */
        sr->w[i].st.siege_phase = -1;
    }
    /* each WorldServer's Teleporter: new Random(world.getSeed()) */
    teleporter_init(&sr->tp_over, &sr->pop.world);
    teleporter_init(&sr->tp_hell, &sr->hell.world);
    jr_seed(&TP_RAND(&sr->tp_over), snap->seed);
    jr_seed(&TP_RAND(&sr->tp_hell), snap->seed);
    sr_teleporter_restore(sr, &sr->tp_over, 0);
    sr_teleporter_restore(sr, &sr->tp_hell, -1);

    /* Det's streams: one shared state, the SERVER role's, exactly as Det is one
     * static per role in Java. Every world's tick draws from it. */
    if (snap->det == NULL)
    {
        set_err(sr, "the snapshot holds no det.nbt");
        return 0;
    }

    servertick_load_det(&sr->w[0].st, &SR_DET(sr), snap->det);
    /* Det.worldSeed, which seeds a split Random made after the snapshot: the
     * world seed, or mix(seed, TotalTime) after a checkpoint load */
    if (nbt_get(snap->det, "worldSeed") != NULL)
        SR_DET(sr).world_seed = nbt_int_value(nbt_get(snap->det, "worldSeed"));

    for (int i = 1; i < sr->nworlds; ++i) sr->w[i].st.det = &SR_DET(sr);

    /* the overworld's entity lists, the entity pass both pools drive: the
     * snapshot's own entities are adopted by the caller (the tape's rows carry
     * them), the tick's spawns arrive through the on_spawn hook */
    ie_init(&sr->d->iew, &sr->pop.world, &SR_DET(sr));
    /* a drop before the first entity pass (a dev teleport's population at
     * the head of the first tick) draws the server's ids, as every later one */
    sr->d->iew.role = DET_SERVER;
    sr->d->iew.collide_entities = 1;
    sr->d->iew.user_data = sr;
    sr->d->iew.on_egg_chicken = sr_egg_chicken;
    sr->d->iew.on_tnt_explode = tnt_replay_explode;
    sr->d->iew.tnt_ctx = sr;
    fh_init(&sr->d->fhw, &sr->pop.world, &SR_DET(sr));
    sr->d->fhw.role = DET_SERVER;
    an_init(&sr->d->anw, &sr->pop.world, &SR_DET(sr));
    sr->d->anw.process_dead = 1;
    sr->d->anw.update_ridden = 1;
    sr->d->anw.iew.role = DET_SERVER;
    /* WorldServer.tick's tail: the villages, the siege, and the shuffle
     * Random the villagers' offer rebuilds draw (det.nbt's "shuf" records its
     * 48-bit state at the snapshot) */
    vc_init(&sr->vc, &sr->d->anw);
    sr->d->anw.village_collection = &sr->vc;
    vs_init(&sr->vs, &sr->d->anw);
    sr->d->anw.village_siege = &sr->vs;
    /* WorldServer.villageCollectionObj as the snapshot saw it (villages.nbt,
     * VillageCollection.readFromNBT's keys plus its runtime state; a
     * snapshot without one had no village), and the siege's village */
    {
        char vpath[1200];
        char *vtext = NULL;
        snprintf(vpath, sizeof vpath, "%s/villages.nbt", dir);
        if (read_file(vpath, &vtext))
        {
            nbt *vroot = nbt_parse(vtext);
            free(vtext);
            if (vroot == NULL || !vc_load(&sr->vc, vroot))
            {
                set_err(sr, "villages.nbt is malformed");
                nbt_free(vroot);
                return 0;
            }
            int sv = (int)scalar_long(vroot, "siegeV");
            sr->vs.the_village = sv >= 0 && sv < sr->vc.num_villages ? sr->vc.list[sv] + 1 : 0;
            sr->vs.field_75532_g = (int)scalar_long(vroot, "siegeG");
            sr->vs.field_75538_h = (int)scalar_long(vroot, "siegeH");
            sr->vs.field_75539_i = (int)scalar_long(vroot, "siegeI");
            nbt_free(vroot);
        }
    }
    if (nbt_get(snap->det, "shuf") != NULL)
        SR_SHUF(sr).seed = (uint64_t)sr_det_long(nbt_get(snap->det, "shuf")) & JR_MASK;   /* the state, not a seed */
    else
        sr_shuf_from_reset(sr, snap, dir);
    /* the block singletons' own Randoms (spill.h), the breakBlock spill's:
     * det.nbt's furnace, chest, dispenser, hopper and brewing groups (a group
     * an older snapshot lacks keeps its born state) */
    block_rand_reset();
    for (int gi = 0; gi < 5; ++gi)
    {
        const struct block_rand_group *grp = &block_rand_groups[gi];
        const nbt *list = nbt_get(snap->det, grp->key);
        for (int i = 0; list != NULL && i < grp->n && i < nbt_list_size(list); ++i)
        {
            const nbt *fe = nbt_list_get(list, i);
            det_rng *r = &block_rand[grp->first + i];
            r->r.seed = (uint64_t)sr_det_long(nbt_get(fe, "state"));
            r->have_next_next_gaussian = 0;
            char *g = nbt_get(fe, "gauss") ? nbt_render(nbt_get(fe, "gauss")) : NULL;
            if (g != NULL)
            {
                const char *h = g + (*g == '"');
                uint64_t bits = strtoull(h + 2, NULL, 16);
                memcpy(&r->next_next_gaussian, &bits, sizeof bits);
                r->have_next_next_gaussian = 1;
                free(g);
            }
        }
    }
    sr->d->anw.shuf_rand = &SR_SHUF(sr);
    const nbt *rules = nbt_get(snap->worldinfo, "GameRules");
    const char *fire_rule = nbt_string_value(nbt_get(rules, "doFireTick"));
    for (int i = 0; i < sr->nworlds; ++i)
        sr->w[i].st.fire_rule_off = fire_rule != NULL && strcmp(fire_rule, "false") == 0;
    const char *spawn_rule = nbt_string_value(nbt_get(rules, "doMobSpawning"));
    sr->natural_spawning = spawn_rule && strcmp(spawn_rule, "true") == 0;
    for (int i = 0; i < sr->nworlds; ++i) sr->w[i].st.mob_spawning = sr->natural_spawning;
    const char *keep_rule = nbt_string_value(nbt_get(rules, "keepInventory"));
    surv.keep_inventory = keep_rule != NULL && strcmp(keep_rule, "true") == 0;
    const char *daylight_rule = nbt_string_value(nbt_get(rules, "doDaylightCycle"));
    for (int i = 0; i < sr->nworlds; ++i)
        sr->w[i].st.daylight_cycle = daylight_rule == NULL || strcmp(daylight_rule, "true") == 0;
    /* Saved animals can load after a player respawns even when the gamerule
     * blocks new natural spawns. Keep their entity simulation available. */
    sr->mobs_enabled = 1;
    if (sr->mobs_enabled)
    {
        int64_t month = 0, day = 0;
        int has_date = json_int(json_get(srv, "calMonth"), &month) &&
                       json_int(json_get(srv, "calDay"), &day) &&
                       month >= 1 && month <= 12 && day >= 1 && day <= 31;
        if (sr->natural_spawning && !has_date)
        {
            set_err(sr, "the mob snapshot has no valid calendar date");
            return 0;
        }
        if (!has_date) { month = 1; day = 1; }
        spawner_init(&sr->d->spawner, &sr->pop.world, &SR_DET(sr), &ST_RAND(&sr->w[0].st));
        if (snap->version >= 2)
        {
            sr->d->spawner.ensure_chunk = sr_spawner_ensure_chunk;
            sr->d->spawner.ensure_ctx = sr;
        }
        sr->d->spawner.possible_creatures = sr_possible_creatures;
        sr->d->spawner.possible_ctx = sr;
        sr->d->spawner.blocked = sr_spawn_blocked;
        sr->d->spawner.blocked_ctx = sr;
        spawner_set_scene(&sr->d->spawner, snap->seed, sr->w[0].st.difficulty, (int)month, (int)day,
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnX")),
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnY")),
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnZ")));
        /* the server start's func_147139_a: no monsters at PEACEFUL */
        spawner_set_spawn_types(&sr->d->spawner, sr->w[0].st.difficulty != 0, 1);
    }
    sr->d->nents = 0;

    /* the provider and player manager: with MobFree's
     * loadChunkOnProvideRequest off, loads never touch the world, but the
     * unload queue still empties. The spawn coords gate
     * unloadChunksIfNotNearSpawn. */
    sr->d->cl = cl_init(&sr->pop.world, (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnX")),
                     (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnZ")));
    /* every dimension's PlayerInstance.removePlayer S21s reach the client */
    cl_on_client_unload(sr->d->cl, sr_client_unload, sr);
    cl_set_chunk_io(sr->d->cl, sr_chunk_load_cb, sr_chunk_unload_cb, sr);
    cl_set_no_load(sr->d->cl, 1);
    cl_on_event(sr->d->cl, sr_cl_event, sr);
    /* the inhabitedTime clock reads the overworld's total time (the
     * freeze-inhabited negative check stops it) */
    cl_set_clock(sr->d->cl, (nw_env->cfg.sr_negative & 16) ? NULL : &sr->w[0].st.total_time);

    /* the join, which the snapshot is taken after: PlayerManager.addPlayer at
     * the parked player's position (radius 10), then the integrated server's
     * first-tail view distance change to 8 at tick 1. The outer ring the
     * smaller radius drops dies with unloadChunksIfNotNearSpawn's 128-block
     * spawn rule, which mark() carries, so nothing is queued near spawn.
     * The player logs into its own dimension's manager: the manifest's
     * playerX/Z is that world's view, and it is zero when the player is away
     * (then the position comes from the player NBT's Pos). The away
     * dimension's login happens where that state is built, below. */
    {
        const struct jval *p = json_get(snap->manifest_json, "server");
        const struct jval *jx = p ? json_get(p, "playerX") : NULL;
        const struct jval *jz = p ? json_get(p, "playerZ") : NULL;
        uint64_t bx = 0, bz = 0;
        double x = 0.0, z = 0.0;

        if (jx && jz && json_double(jx, &bx) && json_double(jz, &bz))
        {
            memcpy(&x, &bx, 8);
            memcpy(&z, &bz, 8);
        }

        /* PlayerManager's square follows managedPosX/Z, which moves only when
         * the player has gone 8 blocks from it: a setup's short teleport leaves
         * it at the join position. Snapshots without the record assume the
         * player stands on it. */
        const struct jval *mx = p ? json_get(p, "managedX") : NULL;
        const struct jval *mz = p ? json_get(p, "managedZ") : NULL;

        if (mx && mz && json_double(mx, &bx) && json_double(mz, &bz))
        {
            memcpy(&x, &bx, 8);
            memcpy(&z, &bz, 8);
        }

        /* The player logs into its own dimension's manager: the manifest's
         * position is the overworld manager's view, and it is zero when the
         * player is away (then the position comes from the player NBT's Pos;
         * the away dimension's login happens where that state is built,
         * below) */
        const nbt *ps = nbt_get(snap->player_server, "fields");
        int join_dim = (int)nbt_int_value(nbt_get(ps, "dimension"));
        const nbt *pn = nbt_get(snap->player_server, "nbt");
        const nbt *pos = nbt_get(pn, "Pos");
        uint64_t xbits = nbt_double_bits(nbt_list_get(pos, 0));
        uint64_t zbits = nbt_double_bits(nbt_list_get(pos, 2));
        double pnx, pnz;
        memcpy(&pnx, &xbits, sizeof pnx);
        memcpy(&pnz, &zbits, sizeof pnz);
        double px = join_dim == 0 && (x != 0.0 || z != 0.0) ? x : pnx;
        double pz = join_dim == 0 && (x != 0.0 || z != 0.0) ? z : pnz;
        /* EntityPlayerMP.managedPosX/Z itself when the snapshot records the
         * player's runtime: the square of the manager the player is in,
         * whichever dimension that is */
        {
            const nbt *rf = nbt_get(nbt_get(snap->player_server, "rt"), "f");
            const nbt *mx2 = rf ? nbt_get(rf, "managedPosX") : NULL;
            const nbt *mz2 = rf ? nbt_get(rf, "managedPosZ") : NULL;
            if (mx2 && mz2)
            {
                uint64_t b1 = nbt_double_bits(mx2), b2 = nbt_double_bits(mz2);
                memcpy(&px, &b1, sizeof px);
                memcpy(&pz, &b2, sizeof pz);
            }
        }
        sr->join_x = px;
        sr->join_z = pz;

        if (join_dim == 0)
        {
            /* A v2 snapshot is taken at the start of tick 2 of a fresh join with
             * an empty client world: the login's queue (radius 10, spiral order)
             * sent its first bulk in tick 1's entity pass, over the chunk flags
             * tick 1's block pass left, which the snapshot recorded; the view
             * distance change at tick 1's tail then dropped the outer ring. That
             * bulk reaches the client at the first replayed tick. */
            sr->stream_chunks = snap->version >= 2;
            cl_on_client_unload(sr->d->cl, sr_client_unload, sr);
            cl_login(sr->d->cl, 0, px, pz);
            int resume = nbt_get(nbt_get(snap->player_server, "fields"), "sendQueue") != NULL;
            if (sr->stream_chunks && !resume) sr_send_chunks(sr, 1);
            cl_view_radius(sr->d->cl, 1, sr->view);
            if (resume) sr_resume_stream(sr, snap);

            /* MobFree emptied the provider's unload queue before the snapshot:
             * the marks the radius drop just made are gone, and the instances
             * are dead, so nothing is re-queued until the t % 900 save marks. */
            if (snap->version < 2) cl_clear_unload(sr->d->cl);
            else cl_restore_unload(sr->d->cl, unload_coords, nunload, unload_bins);
            cl_set_no_load(sr->d->cl, 0);

            /* each instance's previousWorldTime as the snapshot recorded it;
             * without the record the join's marks stand (the snapshot tick) */
            for (int k = 0; k + 2 < npm_inst; k += 3) cl_set_mark(sr->d->cl, pm_inst[k], pm_inst[k + 1], pm_inst[k + 2]);
        }
        else
        {
            /* the player is away: the overworld manager has no player, and
             * the away dimension's manager holds the login (its cl is built
             * in the away state below). Its provider still loads on
             * request (a neighbour notify reaching an unloaded chunk loads
             * the saved copy with its ticks), and its queue is the one the
             * snapshot recorded. */
            sr->join_dim = join_dim;
            /* tick 1's view distance change reached this manager too
             * (func_152622_a on every world): a later login here watches it */
            cl_set_radius(sr->d->cl, sr->view);
            if (snap->version >= 2)
            {
                cl_restore_unload(sr->d->cl, unload_coords, nunload, unload_bins);
                cl_set_no_load(sr->d->cl, 0);
            }
        }
    }
    free(pm_inst);

    /* the landing drop a falling block makes is a new EntityItem: adopt it
     * into the item pool with the draws the drop engine already spent */
    nw_env->fallhang.env.drop = sr_fh_drop;
    nw_env->fallhang.env.hurt = sr_fh_hurt;
    nw_env->fallhang.env.aux_sfx = NULL;
    nw_env->fallhang.env.ctx = sr;

    /* the Nether's own state, made in the slots it will be used from */
    sr->dims[1].live = 1;
    sr_use_dim(sr, &sr->dims[1]);
    {
        struct world *hw = &sr->hell.world;
        struct sr_world *hws = sr_dim_ws(sr, -1);

        ie_init(&sr->d->iew, hw, &SR_DET(sr));
        sr->d->iew.role = DET_SERVER;
        sr->d->iew.collide_entities = 1;
        sr->d->iew.user_data = sr;
        sr->d->iew.on_egg_chicken = sr_egg_chicken;
        /* EntityTNTPrimed.explode in the Nether: the blast over the Nether's
         * world and its World.rand (tnt_replay_explode) */
        sr->d->iew.on_tnt_explode = tnt_replay_explode;
        sr->d->iew.tnt_ctx = sr;
        fh_init(&sr->d->fhw, hw, &SR_DET(sr));
        sr->d->fhw.role = DET_SERVER;
        an_init(&sr->d->anw, hw, &SR_DET(sr));
        sr->d->anw.iew.on_egg_chicken = sr_anw_egg_chicken;
        sr->d->anw.iew.on_arrow_tnt = sr_arrow_tnt;
        sr->d->anw.iew.on_arrow_button = sr_arrow_button;
        /* Collections.shuffle's shared Random (a villager's first offers) */
        sr->d->anw.shuf_rand = &SR_SHUF(sr);
        /* the fireball lane's impact handler: the Nether fireballs carry the
         * ghast's 1000.0F branch and the explosion extras (living.c's own
         * callback covers only the small fireball) */
        sr->d->anw.iew.on_fireball_impact = gh_fireball_impact;
        sr->d->anw.process_dead = 1;
        sr->d->anw.update_ridden = 1;
        sr->d->anw.iew.role = DET_SERVER;
        sr->d->anw.dimension = -1;
        /* the Nether's own VillageCollection (villages_nether), which a
         * villager that came through a portal feeds */
        vc_init(&sr->d->vc, &sr->d->anw);
        sr->d->vc.tick_counter = sr->vc.tick_counter;
        sr->d->anw.village_collection = &sr->d->vc;
        /* Collections.shuffle's Random is one for the JVM: a villager's
         * offers in the Nether shuffle with the overworld's */
        sr->d->anw.shuf_rand = &SR_SHUF(sr);
        /* EntityGhast reads playerEntities through the ghast lane's
         * gh_world, which mirrors the server player each pass */
        struct gh_world *gw = calloc(1, sizeof *gw);
        gw->an = &sr->d->anw;
        sr->ghw = gw;
        sr->d->anw.user_data = gw;
        /* EntityPlayerMP's constructor draws, through the ghast lane's own
         * det slots (test_ghasts.c seeds the probe player the same way):
         * the rand role, the uuid role, then the three Math.random floats */
        {
            det_state fake;
            det_init(&fake);
            fake.world_seed = SR_DET(sr).world_seed;
            struct gh_player *gp = &gw->player;
            gp->entity_id = sr->player_entity_id;
            gp->rand = det_new_random_role(&fake, DET_OTHER);
            det_uuid_role(&fake, DET_OTHER, &gp->uuid_msb, &gp->uuid_lsb);
            (void)det_math_random_role(&fake, DET_OTHER);
            (void)det_math_random_role(&fake, DET_OTHER);
            (void)det_math_random_role(&fake, DET_OTHER);
            det_free(&fake);
        }
        spawner_init(&sr->d->spawner, hw, &SR_DET(sr), &ST_RAND(&hws->st));
        sr->d->spawner.dim = -1;
        sr->d->spawner.possible_creatures = sr_hell_possible_creatures;
        sr->d->spawner.possible_ctx = sr;
        sr->d->spawner.blocked = sr_spawn_blocked;
        sr->d->spawner.blocked_ctx = sr;
        sr->d->spawner.ensure_chunk = sr_hell_ensure_chunk;
        sr->d->spawner.ensure_ctx = sr;
        spawner_set_scene(&sr->d->spawner, snap->seed, hws->st.difficulty, sr->cal_month, sr->cal_day,
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnX")),
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnY")),
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnZ")));
        spawner_set_spawn_types(&sr->d->spawner, hws->st.difficulty != 0, 1);
        /* the Nether's player manager: the server's view distance, no
         * spawn box (canRespawnHere is false) */
        sr->d->cl = cl_init(hw, 0, 0);
        /* every dimension's PlayerInstance.removePlayer S21s reach the client */
        cl_on_client_unload(sr->d->cl, sr_client_unload, sr);
        cl_set_respawn(sr->d->cl, 0);
        cl_set_clock(sr->d->cl, (nw_env->cfg.sr_negative & 16) ? NULL : &hws->st.total_time);
        cl_set_radius(sr->d->cl, hws->view > 0 ? hws->view : sr->view);
        cl_set_chunk_io(sr->d->cl, NULL, sr_chunk_unload_cb, sr);
        cl_on_event(sr->d->cl, sr_cl_event, sr);
        /* the region store, before anything can load from it (a login's
         * watch_make loads through sr_hell_provide) */
        world_init_slab(&sr->d->saved, snap->seed);
        sr->d->saved.dim = -1;
        sr->d->saved_ready = 1;
        sr->d->saved_init = 1;
        sr_adopt_dim_snapshot(sr, snap, -1);
    }
    sr_use_dim(sr, &sr->dims[0]);

    /* The End: its provider generates from the seed on the first transfer
     * when the snapshot leaves generation on (MobFree's generateEnd), unless
     * the snapshot carries the End's loaded chunks. Its own state, made in
     * its slots. */
    populate_init(&sr->sky, snap->seed);
    if (snap->end_loaded)
    {
        sr_adopt_world(&sr->sky.world, &snap->end_world);
        sr->end_live = 1;
    }
    sr->sky.world.dim = 1;
    sr->sky.world.on_chunk = sr_sky_on_chunk;
    sr->sky.world.on_chunk_ctx = sr;
    sr->sky.world.provide = sr_sky_provide;
    sr->sky.world.provide_ctx = sr;
    {
        int64_t end_no_gen = 1;
        if (no_gen_worlds && json_len(no_gen_worlds) > 2)
            json_int(json_at(no_gen_worlds, 2), &end_no_gen);
        sr->sky.world.no_generate = end_no_gen != 0;
    }
    for (int i = 0; i < sr->nworlds; ++i)
        if (sr->w[i].dim == 1) sr->w[i].st.w = &sr->sky.world;
    teleporter_init(&sr->tp_end, &sr->sky.world);
    jr_seed(&TP_RAND(&sr->tp_end), snap->seed);
    sr_teleporter_restore(sr, &sr->tp_end, 1);
    sr->end = calloc(1, sizeof *sr->end);
    sr->dims[2].live = 1;
    sr_use_dim(sr, &sr->dims[2]);
    {
        struct world *ew = &sr->sky.world;
        struct sr_world *ews = sr_dim_ws(sr, 1);

        ie_init(&sr->d->iew, ew, &SR_DET(sr));
        sr->d->iew.role = DET_SERVER;
        sr->d->iew.collide_entities = 1;
        sr->d->iew.user_data = sr;
        sr->d->iew.on_egg_chicken = sr_egg_chicken;
        sr->d->iew.on_tnt_explode = tnt_replay_explode;
        sr->d->iew.tnt_ctx = sr;
        fh_init(&sr->d->fhw, ew, &SR_DET(sr));
        sr->d->fhw.role = DET_SERVER;
        an_init(&sr->d->anw, ew, &SR_DET(sr));
        sr->d->anw.iew.on_egg_chicken = sr_anw_egg_chicken;
        sr->d->anw.iew.on_arrow_tnt = sr_arrow_tnt;
        sr->d->anw.iew.on_arrow_button = sr_arrow_button;
        sr->d->anw.shuf_rand = &SR_SHUF(sr);
        sr->d->anw.process_dead = 1;
        sr->d->anw.update_ridden = 1;
        sr->d->anw.iew.role = DET_SERVER;
        sr->d->anw.dimension = 1;
        /* the End's own VillageCollection (villages_end) */
        vc_init(&sr->d->vc, &sr->d->anw);
        sr->d->vc.tick_counter = sr->vc.tick_counter;
        sr->d->anw.village_collection = &sr->d->vc;
        sr->d->anw.shuf_rand = &SR_SHUF(sr);   /* the JVM's one shuffle Random, as in the Nether */
        sr->d->anw.iew.query_other = sr_end_query_other;
        sr->d->anw.iew.on_other_hit = sr_end_other_hit;
        sr->d->anw.collide_extra = sr_end_mob_collide;
        sr->d->anw.collide_extra_before = sr_end_mob_collide_before;
        sr->d->anw.collide_extra_ctx = sr;
        sr_end_ctx = sr;
        /* SpawnerAnimals over BiomeGenEnd's list (the enderman), read from
         * the chunks' biome arrays */
        spawner_init(&sr->d->spawner, ew, &SR_DET(sr), &ST_RAND(&ews->st));
        sr->d->spawner.dim = 1;
        sr->d->spawner.ensure_chunk = sr_sky_ensure_chunk;
        sr->d->spawner.ensure_ctx = sr;
        sr->d->spawner.blocked = sr_spawn_blocked;
        sr->d->spawner.blocked_ctx = sr;
        spawner_set_scene(&sr->d->spawner, snap->seed, ews->st.difficulty, sr->cal_month, sr->cal_day,
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnX")),
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnY")),
                          (int)nbt_int_value(nbt_get(snap->worldinfo, "SpawnZ")));
        spawner_set_spawn_types(&sr->d->spawner, ews->st.difficulty != 0, 1);
        /* WorldProviderEnd.canRespawnHere is false too */
        sr->d->cl = cl_init(ew, 0, 0);
        /* every dimension's PlayerInstance.removePlayer S21s reach the client */
        cl_on_client_unload(sr->d->cl, sr_client_unload, sr);
        cl_set_respawn(sr->d->cl, 0);
        cl_set_clock(sr->d->cl, (nw_env->cfg.sr_negative & 16) ? NULL : &ews->st.total_time);
        cl_set_radius(sr->d->cl, ews->view > 0 ? ews->view : sr->view);
        cl_set_chunk_io(sr->d->cl, NULL, sr_chunk_unload_cb, sr);
        cl_on_event(sr->d->cl, sr_cl_event, sr);
        world_init_slab(&sr->d->saved, snap->seed);
        sr->d->saved.dim = 1;
        sr->d->saved_ready = 1;
        sr->d->saved_init = 1;
        sr_adopt_dim_snapshot(sr, snap, 1);
    }
    sr_use_dim(sr, &sr->dims[0]);

    if (snap->version >= 2 && !sr_load_snapshot_entities(sr, snap)) return 0;
    if (snap->version >= 2)
    {
        sr->d->initial_ent_count = sr->d->nents;
        sr->order_new_by_id = 1;
        sr->pop.world.provide = sr_provide;
        sr->pop.world.provide_ctx = sr;
    }

    /* World.loadedTileEntityList as the snapshot's world had it: each
     * chunk load appends its chunkTileEntityMap values in HashMap order, and
     * the tick walks them in that order (tables sharing the enchanting
     * table's static Random: pfc-teorder-g18) */
    for (int i = 0; i < sr->nworlds && i < 3; ++i)
    {
        struct world *tw = sr->w[i].st.w;
        if (tw != NULL && sr->te_order[i] != NULL) sr_te_reorder(tw, sr->te_order[i], sr->te_order_n[i]);
        free(sr->te_order[i]);
        sr->te_order[i] = NULL;
    }

    /* The pending ticks: each world's own TreeSet, loaded with that world
     * entered (its ticks_set; nextTickEntryID is one static counter, the
     * environment's). */
    for (int i = 0; i < sr->nworlds; ++i)
    {
        serverreplay_enter(sr, sr->w[i].dim);
        ticks_reset(sr->w[i].st.next_tick_entry);
        int n = 0;
        for (int k = 0; k < snap->nticks; ++k)
        {
            if (snap->ticks[k].dim != sr->w[i].dim) continue;
            struct tick_entry e;
            e.x = snap->ticks[k].x;
            e.y = snap->ticks[k].y;
            e.z = snap->ticks[k].z;
            e.block = snap->ticks[k].id;
            e.time = snap->ticks[k].scheduled;
            e.priority = snap->ticks[k].priority;
            e.entry = snap->ticks[k].entry;
            ticks_load_entry(&e);
            ++n;
        }
        if (n != sr->w[i].pending)
        {
            set_err(sr, "world %d: %d pending ticks in ticks.jsonl.gz, worlds.nbt says %d", i, n, sr->w[i].pending);
            serverreplay_enter(sr, 0);
            return 0;
        }
    }
    serverreplay_enter(sr, 0);
    /* the snapshot's list is in the worlds' sets now and nothing reads it
     * again (a village start's backlog: 7 MB of every env's image) */
    free(snap->ticks);
    snap->ticks = NULL;
    snap->nticks = 0;

    /* A mid-run snapshot (it records the send queue): the region store's
     * context is the one the join's first unload set long before (World.rand
     * as the populate Random, the replay's Det). The store itself still
     * builds at the first load, and that load can come inside an entity's
     * tick, where the pass works on its own World.rand copy which the
     * build must leave in place. */
    if (nbt_get(nbt_get(snap->player_server, "fields"), "sendQueue") != NULL && !sr->d->saved_ctx) sr_saved_context(sr);

    return 1;
}

/* ChunkProviderHell.getPossibleCreatures: the fortress list inside a
 * fortress, the hell biome's otherwise. The block below is read up front;
 * the walk's location check has already loaded its chunk. */
static const struct sp_list *sr_hell_possible_creatures(void *ctx, int type, int x, int y, int z)
{
    struct serverreplay *sr = ctx;
    int below = world_get_block(&sr->hell.world, x, y - 1, z) & 4095;

    return spawning_hell_list(&sr->hell, type, x, y, z, below, NULL);
}

/* ChunkProviderGenerate.getPossibleCreatures: for the monster type, the
 * scattered feature's witch list (MapGenScatteredFeature.spawnList) when
 * func_143030_a(x, y, z) holds, the biome's own list otherwise. func_143030_a
 * is MapGenStructure.func_143028_c over the temple generator's map: a
 * sizeable start whose bounding box meets the (x, z) column, holding a
 * component whose box contains (x, y, z), and the start's first component is
 * the SwampHut. The replay keeps only the temple map's starts (the others do
 * not carry a spawn list), so the walk covers them all. */
static int sr_swamp_hut_at(struct serverreplay *sr, int x, int y, int z)
{
    const struct pop_map *m = &sr->pop.maps[3];

    for (int i = 0; i < m->n; ++i)
    {
        const struct pop_start *ps = &m->starts[i];
        if (!ps->sizeable) continue;
        const struct start *s = &ps->start;
        if (x < s->bb.minX || x > s->bb.maxX || z < s->bb.minZ || z > s->bb.maxZ) continue;
        for (int j = 0; j < s->n; ++j)
        {
            const struct piece *p = s->pieces[j];
            if (x < p->bb.minX || x > p->bb.maxX || y < p->bb.minY || y > p->bb.maxY ||
                z < p->bb.minZ || z > p->bb.maxZ) continue;
            /* Java: any component containing the point answers the start, then
             * func_143030_a tests components.getFirst() (MapGenScatteredFeature
             * 108-116); scattered-feature starts hold one component, but keep
             * the first-component test anyway */
            const struct piece *first = s->pieces[0];
            return first->kind == PIECE_TEMPLE && first->u.temple.kind == TEMPLE_SWAMP_HUT;
        }
    }

    return 0;
}

static const struct sp_list *sr_possible_creatures(void *ctx, int type, int x, int y, int z)
{
    struct serverreplay *sr = ctx;

    if (type == CT_MONSTER && sr_swamp_hut_at(sr, x, y, z)) return spawning_scattered_witch();

    return NULL;
}

/* The Nether spawner's getChunkFromChunkCoords: its provider loads (and
 * populates) on request. */
static void sr_hell_ensure_chunk(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;

    if (!sr->hell.world.no_generate) world_load_chunk(&sr->hell.world, cx, cz);
}

static int sr_mob_kind(int kind)
{
    static const int kinds[SP_KINDS] = {
        HK_ZOMBIE, HK_SKELETON, HK_SPIDER, HK_CREEPER, SK_SLIME, HK_ENDERMAN, HK_WITCH,
        AK_SHEEP, AK_PIG, AK_CHICKEN, AK_COW, AK_MOOSHROOM, AK_BAT, AK_SQUID,
        GK_GHAST, HK_PIGMAN, SK_MAGMA_CUBE, HK_BLAZE
    };
    return kind >= 0 && kind < SP_KINDS ? kinds[kind] : -1;
}

/* The spawner has already made the vanilla constructor and egg-path draws.
 * Rebuild the mob through the ported constructor using its own pre-constructor
 * streams, leaving the replay's shared Det streams at the spawner's result. */
static void sr_adopt_mob(struct serverreplay *sr, const struct sp_rec *r);

/* sr_adopt_mob, then spawnEntityInWorld's EntityTracker.addEntityToTracker
 * for the livings it added in the player's world: the entry and its first
 * watch come at the spawn, so a mob the same tick's pass removes (a
 * despawn) still reached the client */
static void sr_adopt_mob_tracked(struct serverreplay *sr, const struct sp_rec *r)
{
    int before = sr->d->anw.n;
    sr_adopt_mob(sr, r);
    if (sr->here != serverreplay_player_dim(sr)) return;
    for (int i = before; i < sr->d->anw.n; ++i)
        if (an_ent_at(sr->d->anw.slot[i])->is_living) combat_track_spawn(sr, lv_get(an_ent_at(sr->d->anw.slot[i])->livh));
}

static void sr_adopt_mob(struct serverreplay *sr, const struct sp_rec *r)
{
    int kind = sr_mob_kind(r->kind);
    if (kind < 0) return;
    /* A spider's egg path can spawn its skeleton rider before the spawner
     * reports that rider as its own record. hostile_spawn has already added
     * it to the live list in Java's order. */
    for (int i = 0; i < sr->d->anw.n; ++i)
        if (an_ent_at(sr->d->anw.slot[i])->is_living && lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->entity_id == r->id)
        {
            an_chunk_restamp(&sr->d->anw, an_ent_at(sr->d->anw.slot[i]), r->chunk_stamp);
            return;
        }

    det_state fake;
    det_init(&fake);
    uint64_t seeder[DET_ROLES] = {0}, math[DET_ROLES] = {0};
    int32_t ids[DET_ROLES] = {0};
    seeder[DET_OTHER] = r->seeder_before;
    math[DET_OTHER] = r->math_before;
    ids[DET_OTHER] = r->next_id_before;
    det_load(&fake, sr->pop.world.seed, seeder, math, ids);

    det_state *real = sr->d->anw.det;
    sr->d->anw.det = &fake;
    sr->d->anw.iew.det = &fake;
    int real_role = sr->d->anw.iew.role;
    sr->d->anw.iew.role = DET_OTHER;
    sr->d->anw.iew.world_rand.r.seed = r->world_before_egg;
    sr->d->anw.egg_adopt = 1;
    sr->d->anw.egg_spider_potion = r->spider_potion;
    int before = sr->d->anw.n;
    struct living *l;

    if (kind == HK_ZOMBIE || kind == HK_SKELETON || kind == HK_SPIDER || kind == HK_CREEPER ||
        kind == HK_ENDERMAN || kind == HK_WITCH || kind == HK_PIGMAN || kind == HK_BLAZE)
        /* the fields the egg path sets are overwritten from the record below,
         * except a spider jockey's rider, which the spider's path builds
         * whole: its armour and enchantment rolls read func_147462_b */
        l = hostile_spawn(&sr->d->anw, kind, before, r->x, r->y, r->z, r->yaw, 0.0F,
                          0.0, 0.0, 0.0, r->child, r->villager, r->skel_type,
                          0, kind == HK_SPIDER ? spawner_local_difficulty(&sr->d->spawner, r->x, r->y, r->z) : 0.0F);
    else if (kind == GK_GHAST)
        l = gh_spawn_ghast(&sr->d->anw, before, r->x, r->y, r->z, r->yaw, 0.0F);
    else if (kind == SK_MAGMA_CUBE || kind == SK_SLIME)
        l = an_spawn_slime(&sr->d->anw, kind, before, r->x, r->y, r->z, r->yaw, 0.0F, r->slime_size, 1);
    else
        l = an_spawn_living(&sr->d->anw, kind, before, r->x, r->y, r->z, r->yaw, 0.0F,
                            r->child ? -24000 : 0, 0, 0, 0, 0, 1);
    sr->d->anw.egg_adopt = 0;
    sr->d->anw.egg_spider_potion = 0;

    if (l != NULL)
    {
        l->rand = r->e;
        l->entity_id = r->id;
        for (int i = before; i < sr->d->anw.n; ++i)
            if (an_ent_at(sr->d->anw.slot[i])->is_living && lv_get(an_ent_at(sr->d->anw.slot[i])->livh) == l)
                an_chunk_restamp(&sr->d->anw, an_ent_at(sr->d->anw.slot[i]), r->chunk_stamp);
        l->uuid_msb = r->uuid_msb;
        l->uuid_lsb = r->uuid_lsb;
        l->persistence_required = 0;
        l->can_pick_up_loot = (uint8_t)r->pickup;
        l->zombie_can_break_doors = r->break_doors || r->leader;
        if (kind == HK_ZOMBIE || kind == HK_PIGMAN)
        {
            zombie_set_child(l, r->child);
            l->zombie_is_villager = r->villager;
            if (l->zombie_can_break_doors) ai_add_break_door(l);
            if (r->leader)
            {
                struct attr_mod reinforcement = {0};
                reinforcement.name = MODN_LEADER_ZOMBIE_BONUS;
                reinforcement.amount = r->leader_reinforcements;
                reinforcement.operation = 0;
                reinforcement.uuid_msb = r->leader_reinforcements_msb;
                reinforcement.uuid_lsb = r->leader_reinforcements_lsb;
                reinforcement.saved = 1;
                attrs_apply(&l->attrs.a[ATTR_SPAWN_REINFORCEMENTS], &reinforcement);

                struct attr_mod health = reinforcement;
                health.amount = r->leader_health;
                health.operation = 2;
                health.uuid_msb = r->leader_health_msb;
                health.uuid_lsb = r->leader_health_lsb;
                attrs_apply(&l->attrs.a[ATTR_MAX_HEALTH], &health);
            }
        }
        memset(l->equip, 0, sizeof l->equip);
        for (int slot = 0; slot < 5; ++slot)
        {
            if (r->eq_item[slot] < 0) continue;
            l->equip[slot].id = r->eq_item[slot];
            l->equip[slot].damage = r->eq_dmg[slot];
            l->equip[slot].count = r->eq_cnt[slot];
            l->equip[slot].tag = r->eq_tag[slot];
        }
        /* the held item's attack modifier follows the recorded hand, not
         * the egg path's roll (a record with an empty hand kept the egg's
         * sword modifier: pfc-zombiehand-s21) */
        if (kind == HK_ZOMBIE) living_held_item_modifiers(l, l->equip[0].id);
        if (kind == HK_SKELETON)
        {
            /* EntitySkeleton.onSpawnWithEgg's Nether branch: the wither
             * skeleton's size, damage and sword; the task follows the
             * weapon either way */
            if (r->skel_type == 1)
            {
                skeleton_set_type(l, 1);
                attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 4.0);
            }
            else
            {
                /* the overworld branch: whatever the stand-in egg rolled
                 * above, the plain skeleton's type, box and base damage */
                skeleton_set_type(l, 0);
                attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 2.0);
            }
            skeleton_apply_weapon(l);
            skeleton_set_combat_task(l);
        }
        /* the spawn bonus modifiers as the spawner drew them: an egg path
         * that constructs another entity midway (the chicken jockey) moves
         * the replayed UUID draws, the record does not */
        struct attr_instance *follow = &l->attrs.a[ATTR_FOLLOW_RANGE];
        for (int i = 0; i < follow->nmods; ++i)
            if (follow->mods[i].name == MODN_RANDOM_SPAWN_BONUS)
            {
                follow->mods[i].amount = r->follow_bonus;
                follow->mods[i].uuid_msb = r->follow_msb;
                follow->mods[i].uuid_lsb = r->follow_lsb;
            }
        struct attr_instance *knockback = &l->attrs.a[ATTR_KNOCKBACK_RESISTANCE];
        for (int i = 0; i < knockback->nmods; ++i)
            if (knockback->mods[i].name == MODN_RANDOM_SPAWN_BONUS)
            {
                knockback->mods[i].amount = r->knockback_bonus;
                knockback->mods[i].uuid_msb = r->knockback_msb;
                knockback->mods[i].uuid_lsb = r->knockback_lsb;
            }
        /* EntityZombie.onSpawnWithEgg's chicken jockey: the chicken is the
         * record after its rider, and mountEntity links the two */
        if (kind == AK_CHICKEN && r->chicken_jockey)
        {
            l->is_chicken_jockey = 1;
            for (int i = 0; i < sr->d->anw.n; ++i)
            {
                struct an_ent *o = an_ent_at(sr->d->anw.slot[i]);
                if (o->is_living && lv_get(o->livh)->entity_id == r->ridden_by)
                {
                    lv_get(o->livh)->riding_entity = lv_ref(l);
                    l->ridden_by_entity = lv_ref(lv_get(o->livh));
                }
            }
        }
        /* its first branch: a chicken already in the world (func_152117_i,
         * then mountEntity), the record the spawner found for the rider */
        if (r->riding >= 0 && kind != AK_CHICKEN)
            for (int i = 0; i < sr->d->anw.n; ++i)
            {
                struct an_ent *o = an_ent_at(sr->d->anw.slot[i]);
                if (!o->is_living || lv_get(o->livh) == l) continue;
                struct living *v = lv_get(o->livh);
                if (v->entity_id != r->riding || v->kind != AK_CHICKEN) continue;
                v->is_chicken_jockey = 1;
                l->riding_entity = lv_ref(v);
                v->ridden_by_entity = lv_ref(l);
            }
        for (int i = before; i < sr->d->anw.n; ++i)
            if (r->order_key > 0) an_ent_at(sr->d->anw.slot[i])->order_key = r->order_key;
        /* inside an entity's tick (a population its path's chunk cache
         * set off) sr_tick_an appends the new livings itself */
        if (!sr->in_entity_tick)
            for (int i = before; i < sr->d->anw.n; ++i) sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[i]));
        /* spawnEntityInWorld's EntityTracker.trackEntity: the entry and a
         * watching player's S0F take the spawn pose, before the mob's first
         * update moves it (the client keeps the S0F's position: a zombie
         * pushed 0.15 on its first tick stood 5/32 off on the client, and a
         * later hit missed it natively; seed-1 S12 row 10662) */
        for (int i = before; i < sr->d->anw.n; ++i)
            if (an_ent_at(sr->d->anw.slot[i])->is_living) combat_track_spawn(sr, lv_get(an_ent_at(sr->d->anw.slot[i])->livh));
    }

    sr->d->anw.det = real;
    sr->d->anw.iew.det = real;
    sr->d->anw.iew.role = real_role;
    det_free(&fake);
}

/* Dev's summon: the spawner makes the vanilla constructor and onSpawnWithEgg
 * draws at the current world time's moon phase, then the mob is rebuilt from
 * the record like a natural spawn. child and size are Dev.java's forced NBT
 * keys (IsBaby, Size), which the record path applies before the egg draws. */
int serverreplay_summon_hostile(struct serverreplay *sr, int kind, double x, double y, double z,
                                int child, int size)
{
    int spk = -1;
    for (int k = 0; k < SP_KINDS; ++k)
        if (sr_mob_kind(k) == kind) spk = k;
    if (spk != SP_ZOMBIE && spk != SP_SLIME && spk != SP_MAGMA_CUBE && spk != SP_PIG_ZOMBIE &&
        spk != SP_SKELETON)
        return 0;
    struct sp_rec r;
    sr->d->spawner.world_time = sr->w[0].st.world_time;
    sr->d->spawner.total_time = sr->w[0].st.total_time;
    sr->d->spawner.difficulty = sr->w[0].st.difficulty;
    if (!spawner_summon(&sr->d->spawner, spk, x, y, z, child, size, &r)) return 0;
    int before = sr->d->anw.n;
    sr_adopt_mob(sr, &r);
    if (sr->d->anw.n == before) return 0;
    for (int i = before; i < sr->d->anw.n; ++i)
        if (an_ent_at(sr->d->anw.slot[i])->is_living)
        {
            /* Dev.java's createEntityFromNBT: readFromNBT sets
             * Entity.dimension from the tag's absent Dimension key, 0 */
            lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->dimension = 0;
            /* and the absent Air short: the air supply starts at 0 */
            lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->air = 0;
            if ((kind == HK_ZOMBIE || kind == HK_PIGMAN) && child)
            {
                /* the summon's child flag is readEntityFromNBT's IsBaby, set
                 * before setLocationAndAngles: Java's placement re-centers the
                 * box the shrink moved, hostile_spawn's flag lands after it */
                zombie_set_child(lv_get(an_ent_at(sr->d->anw.slot[i])->livh), 1);
                living_set_location_and_angles(lv_get(an_ent_at(sr->d->anw.slot[i])->livh), x, y, z,
                                               lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->rotation_yaw,
                                               lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->rotation_pitch);
            }
            if ((kind == SK_SLIME || kind == SK_MAGMA_CUBE) && size > 0)
                slime_set_size(lv_get(an_ent_at(sr->d->anw.slot[i])->livh), size);
            /* in the world's loaded entity list: the natural spawner's
             * cap counts it and its box blocks a spawn */
            spawner_register_living(&sr->d->spawner, lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->entity_id == r.id ? spk : SP_KINDS,
                                    lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->entity_id, &lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->e);
            /* spawnEntityInWorld's EntityTracker.addEntityToTracker: the
             * entry and its S0F take the summoned pose, before the first
             * tick moves it (a wither skeleton's widened box re-centers on
             * that move, and the client keeps the S0F's position) */
            combat_track_spawn(sr, lv_get(an_ent_at(sr->d->anw.slot[i])->livh));
        }
    sr->native_entities = sr_count_entities(sr);
    return 1;
}

/* servertick's portal_spawn: BlockPortal.updateTick's
 * ItemMonsterPlacer.spawnCreature(world, 57, x, y, z) inside the random tick,
 * the pigman rebuilt from the spawner's record like a summon, then
 * var7.timeUntilPortal = getPortalCooldown() (300 for any non-player). */
static void sr_portal_spawn(void *ctx, double x, double y, double z)
{
    struct serverreplay *sr = ctx;
    struct sr_world *ws = sr_dim_ws(sr, sr->here);
    struct sp_rec r;

    sr->d->spawner.world_time = ws->st.world_time;
    sr->d->spawner.total_time = ws->st.total_time;
    if (!spawner_spawn_creature(&sr->d->spawner, SP_PIG_ZOMBIE, x, y, z, &r)) return;
    int before = sr->d->anw.n;
    sr_adopt_mob(sr, &r);
    for (int i = before; i < sr->d->anw.n; ++i)
        if (an_ent_at(sr->d->anw.slot[i])->is_living)
        {
            struct living *l = lv_get(an_ent_at(sr->d->anw.slot[i])->livh);
            spawner_register_living(&sr->d->spawner, l->entity_id == r.id ? SP_PIG_ZOMBIE : SP_KINDS,
                                    l->entity_id, &l->e);
            if (l->entity_id == r.id) l->time_until_portal = 300;
        }
    sr->native_entities = sr_count_entities(sr);
}

int serverreplay_summon_chicken_jockey(struct serverreplay *sr, double x, double y, double z, int attempts)
{
    struct sp_rec r;
    int nout = sr->d->spawner.nout, hit = 0;

    sr->d->spawner.world_time = sr->w[0].st.world_time;
    sr->d->spawner.total_time = sr->w[0].st.total_time;
    for (int a = 0; a < attempts && !hit; ++a)
    {
        if (!spawner_summon(&sr->d->spawner, SP_ZOMBIE, x, y, z, 0, 0, &r)) return 0;
        hit = r.riding >= 0;
    }
    if (!hit) return 0;

    /* the chicken the hit's egg path spawned (rec_spawn's out entry) */
    const struct sp_rec *ch = NULL;
    for (int i = nout; i < sr->d->spawner.nout; ++i)
        if (sr->d->spawner.out[i]->id == r.riding) ch = sr->d->spawner.out[i];
    sr->d->spawner.nout = nout;
    if (ch == NULL) return 0;

    /* loadedEntityList appends: the chicken's higher id comes first, so the
     * post-snapshot id order does not apply to this pair */
    int before = sr->d->anw.n, by_id = sr->order_new_by_id;
    sr->order_new_by_id = 0;
    sr_adopt_mob(sr, ch);
    sr_adopt_mob(sr, &r);
    sr->order_new_by_id = by_id;
    struct living *chicken = NULL, *zombie = NULL;
    for (int i = before; i < sr->d->anw.n; ++i)
    {
        if (!an_ent_at(sr->d->anw.slot[i])->is_living) continue;
        struct living *l = lv_get(an_ent_at(sr->d->anw.slot[i])->livh);
        if (l->entity_id == ch->id) chicken = l;
        if (l->entity_id == r.id) zombie = l;
        spawner_track(&sr->d->spawner, l->entity_id, &l->e);
    }
    if (chicken == NULL || zombie == NULL) return 0;
    living_mount(zombie, chicken);
    /* EntityTracker.addEntityToTracker in each spawnEntityInWorld: the
     * chicken's spawn, then the mounted zombie's at its own pose */
    combat_track_spawn(sr, chicken);
    combat_track_spawn(sr, zombie);
    sr->native_entities = sr_count_entities(sr);
    return 1;
}

static void sr_spawner_tick(void *ctx, struct servertick *st)
{
    struct serverreplay *sr = ctx;
    /* countEntities reads each living's persistenceRequired as it stands
     * (a name tag used in the last network tick, a picked-up item) */
    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        const struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (en->is_living && en->livh != 0)
            spawner_set_persistent(&sr->d->spawner, lv_get(en->livh)->entity_id, lv_get(en->livh)->persistence_required);
    }
    sr->in_spawner_tick = 1;
    spawner_tick(&sr->d->spawner, st);
    sr->in_spawner_tick = 0;
    for (int i = 0; i < sr->d->spawner.nout; ++i) sr_adopt_mob_tracked(sr, sr->d->spawner.out[i]);
}

int serverreplay_move_mob(struct serverreplay *sr, int id, double x, double y, double z)
{
    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (!en->is_living || lv_get(en->livh)->entity_id != id) continue;
        entity_set_position(&lv_get(en->livh)->e, x, y, z);
        spawner_track(&sr->d->spawner, id, &lv_get(en->livh)->e);
        return 1;
    }
    return 0;
}

/* Entity.setFire on the server player (no Fire Protection in these runs) */
static void sr_player_set_fire(void *ctx, int ticks)
{
    struct serverreplay *sr = ctx;
    if (sr->player != NULL && sr->player->e.fire < ticks) sr->player->e.fire = ticks;
}

/* EntityLivingBase.onDeath's player half, the credited attacker or the
 * killing source's entity being this replay's player: addToPlayerScore
 * first when score (EntityPlayer.java:745, scoreValue 0; its mobKills
 * counter, the victim never a player here), then when kill onKillEntity's
 * killEnemy for an IMob and the victim egg's stat.killEntity.<name>; last
 * EntitySkeleton.onDeath's tail, snipeSkeleton for a skeleton the player's
 * arrow killed from 50 blocks or more. */
static void sr_player_kill(void *ctx, struct living *victim, int source, int score, int kill)
{
    struct serverreplay *sr = ctx;
    if (!sr->player) return;
    (void)source;

    if (score) surv_add_stat(sr->player, STAT_MOB_KILLS, 1);
    if (!kill) return;

    if (surv_is_imob(victim->kind)) surv_add_stat(sr->player, ACH_KILL_ENEMY, 1);

    int egg = surv_egg_of_kind(victim->kind);
    if (egg >= 0) surv_add_stat(sr->player, STAT_KILL_ENTITY + egg, 1);

    if (victim->kind == HK_SKELETON && victim->player_arrow_hit)
        surv_skeleton_shot(sr->player, victim->e.pos_x, victim->e.pos_z);
}

/* EntityPlayer.triggerAchievement on this replay's player from a mob's
 * tick (EntityLiving.onLivingUpdate's diamondsToYou). */
static void sr_mob_collect(struct an_world *an, int item_entity_id, void *ctx)
{
    (void)an;
    pickobj_server_collect(ctx, item_entity_id);
}

static void sr_player_achievement(struct an_world *an, int stat, void *ctx)
{
    struct serverreplay *sr = ctx;
    (void)an;
    if (sr->player) surv_add_stat(sr->player, stat, 1);
}

/* EntityAIMate.spawnBaby's credit, the fed parent's player being this
 * replay's. */
static void sr_player_bred(struct an_world *an, int kind, void *ctx)
{
    struct serverreplay *sr = ctx;
    (void)an;
    if (sr->player) surv_bred(sr->player, kind);
}

static int sr_player_attack(void *ctx, int source, float amount)
{
    struct serverreplay *sr = ctx;
    if (source == DMG_EXPLOSION) sr->blast_pending = 1;
    /* a hit ahead of the player's own place in the pass: the row's packet
     * queue opens here, so the hit's S19 reaches the client */
    if (sr->player && !sr->player->dev_prequeued && !sr->player->net_phase)
    {
        s2c_clear();
        sr->player->dev_prequeued = 1;
    }
    /* the DamageSource families the player can take: a mob's melee hit keeps
     * its blockable EntityDamageSource (SURV_MOB); the projectiles and thorns
     * keep theirs; magic, indirect magic, wither and the generic
     * environmental source are bypasses-armor (SURV_MAGIC) */
    int kind = source == DMG_EXPLOSION ? SURV_EXPLOSION
             : source == DMG_MOB ? SURV_MOB
             : source == DMG_ARROW ? SURV_ARROW
             : source == DMG_THROWN ? SURV_THROWN
             : source == DMG_FIREBALL ? SURV_FIREBALL
             : source == DMG_THORNS ? SURV_THORNS
             : source == DMG_ANVIL ? SURV_ANVIL : SURV_MAGIC;
    const struct living *attacker = lv_get(lv_get(sr->player_livh)->damage_attacker);
    double at[2] = {attacker ? attacker->e.pos_x : 0.0, attacker ? attacker->e.pos_z : 0.0};
    /* a shooterless arrow (EntityPlayer.attackEntityFrom keeps the arrow when
     * its shootingEntity is null): the knockback comes from the arrow */
    int arrow_src = attacker == NULL && lv_get(sr->player_livh)->hit_src_arrow;
    if (arrow_src)
    {
        at[0] = lv_get(sr->player_livh)->hit_src_x;
        at[1] = lv_get(sr->player_livh)->hit_src_z;
    }
    /* the CombatTracker's attacker: DamageSource.getEntity() */
    surv_set_attacker(attacker == NULL ? -1 : attacker == lv_get(sr->player_livh) ? SK_PLAYER : attacker->kind);
    surv_set_attacker_ref(attacker == NULL || attacker == lv_get(sr->player_livh) ? 0 : lv_ref(attacker));
    /* the death message's source: the family and its entity's name */
    surv_set_attacker_msg(source, attacker == NULL ? -1 : attacker == lv_get(sr->player_livh) ? SK_PLAYER
                          : attacker->kind);
    /* the twin's rand and sv.erand are the same java.util.Random: the caller
     * may have advanced l->rand already this tick (the potion-particle draw
     * inside updatePotionEffects), so sync before the damage path draws and
     * sync back after, or the two copies fork and the draws are lost */
    sr->player->sv.erand = lv_get(sr->player_livh)->rand;
    if (attacker || arrow_src)
    {
        /* the knockback lands on the server player's motion, which the
         * entity pass copies back from the living twin after each entity */
        sr->player->e.motion_x = lv_get(sr->player_livh)->e.motion_x;
        sr->player->e.motion_y = lv_get(sr->player_livh)->e.motion_y;
        sr->player->e.motion_z = lv_get(sr->player_livh)->e.motion_z;
    }
    int accepted = surv_server_damage_by(sr->player, kind, amount, attacker || arrow_src ? at : NULL);
    lv_get(sr->player_livh)->rand = sr->player->sv.erand;
    if (attacker || arrow_src)
    {
        lv_get(sr->player_livh)->e.motion_x = sr->player->e.motion_x;
        lv_get(sr->player_livh)->e.motion_y = sr->player->e.motion_y;
        lv_get(sr->player_livh)->e.motion_z = sr->player->e.motion_z;
    }
    lv_get(sr->player_livh)->health = sr->player->sv.health;
    /* and the absorption the hit ate (damageEntity's setAbsorptionAmount):
     * the potion tick copies the twin back over the player afterwards */
    lv_get(sr->player_livh)->absorption = sr->player->sv.absorption;
    lv_get(sr->player_livh)->is_dead = sr->player->sv.dead;
    return accepted;
}

/* WorldManager.playAuxSFX's fan-out (ServerConfigurationManager.func_148543_a
 * with no excluded player): an effect in the player's world within 64
 * blocks of the player sends it an S28, in the tick's queue order. The
 * list is playerEntityList, which keeps a dead player (and one past the
 * exit portal) until its respawn */
static void sr_aux_sfx(void *ctx, struct world *w, int type, int x, int y, int z, int data)
{
    struct serverreplay *sr = ctx;
    struct server_player *p = sr->player;
    if (p == NULL || w != sr_dim_world(sr, p->dimension)) return;
    double dx = (double)x - p->e.pos_x, dy = (double)y - p->e.pos_y, dz = (double)z - p->e.pos_z;
    if (!(dx * dx + dy * dy + dz * dz < 4096.0)) return;
    /* an effect of the world tick or of an entity ahead of the player in
     * the pass opens the row's queue, as a head-of-tick dev op does */
    if (!p->dev_prequeued && !p->net_phase)
    {
        s2c_clear();
        p->dev_prequeued = 1;
    }
    struct s2c_pkt *pk = s2c_add(s2c_out());
    pk->kind = PK_S28;
    pk->i0 = type;
    pk->i1 = data;
    pk->f0 = x;
    pk->f1 = y;
    pk->f2 = z;
}

/* WorldServer.addBlockEvent: queued (an equal one already queued is
 * dropped) for the world's next func_147488_Z */
static void sr_block_event(void *ctx, struct world *w, int x, int y, int z, int block, int event, int param)
{
    struct serverreplay *sr = ctx;
    for (int i = 0; i < sr->nbev; ++i)
    {
        const struct sr_bev *b = &sr->bevs[i];
        if (b->w == w && b->x == x && b->y == y && b->z == z && b->block == block && b->event == event &&
            b->param == param)
            return;
    }
    if (sr->nbev == (int)(sizeof sr->bevs / sizeof sr->bevs[0])) return;
    sr->bevs[sr->nbev++] = (struct sr_bev){w, x, y, z, block, event, param};
}

/* WorldServer.func_147488_Z, the world tick's last step: each queued event
 * of this world whose block still answers it (a chest's does) goes to the
 * players within 64 blocks of (x + 0.5, y + 0.5, z + 0.5) as an S24 */
static void sr_send_block_events(struct serverreplay *sr, struct world *w)
{
    int keep = 0;
    for (int i = 0; i < sr->nbev; ++i)
    {
        const struct sr_bev b = sr->bevs[i];
        if (b.w != w)
        {
            sr->bevs[keep++] = b;
            continue;
        }
        /* func_148541_a: playerEntityList, a dead player too */
        struct server_player *p = sr->player;
        if (p == NULL || w != sr_dim_world(sr, p->dimension)) continue;
        if ((world_get_block(w, b.x, b.y, b.z) & 4095) != b.block) continue;
        double dx = (double)b.x + 0.5 - p->e.pos_x, dy = (double)b.y + 0.5 - p->e.pos_y;
        double dz = (double)b.z + 0.5 - p->e.pos_z;
        if (!(dx * dx + dy * dy + dz * dz < 4096.0)) continue;
        if (!p->dev_prequeued && !p->net_phase)
        {
            s2c_clear();
            p->dev_prequeued = 1;
        }
        struct s2c_pkt *pk = s2c_add(s2c_out());
        pk->kind = PK_S24;
        pk->i0 = b.block;
        pk->i1 = b.event;
        pk->i2 = b.param;
        pk->f0 = b.x;
        pk->f1 = b.y;
        pk->f2 = b.z;
    }
    sr->nbev = keep;
}

/* WorldServer.newExplosion's fan-out: an explosion in the player's world
 * within 64 blocks of the player sends it an S27 (the explosion.on_done
 * hook, after doExplosionB(false)). The list is the world's
 * playerEntities: a dead player stays in it, World.removeEntity after the
 * exit portal takes the player out */
static void sr_explosion_done(void *ctx, struct world *w, double x, double y, double z, float size, int naffected,
                              const struct s27_xp *xp, int nxp)
{
    struct serverreplay *sr = ctx;
    struct server_player *p = sr->player;
    if (p == NULL || p->conquered_end || p->limbo || p->offworld || w != sr_dim_world(sr, p->dimension)) return;
    double dx = p->e.pos_x - x, dy = p->e.pos_y - y, dz = p->e.pos_z - z;
    if (!(dx * dx + dy * dy + dz * dz < 4096.0)) return;
    if (sr->ns27 == (int)(sizeof sr->s27s / sizeof sr->s27s[0])) return;
    struct sr_s27 *e = &sr->s27s[sr->ns27++];
    *e = (struct sr_s27){x, y, z, size, naffected, nxp, {{0, 0}}};
    memcpy(e->xp, xp, (size_t)nxp * sizeof *xp);
}

void serverreplay_queue_packets(struct serverreplay *sr)
{
    if ((!sr->has_s27 && sr->ns27 == 0) || sr->player == NULL)
    {
        sr->ns27 = 0;
        return;
    }
    /* each explosion's S27 at the head of the tick's queue, in order: the
     * knockback (summed over the tick's blasts) rides the first */
    struct s2c_queue *q = s2c_out();
    int n = sr->ns27 > 0 ? sr->ns27 : 1;
    for (int k = n - 1; k >= 0; --k)
    {
        if (q->n >= S2C_MAX) break;
        memmove(&q->q[1], &q->q[0], (size_t)q->n * sizeof q->q[0]);
        ++q->n;
        memset(&q->q[0], 0, sizeof q->q[0]);
        struct s2c_pkt *pk = &q->q[0];
        pk->kind = PK_S27;
        if (k == 0)
        {
            pk->f0 = (double)(float)sr->s27_x;
            pk->f1 = (double)(float)sr->s27_y;
            pk->f2 = (double)(float)sr->s27_z;
        }
        /* the packet's center, strength and block count */
        if (sr->ns27 > 0)
        {
            const struct sr_s27 *e = &sr->s27s[k];
            struct s2c_s27 *x = s2c_side_new(q, pk, sizeof *x);
            *x = (struct s2c_s27){e->x, e->y, e->z, e->size, e->naffected, e->nxp, {{0, 0}}};
            memcpy(x->xp, e->xp, (size_t)e->nxp * sizeof e->xp[0]);
        }
    }
    sr->has_s27 = 0;
    sr->ns27 = 0;
    sr->s27_x = sr->s27_y = sr->s27_z = 0.0;
}

/* EntityPlayerMP's potion hooks (oracle/src/entity/player/EntityPlayerMP.java
 * 1001-1017): onNewPotionEffect and onChangedPotionEffect both send the S1D
 * carrying the effect at its state at send time (the duration clamped to the
 * short's 32767), onFinishedPotionEffect sends the S1E. The queue position is
 * the potion call's point inside the tick, ahead of the tracker pass. */
static int sr_potion_event(void *ctx, int event, int id, int amplifier, int duration)
{
    struct serverreplay *sr = ctx;
    if (sr->player == NULL) return 0;
    struct s2c_queue *q = s2c_out();
    if (q->n >= S2C_MAX) return 0;

    struct s2c_pkt *pkt = &q->q[q->n++];
    memset(pkt, 0, sizeof *pkt);
    if (event == 2)
    {
        pkt->kind = PK_S1E;
        pkt->i0 = id;
    }
    else
    {
        pkt->kind = PK_S1D;
        pkt->i0 = id;
        pkt->i1 = amplifier;
        pkt->i2 = duration > 32767 ? 32767 : duration;
    }
    return 1;
}
/* EntityLivingBase.heal on the player (the twin already clamped it to its
 * maximum): the server player's health */
static void sr_player_heal(void *ctx, float health)
{
    struct serverreplay *sr = ctx;
    if (sr->player != NULL) sr->player->sv.health = health;
}
static void sr_player_set_in_portal(void *ctx);
static void sr_player_end_portal(void *ctx);
static void sr_player_conquer_end(struct serverreplay *sr);
static void sr_player_entity_update(void *ctx);

void serverreplay_bind_player(struct serverreplay *sr, struct server_player *player, int entity_id)
{
    sr->player = player;
    player->e.set_in_portal = sr_player_set_in_portal;
    player->e.end_portal = sr_player_end_portal;
    player->on_entity_update = sr_player_entity_update;
    player->on_entity_ctx = sr;
    player->replay = sr;
    sr->d->iew.player = &player->e;
    sr->d->anw.iew.player = &player->e;
    sr->player_entity_id = entity_id;
    nw_env->explosion.on_done = sr_explosion_done;
    nw_env->explosion.on_done_ctx = sr;
    nw_env->aux_sfx.fn = sr_aux_sfx;
    nw_env->aux_sfx.ctx = sr;
    nw_env->block_event.fn = sr_block_event;
    nw_env->block_event.ctx = sr;
    /* a v2 snapshot placed the player's entry among its entities; else the
     * snapshot's only entity: the player heads its world's list */
    int at = sr_order_find_player(sr);
    if (at >= 0) sr->d->ents[at].id = sr_ent_id(sr, sr->d->ents[at].pool, player);
    else
    {
        sr_order_push(sr, 3, player);
        if (sr->d->nents > 1)
        {
            struct sr_ent e = sr->d->ents[sr->d->nents - 1];
            memmove(&sr->d->ents[1], &sr->d->ents[0], (size_t)(sr->d->nents - 1) * sizeof *sr->d->ents);
            sr->d->ents[0] = e;
        }
    }
    if (!sr->mobs_enabled) return;
    det_state fake;
    det_init(&fake);
    sr->player_livh = lv_ref(living_alloc());
    /* the world the player joined in: a checkpoint in the End or the Nether
     * starts there, and the twin's canEntityBeSeen traces through it (an
     * enderman's shouldAttackPlayer; sr_transfer_player moves it later) */
    living_init(lv_get(sr->player_livh), sr_dim_world(sr, sr->here), HK_PLAYER, &fake);
    player_construct(lv_get(sr->player_livh), &fake);
    det_free(&fake);
    lv_get(sr->player_livh)->an = &sr->d->anw;
    lv_get(sr->player_livh)->player_mp = 1;
    lv_get(sr->player_livh)->player_sp = player;
    lv_get(sr->player_livh)->external_attack = sr_player_attack;
    lv_get(sr->player_livh)->external_attack_ctx = sr;
    lv_get(sr->player_livh)->external_set_fire = sr_player_set_fire;
    lv_get(sr->player_livh)->external_heal = sr_player_heal;
    lv_get(sr->player_livh)->on_potion_event = sr_potion_event;
    lv_get(sr->player_livh)->on_potion_event_ctx = sr;
    /* EntityPlayer.onKillEntity and addToPlayerScore, from onDeath's var2 */
    lv_get(sr->player_livh)->on_death = sr_player_kill;
    lv_get(sr->player_livh)->on_death_ctx = sr;
    sr->d->anw.bred_hook = sr_player_bred;
    sr->d->anw.bred_ctx = sr;
    sr->d->anw.achievement_hook = sr_player_achievement;
    sr->d->anw.collect_hook = sr_mob_collect;
    lv_get(sr->player_livh)->entity_id = entity_id;
    /* the twin's rand is Entity.rand of the player: the same stream the
     * survival code draws from (sv.erand, which the snapshot carries). The
     * whole det_rng rides along: it owns a pending nextGaussian. */
    lv_get(sr->player_livh)->rand = player->sv.erand;
    attrs_set_base(&lv_get(sr->player_livh)->attrs.a[ATTR_MAX_HEALTH], 20.0);
    sr->player_enth = an_ref(an_ent_alloc());
    an_deref(sr->player_enth)->used = an_deref(sr->player_enth)->is_living = 1;
    an_deref(sr->player_enth)->livh = lv_ref(lv_get(sr->player_livh));
    sr->d->anw.playerh = lv_ref(lv_get(sr->player_livh));
    /* every dimension's living world: the player reaches the Nether's and
     * the End's lists through a portal later (a pearl thrown there lands
     * there) */
    for (int di = 0; di < 3; ++di)
    {
        if (!sr->dims[di].live) continue;
        sr->dims[di].anw.iew.on_egg_chicken = sr_anw_egg_chicken;
        sr->dims[di].anw.iew.on_arrow_tnt = sr_arrow_tnt;
        sr->dims[di].anw.iew.on_arrow_button = sr_arrow_button;
        /* a TNT a creeper's blast primed sits in the living world's items:
         * its own explosion is the replay's too */
        sr->dims[di].anw.iew.on_tnt_explode = tnt_replay_explode;
        sr->dims[di].anw.iew.tnt_ctx = sr;
    }
    sr->d->iew.peer = &sr->d->anw.iew;
    sr->d->anw.iew.peer = &sr->d->iew;
    throw_bind(sr);
    leash_bind(sr);
    serverreplay_resolve_village_agressors(sr);
    for (int i = 0; i < sr->nrt_player_refs; ++i) *sr->rt_player_refs[i] = lv_ref(lv_get(sr->player_livh));
    slab_table_free(sr->rt_player_refs, sr->cap_rt_player_refs, sizeof *sr->rt_player_refs);
    sr->rt_player_refs = NULL;
    sr->nrt_player_refs = sr->cap_rt_player_refs = 0;
    sr_runtime_player_stamp(sr);
    /* the player's potion effects (EntityLivingBase.readEntityFromNBT's
     * ActiveEffects; the attribute half is in the snapshot's values) and
     * the runtime flags beside them */
    if (sr->snap)
    {
        const nbt *fx = nbt_get(nbt_get(sr->snap->player_server, "nbt"), "ActiveEffects");
        for (int i = 0; fx && i < nbt_list_size(fx); ++i)
        {
            const nbt *c = nbt_list_get(fx, i);
            struct potion_effect eff = {
                .id = (uint8_t)nbt_int_value(nbt_get(c, "Id")),
                .amplifier = (int8_t)nbt_int_value(nbt_get(c, "Amplifier")),
                .duration = (int)nbt_int_value(nbt_get(c, "Duration")),
                .is_splash = 0,
                .is_ambient = (uint8_t)nbt_int_value(nbt_get(c, "Ambient")),
            };
            potion_map_put(&lv_get(sr->player_livh)->potions, &eff);
        }
        /* SharedMonsterAttributes.func_151475_a: the Attributes list's saved
         * modifiers, the running potions' (potion.moveSpeed 0, potion.damageBoost
         * 0) among them, which the twin's own Attributes write carries again
         * and the effect's end removes by UUID (pfc-potattr-g26) */
        const nbt *al = nbt_get(nbt_get(sr->snap->player_server, "nbt"), "Attributes");
        for (int i = 0; al && i < nbt_list_size(al); ++i)
        {
            const nbt *one = nbt_list_get(al, i);
            int idx = attrs_index_by_name(nbt_string_value(nbt_get(one, "Name")));
            const nbt *mods = nbt_get(one, "Modifiers");
            for (int k = 0; idx >= 0 && mods && k < nbt_list_size(mods); ++k)
            {
                const nbt *m = nbt_list_get(mods, k);
                const char *mn = nbt_string_value(nbt_get(m, "Name"));
                if (mn == NULL || strncmp(mn, "potion.", 7) != 0) continue;
                struct attr_mod mod;
                memset(&mod, 0, sizeof mod);
                uint64_t bits = nbt_double_bits(nbt_get(m, "Amount"));
                memcpy(&mod.amount, &bits, sizeof mod.amount);
                mod.operation = (int)nbt_int_value(nbt_get(m, "Operation"));
                mod.name = attr_name_id(mn);
                mod.uuid_msb = nbt_int_value(nbt_get(m, "UUIDMost"));
                mod.uuid_lsb = nbt_int_value(nbt_get(m, "UUIDLeast"));
                mod.saved = 1;
                attrs_apply(&lv_get(sr->player_livh)->attrs.a[idx], &mod);
            }
        }
        const nbt *rt = nbt_get(sr->snap->player_server, "rt");
        const nbt *nu = nbt_get(nbt_get(rt, "f"), "potionsNeedUpdate");
        if (nu) lv_get(sr->player_livh)->potions_need_update = (uint8_t)nbt_int_value(nu);
        const nbt *dw = nbt_get(rt, "dw");
        if (nbt_get(dw, "7")) lv_get(sr->player_livh)->potion_liquid_color = (int)nbt_int_value(nbt_get(dw, "7"));
        if (nbt_get(dw, "8")) lv_get(sr->player_livh)->potion_is_ambient = (uint8_t)nbt_int_value(nbt_get(dw, "8"));
        /* the watcher's invisible flag (setInvisible, only from the
         * potion pass's update): an invisible player's particle roll is
         * nextInt(15), not nextBoolean (pfc-invisjoin-f111) */
        if (nbt_get(dw, "0"))
        {
            struct living *tw = lv_get(sr->player_livh);
            tw->flags0 = (tw->flags0 & ~(1 << 5)) | ((int)nbt_int_value(nbt_get(dw, "0")) & (1 << 5));
        }
    }
    /* a snapshot taken while the player rides: EntityPlayerMP.ridingEntity
     * (the runtime's ridingEntity) and the vehicle's riddenByEntity */
    if (sr->snap)
    {
        const nbt *rf = nbt_get(nbt_get(sr->snap->player_server, "rt"), "f");
        const nbt *rv = rf ? nbt_get(rf, "ridingEntity") : NULL;
        const char *rs = rv ? nbt_string_value(rv) : NULL;
        int vid = rs && !strncmp(rs, "e:", 2) ? (int)strtol(rs + 2, NULL, 10) : -1;
        for (int i = 0; vid > 0 && i < sr->d->anw.n; ++i)
        {
            struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
            if (!en->used || !en->is_living || !en->livh || lv_get(en->livh)->entity_id != vid) continue;
            player->ridingh = lv_ref(lv_get(en->livh));
            lv_get(sr->player_livh)->riding_entity = lv_ref(lv_get(en->livh));
            lv_get(en->livh)->ridden_by_entity = lv_ref(lv_get(sr->player_livh));
            break;
        }
    }
}

int serverreplay_dead_player_id(struct snapshot *snap, struct server_player *sp, int *removed)
{
    const nbt *id = nbt_get(nbt_get(nbt_get(snap->player_server, "rt"), "f"), "field_145783_c");
    if (id == NULL) return -1;
    /* EntityPlayerMP in playerEntities but out of loadedEntityList: the
     * entity count still holds its slot until the respawn's new player */
    sp->sv.removed = 1;
    sp->sv.dead = 1;
    snap->players = 1;
    *removed = 1;
    return (int)nbt_int_value(id);
}

static struct living *sr_living_by_id(void *ctx, int id)
{
    struct serverreplay *sr = ctx;
    if (sr->player_livh != 0 && lv_get(sr->player_livh)->entity_id == id) return lv_get(sr->player_livh);
    for (int i = 0; i < sr->d->anw.n; ++i)
        if (an_ent_at(sr->d->anw.slot[i])->is_living && an_ent_at(sr->d->anw.slot[i])->livh != 0 && lv_get(an_ent_at(sr->d->anw.slot[i])->livh)->entity_id == id)
            return lv_get(an_ent_at(sr->d->anw.slot[i])->livh);
    return NULL;
}

/* A snapshot's village aggressors name entities by id (villages.nbt's Agg):
 * each is resolved once its living (or the player) exists. */
void serverreplay_resolve_village_agressors(struct serverreplay *sr)
{
    vc_resolve_agressors(&sr->vc, sr_living_by_id, sr);
}

static void sr_player_scene(struct serverreplay *sr)
{
    if (sr->player == NULL) return;
    struct living *l = lv_get(sr->player_livh);
    const struct server_player *p = sr->player;
    int cx = (int)floor(p->e.pos_x / 16.0);
    int cy = (int)floor(p->e.pos_y / 16.0);
    int cz = (int)floor(p->e.pos_z / 16.0);
    /* unlisted, no world update files it anew (updateEntityWithOptionalForce) */
    int refile = !sr->player_unlisted;
    if (refile && l->added_to_chunk &&
        (l->chunk_coord_x != cx || l->chunk_coord_y != cy || l->chunk_coord_z != cz))
    {
        an_chunk_remove(&sr->d->anw, an_deref(sr->player_enth), l->chunk_coord_y);
        l->added_to_chunk = 0;
    }
    l->e.pos_x = p->e.pos_x;
    l->e.pos_y = p->e.pos_y;
    l->e.pos_z = p->e.pos_z;
    l->e.bounding_box = p->e.bounding_box;
    l->e.motion_x = p->e.motion_x;
    l->e.motion_y = p->e.motion_y;
    l->e.motion_z = p->e.motion_z;
    /* EntityEnderman.shouldAttackPlayer reads the player's getLook(1.0F) */
    l->rotation_yaw = p->rotation_yaw;
    l->rotation_pitch = p->rotation_pitch;
    l->health = p->sv.health;
    l->is_dead = p->sv.dead;
    /* (a dead player out of the world is in no chunk's list) */
    if (refile && !l->added_to_chunk && !p->sv.removed) an_chunk_add(&sr->d->anw, an_deref(sr->player_enth), cx, cy, cz);
}

static int sr_dragon_before(const struct dragon_state *d, const struct living *q);

/* Entity.applyEntityCollision of the dragon with the player as the other. */
static void sr_player_dragon_push(struct serverreplay *sr, struct dragon_state *d)
{
    double v2 = lv_get(sr->player_livh)->e.pos_x - d->x;
    double v4 = lv_get(sr->player_livh)->e.pos_z - d->z;
    double v6 = fabs(v2) > fabs(v4) ? fabs(v2) : fabs(v4);
    if (v6 < 0.009999999776482582) return;
    v6 = (double)(float)sqrt(v6);
    v2 /= v6;
    v4 /= v6;
    double v8 = 1.0 / v6;
    if (v8 > 1.0) v8 = 1.0;
    v2 *= v8;
    v4 *= v8;
    v2 *= 0.05000000074505806;
    v4 *= 0.05000000074505806;
    d->mx -= v2;
    d->mz -= v4;
    d->is_air_borne = 1;
    lv_get(sr->player_livh)->e.motion_x += v2;
    lv_get(sr->player_livh)->e.motion_z += v4;
}

/* The player moved inside the entity pass (a pearl's setPositionAndUpdate):
 * the entities after it in the list read the one EntityPlayerMP, so the
 * pass's copies follow at once. Its chunk membership waits for the player's
 * own update, as World.updateEntityWithOptionalForce's does. */
void serverreplay_player_moved(struct serverreplay *sr)
{
    if (!sr || !sr->player || !sr->player_livh) return;
    const struct server_player *p = sr->player;
    struct living *l = lv_get(sr->player_livh);
    l->e.pos_x = p->e.pos_x;
    l->e.pos_y = p->e.pos_y;
    l->e.pos_z = p->e.pos_z;
    l->e.bounding_box = p->e.bounding_box;
    l->rotation_yaw = p->rotation_yaw;
    l->rotation_pitch = p->rotation_pitch;
    if (sr->d->anw.has_player)
    {
        sr->d->anw.player_x = p->e.pos_x;
        sr->d->anw.player_y = p->e.pos_y;
        sr->d->anw.player_z = p->e.pos_z;
    }
    if (sr->d->anw.user_data != NULL)
    {
        struct gh_player *gp = &((struct gh_world *)sr->d->anw.user_data)->player;
        gp->pos_x = p->e.pos_x;
        gp->pos_y = p->e.pos_y;
        gp->pos_z = p->e.pos_z;
        gp->rotation_yaw = p->rotation_yaw;
        gp->rotation_pitch = p->rotation_pitch;
    }
}

void serverreplay_player_resync(struct serverreplay *sr)
{
    if (sr != NULL && sr->player != NULL && sr->player_livh != 0) sr_player_scene(sr);
}

/* EntityPlayerMP's onLivingUpdate runs after the world entity pass, inside
 * processPlayer. Its collideWithNearbyEntities applies each nearby living
 * entity's applyEntityCollision to the player. */
void serverreplay_player_collide(struct serverreplay *sr)
{
    if (!sr || !sr->mobs_enabled || !sr->player_livh) return;
    sr_player_scene(sr);
    struct aabb box = aabb_expand(lv_get(sr->player_livh)->e.bounding_box,
                                  0.20000000298023224, 0.0, 0.20000000298023224);
    AN_QUERY_LIST(near);
    int n = an_entities_excluding(&sr->d->anw, an_deref(sr->player_enth), &box, near, AN_MAX_ENTITIES);
    /* the dragon is an EntityLivingBase in the End's lists, pushable while it
     * is not dead: its applyEntityCollision moves both, at its place in the
     * query's order (chunk x, then z, then section, then the list's
     * insertion), where the pushes add up in the same order as Java's */
    struct dragon_state *d = NULL;
    if (sr->here == 1 && sr->end != NULL && sr->end->dragon_listed && !sr->end->d.dead &&
        endfight_dragon_listed_near(&sr->end->d, &box))
    {
        struct aabb db = aabb_make(sr->end->d.bb_min_x, sr->end->d.bb_min_y, sr->end->d.bb_min_z,
                                   sr->end->d.bb_max_x, sr->end->d.bb_min_y + 8.0, sr->end->d.bb_max_z);
        if (aabb_intersects(&db, &box)) d = &sr->end->d;
    }
    for (int i = 0; i <= n; ++i)
    {
        if (d != NULL)
        {
            int before = i == n;
            if (!before)
            {
                const struct living *q = near[i]->is_living ? lv_get(near[i]->livh) : NULL;
                if (q != NULL) before = sr_dragon_before(d, q);
            }
            if (before)
            {
                sr_player_dragon_push(sr, d);
                d = NULL;
            }
        }
        if (i == n) break;
        if (!near[i]->is_living || !living_can_be_pushed(lv_get(near[i]->livh))) continue;
        living_apply_entity_collision_pub(lv_get(near[i]->livh), lv_get(sr->player_livh));
    }

    /* EntityPlayer.onLivingUpdate's own query, over the 1.0/0.5/1.0 box:
     * every entity in it takes onCollideWithPlayer(player). EntitySlime's
     * override is the size-scaled damage and its two attack-sound draws; the
     * items and orbs the same Java loop reaches are the survival module's
     * own pools (surv_server_living_tail). */
    {
        struct aabb wide = sr->player != NULL ? ride_server_pickup_box(sr->player)
                                              : aabb_expand(lv_get(sr->player_livh)->e.bounding_box, 1.0, 0.5, 1.0);
        AN_QUERY_LIST(hit);
        int nhit = an_entities_excluding(&sr->d->anw, an_deref(sr->player_enth), &wide, hit, AN_MAX_ENTITIES);

        for (int i = 0; i < nhit; ++i)
        {
            if (!hit[i]->is_living || lv_get(hit[i]->livh)->is_dead) continue;
            if (!IS_SLIME_KIND(lv_get(hit[i]->livh)->kind)) continue;
            slime_on_collide_with_player(lv_get(hit[i]->livh), lv_get(sr->player_livh), &SR_DET(sr));
        }
    }

    sr->player->e.motion_x = lv_get(sr->player_livh)->e.motion_x;
    sr->player->e.motion_y = lv_get(sr->player_livh)->e.motion_y;
    sr->player->e.motion_z = lv_get(sr->player_livh)->e.motion_z;
}

/* EntityPlayerMP.onUpdate with the chunk sends it makes, at the player's
 * place in the pass or from its vehicle's update (riding.c). A player out
 * of loadedEntityList (dead, or past the exit portal) gets no onUpdate from
 * World.updateEntities: no chunk goes out, and no tracker entry tries it
 * for one (func_85172_a), until the respawn's new player */
void serverreplay_player_on_update(struct serverreplay *sr, struct world *w)
{
    sr_player_world_update(sr);
    if (sr->player->sv.removed) return;
    sr_send_chunks(sr, sr->current_tick);
    sr_player_send_chunks(sr, w);
}

float serverreplay_rain_strength(struct serverreplay *sr, int dim)
{
    struct sr_world *ws = sr_dim_ws(sr, dim);
    return ws ? ws->st.raining_strength : 0.0F;
}

struct world *serverreplay_player_world(struct serverreplay *sr)
{
    return sr_dim_ws(sr, serverreplay_player_dim(sr))->st.w;
}

struct living *serverreplay_player_living(struct serverreplay *sr)
{
    return lv_get(sr->player_livh);
}

/* EntityPlayer.onLivingUpdate's own query, over the 1.0/0.5/1.0 box (joined
 * with the vehicle's while riding) */
/* EntityPlayerMP.onUpdate at the player's place in the pass. The world tick
 * runs ahead of the network tick that normally opens the row's packet queue,
 * so it opens it here (as a head-of-tick dev op does); a delayed dig break's
 * drops join the pass's order. */
static void sr_player_world_update(struct serverreplay *sr)
{
    struct server_player *p = sr->player;

    if (p->sv.removed) return;

    if (!p->dev_prequeued && !p->net_phase)
    {
        s2c_clear();
        p->dev_prequeued = 1;
    }

    int before = sr->d->iew.n;
    surv_server_world_update(p);
    for (int j = before; j < sr->d->iew.n; ++j) sr_order_push(sr, 0, ie_ent_at(sr->d->iew.slot[j]));
}

/* The entity pass writes too (a falling block's first tick clears its block),
 * and servertick's listener is only installed while its own tick body runs. */
static void sr_on_block(void *ctx, int x, int y, int z, int id, int meta)
{
    struct serverreplay *sr = ctx;

    if (sr->d->nextra == ST_MAX_WRITES) abort();

    struct st_write *w = &sr->d->extra[sr->d->nextra++];
    if (sr->d->nextra > sr->d->hiextra) sr->d->hiextra = sr->d->nextra;
    w->x = x;
    w->y = y;
    w->z = z;
    w->id = (int16_t)id;
    w->meta = (uint8_t)meta;
    /* an explosion's writes to its listed cells: the S27 names them and
     * the client's own doExplosionB clears them with the next tick's
     * packets, ahead of the flush (SR_WRITE_S27); a write the clears set
     * off elsewhere (a double plant's other half off the list) is the
     * server's alone, as the client world runs no neighbour change, and
     * waits for the flush */
    w->pad = 0;
    for (int i = 0; nw_env->explosion.listed != NULL && i < nw_env->explosion.nlisted; ++i)
    {
        const int *c = &nw_env->explosion.listed[i * 3];
        if (c[0] == x && c[1] == y && c[2] == z) { w->pad = SR_WRITE_S27; break; }
    }
    const struct world *ww = sr_dim_world(sr, sr->here);
    if (ww != NULL && !(ww->write_flags & 2)) w->pad |= ST_WRITE_UNSENT;
}

/* --------------------------------------------------------------- the tick */

/* The entity an order entry names, and the name of an entity in a pool:
 * the pools' own slots (ie, fh, an), the crystal's index in the End's
 * dragon state, 0 for the player and the dragon; -1 and NULL for none. */
void *sr_ent_p(const struct serverreplay *sr, const struct sr_ent *e)
{
    if (e->id < 0) return NULL;
    switch (e->pool)
    {
    case 0: return ie_ent_at(e->id);
    case 1: return fh_ent_at(e->id);
    case 2: return an_ent_at(e->id);
    case 3: return sr->player;
    case SR_POOL_DRAGON: return &sr->end->d;
    case SR_POOL_CRYSTAL: return &sr->end->d.crystals[e->id];
    }
    abort();
}

int32_t sr_ent_id(const struct serverreplay *sr, int pool, const void *p)
{
    if (p == NULL) return -1;
    switch (pool)
    {
    case 0: return ie_ent_index(p);
    case 1: return fh_ent_index(p);
    case 2: return an_ent_index(p);
    case 3: return 0;
    case SR_POOL_DRAGON: return 0;
    case SR_POOL_CRYSTAL: return (int32_t)((const struct dragon_crystal_state *)p - sr->end->d.crystals);
    }
    abort();
}

/* a saved record's live instance, and the test against one */
static struct sr_ent sr_active_of(const struct serverreplay *sr, int pool, const void *p)
{
    return (struct sr_ent){pool, sr_ent_id(sr, pool, p)};
}

static int sr_active_is(const struct serverreplay *sr, const struct sr_ent *a, int pool, const void *p)
{
    return a->id >= 0 && a->pool == pool && sr_ent_p(sr, a) == p;
}

/* A saved record's live instance in the living world (pool 2), with its
 * slot's generation. */
static void sr_saved_activate(const struct serverreplay *sr, struct sr_saved_entity *saved, const struct an_ent *en)
{
    saved->active = sr_active_of(sr, 2, en);
    saved->active_gen = en != NULL ? nw_env->arena.an_ents.gen[an_ent_index(en)] : 0;
}

static int sr_saved_active_is(const struct serverreplay *sr, const struct sr_saved_entity *saved, const struct an_ent *en)
{
    return sr_active_is(sr, &saved->active, 2, en) && saved->active_gen == nw_env->arena.an_ents.gen[an_ent_index(en)];
}

static int sr_order_id(int pool, const void *p)
{
    if (pool == 0) return ((const ie_ent *)p)->order_key > 0 ? ((const ie_ent *)p)->order_key : ((const ie_ent *)p)->entity_id;
    if (pool == 1) return ((const fh_ent *)p)->entity_id;
    if (pool == SR_POOL_DRAGON) return ((const struct dragon_state *)p)->entity_id;
    if (pool == SR_POOL_CRYSTAL)
        return ((const struct dragon_crystal_state *)p)->order_key > 0 ? ((const struct dragon_crystal_state *)p)->order_key
                                                                         : ((const struct dragon_crystal_state *)p)->entity_id;
    const struct an_ent *en = p;
    if (en->order_key > 0) return en->order_key;
    return en->is_living ? lv_get(en->livh)->entity_id : ie_get(en->ieh)->entity_id;
}

/* One entity joins the pass's order, at the tail, as Java's spawnEntityInWorld
 * appends to loadedEntityList. */
void sr_order_push(struct serverreplay *sr, int pool, void *p)
{
    if (sr->d->nents == SR_MAX_ORDER) abort();

    /* the player's entry (pool 3) always joins at the tail, as
     * spawnEntityInWorld appends it; the others after a v2 snapshot keep id
     * order behind the snapshot's own, passing the player's entry only
     * together with the entity ahead of it */
    int at = sr->d->nents;
    /* a chunk load inside a spawn candidate's getCanSpawnHere: what it
     * brings joins loadedEntityList ahead of the candidate, whose
     * spawnEntityInWorld follows the check (its id is the smaller one) */
    if (pool == 2 && sr->in_spawner_tick && sr->d->spawner.checking_id > 0 && ((struct an_ent *)p)->order_key == 0)
        ((struct an_ent *)p)->order_key = sr->d->spawner.checking_id;
    /* the items such a load brings too, in the chunk's order among them */
    if (pool == 0 && sr->in_spawner_tick && sr->d->spawner.checking_id > 0 && ((ie_ent *)p)->order_key == 0)
        ((ie_ent *)p)->order_key = sr->d->spawner.checking_id;
    /* an End spike's crystal a candidate's chunk population placed */
    if (pool == SR_POOL_CRYSTAL && sr->in_spawner_tick && sr->d->spawner.checking_id > 0 &&
        ((struct dragon_crystal_state *)p)->order_key == 0)
        ((struct dragon_crystal_state *)p)->order_key = sr->d->spawner.checking_id;
    if (sr->order_new_by_id && pool != 3)
    {
        int id = sr_order_id(pool, p);
        /* one entry per entity: an item restored from the region store
         * inside a phase whose tail appends every item the phase added (a
         * chunk the player's update loads) arrives twice. The tail is in id
         * order, so the scan stops at the first smaller id. */
        for (int k = sr->d->nents - 1; k >= sr->d->initial_ent_count; --k)
        {
            if (sr->d->ents[k].pool == 3) continue;
            if (sr->d->ents[k].pool == pool && sr_ent_p(sr, &sr->d->ents[k]) == p) return;
            if (sr_order_id(sr->d->ents[k].pool, sr_ent_p(sr, &sr->d->ents[k])) < id) break;
        }
        while (at > sr->d->initial_ent_count)
        {
            if (sr->d->ents[at - 1].pool == 3)
            {
                if (at - 2 < sr->d->initial_ent_count ||
                    sr_order_id(sr->d->ents[at - 2].pool, sr_ent_p(sr, &sr->d->ents[at - 2])) <= id) break;
                sr->d->ents[at] = sr->d->ents[at - 1];
                sr->d->ents[at - 1] = sr->d->ents[at - 2];
                at -= 2;
                continue;
            }
            if (sr_order_id(sr->d->ents[at - 1].pool, sr_ent_p(sr, &sr->d->ents[at - 1])) <= id) break;
            sr->d->ents[at] = sr->d->ents[at - 1];
            --at;
        }
    }
    sr->d->ents[at].pool = pool;
    sr->d->ents[at].id = sr_ent_id(sr, sr->d->ents[at].pool, p);
    ++sr->d->nents;
    /* EntityTracker.trackEntity for the pick's non-living kinds */
    pickobj_server_spawn(sr, pool, p);
}

/* World.loadedEntityList.remove keeps the others in order. The snapshot's
 * entities keep their recorded order ahead of the id-ordered new ones, so a
 * removal among them moves that boundary down with it. */
static void sr_order_remove(struct serverreplay *sr, int k)
{
    if (k < sr->d->initial_ent_count) --sr->d->initial_ent_count;
    memmove(&sr->d->ents[k], &sr->d->ents[k + 1], (size_t)(sr->d->nents - k - 1) * sizeof *sr->d->ents);
    --sr->d->nents;
    /* no stale copy past the end for grave.c's scan to keep */
    memset(&sr->d->ents[sr->d->nents], 0, sizeof *sr->d->ents);
}

/* The Dev module uses the same global entity order as game spawns. */
void serverreplay_track_entity(struct serverreplay *sr, int pool, void *entity)
{
    sr_order_push(sr, pool, entity);
}

/* The living constructors were ported against probe threads and draw from
 * DET_OTHER explicitly. A constructor reached from the integrated server
 * must spend the identical draws in DET_SERVER instead. */
void sr_constructor_begin(det_state *fake, const det_state *real, int role)
{
    det_init(fake);
    fake->world_seed = real->world_seed;
    fake->next_id[DET_OTHER] = real->next_id[role];
    fake->seeder[DET_OTHER] = real->seeder[role];
    fake->math[DET_OTHER] = real->math[role];
    det_set_role(fake, DET_OTHER);
}

void sr_constructor_end(det_state *real, det_state *fake, int role)
{
    real->next_id[role] = fake->next_id[DET_OTHER];
    real->seeder[role] = fake->seeder[DET_OTHER];
    real->math[role] = fake->math[DET_OTHER];
    det_free(fake);
}

/* EntityList.createEntityByName for the spawner ids the ported mobs cover:
 * the dungeon (zombie, skeleton, spider), mineshaft (cave spider), fortress
 * (blaze) and stronghold (silverfish) spawners, and the Pig default. */
static int sr_tile_mob_kind(const char *name)
{
    static const struct { const char *name; int kind; } ids[] = {
        {"Zombie", HK_ZOMBIE}, {"Skeleton", HK_SKELETON}, {"Spider", HK_SPIDER},
        {"CaveSpider", HK_CAVE_SPIDER}, {"Silverfish", HK_SILVERFISH}, {"Blaze", HK_BLAZE},
        {"Creeper", HK_CREEPER}, {"Enderman", HK_ENDERMAN}, {"Witch", HK_WITCH},
        {"PigZombie", HK_PIGMAN}, {"Pig", AK_PIG}, {"Cow", AK_COW}, {"Sheep", AK_SHEEP},
        {"Chicken", AK_CHICKEN},
    };

    if (!name) return -1;
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; ++i)
        if (!strcmp(name, ids[i].name)) return ids[i].kind;
    return -1;
}

/* The constructor createEntityByName runs (the entity is not in the world). */
static struct living *sr_tile_construct(struct serverreplay *sr, int kind, int dim, det_state *det)
{
    struct living *l = living_alloc();
    if (!l) return NULL;
    living_init(l, sr->d->anw.w, kind, det);
    l->an = &sr->d->anw;
    l->dimension = dim;
    switch (kind)
    {
    case HK_ZOMBIE: zombie_construct(l, det); break;
    case HK_SKELETON: skeleton_construct(l, det); break;
    case HK_SPIDER: case HK_CAVE_SPIDER: spider_construct(l, det); break;
    case HK_SILVERFISH: silverfish_construct(l, det); break;
    case HK_BLAZE: blaze_construct(l, det); break;
    case HK_CREEPER: creeper_construct(l, det); break;
    case HK_ENDERMAN: enderman_construct(l, det); break;
    case HK_WITCH: witch_construct(l, det); break;
    case HK_PIGMAN: pigman_construct(l, det); break;
    default: animal_construct(l, det); break;
    }
    return l;
}

/* World.checkNoEntityCollision(box): a live entity that prevents spawning. */
static int sr_tile_no_entity_collision(struct serverreplay *sr, const struct aabb *box)
{
    if (sr->player_livh && !lv_get(sr->player_livh)->is_dead &&
        aabb_intersects(box, &lv_get(sr->player_livh)->e.bounding_box)) return 0;
    for (int i = 0; i < sr->d->nents; ++i)
    {
        const struct sr_ent *rec = &sr->d->ents[i];
        const struct entity *e;
        if (rec->pool == 3) continue;   /* the player is checked above */
        if (rec->pool == 2)
        {
            const struct an_ent *ae = sr_ent_p(sr, rec);
            if (!ae->used) continue;
            if (ae->is_living && lv_get(ae->livh)->is_dead) continue;
            e = ae->is_living ? &lv_get(ae->livh)->e : &ie_get(ae->ieh)->e;
        }
        else if (rec->pool == 1) e = &((const fh_ent *)sr_ent_p(sr, rec))->e;
        else
        {
            const ie_ent *ie = sr_ent_p(sr, rec);
            if (ie->is_dead) continue;
            e = &ie->e;
        }
        if (e->prevent_entity_spawning && aabb_intersects(box, &e->bounding_box)) return 0;
    }
    return 1;
}

/* The natural spawner's checkNoEntityCollision beyond its own boxes (the
 * livings and the player): the non-living entities that set
 * preventEntitySpawning, EntityFallingBlock and EntityTNTPrimed (a creeper
 * under gravel falling into a cave, explore-long-s1). */
static int sr_spawn_blocked(void *ctx, const struct aabb *box)
{
    struct serverreplay *sr = ctx;

    for (int i = 0; i < sr->d->nents; ++i)
    {
        const struct sr_ent *rec = &sr->d->ents[i];

        if (rec->pool == 3) continue;
        if (rec->pool == 1)
        {
            const fh_ent *fh = sr_ent_p(sr, rec);
            if (fh->e.prevent_entity_spawning && aabb_intersects(box, &fh->e.bounding_box)) return 1;
            continue;
        }

        const ie_ent *ie;

        if (rec->pool == 2)
        {
            const struct an_ent *ae = sr_ent_p(sr, rec);
            if (!ae->used || ae->is_living) continue;
            ie = ie_get(ae->ieh);
        }
        else ie = sr_ent_p(sr, rec);
        if (ie->is_dead) continue;
        /* the item pool's primed TNT carries no flag of its own */
        if ((ie->e.prevent_entity_spawning || ie->kind == IE_TNT) && aabb_intersects(box, &ie->e.bounding_box)) return 1;
    }
    return 0;
}

/* getCanSpawnHere down each class's chain: EntityMob's difficulty and
 * isValidLightLevel (the entity's own Random), EntityLiving's entity, block
 * and liquid checks, EntityCreature's getBlockPathWeight; EntityAnimal's grass
 * and light head; EntityPigZombie's own body; EntitySilverfish's closest
 * player. The block collision has no entity boxes: boats and minecarts are
 * switched off. */
static int sr_tile_can_spawn(struct serverreplay *sr, struct sr_world *sw, struct living *l)
{
    struct servertick *st = &sw->st;
    struct spawner *sp = &sr->d->spawner;
    struct aabb box = l->e.bounding_box;
    int x = (int)floor(l->e.pos_x), y = (int)floor(box.min_y), z = (int)floor(l->e.pos_z);
    int animal = l->kind == AK_PIG || l->kind == AK_COW || l->kind == AK_SHEEP || l->kind == AK_CHICKEN;

    if (animal)
    {
        if ((world_get_block(st->w, x, y - 1, z) & 4095) != 2 ||
            world_get_full_block_light_value(st->w, x, y, z, st->skylight_subtracted) <= 8)
            return 0;
    }
    else
    {
        if (st->difficulty == 0) return 0;
        if (l->kind != HK_BLAZE && l->kind != HK_SILVERFISH && l->kind != HK_PIGMAN)
        {
            /* EntityMob.isValidLightLevel */
            if (spawner_saved_light(sp, LIGHT_SKY, x, y, z) > det_rng_int_n(&l->rand, 32)) return 0;
            int v = spawner_block_light_value(sp, x, y, z);
            if (st->thundering)
            {
                int save = sp->skylight_subtracted;
                sp->skylight_subtracted = 10;
                v = spawner_block_light_value(sp, x, y, z);
                sp->skylight_subtracted = save;
            }
            if (v > det_rng_int_n(&l->rand, 8)) return 0;
        }
    }

    if (!sr_tile_no_entity_collision(sr, &box)) return 0;
    if (!world_colliding_boxes_empty(st->w, box)) return 0;
    if (living_world_is_any_liquid(st->w, box)) return 0;
    if (l->kind == HK_PIGMAN) return 1;

    /* EntityCreature: the path weight at the box's feet */
    float weight;
    int below = world_get_block(st->w, x, y - 1, z) & 4095;
    float bright = spawner_brightness(sw->dim, spawner_block_light_value(sp, x, y, z));
    if (animal) weight = below == 2 ? 10.0F : bright - 0.5F;
    else if (l->kind == HK_SILVERFISH && below == 1) weight = 10.0F;
    else weight = 0.5F - bright;
    if (weight < 0.0F) return 0;

    if (l->kind == HK_SILVERFISH && sr->player)
    {
        /* getClosestPlayerToEntity(this, 5.0D) */
        double dx = sr->player->e.pos_x - l->e.pos_x, dy = sr->player->e.pos_y - l->e.pos_y;
        double dz = sr->player->e.pos_z - l->e.pos_z;
        if (dx*dx + dy*dy + dz*dz < 25.0) return 0;
    }
    return 1;
}

static void sr_tile_reset(struct te_spawner *sp, jrand *rand)
{
    if (sp->max_spawn_delay <= sp->min_spawn_delay) sp->spawn_delay = sp->min_spawn_delay;
    else sp->spawn_delay = sp->min_spawn_delay + jr_int_n(rand, sp->max_spawn_delay - sp->min_spawn_delay);
    /* SpawnPotentials (the weighted minecart list) is never set by a ported
     * path; func_98267_a(1)'s block event never reaches a client: the
     * server's own receiveClientEvent answers false (setDelayToMin acts on a
     * client world only), so func_147488_Z sends no S24 for it */
}

/* MobSpawnerBaseLogic.updateSpawner on the server, from World.updateEntities'
 * tile entity pass. */
static void sr_tile_spawner(void *ctx, struct world *w, struct tile_entity *te)
{
    struct serverreplay *sr = ctx;
    struct sr_world *sw = NULL;
    te->u.spawner.desc_delay = te->u.spawner.spawn_delay;
    for (int i = 0; i < sr->nworlds; ++i)
        if (sr->w[i].st.w == w) sw = &sr->w[i];
    /* World.getClosestPlayer walks this world's own players */
    if (sw == NULL || sr->player == NULL || sr->player->dimension != sw->dim) return;

    struct servertick *st = &sw->st;
    struct te_spawner *sp = &te->u.spawner;

    /* canRun: World.getClosestPlayer within activatingRangeFromPlayer */
    double dx = sr->player->e.pos_x - ((double)te->x + 0.5);
    double dy = sr->player->e.pos_y - ((double)te->y + 0.5);
    double dz = sr->player->e.pos_z - ((double)te->z + 0.5);
    double range = (double)sp->required_player_range;
    if (!(dx*dx + dy*dy + dz*dz < range * range)) return;

    if (sp->spawn_delay == -1) sr_tile_reset(sp, &ST_RAND(st));
    if (sp->spawn_delay > 0) { --sp->spawn_delay; return; }

    int kind = sr_tile_mob_kind(sp->mob_id);
    if (kind < 0) { set_err(sr, "unported tile spawner entity %s", sp->mob_id ? sp->mob_id : "(none)"); return; }

    /* the light and difficulty queries read the world as it stands now */
    int saved_sky = sr->d->spawner.skylight_subtracted, saved_thunder = sr->d->spawner.thundering;
    sr->d->spawner.skylight_subtracted = st->skylight_subtracted;
    sr->d->spawner.thundering = servertick_is_thundering(st);
    sr->d->spawner.difficulty = st->difficulty;
    sr->d->anw.difficulty = st->difficulty;
    sr->d->spawner.world_time = st->world_time;
    sr->d->spawner.total_time = st->total_time;

    int any = 0;
    for (int attempt = 0; attempt < sp->spawn_count; ++attempt)
    {
        det_state fake;
        sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
        det_state *saved_det = sr->d->anw.det;
        sr->d->anw.det = sr->d->anw.iew.det = &fake;
        struct living *l = sr_tile_construct(sr, kind, sw->dim, &fake);

        /* getEntitiesWithinAABB(entity.getClass(), ...): the class and its
         * subclasses (a zombie spawner counts pigmen, a spider one cave
         * spiders) */
        struct aabb nearby = aabb_make((double)te->x, (double)te->y, (double)te->z,
                                       (double)(te->x + 1), (double)(te->y + 1), (double)(te->z + 1));
        nearby = aabb_expand(nearby, (double)(sp->spawn_range * 2), 4.0, (double)(sp->spawn_range * 2));
        int count = an_entities_within_aabb(&sr->d->anw, kind, &nearby, NULL, 0);
        if (count >= sp->max_nearby_entities)
        {
            sr->d->anw.det = sr->d->anw.iew.det = saved_det;
            sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);
            living_drop_paths(l);
            living_release(l);
            sr_tile_reset(sp, &ST_RAND(st));
            any = 0;   /* resetTimer, then return: no second reset */
            break;
        }

        double x = (double)te->x + (jr_double(&ST_RAND(st)) - jr_double(&ST_RAND(st))) * (double)sp->spawn_range;
        double y = (double)(te->y + jr_int_n(&ST_RAND(st), 3) - 1);
        double z = (double)te->z + (jr_double(&ST_RAND(st)) - jr_double(&ST_RAND(st))) * (double)sp->spawn_range;
        float yaw = jr_float(&ST_RAND(st)) * 360.0F;
        living_set_location_and_angles(l, x, y, z, yaw, 0.0F);

        if (sr_tile_can_spawn(sr, sw, l))
        {
            /* func_98265_a: onSpawnWithEgg(null) (a spider jockey's rider
             * joins the world inside it), then spawnEntityInWorld */
            l->difficulty_factor = spawner_local_difficulty(&sr->d->spawner, x, y, z);
            sr->d->anw.iew.world_rand.r = ST_RAND(st);
            struct living *rider = living_on_spawn_with_egg_ret(l, &fake);
            ST_RAND(st) = sr->d->anw.iew.world_rand.r;
            /* loadedEntityList appends: the partner's higher id comes first,
             * so the post-snapshot id order does not apply to this pair */
            int by_id = sr->order_new_by_id;
            if (rider)
            {
                /* the spider's skeleton, the baby zombie's jockey chicken */
                sr->order_new_by_id = 0;
                egg_take_pending_partner();
                sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
                spawner_register_living(&sr->d->spawner, rider->kind == AK_CHICKEN ? SP_CHICKEN : SP_SKELETON,
                                        rider->entity_id, &rider->e);
            }
            an_add_living(&sr->d->anw, l, sr->d->anw.n);
            sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
            sr->order_new_by_id = by_id;
            static const int sp_kind[HK_KINDS] = {
                [HK_ZOMBIE] = SP_ZOMBIE, [HK_SKELETON] = SP_SKELETON, [HK_SPIDER] = SP_SPIDER,
                [HK_CREEPER] = SP_CREEPER, [HK_ENDERMAN] = SP_ENDERMAN, [HK_WITCH] = SP_WITCH,
                [AK_PIG] = SP_PIG, [AK_COW] = SP_COW, [AK_SHEEP] = SP_SHEEP, [AK_CHICKEN] = SP_CHICKEN,
                [HK_PIGMAN] = SP_PIG_ZOMBIE, [HK_BLAZE] = SP_BLAZE,
            };
            /* the natural spawner's cap and collision view; the kinds it has
             * no row for (cave spider, silverfish) still count as monsters */
            int spk = sp_kind[kind];
            if (spk == 0 && kind != HK_ZOMBIE) spk = SP_KINDS;
            spawner_register_living(&sr->d->spawner, spk, l->entity_id, &l->e);

            env_aux_sfx(w, 2004, te->x, te->y, te->z, 0);
            /* EntityLiving.spawnExplosionParticle
             * spends the entity's Random on the server too, though
             * spawnParticle there draws nothing. */
            for (int particle = 0; particle < 20; ++particle)
            {
                det_rng_gaussian(&l->rand);
                det_rng_gaussian(&l->rand);
                det_rng_gaussian(&l->rand);
                det_rng_float(&l->rand);
                det_rng_float(&l->rand);
                det_rng_float(&l->rand);
            }
            any = 1;
        }
        else
        {
            living_drop_paths(l);
            living_release(l);
        }
        sr->d->anw.det = sr->d->anw.iew.det = saved_det;
        sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);
    }
    if (any) sr_tile_reset(sp, &ST_RAND(st));

    sr->d->spawner.skylight_subtracted = saved_sky;
    sr->d->spawner.thundering = saved_thunder;
}

/* TileEntityChest's recount: the players inside the chest's box grown by 5
 * whose openContainer is a ContainerChest over this chest (alone or as half
 * of the pair). */
/* World.getClosestPlayer(x, y, z, dist) over the one server player in w:
 * getDistanceSq from its feet, strictly under dist squared. */
static int sr_closest_player(void *ctx, struct world *w, double x, double y, double z, double dist, double *px,
                             double *pz)
{
    struct serverreplay *sr = ctx;
    const struct server_player *p = sr->player;
    if (p == NULL || p->e.world != w) return 0;
    double dx = p->e.pos_x - x, dy = p->e.pos_y - y, dz = p->e.pos_z - z;
    if (!(dist < 0.0 || dx * dx + dy * dy + dz * dz < dist * dist)) return 0;
    *px = p->e.pos_x;
    *pz = p->e.pos_z;
    return 1;
}

static int sr_chest_viewers(void *ctx, struct world *w, struct tile_entity *te)
{
    struct serverreplay *sr = ctx;
    const struct server_player *p = sr->player;
    (void)w;
    if (p == NULL || p->sv.dead) return 0;
    struct aabb box = aabb_make((double)((float)te->x - 5.0F), (double)((float)te->y - 5.0F),
                                (double)((float)te->z - 5.0F), (double)((float)(te->x + 1) + 5.0F),
                                (double)((float)(te->y + 1) + 5.0F), (double)((float)(te->z + 1) + 5.0F));
    if (!aabb_intersects(&box, &p->e.bounding_box)) return 0;
    const struct container *c = p->open_container;
    if (c == NULL || c->kind != CONTAINER_CHEST || p->gui_y != te->y) return 0;
    if (p->gui_x == te->x && p->gui_z == te->z) return 1;
    return p->gui_has_pair && p->gui_pair_x == te->x && p->gui_pair_z == te->z;
}

/* The wooden button's World.getEntitiesWithinAABB(EntityArrow.class, box).
 * The arrows live in the mob world's projectile pool (pool 2, not living). */
static int sr_arrows_in(void *ctx, const struct aabb *box)
{
    struct serverreplay *sr = ctx;
    for (int i = 0; i < sr->d->nents; ++i)
    {
        const ie_ent *ie;
        if (sr->d->ents[i].pool == 0) ie = sr_ent_p(sr, &sr->d->ents[i]);
        else if (sr->d->ents[i].pool == 2)
        {
            const struct an_ent *en = sr_ent_p(sr, &sr->d->ents[i]);
            if (en->is_living) continue;
            ie = ie_get(en->ieh);
        }
        else continue;
        if (ie->kind == IE_ARROW && !ie->is_dead && aabb_intersects(box, &ie->e.bounding_box)) return 1;
    }
    return 0;
}

/* BlockButton.onEntityCollidedWithBlock for an arrow in flight: the wooden
 * button runs func_150046_n only while up. */
static void sr_arrow_button(struct ie_world *iew, struct ie_ent *arrow, int x, int y, int z)
{
    struct serverreplay *sr = ((struct sr_dimstate *)((char *)iew - offsetof(struct sr_dimstate, anw.iew)))->sr;
    struct world *w = arrow->e.world;

    if ((world_get_meta(w, x, y, z) & 8) == 0) servertick_wood_button(w, x, y, z, sr_arrows_in, sr);
}

/* ------------------------------------------------- the pressure plates ---- */

/* BlockBasePressurePlate.func_150062_a's count: func_150065_e over the plate's
 * AABB (func_150061_a: 0.125 inset on x/z, y to y+0.25). The pass order's
 * pools are Java loadedEntityList's: everything for the wooden plate's
 * getEntitiesWithinAABBExcludingEntity(null, box), and EntityLivingBase only
 * (the two living pools) for the stone plate's getEntitiesWithinAABB. The
 * doesEntityNotTriggerPressurePlate override (EntityBat's true) skips the bat
 * in both. */
static int sr_box_count(struct serverreplay *sr, struct aabb box, int id);

static int sr_box_count_all(struct serverreplay *sr, struct aabb box);

static int sr_plate_count(struct serverreplay *sr, int x, int y, int z, int id)
{
    struct aabb box = aabb_make((double)x + 0.125, (double)y, (double)z + 0.125,
                                (double)x + 0.875, (double)y + 0.25, (double)z + 0.875);

    if (id == 147 || id == 148)
    {
        /* BlockPressurePlateWeighted.func_150065_e: getEntitiesWithinAABB(
         * Entity.class, box).size() against field_150068_a (15 gold, 150
         * iron), no doesEntityNotTriggerPressurePlate test, then
         * ceiling_float_int(min(max, n) / (float)max * 15.0F) */
        int max = id == 147 ? 15 : 150;
        int n = sr_box_count_all(sr, box);

        if (n > max) n = max;
        if (n <= 0) return 0;

        float v = (float)n / (float)max * 15.0F;
        int up = (int)v;

        return v > (float)up ? up + 1 : up;
    }

    return sr_box_count(sr, box, id);
}

/* The entity test the plates' func_150065_e and the tripwire's
 * func_150140_e make over getEntitiesWithinAABBExcludingEntity(null, box):
 * 15 when one entity that does not answer doesEntityNotTriggerPressurePlate
 * (the bat) is inside. The stone plate (70) counts livings only; the wooden
 * plate and the tripwire (72, 132) count every entity. Like the weighted
 * count (sr_box_count_all) it reads the chunk lists, dead entities and all:
 * an item picked up earlier in the tick still presses the plate. */
static int sr_box_count(struct serverreplay *sr, struct aabb box, int id)
{
    int everything = id == 72 || id == 132;

    for (int i = 0; i < sr->d->nents; ++i)
    {
        const struct aabb *bb = NULL;
        int not_trigger = 0;

        switch (sr->d->ents[i].pool)
        {
        case 0:
        {
            const ie_ent *ie = sr_ent_p(sr, &sr->d->ents[i]);
            if (!everything) continue;   /* everything: the items and orbs */
            bb = &ie->e.bounding_box;
            break;
        }
        case 1:
        {
            const fh_ent *fh = sr_ent_p(sr, &sr->d->ents[i]);
            if (!everything) continue;   /* everything: the falling blocks */
            bb = &fh->e.bounding_box;
            break;
        }
        case 2:
        {
            const struct an_ent *en = sr_ent_p(sr, &sr->d->ents[i]);
            if (en->is_living)
            {
                if (lv_get(en->livh)->kind == AK_BAT) not_trigger = 1;
                bb = &lv_get(en->livh)->e.bounding_box;
            }
            else
            {
                if (!everything) continue;
                bb = &ie_get(en->ieh)->e.bounding_box;
            }
            break;
        }
        case 3:
            /* a dead player left its chunk's lists with loadedEntityList */
            if (sr->player == NULL || sr->player->sv.removed) continue;
            bb = &sr->player->e.bounding_box;
            break;
        }

        if (bb == NULL || not_trigger) continue;
        if (aabb_intersects(bb, &box)) return 15;
    }

    return 0;
}

/* World.getEntitiesWithinAABB(Entity.class, box).size(): every entity the
 * entity pass holds whose box meets BOX (the weighted plates' count). The
 * chunk lists it reads keep a dead entity until updateEntities removes it at
 * its turn, so an item the player picked up this tick still counts. The End's
 * dragon and crystals (pools 4, 5) never meet a plate here. */
static int sr_box_count_all(struct serverreplay *sr, struct aabb box)
{
    int n = 0;

    for (int i = 0; i < sr->d->nents; ++i)
    {
        const struct aabb *bb = NULL;

        switch (sr->d->ents[i].pool)
        {
        case 0:
        {
            const ie_ent *ie = sr_ent_p(sr, &sr->d->ents[i]);
            bb = &ie->e.bounding_box;
            break;
        }
        case 1:
        {
            const fh_ent *fh = sr_ent_p(sr, &sr->d->ents[i]);
            bb = &fh->e.bounding_box;
            break;
        }
        case 2:
        {
            const struct an_ent *en = sr_ent_p(sr, &sr->d->ents[i]);
            bb = en->is_living ? &lv_get(en->livh)->e.bounding_box : &ie_get(en->ieh)->e.bounding_box;
            break;
        }
        case 3:
            if (sr->player != NULL) bb = &sr->player->e.bounding_box;
            break;
        }

        if (bb != NULL && aabb_intersects(bb, &box)) ++n;
    }

    return n;
}

/* BlockBasePressurePlate.func_150062_a: var5 is the metadata's state,
 * func_150060_c(meta) (15 down, 0 up). The meta write is
 * setBlockMetadataWithNotify(.., func_150066_d(var6), 2): flag 2 only, so no
 * notifyBlockChange; the notifies are func_150064_a_'s two
 * notifyBlocksOfNeighborChange calls (the plate's cell and the one below).
 * The click sounds are playSoundEffect, empty on the server with constant
 * arguments, so no draws. While occupied the tick re-arms at
 * func_149738_a's 20. */
static void sr_plate_func_150062_a(struct serverreplay *sr, struct world *w,
                                   int x, int y, int z, int id, int was)
{
    int weighted = id == 147 || id == 148;
    int count = sr_plate_count(sr, x, y, z, id);
    int var7 = was > 0;
    int var8 = count > 0;

    if (was != count)
    {
        /* func_150066_d: the weighted plates store the power itself */
        world_set_meta(w, x, y, z, weighted ? count : count > 0 ? 1 : 0, 2);
        world_notify_neighbors(w, x, y, z, id);
        world_notify_neighbors(w, x, y - 1, z, id);
        /* markBlockRangeForRenderUpdate: no state */
    }

    if (!var8 && var7) { /* random.click 0.3F 0.5F: empty */ }
    else if (var8 && !var7) { /* random.click 0.3F 0.6F: empty */ }

    /* func_149738_a: 20, the weighted plates' override 10 */
    if (var8) ticks_schedule_block_update(w, x, y, z, id, weighted ? 10 : 20);
}

/* BlockTripWire.func_150140_e: the wire's box (the bounds its own
 * setBlockBoundsBasedOnState gives this state: vanilla reads the singleton's
 * last bounds, which any ray or render over any wire sets, and the oracle's
 * tripwire pin, SPEC.md, sets them from the wire first) against every
 * entity, the tripped bit, the hooks through func_150138_a, and the 10-tick
 * re-check while occupied. */
static void sr_tripwire_check(struct serverreplay *sr, struct world *w, int x, int y, int z)
{
    int meta = world_get_meta(w, x, y, z);
    int was = (meta & 1) == 1;
    float y0 = 0.0F, y1 = 0.09375F;

    if ((meta & 2) == 2)
    {
        if ((meta & 4) != 4) y1 = 0.5F;
        else { y0 = 0.0625F; y1 = 0.15625F; }
    }

    struct aabb box = aabb_make((double)x + (double)0.0F, (double)y + (double)y0, (double)z + (double)0.0F,
                                (double)x + (double)1.0F, (double)y + (double)y1, (double)z + (double)1.0F);
    int now = sr_box_count(sr, box, 132) > 0;

    if (now && !was) meta |= 1;
    if (!now && was) meta &= -2;

    if (now != was)
    {
        world_set_meta(w, x, y, z, meta, 3);
        blockcb_tripwire_hooks(w, x, y, z, meta);
    }

    if (now) ticks_schedule_block_update(w, x, y, z, 132, 10);
}

/* BlockPressurePlate.onEntityCollidedWithBlock: only acts while up. */
static void sr_plate_collide(void *ctx, int x, int y, int z, int id)
{
    struct serverreplay *sr = ctx;
    struct world *w = sr_dim_ws(sr, sr->here)->st.w;
    int meta = world_get_meta(w, x, y, z);

    /* BlockTripWire.onEntityCollidedWithBlock: only while not tripped */
    if (id == 132)
    {
        if ((meta & 1) != 1) sr_tripwire_check(sr, w, x, y, z);
        return;
    }

    /* func_150060_c(meta) > 0: down (the weighted plates' state is the
     * metadata itself) */
    if (id == 147 || id == 148 ? meta > 0 : meta == 1) return;
    sr_plate_func_150062_a(sr, w, x, y, z, id, 0);
}

/* BlockBasePressurePlate.updateTick: only re-runs the state machine while
 * down (the metadata's state, not the raw metadata). */
static void sr_plate_tick(void *ctx, int x, int y, int z, int id)
{
    struct serverreplay *sr = ctx;
    struct world *w = sr_dim_ws(sr, sr->here)->st.w;
    int meta = world_get_meta(w, x, y, z);
    int was = id == 147 || id == 148 ? meta : meta == 1 ? 15 : 0;

    /* BlockTripWire.updateTick: re-checks while tripped */
    if (id == 132)
    {
        if ((meta & 1) == 1) sr_tripwire_check(sr, w, x, y, z);
        return;
    }

    if (was > 0) sr_plate_func_150062_a(sr, w, x, y, z, id, was);
}

/* an_world.constructor_hook: EntityAIMate.spawnBaby's child constructor on
 * the server's streams. */
static void sr_living_constructor(struct an_world *an, int begin, void *ctx)
{
    struct serverreplay *sr = ctx;
    det_state *fake = &sr->ctor_fake;
    if (begin)
    {
        sr_constructor_begin(fake, &SR_DET(sr), DET_SERVER);
        an->det = fake;
        an->iew.det = fake;
    }
    else
    {
        sr_constructor_end(&SR_DET(sr), fake, DET_SERVER);
        an->det = &SR_DET(sr);
        an->iew.det = &SR_DET(sr);
    }
}

/* EntityZombie.onKillEntity's infection, inside the overworld's entity pass:
 * new EntityZombie, copyLocationAndAnglesFrom the villager, removeEntity(the
 * villager), onSpawnWithEgg(null), setVillager(true), a child's setChild,
 * spawnEntityInWorld. The constructor and egg draws are the summon's record
 * path on the live World.rand; the zombie joins the pass's order after the
 * killer's tick (sr_tick_an), so the record path's own order push is taken
 * back here. */
static void sr_on_infect(struct an_world *an, struct living *zombie, struct living *victim, void *ctx)
{
    struct serverreplay *sr = ctx;
    (void)zombie;
    /* the pass's own world (a villager killed in the End infects there) */
    if (an != &sr->d->anw) return;
    struct sr_world *ws = sr_dim_ws(sr, an->dimension);
    living_set_dead(victim);
    ST_RAND(&ws->st) = an->iew.world_rand.r;
    int before = an->n;
    int ok = serverreplay_summon_hostile(sr, HK_ZOMBIE, victim->e.pos_x, victim->e.pos_y, victim->e.pos_z, 0, 0);
    an->iew.world_rand.r = ST_RAND(&ws->st);
    if (!ok) return;
    for (int i = before; i < an->n; ++i)
    {
        for (int k = sr->d->nents - 1; k >= 0; --k)
            if (sr->d->ents[k].pool == 2 && sr_ent_p(sr, &sr->d->ents[k]) == an_ent_at(an->slot[i]))
            {
                sr_order_remove(sr, k);
                break;
            }
        if (!an_ent_at(an->slot[i])->is_living || lv_get(an_ent_at(an->slot[i])->livh)->kind != HK_ZOMBIE) continue;
        struct living *z = lv_get(an_ent_at(an->slot[i])->livh);
        z->rotation_yaw = z->prev_rotation_yaw = victim->rotation_yaw;
        z->rotation_pitch = z->prev_rotation_pitch = victim->rotation_pitch;
        z->zombie_is_villager = 1;
        /* new EntityZombie(worldObj): Entity.dimension is the world's (the
         * summon's tag read leaves 0) */
        z->dimension = an->dimension;
        /* new EntityZombie's data watcher air, 300 (the summon's
         * readFromNBT left the absent Air's 0: pfc-infectair-g6) */
        z->air = 300;
        if (victim->growing_age < 0) zombie_set_child(z, 1);
        combat_track_spawn(sr, z);
    }
}

/* EntityZombie.attackEntityFrom's HARD reinforcement, the roll passed: the
 * spawner's record path makes the new zombie's constructor draws (on the
 * server's streams, spent whether or not it spawns), the caller's 50 tries
 * and onSpawnWithEgg on the live World.rand; the zombie is rebuilt from the
 * record like a summon (spawnEntityInWorld, then setAttackTarget), and each
 * side gets its -0.05 charge on spawnReinforcements, caller first, their
 * UUIDs the modifiers' Det.uuid(). */
static void sr_on_reinforce(struct an_world *an, struct living *caller, struct living *target, det_state *det, void *ctx)
{
    struct serverreplay *sr = ctx;
    struct sr_world *ws = sr_dim_ws(sr, an->dimension);
    struct spawner *sp = &sr->d->spawner;
    jrand *live = nw_env->blockcb.env.world_rand != NULL ? nw_env->blockcb.env.world_rand : &an->iew.world_rand.r;
    jrand *prev = sp->rand;
    sp->rand = live;
    sp->world_time = ws->st.world_time;
    sp->total_time = ws->st.total_time;
    sp->difficulty = ws->st.difficulty;
    /* getBlockLightValue's sky part (the spawner's own tick may not run) */
    sp->skylight_subtracted = ws->st.skylight_subtracted;
    struct sp_rec r;
    int ok = spawner_reinforce(sp, &caller->rand, (int)floor(caller->e.pos_x), (int)floor(caller->e.pos_y),
                               (int)floor(caller->e.pos_z), &r);
    sp->rand = prev;
    if (!ok) return;

    int before = an->n;
    sr_adopt_mob(sr, &r);
    struct living *z = NULL;
    for (int i = before; i < an->n; ++i)
    {
        if (!an_ent_at(an->slot[i])->is_living) continue;
        struct living *l = lv_get(an_ent_at(an->slot[i])->livh);
        spawner_register_living(sp, l->entity_id == r.id ? SP_ZOMBIE : SP_KINDS, l->entity_id, &l->e);
        combat_track_spawn(sr, l);
        if (l->entity_id == r.id) z = l;
    }
    sr->native_entities = sr_count_entities(sr);
    if (z == NULL) return;
    z->attack_target = lv_ref(target);

    struct attr_mod m;
    memset(&m, 0, sizeof m);
    m.name = MODN_ZOMBIE_CALLER_CHARGE;
    m.amount = -0.05000000074505806;
    m.operation = 0;
    m.saved = 1;
    det_uuid_role(det != NULL ? det : &SR_DET(sr), DET_SERVER, &m.uuid_msb, &m.uuid_lsb);
    attrs_apply(&caller->attrs.a[ATTR_SPAWN_REINFORCEMENTS], &m);
    m.name = MODN_ZOMBIE_CALLEE_CHARGE;
    det_uuid_role(det != NULL ? det : &SR_DET(sr), DET_SERVER, &m.uuid_msb, &m.uuid_lsb);
    attrs_apply(&z->attrs.a[ATTR_SPAWN_REINFORCEMENTS], &m);
}

/* EntityZombie.convertToVillager, inside the zombie's own onUpdate: new
 * EntityVillager on the server's streams, copyLocationAndAnglesFrom the
 * zombie, onSpawnWithEgg (the follow-range roll, the profession off
 * World.rand), setLookingForHome, a child's growing age, removeEntity(the
 * zombie), spawnEntityInWorld, then the 200-tick nausea. */
static void sr_on_convert(struct an_world *an, struct living *zombie, void *ctx)
{
    struct serverreplay *sr = ctx;
    sr_living_constructor(an, 1, sr);
    struct living *v = vil_spawn_living(an, VK_VILLAGER, an->n, zombie->e.pos_x, zombie->e.pos_y, zombie->e.pos_z,
                                        zombie->rotation_yaw, zombie->rotation_pitch, 0, 0, 0, 0, 1);
    sr_living_constructor(an, 0, sr);
    if (v == NULL) return;
    v->is_looking_for_home = 1;
    if (zombie->zombie_is_child) living_set_growing_age(v, -24000);
    living_set_dead(zombie);
    combat_track_spawn(sr, v);
    spawner_register_living(&sr->d->spawner, SP_OTHER, v->entity_id, &v->e);
    struct potion_effect eff = {.id = POT_CONFUSION, .amplifier = 0, .duration = 200};
    living_add_potion_effect(v, &eff, an->det);
}

static void sr_egg_chicken(struct ie_world *iew, struct ie_ent *egg)
{
    struct serverreplay *sr = iew->user_data;
    int role = iew->role;
    det_state fake;
    if (role != DET_OTHER)
    {
        sr_constructor_begin(&fake, &SR_DET(sr), role);
        sr->d->anw.det = &fake;
        sr->d->anw.iew.det = &fake;
    }
    an_egg_chicken(&sr->d->anw, egg);
    if (role != DET_OTHER)
    {
        sr_constructor_end(&SR_DET(sr), &fake, role);
        sr->d->anw.det = &SR_DET(sr);
        sr->d->anw.iew.det = &SR_DET(sr);
    }
    struct an_ent *en = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
    sr_order_push(sr, 2, en);
    if (sr->mobs_enabled) spawner_track(&sr->d->spawner, lv_get(en->livh)->entity_id, &lv_get(en->livh)->e);
    /* spawnEntityInWorld's addEntityToTracker: the S0F from where it hatched */
    combat_track_spawn(sr, lv_get(en->livh));
}

/* A player's egg lives in the living world's pool: its hatchling is built on
 * the server's streams like sr_egg_chicken's, and the entity pass that ticks
 * the egg appends the new list entry to the order itself. */
static void sr_anw_egg_chicken(struct ie_world *iew, struct ie_ent *egg)
{
    struct serverreplay *sr = ((struct sr_dimstate *)((char *)iew - offsetof(struct sr_dimstate, anw.iew)))->sr;
    int role = iew->role;
    det_state fake;
    sr_constructor_begin(&fake, &SR_DET(sr), role);
    sr->d->anw.det = &fake;
    sr->d->anw.iew.det = &fake;
    an_egg_chicken(&sr->d->anw, egg);
    sr_constructor_end(&SR_DET(sr), &fake, role);
    sr->d->anw.det = &SR_DET(sr);
    sr->d->anw.iew.det = &SR_DET(sr);
    struct an_ent *en = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
    spawner_track(&sr->d->spawner, lv_get(en->livh)->entity_id, &lv_get(en->livh)->e);
    combat_track_spawn(sr, lv_get(en->livh));
}

/* BlockTNT.onEntityCollidedWithBlock for a burning arrow: func_150114_a(x,
 * y, z, 1, shooter) builds the EntityTNTPrimed at the block's centre (its
 * constructor's Math.random heading and seeder draws are ie_spawn_tnt's) and
 * spawns it into the pass, then setBlockToAir. The placer is the arrow's
 * shooter when it is a living; only the player's is modelled as an attacker. */
static void sr_arrow_tnt(struct ie_world *iew, struct ie_ent *arrow, int x, int y, int z)
{
    struct serverreplay *sr = ((struct sr_dimstate *)((char *)iew - offsetof(struct sr_dimstate, anw.iew)))->sr;
    struct world *w = arrow->e.world;
    int by_player = arrow->shooter_is_player && arrow->shooter != 0;
    ie_ent *tnt = ie_spawn_tnt(&sr->d->iew, (double)((float)x + 0.5F), (double)((float)y + 0.5F),
                               (double)((float)z + 0.5F), by_player);

    /* sr_tick_an adds the new item-pool entry to the pass's order */
    if (tnt != NULL) ie_added_to_world(&sr->d->iew, tnt);

    world_set_block(w, x, y, z, 0, 0, 3);
}

/* A support break during World.updateEntities still uses World.rand and adds
 * its item to the live entity walk, so the new item can tick this same row. */
static void sr_entity_drop(void *ctx, double x, double y, double z, int item, int damage, int count)
{
    struct serverreplay *sr = ctx;
    ie_ent *en = ie_spawn_item(&sr->d->iew, x, y, z, item, damage, count);
    if (!en) return;
    en->delay = 10;
    ie_added_to_world(&sr->d->iew, en);
    sr_order_push(sr, 0, en);
}

/* A population's own tick drops (a spring's flow washing a mineshaft rail
 * away, through randomtick's table): the item joins the world at once,
 * ordered now unless a network phase's unhook orders it (a transfer's
 * placement is past that range). */
static void sr_pop_entity_drop(void *ctx, double x, double y, double z, int item, int damage, int count)
{
    struct serverreplay *sr = ctx;
    ie_ent *en = ie_spawn_item(&sr->d->iew, x, y, z, item, damage, count);
    if (!en) return;
    en->delay = 10;
    ie_added_to_world(&sr->d->iew, en);
    if (sr->action_world == NULL || sr->in_transfer) sr_order_push(sr, 0, en);
}

static void sr_block_env_drop(void *ctx, int id, uint64_t rand_state, int64_t msb, int64_t lsb,
                              double x, double y, double z, float yaw, float hover,
                              double mx, double mz, int item, int damage, int count)
{
    struct serverreplay *sr = ctx;
    ie_ent *en = ie_adopt_item(&sr->d->iew, id, msb, lsb, rand_state, x, y, z,
                               mx, 0.20000000298023224, mz, yaw, hover, item, damage, count, 0);
    if (!en) return;
    en->delay = 10;
    ie_added_to_world(&sr->d->iew, en);
    /* inside the network phase (a dig's door pop) the unhook orders every
     * item the phase appended; a transfer's placement is past its range */
    if (sr->action_world == NULL || sr->in_transfer) sr_order_push(sr, 0, en);
}

static void sr_action_block(void *ctx, int x, int y, int z, int id, int meta);

/* A dev op's block writes (Dev.apply's setBlock and fill, at the head of the
 * tick): a plant a write leaves unsupported drops through World.rand into
 * the server's item world, as a tick's own writes do. The caller restores
 * the tick stream with randomtick_tick_load. */
void serverreplay_set_difficulty(struct serverreplay *sr, int v)
{
    surv.difficulty = v;
    for (int i = 0; i < sr->nworlds; ++i) sr->w[i].st.difficulty = v;
    for (int i = 0; i < (int)(sizeof sr->dims / sizeof sr->dims[0]); ++i)
        spawner_set_spawn_types(&sr->dims[i].spawner, v != 0, 1);
}

void serverreplay_set_view_distance(struct serverreplay *sr, int t, int view, double px, double pz)
{
    int pdim = serverreplay_player_dim(sr);

    sr->view = view;
    for (int i = 0; i < sr->nworlds; ++i)
    {
        sr->w[i].view = view;
        /* a manager without the player only takes the radius */
        if (sr->w[i].dim == pdim) continue;
        for (int k = 0; k < 3; ++k)
            if (sr->dims[k].cl != NULL && sr->dims[k].dim == sr->w[i].dim) cl_set_radius(sr->dims[k].cl, view);
    }
    /* the chunks a larger radius loads (PlayerInstance's loadChunk) generate
     * and populate after the tick: their writes are the row's (Rows.onBlock)
     * and reach the client with the next flush, as the network tick's do */
    struct cl *pm = serverreplay_player_manager(sr);
    struct world *pw = sr_dim_world(sr, pdim);
    void (*prev_hook)(void *, int, int, int, int, int) = pw->on_block;
    void *prev_ctx = pw->on_block_ctx;

    pw->on_block = sr_action_block;
    pw->on_block_ctx = sr;
    cl_view_radius_at(pm, t, view, px, pz);
    pw->on_block = prev_hook;
    pw->on_block_ctx = prev_ctx;
    /* and what population spawns is in the row's count */
    sr->native_entities = sr_count_entities(sr);
}

void serverreplay_dev_block_env(struct serverreplay *sr, jrand *wrand, struct randomtick_env *saved)
{
    randomtick_tick_save(saved);
    randomtick_tick_rand(wrand);
    randomtick_tick_det(&SR_DET(sr), DET_SERVER);
    randomtick_tick_drop_sink(sr_entity_drop, sr);
}

/* The same env's container spill: a write that replaces a chest (a
 * population step carving through a dungeon's) runs BlockChest.breakBlock,
 * whose items take the spill's own motions and keep pickup delay 0. */
static void sr_block_env_spill(void *ctx, const struct spill_item *s)
{
    struct serverreplay *sr = ctx;
    ie_ent *en = ie_adopt_item(&sr->d->iew, s->entity_id, s->uuid_msb, s->uuid_lsb, s->rand_state, s->x, s->y, s->z,
                               s->mx, s->my, s->mz, s->yaw, s->hover, s->item, s->damage, s->count, s->tag);
    if (!en) return;
    en->delay = 0;
    ie_added_to_world(&sr->d->iew, en);
    if (sr->action_world == NULL || sr->in_transfer) sr_order_push(sr, 0, en);
}

/* The network tick's player packets can write blocks after servertick_tick has
 * finished. Rows.onBlock still counts these writes in the same row. */
static void sr_action_block(void *ctx, int x, int y, int z, int id, int meta)
{
    struct serverreplay *sr = ctx;
    sr_on_block(sr, x, y, z, id, meta);
    uint64_t h = sr->blk_hash;
    h = (h ^ (uint64_t)(int64_t)(sr->player ? sr->player->dimension : 0)) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)x) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)y) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)z) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)(int16_t)id) * FNV_PRIME;
    h = (h ^ (uint64_t)(int64_t)meta) * FNV_PRIME;
    sr->blk_hash = h;
    ++sr->blk_count;
}

int serverreplay_action_begin(struct serverreplay *sr)
{
    serverreplay_enter(sr, serverreplay_player_dim(sr));
    sr->action_prev_env = nw_env->blockcb.env;
    sr->action_moved = 0;
    sr->action_first_item = sr->d->iew.n;
    sr_action_hooks(sr);
    sr->action_an_n = sr->d->anw.n;
    return sr->d->iew.n;
}

void serverreplay_action_end(struct serverreplay *sr, int first_item)
{
    if (!sr->action_moved) sr->action_first_item = first_item;
    sr_action_unhook(sr);
    sr->action_world = NULL;
    sr->action_moved = 0;
    randomtick_tick_rand(NULL);
    nw_env->blockcb.env = sr->action_prev_env;
    sr->native_entities = sr_count_entities(sr);
}

struct cl *serverreplay_player_manager(struct serverreplay *sr)
{
    serverreplay_enter(sr, serverreplay_player_dim(sr));
    return sr->d->cl;
}

/* PlayerManager's block packets reach the client at its next tick. The packet
 * carries the final block and metadata at each written cell, including a
 * metadata-only write; the client does not run server block callbacks. Only
 * the writes of the client world's own dimension reach it. */
static void sr_flush_release(struct serverreplay *sr);

static int y_of_cell(uint16_t cell)
{
    return cell & 255;
}

/* the client cell's tile entity (clientworld.h's client_te_cell), before
 * the sync writes the cell */
static void sr_client_te_cell(struct serverreplay *sr, struct world *client, int x, int y, int z, int id, int meta)
{
    if (sr->pickobj == NULL) return;
    client_te_cell(&sr->pickobj->ctes, &SR_DET(sr), x, y, z, world_get_block(client, x, y, z),
                   world_get_meta(client, x, y, z), id, meta);
}

void serverreplay_sync_client(struct serverreplay *sr, struct world *client)
{
    int dim = client->dim;
    const struct servertick *st = &sr_dim_ws(sr, dim)->st;
    struct world *sw = sr_dim_world(sr, dim);

    serverreplay_enter(sr, dim);

    /* WorldServer.tick's updatePlayerInstances flushes what changed since
     * the last flush: the world tick's own writes of this tick, and the
     * entity pass's and the network tick's of the tick before (they came
     * after that tick's flush). The cells go out as they stand now. */
    int ndev = dim == sr->dev_writes_dim ? sr->ndev_writes : 0;
    int total = st->nwrites + sr->d->nlate + ndev;

    (void)total;
    /* the flush's packets (sr_flush_capture): an S23 or S22 cell by cell
     * (WorldClient.func_147492_c: the write with the client's relight), an
     * S21 as fillChunk (the held sections' cells and light in place, no
     * relight; then generateHeightMap, and resetRelightChecks: not ground-up) */
    for (int k = 0; sr->flush_ready && k < sr->nflush; ++k)
    {
        struct sr_flush_chunk *f = &sr->flush[k];
        struct chunk *c = world_chunk(client, f->cx, f->cz);
        if (c == NULL) continue;
        if (f->n < 64)
        {
            for (int j = 0; j < f->n; ++j)
            {
                int x = f->cx * 16 + (f->cells[j] >> 12 & 15), z = f->cz * 16 + (f->cells[j] >> 8 & 15);
                /* an S23/S22 cell's setBlock makes the client's tile entity
                 * (clientworld.h's client_te_cell); a whole-chunk S21's
                 * fillChunk makes none */
                sr_client_te_cell(sr, client, x, y_of_cell(f->cells[j]), z, f->ids[j], f->metas[j]);
                world_client_set_block(client, x, y_of_cell(f->cells[j]), z, f->ids[j], f->metas[j]);
            }
            continue;
        }
        /* the client's pending light calls run before fillChunk's cells land
         * (lightdefer.h) */
        world_light_sync(client);
        for (int sec = 0; sec < 16; ++sec)
            if (f->bands[sec] != NULL)
            {
                chunk_sec_install(c, sec, f->bands[sec]);
                f->bands[sec] = NULL;
                c->mask |= (uint16_t)(1 << sec);   /* the storage fillChunk makes */
            }
        chunk_generate_height_map(c);
        c->queued_light_checks = 0;
        world_note_write(client, c);
        /* then sendChunkUpdate's S35 for each tile entity of the flagged
         * sections (func_147486_a): a spawner's getTileEntity makes its
         * client entity */
        const struct chunk *src = world_chunk(sw, f->cx, f->cz);
        for (int k2 = 0; sr->pickobj != NULL && src != NULL && k2 < src->tes.n; ++k2)
        {
            const struct tile_entity *te = src->tes.v[k2];
            if (te != NULL && te->kind == TE_MOB_SPAWNER && !te->invalid && te->y >= 0 && te->y < 256 &&
                (f->secs & 1 << (te->y >> 4)))
                (void)client_te_get(&sr->pickobj->ctes, &SR_DET(sr), te->x, te->y, te->z, 52);
        }
    }
    sr_flush_release(sr);
    /* the world tick's writes after the flush go with the next one */
    int nsplit = sr->flush_split < st->nwrites ? st->nwrites - sr->flush_split : 0;

    /* this tick's entity-pass and network writes wait for the next flush;
     * an explosion's go now, with its S27 */
    sr->d->nlate = 0;
    if (nsplit + sr->d->nextra > sr->d->caplate)
    {
        fprintf(stderr, "serverreplay: more than %d writes wait for one flush\n", sr->d->caplate);
        abort();
    }
    for (int i = 0; i < nsplit; ++i)
        sr->d->late[sr->d->nlate++] = st->writes[sr->flush_split + i];
    for (int i = 0; i < sr->d->nextra; ++i)
    {
        const struct st_write *wr = &sr->d->extra[i];
        if (wr->pad & SR_WRITE_S27)
        {
            int x = wr->x, y = wr->y, z = wr->z;
            struct chunk *c = y >= 0 && y < 256 ? world_chunk(client, x >> 4, z >> 4) : NULL;
            if (c && world_chunk(sw, x >> 4, z >> 4))
            {
                int id = world_get_block(sw, x, y, z);
                int meta = world_get_meta(sw, x, y, z);
                /* the client's doExplosionB(true) only clears the blast's
                 * cells: a flaming blast's fire reaches it with the next
                 * flush */
                if ((id & 4095) == 51) id = meta = 0;
                /* WorldClient.func_147492_c: the write with the client's relight */
                sr_client_te_cell(sr, client, x, y, z, id, meta);
                world_client_set_block(client, x, y, z, id, meta);
            }
        }
        sr->d->late[sr->d->nlate++] = *wr;
    }

    /* the S23s a packet handler sent on its own (processPlayerBlockPlacement) */
    for (int i = 0; i < sr->nresend; ++i)
    {
        int x = sr->resend[i][0], y = sr->resend[i][1], z = sr->resend[i][2];
        if (sr->resend[i][3] != dim || y < 0 || y >= 256) continue;
        struct chunk *c = world_chunk(client, x >> 4, z >> 4);
        if (!c || !world_chunk(sw, x >> 4, z >> 4)) continue;

        /* the block as the packet was made, after the handler's work
         * (whose writes the next flush sends again, as they then stand) */
        int id = sr->resend[i][4];
        int meta = sr->resend[i][5];
        /* WorldClient.func_147492_c: the write with the client's relight */
        sr_client_te_cell(sr, client, x, y, z, id, meta);
        world_client_set_block(client, x, y, z, id, meta);
    }
    sr->nresend = 0;

    /* this dimension's writes made inside another world's pass (a portal
     * traveller's placement) */
    for (int i = 0; i < sr->nxw; ++i)
    {
        if (sr->xw[i].dim != dim) continue;
        int x = sr->xw[i].w.x, y = sr->xw[i].w.y, z = sr->xw[i].w.z;
        if (y < 0 || y >= 256) continue;
        struct chunk *c = world_chunk(client, x >> 4, z >> 4);
        if (!c || !world_chunk(sw, x >> 4, z >> 4)) continue;

        int id = world_get_block(sw, x, y, z);
        int meta = world_get_meta(sw, x, y, z);
        /* WorldClient.func_147492_c: the write with the client's relight */
        sr_client_te_cell(sr, client, x, y, z, id, meta);
        world_client_set_block(client, x, y, z, id, meta);
    }

    serverreplay_enter(sr, serverreplay_player_dim(sr));
}

/* The client world a resumed stream starts from (sr_resume_stream): every
 * chunk the client's ChunkProviderClient listed at the snapshot
 * (clientworld.nbt's loaded, chunkListing order), as the server holds it. A
 * chunk only the client still has (the server unloaded it since) is left
 * out. Returns how many were copied, -1 for a snapshot without the record. */
void serverreplay_client_chunk_fresh(struct chunk *c, int dim)
{
    /* WorldClient.doPreChunk's new Chunk, then fillChunk: no column waits
     * for its gaps (updateSkylightColumns, isGapLightingUpdated), and the
     * relight checks done (queuedLightChecks 4096) until handleChunkData's
     * resetRelightChecks, which a ground-up chunk takes outside the
     * overworld */
    memset(c->update_skylight_columns, 0, sizeof c->update_skylight_columns);
    c->gap_lighting_updated = 0;
    c->queued_light_checks = dim == 0 ? 4096 : 0;
}

int serverreplay_client_prefill(struct serverreplay *sr, const struct snapshot *snap, struct world *client)
{
    if (nbt_get(nbt_get(snap->player_server, "fields"), "sendQueue") == NULL) return -1;
    const nbt *loaded = nbt_get(snap->clientworld, "loaded");
    struct world *sw = sr_dim_world(sr, client->dim);
    int n = 0;
    for (int i = 0; i < (loaded ? nbt_list_size(loaded) : 0); ++i)
    {
        const nbt *p = nbt_list_get(loaded, i);
        int cx = (int)nbt_int_value(nbt_list_get(p, 0)), cz = (int)nbt_int_value(nbt_list_get(p, 1));
        const struct chunk *src = world_chunk(sw, cx, cz);
        if (src == NULL || world_chunk(client, cx, cz) != NULL) continue;
        /* a copy sharing the bands until either side writes one (chunk_share) */
        struct chunk *dst = chunk_new();
        chunk_share(dst, src);
        memset(&dst->tes, 0, sizeof dst->tes);
        serverreplay_client_chunk_fresh(dst, client->dim);
        world_put_chunk(client, dst);
        ++n;
    }
    return n;
}

void serverreplay_sync_client_chunks(struct serverreplay *sr, struct world *client,
                                     void (*on)(void *ctx, char kind, int cx, int cz), void *ctx)
{
    for (int i = 0; i < sr->ncchunks; ++i)
    {
        const struct sr_client_chunk *cc = &sr->cchunks[i];
        /* a packet another dimension's player manager sent went to the
         * WorldClient the S07 has since replaced */
        if (cc->dim != client->dim) continue;
        if (cc->kind == 'l')
        {
            const struct chunk *src = world_chunk(sr_dim_world(sr, cc->dim), cc->cx, cc->cz);
            if (!src) continue;
            struct chunk *dst = world_chunk(client, cc->cx, cc->cz);
            if (!dst)
            {
                dst = chunk_new();
                chunk_share(dst, src);
                memset(&dst->tes, 0, sizeof dst->tes);
                serverreplay_client_chunk_fresh(dst, client->dim);
                world_put_chunk(client, dst);
            }
            else
            {
                struct te_store keep = dst->tes;
                world_light_sync(client);
                chunk_share(dst, src);
                dst->tes = keep;
                serverreplay_client_chunk_fresh(dst, client->dim);
                world_note_write(client, dst);
            }
            /* the S35s after the chunk (EntityPlayerMP.onUpdate's
             * func_147097_b): a spawner's getTileEntity makes its client
             * entity */
            if (sr->pickobj)
                for (int k = 0; k < src->tes.n; ++k)
                {
                    const struct tile_entity *te = src->tes.v[k];
                    if (te != NULL && te->kind == TE_MOB_SPAWNER && !te->invalid)
                        (void)client_te_get(&sr->pickobj->ctes, &SR_DET(sr), te->x, te->y, te->z, 52);
                }
        }
        else
        {
            struct chunk *gone = world_take_chunk(client, cc->cx, cc->cz);
            if (!gone) continue;
            chunk_free(gone);
            if (sr->pickobj) client_te_chunk_gone(&sr->pickobj->ctes, cc->cx, cc->cz);
        }
        if (on) on(ctx, cc->kind, cc->cx, cc->cz);
    }
    sr->ncchunks = 0;
}

/* fh_env.drop: a falling block landed and spawned an EntityItem. The drop
 * engine already drew the item's constructor values, so the item is adopted
 * with them and joins the pass's order; a drop made while the pass runs is
 * visited in the same pass, as Java's live loadedEntityList iteration does. */
#define sr_fh_drop_unordered (nw_env->serverreplay.fh_drop_unordered)

static void sr_fh_drop(void *ctx, const fh_drop *d)
{
    struct serverreplay *sr = ctx;
    ie_ent *en = ie_adopt_item(&sr->d->iew, d->entity_id, d->uuid_msb, d->uuid_lsb, d->rand_state, d->x, d->y, d->z,
                               d->motion_x, d->motion_y, d->motion_z, d->yaw, d->hover, d->item, d->damage,
                               d->count, d->tag);

    if (en == NULL) return;

    en->age = d->age;
    en->delay = d->delay;
    ie_added_to_world(&sr->d->iew, en);
    if (!sr_fh_drop_unordered) sr_order_push(sr, 0, en);
}

/* EntityFallingBlock.fall's pass for a landing anvil: new ArrayList of
 * getEntitiesWithinAABBExcludingEntity(this, boundingBox) over the whole
 * world (the chunk walk two blocks past the box, cx outer, cz inner, the
 * sections bottom up, each section's entities in the order they joined it,
 * across the pools), then attackEntityFrom(DamageSource.anvil, amount) on
 * each: a living's through its own path (the helmet, armour, the random
 * attackedAtYaw), the player's through EntityPlayerMP's, an item's or an
 * orb's health, a falling block's setBeenAttacked, a hanging entity's break,
 * any other kind's setBeenAttacked. */
struct sr_fh_hit {
    int kind;                 /* 0 fh, 1 ie, 2 living, 3 the player */
    void *ent;
    int cx, cz, sec;
    uint64_t stamp;
};

static int sr_fh_hit_before(const struct sr_fh_hit *a, const struct sr_fh_hit *b)
{
    if (a->cx != b->cx) return a->cx < b->cx;
    if (a->cz != b->cz) return a->cz < b->cz;
    if (a->sec != b->sec) return a->sec < b->sec;
    return a->stamp < b->stamp;
}

static void sr_fh_hit_add(struct sr_fh_hit *list, int *n, int cap, int kind, void *ent, int cx, int cy, int cz,
                          uint64_t stamp)
{
    if (*n >= cap) return;
    if (cy < 0) cy = 0;
    if (cy > 15) cy = 15;
    list[*n] = (struct sr_fh_hit){kind, ent, cx, cz, cy, stamp};
    ++*n;
}

static void sr_fh_hit_ie(struct sr_fh_hit *list, int *n, int cap, ie_ent *ie, const struct aabb *box)
{
    if (ie == NULL || !ie->added_to_chunk || !aabb_intersects(&ie->e.bounding_box, box) ||
        !sr_chunk_in_search(ie->chunk_x, ie->chunk_y, ie->chunk_z, box)) return;
    sr_fh_hit_add(list, n, cap, 1, ie, ie->chunk_x, ie->chunk_y, ie->chunk_z, ie->e.chunk_stamp);
}

static void sr_fh_hit_attack(struct serverreplay *sr, const struct sr_fh_hit *h, float amount);

static void sr_fh_hurt(void *ctx, fh_ent *faller, float amount)
{
    struct serverreplay *sr = ctx;
    enum { CAP = 512 };
    struct sr_fh_hit *list ENV_LOCAL = envstack_zeroed(CAP * sizeof *list);
    struct aabb box = faller->e.bounding_box;
    int n = 0;

    for (int i = 0; i < sr->d->fhw.n; ++i)
    {
        fh_ent *f = fh_ent_at(sr->d->fhw.slot[i]);
        if (f == faller || !f->added_to_chunk || !aabb_intersects(&f->e.bounding_box, &box) ||
            !sr_chunk_in_search(f->chunk_x, f->chunk_y, f->chunk_z, &box)) continue;
        sr_fh_hit_add(list, &n, CAP, 0, f, f->chunk_x, f->chunk_y, f->chunk_z, f->e.chunk_stamp);
    }
    for (int i = 0; i < sr->d->iew.n; ++i) sr_fh_hit_ie(list, &n, CAP, ie_ent_at(sr->d->iew.slot[i]), &box);
    struct living *twin = lv_get(sr->player_livh);
    for (int i = 0; sr->mobs_enabled && i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (!en->used) continue;
        if (!en->is_living)
        {
            sr_fh_hit_ie(list, &n, CAP, ie_get(en->ieh), &box);
            continue;
        }
        struct living *l = lv_get(en->livh);
        if (l == twin || !l->added_to_chunk || !aabb_intersects(&l->e.bounding_box, &box) ||
            !sr_chunk_in_search(l->chunk_coord_x, l->chunk_coord_y, l->chunk_coord_z, &box)) continue;
        sr_fh_hit_add(list, &n, CAP, 2, l, l->chunk_coord_x, l->chunk_coord_y, l->chunk_coord_z, l->e.chunk_stamp);
    }
    struct server_player *p = sr->player;
    if (p != NULL && twin != NULL && !p->sv.removed && p->dimension == sr->here &&
        aabb_intersects(&p->e.bounding_box, &box))
        sr_fh_hit_add(list, &n, CAP, 3, twin, twin->chunk_coord_x, twin->chunk_coord_y, twin->chunk_coord_z,
                      twin->e.chunk_stamp);

    /* the walk's order: an insertion sort over the few entries */
    for (int i = 1; i < n; ++i)
    {
        struct sr_fh_hit t = list[i];
        int j = i;
        while (j > 0 && sr_fh_hit_before(&t, &list[j - 1])) { list[j] = list[j - 1]; --j; }
        list[j] = t;
    }

    /* what a hit spawns (a hanging entity's drop, a killed mob's loot, the
     * player's death drops) joins the pass's tail in the order it spawned,
     * as spawnEntityInWorld appends to loadedEntityList */
    int prev_unordered = sr_fh_drop_unordered;
    sr_fh_drop_unordered = 1;
    for (int i = 0; i < n; ++i)
    {
        int an_before = sr->d->anw.n, ie_before = sr->d->iew.n;
        sr_fh_hit_attack(sr, &list[i], amount);
        for (int j = ie_before; j < sr->d->iew.n; ++j) sr_order_push(sr, 0, ie_ent_at(sr->d->iew.slot[j]));
        for (int j = an_before; j < sr->d->anw.n; ++j) sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[j]));
    }
    sr_fh_drop_unordered = prev_unordered;
}

static void sr_fh_hit_attack(struct serverreplay *sr, const struct sr_fh_hit *h, float amount)
{
    if (h->kind == 0) fh_attacked(&sr->d->fhw, h->ent);
    else if (h->kind == 1)
    {
        /* EntityItem and EntityXPOrb: setBeenAttacked, the health; every
         * other kind Entity.attackEntityFrom's setBeenAttacked */
        ie_ent *ie = h->ent;
        ie->e.velocity_changed = 1;
        if (ie->kind != IE_ITEM && ie->kind != IE_ORB) return;
        ie->health = (int)((float)ie->health - amount);
        if (ie->health <= 0) ie->is_dead = 1;
    }
    else living_attack_entity_from(h->ent, DMG_ANVIL, amount, &SR_DET(sr));
}

/* A hanging entity broken by a player's attack or a projectile: the drops
 * join the item pool, and the phase that broke it (the network phase's
 * unhook, the living pass's tail) orders them. */
void serverreplay_break_hanging(struct serverreplay *sr, fh_ent *en)
{
    (void)sr;
    if (en->is_dead) return;
    sr_fh_drop_unordered = 1;
    fallhang_player_break(en);
    sr_fh_drop_unordered = 0;
}

/* ItemHangingEntity.onItemUse's server half: the EntityItemFrame or
 * EntityPainting(world, x, y, z, dir) constructor on the server's streams
 * (the fake-det dance the dev hanging op uses; the painting's art loop and
 * its Random's pick), onValidSurface, and spawnEntityInWorld when it holds.
 * Returns whether it hangs. */
int serverreplay_place_hanging(struct serverreplay *sr, int kind, int x, int y, int z, int dir)
{
    det_state fake;
    sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
    det_state *saved_det = sr->d->fhw.det;
    int saved_role = sr->d->fhw.role;
    sr->d->fhw.det = &fake;
    sr->d->fhw.role = DET_OTHER;
    fh_ent *fh = fh_place_hanging(&sr->d->fhw, kind, x, y, z, dir, 0, 0, 0);
    sr->d->fhw.det = saved_det;
    sr->d->fhw.role = saved_role;
    sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);
    if (fh == NULL) return 0;
    serverreplay_track_entity(sr, 1, fh);
    sr->native_entities = sr->d->iew.n + sr->d->fhw.n + sr->d->anw.n;
    return 1;
}

/* servertick's spawn hook: the world tick constructed an entity, so the native
 * list adopts it with the draws already spent. Only the overworld's lists are
 * modelled: the tapes carry no entities in the Nether or the End. */
void serverreplay_bolt_packet(struct serverreplay *sr, int dim, double x, double y, double z)
{
    /* WorldServer.addWeatherEffect's S2C through func_148541_a: every
     * listed player of the bolt's dimension within 512 blocks, a dead one
     * too */
    struct server_player *p = sr->player;
    if (p == NULL || p->dimension != dim) return;
    double dx = x - p->e.pos_x, dy = y - p->e.pos_y, dz = z - p->e.pos_z;
    if (!(dx * dx + dy * dy + dz * dz < 512.0 * 512.0)) return;
    /* a world tick's packet opens the row's queue, as a head-of-tick dev
     * op's does */
    if (!p->dev_prequeued && !p->net_phase)
    {
        s2c_clear();
        p->dev_prequeued = 1;
    }
    struct s2c_pkt *pk = s2c_add(s2c_out());
    pk->kind = PK_S2C;
    pk->f0 = x;
    pk->f1 = y;
    pk->f2 = z;
}

static void sr_on_spawn(void *ctx, const struct st_spawn *sp)
{
    struct serverreplay *sr = ctx;

    if (!strcmp(sp->cls, "EntityLightningBolt"))
    {
        serverreplay_bolt_packet(sr, sr->here, sp->x, sp->y, sp->z);
        if (sr->d->nbolts == sr->d->capbolts)
        {
            sr->d->capbolts = sr->d->capbolts ? sr->d->capbolts * 2 : 8;
            sr->d->bolts = realloc(sr->d->bolts, (size_t)sr->d->capbolts * sizeof *sr->d->bolts);
        }
        lightning_adopt(&sr->d->bolts[sr->d->nbolts++], sp->x, sp->y, sp->z,
                        sp->rand_state, sp->bolt_vertex, sp->bolt_living_time);
        return;
    }

    if (!strcmp(sp->cls, "EntityItem"))
    {
        ie_ent *en = ie_adopt_item(&sr->d->iew, sp->id, sp->uuid_msb, sp->uuid_lsb, sp->rand_state, sp->x, sp->y,
                                   sp->z, sp->mx, sp->my, sp->mz, sp->yaw, sp->hover, sp->item, sp->damage,
                                   sp->count, 0);

        if (en != NULL)
        {
            en->delay = sp->delay;
            ie_added_to_world(&sr->d->iew, en);
            sr_order_push(sr, 0, en);
        }

        return;
    }

    if (!strcmp(sp->cls, "EntityXPOrb"))
    {
        ie_ent *en = ie_adopt_orb(&sr->d->iew, sp->id, sp->uuid_msb, sp->uuid_lsb, sp->rand_state, sp->x, sp->y,
                                  sp->z, sp->mx, sp->my, sp->mz, sp->yaw, sp->age);

        if (en != NULL)
        {
            ie_added_to_world(&sr->d->iew, en);
            sr_order_push(sr, 0, en);
        }

        return;
    }

    if (!strcmp(sp->cls, "EntityTNTPrimed"))
    {
        ie_ent *en = ie_adopt_tnt(&sr->d->iew, sp->id, sp->uuid_msb, sp->uuid_lsb, sp->rand_state, sp->x, sp->y,
                                  sp->z, sp->mx, sp->my, sp->mz, sp->time, 0);

        if (en != NULL)
        {
            ie_added_to_world(&sr->d->iew, en);
            sr_order_push(sr, 0, en);
        }

        return;
    }

    if (!strcmp(sp->cls, "EntityFallingBlock"))
    {
        fh_ent *en = fh_adopt_falling(&sr->d->fhw, sp->id, sp->uuid_msb, sp->uuid_lsb, sp->rand_state, sp->x,
                                      sp->y, sp->z, sp->tile, sp->data);

        if (en != NULL)
        {
            fh_added_to_world(&sr->d->fhw, en);
            sr_order_push(sr, 1, en);
        }

        return;
    }

    /* An unported entity cannot be silently omitted from the replay. */
    set_err(sr, "the tick spawned %s, which the native lists do not carry", sp->cls);
}

/* World.getEntitiesWithinAABBExcludingEntity sees the loaded entities in
 * chunk-list order. For the bolt's small box, the replay's spawn order is the
 * same order until a chunk crossing; each hit is applied before the next. */
void serverreplay_lightning_strike(void *ctx, const struct lightning_bolt *bolt)
{
    struct serverreplay *sr = ctx;
    struct aabb box = aabb_make(bolt->x - 3.0, bolt->y - 3.0, bolt->z - 3.0,
                                bolt->x + 3.0, bolt->y + 9.0, bolt->z + 3.0);
    int n = sr->d->nents;
    for (int i = 0; i < n; ++i)
    {
        struct sr_ent *rec = &sr->d->ents[i];
        struct entity *e;
        struct living *l = NULL;
        ie_ent *ie = NULL;
        fh_ent *fh = NULL;
        if (rec->pool == 3) continue;   /* the player is struck below */
        if (rec->pool == 2)
        {
            struct an_ent *ae = sr_ent_p(sr, rec);
            if (!ae->used) continue;
            if (ae->is_living) { l = lv_get(ae->livh); e = &l->e; }
            else { ie = ie_get(ae->ieh); e = &ie->e; }
        }
        else if (rec->pool == 1) { fh = sr_ent_p(sr, rec); e = &fh->e; }
        else { ie = sr_ent_p(sr, rec); e = &ie->e; }
        if ((l && l->is_dead) || (ie && ie->is_dead) || (fh && fh->is_dead) ||
            !aabb_intersects(&box, &e->bounding_box)) continue;

        if (l && l->kind == AK_PIG)
        {
            struct living *z = living_alloc();
            int role = sr->d->iew.role;
            det_state fake;
            det_state *ctor = &SR_DET(sr);
            if (role != DET_OTHER)
            {
                sr_constructor_begin(&fake, &SR_DET(sr), role);
                ctor = &fake;
            }
            living_init(z, sr->d->anw.w, HK_PIGMAN, ctor);
            z->an = &sr->d->anw;
            pigman_construct(z, ctor);
            if (role != DET_OTHER) sr_constructor_end(&SR_DET(sr), &fake, role);
            pigman_add_random_armor(z);
            living_set_location_and_angles(z, e->pos_x, e->pos_y, e->pos_z,
                                           l->rotation_yaw, l->rotation_pitch);
            an_add_living(&sr->d->anw, z, sr->d->anw.n);
            sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
            l->is_dead = 1;
            continue;
        }
        if (l)
        {
            if (!l->immune_to_fire) living_attack_entity_from(l, DMG_IN_FIRE, 5.0F, &SR_DET(sr));
            ++e->fire;
            if (e->fire == 0) living_set_fire(l, 8);
            if (l->kind == HK_CREEPER) l->creeper_powered = 1;
        }
        else
        {
            if (e->attack_from) e->attack_from(e->self, IE_SRC_IN_FIRE, 5.0F);
            ++e->fire;
            if (e->fire == 0) e->fire = 160;
        }
    }
    if (sr->player_livh && !lv_get(sr->player_livh)->is_dead &&
        aabb_intersects(&box, &lv_get(sr->player_livh)->e.bounding_box))
    {
        /* Entity.onStruckByLightning on the EntityPlayerMP: dealFireDamage(5)
         * is DamageSource.inFire (armor applies, it is not a bypass), and the
         * fire counter is the server player's own (its fireResistance 20). */
        struct living *l = lv_get(sr->player_livh);
        struct server_player *p = sr->player;
        if (p == NULL)
        {
            /* the lightning probe's bare player twin: no handler behind it */
            living_attack_entity_from(l, DMG_IN_FIRE, 5.0F, &SR_DET(sr));
            ++l->e.fire;
            if (l->e.fire == 0) living_set_fire(l, 8);
            return;
        }
        /* the world pass runs ahead of the network tick that normally
         * opens the row's packet queue: open it here */
        if (!p->dev_prequeued && !p->net_phase)
        {
            s2c_clear();
            p->dev_prequeued = 1;
        }
        float before = p->sv.health;
        p->sv.erand = l->rand;
        surv_server_damage(p, SURV_IN_FIRE, 5.0F);
        l->rand = p->sv.erand;
        l->health = p->sv.health;
        l->is_dead = p->sv.dead;
        /* the tracker pass after updateEntities: the changed health (data
         * watcher 6) reaches the player's own client as an S1C this tick */
        if (p->sv.health != before && s2c_out()->n < S2C_MAX)
        {
            struct s2c_pkt *pkt = s2c_add(s2c_out());
            pkt->kind = PK_S1C;
            pkt->f0 = p->sv.health;
        }
        ++p->e.fire;
        /* setFire(8) through getFireTimeForEntity (the twin's last active
         * items are the player's armour) */
        if (p->e.fire == 0)
        {
            int v = ench_fire_time(l, 160);
            if (p->e.fire < v) p->e.fire = v;
        }
    }
}

/* World.updateEntity for one entity of the living pool, with the replay's
 * bookkeeping around it: the explosion's S27, the player's motion, the
 * spawner's view, and the new entities its tick made joining the order.
 * The index it held in the order when it left the lists, else -1. */
static int sr_tick_an(struct serverreplay *sr, struct sr_world *w, int t, int player_here,
                      struct an_ent *en, int keep_dead)
{
    int before = sr->d->anw.n;
    double mx0 = lv_get(sr->player_livh) ? lv_get(sr->player_livh)->e.motion_x : 0.0;
    double my0 = lv_get(sr->player_livh) ? lv_get(sr->player_livh)->e.motion_y : 0.0;
    double mz0 = lv_get(sr->player_livh) ? lv_get(sr->player_livh)->e.motion_z : 0.0;
    sr->d->anw.iew.world_rand.r = ST_RAND(&w->st);
    sr->d->anw.difficulty = w->st.difficulty;
    nw_env->blockcb.env.world_rand = &sr->d->anw.iew.world_rand.r;
    living_set_walking_world(&sr->d->anw.iew.world_rand.r);
    /* a chunk the entity's tick loads (the path finder's ChunkCache) reseeds
     * the World.rand this pass draws */
    jrand *prev_pop_rand = populate_world_rand;
    populate_world_rand = &sr->d->anw.iew.world_rand.r;
    sr->in_entity_tick = 1;
    int removed = 0;
    int iew_before = sr->d->iew.n;
    /* cleared only when a blast left something in it: the 16-byte store
     * across its line was a DRAM demand fill a living (the bits compared,
     * so a -0.0 is cleared too) */
    static const double zero3[3];
    if (memcmp(nw_env->explosion.player_gap, zero3, sizeof zero3))
        memset(nw_env->explosion.player_gap, 0, sizeof nw_env->explosion.player_gap);
    if (keep_dead) an_update_one(&sr->d->anw, en, t);
    else removed = an_tick_one(&sr->d->anw, en, t);
    sr->in_entity_tick = 0;
    populate_world_rand = prev_pop_rand;
    if (sr->blast_pending)
    {
        sr->s27_x += lv_get(sr->player_livh)->e.motion_x - mx0 + nw_env->explosion.player_gap[0];
        sr->s27_y += lv_get(sr->player_livh)->e.motion_y - my0 + nw_env->explosion.player_gap[1];
        sr->s27_z += lv_get(sr->player_livh)->e.motion_z - mz0 + nw_env->explosion.player_gap[2];
        sr->has_s27 = 1;
        sr->blast_pending = 0;
        memset(nw_env->explosion.player_gap, 0, sizeof nw_env->explosion.player_gap);
        /* the blast's push lands on the EntityPlayerMP's own motion too
         * (after the hit's knockBack): the S12 the tracker sends carries it */
        if (sr->player != NULL)
        {
            sr->player->e.motion_x = lv_get(sr->player_livh)->e.motion_x;
            sr->player->e.motion_y = lv_get(sr->player_livh)->e.motion_y;
            sr->player->e.motion_z = lv_get(sr->player_livh)->e.motion_z;
        }
    }
    if (sr->player != NULL && player_here)
    {
        sr->player->e.motion_x = lv_get(sr->player_livh)->e.motion_x;
        sr->player->e.motion_y = lv_get(sr->player_livh)->e.motion_y;
        sr->player->e.motion_z = lv_get(sr->player_livh)->e.motion_z;
    }
    nw_env->blockcb.env.world_rand = &ST_RAND(&w->st);
    ST_RAND(&w->st) = sr->d->anw.iew.world_rand.r;
    living_set_walking_world(&ST_RAND(&w->st));
    int at = -1;
    if (removed)
    {
        if (en->is_living)
            spawner_track(&sr->d->spawner, lv_get(en->livh)->entity_id, NULL);
        for (int k = 0; k < sr->d->nents; ++k)
            if (sr->d->ents[k].pool == 2 && sr_ent_p(sr, &sr->d->ents[k]) == en)
            {
                sr_order_remove(sr, k);
                at = k;
                break;
            }
    }
    else if (en->is_living)
        spawner_track(&sr->d->spawner, lv_get(en->livh)->entity_id, &lv_get(en->livh)->e);
    int first_new = before - removed;
    for (int j = first_new; j < sr->d->anw.n; ++j)
        sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[j]));
    /* the entity's tick can spawn items (the player's death drops through
     * external_attack, a mob's own drop path): they join the order's tail,
     * the same append loadedEntityList gets in Java's pass */
    for (int j = iew_before; j < sr->d->iew.n; ++j)
        sr_order_push(sr, 0, ie_ent_at(sr->d->iew.slot[j]));
    return at;
}

static int sr_box_hits(const double box[6], const struct aabb *e)
{
    return box[3] > e->min_x && box[0] < e->max_x && box[4] > e->min_y && box[1] < e->max_y &&
           box[5] > e->min_z && box[2] < e->max_z;
}

static int sr_chunk_in_search(int cx, int cy, int cz, const struct aabb *box);

/* World.getEntitiesWithinAABBExcludingEntity reaches an entity through the
 * chunk and section it was added to (the chunks and sections two blocks past
 * the box): the dragon's wing and head boxes miss a mob filed one section
 * below them. l NULL is the player. */
static int sr_in_dragon_search(struct serverreplay *sr, const struct living *l, const double box[6])
{
    struct aabb b = aabb_make(box[0], box[1], box[2], box[3], box[4], box[5]);
    if (l == NULL) l = lv_get(sr->player_livh);
    if (l == NULL || !l->added_to_chunk) return 1;
    return sr_chunk_in_search(l->chunk_coord_x, l->chunk_coord_y, l->chunk_coord_z, &b);
}

/* EntityDragon.collideWithEntities: every EntityLivingBase the wing's box
 * reaches (the End's livings and the player, in the pass's order) gets
 * addVelocity away from the body's centre. The player's push stays on the
 * server's motion (addVelocity sets no velocityChanged). */
static void sr_dragon_wing_push(void *ctx, const double box[6], double cx, double cz)
{
    struct serverreplay *sr = ctx;

    for (int k = 0; k < sr->d->nents; ++k)
    {
        struct entity *e = NULL;
        struct living *l = NULL;

        if (sr->d->ents[k].pool == 3)
        {
            if (sr->player == NULL || sr->player->sv.removed) continue;
            e = &sr->player->e;
        }
        else if (sr->d->ents[k].pool == 2)
        {
            struct an_ent *en = sr_ent_p(sr, &sr->d->ents[k]);
            if (!en->used || !en->is_living) continue;
            l = lv_get(en->livh);
            e = &l->e;
        }
        else continue;

        if (!sr_box_hits(box, &e->bounding_box) || !sr_in_dragon_search(sr, l, box)) continue;

        double dx = e->pos_x - cx, dz = e->pos_z - cz;
        double dist = dx * dx + dz * dz;
        e->motion_x += dx / dist * 4.0;
        e->motion_y += 0.20000000298023224;
        e->motion_z += dz / dist * 4.0;
        if (l != NULL) l->is_air_borne = 1;
        else if (sr->player_livh != 0)
        {
            lv_get(sr->player_livh)->e.motion_x = e->motion_x;
            lv_get(sr->player_livh)->e.motion_y = e->motion_y;
            lv_get(sr->player_livh)->e.motion_z = e->motion_z;
        }
    }
}

/* EntityDragon.attackEntitiesInList: DamageSource.causeMobDamage(dragon),
 * 10, on every EntityLivingBase the head's box reaches. */
static void sr_dragon_head_bite(void *ctx, const double box[6])
{
    struct serverreplay *sr = ctx;
    const struct dragon_state *d = &sr->end->d;

    /* the source's entity is the dragon as it stands now, moved this tick */
    sr_dragon_shadow_sync(sr);

    for (int k = 0; k < sr->d->nents; ++k)
    {
        if (sr->d->ents[k].pool == 3)
        {
            struct server_player *p = sr->player;
            if (p == NULL || p->sv.removed || !sr_box_hits(box, &p->e.bounding_box) || !sr_in_dragon_search(sr, NULL, box)) continue;
            double at[2] = {d->x, d->z};
            surv_set_attacker(SURV_ATTACKER_NO_EGG);
            surv_set_attacker_msg(DMG_MOB, CM_NAME_DRAGON);
            surv_server_damage_by(p, SURV_MOB, 10.0F, at);
            if (sr->player_livh != 0)
            {
                lv_get(sr->player_livh)->e.motion_x = p->e.motion_x;
                lv_get(sr->player_livh)->e.motion_y = p->e.motion_y;
                lv_get(sr->player_livh)->e.motion_z = p->e.motion_z;
                lv_get(sr->player_livh)->health = p->sv.health;
                lv_get(sr->player_livh)->is_dead = p->sv.dead;
            }
        }
        else if (sr->d->ents[k].pool == 2)
        {
            struct an_ent *en = sr_ent_p(sr, &sr->d->ents[k]);
            if (!en->used || !en->is_living || !sr_box_hits(box, &lv_get(en->livh)->e.bounding_box) || !sr_in_dragon_search(sr, lv_get(en->livh), box)) continue;
            living_attack_entity_from_attacker(lv_get(en->livh), lv_get(sr->end->shadowh), DMG_MOB, 10.0F, &SR_DET(sr));
        }
    }
}

/* ------------------------------------------------ the End's explosions */


static int sr_chunk_in_search(int cx, int cy, int cz, const struct aabb *box)
{
    int x0 = (int)floor((box->min_x - 2.0) / 16.0), x1 = (int)floor((box->max_x + 2.0) / 16.0);
    int z0 = (int)floor((box->min_z - 2.0) / 16.0), z1 = (int)floor((box->max_z + 2.0) / 16.0);
    int y0 = (int)floor((box->min_y - 2.0) / 16.0), y1 = (int)floor((box->max_y + 2.0) / 16.0);
    if (y0 < 0) y0 = 0;
    if (y0 > 15) y0 = 15;
    if (y1 < 0) y1 = 0;
    if (y1 > 15) y1 = 15;
    if (cy < 0) cy = 0;
    if (cy > 15) cy = 15;
    return cx >= x0 && cx <= x1 && cz >= z0 && cz <= z1 && cy >= y0 && cy <= y1;
}

static struct aabb sr_crystal_box(const struct dragon_crystal_state *c)
{
    return aabb_make(c->x - 1.0, c->y - 1.0, c->z - 1.0, c->x + 1.0, c->y + 1.0, c->z + 1.0);
}

static struct aabb sr_part_box(const struct dragon_part_state *p)
{
    double h = (double)(p->width / 2.0f);
    return aabb_make(p->x - h, p->y, p->z - h, p->x + h, p->y + (double)p->height, p->z + h);
}

static void sr_end_crystal_explode(struct serverreplay *sr, double x, double y, double z);

/* The blast's context: the player's entry in Explosion.field_77288_k. */
struct sr_end_blast {
    struct serverreplay *sr;
    double kx, ky, kz;
    double eye_pos[8][3];     /* the parts' positions as the pass reads them */
    /* the boxes and positions the query hands the blast, alive until it ends */
    struct aabb cboxes[DRAGON_MAX_CRYSTALS];
    double cpos[DRAGON_MAX_CRYSTALS][3];
    struct aabb dbox, pboxes[7];
    double dpos[3];
};

static void eb_player_attack(void *ent, float amount)
{
    struct sr_end_blast *b = ent;
    struct server_player *p = b->sr->player;

    if (b->sr->end_blast_igniter)
    {
        /* setExplosionSource: the TNT's placer is the attacker (tnt.c's
         * player_attack) */
        double at[2] = {p->e.pos_x, p->e.pos_z};
        surv_set_attacker(SK_PLAYER);
        surv_server_damage_by(p, SURV_EXPLOSION, amount, at);
    }
    else
        surv_server_damage_by(p, SURV_EXPLOSION, amount, NULL);
    if (b->sr->player_livh != 0)
    {
        /* the knockback from the placer lands on the twin too (tnt.c's
         * player_attack): the pass copies the twin's motion back */
        lv_get(b->sr->player_livh)->e.motion_x = p->e.motion_x;
        lv_get(b->sr->player_livh)->e.motion_y = p->e.motion_y;
        lv_get(b->sr->player_livh)->e.motion_z = p->e.motion_z;
        lv_get(b->sr->player_livh)->health = p->sv.health;
        lv_get(b->sr->player_livh)->is_dead = p->sv.dead;
    }
}

static void eb_player_motion(void *ent, double mx, double my, double mz)
{
    struct sr_end_blast *b = ent;
    struct server_player *p = b->sr->player;

    p->e.motion_x += mx;
    p->e.motion_y += my;
    p->e.motion_z += mz;
    /* the living twin carries the same motion: the pass copies it back onto
     * the server player after the entity whose tick made the blast */
    if (b->sr->player_livh != 0)
    {
        struct living *tw = lv_get(b->sr->player_livh);
        tw->e.motion_x += mx;
        tw->e.motion_y += my;
        tw->e.motion_z += mz;
    }
    b->kx = nw_env->explosion.raw_push[0];
    b->ky = nw_env->explosion.raw_push[1];
    b->kz = nw_env->explosion.raw_push[2];
}

static void eb_living_attack(void *ent, float amount)
{
    struct living *attacker = sr_end_ctx->end_blast_igniter ? lv_get(sr_end_ctx->player_livh) : NULL;
    living_attack_entity_from_attacker(ent, attacker, DMG_EXPLOSION, amount, &SR_DET(sr_end_ctx));
}

static void eb_living_motion(void *ent, double mx, double my, double mz)
{
    struct living *l = ent;
    l->e.motion_x += mx;
    l->e.motion_y += my;
    l->e.motion_z += mz;
}

/* Entity.attackEntityFrom for the living pool's items, orbs and
 * projectiles: an item's and an orb's health, setBeenAttacked for all. */
static void eb_ie_attack(void *ent, float amount)
{
    ie_ent *en = ent;
    en->e.velocity_changed = 1;
    if (en->kind != IE_ITEM && en->kind != IE_ORB) return;
    en->health = (int)((float)en->health - amount);
    if (en->health <= 0) en->is_dead = 1;
}

static void eb_ie_motion(void *ent, double mx, double my, double mz)
{
    ie_ent *en = ent;
    en->e.motion_x += mx;
    en->e.motion_y += my;
    en->e.motion_z += mz;
}

/* the End's falling blocks and hanging entities: tnt.c's fh_attack
 * (EntityHanging.attackEntityFrom breaks it, a falling block is marked) and
 * the push every entity takes */
static void eb_fh_attack(void *ent, float amount)
{
    fh_ent *en = ent;
    (void)amount;
    fh_attacked(en->fw, en);
}

static void eb_fh_motion(void *ent, double mx, double my, double mz)
{
    fh_ent *en = ent;
    en->e.motion_x += mx;
    en->e.motion_y += my;
    en->e.motion_z += mz;
}

/* EntityEnderCrystal.attackEntityFrom: the first hit kills it and explodes
 * it where it stands (a chain inside the outer blast's pass). */
static void eb_crystal_attack(void *ent, float amount)
{
    struct dragon_crystal_state *c = ent;
    (void)amount;
    if (c->dead) return;
    c->health = 0;
    c->dead = 1;
    sr_end_crystal_explode(sr_end_ctx, c->x, c->y, c->z);
}

static void eb_crystal_motion(void *ent, double mx, double my, double mz)
{
    struct dragon_crystal_state *c = ent;
    c->mx += mx;
    c->my += my;
    c->mz += mz;
}

/* EntityDragon.attackEntityFrom is false; the push is its own. */
static void eb_dragon_attack(void *ent, float amount)
{
    (void)ent;
    (void)amount;
}

static void eb_dragon_motion(void *ent, double mx, double my, double mz)
{
    struct dragon_state *d = ent;
    d->mx += mx;
    d->my += my;
    d->mz += mz;
    d->is_air_borne = 1;
}

/* EntityDragonPart.attackEntityFrom -> attackEntityFromPart with the
 * explosion's source (no entity: a crystal's blast has no exploder). */
static void eb_part_attack(void *ent, float amount)
{
    struct dragon_part_state *p = ent;
    struct serverreplay *sr = sr_end_ctx;
    struct dragon_state *d = &sr->end->d;
    /* a TNT the player lit: the source's entity is the player, so the
     * knockback runs from the player's position */
    if (sr->end_blast_igniter && sr->player != NULL)
    {
        d->player_x = sr->player->e.pos_x;
        d->player_y = sr->player->e.pos_y;
        d->player_z = sr->player->e.pos_z;
        /* onDeath's addToPlayerScore: the mobKills counter */
        if (dragon_attack_part(d, (int)(p - d->part), amount, 1)) surv_add_stat(sr->player, STAT_MOB_KILLS, 1);
    }
    else
        dragon_attack_part(d, (int)(p - d->part), amount, 2);
}

static void eb_part_motion(void *ent, double mx, double my, double mz)
{
    (void)ent;
    (void)mx;
    (void)my;
    (void)mz;
}

/* One entry of the End's blast list, with the key World's chunk walk gives
 * it: chunk x (outer), chunk z, section, then the section's own order. */
struct eb_entry {
    struct expl_extra ex;
    int cx, cz, sec;
    uint64_t stamp;
    int sub;
};

static void eb_add(struct eb_entry *list, int *n, int cap, struct expl_extra ex, int cx, int cy, int cz,
                   uint64_t stamp, int sub)
{
    if (*n >= cap) return;
    if (cy < 0) cy = 0;
    if (cy > 15) cy = 15;
    list[*n].ex = ex;
    list[*n].cx = cx;
    list[*n].cz = cz;
    list[*n].sec = cy;
    list[*n].stamp = stamp;
    list[*n].sub = sub;
    ++*n;
}

static int eb_before(const struct eb_entry *a, const struct eb_entry *b)
{
    if (a->cx != b->cx) return a->cx < b->cx;
    if (a->cz != b->cz) return a->cz < b->cz;
    if (a->sec != b->sec) return a->sec < b->sec;
    if (a->stamp != b->stamp) return a->stamp < b->stamp;
    return a->sub < b->sub;
}

/* Explosion.doExplosionA's entity pass over the End beyond the server's item
 * pool: the livings, the living pool's items, orbs and projectiles, the
 * crystals, the dragon then its seven parts, and the player, each reached
 * through the chunk and section it was added to, in World's walk order. */
static int sr_end_blast_query(void *ctx, struct aabb box, struct expl_extra *out, int cap)
{
    struct sr_end_blast *b = ctx;
    struct serverreplay *sr = b->sr;
    if (sr->end_blast_list == NULL) sr->end_blast_list = slab_take(512 * sizeof *sr->end_blast_list);
    struct eb_entry *list = sr->end_blast_list;
    int n = 0;

    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (!en->used) continue;
        if (en->is_living)
        {
            struct living *l = lv_get(en->livh);
            if (l == lv_get(sr->player_livh) || !l->added_to_chunk) continue;
            if (!aabb_intersects(&l->e.bounding_box, &box) ||
                !sr_chunk_in_search(l->chunk_coord_x, l->chunk_coord_y, l->chunk_coord_z, &box)) continue;
            struct expl_extra ex = {l, &l->e.pos_x, living_eye_height(l), &l->e.bounding_box,
                                    eb_living_attack, eb_living_motion, l};
            eb_add(list, &n, 512, ex, l->chunk_coord_x, l->chunk_coord_y, l->chunk_coord_z, l->e.chunk_stamp, 0);
        }
        else
        {
            ie_ent *ie = ie_get(en->ieh);
            if (!ie->added_to_chunk || !aabb_intersects(&ie->e.bounding_box, &box) ||
                !sr_chunk_in_search(ie->chunk_x, ie->chunk_y, ie->chunk_z, &box)) continue;
            struct expl_extra ex = {ie, &ie->e.pos_x, 0.0F, &ie->e.bounding_box, eb_ie_attack, eb_ie_motion, NULL};
            eb_add(list, &n, 512, ex, ie->chunk_x, ie->chunk_y, ie->chunk_z, ie->e.chunk_stamp, 0);
        }
    }

    /* the falling blocks and the hanging entities (a leash knot) */
    {
        FH_QUERY_LIST(found);
        int k = fh_entities_in_box(&sr->d->fhw, box, found, FH_MAX_ENTITIES);
        for (int i = 0; i < k; ++i)
        {
            fh_ent *f = found[i];
            struct expl_extra ex = {f, &f->e.pos_x, 0.0F, &f->e.bounding_box, eb_fh_attack, eb_fh_motion, NULL};
            eb_add(list, &n, 512, ex, f->chunk_x, f->chunk_y, f->chunk_z, f->e.chunk_stamp, 0);
        }
    }

    struct dragon_state *d = &sr->end->d;
    struct aabb *cboxes = b->cboxes;
    double (*cpos)[3] = b->cpos;
    for (int i = 0; i < d->n_crystals; ++i)
    {
        struct dragon_crystal_state *c = &d->crystals[i];
        if (!c->in_world) continue;
        cboxes[i] = sr_crystal_box(c);
        int cx = (int)floor(c->x) >> 4, cy = (int)floor(c->y / 16.0), cz = (int)floor(c->z) >> 4;
        if (!aabb_intersects(&cboxes[i], &box) || !sr_chunk_in_search(cx, cy, cz, &box)) continue;
        cpos[i][0] = c->x;
        cpos[i][1] = c->y;
        cpos[i][2] = c->z;
        struct expl_extra ex = {c, cpos[i], 0.0F, &cboxes[i], eb_crystal_attack, eb_crystal_motion, NULL};
        eb_add(list, &n, 512, ex, cx, cy, cz, c->chunk_stamp, 0);
    }

    struct aabb *pboxes = b->pboxes;
    double *dpos = b->dpos;
    if (sr->end->dragon_listed && endfight_dragon_listed_near(d, &box))
    {
        b->dbox = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
        dpos[0] = d->x;
        dpos[1] = d->y;
        dpos[2] = d->z;
        if (aabb_intersects(&b->dbox, &box))
        {
            struct expl_extra ex = {d, dpos, 8.0F * 0.85F, &b->dbox, eb_dragon_attack, eb_dragon_motion, NULL};
            eb_add(list, &n, 512, ex, d->chunk_x, d->chunk_y, d->chunk_z, d->chunk_stamp, 0);
        }
        for (int i = 0; i < 7; ++i)
        {
            pboxes[i] = sr_part_box(&d->part[i]);
            b->eye_pos[i][0] = d->part[i].x;
            b->eye_pos[i][1] = d->part[i].y;
            b->eye_pos[i][2] = d->part[i].z;
            if (!aabb_intersects(&pboxes[i], &box)) continue;
            struct expl_extra ex = {&d->part[i], b->eye_pos[i], 0.0F, &pboxes[i], eb_part_attack, eb_part_motion, NULL};
            eb_add(list, &n, 512, ex, d->chunk_x, d->chunk_y, d->chunk_z, d->chunk_stamp, 1 + i);
        }
    }

    struct server_player *p = sr->player;
    if (p != NULL && !p->sv.removed && sr->player_livh != 0 && aabb_intersects(&p->e.bounding_box, &box))
    {
        const struct living *pl = lv_get(sr->player_livh);
        struct expl_extra ex = {b, &p->e.pos_x, 1.62F, &p->e.bounding_box, eb_player_attack, eb_player_motion, pl};
        eb_add(list, &n, 512, ex, pl->chunk_coord_x, pl->chunk_coord_y, pl->chunk_coord_z, pl->e.chunk_stamp, 0);
    }

    /* the walk's order: an insertion sort over the few entries */
    for (int i = 1; i < n; ++i)
    {
        struct eb_entry t = list[i];
        int j = i;
        while (j > 0 && eb_before(&t, &list[j - 1])) { list[j] = list[j - 1]; --j; }
        list[j] = t;
    }

    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) out[k++] = list[i].ex;
    return k;
}

/* EntityEnderCrystal's world.createExplosion(null, x, y, z, 6.0F, true):
 * WorldServer.newExplosion (no fire, blocks destroyed), then the S27 to the
 * player within 64 blocks carrying its knockback. World.rand is the one in
 * use (the pass's copy inside an entity's tick). */
static void sr_end_crystal_explode(struct serverreplay *sr, double x, double y, double z)
{
    struct sr_end_blast *b ENV_LOCAL = envstack_zeroed(sizeof *b);
    b->sr = sr;
    struct sr_world *ws = sr_dim_ws(sr, 1);
    jrand *wr = sr->in_entity_tick ? &sr->d->anw.iew.world_rand.r : &ST_RAND(&ws->st);
    int items_before = sr->d->iew.n;
    const struct ie_ent *prev_exclude = expl_exclude_ie;
    int prev_placed = expl_placed_by_player, prev_igniter = sr->end_blast_igniter;

    /* the crystal's own blast has no exploder and no placer */
    expl_exclude_ie = NULL;
    expl_placed_by_player = 0;
    sr->end_blast_igniter = 0;
    expl_run_extras(&sr->sky.world, &SR_DET(sr), DET_SERVER, wr, &sr->d->iew, NULL, x, y, z, 6.0F, 0, 1, NULL,
                    sr_end_blast_query, b);
    expl_exclude_ie = prev_exclude;
    expl_placed_by_player = prev_placed;
    sr->end_blast_igniter = prev_igniter;

    /* inside a TNT's or a bed's blast the caller orders what the pool gained */
    if (!sr->end_blast_tail)
        for (int j = items_before; j < sr->d->iew.n; ++j) sr_order_push(sr, 0, ie_ent_at(sr->d->iew.slot[j]));

    struct server_player *p = sr->player;
    if (p != NULL && p->dimension == 1 && !p->sv.removed)
    {
        double dx = p->e.pos_x - x, dy = p->e.pos_y - y, dz = p->e.pos_z - z;
        if (dx * dx + dy * dy + dz * dz < 4096.0)
        {
            sr->has_s27 = 1;
            sr->s27_x += b->kx;
            sr->s27_y += b->ky;
            sr->s27_z += b->kz;
        }
    }
}

/* expl_run_extras over the End's lists (sr_end_blast_query: the living pool,
 * the crystals, the dragon and its parts, the player), for the blasts made
 * outside a crystal: a primed TNT's and a bed's. igniter: the player lit the
 * TNT (setExplosionSource's attacker). The caller's pass or phase orders what
 * the item pool gained; knock gets the player's field_77288_k entry. */
void serverreplay_end_run_blast(struct serverreplay *sr, jrand *wr, double x, double y, double z, float power,
                                int flaming, int smoking, int igniter, double knock[3])
{
    struct sr_end_blast *b ENV_LOCAL = envstack_zeroed(sizeof *b);
    b->sr = sr;
    int prev_igniter = sr->end_blast_igniter, prev_tail = sr->end_blast_tail;

    sr->end_blast_igniter = igniter;
    sr->end_blast_tail = 1;
    expl_run_extras(&sr->sky.world, &SR_DET(sr), DET_SERVER, wr, &sr->d->iew, NULL, x, y, z, power, flaming,
                    smoking, NULL, sr_end_blast_query, b);
    sr->end_blast_igniter = prev_igniter;
    sr->end_blast_tail = prev_tail;
    knock[0] = b->kx;
    knock[1] = b->ky;
    knock[2] = b->kz;
}

/* EntityTNTPrimed.explode in the End: worldObj.createExplosion(this, x, y, z,
 * 4.0F, true) over the End's lists (tnt.c's blast reaches only the living
 * pool and the player). The item pass that ticked the TNT orders the drops
 * and the chained TNT. */
void serverreplay_end_tnt_explode(struct serverreplay *sr, struct ie_ent *tnt)
{
    struct sr_world *ws = sr_dim_ws(sr, 1);
    jrand *wr = sr->in_entity_tick ? &sr->d->anw.iew.world_rand.r : &ST_RAND(&ws->st);
    const struct ie_ent *prev_exclude = expl_exclude_ie;
    int prev_placed = expl_placed_by_player;
    double x = tnt->e.pos_x, y = tnt->e.pos_y, z = tnt->e.pos_z, k[3];

    expl_exclude_ie = tnt;
    expl_placed_by_player = tnt->tnt_by_player;
    serverreplay_end_run_blast(sr, wr, x, y, z, 4.0F, 0, 1, tnt->tnt_by_player, k);
    expl_exclude_ie = prev_exclude;
    expl_placed_by_player = prev_placed;

    /* WorldServer.newExplosion's S27 to the player within 64 blocks */
    struct server_player *p = sr->player;
    if (p != NULL && p->dimension == 1 && !p->sv.removed)
    {
        double dx = p->e.pos_x - x, dy = p->e.pos_y - y, dz = p->e.pos_z - z;
        if (dx * dx + dy * dy + dz * dz < 4096.0)
        {
            sr->has_s27 = 1;
            sr->s27_x += k[0];
            sr->s27_y += k[1];
            sr->s27_z += k[2];
        }
    }
}

/* The living pool's projectiles against the crystals and the dragon's parts
 * (Entity.canBeCollidedWith; the dragon's own box is not): each reached
 * through its chunk and section. */
static int sr_end_query_other(struct ie_world *iew, struct aabb box, void **out, struct aabb *boxes, int cap)
{
    struct serverreplay *sr = sr_end_ctx;
    (void)iew;
    if (sr == NULL || sr->here != 1) return 0;
    struct dragon_state *d = &sr->end->d;
    int n = 0;

    for (int i = 0; i < d->n_crystals && n < cap; ++i)
    {
        struct dragon_crystal_state *c = &d->crystals[i];
        if (!c->in_world) continue;
        struct aabb cb = sr_crystal_box(c);
        int cx = (int)floor(c->x) >> 4, cy = (int)floor(c->y / 16.0), cz = (int)floor(c->z) >> 4;
        if (!aabb_intersects(&cb, &box) || !sr_chunk_in_search(cx, cy, cz, &box)) continue;
        out[n] = c;
        boxes[n++] = cb;
    }

    if (sr->end->dragon_listed && endfight_dragon_listed_near(d, &box))
        for (int i = 0; i < 7 && n < cap; ++i)
        {
            struct aabb pb = sr_part_box(&d->part[i]);
            if (!aabb_intersects(&pb, &box)) continue;
            out[n] = &d->part[i];
            boxes[n++] = pb;
        }

    return n;
}

/* A projectile's entity hit on a crystal or a part. The arrow's whole hit
 * (EntityArrow.onUpdate: the damage from its speed, the critical roll, the
 * attack, the bowhit pitch and setDead); a throwable's attackEntityFrom
 * (causeThrownDamage, 0 but 3 on a blaze). */
static void sr_end_other_hit(struct ie_world *iew, struct ie_ent *pr, void *other)
{
    struct serverreplay *sr = sr_end_ctx;
    struct dragon_state *d = &sr->end->d;
    struct dragon_crystal_state *c = NULL;
    int part = -1;
    (void)iew;

    if ((char *)other >= (char *)d->crystals && (char *)other < (char *)(d->crystals + DRAGON_MAX_CRYSTALS))
        c = other;
    else
        part = (int)((struct dragon_part_state *)other - d->part);

    float amount = 0.0F;
    if (pr->kind == IE_ARROW)
    {
        double speed = sqrt(pr->e.motion_x * pr->e.motion_x + pr->e.motion_y * pr->e.motion_y +
                            pr->e.motion_z * pr->e.motion_z);
        double dmg = (double)(float)speed * pr->arrow_damage;
        int v = (int)dmg;
        if (dmg > (double)v) ++v;
        if (pr->is_critical) v += det_rng_int_n(&pr->rand, v / 2 + 2);
        amount = (float)v;
    }
    /* causeFireballDamage: the large fireball's 6.0F, the small one's 5.0F */
    else if (pr->kind == IE_LARGE_FIREBALL) amount = 6.0F;
    else if (pr->kind == IE_SMALL_FIREBALL) amount = 5.0F;

    /* the source's entity: the shooter (the player's arrows and throws) */
    int by_player = pr->shooter_is_player || (sr->player != NULL && pr->shooting_entity == lv_ref(lv_get(sr->player_livh)));

    if (c != NULL)
    {
        if (!c->dead)
        {
            c->health = 0;
            c->dead = 1;
            sr_end_crystal_explode(sr, c->x, c->y, c->z);
        }
    }
    else if (part >= 0 && part < 7)
    {
        if (by_player && sr->player != NULL)
        {
            d->player_x = sr->player->e.pos_x;
            d->player_y = sr->player->e.pos_y;
            d->player_z = sr->player->e.pos_z;
        }
        /* onDeath's addToPlayerScore: the mobKills counter */
        if (dragon_attack_part(d, part, amount, by_player ? 1 : 0) && by_player && sr->player != NULL)
            surv_add_stat(sr->player, STAT_MOB_KILLS, 1);
    }

    if (pr->kind == IE_ARROW)
    {
        /* attackEntityFrom was true for both: the bowhit pitch, then setDead */
        (void)det_rng_float(&pr->rand);
        pr->is_dead = 1;
    }
}

void serverreplay_end_crystal_hit(struct serverreplay *sr, struct dragon_crystal_state *c)
{
    if (c->dead) return;
    c->health = 0;
    c->dead = 1;
    sr_end_crystal_explode(sr, c->x, c->y, c->z);
}

/* The collidable entities of the fh pool in a projectile's entity pass
 * (ie_can_be_collided_with's other half): a falling block while alive
 * (EntityFallingBlock.canBeCollidedWith), a painting or an item frame
 * always (EntityHanging's), after the End's crystals and parts. */
#define sr_pick_ctx (nw_env->serverreplay.pick_ctx)

static int sr_query_other(struct ie_world *iew, struct aabb box, void **out, struct aabb *boxes, int cap)
{
    struct serverreplay *sr = sr_pick_ctx;
    if (sr == NULL) return 0;
    int n = sr->here == 1 && sr->end != NULL ? sr_end_query_other(iew, box, out, boxes, cap) : 0;
    FH_QUERY_LIST(f);
    int m = fh_entities_in_box(&sr->d->fhw, box, f, FH_MAX_ENTITIES);
    for (int i = 0; i < m; ++i)
    {
        if (f[i]->kind == FH_FALLING && f[i]->is_dead) continue;
        if (n >= cap) list_full("collidable entities in one projectile's path", cap);
        out[n] = f[i];
        boxes[n++] = f[i]->e.bounding_box;
    }
    /* the primed TNT in the item pool (EntityTNTPrimed.canBeCollidedWith
     * answers !isDead; items and orbs answer false) */
    for (int i = 0; i < sr->d->iew.n; ++i)
    {
        ie_ent *t = ie_ent_at(sr->d->iew.slot[i]);
        if (t->kind != IE_TNT || t->is_dead || !aabb_intersects(&t->e.bounding_box, &box)) continue;
        if (n >= cap) list_full("collidable entities in one projectile's path", cap);
        out[n] = t;
        boxes[n++] = t->e.bounding_box;
    }
    return n;
}

/* A projectile's hit on a falling block or a hanging entity. The arrow's
 * whole entity hit (EntityArrow.onUpdate: the damage and its critical draw,
 * the burning arrow's setFire(5), then attackEntityFrom: a hanging entity
 * breaks and the arrow dies after its bowhit pitch, a falling block only
 * takes setBeenAttacked and the arrow turns back); a throwable's
 * attackEntityFrom (the snowball, the egg and the pearl attack, the potion
 * and the bottle do not); a fireball's onImpact attack (causeFireballDamage;
 * the explosion follows in projectile.c). */
static void sr_other_hit(struct ie_world *iew, struct ie_ent *pr, void *other)
{
    struct serverreplay *sr = sr_pick_ctx;

    /* a primed TNT: Entity.attackEntityFrom's setBeenAttacked and false,
     * so an arrow (after its damage roll and the burning arrow's
     * setFire(5)) turns back, and a throwable only marks it */
    for (int i = 0; sr != NULL && i < sr->d->iew.n; ++i)
    {
        if (ie_ent_at(sr->d->iew.slot[i]) != other) continue;
        ie_ent *tnt = other;
        if (pr->kind == IE_ARROW)
        {
            double speed = sqrt(pr->e.motion_x * pr->e.motion_x + pr->e.motion_y * pr->e.motion_y +
                                pr->e.motion_z * pr->e.motion_z);
            double dmg = (double)(float)speed * pr->arrow_damage;
            int v = (int)dmg;
            if (dmg > (double)v) ++v;
            if (pr->is_critical) (void)det_rng_int_n(&pr->rand, v / 2 + 2);
            if (pr->e.fire > 0 && tnt->e.fire < 100) tnt->e.fire = 100;
            tnt->e.velocity_changed = 1;
            pr->e.motion_x *= -0.10000000149011612;
            pr->e.motion_y *= -0.10000000149011612;
            pr->e.motion_z *= -0.10000000149011612;
            pr->rotation_yaw += 180.0F;
            pr->prev_yaw += 180.0F;
            pr->ticks_in_air = 0;
        }
        else if (pr->kind == IE_SNOWBALL || pr->kind == IE_EGG || pr->kind == IE_ENDER_PEARL ||
                 pr->kind == IE_LARGE_FIREBALL || pr->kind == IE_SMALL_FIREBALL)
        {
            tnt->e.velocity_changed = 1;
        }
        return;
    }

    fh_ent *t = NULL;
    for (int i = 0; sr != NULL && i < sr->d->fhw.n; ++i)
        if (fh_ent_at(sr->d->fhw.slot[i]) == other) { t = other; break; }
    if (t == NULL)
    {
        sr_end_other_hit(iew, pr, other);
        return;
    }

    int hanging = t->kind != FH_FALLING;
    if (pr->kind == IE_ARROW)
    {
        double speed = sqrt(pr->e.motion_x * pr->e.motion_x + pr->e.motion_y * pr->e.motion_y +
                            pr->e.motion_z * pr->e.motion_z);
        double dmg = (double)(float)speed * pr->arrow_damage;
        int v = (int)dmg;
        if (dmg > (double)v) ++v;
        if (pr->is_critical) (void)det_rng_int_n(&pr->rand, v / 2 + 2);
        if (pr->e.fire > 0 && t->e.fire < 100) t->e.fire = 100;
        if (hanging)
        {
            serverreplay_break_hanging(sr, t);
            (void)det_rng_float(&pr->rand); /* the bowhit pitch */
            pr->is_dead = 1;
        }
        else
        {
            t->e.velocity_changed = 1;
            pr->e.motion_x *= -0.10000000149011612;
            pr->e.motion_y *= -0.10000000149011612;
            pr->e.motion_z *= -0.10000000149011612;
            pr->rotation_yaw += 180.0F;
            pr->prev_yaw += 180.0F;
            pr->ticks_in_air = 0;
        }
        return;
    }
    if (pr->kind == IE_SNOWBALL || pr->kind == IE_EGG || pr->kind == IE_ENDER_PEARL ||
        pr->kind == IE_LARGE_FIREBALL || pr->kind == IE_SMALL_FIREBALL)
    {
        if (hanging)
        {
            serverreplay_break_hanging(sr, t);
            /* EntitySmallFireball.onImpact: the landed hit on an entity not
             * immune to fire sets it burning */
            if (pr->kind == IE_SMALL_FIREBALL && t->e.fire < 100) t->e.fire = 100;
        }
        else t->e.velocity_changed = 1;
    }
}

/* Entity.applyEntityCollision with the dragon as the receiver of a pushable
 * entity's collide (EntityLivingBase.collideWithEntity): both move. */
static void sr_dragon_push(struct dragon_state *d, struct entity *e, struct living *l)
{
    double v2 = e->pos_x - d->x;
    double v4 = e->pos_z - d->z;
    double v6 = fabs(v2) > fabs(v4) ? fabs(v2) : fabs(v4);

    if (v6 < 0.009999999776482582) return;

    v6 = (double)(float)sqrt(v6);
    v2 /= v6;
    v4 /= v6;
    double v8 = 1.0 / v6;
    if (v8 > 1.0) v8 = 1.0;
    v2 *= v8;
    v4 *= v8;
    v2 *= 0.05000000074505806;
    v4 *= 0.05000000074505806;
    d->mx -= v2;
    d->mz -= v4;
    /* Entity.addVelocity on the dragon: isAirBorne runs its tracker entry's
     * update block */
    d->is_air_borne = 1;
    e->motion_x += v2;
    e->motion_z += v4;
    if (l != NULL) l->is_air_borne = 1;
}

/* The dragon before living q in World.getEntitiesWithinAABBExcludingEntity's
 * order: chunk x, then z, then the section, then the list's insertion. */
static int sr_dragon_before(const struct dragon_state *d, const struct living *q)
{
    int dcy = d->chunk_y < 0 ? 0 : (d->chunk_y > 15 ? 15 : d->chunk_y);
    int qcy = q->chunk_coord_y < 0 ? 0 : (q->chunk_coord_y > 15 ? 15 : q->chunk_coord_y);
    return d->chunk_x != q->chunk_coord_x ? d->chunk_x < q->chunk_coord_x
         : d->chunk_z != q->chunk_coord_z ? d->chunk_z < q->chunk_coord_z
         : dcy != qcy ? dcy < qcy : d->chunk_stamp < q->e.chunk_stamp;
}

static int sr_end_mob_collide_before(void *ctx, const struct living *q)
{
    struct serverreplay *sr = ctx;
    struct dragon_state *d = serverreplay_dragon(sr);
    return d != NULL && !d->dead && sr_dragon_before(d, q);
}

/* A living of the End's pool reaching the dragon in its collide box. */
static void sr_end_mob_collide(void *ctx, struct living *l, const struct aabb *box)
{
    struct serverreplay *sr = ctx;
    struct dragon_state *d = serverreplay_dragon(sr);

    if (d == NULL || d->dead || l == lv_get(sr->end->shadowh) || !endfight_dragon_listed_near(d, box)) return;

    struct aabb db = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
    if (aabb_intersects(&db, box)) sr_dragon_push(d, &l->e, l);
}

/* World.updateEntities for the dragon or a crystal at index k of the End's
 * pass: updateEntity (updateEntityWithOptionalForce's guard: an entity whose
 * 32-block square holds an unloaded chunk is left alone), then the removal of
 * a dead one. 1 when the entry left the order. */
static int sr_end_update(struct serverreplay *sr, struct sr_world *w, int k)
{
    struct sr_end *e = sr->end;
    struct dragon_state *d = &e->d;

    if (sr->d->ents[k].pool == SR_POOL_CRYSTAL)
    {
        struct dragon_crystal_state *c = sr_ent_p(sr, &sr->d->ents[k]);

        if (!c->dead)
        {
            int ex = (int)floor(c->x), ez = (int)floor(c->z);

            /* EntityEnderCrystal.onUpdate's fire is the End world's
             * whether or not the dragon is loaded */
            if (d->world == NULL) d->world = w->st.w;
            if (world_entity_area_loaded(w->st.w, ex, ez))
                dragon_crystal_tick(d, (int)(c - d->crystals));
        }

        if (!c->dead) return 0;

        c->in_world = 0;
        sr_end_track(sr, (int)(c - d->crystals));
        sr_order_remove(sr, k);
        return 1;
    }

    if (!d->dead)
    {
        int ex = (int)floor(d->x), ez = (int)floor(d->z);

        if (world_entity_area_loaded(w->st.w, ex, ez))
        {
            /* World.playerEntities as the dragon reads it: the server player
             * while it is in the End and not taken out by the credits */
            struct server_player *p = sr->player;

            d->has_player = p != NULL && p->dimension == 1 && !p->conquered_end && !p->limbo;
            if (p != NULL && d->has_player)
            {
                d->player_x = p->e.pos_x;
                d->player_y = p->e.pos_y;
                d->player_z = p->e.pos_z;
                d->player_min_y = p->e.bounding_box.min_y;
            }
            int an_before = sr->d->anw.n, ie_before = sr->d->iew.n;
            dragon_tick(d);
            /* a mob the bite killed drops into the End's lists: those join
             * the pass's tail, as spawnEntityInWorld appends them in Java */
            for (int j = an_before; j < sr->d->anw.n; ++j) sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[j]));
            for (int j = ie_before; j < sr->d->iew.n; ++j) sr_order_push(sr, 0, ie_ent_at(sr->d->iew.slot[j]));
            endfight_dragon_chunk(d, 0);
            sr_end_track(sr, -1);
            sr_dragon_shadow_sync(sr);
        }
    }

    if (!d->dead) return 0;

    e->dragon_listed = 0;
    sr_end_track(sr, -1);
    sr_dragon_shadow_sync(sr);
    sr_order_remove(sr, k);
    return 1;
}

/* One world's WorldServer.tick and World.updateEntities, for a world with
 * loaded chunks. Its dimension's state must be here. */
/* The world tick's phases (phase.h), in WorldServer.tick's and then
 * World.updateEntities' order; servertick_tick runs S1 to S6 between them. */

/* Before WorldServer.tick: the active set, the player, and the hooks the
 * servertick phases call back into the replay through. */
static void sr_world_begin(struct serverreplay *sr, struct sr_world *w, int player_here,
                           double px, double py, double pz)
{
    int *active = sr->active_scratch;

    if (player_here) sr_player_scene(sr);
    else if (sr->player != NULL && sr->player_livh != 0)
    {
        /* one EntityPlayerMP: a mob of another world that targets it reads
         * its live health (a death in the player's world, processPlayer's
         * fall, drops the target in isEntityAlive's test next tick) */
        lv_get(sr->player_livh)->health = sr->player->sv.health;
        lv_get(sr->player_livh)->is_dead = sr->player->sv.dead;
    }
    int n = player_here ? sr_active_set_memo(sr, px, pz, active, &w->active_cap, &w->active_memo) : 0;

    if (player_here) servertick_set_player(&w->st, px, py, pz);
    /* World.func_147456_g's playerCheckLight draws need a player in
     * playerEntities, and a world without one has an empty active set */
    w->st.no_player = !player_here;
    w->st.no_sky = w->dim != 0;
    /* World.isRaining for Entity.isWet in moveEntity during this world's tick */
    entity_set_raining((double)(w->st.prev_raining_strength + (w->st.raining_strength - w->st.prev_raining_strength) * 1.0F) > 0.2);
    servertick_set_active(&w->st, active, n);
    if (sr->mobs_enabled && player_here)
    {
        /* the player's own box, which is not the first when a crystal
         * joined the End's list before the player came */
        if (sr->d->spawner.player_box < 0) spawner_set_player(&sr->d->spawner, px, py, pz);
        else
        {
            struct sp_box *b = &sr->d->spawner.boxes[sr->d->spawner.player_box];
            sr->d->spawner.player_x = px;
            sr->d->spawner.player_y = py;
            sr->d->spawner.player_z = pz;
            b->min_x = px - 0.3;
            b->max_x = px + 0.3;
            b->min_y = py;
            b->max_y = py + 1.8;
            b->min_z = pz - 0.3;
            b->max_z = pz + 0.3;
        }
        if (sr->natural_spawning)
        {
            w->st.on_spawner = sr_spawner_tick;
            w->st.on_spawner_ctx = sr;
        }
    }
    w->st.on_spawn = sr_on_spawn;
    w->st.on_spawn_ctx = sr;
    w->st.arrows_in = sr_arrows_in;
    w->st.arrows_ctx = sr;
    w->st.plate_tick = sr_plate_tick;
    w->st.plate_tick_ctx = sr;
    w->st.portal_spawn = sr_portal_spawn;
    w->st.portal_spawn_ctx = sr;
    w->st.w->on_plate = sr_plate_collide;
    w->st.w->on_plate_ctx = sr;
    w->st.on_unload_step = sr_unload_step;
    w->st.on_unload_step_ctx = sr;
    /* areAllPlayersSleeping reads this world's own players */
    if (player_here)
    {
        w->st.on_sleep_check = sleep_all_asleep;
        w->st.on_wake_all = sleep_wake_all;
        w->st.on_sleep_ctx = sr;
    }
    /* the overworld's siege runs after its village collection, below */
    w->st.siege_deferred = w->dim == 0;
    w->st.siege_muster = sr_siege_muster;
    w->st.siege_muster_ctx = sr;
}

/* S9: WorldServer.tick's tail after func_147456_g: the player manager's
 * sweep, a playerless Nether's unload marks, the portal cache and the
 * village collection (the overworld's siege follows as SG). */
static void sr_world_tail(struct serverreplay *sr, struct sr_world *w, int t)
{
    w->st.on_sleep_check = NULL;
    /* PlayerManager.updatePlayerInstances' processChunk sweep, after the
     * block pass (the chunk sends it also makes are the tick's own) */
    cl_sweep(sr->d->cl, &w->pm_sweep, w->st.total_time);
    w->st.on_wake_all = NULL;
    w->st.on_sleep_ctx = NULL;
    w->st.on_spawn = NULL;
    w->st.on_spawn_ctx = NULL;
    w->st.on_unload_step = NULL;
    w->st.on_unload_step_ctx = NULL;
    w->st.on_spawner = NULL;
    w->st.on_spawner_ctx = NULL;
    w->st.arrows_in = NULL;
    w->st.arrows_ctx = NULL;
    w->st.plate_tick = NULL;
    w->st.plate_tick_ctx = NULL;
    w->st.portal_spawn = NULL;
    w->st.portal_spawn_ctx = NULL;
    /* the plate and tripwire hook stays on the world: the player's own
     * moves (processPlayer in the network tick) reach onEntityCollidedWithBlock
     * too */

    /* PlayerManager.updatePlayerInstances: a world whose provider cannot
     * respawn (the Nether) and that has no player queues every loaded chunk
     * (ChunkProviderServer.unloadAllChunks, in loadedChunks order) */
    if (w->dim != 0 && serverreplay_player_dim(sr) != w->dim) cl_save_marks(sr->d->cl, t);

    /* WorldServer.tick's tail: worldTeleporter.removeStalePortalLocations */
    teleporter_remove_stale_locations(w->dim == -1 ? &sr->tp_hell : w->dim == 1 ? &sr->tp_end : &sr->tp_over,
                                      w->st.total_time);

    /* WorldServer.tick's tail (after updatePlayerInstances, before the
     * entity pass): the villages. They tick on the world's own Random, exactly
     * as the live server does: their draws go through the an_world's view of
     * it (Village.tick's door sweep and golem roll), so the stream is handed
     * over and back as the entity pass after them does per entity
     * (sr_tick_an). The siege's roll is servertick's village_siege_tick. */
    /* every WorldServer ticks its own collection (the Nether's and the
     * End's too: a villager through a portal feeds them) */
    if (sr->mobs_enabled && (w->dim == 0 || sr->d->anw.village_collection == &sr->d->vc))
    {
        struct village_collection *vc = w->dim == 0 ? &sr->vc : &sr->d->vc;
        sr->d->anw.iew.world_rand.r = ST_RAND(&w->st);
        /* a chunk the door scan reads in (addUnassignedWoodenDoorsAroundToNewDoorsList)
         * is provided, and its structure offers reseed this same World.rand */
        jrand *prev_pop_rand = populate_world_rand;
        populate_world_rand = &sr->d->anw.iew.world_rand.r;
        int an_before = sr->d->anw.n;
        vc_tick(vc);
        /* a golem Village.tick spawned joins loadedEntityList's tail */
        for (int j = an_before; j < sr->d->anw.n; ++j) sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[j]));
        populate_world_rand = prev_pop_rand;
        ST_RAND(&w->st) = sr->d->anw.iew.world_rand.r;
    }

    /* WorldServer.tick's last step: func_147488_Z */
    sr_send_block_events(sr, w->st.w);
}

/* World.updateEntities' removal of the dead EntityPlayerMP: out of
 * loadedEntityList (it stays in playerEntities until the respawn) and out
 * of its chunk's list. */
static void sr_player_unload(struct serverreplay *sr)
{
    sr->player->sv.removed = 1;
    sr->player_removed_tick = sr->current_tick;
    combat_player_removed(sr);
    if (sr->player_livh != 0 && lv_get(sr->player_livh)->added_to_chunk)
    {
        an_chunk_remove(&sr->d->anw, an_deref(sr->player_enth), lv_get(sr->player_livh)->chunk_coord_y);
        lv_get(sr->player_livh)->added_to_chunk = 0;
    }
}

/* World.updateEntities' setup: the pass's roles, hooks and world view; 0
 * when the world has gone 1200 passes without a player and skips them. */
static int sr_pass_begin(struct serverreplay *sr, struct sr_world *w, int player_here,
                         double px, double py, double pz, struct blockcb_env *previous_env)
{
    /* World.updateEntities, the world's entity pass, right after the
     * world tick and before the next world's tick. One order over both
     * pools, index-based so an entity appended during the pass (a
     * landing's drop) is visited in the same pass, exactly as Java's
     * live loadedEntityList walk does. Its own block writes (a falling
     * block's first tick) go through the same chain. */
    det_set_role(&SR_DET(sr), DET_SERVER);
    sr->d->iew.role = DET_SERVER;
    sr->d->fhw.role = DET_SERVER;
    sr->d->nextra = 0;
    /* a player setDead by onDeathUpdate leaves loadedEntityList in this pass
     * (it stays in playerEntities until the respawn) */
    /* (a dead rider whose vehicle lives and still carries it is skipped by
     * World.updateEntities' riding test; the vehicle's update lets go of it
     * and the player's own entry removes it: in this pass when the vehicle
     * comes first (sr_entity_pass), else in the next) */
    int rides = 0;
    if (player_here && sr->player != NULL && sr->player->sv.dead)
    {
        struct living *veh = sr->player->ridingh != 0 ? lv_get(sr->player->ridingh) : NULL;
        rides = veh != NULL && !veh->is_dead && sr->player_livh != 0 &&
                lv_get(veh->ridden_by_entity) == lv_get(sr->player_livh);
    }
    if (sr->player != NULL && !sr->player->sv.removed &&
        ((player_here && sr->player->sv.dead && !rides) ||
         /* World.removeEntity's setDead after the exit portal */
         (sr->player->conquered_end && serverreplay_player_dim(sr) == w->dim)))
        sr_player_unload(sr);
    w->st.w->on_block = sr_on_block;
    w->st.w->on_block_ctx = sr;
    sr->d->anw.portal_travel = sr_living_portal_travel;
    sr->d->anw.post_update = sr_living_post_update;
    sr->d->anw.portal_travel_ctx = sr;
    sr->d->iew.portal_travel = sr_ie_portal_travel;
    sr->d->fhw.portal_travel = sr_fh_portal_travel;
    sr->d->fhw.post_update = sr_fh_post_update;
    sr->d->fhw.portal_ctx = sr;
    sr->d->iew.post_update = sr_ie_post_update;
    sr->d->iew.portal_ctx = sr;
    sr->d->anw.iew.portal_travel = sr_ie_portal_travel;
    sr->d->anw.iew.post_update = sr_ie_post_update;
    sr->d->anw.iew.portal_ctx = sr;
    randomtick_tick_rand(&ST_RAND(&w->st));
    /* Block.onEntityWalking: the redstone ore lights up on World.rand */
    living_set_walking_world(&ST_RAND(&w->st));
    randomtick_tick_det(&SR_DET(sr), DET_SERVER);
    randomtick_tick_drop_sink(sr_entity_drop, sr);
    *previous_env = nw_env->blockcb.env;
    nw_env->blockcb.env.world_rand = &ST_RAND(&w->st);
    nw_env->blockcb.env.det = &SR_DET(sr);
    nw_env->blockcb.env.role = DET_SERVER;
    nw_env->blockcb.env.item_drop = sr_block_env_drop;
    nw_env->blockcb.env.item_spill = sr_block_env_spill;
    nw_env->blockcb.env.spill_role = DET_SERVER;
    nw_env->blockcb.env.ctx = sr;
    /* Block.onEntityWalking's redstone ore draws World.rand, which the pass
     * hands each entity through the an_world's view */
    living_set_walking_world(&sr->d->anw.iew.world_rand.r);
    if (sr->d->anw.user_data != NULL && sr->player != NULL)
    {
        /* the ghasts' player: EntityPlayerMP as the pass sees it */
        struct gh_world *gw = sr->d->anw.user_data;
        struct gh_player *gp = &gw->player;
        gw->an = &sr->d->anw;
        /* a fireball's blast also reaches the item pool and the falling and
         * hanging entities */
        gw->blast_other = tnt_blast_other;
        gw->blast_other_ctx = sr;
        gw->player_livh = sr->player_livh;
        gp->entity_id = sr->player_entity_id;
        gp->pos_x = sr->player->e.pos_x;
        gp->pos_y = sr->player->e.pos_y;
        gp->pos_z = sr->player->e.pos_z;
        gp->motion_x = sr->player->e.motion_x;
        gp->motion_y = sr->player->e.motion_y;
        gp->motion_z = sr->player->e.motion_z;
        gp->rotation_yaw = sr->player->rotation_yaw;
        gp->rotation_pitch = sr->player->rotation_pitch;
        gp->health = sr->player->sv.health;
        gp->is_dead = sr->player->sv.dead;
    }
    if (sr->mobs_enabled)
    {
        sr->d->anw.has_player = player_here;
        sr_pick_ctx = sr;
        sr->d->anw.iew.query_other = sr_query_other;
        sr->d->anw.iew.on_other_hit = sr_other_hit;
        sr->d->anw.no_players = !player_here;
        /* the live player: wakeAllPlayers (WorldServer.tick's head) may
         * have moved it off its bed since the tick began */
        sr->d->anw.player_x = player_here && sr->player != NULL ? sr->player->e.pos_x : px;
        sr->d->anw.player_y = player_here && sr->player != NULL ? sr->player->e.pos_y : py;
        sr->d->anw.player_z = player_here && sr->player != NULL ? sr->player->e.pos_z : pz;
        sr->d->anw.player_held_item = 0;
        sr->d->anw.constructor_hook = sr_living_constructor;
        sr->d->anw.constructor_ctx = sr;
        sr->d->anw.track_spawn = sr_track_spawn;
        sr->d->anw.on_infect = sr_on_infect;
        sr->d->anw.on_convert = sr_on_convert;
        sr->d->anw.on_reinforce = sr_on_reinforce;
        sr->d->anw.achievement_hook = sr_player_achievement;
        sr->d->anw.collect_hook = sr_mob_collect;
        sr->d->anw.bred_ctx = sr;
        if (player_here && sr->player)
        {
            const struct surv_stack *held = &sr->player->sv.inv[sr->player->sv.current_item];
            if (held->count > 0) sr->d->anw.player_held_item = held->item;
        }
        sr->d->anw.world_time = w->st.world_time;
        sr->d->anw.skylight = w->st.skylight_subtracted;
        {
            /* World.isRaining: getRainStrength(1.0F) > 0.2 */
            float rs = w->st.prev_raining_strength + (w->st.raining_strength - w->st.prev_raining_strength) * 1.0F;
            sr->d->anw.raining = (double)rs > 0.2;
        }
    }

    /* WorldServer.updateEntities: a world with no player for 1200 passes
     * stops running its pass (entities, weather effects, tile entities) until
     * a player is back */
    int pass_runs = 1;
    if (!player_here) pass_runs = w->update_entity_tick++ < 1200;
    else w->update_entity_tick = 0;
    return pass_runs;
}

/* S10: World.updateEntities visits weatherEffects before the ordinary
 * loadedEntityList. A bolt made by updateBlocks ticks this row. */
static void sr_weather_effects(struct serverreplay *sr, struct sr_world *w, int pass_runs)
{
    for (int k = 0; pass_runs && k < sr->d->nbolts; ++k)
    {
        lightning_tick(&sr->d->bolts[k], w->st.w, !w->st.fire_rule_off,
                       serverreplay_lightning_strike, sr);
        if (sr->d->bolts[k].dead)
        {
            memmove(&sr->d->bolts[k], &sr->d->bolts[k + 1],
                    (size_t)(sr->d->nbolts - k - 1) * sizeof *sr->d->bolts);
            --sr->d->nbolts;
            --k;
        }
    }
}

/* S11: the loadedEntityList walk. */
static void sr_entity_pass(struct serverreplay *sr, struct sr_world *w, int t, int player_here,
                           int pass_runs)
{
    for (int k = 0; pass_runs && k < sr->d->nents; ++k)
    {
        if (sr->d->ents[k].pool == 3)
        {
            if (serverreplay_player_dim(sr) == w->dim) sr->pass_player_done = 1;
            /* a rider is updated by its vehicle's updateEntity (riding.c);
             * a dead or changed vehicle lets go of it here */
            if (sr->player != NULL && sr->player->ridingh != 0)
            {
                struct living *vehicle = lv_get(sr->player->ridingh);
                if (!vehicle->is_dead && lv_get(vehicle->ridden_by_entity) == lv_get(sr->player_livh)) continue;
                if (lv_get(vehicle->ridden_by_entity) == lv_get(sr->player_livh)) vehicle->ridden_by_entity = 0;
                sr->player->ridingh = 0;
                if (sr->player_livh) lv_get(sr->player_livh)->riding_entity = 0;
            }
            /* the dead rider its vehicle let go of earlier in this pass:
             * the entry's isDead test removes it */
            if (sr->player != NULL && sr->player->sv.dead && !sr->player->sv.removed &&
                serverreplay_player_dim(sr) == w->dim)
            {
                sr_player_unload(sr);
                continue;
            }
            /* EntityPlayerMP.onUpdate at the player's place in the list: its
             * S2Fs open this row's queue, then the queued chunks go out: the
             * player manager's stream (a v2 snapshot's join, sr_send_chunks)
             * and the queue a portal transfer made (sr_player_send_chunks) */
            /* (a dead player out of loadedEntityList gets no onUpdate: a
             * dev teleport's chunks never go out) */
            if (sr->player != NULL && !sr->player->sv.removed)
            {
                /* World.updateEntityWithOptionalForce's guard holds for the
                 * player too: after a teleport into chunks that are not
                 * loaded yet, onUpdate (the invulnerability and hurt
                 * countdowns, the chunk sends) waits for them */
                int ex = (int)floor(sr->player->e.pos_x), ez = (int)floor(sr->player->e.pos_z);
                if (world_entity_area_loaded(w->st.w, ex, ez))
                    serverreplay_player_on_update(sr, w->st.w);
            }
            continue;
        }
        if (sr->d->ents[k].pool == SR_POOL_DRAGON || sr->d->ents[k].pool == SR_POOL_CRYSTAL)
        {
            if (sr_end_update(sr, w, k)) --k;
            continue;
        }
        if (sr->d->ents[k].pool == 2)
        {
            struct an_ent *en = sr_ent_p(sr, &sr->d->ents[k]);
            if (en->is_living && lv_get(lv_get(en->livh)->riding_entity) != NULL)
            {
                struct living *vehicle = lv_get(lv_get(en->livh)->riding_entity);
                /* a rider is updated by its vehicle's updateEntity */
                if (!vehicle->is_dead && lv_get(vehicle->ridden_by_entity) == lv_get(en->livh)) continue;
                vehicle->ridden_by_entity = 0;
                lv_get(en->livh)->riding_entity = 0;
            }
            if ((nw_env->cfg.sr_negative & 8) && !sr->negative_mob_skipped && en->is_living)
            {
                int ex = (int)floor(lv_get(en->livh)->e.pos_x);
                int ez = (int)floor(lv_get(en->livh)->e.pos_z);
                if (world_entity_area_loaded(w->st.w, ex, ez))
                {
                    sr->negative_mob_skipped = 1;
                    continue;
                }
            }
            struct living *rider = en->is_living ? lv_get(lv_get(en->livh)->ridden_by_entity) : NULL;
            /* updateEntityWithOptionalForce runs the rider's tail inside its
             * guard: a vehicle left alone this tick (a chunk of its 32-block
             * neighbourhood unloaded, or dead) leaves its rider alone too */
            if (rider != NULL)
            {
                const struct living *v = lv_get(en->livh);
                int vx = (int)floor(v->e.pos_x), vz = (int)floor(v->e.pos_z);
                if (v->is_dead || !world_entity_area_loaded(w->st.w, vx, vz)) rider = NULL;
            }
            /* every entity's own moveEntity walk (func_145775_I) presses
             * plates and trips wires: a villager stepping onto the table
             * plate in its house, a mob's drop landing on a wire */
            w->st.w->on_plate = sr_plate_collide;
            w->st.w->on_plate_ctx = sr;
            int gone = sr_tick_an(sr, w, t, player_here, en, 0);
            w->st.w->on_plate = NULL;
            w->st.w->on_plate_ctx = NULL;
            if (gone >= 0) --k;
            /* updateEntityWithOptionalForce's tail: the rider, now */
            if (rider != NULL && rider == lv_get(sr->player_livh) && sr->player != NULL)
            {
                if (!sr->player->sv.dead && lv_get(sr->player->ridingh) == lv_get(en->livh))
                {
                    ride_server_update_entity(sr->player);
                    /* updateRiderPosition moved the one EntityPlayerMP: the
                     * entities after the vehicle read it there (an
                     * enderman's despawn distance) */
                    serverreplay_player_moved(sr);
                }
                else
                {
                    lv_get(en->livh)->ridden_by_entity = 0;
                    sr->player->ridingh = 0;
                    rider->riding_entity = 0;
                }
            }
            else if (rider != NULL)
            {
                struct living *vehicle = lv_get(rider->riding_entity);
                if (!rider->is_dead && vehicle != NULL && lv_get(vehicle->ridden_by_entity) == rider)
                {
                    struct an_ent *ren = NULL;
                    for (int i = 0; i < sr->d->anw.n; ++i)
                        if (an_ent_at(sr->d->anw.slot[i])->is_living && lv_get(an_ent_at(sr->d->anw.slot[i])->livh) == rider) ren = an_ent_at(sr->d->anw.slot[i]);
                    if (ren != NULL) sr_tick_an(sr, w, t, player_here, ren, 1);
                }
                else if (vehicle != NULL)
                {
                    vehicle->ridden_by_entity = 0;
                    rider->riding_entity = 0;
                }
            }
            continue;
        }
        /* World.updateEntityWithOptionalForce's guard: an entity whose
         * 32 block box holds a chunk that is not loaded is left alone
         * for the tick -- not even its lastTickPos moves. Java reaches
         * this for every entity in the pass, and the spawn-area chunks
         * leave corners of it unloaded. */
        const struct entity *en = sr_ent_p(sr, &sr->d->ents[k]);
        int ex = (int)floor(en->pos_x), ez = (int)floor(en->pos_z);

        int exist = world_entity_area_loaded(w->st.w, ex, ez);
        int pool = sr->d->ents[k].pool;

        /* the guard only skips the update: World.updateEntities still
         * removes a dead one (an item a neighbour's stack took in) */
        if (!exist && !(pool == 0 ? ((ie_ent *)sr_ent_p(sr, &sr->d->ents[k]))->is_dead : ((fh_ent *)sr_ent_p(sr, &sr->d->ents[k]))->is_dead)) continue;

        int items_before = sr->d->iew.n, an_before = sr->d->anw.n;
        /* an item's or a falling block's moveEntity walk trips wires and
         * presses the wooden plate (Sensitivity.everything) like a mob's */
        w->st.w->on_plate = sr_plate_collide;
        w->st.w->on_plate_ctx = sr;
        int removed = pool == 0
            ? ie_tick_one(&sr->d->iew, sr_ent_p(sr, &sr->d->ents[k]), t, NULL, 0, NULL)
            : fh_tick_one(&sr->d->fhw, sr_ent_p(sr, &sr->d->ents[k]), t, NULL, 0, NULL);
        w->st.w->on_plate = NULL;
        w->st.w->on_plate_ctx = NULL;

        /* a primed TNT's blast spawns drops and chained TNT into the item
         * pool, and the loot of the mobs it kills into the living pool:
         * they join the pass's order at its tail, in the order they
         * spawned (a sheep's wool between the player's drops and the
         * blast's blocks) */
        if (pool == 0)
        {
            int i = items_before - removed, j = an_before;
            while (i < sr->d->iew.n || j < sr->d->anw.n)
            {
                ie_ent *ie = i < sr->d->iew.n ? ie_ent_at(sr->d->iew.slot[i]) : NULL;
                struct an_ent *an = j < sr->d->anw.n ? an_ent_at(sr->d->anw.slot[j]) : NULL;
                if (an == NULL || (ie != NULL && sr_order_id(0, ie) < sr_order_id(2, an)))
                {
                    sr_order_push(sr, 0, ie);
                    ++i;
                }
                else
                {
                    /* one entry per entity: a hook that ordered its own */
                    int listed = 0;
                    for (int q = sr->d->nents - 1; q >= 0 && !listed; --q)
                        listed = sr->d->ents[q].pool == 2 && sr_ent_p(sr, &sr->d->ents[q]) == an;
                    if (!listed) sr_order_push(sr, 2, an);
                    ++j;
                }
            }
        }

        if (removed)
        {
            sr_order_remove(sr, k);
            --k;
        }
    }
}

/* S12: the tile entity walk. */
static void sr_tile_pass(struct serverreplay *sr, struct sr_world *w, int pass_runs)
{
    /* World.updateEntities walks tileEntityList after loadedEntityList. Keep
     * the block and Random hooks installed: a furnace swap can write blocks
     * and its neighbour callbacks use World.rand, and the chest lid sounds
     * draw it. */
    float celestial[2] = {0.0F, servertick_celestial_radians(&w->st)};
    tileticks_set_time(w->st.world_time, w->st.total_time,
                       w->st.skylight_subtracted, celestial);
    struct tt_env env = {sr, sr_tile_spawner, sr_chest_viewers, sr_closest_player, &SR_DET(sr)};
    tileticks_set_env(&env);
    tileticks_set_world_rand(&ST_RAND(&w->st));
    if (pass_runs) tileticks_pass(w->st.w);

    tileticks_set_world_rand(NULL);
    tileticks_set_env(NULL);
}

static void sr_pass_end(struct sr_world *w, const struct blockcb_env *previous_env)
{
    w->st.w->on_block = NULL;
    w->st.w->on_block_ctx = NULL;
    randomtick_tick_rand(NULL);
    randomtick_tick_drop_sink(NULL, NULL);
    living_set_walking_world(NULL);
    nw_env->blockcb.env = *previous_env;
    living_set_walking_world(NULL);
    entity_set_raining(0);
}

/* PlayerInstance.flagChunkForUpdate over what changed since the last flush
 * (the world tick's writes so far, the last tick's entity-pass and network
 * writes, a dev op's at the tick's head), then sendChunkUpdate's packets as
 * they are built now: each chunk the client holds (a chunk still in its
 * send queue gets none: sendToAllPlayersWatchingChunk skips it) with its
 * cells' block and metadata, or its flagged sections held as they are. */
static void sr_flush_release(struct serverreplay *sr)
{
    for (int k = 0; k < sr->nflush; ++k)
        for (int sec = 0; sec < 16; ++sec)
            if (sr->flush[k].bands[sec]) chunk_sec_drop(sr->flush[k].bands[sec]);
    sr->nflush = 0;
    sr->flush_ready = 0;
}

static void sr_flush_capture(struct serverreplay *sr, struct sr_world *w)
{
    struct world *client = sr->client, *sw = w->st.w;
    const struct servertick *st = &w->st;
    int ndev = w->dim == sr->dev_writes_dim ? sr->ndev_writes : 0;
    int total = st->nwrites + sr->d->nlate + ndev;

    sr_flush_release(sr);
    for (int i = 0; i < total; ++i)
    {
        const struct st_write *wr = i < st->nwrites ? &st->writes[i]
            : i < st->nwrites + sr->d->nlate ? &sr->d->late[i - st->nwrites]
            : &sr->dev_writes[i - st->nwrites - sr->d->nlate];
        if (wr->pad & 0x10) continue;   /* another dimension's write */
        if (wr->pad & ST_WRITE_UNSENT) continue;
        int x = wr->x, y = wr->y, z = wr->z;
        if (y < 0 || y >= 256) continue;
        if (!world_chunk(client, x >> 4, z >> 4) || !world_chunk(sw, x >> 4, z >> 4)) continue;

        int k = sr->nflush - 1;
        while (k >= 0 && (sr->flush[k].cx != x >> 4 || sr->flush[k].cz != z >> 4)) --k;
        if (k < 0)
        {
            if (sr->nflush == SR_FLUSH_MAX)
            {
                fprintf(stderr, "serverreplay: a flush over %d chunks\n", SR_FLUSH_MAX);
                abort();
            }
            k = sr->nflush++;
            memset(&sr->flush[k], 0, sizeof sr->flush[k]);
            sr->flush[k].cx = x >> 4;
            sr->flush[k].cz = z >> 4;
        }
        struct sr_flush_chunk *f = &sr->flush[k];
        f->secs |= 1 << (y >> 4);
        uint16_t cell = (uint16_t)((x & 15) << 12 | (z & 15) << 8 | y);
        int j = 0;
        while (j < f->n && f->cells[j] != cell) ++j;
        if (j == f->n && f->n < 64) f->cells[f->n++] = cell;
    }
    for (int k = 0; k < sr->nflush; ++k)
    {
        struct sr_flush_chunk *f = &sr->flush[k];
        const struct chunk *src = world_chunk(sw, f->cx, f->cz);
        if (f->n < 64)
            for (int j = 0; j < f->n; ++j)
            {
                int x = f->cx * 16 + (f->cells[j] >> 12 & 15), z = f->cz * 16 + (f->cells[j] >> 8 & 15);
                int y = f->cells[j] & 255;
                f->ids[j] = (int16_t)world_get_block(sw, x, y, z);
                f->metas[j] = (uint8_t)world_get_meta(sw, x, y, z);
            }
        else
            /* S21PacketChunkData(chunk, false, flags): the sections the
             * server holds */
            for (int sec = 0; sec < 16; ++sec)
                if ((f->secs & 1 << sec) && (src->mask & 1 << sec)) f->bands[sec] = chunk_sec_hold(src, sec);
    }
    sr->flush_ready = 1;
    sr->flush_split = st->nwrites;
}

/* NetHandlerPlayClient.handleRespawn's new WorldClient: the old world's
 * flush never reaches it. */
void serverreplay_set_client(struct serverreplay *sr, struct world *client)
{
    sr_flush_release(sr);
    sr->client = client;
}

static void sr_tick_world(struct serverreplay *sr, struct sr_world *w, int t, int player_here,
                          double px, double py, double pz)
{
    /* a live server world: its block events queue (world_block_event) */
    w->st.w->bev_on = 1;
    PHASE_SUB(PS_WBEGIN, sr_world_begin(sr, w, player_here, px, py, pz));
    servertick_tick(&w->st, t);
    /* thePlayerManager.updatePlayerInstances, after func_147456_g */
    if (sr->client != NULL && w->dim == sr->client->dim) sr_flush_capture(sr, w);
    PHASE_RUN(PH_S9, sr_world_tail(sr, w, t));
    /* WorldServer.tick's tail: villageSiegeObj.tick after the villages */
    if (w->dim == 0) PHASE_RUN(PH_SG, servertick_village_siege(&w->st));
    /* WorldServer.tick's last step: func_147488_Z delivers the block events
     * the previous network tick and tile pass queued */
    PHASE_RUN(PH_BE, world_block_events_run(w->st.w));
    w->st.siege_deferred = 0;
    w->st.siege_muster = NULL;
    w->st.siege_muster_ctx = NULL;

    struct blockcb_env previous_env;
    int pass_runs = sr_pass_begin(sr, w, player_here, px, py, pz, &previous_env);
    PHASE_RUN(PH_S10, sr_weather_effects(sr, w, pass_runs));
    PHASE_RUN(PH_S11, sr_entity_pass(sr, w, t, player_here, pass_runs));
    PHASE_RUN(PH_S12, sr_tile_pass(sr, w, pass_runs));
    sr_pass_end(w, &previous_env);
}

/* NT: WorldServer's EntityTracker.updateTrackedEntities, right after its
 * updateEntities and before the network tick: a dev op's dirty health
 * leaves as the S1C the player's own entry sends now, then the combat
 * mirrors. */
static void sr_track(struct serverreplay *sr, int player_here, int player_world)
{
    if (player_here && sr->player != NULL && sr->player->dev_s1c_index > 0)
    {
        struct server_player *sp = sr->player;
        struct s2c_queue *q = s2c_out();
        if (sp->dev_s1c_index <= q->n && q->q[sp->dev_s1c_index - 1].kind == PK_S1C)
            q->q[sp->dev_s1c_index - 1].f0 = sp->sv.health;
        sp->dev_s1c_index = 0;
    }
    if (player_world) combat_server_finish(sr);
    else combat_server_quiet(sr);
}

/* SE: WorldServer.tick for a world without chunks; a world without chunks
 * has no instances, but its manager's processChunk mark still moves. */
static void sr_tick_empty(struct sr_world *w)
{
    servertick_tick_empty(&w->st, w->dim == 0);
    cl_sweep(NULL, &w->pm_sweep, w->st.total_time);
}

/* S13: the world's writes, this tick's, folded into Rows.blkHash. */
static void sr_fold_writes(struct serverreplay *sr, struct sr_world *w, int loaded)
{
    int nw;
    const struct st_write *ws = servertick_writes(&w->st, &nw);
    uint64_t h = sr->blk_hash;
    int total = nw;

    if (loaded) total += sr->d->nextra;

    for (int k = 0; k < total; ++k)
    {
        if ((nw_env->cfg.sr_negative & 2) && k == 0) continue; /* the negative check */

        const struct st_write *b = k < nw ? &ws[k] : &sr->d->extra[k - nw];
        int bdim = (b->pad & 0x10) ? (b->pad & 0x0F) - 2 : w->dim;
        h = (h ^ (uint64_t)(int64_t)bdim) * FNV_PRIME;
        h = (h ^ (uint64_t)(int64_t)b->x) * FNV_PRIME;
        h = (h ^ (uint64_t)(int64_t)b->y) * FNV_PRIME;
        h = (h ^ (uint64_t)(int64_t)b->z) * FNV_PRIME;
        h = (h ^ (uint64_t)(int64_t)b->id) * FNV_PRIME;
        h = (h ^ (uint64_t)(int64_t)b->meta) * FNV_PRIME;
    }

    sr->blk_hash = h;
    sr->blk_count += total;
}

void serverreplay_tick(struct serverreplay *sr, int t, double px, double py, double pz)
{
    sr->current_tick = t;
    sr->pass_player_done = 0;
    /* MinecraftServer.tick's ++tickCounter comes before the worlds tick */
    if (sr->player) sr->player->server_tick = t;
    /* the twin's rand is the player's Entity.rand: the draws the last
     * network tick made after the player's potion pass (a swim or hurt
     * sound, the drowning bubbles) are the stream a mob's hit continues */
    if (sr->player != NULL && sr->player_livh != 0) lv_get(sr->player_livh)->rand = sr->player->sv.erand;
    sr->blk_count = sr->dev_block_count;
    sr->nxw = 0;

    /* Dev teleport runs at the head of this server tick. The caller's
     * previous-row coordinates predate it; WorldServer reads the live player. */
    if (sr->player)
    {
        px = sr->player->e.pos_x;
        py = sr->player->e.pos_y;
        pz = sr->player->e.pos_z;
    }

    int pdim = serverreplay_player_dim(sr);

    for (int i = 0; i < sr->nworlds; ++i)
    {
        struct sr_world *w = &sr->w[i];
        int loaded = w->dim == 0 || (w->dim == -1 && sr->hell_live) || (w->dim == 1 && sr->end_live);

        PHASE_WORLD(w->dim);
        servertick_clear(&w->st);

        if (loaded)
        {
            serverreplay_enter(sr, w->dim);
            /* playerEntities: a player who went through the exit portal has
             * left it (it stays in the player manager until the respawn) */
            int player_here = pdim == w->dim && !(sr->player != NULL && (sr->player->conquered_end || sr->player->limbo || sr->player->offworld));
            sr_tick_world(sr, w, t, player_here, px, py, pz);
            PHASE_RUN(PH_NT, sr_track(sr, player_here, pdim == w->dim));
        }
        else if (!(nw_env->cfg.sr_negative & 1)) PHASE_RUN(PH_SE, sr_tick_empty(w));
        else continue;

        PHASE_RUN(PH_S13, sr_fold_writes(sr, w, loaded));
    }

    /* MinecraftServer.updateTimeLightAndEntities' network tick, after every
     * world: the C03 the client sent during the previous tick.
     * PlayerManager.updateMountedMovingPlayer over the position it carried
     * (cl_c03, run by the caller after the player's packets). */

    serverreplay_enter(sr, pdim);
    sr->native_entities = sr_count_entities(sr);
}

/* MinecraftServer.tick's t % 900 save, after updateTimeLightAndEntities (so
 * after the network tick's C03 and its player-manager move): every world
 * marks each loaded chunk that has no player instance for unloading. The
 * saves themselves change nothing a row carries. */
void serverreplay_save_marks(struct serverreplay *sr, int t)
{
    if ((t + 1) % 900 != 0) return;

    int pdim = serverreplay_player_dim(sr);

    serverreplay_enter(sr, 0);
    cl_save_marks(sr->d->cl, t);

    if (sr->hell_live)
    {
        serverreplay_enter(sr, -1);
        cl_save_marks(sr->d->cl, t);
    }

    if (sr->end_live)
    {
        serverreplay_enter(sr, 1);
        cl_save_marks(sr->d->cl, t);
    }

    serverreplay_enter(sr, pdim);
}

/* -------------------------------------------------- the chunkload engine */

/* ChunkProviderServer.unloadQueuedChunks, at the head of WorldServer.tick: the
 * engine calls world_unload_chunk itself, and every unload it makes comes back
 * through sr_cl_event. */
static void sr_unload_step(void *ctx, int t)
{
    struct serverreplay *sr = ctx;
    cl_unload_step(sr->d->cl, t);
}

/* The engine's events. Only the unloads need the replay's own bookkeeping: the
 * chunk is already gone from the world, so its entities leave the pools and
 * the pass's order, exactly as World.updateEntities drops the ones
 * Chunk.onChunkUnload handed to World.unloadEntities. Loads cannot happen
 * (cl_set_no_load); marks and unmarks are the queue's own state. */
static void sr_on_chunk(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;
    populate_offer_chunk(&sr->pop, cx, cz);
}

static void sr_hell_populate(struct serverreplay *sr, int cx, int cz)
{
    struct chunk *c = world_chunk(&sr->hell.world, cx, cz);
    if (c == NULL || c->terrain_populated) return;
    struct blockcb_env old_env = nw_env->blockcb.env;
    /* World.rand as it stands: inside an entity's update of the Nether's
     * pass the pass hands it that entity (sr_tick_an's view) */
    jrand *wr = sr->in_entity_tick && sr->here == -1 ? &sr->d->anw.iew.world_rand.r : &ST_RAND(&sr->w[1].st);
    jrand *old_world_rand = populate_world_rand;
    det_state *old_det = populate_det;
    nw_env->blockcb.env.world_rand = wr;
    nw_env->blockcb.env.det = &SR_DET(sr);
    nw_env->blockcb.env.role = DET_SERVER;
    nw_env->blockcb.env.item_drop = sr_block_env_drop;
    nw_env->blockcb.env.item_spill = sr_block_env_spill;
    nw_env->blockcb.env.spill_role = DET_SERVER;
    nw_env->blockcb.env.ctx = sr;
    populate_world_rand = wr;
    populate_det = &SR_DET(sr);
    /* the lava springs' immediate updates draw World.rand and schedule
     * against the world's time, whoever asked for the chunk */
    ticks_set_rand(wr);
    ticks_set_total_time(sr_dim_ws(sr, 0)->st.total_time);   /* DerivedWorldInfo: the overworld's clock */
    struct randomtick_env old_rt;
    randomtick_tick_save(&old_rt);
    randomtick_tick_rand(wr);
    randomtick_tick_det(&SR_DET(sr), DET_SERVER);
    randomtick_tick_drop_sink(sr_entity_drop, sr);
    populate_150809_p(&sr->hell, c);
    ticks_set_fall_instantly(1);
    populate_hell_structures(&sr->hell, cx, cz);
    populate_hell_lava(&sr->hell, cx, cz);
    populate_hell_fire(&sr->hell, cx, cz);
    populate_hell_glow1(&sr->hell, cx, cz);
    populate_hell_glow2(&sr->hell, cx, cz);
    populate_hell_brown(&sr->hell, cx, cz);
    populate_hell_red(&sr->hell, cx, cz);
    populate_hell_quartz(&sr->hell, cx, cz);
    populate_hell_hidden(&sr->hell, cx, cz);
    ticks_set_fall_instantly(0);
    randomtick_tick_load(&old_rt);
    populate_world_rand = old_world_rand;
    populate_det = old_det;
    nw_env->blockcb.env = old_env;
}

static void sr_hell_on_chunk_(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;
    struct world *w = &sr->hell.world;
    populate_hell_offer_chunk(&sr->hell, cx, cz);
    if (world_chunk_loaded(w, cx + 1, cz + 1) &&
        world_chunk_loaded(w, cx, cz + 1) && world_chunk_loaded(w, cx + 1, cz))
        sr_hell_populate(sr, cx, cz);
    if (world_chunk_loaded(w, cx - 1, cz) && world_chunk_loaded(w, cx - 1, cz + 1) &&
        world_chunk_loaded(w, cx, cz + 1))
        sr_hell_populate(sr, cx - 1, cz);
    if (world_chunk_loaded(w, cx, cz - 1) && world_chunk_loaded(w, cx + 1, cz - 1) &&
        world_chunk_loaded(w, cx + 1, cz))
        sr_hell_populate(sr, cx, cz - 1);
    if (world_chunk_loaded(w, cx - 1, cz - 1) && world_chunk_loaded(w, cx, cz - 1) &&
        world_chunk_loaded(w, cx - 1, cz))
        sr_hell_populate(sr, cx - 1, cz - 1);
}

static void sr_hell_on_chunk(void *ctx, int cx, int cz)
{
    PHASE_SUB(PS_POP, sr_hell_on_chunk_(ctx, cx, cz));
}

static void sr_dragon_wing_push(void *ctx, const double box[6], double cx, double cz);
static void sr_dragon_head_bite(void *ctx, const double box[6]);

static int sr_dragon_shadow_attacked(void *ctx, int source, float amount)
{
    (void)ctx;
    (void)source;
    (void)amount;
    return 0;
}

/* The shadow in step with the dragon. */
static void sr_dragon_shadow_sync(struct serverreplay *sr)
{
    const struct dragon_state *d = &sr->end->d;
    struct living *s = lv_get(sr->end->shadowh);

    if (s == NULL) return;
    s->e.pos_x = d->x;
    s->e.pos_y = d->y;
    s->e.pos_z = d->z;
    s->e.motion_x = d->mx;
    s->e.motion_y = d->my;
    s->e.motion_z = d->mz;
    s->e.bounding_box = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
    s->rotation_yaw = d->yaw;
    s->health = d->health;
    s->is_dead = d->dead || !sr->end->dragon_listed;
}

/* EntityDragon.onDeathUpdate's XP: EntityXPOrb.getXPSplit's pieces, each a
 * new EntityXPOrb at the dragon spawned into the End's list (the pass visits
 * it this tick). */
static void sr_dragon_spawn_xp(void *ctx, double x, double y, double z, int amount)
{
    static const int splits[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3, 1};
    struct serverreplay *sr = ctx;

    sr->d->iew.role = DET_SERVER;
    while (amount > 0)
    {
        int s = 1;
        for (size_t i = 0; i < sizeof splits / sizeof splits[0]; ++i)
            if (amount >= splits[i]) { s = splits[i]; break; }
        amount -= s;
        ie_ent *en = ie_spawn_orb(&sr->d->iew, x, y, z, s);
        if (en == NULL) return;
        ie_added_to_world(&sr->d->iew, en);
        sr_order_push(sr, 0, en);
    }
}

/* The dragon's view of the End's entities, and theirs of it. */
static void sr_end_hooks(struct serverreplay *sr)
{
    sr->end->d.spawn_xp = sr_dragon_spawn_xp;
    sr->end->d.spawn_xp_ctx = sr;
    sr->end->d.wing_push = sr_dragon_wing_push;
    sr->end->d.head_bite = sr_dragon_head_bite;
    sr->end->d.world_ctx = sr;
    if (sr->end->shadowh == 0)
    {
        det_state fake;
        det_init(&fake);
        sr->end->shadowh = lv_ref(living_alloc());
        living_init(lv_get(sr->end->shadowh), &sr->sky.world, GK_GHAST, &fake);
        det_free(&fake);
        lv_get(sr->end->shadowh)->external_attack = sr_dragon_shadow_attacked;
        lv_get(sr->end->shadowh)->external_attack_ctx = sr;
        lv_get(sr->end->shadowh)->dimension = 1;
    }
    struct living *s = lv_get(sr->end->shadowh);
    s->entity_id = sr->end->d.entity_id;
    s->e.width = 16.0F;
    s->e.height = 8.0F;
    s->is_dead = 0;
    sr_dragon_shadow_sync(sr);
}

/* The End spawner's view of the fight: the dragon's box (16 wide, 8 high,
 * from its feet) and each crystal's (2 cubed around its centre). Both prevent
 * spawning; only the dragon, an IMob, counts toward the monster cap. */
static void sr_end_track(struct serverreplay *sr, int crystal)
{
    const struct dragon_state *d = &sr->end->d;
    struct entity e;

    memset(&e, 0, sizeof e);
    if (crystal < 0)
    {
        e.pos_x = d->x; e.pos_y = d->y; e.pos_z = d->z;
        e.bounding_box = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
        if (d->dead) spawner_track(&sr->d->spawner, d->entity_id, NULL);
        else spawner_register_living(&sr->d->spawner, SP_DRAGON, d->entity_id, &e);
        return;
    }

    const struct dragon_crystal_state *c = &d->crystals[crystal];
    e.pos_x = c->x; e.pos_y = c->y; e.pos_z = c->z;
    e.bounding_box = aabb_make(c->x - 1.0, c->y - 1.0, c->z - 1.0, c->x + 1.0, c->y + 1.0, c->z + 1.0);
    if (c->dead) spawner_track(&sr->d->spawner, c->entity_id, NULL);
    else spawner_register_living(&sr->d->spawner, SP_CRYSTAL, c->entity_id, &e);
}

void serverreplay_end_listed(struct serverreplay *sr, int crystal)
{
    if (crystal < 0)
    {
        sr_end_hooks(sr);
        sr_order_push(sr, SR_POOL_DRAGON, &sr->end->d);
    }
    else sr_order_push(sr, SR_POOL_CRYSTAL, &sr->end->d.crystals[crystal]);
    sr_end_track(sr, crystal);
}

/* ChunkProviderEnd.populate: BiomeEndDecorator.func_150513_a over the End
 * World's rand (the pass's copy inside an entity's tick): the ores, which
 * place nothing, the spike and the EntityEnderCrystal it spawns on top, then
 * at chunk 0,0 the EntityDragon. The two join the End's loadedEntityList as
 * spawnEntityInWorld adds them, with their constructors' Det draws. */
static void sr_sky_populate(struct serverreplay *sr, int cx, int cz)
{
    struct chunk *c = world_chunk(&sr->sky.world, cx, cz);
    if (c == NULL || c->terrain_populated) return;
    if (sr->here != 1)
    {
        fprintf(stderr, "serverreplay: End chunk %d,%d populates away from the End's state\n", cx, cz);
        return;
    }
    struct sr_world *ws = sr_dim_ws(sr, 1);
    jrand *wr = sr->in_entity_tick ? &sr->d->anw.iew.world_rand.r : &ST_RAND(&ws->st);
    struct blockcb_env old_env = nw_env->blockcb.env;
    nw_env->blockcb.env.world_rand = wr;
    nw_env->blockcb.env.det = &SR_DET(sr);
    nw_env->blockcb.env.role = DET_SERVER;
    nw_env->blockcb.env.item_drop = sr_block_env_drop;
    nw_env->blockcb.env.item_spill = sr_block_env_spill;
    nw_env->blockcb.env.spill_role = DET_SERVER;
    nw_env->blockcb.env.ctx = sr;
    int role = det_role(&SR_DET(sr));
    det_set_role(&SR_DET(sr), DET_SERVER);

    populate_150809_p(&sr->sky, c);
    ticks_set_fall_instantly(1);
    sr->sky.end_rand = *wr;
    populate_end_ores(&sr->sky, cx, cz);
    spike_crystal_last.spawned = 0;
    populate_end_spike(&sr->sky, cx, cz);
    /* spawnEntityInWorld refuses a position whose chunk is not loaded */
    if (spike_crystal_last.spawned &&
        world_chunk_loaded(&sr->sky.world, (int)floor(spike_crystal_last.x) >> 4, (int)floor(spike_crystal_last.z) >> 4))
    {
        int i = endfight_spawn_crystal(sr->end, &SR_DET(sr), spike_crystal_last.x, spike_crystal_last.y,
                                       spike_crystal_last.z, spike_crystal_last.yaw);
        if (i >= 0)
        {
            sr_order_push(sr, SR_POOL_CRYSTAL, &sr->end->d.crystals[i]);
            sr_end_track(sr, i);
        }
    }
    populate_end_dragon(&sr->sky, cx, cz);
    if (end_dragon_last.spawned)
    {
        endfight_spawn_dragon(sr->end, &sr->sky.world, &SR_DET(sr), end_dragon_last.yaw);
        sr_end_hooks(sr);
        sr_order_push(sr, SR_POOL_DRAGON, &sr->end->d);
        sr_end_track(sr, -1);
    }
    *wr = sr->sky.end_rand;
    ticks_set_fall_instantly(0);

    det_set_role(&SR_DET(sr), role);
    nw_env->blockcb.env = old_env;
}

static void sr_sky_on_chunk_(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;
    struct world *w = &sr->sky.world;
    if (world_chunk_loaded(w, cx + 1, cz + 1) &&
        world_chunk_loaded(w, cx, cz + 1) && world_chunk_loaded(w, cx + 1, cz))
        sr_sky_populate(sr, cx, cz);
    if (world_chunk_loaded(w, cx - 1, cz) && world_chunk_loaded(w, cx - 1, cz + 1) &&
        world_chunk_loaded(w, cx, cz + 1))
        sr_sky_populate(sr, cx - 1, cz);
    if (world_chunk_loaded(w, cx, cz - 1) && world_chunk_loaded(w, cx + 1, cz - 1) &&
        world_chunk_loaded(w, cx + 1, cz))
        sr_sky_populate(sr, cx, cz - 1);
    if (world_chunk_loaded(w, cx - 1, cz - 1) && world_chunk_loaded(w, cx, cz - 1) &&
        world_chunk_loaded(w, cx - 1, cz))
        sr_sky_populate(sr, cx - 1, cz - 1);
}

static void sr_sky_on_chunk(void *ctx, int cx, int cz)
{
    PHASE_SUB(PS_POP, sr_sky_on_chunk_(ctx, cx, cz));
}

static void sr_sky_ensure_chunk(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;

    if (!sr->sky.world.no_generate) world_load_chunk(&sr->sky.world, cx, cz);
}

/* The End's region store, as the Nether's (sr_hell_provide). */
static struct chunk *sr_sky_provide(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;

    if (sr->foreign && sr->here != 1 && sr->foreign_from == 1) return sr_provide_home(sr, 1, cx, cz);
    if (sr->here != 1) return NULL;

    struct chunk *c = sr_saved_take(sr, cx, cz);

    if (c == NULL) return NULL;
    chunk_unpack(c);

    sr_tiles_loaded(&sr->sky.world, c);

    for (int i = 0; i < 256; ++i) c->precip[i] = -999;
    c->height_min = 0;
    memset(c->update_skylight_columns, 0, sizeof c->update_skylight_columns);
    c->gap_lighting_updated = 0;
    c->queued_light_checks = 4096;
    c->populated = 0;
    sr_restore_chunk_entities(sr, cx, cz);
    sr_dispenser_births(sr, c);
    ticks_set_total_time(sr_dim_ws(sr, 0)->st.total_time);   /* DerivedWorldInfo: the overworld's clock */
    sr_restore_chunk_ticks(sr, cx, cz);
    world_chunk_request(&sr->sky.world, CREQ_STORED, c->cx, c->cz, c, NULL);
    return c;
}

/* AnvilChunkLoader.writeChunkToNBT's entity half for a Nether chunk: its
 * living entities by section, in each section's insertion order
 * (writeToNBTOptional skips the dead, the players and a ridden vehicle, which
 * rides along inside its rider's "Riding" tag). The struct is kept whole as
 * the record of every field the NBT carries. */
static void sr_save_item(struct serverreplay *sr, const ie_ent *ie, int pool, int cx, int cz);
/* The non-living kinds a chunk save writes by their own NBT and a reload
 * rebuilds (sr_restore_arrow): Entity.writeToNBTOptional saves every one with
 * an EntityList name. EntityEgg has none (EntityList.java registers no
 * ThrownEgg), so an egg in flight is not saved and is gone after the reload;
 * the eye of ender is "EyeOfEnderSignal". */
static int sr_saves_projectile(int kind)
{
    return kind == IE_ARROW || kind == IE_SNOWBALL || kind == IE_ENDER_PEARL ||
           kind == IE_EXP_BOTTLE || kind == IE_POTION || kind == IE_SMALL_FIREBALL || kind == IE_LARGE_FIREBALL ||
           kind == IE_ENDER_EYE;
}

/* The End's own records (sr_end_save_entities) back into the End: a crystal
 * (pool 10) or the dragon (pool 8), each constructed anew. */
static void sr_restore_end_record(struct serverreplay *sr, struct sr_saved_entity *sv)
{
    sv->loaded = 1;

    int role = det_role(&SR_DET(sr));
    det_set_role(&SR_DET(sr), DET_SERVER);
    if (sv->pool == 10)
    {
        int k = endfight_spawn_crystal(sr->end, &SR_DET(sr), sv->ent.x, sv->ent.y, sv->ent.z, sv->ent.yaw);
        if (k >= 0)
        {
            sr->end->d.crystals[k].uuid_msb = sv->uuid_msb;
            sr->end->d.crystals[k].uuid_lsb = sv->uuid_lsb;
            sr_order_push(sr, SR_POOL_CRYSTAL, &sr->end->d.crystals[k]);
            sr_end_track(sr, k);
        }
    }
    else
    {
        endfight_reload_dragon(sr->end, &sr->sky.world, &SR_DET(sr), sv->dragon_copy);
        sr_end_hooks(sr);
        sr_order_push(sr, SR_POOL_DRAGON, &sr->end->d);
        sr_end_track(sr, -1);
    }
    det_set_role(&SR_DET(sr), role);
}

/* The End's own half of AnvilChunkLoader.writeChunkToNBT's entity lists:
 * the crystals and the dragon (writeToNBTOptional skips a dead one), kept as
 * records for the reload, which constructs them anew. */
static void sr_end_save_entities(struct serverreplay *sr, int cx, int cz)
{
    struct dragon_state *d = &sr->end->d;

    for (int i = 0; i < d->n_crystals; ++i)
    {
        const struct dragon_crystal_state *c = &d->crystals[i];

        if (!c->in_world || c->dead || ((int)floor(c->x) >> 4) != cx || ((int)floor(c->z) >> 4) != cz) continue;

        sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
        struct sr_saved_entity *sv = &sr->d->saved_ents[sr->d->nsaved_ents++];
        memset(sv, 0, sizeof *sv);
        sv->active.id = -1;
        sv->pool = 10;
        sv->cx = cx;
        sv->cz = cz;
        sv->section = (int)floor(c->y / 16.0);
        sv->seq = c->chunk_stamp;
        sv->ent.x = c->x;
        sv->ent.y = c->y;
        sv->ent.z = c->z;
        sv->ent.yaw = c->yaw;
        sv->uuid_msb = c->uuid_msb;
        sv->uuid_lsb = c->uuid_lsb;
    }

    if (sr->end->dragon_listed && !d->dead && d->chunk_x == cx && d->chunk_z == cz)
    {
        sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
        struct sr_saved_entity *sv = &sr->d->saved_ents[sr->d->nsaved_ents++];
        memset(sv, 0, sizeof *sv);
        sv->active.id = -1;
        sv->pool = 8;
        sv->cx = cx;
        sv->cz = cz;
        sv->section = d->chunk_y;
        sv->seq = d->chunk_stamp;
        struct dragon_state *copy = slab_take(sizeof *copy);
        *copy = *d;
        sv->dragon_copy = copy;
    }
}

/* Chunk.onChunkUnload's entity half for the End's two kinds. */
static void sr_end_unload_entities(struct serverreplay *sr, int cx, int cz)
{
    struct dragon_state *d = &sr->end->d;

    for (int i = 0; i < d->n_crystals; ++i)
    {
        struct dragon_crystal_state *c = &d->crystals[i];

        if (!c->in_world || ((int)floor(c->x) >> 4) != cx || ((int)floor(c->z) >> 4) != cz) continue;

        c->in_world = 0;
        spawner_track(&sr->d->spawner, c->entity_id, NULL);
        for (int k = 0; k < sr->d->nents; ++k)
            if (sr->d->ents[k].pool == SR_POOL_CRYSTAL && sr_ent_p(sr, &sr->d->ents[k]) == c) { sr_order_remove(sr, k); break; }
    }

    if (sr->end->dragon_listed && d->chunk_x == cx && d->chunk_z == cz)
    {
        sr->end->dragon_listed = 0;
        /* World.unloadEntities takes the EntityDragon out of the lists but
         * leaves it alive where it stopped: an enderman whose target it is
         * keeps facing that object, and a reload is a new EntityDragon.
         * The shadow stays as it is (not dead, not moving) for them; the
         * reload makes a new one (sr_end_hooks). */
        sr->end->shadowh = 0;
        spawner_track(&sr->d->spawner, d->entity_id, NULL);
        for (int k = 0; k < sr->d->nents; ++k)
            if (sr->d->ents[k].pool == SR_POOL_DRAGON) { sr_order_remove(sr, k); break; }
    }
}

/* ChunkProviderServer.provideChunk for the Nether (world.provide): a chunk
 * the region store holds comes back as AnvilChunkLoader builds it (a new
 * Chunk, so the transient caches start over), with its pending ticks,
 * inserted; else NULL and the generator makes it. */
static struct chunk *sr_hell_provide(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;

    if (sr->foreign && sr->here != -1 && sr->foreign_from == -1) return sr_provide_home(sr, -1, cx, cz);
    if (sr->here != -1) return NULL;

    struct chunk *c = sr_saved_take(sr, cx, cz);

    if (c == NULL) return NULL;
    chunk_unpack(c);

    sr_tiles_loaded(&sr->hell.world, c);

    for (int i = 0; i < 256; ++i) c->precip[i] = -999;
    c->height_min = 0;
    memset(c->update_skylight_columns, 0, sizeof c->update_skylight_columns);
    c->gap_lighting_updated = 0;
    c->queued_light_checks = 4096;
    c->populated = 0;
    sr_restore_chunk_entities(sr, cx, cz);
    sr_dispenser_births(sr, c);
    /* func_147446_b schedules against the world's time as of now, which a
     * load inside the spawner's walk reads before the tick body sets it; the
     * Nether's DerivedWorldInfo reads the overworld's clock, which a load
     * from the overworld's pass (a mob's portal travel) sees already
     * advanced */
    ticks_set_total_time(sr_dim_ws(sr, 0)->st.total_time);
    sr_restore_chunk_ticks(sr, cx, cz);
    world_chunk_request(&sr->hell.world, CREQ_STORED, c->cx, c->cz, c, NULL);
    /* safeLoadChunk's recreateStructures: ChunkProviderHell offers the
     * fortress candidates around the loaded chunk (MapGenBase's own Random) */
    populate_hell_offer_chunk(&sr->hell, cx, cz);
    return c;
}

/* The pass order's player entry. */
static int sr_order_find_player(const struct serverreplay *sr)
{
    for (int k = 0; k < sr->d->nents; ++k)
        if (sr->d->ents[k].pool == 3) return k;

    return -1;
}

void serverreplay_player_to_tail(struct serverreplay *sr)
{
    int k = sr_order_find_player(sr);

    /* the respawned player's spawnEntityInWorld: its chunk's list takes it
     * at the tail too (the next world begin adds it) */
    if (sr->player_livh != 0 && lv_get(sr->player_livh)->added_to_chunk)
    {
        an_chunk_remove(&sr->d->anw, an_deref(sr->player_enth), lv_get(sr->player_livh)->chunk_coord_y);
        lv_get(sr->player_livh)->added_to_chunk = 0;
    }

    /* a respawn in the world whose ghost unlisted the old player: the new
     * one joins the tail */
    if (k < 0 && sr->player_unlisted && sr->player != NULL)
    {
        sr->player_unlisted = 0;
        sr_order_push(sr, 3, sr->player);
        return;
    }
    if (k < 0) return;

    struct sr_ent e = sr->d->ents[k];
    sr_order_remove(sr, k);
    sr_order_push(sr, e.pool, sr_ent_p(sr, &e));
}

void serverreplay_order_players_only(struct serverreplay *sr)
{
    int k = sr_order_find_player(sr);
    struct sr_ent e = k >= 0 ? sr->d->ents[k] : (struct sr_ent){0, -1};

    sr->d->nents = 0;
    sr->d->initial_ent_count = 0;

    if (k >= 0) sr_order_push(sr, e.pool, sr_ent_p(sr, &e));
}

/* The action's hooks on the world the player is in: the block writes the
 * network tick's packets make, the drops they spawn, World.rand. */
static void sr_action_hooks(struct serverreplay *sr)
{
    struct servertick *st = &sr_dim_ws(sr, sr->here)->st;

    sr->action_world = st->w;
    randomtick_tick_rand(&ST_RAND(st));
    randomtick_tick_det(&SR_DET(sr), DET_SERVER);
    nw_env->blockcb.env.world_rand = &ST_RAND(st);
    nw_env->blockcb.env.det = &SR_DET(sr);
    nw_env->blockcb.env.role = DET_SERVER;
    nw_env->blockcb.env.item_drop = sr_block_env_drop;
    nw_env->blockcb.env.item_spill = sr_block_env_spill;
    nw_env->blockcb.env.spill_role = DET_SERVER;
    nw_env->blockcb.env.ctx = sr;
    ticks_set_rand(&ST_RAND(st));
    /* a Dev op at the tick's head or a network-tick placement schedules
     * against WorldInfo.getWorldTotalTime() as the last tick left it, before
     * servertick_tick publishes the next one */
    ticks_set_total_time(st->total_time);
    st->w->on_block = sr_action_block;
    st->w->on_block_ctx = sr;
    /* processPlayer's moveEntity walks the blocks too: the player presses a
     * plate or trips a wire from the network tick */
    st->w->on_plate = sr_plate_collide;
    st->w->on_plate_ctx = sr;
    /* World.isRaining for Entity.isWet in processPlayer's moveEntity */
    entity_set_raining((double)(st->prev_raining_strength + (st->raining_strength - st->prev_raining_strength) * 1.0F) > 0.2);
}

static void sr_action_unhook(struct serverreplay *sr)
{
    struct world *w = sr->action_world;

    if (w != NULL)
    {
        w->on_block = NULL;
        w->on_block_ctx = NULL;
        w->on_plate = NULL;
        w->on_plate_ctx = NULL;
    }
    entity_set_raining(0);
    sr_action_order_new(sr);
}

/* The network phase's new entities join the order and the tracker
 * (spawnEntityInWorld's trackEntity), the ones since the last call. */
static void sr_action_order_new(struct serverreplay *sr)
{
    for (int i = sr->action_first_item; i < sr->d->iew.n; ++i)
        sr_order_push(sr, 0, ie_ent_at(sr->d->iew.slot[i]));

    /* Player C02 attacks can kill a mob during the network phase. Its drops
     * are appended to an_world after World.updateEntities has already run. */
    for (int i = sr->action_an_n; i < sr->d->anw.n; ++i)
    {
        /* C03 can also load a chunk during this phase; its restored mobs
         * already entered the order in sr_restore_saved_entities. */
        int ordered = 0;
        for (int j = 0; j < sr->d->nents; ++j)
            if (sr->d->ents[j].pool == 2 && sr_ent_p(sr, &sr->d->ents[j]) == an_ent_at(sr->d->anw.slot[i]))
                { ordered = 1; break; }
        if (!ordered) sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[i]));
    }
    sr->action_first_item = sr->d->iew.n;
    sr->action_an_n = sr->d->anw.n;
}

/* NetHandlerPlayServer.setPlayerLocation: hasMoved off, the last position,
 * setPositionAndRotation (its angles wrapped by setRotation), the S08. */
static void sr_set_player_location(struct server_player *p, double x, double y, double z, float yaw, float pitch)
{
    p->has_moved = 0;
    p->last_pos_x = x;
    p->last_pos_y = y;
    p->last_pos_z = z;
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S08;
    pkt->f0 = x;
    pkt->f1 = y + 1.6200000047683716;
    pkt->f2 = z;
    pkt->f3 = yaw;
    pkt->f4 = pitch;
    entity_set_pos_rot(&p->e, x, y, z, &p->rotation_yaw, &p->rotation_pitch,
                       &p->prev_rotation_yaw, &p->prev_rotation_pitch,
                       fmodf(yaw, 360.0F), fmodf(pitch, 360.0F));
    /* the one EntityPlayerMP: the entities the pass updates after this (a
     * dismount inside the vehicle's update) read the new place, their
     * despawn distance too; its chunk section moves at its own update */
    struct serverreplay *sr = p->replay;
    if (sr != NULL && sr->player == p && sr->player_livh)
    {
        struct living *l = lv_get(sr->player_livh);
        l->e.pos_x = p->e.pos_x;
        l->e.pos_y = p->e.pos_y;
        l->e.pos_z = p->e.pos_z;
        l->e.bounding_box = p->e.bounding_box;
        l->rotation_yaw = p->rotation_yaw;
        l->rotation_pitch = p->rotation_pitch;
    }
}

void serverreplay_set_player_location(struct server_player *p, double x, double y, double z,
                                     float yaw, float pitch)
{
    sr_set_player_location(p, x, y, z, yaw, pitch);
}

/* ServerConfigurationManager.transferEntityToWorld's first half for the
 * player: the scaled place (x / 8 into the Nether, x * 8 out of it, the End's
 * entrance (100, 50, 0) into the End) set on the entity, then, alive,
 * from.updateEntityWithOptionalForce(entity, false). removePlayerEntityDangerously
 * left addedToChunk set, so a chunk other than the one it was filed in
 * takes it when that chunk is loaded in the old world. */
static void sr_player_ghost(struct serverreplay *sr, int from, int to)
{
    struct server_player *p = sr->player;
    struct living *l = sr->player_livh != 0 ? lv_get(sr->player_livh) : NULL;
    double x = p->e.pos_x, y = p->e.pos_y, z = p->e.pos_z;

    if (to == -1) { x /= 8.0; z /= 8.0; }
    else if (to == 0) { x *= 8.0; z *= 8.0; }
    else { x = 100.0; y = 50.0; z = 0.0; }

    int cx = (int)floor(x / 16.0), cy = (int)floor(y / 16.0), cz = (int)floor(z / 16.0);

    if (p->sv.dead || p->sv.health <= 0.0F) return;
    if (l != NULL && l->added_to_chunk && l->chunk_coord_x == cx && l->chunk_coord_y == cy && l->chunk_coord_z == cz)
        return;
    if (!world_chunk_loaded(sr_dim_world(sr, from), cx, cz)) return;
    for (int g = 0; g < sr->npghost; ++g)
        if (sr->pghost[g].dim == from && sr->pghost[g].cx == cx && sr->pghost[g].cz == cz)
        {
            sr->pghost[g].same = 1;
            return;
        }
    if (sr->npghost == (int)(sizeof sr->pghost / sizeof sr->pghost[0])) return;
    sr->pghost[sr->npghost++] = (struct sr_pghost){from, cx, cz, 1};
}

/* The ghost's chunk unloaded in the world being ticked (sr->here): World.
 * updateEntities' removeAll takes the live player out of this world's
 * loadedEntityList when it is here; for the live object itself, the removal
 * also takes it out of the chunk it is filed in. onEntityRemoved's
 * EntityTracker.removeEntityFromAllTrackingPlayers: every entry lets the
 * player go (equals by id) and the player's own entry goes, so only an
 * entity's own four-block check brings it back (as a new spawn). */
static void sr_player_ghost_unload(struct serverreplay *sr, int g)
{
    struct sr_pghost gh = sr->pghost[g];
    struct server_player *p = sr->player;

    sr->pghost[g] = sr->pghost[--sr->npghost];
    if (p == NULL || p->sv.removed || serverreplay_player_dim(sr) != sr->here || sr->player_unlisted) return;

    int k = sr_order_find_player(sr);
    if (k >= 0) sr_order_remove(sr, k);
    sr->player_unlisted = 1;
    if (gh.same && sr->player_livh != 0 && lv_get(sr->player_livh)->added_to_chunk)
    {
        an_chunk_remove(&sr->d->anw, an_deref(sr->player_enth), lv_get(sr->player_livh)->chunk_coord_y);
        lv_get(sr->player_livh)->added_to_chunk = 0;
    }
    combat_player_removed(sr);
}

/* ServerConfigurationManager.transferPlayerToDimension, reached from
 * Entity.onEntityUpdate's portal countdown inside processPlayer's
 * onUpdateEntity (EntityPlayerMP.travelToDimension). */
static void sr_transfer_player(struct serverreplay *sr, int to)
{
    struct server_player *p = sr->player;
    int from = p->dimension;
    int t = sr->current_tick;

    if (to != 0 && to != -1 && to != 1) return;

    /* the row's earlier packets' new entities (a C07's drop) were tracked
     * at their spawn, the player still in this world: their S0Es go ahead
     * of the S07 */
    if (sr->action_world != NULL) sr_action_order_new(sr);

    /* the new world's tracker takes the player with a fresh entry; the
     * DataWatcher's dirt is the entity's own (a change the old world's pass
     * did not send goes out with the new entry's first pass) */
    p->trk_ticks = 0;

    struct world *dest = sr_dim_world(sr, to);
    struct sr_world *dws = sr_dim_ws(sr, to);
    struct teleporter *tp = to == -1 ? &sr->tp_hell : to == 1 ? &sr->tp_end : &sr->tp_over;
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S07;
    pkt->i0 = to;
    if (sr->combat) sr->combat->s07_mark = 1 + sr->combat->npending;
    if (sr->pickobj) sr->pickobj->s07_mark = 1 + sr->pickobj->npend;

    /* removePlayerEntityDangerously: setDead (the cursor and the grids
     * spill into the old world, tracked there while the player is still in
     * its playerEntities: their S0Es follow the S07), then the player leaves
     * the old world's loadedEntityList, its chunk and playerEntities, and
     * its entries let it go; the new world's spawnEntityInWorld makes every
     * entry there try it */
    surv_server_set_dead(p);

    /* transferEntityToWorld's ghost (recorded here, before the removal
     * below forgets the chunk the player was filed in) */
    sr_player_ghost(sr, from, to);
    sr_action_unhook(sr);
    pickobj_server_player_left(sr);
    combat_player_removed(sr);
    if (sr->combat) sr->combat->player_watch_initialized = 2;
    if (sr->pickobj) sr->pickobj->player_watch_initialized = 2;
    p->dimension = to;
    int k = sr_order_find_player(sr);
    if (k >= 0) sr_order_remove(sr, k);
    if (sr->player_livh != 0 && lv_get(sr->player_livh)->added_to_chunk)
    {
        an_chunk_remove(&sr->d->anw, an_deref(sr->player_enth), lv_get(sr->player_livh)->chunk_coord_y);
        lv_get(sr->player_livh)->added_to_chunk = 0;
    }
    sr->d->anw.playerh = 0;
    sr->d->anw.has_player = 0;
    sr->d->iew.player = NULL;
    sr->d->anw.iew.player = NULL;
    struct cl *old_cl = sr->d->cl;

    /* transferEntityToWorld: the scaled position, placeInPortal (the search
     * loads and generates the destination), spawnEntityInWorld */
    serverreplay_enter(sr, to);
    if (to == -1) sr->hell_live = 1;
    if (to == 1) sr->end_live = 1;
    sr_action_hooks(sr);
    tp->world_time = sr_shared_clock(sr);
    /* the search's populations spawn their drops into the destination's
     * loadedEntityList ahead of the player */
    /* isEntityAlive (removePlayerEntityDangerously's setDead is undone):
     * a player that died in the portal is moved, but no portal is placed
     * and the new world's spawnEntityInWorld does not take it, so it is in
     * neither world's lists until the respawn */
    int alive = p->sv.health > 0.0F;
    sr->in_transfer = 1;
    /* transferEntityToWorld's placing half runs only when the old
     * dimension is not the End: out of a Nether portal lit in the End the
     * player keeps the scaled position (no portal search, no clamp) and is
     * never spawned into the destination (not in its loadedEntityList nor
     * its playerEntities; func_72375_a still files it in the player
     * manager) */
    int offworld = from == 1 && to != 1;
    if (offworld)
    {
        double sx = p->e.pos_x, sz = p->e.pos_z;
        if (to == -1) { sx /= 8.0; sz /= 8.0; }
        else { sx *= 8.0; sz *= 8.0; }
        p->e.pos_x = sx;
        p->e.pos_z = sz;
        p->portal.dimension = to;
    }
    else
        portal_transfer_entity(&p->e, &p->portal, from, to, p->e.world, dest, tp,
                               &p->rotation_yaw, &p->rotation_pitch, alive);
    p->offworld = offworld;
    sr->in_transfer = 0;
    p->e.world = dest;
    entity_set_position(&p->e, p->e.pos_x, p->e.pos_y, p->e.pos_z);
    p->e.prev_pos_x = p->e.pos_x;
    p->e.prev_pos_y = p->e.pos_y;
    p->e.prev_pos_z = p->e.pos_z;
    /* the twin's own world pointer too: canEntityBeSeen traces there */
    if (sr->player_livh != 0) lv_get(sr->player_livh)->e.world = lv_get(sr->player_livh)->world = dest;
    sr->player_unlisted = offworld;
    sr->action_first_item = sr->d->iew.n;
    sr->action_an_n = sr->d->anw.n;
    sr->action_moved = 1;
    if (alive)
    {
        if (!offworld) sr_order_push(sr, 3, p);
        sr->d->anw.playerh = offworld ? 0 : lv_ref(lv_get(sr->player_livh));
        sr->d->iew.player = &p->e;
        sr->d->anw.iew.player = &p->e;

        /* spawnEntityInWorld's EntityTracker.addEntityToTracker: every entry of
         * the new world tries the player, whose worldObj is still the old world
         * (setWorld comes last), so isPlayerWatchingThisChunk asks the old
         * world's player manager, which still holds it, at the entity's chunk
         * coordinates (a mob there gets its S0F now; seed-1 S9 row 2048) */
        if (!offworld)
        {
            sr->watch_cl = old_cl;
            combat_player_spawned(sr);
            sr->watch_cl = NULL;
        }
    }
    else
    {
        p->sv.removed = 1;
        p->limbo = 1;
        sr->player_removed_tick = t;
    }

    /* func_72375_a: the old player manager drops the player, the new one
     * takes it and loads its chunk */
    cl_logout(old_cl, t);
    cl_login(sr->d->cl, t, p->e.pos_x, p->e.pos_z);
    cl_replay_load(sr->d->cl, t, (int)p->e.pos_x >> 4, (int)p->e.pos_z >> 4, "transfer");

    /* EntityPlayerMP.loadedChunks: the old queue's chunks left with
     * removePlayer, addPlayer queued the new square, and
     * filterChunkLoadQueue ordered it */
    int side = 2 * cl_radius(sr->d->cl) + 1;
    if (sr->capsend < side * side * 2)
    {
        sr->send_queue = slab_grow(sr->send_queue, sizeof(int) * (size_t)sr->capsend, sizeof(int) * (size_t)side * side * 2);
        sr->capsend = side * side * 2;
    }
    sr->nsend = cl_send_order(sr->d->cl, p->e.pos_x, p->e.pos_z, sr->send_queue);
    sr->send_dim = to;

    sr_set_player_location(p, p->e.pos_x, p->e.pos_y, p->e.pos_z, p->rotation_yaw, p->rotation_pitch);

    /* updateTimeAndWeatherForPlayer (the S03: the overworld's time, which
     * DerivedWorldInfo gives every world, as the entity pass finds it),
     * syncPlayerInventory (the window items, then setPlayerHealthUpdated),
     * travelToDimension's own resets */
    p->s03_sent = 1;
    p->s03_rule = sr->w[0].st.daylight_cycle;
    p->s03_total = sr->w[0].st.total_time;
    p->s03_day = sr->w[0].st.world_time;
    surv_server_sync_inventory(p);
    p->sv.last_xp_total = -1;
    p->sv.last_health = -1.0F;
    p->sv.last_food = -1;
    surv.world_rand = &ST_RAND(&dws->st);
    /* theItemInWorldManager.setWorld(var5) */
    surv_dig_set_world(dest);
}

/* ------------------------------------------------ non-player portal travel */

/* The destination world's half of Entity.travelToDimension made inside
 * another world's entity pass: that dimension's state is entered (its
 * pools, its player manager and provider, its pending ticks) and its
 * World.rand is the one the pass hands the entity (the an_world's view), so
 * the placement, its chunk loads and the rest of the traveller's own update
 * draw and write in that world. Its block writes join the chain in call
 * order: collected here, then filed into the pass's own writes tagged with
 * their dimension (sr_foreign_end). */
static void sr_foreign_block(void *ctx, int x, int y, int z, int id, int meta)
{
    struct serverreplay *sr = ctx;

    if (sr->nxw == sr->capxw)
    {
        sr->capxw = sr->capxw ? sr->capxw * 2 : 1024;
        sr->xw = realloc(sr->xw, (size_t)sr->capxw * sizeof *sr->xw);
    }

    struct sr_xwrite *w = &sr->xw[sr->nxw++];
    w->dim = sr->here;
    w->w.x = x;
    w->w.y = y;
    w->w.z = z;
    w->w.id = (int16_t)id;
    w->w.meta = (uint8_t)meta;
    w->w.pad = 0;
}

static void sr_foreign_begin(struct serverreplay *sr, int to)
{
    struct sr_world *dws = sr_dim_ws(sr, to);

    sr->foreign_from = sr->here;
    sr->foreign_in_tick = sr->in_entity_tick;
    /* the placement's chunk loads join the destination's order themselves */
    sr->in_entity_tick = 0;
    sr->foreign_env = nw_env->blockcb.env;
    sr->foreign_pop_rand = populate_world_rand;
    sr->foreign_ticks_rand = ticks_get_rand();
    serverreplay_enter(sr, to);
    if (to == -1) sr->hell_live = 1;
    if (to == 1) sr->end_live = 1;
    sr->foreign_view_n = sr->d->anw.n;
    nw_env->blockcb.env.world_rand = &ST_RAND(&dws->st);
    nw_env->blockcb.env.det = &SR_DET(sr);
    nw_env->blockcb.env.role = DET_SERVER;
    nw_env->blockcb.env.item_drop = sr_block_env_drop;
    nw_env->blockcb.env.item_spill = sr_block_env_spill;
    nw_env->blockcb.env.spill_role = DET_SERVER;
    nw_env->blockcb.env.ctx = sr;
    populate_world_rand = &ST_RAND(&dws->st);
    ticks_set_rand(&ST_RAND(&dws->st));
    ticks_set_total_time(sr_shared_clock(sr));
    living_set_walking_world(&ST_RAND(&dws->st));
    sr->d->anw.world_time = dws->st.world_time;
    sr->d->anw.skylight = dws->st.skylight_subtracted;
    sr->d->anw.difficulty = dws->st.difficulty;
    /* a world that never ran a pass since the snapshot has none of the
     * pass's hooks on its pool yet */
    sr->d->anw.portal_travel = sr_living_portal_travel;
    sr->d->anw.post_update = sr_living_post_update;
    sr->d->anw.portal_travel_ctx = sr;
    sr->d->anw.constructor_hook = sr_living_constructor;
    sr->d->anw.constructor_ctx = sr;
    sr->d->iew.portal_travel = sr_ie_portal_travel;
    sr->d->fhw.portal_travel = sr_fh_portal_travel;
    sr->d->fhw.post_update = sr_fh_post_update;
    sr->d->fhw.portal_ctx = sr;
    sr->d->iew.post_update = sr_ie_post_update;
    sr->d->iew.portal_ctx = sr;
    sr->d->anw.iew.portal_travel = sr_ie_portal_travel;
    sr->d->anw.iew.post_update = sr_ie_post_update;
    sr->d->anw.iew.portal_ctx = sr;
    sr->xw_first = sr->nxw;
    dws->st.w->on_block = sr_foreign_block;
    dws->st.w->on_block_ctx = sr;
    sr->foreign = 1;
    /* a traveller arriving where the player is meets the one EntityPlayerMP:
     * the pass's copy of it starts from the player's state now */
    if (sr->player != NULL && sr->player_livh != 0 && serverreplay_player_dim(sr) == to) sr_player_scene(sr);
}

/* The rest of the traveller's update draws World.rand through its pool's
 * view, as the pass hands it every entity (sr_tick_an). */
static void sr_foreign_view(struct serverreplay *sr)
{
    struct sr_world *dws = sr_dim_ws(sr, sr->here);

    sr->d->anw.iew.world_rand.r = ST_RAND(&dws->st);
    nw_env->blockcb.env.world_rand = &sr->d->anw.iew.world_rand.r;
    populate_world_rand = &sr->d->anw.iew.world_rand.r;
    ticks_set_rand(&sr->d->anw.iew.world_rand.r);
    living_set_walking_world(&sr->d->anw.iew.world_rand.r);
    sr->in_entity_tick = sr->foreign_in_tick;
    sr->foreign_view_n = sr->d->anw.n;
    sr->foreign = 2;
}

static void sr_foreign_end(struct serverreplay *sr)
{
    sr->foreign_old = NULL;
    if (!sr->foreign) return;

    struct sr_world *dws = sr_dim_ws(sr, sr->here);
    struct sr_world *sws = sr_dim_ws(sr, sr->foreign_from);

    if (sr->foreign == 2) ST_RAND(&dws->st) = sr->d->anw.iew.world_rand.r;
    /* what the rest of the traveller's update loaded or spawned there (an
     * egg's hatchling) joins the destination's order now (the pass only
     * orders its own world's) */
    for (int i = sr->foreign_view_n; i < sr->d->anw.n; ++i)
    {
        int ordered = 0;
        for (int k = 0; k < sr->d->nents && !ordered; ++k)
            ordered = sr->d->ents[k].pool == 2 && sr_ent_p(sr, &sr->d->ents[k]) == an_ent_at(sr->d->anw.slot[i]);
        if (!ordered) sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[i]));
    }
    dws->st.w->on_block = NULL;
    dws->st.w->on_block_ctx = NULL;
    /* and its pushes (applyEntityCollision) land on the player's motion */
    if (sr->player != NULL && sr->player_livh != 0 && serverreplay_player_dim(sr) == sr->here)
    {
        sr->player->e.motion_x = lv_get(sr->player_livh)->e.motion_x;
        sr->player->e.motion_y = lv_get(sr->player_livh)->e.motion_y;
        sr->player->e.motion_z = lv_get(sr->player_livh)->e.motion_z;
    }
    serverreplay_enter(sr, sr->foreign_from);
    nw_env->blockcb.env = sr->foreign_env;
    populate_world_rand = sr->foreign_pop_rand;
    ticks_set_rand(sr->foreign_ticks_rand);
    ticks_set_total_time(sws->st.total_time);
    living_set_walking_world(&sr->d->anw.iew.world_rand.r);
    sr->in_entity_tick = sr->foreign_in_tick;
    sr->foreign = 0;

    /* the pass's own chain: pad 0x10 marks a write of another dimension,
     * its low bits that dimension + 2 */
    for (int i = sr->xw_first; i < sr->nxw; ++i)
    {
        const struct sr_xwrite *x = &sr->xw[i];
        sr_on_block(sr, x->w.x, x->w.y, x->w.z, x->w.id, x->w.meta);
        sr->d->extra[sr->d->nextra - 1].pad = (uint8_t)(0x10 | (x->dim + 2));
    }
}

/* transferEntityToWorld's placement of the old instance, the destination
 * entered: the scaled position (setLocationAndAngles adds yOffset every
 * time), or for the End its getEntrancePortalLocation (100, 50, 0) facing
 * 90, 0; the clamp through an int, then the destination's
 * Teleporter.placeInPortal (a search, else makePortal; the End's obsidian
 * platform) and the final setLocationAndAngles. The old world's
 * updateEntityWithOptionalForce(false) at the moved position is not
 * modelled: it acts only where that position's 32-block neighbourhood is
 * loaded. */
static void sr_travel_place(struct serverreplay *sr, struct entity *e, int from, int to, int teleport_dir, int alive,
                            float *yaw, float *pitch,
                            void (*set_loc)(void *self, double x, double y, double z, float yaw, float pitch),
                            void *self)
{
    struct teleporter *tp = to == -1 ? &sr->tp_hell : to == 1 ? &sr->tp_end : &sr->tp_over;
    double ox = e->pos_x, oy = e->pos_y, oz = e->pos_z;
    float oyaw = *yaw;
    double x = e->pos_x, z = e->pos_z;

    if (to == 1)
    {
        x = 100.0;
        z = 0.0;
        e->pos_y = 50.0;
        set_loc(self, x, e->pos_y, z, 90.0F, 0.0F);
    }
    else
    {
        if (to == -1) { x /= 8.0; z /= 8.0; }
        else { x *= 8.0; z *= 8.0; }
        set_loc(self, x, e->pos_y, z, *yaw, *pitch);
    }
    /* transferEntityToWorld's placing half runs only for a traveller that
     * does not come from the End (its dimension field 1): out of a Nether
     * portal in the End it stays at the scaled position, no portal search;
     * and only for one isEntityAlive (a living at 0 health stays at the
     * scaled position too) */
    if (from == 1 || !alive) return;
    x = (double)((int)x < -29999872 ? -29999872 : (int)x > 29999872 ? 29999872 : (int)x);
    z = (double)((int)z < -29999872 ? -29999872 : (int)z > 29999872 ? 29999872 : (int)z);
    set_loc(self, x, e->pos_y, z, *yaw, *pitch);

    tp->world_time = sr_shared_clock(sr);
    float ny = *yaw, np = *pitch;
    teleporter_place_in_portal(tp, e, teleport_dir, ox, oy, oz, oyaw, *pitch, &ny, &np);
    set_loc(self, e->pos_x, e->pos_y, e->pos_z, ny, np);
}

static void sr_living_set_loc(void *self, double x, double y, double z, float yaw, float pitch)
{
    living_set_location_and_angles(self, x, y, z, yaw, pitch);
}

/* an_world.portal_travel: Entity.travelToDimension for a living of the
 * pass. removeEntity's setDead is undone at once; the old instance is
 * placed in the destination and joins its loadedEntityList, then its new
 * copy, built from its NBT (EntityList.createEntityByName, copyDataFrom,
 * timeUntilPortal and teleportDirection carried over), joins behind it;
 * both worlds' updateEntityTick restart. The destination stays entered: the
 * rest of the old instance's update runs dead in the new world (its moves,
 * its pushes on the copy; its pool pointer names the destination, as
 * transferEntityToWorld's setWorld does), and an_world.post_update enters the
 * pass's dimension again before the pass takes it off its own lists. The dead
 * instance joins the destination chunk's list (spawnEntityInWorld), where it
 * can stay as a ghost (below). */
static void sr_living_portal_travel(void *ctx, struct living *l, int to)
{
    struct serverreplay *sr = ctx;
    /* EntityLivingBase.isEntityAlive as the travel finds it (0 health: dying) */
    int alive = living_is_alive(l);

    if ((to != 0 && to != -1 && to != 1) || sr->foreign) return;
    /* travelToDimension reads its source from the Entity.dimension field,
     * not the world: a summoned mob's (or one loaded from a tag without
     * Dimension) is 0 wherever it stands, so in the End's exit portal it
     * travels from "the overworld" to the End's entrance platform. The true
     * exit (a mob whose field is 1, to the overworld's spawn) is not
     * modelled: the old instance's update goes on at eight times its
     * position in the overworld, where its block reads load chunks. */
    if (to == 1 && l->dimension == 1) return;

    int from = l->dimension == -1 || l->dimension == 1 ? l->dimension : 0;
    struct world *dest = sr_dim_world(sr, to);

    /* the old world's tracker lets it go, its S13 through the player's
     * destroyedItemsNetCache */
    combat_untrack(sr, l->entity_id);
    l->dimension = to;
    /* transferEntityToWorld's updateEntityWithOptionalForce(false) in the
     * old world moves it out of its chunk there (into the scaled position's
     * chunk when that one is loaded, which is not modelled: nothing live
     * reads a dead entity's chunk entry) */
    if (l->added_to_chunk)
        for (int i = 0; i < sr->d->anw.n; ++i)
            if (an_ent_at(sr->d->anw.slot[i])->is_living && lv_get(an_ent_at(sr->d->anw.slot[i])->livh) == l)
            {
                an_chunk_remove(&sr->d->anw, an_ent_at(sr->d->anw.slot[i]), l->chunk_coord_y);
                break;
            }
    l->added_to_chunk = 0;
    sr_foreign_begin(sr, to);
    /* the old instance goes on in the destination (transferEntityToWorld's
     * setWorld) */
    l->an = &sr->d->anw;
    sr_travel_place(sr, &l->e, from, to, l->teleport_direction, alive, &l->rotation_yaw, &l->rotation_pitch,
                    sr_living_set_loc, l);
    /* the destination's updateEntityWithOptionalForce(false) */
    l->prev_rotation_yaw = l->rotation_yaw;
    l->prev_rotation_pitch = l->rotation_pitch;
    l->world = dest;
    l->e.world = dest;

    int by_id = sr->order_new_by_id;
    sr->order_new_by_id = 0;
    /* spawnEntityInWorld of the old instance: not for a traveller out of
     * the End nor a dying one (transferEntityToWorld's placing half does
     * not run) */
    if (from != 1 && alive)
    {
        an_add_living_list(&sr->d->anw, l, sr->d->anw.n);
        sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
        /* spawnEntityInWorld's chunk add names the destination chunk
         * (chunkCoordX/Z), then its onEntityAdded: the destination's
         * tracker takes it, and a player there in range gets its spawn (the
         * copy's follows; the dead old one leaves the next pass with its
         * S13) */
        struct an_ent *oen = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
        an_chunk_add(&sr->d->anw, oen, (int)floor(l->e.pos_x / 16.0), (int)floor(l->e.pos_y / 16.0),
                     (int)floor(l->e.pos_z / 16.0));
        /* (sr_living_post_update decides whether it stays there) */
        sr->foreign_old = oen;
        sr->foreign_old_cx = l->chunk_coord_x;
        sr->foreign_old_cy = l->chunk_coord_y;
        sr->foreign_old_cz = l->chunk_coord_z;
        combat_track_spawn(sr, l);
    }

    nbt *tag = nbt_new_compound();
    living_write_nbt(l, tag);
    struct living *copy = sr_living_copy_nbt(sr, l->kind, tag, dest);
    nbt_free(tag);
    /* spawnEntityInWorld refuses a copy whose chunk is not loaded (a
     * traveller out of the End, placed by no portal search) */
    if (!world_chunk(dest, (int)floor(copy->e.pos_x / 16.0), (int)floor(copy->e.pos_z / 16.0)))
    {
        living_release(copy);
        copy = NULL;
    }
    if (copy != NULL)
    {
        copy->time_until_portal = l->time_until_portal;
        copy->teleport_direction = l->teleport_direction;
        copy->an = &sr->d->anw;
        an_add_living(&sr->d->anw, copy, sr->d->anw.n);
        sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
        int sk = sr_spawn_kind(copy->kind);
        if (sk >= 0) spawner_register_living(&sr->d->spawner, sk, copy->entity_id, &copy->e);
        /* spawnEntityInWorld(copy)'s onEntityAdded: the tracker takes it */
        combat_track_spawn(sr, copy);
    }
    sr->order_new_by_id = by_id;

    l->is_dead = 1;
    sr_dim_ws(sr, to)->update_entity_tick = 0;
    sr_dim_ws(sr, from)->update_entity_tick = 0;
    sr_foreign_view(sr);
}

static void sr_ie_set_loc(void *self, double x, double y, double z, float yaw, float pitch)
{
    ie_ent *en = self;

    en->last_tick_x = en->prev_x = en->e.pos_x = x;
    en->last_tick_y = en->prev_y = en->e.pos_y = y + (double)en->e.y_offset;
    en->last_tick_z = en->prev_z = en->e.pos_z = z;
    en->rotation_yaw = yaw;
    en->rotation_pitch = pitch;
    entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
}

/* EntityList.createEntityByName and copyDataFrom for an item, an orb, an
 * arrow or a throwable: the Entity constructor's draws (the id, the Random,
 * the UUID, then EntityItem's hoverStart), readFromNBT over the old
 * instance's fields (the saved ones carried, the rest at the (World)
 * constructor's values) and the portal state copyDataFrom sets. */
static ie_ent *sr_ie_copy(struct serverreplay *sr, ie_world *iew, const ie_ent *old)
{
    ie_ent *c = ie_ent_alloc();
    if (c == NULL) abort();
    *c = *old;
    c->entity_id = det_next_entity_id_role(&SR_DET(sr), DET_SERVER);
    c->rand = det_new_random_role(&SR_DET(sr), DET_SERVER);
    int64_t msb, lsb;
    det_uuid_role(&SR_DET(sr), DET_SERVER, &msb, &lsb);   /* readFromNBT puts the old one back */
    if (c->kind == IE_ITEM) c->hover_start = (float)(det_math_random_role(&SR_DET(sr), DET_SERVER) * 3.141592653589793 * 2.0);
    c->e.self = c;
    c->iew = iew;
    c->stack_count_link = stack_link_item(c);
    c->order_key = 0;
    c->is_dead = 0;
    c->added_to_chunk = 0;
    c->first_update = 1;
    c->e.first_update = 1;
    c->velocity_changed = 0;
    c->in_water = 0;
    c->ticks_existed = 0;
    c->in_portal = 0;
    c->portal_counter = 0;
    if (fabs(c->e.motion_x) > 10.0) c->e.motion_x = 0.0;
    if (fabs(c->e.motion_y) > 10.0) c->e.motion_y = 0.0;
    if (fabs(c->e.motion_z) > 10.0) c->e.motion_z = 0.0;
    /* readFromNBT: Pos is posY + ySize, then setPosition and setRotation's
     * % 360 */
    c->e.pos_y = old->e.pos_y + (double)old->e.y_size;
    c->last_tick_x = c->prev_x = c->e.pos_x;
    c->last_tick_y = c->prev_y = c->e.pos_y;
    c->last_tick_z = c->prev_z = c->e.pos_z;
    c->rotation_yaw = fmodf(old->rotation_yaw, 360.0F);
    c->rotation_pitch = fmodf(old->rotation_pitch, 360.0F);
    c->prev_yaw = c->rotation_yaw;
    c->prev_pitch = c->rotation_pitch;
    switch (c->kind)
    {
    case IE_ITEM:
        c->delay = 0;                 /* delayBeforeCanPickup is not saved */
        break;
    case IE_ORB:
        c->delay = 0;
        /* the new EntityXPOrb's xpColor, xpTargetColor and closestPlayer
         * (the NBT carries none of them; seed-1 S23 row 16779) */
        c->xp_color = 0;
        c->xp_target_color = 0;
        c->xp_has_target = 0;
        break;
    default:
        /* Block.getBlockById(inTile & 255): an id no block has (the -1 a
         * null block writes) reads back as air */
        c->in_tile = BLOCKS[c->in_tile & 255].exists ? (c->in_tile & 255) : 0;
        c->in_data &= 255;
        c->shake &= 255;
        c->ticks_in_air = 0;
        if (c->kind == IE_ARROW)
        {
            c->knockback_strength = 0;
            c->is_critical = 0;
            c->shooting_entity = 0;
            /* EntityArrow(World) sets 0.5 by 0.5: a mob's arrow (its target
             * constructor keeps the Entity's 0.6 by 1.8) shrinks in the copy */
            entity_set_size(&c->e, 0.5F, 0.5F);
        }
        else
        {
            if (c->kind != IE_ARROW && c->kind < IE_SMALL_FIREBALL) c->ticks_in_ground = 0;
            c->shooter = 0;
            c->shooter_is_player = 0;
            /* getThrower looks the ownerName up again in the new world */
            c->owner_pending = c->owner_name != NULL;
        }
        break;
    }
    if (c->kind == IE_ORB) ie_orb_nbt_size(c);
    else entity_set_position(&c->e, c->e.pos_x, c->e.pos_y, c->e.pos_z);
    return c;
}

/* ie_world.portal_travel: Entity.travelToDimension (EntityItem's override
 * adds searchForOtherItemsNearby, which a dead item cannot act on) for an
 * item, orb, arrow or throwable, the way sr_living_portal_travel moves a
 * living: the server's own item pool (pass order pool 0), or the mob
 * pool's items and projectiles, each behind an an_ent (pool 2). The old
 * instance's list entry in the destination becomes a copy of its final
 * state in post_update, since the pass frees the old one. */
int sr_top_solid_or_liquid(struct world *w, int x, int z);
static void sr_ie_exit_end(struct serverreplay *sr, struct ie_world *iew, ie_ent *en);

static void sr_ie_portal_travel(struct ie_world *iew, ie_ent *en, int to)
{
    struct serverreplay *sr = iew->portal_ctx;

    if (to == 1 && sr->here == 1 && !sr->foreign)
    {
        sr_ie_exit_end(sr, iew, en);
        return;
    }
    if ((to != 0 && to != -1 && to != 1) || sr->foreign) return;

    int mob_pool = iew == &sr->d->anw.iew;
    int from = sr->here;
    struct world *dest = sr_dim_world(sr, to);

    en->dimension = to;
    ie_chunk_leave(iew, en);
    if (mob_pool)
        for (int i = 0; i < sr->d->anw.n; ++i)
            if (!an_ent_at(sr->d->anw.slot[i])->is_living && ie_get(an_ent_at(sr->d->anw.slot[i])->ieh) == en)
            {
                an_chunk_remove(&sr->d->anw, an_ent_at(sr->d->anw.slot[i]), en->chunk_y);
                break;
            }
    /* World.removeEntity's untrackEntity: the old world's tracker lets it go */
    pickobj_server_untrack(sr, en->entity_id);
    sr_foreign_begin(sr, to);
    /* the old instance goes on in the destination's pool (transferEntityToWorld's
     * setWorld) */
    iew = mob_pool ? &sr->d->anw.iew : &sr->d->iew;
    en->iew = iew;
    sr_travel_place(sr, &en->e, from, to, en->teleport_direction, 1, &en->rotation_yaw, &en->rotation_pitch,
                    sr_ie_set_loc, en);
    /* the destination's updateEntityWithOptionalForce(false) */
    en->prev_yaw = en->rotation_yaw;
    en->prev_pitch = en->rotation_pitch;
    en->e.world = dest;
    /* spawnEntityInWorld's chunk coordinates at the new place (the tracker's
     * isPlayerWatchingThisChunk reads them); the dead old instance takes
     * part in no query */
    en->chunk_x = (int)floor(en->e.pos_x / 16.0);
    en->chunk_y = (int)floor(en->e.pos_y / 16.0);
    en->chunk_z = (int)floor(en->e.pos_z / 16.0);

    int by_id = sr->order_new_by_id;
    sr->order_new_by_id = 0;
    ie_ent *copy = NULL;
    /* transferEntityToWorld spawns the old instance in the destination too,
     * except for a traveller out of the End (no placing half); the list
     * stops the program past its capacity (ie_list_push) */
    int listed = from != 1;
    if (listed) ie_list_push(iew, en);
    /* EntityEgg has no EntityList name: createEntityByName gives null, so an
     * egg travels as nothing */
    if (en->kind != IE_EGG)
    {
        copy = sr_ie_copy(sr, iew, en);
        /* spawnEntityInWorld refuses a copy whose chunk is not loaded */
        if (!world_chunk(dest, (int)floor(copy->e.pos_x / 16.0), (int)floor(copy->e.pos_z / 16.0)))
        {
            ie_ent_release(copy);
            copy = NULL;
        }
        else
        {
            ie_list_push(iew, copy);
            ie_added_to_world(iew, copy);
        }
    }
    if (!mob_pool)
    {
        if (listed) sr_order_push(sr, 0, en);
        if (copy != NULL) sr_order_push(sr, 0, copy);
    }
    else
    {
        struct an_ent *wo = an_ent_alloc(), *wc = an_ent_alloc();
        if (listed)
        {
            wo->used = 1;
            wo->is_living = 0;
            wo->spawn_index = sr->d->anw.n;
            wo->ieh = ie_ref(en);
            an_list_push(&sr->d->anw, wo);
            sr_order_push(sr, 2, wo);
        }
        else an_ent_release(wo);
        if (copy != NULL)
        {
            wc->used = 1;
            wc->is_living = 0;
            wc->spawn_index = sr->d->anw.n;
            wc->ieh = ie_ref(copy);
            copy->spawn_index = sr->d->anw.n;
            an_list_push(&sr->d->anw, wc);
            an_chunk_add(&sr->d->anw, wc, (int)floor(copy->e.pos_x / 16.0), (int)floor(copy->e.pos_y / 16.0),
                         (int)floor(copy->e.pos_z / 16.0));
            sr_order_push(sr, 2, wc);
        }
        else an_ent_release(wc);
    }
    sr->order_new_by_id = by_id;

    en->is_dead = 1;
    /* a listed old instance's final state stays in the destination
     * (sr_ie_post_update) */
    sr->foreign_ie = listed ? en : NULL;
    sr->foreign_iew = iew;
    sr_dim_ws(sr, to)->update_entity_tick = 0;
    sr_dim_ws(sr, from)->update_entity_tick = 0;
}

static void sr_fh_set_loc(void *self, double x, double y, double z, float yaw, float pitch)
{
    fh_ent *en = self;

    en->last_tick_x = en->e.prev_pos_x = en->e.pos_x = x;
    en->last_tick_y = en->e.prev_pos_y = en->e.pos_y = y + (double)en->e.y_offset;
    en->last_tick_z = en->e.prev_pos_z = en->e.pos_z = z;
    en->rotation_yaw = yaw;
    en->rotation_pitch = pitch;
    entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
}

/* fh_world.portal_travel: Entity.travelToDimension for a falling block in
 * an End portal (the only portal it reaches: its onUpdate never runs
 * Entity.onEntityUpdate's portal count). removeEntity takes it off its
 * chunk and its tracker; transferEntityToWorld places the old instance on
 * the End's entrance platform (placeInPortal builds it) and the rest of its
 * update runs there (worldObj); the new one (EntityList.createEntityByName:
 * the constructor's draws on the server's streams, then copyDataFrom's
 * readFromNBT over the old one's tag) joins the End's lists. The old
 * instance joins them too in Java, dead, and leaves with the End's next
 * pass, before any row reads it: it is not listed here. The End's exit
 * portal (a falling block whose Entity.dimension is 1) is not modelled. */
static void sr_fh_portal_travel(struct fh_world *fw, struct fh_ent *en, int to)
{
    struct serverreplay *sr = fw->portal_ctx;

    if (to != 1 || sr->here == 1 || sr->foreign) return;

    int from = sr->here;
    struct world *dest = sr_dim_world(sr, to);

    fh_chunk_leave(fw, en);
    pickobj_server_untrack(sr, en->entity_id);
    sr_foreign_begin(sr, to);
    fh_world *dw = &sr->d->fhw;
    sr_travel_place(sr, &en->e, from, to, 0, 1, &en->rotation_yaw, &en->rotation_pitch, sr_fh_set_loc, en);
    en->prev_rotation_yaw = en->rotation_yaw;
    en->e.world = dest;
    en->fw = dw;

    int by_id = sr->order_new_by_id;
    sr->order_new_by_id = 0;
    nbt *tag = fh_write_nbt(en);
    fh_ent *copy = fh_spawn_falling_nbt(dw, tag);
    nbt_free(tag);
    /* readFromNBT puts the old UUID back */
    copy->uuid_msb = en->uuid_msb;
    copy->uuid_lsb = en->uuid_lsb;
    /* spawnEntityInWorld refuses a copy whose chunk is not loaded */
    if (!world_chunk(dest, (int)floor(copy->e.pos_x / 16.0), (int)floor(copy->e.pos_z / 16.0)))
    {
        nbt_free(copy->tile_entity_data);
        fh_ent_release(copy);
    }
    else
    {
        fh_added_to_world(dw, copy);
        sr_order_push(sr, 1, copy);
    }
    sr->order_new_by_id = by_id;

    en->is_dead = 1;
    sr_dim_ws(sr, to)->update_entity_tick = 0;
    sr_dim_ws(sr, from)->update_entity_tick = 0;
}

/* fh_world.post_update: the traveller's update is over, back to the world
 * whose pass it is. */
static void sr_fh_post_update(struct fh_world *fw)
{
    struct serverreplay *sr = fw->portal_ctx;

    if (!sr->foreign) return;
    sr_foreign_end(sr);
    sr->native_entities = sr_count_entities(sr);
}

/* Entity.travelToDimension(1) from the End (an item, orb or arrow in the
 * exit portal): the dimension becomes 0, so transferEntityToWorld moves the
 * old entity to 8 times its x and z and back into the End's chunks (the End's
 * updateEntityWithOptionalForce(false)), and places it nowhere else (the
 * from-dimension is 1); a new one of its kind (the constructor's draws on the
 * server's streams), copyDataFrom, then setLocationAndAngles at the
 * overworld's spawn point on its getTopSolidOrLiquidBlock, spawned into the
 * overworld's lists. The old one is dead; its update goes on in the End. */
static void sr_ie_exit_end(struct serverreplay *sr, struct ie_world *iew, ie_ent *en)
{
    int mob_pool = iew == &sr->d->anw.iew;
    en->e.pos_x *= 8.0;
    en->e.pos_z *= 8.0;
    entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
    en->prev_x = en->last_tick_x = en->e.pos_x;
    en->prev_y = en->last_tick_y = en->e.pos_y;
    en->prev_z = en->last_tick_z = en->e.pos_z;
    en->e.y_size = 0.0F;
    ie_chunk_leave(iew, en);

    sr_foreign_begin(sr, 0);
    /* the copy joins the overworld's pool of the same kind */
    iew = mob_pool ? &sr->d->anw.iew : &sr->d->iew;
    int by_id = sr->order_new_by_id;
    sr->order_new_by_id = 0;
    if (iew->n + 1 <= IE_MAX_ENTITIES)
    {
        ie_ent *copy = sr_ie_copy(sr, iew, en);
        copy->dimension = 0;
        copy->e.world = &sr->pop.world;
        int x = sr->d->spawner.spawn_x, z = sr->d->spawner.spawn_z;
        int y = sr_top_solid_or_liquid(&sr->pop.world, x, z);
        copy->e.pos_x = copy->prev_x = copy->last_tick_x = (double)x;
        /* setLocationAndAngles adds the yOffset (the orb's and item's
         * half height) */
        copy->e.pos_y = copy->prev_y = copy->last_tick_y = (double)y + (double)copy->e.y_offset;
        copy->e.pos_z = copy->prev_z = copy->last_tick_z = (double)z;
        copy->e.y_size = 0.0F;
        if (copy->kind == IE_ORB) ie_orb_nbt_size(copy);
        else entity_set_position(&copy->e, copy->e.pos_x, copy->e.pos_y, copy->e.pos_z);
        ie_list_push(iew, copy);
        ie_added_to_world(iew, copy);
        if (!mob_pool) sr_order_push(sr, 0, copy);
        else
        {
            struct an_ent *wc = an_ent_alloc();
            if (wc == NULL) abort();
            wc->used = 1;
            wc->is_living = 0;
            wc->spawn_index = sr->d->anw.n;
            wc->ieh = ie_ref(copy);
            copy->spawn_index = sr->d->anw.n;
            an_list_push(&sr->d->anw, wc);
            an_chunk_add(&sr->d->anw, wc, (int)floor(copy->e.pos_x / 16.0), (int)floor(copy->e.pos_y / 16.0),
                         (int)floor(copy->e.pos_z / 16.0));
            sr_order_push(sr, 2, wc);
        }
    }
    sr->order_new_by_id = by_id;
    /* transferEntityToWorld's setWorld: the rest of the old instance's
     * update (the block walk's remaining cells, the friction read) reads
     * the overworld and loads what it touches there (an orb in the exit
     * portal loaded the overworld chunk under it; seed-1 S23 row 16673),
     * so the overworld's context lasts until its post_update */
    en->e.world = &sr->pop.world;
    sr->foreign_exit = 1;
    en->is_dead = 1;
    sr_dim_ws(sr, 0)->update_entity_tick = 0;
    sr_dim_ws(sr, 1)->update_entity_tick = 0;
}

/* ie_world.post_update: the old instance's final state stays in the
 * destination's lists (the pass frees the original), then back to the
 * pass's world. */
static void sr_ie_post_update(struct ie_world *iew)
{
    struct serverreplay *sr = iew->portal_ctx;

    if (sr->foreign_exit)
    {
        sr->foreign_exit = 0;
        sr_foreign_end(sr);
        return;
    }
    if (!sr->foreign) return;
    if (sr->foreign_ie == NULL)
    {
        /* (a traveller out of the End: not in the destination's lists) */
        sr_foreign_end(sr);
        sr->native_entities = sr_count_entities(sr);
        return;
    }

    ie_ent *old = sr->foreign_ie;
    ie_ent *ghost = ie_ent_alloc();
    if (ghost == NULL) abort();
    *ghost = *old;
    ghost->e.self = ghost;
    ghost->stack_count_link = stack_link_item(ghost);
    ghost->added_to_chunk = 0;
    ie_world *dw = sr->foreign_iew;
    for (int i = 0; i < dw->n; ++i)
        if (ie_ent_at(dw->slot[i]) == old) dw->slot[i] = ie_ent_index(ghost);
    for (int k = 0; k < sr->d->nents; ++k)
        if (sr->d->ents[k].pool == 0 && sr_ent_p(sr, &sr->d->ents[k]) == old) sr->d->ents[k].id = sr_ent_id(sr, sr->d->ents[k].pool, ghost);
    for (int i = 0; i < sr->d->anw.n; ++i)
        if (!an_ent_at(sr->d->anw.slot[i])->is_living && ie_get(an_ent_at(sr->d->anw.slot[i])->ieh) == old) an_ent_at(sr->d->anw.slot[i])->ieh = ie_ref(ghost);
    sr->foreign_ie = NULL;
    sr_foreign_end(sr);
    sr->native_entities = sr_count_entities(sr);
}

/* an_world.post_update: the traveller's update is over, back to the world
 * whose pass it is. */
static void sr_living_post_update(void *ctx)
{
    struct serverreplay *sr = ctx;

    if (!sr->foreign) return;
    /* the old instance the trip put in the destination chunk's list: the
     * source world's updateEntityWithOptionalForce tail after the rest of
     * its update finds it elsewhere when it moved to another chunk (or
     * section) there, and retargets or clears its membership, so the
     * destination pass that drops it never takes it out of that list: a
     * ghost findNearestEntityWithinAABB meets until the chunk unloads. Else
     * the destination pass takes it out (done here, before any query of
     * that world meets it) */
    if (sr->foreign_old != NULL)
    {
        struct an_ent *oen = sr->foreign_old;
        const struct living *ol = lv_get(oen->livh);
        int cy = (int)floor(ol->e.pos_y / 16.0);
        if ((int)floor(ol->e.pos_x / 16.0) != sr->foreign_old_cx || (int)floor(ol->e.pos_z / 16.0) != sr->foreign_old_cz ||
            cy != sr->foreign_old_cy)
            oen->ghost = 1;
        else
        {
            struct an_chunk *c = an_chunk_find(&sr->d->anw, sr->foreign_old_cx, sr->foreign_old_cz);
            if (c != NULL) sec_remove_first(&c->sec[sr->foreign_old_cy], an_ent_index(oen));
        }
        sr->foreign_old = NULL;
    }
    sr_foreign_end(sr);
    sr->native_entities = sr_count_entities(sr);
}

/* Entity.onEntityUpdate's head for the server player, inside
 * onUpdateEntity: the previous position, then the portal countdown, which
 * travels once the player has stood in a portal for getMaxInPortalTime. */
static void sr_player_entity_update(void *ctx)
{
    struct serverreplay *sr = ctx;
    struct server_player *p = sr->player;
    int target = 0;

    p->e.prev_pos_x = p->e.pos_x;
    p->e.prev_pos_y = p->e.pos_y;
    p->e.prev_pos_z = p->e.pos_z;

    /* Entity.onEntityUpdate clears inPortal every tick it was set, so the
     * next collision's setInPortal takes the direction again */
    int was_in = p->portal.in_portal;

    /* ridingEntity == null && portalCounter++ >= max: a rider's counter
     * stands still and inPortal clears */
    if (was_in && p->ridingh != 0)
    {
        p->portal.in_portal = 0;
        if (p->portal.time_until_portal > 0) --p->portal.time_until_portal;
        return;
    }

    if (portal_entity_tick(&p->e, &p->portal, 1, &target))
    {
        /* EntityPlayerMP.travelToDimension: the portal achievement on every
         * Nether trip, either way, before the transfer */
        surv_add_stat(p, ACH_PORTAL, 1);
        sr_transfer_player(sr, target);
    }
    else if (was_in) p->portal.in_portal = 0;
}

/* BlockEndPortal.onEntityCollidedWithBlock (not riding, not ridden):
 * EntityPlayerMP.travelToDimension(1), inside the moveEntity of
 * processPlayer. From the overworld the player first stands at the End's
 * entrance (setPlayerLocation, S08), then transfers. A player is never
 * ridden; riding is its vehicle. */
static void sr_player_end_portal(void *ctx)
{
    struct server_player *p = ctx;
    struct serverreplay *sr = p->replay;

    if (sr == NULL || p->ridingh != 0) return;

    /* every portal block the box overlaps calls travelToDimension(1): the
     * exit repeats (the achievement counts each, and each sends its S2B);
     * processPlayer skips the moves from then on */
    if (p->dimension == 1)
    {
        sr_player_conquer_end(sr);
        return;
    }

    if (p->conquered_end) return;

    /* travelToDimension: theEnd from the overworld, then the entrance's
     * setPlayerLocation; portal from anywhere else */
    if (p->dimension == 0)
    {
        surv_add_stat(p, ACH_THE_END, 1);
        sr_set_player_location(p, 100.0, 50.0, 0.0, 0.0F, 0.0F);
    }
    else surv_add_stat(p, ACH_PORTAL, 1);
    sr_transfer_player(sr, 1);
}

/* BlockPortal.onEntityCollidedWithBlock: not riding, not ridden. */
static void sr_player_set_in_portal(void *ctx)
{
    struct server_player *p = ctx;

    if (p->ridingh != 0) return;
    portal_entity_set_in_portal(&p->e, &p->portal);
}

/* EntityPlayerMP.onUpdate's chunk half, at the player's place in its
 * world's entity pass: up to 5 queued chunks that are loaded and
 * func_150802_k (ticked, populated, lit) leave the queue as one
 * S26PacketMapChunkBulk; a chunk that is not ready stays queued. */
static void sr_player_send_chunks(struct serverreplay *sr, struct world *w)
{
    int kept = 0;
    int i = 0;
    int bulk = 0;

    if (sr->send_dim != sr->here) return;

    for (; i < sr->nsend && bulk < 5; ++i)
    {
        int cx = sr->send_queue[i * 2], cz = sr->send_queue[i * 2 + 1];
        struct chunk *c = world_chunk(w, cx, cz);

        if (c != NULL && c->populated && c->terrain_populated && c->light_populated)
        {
            ++bulk;
            if (sr->nsent < 5)
            {
                struct chunk *copy = chunk_new();
                chunk_share(copy, c);
                memset(&copy->tes, 0, sizeof copy->tes);
                sr->sent[sr->nsent].dim = sr->here;
                sr->sent[sr->nsent].c = copy;
                ++sr->nsent;
            }
            continue;
        }

        sr->send_queue[kept * 2] = cx;
        sr->send_queue[kept * 2 + 1] = cz;
        ++kept;
    }

    for (; i < sr->nsend; ++i)
    {
        sr->send_queue[kept * 2] = sr->send_queue[i * 2];
        sr->send_queue[kept * 2 + 1] = sr->send_queue[i * 2 + 1];
        ++kept;
    }

    sr->nsend = kept;
}

int serverreplay_take_sent(struct serverreplay *sr, struct sr_sent_chunk *out, int max)
{
    int n = sr->nsent < max ? sr->nsent : max;

    memcpy(out, sr->sent, sizeof *out * (size_t)n);
    sr->nsent = 0;
    return n;
}

/* populate_on_ent during a live population: World.spawnEntityInWorld for the
 * entities a structure piece constructed (Village.spawnVillagers' villagers,
 * a piece's item drop). The piece already made the constructor draws on the
 * server's streams; the record carries what they produced. A chest cart
 * never spawns (minecarts are off). */
static void sr_pop_ent(void *ctx, const struct sc_ent *e)
{
    struct serverreplay *sr = ctx;

    if (e->kind == SC_ITEM && e->spilled)
    {
        struct spill_item s;
        memset(&s, 0, sizeof s);
        s.entity_id = e->entity_id;
        s.rand_state = e->rand_state;
        s.uuid_msb = e->uuid_msb;
        s.uuid_lsb = e->uuid_lsb;
        s.x = e->x;
        s.y = e->y;
        s.z = e->z;
        s.mx = e->motion_x;
        s.my = e->motion_y;
        s.mz = e->motion_z;
        s.yaw = e->yaw;
        s.hover = e->hover;
        s.item = e->item;
        s.damage = e->damage;
        s.count = e->count;
        s.tag = e->tag;
        sr_block_env_spill(sr, &s);
        return;
    }
    if (e->kind == SC_ITEM)
    {
        sr_block_env_drop(sr, e->entity_id, e->rand_state, e->uuid_msb, e->uuid_lsb,
                          e->x, e->y, e->z, e->yaw, e->hover, e->motion_x, e->motion_z,
                          e->item, e->damage, e->count);
        return;
    }
    if (e->kind == SC_WITCH && sr->mobs_enabled)
    {
        /* SwampHut.addComponentParts: new EntityWitch(world),
         * setLocationAndAngles(x, y, z, 0, 0), onSpawnWithEgg(null),
         * spawnEntityInWorld. The record's draws are spent on the real
         * streams; the rebuild runs the same constructor and egg over a
         * copy of the streams from before them */
        det_state fake;
        det_init(&fake);
        fake.world_seed = SR_DET(sr).world_seed;
        fake.seeder[DET_OTHER] = e->pre_seeder;
        fake.math[DET_OTHER] = e->pre_math;
        fake.next_id[DET_OTHER] = e->pre_next_id;
        det_set_role(&fake, DET_OTHER);
        det_state *real = sr->d->anw.det;
        sr->d->anw.det = &fake;
        sr->d->anw.iew.det = &fake;
        sr->d->spawner.world_time = sr->w[0].st.world_time;
        float diff = spawner_local_difficulty(&sr->d->spawner, e->x, e->y, e->z);
        struct living *l = hostile_spawn(&sr->d->anw, HK_WITCH, sr->d->anw.n, e->x, e->y, e->z,
                                         0.0F, 0.0F, 0.0, 0.0, 0.0, 0, 0, 0, 0, diff);
        sr->d->anw.det = real;
        sr->d->anw.iew.det = real;
        det_free(&fake);
        if (!l) return;
        l->persistence_required = 0;
        struct an_ent *en = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
        if (!sr->in_entity_tick) sr_order_push(sr, 2, en);
        /* loadedEntityList: the witch counts toward the monster cap */
        spawner_register_living(&sr->d->spawner, sr_spawn_kind(HK_WITCH), l->entity_id, &l->e);
        return;
    }
    if (e->kind != SC_VILLAGER || !sr->mobs_enabled) return;

    /* new EntityVillager(world, profession), setLocationAndAngles(x, y, z,
     * 0, 0): built on a scratch copy of the streams (their draws are spent
     * already), then given the record's id, Random, UUID and head yaw */
    det_state fake;
    sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
    det_state *real = sr->d->anw.det;
    sr->d->anw.det = &fake;
    sr->d->anw.iew.det = &fake;
    struct living *l = vil_spawn_living(&sr->d->anw, VK_VILLAGER, sr->d->anw.n, e->x, e->y, e->z,
                                        0.0F, 0.0F, 0, e->profession, 0, 0, 0);
    sr->d->anw.det = real;
    sr->d->anw.iew.det = real;
    det_free(&fake);
    if (!l) return;
    l->entity_id = e->entity_id;
    l->rand.r.seed = e->rand_state;
    l->uuid_msb = e->uuid_msb;
    l->uuid_lsb = e->uuid_lsb;
    l->rotation_yaw_head = e->yaw;
    struct an_ent *en = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
    if (!sr->in_entity_tick) sr_order_push(sr, 2, en);
}

static void sr_populate(struct serverreplay *sr, int cx, int cz)
{
    struct chunk *c = world_chunk(&sr->pop.world, cx, cz);
    if (!c || c->terrain_populated) return;

    /* World.rand as the caller holds it: inside an entity's tick the pass
     * works on its own copy (written back after the entity), so a population
     * a path finder's chunk load triggers there draws that copy */
    jrand *wr = sr->in_entity_tick ? &sr->d->anw.iew.world_rand.r : &ST_RAND(&sr->w[0].st);
    struct blockcb_env previous_env = nw_env->blockcb.env;
    nw_env->blockcb.env.world_rand = wr;
    nw_env->blockcb.env.det = &SR_DET(sr);
    nw_env->blockcb.env.role = DET_SERVER;
    nw_env->blockcb.env.item_drop = sr_block_env_drop;
    nw_env->blockcb.env.item_spill = sr_block_env_spill;
    nw_env->blockcb.env.spill_role = DET_SERVER;
    nw_env->blockcb.env.ctx = sr;

    populate_150809_p(&sr->pop, c);

    struct world *w = &sr->pop.world;
    jrand *previous_ticks_rand = ticks_get_rand();
    jrand *previous_spawner_rand = sr->d->spawner.rand;
    struct randomtick_env previous_randomtick, pop_randomtick = {wr, &SR_DET(sr), DET_SERVER, sr_pop_entity_drop, sr};
    randomtick_tick_save(&previous_randomtick);
    populate_world_rand = wr;
    populate_det = &SR_DET(sr);
    ticks_set_rand(wr);
    randomtick_tick_load(&pop_randomtick);
    sr->d->spawner.rand = wr;

    int biome = world_get_biome(w, cx * 16 + 16, cz * 16 + 16);
    int village = 0;

    ticks_set_fall_instantly(1);
    populate_seed(&sr->pop, cx, cz);

    void (*previous_on_ent)(void *, const struct sc_ent *) = populate_on_ent;
    void *previous_on_ent_ctx = populate_on_ent_ctx;
    populate_on_ent = sr_pop_ent;
    populate_on_ent_ctx = sr;
    for (int g = 0; g < 4; ++g)
    {
        int any = populate_structures(&sr->pop, g, cx, cz);
        if (g == 1) village = any;
    }
    populate_on_ent = previous_on_ent;
    populate_on_ent_ctx = previous_on_ent_ctx;

    populate_lakes(&sr->pop, cx, cz, village);
    populate_dungeons(&sr->pop, cx, cz);
    decorator_decorate(&sr->pop, biome, cx, cz);
    if (sr->mobs_enabled)
    {
        int before = sr->d->spawner.nout;
        sr->d->spawner.pop_rand = &sr->pop.rand;
        spawner_world_gen(&sr->d->spawner, biome, cx * 16 + 8, cz * 16 + 8);
        sr->d->spawner.pop_rand = NULL;
        if (!sr->in_spawner_tick)
            for (int i = before; i < sr->d->spawner.nout; ++i)
            {
                sr_adopt_mob_tracked(sr, sr->d->spawner.out[i]);
            }
    }
    populate_stage_now(&sr->pop, POP_SPAWNING);
    populate_freeze(&sr->pop, cx, cz);

    ticks_set_fall_instantly(0);
    ticks_set_rand(previous_ticks_rand);
    randomtick_tick_load(&previous_randomtick);
    sr->d->spawner.rand = previous_spawner_rand;
    nw_env->blockcb.env = previous_env;
}

static void sw_free_safe(struct seedworld *sw)
{
    struct world *w = &sw->p.world;
    for (size_t i = 0; i < w->cap; ++i)
    {
        if (w->slot[i] == 0) continue;
        chunk_free(chunk_ptr(w->slot[i]));
    }
    free(w->slot);
    free(w->te_list);
    free(w->te_added);
    free(w->load_order);

    for (int t = 0; t < 4; ++t)
    {
        for (int i = 0; i < sw->p.maps[t].n; ++i)
            start_free(&sw->p.maps[t].starts[i].start);
        free(sw->p.maps[t].starts);
        jhm64_free(&sw->p.maps[t].keys);
    }
    free(sw->p.order);
    det_free(&sw->det);
    free(sw->ents);
}

/* The spawn-load creatures get one entity pass before the outer chunks are
 * saved. Preserve that pass's NBT fields so a later disk load starts from the
 * saved state, not from the constructor state at initial world generation. */
static void sr_step_seed_creatures(struct seedworld *sw)
{
    static const int kinds[] = { AK_SHEEP, AK_PIG, AK_CHICKEN, AK_COW, AK_MOOSHROOM };
    det_state det = sw->det;
    det.splits = NULL;
    det.seeder[DET_OTHER] = det.seeder[DET_SERVER];
    det.math[DET_OTHER] = det.math[DET_SERVER];
    det.next_id[DET_OTHER] = det.next_id[DET_SERVER];
    struct an_world *an ENV_LOCAL = envstack_take(sizeof *an);
    an_init(an, &sw->p.world, &det);
    an->iew.world_rand.r = sw->world_rand;
    an->has_player = 1;
    an->player_x = (double)sw->spawn_x;
    an->player_y = 70.0;
    an->player_z = (double)sw->spawn_z;
    for (int i = 0; i < sw->nents; ++i)
    {
        const struct sw_entity *e = &sw->ents[i];
        if (e->cls < SW_SHEEP || e->cls > SW_MOOSHROOM) continue;
        struct living *l = an_spawn_living(an, kinds[e->cls], i, e->x, e->y, e->z,
                                            e->yaw, e->pitch, 0, 0, 0, 0, 0, 0);
        l->rand = e->rand;
        l->entity_id = e->id;
        l->uuid_msb = e->uuid_msb;
        l->uuid_lsb = e->uuid_lsb;
        if (e->cls == SW_SHEEP) l->data_watcher_16 = (l->data_watcher_16 & 240) | e->fleece;
        struct attr_mod mod = {0};
        mod.name = MODN_RANDOM_SPAWN_BONUS;
        mod.amount = e->follow_bonus;
        mod.operation = 1;
        mod.uuid_msb = e->follow_msb;
        mod.uuid_lsb = e->follow_lsb;
        mod.saved = 1;
        attrs_apply(&l->attrs.a[ATTR_FOLLOW_RANGE], &mod);
    }
    /* Spawning has finished by the first tick; constructor draws above must
     * not move the tick's Det streams. */
    det.seeder[DET_OTHER] = sw->det.seeder[DET_SERVER];
    det.math[DET_OTHER] = sw->det.math[DET_SERVER];
    det.next_id[DET_OTHER] = sw->det.next_id[DET_SERVER];
    an_tick(an, 0);
    for (int i = 0; i < an->n; ++i)
    {
        struct an_ent *en = an_ent_at(an->slot[i]);
        struct sw_entity *e = &sw->ents[en->spawn_index];
        struct living *l = lv_get(en->livh);
        e->x = l->e.pos_x; e->y = l->e.pos_y; e->z = l->e.pos_z;
        e->motion_x = l->e.motion_x; e->motion_y = l->e.motion_y; e->motion_z = l->e.motion_z;
        e->fire = l->e.fire;
        living_drop_paths(l);
        living_release(l);
        an_ent_release(en);
    }
    an_free(an);
    det_free(&det);
}

static void sr_put_chunk_ticks(struct serverreplay *sr, int cx, int cz, const struct tick_entry *tmp, int count,
                               int64_t current_time);

static void sr_save_chunk_ticks(struct serverreplay *sr, int cx, int cz, int64_t current_time)
{
    /* one walk, into room for every entry the box's columns hold */
    int room = ticks_chunk_bound(cx, cz);
    struct tick_entry *tmp = room > 0 ? slab_take((size_t)room * sizeof *tmp) : NULL;
    int count = room > 0 ? ticks_chunk_updates(cx, cz, tmp, room) : 0;
    sr_put_chunk_ticks(sr, cx, cz, tmp, count, current_time);
    slab_give(tmp, (size_t)room * sizeof *tmp);
}

/* A chunk's saved pending ticks, from ticks_chunk_updates' list: the delay
 * each has left at current_time. */
static void sr_put_chunk_ticks(struct serverreplay *sr, int cx, int cz, const struct tick_entry *tmp, int count,
                               int64_t current_time)
{
    struct sr_chunk_ticks *entry = NULL;
    for (int i = 0; i < sr->d->nchunk_ticks; ++i)
    {
        if (sr->d->chunk_ticks[i].cx == cx && sr->d->chunk_ticks[i].cz == cz)
        {
            entry = &sr->d->chunk_ticks[i];
            break;
        }
    }
    if (entry == NULL)
    {
        if (sr->d->nchunk_ticks == sr->d->capchunk_ticks)
        {
            int cap = sr->d->capchunk_ticks ? sr->d->capchunk_ticks * 2 : 64;
            sr->d->chunk_ticks = slab_grow(sr->d->chunk_ticks, (size_t)sr->d->capchunk_ticks * sizeof *sr->d->chunk_ticks,
                                           (size_t)cap * sizeof *sr->d->chunk_ticks);
            sr->d->capchunk_ticks = cap;
        }
        entry = &sr->d->chunk_ticks[sr->d->nchunk_ticks++];
        entry->cx = cx;
        entry->cz = cz;
        entry->n = 0;
        entry->ticks = NULL;
    }
    else
    {
        slab_give(entry->ticks, (size_t)entry->n * sizeof *entry->ticks);
        entry->ticks = NULL;
        entry->n = 0;
    }

    if (count > 0)
    {
        entry->ticks = slab_take((size_t)count * sizeof *entry->ticks);
        entry->n = count;
        for (int k = 0; k < count; ++k)
        {
            entry->ticks[k].x = tmp[k].x;
            entry->ticks[k].y = tmp[k].y;
            entry->ticks[k].z = tmp[k].z;
            entry->ticks[k].block = tmp[k].block;
            entry->ticks[k].delay = (int)(tmp[k].time - current_time);
            entry->ticks[k].priority = tmp[k].priority;
        }
    }
}

static void sr_restore_chunk_ticks(struct serverreplay *sr, int cx, int cz)
{
    struct sr_chunk_ticks *entry = NULL;
    for (int i = 0; i < sr->d->nchunk_ticks; ++i)
    {
        if (sr->d->chunk_ticks[i].cx == cx && sr->d->chunk_ticks[i].cz == cz)
        {
            entry = &sr->d->chunk_ticks[i];
            break;
        }
    }
    if (entry == NULL || entry->n == 0) return;

    for (int k = 0; k < entry->n; ++k)
    {
        ticks_load_chunk_entry(entry->ticks[k].x, entry->ticks[k].y, entry->ticks[k].z,
                               entry->ticks[k].block, entry->ticks[k].delay, entry->ticks[k].priority);
    }
}

/* The saved pending ticks of one unloaded chunk, verbatim from the snapshot's
 * seedticks.jsonl.gz (the region files' TileTicks): the entries the oracle's
 * chunk load re-adds. A chunk the region store has no ticks for saves none,
 * which is the merge the oracle runs too (a chunk that was never saved
 * contributes nothing). The seedworld's own derived ticks are discarded: their
 * absolute times are the populate's, not what the pre-snapshot saves left on
 * disk. */
/* The snapshot's seed ticks by chunk: their indices sorted by (cx, cz), then
 * by index, every dimension's (the store's build asks which chunks any of
 * them names, and takes each chunk's overworld ones in snapshot order).
 * Built once per store build, so the build is not quadratic in them. */
struct sr_st_key { int cx, cz, i; };

static int sr_st_cmp(const void *a, const void *b)
{
    const struct sr_st_key *x = a, *y = b;
    if (x->cx != y->cx) return x->cx < y->cx ? -1 : 1;
    if (x->cz != y->cz) return x->cz < y->cz ? -1 : 1;
    return (x->i > y->i) - (x->i < y->i);
}

static struct sr_st_key *sr_st_index(const struct serverreplay *sr)
{
    struct sr_st_key *v = malloc(sizeof *v * (size_t)(sr->nseed_ticks + 1));
    if (v == NULL) abort();
    for (int i = 0; i < sr->nseed_ticks; ++i)
        v[i] = (struct sr_st_key){sr->seed_ticks[i].cx, sr->seed_ticks[i].cz, i};
    qsort(v, (size_t)sr->nseed_ticks, sizeof *v, sr_st_cmp);
    return v;
}

/* The first index entry of (cx, cz), or -1 when no seed tick names it. */
static int sr_st_first(const struct serverreplay *sr, const struct sr_st_key *ix, int cx, int cz)
{
    int lo = 0, hi = sr->nseed_ticks;
    while (lo < hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (ix[mid].cx < cx || (ix[mid].cx == cx && ix[mid].cz < cz)) lo = mid + 1;
        else hi = mid;
    }
    return lo < sr->nseed_ticks && ix[lo].cx == cx && ix[lo].cz == cz ? lo : -1;
}

static void sr_save_seed_ticks(struct serverreplay *sr, const struct sr_st_key *ix, int cx, int cz)
{
    struct sr_chunk_ticks *entry = NULL;
    for (int i = 0; i < sr->d->nchunk_ticks; ++i)
    {
        if (sr->d->chunk_ticks[i].cx == cx && sr->d->chunk_ticks[i].cz == cz)
        {
            entry = &sr->d->chunk_ticks[i];
            break;
        }
    }
    if (entry == NULL)
    {
        if (sr->d->nchunk_ticks == sr->d->capchunk_ticks)
        {
            int cap = sr->d->capchunk_ticks ? sr->d->capchunk_ticks * 2 : 64;
            sr->d->chunk_ticks = slab_grow(sr->d->chunk_ticks, (size_t)sr->d->capchunk_ticks * sizeof *sr->d->chunk_ticks,
                                           (size_t)cap * sizeof *sr->d->chunk_ticks);
            sr->d->capchunk_ticks = cap;
        }
        entry = &sr->d->chunk_ticks[sr->d->nchunk_ticks++];
        entry->cx = cx;
        entry->cz = cz;
        entry->n = 0;
        entry->ticks = NULL;
    }
    else
    {
        slab_give(entry->ticks, (size_t)entry->n * sizeof *entry->ticks);
        entry->ticks = NULL;
        entry->n = 0;
    }

    int first = sr_st_first(sr, ix, cx, cz);
    if (first < 0) return;
    int count = 0, end = first;
    for (; end < sr->nseed_ticks && ix[end].cx == cx && ix[end].cz == cz; ++end)
        if (sr->seed_ticks[ix[end].i].dim == 0) ++count;
    if (count == 0) return;

    entry->ticks = slab_take((size_t)count * sizeof *entry->ticks);
    entry->n = count;
    int k = 0;
    for (int j = first; j < end; ++j)
    {
        const struct snap_seed_tick *t = &sr->seed_ticks[ix[j].i];
        if (t->dim != 0) continue;
        entry->ticks[k].x = t->x;
        entry->ticks[k].y = t->y;
        entry->ticks[k].z = t->z;
        entry->ticks[k].block = t->id;
        entry->ticks[k].delay = (int)t->t;
        entry->ticks[k].priority = t->priority;
        ++k;
    }
}

static void sr_ensure_saved(struct serverreplay *sr);

/* The tick this chunk's join-time unload save ran at, or -1 when the snapshot
 * has no record of it (older snapshots, or a chunk the join never unloaded:
 * the seed save keeps the old clock-1 behavior for those). */
static int sr_join_clock(const struct serverreplay *sr, int cx, int cz)
{
    for (int i = 0; i < sr->njoin_clock; ++i)
    {
        if (sr->join_clock[i * 3] == cx && sr->join_clock[i * 3 + 1] == cz)
            return sr->join_clock[i * 3 + 2];
    }
    return -1;
}

/* The region store itself, empty: what an unload needs to save its chunk
 * into. */
static void sr_saved_init(struct serverreplay *sr)
{
    if (sr->d->saved_init) return;
    sr->d->saved_init = 1;
    world_init_slab(&sr->d->saved, sr->pop.world.seed);
}

/* The region store here holds (cx, cz): parked in memory or in the spill
 * file (regionspill.h). */
static int sr_saved_has(struct serverreplay *sr, int cx, int cz)
{
    return world_chunk(&sr->d->saved, cx, cz) != NULL || rspill_has(sr_dim_index(sr->d->dim), cx, cz);
}

/* The region store's chunk (cx, cz) out of it, from memory or the spill
 * file (unpacked then), or NULL. */
static struct chunk *sr_saved_take(struct serverreplay *sr, int cx, int cz)
{
    struct chunk *c = world_take_chunk(&sr->d->saved, cx, cz);
    if (c != NULL) return c;
    const uint8_t *p;
    uint32_t n;
    c = rspill_take(sr_dim_index(sr->d->dim), cx, cz, &p, &n);
    if (c == NULL) return NULL;

    /* the chunk's saved entities (after the store's others, in their own
     * order: a load restores a chunk's records by section and seq, the
     * array order only breaking ties among them) and its saved ticks */
    const uint8_t *end = p + n;
    struct sr_dimstate *d = sr->d;
    /* the tile entities, in their store's order, the loading world's */
    uint32_t nte = rspill_in_u32(&p, end);
    if (nte != (uint32_t)c->tes.n || c->tes.cap < c->tes.n)
    {
        fprintf(stderr, "serverreplay: chunk (%d,%d)'s spilled tile entities do not match its store\n", cx, cz);
        abort();
    }
    c->tes.v = c->tes.cap ? malloc((size_t)c->tes.cap * sizeof *c->tes.v) : NULL;
    for (uint32_t k = 0; k < nte; ++k)
    {
        struct tile_entity *te = malloc(sizeof *te);
        if (te == NULL) abort();
        rspill_in(&p, end, te, sizeof *te);
        uint32_t mob = rspill_in_u32(&p, end);
        if (mob)
        {
            te->u.spawner.mob_id = malloc(mob);
            if (te->u.spawner.mob_id == NULL) abort();
            rspill_in(&p, end, te->u.spawner.mob_id, mob - 1);
            te->u.spawner.mob_id[mob - 1] = 0;
        }
        c->tes.v[k] = te;
        world_te_register(sr_dim_world(sr, d->dim), te);
    }
    uint32_t nents = rspill_in_u32(&p, end);
    for (uint32_t k = 0; k < nents; ++k)
    {
        d->saved_ents = slab_grow(d->saved_ents, (size_t)d->nsaved_ents * sizeof *d->saved_ents,
                                  (size_t)(d->nsaved_ents + 1) * sizeof *d->saved_ents);
        struct sr_saved_entity *sv = &d->saved_ents[d->nsaved_ents++];
        rspill_in(&p, end, sv, sizeof *sv);
        uint8_t has;
        rspill_in(&p, end, &has, 1);
        if (has & 1) sv->tag = rspill_nbt_in(&p, end);
        if (has & 2)
        {
            uint32_t len = rspill_in_u32(&p, end);
            sv->cls = malloc(len + 1);
            if (sv->cls == NULL) abort();
            rspill_in(&p, end, sv->cls, len);
            sv->cls[len] = 0;
        }
    }
    if (rspill_in_u32(&p, end))
    {
        --nw_env->rspill.dims[sr_dim_index(d->dim)].nticks;
        if (d->nchunk_ticks == d->capchunk_ticks)
        {
            int cap = d->capchunk_ticks ? d->capchunk_ticks * 2 : 64;
            d->chunk_ticks = slab_grow(d->chunk_ticks, (size_t)d->capchunk_ticks * sizeof *d->chunk_ticks,
                                       (size_t)cap * sizeof *d->chunk_ticks);
            d->capchunk_ticks = cap;
        }
        struct sr_chunk_ticks *ct = &d->chunk_ticks[d->nchunk_ticks++];
        ct->cx = cx;
        ct->cz = cz;
        ct->n = (int)rspill_in_u32(&p, end);
        ct->ticks = ct->n ? slab_take((size_t)ct->n * sizeof *ct->ticks) : NULL;
        if (ct->n) rspill_in(&p, end, ct->ticks, (size_t)ct->n * sizeof *ct->ticks);
    }
    return c;
}

/* What the region store's build leaves set for the ticks after it: the
 * overworld's World.rand as the populate Random (and as the tick Random after
 * the seed world's build, which hands it over past its ticks_restore; the
 * checkpoint's restore puts the old one back), the replay's Det, no populate
 * entity hook. The build used to run at the first unload; it now waits for
 * the first load, so the first unload sets these as the build would have,
 * and a build after that leaves them as it found them. */
static void sr_saved_context(struct serverreplay *sr)
{
    populate_world_rand = &ST_RAND(&sr->w[0].st);
    populate_det = &SR_DET(sr);
    populate_on_ent = NULL;
    populate_on_ent_ctx = NULL;
    if (!sr->save_dir[0]) ticks_set_rand(&ST_RAND(&sr->w[0].st));
    sr->d->saved_ctx = 1;
}

/* Whether (cx, cz) is among the n coordinate pairs in pre. */
static int sr_coords_have(const int *pre, int n, int cx, int cz)
{
    for (int i = 0; i < n; ++i)
        if (pre[i * 2] == cx && pre[i * 2 + 1] == cz) return 1;
    return 0;
}

/* One entity of a region chunk's Entities, filed for the chunk's next load
 * (EntityList.createEntityFromNBT, in list order per section; the restore
 * callback rebuilds it at load time) with a tree of its own: the region path
 * and seedents.jsonl.gz's (lane/seedgen). */
static void sr_file_region_entity(struct serverreplay *sr, const nbt *tag, int cx, int cz)
{
    const char *cls = nbt_string_value(nbt_get(tag, "id"));
    if (!cls) return;
    double x = cp_dat(tag, "Pos", 0);
    double y = cp_dat(tag, "Pos", 1);
    double z = cp_dat(tag, "Pos", 2);
    int ecx = (int)floor(x / 16.0), ecz = (int)floor(z / 16.0);
    if (ecx != cx || ecz != cz) return;

    nbt *own = nbt_copy_canonical(tag);
    if (!own)
    {
        char *text = nbt_render((nbt *)tag);
        if (!text) return;
        own = nbt_parse(text);
        free(text);
        if (!own) return;
    }

    int section = (int)floor(y / 16.0);
    if (section < 0) section = 0;
    if (section > 15) section = 15;

    sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                               (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
    struct sr_saved_entity *saved = &sr->d->saved_ents[sr->d->nsaved_ents++];
    memset(saved, 0, sizeof *saved);
    saved->active.id = -1;
    saved->tag = own;
    saved->uuid_msb = nbt_int_value(nbt_get(own, "UUIDMost"));
    saved->uuid_lsb = nbt_int_value(nbt_get(own, "UUIDLeast"));
    saved->cx = cx;
    saved->cz = cz;
    saved->section = section;
    /* the pool decides how the reload rebuilds it: a living the
     * living machinery knows by kind, a falling block, an item or
     * an orb through their NBT readers, anything else (an Arrow,
     * say) through sr_restore_checkpoint_entity */
    int kind = sr_living_kind(cls);
    if (kind >= 0)
    {
        saved->pool = 3;
        saved->kind = kind;
        return;
    }
    if (strcmp(cls, "EntityFallingBlock") == 0 || strcmp(cls, "EntityFallingSand") == 0)
    {
        saved->pool = 1;
        return;
    }
    if (strcmp(cls, "Item") == 0)
    {
        saved->pool = 5;
        saved->kind = IE_ITEM;
        return;
    }
    if (strcmp(cls, "XPOrb") == 0)
    {
        saved->pool = 5;
        saved->kind = IE_ORB;
        return;
    }
    saved->pool = 3;
    saved->kind = -1;
    saved->cls = strdup(cls);
}

/* One chunk of the checkpoint's region files into the region store: its
 * HeightMap, Sections and Biomes, then its tile entities (Chunk.addTileEntity),
 * pending ticks (func_147446_b; skipped for a chunk seedticks.jsonl.gz
 * covers, whose entry replaced them) and entities (the chunk's entity
 * lists). A chunk whose save went missing simply does not come back. */
static void sr_saved_read_region(struct serverreplay *sr, int cx, int cz, int skip_ticks)
{

    /* the join's own save of the chunk, else the checkpoint's */
    nbt *level = sr->join_save_dir[0] ? region_load_chunk(sr->join_save_dir, cx, cz) : NULL;
    if (!level) level = region_load_chunk(sr->save_dir, cx, cz);
    if (!level) return;

    struct chunk *c = chunk_new();
    c->cx = cx;
    c->cz = cz;
    /* Chunk(World, cx, cz) then readChunkFromNBT: the precipitation
     * map stays at -999, the biome array at -1 until Biomes fills
     * it, queuedLightChecks at 4096 */
    for (int k = 0; k < 256; ++k) c->precip[k] = -999;
    memset(c->biome, 0xff, sizeof c->biome);
    c->queued_light_checks = 4096;
    c->height_min = 0x7fffffff;

    /* HeightMap: var5.heightMap = getIntArray("HeightMap") */
    {
        int hn = 0;
        const nbt *htag = nbt_get(level, "HeightMap");
        const int *hm = htag ? nbt_int_array(htag, &hn) : NULL;
        if (hm && hn == 256)
            for (int k = 0; k < 256; ++k) c->height[k] = hm[k];
    }

    c->terrain_populated = (uint8_t)region_level_terrain_populated(level);
    {
        const nbt *lp = nbt_get(level, "LightPopulated");
        c->light_populated = (uint8_t)(lp && nbt_int_value(lp) != 0);
    }

    /* Sections: one ExtendedBlockStorage per Y byte, the LSB array
     * flat, the nibble arrays indexed y << 8 | z << 4 | x (the
     * NibbleArray's own layout), the MSB array only when Add came
     * along. removeInvalidBlocks refills the per-section tick count. */
    const nbt *sections = nbt_get(level, "Sections");
    int nsec = sections ? nbt_list_size(sections) : 0;
    for (int s = 0; s < nsec; ++s)
    {
        const nbt *sec = nbt_list_get(sections, s);
        int ybase = (int)nbt_int_value(nbt_get(sec, "Y"));
        if (ybase < 0 || ybase > 15) continue;

        int bn = 0;
        const nbt *btag = nbt_get(sec, "Blocks");
        const signed char *blocks = btag ? nbt_byte_array(btag, &bn) : NULL;
        if (!blocks || bn != 4096) continue;
        /* readChunkFromNBT allocates the ExtendedBlockStorage
         * whatever it holds: a section a light write allocated
         * and no block ever filled is written, read back and
         * counts for getTopFilledSegment */
        c->mask |= (uint16_t)(1 << ybase);
        int addn = 0;
        const nbt *atag = nbt_get(sec, "Add");
        const signed char *add = atag ? nbt_byte_array(atag, &addn) : NULL;
        int metan = 0;
        const nbt *mtag = nbt_get(sec, "Data");
        const signed char *meta = mtag ? nbt_byte_array(mtag, &metan) : NULL;
        int skyn = 0;
        const nbt *sktag = nbt_get(sec, "SkyLight");
        const signed char *sky = sktag ? nbt_byte_array(sktag, &skyn) : NULL;
        int bln = 0;
        const nbt *bltag = nbt_get(sec, "BlockLight");
        const signed char *bl = bltag ? nbt_byte_array(bltag, &bln) : NULL;

        /* the section at once (the loop below is its cell by cell form) */
        if (chunk_sec_load(c, ybase, (const uint8_t *)blocks, add && addn == 2048 ? (const uint8_t *)add : NULL,
                           meta && metan == 2048 ? (const uint8_t *)meta : NULL, sky && skyn == 2048 ? (const uint8_t *)sky : NULL,
                           bl && bln == 2048 ? (const uint8_t *)bl : NULL))
        {
            for (int k = 0; k < 4096; ++k)
            {
                int id = (unsigned char)blocks[k];
                if (add && addn == 2048) id |= ((k & 1) == 0 ? add[k >> 1] & 15 : (add[k >> 1] >> 4) & 15) << 8;
                if (id != 0 && BLOCKS[id & 4095].tick_randomly) ++c->sections_ticking[ybase];
            }
            continue;
        }

        for (int k = 0; k < 4096; ++k)
        {
            /* the flat NBT array is y << 8 | z << 4 | x within the
             * section, the chunk's own x << 12 | z << 8 | y over the
             * whole column: the section's Y base comes back in */
            int fy = k >> 8, fz = (k >> 4) & 15, fx = k & 15;
            int dst = fx << 12 | fz << 8 | (ybase << 4 | fy);
            int vi = fy << 8 | fz << 4 | fx;
            int id = (unsigned char)blocks[k] & 255;
            if (add && addn == 2048)
            {
                /* NibbleArray.get: the low nibble of the byte the
                 * even cells share, the high nibble for the odd */
                int ai = vi >> 1;
                int nib = (vi & 1) == 0 ? add[ai] & 15 : (add[ai] >> 4) & 15;
                id |= (nib & 15) << 8;
            }
            chunk_set_id(c, dst, id);
            if (meta && metan == 2048)
            {
                int mi = vi >> 1;
                int nib = (vi & 1) == 0 ? meta[mi] & 15 : (meta[mi] >> 4) & 15;
                chunk_set_meta_cell(c, dst, nib);
            }
            if (sky && skyn == 2048)
            {
                int si = vi >> 1;
                int nib = (vi & 1) == 0 ? sky[si] & 15 : (sky[si] >> 4) & 15;
                chunk_set_sky(c, dst, nib);
            }
            if (bl && bln == 2048)
            {
                int bi = vi >> 1;
                int nib = (vi & 1) == 0 ? bl[bi] & 15 : (bl[bi] >> 4) & 15;
                chunk_set_blocklight(c, dst, nib);
            }
            if (id != 0)
            {
                c->mask |= (uint16_t)(1 << ybase);
                if (BLOCKS[id & 4095].tick_randomly) ++c->sections_ticking[ybase];
            }
        }
    }

    {
        int bmn = 0;
        const nbt *btag2 = nbt_get(level, "Biomes");
        const signed char *biomes = btag2 ? nbt_byte_array(btag2, &bmn) : NULL;
        if (biomes && bmn == 256)
            for (int k = 0; k < 256; ++k) c->biome[k] = (uint8_t)biomes[k];
    }

    world_put_chunk(&sr->d->saved, c);

    /* readChunkFromNBT's InhabitedTime, which func_147462_b reads */
    {
        const nbt *it = nbt_get(level, "InhabitedTime");
        if (it) c->inhabited_time = nbt_int_value(it);
    }

    /* TileEntities: Chunk.addTileEntity puts each in the chunk map;
     * World.func_147448_a happens at onChunkLoad, so the live world's
     * walk list gains them in the load callback, not here. */
    const nbt *tes = nbt_get(level, "TileEntities");
    int ntes = tes ? nbt_list_size(tes) : 0;
    for (int t = 0; t < ntes; ++t)
    {
        const nbt *tag = nbt_list_get(tes, t);
        int x = (int)nbt_int_value(nbt_get(tag, "x"));
        int y = (int)nbt_int_value(nbt_get(tag, "y"));
        int z = (int)nbt_int_value(nbt_get(tag, "z"));
        int block = world_get_block(&sr->d->saved, x, y, z);
        int kind = te_kind_of_block(block);
        if (!kind) continue;
        struct tile_entity *te = te_new(kind);
        if (!te) continue;
        te->block = block;
        /* te_load reads the canonical tree; the member is borrowed
         * from level, so copy it as the canonical text form gives it */
        nbt *own0 = nbt_copy_canonical(tag);
        char *text = own0 ? NULL : nbt_render((nbt *)tag);
        if (own0 || text)
        {
            nbt *own = own0 ? own0 : nbt_parse(text);
            free(text);
            if (own)
            {
                te_load(te, own);
                nbt_free(own);
                world_set_tile_entity(&sr->d->saved, x, y, z, te);
                continue;
            }
        }
        te_free(te);
    }

    /* TileTicks: WorldServer.func_147446_b schedules at t plus the
     * world's total time, which at the checkpoint's join equals the
     * save's totalTime; sr_restore_chunk_ticks hands the same delta
     * to ticks_load_chunk_entry. */
    const nbt *tts = skip_ticks ? NULL : nbt_get(level, "TileTicks");
    int ntts = tts ? nbt_list_size(tts) : 0;
    for (int t = 0; t < ntts; ++t)
    {
        const nbt *tag = nbt_list_get(tts, t);
        int x = (int)nbt_int_value(nbt_get(tag, "x"));
        int y = (int)nbt_int_value(nbt_get(tag, "y"));
        int z = (int)nbt_int_value(nbt_get(tag, "z"));
        int id = (int)nbt_int_value(nbt_get(tag, "i"));
        int delay = (int)nbt_int_value(nbt_get(tag, "t"));
        int pri = (int)nbt_int_value(nbt_get(tag, "p"));
        if (id == 0) continue;

        struct sr_chunk_ticks *entry = NULL;
        for (int e = 0; e < sr->d->nchunk_ticks; ++e)
            if (sr->d->chunk_ticks[e].cx == cx && sr->d->chunk_ticks[e].cz == cz)
            { entry = &sr->d->chunk_ticks[e]; break; }
        if (!entry)
        {
            if (sr->d->nchunk_ticks == sr->d->capchunk_ticks)
            {
                int cap = sr->d->capchunk_ticks ? sr->d->capchunk_ticks * 2 : 64;
            sr->d->chunk_ticks = slab_grow(sr->d->chunk_ticks, (size_t)sr->d->capchunk_ticks * sizeof *sr->d->chunk_ticks,
                                           (size_t)cap * sizeof *sr->d->chunk_ticks);
            sr->d->capchunk_ticks = cap;
            }
            entry = &sr->d->chunk_ticks[sr->d->nchunk_ticks++];
            entry->cx = cx;
            entry->cz = cz;
            entry->n = 0;
            entry->ticks = NULL;
        }
        entry->ticks = slab_grow(entry->ticks, (size_t)entry->n * sizeof *entry->ticks,
                                 (size_t)(entry->n + 1) * sizeof *entry->ticks);
        entry->ticks[entry->n].x = x;
        entry->ticks[entry->n].y = y;
        entry->ticks[entry->n].z = z;
        entry->ticks[entry->n].block = id;
        entry->ticks[entry->n].delay = delay;
        entry->ticks[entry->n].priority = pri;
        ++entry->n;
    }

    /* Entities: EntityList.createEntityFromNBT, in list order per
     * section; the restore callback rebuilds them at load time. Keep
     * each member's own tree: the level root frees below. */
    const nbt *ents = nbt_get(level, "Entities");
    int nents = ents ? nbt_list_size(ents) : 0;
    for (int e = 0; e < nents; ++e) sr_file_region_entity(sr, nbt_list_get(ents, e), cx, cz);

    nbt_free(level);
}

/* The region chunks the store's build left on disk: the first load of one
 * reads it (sr_saved_read_region) into the store, where it would have been
 * since the build; each is read at most once. */
static void sr_saved_fetch(struct serverreplay *sr, int cx, int cz)
{
    for (int i = 0; i < sr->d->nlazy; ++i)
    {
        int *e = &sr->d->lazy[i * 3];
        if (e[0] != cx || e[1] != cz) continue;
        int skip = e[2];
        sr->d->lazy[i * 3] = sr->d->lazy[(sr->d->nlazy - 1) * 3];
        sr->d->lazy[i * 3 + 1] = sr->d->lazy[(sr->d->nlazy - 1) * 3 + 1];
        sr->d->lazy[i * 3 + 2] = sr->d->lazy[(sr->d->nlazy - 1) * 3 + 2];
        --sr->d->nlazy;
        if (!sr_saved_has(sr, cx, cz)) sr_saved_read_region(sr, cx, cz, skip);
        return;
    }
}

/* The region store's content from before the snapshot: the seed world's
 * spawn-area save or the checkpoint's region files, for every chunk the
 * snapshot world does not hold. Built at the first load that reaches the
 * store (a recording that never loads a chunk never builds it). The chunks
 * the replay unloaded before that are already in the store with their
 * ticks and entities, and keep them: they were in the snapshot world when an
 * earlier build would have run, so it would have left them out too. */

static void sr_ensure_saved(struct serverreplay *sr)
{
    if (sr->d->saved_ready) return;
    sr->d->saved_ready = 1;

    struct ticks_backup *tb = ticks_save();
    struct blockcb_env saved_env = nw_env->blockcb.env;
    jrand *saved_pop_rand = populate_world_rand;
    det_state *saved_pop_det = populate_det;
    void (*saved_on_ent)(void *, const struct sc_ent *) = populate_on_ent;
    void *saved_on_ent_ctx = populate_on_ent_ctx;
    /* the scheduled-tick Random and the random-tick environment (World.rand,
     * the Det a lava fizz's Math.random draws): the seed world's build points
     * them at its own world, a local gone once the build returns, and a build
     * inside a tick (the first chunk load) must leave the tick's in place */
    jrand *saved_ticks_rand = ticks_get_rand();
    struct randomtick_env saved_rt;
    randomtick_tick_save(&saved_rt);

    sr_saved_init(sr);
    struct sr_st_key *st_ix = sr->has_seedticks ? sr_st_index(sr) : NULL;

    /* the chunks unloaded into the store before this build */
    int *pre = malloc(sizeof(int) * 2 * (sr->d->saved.used + 1));
    int npre = 0;
    for (size_t i = 0; i < sr->d->saved.cap; ++i)
    {
        const struct chunk *c = chunk_ptr(sr->d->saved.slot[i]);
        if (c == NULL) continue;
        pre[npre * 2] = c->cx;
        pre[npre * 2 + 1] = c->cz;
        ++npre;
    }

    /* A checkpoint start loads the AnvilChunkLoader save the checkpoint
     * recorded: every chunk on disk, its tile entities, pending ticks and
     * entities, instead of rebuilding the seed world. The snapshot's own
     * chunks (the ones the join had loaded) are already in sr->pop.world and
     * stay as they were saved. */
    if (sr->save_dir[0])
    {
        int *coords = NULL;
        int ncoords = 0;
        if (region_enumerate(sr->save_dir, &coords, &ncoords) != 0)
        {
            free(coords);
            free(pre);
            free(st_ix);
            ticks_restore(tb);
            nw_env->blockcb.env = saved_env;
            randomtick_tick_load(&saved_rt);
            return;
        }
        /* a chunk the join ticks saved and the checkpoint never had comes
         * after the checkpoint's own */
        if (sr->join_save_dir[0])
        {
            int *more = NULL;
            int nmore = 0;
            if (region_enumerate(sr->join_save_dir, &more, &nmore) == 0)
            {
                for (int j = 0; j < nmore; ++j)
                {
                    int dup = 0;
                    for (int i = 0; i < ncoords && !dup; ++i)
                        dup = coords[i * 2] == more[j * 2] && coords[i * 2 + 1] == more[j * 2 + 1];
                    if (dup) continue;
                    coords = realloc(coords, (size_t)(ncoords + 1) * 2 * sizeof *coords);
                    coords[ncoords * 2] = more[j * 2];
                    coords[ncoords * 2 + 1] = more[j * 2 + 1];
                    ++ncoords;
                }
            }
            free(more);
        }

        /* HeightMap, Sections and Biomes per chunk, then the chunk's tile
         * entities (Chunk.addTileEntity), pending ticks (func_147446_b) and
         * entities (the chunk's entity lists). The loader reads chunk by
         * chunk in save order; a chunk whose save went missing simply does
         * not come back. */
        for (int i = 0; i < ncoords; ++i)
        {
            int cx = coords[i * 2], cz = coords[i * 2 + 1];
            if (world_chunk(&sr->pop.world, cx, cz) != NULL || sr_coords_have(pre, npre, cx, cz)) continue;

            /* read at the chunk's first load (sr_saved_fetch), not here: a
             * checkpoint's save holds every chunk the playthrough ever
             * visited, and a segment reaches few of them. The pending ticks
             * of a chunk the seedticks pass below covers are that pass's. */
            int skip_ticks = st_ix != NULL && sr_st_first(sr, st_ix, cx, cz) >= 0;
            if (sr->d->nlazy == sr->d->caplazy)
            {
                sr->d->caplazy = sr->d->caplazy ? sr->d->caplazy * 2 : 256;
                sr->d->lazy = realloc(sr->d->lazy, (size_t)sr->d->caplazy * 3 * sizeof *sr->d->lazy);
            }
            sr->d->lazy[sr->d->nlazy * 3] = cx;
            sr->d->lazy[sr->d->nlazy * 3 + 1] = cz;
            sr->d->lazy[sr->d->nlazy * 3 + 2] = skip_ticks;
            ++sr->d->nlazy;
        }

        free(coords);

        /* The checkpoint's region files hold the chunk states at the
         * checkpoint's save event. A chunk the snapshot shows as not loaded
         * may have been loaded and unloaded since then (the join's load
         * radius churns): its region TileTicks were re-saved at that unload
         * with new delays, and its next load merges THOSE. The snapshot's
         * seedticks.jsonl.gz is the region state at snapshot time, so for
         * every chunk it covers it replaces the pristine checkpoint entry. */
        if (sr->has_seedticks)
        {
            /* each chunk once, at its first seed tick */
            for (int i = 0; i < sr->nseed_ticks; ++i)
            {
                int cx = sr->seed_ticks[i].cx, cz = sr->seed_ticks[i].cz;
                if (st_ix[sr_st_first(sr, st_ix, cx, cz)].i != i || sr_coords_have(pre, npre, cx, cz)) continue;
                sr_save_seed_ticks(sr, st_ix, cx, cz);
            }
        }

        nw_env->blockcb.env = saved_env;
        randomtick_tick_load(&saved_rt);
        /* the region load ran inside ticks_save/ticks_restore: hand the
         * snapshot's pending set back (the region's own TileTicks are in the
         * snapshot's set already) */
        ticks_restore(tb);
        free(pre);
        free(st_ix);
        if (!sr->d->saved_ctx) sr_saved_context(sr);
        else
        {
            populate_world_rand = saved_pop_rand;
            populate_det = saved_pop_det;
            populate_on_ent = saved_on_ent;
            populate_on_ent_ctx = saved_on_ent_ctx;
            ticks_set_rand(saved_ticks_rand);
        }
        return;
    }

    /* the seed world: its build (834 KB of world, too large for a stack
     * frame; a chunk load, where the tick may allocate) runs once per
     * distinct input, and every replay after maps its image from the region
     * cache (regioncache.h). A miss builds and captures the same image, so
     * what follows reads the image either way. */
    struct rc_image *img = calloc(1, sizeof *img);
    if (img == NULL) abort();
    /* the seed world's spawn-area population is the past the snapshot's
     * bigTree state already holds: it ran in a fresh JVM, whose biomes'
     * WorldGenBigTree.heightLimit were all 0, so it is rebuilt from 0 and
     * the snapshot's state (what those trees and the run after left) is put
     * back after */
    int keep_bigtree[sizeof nw_env->decorator.bigtree_state / sizeof nw_env->decorator.bigtree_state[0]];
    memcpy(keep_bigtree, nw_env->decorator.bigtree_state, sizeof keep_bigtree);
    memset(nw_env->decorator.bigtree_state, 0, sizeof nw_env->decorator.bigtree_state);
    if (regioncache_begin(img, sr->pop.world.seed, sr->quiet_join))
    {
        regioncache_apply_env(img);
        if (sr->has_bigtree) memcpy(nw_env->decorator.bigtree_state, keep_bigtree, sizeof keep_bigtree);
    }
    else
    {
        struct seedworld *sw = malloc(sizeof *sw);
        if (sw == NULL) abort();
        /* the springs' shared arrays are scratch the replay's own liquid
         * updates left (the Java server built its spawn area before them):
         * the build's writes do not stay, hit or miss */
        __typeof__(nw_env->features_springs) keep_springs = nw_env->features_springs;
        seedworld_init(sw, sr->pop.world.seed);
        seedworld_build(sw);
        regioncache_note_bigtree(img);
        if (sr->has_bigtree) memcpy(nw_env->decorator.bigtree_state, keep_bigtree, sizeof keep_bigtree);
        /* under the quiet join pin nothing ticked before the spawn-area
         * chunks were saved */
        if (!sr->quiet_join) sr_step_seed_creatures(sw);
        regioncache_capture(img, sw);
        nw_env->features_springs = keep_springs;
        sw_free_safe(sw);
        free(sw);
    }
    regioncache_tags(img);

    /* A mid-run snapshot of a seed start carries every chunk the run saved
     * (its save/ overlay, SaveOverlay.java): those load from there, as the
     * checkpoint path loads its region files, and the rest from the seed
     * world's build */
    int *over = NULL;
    int nover = 0;
    if (sr->join_save_dir[0] && region_enumerate(sr->join_save_dir, &over, &nover) != 0)
    {
        free(over);
        over = NULL;
        nover = 0;
    }

    /* the image's chunks in the seed world's slot order */
    int *pick = malloc(sizeof(int) * (size_t)(img->nchunks + 1));
    int npick = 0;
    for (int i = 0; i < img->nchunks; ++i)
    {
        int cx, cz;
        regioncache_chunk_pos(img, i, &cx, &cz);
        if (world_chunk(&sr->pop.world, cx, cz) != NULL || sr_coords_have(pre, npre, cx, cz) ||
            sr_coords_have(over, nover, cx, cz))
            continue;
        pick[npick++] = i;
    }
    for (int k = 0; k < npick; ++k)
    {
        int cx, cz;
        regioncache_chunk_pos(img, pick[k], &cx, &cz);
        /* One source per chunk, never a mix. func_147446_b re-adds a
         * chunk's TileTicks at t + total at its next load, so the region
         * file at snapshot time is the truth: a v3 snapshot carries it
         * (seedticks.jsonl.gz), including chunks re-saved after the join,
         * and those entries go in verbatim. An older snapshot re-derives
         * the join unload save from the seedworld: writeChunkToNBT wrote
         * t = sched - clock with the clock its unload ran at (tick - 1: 1
         * for tick 2's 100-chunk batch, 2 for tick 3's rest, from
         * worlds.nbt's unloadClock), or 1 when that is not recorded. */
        if (sr->has_seedticks) sr_save_seed_ticks(sr, st_ix, cx, cz);
        else
        {
            int jt = sr_join_clock(sr, cx, cz);
            struct tick_entry *t = NULL;
            int nt = regioncache_ticks(img, pick[k], &t);
            sr_put_chunk_ticks(sr, cx, cz, t, nt, jt >= 0 ? jt - 1 : 1);
            free(t);
        }
        struct chunk *c = regioncache_chunk(img, pick[k]);
        world_put_chunk(&sr->d->saved, c);
        chunk_pack(c);
    }
    free(pick);

    for (int i = 0; i < img->nents; ++i)
    {
        struct sw_entity e;
        regioncache_entity(img, i, &e);
        int cx = (int)floor(e.x / 16.0), cz = (int)floor(e.z / 16.0);
        if (!world_chunk(&sr->d->saved, cx, cz) || sr_coords_have(pre, npre, cx, cz) || sr_coords_have(over, nover, cx, cz))
            continue;
        /* a chunk seedents.jsonl.gz holds: its region Entities below */
        if (sr->has_seedents && sr_seed_chunk(sr, cx, cz) != NULL) continue;
        sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
        sr->d->saved_ents[sr->d->nsaved_ents++] = (struct sr_saved_entity){
            .ent = e, .pool = 2, .cx = cx, .cz = cz,
            .section = (int)floor(e.y / 16.0)
        };
    }

    /* The region files' own Entities of the chunks the seed world's build
     * just stored (seedents.jsonl.gz): what the first ticks put in a chunk
     * the join's unload drain saved (a squid, an item) is in no seed-world
     * build; one source per chunk, as for the ticks. A chunk's entities in
     * list order. */
    for (int i = 0; sr->has_seedents && i < sr->nseed_chunks; ++i)
    {
        const struct snap_seed_chunk *sc = &sr->seed_chunks[i];
        if (!world_chunk(&sr->d->saved, sc->cx, sc->cz) || sr_coords_have(pre, npre, sc->cx, sc->cz) ||
            sr_coords_have(over, nover, sc->cx, sc->cz))
            continue;
        for (int k = 0; k < sc->nents; ++k)
            if (sc->ents[k]) sr_file_region_entity(sr, sc->ents[k], sc->cx, sc->cz);
    }

    for (int i = 0; i < nover; ++i)
    {
        int cx = over[i * 2], cz = over[i * 2 + 1];
        if (world_chunk(&sr->pop.world, cx, cz) != NULL || sr_coords_have(pre, npre, cx, cz) ||
            world_chunk(&sr->d->saved, cx, cz) != NULL)
            continue;
        sr_saved_read_region(sr, cx, cz, 0);
    }
    free(over);

    ticks_restore(tb);
    free(pre);
    free(st_ix);

    nw_env->blockcb.env = saved_env;
    randomtick_tick_load(&saved_rt);
    if (!sr->d->saved_ctx) sr_saved_context(sr);
    else
    {
        populate_world_rand = saved_pop_rand;
        populate_det = saved_pop_det;
        populate_on_ent = saved_on_ent;
        populate_on_ent_ctx = saved_on_ent_ctx;
        ticks_set_rand(saved_ticks_rand);
    }
    /* last, so verify's audit sees the env as the tick will */
    regioncache_end(img);
    free(img);
}

/* AnvilChunkLoader reads the saved NBT by section, then Chunk.onChunkLoad
 * appends each section to loadedEntityList. The constructor draws are new on
 * reload; NBT restores the original UUID, position and saved attributes. */
/* The checkpoint's kind -> EntityList name, the reverse of the class names
 * readChunkFromNBT hands EntityList.createEntityFromNBT (which the restore
 * mirrors), and the name living_write_nbt's caller writes back on unload. */
static int cp_living_kind(const char *name)
{
    static const struct { const char *name; int kind; } map[] = {
        {"Pig", AK_PIG}, {"Cow", AK_COW}, {"MushroomCow", AK_MOOSHROOM},
        {"Chicken", AK_CHICKEN}, {"Sheep", AK_SHEEP},
        {"Villager", VK_VILLAGER}, {"VillagerGolem", VK_IRON_GOLEM},
        {"Slime", SK_SLIME}, {"LavaSlime", SK_MAGMA_CUBE},
        {"Zombie", HK_ZOMBIE}, {"Skeleton", HK_SKELETON},
        {"Creeper", HK_CREEPER}, {"Spider", HK_SPIDER},
        {"CaveSpider", HK_CAVE_SPIDER}, {"Ghast", GK_GHAST},
        {"Squid", AK_SQUID}, {"Bat", AK_BAT},
        {"Enderman", HK_ENDERMAN}, {"Witch", HK_WITCH},
        {"Silverfish", HK_SILVERFISH}, {"PigZombie", HK_PIGMAN},
        {"Blaze", HK_BLAZE},
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; ++i)
        if (strcmp(name, map[i].name) == 0) return map[i].kind;
    return -1;
}


/* The construct dispatch EntityList.createEntityByName runs per class, the
 * same switch snapshot_entities.c keeps for the snapshot's own entities. */
static void cp_construct(struct living *l, det_state *fake)
{
    switch (l->kind)
    {
        case AK_PIG: case AK_COW: case AK_MOOSHROOM: case AK_CHICKEN:
        case AK_SHEEP: case AK_SQUID: case AK_BAT:
            animal_construct(l, fake); break;
        case VK_VILLAGER: villager_construct(l, fake); break;
        case VK_IRON_GOLEM: iron_golem_construct(l, fake); break;
        case SK_SLIME: case SK_MAGMA_CUBE: slime_construct(l, fake); break;
        case HK_ZOMBIE: zombie_construct(l, fake); break;
        case HK_SKELETON: skeleton_construct(l, fake); break;
        case HK_CREEPER: creeper_construct(l, fake); break;
        case HK_SPIDER: case HK_CAVE_SPIDER: spider_construct(l, fake); break;
        case HK_ENDERMAN: enderman_construct(l, fake); break;
        case HK_WITCH: witch_construct(l, fake); break;
        case HK_SILVERFISH: silverfish_construct(l, fake); break;
        case HK_PIGMAN: pigman_construct(l, fake); break;
        case HK_BLAZE: blaze_construct(l, fake); break;
        case GK_GHAST: ghast_construct(l, fake); break;
    }
}

/* The per-kind NBT restore, the fields EntityLivingBase and friends read in
 * readEntityFromNBT that a constructor does not set. */
static void cp_restore_living(struct living *l, const nbt *tag)
{
    l->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    l->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    /* a load from disk ends in Entity.setRotation: yaw % 360, pitch % 360 */
    l->rotation_yaw = fmodf(cp_ftat(tag, "Rotation", 0), 360.0F);
    l->rotation_pitch = fmodf(cp_ftat(tag, "Rotation", 1), 360.0F);
    living_set_location_and_angles(l, cp_dat(tag, "Pos", 0), cp_dat(tag, "Pos", 1),
                                   cp_dat(tag, "Pos", 2), l->rotation_yaw, l->rotation_pitch);
    /* Entity.readFromNBT drops a motion component over 10 */
    l->e.motion_x = fabs(cp_dat(tag, "Motion", 0)) > 10.0 ? 0.0 : cp_dat(tag, "Motion", 0);
    l->e.motion_y = fabs(cp_dat(tag, "Motion", 1)) > 10.0 ? 0.0 : cp_dat(tag, "Motion", 1);
    l->e.motion_z = fabs(cp_dat(tag, "Motion", 2)) > 10.0 ? 0.0 : cp_dat(tag, "Motion", 2);
    l->e.fall_distance = cp_fat(tag, "FallDistance");
    l->e.fire = cp_num(tag, "Fire");
    l->e.on_ground = cp_num(tag, "OnGround");
    l->air = cp_num(tag, "Air");
    l->invulnerable = cp_num(tag, "Invulnerable");
    l->time_until_portal = cp_num(tag, "PortalCooldown");
    l->health = cp_fat(tag, "HealF");
    l->absorption = cp_fat(tag, "AbsorptionAmount");
    l->hurt_time = cp_num(tag, "HurtTime");
    l->death_time = cp_num(tag, "DeathTime");
    l->attack_time = cp_num(tag, "AttackTime");
    l->can_pick_up_loot = cp_num(tag, "CanPickUpLoot");
    l->persistence_required = cp_num(tag, "PersistenceRequired");
    {
        const nbt *cn = nbt_get(tag, "CustomName");
        const char *s = cn ? nbt_string_value(cn) : NULL;
        snprintf(l->custom_name, sizeof l->custom_name, "%s", s ? s : "");
    }
    leash_read_nbt(l, tag);
    l->growing_age = cp_num(tag, "Age");
    l->in_love = cp_num(tag, "InLove");
    l->profession = cp_num(tag, "Profession");
    l->wealth = cp_num(tag, "Riches");
    /* EntityVillager.readEntityFromNBT's Offers: the buyingList it saved */
    if (l->kind == VK_VILLAGER)
    {
        trades_from_nbt(&lv_villager(l)->recipes, nbt_get(tag, "Offers"));
        l->has_recipes = lv_villager(l)->recipes.n > 0;
    }
    l->is_player_created = cp_num(tag, "PlayerCreated");
    l->explosion_power = cp_num(tag, "ExplosionPower");

    /* SharedMonsterAttributes.func_151475_a over the saved attributes */
    {
        const nbt *a = nbt_get(tag, "Attributes");
        for (int i = 0; i < cp_len(a); ++i)
        {
            const nbt *one = nbt_list_get(a, i);
            const char *name = nbt_string_value(nbt_get(one, "Name"));
            if (!name) continue;
            int j = attrs_index_by_name(name);
            if (j < 0) continue;
            {
                struct attr_instance *dst = &l->attrs.a[j];
                dst->base = cp_dbl(nbt_get(one, "Base"));
                dst->nmods = 0;
                const nbt *mods = nbt_get(one, "Modifiers");
                for (int k = 0; k < cp_len(mods); ++k)
                {
                    const nbt *m = nbt_list_get(mods, k);
                    struct attr_mod mod = {0};
                    const char *mn = nbt_string_value(nbt_get(m, "Name"));
                    if (mn) mod.name = attr_name_id(mn);
                    mod.amount = cp_dbl(nbt_get(m, "Amount"));
                    mod.operation = cp_num(m, "Operation");
                    mod.uuid_msb = nbt_int_value(nbt_get(m, "UUIDMost"));
                    mod.uuid_lsb = nbt_int_value(nbt_get(m, "UUIDLeast"));
                    mod.saved = 1;
                    attrs_apply(dst, &mod);
                }
                dst->needs_update = 1;
            }
        }
    }

    if (l->growing_age != 0 && l->kind <= AK_SHEEP) living_set_growing_age(l, l->growing_age);
    if (l->kind == AK_SHEEP)
        l->data_watcher_16 = cp_num(tag, "Color") | (cp_num(tag, "Sheared") ? 16 : 0);
    if (l->kind == AK_PIG) l->data_watcher_16 = cp_num(tag, "Saddle");
    if (l->kind == AK_BAT) l->data_watcher_16 = cp_num(tag, "BatFlags");
    if (l->kind == AK_CHICKEN) l->is_chicken_jockey = cp_num(tag, "IsChickenJockey");
    if (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN)
    {
        if (cp_num(tag, "IsBaby")) zombie_set_child(l, 1);
        l->zombie_is_villager = cp_num(tag, "IsVillager");
        l->zombie_can_break_doors = cp_num(tag, "CanBreakDoors");
        l->zombie_conversion_time = cp_num(tag, "ConversionTime");
    }
    if (l->kind == HK_CREEPER)
    {
        l->creeper_powered = cp_num(tag, "powered");
        l->creeper_ignited = cp_num(tag, "ignited");
        if (nbt_get(tag, "Fuse")) l->creeper_fuse_time = cp_num(tag, "Fuse");
        if (nbt_get(tag, "ExplosionRadius")) l->creeper_explosion_radius = cp_num(tag, "ExplosionRadius");
    }
    if (l->kind == HK_ENDERMAN)
    {
        l->enderman_carried_block = cp_num(tag, "carried");
        l->enderman_carrying_data = cp_num(tag, "carriedData");
    }
    if (IS_SLIME_KIND(l->kind))
    {
        /* EntitySlime.readEntityFromNBT: setSlimeSize(Size + 1) after the
         * base read (the box, the max health base, a full heal) */
        int size = cp_num(tag, "Size");
        slime_set_size(l, (size < 0 ? 0 : size) + 1);
    }
    if (l->kind == HK_PIGMAN) l->pigman_anger_level = cp_num(tag, "Anger");
    const nbt *eq = nbt_get(tag, "Equipment");
    for (int i = 0; i < 5 && i < cp_len(eq); ++i)
    {
        const nbt *item = nbt_list_get(eq, i);
        l->equip[i].id = cp_num(item, "id");
        l->equip[i].count = cp_num(item, "Count");
        l->equip[i].damage = cp_num(item, "Damage");
        l->equip[i].tag = itag_from_item(item);
    }
    const nbt *chances = nbt_get(tag, "DropChances");
    for (int i = 0; i < 5 && i < cp_len(chances); ++i)
        l->equipment_drop_chances[i] = cp_flt(nbt_list_get(chances, i));
    /* EntitySkeleton.readEntityFromNBT: SkeletonType and setCombatTask
     * after super's read, whose Equipment decides the bow's arrow task
     * (before it, a bow skeleton from a saved chunk kept the melee task) */
    if (l->kind == HK_SKELETON)
    {
        skeleton_set_type(l, cp_num(tag, "SkeletonType"));
        skeleton_set_combat_task(l);
    }
}

/* One living entity from region NBT: the constructor on the DET_SERVER
 * streams (a new entity id, Random and UUID there), readFromNBT over it,
 * and its join. */
static struct living *sr_checkpoint_living(struct serverreplay *sr, int kind, const nbt *tag)
{
    det_state fake;
    sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
    det_state *real_det = sr->d->anw.det;
    sr->d->anw.det = &fake;
    sr->d->anw.iew.det = &fake;
    struct living *l = living_alloc();
    if (!l) abort();
    living_init(l, sr_dim_world(sr, sr->here), kind, &fake);
    l->an = &sr->d->anw;
    l->dimension = sr->d->anw.dimension;
    int fake_next_id = l->entity_id;
    cp_construct(l, &fake);
    /* an_spawn_living's tail: the chunk add needs the location first */
    living_set_location_and_angles(l, cp_dat(tag, "Pos", 0), cp_dat(tag, "Pos", 1),
                                   cp_dat(tag, "Pos", 2),
                                   cp_ftat(tag, "Rotation", 0), cp_ftat(tag, "Rotation", 1));
    sr->d->anw.det = real_det;
    sr->d->anw.iew.det = real_det;
    sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);

    cp_restore_living(l, tag);
    /* Entity.readFromNBT's Dimension: the field travelToDimension reads (a
     * summoned mob's 0 stays 0 in the Nether: pfc-dimload-g7) */
    if (nbt_get(tag, "Dimension") != NULL) l->dimension = (int)nbt_int_value(nbt_get(tag, "Dimension"));
    /* Entity.readFromNBT's tail (shouldSetPosAfterLoading): setPosition
     * re-centres the box a readEntityFromNBT size change (a baby's
     * setSize) left anchored at its minimum corner */
    entity_set_position(&l->e, l->e.pos_x, l->e.pos_y, l->e.pos_z);
    l->entity_id = fake_next_id;
    an_add_living(&sr->d->anw, l, sr->d->anw.n);
    /* inside the entity pass the pass pushes every living entity the
     * tick added, this one included */
    if (!sr->in_entity_tick) sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
    /* World.countEntities' class for the spawner's count: a villager or a
     * golem is none of the four (a designated-initializer table here left
     * them at 0, SP_ZOMBIE, and reloaded villagers filled the monster cap),
     * a cave spider or a silverfish is a monster without a list row */
    int sk = sr_spawn_kind(kind);
    if (sk >= 0) spawner_register_living(&sr->d->spawner, sk, l->entity_id, &l->e);
    return l;
}

/* EntityList.createEntityFromNBT for one region member: the constructor on
 * the DET_SERVER streams (a new entity id, Random and UUID there), then
 * readFromNBT over it. */
/* An arrow built for a region-NBT load: EntityArrow.readEntityFromNBT
 * over it, then it joins the living world's list and chunk. */
static struct an_ent *sr_arrow_adopt(struct serverreplay *sr, ie_ent *ie, const nbt *tag)
{
    ie->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    ie->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    ie->e.motion_x = fabs(cp_dat(tag, "Motion", 0)) > 10.0 ? 0.0 : cp_dat(tag, "Motion", 0);
    ie->e.motion_y = fabs(cp_dat(tag, "Motion", 1)) > 10.0 ? 0.0 : cp_dat(tag, "Motion", 1);
    ie->e.motion_z = fabs(cp_dat(tag, "Motion", 2)) > 10.0 ? 0.0 : cp_dat(tag, "Motion", 2);
    ie->rotation_yaw = ie->prev_yaw = (float)fmod(cp_ftat(tag, "Rotation", 0), 360.0F);
    ie->rotation_pitch = ie->prev_pitch = cp_ftat(tag, "Rotation", 1);
    ie->e.fall_distance = cp_fat(tag, "FallDistance");
    ie->e.fire = cp_num(tag, "Fire");
    ie->e.on_ground = cp_num(tag, "OnGround");
    ie->time_until_portal = cp_num(tag, "PortalCooldown");
    ie->tile_x = cp_num(tag, "xTile");
    ie->tile_y = cp_num(tag, "yTile");
    ie->tile_z = cp_num(tag, "zTile");
    ie->ticks_in_ground = cp_num(tag, "life");
    ie->in_tile = cp_num(tag, "inTile") & 255;
    /* Block.getBlockById: an unregistered id (a flying arrow's 255) is air */
    if (!BLOCKS[ie->in_tile].exists) ie->in_tile = 0;
    ie->in_data = cp_num(tag, "inData") & 255;
    ie->shake = cp_num(tag, "shake") & 255;
    ie->in_ground = cp_num(tag, "inGround") == 1;
    if (nbt_get(tag, "damage")) ie->arrow_damage = cp_dbl(nbt_get(tag, "damage"));
    if (nbt_get(tag, "pickup")) ie->can_be_picked_up = cp_num(tag, "pickup");
    ie->shooting_entity = 0;
    ie_added_to_world(&sr->d->anw.iew, ie);
    ie->spawn_index = sr->d->anw.n;
    struct an_ent *en = an_ent_alloc();
    if (!en) abort();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = sr->d->anw.n;
    en->ieh = ie_ref(ie);
    an_list_push(&sr->d->anw, en);
    an_chunk_add(&sr->d->anw, en, (int)floor(ie->e.pos_x / 16.0), (int)floor(ie->e.pos_y / 16.0),
             (int)floor(ie->e.pos_z / 16.0));
    sr_order_push(sr, 2, en);
    return en;
}

/* A run's own saved arrow back at its chunk's load: EntityList.
 * createEntityFromNBT's EntityArrow(World), whose Entity constructor takes
 * the id, the Random and the UUID on the server's streams (readFromNBT then
 * replaces the UUID), no other draw. */
static void sr_restore_arrow(struct serverreplay *sr, struct sr_saved_entity *saved)
{
    const nbt *tag = saved->tag;
    int id = det_next_entity_id_role(&SR_DET(sr), DET_SERVER);
    det_rng rnd = det_new_random_role(&SR_DET(sr), DET_SERVER);
    int64_t msb, lsb;
    det_uuid_role(&SR_DET(sr), DET_SERVER, &msb, &lsb);
    if (saved->kind != IE_ARROW)
    {
        /* a throwable (EntityThrowable(World), readEntityFromNBT: no thrower
         * until getThrower finds its ownerName) or a fireball (no shooter, no
         * acceleration) */
        saved->loaded = 1;
        ie_ent *ie = saved->kind == IE_SMALL_FIREBALL || saved->kind == IE_LARGE_FIREBALL
                         ? sr_fireball_from_nbt(sr, saved->kind == IE_LARGE_FIREBALL, tag, id, rnd)
                     : saved->kind == IE_ENDER_EYE ? sr_ender_eye_from_nbt(sr, tag, id, rnd)
                         : sr_throwable_from_nbt(sr, saved->kind, tag, id, rnd);
        if (!ie) return;
        /* an eye comes back with none of its flight: at (0, 0, 0), timer 0,
         * the shatter */
        if (saved->kind == IE_ENDER_EYE) ie->rand = rnd;
        struct an_ent *en = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
        sr_order_push(sr, 2, en);
        sr_saved_activate(sr, saved, en);
        return;
    }
    /* proj_spawn_arrow's own constructor draws go to a scratch state */
    det_state scratch;
    det_init(&scratch);
    det_state *real = sr->d->anw.iew.det;
    sr->d->anw.iew.det = &scratch;
    ie_ent *ie = proj_spawn_arrow(&sr->d->anw.iew, cp_dat(tag, "Pos", 0), cp_dat(tag, "Pos", 1),
                                  cp_dat(tag, "Pos", 2), 0.0, 1.0, 0.0, 0.0F, 0.0F);
    sr->d->anw.iew.det = real;
    det_free(&scratch);
    saved->loaded = 1;
    if (!ie) return;
    ie->entity_id = id;
    ie->rand.r.seed = rnd.r.seed;
    sr_saved_activate(sr, saved, sr_arrow_adopt(sr, ie, tag));
}

static void sr_restore_checkpoint_entity(struct serverreplay *sr, struct sr_saved_entity *saved,
                                         int section)
{
    (void)section;
    const nbt *tag = saved->tag;
    const char *cls = saved->cls;

    int kind = cp_living_kind(cls);
    if (kind >= 0)
    {
        struct living *l = sr_checkpoint_living(sr, kind, tag);
        sr_saved_activate(sr, saved, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
        saved->loaded = 1;
        /* AnvilChunkLoader.readChunkFromNBT's rider half: each level of the
         * "Riding" chain is its own createEntityFromNBT, joins the chunk,
         * and the previous entity mounts it */
        for (const nbt *ride = nbt_get(tag, "Riding"); ride != NULL; ride = nbt_get(ride, "Riding"))
        {
            const char *vcls = nbt_string_value(nbt_get(ride, "id"));
            int vkind = vcls ? cp_living_kind(vcls) : -1;
            if (vkind < 0) break;
            struct living *v = sr_checkpoint_living(sr, vkind, ride);
            living_mount(l, v);
            l = v;
        }
        return;
    }

    if (strcmp(cls, "Item") == 0 || strcmp(cls, "XPOrb") == 0)
    {
        int orb = strcmp(cls, "XPOrb") == 0;
        det_state fake;
        sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
        /* Entity's constructor on the seeder: the item's Random state */
        det_rng rnd = det_new_random_role(&fake, DET_OTHER);
        int fake_next_id = fake.next_id[DET_OTHER];
        /* EntityItem's 4-arg constructor: the hover start, then the yaw and
         * the two motion draws; EntityXPOrb's draws nothing of the four */
        float hover = 0.0F;
        float yaw = 0.0F;
        if (!orb)
        {
            hover = (float)(det_math_random_role(&fake, DET_OTHER) * M_PI * 2.0);
            yaw = (float)(det_math_random_role(&fake, DET_OTHER) * 360.0);
            det_math_random_role(&fake, DET_OTHER);
            det_math_random_role(&fake, DET_OTHER);
        }
        sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);

        const nbt *item = nbt_get(tag, "Item");
        int64_t msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
        int64_t lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
        /* the entity id the constructor spent (region NBT has none) */
        int eid = fake_next_id;
        ie_ent *en = orb
            ? ie_adopt_orb(&sr->d->iew, eid, msb, lsb, rnd.r.seed,
                cp_dat(tag, "Pos", 0), cp_dat(tag, "Pos", 1), cp_dat(tag, "Pos", 2),
                cp_dat(tag, "Motion", 0), cp_dat(tag, "Motion", 1), cp_dat(tag, "Motion", 2),
                yaw, cp_num(tag, "Value"))
            : ie_adopt_item(&sr->d->iew, eid, msb, lsb, rnd.r.seed,
                cp_dat(tag, "Pos", 0), cp_dat(tag, "Pos", 1), cp_dat(tag, "Pos", 2),
                cp_dat(tag, "Motion", 0), cp_dat(tag, "Motion", 1), cp_dat(tag, "Motion", 2),
                yaw, hover, cp_num(item, "id"), cp_num(item, "Damage"), cp_num(item, "Count"),
                itag_from_item(item));
        if (!en) return;
        /* EntityItem(World)'s pickup delay 0: the NBT has none */
        if (!orb) en->delay = 0;
        en->age = cp_num(tag, "Age");
        en->health = cp_num(tag, "Health");
        en->e.fire = cp_num(tag, "Fire");
        en->e.fall_distance = cp_fat(tag, "FallDistance");
        en->e.on_ground = cp_num(tag, "OnGround");
        ie_added_to_world(&sr->d->iew, en);
        sr_order_push(sr, 0, en);
        saved->active = sr_active_of(sr, 0, en);
        saved->loaded = 1;
        return;
    }

    if (strcmp(cls, "FallingSand") == 0)
    {
        /* new EntityFallingBlock(World): Entity's constructor draws the id,
         * the Random and the UUID on the server's streams (the fake's OTHER
         * slots carry them), then readFromNBT's UUID replaces the drawn one */
        det_state fake;
        sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
        det_state *real = sr->d->fhw.det;
        int real_role = sr->d->fhw.role;
        sr->d->fhw.det = &fake;
        sr->d->fhw.role = DET_OTHER;
        fh_ent *en = fh_spawn_falling_nbt(&sr->d->fhw, tag);
        sr->d->fhw.det = real;
        sr->d->fhw.role = real_role;
        sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);
        if (!en) return;
        en->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
        en->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
        fh_added_to_world(&sr->d->fhw, en);
        sr_order_push(sr, 1, en);
        saved->active = sr_active_of(sr, 1, en);
        saved->loaded = 1;
        return;
    }

    /* a snapshot's saved arrow: the run's own saved arrow's reload (the
     * constructor's id, Random and UUID on the server's streams; the
     * living world's iew draws on its own role, not the fake's DET_OTHER) */
    if (strcmp(cls, "Arrow") == 0)
    {
        sr_restore_arrow(sr, saved);
        return;
    }

    /* an eye of ender in flight when its chunk was saved: the (World)
     * constructor's id, Random and UUID on the server's streams */
    if (strcmp(cls, "EyeOfEnderSignal") == 0)
    {
        det_state fake;
        sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
        int id = det_next_entity_id_role(&fake, DET_OTHER);
        det_rng rnd = det_new_random_role(&fake, DET_OTHER);
        int64_t msb, lsb;
        det_uuid_role(&fake, DET_OTHER, &msb, &lsb);
        sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);
        ie_ent *ie = sr_eye_from_nbt(sr, tag, id);
        if (!ie) return;
        ie->rand = rnd;
        sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
        sr_saved_activate(sr, saved, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
        saved->loaded = 1;
        return;
    }

    /* a throwable in flight when its chunk was saved */
    int tkind = sr_throwable_kind(cls);
    if (tkind)
    {
        det_state fake;
        sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
        int id = det_next_entity_id_role(&fake, DET_OTHER);
        det_rng rnd = det_new_random_role(&fake, DET_OTHER);
        int64_t msb, lsb;
        det_uuid_role(&fake, DET_OTHER, &msb, &lsb);
        sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);
        if (!sr_throwable_from_nbt(sr, tkind, tag, id, rnd)) return;
        sr_order_push(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
        sr_saved_activate(sr, saved, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
        saved->loaded = 1;
        return;
    }

    /* the End's two: the crystal through its constructor (entity id, Random,
     * UUID, innerRotation) and readFromNBT's position; the dragon through
     * the chunk-reload path, its flight state starting over */
    if (sr->here == 1 && (strcmp(cls, "EnderCrystal") == 0 || strcmp(cls, "EnderDragon") == 0))
    {
        int role = det_role(&SR_DET(sr));
        det_set_role(&SR_DET(sr), DET_SERVER);
        if (cls[5] == 'C')
        {
            int k = endfight_spawn_crystal(sr->end, &SR_DET(sr), cp_dat(tag, "Pos", 0), cp_dat(tag, "Pos", 1),
                                           cp_dat(tag, "Pos", 2), fmodf(cp_ftat(tag, "Rotation", 0), 360.0F));
            if (k >= 0)
            {
                struct dragon_crystal_state *c = &sr->end->d.crystals[k];
                c->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
                c->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
                serverreplay_end_listed(sr, k);
                saved->active = sr_active_of(sr, SR_POOL_CRYSTAL, c);
            }
        }
        else
        {
            struct dragon_state *o ENV_LOCAL = envstack_zeroed(sizeof *o);
            o->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
            o->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
            o->x = cp_dat(tag, "Pos", 0);
            o->y = cp_dat(tag, "Pos", 1);
            o->z = cp_dat(tag, "Pos", 2);
            o->mx = cp_dat(tag, "Motion", 0);
            o->my = cp_dat(tag, "Motion", 1);
            o->mz = cp_dat(tag, "Motion", 2);
            o->yaw = cp_ftat(tag, "Rotation", 0);
            o->health = cp_fat(tag, "HealF");
            o->hurt_time = cp_num(tag, "HurtTime");
            o->air = cp_num(tag, "Air");
            o->mob_griefing = 1;
            endfight_reload_dragon(sr->end, &sr->sky.world, &SR_DET(sr), o);
            serverreplay_end_listed(sr, -1);
            saved->active = sr_active_of(sr, SR_POOL_DRAGON, &sr->end->d);
        }
        det_set_role(&SR_DET(sr), role);
        saved->loaded = 1;
        return;
    }

    /* a class this replay does not port (a minecart, a boat): skipped */
}

/* EntityList.createEntityFromNBT for a primed TNT a run's own unload saved
 * (pool 11): EntityTNTPrimed(World)'s Entity draws (the id, the Random, the
 * UUID; the placing constructor's fuse angle is not drawn), then readFromNBT:
 * the Fuse byte, no igniter (tntPlacedBy is not saved). It rejoins the
 * server's own item pool. */
static double sr_nbt_d(const nbt *tag, const char *key, int i);
static float sr_nbt_f(const nbt *v);

static void sr_restore_tnt(struct serverreplay *sr, struct sr_saved_entity *saved)
{
    const nbt *tag = saved->tag;
    det_state *d = &SR_DET(sr);
    int id = det_next_entity_id_role(d, DET_SERVER);
    det_rng r = det_new_random_role(d, DET_SERVER);
    int64_t msb, lsb;
    det_uuid_role(d, DET_SERVER, &msb, &lsb);
    double m[3];
    for (int i = 0; i < 3; ++i)
    {
        m[i] = sr_nbt_d(tag, "Motion", i);
        if (fabs(m[i]) > 10.0) m[i] = 0.0;
    }
    saved->loaded = 1;
    ie_ent *en = ie_adopt_tnt(&sr->d->iew, id, nbt_int_value(nbt_get(tag, "UUIDMost")),
                              nbt_int_value(nbt_get(tag, "UUIDLeast")), r.r.seed, sr_nbt_d(tag, "Pos", 0),
                              sr_nbt_d(tag, "Pos", 1), sr_nbt_d(tag, "Pos", 2), m[0], m[1], m[2],
                              (int)(signed char)nbt_int_value(nbt_get(tag, "Fuse")), 0);
    if (en == NULL) return;
    en->rotation_yaw = fmodf(sr_nbt_f(nbt_list_get(nbt_get(tag, "Rotation"), 0)), 360.0F);
    en->rotation_pitch = fmodf(sr_nbt_f(nbt_list_get(nbt_get(tag, "Rotation"), 1)), 360.0F);
    en->e.fire = (int)nbt_int_value(nbt_get(tag, "Fire"));
    en->e.fall_distance = sr_nbt_f(nbt_get(tag, "FallDistance"));
    en->e.on_ground = (int)nbt_int_value(nbt_get(tag, "OnGround"));
    en->time_until_portal = (int)nbt_int_value(nbt_get(tag, "PortalCooldown"));
    en->dimension = (int)nbt_int_value(nbt_get(tag, "Dimension"));
    ie_added_to_world(&sr->d->iew, en);
    ie_add_to_chunk(&sr->d->iew, en);
    if (!sr->in_entity_tick) sr_order_push(sr, 0, en);
}

static void sr_restore_chunk_entities(struct serverreplay *sr, int cx, int cz);

static void sr_restore_saved_entities(struct serverreplay *sr, int cx, int cz)
{
    if (!sr->mobs_enabled) return;
    sr_restore_chunk_entities(sr, cx, cz);
}

/* AnvilChunkLoader.readChunkFromNBT's entity half, for any dimension: every
 * saved entity of the chunk, section by section in the order the chunk saved
 * them. */
static void sr_restore_chunk_entities(struct serverreplay *sr, int cx, int cz)
{
    static const int kinds[] = { AK_SHEEP, AK_PIG, AK_CHICKEN, AK_COW, AK_MOOSHROOM };
    /* section by section, each in the order the chunk saved them (seed-world
     * creatures, never saved at run time, first in their own order) */
    const size_t order_bytes = sizeof(int) * (size_t)(sr->d->nsaved_ents + 1);
    int *order = slab_take(order_bytes);
    for (int section = 0; section < 16; ++section)
    {
        int n = 0;
        for (int i = 0; i < sr->d->nsaved_ents; ++i)
        {
            const struct sr_saved_entity *c = &sr->d->saved_ents[i];
            if (c->loaded || c->cx != cx || c->cz != cz || c->section != section) continue;
            int at = n++;
            while (at > 0 && sr->d->saved_ents[order[at - 1]].seq > c->seq) { order[at] = order[at - 1]; --at; }
            order[at] = i;
        }
        for (int k = 0; k < n; ++k)
        {
            int i = order[k];
            struct sr_saved_entity *saved = &sr->d->saved_ents[i];
            const struct sw_entity *e = &saved->ent;
            if (saved->loaded || saved->cx != cx || saved->cz != cz || saved->section != section) continue;

            /* a checkpoint's saved entity of a class the pools above do not
             * cover (an Arrow, say): rebuild it from its region NBT, the
             * constructor's draws on the DET_SERVER streams exactly as
             * EntityList.createEntityFromNBT spends them */
            if (saved->pool == 3 && saved->kind < 0 && saved->cls)
            {
                sr_restore_checkpoint_entity(sr, saved, section);
                continue;
            }

            if (saved->pool == 1)
            {
                fh_ent *fh = fh_spawn_falling_nbt(&sr->d->fhw, saved->tag);
                if (!fh) continue;
                fh->uuid_msb = saved->uuid_msb;
                fh->uuid_lsb = saved->uuid_lsb;
                fh_add_to_chunk(&sr->d->fhw, fh);
                fh_added_to_world(&sr->d->fhw, fh);
                sr_order_push(sr, 1, fh);
                saved->active = sr_active_of(sr, 1, fh);
                saved->loaded = 1;
                continue;
            }
            if (saved->pool == 7)
            {
                /* EntityList.createEntityFromNBT's EntityPainting(World) /
                 * EntityItemFrame(World), then EntityHanging.readEntityFromNBT:
                 * the direction and tile read back from the NBT, no art
                 * candidate draw (the one-arg painting constructor never
                 * reaches the loop). The Det draws (id, Random, UUID) spent
                 * here are the constructor's. */
                det_state fake;
                sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
                det_state *real = sr->d->fhw.det;
                int real_role = sr->d->fhw.role;
                sr->d->fhw.det = &fake;
                sr->d->fhw.role = DET_OTHER;   /* the fake carries the SERVER streams in its OTHER slot */
                fh_ent *fh = fh_spawn_hanging_nbt(&sr->d->fhw, saved->kind, saved->tag);
                sr->d->fhw.det = real;
                sr->d->fhw.role = real_role;
                sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);
                if (!fh) continue;
                fh->uuid_msb = saved->uuid_msb;
                fh->uuid_lsb = saved->uuid_lsb;
                fh_add_to_chunk(&sr->d->fhw, fh);
                fh_added_to_world(&sr->d->fhw, fh);
                sr_order_push(sr, 1, fh);
                saved->active = sr_active_of(sr, 1, fh);
                saved->loaded = 1;
                continue;
            }
            if (saved->pool == 4 || saved->pool == 5)
            {
                sr_restore_item(sr, saved);
                continue;
            }
            if (saved->pool == 9)
            {
                sr_restore_arrow(sr, saved);
                continue;
            }
            if (saved->pool == 11)
            {
                sr_restore_tnt(sr, saved);
                continue;
            }
            if (saved->pool == 8 || saved->pool == 10)
            {
                sr_restore_end_record(sr, saved);
                continue;
            }
            if (saved->pool == 3)
            {
                struct living *l = sr_living_from_nbt(sr, saved->kind, saved->tag);
                struct an_ent *en = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
                if (!sr->in_entity_tick) sr_order_push(sr, 2, en);
                sr_saved_activate(sr, saved, en);
                saved->loaded = 1;
                /* AnvilChunkLoader.readChunkFromNBT's rider half: the
                 * "Riding" chain under the saved tag. Each level is its own
                 * EntityList.createEntityFromNBT (fresh id, Random and UUID),
                 * joins the world, and the previous entity mounts it. The
                 * vehicle itself was never a saved list entry
                 * (writeToNBTOptional refuses a ridden entity). */
                for (const nbt *ride = nbt_get(saved->tag, "Riding");
                     ride != NULL; ride = nbt_get(ride, "Riding"))
                {
                    int vkind = sr_kind_for_class(nbt_string_value(nbt_get(ride, "id")));
                    if (vkind < 0) break;
                    struct living *v = sr_living_from_nbt(sr, vkind, ride);
                    struct an_ent *ven = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
                    if (!sr->in_entity_tick) sr_order_push(sr, 2, ven);
                    living_mount(l, v);
                    l = v;
                }
                continue;
            }
            if (e->cls < SW_SHEEP || e->cls > SW_MOOSHROOM) continue;

            det_state fake = SR_DET(sr);
            fake.splits = NULL;
            fake.seeder[DET_OTHER] = SR_DET(sr).seeder[DET_SERVER];
            fake.math[DET_OTHER] = SR_DET(sr).math[DET_SERVER];
            fake.next_id[DET_OTHER] = SR_DET(sr).next_id[DET_SERVER];
            det_state *real = sr->d->anw.det;
            sr->d->anw.det = &fake;
            sr->d->anw.iew.det = &fake;
            /* readFromNBT ends in Entity.setRotation: yaw % 360, pitch % 360 */
            struct living *l = an_spawn_living(&sr->d->anw, kinds[e->cls], sr->d->anw.n,
                                                e->x, e->y, e->z, fmodf(e->yaw, 360.0F), fmodf(e->pitch, 360.0F),
                                                0, 0, 0, 0, 0, 0);
            SR_DET(sr).seeder[DET_SERVER] = fake.seeder[DET_OTHER];
            SR_DET(sr).math[DET_SERVER] = fake.math[DET_OTHER];
            SR_DET(sr).next_id[DET_SERVER] = fake.next_id[DET_OTHER];
            sr->d->anw.det = real;
            sr->d->anw.iew.det = real;
            if (!l) continue;

            l->uuid_msb = e->uuid_msb;
            l->uuid_lsb = e->uuid_lsb;
            /* Entity.readFromNBT: a motion component over 10 reads as 0 (a mob
             * pushed while it was not updated can have saved one) */
            l->e.motion_x = fabs(e->motion_x) > 10.0 ? 0.0 : e->motion_x;
            l->e.motion_y = fabs(e->motion_y) > 10.0 ? 0.0 : e->motion_y;
            l->e.motion_z = fabs(e->motion_z) > 10.0 ? 0.0 : e->motion_z;
            l->e.fire = e->fire;
            if (e->cls == SW_SHEEP) l->data_watcher_16 = (l->data_watcher_16 & 240) | e->fleece;
            struct attr_mod mod = {0};
            mod.name = MODN_RANDOM_SPAWN_BONUS;
            mod.amount = e->follow_bonus;
            mod.operation = 1;
            mod.uuid_msb = e->follow_msb;
            mod.uuid_lsb = e->follow_lsb;
            mod.saved = 1;
            if (!saved->no_follow_bonus) attrs_apply(&l->attrs.a[ATTR_FOLLOW_RANGE], &mod);
            if (saved->resaved)
            {
                l->health = saved->health;
                l->absorption = saved->absorption;
                l->e.fall_distance = saved->fall_distance;
                l->air = saved->air;
                l->e.on_ground = saved->on_ground;
                /* EntityAgeable.readEntityFromNBT's setGrowingAge (a child's
                 * setScaleForAge shrinks the box from its min corner), then
                 * readFromNBT's tail setPosition re-centres it */
                living_set_growing_age(l, saved->growing_age);
                entity_set_position(&l->e, l->e.pos_x, l->e.pos_y, l->e.pos_z);
                l->in_love = saved->in_love;
                l->data_watcher_16 = saved->watcher16;
                /* timeUntilNextEgg is not in EntityChicken NBT: the
                 * constructor's fresh draw stands */
                l->persistence_required = saved->persistence_required;
                memcpy(l->custom_name, saved->custom_name, sizeof l->custom_name);
                l->can_pick_up_loot = saved->can_pick_up_loot;
                l->is_leashed = saved->leashed;
                l->leash_pending = saved->leashed ? saved->leash_pending : 0;
                l->leash_x = saved->leash_x;
                l->leash_y = saved->leash_y;
                l->leash_z = saved->leash_z;
                l->leash_msb = saved->leash_msb;
                l->leash_lsb = saved->leash_lsb;
            }
            struct an_ent *en = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
            /* inside the entity pass, the pass pushes every living entity the
             * tick added, this one included */
            if (!sr->in_entity_tick) sr_order_push(sr, 2, en);
            static const int spawn_kinds[] = { SP_SHEEP, SP_PIG, SP_CHICKEN, SP_COW, SP_MOOSHROOM };
            spawner_register_living(&sr->d->spawner, spawn_kinds[e->cls], l->entity_id, &l->e);
            sr_saved_activate(sr, saved, en);
            saved->loaded = 1;
        }
    }
    slab_give(order, order_bytes);
}

/* AnvilChunkLoader.readChunkFromNBT's tile entities, after its entities:
 * TileEntity.createAndLoadEntity constructs each one, and a dispenser's (a
 * dropper's) field_146021_j is a Det.newRandom on the server's seeder. The
 * native store keeps its tile entities across the unload, so the draw is
 * spent at the load. */
static void sr_dispenser_births(struct serverreplay *sr, const struct chunk *c)
{
    for (int i = 0; i < c->tes.n; ++i)
    {
        const struct tile_entity *te = c->tes.v[i];
        if (te && !te->invalid && te->kind == TE_DISPENSER) det_new_random_role(&SR_DET(sr), DET_SERVER);
    }
}

/* Chunk.onChunkLoad's World.func_147448_a: a chunk read from disk brings
 * fresh tile entities (TileEntity.createAndLoadEntity: the constructor, then
 * readFromNBT; only what the NBT carries survives), appended to the tick
 * list, or to the added list while the tile pass runs. The chunk kept its
 * structs, so what the NBT does not carry is reset here: the chest's and
 * ender chest's neighbour scan, lid, viewer count and tick counter (the
 * ender chest's every-20-tick event phase), the furnace's
 * currentItemBurnTime (readFromNBT recomputes it from the fuel slot, 0 when
 * the slot is empty), the brewing stand's last written bottle mask, the
 * enchantment table's book floats, the piston's lastProgress (readFromNBT
 * sets it to progress) and the cached block metadata. */
static void sr_tiles_loaded(struct world *w, struct chunk *c)
{
    for (int i = 0; i < c->tes.n; ++i)
    {
        struct tile_entity *te = c->tes.v[i];
        if (!te || te->invalid) continue;
        te->block_metadata = -1;
        switch (te->kind)
        {
        case TE_CHEST:
        case TE_ENDER_CHEST:
        {
            struct te_chest *ch = &te->u.chest;
            ch->adjacent_checked = 0;
            ch->lid = ch->prev_lid = 0.0F;
            ch->players_using = 0;
            ch->tick_counter = 0;
            break;
        }
        case TE_FURNACE:
        {
            struct te_furnace *f = &te->u.furnace;
            f->fuel_total = f->slots[1].count > 0 ? furnace_fuel_value(f->slots[1].item) : 0;
            break;
        }
        case TE_BREWING_STAND:
            te->u.brewing.filled_slots = 0;
            break;
        case TE_ENCHANT_TABLE:
            memset(&te->u.enchant, 0, sizeof te->u.enchant);
            break;
        case TE_PISTON:
            te->u.piston.last_progress = te->u.piston.progress;
            break;
        }
        int present = 0;
        for (int k = 0; k < w->te_n && !present; ++k) present = w->te_list[k] == te;
        for (int k = 0; k < w->te_added_n && !present; ++k) present = w->te_added[k] == te;
        if (present) continue;
        struct tile_entity ***list = w->te_ticking ? &w->te_added : &w->te_list;
        int *n = w->te_ticking ? &w->te_added_n : &w->te_n;
        int *cap = w->te_ticking ? &w->te_added_cap : &w->te_cap;
        if (*n == *cap)
        {
            *cap = *cap ? *cap * 2 : 16;
            *list = realloc(*list, (size_t)*cap * sizeof **list);
        }
        (*list)[(*n)++] = te;
    }
}

/* Chunk.onChunkUnload's World.func_147457_a: the chunk's tile entities leave
 * the tick list (after this tick's walk, which already skips them, their
 * chunk being gone). */
static void sr_tiles_unloaded(struct world *w, struct chunk *c)
{
    for (int i = 0; i < c->tes.n; ++i)
    {
        struct tile_entity *te = c->tes.v[i];
        for (int k = 0; k < w->te_n; ++k)
            if (w->te_list[k] == te)
            {
                memmove(&w->te_list[k], &w->te_list[k + 1], (size_t)(w->te_n - k - 1) * sizeof *w->te_list);
                --w->te_n;
                break;
            }
        for (int k = 0; k < w->te_added_n; ++k)
            if (w->te_added[k] == te)
            {
                memmove(&w->te_added[k], &w->te_added[k + 1], (size_t)(w->te_added_n - k - 1) * sizeof *w->te_added);
                --w->te_added_n;
                break;
            }
    }
}

static struct chunk *sr_chunk_load_cb(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;
    /* the profiler's sub-mark only on the call that builds */
    if (!sr->d->saved_ready) PHASE_SUB(PS_SEEDW, sr_ensure_saved(sr));
    sr_saved_fetch(sr, cx, cz);
    struct chunk *c = sr_saved_take(sr, cx, cz);
    if (c != NULL)
    {
        chunk_unpack(c);
        sr_tiles_loaded(&sr->pop.world, c);
        /* precipitationHeightMap is a transient Chunk cache. The Anvil loader
         * constructs a new Chunk and leaves its 256 entries at -999. */
        for (int i = 0; i < 256; ++i) c->precip[i] = -999;
        c->height_min = 0;
        memset(c->update_skylight_columns, 0, sizeof c->update_skylight_columns);
        c->gap_lighting_updated = 0;
        c->queued_light_checks = 4096;
        c->populated = 0;
        world_chunk_request(&sr->pop.world, CREQ_STORED, c->cx, c->cz, c, NULL);
        sr_restore_chunk_ticks(sr, cx, cz);
        sr_restore_saved_entities(sr, cx, cz);
        sr_dispenser_births(sr, c);
        return c;
    }
    return world_generate_chunk(&sr->pop.world, cx, cz);
}

/* A traveller's update gone on in the destination (sr_foreign_view)
 * reaching the world it left: its PathNavigate keeps the world it was made
 * with, so the path search's ChunkCache loads that world's chunks at the
 * scaled position. The load is that world's own (its chunk list, its
 * pending ticks, its population), on its World.rand as the pass hands it to
 * this entity (the structure offers reseed it, the population draws it),
 * the destination's writes so far chained ahead of the load's. */
static struct chunk *sr_provide_home(struct serverreplay *sr, int dim, int cx, int cz)
{
    int back = sr->here;
    struct blockcb_env env = nw_env->blockcb.env;
    jrand *pop_rand = populate_world_rand;
    jrand *ticks_rand = ticks_get_rand();
    jrand *walking = nw_env->living.walking_world;
    int in_tick = sr->in_entity_tick;
    serverreplay_enter(sr, dim);
    for (int i = sr->xw_first; i < sr->nxw; ++i)
    {
        const struct sr_xwrite *x = &sr->xw[i];
        sr_on_block(sr, x->w.x, x->w.y, x->w.z, x->w.id, x->w.meta);
        sr->d->extra[sr->d->nextra - 1].pad = (uint8_t)(0x10 | (x->dim + 2));
    }
    sr->xw_first = sr->nxw;
    jrand *home_ticks_rand = ticks_get_rand();
    jrand *live = sr->foreign_in_tick ? &sr->d->anw.iew.world_rand.r : &ST_RAND(&sr_dim_ws(sr, dim)->st);
    nw_env->blockcb.env = sr->foreign_env;
    nw_env->blockcb.env.world_rand = live;
    populate_world_rand = live;
    ticks_set_rand(live);
    living_set_walking_world(live);
    sr->in_entity_tick = sr->foreign_in_tick;
    struct chunk *c;
    if (dim == 0)
    {
        cl_replay_load(sr->d->cl, sr->current_tick, cx, cz, "provide");
        c = world_chunk(&sr->pop.world, cx, cz);
    }
    else c = world_load_chunk(sr_dim_world(sr, dim), cx, cz);
    ticks_set_rand(home_ticks_rand);
    serverreplay_enter(sr, back);
    nw_env->blockcb.env = env;
    populate_world_rand = pop_rand;
    ticks_set_rand(ticks_rand);
    nw_env->living.walking_world = walking;
    sr->in_entity_tick = in_tick;
    return c;
}

/* World.getBlock and friends reaching a chunk that is not loaded:
 * ChunkProviderServer.loadChunk, top-level (the populate rule follows) or,
 * inside a populate call, a feature's load (no populate rule). */
static struct chunk *sr_provide(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;
    if (sr->pop.world.no_generate) return NULL;
    if (sr->foreign && sr->here != 0 && sr->foreign_from == 0) return sr_provide_home(sr, 0, cx, cz);
    if (sr->pop_depth > 0) cl_pop_load(sr->d->cl, sr->current_tick, cx, cz, sr->pop_id, "feature");
    else cl_replay_load(sr->d->cl, sr->current_tick, cx, cz, "provide");
    return world_chunk(&sr->pop.world, cx, cz);
}

static void sr_spawner_ensure_chunk(void *ctx, int cx, int cz)
{
    sr_provide(ctx, cx, cz);
}

/* Entity.writeToNBTOptional for an item or an orb in an unloading chunk: its
 * NBT, into the pool it came from (4 the server's, 5 the living world's). */
static void sr_save_item(struct serverreplay *sr, const ie_ent *ie, int pool, int cx, int cz)
{
    sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
    struct sr_saved_entity *saved = &sr->d->saved_ents[sr->d->nsaved_ents++];
    memset(saved, 0, sizeof *saved);
    saved->active.id = -1;
    saved->tag = ent_nbt_ie(ie);
    saved->pool = pool;
    saved->kind = ie->kind;
    saved->seq = ie->e.chunk_stamp;
    saved->cx = cx; saved->cz = cz; saved->section = ie->chunk_y;
}

static double sr_nbt_d(const nbt *tag, const char *key, int i)
{
    uint64_t bits = nbt_double_bits(nbt_list_get(nbt_get(tag, key), i));
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static float sr_nbt_f(const nbt *v)
{
    uint32_t bits = v ? nbt_float_bits(v) : 0;
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* EntityList.createEntityFromNBT for an EntityItem or an EntityXPOrb: the
 * Entity constructor's id, Random and UUID, the item's hoverStart, then
 * readFromNBT (a motion past 10 reads as 0, the rotation wraps at 360; the
 * pickup delay is not saved: EntityItem(World) leaves it 0; the owner and
 * the thrower are). */
static ie_ent *sr_item_from_nbt(struct serverreplay *sr, ie_world *iew, const nbt *tag, int kind)
{
    det_state *d = &SR_DET(sr);
    int id = det_next_entity_id_role(d, DET_SERVER);
    det_rng r = det_new_random_role(d, DET_SERVER);
    int64_t msb, lsb;
    det_uuid_role(d, DET_SERVER, &msb, &lsb);
    float hover = kind == IE_ITEM ? (float)(det_math_random_role(d, DET_SERVER) * 3.141592653589793 * 2.0) : 0.0F;
    double m[3];
    for (int i = 0; i < 3; ++i)
    {
        m[i] = sr_nbt_d(tag, "Motion", i);
        if (fabs(m[i]) > 10.0) m[i] = 0.0;
    }
    float yaw = fmodf(sr_nbt_f(nbt_list_get(nbt_get(tag, "Rotation"), 0)), 360.0F);
    msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    const nbt *item = nbt_get(tag, "Item");
    ie_ent *en = kind == IE_ORB
        ? ie_adopt_orb(iew, id, msb, lsb, r.r.seed, sr_nbt_d(tag, "Pos", 0), sr_nbt_d(tag, "Pos", 1),
                       sr_nbt_d(tag, "Pos", 2), m[0], m[1], m[2], yaw, (int)nbt_int_value(nbt_get(tag, "Value")))
        : ie_adopt_item(iew, id, msb, lsb, r.r.seed, sr_nbt_d(tag, "Pos", 0), sr_nbt_d(tag, "Pos", 1),
                        sr_nbt_d(tag, "Pos", 2), m[0], m[1], m[2], yaw, hover,
                        (int)nbt_int_value(nbt_get(item, "id")), (int)nbt_int_value(nbt_get(item, "Damage")),
                        (int)nbt_int_value(nbt_get(item, "Count")), itag_from_item(item));
    if (!en) return NULL;
    if (kind == IE_ORB) ie_orb_nbt_size(en);
    /* delayBeforeCanPickup is not in the NBT: a reloaded item or orb can be
     * picked up at once */
    en->delay = 0;
    en->age = (int)nbt_int_value(nbt_get(tag, "Age"));
    en->health = (int)nbt_int_value(nbt_get(tag, "Health")) & 255;
    en->e.fire = (int)nbt_int_value(nbt_get(tag, "Fire"));
    en->e.fall_distance = sr_nbt_f(nbt_get(tag, "FallDistance"));
    en->e.on_ground = (int)nbt_int_value(nbt_get(tag, "OnGround"));
    en->time_until_portal = (int)nbt_int_value(nbt_get(tag, "PortalCooldown"));
    en->rotation_pitch = fmodf(sr_nbt_f(nbt_list_get(nbt_get(tag, "Rotation"), 1)), 360.0F);
    if (kind == IE_ITEM) en->delay = 0;   /* PickupDelay is not saved */
    if (kind != IE_ORB)
    {
        /* EntityItem.readEntityFromNBT's Owner and Thrower (a Q drop saved
         * with its chunk); the names stay in the saved tag, which lives on */
        const nbt *thrower = nbt_get(tag, "Thrower");
        en->thrower = thrower ? nbt_string_value(thrower) : NULL;
        const nbt *owner = nbt_get(tag, "Owner");
        en->owner_name = owner ? nbt_string_value(owner) : NULL;
    }
    ie_added_to_world(iew, en);
    return en;
}

/* A saved item or orb back into the pool it left: 4 the server's own list,
 * 5 the living world's. */
static void sr_restore_item(struct serverreplay *sr, struct sr_saved_entity *saved)
{
    saved->loaded = 1;
    if (saved->pool == 4)
    {
        ie_ent *ie = sr_item_from_nbt(sr, &sr->d->iew, saved->tag, saved->kind);
        if (!ie) return;
        ie_add_to_chunk(&sr->d->iew, ie);
        /* inside an entity's tick (a path's chunk cache loads the chunk)
         * sr_tick_an appends the pool's new items itself */
        if (!sr->in_entity_tick) sr_order_push(sr, 0, ie);
        return;
    }
    ie_ent *ie = sr_item_from_nbt(sr, &sr->d->anw.iew, saved->tag, saved->kind);
    if (!ie) return;
    /* the chunk being read takes it before it joins the world: the pool's
     * own chunk lists too, where the merge search finds it */
    ie_add_to_chunk(&sr->d->anw.iew, ie);
    ie->spawn_index = sr->d->anw.n;
    struct an_ent *en = an_ent_alloc();
    if (!en) abort();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = sr->d->anw.n;
    en->ieh = ie_ref(ie);
    an_list_push(&sr->d->anw, en);
    an_chunk_add(&sr->d->anw, en, (int)floor(ie->e.pos_x / 16.0), (int)floor(ie->e.pos_y / 16.0),
                 (int)floor(ie->e.pos_z / 16.0));
    if (!sr->in_entity_tick) sr_order_push(sr, 2, en);
}

/* The unloaded chunk itself goes to its dimension's region store; its tile
 * entities leave that world's tick list. */
static int sr_store_chunk(struct serverreplay *sr, int cx, int cz)
{
    struct world *w = sr_dim_world(sr, sr->here);
    struct chunk *c = world_take_chunk(w, cx, cz);
    if (c == NULL) return 0;
    sr_tiles_unloaded(w, c);
    world_put_chunk(&sr->d->saved, c);
    /* nothing reads a stored chunk's cells until it loads again: it is
     * packed between rows (serverreplay_park) */
    if (sr->d->npark < SR_PARK_MAX)
    {
        sr->d->park[sr->d->npark][0] = cx;
        sr->d->park[sr->d->npark][1] = cz;
        sr->d->park_row[sr->d->npark] = sr->park_rows;
    }
    ++sr->d->npark;
    return 1;
}

/* A parked chunk of store d packed, and to the spill file when it can go
 * (regionspill.h: once the store's build ran, so the chunks it found stored
 * are the map's) */
static void sr_park_chunk(struct sr_dimstate *d, int i, struct chunk *c)
{
    chunk_pack(c);
    if (!d->saved_ready || !rspill_ok(c)) return;
    const int cx = c->cx, cz = c->cz;

    /* its tile entities go too, as bytes: each owned by its world's
     * registry (the dimension's, or the store's for a snapshot's adopted
     * chunks) and on no tick list */
    struct world *live = sr_dim_world(d->sr, d->dim);
    for (int k = 0; k < c->tes.n; ++k)
    {
        const struct tile_entity *te = c->tes.v[k];
        if (te == NULL || te->raw != NULL || world_te_listed(live, te)) return;
    }

    /* the chunk's records go with it: its saved entities not loaded (none
     * the store keeps whole: a Nether living, the End's records) and its
     * saved ticks */
    int nents = 0;
    for (int k = 0; k < d->nsaved_ents; ++k)
    {
        const struct sr_saved_entity *sv = &d->saved_ents[k];
        if (sv->loaded || sv->cx != cx || sv->cz != cz) continue;
        if (sv->pool == 6 || sv->pool == 8 || sv->pool == 10 || sv->dragon_copy != NULL) return;
        ++nents;
    }
    int ti = -1;
    for (int k = 0; k < d->nchunk_ticks && ti < 0; ++k)
        if (d->chunk_ticks[k].cx == cx && d->chunk_ticks[k].cz == cz) ti = k;

    struct rspill_out o = {0};
    uint32_t v = (uint32_t)c->tes.n;
    rspill_out_put(&o, &v, sizeof v);
    for (int k = 0; k < c->tes.n; ++k)
    {
        const struct tile_entity *te = c->tes.v[k];
        struct tile_entity h = *te;
        const char *mob = te->kind == TE_MOB_SPAWNER ? te->u.spawner.mob_id : NULL;
        if (te->kind == TE_MOB_SPAWNER) h.u.spawner.mob_id = NULL;
        rspill_out_put(&o, &h, sizeof h);
        v = mob != NULL ? (uint32_t)strlen(mob) + 1 : 0;
        rspill_out_put(&o, &v, sizeof v);
        if (mob != NULL) rspill_out_put(&o, mob, v - 1);
    }
    v = (uint32_t)nents;
    rspill_out_put(&o, &v, sizeof v);
    for (int k = 0; k < d->nsaved_ents; ++k)
    {
        const struct sr_saved_entity *sv = &d->saved_ents[k];
        if (sv->loaded || sv->cx != cx || sv->cz != cz) continue;
        struct sr_saved_entity h = *sv;
        h.tag = NULL;
        h.cls = NULL;
        rspill_out_put(&o, &h, sizeof h);
        uint8_t has = (uint8_t)((sv->tag != NULL) | (sv->cls != NULL) << 1);
        rspill_out_put(&o, &has, 1);
        if (sv->tag != NULL) rspill_nbt_out(&o, sv->tag);
        if (sv->cls != NULL)
        {
            v = (uint32_t)strlen(sv->cls);
            rspill_out_put(&o, &v, sizeof v);
            rspill_out_put(&o, sv->cls, v);
        }
    }
    v = ti >= 0;
    rspill_out_put(&o, &v, sizeof v);
    if (ti >= 0)
    {
        const struct sr_chunk_ticks *ct = &d->chunk_ticks[ti];
        v = (uint32_t)ct->n;
        rspill_out_put(&o, &v, sizeof v);
        rspill_out_put(&o, ct->ticks, (size_t)ct->n * sizeof *ct->ticks);
    }

    int nte = c->tes.n;
    struct tile_entity **tes = nte ? malloc((size_t)nte * sizeof *tes) : NULL;
    if (nte) memcpy(tes, c->tes.v, (size_t)nte * sizeof *tes);
    world_take_chunk(&d->saved, cx, cz);
    if (!rspill_put(i, c, o.p, (uint32_t)o.n))
    {
        world_put_chunk(&d->saved, c);
        free(o.p);
        free(tes);
        return;
    }
    free(o.p);
    for (int k = 0; k < nte; ++k)
    {
        if (!world_te_unregister(live, tes[k])) world_te_unregister(&d->saved, tes[k]);
        te_free(tes[k]);
    }
    free(tes);

    /* out of the store's arrays, the rest in their order */
    if (nents > 0)
    {
        int w = 0;
        const int was = d->nsaved_ents;
        for (int k = 0; k < was; ++k)
        {
            struct sr_saved_entity *sv = &d->saved_ents[k];
            if (!sv->loaded && sv->cx == cx && sv->cz == cz)
            {
                nbt_free(sv->tag);
                free(sv->cls);
                continue;
            }
            if (w != k) d->saved_ents[w] = *sv;
            ++w;
        }
        d->nsaved_ents = w;
        /* the block's class follows the count (slab_grow grows it one
         * record at a time) */
        if (w == 0)
        {
            slab_give(d->saved_ents, (size_t)was * sizeof *d->saved_ents);
            d->saved_ents = NULL;
        }
        else
            d->saved_ents = slab_grow(d->saved_ents, (size_t)was * sizeof *d->saved_ents, (size_t)w * sizeof *d->saved_ents);
    }
    if (ti >= 0)
    {
        ++nw_env->rspill.dims[i].nticks;
        slab_give(d->chunk_ticks[ti].ticks, (size_t)d->chunk_ticks[ti].n * sizeof *d->chunk_ticks[ti].ticks);
        memmove(&d->chunk_ticks[ti], &d->chunk_ticks[ti + 1], (size_t)(d->nchunk_ticks - ti - 1) * sizeof *d->chunk_ticks);
        --d->nchunk_ticks;
    }
}

/* The store's saved-entity records nothing reads again, out of the array
 * (the rest in their order): an item or orb put back in the world (its
 * next save writes a new record; only a Thrower or Owner name is read from
 * the tag while the item lives), and a creature's record whose live
 * instance left the world (a save finds its record by that instance). They
 * grew with every entity a reload brought back. */
static void sr_saved_gc(struct sr_dimstate *d)
{
    int w = 0;
    const int was = d->nsaved_ents;
    for (int k = 0; k < was; ++k)
    {
        struct sr_saved_entity *sv = &d->saved_ents[k];
        int drop = 0;
        if (sv->loaded && (sv->pool == 4 || sv->pool == 5))
            drop = sv->tag == NULL || (nbt_get(sv->tag, "Thrower") == NULL && nbt_get(sv->tag, "Owner") == NULL);
        else if (sv->loaded && (sv->pool == 2 || sv->pool == 3))
        {
            const struct an_ent *en = sv->active.pool == 2 && sv->active.id >= 0 ? an_ent_at(sv->active.id) : NULL;
            drop = en == NULL || !en->used || sv->active_gen != nw_env->arena.an_ents.gen[an_ent_index(en)];
        }
        if (drop)
        {
            nbt_free(sv->tag);
            free(sv->cls);
            continue;
        }
        if (w != k) d->saved_ents[w] = *sv;
        ++w;
    }
    if (w == was) return;
    d->nsaved_ents = w;
    /* the block's class follows the count (sr_park_chunk) */
    if (w == 0)
    {
        slab_give(d->saved_ents, (size_t)was * sizeof *d->saved_ents);
        d->saved_ents = NULL;
    }
    else
        d->saved_ents = slab_grow(d->saved_ents, (size_t)was * sizeof *d->saved_ents, (size_t)w * sizeof *d->saved_ents);
}

/* An unloaded chunk's entity lists, gone once empty (vanilla's go with its
 * Chunk): the living world's and its item lists, the items', the falling
 * blocks'. The tables grew with every chunk that ever held an entity. */
static void sr_drop_entity_lists(struct sr_dimstate *d, int cx, int cz)
{
    if (!d->live || d->anw.w == NULL || world_chunk_loaded(d->anw.w, cx, cz)) return;
    an_chunk_drop_empty(&d->anw, cx, cz);
    ie_chunk_drop_empty(&d->anw.iew, cx, cz);
    ie_chunk_drop_empty(&d->iew, cx, cz);
    fh_chunk_drop_empty(&d->fhw, cx, cz);
}

/* every table's chunks not loaded (a park past SR_PARK_MAX) */
static void sr_drop_entity_lists_all(struct sr_dimstate *d)
{
    if (!d->live || d->anw.w == NULL) return;
    int n = d->anw.nchunks + d->anw.iew.nchunks + d->iew.nchunks + d->fhw.nchunks, k = 0;
    int *xz = malloc(sizeof(int) * 2 * ((size_t)n + 1));
    for (int i = 0; i < d->anw.nchunks; ++i, ++k) { xz[2 * k] = d->anw.chunks[i].cx; xz[2 * k + 1] = d->anw.chunks[i].cz; }
    for (int i = 0; i < d->anw.iew.nchunks; ++i, ++k) { xz[2 * k] = d->anw.iew.chunks[i].cx; xz[2 * k + 1] = d->anw.iew.chunks[i].cz; }
    for (int i = 0; i < d->iew.nchunks; ++i, ++k) { xz[2 * k] = d->iew.chunks[i].cx; xz[2 * k + 1] = d->iew.chunks[i].cz; }
    for (int i = 0; i < d->fhw.nchunks; ++i, ++k) { xz[2 * k] = d->fhw.chunks[i].cx; xz[2 * k + 1] = d->fhw.chunks[i].cz; }
    for (k = 0; k < n; ++k) sr_drop_entity_lists(d, xz[2 * k], xz[2 * k + 1]);
    free(xz);
}

void serverreplay_park(struct serverreplay *sr)
{
    /* the overworld spawner's records of mobs gone for good */
    if (sr->dims[0].live && sr->mobs_enabled) spawner_compact(&sr->dims[0].spawner);
    /* the mineshafts out of the player's reach: their pieces to the blob
     * file until a population meets them (a chunk populates within the view
     * distance, and its box reaches a chunk further) */
    if (sr->here == 0 && sr->player != NULL && sr->dims[0].npark > sr->dims[0].nwait)
        populate_spill_far(&sr->pop, 0, sr->player->e.pos_x, sr->player->e.pos_z, (sr->view + 3) * 16);
    for (int i = 0; i < 3; ++i)
    {
        struct sr_dimstate *d = &sr->dims[i];
        if (d->npark == 0) continue;
        if (d->live && d->npark > d->nwait) sr_saved_gc(d);
        if (d->npark > SR_PARK_MAX)
        {
            /* the whole store: the chunks first (a spill takes them out of
             * the map) */
            int n = 0;
            for (size_t k = 0; k < d->saved.cap; ++k)
                if (d->saved.slot[k] != 0) ++n;
            chunkref *all = malloc(((size_t)n + 1) * sizeof *all);
            n = 0;
            for (size_t k = 0; k < d->saved.cap; ++k)
                if (d->saved.slot[k] != 0) all[n++] = d->saved.slot[k];
            for (int k = 0; k < n; ++k) sr_park_chunk(d, i, chunk_ptr(all[k]));
            free(all);
            sr_drop_entity_lists_all(d);
            d->npark = d->nwait = 0;
            continue;
        }
        int keep = 0;
        for (int k = 0; k < d->npark; ++k)
        {
            const int cx = d->park[k][0], cz = d->park[k][1];
            /* gone again if a later load took it back */
            struct chunk *c = world_chunk(&d->saved, cx, cz);
            if (c != NULL && sr->park_rows - d->park_row[k] < SR_PARK_WAIT)
            {
                d->park[keep][0] = cx;
                d->park[keep][1] = cz;
                d->park_row[keep++] = d->park_row[k];
            }
            else if (c != NULL) sr_park_chunk(d, i, c);
            /* an unload's lists go at the park of its own row */
            if (k >= d->nwait) sr_drop_entity_lists(d, cx, cz);
        }
        d->npark = d->nwait = keep;
    }
    ++sr->park_rows;
}

static int sr_chunk_unload_cb(void *ctx, int cx, int cz)
{
    struct serverreplay *sr = ctx;
    sr_saved_init(sr);
    if (!sr->d->saved_ready && !sr->d->saved_ctx) sr_saved_context(sr);
    /* The join replay's unload steps (the login marks, ticks 2-3) hit chunks
     * the snapshot world does not hold: their ticks are already in sr->d->saved
     * with the clock their own unload wrote (the seed save). A save here
     * would read the live set, find nothing, and replace the saved entry
     * with an empty one. A live unload always takes a loaded chunk. */
    if (world_chunk(sr_dim_world(sr, sr->here), cx, cz) == NULL) return 0;
    /* Java saves before WorldServer.tick advances the clock. The native
     * unload callback runs after servertick_tick_head has advanced it. */
    sr_save_chunk_ticks(sr, cx, cz, sr_dim_ws(sr, sr->here)->st.total_time - (sr->here == 0 ? 1 : 0));
    /* the Nether and the End save like the overworld (every living kind by
     * its NBT); the End's crystals and dragon as their own records */
    int away = sr->here != 0;
    if (sr->here == 1) sr_end_save_entities(sr, cx, cz);
    /* A creature reloaded from NBT can be saved and loaded again. Keep its
     * live position and motion, and make its saved record available on the
     * next load. Chunk.onChunkUnload removes it after safeSaveChunk. */
    /* AnvilChunkLoader.writeChunkToNBT walks the chunk's entity lists
     * section by section, each in the order its entities entered it; the
     * reload adds them back in that order */
    struct an_chunk *ach = an_chunk_find(&sr->d->anw, cx, cz);
    for (int sec = 0; ach && sec < 16; ++sec)
    for (int j = 0; j < ach->sec[sec].n; ++j)
    {
        struct an_ent *en = an_ent_at(sec_items(&ach->sec[sec])[j]);
        if (en == an_deref(sr->player_enth) || (en->is_living && en->livh == sr->player_livh)) continue;
        if (!en->is_living && en->ieh && !ie_get(en->ieh)->is_dead && ie_get(en->ieh)->added_to_chunk &&
            ie_get(en->ieh)->chunk_x == cx && ie_get(en->ieh)->chunk_z == cz && (ie_get(en->ieh)->kind == IE_ITEM || ie_get(en->ieh)->kind == IE_ORB))
        {
            sr_save_item(sr, ie_get(en->ieh), 5, cx, cz);
            continue;
        }
        /* an arrow (a skeleton's, stuck or in flight), a throwable or a
         * fireball in flight is one of the chunk's entities too: its
         * writeEntityToNBT, rebuilt at the next load through the region-NBT
         * path */
        if (!en->is_living && en->ieh && !ie_get(en->ieh)->is_dead && ie_get(en->ieh)->added_to_chunk &&
            ie_get(en->ieh)->chunk_x == cx && ie_get(en->ieh)->chunk_z == cz && sr_saves_projectile(ie_get(en->ieh)->kind))
        {
            sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
            struct sr_saved_entity *sv = &sr->d->saved_ents[sr->d->nsaved_ents++];
            memset(sv, 0, sizeof *sv);
            sv->active.id = -1;
            sv->tag = ent_nbt_ie(ie_get(en->ieh));
            sv->pool = 9;
            sv->kind = ie_get(en->ieh)->kind;
            sv->seq = ie_get(en->ieh)->e.chunk_stamp;
            sv->cx = cx; sv->cz = cz; sv->section = ie_get(en->ieh)->chunk_y;
            sv->uuid_msb = ie_get(en->ieh)->uuid_msb;
            sv->uuid_lsb = ie_get(en->ieh)->uuid_lsb;
            continue;
        }
        if (!en->is_living || !lv_get(en->livh)->added_to_chunk ||
            lv_get(en->livh)->chunk_coord_x != cx || lv_get(en->livh)->chunk_coord_z != cz) continue;
        struct sr_saved_entity *saved = NULL;
        for (int i = 0; i < sr->d->nsaved_ents; ++i)
        {
            if (sr->d->saved_ents[i].loaded && sr_saved_active_is(sr, &sr->d->saved_ents[i], en) &&
                (sr->d->saved_ents[i].pool == 2 || sr->d->saved_ents[i].pool == 3))
            { saved = &sr->d->saved_ents[i]; break; }
        }
        struct living *l = lv_get(en->livh);
        /* Entity.writeToNBTOptional refuses a ridden entity of any kind (the
         * jockey's chicken too): it saves as the "Riding" subtree of its
         * rider's NBT */
        if (lv_get(l->ridden_by_entity) != NULL) continue;
        int cls = away ? -1 : l->kind == AK_SHEEP ? SW_SHEEP : l->kind == AK_PIG ? SW_PIG :
                  l->kind == AK_CHICKEN ? SW_CHICKEN : l->kind == AK_COW ? SW_COW :
                  l->kind == AK_MOOSHROOM ? SW_MOOSHROOM : -1;
        /* a farm animal that came back from a checkpoint's region NBT
         * (pool 3) reloads from its tag, so the tag is what it saves to; so
         * does a jockey's chicken whose rider is gone (the compact record
         * has no IsChickenJockey) */
        if (cls < 0 || (saved && saved->pool == 3) || (l->kind == AK_CHICKEN && l->is_chicken_jockey))
        {
            /* every other living kind saves its NBT (Entity.writeToNBTOptional:
             * not a dead one) and comes back through its constructor and
             * readFromNBT. */
            if (l->is_dead) continue;
            if (!saved)
            {
                sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
                saved = &sr->d->saved_ents[sr->d->nsaved_ents++];
                memset(saved, 0, sizeof *saved);
                saved->active.id = -1;
            }
            nbt_free(saved->tag);
            saved->tag = nbt_new_compound();
            living_write_nbt(l, saved->tag);
            saved->pool = 3;
            saved->kind = l->kind;
            saved->seq = l->e.chunk_stamp;
            saved->cx = cx; saved->cz = cz; saved->section = l->chunk_coord_y;
            saved->loaded = 0; saved->active = (struct sr_ent){0, -1};
            continue;
        }
        if (!saved)
        {
            sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
            saved = &sr->d->saved_ents[sr->d->nsaved_ents++];
            memset(saved, 0, sizeof *saved);
            saved->active.id = -1;
            saved->pool = 2;
            saved->ent.cls = cls;
        }
        struct sw_entity *e = &saved->ent;
        e->x = l->e.pos_x; e->y = l->e.pos_y; e->z = l->e.pos_z;
        e->motion_x = l->e.motion_x; e->motion_y = l->e.motion_y; e->motion_z = l->e.motion_z;
        e->yaw = l->rotation_yaw; e->pitch = l->rotation_pitch;
        e->fire = l->e.fire;
        e->uuid_msb = l->uuid_msb; e->uuid_lsb = l->uuid_lsb;
        e->fleece = l->data_watcher_16 & 15;
        struct attr_instance *follow = &l->attrs.a[ATTR_FOLLOW_RANGE];
        saved->no_follow_bonus = 1;
        for (int m = 0; m < follow->nmods; ++m)
            if (follow->mods[m].name == MODN_RANDOM_SPAWN_BONUS)
            {
                saved->no_follow_bonus = 0;
                e->follow_bonus = follow->mods[m].amount;
                e->follow_msb = follow->mods[m].uuid_msb;
                e->follow_lsb = follow->mods[m].uuid_lsb;
                break;
            }
        saved->health = l->health; saved->absorption = l->absorption;
        saved->fall_distance = l->e.fall_distance;
        saved->air = l->air; saved->on_ground = l->e.on_ground;
        saved->growing_age = l->growing_age; saved->in_love = l->in_love;
        saved->watcher16 = l->data_watcher_16; saved->egg_timer = l->time_until_next_egg;
        saved->persistence_required = l->persistence_required;
        memcpy(saved->custom_name, l->custom_name, sizeof saved->custom_name);
        saved->can_pick_up_loot = l->can_pick_up_loot;
        saved->leashed = l->is_leashed;
        saved->leash_pending = 0;
        if (l->is_leashed && l->leash_holder == LEASH_KNOT && l->leash_knot != 0)
        {
            saved->leash_pending = 2;
            saved->leash_x = l->leash_x;
            saved->leash_y = l->leash_y;
            saved->leash_z = l->leash_z;
        }
        else if (l->is_leashed && l->leash_holder == LEASH_PLAYER && sr->player != NULL)
        {
            saved->leash_pending = 1;
            ent_nbt_player_uuid(sr->player, &saved->leash_msb, &saved->leash_lsb);
        }
        saved->resaved = 1;
        saved->seq = l->e.chunk_stamp;
        saved->cx = cx; saved->cz = cz; saved->section = l->chunk_coord_y;
        saved->loaded = 0; saved->active = (struct sr_ent){0, -1};
    }
    /* the server's own items and orbs (the snapshot's, the block drops),
     * section by section after the living world's */
    for (int c = 0; c < sr->d->iew.nchunks; ++c)
    {
        const ie_chunk *ic = &sr->d->iew.chunks[c];
        if (!ic->used || ic->cx != cx || ic->cz != cz) continue;
        for (int sec = 0; sec < IE_SECTIONS; ++sec)
            for (int j = 0; j < ic->sec[sec].n; ++j)
            {
                ie_ent *ie = ie_ent_at(sec_items(&ic->sec[sec])[j]);
                if (ie->is_dead) continue;
                if (ie->kind == IE_TNT)
                {
                    /* a primed TNT: its NBT (the Fuse), rebuilt at the next
                     * load (sr_restore_tnt) */
                    sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                                  (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
                    struct sr_saved_entity *sv = &sr->d->saved_ents[sr->d->nsaved_ents++];
                    memset(sv, 0, sizeof *sv);
                    sv->active.id = -1;
                    sv->tag = ent_nbt_ie(ie);
                    sv->pool = 11;
                    sv->kind = ie->kind;
                    sv->seq = ie->e.chunk_stamp;
                    sv->cx = cx; sv->cz = cz; sv->section = ie->chunk_y;
                    continue;
                }
                if (ie->kind != IE_ITEM && ie->kind != IE_ORB) continue;
                sr_save_item(sr, ie, 4, cx, cz);
            }
    }

    /* AnvilChunkLoader saves the chunk's entity lists by section. Capture the
     * live falling blocks before the unload event removes their pool entries. */
    for (int k = 0; k < sr->d->nents; ++k)
    {
        if (sr->d->ents[k].pool != 1) continue;
        fh_ent *fh = sr_ent_p(sr, &sr->d->ents[k]);
        if (!fh->added_to_chunk || fh->chunk_x != cx || fh->chunk_z != cz) continue;
        /* a hanging entity is one of the chunk's own entities too: the same
         * write, into its own pool so the reload rebuilds it by NBT (pool 7) */
        if (fh->kind != FH_FALLING && fh->kind != FH_PAINTING && fh->kind != FH_FRAME) continue;
        int hanging = fh->kind != FH_FALLING;
        struct sr_saved_entity *saved = NULL;
        for (int i = 0; i < sr->d->nsaved_ents; ++i)
            if (sr_active_is(sr, &sr->d->saved_ents[i].active, 1, fh) && sr->d->saved_ents[i].loaded)
            { saved = &sr->d->saved_ents[i]; break; }
        if (!saved)
        {
            sr->d->saved_ents = slab_grow(sr->d->saved_ents, (size_t)sr->d->nsaved_ents * sizeof *sr->d->saved_ents,
                                   (size_t)(sr->d->nsaved_ents + 1) * sizeof *sr->d->saved_ents);
            saved = &sr->d->saved_ents[sr->d->nsaved_ents++];
            memset(saved, 0, sizeof *saved);
            saved->active.id = -1;
        }
        nbt_free(saved->tag);
        saved->tag = fh_write_nbt(fh);
        saved->pool = hanging ? 7 : 1;
        saved->kind = fh->kind;
        saved->seq = fh->e.chunk_stamp;
        saved->cx = cx; saved->cz = cz; saved->section = fh->chunk_y;
        saved->uuid_msb = fh->uuid_msb; saved->uuid_lsb = fh->uuid_lsb;
        saved->loaded = 0;
        saved->active = (struct sr_ent){0, -1};
    }
    return sr_store_chunk(sr, cx, cz);
}

static void sr_cl_event(void *ctx, const struct cl_event *e)
{
    struct serverreplay *sr = ctx;

    if (e->kind == 'p')
    {
        int depth = sr->pop_depth, id = sr->pop_id;
        sr->pop_depth = depth + 1;
        sr->pop_id = e->id;
        PHASE_SUB(PS_POP, sr_populate(sr, e->cx, e->cz));
        sr->pop_depth = depth;
        sr->pop_id = id;
        return;
    }

    if (e->kind != 'u') return;

    for (int g = 0; g < sr->npghost; ++g)
        if (sr->pghost[g].dim == sr->here && sr->pghost[g].cx == e->cx && sr->pghost[g].cz == e->cz)
        {
            sr_player_ghost_unload(sr, g);
            --g;
        }

    if (sr->here == 1) sr_end_unload_entities(sr, e->cx, e->cz);

    /* a traveller's ghost in this chunk's list goes with it */
    {
        struct an_chunk *gc = an_chunk_find(&sr->d->anw, e->cx, e->cz);
        for (int sec = 0; gc != NULL && sec < 16; ++sec)
            for (int j = gc->sec[sec].n - 1; j >= 0; --j)
            {
                struct an_ent *g = an_ent_at(sec_items(&gc->sec[sec])[j]);
                if (!g->used || !g->ghost || !g->is_living) continue;
                struct living *gl = lv_get(g->livh);
                sec_remove_first(&gc->sec[sec], an_ent_index(g));
                g->ghost = 0;
                g->used = 0;
                grave_bury(gl, g);
            }
    }

    ie_ent **ies = nw_scratch->unload_ies;
    ie_ent **mob_items = nw_scratch->unload_mob_items;
    fh_ent **fhs = nw_scratch->unload_fhs;
    int ni = ie_unload_chunk(&sr->d->iew, e->cx, e->cz, ies, sr->d->iew.n + 1);
    int nmi = ie_unload_chunk(&sr->d->anw.iew, e->cx, e->cz, mob_items, sr->d->anw.iew.n + 1);
    int nf = fh_unload_chunk(&sr->d->fhw, e->cx, e->cz, fhs, sr->d->fhw.n + 1);

    for (int i = 0; i < sr->d->anw.n; ++i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        struct living *l = lv_get(en->livh);
        int remove = en->is_living && l->added_to_chunk &&
                     l->chunk_coord_x == e->cx && l->chunk_coord_z == e->cz;
        if (!en->is_living)
            for (int k = 0; k < nmi; ++k)
                if (ie_get(en->ieh) == mob_items[k]) { remove = 1; break; }
        if (!remove) continue;
        an_chunk_remove(&sr->d->anw, en, en->is_living ? l->chunk_coord_y : ie_get(en->ieh)->chunk_y);
        if (en->is_living)
        {
            l->added_to_chunk = 0;
            spawner_track(&sr->d->spawner, l->entity_id, NULL);
        }
        en->used = 0;
        memmove(&sr->d->anw.slot[i], &sr->d->anw.slot[i + 1],
                (size_t)(sr->d->anw.n - i - 1) * sizeof sr->d->anw.slot[0]);
        --sr->d->anw.n;
        sr->d->anw.slot[sr->d->anw.n] = -1;
        --i;
        for (int k = 0; k < sr->d->nents; ++k)
            if (sr->d->ents[k].pool == 2 && sr_ent_p(sr, &sr->d->ents[k]) == en)
            {
                sr_order_remove(sr, k);
                break;
            }
        /* World.unloadEntities drops the object: what the chunk's save kept
         * of it is its NBT or record (a Nether or End living kept whole is
         * referred to by its record, which keeps it from the grave) */
        if (en->is_living && l != lv_get(sr->player_livh)) grave_bury(l, en);
    }

    for (int i = 0; i < nmi; ++i) ie_ent_release(mob_items[i]);

    for (int i = 0; i < ni; ++i)
    {
        for (int k = 0; k < sr->d->nents; ++k)
            if (sr->d->ents[k].pool == 0 && sr_ent_p(sr, &sr->d->ents[k]) == ies[i])
            {
                sr_order_remove(sr, k);
                break;
            }

        ie_ent_release(ies[i]);
    }

    for (int i = 0; i < nf; ++i)
    {
        for (int k = 0; k < sr->d->nents; ++k)
            if (sr->d->ents[k].pool == 1 && sr_ent_p(sr, &sr->d->ents[k]) == fhs[i])
            {
                sr_order_remove(sr, k);
                break;
            }

        nbt_free(fhs[i]->tile_entity_data);
        fh_ent_release(fhs[i]);
    }


}

int64_t sr_world_time(const struct serverreplay *sr) { return sr->w[0].st.world_time; }
int64_t sr_total_time(const struct serverreplay *sr) { return sr->w[0].st.total_time; }
int sr_block_writes(const struct serverreplay *sr) { return (int)sr->blk_count; }
uint64_t sr_block_hash(const struct serverreplay *sr) { return sr->blk_hash; }

uint64_t sr_world_rng(const struct serverreplay *sr)
{
    uint64_t rng = 0;
    int n = (nw_env->cfg.sr_negative & 4) ? 1 : sr->nworlds; /* the negative check */

    for (int i = 0; i < n; ++i)
    {
        rng = rng * 31 + (ST_RAND(&sr->w[i].st).seed & ((1ULL << 48) - 1));
        rng = rng * 31 + (uint64_t)(int64_t)ST_LCG(&sr->w[i].st);
    }

    return rng;
}

void sr_det(const struct serverreplay *sr, uint64_t *seeder, uint64_t *math, uint64_t *split)
{
    *seeder = det_seeder_state(&SR_DET(sr), DET_SERVER);
    *math = det_math_state(&SR_DET(sr), DET_SERVER);
    *split = det_split_state(&SR_DET(sr), DET_SERVER);
}

/* EntityPlayerMP.travelToDimension(1) in the End, from the exit portal:
 * the theEnd2 achievement, playerConqueredTheEnd, World.removeEntity (setDead: the next entity pass
 * drops it from loadedEntityList; out of playerEntities at once, so no mob,
 * orb or dragon sees it), S2B state 4 (the client shows GuiWinGame). From
 * now on processPlayer ignores the client's moves. */
static void sr_player_conquer_end(struct serverreplay *sr)
{
    struct server_player *p = sr->player;
    surv_add_stat(p, ACH_THE_END2, 1);
    /* World.removeEntity's setDead: the cursor and the grids spill */
    surv_server_set_dead(p);
    p->conquered_end = 1;
    sr->d->anw.playerh = 0;
    sr->d->anw.has_player = 0;
    sr->d->iew.player_gone = 1;
    sr->d->anw.iew.player_gone = 1;
    /* setDead: a mob chasing it lets go (isEntityAlive) */
    if (sr->player_livh != 0) lv_get(sr->player_livh)->is_dead = 1;
    /* removeEntity's onEntityRemoved runs at once for a player: every entry
     * lets it go, so an orb it takes later in this update sends no S0D */
    combat_player_removed(sr);
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S2B;
    pkt->i0 = 4;
}

/* World.getTopSolidOrLiquidBlock over the replay's world. */
int sr_top_solid_or_liquid(struct world *w, int x, int z)
{
    struct chunk *c = world_load_chunk(w, x >> 4, z >> 4);
    int top = 0;

    if (c == NULL) return -1;
    for (int s = 15; s >= 0; --s)
        if (c->mask & (1 << s)) { top = s * 16; break; }

    for (int y = top + 15; y > 0; --y)
    {
        int id = world_get_block(w, x, y, z) & 4095;
        const struct material_def *m = &MATERIALS[BLOCKS[id].material];
        if (m->blocks_movement && strcmp(m->name, "leaves") != 0) return y + 1;
    }

    return -1;
}

/* NetHandlerPlayServer.processClientStatus(PERFORM_RESPAWN) after the
 * credits: ServerConfigurationManager.respawnPlayer(player, 0, true). The
 * End's player manager lets the player go; a new EntityPlayerMP (its
 * constructor's Det draws, then the spawn point fuzzed by its own Random over
 * the spawn protection, on the top solid block) takes the old one's entity
 * id and clonePlayer(old, true)'s state: the inventory, health, food, the
 * experience and the score. It joins the overworld's player manager and the
 * tail of its loadedEntityList; the client gets S07, the S08, the S1F and
 * the window items. */
/* respawnPlayer's leaving half for a player in another dimension (the End
 * after the credits, a death in the Nether or the End): the old world's
 * player manager and lists let it go, and it enters the overworld, where
 * the fresh EntityPlayerMP is built. */
static void sr_respawn_depart(struct serverreplay *sr)
{
    struct server_player *p = sr->player;
    int t = sr->current_tick;

    /* the old world: its player manager and its lists */
    serverreplay_enter(sr, p->dimension);
    sr_action_unhook(sr);
    cl_logout(sr->d->cl, t);
    int k = sr_order_find_player(sr);
    if (k >= 0) sr_order_remove(sr, k);
    if (sr->player_livh != 0 && lv_get(sr->player_livh)->added_to_chunk)
    {
        an_chunk_remove(&sr->d->anw, an_deref(sr->player_enth), lv_get(sr->player_livh)->chunk_coord_y);
        lv_get(sr->player_livh)->added_to_chunk = 0;
    }
    /* the old EntityPlayerMP stays whatever the End's orbs cached, frozen
     * once respawnPlayer's setPlayerLocation has moved it to the fresh
     * player's placement (sr_respawn_old_moved) */
    sr->departed = p->e;
    sr->d->anw.playerh = 0;
    sr->d->iew.player = &sr->departed;
    sr->d->anw.iew.player = &sr->departed;

    p->dimension = 0;
    p->portal.dimension = 0;
    p->conquered_end = 0;
    if (sr->player_livh != 0) lv_get(sr->player_livh)->is_dead = 0;
    serverreplay_enter(sr, 0);
    sr_action_hooks(sr);
    struct world *ow = &sr->pop.world;
    p->e.world = ow;
    if (sr->player_livh != 0) lv_get(sr->player_livh)->e.world = lv_get(sr->player_livh)->world = ow;
    /* the fresh player's ItemInWorldManager is the overworld's */
    surv_dig_set_world(ow);
}

/* respawnPlayer's arriving half, after the placement: the overworld's
 * loadedEntityList takes the player at its tail. */
static void sr_respawn_arrive(struct serverreplay *sr)
{
    struct server_player *p = sr->player;

    sr_order_push(sr, 3, p);
    sr->player_unlisted = 0;
    p->offworld = 0;
    sr->d->anw.playerh = lv_ref(lv_get(sr->player_livh));
    sr->d->iew.player = &p->e;
    sr->d->anw.iew.player = &p->e;
    sr->d->iew.player_gone = 0;
    sr->d->anw.iew.player_gone = 0;
    sr->action_first_item = sr->d->iew.n;
    sr->action_an_n = sr->d->anw.n;
    sr->action_moved = 1;
}

void serverreplay_respawn_conquered(struct serverreplay *sr)
{
    for (int g = 0; g < sr->npghost; ++g) sr->pghost[g].same = 0;

    struct server_player *p = sr->player;
    int t = sr->current_tick;
    /* removePlayerEntityDangerously's setDead (the exit portal's already
     * spilled the cursor and the grids; an open chest closes again) */
    surv_server_set_dead(p);
    struct surv_clone *keep ENV_LOCAL = envstack_take(sizeof *keep);
    surv_clone_take(keep, &p->sv);

    /* the old object stays in the End */
    p->life_dim[p->life & 7] = p->dimension;
    ++p->life;

    sr_respawn_depart(sr);
    struct world *ow = &sr->pop.world;

    /* new EntityPlayerMP(overworld): the constructor's draws and fresh
     * state (surv_server_fresh's reset), the spawn fuzz on the new Random;
     * clonePlayer(old, true) keeps the teleportDirection */
    int teleport_dir = p->portal.teleport_direction;
    surv_server_fresh_state(p);
    p->portal.teleport_direction = teleport_dir;
    int sx = sr->d->spawner.spawn_x, sz = sr->d->spawner.spawn_z;
    int x = sx + det_rng_int_n(&p->sv.erand, 20) - 10;
    int z = sz + det_rng_int_n(&p->sv.erand, 20) - 10;
    int y = sr_top_solid_or_liquid(ow, x, z);
    entity_set_pos_rot(&p->e, (double)x + 0.5, (double)y, (double)z + 0.5, &p->rotation_yaw, &p->rotation_pitch,
                       &p->prev_rotation_yaw, &p->prev_rotation_pitch, 0.0F, 0.0F);
    p->e.prev_pos_x = p->e.pos_x;
    p->e.prev_pos_y = p->e.pos_y;
    p->e.prev_pos_z = p->e.pos_z;
    while (!world_colliding_boxes_empty(ow, p->e.bounding_box))
        entity_set_position(&p->e, p->e.pos_x, p->e.pos_y + 1.0, p->e.pos_z);

    /* the bed, as for a death respawn: verifyRespawnCoordinates in the
     * overworld; clonePlayer does not carry the spawn chunk, so the fresh
     * player keeps it only when the bed still stands (setSpawnChunk), and a
     * missing bed sends tile.bed.notValid */
    if (p->sv.has_spawn)
    {
        int rx, rz;

        if (surv_verify_respawn(ow, p->sv.spawn_x, p->sv.spawn_y, p->sv.spawn_z, &rx, &rz))
        {
            entity_set_pos_rot(&p->e, (double)((float)rx + 0.5F), (double)((float)p->sv.spawn_y + 0.1F),
                               (double)((float)rz + 0.5F), &p->rotation_yaw, &p->rotation_pitch,
                               &p->prev_rotation_yaw, &p->prev_rotation_pitch, 0.0F, 0.0F);
            p->e.prev_pos_x = p->e.pos_x;
            p->e.prev_pos_y = p->e.pos_y;
            p->e.prev_pos_z = p->e.pos_z;
        }
        else
        {
            struct s2c_pkt *bad = s2c_add(s2c_out());
            bad->kind = PK_S2B;
            bad->i0 = 0;
            p->sv.has_spawn = 0;
        }
    }

    /* clonePlayer(old, true); EntityPlayerMP's own resets the S06/S1F gates */
    memcpy(p->sv.inv, keep->inv, sizeof p->sv.inv);
    p->sv.current_item = keep->current_item;
    p->sv.health = keep->health;
    p->sv.food = keep->food;
    p->sv.xp_level = keep->xp_level;
    p->sv.xp_total = keep->xp_total;
    p->sv.xp_progress = keep->xp_progress;
    p->sv.score = keep->score;
    p->sv.last_xp_total = -1;
    p->sv.last_health = -1.0F;
    p->sv.last_food = -1;

    /* the spawn chunk, the push-up, the packets */
    world_load_chunk(ow, (int)p->e.pos_x >> 4, (int)p->e.pos_z >> 4);
    while (!world_colliding_boxes_empty(ow, p->e.bounding_box))
        entity_set_position(&p->e, p->e.pos_x, p->e.pos_y + 1.0, p->e.pos_z);
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S07;
    pkt->i0 = 0;
    sr_set_player_location(p, p->e.pos_x, p->e.pos_y, p->e.pos_z, p->rotation_yaw, p->rotation_pitch);
    entity_set_position(&sr->departed, p->e.pos_x, p->e.pos_y, p->e.pos_z);
    pkt = s2c_add(s2c_out());
    pkt->kind = PK_S1F;
    pkt->f0 = p->sv.xp_progress;
    pkt->i0 = p->sv.xp_total;
    pkt->i1 = p->sv.xp_level;

    /* the overworld's player manager and list; its queued chunks go out
     * five a tick from the player's onUpdate */
    cl_login(sr->d->cl, t, p->e.pos_x, p->e.pos_z);
    sr_respawn_arrive(sr);
    int side = 2 * cl_radius(sr->d->cl) + 1;
    if (sr->capsend < side * side * 2)
    {
        sr->send_queue = slab_grow(sr->send_queue, sizeof(int) * (size_t)sr->capsend, sizeof(int) * (size_t)side * side * 2);
        sr->capsend = side * side * 2;
    }
    sr->nsend = cl_send_order(sr->d->cl, p->e.pos_x, p->e.pos_z, sr->send_queue);
    sr->send_dim = 0;

    /* addSelfToInternalCraftingInventory: the window items and the cursor
     * (addCraftingToCrafters); respawnPlayer sends no S09, so the fresh
     * client player keeps currentItem 0 */
    surv_server_send_container(p);
    p->sv.last_health = -1.0F;
    surv.world_rand = &ST_RAND(&sr_dim_ws(sr, 0)->st);
}

void serverreplay_player_fresh(struct serverreplay *sr)
{
    struct living *l = sr->player_livh ? lv_get(sr->player_livh) : NULL;
    if (l == NULL) return;

    /* the twin stands for the new object: its effects and their attribute
     * modifiers stay with the old one (clonePlayer copies none), and the
     * constructor's potionsNeedUpdate and DataWatcher 7 and 8 */
    uint8_t ids[POT_COUNT];
    int n = potion_map_order(&l->potions, ids);
    for (int i = 0; i < n; ++i)
        potion_remove_attributes(l, l->potions.eff[ids[i]].id, l->potions.eff[ids[i]].amplifier);
    potion_map_init(&l->potions);
    l->potions_need_update = 1;
    l->potion_liquid_color = 0;
    l->potion_is_ambient = 0;
}

void serverreplay_respawn_leave(struct serverreplay *sr)
{
    /* respawnPlayer's new EntityPlayerMP: the ghosts stay the old object */
    for (int g = 0; g < sr->npghost; ++g) sr->pghost[g].same = 0;

    /* processClientStatus's respawnPlayer(player, 0, false): a death in the
     * Nether or the End respawns in the overworld */
    if (serverreplay_player_dim(sr) != 0)
    {
        sr_respawn_depart(sr);
        sr->respawn_arriving = 1;
        return;
    }
    serverreplay_enter(sr, 0);
    cl_logout(sr->d->cl, sr->current_tick);
}

void serverreplay_resend_block(struct serverreplay *sr, int x, int y, int z)
{
    if (sr->nresend >= (int)(sizeof sr->resend / sizeof sr->resend[0]))
    {
        fprintf(stderr, "serverreplay: more than %d S23 resends in one tick\n", SR_RESEND_CAP);
        abort();
    }
    int *r = sr->resend[sr->nresend++];
    struct world *w = sr_dim_world(sr, serverreplay_player_dim(sr));
    r[0] = x;
    r[1] = y;
    r[2] = z;
    r[3] = serverreplay_player_dim(sr);
    r[4] = y >= 0 && y < 256 ? world_get_block(w, x, y, z) : 0;
    r[5] = y >= 0 && y < 256 ? world_get_meta(w, x, y, z) : 0;
}

/* respawnPlayer's setPlayerLocation runs on the net handler, whose
 * playerEntity is still the old EntityPlayerMP: the old object, which orbs
 * may hold as closestPlayer, moves to the fresh player's placement */
static void sr_respawn_old_moved(struct serverreplay *sr, const struct entity *at)
{
    if (sr->respawn_arriving)
    {
        entity_set_position(&sr->departed, at->pos_x, at->pos_y, at->pos_z);
        return;
    }
    ie_player_left(&sr->d->iew, at);
    ie_player_left(&sr->d->anw.iew, at);
}

void serverreplay_respawn_to_tail(struct serverreplay *sr)
{
    if (sr->player != NULL) sr_respawn_old_moved(sr, &sr->player->e);
    if (sr->respawn_arriving)
    {
        sr->respawn_arriving = 0;
        sr_respawn_arrive(sr);
        surv.world_rand = &ST_RAND(&sr_dim_ws(sr, 0)->st);
        return;
    }
    serverreplay_player_to_tail(sr);
}

void serverreplay_respawn_load(struct serverreplay *sr, double x, double z)
{
    if (serverreplay_player_dim(sr) != 0) return;
    serverreplay_enter(sr, 0);
    world_load_chunk(&sr->pop.world, (int)x >> 4, (int)z >> 4);
}

void serverreplay_respawn_join(struct serverreplay *sr, double x, double z)
{
    if (serverreplay_player_dim(sr) != 0) return;
    serverreplay_enter(sr, 0);
    cl_login(sr->d->cl, sr->current_tick, x, z);
    int side = 2 * cl_radius(sr->d->cl) + 1;
    if (sr->capsend < side * side * 2)
    {
        sr->send_queue = slab_grow(sr->send_queue, sizeof(int) * (size_t)sr->capsend, sizeof(int) * (size_t)side * side * 2);
        sr->capsend = side * side * 2;
    }
    sr->nsend = cl_send_order(sr->d->cl, x, z, sr->send_queue);
    sr->send_dim = 0;
}

void serverreplay_pause_save(struct serverreplay *sr, int t)
{
    int here = sr->here;

    serverreplay_enter(sr, 0);
    cl_save_marks(sr->d->cl, t);
    if (sr->hell_live)
    {
        serverreplay_enter(sr, -1);
        cl_save_marks(sr->d->cl, t);
    }
    if (sr->end_live)
    {
        serverreplay_enter(sr, 1);
        cl_save_marks(sr->d->cl, t);
    }
    serverreplay_enter(sr, here);
}
