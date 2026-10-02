/* Minecraft 1.7.10 crafting: CraftingManager.findMatchingRecipe and the recipe
 * classes it walks. See crafting.h for the shape of the data; every branch here
 * follows one method of oracle/src/item/crafting (or of ShapedRecipes,
 * ItemStack, ItemArmor, BlockColored and ItemDye for the helpers they use), and
 * the comment on each block names it.
 *
 * The one liberty is that a Java exception becomes a status and a class name
 * instead of a crash: the probe passes a null World, so RecipesMapExtending
 * reads through it, and a dye damage outside 0..15 indexes ItemDye's colour
 * array out of bounds. Those are exact: the native test compares the class name
 * with the one the oracle recorded for that grid. */
#include "crafting.h"
#include "itemtag.h"
#include "nbtedit.h"
#include "firework.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ItemArmor.ArmorMaterial.CLOTH: the only material RecipesArmorDyes accepts. */
#define ARMOR_CLOTH 0

/* The wildcard damage an Item or Block argument in addRecipe produced. */
#define WILDCARD 32767

/* ---- stacks ---------------------------------------------------------- */

void craft_grid_clear(struct craft_grid *g, int side)
{
    memset(g, 0, sizeof *g);
    g->side = side;

    for (int i = 0; i < CRAFT_MAX_SLOTS; ++i) g->slot[i].item = -1;
}

int craft_slot_empty(const struct craft_stack *s)
{
    return s->item < 0;
}

/* A copy of a subtree (the canonical text round trip). A tag the canonical
 * text cannot carry back (a string holding a quote, which the format does not
 * escape and vanilla crafting never produces) is refused by the tag store
 * rather than dropped, so a wrong result cannot pass silently. */
static nbt *copy_tag(const nbt *tag)
{
    if (tag == NULL) return NULL;

    char *text = nbt_render(tag);
    nbt *r = nbt_parse(text);

    if (r == NULL)
    {
        fprintf(stderr, "crafting: a tag does not survive its canonical text: %s\n", text);
        abort();
    }

    free(text);
    return r;
}

/* An interned tag's tree, for the recipes that read or build compounds
 * (NULL for no tag; the caller frees it). */
static nbt *tag_tree(int tag)
{
    return itag_tree(tag);
}

/* A built tree interned and released. */
static int tag_own(nbt *tree)
{
    int t = itag_from_tree(tree);
    nbt_free(tree);
    return t;
}

void craft_stack_copy(struct craft_stack *dst, const struct craft_stack *src)
{
    *dst = *src;
}

void craft_stack_free(struct craft_stack *s)
{
    s->tag = 0;
    s->item = -1;
    s->count = 0;
    s->damage = 0;
}

void craft_result_free(struct craft_result *r)
{
    craft_stack_free(&r->out);
    r->status = CRAFT_NONE;
    r->threw = NULL;
}

static void clear_result(struct craft_result *r)
{
    memset(r, 0, sizeof *r);
    r->out.item = -1;
}

static void thrown(struct craft_result *r, const char *what)
{
    craft_stack_free(&r->out);
    r->status = CRAFT_THROW;
    r->threw = what;
}

/* Replaces whatever the result held with a stack, taking the tag. */
static void give(struct craft_result *r, int item, int count, int damage, nbt *tag)
{
    craft_stack_free(&r->out);
    r->status = CRAFT_STACK;
    r->out.item = item;
    r->out.count = count;
    r->out.damage = damage;
    r->out.tag = tag_own(tag);
}

/* comp with one key set, comp consumed. */
static nbt *tag_set(nbt *comp, const char *key, nbt *value)
{
    if (comp == NULL) comp = nbt_new_compound();

    nbt *r = nbt_set_key(comp, key, value);
    nbt_free(comp);
    return r;
}

/* Java's getStackInRowAndColumn: null when the column is outside the width or
 * the row outside the grid, and null for an empty slot. InventoryCrafting is
 * always square here, so the column check and getStackInSlot's bound agree. */
