/* The snapshot session both the playable client and the replay gate run
 * (session.h). This was two copies, play.c's and test_snapshots.c's, and the
 * client fell behind whenever a fix reached only the gate's. */
#define _XOPEN_SOURCE 700 /* realpath */
#include "session.h"
#include "env.h"
#include "envstack.h"
#include "jmath.h"
#include "jorder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "combat.h"
#include "container.h"
#include "envstack.h"
#include "grave.h"
#include "nbtedit.h"
#include "nbtjson.h"
#include "particles_live.h"
#include "serverreplay.h"
#include "snapshot.h"
#include "tape.h"

static int fail(struct session *ss, const char *fmt, const char *a, const char *b)
{
    snprintf(ss->err, sizeof ss->err, fmt, a ? a : "", b ? b : "");
    return 0;
}

/* A canonical scalar as bare text ("i:525"): the quotes and a "str:" prefix
 * come off. */
static void nbt_scalar(const nbt *comp, const char *key, char *b, size_t n)
{
    const nbt *v = comp ? nbt_get(comp, key) : NULL;
    if (!v) { snprintf(b, n, "-"); return; }
    char *t = nbt_render(v);
    size_t len = strlen(t);
    const char *p = t;
    if (len >= 2 && t[0] == '"' && t[len - 1] == '"') { t[len - 1] = 0; p = t + 1; }
    if (!strncmp(p, "str:", 4)) p += 4;
    snprintf(b, n, "%s", p);
    free(t);
}

static int det_l(const nbt *tag, int64_t *out)
{
    if (!tag) return 0;

    char *t = nbt_render(tag);
    const char *p = t;
    size_t n = strlen(t);

    if (n >= 2 && t[0] == '"' && t[n - 1] == '"') { t[n - 1] = 0; p = t + 1; }
    int ok = p[1] == ':';
    if (ok) *out = strtoll(p + 2, NULL, 10);
    free(t);
    return ok;
}

/* The snapshot's det.nbt as a det_state: the 48-bit stream states, the
 * per-role entity id counters and the splits, exactly det_load + det_split_add
 * read them. The roles are in CLIENT SERVER OTHER RENDER order. */
static int det_from_snapshot(det_state *det, const nbt *tree, int64_t seed)
{
    int64_t l;
    uint64_t seeder[DET_ROLES] = {0}, mathv[DET_ROLES] = {0};
    int32_t next_id[DET_ROLES] = {0};
    const nbt *seeder_l = nbt_get(tree, "seeder");
    const nbt *math_l = nbt_get(tree, "math");
    const nbt *ids = nbt_get(tree, "nextId");

    if (!seeder_l || !math_l || !ids || nbt_kind(seeder_l) != NBT_LIST ||
        nbt_kind(math_l) != NBT_LIST || nbt_kind(ids) != NBT_LIST) return 0;

    for (int r = 0; r < DET_ROLES; ++r)
    {
        if (!det_l(nbt_list_get(seeder_l, r), &l)) return 0;
        seeder[r] = (uint64_t)l;
        if (!det_l(nbt_list_get(math_l, r), &l)) return 0;
        mathv[r] = (uint64_t)l;
        if (!det_l(nbt_list_get(ids, r), &l)) return 0;
        next_id[r] = (int32_t)l;
    }

    det_load(det, seed, seeder, mathv, next_id);

    const nbt *splits = nbt_get(tree, "splits");

    if (splits && nbt_kind(splits) == NBT_LIST)
    {
        for (int i = 0; i < nbt_list_size(splits); ++i)
        {
            const nbt *e = nbt_list_get(splits, i);
            if (!e || nbt_kind(e) != NBT_COMPOUND) continue;

            const nbt *states = nbt_get(e, "state");
            const nbt *used = nbt_get(e, "used");
            if (!states || !used) continue;

            uint64_t st[DET_ROLES] = {0};
            uint8_t us[DET_ROLES] = {0};

            for (int r = 0; r < DET_ROLES; ++r)
            {
                if (!det_l(nbt_list_get(states, r), &l)) return 0;
                st[r] = (uint64_t)l;
                if (!det_l(nbt_list_get(used, r), &l)) return 0;
                us[r] = (uint8_t)l;
            }

            char name[DET_NAME_MAX];
            nbt_scalar(e, "name", name, sizeof name);
            det_split_add(det, name, st, us);
        }
    }

    return 1;
}

int session_refuse_dev(const struct jval *hdr, const struct jval *manifest, int dev_build)
{
    const struct jval *flag = json_get(hdr, "dev");
    int dev_tape = flag && flag->kind == J_BOOL && flag->boolean;
    if (dev_tape && !dev_build) return 1;

    const struct jval *snapshot_dev = json_get(manifest, "dev");
    if (!dev_tape && snapshot_dev && snapshot_dev->kind == J_BOOL && snapshot_dev->boolean) return 1;

    const struct jval *setups = json_get(hdr, "setup");
    const char *mode = json_str(json_get(hdr, "mode"));
    for (int i = 0; i < json_len(setups); ++i)
    {
        const char *cls = json_str(json_get(json_at(setups, i), "class"));
        int legacy = cls && (!strcmp(cls, "MobFree") || !strcmp(cls, "MobMove"));
        if ((!dev_tape && !legacy) || (cls && !strcmp(cls, "Dev") && mode && !strcmp(mode, "play"))) return 1;
    }
    return 0;
}

/* NetHandlerPlayClient.handleRespawn's new WorldClient: the S07 asks for an
 * empty one of the new dimension, filled by the server's chunk bulks. */
static void client_world_change(void *ctx, int dim)
{
    struct session *ss = ctx;

    /* handleRespawn comes after the packets the server sent ahead of the
     * S07 (the old world's tracker pass): they build their entities in the
     * old WorldClient first */
    combat_client_flush(&ss->cp);
    if (ss->client_world) { world_free(ss->client_world); free(ss->client_world); }

    struct world *w = malloc(sizeof *w);
    world_init(w, ss->s->seed);
    w->no_generate = 1;
    w->is_remote = 1;
    w->dim = dim;
    ss->client_world = w;
    ss->cp.e.world = w;
    serverreplay_set_client(ss->sr, w);
    combat_client_world_change(&ss->cp);
    if (ss->hooks.world_changed) ss->hooks.world_changed(ss->hooks.ctx, dim);
}

/* The S26 chunk bulks the server's last tick sent, inserted before the
 * client's pump reads the packets that followed them. */
static void client_take_chunks(struct session *ss)
{
    struct sr_sent_chunk sent[5];
    int n = serverreplay_take_sent(ss->sr, sent, 5);
    struct world *client = ss->client_world;

    for (int i = 0; i < n; ++i)
    {
        struct chunk *c = sent[i].c;

        if (client != NULL && sent[i].dim == client->dim)
        {
            struct chunk *old = world_take_chunk(client, c->cx, c->cz);
            chunk_free(old);
            world_put_chunk(client, c);
        }
        else chunk_free(c);
    }
}

