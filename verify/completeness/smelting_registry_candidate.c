#include <stdio.h>

#include "smelting_recipes.h"

int main(void) {
    SRRecipe recipes[SR_NRECIPES];
    const int negatives[][2] = {
        {0, 0}, {19, 0}, {19, 2}, {98, 1}, {98, 2},
        {349, 2}, {349, 3}, {331, 0}, {500, 0},
    };
    int count = sr_build(recipes);

    printf("COUNT %d\n", count);
    for (int i = 0; i < count; ++i) {
        SRStack hot = sr_getSmeltingResultBuiltin(recipes[i].input);
        printf("R %d %d %d %d %d %d %d %d %d\n",
               recipes[i].input.item, recipes[i].input.count,
               recipes[i].input.meta, recipes[i].output.item,
               recipes[i].output.count, recipes[i].output.meta,
               hot.item, hot.count, hot.meta);
    }
    for (int i = 0; i < (int)(sizeof(negatives) / sizeof(negatives[0])); ++i) {
        SRStack hot = sr_getSmeltingResultBuiltin(
            sr_mk(negatives[i][0], 1, negatives[i][1]));
        printf("N %d %d %d %d %d\n",
               negatives[i][0], negatives[i][1],
               hot.item, hot.count, hot.meta);
    }
    return 0;
}