static const struct craft_stack *slot_ptr(const struct craft_grid *g, int x, int y)
{
    if (x < 0 || x >= g->side || y < 0 || y >= g->side) return NULL;

    const struct craft_stack *s = &g->slot[x + y * g->side];
    return craft_slot_empty(s) ? NULL : s;
}

/* The tag's "display" compound, or NULL when there is none. getCompoundTag
 * hands back a detached empty compound for a missing key, which func_150297_b
 * then sees as not a compound, so a reader is unaffected either way. */
static const nbt *display_compound(const nbt *tag)
{
    const nbt *d = tag != NULL ? nbt_get(tag, "display") : NULL;

    return d != NULL && nbt_kind(d) == NBT_COMPOUND ? d : NULL;
}

/* ---- shaped and shapeless -------------------------------------------- */

/* ShapedRecipes.checkMatch: the recipe's width*height region at (ox, oy) of the
 * 3x3 the recipe scans, mirrored when asked. Only the item and, unless the
 * ingredient's damage is the wildcard, the damage are compared. */
static int shaped_check(const struct recipe_def *r, const struct craft_grid *g,
                        int ox, int oy, int mirror)
{
    for (int x = 0; x < 3; ++x)
    {
        for (int y = 0; y < 3; ++y)
        {
            int rx = x - ox, ry = y - oy;
            const struct stack_def *d = NULL;

            if (rx >= 0 && ry >= 0 && rx < r->width && ry < r->height)
                d = &RECIPE_ITEMS[r->items + (mirror ? r->width - rx - 1 + ry * r->width
                                                      : rx + ry * r->width)];

            const struct craft_stack *got = slot_ptr(g, x, y);
            int want_empty = d == NULL || !d->exists;

            if (want_empty && got == NULL) continue;
            if (want_empty != (got == NULL)) return 0;
            if (d->item != got->item) return 0;
            if (d->damage != WILDCARD && d->damage != got->damage) return 0;
        }
    }

    return 1;
}

/* ShapedRecipes.matches: every offset the recipe fits at, the mirror tried
 * first at each offset. The scan is over a 3x3 even for a 2x2 grid, whose third
 * column and row read null. */
static int shaped_matches(const struct recipe_def *r, const struct craft_grid *g)
{
    for (int ox = 0; ox <= 3 - r->width; ++ox)
        for (int oy = 0; oy <= 3 - r->height; ++oy)
            if (shaped_check(r, g, ox, oy, 1) || shaped_check(r, g, ox, oy, 0)) return 1;

    return 0;
}

/* ShapelessRecipes.matches: each grid stack consumes one ingredient of the same
 * item and damage (a wildcard ingredient takes any damage), and every
 * ingredient must be consumed. */
static int shapeless_matches(const struct recipe_def *r, const struct craft_grid *g)
{
    unsigned char used[CRAFT_MAX_SLOTS];
    int left = r->count;

    memset(used, 0, sizeof used);

    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            const struct craft_stack *got = slot_ptr(g, x, y);

            if (got == NULL) continue;

            int found = 0;

            for (int i = 0; i < r->count; ++i)
            {
                const struct stack_def *d = &RECIPE_ITEMS[r->items + i];

                if (used[i] || !d->exists) continue;
                if (d->item != got->item) continue;
                if (d->damage != WILDCARD && d->damage != got->damage) continue;

                used[i] = 1;
                --left;
                found = 1;
                break;
            }

            if (!found) return 0;
        }
    }

    return left == 0;
}

/* ShapedRecipes.getCraftingResult: the recipe's output, plus - for a mirrored
 * recipe - the tag of the last grid stack that carries one. */
static void shaped_result(const struct recipe_def *r, const struct craft_grid *g, struct craft_result *out)
{
    int tag = 0;

    if (r->mirrored)
    {
        for (int i = 0; i < g->side * g->side; ++i)
        {
            const struct craft_stack *s = &g->slot[i];

            if (craft_slot_empty(s) || s->tag == 0) continue;

            tag = s->tag;
        }
    }

    give(out, r->output.item, r->output.count, r->output.damage, NULL);
    out->out.tag = tag;
}

/* ---- the tool repair path -------------------------------------------- */

