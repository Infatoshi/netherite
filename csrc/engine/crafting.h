/* Minecraft 1.7.10 crafting: CraftingManager.findMatchingRecipe, and the
 * IRecipe classes the recipe list holds.
 *
 * The registry itself is generated (recipes.h) from the oracle's live objects:
 * RECIPES holds every entry of CraftingManager.getRecipeList() in list order,
 * which is the order findMatchingRecipe walks, and RECIPE_ITEMS holds the
 * ingredient stacks. The shaped and shapeless matches, the mirror rule, the
 * wildcard damage 32767 and the tool-repair path live here; so do the five
 * special recipe classes the list holds (armor dyes, book cloning, map cloning,
 * map extending, fireworks) because their matching is code, not a table.
 *
 * The grid is an InventoryCrafting: a side*side ItemStack array read either by
 * slot index or by row and column, where a column at or past the width reads
 * null (Java's getStackInRowAndColumn checks the width, not the array length).
 *
 * Where vanilla throws, this returns the exception class name instead of
 * pretending a match: the probe passes a null World, so RecipesMapExtending's
 * MapData read is a NullPointerException, and a dye whose damage is outside 0
 * to 15 indexes ItemDye's colour array out of bounds in RecipeFireworks. */
#ifndef NETHERITE_CRAFTING_H
#define NETHERITE_CRAFTING_H

#include "itemtag.h"
#include "nbtjson.h"
#include "recipes.h"

#define CRAFT_MAX_SLOTS 9

/* One ItemStack as findMatchingRecipe sees it: item is a registry id, or -1 for
 * Java's null slot. tag is the stack's tag compound in the item tag store
 * (itemtag.h): 0 when the stack has no tag, which is hasTagCompound false; an
 * empty compound is a different thing and is kept. */
struct craft_stack {
    int item;
    int count;
    int damage;
    int tag;
};

/* An InventoryCrafting: side*side slots in row-major order, the order
 * getStackInSlot walks. side is also the width the row-and-column accessors
 * use. */
struct craft_grid {
    int side;
    struct craft_stack slot[CRAFT_MAX_SLOTS];
};

/* What findMatchingRecipe needs from the World argument. Only
 * RecipesMapExtending reads anything from it: the filled map's MapData scale.
 * Java's call dereferences the world there, so a NULL world is a null
 * dereference; with a world, map_data reports whether that damage's MapData
 * exists and, if it does, its scale. The native world has no map storage yet,
 * so its hook will answer 0 (no data) until the map layer lands. */
struct craft_world {
    int (*map_data)(void *ctx, int item_damage, int *scale);
    void *ctx;
};

enum craft_status {
    CRAFT_NONE,   /* no recipe matched: findMatchingRecipe returns null */
    CRAFT_STACK,  /* out holds the result */
    CRAFT_THROW   /* vanilla throws; threw names the exception class */
};

struct craft_result {
    enum craft_status status;
    struct craft_stack out;   /* CRAFT_STACK: owned, free with craft_stack_free */
    const char *threw;        /* CRAFT_THROW: the Java exception simple name */
};

/* An empty grid of the given side (2 for the player grid, 3 for a workbench). */
void craft_grid_clear(struct craft_grid *g, int side);

/* Java's stack == null. */
int craft_slot_empty(const struct craft_stack *s);

/* Deep copy: a tag is copied through its canonical text, like ItemStack.copy. */
void craft_stack_copy(struct craft_stack *dst, const struct craft_stack *src);

/* Releases dst's tag and leaves it empty. */
void craft_stack_free(struct craft_stack *s);

/* Releases r's tag; the result of one call is good until this runs. */
void craft_result_free(struct craft_result *r);

/* CraftingManager.findMatchingRecipe(grid, world): the two-stack repair path
 * first, then RECIPES in list order, first match wins. */
void craft_find_matching(const struct craft_grid *g, const struct craft_world *w, struct craft_result *r);

/* The ItemArmor material ordinal for an item id (0 is CLOTH, the only dyeable
 * material), or -1 when the item is not an ItemArmor. */
int craft_armor_material(int item);

#endif