/* A checkpoint start replays from the checkpoint's own save: the start.dir
 * the tape header carries (a dead lane path falls back to this snapshot
 * tree's checkpoints/<seed>/<name>) becomes sr->save_dir, which makes the
 * whole-server replay load its chunks from the checkpoint's region files. */
static int resolve_checkpoint(struct session *ss, const char *dir, const char *rec_dir)
{
    struct serverreplay *sr = ss->sr;
    const struct jval *st = json_get(ss->hdr, "start");
    const char *kind = json_str(json_get(st, "kind"));
    const char *sdir = json_str(json_get(st, "dir"));
    struct stat sb;

    if (kind && strcmp(kind, "checkpoint") == 0 && sdir && sdir[0])
    {
        char real[4096];
        char path[4096];
        int ok = stat(sdir, &sb) == 0 && S_ISDIR(sb.st_mode);
        if (ok) snprintf(path, sizeof path, "%s", sdir);
        else if (realpath(rec_dir, real) == NULL) ok = 0;
        else
        {
            /* the tape was recorded in another tree: the checkpoint lives
             * next to the recording (a symlink's target) */
            const char *base = strrchr(sdir, '/');
            base = base ? base + 1 : sdir;
            snprintf(path, sizeof path, "%.3800s/../../checkpoints/%lld/%.100s",
                     real, (long long)ss->s->seed, base);
            ok = stat(path, &sb) == 0 && S_ISDIR(sb.st_mode);
        }
        if (!ok) return fail(ss, "checkpoint start.dir %s does not resolve", sdir, NULL);

        /* the checkpoint's seed must be the snapshot's */
        char mpath[4600];
        snprintf(mpath, sizeof mpath, "%s/manifest.json", path);
        FILE *f = fopen(mpath, "rb");
        if (!f) return fail(ss, "checkpoint manifest %s missing", mpath, NULL);
        char text[65536];
        size_t n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        /* json_parse takes the text: it frees it with the tree */
        struct jval *mj = json_parse(strdup(text));
        int64_t mseed = 0;
        json_int(json_get(mj, "seed"), &mseed);
        json_free(mj);
        if (mseed != ss->s->seed)
        {
            char a[32], b[32];
            snprintf(a, sizeof a, "%lld", (long long)mseed);
            snprintf(b, sizeof b, "%lld", (long long)ss->s->seed);
            return fail(ss, "checkpoint seed %s, the tape says %s", a, b);
        }
        snprintf(sr->save_dir, sizeof sr->save_dir, "%.1000s/save", path);
    }

    /* the chunks the join ticks saved (SaveOverlay.java), or a mid-run
     * snapshot's of a seed start */
    char jpath[4200];
    snprintf(jpath, sizeof jpath, "%.4000s/save/region", dir);
    if (stat(jpath, &sb) == 0 && S_ISDIR(sb.st_mode))
        snprintf(sr->join_save_dir, sizeof sr->join_save_dir, "%.1000s/save", dir);
    return 1;
}

/* EntityLivingBase.previousEquipment[0], which the snapshot does not
 * record, read back from the held stack's modifier on the snapshot's
 * attackDamage (a tool's and a sword's share one UUID): the modifier is what
 * the last equipment check applied. A stack that reached the hand since then
 * (a rejoin's inventory, after the join's first update) has none yet; the
 * next check applies it. Without Attributes (an old snapshot): the held stack. */
static int held_attr_seed(const struct server_player *sp, const nbt *tag)
{
    int held = sp->sv.current_item;
    int item = held >= 0 && held < 9 && sp->sv.inv[held].count > 0 ? sp->sv.inv[held].item : -1;
    const nbt *a = tag ? nbt_get(tag, "Attributes") : NULL;
    if (!a) return item;
    int weapon = item >= 0 && item < 4096 && (ITEMS[item].kind == ITEM_TOOL || ITEMS[item].kind == ITEM_SWORD);
    for (int i = 0; i < nbt_list_size(a); ++i)
    {
        const nbt *one = nbt_list_get(a, i);
        const char *name = nbt_string_value(nbt_get(one, "Name"));
        if (!name || strcmp(name, "generic.attackDamage") != 0) continue;
        const nbt *mods = nbt_get(one, "Modifiers");
        for (int k = 0; mods && k < nbt_list_size(mods); ++k)
        {
            const nbt *m = nbt_list_get(mods, k);
            if (nbt_int_value(nbt_get(m, "UUIDMost")) != -3801225194067177672L) continue;
            const char *mn = nbt_string_value(nbt_get(m, "Name"));
            int kind = mn && strcmp(mn, "Weapon modifier") == 0 ? ITEM_SWORD : ITEM_TOOL;
            uint64_t bits = nbt_double_bits(nbt_get(m, "Amount"));
            double amount;
            memcpy(&amount, &bits, sizeof amount);
            if (weapon && ITEMS[item].kind == kind && (double)ITEMS[item].damage == amount) return item;
            /* another stack's: only its kind and damage are read */
            for (int j = 0; j < 4096; ++j)
                if (ITEMS[j].kind == kind && (double)ITEMS[j].damage == amount) return j;
            return item;
        }
        /* no modifier: the last check saw no tool or sword in the hand */
        return weapon ? -1 : item;
    }
    return item;
}

