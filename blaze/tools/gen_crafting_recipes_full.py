#!/usr/bin/env python3
"""Generate core/crafting_recipes_full.h from KEEP-scope vanilla recipe registration order."""
import os
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

B = dict(
    AIR=0, COBBLESTONE=4, PLANKS=5, GLASS=20, LOG=17, LOG2=162,
    BROWN_MUSHROOM=39, RED_MUSHROOM=40,
    WOOL=35, GOLD_BLOCK=41, IRON_BLOCK=42, LAPIS_BLOCK=22, TORCH=50, CHEST=54, DIAMOND_BLOCK=57,
    CRAFTING_TABLE=58, FURNACE=61, PUMPKIN=86, MELON_BLOCK=103,
    EMERALD_BLOCK=133, REDSTONE_BLOCK=152, SLIME_BLOCK=165, HAY_BLOCK=170,
    COAL_BLOCK=173,
)
I = dict(
    AIR=0, IRON_SHOVEL=256, IRON_PICKAXE=257, IRON_AXE=258, FLINT_AND_STEEL=259,
    BOW=261, ARROW=262, COAL=263, DIAMOND=264, IRON_INGOT=265, GOLD_INGOT=266,
    IRON_SWORD=267, WOODEN_SWORD=268, WOODEN_SHOVEL=269, WOODEN_PICKAXE=270,
    WOODEN_AXE=271, STONE_SWORD=272, STONE_SHOVEL=273, STONE_PICKAXE=274,
    STONE_AXE=275, DIAMOND_SWORD=276, DIAMOND_SHOVEL=277, DIAMOND_PICKAXE=278,
    DIAMOND_AXE=279, STICK=280, BOWL=281, MUSHROOM_STEW=282, GOLDEN_SWORD=283,
    GOLDEN_SHOVEL=284, GOLDEN_PICKAXE=285, GOLDEN_AXE=286, STRING=287,
    FEATHER=288, WOODEN_HOE=290, STONE_HOE=291, IRON_HOE=292, DIAMOND_HOE=293,
    GOLDEN_HOE=294, WHEAT=296, LEATHER_HELMET=298, LEATHER_CHESTPLATE=299,
    LEATHER_LEGGINGS=300, LEATHER_BOOTS=301, IRON_HELMET=306, IRON_CHESTPLATE=307,
    IRON_LEGGINGS=308, IRON_BOOTS=309, DIAMOND_HELMET=310, DIAMOND_CHESTPLATE=311,
    DIAMOND_LEGGINGS=312, DIAMOND_BOOTS=313, GOLDEN_HELMET=314, GOLDEN_CHESTPLATE=315,
    GOLDEN_LEGGINGS=316, GOLDEN_BOOTS=317, FLINT=318, REDSTONE=331, LEATHER=334,
    GLOWSTONE_DUST=348, SUGAR=353, COOKIE=357, SHEARS=359, MELON=360,
    PUMPKIN_SEEDS=361, MELON_SEEDS=362, BLAZE_ROD=369, GOLD_NUGGET=371,
    GLASS_BOTTLE=374,
    SPIDER_EYE=375, FERMENTED_SPIDER_EYE=376, BLAZE_POWDER=377, MAGMA_CREAM=378,
    BREWING_STAND=379, SPECKLED_MELON=382, EMERALD=388, CARROT=391,
    BAKED_POTATO=393, GOLDEN_CARROT=396, EGG=344, PUMPKIN_PIE=400,
    COOKED_RABBIT=412, RABBIT_STEW=413, BEETROOT=434, BEETROOT_SOUP=436,
    SPECTRAL_ARROW=439, DYE=351, SLIME_BALL=341, IRON_NUGGET=452,
    BUCKET=325, BED=355, ENDER_PEARL=368, ENDER_EYE=381,
)
W = 32767


def stk(item, count=1, meta=0, block=False, explicit=False):
    d = dict(item=item, count=count, meta=W if block else meta, is_block=block)
    if explicit or (not block and meta != 0):
        d['explicit'] = True
    return d


def fmt_ing(s):
    if s['item'] == I['AIR']:
        return 'crf_empty()'
    if s.get('is_block'):
        return f"crf_blk({s['item']})"
    if s.get('explicit'):
        return f"crf_is({s['item']},{s['meta']})"
    return f"crf_it({s['item']})"


