/* crafting_recipes_full: MC 1.11.2 CraftingManager KEEP-scope recipe set (generated + hand header).
 * Matcher = verbatim ShapedRecipes/ShapelessRecipes/findMatchingRecipe (same as crafting_recipes.h).
 * Registry = RecipesTools/Weapons/Ingots/Food + RecipesCrafting(chest,furnace,table) + RecipesArmor +
 * inline planks/sticks/torch/flint_and_steel plus route-critical End/brewing recipes. 99 recipes.
 * CUT: RecipesDyes, special IRecipes, CraftingManager decorative/redstone inline rows. */
#ifndef MC_CRAFTING_RECIPES_FULL_H
#define MC_CRAFTING_RECIPES_FULL_H
#include "mc.h"

#define CRF_WILDCARD 32767
typedef struct { i32 item; i32 count; i32 meta; } CRStack;
MC_HD static inline CRStack crf_empty(void) { CRStack s; s.item = 0; s.count = 0; s.meta = 0; return s; }
MC_HD static inline CRStack crf_mk(i32 item, i32 count, i32 meta) { CRStack s; s.item = item; s.count = count; s.meta = meta; return s; }
MC_HD static inline int crf_isEmpty(CRStack s) { return s.item == 0 || s.count <= 0; }
typedef struct { int shaped; int width, height; int nIng; CRStack ing[9]; CRStack output; } CRRecipe;
#define CRF_GRID 3
MC_HD static inline CRStack crf_blk(i32 id) { return crf_mk(id, 1, CRF_WILDCARD); }
MC_HD static inline CRStack crf_it(i32 id)  { return crf_mk(id, 1, 0); }
MC_HD static inline CRStack crf_is(i32 id, i32 meta) { return crf_mk(id, 1, meta); }
MC_HD static inline int crf_checkMatch(const CRRecipe *r, const CRStack *grid, int offX, int offY, int mirror) {
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        int k = i - offX, l = j - offY; CRStack ing = crf_empty();
        if (k >= 0 && l >= 0 && k < r->width && l < r->height)
            ing = mirror ? r->ing[r->width - k - 1 + l * r->width] : r->ing[k + l * r->width];
        CRStack g = grid[i + j * CRF_GRID];
        if (!crf_isEmpty(g) || !crf_isEmpty(ing)) {
            if (crf_isEmpty(g) != crf_isEmpty(ing)) return 0;
            if (ing.item != g.item) return 0;
            if (ing.meta != CRF_WILDCARD && ing.meta != g.meta) return 0;
        }
    }
    return 1;
}
MC_HD static inline int crf_shapedMatches(const CRRecipe *r, const CRStack *grid) {
    for (int i = 0; i <= 3 - r->width; ++i) for (int j = 0; j <= 3 - r->height; ++j) {
        if (crf_checkMatch(r, grid, i, j, 1)) return 1;
        if (crf_checkMatch(r, grid, i, j, 0)) return 1;
    }
    return 0;
}
MC_HD static inline int crf_shapelessMatches(const CRRecipe *r, const CRStack *grid) {
    int used[9]; for (int i = 0; i < r->nIng; ++i) used[i] = 0;
    int remaining = r->nIng;
    for (int i = 0; i < CRF_GRID; ++i) for (int j = 0; j < CRF_GRID; ++j) {
        CRStack g = grid[j + i * CRF_GRID];
        if (!crf_isEmpty(g)) {
            int flag = 0;
            for (int z = 0; z < r->nIng; ++z) if (!used[z]) {
                CRStack ig = r->ing[z];
                if (g.item == ig.item && (ig.meta == CRF_WILDCARD || g.meta == ig.meta)) {
                    flag = 1; used[z] = 1; --remaining; break;
                }
            }
            if (!flag) return 0;
        }
    }
    return remaining == 0;
}
MC_HD static inline int crf_matches(const CRRecipe *r, const CRStack *grid) {
    return r->shaped ? crf_shapedMatches(r, grid) : crf_shapelessMatches(r, grid);
}
MC_HD static inline CRStack crf_findMatching(const CRRecipe *recipes, int n, const CRStack *grid) {
    for (int i = 0; i < n; ++i) if (crf_matches(&recipes[i], grid)) return recipes[i].output;
    return crf_mk((i32)0xffffffff, 0, 0);
}