/* The whole server, when the snapshot is one (onlyPlayers, or v2). */
static int open_server(struct session *ss, const char *dir, const char *rec_dir)
{
    struct snapshot *s = ss->s;
    struct serverreplay *sr = ss->sr = calloc(1, sizeof *ss->sr);
    if (!sr) return fail(ss, "cannot allocate the server replay", NULL, NULL);

    if (!serverreplay_load(sr, s, dir)) return fail(ss, "serverreplay_load: %s", sr->err, NULL);

    const struct jval *qj = json_get(json_get(ss->hdr, "start"), "quietJoin");
    sr->quiet_join = qj && qj->kind == J_BOOL && qj->boolean;
    const struct jval *spn = json_get(json_get(ss->hdr, "start"), "spawnerPin");
    ss->spawner_pin = spn && spn->kind == J_BOOL && spn->boolean;
    if (!resolve_checkpoint(ss, dir, rec_dir)) return 0;

    /* The client predicts a break locally before its C07 reaches the
     * server: its own world keeps that prediction out of the authoritative
     * one. The client is in the player's dimension: its chunk packets and
     * the S07 swap key off the same dim the server player carries. */
    struct world *cw = ss->client_world = malloc(sizeof *cw);
    if (!cw) return fail(ss, "cannot allocate the client world", NULL, NULL);
    world_init(cw, s->seed);
    cw->no_generate = 1;
    cw->dim = ss->sp.dimension;
    cw->is_remote = 1;

    /* a v2 snapshot's client world is empty (clientworld.nbt) and fills
     * from the server's chunk packets; an older one starts as the whole
     * server world, which it never leaves */
    for (size_t i = 0; !sr->stream_chunks && i < sr->pop.world.cap; ++i)
    {
        const struct chunk *src = chunk_ptr(sr->pop.world.slot[i]);
        if (!src) continue;
        struct chunk *dst = chunk_new();
        chunk_share(dst, src);
        memset(&dst->tes, 0, sizeof dst->tes);
        serverreplay_client_chunk_fresh(dst, cw->dim);
        world_put_chunk(cw, dst);
    }

    /* a resumed chunk stream starts from the client's own chunk list */
    serverreplay_client_prefill(sr, s, cw);
    serverreplay_set_client(sr, cw);
    /* WorldClient.func_147456_g's sets as the snapshot left them (none
     * before the snapshots recorded them: the walk starts afresh) */
    {
        int np = 0;
        const nbt *pa = s->clientworld ? nbt_get(s->clientworld, "prevActive") : NULL;
        const int *pv = pa ? nbt_int_array(pa, &np) : NULL;
        ss->cw_nprev = 0;
        for (int i = 0; pv && i + 1 < np && ss->cw_nprev < 128; i += 2)
        {
            ss->cw_prev[ss->cw_nprev][0] = pv[i];
            ss->cw_prev[ss->cw_nprev][1] = pv[i + 1];
            ++ss->cw_nprev;
        }
        const nbt *ac = s->clientworld ? nbt_get(s->clientworld, "activeCap") : NULL;
        ss->cw_active_cap = ac ? (int)nbt_int_value(ac) : 0;
    }

    ss->cp.e.world = cw;
    ss->sp.e.world = sr->join_dim == -1 ? &sr->hell.world : sr->join_dim == 1 ? &sr->sky.world : &sr->pop.world;
    int player_id = -1;
    for (int i = 0; i < s->nents; ++i) if (s->ents[i].player) { player_id = s->ents[i].id; break; }
    /* a player dead at the snapshot has left loadedEntityList (it stays in
     * playerEntities until the respawn): its id is its runtime's */
    int player_removed = 0;
    if (player_id < 0) player_id = serverreplay_dead_player_id(s, &ss->sp, &player_removed);
    if (player_id < 0) return fail(ss, "the snapshot has no server player entity", NULL, NULL);
    /* the player's entry sits in its own dimension's entity order; the bind
     * moves it to the head of that list (serverreplay_load's tail left the
     * overworld active) */
    serverreplay_enter(sr, sr->join_dim);
    serverreplay_bind_player(sr, &ss->sp, player_id);
    serverreplay_enter(sr, 0);
    /* the player's own tracker entry: its ticks counter as the snapshot
     * recorded it (0 for a snapshot before the entries were recorded) */
    ss->sp.trk_ticks = 0;
    ss->sp.trk_w_valid = 0;
    for (int i = 0; i < sr->ntrk; ++i)
        if (sr->trk[i].id == player_id) ss->sp.trk_ticks = sr->trk[i].v[0];
    combat_bind(sr, &ss->cp);
    ss->cp.on_world_change = client_world_change;
    ss->cp.world_change_ctx = ss;
    ss->cp.dimension = ss->sp.dimension;

    /* a v1 snapshot pins chunk generation off; a v2 one says per world */
    const struct jval *ng = json_get(json_get(s->manifest_json, "server"), "noChunkGeneration");
    ss->no_gen = 0;
    if (ng && json_len(ng) > 0)
    {
        int64_t b = 0;
        json_int(json_at(ng, 0), &b);
        ss->no_gen = (int)b;
    }
    s->world.no_generate = ss->no_gen;
    sr->pop.world.no_generate = ss->no_gen;
    return 1;
}

int session_open(struct session *ss, struct snapshot *s, const char *dir, const struct jval *hdr)
{
    return session_open_at(ss, s, dir, dir, hdr);
}

static int session_open_body(struct session *ss, struct snapshot *s, const char *dir, const char *rec_dir,
                             const struct jval *hdr);

/* An image's directory (image.h img_dir): where the session's parts sit,
 * as offsets from the environment. */
static int64_t dir_off(const void *p)
{
    return p ? (int64_t)((const unsigned char *)p - (const unsigned char *)nw_env) : 0;
}

static void session_directory(struct session *ss)
{
    struct img_dir *d = &nw_env->img.dir;

    memset(d, 0, sizeof *d);
    d->session = dir_off(ss);
    d->snapshot = dir_off(ss->s);
    d->client_world = dir_off(ss->client_world);
    if (ss->sr == NULL) return;
    d->sr = dir_off(ss->sr);
    for (int i = 0; i < ss->sr->nworlds && i < 3; ++i)
    {
        d->sr_world[i] = dir_off(&ss->sr->w[i]);
        d->world[i] = dir_off(ss->sr->w[i].st.w);
    }
    for (int k = 0; k < 3; ++k) d->dim[k] = dir_off(&ss->sr->dims[k]);
}

/* The load runs with the environment's image heap on (image.h): what it
 * makes is the environment's. */
int session_open_at(struct session *ss, struct snapshot *s, const char *dir, const char *rec_dir, const struct jval *hdr)
{
    int heap = image_heap_set(nw_env, 1);
    int ok = session_open_body(ss, s, dir, rec_dir, hdr);

    if (ok) session_directory(ss);
    image_heap_set(nw_env, heap);
    return ok;
}

