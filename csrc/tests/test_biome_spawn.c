/* Gate: every biome's spawn lists (csrc/engine/spawning.c spawning_list_for)
 * and the swamp hut's witch list (spawning_scattered_witch) against the
 * oracle's BiomeSpawnProbe recording (out/java/biome_spawn/<name>/, made with
 *
 *   make -C oracle run SEED=1 CLASS=BiomeSpawnProbe NAME=bs-1 \
 *     CMD='{"out":"<abs>/out/java/biome_spawn/bs-1"}'
 *
 * ). For each registered biome and each EnumCreatureType the rows (class,
 * weight, min and max group) must match in order.
 *
 * Negative check: --negative=noslime answers the swamp's monster list with
 * the base list (the slime row BiomeGenSwamp adds is lost); it must fail.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/nbtjson.h"
#include "../engine/spawning.h"
#include "../engine/tape.h"

static const char *kind_name(int k)
{
    switch (k)
    {
    case SP_ZOMBIE: return "EntityZombie";
    case SP_SKELETON: return "EntitySkeleton";
    case SP_SPIDER: return "EntitySpider";
    case SP_CREEPER: return "EntityCreeper";
    case SP_SLIME: return "EntitySlime";
    case SP_ENDERMAN: return "EntityEnderman";
    case SP_WITCH: return "EntityWitch";
    case SP_SHEEP: return "EntitySheep";
    case SP_PIG: return "EntityPig";
    case SP_CHICKEN: return "EntityChicken";
    case SP_COW: return "EntityCow";
    case SP_MOOSHROOM: return "EntityMooshroom";
    case SP_BAT: return "EntityBat";
    case SP_SQUID: return "EntitySquid";
    case SP_GHAST: return "EntityGhast";
    case SP_PIG_ZOMBIE: return "EntityPigZombie";
    case SP_MAGMA_CUBE: return "EntityMagmaCube";
    case SP_BLAZE: return "EntityBlaze";
    default: return "?";
    }
}

static int compare_list(const char *dir, const char *what, const struct jval *rows, const struct sp_list *l)
{
    if (json_len(rows) != l->n)
    {
        printf("FAIL %s: %s has %d rows, native %d\n", dir, what, json_len(rows), l->n);
        return 0;
    }
    for (int i = 0; i < l->n; ++i)
    {
        const struct jval *r = json_at(rows, i);
        int64_t w = 0, mn = 0, mx = 0;
        json_int(json_get(r, "weight"), &w);
        json_int(json_get(r, "min"), &mn);
        json_int(json_get(r, "max"), &mx);
        const char *cls = json_str(json_get(r, "class"));
        if (!cls || strcmp(cls, kind_name(l->kind[i])) || w != l->weight[i] ||
            mn != l->min_group[i] || mx != l->max_group[i])
        {
            printf("FAIL %s: %s row %d: the oracle %s %lld %lld-%lld, native %s %d %d-%d\n", dir, what, i,
                   cls ? cls : "?", (long long)w, (long long)mn, (long long)mx,
                   kind_name(l->kind[i]), l->weight[i], l->min_group[i], l->max_group[i]);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    int negative_noslime = 0;
    const char *dir = NULL;

    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--negative=noslime")) negative_noslime = 1;
        else dir = argv[i];
    }
    if (dir == NULL)
    {
        fprintf(stderr, "usage: test_biome_spawn [--negative=noslime] DIR\n");
        return 2;
    }

    char path[4096];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAIL %s: no manifest.json\n", dir); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)len + 1);
    if (fread(text, 1, (size_t)len, f) != (size_t)len) { fclose(f); return 2; }
    text[len] = 0;
    fclose(f);
    struct jval *m = json_parse(text);   /* takes the buffer over */
    const char *kind = m ? json_str(json_get(m, "kind")) : NULL;
    if (!kind || strcmp(kind, "biome_spawn"))
    {
        printf("FAIL %s: not a biome_spawn manifest\n", dir);
        return 1;
    }

    static const char *types[CT_TYPES] = { "monster", "creature", "ambient", "water" };
    const struct jval *biomes = json_get(m, "biomes");
    int ok = 1, nb = json_len(biomes), rows = 0;

    for (int b = 0; b < nb; ++b)
    {
        const struct jval *o = json_at(biomes, b);
        int64_t id = -1;
        json_int(json_get(o, "id"), &id);
        const char *name = json_str(json_get(o, "name"));

        for (int t = 0; t < CT_TYPES; ++t)
        {
            const struct sp_list *l = spawning_list_for((int)id, t);
            if (negative_noslime && t == CT_MONSTER && id == 6) l = spawning_list_for(1, t);
            char what[160];
            snprintf(what, sizeof what, "biome %lld (%s) %s", (long long)id, name ? name : "?", types[t]);
            ok &= compare_list(dir, what, json_get(o, types[t]), l);
            rows += l->n;
        }
    }
    ok &= compare_list(dir, "the swamp hut's list", json_get(m, "witch"), spawning_scattered_witch());

    json_free(m);
    if (!ok) return 1;
    printf("OK %s: %d biomes x %d types, %d rows, and the hut's list\n", dir, nb, CT_TYPES, rows);
    return 0;
}
