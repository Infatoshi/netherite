/* A row record: every field test_snapshots compares against the oracle's row,
 * as the native engine has it after the row's tick, written as one text line
 * (and, after the last row, the end state its end/ checks read). Two runs of
 * one recording that print the same records gave the same rows: the env pool
 * (csrc/runtime/pool_gate.c) compares each of its envs, row by row, with
 * test_snapshots --row-dump FILE over the same tape.
 *
 * Values are exact: doubles and floats as their bits in hex, hashes in hex.
 * A field test_snapshots skips (d.cw before the client world's Random is
 * known) is written as "-". The reads are the ones the row comparison makes,
 * in its order; the one call with an effect, surv_server_gui_pull (the open
 * tile window's slots from its tile entity), is idempotent. */
#ifndef NETHERITE_ROWREC_H
#define NETHERITE_ROWREC_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/chatcomp.h"
#include "../engine/container.h"
#include "../engine/crafting.h"
#include "../engine/cwrand.h"
#include "../engine/env.h"
#include "../engine/nbtjson.h"
#include "../engine/player.h"
#include "../engine/serverreplay.h"
#include "../engine/session.h"
#include "../engine/snapshot.h"
#include "../engine/survival.h"
#include "../engine/tileentity.h"

struct rowrec_buf {
    char *p;
    size_t n, len;
};

__attribute__((format(printf, 2, 3))) static void rowrec_put(struct rowrec_buf *b, const char *fmt, ...)
{
    if (b->len >= b->n) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(b->p + b->len, b->n - b->len, fmt, ap);
    va_end(ap);
    if (k > 0) b->len += (size_t)k;
    if (b->len > b->n) b->len = b->n;
}