static int session_open_body(struct session *ss, struct snapshot *s, const char *dir, const char *rec_dir,
                             const struct jval *hdr)
{
    memset(ss, 0, sizeof *ss);
    s2c_reset();
    ss->s = s;
    ss->hdr = hdr;
    ss->setups = json_get(hdr, "setup");
    /* the header's render distance (options.rd, 4 without), the client
     * world's active square until the client sends its own */
    {
        int64_t hrd = 4;
        json_int(json_get(json_get(hdr, "options"), "rd"), &hrd);
        ss->hdr_rd = (int)hrd;
    }
    /* the recording's GameSettings.particleSetting (options.particles; a
     * tape without it is the pinned profile's minimal) */
    {
        int64_t particles = 2;
        json_int(json_get(json_get(hdr, "options"), "particles"), &particles);
        nw_env->cfg.particle_setting = (int)particles + 1;
        surv_fx.setting = (int)particles;
        int64_t fancy = 0;
        json_int(json_get(json_get(hdr, "options"), "fancy"), &fancy);
        surv_fx.fancy = (int)fancy;
    }

    if (!client_player_load(&ss->cp, s->player_client, &s->world))
        return fail(ss, "the client player snapshot did not load", NULL, NULL);
    if (!server_player_load(&ss->sp, s->player_server, &s->world))
        return fail(ss, "the server player snapshot did not load", NULL, NULL);
    if (!surv_client_load(&ss->cp, s->player_client))
        return fail(ss, "the client survival snapshot did not load", NULL, NULL);
    /* EntityRenderer.rendererUpdateCount: one per unpaused client tick from
     * the world's join at the tape's tick 1, so t - 1 at row t (after its
     * increment) */
    ss->cp.renderer_update_count = s->tick - 2;
    if (!surv_server_load(&ss->sp, s->player_server))
        return fail(ss, "the server survival snapshot did not load", NULL, NULL);

    /* the StatisticsFile and the client mirror as the join left them; the
     * native server tick counter is the row tick, one behind Java's
     * getTickCounter inside a row, so the snapshot's counter is tick - 1 */
    if (s->stats_json != NULL && surv_stats_load_snapshot(&ss->sp, &ss->cp, s->stats_json, (int)s->tick - 1))
        return fail(ss, "stats.json did not load", NULL, NULL);
    ss->tc_offset = 1;
    if (s->stats_json != NULL)
    {
        char *copy = strdup(s->stats_json);
        struct jval *root = copy ? json_parse(copy) : NULL;
        int64_t tc = 0;
        if (root && json_int(json_get(root, "tickCounter"), &tc)) ss->tc_offset = tc - (int64_t)s->tick + 1;
        json_free(root);
    }

    /* The join: an empty client world (the movement tapes, taken before any
     * terrain arrived) skips the client's first replayed tick; a loaded one
     * runs it, so the server sees the bare C03 every standing tick sends and
     * the survival counters stay in phase with the rows. */
    if (!nbt_get(s->clientworld, "chunks")) return fail(ss, "clientworld.nbt has no chunks", NULL, NULL);
    {
        char ct[64];
        nbt_scalar(s->clientworld, "chunks", ct, sizeof ct);
        ss->cp.first_client_tick = ct[0] == 'i' && ct[1] == ':' && strtoll(ct + 2, NULL, 10) > 0 ? 1 : 0;
        /* a snapshot with the player's runtime says so itself */
        int atc = client_player_added_to_chunk(s->player_client);
        if (atc >= 0) ss->cp.first_client_tick = atc;
    }

    /* the server player the world tick reads: the snapshot's position until
     * the first row's processPlayer moves it */
    ss->sv_px = ss->sp.e.pos_x;
    ss->sv_py = ss->sp.e.pos_y;
    ss->sv_pz = ss->sp.e.pos_z;

    int64_t only_players = 0;
    json_int(json_get(json_get(s->manifest_json, "server"), "onlyPlayers"), &only_players);
    ss->server_rows = only_players == 1 || s->version >= 2;

    /* The survival module: on a whole-server tape the replay owns the entity
     * list and the world tick, so the survival draws from the replay's det
     * and its drops and pickups live on the replay's entity list; on the
     * movement tapes the module owns both. */
    if (ss->server_rows)
    {
        if (!open_server(ss, dir, rec_dir)) return 0;
        /* WorldClient's clock at the snapshot: the last S03 carried the
         * overworld's time before its tick (a Nether or End player's, the
         * overworld's after it), and every client tick since added one, so
         * the client runs a tick behind in the overworld, level elsewhere */
        {
            const struct servertick *st0 = &ss->sr->w[0].st;
            int lag = ss->sp.dimension == 0;
            ss->cp.cw_daylight = st0->daylight_cycle;
            ss->cp.cw_total = st0->total_time - lag;
            ss->cp.cw_day = st0->world_time - (lag && st0->daylight_cycle);
        }
        /* serverreplay_load read the worldinfo's keepInventory gamerule */
        surv_world_setup(&SR_DET(ss->sr), ss->sr->w[0].st.difficulty, surv.keep_inventory, &ss->sr->d->iew);
        if (ss->sr->mobs_enabled) { surv.extra_iew = &ss->sr->d->anw.iew; surv.anw = &ss->sr->d->anw; }
        surv.entity_tick_off = 1;
    }
    else
    {
        det_init(&ss->det);
        if (!det_from_snapshot(&ss->det, s->det, s->seed))
            return fail(ss, "the snapshot's det.nbt did not load", NULL, NULL);
        ie_init(&ss->iew, &s->world, &ss->det);
        /* the items and orbs the survival module spawns are the server's own
         * entities: the oracle draws their ids, rands and constructor math
         * from the server role (Det.newRandom on the server thread), not the
         * probe's OTHER */
        surv_world_setup(&ss->det, 2, 0, &ss->iew);
    }

    /* The containers the window clicks and the craft result run on: the
     * player container over each side's inventory (the server's is the
     * truth the mirror reads). */
    struct client_player *cp = &ss->cp;
    struct server_player *sp = &ss->sp;
    container_init(&cp->own_container, CONTAINER_PLAYER, 0, 0);
    container_init(&sp->own_container, CONTAINER_PLAYER, 0, 0);
    surv_server_hook_own_container(sp);
    cp->open_container = &cp->own_container;
    sp->open_container = &sp->own_container;

    for (int i = 0; i < 40; ++i)
    {
        struct craft_stack cs = {sp->sv.inv[i].count > 0 ? sp->sv.inv[i].item : -1,
            sp->sv.inv[i].count, sp->sv.inv[i].count > 0 ? sp->sv.inv[i].damage : 0,
            sp->sv.inv[i].count > 0 ? sp->sv.inv[i].tag : 0};
        struct craft_stack empty = {-1, 0, 0, 0};
        container_set_player(&sp->own_container, i, &cs);
        container_set_player(&cp->own_container, i, &empty);
    }

    /* the join dimension's world Random (a Nether join digs on the Nether's),
     * whose state the dig machine's drops continue */
    surv.world_rand = ss->server_rows ? serverreplay_world_rand(ss->sr, serverreplay_player_dim(ss->sr)) : NULL;

    /* the equipment check runs inside the player's update (processPlayer's
     * onUpdateEntity); what it last applied seeds it */
    sp->held_attr_item = held_attr_seed(sp, nbt_get(s->player_server, "nbt"));
    surv_dig_load(cp, sp, s->player_client, s->player_server);
    surv_gui_load(cp, sp, s->player_client, s->player_server);

    /* the packets the last join tick sent, read by the first client tick */
    surv_client_pending(s->player_client, s2c_sent_queue());
    surv_client_pending_s20(s->player_client, cp);
    return 1;
}

