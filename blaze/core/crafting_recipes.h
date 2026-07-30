/* crafting_recipes: exact C port of MC 1.11.2 item/crafting/* (the vanilla crafting engine) +
 * a SURVIVAL-CRITICAL recipe subset registered in the exact vanilla order.
 *
 * PORT TARGETS (net/minecraft/item/crafting/ + net/minecraft/inventory/):
 *   - CraftingManager.findMatchingRecipe (iterate recipes in registration order, first match wins)
 *   - ShapedRecipes.matches + checkMatch (the (x,y) offset loop AND the horizontal-mirror flag) +
 *     getCraftingResult (returns a COPY with the recipe's count/meta)
 *   - ShapelessRecipes.matches (copy-list, remove each matched ingredient once) + getCraftingResult
 *   - InventoryCrafting.getStackInRowAndColumn (3x3 grid, slot = row + column*width)
 *   - the 32767 metadata WILDCARD rule (port verbatim)
 *
 * This unit is PURE LOGIC: no RNG, no floats. Deterministic => Java == CPU == CUDA by construction
 * (SPEC verify tier: vanilla bitwise). No seed is used.
 *
 * SANCTIONED REGISTRY SUBSTITUTION (same pattern as genlayer_biomes' Biome-id tables): the Item /
 * Block object registry is replaced by the exact vanilla legacy integer ITEM ids. An ItemStack is
 * the POD triple (item, count, meta). isEmpty / getItem / getMetadata / copy reduce to field ops.
 * A Block used as an ingredient has its item form's id == the block id (legacy numbering) and, via
 * CraftingManager.addRecipe's Block path, metadata 32767 (wildcard). The MATCHING ALGORITHM and the
 * RECIPE DATA are verbatim MC; only the registry lookup is the integer-id shim. The golden
 * (oracle/goldens/crafting_recipes/Golden.java) runs the verbatim ShapedRecipes/ShapelessRecipes/
 * CraftingManager.addRecipe code over the SAME id constants and the SAME test battery.
 *
 * DOCUMENTED DEVIATIONS:
 *  - CraftingManager's constructor ends with Collections.sort(recipes, comparator) (shaped before
 *    shapeless, then larger recipeSize first). We iterate in REGISTRATION order instead (the task's
 *    sanctioned "iteration order = registration order, first match wins"). This cannot change ANY
 *    battery output: the battery is constructed so no grid matches more than one recipe in the
 *    subset, so first-match is sort-invariant. The golden likewise omits the sort, so golden and C
 *    share one iteration order by construction. (Analogous to the genlayer IntCache substitution.)
 *  - NBT / getRemainingItems / copyIngredientNBT are irrelevant to (item,count,meta) output: CUT.
 */
#ifndef MC_CRAFTING_RECIPES_H
#define MC_CRAFTING_RECIPES_H

#include "mc.h"

/* ===== vanilla 1.11.2 legacy item ids (Block.getIdFromBlock for blocks; Items.java registration
 * order for items). Cross-checked vs core/mc_blocks.h (PLANKS=5, LOG=17, COBBLESTONE=4, TORCH=50).
 * Equality is all the matcher needs; the exact values are the real vanilla ids for faithfulness. */
enum {
    IT_AIR             = 0,
    /* blocks (item id == block id) */
    IT_COBBLESTONE     = 4,
    IT_PLANKS          = 5,
    IT_LOG             = 17,
    IT_TORCH           = 50,
    IT_CHEST           = 54,
    IT_CRAFTING_TABLE  = 58,
    IT_FURNACE         = 61,
    /* items (256+) */
    IT_FLINT_AND_STEEL = 259,
    IT_COAL            = 263,
    IT_IRON_INGOT      = 265,
    IT_WOODEN_SWORD    = 268,
    IT_WOODEN_SHOVEL   = 269,
    IT_WOODEN_PICKAXE  = 270,
    IT_WOODEN_AXE      = 271,
    IT_STONE_SWORD     = 272,
    IT_STONE_SHOVEL    = 273,
    IT_STONE_PICKAXE   = 274,
    IT_STONE_AXE       = 275,
    IT_STICK           = 280,
    IT_WOODEN_HOE      = 290,
    IT_STONE_HOE       = 291,
    IT_FLINT           = 318
};