def emit_c_build():
    lines = []
    n = 0

    def shaped(out, pattern_rows, symbols):
        nonlocal n
        h = len(pattern_rows)
        w = len(pattern_rows[0])
        ing = []
        for row in pattern_rows:
            for ch in row:
                ing.append(symbols.get(ch, stk(I['AIR'])))
        oc = out['item']
        cnt = out.get('count', 1)
        meta = out.get('meta', 0)
        lines.append(f"    R[n].shaped=1; R[n].width={w}; R[n].height={h}; R[n].nIng={w*h};")
        lines.append(f"    R[n].output=crf_mk({oc},{cnt},{meta});")
        lines.append(f"    {{ CRStack a[{w*h}]={{")
        lines.append(',\n'.join('        ' + fmt_ing(s) for s in ing))
        lines.append(f"    }}; for(int q=0;q<{w*h};++q)R[n].ing[q]=a[q]; }} ++n;")
        n += 1

    def shapeless(out, ings):
        nonlocal n
        oc = out['item']
        cnt = out.get('count', 1)
        meta = out.get('meta', 0)
        lines.append(f"    R[n].shaped=0; R[n].width=0; R[n].height=0; R[n].nIng={len(ings)};")
        lines.append(f"    R[n].output=crf_mk({oc},{cnt},{meta});")
        for j, s in enumerate(ings):
            lines.append(f"    R[n].ing[{j}]={fmt_ing(s)};")
        lines.append("    ++n;")
        n += 1

    tool_pats = [
        ["XXX", " # ", " # "], ["X", "#", "#"], ["XX", "X#", " #"], ["XX", " #", " #"]
    ]
    mats = [
        (stk(B['PLANKS'], block=True), I['WOODEN_PICKAXE'], I['WOODEN_SHOVEL'], I['WOODEN_AXE'], I['WOODEN_HOE']),
        (stk(B['COBBLESTONE'], block=True), I['STONE_PICKAXE'], I['STONE_SHOVEL'], I['STONE_AXE'], I['STONE_HOE']),
        (stk(I['IRON_INGOT']), I['IRON_PICKAXE'], I['IRON_SHOVEL'], I['IRON_AXE'], I['IRON_HOE']),
        (stk(I['DIAMOND']), I['DIAMOND_PICKAXE'], I['DIAMOND_SHOVEL'], I['DIAMOND_AXE'], I['DIAMOND_HOE']),
        (stk(I['GOLD_INGOT']), I['GOLDEN_PICKAXE'], I['GOLDEN_SHOVEL'], I['GOLDEN_AXE'], I['GOLDEN_HOE']),
    ]
    S = stk(I['STICK'])
    E = stk(I['AIR'])
    for mat, pk, sh, ax, ho in mats:
        X = mat
        for pat, out_id in zip(tool_pats, [pk, sh, ax, ho]):
            shaped(dict(item=out_id), pat, {'X': X, '#': S, ' ': E})
    shaped(dict(item=I['SHEARS']), [" #", "# "], {'#': stk(I['IRON_INGOT']), ' ': E})

    for X, sw in [
        (stk(B['PLANKS'], block=True), I['WOODEN_SWORD']),
        (stk(B['COBBLESTONE'], block=True), I['STONE_SWORD']),
        (stk(I['IRON_INGOT']), I['IRON_SWORD']),
        (stk(I['DIAMOND']), I['DIAMOND_SWORD']),
        (stk(I['GOLD_INGOT']), I['GOLDEN_SWORD']),
    ]:
        shaped(dict(item=sw), ["X", "X", "#"], {'X': X, '#': S, ' ': E})
    shaped(dict(item=I['BOW']), [" #X", "# X", " #X"], {'X': stk(I['STRING']), '#': S, ' ': E})
    shaped(dict(item=I['ARROW'], count=4), ["X", "#", "Y"],
            {'X': stk(I['FLINT']), '#': S, 'Y': stk(I['FEATHER']), ' ': E})
    shaped(dict(item=I['SPECTRAL_ARROW'], count=2), [" # ", "#X#", " # "],
            {'X': stk(I['ARROW']), '#': stk(I['GLOWSTONE_DUST']), ' ': E})

    for blk, it, cnt, meta in [
        (B['GOLD_BLOCK'], I['GOLD_INGOT'], 9, 0),
        (B['IRON_BLOCK'], I['IRON_INGOT'], 9, 0),
        (B['DIAMOND_BLOCK'], I['DIAMOND'], 9, 0),
        (B['EMERALD_BLOCK'], I['EMERALD'], 9, 0),
        (B['LAPIS_BLOCK'], I['DYE'], 9, 4),
        (B['REDSTONE_BLOCK'], I['REDSTONE'], 9, 0),
        (B['COAL_BLOCK'], I['COAL'], 9, 0),
        (B['HAY_BLOCK'], I['WHEAT'], 9, 0),
        (B['SLIME_BLOCK'], I['SLIME_BALL'], 9, 0),
    ]:
        ing = stk(it, meta=meta, explicit=(meta != 0))
        shaped(dict(item=blk), ["###", "###", "###"], {'#': ing, ' ': E})
        shaped(dict(item=it, count=cnt, meta=meta), ["#"], {'#': stk(blk, block=True), ' ': E})
    shaped(dict(item=I['GOLD_INGOT']), ["###", "###", "###"], {'#': stk(I['GOLD_NUGGET']), ' ': E})
    shaped(dict(item=I['GOLD_NUGGET'], count=9), ["#"], {'#': stk(I['GOLD_INGOT']), ' ': E})
    shaped(dict(item=I['IRON_INGOT']), ["###", "###", "###"], {'#': stk(I['IRON_NUGGET']), ' ': E})
    shaped(dict(item=I['IRON_NUGGET'], count=9), ["#"], {'#': stk(I['IRON_INGOT']), ' ': E})

    shapeless(dict(item=I['MUSHROOM_STEW']), [
        stk(B['BROWN_MUSHROOM'], block=True), stk(B['RED_MUSHROOM'], block=True), stk(I['BOWL'])])
    shaped(dict(item=I['COOKIE'], count=8), ["#X#"],
            {'#': stk(I['WHEAT']), 'X': stk(I['DYE'], meta=3, explicit=True), ' ': E})
    shaped(dict(item=I['RABBIT_STEW']), [" R ", "CPM", " B "], {
        'R': stk(I['COOKED_RABBIT']), 'C': stk(I['CARROT']), 'P': stk(I['BAKED_POTATO']),
        'M': stk(B['BROWN_MUSHROOM'], block=True), 'B': stk(I['BOWL']), ' ': E})
    shaped(dict(item=I['RABBIT_STEW']), [" R ", "CPD", " B "], {
        'R': stk(I['COOKED_RABBIT']), 'C': stk(I['CARROT']), 'P': stk(I['BAKED_POTATO']),
        'D': stk(B['RED_MUSHROOM'], block=True), 'B': stk(I['BOWL']), ' ': E})
    shaped(dict(item=B['MELON_BLOCK']), ["MMM", "MMM", "MMM"], {'M': stk(I['MELON']), ' ': E})
    shaped(dict(item=I['BEETROOT_SOUP']), ["OOO", "OOO", " B "],
            {'O': stk(I['BEETROOT']), 'B': stk(I['BOWL']), ' ': E})
    shaped(dict(item=I['MELON_SEEDS']), ["M"], {'M': stk(I['MELON']), ' ': E})
    shaped(dict(item=I['PUMPKIN_SEEDS'], count=4), ["M"], {'M': stk(B['PUMPKIN'], block=True), ' ': E})
    shapeless(dict(item=I['PUMPKIN_PIE']), [stk(B['PUMPKIN'], block=True), stk(I['SUGAR']), stk(I['EGG'])])
    shapeless(dict(item=I['FERMENTED_SPIDER_EYE']),
              [stk(I['SPIDER_EYE']), stk(B['BROWN_MUSHROOM'], block=True), stk(I['SUGAR'])])
    shapeless(dict(item=I['BLAZE_POWDER'], count=2), [stk(I['BLAZE_ROD'])])
    shapeless(dict(item=I['MAGMA_CREAM']), [stk(I['BLAZE_POWDER']), stk(I['SLIME_BALL'])])

    P = stk(B['PLANKS'], block=True)
    C = stk(B['COBBLESTONE'], block=True)
    shaped(dict(item=B['CHEST']), ["###", "# #", "###"], {'#': P, ' ': E})
    shaped(dict(item=B['FURNACE']), ["###", "# #", "###"], {'#': C, ' ': E})
    shaped(dict(item=B['CRAFTING_TABLE']), ["##", "##"], {'#': P, ' ': E})

    armor_pats = [["XXX", "X X"], ["X X", "XXX", "XXX"], ["XXX", "X X", "X X"], ["X X", "X X"]]
    for mat, hm, ch, lg, bt in [
        (stk(I['LEATHER']), I['LEATHER_HELMET'], I['LEATHER_CHESTPLATE'], I['LEATHER_LEGGINGS'], I['LEATHER_BOOTS']),
        (stk(I['IRON_INGOT']), I['IRON_HELMET'], I['IRON_CHESTPLATE'], I['IRON_LEGGINGS'], I['IRON_BOOTS']),
        (stk(I['DIAMOND']), I['DIAMOND_HELMET'], I['DIAMOND_CHESTPLATE'], I['DIAMOND_LEGGINGS'], I['DIAMOND_BOOTS']),
        (stk(I['GOLD_INGOT']), I['GOLDEN_HELMET'], I['GOLDEN_CHESTPLATE'], I['GOLDEN_LEGGINGS'], I['GOLDEN_BOOTS']),
    ]:
        X = mat
        for pat, oid in zip(armor_pats, [hm, ch, lg, bt]):
            shaped(dict(item=oid), pat, {'X': X, ' ': E})

    # Per-species planks, vanilla registration order (CraftingManager.java:117-122):
    # LOG 17 metas 0-3 -> planks metas 0-3, LOG2 162 metas 0-1 -> planks metas 4-5.
    for src, src_meta, out_meta in [
        (B['LOG'], 0, 0), (B['LOG'], 1, 1), (B['LOG'], 2, 2), (B['LOG'], 3, 3),
        (B['LOG2'], 0, 4), (B['LOG2'], 1, 5),
    ]:
        shaped(dict(item=B['PLANKS'], count=4, meta=out_meta), ["#"],
                {'#': stk(src, meta=src_meta, explicit=True), ' ': E})
    shaped(dict(item=I['STICK'], count=4), ["#", "#"], {'#': P, ' ': E})
    shaped(dict(item=B['TORCH'], count=4), ["X", "#"], {'X': stk(I['COAL']), '#': S, ' ': E})
    shaped(dict(item=B['TORCH'], count=4), ["X", "#"],
            {'X': stk(I['COAL'], meta=1, explicit=True), '#': S, ' ': E})
    shapeless(dict(item=I['FLINT_AND_STEEL']), [stk(I['IRON_INGOT']), stk(I['FLINT'])])

    # Route-critical End-run recipes (vanilla CraftingManager.java:146,189,193):
    # bucket, bed, eye of ender. Appended after the KEEP set; their grids are
    # disjoint from every KEEP recipe, so first-match order is unaffected.
    shaped(dict(item=I['BUCKET']), ["# #", " # "], {'#': stk(I['IRON_INGOT']), ' ': E})
    shaped(dict(item=I['BED']), ["###", "XXX"],
            {'#': stk(B['WOOL'], block=True), 'X': stk(B['PLANKS'], block=True), ' ': E})
    shapeless(dict(item=I['ENDER_EYE']), [stk(I['ENDER_PEARL']), stk(I['BLAZE_POWDER'])])

    # Route-critical brewing recipes (vanilla CraftingManager.java:127,138,177-178).
    # Appended after the KEEP set; their exact grids are disjoint from every
    # registered recipe, so the first-match result is unchanged.
    shaped(dict(item=I['GLASS_BOTTLE'], count=3), ["# #", " # "],
            {'#': stk(B['GLASS'], block=True), ' ': E})
    shaped(dict(item=I['BREWING_STAND']), [" B ", "###"],
            {'B': stk(I['BLAZE_ROD']), '#': stk(B['COBBLESTONE'], block=True), ' ': E})
    shaped(dict(item=I['GOLDEN_CARROT']), ["###", "#X#", "###"],
            {'#': stk(I['GOLD_NUGGET']), 'X': stk(I['CARROT']), ' ': E})
    shaped(dict(item=I['SPECKLED_MELON']), ["###", "#X#", "###"],
            {'#': stk(I['GOLD_NUGGET']), 'X': stk(I['MELON']), ' ': E})

    return '\n'.join(lines), n