int session_parse_act(const struct jval *row, struct act *act, char *err, size_t err_size)
{
    int bad = 0;
#define BAD(...) do { if (!bad++) snprintf(err, err_size, __VA_ARGS__); } while (0)

    memset(act, 0, sizeof *act);
    act->hb = -1;
    const struct jval *actj = json_get(row, "act");
    if (!actj) return 0;

    const struct jval *look = json_get(actj, "look");
    if (look)
    {
        act->has_look = 1;

        for (int i = 0; i < 4 && i < json_len(look); ++i)
        {
            uint64_t bits;
            if (!json_double(json_at(look, i), &bits)) { BAD("bad look"); break; }
            double d;
            memcpy(&d, &bits, 8);
            act->look[i] = (float)d;
        }

        /* the vanilla client clamps the pitch (Entity.setAngles); Act.java
         * refuses an agent look outside it the same way. The previous pitch
         * is not clamped: setAngles adds the clamped pitch's float step to
         * it, which at the limit can land an ulp past it (-90.00001) */
        if (json_len(look) > 1 && !(act->look[1] >= -90.0F && act->look[1] <= 90.0F))
            BAD("pitch %g outside -90..90", (double)act->look[1]);
    }

    const struct jval *gui = json_get(actj, "gui");
    for (int i = 0; i < json_len(gui); ++i)
    {
        const struct jval *op = json_at(gui, i);
        const char *kind = json_str(json_at(op, 0));

        if (kind && !strcmp(kind, "close"))
        {
            act->gui_close = 1;
            if (act->closes >= (int)(sizeof act->close_at / sizeof act->close_at[0])) BAD("more closes than the act holds");
            else act->close_at[act->closes++] = act->clicks;
        }
        else if (kind && !strcmp(kind, "respawn"))
        {
            act->gui_respawn = 1;
            act->gui_respawn_after_close = act->gui_close;
        }
        else if (kind && !strcmp(kind, "wake")) act->gui_wake = 1;
        else if (kind && !strcmp(kind, "click"))
        {
            /* ["click", window, slot, button, mode]; the window the client
             * carries as the packet's own field */
            if (act->clicks >= (int)(sizeof act->gui_click / sizeof act->gui_click[0]))
            {
                BAD("more than %d clicks", (int)(sizeof act->gui_click / sizeof act->gui_click[0]));
                continue;
            }

            int64_t w2 = -1, s2 = 0, b2 = 0, m2 = 0;
            json_int(json_at(op, 1), &w2);
            /* the agent's window -1 is resolved to the open container's id
             * when a screen takes the row's input (Act.applyGui); a -1 left
             * in the tape means no screen was open, so the click never ran */
            if (w2 < 0) continue;

            json_int(json_at(op, 2), &s2);
            json_int(json_at(op, 3), &b2);
            json_int(json_at(op, 4), &m2);
            act->gui_click[act->clicks++] = (struct guiclick){(int)w2, (int)s2, (int)b2, (int)m2};
        }
        else if (kind && !strcmp(kind, "trsel"))
        {
            /* ["trsel", window, index]: GuiMerchant's arrow buttons' C17
             * MC|TrSel */
            if (act->trsels >= (int)(sizeof act->trsel / sizeof act->trsel[0]))
            {
                BAD("more than %d trsels", (int)(sizeof act->trsel / sizeof act->trsel[0]));
                continue;
            }

            int64_t w2 = 0, i2 = 0;
            json_int(json_at(op, 1), &w2);
            json_int(json_at(op, 2), &i2);
            act->trsel[act->trsels].window = (int)w2;
            act->trsel[act->trsels].index = (int)i2;
            act->trsel[act->trsels].at = act->clicks;
            ++act->trsels;
        }
        else if (kind && !strcmp(kind, "chat"))
        {
            /* ["chat", mods, px, py, clip, comp, events]: the chat screen's
             * batch (oracle/harness/netherite/oracle/ChatInput.java) */
            struct chat_op *c = &act->chat;
            int64_t v = 0;
            act->has_chat = 1;
            json_int(json_at(op, 1), &v); c->mods = (int)v;
            v = 0; json_int(json_at(op, 2), &v); c->px = (int)v;
            v = 0; json_int(json_at(op, 3), &v); c->py = (int)v;
            const char *clip = json_str(json_at(op, 4));
            if (clip)
            {
                /* what a paste can take: the first GC_MAX allowed chars
                 * (func_146191_b filters, then fits the room) */
                c->has_clip = 1;
                c->clip_len = gui_chat_utf16_allowed(clip, c->clip, GC_MAX);
            }
            const struct jval *comp = json_at(op, 5);
            if (comp && comp->kind == J_ARR)
            {
                const char *action = json_str(json_at(comp, 0));
                if (!action || strcmp(action, "suggest_command")) BAD("chat click action %s", action ? action : "?");
                c->has_comp = 1;
                snprintf(c->comp_value, sizeof c->comp_value, "%s", json_str(json_at(comp, 1)) ? json_str(json_at(comp, 1)) : "");
                snprintf(c->comp_text, sizeof c->comp_text, "%s", json_str(json_at(comp, 2)) ? json_str(json_at(comp, 2)) : "");
            }
            const struct jval *ev = json_at(op, 6);
            for (int k = 0; k < json_len(ev); ++k)
            {
                const struct jval *e = json_at(ev, k);
                int64_t f[5] = {0, 0, 0, 0, 0};
                for (int j = 0; j < 5 && j < json_len(e); ++j) json_int(json_at(e, j), &f[j]);
                if (c->nev >= CHAT_OP_EV) { BAD("more than %d chat events", CHAT_OP_EV); break; }
                struct chat_ev *x = &c->ev[c->nev++];
                memset(x, 0, sizeof *x);
                x->kind = (int8_t)f[0];
                if (f[0] == CHAT_EV_KEY) { x->code = (int32_t)f[1]; x->ch = (uint16_t)f[2]; }
                else if (f[0] == CHAT_EV_PRESS || f[0] == CHAT_EV_RELEASE)
                {
                    x->x = (int32_t)f[1];
                    x->y = (int32_t)f[2];
                    x->button = (int8_t)f[3];
                }
                else if (f[0] == CHAT_EV_WHEEL) x->code = (int32_t)f[1];
                else BAD("chat event kind %lld", (long long)f[0]);
            }
        }
        else BAD("unmodelled gui op %s", kind ? kind : "?");
    }

    /* ["opts"]: Act.applyOpts's settings, with the input snapshot */
    act->o_tpv = act->o_hide = act->o_smooth = act->o_rd = act->o_dbg = -1;
    const struct jval *opts = json_get(actj, "opts");
    if (opts)
    {
        int64_t v;
        act->has_opts = 1;
        if (json_int(json_get(opts, "tpv"), &v)) act->o_tpv = (int)v;
        if (json_int(json_get(opts, "hide"), &v)) act->o_hide = (int)v;
        if (json_int(json_get(opts, "smooth"), &v)) act->o_smooth = (int)v;
        if (json_int(json_get(opts, "rd"), &v)) act->o_rd = (int)v;
        if (json_int(json_get(opts, "dbg"), &v)) act->o_dbg = (int)v;
    }
    int64_t rd = 0;
    if (json_int(json_get(json_get(actj, "opts"), "rd"), &rd)) act->rd = (int)rd;
    if (act->rd > SR_MAX_VIEW)
    {
        BAD("render distance %d past the engine's %d", act->rd, SR_MAX_VIEW);
        act->rd = 0;
    }

    const struct jval *in = json_get(actj, "in");
    if (!in) return bad;
    act->has_in = 1;

    const struct jval *keys = json_get(in, "keys");
    int nk = keys ? keys->nfields : 0;

    for (int i = 0; i < nk; ++i)
    {
        /* one {desc: [held, presses]} member, in the parser's own field list */
        const char *name = keys->fields[i].key;
        const struct jval *v = keys->fields[i].val;
        int64_t held = 0, presses = 0;

        if (!v || !json_int(json_at(v, 0), &held) || !json_int(json_at(v, 1), &presses))
        {
            BAD("bad key value");
            continue;
        }

        int k = key_lookup(name ? name : "");
        if (k < 0)
        {
            BAD("key %s is not a vanilla binding", name ? name : "?");
            continue;
        }

        act->keys.held[k] = (uint8_t)(held != 0);
        act->keys.presses[k] = (int)presses;
    }

    int64_t hb, focus = 0, lcc = 0, ctrl = 0;
    if (!json_int(json_get(in, "hb"), &hb)) hb = -1;
    json_int(json_get(in, "focus"), &focus);
    json_int(json_get(in, "lcc"), &lcc);
    json_int(json_get(in, "ctrl"), &ctrl);
    /* the hotbar is 0..8 (-1: no player); Act.java refuses the same rows */
    if (hb < -1 || hb > 8) BAD("hotbar %lld outside 0..8", (long long)hb);
    act->hb = (int)hb;
    act->focus = (int)focus;
    act->lcc = (int)lcc;
    act->ctrl = (int)ctrl;
#undef BAD
    return bad;
}