#define CR_WILDCARD 32767   /* OreDictionary-free vanilla metadata wildcard (ShapedRecipes line 118) */

/* POD ItemStack = (item, count, meta). EMPTY <=> item == AIR (or count <= 0). */
typedef struct { i32 item; i32 count; i32 meta; } CRStack;

MC_HD static inline CRStack cr_empty(void) { CRStack s; s.item = IT_AIR; s.count = 0; s.meta = 0; return s; }
MC_HD static inline CRStack cr_mk(i32 item, i32 count, i32 meta) { CRStack s; s.item = item; s.count = count; s.meta = meta; return s; }
/* ItemStack.isEmpty(): AIR or non-positive count. */
MC_HD static inline int cr_isEmpty(CRStack s) { return s.item == IT_AIR || s.count <= 0; }

/* Recipe POD. shaped: ing[] is row-major width*height (the verbatim ShapedRecipes.recipeItems built
 * by CraftingManager.addRecipe). shapeless: ing[0..nIng-1] is the unordered ingredient list. For
 * ingredients only (item, meta) matter (count is forced to 1 by addRecipe / setCount(1)). */
typedef struct {
    int shaped;          /* 1 = ShapedRecipes, 0 = ShapelessRecipes */
    int width, height;   /* shaped dims */
    int nIng;            /* shapeless: ingredient count */
    CRStack ing[9];
    CRStack output;
} CRRecipe;

#define CR_GRID 3        /* InventoryCrafting is 3x3; slot = x + y*3 (== getStackInRowAndColumn(x,y)) */

/* ===== ShapedRecipes.checkMatch (verbatim): try the recipe at (offX,offY), optionally mirrored. */
MC_HD static inline int cr_checkMatch(const CRRecipe *r, const CRStack *grid,
                                      int offX, int offY, int mirror) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            int k = i - offX;
            int l = j - offY;
            CRStack ing = cr_empty();
            if (k >= 0 && l >= 0 && k < r->width && l < r->height) {
                if (mirror)
                    ing = r->ing[r->width - k - 1 + l * r->width];
                else
                    ing = r->ing[k + l * r->width];
            }
            CRStack g = grid[i + j * CR_GRID];   /* getStackInRowAndColumn(i, j) */
            if (!cr_isEmpty(g) || !cr_isEmpty(ing)) {
                if (cr_isEmpty(g) != cr_isEmpty(ing)) return 0;
                if (ing.item != g.item) return 0;
                if (ing.meta != CR_WILDCARD && ing.meta != g.meta) return 0;
            }
        }
    }
    return 1;
}

/* ShapedRecipes.matches (verbatim): scan every legal offset; mirror then non-mirror. */
MC_HD static inline int cr_shapedMatches(const CRRecipe *r, const CRStack *grid) {
    for (int i = 0; i <= 3 - r->width; ++i) {
        for (int j = 0; j <= 3 - r->height; ++j) {
            if (cr_checkMatch(r, grid, i, j, 1)) return 1;
            if (cr_checkMatch(r, grid, i, j, 0)) return 1;
        }
    }
    return 0;
}

/* ShapelessRecipes.matches (verbatim): copy the ingredient list, consume each matched item once. */
MC_HD static inline int cr_shapelessMatches(const CRRecipe *r, const CRStack *grid) {
    int used[9];
    for (int i = 0; i < r->nIng; ++i) used[i] = 0;
    int remaining = r->nIng;
    for (int i = 0; i < CR_GRID; ++i) {          /* i = column/y (inv.getHeight()) */
        for (int j = 0; j < CR_GRID; ++j) {      /* j = row/x    (inv.getWidth())  */
            CRStack g = grid[j + i * CR_GRID];   /* getStackInRowAndColumn(j, i) */
            if (!cr_isEmpty(g)) {
                int flag = 0;
                for (int z = 0; z < r->nIng; ++z) {
                    if (used[z]) continue;
                    CRStack ig = r->ing[z];
                    if (g.item == ig.item && (ig.meta == CR_WILDCARD || g.meta == ig.meta)) {
                        flag = 1; used[z] = 1; --remaining; break;
                    }
                }
                if (!flag) return 0;
            }
        }
    }
    return remaining == 0;
}

