/* d.ents, the digest of every server entity's id and NBT, shared by the
 * snapshot gate (tests/test_snapshots.c) and the playable client's row check
 * (play/play.c --check). */
#include "serverreplay.h"

#include "endfight.h"
#include "entity_nbt.h"
#include "player.h"
#include "snapshot.h"
#include "env.h"
#include "nbtw.h"

/* One loadedEntityList entry's id, its NBT through w. */
static void sr_entry_write(struct nbtw *w, const struct snapshot *s, const struct server_player *sp,
                           const struct sr_ent *rec, int *id)
{
    const struct serverreplay *sr = sp->replay;

    if (rec->pool == 3)
    {
        /* the bound player's id (a snapshot taken while the player was
         * dead has no player entry) */
        *id = sp->replay ? sp->replay->player_entity_id : 0;
        for (int i = 0; i < s->nents; ++i)
            if (s->ents[i].player) *id = s->ents[i].id;
        ent_w_player(w, sp);
    }
    else if (rec->pool == 0)
    {
        const ie_ent *en = sr_ent_p(sr, rec);
        *id = en->entity_id;
        ent_w_ie(w, en);
    }
    else if (rec->pool == 1)
    {
        const fh_ent *en = sr_ent_p(sr, rec);
        *id = en->entity_id;
        fh_write_w(w, en);
    }
    else if (rec->pool == SR_POOL_DRAGON)
    {
        const struct dragon_state *d = sr_ent_p(sr, rec);
        *id = d->entity_id;
        endfight_dragon_w(w, d);
    }
    else if (rec->pool == SR_POOL_CRYSTAL)
    {
        const struct dragon_crystal_state *c = sr_ent_p(sr, rec);
        *id = c->entity_id;
        endfight_crystal_w(w, c);
    }
    else
    {
        const struct an_ent *en = sr_ent_p(sr, rec);
        *id = en->is_living ? lv_get(en->livh)->entity_id : ie_get(en->ieh)->entity_id;
        if (en->is_living) living_write_w(lv_get(en->livh), w);
        else ent_w_ie(w, ie_get(en->ieh));
    }
}

nbt *sr_entry_nbt(const struct snapshot *s, const struct server_player *sp,
                  const struct sr_ent *rec, int *id)
{
    nbt *tag = nbt_new_compound();
    struct nbtw w;

    nbtw_tree(&w, tag);
    sr_entry_write(&w, s, sp, rec, id);
    return tag;
}

/* Every world's pass order, overworld, Nether, End (worldServers order), the
 * player at its own place in its world's list (a respawn moves it to the
 * tail; a dead player has left the list until then). A world with an empty
 * list is not entered: the swap only moves state, so skipping it changes
 * nothing but the time the digest takes. Each entity's NBT goes straight
 * into its binary form (nbtw.h), no tree. */
uint64_t sr_entity_digest(const struct snapshot *s, struct serverreplay *sr,
                              const struct server_player *sp)
{
    static const int dims[3] = {0, -1, 1};
    uint64_t h = 0;
    struct nbtw w;

    for (int d = 0; d < 3; ++d)
    {
        if (serverreplay_dim_nents(sr, dims[d]) == 0) continue;

        serverreplay_enter(sr, dims[d]);

        for (int k = 0; k < sr->d->nents; ++k)
        {
            if (sr->d->ents[k].pool == 3 && sp->sv.removed) continue;
            int id;

            nbtw_bin_begin(&w, &nw_env->nbtbin.memo_scratch);
            sr_entry_write(&w, s, sp, &sr->d->ents[k], &id);
            nbtw_bin_end(&w);
            h = ent_digest_add_w(h, id, &w);
        }
    }

    serverreplay_enter(sr, serverreplay_player_dim(sr));
    return h;
}