static int setup_tick(const struct jval *entry)
{
    int64_t at = -1;
    json_int(json_get(entry, "tick"), &at);
    return (int)at;
}

/* The next setup entry due by tick t, the header's before the extra ones at
 * one tick; the header's before the snapshot tick are the join's (and a
 * MobFree at the snapshot tick is the snapshot's own). */
static const struct jval *next_setup(struct session *ss, int64_t t)
{
    for (;;)
    {
        const struct jval *a = ss->next_setup < json_len(ss->setups) ? json_at(ss->setups, ss->next_setup) : NULL;
        const struct jval *b = ss->next_extra < ss->nextra ? ss->extra[ss->next_extra] : NULL;
        if (a && setup_tick(a) > t) a = NULL;
        if (b && setup_tick(b) > t) b = NULL;
        if (!a && !b) return NULL;
        if (a && (!b || setup_tick(a) <= setup_tick(b)))
        {
            ++ss->next_setup;
            int at = setup_tick(a);
            const char *cls = json_str(json_get(a, "class"));
            if (at < ss->s->tick || (at == ss->s->tick && cls && !strcmp(cls, "MobFree"))) continue;
            return a;
        }
        ++ss->next_extra;
        return b;
    }
}

/* The setup entries due at the head of row t: a MobMove moves its mob at
 * once, the Dev entries wait for the server tick. */
static int take_setups(struct session *ss, int64_t t)
{
    const struct jval *setup;
    char tick[32];

    while ((setup = next_setup(ss, t)) != NULL)
    {
        /* the row's number, for the failures below (formatted only when a
         * setup entry is due) */
        snprintf(tick, sizeof tick, "%lld", (long long)t);
        const char *cls = json_str(json_get(setup, "class"));
        const struct jval *cmd = json_get(setup, "cmd");
        if (cls && !strcmp(cls, "Dev"))
        {
            if (ss->ndev_pending >= SESSION_DEV_MAX) return fail(ss, "row %s: too many Dev entries", tick, NULL);
            ss->dev_pending[ss->ndev_pending++] = cmd;
            continue;
        }
        if (!cls || strcmp(cls, "MobMove"))
            return fail(ss, "row %s: unknown post-snapshot setup class %s", tick, cls ? cls : "(missing)");

        int64_t id = -1;
        uint64_t bx, by, bz;
        if (!ss->server_rows || !json_int(json_get(cmd, "id"), &id) || !json_double(json_get(cmd, "x"), &bx) ||
            !json_double(json_get(cmd, "y"), &by) || !json_double(json_get(cmd, "z"), &bz))
            return fail(ss, "row %s: MobMove setup is malformed", tick, NULL);
        double x, y, z;
        memcpy(&x, &bx, 8);
        memcpy(&y, &by, 8);
        memcpy(&z, &bz, 8);
        if (!serverreplay_move_mob(ss->sr, (int)id, x, y, z))
        {
            char ids[32];
            snprintf(ids, sizeof ids, "%lld", (long long)id);
            return fail(ss, "row %s: MobMove entity %s is absent", tick, ids);
        }
    }
    return 1;
}

/* The Dev entries at the head of the server tick, after the client side and
 * before the first world tick. */
static int run_dev(struct session *ss, int64_t t)
{
    struct serverreplay *sr = ss->sr;
    int n = ss->ndev_pending;
    char tick[32];

    ss->ndev_pending = 0;
    if (sr)
    {
        sr->ndev_writes = 0;
        sr->dev_block_count = 0;
    }
    if (n == 0) return 1;
    snprintf(tick, sizeof tick, "%lld", (long long)t);
    if (!ss->server_rows) return fail(ss, "row %s: Dev needs whole-server replay", tick, NULL);
    if (!ss->dev_apply) return fail(ss, "row %s: Dev entries need a dev build", tick, NULL);

    sr->d->nextra = 0;
    sr->blk_count = 0;
    s2c_clear();
    int first_item = serverreplay_action_begin(sr);
    for (int d = 0; d < n; ++d)
    {
        char error[160];
        if (!ss->dev_apply(sr, &ss->sp, ss->dev_pending[d], error, sizeof error))
            return fail(ss, "row %s: Dev %s", tick, error);
        const char *op = json_str(json_get(ss->dev_pending[d], "op"));
        if (op && !strcmp(op, "purge")) first_item = 0;
    }
    serverreplay_action_end(sr, first_item);
    if (ss->dev_end) ss->dev_end(sr);
    return 1;
}

#ifdef NETHERITE_WL_DEPTH
/* The depth build measures the stack a row takes: the region under the
 * caller's frame is painted before the row, and after it the deepest byte
 * the row changed is its depth (a static bound is make -C csrc stack). */
#define ROW_PAINT (256 << 10)
static int session_tick_row(struct session *ss, int64_t t, const struct act *act);

__attribute__((noinline)) static uintptr_t row_paint(void)
{
    uintptr_t lo = (uintptr_t)__builtin_frame_address(0) - 512 - ROW_PAINT;

    for (size_t i = 0; i < ROW_PAINT; ++i) ((volatile unsigned char *)lo)[i] = 0xA5;
    return lo;
}

int session_tick(struct session *ss, int64_t t, const struct act *act)

    uintptr_t top = (uintptr_t)__builtin_frame_address(0);
    uintptr_t lo = row_paint();
    int r = session_tick_row(ss, t, act);
    size_t i = 0;

    while (i < ROW_PAINT && ((volatile unsigned char *)lo)[i] == 0xA5) ++i;
    WL_DEPTH_NOTE(tick_stack, (int)(top - (lo + i)));
    return r;
}
#define session_tick session_tick_row
#endif

/* R0: the header's setup entries due at the head of the row. */
static int session_setup(struct session *ss, int64_t t)
{
    return take_setups(ss, t);
}

/* C1: the chunk packets and the block writes the last server tick sent,
 * pumped into the client's world (one row later, as in the lockstep). */
static void session_sync_client(struct session *ss, int64_t t, const struct act *act)
{
    struct serverreplay *sr = ss->sr;

    /* Minecraft.runTick's getMouseOver comes before updateController hands
     * the tick its packets: a block or a chunk they bring (S23, S22, S26,
     * S21) is not under the crosshair until the next tick */
    client_player_pick(&ss->cp, act);
    if (t > ss->s->tick) serverreplay_sync_client(sr, ss->client_world);
    serverreplay_sync_client_chunks(sr, ss->client_world, ss->hooks.on_chunk, ss->hooks.ctx);
    if (t > ss->s->tick) client_take_chunks(ss);
}