#define CRF_NRECIPES 99
#define CRF_NTESTS 54
MC_HD static inline int crf_build(CRRecipe *R) {
    int n = 0;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(270,1,0);
    { CRStack a[9]={
        crf_blk(5),
        crf_blk(5),
        crf_blk(5),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_empty(),
        crf_it(280),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(269,1,0);
    { CRStack a[3]={
        crf_blk(5),
        crf_it(280),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(271,1,0);
    { CRStack a[6]={
        crf_blk(5),
        crf_blk(5),
        crf_blk(5),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(290,1,0);
    { CRStack a[6]={
        crf_blk(5),
        crf_blk(5),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(274,1,0);
    { CRStack a[9]={
        crf_blk(4),
        crf_blk(4),
        crf_blk(4),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_empty(),
        crf_it(280),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(273,1,0);
    { CRStack a[3]={
        crf_blk(4),
        crf_it(280),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(275,1,0);
    { CRStack a[6]={
        crf_blk(4),
        crf_blk(4),
        crf_blk(4),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(291,1,0);
    { CRStack a[6]={
        crf_blk(4),
        crf_blk(4),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(257,1,0);
    { CRStack a[9]={
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_empty(),
        crf_it(280),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(256,1,0);
    { CRStack a[3]={
        crf_it(265),
        crf_it(280),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(258,1,0);
    { CRStack a[6]={
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(292,1,0);
    { CRStack a[6]={
        crf_it(265),
        crf_it(265),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(278,1,0);
    { CRStack a[9]={
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_empty(),
        crf_it(280),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(277,1,0);
    { CRStack a[3]={
        crf_it(264),
        crf_it(280),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(279,1,0);
    { CRStack a[6]={
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(293,1,0);
    { CRStack a[6]={
        crf_it(264),
        crf_it(264),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(285,1,0);
    { CRStack a[9]={
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_empty(),
        crf_it(280),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(284,1,0);
    { CRStack a[3]={
        crf_it(266),
        crf_it(280),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(286,1,0);
    { CRStack a[6]={
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(294,1,0);
    { CRStack a[6]={
        crf_it(266),
        crf_it(266),
        crf_empty(),
        crf_it(280),
        crf_empty(),
        crf_it(280)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(359,1,0);
    { CRStack a[4]={
        crf_empty(),
        crf_it(265),
        crf_it(265),
        crf_empty()
    }; for(int q=0;q<4;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(268,1,0);
    { CRStack a[3]={
        crf_blk(5),
        crf_blk(5),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(272,1,0);
    { CRStack a[3]={
        crf_blk(4),
        crf_blk(4),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(267,1,0);
    { CRStack a[3]={
        crf_it(265),
        crf_it(265),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(276,1,0);
    { CRStack a[3]={
        crf_it(264),
        crf_it(264),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(283,1,0);
    { CRStack a[3]={
        crf_it(266),
        crf_it(266),
        crf_it(280)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(261,1,0);
    { CRStack a[9]={
        crf_empty(),
        crf_it(280),
        crf_it(287),
        crf_it(280),
        crf_empty(),
        crf_it(287),
        crf_empty(),
        crf_it(280),
        crf_it(287)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(262,4,0);
    { CRStack a[3]={
        crf_it(318),
        crf_it(280),
        crf_it(288)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(439,2,0);
    { CRStack a[9]={
        crf_empty(),
        crf_it(348),
        crf_empty(),
        crf_it(348),
        crf_it(262),
        crf_it(348),
        crf_empty(),
        crf_it(348),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(41,1,0);
    { CRStack a[9]={
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(266,9,0);
    { CRStack a[1]={
        crf_blk(41)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(42,1,0);
    { CRStack a[9]={
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(265,9,0);
    { CRStack a[1]={
        crf_blk(42)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(57,1,0);
    { CRStack a[9]={
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(264,9,0);
    { CRStack a[1]={
        crf_blk(57)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(133,1,0);
    { CRStack a[9]={
        crf_it(388),
        crf_it(388),
        crf_it(388),
        crf_it(388),
        crf_it(388),
        crf_it(388),
        crf_it(388),
        crf_it(388),
        crf_it(388)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(388,9,0);
    { CRStack a[1]={
        crf_blk(133)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(22,1,0);
    { CRStack a[9]={
        crf_is(351,4),
        crf_is(351,4),
        crf_is(351,4),
        crf_is(351,4),
        crf_is(351,4),
        crf_is(351,4),
        crf_is(351,4),
        crf_is(351,4),
        crf_is(351,4)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(351,9,4);
    { CRStack a[1]={
        crf_blk(22)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(152,1,0);
    { CRStack a[9]={
        crf_it(331),
        crf_it(331),
        crf_it(331),
        crf_it(331),
        crf_it(331),
        crf_it(331),
        crf_it(331),
        crf_it(331),
        crf_it(331)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(331,9,0);
    { CRStack a[1]={
        crf_blk(152)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(173,1,0);
    { CRStack a[9]={
        crf_it(263),
        crf_it(263),
        crf_it(263),
        crf_it(263),
        crf_it(263),
        crf_it(263),
        crf_it(263),
        crf_it(263),
        crf_it(263)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(263,9,0);
    { CRStack a[1]={
        crf_blk(173)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(170,1,0);
    { CRStack a[9]={
        crf_it(296),
        crf_it(296),
        crf_it(296),
        crf_it(296),
        crf_it(296),
        crf_it(296),
        crf_it(296),
        crf_it(296),
        crf_it(296)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(296,9,0);
    { CRStack a[1]={
        crf_blk(170)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(165,1,0);
    { CRStack a[9]={
        crf_it(341),
        crf_it(341),
        crf_it(341),
        crf_it(341),
        crf_it(341),
        crf_it(341),
        crf_it(341),
        crf_it(341),
        crf_it(341)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(341,9,0);
    { CRStack a[1]={
        crf_blk(165)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(266,1,0);
    { CRStack a[9]={
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(371,9,0);
    { CRStack a[1]={
        crf_it(266)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(265,1,0);
    { CRStack a[9]={
        crf_it(452),
        crf_it(452),
        crf_it(452),
        crf_it(452),
        crf_it(452),
        crf_it(452),
        crf_it(452),
        crf_it(452),
        crf_it(452)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(452,9,0);
    { CRStack a[1]={
        crf_it(265)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(282,1,0);
    R[n].ing[0]=crf_blk(39);
    R[n].ing[1]=crf_blk(40);
    R[n].ing[2]=crf_it(281);
    ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(357,8,0);
    { CRStack a[3]={
        crf_it(296),
        crf_is(351,3),
        crf_it(296)
    }; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(413,1,0);
    { CRStack a[9]={
        crf_empty(),
        crf_it(412),
        crf_empty(),
        crf_it(391),
        crf_it(393),
        crf_blk(39),
        crf_empty(),
        crf_it(281),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(413,1,0);
    { CRStack a[9]={
        crf_empty(),
        crf_it(412),
        crf_empty(),
        crf_it(391),
        crf_it(393),
        crf_blk(40),
        crf_empty(),
        crf_it(281),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(103,1,0);
    { CRStack a[9]={
        crf_it(360),
        crf_it(360),
        crf_it(360),
        crf_it(360),
        crf_it(360),
        crf_it(360),
        crf_it(360),
        crf_it(360),
        crf_it(360)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(436,1,0);
    { CRStack a[9]={
        crf_it(434),
        crf_it(434),
        crf_it(434),
        crf_it(434),
        crf_it(434),
        crf_it(434),
        crf_empty(),
        crf_it(281),
        crf_empty()
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(362,1,0);
    { CRStack a[1]={
        crf_it(360)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(361,4,0);
    { CRStack a[1]={
        crf_blk(86)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(400,1,0);
    R[n].ing[0]=crf_blk(86);
    R[n].ing[1]=crf_it(353);
    R[n].ing[2]=crf_it(344);
    ++n;
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(376,1,0);
    R[n].ing[0]=crf_it(375);
    R[n].ing[1]=crf_blk(39);
    R[n].ing[2]=crf_it(353);
    ++n;
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(377,2,0);
    R[n].ing[0]=crf_it(369);
    ++n;
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(378,1,0);
    R[n].ing[0]=crf_it(377);
    R[n].ing[1]=crf_it(341);
    ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(54,1,0);
    { CRStack a[9]={
        crf_blk(5),
        crf_blk(5),
        crf_blk(5),
        crf_blk(5),
        crf_empty(),
        crf_blk(5),
        crf_blk(5),
        crf_blk(5),
        crf_blk(5)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(61,1,0);
    { CRStack a[9]={
        crf_blk(4),
        crf_blk(4),
        crf_blk(4),
        crf_blk(4),
        crf_empty(),
        crf_blk(4),
        crf_blk(4),
        crf_blk(4),
        crf_blk(4)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(58,1,0);
    { CRStack a[4]={
        crf_blk(5),
        crf_blk(5),
        crf_blk(5),
        crf_blk(5)
    }; for(int q=0;q<4;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(298,1,0);
    { CRStack a[6]={
        crf_it(334),
        crf_it(334),
        crf_it(334),
        crf_it(334),
        crf_empty(),
        crf_it(334)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(299,1,0);
    { CRStack a[9]={
        crf_it(334),
        crf_empty(),
        crf_it(334),
        crf_it(334),
        crf_it(334),
        crf_it(334),
        crf_it(334),
        crf_it(334),
        crf_it(334)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(300,1,0);
    { CRStack a[9]={
        crf_it(334),
        crf_it(334),
        crf_it(334),
        crf_it(334),
        crf_empty(),
        crf_it(334),
        crf_it(334),
        crf_empty(),
        crf_it(334)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(301,1,0);
    { CRStack a[6]={
        crf_it(334),
        crf_empty(),
        crf_it(334),
        crf_it(334),
        crf_empty(),
        crf_it(334)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(306,1,0);
    { CRStack a[6]={
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_empty(),
        crf_it(265)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(307,1,0);
    { CRStack a[9]={
        crf_it(265),
        crf_empty(),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(308,1,0);
    { CRStack a[9]={
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_it(265),
        crf_empty(),
        crf_it(265),
        crf_it(265),
        crf_empty(),
        crf_it(265)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(309,1,0);
    { CRStack a[6]={
        crf_it(265),
        crf_empty(),
        crf_it(265),
        crf_it(265),
        crf_empty(),
        crf_it(265)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(310,1,0);
    { CRStack a[6]={
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_empty(),
        crf_it(264)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(311,1,0);
    { CRStack a[9]={
        crf_it(264),
        crf_empty(),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(312,1,0);
    { CRStack a[9]={
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_it(264),
        crf_empty(),
        crf_it(264),
        crf_it(264),
        crf_empty(),
        crf_it(264)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(313,1,0);
    { CRStack a[6]={
        crf_it(264),
        crf_empty(),
        crf_it(264),
        crf_it(264),
        crf_empty(),
        crf_it(264)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(314,1,0);
    { CRStack a[6]={
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_empty(),
        crf_it(266)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(315,1,0);
    { CRStack a[9]={
        crf_it(266),
        crf_empty(),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(316,1,0);
    { CRStack a[9]={
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_it(266),
        crf_empty(),
        crf_it(266),
        crf_it(266),
        crf_empty(),
        crf_it(266)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(317,1,0);
    { CRStack a[6]={
        crf_it(266),
        crf_empty(),
        crf_it(266),
        crf_it(266),
        crf_empty(),
        crf_it(266)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,0);
    { CRStack a[1]={
        crf_is(17,0)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,1);
    { CRStack a[1]={
        crf_is(17,1)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,2);
    { CRStack a[1]={
        crf_is(17,2)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,3);
    { CRStack a[1]={
        crf_is(17,3)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,4);
    { CRStack a[1]={
        crf_is(162,0)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,5);
    { CRStack a[1]={
        crf_is(162,1)
    }; for(int q=0;q<1;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(280,4,0);
    { CRStack a[2]={
        crf_blk(5),
        crf_blk(5)
    }; for(int q=0;q<2;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(50,4,0);
    { CRStack a[2]={
        crf_it(263),
        crf_it(280)
    }; for(int q=0;q<2;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(50,4,0);
    { CRStack a[2]={
        crf_is(263,1),
        crf_it(280)
    }; for(int q=0;q<2;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(259,1,0);
    R[n].ing[0]=crf_it(265);
    R[n].ing[1]=crf_it(318);
    ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(325,1,0);
    { CRStack a[6]={
        crf_it(265),
        crf_empty(),
        crf_it(265),
        crf_empty(),
        crf_it(265),
        crf_empty()
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(355,1,0);
    { CRStack a[6]={
        crf_blk(35),
        crf_blk(35),
        crf_blk(35),
        crf_blk(5),
        crf_blk(5),
        crf_blk(5)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(381,1,0);
    R[n].ing[0]=crf_it(368);
    R[n].ing[1]=crf_it(377);
    ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(374,3,0);
    { CRStack a[6]={
        crf_blk(20),
        crf_empty(),
        crf_blk(20),
        crf_empty(),
        crf_blk(20),
        crf_empty()
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(379,1,0);
    { CRStack a[6]={
        crf_empty(),
        crf_it(369),
        crf_empty(),
        crf_blk(4),
        crf_blk(4),
        crf_blk(4)
    }; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(396,1,0);
    { CRStack a[9]={
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(391),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(382,1,0);
    { CRStack a[9]={
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(360),
        crf_it(371),
        crf_it(371),
        crf_it(371),
        crf_it(371)
    }; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    return n;
}
MC_HD static inline void crf_battery(CRStack out[CRF_NTESTS][9]) {
    CRStack b[CRF_NTESTS][9] = {
        /* wooden_pickaxe */ { crf_mk(5,1,0), crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty() },
        /* pickaxe_nonmatch */ { crf_mk(5,1,0), crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* stone_pickaxe */ { crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty() },
        /* wooden_axe */ { crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(5,1,0), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty() },
        /* wooden_axe_mirror */ { crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(280,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty() },
        /* wooden_axe_offset */ { crf_empty(), crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(5,1,0), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0) },
        /* wooden_hoe */ { crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty() },
        /* wooden_sword */ { crf_mk(5,1,0), crf_empty(), crf_empty(), crf_mk(5,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty() },
        /* stone_sword_offset */ { crf_empty(), crf_mk(4,1,0), crf_empty(), crf_empty(), crf_mk(4,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty() },
        /* chest */ { crf_mk(5,1,0), crf_mk(5,1,0), crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(5,1,0), crf_mk(5,1,0), crf_mk(5,1,0), crf_mk(5,1,0) },
        /* furnace */ { crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0), crf_empty(), crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0) },
        /* furnace_nonmatch */ { crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0), crf_empty(), crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(5,1,0) },
        /* crafting_table */ { crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* crafting_table_offset */ { crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_mk(5,1,0), crf_mk(5,1,0) },
        /* planks_oak */ { crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_mk(17,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* planks_spruce */ { crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_mk(17,1,1), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* planks_acacia */ { crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_mk(162,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* sticks */ { crf_mk(5,1,0), crf_empty(), crf_empty(), crf_mk(5,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* torch_coal */ { crf_mk(263,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* torch_charcoal */ { crf_mk(263,1,1), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* flint_steel */ { crf_mk(265,1,0), crf_mk(318,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* flint_steel_scrambled */ { crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_mk(318,1,0), crf_empty(), crf_empty(), crf_empty(), crf_mk(265,1,0) },
        /* flint_steel_extra */ { crf_mk(265,1,0), crf_mk(318,1,0), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* empty */ { crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* single_plank */ { crf_mk(5,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* sword_nonmatch */ { crf_empty(), crf_mk(4,1,0), crf_empty(), crf_empty(), crf_mk(4,1,0), crf_empty(), crf_empty(), crf_mk(5,1,0), crf_empty() },
        /* torch_nonmatch */ { crf_mk(263,1,0), crf_empty(), crf_empty(), crf_mk(5,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* wooden_shovel */ { crf_mk(5,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty() },
        /* stone_shovel_offset */ { crf_empty(), crf_empty(), crf_mk(4,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0) },
        /* iron_pickaxe */ { crf_mk(265,1,0), crf_mk(265,1,0), crf_mk(265,1,0), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty() },
        /* diamond_sword */ { crf_mk(264,1,0), crf_empty(), crf_empty(), crf_mk(264,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty() },
        /* golden_hoe */ { crf_mk(266,1,0), crf_mk(266,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty() },
        /* shears */ { crf_empty(), crf_mk(265,1,0), crf_empty(), crf_mk(265,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* bow */ { crf_empty(), crf_mk(280,1,0), crf_mk(287,1,0), crf_empty(), crf_mk(280,1,0), crf_mk(287,1,0), crf_empty(), crf_mk(280,1,0), crf_mk(287,1,0) },
        /* arrow */ { crf_mk(318,1,0), crf_empty(), crf_empty(), crf_mk(280,1,0), crf_empty(), crf_empty(), crf_mk(288,1,0), crf_empty(), crf_empty() },
        /* spectral_arrow */ { crf_empty(), crf_mk(348,1,0), crf_empty(), crf_mk(348,1,0), crf_mk(262,1,0), crf_mk(348,1,0), crf_empty(), crf_mk(348,1,0), crf_empty() },
        /* gold_block */ { crf_mk(266,1,0), crf_mk(266,1,0), crf_mk(266,1,0), crf_mk(266,1,0), crf_mk(266,1,0), crf_mk(266,1,0), crf_mk(266,1,0), crf_mk(266,1,0), crf_mk(266,1,0) },
        /* iron_ingot_from_block */ { crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_mk(42,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* mushroom_stew */ { crf_mk(39,1,0), crf_empty(), crf_empty(), crf_empty(), crf_mk(40,1,0), crf_empty(), crf_empty(), crf_mk(281,1,0), crf_empty() },
        /* cookie */ { crf_mk(296,1,0), crf_mk(351,1,3), crf_mk(296,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* iron_helmet */ { crf_mk(265,1,0), crf_mk(265,1,0), crf_mk(265,1,0), crf_mk(265,1,0), crf_empty(), crf_mk(265,1,0), crf_empty(), crf_empty(), crf_empty() },
        /* diamond_chestplate */ { crf_mk(264,1,0), crf_empty(), crf_mk(264,1,0), crf_mk(264,1,0), crf_mk(264,1,0), crf_mk(264,1,0), crf_mk(264,1,0), crf_mk(264,1,0), crf_mk(264,1,0) },
        /* leather_boots */ { crf_mk(334,1,0), crf_empty(), crf_mk(334,1,0), crf_mk(334,1,0), crf_empty(), crf_mk(334,1,0), crf_empty(), crf_empty(), crf_empty() },
        /* blaze_powder */ { crf_mk(369,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* pumpkin_pie */ { crf_mk(86,1,0), crf_empty(), crf_empty(), crf_empty(), crf_mk(353,1,0), crf_empty(), crf_empty(), crf_mk(344,1,0), crf_empty() },
        /* iron_nonmatch */ { crf_mk(265,1,0), crf_mk(265,1,0), crf_mk(265,1,0), crf_mk(265,1,0), crf_mk(265,1,0), crf_mk(265,1,0), crf_empty(), crf_mk(280,1,0), crf_empty() },
        /* bucket */ { crf_mk(265,1,0), crf_empty(), crf_mk(265,1,0), crf_empty(), crf_mk(265,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* bed_wool_meta14 */ { crf_mk(35,1,14), crf_mk(35,1,14), crf_mk(35,1,14), crf_mk(5,1,0), crf_mk(5,1,0), crf_mk(5,1,0), crf_empty(), crf_empty(), crf_empty() },
        /* ender_eye_scrambled */ { crf_empty(), crf_empty(), crf_mk(368,1,0), crf_empty(), crf_empty(), crf_empty(), crf_mk(377,1,0), crf_empty(), crf_empty() },
        /* glass_bottles */ { crf_mk(20,1,0), crf_empty(), crf_mk(20,1,0), crf_empty(), crf_mk(20,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* glass_bottles_nonmatch */ { crf_mk(20,1,0), crf_empty(), crf_mk(20,1,0), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty(), crf_empty() },
        /* brewing_stand */ { crf_empty(), crf_mk(369,1,0), crf_empty(), crf_mk(4,1,0), crf_mk(4,1,0), crf_mk(4,1,0), crf_empty(), crf_empty(), crf_empty() },
        /* golden_carrot */ { crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(391,1,0), crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(371,1,0) },
        /* speckled_melon */ { crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(360,1,0), crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(371,1,0), crf_mk(371,1,0) },
    };
    for (int t = 0; t < CRF_NTESTS; ++t) for (int q = 0; q < 9; ++q) out[t][q] = b[t][q];
}
#endif