/* CraftingManager.findMatchingRecipe's first branch: exactly two stacks of the
 * same damageable item at stack size 1 each. Their remaining durability is
 * added, 5% of the maximum is added on top, and the result is what is left. */
static int repair_result(const struct craft_grid *g, struct craft_result *out)
{
    const struct craft_stack *a = NULL, *b = NULL;
    int n = 0;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (craft_slot_empty(s)) continue;
        if (n == 0) a = s;
        if (n == 1) b = s;
        ++n;
    }

    if (n != 2) return 0;
    if (a->item != b->item || a->count != 1 || b->count != 1) return 0;
    if (a->item < 0 || a->item >= (int)(sizeof ITEMS / sizeof ITEMS[0])) return 0;
    if (!ITEMS[a->item].exists) return 0;
    if (!(ITEMS[a->item].max_damage > 0 && !ITEMS[a->item].has_subtypes)) return 0;

    int max = ITEMS[a->item].max_damage;
    int left = max - a->damage;
    int other = max - b->damage;
    int damage = max - (left + other + max * 5 / 100);

    if (damage < 0) damage = 0;

    give(out, a->item, 1, damage, NULL);
    return 1;
}

/* ---- RecipesArmorDyes ------------------------------------------------ */

int craft_armor_material(int item)
{
    for (int i = 0; i < (int)(sizeof CRAFT_ARMOR / sizeof CRAFT_ARMOR[0]); ++i)
        if (CRAFT_ARMOR[i].item == item) return CRAFT_ARMOR[i].material;

    return -1;
}

/* ItemArmor.hasColor, for a CLOTH stack. */
static int armor_has_color(const struct craft_stack *s)
{
    return itag_has_color(s->tag);
}

/* ItemArmor.getColor: the display colour, or the default leather colour. */
static int armor_color(const struct craft_stack *s)
{
    return itag_color(s->tag, 10511680);
}

/* ItemArmor.func_82813_b: put the colour in the stack's display compound,
 * creating the tag and the compound when they are missing, and keeping whatever
 * else the compound holds. 0 when "display" holds a non-compound, where Java's
 * cast throws. */
static int armor_set_color(struct craft_stack *s, int color)
{
    nbt *tree = tag_tree(s->tag);
    int bad = tree != NULL && nbt_get(tree, "display") != NULL && display_compound(tree) == NULL;

    nbt_free(tree);
    if (bad) return 0;

    s->tag = itag_set_color(s->tag, color);
    return 1;
}

/* RecipesArmorDyes.matches: one CLOTH ItemArmor and at least one dye, nothing
 * else. */
static int dye_matches(const struct craft_grid *g)
{
    int armor = 0, dyes = 0;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (craft_slot_empty(s)) continue;

        int material = craft_armor_material(s->item);

        if (material >= 0)
        {
            if (material != ARMOR_CLOTH || armor) return 0;
            armor = 1;
        }
        else
        {
            if (s->item != CRAFT_ITEM_DYE) return 0;
            ++dyes;
        }
    }

    return armor && dyes > 0;
}

/* BlockColored.func_150032_b, the dye damage to fleece index map. The generated
 * CRAFT_DYE_TO_FLEECE holds the 16 normal damages; every other damage (the
 * wildcard among them) is the same ~damage & 15, which the native test checks
 * the table against. */
static int dye_fleece(int damage)
{
    return ~damage & 15;
}