MATCHER = r'''
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
'''

BATTERY = [
    ("wooden_pickaxe", "P,P,P,E,S,E,E,S,E"),
    ("pickaxe_nonmatch", "P,P,P,E,S,E,E,E,E"),
    ("stone_pickaxe", "C,C,C,E,S,E,E,S,E"),
    ("wooden_axe", "P,P,E,P,S,E,E,S,E"),
    ("wooden_axe_mirror", "P,P,E,S,P,E,S,E,E"),
    ("wooden_axe_offset", "E,P,P,E,P,S,E,E,S"),
    ("wooden_hoe", "P,P,E,E,S,E,E,S,E"),
    ("wooden_sword", "P,E,E,P,E,E,S,E,E"),
    ("stone_sword_offset", "E,C,E,E,C,E,E,S,E"),
    ("chest", "P,P,P,P,E,P,P,P,P"),
    ("furnace", "C,C,C,C,E,C,C,C,C"),
    ("furnace_nonmatch", "C,C,C,C,E,C,C,C,P"),
    ("crafting_table", "P,P,E,P,P,E,E,E,E"),
    ("crafting_table_offset", "E,E,E,E,P,P,E,P,P"),
    ("planks_oak", "E,E,E,E,L0,E,E,E,E"),
    ("planks_spruce", "E,E,E,E,L1,E,E,E,E"),
    ("planks_acacia", "E,E,E,E,LA,E,E,E,E"),
    ("sticks", "P,E,E,P,E,E,E,E,E"),
    ("torch_coal", "CO,E,E,S,E,E,E,E,E"),
    ("torch_charcoal", "CH,E,E,S,E,E,E,E,E"),
    ("flint_steel", "IR,FL,E,E,E,E,E,E,E"),
    ("flint_steel_scrambled", "E,E,E,E,FL,E,E,E,IR"),
    ("flint_steel_extra", "IR,FL,S,E,E,E,E,E,E"),
    ("empty", "E,E,E,E,E,E,E,E,E"),
    ("single_plank", "P,E,E,E,E,E,E,E,E"),
    ("sword_nonmatch", "E,C,E,E,C,E,E,P,E"),
    ("torch_nonmatch", "CO,E,E,P,E,E,E,E,E"),
    ("wooden_shovel", "P,E,E,S,E,E,S,E,E"),
    ("stone_shovel_offset", "E,E,C,E,E,S,E,E,S"),
    ("iron_pickaxe", "IR,IR,IR,E,S,E,E,S,E"),
    ("diamond_sword", "D,E,E,D,E,E,S,E,E"),
    ("golden_hoe", "G,G,E,E,S,E,E,S,E"),
    ("shears", "E,IR,E,IR,E,E,E,E,E"),
    ("bow", "E,S,ST,E,S,ST,E,S,ST"),
    ("arrow", "FL,E,E,S,E,E,FE,E,E"),
    ("spectral_arrow", "E,GD,E,GD,A,GD,E,GD,E"),
    ("gold_block", "G,G,G,G,G,G,G,G,G"),
    ("iron_ingot_from_block", "E,E,E,E,IB,E,E,E,E"),
    ("mushroom_stew", "BM,E,E,E,RM,E,E,BW,E"),
    ("cookie", "W,DY3,W,E,E,E,E,E,E"),
    ("iron_helmet", "IR,IR,IR,IR,E,IR,E,E,E"),
    ("diamond_chestplate", "D,E,D,D,D,D,D,D,D"),
    ("leather_boots", "L,E,L,L,E,L,E,E,E"),
    ("blaze_powder", "BR,E,E,E,E,E,E,E,E"),
    ("pumpkin_pie", "PK,E,E,E,SG,E,E,EG,E"),
    ("iron_nonmatch", "IR,IR,IR,IR,IR,IR,E,S,E"),
    ("bucket", "IR,E,IR,E,IR,E,E,E,E"),
    ("bed_wool_meta14", "WO,WO,WO,P,P,P,E,E,E"),
    ("ender_eye_scrambled", "E,E,EP,E,E,E,BP,E,E"),
    ("glass_bottles", "GL,E,GL,E,GL,E,E,E,E"),
    ("glass_bottles_nonmatch", "GL,E,GL,E,E,E,E,E,E"),
    ("brewing_stand", "E,BR,E,C,C,C,E,E,E"),
    ("golden_carrot", "GN,GN,GN,GN,CA,GN,GN,GN,GN"),
    ("speckled_melon", "GN,GN,GN,GN,ME,GN,GN,GN,GN"),
]