int session_client_rd(const struct session *ss)
{
    return ss->client_rd > 0 ? ss->client_rd : ss->hdr_rd;
}

/* WorldClient.func_147456_g, after the chunk cache: World's
 * setActivePlayerChunksAndCheckLight (the client player's square of the
 * render distance, HashSet order; its random light check draws on
 * WorldClient.rand, which no snapshot carries, and is not run), then
 * previousActiveChunkSet's walk: the active chunks not visited since the set
 * last moved, ten at most a tick, each func_147467_a (the client plays no
 * mood sound) and so Chunk.enqueueRelightChecks, a chunk the client lacks
 * being the blank chunk, which has none. */
static void session_client_blocks(struct session *ss, struct world *w)
{
    const struct client_player *cp = &ss->cp;
    int r = session_client_rd(ss);
    if (r < 0 || r > 32) return;
    int n = (2 * r + 1) * (2 * r + 1);
    if (jord_cap(n) > ss->cw_active_cap) ss->cw_active_cap = jord_cap(n);
    int *order ENV_LOCAL = envstack_take((size_t)n * 2 * sizeof *order);
    int pcx = mh_floor(cp->e.pos_x / 16.0), pcz = mh_floor(cp->e.pos_z / 16.0);
    if (jord_ccp_window_memo(&ss->cw_memo, pcx, pcz, r, &ss->cw_active_cap, order) != n) return;

    /* playerCheckLight: func_147451_t at the cell the client player's tick
     * drew from WorldClient.rand (player.c cp_world_tick) */
    if (ss->cp.cw_check_pending)
    {
        ss->cp.cw_check_pending = 0;
        (void)world_check_light_at(w, ss->cp.cw_check_x, ss->cp.cw_check_y, ss->cp.cw_check_z);
    }

    /* retainAll, then clear when every active chunk has been visited */
    int k = 0;
    for (int i = 0; i < ss->cw_nprev; ++i)
        if (abs(ss->cw_prev[i][0] - pcx) <= r && abs(ss->cw_prev[i][1] - pcz) <= r)
        {
            ss->cw_prev[k][0] = ss->cw_prev[i][0];
            ss->cw_prev[k][1] = ss->cw_prev[i][1];
            ++k;
        }
    ss->cw_nprev = k == n ? 0 : k;
    int done = 0;
    for (int i = 0; i < n && done < 10; ++i)
    {
        int cx = order[2 * i], cz = order[2 * i + 1], seen = 0;
        for (int j = 0; j < ss->cw_nprev && !seen; ++j) seen = ss->cw_prev[j][0] == cx && ss->cw_prev[j][1] == cz;
        if (seen) continue;
        struct chunk *c = world_chunk(w, cx, cz);
        if (c != NULL) chunk_enqueue_relight_checks(w, c);
        if (ss->cw_nprev < 128)
        {
            ss->cw_prev[ss->cw_nprev][0] = cx;
            ss->cw_prev[ss->cw_nprev][1] = cz;
            ++ss->cw_nprev;
        }
        ++done;
    }
}

/* C2: Minecraft.runTick's client player. */
static void session_client(struct session *ss, int64_t t, const struct act *act, int paused, struct client_out *co)
{
    struct serverreplay *sr = ss->sr;

    if (ss->server_rows && sr->mobs_enabled) { surv.anw = &sr->d->anw; surv.extra_iew = &sr->d->anw.iew; }
    if (ss->hooks.before_client_tick) ss->hooks.before_client_tick(ss->hooks.ctx, paused);
    ss->cp.client_row_tick = t;
    /* Det's client consumer pin (particles_live_pin): the oracle's FX take
     * their values from the tick's own streams */
    particles_live_pin(&surv_fx, ss->s->seed, t);
    /* the pump's S03 (handleTimeUpdate: the total, then setWorldTime, whose
     * sign is the doDaylightCycle rule), ahead of the textures' tick */
    struct client_player *cp = &ss->cp;
    if (!paused && cp->s03_pending)
    {
        cp->cw_total = cp->s03_total;
        cp->cw_daylight = cp->s03_rule;
        cp->cw_day = cp->s03_rule ? cp->s03_day : cp->s03_day == 0 ? 1 : cp->s03_day;
        cp->s03_pending = 0;
    }
    /* WorldClient's rainingStrength: the last S2B 7, the server world's
     * value after its last tick */
    if (sr != NULL && ss->server_rows && cp->e.world != NULL) cp->cw_rain = serverreplay_rain_strength(sr, cp->e.world->dim);
    client_player_tick(&ss->cp, act, co);
    cp->nuse = 0;
    for (int i = 0; i < co->npkt && cp->nuse < 8; ++i)
        if (co->pkt[i].kind == CPK_C02_USE) cp->use_ids[cp->nuse++] = co->pkt[i].v;
    /* WorldClient.tick's clock */
    if (!paused)
    {
        ++cp->cw_total;
        if (cp->cw_daylight) ++cp->cw_day;
    }
    /* WorldClient.tick's chunkCache: ChunkProviderClient.unloadQueuedChunks,
     * func_150804_b(false) over chunkListing: the client's recheckGaps (one
     * flagged column a chunk a tick) where the world has sky; a received
     * chunk is light-populated, so func_150809_p never runs */
    struct world *w = ss->client_world;
    if (!paused && w != NULL && w->dim == 0)
        for (size_t i = 0; i < w->lon; ++i)
        {
            int64_t key = w->load_order[i];
            struct chunk *c = world_chunk(w, (int)(int32_t)(key & 0xffffffff), (int)(key >> 32));
            if (c != NULL && c->gap_lighting_updated) chunk_recheck_gaps(w, c, 1);
        }
    if (!paused && w != NULL && ss->server_rows) session_client_blocks(ss, w);
}

/* N1: the network tick's processPlayer and the row's packets. */
static void session_player(struct session *ss, int64_t t, const struct act *act, const struct client_out *co)
{
    server_player_tick(&ss->sp, co, act, (int)t);
    if (ss->hooks.after_player_tick) ss->hooks.after_player_tick(ss->hooks.ctx);
}

/* N2: PlayerManager.updateMountedMovingPlayer over the C03's position. */
static void session_move(struct session *ss, int64_t t)
{
    if (ss->sp.pertinent_update) cl_c03(serverreplay_player_manager(ss->sr), (int)t, ss->sp.e.pos_x, ss->sp.e.pos_z);
}