static int max3(int a, int b, int c)
{
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

/* RecipesArmorDyes.getCraftingResult: average the colours of the dyes and of
 * the armor's own colour, then scale the average so its brightest channel keeps
 * the averaged brightness. Returns nothing (status stays CRAFT_NONE, Java's
 * null) when the grid does not hold exactly one dyeable armor. */
static void dye_result(const struct craft_grid *g, struct craft_result *out)
{
    struct craft_stack copy;
    int rgb[3] = {0, 0, 0};
    int total = 0, n = 0, armor = 0;

    memset(&copy, 0, sizeof copy);
    copy.item = -1;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (craft_slot_empty(s)) continue;

        int material = craft_armor_material(s->item);

        if (material >= 0)
        {
            if (material != ARMOR_CLOTH || armor)
            {
                craft_stack_free(&copy);
                return;
            }

            armor = 1;
            craft_stack_copy(&copy, s);
            copy.count = 1;

            if (armor_has_color(s))
            {
                int c = armor_color(s);
                float rf = (float)(c >> 16 & 255) / 255.0f;
                float gf = (float)(c >> 8 & 255) / 255.0f;
                float bf = (float)(c & 255) / 255.0f;

                total = (int)((float)total + fmaxf(rf, fmaxf(gf, bf)) * 255.0f);
                rgb[0] = (int)((float)rgb[0] + rf * 255.0f);
                rgb[1] = (int)((float)rgb[1] + gf * 255.0f);
                rgb[2] = (int)((float)rgb[2] + bf * 255.0f);
                ++n;
            }
        }
        else
        {
            if (s->item != CRAFT_ITEM_DYE)
            {
                craft_stack_free(&copy);
                return;
            }

            const float *fc = CRAFT_FLEECE_COLORS[dye_fleece(s->damage)];
            int r8 = (int)(fc[0] * 255.0f);
            int g8 = (int)(fc[1] * 255.0f);
            int b8 = (int)(fc[2] * 255.0f);

            total += max3(r8, g8, b8);
            rgb[0] += r8;
            rgb[1] += g8;
            rgb[2] += b8;
            ++n;
        }
    }

    if (!armor)
    {
        craft_stack_free(&copy);
        return;
    }

    int r7 = rgb[0] / n, g7 = rgb[1] / n, b7 = rgb[2] / n;
    float avg = (float)total / (float)n;
    float bright = (float)max3(r7, g7, b7);

    /* Java's (int) of a NaN is 0 and C's is undefined; the one division that
     * can be 0/0 is an all-black colour set, which Java turns into black. */
    if (bright == 0.0f)
    {
        r7 = g7 = b7 = 0;
    }
    else
    {
        r7 = (int)((float)r7 * avg / bright);
        g7 = (int)((float)g7 * avg / bright);
        b7 = (int)((float)b7 * avg / bright);
    }

    if (!armor_set_color(&copy, ((r7 << 8) + g7) << 8 | b7))
    {
        craft_stack_free(&copy);
        thrown(out, "ReportedException");
        return;
    }

    out->status = CRAFT_STACK;
    craft_stack_free(&out->out);
    out->out = copy;
}

/* ---- RecipeBookCloning ----------------------------------------------- */

/* RecipeBookCloning.matches: one written book and at least one writable one. */
static int book_matches(const struct craft_grid *g)
{
    int written = 0, writable = 0;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (craft_slot_empty(s)) continue;

        if (s->item == CRAFT_ITEM_WRITTEN_BOOK)
        {
            if (written) return 0;
            written = 1;
        }
        else if (s->item == CRAFT_ITEM_WRITABLE_BOOK)
        {
            ++writable;
        }
        else
        {
            return 0;
        }
    }

    return written && writable > 0;
}

/* RecipeBookCloning.getCraftingResult: one written book for each writable one
 * plus the original, carrying a copy of the original's tag. A written book
 * without a tag compound is a null dereference in Java. Java then calls
 * setStackDisplayName with the name it just read, which writes back the same
 * string, so it changes nothing. */
static void book_result(const struct craft_grid *g, struct craft_result *out)
{
    const struct craft_stack *book = NULL;
    int writable = 0;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (craft_slot_empty(s)) continue;

        if (s->item == CRAFT_ITEM_WRITTEN_BOOK)
        {
            if (book != NULL) return;
            book = s;
        }
        else if (s->item == CRAFT_ITEM_WRITABLE_BOOK)
        {
            ++writable;
        }
        else
        {
            return;
        }
    }

    if (book == NULL || writable < 1) return;

    if (book->tag == 0)
    {
        thrown(out, "NullPointerException");
        return;
    }

    give(out, CRAFT_ITEM_WRITTEN_BOOK, writable + 1, 0, NULL);
    out->out.tag = book->tag;
}

