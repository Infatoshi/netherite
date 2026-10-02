/* The biome creature spawn lists Minecraft 1.7.10 hands performWorldGenSpawning,
 * read off the live biome objects after WorldConf.pruneSpawns (horses, wolves and
 * ocelots are off, so the entries they add are gone). Generated from the oracle:
 *
 *   make -C oracle script SCRIPT=<a script holding {"cmd":"quit"}> \
 *     ARGS="--spawndump DIR"          (out/java/tmp/seedquit.jsonl)
 *   jq -c . DIR/creatures.jsonl       (one line per registered biome id)
 *
 * Four lists cover every registered biome: the four entries every biome starts
 * with, the jungle's extra chicken, the mushroom island's mooshroom, and the
 * empty list (desert, beach, ocean, river, snow, stone beach, mesa, the Nether
 * and the End clear theirs). BiomeGenBase.SpawnListEntry fields, in list order:
 * the weight WeightedRandom.getRandomItem reads, the min and max group count,
 * and the entity class the group constructs. */
#ifndef NETHERITE_SEEDWORLD_CREATURES_H
#define NETHERITE_SEEDWORLD_CREATURES_H

struct sw_spawn_entry
{
    int weight, min, max;
    int cls;
};

static const struct sw_spawn_entry SW_LIST_DEFAULT[] = {
    {12, 4, 4, SW_SHEEP},
    {10, 4, 4, SW_PIG},
    {10, 4, 4, SW_CHICKEN},
    {8, 4, 4, SW_COW},
};

static const struct sw_spawn_entry SW_LIST_MOOSHROOM[] = {
    {8, 4, 8, SW_MOOSHROOM},
};

static const struct sw_spawn_entry SW_LIST_JUNGLE[] = {
    {12, 4, 4, SW_SHEEP},
    {10, 4, 4, SW_PIG},
    {10, 4, 4, SW_CHICKEN},
    {8, 4, 4, SW_COW},
    {10, 4, 4, SW_CHICKEN},
};

/* Biome id -> its list index: 0 empty, 1 default, 2 mooshroom island,
 * 3 jungle; -1 is an id no registered biome has (the generator never makes
 * one, so it is an error to read this and find -1). */
static const int8_t SW_BIOME_LIST[256] = {
       0,    1,    0,    1,    1,    1,    1,    0,    0,    0,    0,    0,    0,    0,    2,    2,
       0,    0,    1,    1,    1,    3,    3,    3,    0,    0,    0,    1,    1,    1,    1,    1,
       1,    1,    1,    1,    1,    0,    0,    0,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,    1,    0,    1,    1,    1,    1,   -1,   -1,   -1,   -1,   -1,    0,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,    3,   -1,    3,   -1,   -1,   -1,    1,    1,    1,    1,   -1,
       1,    1,    1,    1,    1,    0,    0,    0,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
};

#endif