SYM = {
    'E': 'crf_empty()', 'P': 'crf_mk(5,1,0)', 'C': 'crf_mk(4,1,0)', 'S': 'crf_mk(280,1,0)',
    'L0': 'crf_mk(17,1,0)', 'L1': 'crf_mk(17,1,1)', 'LA': 'crf_mk(162,1,0)',
    'CO': 'crf_mk(263,1,0)', 'CH': 'crf_mk(263,1,1)',
    'IR': 'crf_mk(265,1,0)', 'FL': 'crf_mk(318,1,0)', 'D': 'crf_mk(264,1,0)', 'G': 'crf_mk(266,1,0)',
    'GD': 'crf_mk(348,1,0)', 'A': 'crf_mk(262,1,0)', 'FE': 'crf_mk(288,1,0)', 'IB': 'crf_mk(42,1,0)',
    'BM': 'crf_mk(39,1,0)', 'RM': 'crf_mk(40,1,0)', 'BW': 'crf_mk(281,1,0)', 'W': 'crf_mk(296,1,0)',
    'DY3': 'crf_mk(351,1,3)', 'L': 'crf_mk(334,1,0)', 'BR': 'crf_mk(369,1,0)', 'PK': 'crf_mk(86,1,0)',
    'SG': 'crf_mk(353,1,0)', 'EG': 'crf_mk(344,1,0)', 'ST': 'crf_mk(287,1,0)',
    'WO': 'crf_mk(35,1,14)', 'EP': 'crf_mk(368,1,0)', 'BP': 'crf_mk(377,1,0)',
    'GL': 'crf_mk(20,1,0)', 'GN': 'crf_mk(371,1,0)',
    'CA': 'crf_mk(391,1,0)', 'ME': 'crf_mk(360,1,0)',
}