/* ---- RecipesMapCloning ----------------------------------------------- */

/* RecipesMapCloning.matches: one filled map and at least one empty map. */
static int map_clone_matches(const struct craft_grid *g)
{
    int filled = 0, empty = 0;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (craft_slot_empty(s)) continue;

        if (s->item == CRAFT_ITEM_FILLED_MAP)
        {
            if (filled) return 0;
            filled = 1;
        }
        else if (s->item == CRAFT_ITEM_MAP)
        {
            ++empty;
        }
        else
        {
            return 0;
        }
    }

    return filled && empty > 0;
}

/* RecipesMapCloning.getCraftingResult: a filled map for each empty one plus the
 * original, at the original's damage, carrying only the original's display
 * name. The rest of the filled map's tag is dropped, as in Java. */
static void map_clone_result(const struct craft_grid *g, struct craft_result *out)
{
    const struct craft_stack *map = NULL;
    int empty = 0;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (craft_slot_empty(s)) continue;

        if (s->item == CRAFT_ITEM_FILLED_MAP)
        {
            if (map != NULL) return;
            map = s;
        }
        else if (s->item == CRAFT_ITEM_MAP)
        {
            ++empty;
        }
        else
        {
            return;
        }
    }

    if (map == NULL || empty < 1) return;

    give(out, CRAFT_ITEM_FILLED_MAP, empty + 1, map->damage, NULL);

    /* ItemStack.hasDisplayName / setStackDisplayName. */
    const struct itag *mt = itag_get(map->tag);

    if (mt != NULL && mt->has_name)
    {
        nbt *tree = tag_tree(map->tag);
        const nbt *name = nbt_get(display_compound(tree), "Name");
        nbt *tag = nbt_new_compound();
        nbt *d = nbt_new_compound();
        nbt_put(d, "Name", nbt_new_string(nbt_string_value(name)));
        nbt_put(tag, "display", d);
        out->out.tag = tag_own(tag);
        nbt_free(tree);
    }
}

/* ---- RecipesMapExtending --------------------------------------------- */

/* Items.filled_map.getMapData reads the World argument, so a null world is a
 * null dereference here (Java throws inside the read); with a world, the
 * filled map's MapData must exist and have a scale below 4. The native world
 * has no map storage yet, so its hook answers "no data" and this does not
 * match until the map layer lands. Returns -1 for the throw. */
static int map_extend_matches(const struct craft_grid *g, const struct craft_world *w)
{
    const struct craft_stack *map = NULL;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (!craft_slot_empty(s) && s->item == CRAFT_ITEM_FILLED_MAP)
        {
            map = s;
            break;
        }
    }

    if (map == NULL) return 0;
    if (w == NULL) return -1;

    int scale = 0;

    if (!w->map_data(w->ctx, map->damage, &scale)) return 0;

    return scale < 4;
}

/* RecipesMapExtending.getCraftingResult: a copy of the filled map at stack size
 * 1 with map_is_scaling set, keeping the rest of its tag. */
static void map_extend_result(const struct craft_grid *g, struct craft_result *out)
{
    const struct craft_stack *map = NULL;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (!craft_slot_empty(s) && s->item == CRAFT_ITEM_FILLED_MAP)
        {
            map = s;
            break;
        }
    }

    if (map == NULL) return;

    give(out, CRAFT_ITEM_FILLED_MAP, 1, map->damage, tag_set(tag_tree(map->tag), "map_is_scaling", nbt_new_byte(1)));
}

/* ---- RecipeFireworks ------------------------------------------------- */

/* ItemDye's firework colour for a damage, or -1 for a damage outside the 16 the
 * array holds, which Java indexes out of bounds. */
static int firework_dye_color(int damage)
{
    return damage >= 0 && damage < 16 ? CRAFT_DYE_COLORS[damage] : -1;
}

/* RecipeFireworks.matches: it sorts the grid into counts, then picks one of
 * three shapes and builds the result as it goes. Returns 1 when it matched,
 * with out set; 0 when it did not. The one thing that can throw is a dye damage
 * outside 0..15, indexed against ItemDye's colour array. */