/* IRecipe.matches dispatch. */
MC_HD static inline int cr_matches(const CRRecipe *r, const CRStack *grid) {
    return r->shaped ? cr_shapedMatches(r, grid) : cr_shapelessMatches(r, grid);
}

/* CraftingManager.findMatchingRecipe: first matching recipe (registration order) -> result COPY;
 * no match -> sentinel (item = 0xffffffff, count 0, meta 0) standing in for ItemStack.EMPTY. */
MC_HD static inline CRStack cr_findMatching(const CRRecipe *recipes, int n, const CRStack *grid) {
    for (int i = 0; i < n; ++i) {
        if (cr_matches(&recipes[i], grid)) {
            return recipes[i].output;   /* getCraftingResult: copy of recipeOutput */
        }
    }
    return cr_mk((i32)0xffffffff, 0, 0);
}

/* ===== the subset registry, in EXACT vanilla registration order =====
 * order source = CraftingManager constructor call sequence, restricted to the chosen subset:
 *   RecipesTools.addRecipes  (loop i over {PLANKS,COBBLESTONE}, j over {pickaxe,shovel,axe,hoe})
 *   RecipesWeapons.addRecipes(loop i over {PLANKS,COBBLESTONE}, j over {sword})
 *   RecipesCrafting.addRecipes(chest, furnace, crafting_table)  [skipping its other rows]
 *   CraftingManager inline: planks(oak), sticks, torch(coal), torch(charcoal), flint_and_steel
 * CUT (documented): iron/diamond/gold tools+swords, shears, bow/arrow, all RecipesIngots/RecipesFood,
 * every other RecipesCrafting/inline row, and the banner/firework/armor-dye/map/shulker/tipped-arrow
 * special IRecipe classes. */

/* shaped builders (ingredient count is always 1; meta per vanilla source). */
MC_HD static inline CRStack cr_blk(i32 id) { return cr_mk(id, 1, CR_WILDCARD); }  /* Block ingredient */
MC_HD static inline CRStack cr_it(i32 id)  { return cr_mk(id, 1, 0); }            /* Item ingredient  */
MC_HD static inline CRStack cr_is(i32 id, i32 meta) { return cr_mk(id, 1, meta); }/* explicit ItemStack */

#define CR_NRECIPES 18

