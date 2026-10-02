/* FurnaceRecipes, out of the generated table (recipes.h). The two lookups walk
 * different maps and Java's experience lookup is the surprising one: it compares
 * the queried stack against each key of experienceList, and those keys are
 * result stacks, not inputs. See smelting.h. */
#include "smelting.h"

#include <stdlib.h>

/* A table entry's damage that takes any queried damage. */
#define WILDCARD 32767

static int matches(const struct stack_def *key, int item, int damage)
{
    return key->exists && key->item == item
        && (key->damage == WILDCARD || key->damage == damage);
}

int smelt_result(int item, int damage, struct craft_stack *out)
{
    for (int i = 0; i < SMELTING_COUNT; ++i)
    {
        const struct smelting_def *s = &SMELTING[i];

        if (!matches(&s->input, item, damage)) continue;

        out->item = s->output.item;
        out->count = s->output.count;
        out->damage = s->output.damage;
        out->tag = 0;
        return 1;
    }

    return 0;
}

float smelt_experience(int item, int damage)
{
    for (int i = 0; i < (int)(sizeof SMELT_EXPERIENCE / sizeof SMELT_EXPERIENCE[0]); ++i)
    {
        const struct smelting_def *s = &SMELT_EXPERIENCE[i];

        if (matches(&s->input, item, damage)) return s->experience;
    }

    return 0.0f;
}