static int fireworks_matches(const struct craft_grid *g, struct craft_result *out)
{
    int paper = 0, gunpowder = 0, dye = 0, charge = 0, shine = 0, tail = 0;

    for (int i = 0; i < g->side * g->side; ++i)
    {
        const struct craft_stack *s = &g->slot[i];

        if (craft_slot_empty(s)) continue;

        if (s->item == CRAFT_ITEM_PAPER) ++paper;
        else if (s->item == CRAFT_ITEM_FIREWORK_CHARGE) ++charge;
        else if (s->item == CRAFT_ITEM_DYE) ++dye;
        else if (s->item == CRAFT_ITEM_GUNPOWDER) ++gunpowder;
        else if (s->item == CRAFT_ITEM_GLOWSTONE_DUST) ++shine;
        else if (s->item == CRAFT_ITEM_DIAMOND) ++shine;
        else if (s->item == CRAFT_ITEM_FIRE_CHARGE) ++tail;
        else if (s->item == CRAFT_ITEM_FEATHER) ++tail;
        else if (s->item == CRAFT_ITEM_GOLD_NUGGET) ++tail;
        else if (s->item == CRAFT_ITEM_SKULL) ++tail;
        else return 0;
    }

    int extra = shine + dye + tail;

    if (!(gunpowder <= 3 && paper <= 1)) return 0;

    if (gunpowder >= 1 && paper == 1 && extra == 0)
    {
        /* A rocket: the charges' explosions, and the gunpowder count as the
         * flight time. */
        give(out, CRAFT_ITEM_FIREWORKS, 1, 0, NULL);

        if (charge > 0)
        {
            nbt *explosions = nbt_new_list();

            for (int i = 0; i < g->side * g->side; ++i)
            {
                const struct craft_stack *s = &g->slot[i];

                if (craft_slot_empty(s)) continue;
                if (s->item != CRAFT_ITEM_FIREWORK_CHARGE || s->tag == 0) continue;

                nbt *tree = tag_tree(s->tag);
                const nbt *e = nbt_get(tree, "Explosion");

                if (e != NULL && nbt_kind(e) == NBT_COMPOUND) nbt_list_add(explosions, copy_tag(e));
                nbt_free(tree);
            }

            nbt *rocket = nbt_new_compound();
            nbt_put(rocket, "Explosions", explosions);
            nbt_put(rocket, "Flight", nbt_new_byte(gunpowder));
            nbt *tag = nbt_new_compound();
            nbt_put(tag, "Fireworks", rocket);
            out->out.tag = tag_own(tag);
        }

        return 1;
    }

    if (gunpowder == 1 && paper == 0 && charge == 0 && dye > 0 && tail <= 1)
    {
        /* A charge: its colours, whether it flickers or trails, and the shape
         * its tail item names. */
        int colors[CRAFT_MAX_SLOTS];
        int n = 0;
        int flicker = 0, trail = 0, type = 0;

        for (int i = 0; i < g->side * g->side; ++i)
        {
            const struct craft_stack *s = &g->slot[i];

            if (craft_slot_empty(s)) continue;

            if (s->item == CRAFT_ITEM_DYE)
            {
                int c = firework_dye_color(s->damage);

                if (c < 0)
                {
                    thrown(out, "ArrayIndexOutOfBoundsException");
                    return 1;
                }

                colors[n++] = c;
            }
            else if (s->item == CRAFT_ITEM_GLOWSTONE_DUST) flicker = 1;
            else if (s->item == CRAFT_ITEM_DIAMOND) trail = 1;
            else if (s->item == CRAFT_ITEM_FIRE_CHARGE) type = 1;
            else if (s->item == CRAFT_ITEM_FEATHER) type = 4;
            else if (s->item == CRAFT_ITEM_GOLD_NUGGET) type = 2;
            else if (s->item == CRAFT_ITEM_SKULL) type = 3;
        }

        nbt *explosion = nbt_new_compound();
        nbt_put(explosion, "Colors", nbt_new_int_array(colors, n));
        if (flicker) nbt_put(explosion, "Flicker", nbt_new_byte(1));
        if (trail) nbt_put(explosion, "Trail", nbt_new_byte(1));
        nbt_put(explosion, "Type", nbt_new_byte(type));
        nbt *tag = nbt_new_compound();
        nbt_put(tag, "Explosion", explosion);
        give(out, CRAFT_ITEM_FIREWORK_CHARGE, 1, 0, tag);
        return 1;
    }

    if (gunpowder == 0 && paper == 0 && charge == 1 && dye > 0 && extra == dye)
    {
        /* Fading colours onto the one charge in the grid. */
        const struct craft_stack *c = NULL;

        for (int i = 0; i < g->side * g->side; ++i)
        {
            const struct craft_stack *s = &g->slot[i];

            if (!craft_slot_empty(s) && s->item == CRAFT_ITEM_FIREWORK_CHARGE) c = s;
        }

        if (c == NULL || c->tag == 0) return 0;

        int colors[CRAFT_MAX_SLOTS];
        int n = 0;

        for (int i = 0; i < g->side * g->side; ++i)
        {
            const struct craft_stack *s = &g->slot[i];

            if (craft_slot_empty(s) || s->item != CRAFT_ITEM_DYE) continue;

            int v = firework_dye_color(s->damage);

            if (v < 0)
            {
                thrown(out, "ArrayIndexOutOfBoundsException");
                return 1;
            }

            colors[n++] = v;
        }

        give(out, CRAFT_ITEM_FIREWORK_CHARGE, 1, 0, NULL);
        out->out.tag = c->tag;

        /* getCompoundTag("Explosion") hands back a detached empty compound when
         * the key is missing, so setting FadeColors on it changes nothing; a key
         * that holds something else makes Java's cast throw. */
        nbt *tree = tag_tree(c->tag);
        const nbt *e = nbt_get(tree, "Explosion");

        if (e != NULL && nbt_kind(e) != NBT_COMPOUND)
        {
            nbt_free(tree);
            thrown(out, "ReportedException");
            return 1;
        }

        if (e != NULL)
        {
            nbt *explosion = tag_set(copy_tag(e), "FadeColors", nbt_new_int_array(colors, n));
            out->out.tag = tag_own(tag_set(tag_tree(out->out.tag), "Explosion", explosion));
        }

        nbt_free(tree);
        return 1;
    }

    return 0;
}

