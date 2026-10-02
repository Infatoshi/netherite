#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/hostiles.h"
#include "../engine/hostiles_creeper.h"
#include "../engine/item_entity.h"
#include "../engine/lightning.h"
#include "../engine/living.h"
#include "../engine/serverreplay.h"
#include "../engine/env.h"
#include "../engine/world.h"

struct ref {
    int64_t seed;
    int32_t ids[4];
    uint64_t seeder[4], math[4];
    char names[64][128];
    uint64_t state[64][4];
    uint8_t used[64][4];
    int nsplits;
};

static int ref_read(const char *path, struct ref *r)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    memset(r, 0, sizeof *r);
    char line[1024];
    while (fgets(line, sizeof line, f))
    {
        long long seed;
        int role;
        unsigned long long a, b, c, d;
        if (sscanf(line, "worldSeed %lld", &seed) == 1) r->seed = seed;
        else if (sscanf(line, "nextId %d %d %d %d", &r->ids[0], &r->ids[1],
                        &r->ids[2], &r->ids[3]) == 4) {}
        else if (sscanf(line, "digest %d %llx %llx %llx", &role, &a, &b, &c) == 4)
        { r->seeder[role] = a; r->math[role] = b; }
        else if (r->nsplits < 64)
        {
            char name[128]; int u[4];
            if (sscanf(line, "split %127s %llx %llx %llx %llx %d %d %d %d",
                       name, &a, &b, &c, &d, &u[0], &u[1], &u[2], &u[3]) == 9)
            {
                int n = r->nsplits++;
                snprintf(r->names[n], sizeof r->names[n], "%s", name);
                r->state[n][0] = a; r->state[n][1] = b;
                r->state[n][2] = c; r->state[n][3] = d;
                for (int j = 0; j < 4; ++j) r->used[n][j] = (uint8_t)u[j];
            }
        }
    }
    fclose(f);
    return 1;
}

static void add_ent(struct serverreplay *sr, int pool, void *ent)
{
    if (sr->d->nents == sr->d->capents)
    {
        sr->d->capents = sr->d->capents ? sr->d->capents * 2 : 16;
        sr->d->ents = realloc(sr->d->ents, (size_t)sr->d->capents * sizeof *sr->d->ents);
    }
    sr->d->ents[sr->d->nents].pool = pool;
    sr->d->ents[sr->d->nents].id = sr_ent_id(sr, pool, ent);
    ++sr->d->nents;
}