MC_HD static inline int cr_build(CRRecipe *R) {
    CRStack E = cr_empty();
    CRStack P = cr_blk(IT_PLANKS);       /* X / # planks ingredient (Block -> wildcard) */
    CRStack C = cr_blk(IT_COBBLESTONE);  /* X / # cobblestone ingredient (Block -> wildcard) */
    CRStack S = cr_it(IT_STICK);         /* # stick ingredient (Item -> meta 0) */
    int n = 0;

    /* --- RecipesTools: pickaxe XXX/ # / # ; shovel X/#/# ; axe XX/X#/ # ; hoe XX/ #/ # --- */
    /* i=0 X=PLANKS */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9; R[n].output=cr_it(IT_WOODEN_PICKAXE);
    { CRStack a[9]={P,P,P, E,S,E, E,S,E}; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3; R[n].output=cr_it(IT_WOODEN_SHOVEL);
    { CRStack a[3]={P,S,S}; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6; R[n].output=cr_it(IT_WOODEN_AXE);
    { CRStack a[6]={P,P, P,S, E,S}; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6; R[n].output=cr_it(IT_WOODEN_HOE);
    { CRStack a[6]={P,P, E,S, E,S}; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    /* i=1 X=COBBLESTONE */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9; R[n].output=cr_it(IT_STONE_PICKAXE);
    { CRStack a[9]={C,C,C, E,S,E, E,S,E}; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3; R[n].output=cr_it(IT_STONE_SHOVEL);
    { CRStack a[3]={C,S,S}; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6; R[n].output=cr_it(IT_STONE_AXE);
    { CRStack a[6]={C,C, C,S, E,S}; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=3; R[n].nIng=6; R[n].output=cr_it(IT_STONE_HOE);
    { CRStack a[6]={C,C, E,S, E,S}; for(int q=0;q<6;++q)R[n].ing[q]=a[q]; } ++n;

    /* --- RecipesWeapons: sword X/X/# --- */
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3; R[n].output=cr_it(IT_WOODEN_SWORD);
    { CRStack a[3]={P,P,S}; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=1; R[n].height=3; R[n].nIng=3; R[n].output=cr_it(IT_STONE_SWORD);
    { CRStack a[3]={C,C,S}; for(int q=0;q<3;++q)R[n].ing[q]=a[q]; } ++n;

    /* --- RecipesCrafting: chest ###/# #/### planks ; furnace ###/# #/### cobble ; table ##/## planks --- */
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9; R[n].output=cr_it(IT_CHEST);
    { CRStack a[9]={P,P,P, P,E,P, P,P,P}; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=3; R[n].height=3; R[n].nIng=9; R[n].output=cr_it(IT_FURNACE);
    { CRStack a[9]={C,C,C, C,E,C, C,C,C}; for(int q=0;q<9;++q)R[n].ing[q]=a[q]; } ++n;
    R[n].shaped=1; R[n].width=2; R[n].height=2; R[n].nIng=4; R[n].output=cr_it(IT_CRAFTING_TABLE);
    { CRStack a[4]={P,P, P,P}; for(int q=0;q<4;++q)R[n].ing[q]=a[q]; } ++n;

    /* --- CraftingManager inline --- */
    /* planks: "#" with # = new ItemStack(LOG,1,OAK=0) (explicit meta 0, NOT wildcard) -> 4 planks */
    R[n].shaped=1; R[n].width=1; R[n].height=1; R[n].nIng=1; R[n].output=cr_mk(IT_PLANKS,4,0);
    { R[n].ing[0]=cr_is(IT_LOG,0); } ++n;
    /* sticks: "#"/"#" planks -> 4 sticks */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2; R[n].output=cr_mk(IT_STICK,4,0);
    { CRStack a[2]={P,P}; for(int q=0;q<2;++q)R[n].ing[q]=a[q]; } ++n;
    /* torch (coal): "X"/"#" X=Items.COAL(meta0) #=Items.STICK -> 4 torch */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2; R[n].output=cr_mk(IT_TORCH,4,0);
    { CRStack a[2]={cr_it(IT_COAL),S}; for(int q=0;q<2;++q)R[n].ing[q]=a[q]; } ++n;
    /* torch (charcoal): "X"/"#" X=new ItemStack(COAL,1,1) #=STICK -> 4 torch */
    R[n].shaped=1; R[n].width=1; R[n].height=2; R[n].nIng=2; R[n].output=cr_mk(IT_TORCH,4,0);
    { CRStack a[2]={cr_is(IT_COAL,1),S}; for(int q=0;q<2;++q)R[n].ing[q]=a[q]; } ++n;

    /* flint_and_steel: SHAPELESS {iron_ingot, flint} -> 1 flint_and_steel */
    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng=2; R[n].output=cr_it(IT_FLINT_AND_STEEL);
    { R[n].ing[0]=cr_it(IT_IRON_INGOT); R[n].ing[1]=cr_it(IT_FLINT); } ++n;

    return n;   /* == CR_NRECIPES */
}

/* ===== fixed deterministic test BATTERY (hardcoded 3x3 grids; slot = x + y*3) ===== */
#define CR_NTESTS 28

MC_HD static inline void cr_battery(CRStack out[CR_NTESTS][9]) {
    CRStack E  = cr_empty();
    CRStack P  = cr_mk(IT_PLANKS, 1, 0);
    CRStack C  = cr_mk(IT_COBBLESTONE, 1, 0);
    CRStack S  = cr_mk(IT_STICK, 1, 0);
    CRStack L0 = cr_mk(IT_LOG, 1, 0);    /* oak log */
    CRStack L1 = cr_mk(IT_LOG, 1, 1);    /* spruce log (wrong meta for the oak-only planks recipe) */
    CRStack CO = cr_mk(IT_COAL, 1, 0);   /* coal */
    CRStack CH = cr_mk(IT_COAL, 1, 1);   /* charcoal */
    CRStack IR = cr_mk(IT_IRON_INGOT, 1, 0);
    CRStack FL = cr_mk(IT_FLINT, 1, 0);
    CRStack b[CR_NTESTS][9] = {
        /* G0  wooden_pickaxe (correct)            */ { P,P,P, E,S,E, E,S,E },
        /* G1  pickaxe family NON-match (1 stick)  */ { P,P,P, E,S,E, E,E,E },
        /* G2  stone_pickaxe (correct)             */ { C,C,C, E,S,E, E,S,E },
        /* G3  wooden_axe (correct)                */ { P,P,E, P,S,E, E,S,E },
        /* G4  wooden_axe MIRRORED                  */ { P,P,E, S,P,E, S,E,E },
        /* G5  wooden_axe OFFSET (x=1)             */ { E,P,P, E,P,S, E,E,S },
        /* G6  wooden_hoe (correct)                */ { P,P,E, E,S,E, E,S,E },
        /* G7  wooden_sword (correct, col0)        */ { P,E,E, P,E,E, S,E,E },
        /* G8  stone_sword OFFSET (col1)           */ { E,C,E, E,C,E, E,S,E },
        /* G9  chest (correct)                     */ { P,P,P, P,E,P, P,P,P },
        /* G10 furnace (correct)                   */ { C,C,C, C,E,C, C,C,C },
        /* G11 furnace family NON-match (1 plank)  */ { C,C,C, C,E,C, C,C,P },
        /* G12 crafting_table (correct, off 0,0)   */ { P,P,E, P,P,E, E,E,E },
        /* G13 crafting_table OFFSET (1,1)         */ { E,E,E, E,P,P, E,P,P },
        /* G14 planks (oak log, center)            */ { E,E,E, E,L0,E, E,E,E },
        /* G15 planks NON-match (wrong log meta)   */ { E,E,E, E,L1,E, E,E,E },
        /* G16 sticks (correct)                    */ { P,E,E, P,E,E, E,E,E },
        /* G17 torch (coal)                        */ { CO,E,E, S,E,E, E,E,E },
        /* G18 torch (charcoal, meta-specific)     */ { CH,E,E, S,E,E, E,E,E },
        /* G19 flint_and_steel shapeless (in order)*/ { IR,FL,E, E,E,E, E,E,E },
        /* G20 flint_and_steel SCRAMBLED slots     */ { E,E,E, E,FL,E, E,E,IR },
        /* G21 flint_and_steel NON-match (extra)   */ { IR,FL,S, E,E,E, E,E,E },
        /* G22 empty grid                          */ { E,E,E, E,E,E, E,E,E },
        /* G23 single plank (matches nothing)      */ { P,E,E, E,E,E, E,E,E },
        /* G24 sword family NON-match (plank base) */ { E,C,E, E,C,E, E,P,E },
        /* G25 torch/sticks NON-match (coal+plank) */ { CO,E,E, P,E,E, E,E,E },
        /* G26 wooden_shovel (correct, col0)       */ { P,E,E, S,E,E, S,E,E },
        /* G27 stone_shovel OFFSET (col2)          */ { E,E,C, E,E,S, E,E,S },
    };
    for (int t = 0; t < CR_NTESTS; ++t)
        for (int q = 0; q < 9; ++q) out[t][q] = b[t][q];
}

#endif /* MC_CRAFTING_RECIPES_H */