def emit_battery():
    lines = ['MC_HD static inline void crf_battery(CRStack out[CRF_NTESTS][9]) {']
    lines.append('    CRStack b[CRF_NTESTS][9] = {')
    for name, slots in BATTERY:
        cells = [SYM[x] for x in slots.split(',')]
        lines.append('        /* ' + name + ' */ { ' + ', '.join(cells) + ' },')
    lines.append('    };')
    lines.append('    for (int t = 0; t < CRF_NTESTS; ++t) for (int q = 0; q < 9; ++q) out[t][q] = b[t][q];')
    lines.append('}')
    return '\n'.join(lines)


def main():
    build_body, nrec = emit_c_build()
    nrec_actual = build_body.count('++n;')
    header = f'''/* crafting_recipes_full: MC 1.11.2 CraftingManager KEEP-scope recipe set (generated + hand header).
 * Matcher = verbatim ShapedRecipes/ShapelessRecipes/findMatchingRecipe (same as crafting_recipes.h).
 * Registry = RecipesTools/Weapons/Ingots/Food + RecipesCrafting(chest,furnace,table) + RecipesArmor +
 * inline planks/sticks/torch/flint_and_steel plus route-critical End/brewing recipes. {nrec_actual} recipes.
 * CUT: RecipesDyes, special IRecipes, CraftingManager decorative/redstone inline rows. */
#ifndef MC_CRAFTING_RECIPES_FULL_H
#define MC_CRAFTING_RECIPES_FULL_H
#include "mc.h"
{MATCHER}
#define CRF_NRECIPES {nrec_actual}
#define CRF_NTESTS {len(BATTERY)}
MC_HD static inline int crf_build(CRRecipe *R) {{
    int n = 0;
{build_body}
    return n;
}}
{emit_battery()}
#endif
'''
    path = os.path.join(ROOT, 'core', 'crafting_recipes_full.h')
    with open(path, 'w') as f:
        f.write(header)
    print(f'wrote {path}  recipes={nrec_actual} tests={len(BATTERY)}')


if __name__ == '__main__':
    main()