static unsigned fire_mask(struct world *w, int x, int y, int z)
{
    unsigned mask = 0;
    int bit = 0;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz, ++bit)
                if ((world_get_block(w, x + dx, y + dy, z + dz) & 4095) == 51)
                    mask |= 1u << bit;
    return mask;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: test_lightning DIR\n"); return 2; }
    char path[1024], manifest[1024];
    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);
    FILE *mf = fopen(path, "r");
    if (!mf) { printf("skip %s: no manifest\n", argv[1]); return 0; }
    if (!fgets(manifest, sizeof manifest, mf)) return 2;
    fclose(mf);
    if (!strstr(manifest, "\"kind\":\"lightning2\"")) return 0;
    char *difficulty_key = strstr(manifest, "\"difficulty\":");
    int difficulty = difficulty_key ? atoi(difficulty_key + 13) : 2;
    int fire_off = strstr(manifest, "\"fire_off\":true") != NULL;

    struct ref start, end;
    snprintf(path, sizeof path, "%s/start.txt", argv[1]);
    if (!ref_read(path, &start)) return 2;
    snprintf(path, sizeof path, "%s/end.txt", argv[1]);
    if (!ref_read(path, &end)) return 2;
    struct serverreplay *sr = calloc(1, sizeof *sr);
    serverreplay_dims_init(sr);
    world_init(&sr->pop.world, start.seed);
    struct world *w = &sr->pop.world;
    for (int cx = 90; cx <= 116; ++cx)
        for (int cz = 90; cz <= 110; ++cz) world_load_chunk(w, cx, cz);
    int xs[6], ys[6], zs[6];
    snprintf(path, sizeof path, "%s/places.txt", argv[1]);
    FILE *pf = fopen(path, "r");
    if (!pf) return 2;
    for (int i = 0; i < 6; ++i)
    {
        if (fscanf(pf, "%d %d %d", &xs[i], &ys[i], &zs[i]) != 3) return 2;
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz)
            {
                world_set_block(w, xs[i] + dx, ys[i] - 1, zs[i] + dz, 1, 0, 2);
                for (int dy = 0; dy <= 3; ++dy)
                    world_set_block(w, xs[i] + dx, ys[i] + dy, zs[i] + dz, 0, 0, 2);
            }
    }
    fclose(pf);
    det_init(&SR_DET(sr));
    det_load(&SR_DET(sr), start.seed, start.seeder, start.math, start.ids);
    for (int i = 0; i < start.nsplits; ++i)
        det_split_add(&SR_DET(sr), start.names[i], start.state[i], start.used[i]);
    an_init(&sr->d->anw, w, &SR_DET(sr));
    ie_init(&sr->d->iew, w, &SR_DET(sr));
    sr->mobs_enabled = 1;
    det_state fake;
    det_init(&fake);
    det_reset(&fake, start.seed);
    struct living *living[6] = {0};
    ie_ent *item = NULL;
    int fails = 0;
    snprintf(path, sizeof path, "%s/targets.txt", argv[1]);
    FILE *tf = fopen(path, "r");
    if (!tf) return 2;
    char sx[64], sy[64], sz[64];
    int kind, id;
    unsigned long long rand_state;
    while (fscanf(tf, "%d %d %63s %63s %63s %llx", &kind, &id, sx, sy, sz,
                  &rand_state) == 6)
    {
        double x = strtod(sx, NULL), y = strtod(sy, NULL), z = strtod(sz, NULL);
        if (kind == 4)
        {
            item = ie_adopt_item(&sr->d->iew, id, 0, 0, rand_state, x, y, z,
                                 0, 0, 0, 0.0F, 0.0F, 1, 0, 1, 0);
            ie_added_to_world(&sr->d->iew, item);
            add_ent(sr, 0, item);
        }
        else
        {
            int lk = kind == 1 ? AK_PIG : kind == 2 ? VK_VILLAGER :
                     kind == 3 ? HK_CREEPER : HK_PLAYER;
            struct living *l = living_alloc();
            living_init(l, w, lk, &fake);
            l->an = &sr->d->anw;
            if (kind == 1) animal_construct(l, &fake);
            else if (kind == 2) villager_construct(l, &fake);
            else if (kind == 3) creeper_construct(l, &fake);
            else player_construct(l, &fake);
            if (kind == 5)
            {
                attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 20.0);
                living_set_health(l, 20.0F);
            }
            living_set_location_and_angles(l, x, y, z, 0.0F, 0.0F);
            l->entity_id = id;
            l->rand.r.seed = rand_state;
            living[kind] = l;
            if (kind == 5) sr->player_livh = lv_ref(l);
            else
            {
                an_add_living(&sr->d->anw, l, sr->d->anw.n);
                add_ent(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
            }
        }
    }
    fclose(tf);

    struct lightning_bolt bolts[6];
    snprintf(path, sizeof path, "%s/bolts.txt", argv[1]);
    FILE *bf = fopen(path, "r");
    if (!bf) return 2;
    for (int i = 0; i < 6; ++i)
    {
        int idx, eid, life;
        long long vertex;
        unsigned long long final_rand;
        if (fscanf(bf, "%d %d %lld %d %llx", &idx, &eid, &vertex, &life, &final_rand) != 5)
            return 2;
        int native_id = det_next_entity_id_role(&SR_DET(sr), DET_OTHER);
        det_rng rng = det_new_random_role(&SR_DET(sr), DET_OTHER);
        int64_t msb, lsb;
        det_uuid_role(&SR_DET(sr), DET_OTHER, &msb, &lsb);
        int64_t native_vertex = det_rng_long(&rng);
        int native_life = det_rng_int_n(&rng, 3) + 1;
        lightning_constructor_fire(w, &rng, xs[i] + 0.5, ys[i], zs[i] + 0.5,
                                   difficulty, !fire_off);
        if (native_id != eid || native_vertex != vertex || native_life != life ||
            det_rng_state(&rng) != final_rand)
        { printf("FAIL bolt %d constructor\n", i); ++fails; }
        lightning_adopt(&bolts[i], xs[i] + 0.5, ys[i], zs[i] + 0.5,
                        det_rng_state(&rng), native_vertex, native_life);
    }
    fclose(bf);

    snprintf(path, sizeof path, "%s/ticks.txt", argv[1]);
    FILE *rows = fopen(path, "r");
    if (!rows) return 2;
    snprintf(path, sizeof path, "%s/pigman-nbt.txt", argv[1]);
    FILE *pigman_rows = fopen(path, "r");
    if (!pigman_rows) return 2;
    for (int t = 0; t < 30; ++t)
    {
        for (int i = 0; i < 6; ++i)
            if (!bolts[i].dead)
                lightning_tick(&bolts[i], w, !fire_off, serverreplay_lightning_strike, sr);
        for (int i = 0; i < 6; ++i)
        {
            int rt, ri, state, life, dead, existed, target_dead, target_fire;
            int powered, pigmen, pigman_id;
            long long vertex;
            unsigned long long rng;
            unsigned mask, hpbits;
            if (fscanf(rows, "%d %d %d %d %lld %d %d %llx %x %d %x %d %d %d %d",
                       &rt, &ri, &state, &life, &vertex, &dead, &existed, &rng,
                       &mask, &target_dead, &hpbits, &target_fire, &powered,
                       &pigmen, &pigman_id) != 15) return 2;
            struct lightning_bolt *b = &bolts[i];
            float hp = 0.0F;
            int ndead = 0, nfire = 0, npowered = 0, npigmen = 0, npigman_id = 0;
            if (i == 4) { hp = (float)item->health; ndead = item->is_dead; nfire = item->e.fire; }
            else if (i > 0)
            {
                struct living *l = living[i];
                hp = l->health; ndead = l->is_dead; nfire = l->e.fire;
                if (i == 3) npowered = l->creeper_powered;
            }
            for (int j = 0; j < sr->d->anw.n; ++j)
            {
                struct an_ent *e = an_ent_at(sr->d->anw.slot[j]);
                if (e->is_living && lv_get(e->livh)->kind == HK_PIGMAN &&
                    fabs(lv_get(e->livh)->e.pos_x - (xs[1] + 0.5)) < 3)
                { ++npigmen; npigman_id = lv_get(e->livh)->entity_id; }
            }
            uint32_t nhp;
            memcpy(&nhp, &hp, 4);
            if (rt != t || ri != i || state != b->state || life != b->living_time ||
                vertex != b->vertex || dead != b->dead || existed != b->ticks_existed ||
                rng != det_rng_state(&b->rand) || mask != fire_mask(w, xs[i], ys[i], zs[i]) ||
                target_dead != ndead || hpbits != nhp || target_fire != nfire ||
                powered != npowered || pigmen != npigmen || pigman_id != npigman_id)
            {
                if (fails < 14)
                    printf("FAIL lightning t=%d i=%d state %d/%d life %d/%d rand %llx/%llx mask %x/%x hp %x/%x fire %d/%d pigmen %d/%d\n",
                           t, i, state, b->state, life, b->living_time,
                           rng, (unsigned long long)det_rng_state(&b->rand),
                           mask, fire_mask(w, xs[i], ys[i], zs[i]), hpbits, nhp,
                           target_fire, nfire, pigmen, npigmen);
                ++fails;
            }
            if (i == 1 && npigmen > 0)
            {
                char expected[16384];
                if (!fgets(expected, sizeof expected, pigman_rows)) return 2;
                char *space = strchr(expected, ' ');
                if (!space || atoi(expected) != t) return 2;
                char *want = space + 1;
                want[strcspn(want, "\r\n")] = 0;
                struct living *pigman = NULL;
                for (int j = 0; j < sr->d->anw.n; ++j)
                    if (an_ent_at(sr->d->anw.slot[j])->is_living && lv_get(an_ent_at(sr->d->anw.slot[j])->livh)->kind == HK_PIGMAN)
                    { pigman = lv_get(an_ent_at(sr->d->anw.slot[j])->livh); break; }
                char *got = NULL;
                if (pigman) living_nbt_text(pigman, &got);
                if (!got || strcmp(want, got) != 0)
                {
                    if (fails < 14)
                    {
                        size_t at = 0;
                        if (got) while (want[at] && got[at] && want[at] == got[at]) ++at;
                        printf("FAIL lightning t=%d pigman NBT byte %zu want=%.65s got=%.65s\n",
                               t, at, want + at, got ? got + at : "(missing)");
                    }
                    ++fails;
                }
                free(got);
            }
        }
    }
    fclose(rows);
    fclose(pigman_rows);
    if (SR_DET(sr).next_id[DET_OTHER] != end.ids[DET_OTHER] ||
        det_seeder_state(&SR_DET(sr), DET_OTHER) != end.seeder[DET_OTHER] ||
        det_math_state(&SR_DET(sr), DET_OTHER) != end.math[DET_OTHER])
    { printf("FAIL lightning end Det\n"); ++fails; }
    if (!fails) printf("PASS %s (6 bolts, 30 ticks, six strike scenes)\n", argv[1]);
    else printf("FAIL %s: %d differences\n", argv[1], fails);
    world_free(w);
    det_free(&fake);
    det_free(&SR_DET(sr));
    free(sr);
    return fails ? 1 : 0;
}
