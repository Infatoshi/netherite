/* The native sim's fidelity (fidelity.h). */
#include "fidelity.h"

#include <stdio.h>
#include <string.h>

#include "living.h"
#include "serverreplay.h"
#include "spawning.h"
#include "worldconf.h"

/* fidelity.spawn_types' members, in CT_ order */
static const char *const TYPES[CT_TYPES] = {"monster", "creature", "ambient", "water"};

int fidelity_from_conf(struct fidelity *f, const struct wconf *c, char *err, size_t n)
{
    memset(f, 0, sizeof *f);
    f->no_spawning = !wconf_on(c, WC_FID_SPAWNING);
    const char *types = wconf_get(c, WC_FID_SPAWN_TYPES);
    for (int t = 0; t < CT_TYPES; ++t)
    {
        /* the kind checked the list: a member is a whole comma field */
        char tag[32], list[WCONF_VAL + 2];
        snprintf(tag, sizeof tag, ",%s,", TYPES[t]);
        snprintf(list, sizeof list, ",%s,", types);
        if (!strstr(list, tag)) f->skip_types |= 1u << t;
    }
    const char *mobs = wconf_get(c, WC_FID_MOBS);
    if (strcmp(mobs, "all"))
    {
        f->keep_on = 1;
        for (const char *p = mobs; strcmp(mobs, "none") && *p;)
        {
            const char *plus = strchr(p, '+');
            size_t l = plus ? (size_t)(plus - p) : strlen(p);
            int kind = -1;
            for (int k = 0; k < HK_KINDS; ++k)
            {
                const char *name = living_kind_name(k);
                if (name && strlen(name) == l && !strncmp(name, p, l)) kind = k;
            }
            if (kind < 0)
            {
                snprintf(err, n, "fidelity.mobs: %.*s is not a living kind (EntityList's names: Zombie, Pig, Villager, ...)",
                         (int)l, p);
                return -1;
            }
            f->keep |= 1u << kind;
            p = plus ? plus + 1 : p + l;
        }
    }
    if (!wconf_on(c, WC_FID_RANDOM_TICKS)) f->bits |= FID_NO_RANDOM_TICKS;
    if (!wconf_on(c, WC_FID_WEATHER)) f->bits |= FID_NO_WEATHER;
    if (wconf_on(c, WC_FID_SKIP_AMBIENT)) f->bits |= FID_SKIP_AMBIENT;
    f->distant_tick_radius = wconf_int(c, WC_FID_DISTANT_RADIUS);
    if (f->distant_tick_radius > 0) f->bits |= FID_DISTANT_TICKS;
    return 0;
}

int fidelity_vanilla(const struct fidelity *f)
{
    return !f->no_spawning && !f->skip_types && !f->keep_on && !f->bits && !f->distant_tick_radius;
}

void fidelity_describe(const struct fidelity *f, char *out, size_t n)
{
    out[0] = 0;
    size_t at = 0;
#define ADD(...) (at += (size_t)snprintf(out + at, at < n ? n - at : 0, __VA_ARGS__), at = at < n ? at : n)
    if (f->no_spawning) ADD("%sspawning=off", at ? "," : "");
    if (f->skip_types)
    {
        ADD("%sspawn_types=", at ? "," : "");
        int any = 0;
        for (int t = 0; t < CT_TYPES; ++t)
            if (!(f->skip_types >> t & 1)) ADD("%s%s", any++ ? "," : "", TYPES[t]);
        if (!any) ADD("none");
    }
    if (f->keep_on)
    {
        ADD("%smobs=", at ? "," : "");
        int any = 0;
        for (int k = 0; k < HK_KINDS; ++k)
            if (f->keep >> k & 1) ADD("%s%s", any++ ? "+" : "", living_kind_name(k));
        if (!any) ADD("none");
    }
    if (f->bits & FID_NO_RANDOM_TICKS) ADD("%srandom_ticks=off", at ? "," : "");
    if (f->bits & FID_NO_WEATHER) ADD("%sweather=off", at ? "," : "");
    if (f->bits & FID_SKIP_AMBIENT) ADD("%sskip_ambient=on", at ? "," : "");
    if (f->distant_tick_radius > 0) ADD("%sdistant_tick_radius=%d", at ? "," : "", f->distant_tick_radius);
    if (!at) ADD("vanilla");
#undef ADD
}