/* One row as its phases (phase.h). */
static int session_row(struct session *ss, int64_t t, const struct act *act)
{
    struct client_player *cp = &ss->cp;
    struct server_player *sp = &ss->sp;
    struct serverreplay *sr = ss->sr;
    int server_rows = ss->server_rows;
    int ok = 1;   /* a phase a kernel takes over leaves it */

    /* dead livings are freed once nothing points at them (grave.h): between
     * rows, where no entity update is on the stack */
    PHASE_SUB(PS_GRAVE, grave_collect());
    /* the pools' freed slots' pages back to the kernel (arena.h), and the
     * write records' past what the last tick left in them */
    PHASE_SUB(PS_GRAVE, arena_trim());
    if (sr != NULL)
    {
        for (int i = 0; i < sr->nworlds; ++i) servertick_trim(&sr->w[i].st);
        for (int k = 0; k < 3; ++k)
            if (sr->dims[k].extra != NULL)
                fixed_array_trim(sr->dims[k].extra, (size_t)sr->dims[k].nextra, &sr->dims[k].hiextra,
                                 sizeof *sr->dims[k].extra);
    }
    /* this row's destroyBlockInWorldPartially events (env.h block_break) */
    nw_env->block_break.n = 0;
    PHASE_RUN(PH_R0, ok = session_setup(ss, t));
    if (!ok) return 0;

    /* the world tick reads the server player as the server tick's head sees
     * it: the position the previous tick's processPlayer left */
    double wpx = ss->sv_px, wpy = ss->sv_py, wpz = ss->sv_pz;

    /* the s2c queue the last server tick sent is drained by the client
     * tick's updateController pump (one row later, as in the lockstep):
     * handed on by index, the server tick's own becomes the sent one at the
     * row's end */
    s2c_receive();
    s2c_in_watch_live(sp);

    /* Minecraft.isGamePaused as the last frame left it: a paused tick reads
     * no packet on the client and runs no integrated server tick */
    int paused = cp->game_paused;

    if (server_rows && !paused) PHASE_RUN(PH_C1, session_sync_client(ss, t, act));
    /* the row's packets: the input block's ordered list makes it too big
     * for a tick frame */
    struct client_out *co ENV_LOCAL = envstack_take(sizeof *co);
    if (PHASE_ON()) nw_env->phase.co = co;
    cp->refused[0] = 0;
    PHASE_RUN(PH_C2, session_client(ss, t, act, paused, co));
    if (cp->refused[0])
    {
        char tick[32];
        snprintf(tick, sizeof tick, "%lld", (long long)t);
        return fail(ss, "row %s: %s", tick, cp->refused);
    }

    /* the paused server: its first paused tick saves every world (the
     * unload marks), then nothing ticks; the client's packets (the credits'
     * C16) wait for the first unpaused tick */
    if (paused)
    {
        if (co->c16) ss->carry_c16 = 1;
        if (server_rows && !ss->was_paused) PHASE_RUN(PH_TP, serverreplay_pause_save(sr, (int)t));
        if (server_rows) sr->blk_count = 0;
        if (server_rows && sr->player != NULL) ++sr->player->paused_ticks;
    }
    else if (ss->carry_c16)
    {
        co->c16 = 1;
        ss->carry_c16 = 0;
    }
    ss->was_paused = paused;
    if (paused) return 1;

    if (ss->start_difficulty > 0 && server_rows)
    {
        serverreplay_set_difficulty(sr, ss->start_difficulty - 1);
        ss->start_difficulty = 0;
    }
    PHASE_RUN(PH_DV, ok = run_dev(ss, t));
    if (!ok) return 0;

    /* updateTimeLightAndEntities' S03 every 20th getTickCounter, before
     * each world ticks: the overworld's time before its tick, or for a
     * player in the Nether or the End (DerivedWorldInfo) the overworld's
     * after it */
    if (server_rows && (t + ss->tc_offset) % 20 == 0)
    {
        const struct servertick *st0 = &sr->w[0].st;
        int after = ss->sp.dimension != 0;
        ss->sp.s03_sent = 1;
        ss->sp.s03_rule = st0->daylight_cycle;
        ss->sp.s03_total = st0->total_time + after;
        ss->sp.s03_day = st0->world_time + (after && st0->daylight_cycle);
    }

    /* the whole server, in worldServers order */
    if (server_rows)
    {
        /* WorldServer.tick with its updateEntities pass runs before the
         * network tick that processes this row's C03, so the player's move
         * sees the tick's own block writes. */
        serverreplay_tick(sr, (int)t, wpx, wpy, wpz);
        /* the player's own tick reads the weather the world tick just
         * stepped (isRaining for Entity.isWet's extinguish) */
        surv_server_set_weather(sr->w[0].st.raining_strength > 0.2F, sr->w[0].st.w);
        /* the difficulty scaling in surv_server_damage_by reads
         * surv.difficulty, which the survival module takes from the
         * worlds.nbt scalar the replay does not step */
        surv.difficulty = sr->w[0].st.difficulty;
        if (ss->hooks.after_world_tick) ss->hooks.after_world_tick(ss->hooks.ctx);
    }

    int action_items = server_rows ? serverreplay_action_begin(sr) : 0;
    PHASE_RUN(PH_N1, session_player(ss, t, act, co));
    /* the chat's paths this port stops at (chatcmd.h, gui_chat.h) */
    if (sp->chat_unmodelled) return fail(ss, "chat: %s is not modelled%s", sp->chat_unmodelled, "");
    if (cp->chat_unmodelled) return fail(ss, "chat: %s is not modelled%s", "the sent history past its kept entries", "");
    if (server_rows)
    {
        PHASE_RUN(PH_N2, session_move(ss, t));
        PHASE_RUN(PH_N1E, serverreplay_action_end(sr, action_items));
        PHASE_RUN(PH_N3, serverreplay_queue_packets(sr));
        PHASE_RUN(PH_T, serverreplay_save_marks(sr, (int)t));
        /* IntegratedServer.tick's tail, after the whole server tick: the
         * client's render distance, when it differs, becomes the server's
         * view distance (ServerConfigurationManager.func_152611_a) */
        if (act != NULL && act->rd > 0) ss->client_rd = act->rd;
        if (ss->client_rd > 0 && ss->client_rd != sr->view)
            PHASE_RUN(PH_VD, serverreplay_set_view_distance(sr, (int)t, ss->client_rd, sp->e.pos_x, sp->e.pos_z));
    }
    /* the region stores' packing, after the tick (between rows) */
    if (sr != NULL) PHASE_SUB(PS_PARK, serverreplay_park(sr));
    if (sp->s03_sent)
    {
        cp->s03_pending = 1;
        cp->s03_rule = sp->s03_rule;
        cp->s03_total = sp->s03_total;
        cp->s03_day = sp->s03_day;
        sp->s03_sent = 0;
    }
    cp->pending_s20 = sp->s20_queued;
    cp->pending_s20_mod = sp->s20_mod;
    cp->pending_s20_slow = sp->s20_slow;
    cp->pending_s20_speed = sp->s20_speed;
    server_player_watch_live(sp);
    s2c_send();
    ss->sv_px = sp->e.pos_x;
    ss->sv_py = sp->e.pos_y;
    ss->sv_pz = sp->e.pos_z;
    return 1;
}

int session_tick(struct session *ss, int64_t t, const struct act *act)
{
    int heap = image_heap_set(nw_env, 1);
    if (PHASE_ON()) phase_row_begin_(ss, t);
    int r = session_row(ss, t, act);
    if (PHASE_ON()) phase_row_end_();
    image_heap_set(nw_env, heap);
    return r;
}

void session_close(struct session *ss)
{
    if (ss->client_world) { world_free(ss->client_world); free(ss->client_world); }
    ss->client_world = NULL;
}