/* ---- findMatchingRecipe ---------------------------------------------- */

void craft_find_matching(const struct craft_grid *g, const struct craft_world *w, struct craft_result *r)
{
    clear_result(r);

    if (repair_result(g, r)) return;

    for (int i = 0; i < RECIPE_COUNT; ++i)
    {
        const struct recipe_def *def = &RECIPES[i];

        switch (def->kind)
        {
            case RECIPE_SHAPED:
                if (!shaped_matches(def, g)) break;
                shaped_result(def, g, r);
                return;

            case RECIPE_SHAPELESS:
                if (!shapeless_matches(def, g)) break;
                give(r, def->output.item, def->output.count, def->output.damage, NULL);
                return;

            case RECIPE_ARMOR_DYES:
                if (!dye_matches(g)) break;
                dye_result(g, r);
                return;

            case RECIPE_BOOK_CLONING:
                if (!book_matches(g)) break;
                book_result(g, r);
                return;

            case RECIPE_MAP_CLONING:
                if (!map_clone_matches(g)) break;
                map_clone_result(g, r);
                return;

            case RECIPE_MAP_EXTENDING:
            {
                /* The superclass match runs first, so the throw below only
                 * happens for a grid the pattern really matches. */
                if (!shaped_matches(def, g)) break;

                int ok = map_extend_matches(g, w);

                if (ok < 0)
                {
                    thrown(r, "NullPointerException");
                    return;
                }

                if (!ok) break;

                map_extend_result(g, r);
                return;
            }

            case RECIPE_FIREWORKS:
                if (fireworks_matches(g, r)) return;
                break;

            default:
                abort();
        }
    }

    clear_result(r);
}
int craft_dye_firework_color(int damage)
{
    return firework_dye_color(damage);
}