static uint64_t rowrec_dbits(double d)
{
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

static uint32_t rowrec_fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static uint64_t rowrec_fnv(uint64_t h, const void *p, size_t n)
{
    const unsigned char *c = p;
    for (size_t i = 0; i < n; ++i) h = (h ^ c[i]) * 0x100000001b3ull;
    return h;
}

static void rowrec_inv(struct rowrec_buf *b, const char *name, const struct surv_stack *inv)
{
    rowrec_put(b, " %s=", name);
    for (int i = 0; i < 40; ++i)
        if (inv[i].count != 0 || inv[i].item > 0 || inv[i].damage != 0)
            rowrec_put(b, "%d:%d,%d,%d;", i, inv[i].item, inv[i].count, inv[i].damage);
}

static void rowrec_stack(struct rowrec_buf *b, const char *name, const struct craft_stack *s)
{
    if (s == NULL || craft_slot_empty(s)) rowrec_put(b, " %s=-", name);
    else rowrec_put(b, " %s=%d,%d,%d", name, s->item, s->count, s->damage);
}

/* The client's screen as the row's cp.gui names it (test_snapshots). */
static const char *rowrec_gui(struct client_player *cp)
{
    if (cp->screen_gameover) return "GuiGameOver";
    if (cp->screen_sleep) return "GuiSleepMP";
    if (cp->screen_chat) return "GuiChat";
    if (cp->screen_credits) return "GuiWinGame";
    if (!cp->screen_inventory) return "-";
    const struct container *c = cp_gui_container(cp);
    if (c == NULL) return "GuiInventory";
    switch (c->kind)
    {
    case CONTAINER_WORKBENCH: return "GuiCrafting";
    case CONTAINER_CHEST: return "GuiChest";
    case CONTAINER_FURNACE: return "GuiFurnace";
    case CONTAINER_MERCHANT: return "GuiMerchant";
    case CONTAINER_DISPENSER: return "GuiDispenser";
    case CONTAINER_HOPPER: return "GuiHopper";
    default: return "GuiInventory";
    }
}

/* The stats store and the client's mirror: what surv_stats_compare reads
 * against an oracle dump (stats.jsonl), hashed field by field. */
static uint64_t rowrec_stats(const struct server_player *sp)
{
    const struct surv_stats *st = &sp->sv.stats;
    uint64_t h = 0xcbf29ce484222325ull;
    for (int s = 0; s < STAT_COUNT; ++s)
    {
        int32_t rec[5] = {s, STAT_BIT(st->present, s) ? st->v[s] : 0, STAT_BIT(st->present, s) != 0,
                          STAT_BIT(st->dirty, s) != 0,
                          STAT_BIT(st->client_present, s) ? st->client_v[s] : 0};
        if (rec[2] || rec[3] || rec[4]) h = rowrec_fnv(h, rec, sizeof rec);
    }
    int32_t tail[4] = {st->ach_dirty, st->last_send_tick - sp->paused_ticks, st->has_progress, st->has_stats};
    h = rowrec_fnv(h, tail, sizeof tail);
    h = rowrec_fnv(h, st->explored, sizeof st->explored);
    return h;
}

/* The S02 chat lines the row's server tick sent (chat_s02_check's reads). */
static uint64_t rowrec_s02(int *n)
{
    const struct s2c_queue *q = s2c_sent_queue();
    uint64_t h = 0xcbf29ce484222325ull;
    *n = 0;
    for (int i = 0; q && i < q->n; ++i)
    {
        const struct s2c_pkt *pkt = &q->q[i];
        if (pkt->kind != PK_S02) continue;
        const char *s = chat_s02_json(q, pkt);
        h = rowrec_fnv(h, s, strlen(s) + 1);
        s += strlen(s) + 1;
        for (int j = 0; j < pkt->i0; ++j)
            for (int k = 0; k < 4; ++k)
            {
                h = rowrec_fnv(h, s, strlen(s) + 1);
                s += strlen(s) + 1;
            }
        ++*n;
    }
    return h;
}

/* Row t's record into buf (NUL terminated); its length. cw_known: the
 * client world's Random is the recording's (test_snapshots compares d.cw). */
static size_t rowrec_line(struct session *ss, int64_t t, int cw_known, char *buf, size_t n)
{
    struct rowrec_buf b = {buf, n, 0};
    struct client_player *cp = &ss->cp;
    struct server_player *sp = &ss->sp;
    struct serverreplay *sr = ss->sr;
    rowrec_put(&b, "t=%lld", (long long)t);

    if (ss->server_rows)
    {
        uint64_t seeder, math, split;
        sr_det(sr, &seeder, &math, &split);
        rowrec_put(&b, " w.wt=%lld w.tt=%lld w.bc=%d w.ents=%d", (long long)sr_world_time(sr),
                   (long long)sr_total_time(sr), sr_block_writes(sr),
                   ss->s->players - (sp->sv.removed || sr->player_unlisted ? 1 : 0) + sr->native_entities);
        rowrec_put(&b, " d.blk=%016llx d.sw=%016llx d.sseed=%016llx d.smath=%016llx d.sstat=%016llx",
                   (unsigned long long)sr_block_hash(sr), (unsigned long long)sr_world_rng(sr),
                   (unsigned long long)seeder, (unsigned long long)math, (unsigned long long)split);
        rowrec_put(&b, " d.ents=%016llx", (unsigned long long)sr_entity_digest(ss->s, sr, sp));
        rowrec_put(&b, " d.cseed=%016llx d.cmath=%016llx d.cstat=%016llx",
                   (unsigned long long)det_seeder_state(&SR_DET(sr), DET_CLIENT),
                   (unsigned long long)det_math_state(&SR_DET(sr), DET_CLIENT),
                   (unsigned long long)det_split_state(&SR_DET(sr), DET_CLIENT));
        if (cw_known && cp->cw_rand_known)
            rowrec_put(&b, " d.cw=%016llx", (unsigned long long)cwrand_digest(cp->cw_rand.r.seed, cp->cw_lcg));
        else rowrec_put(&b, " d.cw=-");
    }

    rowrec_put(&b, " cp=%016llx,%016llx,%016llx,%016llx,%016llx,%016llx,%08x,%08x,%08x og=%d",
               (unsigned long long)rowrec_dbits(cp->e.pos_x), (unsigned long long)rowrec_dbits(cp->e.pos_y),
               (unsigned long long)rowrec_dbits(cp->e.pos_z), (unsigned long long)rowrec_dbits(cp->e.motion_x),
               (unsigned long long)rowrec_dbits(cp->e.motion_y), (unsigned long long)rowrec_dbits(cp->e.motion_z),
               rowrec_fbits(cp->rotation_yaw), rowrec_fbits(cp->rotation_pitch), rowrec_fbits(cp->e.fall_distance),
               cp->e.on_ground);
    rowrec_put(&b, " cp.hp=%08x cp.food=%d cp.hb=%d", rowrec_fbits(cp->sv.health), cp->sv.food.level, cp->hotbar);
    if (ss->server_rows)
    {
        rowrec_inv(&b, "cp.inv", cp->sv.inv);
        struct container *gc = cp_gui_container(cp);
        rowrec_stack(&b, "cp.cur", gc ? &gc->cursor : NULL);
        rowrec_put(&b, " cp.gui=%s", rowrec_gui(cp));
        const struct container *cfc = cp->open_container;
        if (cfc != NULL && cfc->kind == CONTAINER_FURNACE)
            rowrec_put(&b, " cp.fur=%d,%d,%d", cfc->furnace_progress[0], cfc->furnace_progress[1],
                       cfc->furnace_progress[2]);
        int ns02;
        uint64_t hs02 = rowrec_s02(&ns02);
        rowrec_put(&b, " s02=%d:%016llx", ns02, (unsigned long long)hs02);
    }

    rowrec_put(&b, " sp=%016llx,%016llx,%016llx sp.hp=%08x sp.food=%d sp.sat=%08x sp.xp=%d sp.dim=%d",
               (unsigned long long)rowrec_dbits(sp->e.pos_x), (unsigned long long)rowrec_dbits(sp->e.pos_y),
               (unsigned long long)rowrec_dbits(sp->e.pos_z), rowrec_fbits(sp->sv.health), sp->sv.food.level,
               rowrec_fbits(sp->sv.food.saturation), sp->sv.xp_total, sp->dimension);
    rowrec_inv(&b, "sp.inv", sp->sv.inv);
    rowrec_stack(&b, "sp.cur", sp->open_container ? &sp->open_container->cursor : NULL);
    surv_server_gui_pull(sp);
    if (sp->open_container != NULL && sp->open_container->kind != CONTAINER_PLAYER)
    {
        const struct container *c = sp->open_container;
        rowrec_put(&b, " sp.win=%d:", (int)c->kind);
        for (int k = 0; k < c->grid.size; ++k)
        {
            const struct craft_stack *g = &c->grid.slot[k];
            if (craft_slot_empty(g)) rowrec_put(&b, "-;");
            else rowrec_put(&b, "%d,%d,%d;", g->item, g->count, g->damage);
        }
        rowrec_stack(&b, "sp.win.res", container_slot(sp->open_container, 0));
    }
    else rowrec_put(&b, " sp.win=-");
    rowrec_put(&b, " stats=%016llx", (unsigned long long)rowrec_stats(sp));
    rowrec_put(&b, "\n");
    return b.len;
}

struct rowrec_chunk {
    int cx, cz;
    int64_t inhabited;
    const struct chunk *c;
};

static int rowrec_chunk_cmp(const void *a, const void *b)
{
    const struct rowrec_chunk *x = a, *y = b;
    if (x->cx != y->cx) return x->cx < y->cx ? -1 : 1;
    return x->cz < y->cz ? -1 : x->cz > y->cz;
}

/* The end state test_snapshots' end/ checks read: every loaded overworld
 * chunk's inhabitedTime and every tile entity's saved form in them, hashed
 * in position order. */
static size_t rowrec_end(struct session *ss, char *buf, size_t n)
{
    struct rowrec_buf b = {buf, n, 0};
    if (!ss->server_rows)
    {
        rowrec_put(&b, "end=-\n");
        return b.len;
    }
    struct serverreplay *sr = ss->sr;
    serverreplay_enter(sr, 0);
    const struct world *w = &sr->pop.world;
    struct rowrec_chunk *cs = malloc((w->cap + 1) * sizeof *cs);
    int nc = 0;
    for (size_t i = 0; i < w->cap; ++i)
    {
        const struct chunk *c = chunk_ptr(w->slot[i]);
        if (c == NULL) continue;
        cs[nc++] = (struct rowrec_chunk){c->cx, c->cz, c->inhabited_time, c};
    }
    qsort(cs, (size_t)nc, sizeof *cs, rowrec_chunk_cmp);
    uint64_t hc = 0xcbf29ce484222325ull, ht = 0xcbf29ce484222325ull;
    int ntes = 0;
    for (int i = 0; i < nc; ++i)
    {
        int64_t rec[3] = {cs[i].cx, cs[i].cz, cs[i].inhabited};
        hc = rowrec_fnv(hc, rec, sizeof rec);
        const struct chunk *c = cs[i].c;
        for (int k = 0; k < c->tes.n; ++k)
        {
            char *text = te_render(c->tes.v[k]);
            if (text == NULL) continue;
            ht = rowrec_fnv(ht, text, strlen(text) + 1);
            free(text);
            ++ntes;
        }
    }
    free(cs);
    serverreplay_enter(sr, serverreplay_player_dim(sr));
    rowrec_put(&b, "end chunks=%d:%016llx tiles=%d:%016llx\n", nc, (unsigned long long)hc, ntes, (unsigned long long)ht);
    return b.len;
}

#endif