/* The categories mobs leaves nothing to: no biome's list holds a kept kind.
 * Left out whole, since their packs would all be dropped, and with none of
 * their mobs alive the count never reaches the cap, so the spawner would
 * walk every chunk every tick for nothing. */
static unsigned empty_types(const struct fidelity *f)
{
    unsigned skip = 0;
    for (int t = 0; f->keep_on && t < CT_TYPES; ++t)
    {
        int any = 0;
        for (int b = 0; b < 256 && !any; ++b)
        {
            const struct sp_list *l = spawning_list_for(b, t);
            for (int e = 0; e < l->n; ++e)
                if (f->keep >> l->kind[e] & 1) any = 1;
        }
        if (!any) skip |= 1u << t;
    }
    return skip;
}

void fidelity_apply(const struct fidelity *f, struct serverreplay *sr)
{
    unsigned skip_types = f->skip_types | empty_types(f);
    if (f->bits & FID_SKIP_AMBIENT) skip_types |= (1u << CT_AMBIENT) | (1u << CT_WATER);
    int dist_r2 = f->distant_tick_radius > 0 ? f->distant_tick_radius * f->distant_tick_radius : 0;
    sr->fid_distant_r2 = dist_r2;

    /* every world and dimension state is made at the load (serverreplay_load) */
    for (int i = 0; i < sr->nworlds; ++i)
    {
        sr->w[i].st.fid = f->bits;
        sr->w[i].st.fid_skip_types = skip_types;
        sr->w[i].st.fid_keep_on = f->keep_on;
        sr->w[i].st.fid_keep = f->keep;
        sr->w[i].st.fid_distant_r2 = dist_r2;
    }
    for (int d = 0; d < 3; ++d)
    {
        sr->dims[d].anw.fid_keep_on = f->keep_on;
        sr->dims[d].anw.fid_keep = f->keep;
        sr->dims[d].anw.fid_skip_ambient = (f->bits & FID_SKIP_AMBIENT) != 0;
    }
    if (f->no_spawning)
    {
        /* the doMobSpawning rule false: sr_world_begin installs no spawner,
         * and BlockPortal rolls no pigman */
        sr->natural_spawning = 0;
        for (int i = 0; i < sr->nworlds; ++i) sr->w[i].st.mob_spawning = 0;
    }
    /* every category left out: no spawner tick at all (its walk over the
     * livings and the eligible chunks costs with nothing to spawn) */
    if ((skip_types & ((1u << CT_TYPES) - 1)) == (1u << CT_TYPES) - 1) sr->natural_spawning = 0;
    if (!f->keep_on && !(f->bits & FID_SKIP_AMBIENT)) return;
    for (int d = 0; d < 3; ++d)
    {
        struct an_world *an = &sr->dims[d].anw;
        if (!sr->dims[d].live) continue;
        for (int i = 0; i < an->n; ++i)
        {
            const struct an_ent *en = an_ent_at(an->slot[i]);
            if (!en->is_living || en->livh == 0 || en->livh == sr->player_livh) continue;
            struct living *l = lv_get(en->livh);
            if (l->kind == SK_PLAYER || l->kind == HK_PLAYER) continue;
            int cull = 0;
            if ((f->bits & FID_SKIP_AMBIENT) && (l->kind == AK_BAT || l->kind == AK_SQUID)) cull = 1;
            if (f->keep_on && !(f->keep >> l->kind & 1)) cull = 1;
            if (cull) living_set_dead(l);
        }
    }
}
