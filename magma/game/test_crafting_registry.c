#include "crafting_registry_cases.h"

#include <stdio.h>

static CRStack crafting_remainder(CRStack input) {
    if (input.item == 326 || input.item == 327 || input.item == 335)
        return crf_mk(325, 1, 0);
    if (input.item == 437) return crf_mk(374, 1, 0);
    return crf_empty();
}

static void print_stack(CRStack stack) {
    if (crf_isEmpty(stack) || stack.item == (i32)0xffffffff)
        printf(" 0:0:0");
    else
        printf(" %d:%d:%d", stack.item, stack.count, stack.meta);
}

int main(void) {
    static CRRecipe recipes[CRF_NRECIPES];
    int count = crf_build(recipes);
    for (int index = 0; index < CRF_REGISTRY_NCASES; ++index) {
        const CrfRegistryCase *test = &crf_registry_cases[index];
        CRStack result = crf_findMatching(recipes, count, test->grid);
        int matched = !crf_isEmpty(result)
            && result.item != (i32)0xffffffff;
        printf("R %d %c", test->registry_index, test->variant);
        print_stack(result);
        for (int slot = 0; slot < 9; ++slot)
            print_stack(matched ? crafting_remainder(test->grid[slot])
                                : test->grid[slot]);
        putchar('\n');
    }
    printf("COUNT %d 400\n", CRF_NRECIPES);
    return 0;
}
