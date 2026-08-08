/* Generated from the initialized Minecraft 1.11.2 CraftingManager.
 * Source: verify/completeness/surface_registry_manifest.json.
 * The static table contains every ordinary shaped/shapeless vanilla and
 * Forge ore recipe in exact registry order. Stateful special recipes remain
 * in the runtime dispatcher because their result/remainder depends on NBT. */
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
MC_HD static inline int crf_stack_matches(CRStack expected, CRStack actual) {
    return expected.item == actual.item
        && (expected.meta == CRF_WILDCARD || expected.meta == actual.meta);
}
MC_HD static inline int crf_checkMatch(const CRRecipe *r, const CRStack *grid, int offX, int offY, int mirror) {
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        int k = i - offX, l = j - offY; CRStack ing = crf_empty();
        if (k >= 0 && l >= 0 && k < r->width && l < r->height)
            ing = mirror ? r->ing[r->width - k - 1 + l * r->width] : r->ing[k + l * r->width];
        CRStack g = grid[i + j * CRF_GRID];
        if (!crf_isEmpty(g) || !crf_isEmpty(ing)) {
            if (crf_isEmpty(g) != crf_isEmpty(ing)) return 0;
            if (!crf_stack_matches(ing, g)) return 0;
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
            int found = 0;
            for (int z = 0; z < r->nIng; ++z) if (!used[z]
                    && crf_stack_matches(r->ing[z], g)) {
                found = 1; used[z] = 1; --remaining; break;
            }
            if (!found) return 0;
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

#define CRF_NRECIPES 389
#define CRF_NTESTS 54
MC_HD static inline int crf_build(CRRecipe *R) {
    int n = 0;
    /* registry 0: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(22,1,0);
    R[n].ing[0]=crf_mk(351,1,4);
    R[n].ing[1]=crf_mk(351,1,4);
    R[n].ing[2]=crf_mk(351,1,4);
    R[n].ing[3]=crf_mk(351,1,4);
    R[n].ing[4]=crf_mk(351,1,4);
    R[n].ing[5]=crf_mk(351,1,4);
    R[n].ing[6]=crf_mk(351,1,4);
    R[n].ing[7]=crf_mk(351,1,4);
    R[n].ing[8]=crf_mk(351,1,4);
    ++n;
    /* registry 1: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(173,1,0);
    R[n].ing[0]=crf_mk(263,1,0);
    R[n].ing[1]=crf_mk(263,1,0);
    R[n].ing[2]=crf_mk(263,1,0);
    R[n].ing[3]=crf_mk(263,1,0);
    R[n].ing[4]=crf_mk(263,1,0);
    R[n].ing[5]=crf_mk(263,1,0);
    R[n].ing[6]=crf_mk(263,1,0);
    R[n].ing[7]=crf_mk(263,1,0);
    R[n].ing[8]=crf_mk(263,1,0);
    ++n;
    /* registry 2: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(170,1,0);
    R[n].ing[0]=crf_mk(296,1,0);
    R[n].ing[1]=crf_mk(296,1,0);
    R[n].ing[2]=crf_mk(296,1,0);
    R[n].ing[3]=crf_mk(296,1,0);
    R[n].ing[4]=crf_mk(296,1,0);
    R[n].ing[5]=crf_mk(296,1,0);
    R[n].ing[6]=crf_mk(296,1,0);
    R[n].ing[7]=crf_mk(296,1,0);
    R[n].ing[8]=crf_mk(296,1,0);
    ++n;
    /* registry 3: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(266,1,0);
    R[n].ing[0]=crf_mk(371,1,0);
    R[n].ing[1]=crf_mk(371,1,0);
    R[n].ing[2]=crf_mk(371,1,0);
    R[n].ing[3]=crf_mk(371,1,0);
    R[n].ing[4]=crf_mk(371,1,0);
    R[n].ing[5]=crf_mk(371,1,0);
    R[n].ing[6]=crf_mk(371,1,0);
    R[n].ing[7]=crf_mk(371,1,0);
    R[n].ing[8]=crf_mk(371,1,0);
    ++n;
    /* registry 4: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(265,1,0);
    R[n].ing[0]=crf_mk(452,1,0);
    R[n].ing[1]=crf_mk(452,1,0);
    R[n].ing[2]=crf_mk(452,1,0);
    R[n].ing[3]=crf_mk(452,1,0);
    R[n].ing[4]=crf_mk(452,1,0);
    R[n].ing[5]=crf_mk(452,1,0);
    R[n].ing[6]=crf_mk(452,1,0);
    R[n].ing[7]=crf_mk(452,1,0);
    R[n].ing[8]=crf_mk(452,1,0);
    ++n;
    /* registry 5: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(413,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(412,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(391,1,0);
    R[n].ing[4]=crf_mk(393,1,0);
    R[n].ing[5]=crf_mk(39,1,32767);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(281,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 6: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(413,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(412,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(391,1,0);
    R[n].ing[4]=crf_mk(393,1,0);
    R[n].ing[5]=crf_mk(40,1,32767);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(281,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 7: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(103,1,0);
    R[n].ing[0]=crf_mk(360,1,0);
    R[n].ing[1]=crf_mk(360,1,0);
    R[n].ing[2]=crf_mk(360,1,0);
    R[n].ing[3]=crf_mk(360,1,0);
    R[n].ing[4]=crf_mk(360,1,0);
    R[n].ing[5]=crf_mk(360,1,0);
    R[n].ing[6]=crf_mk(360,1,0);
    R[n].ing[7]=crf_mk(360,1,0);
    R[n].ing[8]=crf_mk(360,1,0);
    ++n;
    /* registry 8: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(436,1,0);
    R[n].ing[0]=crf_mk(434,1,0);
    R[n].ing[1]=crf_mk(434,1,0);
    R[n].ing[2]=crf_mk(434,1,0);
    R[n].ing[3]=crf_mk(434,1,0);
    R[n].ing[4]=crf_mk(434,1,0);
    R[n].ing[5]=crf_mk(434,1,0);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(281,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 9: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(130,1,0);
    R[n].ing[0]=crf_mk(49,1,32767);
    R[n].ing[1]=crf_mk(49,1,32767);
    R[n].ing[2]=crf_mk(49,1,32767);
    R[n].ing[3]=crf_mk(49,1,32767);
    R[n].ing[4]=crf_mk(381,1,0);
    R[n].ing[5]=crf_mk(49,1,32767);
    R[n].ing[6]=crf_mk(49,1,32767);
    R[n].ing[7]=crf_mk(49,1,32767);
    R[n].ing[8]=crf_mk(49,1,32767);
    ++n;
    /* registry 10: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(203,4,0);
    R[n].ing[0]=crf_mk(201,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(201,1,32767);
    R[n].ing[4]=crf_mk(201,1,32767);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(201,1,32767);
    R[n].ing[7]=crf_mk(201,1,32767);
    R[n].ing[8]=crf_mk(201,1,32767);
    ++n;
    /* registry 11: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(214,1,0);
    R[n].ing[0]=crf_mk(372,1,0);
    R[n].ing[1]=crf_mk(372,1,0);
    R[n].ing[2]=crf_mk(372,1,0);
    R[n].ing[3]=crf_mk(372,1,0);
    R[n].ing[4]=crf_mk(372,1,0);
    R[n].ing[5]=crf_mk(372,1,0);
    R[n].ing[6]=crf_mk(372,1,0);
    R[n].ing[7]=crf_mk(372,1,0);
    R[n].ing[8]=crf_mk(372,1,0);
    ++n;
    /* registry 12: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(53,4,0);
    R[n].ing[0]=crf_mk(5,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(5,1,0);
    R[n].ing[4]=crf_mk(5,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(5,1,0);
    R[n].ing[7]=crf_mk(5,1,0);
    R[n].ing[8]=crf_mk(5,1,0);
    ++n;
    /* registry 13: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(135,4,0);
    R[n].ing[0]=crf_mk(5,1,2);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(5,1,2);
    R[n].ing[4]=crf_mk(5,1,2);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(5,1,2);
    R[n].ing[7]=crf_mk(5,1,2);
    R[n].ing[8]=crf_mk(5,1,2);
    ++n;
    /* registry 14: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(134,4,0);
    R[n].ing[0]=crf_mk(5,1,1);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(5,1,1);
    R[n].ing[4]=crf_mk(5,1,1);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(5,1,1);
    R[n].ing[7]=crf_mk(5,1,1);
    R[n].ing[8]=crf_mk(5,1,1);
    ++n;
    /* registry 15: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(136,4,0);
    R[n].ing[0]=crf_mk(5,1,3);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(5,1,3);
    R[n].ing[4]=crf_mk(5,1,3);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(5,1,3);
    R[n].ing[7]=crf_mk(5,1,3);
    R[n].ing[8]=crf_mk(5,1,3);
    ++n;
    /* registry 16: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(163,4,0);
    R[n].ing[0]=crf_mk(5,1,4);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(5,1,4);
    R[n].ing[4]=crf_mk(5,1,4);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(5,1,4);
    R[n].ing[7]=crf_mk(5,1,4);
    R[n].ing[8]=crf_mk(5,1,4);
    ++n;
    /* registry 17: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(164,4,0);
    R[n].ing[0]=crf_mk(5,1,5);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(5,1,5);
    R[n].ing[4]=crf_mk(5,1,5);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(5,1,5);
    R[n].ing[7]=crf_mk(5,1,5);
    R[n].ing[8]=crf_mk(5,1,5);
    ++n;
    /* registry 18: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(67,4,0);
    R[n].ing[0]=crf_mk(4,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(4,1,32767);
    R[n].ing[4]=crf_mk(4,1,32767);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(4,1,32767);
    R[n].ing[7]=crf_mk(4,1,32767);
    R[n].ing[8]=crf_mk(4,1,32767);
    ++n;
    /* registry 19: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(108,4,0);
    R[n].ing[0]=crf_mk(45,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(45,1,32767);
    R[n].ing[4]=crf_mk(45,1,32767);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(45,1,32767);
    R[n].ing[7]=crf_mk(45,1,32767);
    R[n].ing[8]=crf_mk(45,1,32767);
    ++n;
    /* registry 20: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(109,4,0);
    R[n].ing[0]=crf_mk(98,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(98,1,32767);
    R[n].ing[4]=crf_mk(98,1,32767);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(98,1,32767);
    R[n].ing[7]=crf_mk(98,1,32767);
    R[n].ing[8]=crf_mk(98,1,32767);
    ++n;
    /* registry 21: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(114,4,0);
    R[n].ing[0]=crf_mk(112,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(112,1,32767);
    R[n].ing[4]=crf_mk(112,1,32767);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(112,1,32767);
    R[n].ing[7]=crf_mk(112,1,32767);
    R[n].ing[8]=crf_mk(112,1,32767);
    ++n;
    /* registry 22: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(128,4,0);
    R[n].ing[0]=crf_mk(24,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(24,1,32767);
    R[n].ing[4]=crf_mk(24,1,32767);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(24,1,32767);
    R[n].ing[7]=crf_mk(24,1,32767);
    R[n].ing[8]=crf_mk(24,1,32767);
    ++n;
    /* registry 23: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(180,4,0);
    R[n].ing[0]=crf_mk(179,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(179,1,32767);
    R[n].ing[4]=crf_mk(179,1,32767);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(179,1,32767);
    R[n].ing[7]=crf_mk(179,1,32767);
    R[n].ing[8]=crf_mk(179,1,32767);
    ++n;
    /* registry 24: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(156,4,0);
    R[n].ing[0]=crf_mk(155,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(155,1,32767);
    R[n].ing[4]=crf_mk(155,1,32767);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(155,1,32767);
    R[n].ing[7]=crf_mk(155,1,32767);
    R[n].ing[8]=crf_mk(155,1,32767);
    ++n;
    /* registry 25: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(396,1,0);
    R[n].ing[0]=crf_mk(371,1,0);
    R[n].ing[1]=crf_mk(371,1,0);
    R[n].ing[2]=crf_mk(371,1,0);
    R[n].ing[3]=crf_mk(371,1,0);
    R[n].ing[4]=crf_mk(391,1,0);
    R[n].ing[5]=crf_mk(371,1,0);
    R[n].ing[6]=crf_mk(371,1,0);
    R[n].ing[7]=crf_mk(371,1,0);
    R[n].ing[8]=crf_mk(371,1,0);
    ++n;
    /* registry 26: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(382,1,0);
    R[n].ing[0]=crf_mk(371,1,0);
    R[n].ing[1]=crf_mk(371,1,0);
    R[n].ing[2]=crf_mk(371,1,0);
    R[n].ing[3]=crf_mk(371,1,0);
    R[n].ing[4]=crf_mk(360,1,0);
    R[n].ing[5]=crf_mk(371,1,0);
    R[n].ing[6]=crf_mk(371,1,0);
    R[n].ing[7]=crf_mk(371,1,0);
    R[n].ing[8]=crf_mk(371,1,0);
    ++n;
    /* registry 27: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(426,1,0);
    R[n].ing[0]=crf_mk(20,1,32767);
    R[n].ing[1]=crf_mk(20,1,32767);
    R[n].ing[2]=crf_mk(20,1,32767);
    R[n].ing[3]=crf_mk(20,1,32767);
    R[n].ing[4]=crf_mk(381,1,0);
    R[n].ing[5]=crf_mk(20,1,32767);
    R[n].ing[6]=crf_mk(20,1,32767);
    R[n].ing[7]=crf_mk(370,1,0);
    R[n].ing[8]=crf_mk(20,1,32767);
    ++n;
    /* registry 28: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(216,1,0);
    R[n].ing[0]=crf_mk(351,1,15);
    R[n].ing[1]=crf_mk(351,1,15);
    R[n].ing[2]=crf_mk(351,1,15);
    R[n].ing[3]=crf_mk(351,1,15);
    R[n].ing[4]=crf_mk(351,1,15);
    R[n].ing[5]=crf_mk(351,1,15);
    R[n].ing[6]=crf_mk(351,1,15);
    R[n].ing[7]=crf_mk(351,1,15);
    R[n].ing[8]=crf_mk(351,1,15);
    ++n;
    /* registry 29: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(270,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 30: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(274,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_mk(4,1,0);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 31: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(257,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 32: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(278,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_mk(264,1,0);
    R[n].ing[2]=crf_mk(264,1,0);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 33: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(285,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 34: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(261,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(287,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(287,1,0);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_mk(287,1,0);
    ++n;
    /* registry 35: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(439,2,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(348,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(348,1,0);
    R[n].ing[4]=crf_mk(262,1,0);
    R[n].ing[5]=crf_mk(348,1,0);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(348,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 36: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(41,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_mk(266,1,0);
    R[n].ing[4]=crf_mk(266,1,0);
    R[n].ing[5]=crf_mk(266,1,0);
    R[n].ing[6]=crf_mk(266,1,0);
    R[n].ing[7]=crf_mk(266,1,0);
    R[n].ing[8]=crf_mk(266,1,0);
    ++n;
    /* registry 37: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(42,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(265,1,0);
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_mk(265,1,0);
    R[n].ing[7]=crf_mk(265,1,0);
    R[n].ing[8]=crf_mk(265,1,0);
    ++n;
    /* registry 38: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(57,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_mk(264,1,0);
    R[n].ing[2]=crf_mk(264,1,0);
    R[n].ing[3]=crf_mk(264,1,0);
    R[n].ing[4]=crf_mk(264,1,0);
    R[n].ing[5]=crf_mk(264,1,0);
    R[n].ing[6]=crf_mk(264,1,0);
    R[n].ing[7]=crf_mk(264,1,0);
    R[n].ing[8]=crf_mk(264,1,0);
    ++n;
    /* registry 39: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(133,1,0);
    R[n].ing[0]=crf_mk(388,1,0);
    R[n].ing[1]=crf_mk(388,1,0);
    R[n].ing[2]=crf_mk(388,1,0);
    R[n].ing[3]=crf_mk(388,1,0);
    R[n].ing[4]=crf_mk(388,1,0);
    R[n].ing[5]=crf_mk(388,1,0);
    R[n].ing[6]=crf_mk(388,1,0);
    R[n].ing[7]=crf_mk(388,1,0);
    R[n].ing[8]=crf_mk(388,1,0);
    ++n;
    /* registry 40: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(152,1,0);
    R[n].ing[0]=crf_mk(331,1,0);
    R[n].ing[1]=crf_mk(331,1,0);
    R[n].ing[2]=crf_mk(331,1,0);
    R[n].ing[3]=crf_mk(331,1,0);
    R[n].ing[4]=crf_mk(331,1,0);
    R[n].ing[5]=crf_mk(331,1,0);
    R[n].ing[6]=crf_mk(331,1,0);
    R[n].ing[7]=crf_mk(331,1,0);
    R[n].ing[8]=crf_mk(331,1,0);
    ++n;
    /* registry 41: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(165,1,0);
    R[n].ing[0]=crf_mk(341,1,0);
    R[n].ing[1]=crf_mk(341,1,0);
    R[n].ing[2]=crf_mk(341,1,0);
    R[n].ing[3]=crf_mk(341,1,0);
    R[n].ing[4]=crf_mk(341,1,0);
    R[n].ing[5]=crf_mk(341,1,0);
    R[n].ing[6]=crf_mk(341,1,0);
    R[n].ing[7]=crf_mk(341,1,0);
    R[n].ing[8]=crf_mk(341,1,0);
    ++n;
    /* registry 42: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(54,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(5,1,32767);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(5,1,32767);
    R[n].ing[6]=crf_mk(5,1,32767);
    R[n].ing[7]=crf_mk(5,1,32767);
    R[n].ing[8]=crf_mk(5,1,32767);
    ++n;
    /* registry 43: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(61,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_mk(4,1,0);
    R[n].ing[3]=crf_mk(4,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(4,1,0);
    R[n].ing[6]=crf_mk(4,1,0);
    R[n].ing[7]=crf_mk(4,1,0);
    R[n].ing[8]=crf_mk(4,1,0);
    ++n;
    /* registry 44: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(123,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(331,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(331,1,0);
    R[n].ing[4]=crf_mk(89,1,32767);
    R[n].ing[5]=crf_mk(331,1,0);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(331,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 45: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(138,1,0);
    R[n].ing[0]=crf_mk(20,1,32767);
    R[n].ing[1]=crf_mk(20,1,32767);
    R[n].ing[2]=crf_mk(20,1,32767);
    R[n].ing[3]=crf_mk(20,1,32767);
    R[n].ing[4]=crf_mk(399,1,0);
    R[n].ing[5]=crf_mk(20,1,32767);
    R[n].ing[6]=crf_mk(49,1,32767);
    R[n].ing[7]=crf_mk(49,1,32767);
    R[n].ing[8]=crf_mk(49,1,32767);
    ++n;
    /* registry 46: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(168,1,1);
    R[n].ing[0]=crf_mk(409,1,0);
    R[n].ing[1]=crf_mk(409,1,0);
    R[n].ing[2]=crf_mk(409,1,0);
    R[n].ing[3]=crf_mk(409,1,0);
    R[n].ing[4]=crf_mk(409,1,0);
    R[n].ing[5]=crf_mk(409,1,0);
    R[n].ing[6]=crf_mk(409,1,0);
    R[n].ing[7]=crf_mk(409,1,0);
    R[n].ing[8]=crf_mk(409,1,0);
    ++n;
    /* registry 47: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(168,1,2);
    R[n].ing[0]=crf_mk(409,1,0);
    R[n].ing[1]=crf_mk(409,1,0);
    R[n].ing[2]=crf_mk(409,1,0);
    R[n].ing[3]=crf_mk(409,1,0);
    R[n].ing[4]=crf_mk(351,1,0);
    R[n].ing[5]=crf_mk(409,1,0);
    R[n].ing[6]=crf_mk(409,1,0);
    R[n].ing[7]=crf_mk(409,1,0);
    R[n].ing[8]=crf_mk(409,1,0);
    ++n;
    /* registry 48: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(169,1,0);
    R[n].ing[0]=crf_mk(409,1,0);
    R[n].ing[1]=crf_mk(410,1,0);
    R[n].ing[2]=crf_mk(409,1,0);
    R[n].ing[3]=crf_mk(410,1,0);
    R[n].ing[4]=crf_mk(410,1,0);
    R[n].ing[5]=crf_mk(410,1,0);
    R[n].ing[6]=crf_mk(409,1,0);
    R[n].ing[7]=crf_mk(410,1,0);
    R[n].ing[8]=crf_mk(409,1,0);
    ++n;
    /* registry 49: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(299,1,0);
    R[n].ing[0]=crf_mk(334,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(334,1,0);
    R[n].ing[3]=crf_mk(334,1,0);
    R[n].ing[4]=crf_mk(334,1,0);
    R[n].ing[5]=crf_mk(334,1,0);
    R[n].ing[6]=crf_mk(334,1,0);
    R[n].ing[7]=crf_mk(334,1,0);
    R[n].ing[8]=crf_mk(334,1,0);
    ++n;
    /* registry 50: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(300,1,0);
    R[n].ing[0]=crf_mk(334,1,0);
    R[n].ing[1]=crf_mk(334,1,0);
    R[n].ing[2]=crf_mk(334,1,0);
    R[n].ing[3]=crf_mk(334,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(334,1,0);
    R[n].ing[6]=crf_mk(334,1,0);
    R[n].ing[7]=crf_empty();
    R[n].ing[8]=crf_mk(334,1,0);
    ++n;
    /* registry 51: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(307,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(265,1,0);
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_mk(265,1,0);
    R[n].ing[7]=crf_mk(265,1,0);
    R[n].ing[8]=crf_mk(265,1,0);
    ++n;
    /* registry 52: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(308,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_mk(265,1,0);
    R[n].ing[7]=crf_empty();
    R[n].ing[8]=crf_mk(265,1,0);
    ++n;
    /* registry 53: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(311,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(264,1,0);
    R[n].ing[3]=crf_mk(264,1,0);
    R[n].ing[4]=crf_mk(264,1,0);
    R[n].ing[5]=crf_mk(264,1,0);
    R[n].ing[6]=crf_mk(264,1,0);
    R[n].ing[7]=crf_mk(264,1,0);
    R[n].ing[8]=crf_mk(264,1,0);
    ++n;
    /* registry 54: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(312,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_mk(264,1,0);
    R[n].ing[2]=crf_mk(264,1,0);
    R[n].ing[3]=crf_mk(264,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(264,1,0);
    R[n].ing[6]=crf_mk(264,1,0);
    R[n].ing[7]=crf_empty();
    R[n].ing[8]=crf_mk(264,1,0);
    ++n;
    /* registry 55: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(315,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_mk(266,1,0);
    R[n].ing[4]=crf_mk(266,1,0);
    R[n].ing[5]=crf_mk(266,1,0);
    R[n].ing[6]=crf_mk(266,1,0);
    R[n].ing[7]=crf_mk(266,1,0);
    R[n].ing[8]=crf_mk(266,1,0);
    ++n;
    /* registry 56: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(316,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_mk(266,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(266,1,0);
    R[n].ing[6]=crf_mk(266,1,0);
    R[n].ing[7]=crf_empty();
    R[n].ing[8]=crf_mk(266,1,0);
    ++n;
    /* registry 57: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,15);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,0);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 58: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,15);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,0);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 59: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,14);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,1);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 60: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,14);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,1);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 61: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,13);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,2);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 62: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,13);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,2);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 63: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,12);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,3);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 64: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,12);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,3);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 65: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,11);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,4);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 66: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,11);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,4);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 67: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,10);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,5);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 68: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,10);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,5);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 69: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,9);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,6);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 70: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,9);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,6);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 71: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,8);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,7);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 72: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,8);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,7);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 73: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,7);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,8);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 74: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,7);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,8);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 75: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,6);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,9);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 76: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,6);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,9);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 77: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,5);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,10);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 78: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,5);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,10);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 79: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,4);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,11);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 80: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,4);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,11);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 81: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,3);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,12);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 82: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,3);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,12);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 83: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,2);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,13);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 84: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,2);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,13);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 85: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,1);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,14);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 86: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,1);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,14);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 87: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(159,8,0);
    R[n].ing[0]=crf_mk(172,1,0);
    R[n].ing[1]=crf_mk(172,1,0);
    R[n].ing[2]=crf_mk(172,1,0);
    R[n].ing[3]=crf_mk(172,1,0);
    R[n].ing[4]=crf_mk(351,1,15);
    R[n].ing[5]=crf_mk(172,1,0);
    R[n].ing[6]=crf_mk(172,1,0);
    R[n].ing[7]=crf_mk(172,1,0);
    R[n].ing[8]=crf_mk(172,1,0);
    ++n;
    /* registry 88: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(95,8,0);
    R[n].ing[0]=crf_mk(20,1,0);
    R[n].ing[1]=crf_mk(20,1,0);
    R[n].ing[2]=crf_mk(20,1,0);
    R[n].ing[3]=crf_mk(20,1,0);
    R[n].ing[4]=crf_mk(351,1,15);
    R[n].ing[5]=crf_mk(20,1,0);
    R[n].ing[6]=crf_mk(20,1,0);
    R[n].ing[7]=crf_mk(20,1,0);
    R[n].ing[8]=crf_mk(20,1,0);
    ++n;
    /* registry 89: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,15);
    R[n].ing[0]=crf_mk(35,1,0);
    R[n].ing[1]=crf_mk(35,1,0);
    R[n].ing[2]=crf_mk(35,1,0);
    R[n].ing[3]=crf_mk(35,1,0);
    R[n].ing[4]=crf_mk(35,1,0);
    R[n].ing[5]=crf_mk(35,1,0);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 90: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,14);
    R[n].ing[0]=crf_mk(35,1,1);
    R[n].ing[1]=crf_mk(35,1,1);
    R[n].ing[2]=crf_mk(35,1,1);
    R[n].ing[3]=crf_mk(35,1,1);
    R[n].ing[4]=crf_mk(35,1,1);
    R[n].ing[5]=crf_mk(35,1,1);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 91: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,13);
    R[n].ing[0]=crf_mk(35,1,2);
    R[n].ing[1]=crf_mk(35,1,2);
    R[n].ing[2]=crf_mk(35,1,2);
    R[n].ing[3]=crf_mk(35,1,2);
    R[n].ing[4]=crf_mk(35,1,2);
    R[n].ing[5]=crf_mk(35,1,2);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 92: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,12);
    R[n].ing[0]=crf_mk(35,1,3);
    R[n].ing[1]=crf_mk(35,1,3);
    R[n].ing[2]=crf_mk(35,1,3);
    R[n].ing[3]=crf_mk(35,1,3);
    R[n].ing[4]=crf_mk(35,1,3);
    R[n].ing[5]=crf_mk(35,1,3);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 93: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,11);
    R[n].ing[0]=crf_mk(35,1,4);
    R[n].ing[1]=crf_mk(35,1,4);
    R[n].ing[2]=crf_mk(35,1,4);
    R[n].ing[3]=crf_mk(35,1,4);
    R[n].ing[4]=crf_mk(35,1,4);
    R[n].ing[5]=crf_mk(35,1,4);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 94: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,10);
    R[n].ing[0]=crf_mk(35,1,5);
    R[n].ing[1]=crf_mk(35,1,5);
    R[n].ing[2]=crf_mk(35,1,5);
    R[n].ing[3]=crf_mk(35,1,5);
    R[n].ing[4]=crf_mk(35,1,5);
    R[n].ing[5]=crf_mk(35,1,5);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 95: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,9);
    R[n].ing[0]=crf_mk(35,1,6);
    R[n].ing[1]=crf_mk(35,1,6);
    R[n].ing[2]=crf_mk(35,1,6);
    R[n].ing[3]=crf_mk(35,1,6);
    R[n].ing[4]=crf_mk(35,1,6);
    R[n].ing[5]=crf_mk(35,1,6);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 96: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,8);
    R[n].ing[0]=crf_mk(35,1,7);
    R[n].ing[1]=crf_mk(35,1,7);
    R[n].ing[2]=crf_mk(35,1,7);
    R[n].ing[3]=crf_mk(35,1,7);
    R[n].ing[4]=crf_mk(35,1,7);
    R[n].ing[5]=crf_mk(35,1,7);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 97: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,7);
    R[n].ing[0]=crf_mk(35,1,8);
    R[n].ing[1]=crf_mk(35,1,8);
    R[n].ing[2]=crf_mk(35,1,8);
    R[n].ing[3]=crf_mk(35,1,8);
    R[n].ing[4]=crf_mk(35,1,8);
    R[n].ing[5]=crf_mk(35,1,8);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 98: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,6);
    R[n].ing[0]=crf_mk(35,1,9);
    R[n].ing[1]=crf_mk(35,1,9);
    R[n].ing[2]=crf_mk(35,1,9);
    R[n].ing[3]=crf_mk(35,1,9);
    R[n].ing[4]=crf_mk(35,1,9);
    R[n].ing[5]=crf_mk(35,1,9);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 99: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,5);
    R[n].ing[0]=crf_mk(35,1,10);
    R[n].ing[1]=crf_mk(35,1,10);
    R[n].ing[2]=crf_mk(35,1,10);
    R[n].ing[3]=crf_mk(35,1,10);
    R[n].ing[4]=crf_mk(35,1,10);
    R[n].ing[5]=crf_mk(35,1,10);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 100: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,4);
    R[n].ing[0]=crf_mk(35,1,11);
    R[n].ing[1]=crf_mk(35,1,11);
    R[n].ing[2]=crf_mk(35,1,11);
    R[n].ing[3]=crf_mk(35,1,11);
    R[n].ing[4]=crf_mk(35,1,11);
    R[n].ing[5]=crf_mk(35,1,11);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 101: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,3);
    R[n].ing[0]=crf_mk(35,1,12);
    R[n].ing[1]=crf_mk(35,1,12);
    R[n].ing[2]=crf_mk(35,1,12);
    R[n].ing[3]=crf_mk(35,1,12);
    R[n].ing[4]=crf_mk(35,1,12);
    R[n].ing[5]=crf_mk(35,1,12);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 102: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,2);
    R[n].ing[0]=crf_mk(35,1,13);
    R[n].ing[1]=crf_mk(35,1,13);
    R[n].ing[2]=crf_mk(35,1,13);
    R[n].ing[3]=crf_mk(35,1,13);
    R[n].ing[4]=crf_mk(35,1,13);
    R[n].ing[5]=crf_mk(35,1,13);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 103: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,1);
    R[n].ing[0]=crf_mk(35,1,14);
    R[n].ing[1]=crf_mk(35,1,14);
    R[n].ing[2]=crf_mk(35,1,14);
    R[n].ing[3]=crf_mk(35,1,14);
    R[n].ing[4]=crf_mk(35,1,14);
    R[n].ing[5]=crf_mk(35,1,14);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 104: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(425,1,0);
    R[n].ing[0]=crf_mk(35,1,15);
    R[n].ing[1]=crf_mk(35,1,15);
    R[n].ing[2]=crf_mk(35,1,15);
    R[n].ing[3]=crf_mk(35,1,15);
    R[n].ing[4]=crf_mk(35,1,15);
    R[n].ing[5]=crf_mk(35,1,15);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 105: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(442,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(5,1,32767);
    R[n].ing[4]=crf_mk(5,1,32767);
    R[n].ing[5]=crf_mk(5,1,32767);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(5,1,32767);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 106: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(84,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(5,1,32767);
    R[n].ing[4]=crf_mk(264,1,0);
    R[n].ing[5]=crf_mk(5,1,32767);
    R[n].ing[6]=crf_mk(5,1,32767);
    R[n].ing[7]=crf_mk(5,1,32767);
    R[n].ing[8]=crf_mk(5,1,32767);
    ++n;
    /* registry 107: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(420,2,0);
    R[n].ing[0]=crf_mk(287,1,0);
    R[n].ing[1]=crf_mk(287,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(287,1,0);
    R[n].ing[4]=crf_mk(341,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_empty();
    R[n].ing[8]=crf_mk(287,1,0);
    ++n;
    /* registry 108: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(25,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(5,1,32767);
    R[n].ing[4]=crf_mk(331,1,0);
    R[n].ing[5]=crf_mk(5,1,32767);
    R[n].ing[6]=crf_mk(5,1,32767);
    R[n].ing[7]=crf_mk(5,1,32767);
    R[n].ing[8]=crf_mk(5,1,32767);
    ++n;
    /* registry 109: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(47,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(340,1,0);
    R[n].ing[4]=crf_mk(340,1,0);
    R[n].ing[5]=crf_mk(340,1,0);
    R[n].ing[6]=crf_mk(5,1,32767);
    R[n].ing[7]=crf_mk(5,1,32767);
    R[n].ing[8]=crf_mk(5,1,32767);
    ++n;
    /* registry 110: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(46,1,0);
    R[n].ing[0]=crf_mk(289,1,0);
    R[n].ing[1]=crf_mk(12,1,32767);
    R[n].ing[2]=crf_mk(289,1,0);
    R[n].ing[3]=crf_mk(12,1,32767);
    R[n].ing[4]=crf_mk(289,1,0);
    R[n].ing[5]=crf_mk(12,1,32767);
    R[n].ing[6]=crf_mk(289,1,0);
    R[n].ing[7]=crf_mk(12,1,32767);
    R[n].ing[8]=crf_mk(289,1,0);
    ++n;
    /* registry 111: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(65,3,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(280,1,0);
    R[n].ing[6]=crf_mk(280,1,0);
    R[n].ing[7]=crf_empty();
    R[n].ing[8]=crf_mk(280,1,0);
    ++n;
    /* registry 112: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(323,3,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(5,1,32767);
    R[n].ing[4]=crf_mk(5,1,32767);
    R[n].ing[5]=crf_mk(5,1,32767);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 113: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(354,1,0);
    R[n].ing[0]=crf_mk(335,1,0);
    R[n].ing[1]=crf_mk(335,1,0);
    R[n].ing[2]=crf_mk(335,1,0);
    R[n].ing[3]=crf_mk(353,1,0);
    R[n].ing[4]=crf_mk(344,1,0);
    R[n].ing[5]=crf_mk(353,1,0);
    R[n].ing[6]=crf_mk(296,1,0);
    R[n].ing[7]=crf_mk(296,1,0);
    R[n].ing[8]=crf_mk(296,1,0);
    ++n;
    /* registry 114: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(66,16,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_mk(265,1,0);
    R[n].ing[7]=crf_empty();
    R[n].ing[8]=crf_mk(265,1,0);
    ++n;
    /* registry 115: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(27,6,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_mk(266,1,0);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(266,1,0);
    R[n].ing[6]=crf_mk(266,1,0);
    R[n].ing[7]=crf_mk(331,1,0);
    R[n].ing[8]=crf_mk(266,1,0);
    ++n;
    /* registry 116: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(157,6,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(76,1,32767);
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_mk(265,1,0);
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_mk(265,1,0);
    ++n;
    /* registry 117: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(28,6,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(70,1,32767);
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_mk(265,1,0);
    R[n].ing[7]=crf_mk(331,1,0);
    R[n].ing[8]=crf_mk(265,1,0);
    ++n;
    /* registry 118: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(380,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_mk(265,1,0);
    R[n].ing[7]=crf_mk(265,1,0);
    R[n].ing[8]=crf_mk(265,1,0);
    ++n;
    /* registry 119: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(346,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(287,1,0);
    R[n].ing[6]=crf_mk(280,1,0);
    R[n].ing[7]=crf_empty();
    R[n].ing[8]=crf_mk(287,1,0);
    ++n;
    /* registry 120: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(321,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(35,1,32767);
    R[n].ing[5]=crf_mk(280,1,0);
    R[n].ing[6]=crf_mk(280,1,0);
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_mk(280,1,0);
    ++n;
    /* registry 121: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(389,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(334,1,0);
    R[n].ing[5]=crf_mk(280,1,0);
    R[n].ing[6]=crf_mk(280,1,0);
    R[n].ing[7]=crf_mk(280,1,0);
    R[n].ing[8]=crf_mk(280,1,0);
    ++n;
    /* registry 122: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(322,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_mk(266,1,0);
    R[n].ing[4]=crf_mk(260,1,0);
    R[n].ing[5]=crf_mk(266,1,0);
    R[n].ing[6]=crf_mk(266,1,0);
    R[n].ing[7]=crf_mk(266,1,0);
    R[n].ing[8]=crf_mk(266,1,0);
    ++n;
    /* registry 123: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(404,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(76,1,32767);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(76,1,32767);
    R[n].ing[4]=crf_mk(406,1,0);
    R[n].ing[5]=crf_mk(76,1,32767);
    R[n].ing[6]=crf_mk(1,1,0);
    R[n].ing[7]=crf_mk(1,1,0);
    R[n].ing[8]=crf_mk(1,1,0);
    ++n;
    /* registry 124: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(347,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(266,1,0);
    R[n].ing[4]=crf_mk(331,1,0);
    R[n].ing[5]=crf_mk(266,1,0);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(266,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 125: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(345,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(331,1,0);
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(265,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 126: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(395,1,0);
    R[n].ing[0]=crf_mk(339,1,0);
    R[n].ing[1]=crf_mk(339,1,0);
    R[n].ing[2]=crf_mk(339,1,0);
    R[n].ing[3]=crf_mk(339,1,0);
    R[n].ing[4]=crf_mk(345,1,0);
    R[n].ing[5]=crf_mk(339,1,0);
    R[n].ing[6]=crf_mk(339,1,0);
    R[n].ing[7]=crf_mk(339,1,0);
    R[n].ing[8]=crf_mk(339,1,0);
    ++n;
    /* registry 127: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(23,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_mk(4,1,0);
    R[n].ing[3]=crf_mk(4,1,0);
    R[n].ing[4]=crf_mk(261,1,0);
    R[n].ing[5]=crf_mk(4,1,0);
    R[n].ing[6]=crf_mk(4,1,0);
    R[n].ing[7]=crf_mk(331,1,0);
    R[n].ing[8]=crf_mk(4,1,0);
    ++n;
    /* registry 128: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(158,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_mk(4,1,0);
    R[n].ing[3]=crf_mk(4,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(4,1,0);
    R[n].ing[6]=crf_mk(4,1,0);
    R[n].ing[7]=crf_mk(331,1,0);
    R[n].ing[8]=crf_mk(4,1,0);
    ++n;
    /* registry 129: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(218,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_mk(4,1,0);
    R[n].ing[3]=crf_mk(331,1,0);
    R[n].ing[4]=crf_mk(331,1,0);
    R[n].ing[5]=crf_mk(406,1,0);
    R[n].ing[6]=crf_mk(4,1,0);
    R[n].ing[7]=crf_mk(4,1,0);
    R[n].ing[8]=crf_mk(4,1,0);
    ++n;
    /* registry 130: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(33,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(4,1,0);
    R[n].ing[4]=crf_mk(265,1,0);
    R[n].ing[5]=crf_mk(4,1,0);
    R[n].ing[6]=crf_mk(4,1,0);
    R[n].ing[7]=crf_mk(331,1,0);
    R[n].ing[8]=crf_mk(4,1,0);
    ++n;
    /* registry 131: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(116,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(340,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(264,1,0);
    R[n].ing[4]=crf_mk(49,1,32767);
    R[n].ing[5]=crf_mk(264,1,0);
    R[n].ing[6]=crf_mk(49,1,32767);
    R[n].ing[7]=crf_mk(49,1,32767);
    R[n].ing[8]=crf_mk(49,1,32767);
    ++n;
    /* registry 132: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(145,1,0);
    R[n].ing[0]=crf_mk(42,1,32767);
    R[n].ing[1]=crf_mk(42,1,32767);
    R[n].ing[2]=crf_mk(42,1,32767);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(265,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(265,1,0);
    R[n].ing[7]=crf_mk(265,1,0);
    R[n].ing[8]=crf_mk(265,1,0);
    ++n;
    /* registry 133: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(151,1,0);
    R[n].ing[0]=crf_mk(20,1,32767);
    R[n].ing[1]=crf_mk(20,1,32767);
    R[n].ing[2]=crf_mk(20,1,32767);
    R[n].ing[3]=crf_mk(406,1,0);
    R[n].ing[4]=crf_mk(406,1,0);
    R[n].ing[5]=crf_mk(406,1,0);
    R[n].ing[6]=crf_mk(126,1,32767);
    R[n].ing[7]=crf_mk(126,1,32767);
    R[n].ing[8]=crf_mk(126,1,32767);
    ++n;
    /* registry 134: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(154,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(54,1,32767);
    R[n].ing[5]=crf_mk(265,1,0);
    R[n].ing[6]=crf_empty();
    R[n].ing[7]=crf_mk(265,1,0);
    R[n].ing[8]=crf_empty();
    ++n;
    /* registry 135: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9;
    R[n].output=crf_mk(416,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_empty();
    R[n].ing[6]=crf_mk(280,1,0);
    R[n].ing[7]=crf_mk(44,1,0);
    R[n].ing[8]=crf_mk(280,1,0);
    ++n;
    /* registry 137: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(102,16,0);
    R[n].ing[0]=crf_mk(20,1,32767);
    R[n].ing[1]=crf_mk(20,1,32767);
    R[n].ing[2]=crf_mk(20,1,32767);
    R[n].ing[3]=crf_mk(20,1,32767);
    R[n].ing[4]=crf_mk(20,1,32767);
    R[n].ing[5]=crf_mk(20,1,32767);
    ++n;
    /* registry 138: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(85,3,0);
    R[n].ing[0]=crf_mk(5,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(5,1,0);
    R[n].ing[3]=crf_mk(5,1,0);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(5,1,0);
    ++n;
    /* registry 139: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(188,3,0);
    R[n].ing[0]=crf_mk(5,1,1);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(5,1,1);
    R[n].ing[3]=crf_mk(5,1,1);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(5,1,1);
    ++n;
    /* registry 140: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(190,3,0);
    R[n].ing[0]=crf_mk(5,1,3);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(5,1,3);
    R[n].ing[3]=crf_mk(5,1,3);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(5,1,3);
    ++n;
    /* registry 141: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(192,3,0);
    R[n].ing[0]=crf_mk(5,1,4);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(5,1,4);
    R[n].ing[3]=crf_mk(5,1,4);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(5,1,4);
    ++n;
    /* registry 142: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(191,3,0);
    R[n].ing[0]=crf_mk(5,1,5);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(5,1,5);
    R[n].ing[3]=crf_mk(5,1,5);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(5,1,5);
    ++n;
    /* registry 143: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(139,6,0);
    R[n].ing[0]=crf_mk(4,1,32767);
    R[n].ing[1]=crf_mk(4,1,32767);
    R[n].ing[2]=crf_mk(4,1,32767);
    R[n].ing[3]=crf_mk(4,1,32767);
    R[n].ing[4]=crf_mk(4,1,32767);
    R[n].ing[5]=crf_mk(4,1,32767);
    ++n;
    /* registry 144: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(139,6,1);
    R[n].ing[0]=crf_mk(48,1,32767);
    R[n].ing[1]=crf_mk(48,1,32767);
    R[n].ing[2]=crf_mk(48,1,32767);
    R[n].ing[3]=crf_mk(48,1,32767);
    R[n].ing[4]=crf_mk(48,1,32767);
    R[n].ing[5]=crf_mk(48,1,32767);
    ++n;
    /* registry 145: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(113,6,0);
    R[n].ing[0]=crf_mk(112,1,32767);
    R[n].ing[1]=crf_mk(112,1,32767);
    R[n].ing[2]=crf_mk(112,1,32767);
    R[n].ing[3]=crf_mk(112,1,32767);
    R[n].ing[4]=crf_mk(112,1,32767);
    R[n].ing[5]=crf_mk(112,1,32767);
    ++n;
    /* registry 146: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(107,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(5,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(5,1,0);
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 147: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(184,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(5,1,2);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(5,1,2);
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 148: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(183,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(5,1,1);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(5,1,1);
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 149: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(185,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(5,1,3);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(5,1,3);
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 150: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(187,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(5,1,4);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(5,1,4);
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 151: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(186,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(5,1,5);
    R[n].ing[2]=crf_mk(280,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_mk(5,1,5);
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 152: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(324,3,0);
    R[n].ing[0]=crf_mk(5,1,0);
    R[n].ing[1]=crf_mk(5,1,0);
    R[n].ing[2]=crf_mk(5,1,0);
    R[n].ing[3]=crf_mk(5,1,0);
    R[n].ing[4]=crf_mk(5,1,0);
    R[n].ing[5]=crf_mk(5,1,0);
    ++n;
    /* registry 153: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(427,3,0);
    R[n].ing[0]=crf_mk(5,1,1);
    R[n].ing[1]=crf_mk(5,1,1);
    R[n].ing[2]=crf_mk(5,1,1);
    R[n].ing[3]=crf_mk(5,1,1);
    R[n].ing[4]=crf_mk(5,1,1);
    R[n].ing[5]=crf_mk(5,1,1);
    ++n;
    /* registry 154: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(428,3,0);
    R[n].ing[0]=crf_mk(5,1,2);
    R[n].ing[1]=crf_mk(5,1,2);
    R[n].ing[2]=crf_mk(5,1,2);
    R[n].ing[3]=crf_mk(5,1,2);
    R[n].ing[4]=crf_mk(5,1,2);
    R[n].ing[5]=crf_mk(5,1,2);
    ++n;
    /* registry 155: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(429,3,0);
    R[n].ing[0]=crf_mk(5,1,3);
    R[n].ing[1]=crf_mk(5,1,3);
    R[n].ing[2]=crf_mk(5,1,3);
    R[n].ing[3]=crf_mk(5,1,3);
    R[n].ing[4]=crf_mk(5,1,3);
    R[n].ing[5]=crf_mk(5,1,3);
    ++n;
    /* registry 156: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(430,3,0);
    R[n].ing[0]=crf_mk(5,1,4);
    R[n].ing[1]=crf_mk(5,1,4);
    R[n].ing[2]=crf_mk(5,1,4);
    R[n].ing[3]=crf_mk(5,1,4);
    R[n].ing[4]=crf_mk(5,1,4);
    R[n].ing[5]=crf_mk(5,1,4);
    ++n;
    /* registry 157: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(431,3,0);
    R[n].ing[0]=crf_mk(5,1,5);
    R[n].ing[1]=crf_mk(5,1,5);
    R[n].ing[2]=crf_mk(5,1,5);
    R[n].ing[3]=crf_mk(5,1,5);
    R[n].ing[4]=crf_mk(5,1,5);
    R[n].ing[5]=crf_mk(5,1,5);
    ++n;
    /* registry 158: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(374,3,0);
    R[n].ing[0]=crf_mk(20,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(20,1,32767);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(20,1,32767);
    R[n].ing[5]=crf_empty();
    ++n;
    /* registry 159: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(333,1,0);
    R[n].ing[0]=crf_mk(5,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(5,1,0);
    R[n].ing[3]=crf_mk(5,1,0);
    R[n].ing[4]=crf_mk(5,1,0);
    R[n].ing[5]=crf_mk(5,1,0);
    ++n;
    /* registry 160: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(444,1,0);
    R[n].ing[0]=crf_mk(5,1,1);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(5,1,1);
    R[n].ing[3]=crf_mk(5,1,1);
    R[n].ing[4]=crf_mk(5,1,1);
    R[n].ing[5]=crf_mk(5,1,1);
    ++n;
    /* registry 161: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(445,1,0);
    R[n].ing[0]=crf_mk(5,1,2);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(5,1,2);
    R[n].ing[3]=crf_mk(5,1,2);
    R[n].ing[4]=crf_mk(5,1,2);
    R[n].ing[5]=crf_mk(5,1,2);
    ++n;
    /* registry 162: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(446,1,0);
    R[n].ing[0]=crf_mk(5,1,3);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(5,1,3);
    R[n].ing[3]=crf_mk(5,1,3);
    R[n].ing[4]=crf_mk(5,1,3);
    R[n].ing[5]=crf_mk(5,1,3);
    ++n;
    /* registry 163: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(447,1,0);
    R[n].ing[0]=crf_mk(5,1,4);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(5,1,4);
    R[n].ing[3]=crf_mk(5,1,4);
    R[n].ing[4]=crf_mk(5,1,4);
    R[n].ing[5]=crf_mk(5,1,4);
    ++n;
    /* registry 164: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(448,1,0);
    R[n].ing[0]=crf_mk(5,1,5);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(5,1,5);
    R[n].ing[3]=crf_mk(5,1,5);
    R[n].ing[4]=crf_mk(5,1,5);
    R[n].ing[5]=crf_mk(5,1,5);
    ++n;
    /* registry 165: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(390,1,0);
    R[n].ing[0]=crf_mk(336,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(336,1,0);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(336,1,0);
    R[n].ing[5]=crf_empty();
    ++n;
    /* registry 166: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(271,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 167: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(290,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 168: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(275,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_mk(4,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 169: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(291,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 170: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(258,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 171: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(292,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 172: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(279,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_mk(264,1,0);
    R[n].ing[2]=crf_mk(264,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 173: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(293,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_mk(264,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 174: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(286,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 175: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(294,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(280,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(280,1,0);
    ++n;
    /* registry 176: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(101,16,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(265,1,0);
    R[n].ing[5]=crf_mk(265,1,0);
    ++n;
    /* registry 177: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(298,1,0);
    R[n].ing[0]=crf_mk(334,1,0);
    R[n].ing[1]=crf_mk(334,1,0);
    R[n].ing[2]=crf_mk(334,1,0);
    R[n].ing[3]=crf_mk(334,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(334,1,0);
    ++n;
    /* registry 178: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(301,1,0);
    R[n].ing[0]=crf_mk(334,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(334,1,0);
    R[n].ing[3]=crf_mk(334,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(334,1,0);
    ++n;
    /* registry 179: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(306,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(265,1,0);
    ++n;
    /* registry 180: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(309,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(265,1,0);
    ++n;
    /* registry 181: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(310,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_mk(264,1,0);
    R[n].ing[2]=crf_mk(264,1,0);
    R[n].ing[3]=crf_mk(264,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(264,1,0);
    ++n;
    /* registry 182: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(313,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(264,1,0);
    R[n].ing[3]=crf_mk(264,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(264,1,0);
    ++n;
    /* registry 183: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(314,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_mk(266,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(266,1,0);
    ++n;
    /* registry 184: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(317,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(266,1,0);
    R[n].ing[3]=crf_mk(266,1,0);
    R[n].ing[4]=crf_empty();
    R[n].ing[5]=crf_mk(266,1,0);
    ++n;
    /* registry 185: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,0);
    R[n].ing[0]=crf_mk(95,1,0);
    R[n].ing[1]=crf_mk(95,1,0);
    R[n].ing[2]=crf_mk(95,1,0);
    R[n].ing[3]=crf_mk(95,1,0);
    R[n].ing[4]=crf_mk(95,1,0);
    R[n].ing[5]=crf_mk(95,1,0);
    ++n;
    /* registry 186: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,1);
    R[n].ing[0]=crf_mk(95,1,1);
    R[n].ing[1]=crf_mk(95,1,1);
    R[n].ing[2]=crf_mk(95,1,1);
    R[n].ing[3]=crf_mk(95,1,1);
    R[n].ing[4]=crf_mk(95,1,1);
    R[n].ing[5]=crf_mk(95,1,1);
    ++n;
    /* registry 187: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,2);
    R[n].ing[0]=crf_mk(95,1,2);
    R[n].ing[1]=crf_mk(95,1,2);
    R[n].ing[2]=crf_mk(95,1,2);
    R[n].ing[3]=crf_mk(95,1,2);
    R[n].ing[4]=crf_mk(95,1,2);
    R[n].ing[5]=crf_mk(95,1,2);
    ++n;
    /* registry 188: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,3);
    R[n].ing[0]=crf_mk(95,1,3);
    R[n].ing[1]=crf_mk(95,1,3);
    R[n].ing[2]=crf_mk(95,1,3);
    R[n].ing[3]=crf_mk(95,1,3);
    R[n].ing[4]=crf_mk(95,1,3);
    R[n].ing[5]=crf_mk(95,1,3);
    ++n;
    /* registry 189: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,4);
    R[n].ing[0]=crf_mk(95,1,4);
    R[n].ing[1]=crf_mk(95,1,4);
    R[n].ing[2]=crf_mk(95,1,4);
    R[n].ing[3]=crf_mk(95,1,4);
    R[n].ing[4]=crf_mk(95,1,4);
    R[n].ing[5]=crf_mk(95,1,4);
    ++n;
    /* registry 190: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,5);
    R[n].ing[0]=crf_mk(95,1,5);
    R[n].ing[1]=crf_mk(95,1,5);
    R[n].ing[2]=crf_mk(95,1,5);
    R[n].ing[3]=crf_mk(95,1,5);
    R[n].ing[4]=crf_mk(95,1,5);
    R[n].ing[5]=crf_mk(95,1,5);
    ++n;
    /* registry 191: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,6);
    R[n].ing[0]=crf_mk(95,1,6);
    R[n].ing[1]=crf_mk(95,1,6);
    R[n].ing[2]=crf_mk(95,1,6);
    R[n].ing[3]=crf_mk(95,1,6);
    R[n].ing[4]=crf_mk(95,1,6);
    R[n].ing[5]=crf_mk(95,1,6);
    ++n;
    /* registry 192: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,7);
    R[n].ing[0]=crf_mk(95,1,7);
    R[n].ing[1]=crf_mk(95,1,7);
    R[n].ing[2]=crf_mk(95,1,7);
    R[n].ing[3]=crf_mk(95,1,7);
    R[n].ing[4]=crf_mk(95,1,7);
    R[n].ing[5]=crf_mk(95,1,7);
    ++n;
    /* registry 193: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,8);
    R[n].ing[0]=crf_mk(95,1,8);
    R[n].ing[1]=crf_mk(95,1,8);
    R[n].ing[2]=crf_mk(95,1,8);
    R[n].ing[3]=crf_mk(95,1,8);
    R[n].ing[4]=crf_mk(95,1,8);
    R[n].ing[5]=crf_mk(95,1,8);
    ++n;
    /* registry 194: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,9);
    R[n].ing[0]=crf_mk(95,1,9);
    R[n].ing[1]=crf_mk(95,1,9);
    R[n].ing[2]=crf_mk(95,1,9);
    R[n].ing[3]=crf_mk(95,1,9);
    R[n].ing[4]=crf_mk(95,1,9);
    R[n].ing[5]=crf_mk(95,1,9);
    ++n;
    /* registry 195: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,10);
    R[n].ing[0]=crf_mk(95,1,10);
    R[n].ing[1]=crf_mk(95,1,10);
    R[n].ing[2]=crf_mk(95,1,10);
    R[n].ing[3]=crf_mk(95,1,10);
    R[n].ing[4]=crf_mk(95,1,10);
    R[n].ing[5]=crf_mk(95,1,10);
    ++n;
    /* registry 196: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,11);
    R[n].ing[0]=crf_mk(95,1,11);
    R[n].ing[1]=crf_mk(95,1,11);
    R[n].ing[2]=crf_mk(95,1,11);
    R[n].ing[3]=crf_mk(95,1,11);
    R[n].ing[4]=crf_mk(95,1,11);
    R[n].ing[5]=crf_mk(95,1,11);
    ++n;
    /* registry 197: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,12);
    R[n].ing[0]=crf_mk(95,1,12);
    R[n].ing[1]=crf_mk(95,1,12);
    R[n].ing[2]=crf_mk(95,1,12);
    R[n].ing[3]=crf_mk(95,1,12);
    R[n].ing[4]=crf_mk(95,1,12);
    R[n].ing[5]=crf_mk(95,1,12);
    ++n;
    /* registry 198: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,13);
    R[n].ing[0]=crf_mk(95,1,13);
    R[n].ing[1]=crf_mk(95,1,13);
    R[n].ing[2]=crf_mk(95,1,13);
    R[n].ing[3]=crf_mk(95,1,13);
    R[n].ing[4]=crf_mk(95,1,13);
    R[n].ing[5]=crf_mk(95,1,13);
    ++n;
    /* registry 199: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,14);
    R[n].ing[0]=crf_mk(95,1,14);
    R[n].ing[1]=crf_mk(95,1,14);
    R[n].ing[2]=crf_mk(95,1,14);
    R[n].ing[3]=crf_mk(95,1,14);
    R[n].ing[4]=crf_mk(95,1,14);
    R[n].ing[5]=crf_mk(95,1,14);
    ++n;
    /* registry 200: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(160,16,15);
    R[n].ing[0]=crf_mk(95,1,15);
    R[n].ing[1]=crf_mk(95,1,15);
    R[n].ing[2]=crf_mk(95,1,15);
    R[n].ing[3]=crf_mk(95,1,15);
    R[n].ing[4]=crf_mk(95,1,15);
    R[n].ing[5]=crf_mk(95,1,15);
    ++n;
    /* registry 201: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(189,3,0);
    R[n].ing[0]=crf_mk(5,1,2);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(5,1,2);
    R[n].ing[3]=crf_mk(5,1,2);
    R[n].ing[4]=crf_mk(280,1,0);
    R[n].ing[5]=crf_mk(5,1,2);
    ++n;
    /* registry 202: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(96,2,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(5,1,32767);
    R[n].ing[4]=crf_mk(5,1,32767);
    R[n].ing[5]=crf_mk(5,1,32767);
    ++n;
    /* registry 203: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6;
    R[n].output=crf_mk(330,3,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(265,1,0);
    R[n].ing[5]=crf_mk(265,1,0);
    ++n;
    /* registry 204: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(281,4,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(5,1,32767);
    R[n].ing[5]=crf_empty();
    ++n;
    /* registry 205: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(328,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    R[n].ing[4]=crf_mk(265,1,0);
    R[n].ing[5]=crf_mk(265,1,0);
    ++n;
    /* registry 206: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(379,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(369,1,0);
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(4,1,0);
    R[n].ing[4]=crf_mk(4,1,0);
    R[n].ing[5]=crf_mk(4,1,0);
    ++n;
    /* registry 207: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(325,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_empty();
    R[n].ing[4]=crf_mk(265,1,0);
    R[n].ing[5]=crf_empty();
    ++n;
    /* registry 208: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(356,1,0);
    R[n].ing[0]=crf_mk(76,1,32767);
    R[n].ing[1]=crf_mk(331,1,0);
    R[n].ing[2]=crf_mk(76,1,32767);
    R[n].ing[3]=crf_mk(1,1,0);
    R[n].ing[4]=crf_mk(1,1,0);
    R[n].ing[5]=crf_mk(1,1,0);
    ++n;
    /* registry 209: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=2; R[n].nIng=6;
    R[n].output=crf_mk(355,1,0);
    R[n].ing[0]=crf_mk(35,1,32767);
    R[n].ing[1]=crf_mk(35,1,32767);
    R[n].ing[2]=crf_mk(35,1,32767);
    R[n].ing[3]=crf_mk(5,1,32767);
    R[n].ing[4]=crf_mk(5,1,32767);
    R[n].ing[5]=crf_mk(5,1,32767);
    ++n;
    /* registry 210: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(24,1,0);
    R[n].ing[0]=crf_mk(12,1,0);
    R[n].ing[1]=crf_mk(12,1,0);
    R[n].ing[2]=crf_mk(12,1,0);
    R[n].ing[3]=crf_mk(12,1,0);
    ++n;
    /* registry 211: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(179,1,0);
    R[n].ing[0]=crf_mk(12,1,1);
    R[n].ing[1]=crf_mk(12,1,1);
    R[n].ing[2]=crf_mk(12,1,1);
    R[n].ing[3]=crf_mk(12,1,1);
    ++n;
    /* registry 212: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(24,4,2);
    R[n].ing[0]=crf_mk(24,1,0);
    R[n].ing[1]=crf_mk(24,1,0);
    R[n].ing[2]=crf_mk(24,1,0);
    R[n].ing[3]=crf_mk(24,1,0);
    ++n;
    /* registry 213: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(179,4,2);
    R[n].ing[0]=crf_mk(179,1,0);
    R[n].ing[1]=crf_mk(179,1,0);
    R[n].ing[2]=crf_mk(179,1,0);
    R[n].ing[3]=crf_mk(179,1,0);
    ++n;
    /* registry 214: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(98,4,0);
    R[n].ing[0]=crf_mk(1,1,0);
    R[n].ing[1]=crf_mk(1,1,0);
    R[n].ing[2]=crf_mk(1,1,0);
    R[n].ing[3]=crf_mk(1,1,0);
    ++n;
    /* registry 215: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(112,1,0);
    R[n].ing[0]=crf_mk(405,1,0);
    R[n].ing[1]=crf_mk(405,1,0);
    R[n].ing[2]=crf_mk(405,1,0);
    R[n].ing[3]=crf_mk(405,1,0);
    ++n;
    /* registry 216: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(215,1,0);
    R[n].ing[0]=crf_mk(405,1,0);
    R[n].ing[1]=crf_mk(372,1,0);
    R[n].ing[2]=crf_mk(372,1,0);
    R[n].ing[3]=crf_mk(405,1,0);
    ++n;
    /* registry 217: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(3,4,1);
    R[n].ing[0]=crf_mk(3,1,0);
    R[n].ing[1]=crf_mk(13,1,32767);
    R[n].ing[2]=crf_mk(13,1,32767);
    R[n].ing[3]=crf_mk(3,1,0);
    ++n;
    /* registry 218: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(201,4,0);
    R[n].ing[0]=crf_mk(433,1,0);
    R[n].ing[1]=crf_mk(433,1,0);
    R[n].ing[2]=crf_mk(433,1,0);
    R[n].ing[3]=crf_mk(433,1,0);
    ++n;
    /* registry 219: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(206,4,0);
    R[n].ing[0]=crf_mk(121,1,32767);
    R[n].ing[1]=crf_mk(121,1,32767);
    R[n].ing[2]=crf_mk(121,1,32767);
    R[n].ing[3]=crf_mk(121,1,32767);
    ++n;
    /* registry 220: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(213,1,0);
    R[n].ing[0]=crf_mk(378,1,0);
    R[n].ing[1]=crf_mk(378,1,0);
    R[n].ing[2]=crf_mk(378,1,0);
    R[n].ing[3]=crf_mk(378,1,0);
    ++n;
    /* registry 221: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(80,1,0);
    R[n].ing[0]=crf_mk(332,1,0);
    R[n].ing[1]=crf_mk(332,1,0);
    R[n].ing[2]=crf_mk(332,1,0);
    R[n].ing[3]=crf_mk(332,1,0);
    ++n;
    /* registry 222: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(82,1,0);
    R[n].ing[0]=crf_mk(337,1,0);
    R[n].ing[1]=crf_mk(337,1,0);
    R[n].ing[2]=crf_mk(337,1,0);
    R[n].ing[3]=crf_mk(337,1,0);
    ++n;
    /* registry 223: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(45,1,0);
    R[n].ing[0]=crf_mk(336,1,0);
    R[n].ing[1]=crf_mk(336,1,0);
    R[n].ing[2]=crf_mk(336,1,0);
    R[n].ing[3]=crf_mk(336,1,0);
    ++n;
    /* registry 224: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(155,1,0);
    R[n].ing[0]=crf_mk(406,1,0);
    R[n].ing[1]=crf_mk(406,1,0);
    R[n].ing[2]=crf_mk(406,1,0);
    R[n].ing[3]=crf_mk(406,1,0);
    ++n;
    /* registry 225: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(398,1,0);
    R[n].ing[0]=crf_mk(346,1,0);
    R[n].ing[1]=crf_empty();
    R[n].ing[2]=crf_empty();
    R[n].ing[3]=crf_mk(391,1,0);
    ++n;
    /* registry 226: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(334,1,0);
    R[n].ing[0]=crf_mk(415,1,0);
    R[n].ing[1]=crf_mk(415,1,0);
    R[n].ing[2]=crf_mk(415,1,0);
    R[n].ing[3]=crf_mk(415,1,0);
    ++n;
    /* registry 227: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(359,1,0);
    R[n].ing[0]=crf_empty();
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_empty();
    ++n;
    /* registry 228: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(58,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(5,1,32767);
    R[n].ing[3]=crf_mk(5,1,32767);
    ++n;
    /* registry 229: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(1,2,3);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(406,1,0);
    R[n].ing[2]=crf_mk(406,1,0);
    R[n].ing[3]=crf_mk(4,1,0);
    ++n;
    /* registry 230: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(1,4,4);
    R[n].ing[0]=crf_mk(1,1,3);
    R[n].ing[1]=crf_mk(1,1,3);
    R[n].ing[2]=crf_mk(1,1,3);
    R[n].ing[3]=crf_mk(1,1,3);
    ++n;
    /* registry 231: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(1,4,2);
    R[n].ing[0]=crf_mk(1,1,1);
    R[n].ing[1]=crf_mk(1,1,1);
    R[n].ing[2]=crf_mk(1,1,1);
    R[n].ing[3]=crf_mk(1,1,1);
    ++n;
    /* registry 232: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(1,4,6);
    R[n].ing[0]=crf_mk(1,1,5);
    R[n].ing[1]=crf_mk(1,1,5);
    R[n].ing[2]=crf_mk(1,1,5);
    R[n].ing[3]=crf_mk(1,1,5);
    ++n;
    /* registry 233: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(168,1,0);
    R[n].ing[0]=crf_mk(409,1,0);
    R[n].ing[1]=crf_mk(409,1,0);
    R[n].ing[2]=crf_mk(409,1,0);
    R[n].ing[3]=crf_mk(409,1,0);
    ++n;
    /* registry 234: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(89,1,0);
    R[n].ing[0]=crf_mk(348,1,0);
    R[n].ing[1]=crf_mk(348,1,0);
    R[n].ing[2]=crf_mk(348,1,0);
    R[n].ing[3]=crf_mk(348,1,0);
    ++n;
    /* registry 235: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(35,1,0);
    R[n].ing[0]=crf_mk(287,1,0);
    R[n].ing[1]=crf_mk(287,1,0);
    R[n].ing[2]=crf_mk(287,1,0);
    R[n].ing[3]=crf_mk(287,1,0);
    ++n;
    /* registry 236: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4;
    R[n].output=crf_mk(167,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(265,1,0);
    R[n].ing[3]=crf_mk(265,1,0);
    ++n;
    /* registry 237: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(357,8,0);
    R[n].ing[0]=crf_mk(296,1,0);
    R[n].ing[1]=crf_mk(351,1,3);
    R[n].ing[2]=crf_mk(296,1,0);
    ++n;
    /* registry 238: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(229,1,0);
    R[n].ing[0]=crf_mk(450,1,0);
    R[n].ing[1]=crf_mk(54,1,32767);
    R[n].ing[2]=crf_mk(450,1,0);
    ++n;
    /* registry 239: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(78,6,0);
    R[n].ing[0]=crf_mk(80,1,32767);
    R[n].ing[1]=crf_mk(80,1,32767);
    R[n].ing[2]=crf_mk(80,1,32767);
    ++n;
    /* registry 240: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(44,6,1);
    R[n].ing[0]=crf_mk(24,1,32767);
    R[n].ing[1]=crf_mk(24,1,32767);
    R[n].ing[2]=crf_mk(24,1,32767);
    ++n;
    /* registry 241: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(44,6,4);
    R[n].ing[0]=crf_mk(45,1,32767);
    R[n].ing[1]=crf_mk(45,1,32767);
    R[n].ing[2]=crf_mk(45,1,32767);
    ++n;
    /* registry 242: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(44,6,5);
    R[n].ing[0]=crf_mk(98,1,32767);
    R[n].ing[1]=crf_mk(98,1,32767);
    R[n].ing[2]=crf_mk(98,1,32767);
    ++n;
    /* registry 243: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(44,6,6);
    R[n].ing[0]=crf_mk(112,1,32767);
    R[n].ing[1]=crf_mk(112,1,32767);
    R[n].ing[2]=crf_mk(112,1,32767);
    ++n;
    /* registry 244: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(44,6,7);
    R[n].ing[0]=crf_mk(155,1,32767);
    R[n].ing[1]=crf_mk(155,1,32767);
    R[n].ing[2]=crf_mk(155,1,32767);
    ++n;
    /* registry 245: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(182,6,0);
    R[n].ing[0]=crf_mk(179,1,32767);
    R[n].ing[1]=crf_mk(179,1,32767);
    R[n].ing[2]=crf_mk(179,1,32767);
    ++n;
    /* registry 246: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(205,6,0);
    R[n].ing[0]=crf_mk(201,1,32767);
    R[n].ing[1]=crf_mk(201,1,32767);
    R[n].ing[2]=crf_mk(201,1,32767);
    ++n;
    /* registry 247: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(126,6,0);
    R[n].ing[0]=crf_mk(5,1,0);
    R[n].ing[1]=crf_mk(5,1,0);
    R[n].ing[2]=crf_mk(5,1,0);
    ++n;
    /* registry 248: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(126,6,2);
    R[n].ing[0]=crf_mk(5,1,2);
    R[n].ing[1]=crf_mk(5,1,2);
    R[n].ing[2]=crf_mk(5,1,2);
    ++n;
    /* registry 249: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(126,6,1);
    R[n].ing[0]=crf_mk(5,1,1);
    R[n].ing[1]=crf_mk(5,1,1);
    R[n].ing[2]=crf_mk(5,1,1);
    ++n;
    /* registry 250: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(126,6,3);
    R[n].ing[0]=crf_mk(5,1,3);
    R[n].ing[1]=crf_mk(5,1,3);
    R[n].ing[2]=crf_mk(5,1,3);
    ++n;
    /* registry 251: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(126,6,4);
    R[n].ing[0]=crf_mk(5,1,4);
    R[n].ing[1]=crf_mk(5,1,4);
    R[n].ing[2]=crf_mk(5,1,4);
    ++n;
    /* registry 252: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(126,6,5);
    R[n].ing[0]=crf_mk(5,1,5);
    R[n].ing[1]=crf_mk(5,1,5);
    R[n].ing[2]=crf_mk(5,1,5);
    ++n;
    /* registry 253: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(297,1,0);
    R[n].ing[0]=crf_mk(296,1,0);
    R[n].ing[1]=crf_mk(296,1,0);
    R[n].ing[2]=crf_mk(296,1,0);
    ++n;
    /* registry 254: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(269,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 255: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(273,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 256: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(256,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 257: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(277,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 258: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(284,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 259: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(268,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 260: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(272,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 261: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(267,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 262: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(276,1,0);
    R[n].ing[0]=crf_mk(264,1,0);
    R[n].ing[1]=crf_mk(264,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 263: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(283,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    R[n].ing[2]=crf_mk(280,1,0);
    ++n;
    /* registry 264: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(262,4,0);
    R[n].ing[0]=crf_mk(318,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(288,1,0);
    ++n;
    /* registry 265: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(339,3,0);
    R[n].ing[0]=crf_mk(338,1,0);
    R[n].ing[1]=crf_mk(338,1,0);
    R[n].ing[2]=crf_mk(338,1,0);
    ++n;
    /* registry 266: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(44,6,3);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    R[n].ing[2]=crf_mk(4,1,0);
    ++n;
    /* registry 267: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=3; R[n].height=1; R[n].nIng=3;
    R[n].output=crf_mk(44,6,0);
    R[n].ing[0]=crf_mk(1,1,0);
    R[n].ing[1]=crf_mk(1,1,0);
    R[n].ing[2]=crf_mk(1,1,0);
    ++n;
    /* registry 268: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3;
    R[n].output=crf_mk(131,2,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    R[n].ing[2]=crf_mk(5,1,32767);
    ++n;
    /* registry 269: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(24,1,1);
    R[n].ing[0]=crf_mk(44,1,1);
    R[n].ing[1]=crf_mk(44,1,1);
    ++n;
    /* registry 270: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(179,1,1);
    R[n].ing[0]=crf_mk(182,1,0);
    R[n].ing[1]=crf_mk(182,1,0);
    ++n;
    /* registry 271: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(155,1,1);
    R[n].ing[0]=crf_mk(44,1,7);
    R[n].ing[1]=crf_mk(44,1,7);
    ++n;
    /* registry 272: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(155,2,2);
    R[n].ing[0]=crf_mk(155,1,0);
    R[n].ing[1]=crf_mk(155,1,0);
    ++n;
    /* registry 273: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(98,1,3);
    R[n].ing[0]=crf_mk(44,1,5);
    R[n].ing[1]=crf_mk(44,1,5);
    ++n;
    /* registry 274: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(202,1,0);
    R[n].ing[0]=crf_mk(205,1,32767);
    R[n].ing[1]=crf_mk(205,1,32767);
    ++n;
    /* registry 275: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,0);
    R[n].ing[0]=crf_mk(35,1,0);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 276: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,1);
    R[n].ing[0]=crf_mk(35,1,1);
    R[n].ing[1]=crf_mk(35,1,1);
    ++n;
    /* registry 277: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,2);
    R[n].ing[0]=crf_mk(35,1,2);
    R[n].ing[1]=crf_mk(35,1,2);
    ++n;
    /* registry 278: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,3);
    R[n].ing[0]=crf_mk(35,1,3);
    R[n].ing[1]=crf_mk(35,1,3);
    ++n;
    /* registry 279: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,4);
    R[n].ing[0]=crf_mk(35,1,4);
    R[n].ing[1]=crf_mk(35,1,4);
    ++n;
    /* registry 280: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,5);
    R[n].ing[0]=crf_mk(35,1,5);
    R[n].ing[1]=crf_mk(35,1,5);
    ++n;
    /* registry 281: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,6);
    R[n].ing[0]=crf_mk(35,1,6);
    R[n].ing[1]=crf_mk(35,1,6);
    ++n;
    /* registry 282: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,7);
    R[n].ing[0]=crf_mk(35,1,7);
    R[n].ing[1]=crf_mk(35,1,7);
    ++n;
    /* registry 283: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,8);
    R[n].ing[0]=crf_mk(35,1,8);
    R[n].ing[1]=crf_mk(35,1,8);
    ++n;
    /* registry 284: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,9);
    R[n].ing[0]=crf_mk(35,1,9);
    R[n].ing[1]=crf_mk(35,1,9);
    ++n;
    /* registry 285: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,10);
    R[n].ing[0]=crf_mk(35,1,10);
    R[n].ing[1]=crf_mk(35,1,10);
    ++n;
    /* registry 286: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,11);
    R[n].ing[0]=crf_mk(35,1,11);
    R[n].ing[1]=crf_mk(35,1,11);
    ++n;
    /* registry 287: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,12);
    R[n].ing[0]=crf_mk(35,1,12);
    R[n].ing[1]=crf_mk(35,1,12);
    ++n;
    /* registry 288: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,13);
    R[n].ing[0]=crf_mk(35,1,13);
    R[n].ing[1]=crf_mk(35,1,13);
    ++n;
    /* registry 289: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,14);
    R[n].ing[0]=crf_mk(35,1,14);
    R[n].ing[1]=crf_mk(35,1,14);
    ++n;
    /* registry 290: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(171,3,15);
    R[n].ing[0]=crf_mk(35,1,15);
    R[n].ing[1]=crf_mk(35,1,15);
    ++n;
    /* registry 291: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(91,1,0);
    R[n].ing[0]=crf_mk(86,1,32767);
    R[n].ing[1]=crf_mk(50,1,32767);
    ++n;
    /* registry 292: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(342,1,0);
    R[n].ing[0]=crf_mk(54,1,32767);
    R[n].ing[1]=crf_mk(328,1,0);
    ++n;
    /* registry 293: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(343,1,0);
    R[n].ing[0]=crf_mk(61,1,32767);
    R[n].ing[1]=crf_mk(328,1,0);
    ++n;
    /* registry 294: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(407,1,0);
    R[n].ing[0]=crf_mk(46,1,32767);
    R[n].ing[1]=crf_mk(328,1,0);
    ++n;
    /* registry 295: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(408,1,0);
    R[n].ing[0]=crf_mk(154,1,32767);
    R[n].ing[1]=crf_mk(328,1,0);
    ++n;
    /* registry 296: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(198,4,0);
    R[n].ing[0]=crf_mk(369,1,0);
    R[n].ing[1]=crf_mk(433,1,0);
    ++n;
    /* registry 297: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(280,4,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    ++n;
    /* registry 298: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(50,4,0);
    R[n].ing[0]=crf_mk(263,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    ++n;
    /* registry 299: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(50,4,0);
    R[n].ing[0]=crf_mk(263,1,1);
    R[n].ing[1]=crf_mk(280,1,0);
    ++n;
    /* registry 300: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(69,1,0);
    R[n].ing[0]=crf_mk(280,1,0);
    R[n].ing[1]=crf_mk(4,1,0);
    ++n;
    /* registry 301: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(76,1,0);
    R[n].ing[0]=crf_mk(331,1,0);
    R[n].ing[1]=crf_mk(280,1,0);
    ++n;
    /* registry 302: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(70,1,0);
    R[n].ing[0]=crf_mk(1,1,0);
    R[n].ing[1]=crf_mk(1,1,0);
    ++n;
    /* registry 303: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(72,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    R[n].ing[1]=crf_mk(5,1,32767);
    ++n;
    /* registry 304: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(148,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(265,1,0);
    ++n;
    /* registry 305: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=2; R[n].height=1; R[n].nIng=2;
    R[n].output=crf_mk(147,1,0);
    R[n].ing[0]=crf_mk(266,1,0);
    R[n].ing[1]=crf_mk(266,1,0);
    ++n;
    /* registry 306: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2;
    R[n].output=crf_mk(29,1,0);
    R[n].ing[0]=crf_mk(341,1,0);
    R[n].ing[1]=crf_mk(33,1,32767);
    ++n;
    /* registry 307: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(266,9,0);
    R[n].ing[0]=crf_mk(41,1,32767);
    ++n;
    /* registry 308: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(265,9,0);
    R[n].ing[0]=crf_mk(42,1,32767);
    ++n;
    /* registry 309: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(264,9,0);
    R[n].ing[0]=crf_mk(57,1,32767);
    ++n;
    /* registry 310: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(388,9,0);
    R[n].ing[0]=crf_mk(133,1,32767);
    ++n;
    /* registry 311: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(351,9,4);
    R[n].ing[0]=crf_mk(22,1,32767);
    ++n;
    /* registry 312: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(331,9,0);
    R[n].ing[0]=crf_mk(152,1,32767);
    ++n;
    /* registry 313: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(263,9,0);
    R[n].ing[0]=crf_mk(173,1,32767);
    ++n;
    /* registry 314: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(296,9,0);
    R[n].ing[0]=crf_mk(170,1,32767);
    ++n;
    /* registry 315: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(341,9,0);
    R[n].ing[0]=crf_mk(165,1,32767);
    ++n;
    /* registry 316: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(362,1,0);
    R[n].ing[0]=crf_mk(360,1,0);
    ++n;
    /* registry 317: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(361,4,0);
    R[n].ing[0]=crf_mk(86,1,32767);
    ++n;
    /* registry 318: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,0);
    R[n].ing[0]=crf_mk(17,1,0);
    ++n;
    /* registry 319: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,1);
    R[n].ing[0]=crf_mk(17,1,1);
    ++n;
    /* registry 320: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,2);
    R[n].ing[0]=crf_mk(17,1,2);
    ++n;
    /* registry 321: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,3);
    R[n].ing[0]=crf_mk(17,1,3);
    ++n;
    /* registry 322: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,4);
    R[n].ing[0]=crf_mk(162,1,0);
    ++n;
    /* registry 323: net.minecraft.item.crafting.ShapedRecipes */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(5,4,5);
    R[n].ing[0]=crf_mk(162,1,1);
    ++n;
    /* registry 324: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(371,9,0);
    R[n].ing[0]=crf_mk(266,1,0);
    ++n;
    /* registry 325: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(452,9,0);
    R[n].ing[0]=crf_mk(265,1,0);
    ++n;
    /* registry 326: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(353,1,0);
    R[n].ing[0]=crf_mk(338,1,0);
    ++n;
    /* registry 327: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(77,1,0);
    R[n].ing[0]=crf_mk(1,1,0);
    ++n;
    /* registry 328: net.minecraftforge.oredict.ShapedOreRecipe */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1;
    R[n].output=crf_mk(143,1,0);
    R[n].ing[0]=crf_mk(5,1,32767);
    ++n;
    /* registry 336: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=4;
    R[n].output=crf_mk(351,4,13);
    R[n].ing[0]=crf_mk(351,1,4);
    R[n].ing[1]=crf_mk(351,1,1);
    R[n].ing[2]=crf_mk(351,1,1);
    R[n].ing[3]=crf_mk(351,1,15);
    ++n;
    /* registry 337: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=4;
    R[n].output=crf_mk(340,1,0);
    R[n].ing[0]=crf_mk(339,1,0);
    R[n].ing[1]=crf_mk(339,1,0);
    R[n].ing[2]=crf_mk(339,1,0);
    R[n].ing[3]=crf_mk(334,1,0);
    ++n;
    /* registry 338: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(282,1,0);
    R[n].ing[0]=crf_mk(39,1,0);
    R[n].ing[1]=crf_mk(40,1,0);
    R[n].ing[2]=crf_mk(281,1,0);
    ++n;
    /* registry 339: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(376,1,0);
    R[n].ing[0]=crf_mk(375,1,0);
    R[n].ing[1]=crf_mk(39,1,0);
    R[n].ing[2]=crf_mk(353,1,0);
    ++n;
    /* registry 340: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(400,1,0);
    R[n].ing[0]=crf_mk(86,1,0);
    R[n].ing[1]=crf_mk(353,1,0);
    R[n].ing[2]=crf_mk(344,1,0);
    ++n;
    /* registry 341: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(351,3,7);
    R[n].ing[0]=crf_mk(351,1,0);
    R[n].ing[1]=crf_mk(351,1,15);
    R[n].ing[2]=crf_mk(351,1,15);
    ++n;
    /* registry 342: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(351,3,13);
    R[n].ing[0]=crf_mk(351,1,4);
    R[n].ing[1]=crf_mk(351,1,1);
    R[n].ing[2]=crf_mk(351,1,9);
    ++n;
    /* registry 343: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(386,1,0);
    R[n].ing[0]=crf_mk(340,1,0);
    R[n].ing[1]=crf_mk(351,1,0);
    R[n].ing[2]=crf_mk(288,1,0);
    ++n;
    /* registry 344: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(385,3,0);
    R[n].ing[0]=crf_mk(289,1,0);
    R[n].ing[1]=crf_mk(377,1,0);
    R[n].ing[2]=crf_mk(263,1,0);
    ++n;
    /* registry 345: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=3;
    R[n].output=crf_mk(385,3,0);
    R[n].ing[0]=crf_mk(289,1,0);
    R[n].ing[1]=crf_mk(377,1,0);
    R[n].ing[2]=crf_mk(263,1,1);
    ++n;
    /* registry 346: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(98,1,1);
    R[n].ing[0]=crf_mk(98,1,0);
    R[n].ing[1]=crf_mk(106,1,0);
    ++n;
    /* registry 349: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(378,1,0);
    R[n].ing[0]=crf_mk(377,1,0);
    R[n].ing[1]=crf_mk(341,1,0);
    ++n;
    /* registry 350: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(146,1,0);
    R[n].ing[0]=crf_mk(54,1,0);
    R[n].ing[1]=crf_mk(131,1,0);
    ++n;
    /* registry 351: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(48,1,0);
    R[n].ing[0]=crf_mk(4,1,0);
    R[n].ing[1]=crf_mk(106,1,0);
    ++n;
    /* registry 352: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(1,1,1);
    R[n].ing[0]=crf_mk(1,1,3);
    R[n].ing[1]=crf_mk(406,1,0);
    ++n;
    /* registry 353: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(1,2,5);
    R[n].ing[0]=crf_mk(1,1,3);
    R[n].ing[1]=crf_mk(4,1,0);
    ++n;
    /* registry 354: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,0);
    R[n].ing[0]=crf_mk(351,1,15);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 355: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,1);
    R[n].ing[0]=crf_mk(351,1,14);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 356: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,2);
    R[n].ing[0]=crf_mk(351,1,13);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 357: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,3);
    R[n].ing[0]=crf_mk(351,1,12);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 358: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,4);
    R[n].ing[0]=crf_mk(351,1,11);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 359: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,5);
    R[n].ing[0]=crf_mk(351,1,10);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 360: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,6);
    R[n].ing[0]=crf_mk(351,1,9);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 361: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,7);
    R[n].ing[0]=crf_mk(351,1,8);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 362: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,8);
    R[n].ing[0]=crf_mk(351,1,7);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 363: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,9);
    R[n].ing[0]=crf_mk(351,1,6);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 364: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,10);
    R[n].ing[0]=crf_mk(351,1,5);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 365: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,11);
    R[n].ing[0]=crf_mk(351,1,4);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 366: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,12);
    R[n].ing[0]=crf_mk(351,1,3);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 367: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,13);
    R[n].ing[0]=crf_mk(351,1,2);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 368: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,14);
    R[n].ing[0]=crf_mk(351,1,1);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 369: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(35,1,15);
    R[n].ing[0]=crf_mk(351,1,0);
    R[n].ing[1]=crf_mk(35,1,0);
    ++n;
    /* registry 370: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,9);
    R[n].ing[0]=crf_mk(351,1,1);
    R[n].ing[1]=crf_mk(351,1,15);
    ++n;
    /* registry 371: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,14);
    R[n].ing[0]=crf_mk(351,1,1);
    R[n].ing[1]=crf_mk(351,1,11);
    ++n;
    /* registry 372: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,10);
    R[n].ing[0]=crf_mk(351,1,2);
    R[n].ing[1]=crf_mk(351,1,15);
    ++n;
    /* registry 373: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,8);
    R[n].ing[0]=crf_mk(351,1,0);
    R[n].ing[1]=crf_mk(351,1,15);
    ++n;
    /* registry 374: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,7);
    R[n].ing[0]=crf_mk(351,1,8);
    R[n].ing[1]=crf_mk(351,1,15);
    ++n;
    /* registry 375: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,12);
    R[n].ing[0]=crf_mk(351,1,4);
    R[n].ing[1]=crf_mk(351,1,15);
    ++n;
    /* registry 376: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,6);
    R[n].ing[0]=crf_mk(351,1,4);
    R[n].ing[1]=crf_mk(351,1,2);
    ++n;
    /* registry 377: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,5);
    R[n].ing[0]=crf_mk(351,1,4);
    R[n].ing[1]=crf_mk(351,1,1);
    ++n;
    /* registry 378: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(351,2,13);
    R[n].ing[0]=crf_mk(351,1,5);
    R[n].ing[1]=crf_mk(351,1,9);
    ++n;
    /* registry 379: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(259,1,0);
    R[n].ing[0]=crf_mk(265,1,0);
    R[n].ing[1]=crf_mk(318,1,0);
    ++n;
    /* registry 380: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2;
    R[n].output=crf_mk(381,1,0);
    R[n].ing[0]=crf_mk(368,1,0);
    R[n].ing[1]=crf_mk(377,1,0);
    ++n;
    /* registry 381: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(377,2,0);
    R[n].ing[0]=crf_mk(369,1,0);
    ++n;
    /* registry 382: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,11);
    R[n].ing[0]=crf_mk(37,1,0);
    ++n;
    /* registry 383: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,1);
    R[n].ing[0]=crf_mk(38,1,0);
    ++n;
    /* registry 384: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,9,15);
    R[n].ing[0]=crf_mk(216,1,0);
    ++n;
    /* registry 385: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,12);
    R[n].ing[0]=crf_mk(38,1,1);
    ++n;
    /* registry 386: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,13);
    R[n].ing[0]=crf_mk(38,1,2);
    ++n;
    /* registry 387: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,7);
    R[n].ing[0]=crf_mk(38,1,3);
    ++n;
    /* registry 388: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,1);
    R[n].ing[0]=crf_mk(38,1,4);
    ++n;
    /* registry 389: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,14);
    R[n].ing[0]=crf_mk(38,1,5);
    ++n;
    /* registry 390: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,7);
    R[n].ing[0]=crf_mk(38,1,6);
    ++n;
    /* registry 391: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,9);
    R[n].ing[0]=crf_mk(38,1,7);
    ++n;
    /* registry 392: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,7);
    R[n].ing[0]=crf_mk(38,1,8);
    ++n;
    /* registry 393: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,2,11);
    R[n].ing[0]=crf_mk(175,1,0);
    ++n;
    /* registry 394: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,2,13);
    R[n].ing[0]=crf_mk(175,1,1);
    ++n;
    /* registry 395: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,2,1);
    R[n].ing[0]=crf_mk(175,1,4);
    ++n;
    /* registry 396: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,2,9);
    R[n].ing[0]=crf_mk(175,1,5);
    ++n;
    /* registry 397: net.minecraft.item.crafting.ShapelessRecipes */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,1,1);
    R[n].ing[0]=crf_mk(434,1,0);
    ++n;
    /* registry 398: net.minecraftforge.oredict.ShapelessOreRecipe */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=1;
    R[n].output=crf_mk(351,3,15);
    R[n].ing[0]=crf_mk(352,1,0);
    ++n;
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
