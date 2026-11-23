/* The game's stages (lane/starts): what a start holds and where it is, read from the milestone mask (reward.MILESTONES,
 * vecenv.h NETH_VEC_MILESTONES: an item held) and the dimension, in the order a run reaches them. A start's stage is the
 * last entry of the chain it has reached (its items held now, its dimension now); its next milestone the first entry
 * past its stage it has not. Shared by the library's harvester (ntstarts: a Java tape's rows) and the trainer's
 * curriculum (ntloop: the episode records' ms and dims).
 *
 * A milestone bit added to vecenv.h (lane/dragon's End and dragon bits) fails the build at the assert below until its
 * entry is put in the chain and its item (or none) in STAGE_MS_ITEM: the stages follow the mask, never a fixed list
 * of names elsewhere. */
#ifndef NETHERITE_RUNTIME_TRAIN_STAGES_H
#define NETHERITE_RUNTIME_TRAIN_STAGES_H

#include <stdint.h>
#include <string.h>

#include "../vecenv.h"

_Static_assert(NETH_VEC_MILESTONES == 13, "a milestone bit was added: put it in stages.h's chain and STAGE_MS_ITEM");

/* reward.MILESTONES' items (vecenv.c MS_ITEMS), bit i */
static const int16_t STAGE_MS_ITEM[NETH_VEC_MILESTONES] = {270, 274, 257, 278, 61, 265, 325, 259, 49, 369, 377, 368,
                                                           381};
static const char *const STAGE_MS_NAME[NETH_VEC_MILESTONES] = {
    "wooden_pickaxe", "stone_pickaxe", "iron_pickaxe", "diamond_pickaxe", "furnace", "iron_ingot", "bucket",
    "flint_and_steel", "obsidian", "blaze_rod", "blaze_powder", "ender_pearl", "ender_eye"};

/* a chain entry: a milestone bit (kind 0, ms bit) or a dimension entered (kind 1, the dimension's id); lane/killlib's
 * late entries (ids 16 to 25, after the original 16, whose ids and masks are unchanged): STAGE_EVENT, an event a row's
 * fields do not carry, found by the checked C replay of the Java recording (test_snapshots --stage-events: the first
 * end portal frame within 4 blocks of the player, the end portal lit in an overworld row, crystals destroyed counted
 * by entity id, the dragon's health at or under 150, 100, 50), and STAGE_DRAGON, the dragon's death */
struct stage_ent { const char *name; int kind, v; };
enum { STAGE_MS, STAGE_DIM, STAGE_EVENT, STAGE_DRAGON };
#define STAGE_BASE_N 16         /* existing ids stay fixed */
#define STAGE_N 26
static const struct stage_ent STAGE_CHAIN[STAGE_N] = {
    {"spawn", -1, 0},
    {"wooden_pickaxe", STAGE_MS, 0}, {"stone_pickaxe", STAGE_MS, 1}, {"furnace", STAGE_MS, 4},
    {"iron_ingot", STAGE_MS, 5}, {"iron_pickaxe", STAGE_MS, 2}, {"bucket", STAGE_MS, 6},
    {"flint_and_steel", STAGE_MS, 7}, {"diamond_pickaxe", STAGE_MS, 3}, {"obsidian", STAGE_MS, 8},
    {"nether", STAGE_DIM, -1}, {"blaze_rod", STAGE_MS, 9}, {"blaze_powder", STAGE_MS, 10},
    {"ender_pearl", STAGE_MS, 11}, {"ender_eye", STAGE_MS, 12}, {"end", STAGE_DIM, 1},
    {"stronghold_room", STAGE_EVENT, 0}, {"portal_lit", STAGE_EVENT, 0},
    {"crystals_gone1", STAGE_EVENT, 0}, {"crystals_gone3", STAGE_EVENT, 0},
    {"crystals_gone6", STAGE_EVENT, 0}, {"crystals_gone9", STAGE_EVENT, 0},
    {"dragon_75", STAGE_EVENT, 0}, {"dragon_50", STAGE_EVENT, 0},
    {"dragon_25", STAGE_EVENT, 0}, {"dragon_dead", STAGE_DRAGON, NETH_VEC_MS_DRAGON}};

/* the milestone mask of a held inventory: ids[k] count[k] over k < n */
static inline uint32_t stage_ms_of(const int *ids, const int *counts, int n)
{
    uint32_t m = 0;
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
            if (counts[k] > 0 && ids[k] == STAGE_MS_ITEM[i]) m |= 1u << i;
    return m;
}

/* entry j reached with mask ms in dimension dim (a dimension entry: being there; dims, a vecenv episode's dims bits
 * (bit 0 the Nether, 2 the End), as having been there; -1 none) */
static inline int stage_has(int j, uint32_t ms, int dim, int dims)
{
    const struct stage_ent *s = &STAGE_CHAIN[j];
    if (s->kind == STAGE_MS) return (ms >> s->v & 1) != 0;
    if (s->kind == STAGE_DIM)
        return dim == s->v || (dims >= 0 && ((s->v == -1 && (dims & 1)) || (s->v == 1 && (dims & 4))));
    if (s->kind == STAGE_DRAGON) return (ms >> s->v & 1) != 0;
    if (s->kind == STAGE_EVENT) return 0;
    return 1;
}

/* the stage: the last chain entry reached (0 spawn) */
static inline int stage_of(uint32_t ms, int dim)
{
    int st = 0;
    for (int j = 1; j < STAGE_BASE_N; ++j)
        if (stage_has(j, ms, dim, -1)) st = j;
    return st;
}

/* the next entry past stage st not reached with (ms, dim), STAGE_N when none */
static inline int stage_next(int st, uint32_t ms, int dim)
{
    if (st == 16 || st == 17) return 15; /* room or lit portal -> End */
    if (st == 25) return STAGE_N;        /* the kill is terminal */
    if (st >= 15) return 25;             /* End fight -> dragon death */
    for (int j = st + 1; j < STAGE_BASE_N; ++j)
        if (!stage_has(j, ms, dim, -1)) return j;
    return STAGE_N;
}

static inline int stage_by_name(const char *name)
{
    for (int j = 0; j < STAGE_N; ++j)
        if (!strcmp(name, STAGE_CHAIN[j].name)) return j;
    return -1;
}

#endif
