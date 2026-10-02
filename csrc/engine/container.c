/* Minecraft 1.7.10 inventory clicking, the native port. Every block names the
 * Java method it follows, in the order oracle/src/inventory has them, so the two
 * files can be read side by side. See container.h for what is deliberately not
 * ported (the dropped entity's motion, the experience orbs, the stats and
 * achievements the crafting paths award, and SlotCrafting's dead container-item
 * branch).
 *
 * Two things are worth calling out because Java's object identity is load
 * bearing and C's is not:
 *
 *   - Container.transferStackInSlot reads the slot's stack, hands the same
 *     object to mergeItemStack and only afterwards takes it out of the slot.
 *     The port moves the stack out first and works on the detached copy, which
 *     is the same thing because vanilla's merge ranges never contain the source
 *     slot, and a failed mergeItemStack leaves its argument untouched.
 *   - Container.field_94537_h is a HashSet<Slot>: its iteration order is the
 *     identity hash order and is not reproducible. Nothing depends on that
 *     order (each slot's share comes from the cursor and the set size, and the
 *     leftover is a sum), so the port keeps the slots in a deduplicated array,
 *     which reproduces the size and every per-slot result. */
#include "container.h"
#include "jmath.h"
#include "itemtag.h"
#include "blocks.h"
#include "smelting.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A stack local starts empty: the functions that write an out-parameter free
 * whatever it holds first, so an uninitialised one is a crash. */
#define STACK_EMPTY {-1, 0, 0, 0}
/* Item ids ContainerPlayer's armor slots special-case next to ItemArmor. */
#define ITEM_PUMPKIN 86
#define ITEM_SKULL 397

/* The fuel table TileEntityFurnace.getItemBurnTime names by hand. */
#define BLOCK_WOODEN_SLAB 126
#define BLOCK_COAL_BLOCK 173
#define ITEM_WOODEN_HOE 290
#define ITEM_SAPLING 6
#define ITEM_STICK 280
#define ITEM_COAL 263
#define ITEM_LAVA_BUCKET 327
#define ITEM_BLAZE_ROD 369

/* ---- stacks ---------------------------------------------------------- */

/* InventoryMerchant.resetRecipeAndSlots, defined in the merchant block below
 * (inv_set/inv_decr's reset trigger calls it). */
void container_merchant_reset(struct container *c);

/* SlotMerchantResult.onPickupFromSlot, defined below (slot_on_pickup calls
 * it). */
static void merchant_out_on_pickup(struct container *c, struct craft_stack *s);

/* ItemStack.getMaxStackSize. */
static int stack_max(const struct craft_stack *s)
{
    return s->item >= 0 ? ITEMS[s->item].max_stack_size : 0;
}

/* ItemStack.isItemStackDamageable. */
static int stack_damageable(const struct craft_stack *s)
{
    if (s->item < 0 || ITEMS[s->item].max_damage <= 0) return 0;

    const struct itag *t = itag_get(s->tag);
    return t == NULL || !t->unbreakable;
}

/* ItemStack.isItemDamaged. */
static int stack_damaged(const struct craft_stack *s)
{
    return stack_damageable(s) && s->damage > 0;
}

/* ItemStack.isStackable. */
static int stack_stackable(const struct craft_stack *s)
{
    return stack_max(s) > 1 && (!stack_damageable(s) || !stack_damaged(s));
}

/* ItemStack.getHasSubtypes. */
static int stack_subtypes(const struct craft_stack *s)
{
    return s->item >= 0 && ITEMS[s->item].has_subtypes != 0;
}

/* ItemStack.isItemEqual: the item and the damage, not the count. */
static int stack_same_item(const struct craft_stack *a, const struct craft_stack *b)
{
    return a->item == b->item && a->damage == b->damage;
}

/* ItemStack.areItemStackTagsEqual: interned tags are equal compounds exactly
 * when their indices are (itemtag.h). */
static int stack_tags_equal(const struct craft_stack *a, const struct craft_stack *b)
{
    return a->tag == b->tag;
}

/* ItemStack.copy, into a dst that may already hold a tag. */
static void stack_copy(struct craft_stack *dst, const struct craft_stack *src)
{
    craft_stack_free(dst);
    craft_stack_copy(dst, src);
}

/* Moves src into dst, leaving src empty. */
static void stack_move(struct craft_stack *dst, struct craft_stack *src)
{
    craft_stack_free(dst);
    *dst = *src;
    src->item = -1;
    src->count = 0;
    src->damage = 0;
    src->tag = 0;
}

/* A fresh stack; ItemStack's constructor clamps a negative damage. */
static void stack_new(struct craft_stack *s, int item, int count, int damage)
{
    s->item = item;
    s->count = count;
    s->damage = damage < 0 ? 0 : damage;
    s->tag = 0;
}

/* ItemStack.splitStack. */
static void stack_split(struct craft_stack *s, int n, struct craft_stack *out)
{
    craft_stack_copy(out, s);
    out->count = n;
    s->count -= n;
}

/* Item.isItemStackDamageable for an item id alone. */
static int item_armor(int item)
{
    return item >= 0 && ITEMS[item].kind == ITEM_ARMOR;
}

/* ---- inventories ----------------------------------------------------- */

static struct craft_stack *inv_at(struct inv *iv, int i)
{
    return i >= 0 && i < iv->size ? &iv->slot[i] : NULL;
}

static void matrix_changed(struct container *c);
static void drop_push(struct container *c, struct craft_stack *s);

/* setInventorySlotContents. InventoryBasic, TileEntityChest and
 * TileEntityFurnace clamp the count to the inventory's limit; InventoryPlayer,
 * InventoryCrafting and InventoryCraftResult do not. A grid notifies its
 * container afterwards. s is moved in and left empty; NULL empties the slot. */
static int inv_store(struct inv *iv, int i, struct craft_stack *s)
{
    if (iv == NULL || i < 0 || i >= iv->size)
    {
        if (s != NULL) craft_stack_free(s);
        return 0;
    }

    if ((iv->kind == INV_CHEST || iv->kind == INV_FURNACE) && s != NULL && s->count > iv->limit)
    {
        s->count = iv->limit;
    }

    craft_stack_free(&iv->slot[i]);

    if (s == NULL) stack_new(&iv->slot[i], -1, 0, 0);
    else stack_move(&iv->slot[i], s);

    return 1;
}

/* The two stores that never notify: the craft result (InventoryCraftResult
 * has no container to tell) and the merchant's slot 2 (its reset is for
 * slots 0 and 1), which matrix_changed and container_merchant_reset make. */
static void inv_set_result(struct inv *iv, int i, struct craft_stack *s)
{
    (void)inv_store(iv, i, s);
}

static void inv_set(struct container *c, struct inv *iv, int i, struct craft_stack *s)
{
    if (!inv_store(iv, i, s)) return;

    if (iv->kind == INV_CRAFT) matrix_changed(c);

    /* InventoryMerchant.inventoryResetNeededOnSlotChange: slots 0/1 reset the
     * recipe and the result (slot 2's own set comes from the reset itself). */
    if (iv->kind == INV_MERCHANT && i <= 1) container_merchant_reset(c);
}

/* decrStackSize: 1 and out holds the stack taken out, 0 for Java's null. */
static int inv_decr(struct container *c, struct inv *iv, int i, int n, struct craft_stack *out)
{
    struct craft_stack *s = inv_at(iv, i);

    if (s == NULL || craft_slot_empty(s)) return 0;

    /* InventoryCraftResult.decrStackSize ignores how many were asked for: it
     * hands back the whole stack and empties the slot; so does
     * InventoryMerchant's for its slot 2, the sell, without a reset. */
    if (iv->kind == INV_RESULT || (iv->kind == INV_MERCHANT && i == 2) || s->count <= n)
    {
        stack_move(out, s);
        craft_stack_free(s);
        stack_new(s, -1, 0, 0);
    }
    else
    {
        stack_split(s, n, out);

        if (s->count == 0)
        {
            craft_stack_free(s);
            stack_new(s, -1, 0, 0);
        }
    }

    if (iv->kind == INV_CRAFT) matrix_changed(c);

    /* InventoryMerchant's decrStackSize on slots 0/1 resets too. */
    if (iv->kind == INV_MERCHANT && i <= 1) container_merchant_reset(c);
    return 1;
}

/* getStackInSlotOnClosing: the stack is taken out of the inventory. */
static int inv_take(struct inv *iv, int i, struct craft_stack *out)
{
    struct craft_stack *s = inv_at(iv, i);

    if (s == NULL || craft_slot_empty(s)) return 0;

    stack_move(out, s);
    craft_stack_free(s);
    stack_new(s, -1, 0, 0);
    return 1;
}

/* ---- the player inventory -------------------------------------------- */

/* InventoryPlayer.getFirstEmptyStack, over mainInventory only. */
static int player_first_empty(struct inv *p)
{
    for (int i = 0; i < 36; ++i)
    {
        if (craft_slot_empty(&p->slot[i])) return i;
    }

    return -1;
}

/* InventoryPlayer.storeItemStack: the first stack of the same item that has
 * room. */
static int player_store_item(struct inv *p, const struct craft_stack *s)
{
    for (int i = 0; i < 36; ++i)
    {
        struct craft_stack *m = &p->slot[i];

        if (!craft_slot_empty(m) && m->item == s->item && stack_stackable(m) && m->count < stack_max(m)
            && m->count < p->limit && (!stack_subtypes(m) || m->damage == s->damage)
            && stack_tags_equal(m, s))
        {
            return i;
        }
    }

    return -1;
}

/* InventoryPlayer.storePartialItemStack: the count of s that did not fit, with
 * s mutated in place. */
static int player_store_partial(struct inv *p, struct craft_stack *s)
{
    int left = s->count;

    if (stack_max(s) == 1)
    {
        int i = player_first_empty(p);

        if (i < 0) return left;

        if (craft_slot_empty(&p->slot[i]))
        {
            craft_stack_copy(&p->slot[i], s);
            p->slot[i].count = s->count;
        }

        return 0;
    }

    int i = player_store_item(p, s);

    if (i < 0) i = player_first_empty(p);
    if (i < 0) return left;

    if (craft_slot_empty(&p->slot[i]))
    {
        /* new ItemStack(item, 0, damage) plus the stack's tag */
        craft_stack_copy(&p->slot[i], s);
        p->slot[i].count = 0;
    }

    int take = left;

    if (take > stack_max(&p->slot[i]) - p->slot[i].count) take = stack_max(&p->slot[i]) - p->slot[i].count;
    if (take > p->limit - p->slot[i].count) take = p->limit - p->slot[i].count;

    if (take == 0) return left;

    p->slot[i].count += take;
    s->count = left - take;
    return s->count;
}

/* InventoryPlayer.addItemStackToInventory: 1 when something went in, with s
 * left holding what did not. */
static int player_add(struct inv *p, struct craft_stack *s, int creative)
{
    if (craft_slot_empty(s) || s->count == 0) return 0;

    if (stack_damaged(s))
    {
        int i = player_first_empty(p);

        if (i >= 0)
        {
            craft_stack_copy(&p->slot[i], s);
            p->slot[i].count = s->count;
            s->count = 0;
            return 1;
        }

        if (creative)
        {
            s->count = 0;
            return 1;
        }

        return 0;
    }

    int before;

    do
    {
        before = s->count;
        s->count = player_store_partial(p, s);
    }
    while (s->count > 0 && s->count < before);

    if (s->count == before && creative)
    {
        s->count = 0;
        return 1;
    }

    return s->count < before;
}

/* ---- slots ----------------------------------------------------------- */

static struct craft_stack *slot_at(struct container *c, int i)
{
    return i >= 0 && i < c->nslots ? &c->slots[i].inv->slot[c->slots[i].index] : NULL;
}

static int slot_has(struct slot *sl)
{
    return !craft_slot_empty(&sl->inv->slot[sl->index]);
}

static struct craft_stack *slot_stack(struct slot *sl)
{
    return &sl->inv->slot[sl->index];
}

/* Slot.onSlotChanged: inventory.onInventoryChanged, which only
 * InventoryMerchant turns into state (resetRecipeAndSlots, for any of its
 * three slots: the result and the villager's yes/no sound). */
static void slot_changed(struct container *c, struct slot *sl)
{
    if (sl->inv->kind == INV_MERCHANT) container_merchant_reset(c);
}

/* Slot.putStack: setInventorySlotContents then onSlotChanged. */
static void slot_put(struct container *c, struct slot *sl, struct craft_stack *s)
{
    inv_set(c, sl->inv, sl->index, s);
    slot_changed(c, sl);
}

/* Slot.getSlotStackLimit. */
static int slot_limit(struct slot *sl)
{
    return sl->kind == SLOT_ARMOR ? 1 : sl->inv->limit;
}

/* Slot.isItemValid, for the four slot kinds these containers have. */
static int slot_valid(struct slot *sl, const struct craft_stack *s)
{
    switch (sl->kind)
    {
        case SLOT_CRAFT_OUT:
        case SLOT_FURNACE_OUT:
        case SLOT_MERCHANT_OUT:
            return 0;

        case SLOT_ARMOR:
            if (craft_slot_empty(s)) return 0;

            if (item_armor(s->item)) return ITEMS[s->item].armor_type == sl->armor_type;

            return (s->item == ITEM_PUMPKIN || s->item == ITEM_SKULL) && sl->armor_type == 0;

        default:
            return 1;
    }
}

/* Slot.canTakeStack: no slot of these containers overrides it. */
static int slot_can_take(struct slot *sl)
{
    (void)sl;
    return 1;
}

/* Container.canDragIntoSlot: the default. */
static int can_drag_into(struct container *c, struct slot *sl)
{
    (void)c;
    (void)sl;
    return 1;
}

/* Container.func_94530_a: the player and workbench containers refuse to
 * double-click-collect from the crafted-result slot. */
static int can_collect_from(struct container *c, struct slot *sl)
{
    if ((c->kind == CONTAINER_PLAYER || c->kind == CONTAINER_WORKBENCH) && sl->inv->kind == INV_RESULT) return 0;

    return 1;
}

/* Container.func_94527_a. */
static int drag_matches(struct slot *sl, const struct craft_stack *s, int test)
{
    int ok = sl == NULL || !slot_has(sl);

    if (sl != NULL && slot_has(sl) && !craft_slot_empty(s) && stack_same_item(s, slot_stack(sl))
        && stack_tags_equal(slot_stack(sl), s))
    {
        int extra = test ? 0 : s->count;
        ok |= slot_stack(sl)->count + extra <= stack_max(s);
    }

    return ok;
}

/* GuiContainer's own reads of a slot (gui_input.c): func_94527_a(slot, s,
 * true) over the slot's stack HAVE (NULL or empty for an empty slot), and
 * the slot's isItemValid and the container's func_94530_a. */
int container_stack_fits(const struct craft_stack *have, const struct craft_stack *s)
{
    int ok = have == NULL || craft_slot_empty(have);

    if (have != NULL && !craft_slot_empty(have) && s != NULL && !craft_slot_empty(s) &&
        stack_same_item(s, have) && stack_tags_equal(have, s))
        ok |= have->count <= stack_max(s);

    return ok;
}

int container_slot_accepts(const struct container *c, int i, const struct craft_stack *s)
{
    if (i < 0 || i >= c->nslots) return 0;
    return slot_valid((struct slot *)&c->slots[i], s) &&
           can_drag_into((struct container *)c, (struct slot *)&c->slots[i]);
}

int container_slot_can_drag(const struct container *c, int i)
{
    return i >= 0 && i < c->nslots && can_drag_into((struct container *)c, (struct slot *)&c->slots[i]);
}

int container_slot_limit(const struct container *c, int i)
{
    return i >= 0 && i < c->nslots ? slot_limit((struct slot *)&c->slots[i]) : 0;
}

int container_slot_collectable(const struct container *c, int i)
{
    return i >= 0 && i < c->nslots && can_collect_from((struct container *)c, (struct slot *)&c->slots[i]);
}

/* SlotCrafting.onCrafting(ItemStack): the stats and the Item.onCreated call
 * reach no state this port holds (Item.onCreated is a no-op for every item
 * without a map), so the crafted callback (the stat half, with the counter
 * before the reset) and the counter reset are left. */
static void craft_out_crafted(struct container *c, const struct craft_stack *s)
{
    if (c->crafted != NULL) c->crafted(c->crafted_ctx, s, c->amount_crafted);
    c->amount_crafted = 0;
}

/* SlotFurnace.onCrafting(ItemStack): same, plus the EntityXPOrb spawn the
 * world side owns. */
static void furnace_out_crafted(struct container *c, const struct craft_stack *s)
{
    if (c->crafted != NULL) c->crafted(c->crafted_ctx, s, c->furnace_used);
    if (c->furnace_xp != NULL) c->furnace_xp(c->furnace_xp_ctx, s, c->furnace_used);
    c->furnace_used = 0;
}

/* SlotMerchantResult.onCrafting(ItemStack): ItemStack.onCrafting with
 * field_75231_g, which then resets. */
static void merchant_out_crafted(struct container *c, const struct craft_stack *s)
{
    if (c->merchant_crafted != NULL) c->merchant_crafted(c->merchant_crafted_ctx, s, c->merchant_used);
    c->merchant_used = 0;
}

/* Slot.onCrafting(stack, n) and its two overrides. */
static void slot_crafting(struct container *c, struct slot *sl, const struct craft_stack *s, int n)
{
    if (sl->kind == SLOT_CRAFT_OUT)
    {
        c->amount_crafted += n;
        craft_out_crafted(c, s);
    }
    else if (sl->kind == SLOT_FURNACE_OUT)
    {
        c->furnace_used += n;
        furnace_out_crafted(c, s);
    }
    else if (sl->kind == SLOT_MERCHANT_OUT)
    {
        c->merchant_used += n;
        merchant_out_crafted(c, s);
    }
}

/* Slot.onSlotChange: the stack in the slot changed size, the count that left is
 * what the output slots count. */
static void slot_change(struct container *c, struct slot *sl, const struct craft_stack *after,
                        const struct craft_stack *before)
{
    if (craft_slot_empty(after) || craft_slot_empty(before)) return;
    if (after->item != before->item) return;

    int delta = before->count - after->count;

    if (delta > 0) slot_crafting(c, sl, after, delta);
}

/* Slot.decrStackSize, including the two output slots' counters. */
static int slot_decr(struct container *c, struct slot *sl, int n, struct craft_stack *out)
{
    if (slot_has(sl))
    {
        int size = slot_stack(sl)->count;

        if (sl->kind == SLOT_CRAFT_OUT) c->amount_crafted += n < size ? n : size;
        if (sl->kind == SLOT_FURNACE_OUT) c->furnace_used += n < size ? n : size;
        if (sl->kind == SLOT_MERCHANT_OUT) c->merchant_used += n < size ? n : size;
    }

    return inv_decr(c, sl->inv, sl->index, n, out);
}

/* SlotCrafting.onPickupFromSlot: the grid is consumed one item per slot, and
 * every grid change recomputes the result. */
static void craft_out_on_pickup(struct container *c, struct craft_stack *s)
{
    craft_out_crafted(c, s);

    for (int i = 0; i < c->grid.size; ++i)
    {
        struct craft_stack *g = &c->grid.slot[i];

        if (craft_slot_empty(g)) continue;

        int item = g->item;
        struct craft_stack taken = STACK_EMPTY;
        inv_decr(c, &c->grid, i, 1, &taken);
        craft_stack_free(&taken);

        /* Item.hasContainerItem: the water, lava and milk buckets carry the
         * empty bucket (Item.java's setContainerItem; the cake takes three
         * milk buckets). Item.doesContainerItemLeaveCraftingGrid is true for
         * every item, so the bucket goes to the inventory, else back into
         * the emptied grid slot, else it is dropped. */
        if (item == 326 || item == 327 || item == 335)
        {
            struct craft_stack bucket = STACK_EMPTY;
            stack_new(&bucket, 325, 1, 0);

            if (!player_add(&c->player, &bucket, c->creative))
            {
                if (craft_slot_empty(&c->grid.slot[i])) inv_set(c, &c->grid, i, &bucket);
                else drop_push(c, &bucket);
            }

            craft_stack_free(&bucket);
        }
    }
}

/* SlotFurnace.onPickupFromSlot: onCrafting then Slot.onPickupFromSlot. */
static void furnace_out_on_pickup(struct container *c, struct craft_stack *s)
{
    furnace_out_crafted(c, s);
}

/* Slot.onPickupFromSlot (the base one ends in onSlotChanged). */
static void slot_on_pickup(struct container *c, struct slot *sl, struct craft_stack *s)
{
    if (sl->kind == SLOT_CRAFT_OUT) craft_out_on_pickup(c, s);
    else if (sl->kind == SLOT_FURNACE_OUT) furnace_out_on_pickup(c, s);
    else if (sl->kind == SLOT_MERCHANT_OUT) merchant_out_on_pickup(c, s);
    /* Slot.onPickupFromSlot: onSlotChanged */
    else slot_changed(c, sl);
}

/* ---- the container --------------------------------------------------- */

static void slot_init(struct slot *sl, struct inv *iv, int index, enum slot_kind kind, int armor_type)
{
    sl->inv = iv;
    sl->index = index;
    sl->kind = kind;
    sl->armor_type = armor_type;
}

static void inv_init(struct inv *iv, enum inv_kind kind, int size, int limit)
{
    memset(iv, 0, sizeof *iv);
    iv->kind = kind;
    iv->size = size;
    iv->limit = limit;

    for (int i = 0; i < INV_MAX_SLOTS; ++i) stack_new(&iv->slot[i], -1, 0, 0);
}

void container_init(struct container *c, enum container_kind kind, int chest_size, int creative)
{
    memset(c, 0, sizeof *c);
    c->kind = kind;
    c->creative = creative;
    c->drag_button = -1;
    stack_new(&c->cursor, -1, 0, 0);
    for (int i = 0; i < CONTAINER_MAX_DROPS; ++i) stack_new(&c->drops[i], -1, 0, 0);

    inv_init(&c->player, INV_PLAYER, 40, 64);
    if (kind == CONTAINER_DISPENSER) chest_size = 9;
    else if (kind == CONTAINER_HOPPER) chest_size = 5;
    else if (kind != CONTAINER_CHEST) chest_size = 0;
    inv_init(&c->chest, INV_CHEST, chest_size, 64);
    inv_init(&c->furnace, INV_FURNACE, kind == CONTAINER_FURNACE ? 3 : 0, 64);
    inv_init(&c->grid, INV_CRAFT, 0, 64);
    inv_init(&c->result, INV_RESULT, 1, 64);
    inv_init(&c->merchant, INV_MERCHANT, kind == CONTAINER_MERCHANT ? 3 : 0, 64);
    c->grid.owner = c;
    c->chest_size = chest_size;
    c->current_recipe_index = 0;
    c->current_recipe = -1;
    c->recipes = NULL;
    c->merchant_use = NULL;
    c->merchant_use_ctx = NULL;
    c->merchant_sound = NULL;
    c->merchant_sound_ctx = NULL;

    int n = 0;

    if (kind == CONTAINER_PLAYER)
    {
        /* ContainerPlayer: the result slot, the 2x2 grid, the armor slots from
         * the top down (39, 38, 37, 36) and then main and hotbar. */
        c->grid_side = 2;
        c->grid.size = 4;
        slot_init(&c->slots[n++], &c->result, 0, SLOT_CRAFT_OUT, 0);

        for (int i = 0; i < 4; ++i) slot_init(&c->slots[n++], &c->grid, i, SLOT_PLAIN, 0);
        for (int i = 0; i < 4; ++i) slot_init(&c->slots[n++], &c->player, 39 - i, SLOT_ARMOR, i);
        for (int i = 9; i < 36; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
        for (int i = 0; i < 9; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
    }
    else if (kind == CONTAINER_WORKBENCH)
    {
        c->grid_side = 3;
        c->grid.size = 9;
        slot_init(&c->slots[n++], &c->result, 0, SLOT_CRAFT_OUT, 0);

        for (int i = 0; i < 9; ++i) slot_init(&c->slots[n++], &c->grid, i, SLOT_PLAIN, 0);
        for (int i = 9; i < 36; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
        for (int i = 0; i < 9; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
    }
    else if (kind == CONTAINER_FURNACE)
    {
        slot_init(&c->slots[n++], &c->furnace, 0, SLOT_PLAIN, 0);
        slot_init(&c->slots[n++], &c->furnace, 1, SLOT_PLAIN, 0);
        slot_init(&c->slots[n++], &c->furnace, 2, SLOT_FURNACE_OUT, 0);

        for (int i = 9; i < 36; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
        for (int i = 0; i < 9; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
    }
    else if (kind == CONTAINER_MERCHANT)
    {
        /* ContainerMerchant: the two buy slots, the result (SlotMerchantResult
         * on index 2), then the player's main and hotbar. */
        slot_init(&c->slots[n++], &c->merchant, 0, SLOT_PLAIN, 0);
        slot_init(&c->slots[n++], &c->merchant, 1, SLOT_PLAIN, 0);
        slot_init(&c->slots[n++], &c->merchant, 2, SLOT_MERCHANT_OUT, 0);
        for (int i = 9; i < 36; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
        for (int i = 0; i < 9; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
    }
    else
    {
        /* ContainerChest, ContainerDispenser and ContainerHopper: the tile
         * slots (plain Slots, limit 64, every item valid), then the player's
         * main and hotbar. */
        for (int i = 0; i < chest_size; ++i) slot_init(&c->slots[n++], &c->chest, i, SLOT_PLAIN, 0);

        for (int i = 9; i < 36; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
        for (int i = 0; i < 9; ++i) slot_init(&c->slots[n++], &c->player, i, SLOT_PLAIN, 0);
    }

    c->nslots = n;

    for (int i = 0; i < n; ++i) c->slots[i].number = i;

    if (c->grid.size > 0) matrix_changed(c);
}

void container_free(struct container *c)
{
    struct inv *ivs[6] = {&c->player, &c->chest, &c->furnace, &c->grid, &c->result, &c->merchant};

    for (int k = 0; k < 6; ++k)
    {
        for (int i = 0; i < ivs[k]->size; ++i) craft_stack_free(&ivs[k]->slot[i]);
    }

    craft_stack_free(&c->cursor);

    for (int i = 0; i < c->ndrops; ++i) craft_stack_free(&c->drops[i]);

    c->ndrops = 0;
    c->nslots = 0;
}

void container_set_player(struct container *c, int i, struct craft_stack *s)
{
    inv_set(c, &c->player, i, s);
}

void container_set_chest(struct container *c, int i, struct craft_stack *s)
{
    inv_set(c, &c->chest, i, s);
}

void container_set_furnace(struct container *c, int i, struct craft_stack *s)
{
    inv_set(c, &c->furnace, i, s);
}

void container_set_merchant(struct container *c, int i, struct craft_stack *s)
{
    inv_set(c, &c->merchant, i, s);
}

void container_set_grid(struct container *c, int i, struct craft_stack *s)
{
    inv_set(c, &c->grid, i, s);
}

void container_set_cursor(struct container *c, struct craft_stack *s)
{
    craft_stack_free(&c->cursor);

    if (s == NULL) stack_new(&c->cursor, -1, 0, 0);
    else stack_move(&c->cursor, s);
}

struct craft_stack *container_slot(struct container *c, int i)
{
    if (i < 0 || i >= c->nslots) return NULL;

    struct craft_stack *s = slot_at(c, i);
    return s != NULL && !craft_slot_empty(s) ? s : NULL;
}

/* Container.onCraftMatrixChanged for the two container kinds that have a grid:
 * the result is whatever CraftingManager.findMatchingRecipe says, with the null
 * world argument the probe passes (see crafting.h). */
static void matrix_changed(struct container *c)
{
    if (c->grid.size == 0) return;

    struct craft_grid g;
    craft_grid_clear(&g, c->grid_side);

    for (int i = 0; i < c->grid.size; ++i) craft_stack_copy(&g.slot[i], &c->grid.slot[i]);

    struct craft_result r;
    craft_find_matching(&g, NULL, &r);

    for (int i = 0; i < CRAFT_MAX_SLOTS; ++i) craft_stack_free(&g.slot[i]);

    if (r.status == CRAFT_THROW)
    {
        /* The probe never fills a grid with the items that reach this with a
         * null world (filled_map, map, an untagged written_book, a wildcard
         * dye), so a throw is a port-side error rather than a case to compare:
         * name it and carry on with an empty result slot. */
        fprintf(stderr, "container: findMatchingRecipe threw %s\n", r.threw);
        inv_set_result(&c->result, 0, NULL);
    }
    else if (r.status == CRAFT_STACK)
    {
        inv_set_result(&c->result, 0, &r.out);
    }
    else
    {
        inv_set_result(&c->result, 0, NULL);
    }

    craft_result_free(&r);
}

/* ---- mergeItemStack -------------------------------------------------- */

/* Container.mergeItemStack: s is the stack being moved (the caller's object, as
 * in Java) and is mutated in place. */
static int merge_item_stack(struct container *c, struct craft_stack *s, int start, int end, int reverse)
{
    int moved = 0;
    int i = reverse ? end - 1 : start;

    if (stack_stackable(s))
    {
        while (s->count > 0 && ((!reverse && i < end) || (reverse && i >= start)))
        {
            struct craft_stack *d = slot_stack(&c->slots[i]);

            if (!craft_slot_empty(d) && d->item == s->item && (!stack_subtypes(s) || s->damage == d->damage)
                && stack_tags_equal(s, d))
            {
                int total = d->count + s->count;

                if (total <= stack_max(s))
                {
                    s->count = 0;
                    d->count = total;
                    slot_changed(c, &c->slots[i]);
                    moved = 1;
                }
                else if (d->count < stack_max(s))
                {
                    s->count -= stack_max(s) - d->count;
                    d->count = stack_max(s);
                    slot_changed(c, &c->slots[i]);
                    moved = 1;
                }
            }

            i += reverse ? -1 : 1;
        }
    }

    if (s->count > 0)
    {
        i = reverse ? end - 1 : start;

        while ((!reverse && i < end) || (reverse && i >= start))
        {
            struct craft_stack *d = slot_stack(&c->slots[i]);

            if (craft_slot_empty(d))
            {
                struct craft_stack copy = STACK_EMPTY;
                craft_stack_copy(&copy, s);
                slot_put(c, &c->slots[i], &copy);
                s->count = 0;
                moved = 1;
                break;
            }

            i += reverse ? -1 : 1;
        }
    }

    return moved;
}

/* ---- transferStackInSlot, one per kind ------------------------------- */

/* The tail ContainerPlayer, ContainerWorkbench and ContainerFurnace share:
 * the source slot is empty, the merged stack goes back if anything is left of
 * it, and the output slots hear about the pickup. */
static int transfer_tail(struct container *c, struct slot *sl, struct craft_stack *v5,
                         struct craft_stack *v3, struct craft_stack *out)
{
    int after = v5->count;

    if (after == 0)
    {
        slot_put(c, sl, NULL);
    }
    else
    {
        struct craft_stack back = STACK_EMPTY;
        craft_stack_copy(&back, v5);
        slot_put(c, sl, &back);
    }

    if (after == v3->count)
    {
        craft_stack_free(v5);
        craft_stack_free(v3);
        return 0;
    }

    slot_on_pickup(c, sl, v5);
    craft_stack_free(v5);
    stack_move(out, v3);
    return 1;
}

/* ContainerPlayer.transferStackInSlot. */
static int transfer_player(struct container *c, int index, struct craft_stack *out)
{
    struct slot *sl = &c->slots[index];

    if (!slot_has(sl)) return 0;

    struct craft_stack v5 = STACK_EMPTY;
    stack_move(&v5, slot_stack(sl));
    struct craft_stack v3 = STACK_EMPTY;
    craft_stack_copy(&v3, &v5);

    if (index == 0)
    {
        if (!merge_item_stack(c, &v5, 9, 45, 1))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }

        slot_change(c, sl, &v5, &v3);
    }
    else if (index >= 1 && index < 5)
    {
        if (!merge_item_stack(c, &v5, 9, 45, 0))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }
    }
    else if (index >= 5 && index < 9)
    {
        if (!merge_item_stack(c, &v5, 9, 45, 0))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }
    }
    else if (item_armor(v3.item) && !slot_has(&c->slots[5 + ITEMS[v3.item].armor_type]))
    {
        int a = 5 + ITEMS[v3.item].armor_type;

        if (!merge_item_stack(c, &v5, a, a + 1, 0))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }
    }
    else if (index >= 9 && index < 36)
    {
        if (!merge_item_stack(c, &v5, 36, 45, 0))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }
    }
    else if (index >= 36 && index < 45)
    {
        if (!merge_item_stack(c, &v5, 9, 36, 0))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }
    }
    else if (!merge_item_stack(c, &v5, 9, 45, 0))
    {
        slot_put(c, sl, &v5);
        craft_stack_free(&v3);
        return 0;
    }

    return transfer_tail(c, sl, &v5, &v3, out);
}

/* ---- the merchant ----------------------------------------------------- */

/* The empty stack InventoryMerchant's swap points var2 at. */
static const struct craft_stack EMPTY_MERCHANT_STACK = {-1, 0, 0, 0};

/* A trade stack's ItemStack copy into a craft stack. */
static void trade_stack_to_craft(const struct trade_stack *t, struct craft_stack *s)
{
    s->item = t->item;
    s->count = t->count;
    s->damage = t->damage;
    s->tag = t->tag;
}

/* SlotMerchantResult.func_75230_a: subtract the recipe's buy counts from the
 * two stacks in place, checking the items and the second-buy presence. */
static int merchant_subtract(const struct trade_recipe *r, struct craft_stack *p2,
                             struct craft_stack *p3)
{
    if (p2->item < 0 || p2->item != r->buy.item) return 0;

    if (r->has_buy_b && p3->item >= 0 && r->buy_b.item == p3->item)
    {
        p2->count -= r->buy.count;
        p3->count -= r->buy_b.count;
        return 1;
    }

    if (!r->has_buy_b && p3->item < 0)
    {
        p2->count -= r->buy.count;
        return 1;
    }

    return 0;
}

/* InventoryMerchant.resetRecipeAndSlots: the recipe match over slots 0/1 (the
 * swap when slot 0 is empty) and the sell into slot 2, then the villager's own
 * func_110297_a_ on the result. */
void container_merchant_reset(struct container *c)
{
    if (c->kind != CONTAINER_MERCHANT) return;

    c->current_recipe = -1;
    struct craft_stack *var1 = &c->merchant.slot[0];
    struct craft_stack *var2 = &c->merchant.slot[1];
    struct craft_stack *first = var1;
    struct craft_stack *second = var2;

    /* InventoryMerchant's swap: `if (var1 == null) { var1 = var2; var2 = null; }`
     * reads without clearing slot 0. */
    if (first->item < 0)
    {
        first = var2;
        second = (struct craft_stack *)&EMPTY_MERCHANT_STACK;
    }

    if (first->item < 0)
    {
        inv_set_result(&c->merchant, 2, NULL);
    }
    else if (c->recipes != NULL)
    {
        int idx = trades_can_be_used(c->recipes, first->item, first->count,
                                     second->item, second->count, c->current_recipe_index);
        int disabled = idx < 0 || trades_is_disabled(&c->recipes->r[idx]);

        if (idx >= 0 && !disabled)
        {
            c->current_recipe = idx;
            struct craft_stack sell;
            trade_stack_to_craft(&c->recipes->r[idx].sell, &sell);
            inv_set_result(&c->merchant, 2, &sell);
            craft_stack_free(&sell);
        }
        else if (second->item >= 0)
        {
            idx = trades_can_be_used(c->recipes, second->item, second->count,
                                     first->item, first->count, c->current_recipe_index);
            disabled = idx < 0 || trades_is_disabled(&c->recipes->r[idx]);

            if (idx >= 0 && !disabled)
            {
                c->current_recipe = idx;
                struct craft_stack sell;
                trade_stack_to_craft(&c->recipes->r[idx].sell, &sell);
                inv_set_result(&c->merchant, 2, &sell);
                craft_stack_free(&sell);
            }
            else
            {
                inv_set_result(&c->merchant, 2, NULL);
            }
        }
        else
        {
            inv_set_result(&c->merchant, 2, NULL);
        }
    }
    else
    {
        inv_set_result(&c->merchant, 2, NULL);
    }

    if (c->merchant_sound != NULL)
        c->merchant_sound(c->merchant_sound_ctx, &c->merchant.slot[2]);
}

void container_merchant_set_recipe_index(struct container *c, int index)
{
    c->current_recipe_index = index;
    container_merchant_reset(c);
}

/* SlotMerchantResult.onPickupFromSlot: the buy stacks' counts and the
 * villager's useRecipe. */
static void merchant_out_on_pickup(struct container *c, struct craft_stack *s)
{
    merchant_out_crafted(c, s);

    struct craft_stack var4 = STACK_EMPTY;
    stack_move(&var4, &c->merchant.slot[0]);
    struct craft_stack var5 = STACK_EMPTY;
    stack_move(&var5, &c->merchant.slot[1]);

    int done = 0;
    if (c->current_recipe >= 0 && c->recipes != NULL)
    {
        const struct trade_recipe *r = &c->recipes->r[c->current_recipe];
        done = merchant_subtract(r, &var4, &var5) || merchant_subtract(r, &var5, &var4);

        if (done && c->merchant_use != NULL)
            c->merchant_use(c->merchant_use_ctx, c->current_recipe);
    }

    if (var4.count <= 0) { var4.item = -1; var4.count = 0; }
    if (var5.count <= 0) { var5.item = -1; var5.count = 0; }
    inv_set(c, &c->merchant, 0, var4.item < 0 ? NULL : &var4);
    inv_set(c, &c->merchant, 1, var5.item < 0 ? NULL : &var5);
    craft_stack_free(&var4);
    craft_stack_free(&var5);
}

/* ContainerMerchant.transferStackInSlot. */
static int transfer_merchant(struct container *c, int index, struct craft_stack *out)
{
    struct slot *sl = &c->slots[index];

    if (!slot_has(sl)) return 0;

    struct craft_stack v5 = STACK_EMPTY;
    stack_move(&v5, slot_stack(sl));
    struct craft_stack v3 = STACK_EMPTY;
    craft_stack_copy(&v3, &v5);
    int moved = 1;

    if (index == 2)
    {
        moved = merge_item_stack(c, &v5, 3, 39, 1);

        if (moved) slot_change(c, sl, &v5, &v3);
    }
    else if (index == 0 || index == 1)
    {
        moved = merge_item_stack(c, &v5, 3, 39, 0);
    }
    else if (index >= 3 && index < 30)
    {
        moved = merge_item_stack(c, &v5, 30, 39, 0);
    }
    else if (index >= 30 && index < 39)
    {
        moved = merge_item_stack(c, &v5, 3, 30, 0);
    }
    else
    {
        moved = 0;
    }

    if (!moved)
    {
        /* Java returns null with the slot untouched: no reset */
        (void)inv_store(sl->inv, sl->index, &v5);
        craft_stack_free(&v3);
        return 0;
    }

    return transfer_tail(c, sl, &v5, &v3, out);
}

/* ContainerWorkbench.transferStackInSlot. */
static int transfer_workbench(struct container *c, int index, struct craft_stack *out)
{
    struct slot *sl = &c->slots[index];

    if (!slot_has(sl)) return 0;

    struct craft_stack v5 = STACK_EMPTY;
    stack_move(&v5, slot_stack(sl));
    struct craft_stack v3 = STACK_EMPTY;
    craft_stack_copy(&v3, &v5);

    if (index == 0)
    {
        if (!merge_item_stack(c, &v5, 10, 46, 1))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }

        slot_change(c, sl, &v5, &v3);
    }
    else if (index >= 10 && index < 37)
    {
        if (!merge_item_stack(c, &v5, 37, 46, 0))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }
    }
    else if (index >= 37 && index < 46)
    {
        if (!merge_item_stack(c, &v5, 10, 37, 0))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }
    }
    else if (!merge_item_stack(c, &v5, 10, 46, 0))
    {
        slot_put(c, sl, &v5);
        craft_stack_free(&v3);
        return 0;
    }

    return transfer_tail(c, sl, &v5, &v3, out);
}

/* Material.wood's index in the generated MATERIALS table, found by name like
 * harvest.c finds its materials. */
static int wood_index = -1;

/* before main: the same for every environment */
__attribute__((constructor)) static void wood_init(void)
{
    for (int i = 0; i < (int)(sizeof MATERIALS / sizeof MATERIALS[0]); ++i)
    {
        if (strcmp(MATERIALS[i].name, "wood") == 0) wood_index = i;
    }
}

static int material_wood(void)
{
    return wood_index;
}

/* TileEntityFurnace.getItemBurnTime(ItemStack) > 0, which is what
 * func_145954_b asks. The burn times themselves only matter to the furnace
 * tick, not to a click. */
static int furnace_fuel(const struct craft_stack *s)
{
    if (craft_slot_empty(s) || s->item < 0) return 0;

    const struct item_def *it = &ITEMS[s->item];

    if (it->kind == ITEM_BLOCK && it->block_id > 0)
    {
        int block = it->block_id;

        if (block == BLOCK_WOODEN_SLAB) return 1;
        if (BLOCKS[block].exists && BLOCKS[block].material == material_wood()) return 1;
        if (block == BLOCK_COAL_BLOCK) return 1;
    }

    if (it->kind == ITEM_TOOL && it->tool_material == 0) return 1;   /* a wooden tool */
    if (it->kind == ITEM_SWORD && it->tool_material == 0) return 1;
    if (s->item == ITEM_WOODEN_HOE) return 1;
    if (s->item == ITEM_STICK) return 1;
    if (s->item == ITEM_COAL) return 1;
    if (s->item == ITEM_LAVA_BUCKET) return 1;
    if (s->item == ITEM_SAPLING) return 1;
    if (s->item == ITEM_BLAZE_ROD) return 1;

    return 0;
}

/* ContainerFurnace.transferStackInSlot. */
static int transfer_furnace(struct container *c, int index, struct craft_stack *out)
{
    struct slot *sl = &c->slots[index];

    if (!slot_has(sl)) return 0;

    struct craft_stack v5 = STACK_EMPTY;
    stack_move(&v5, slot_stack(sl));
    struct craft_stack v3 = STACK_EMPTY;
    craft_stack_copy(&v3, &v5);
    int moved = 1;

    if (index == 2)
    {
        moved = merge_item_stack(c, &v5, 3, 39, 1);

        if (moved) slot_change(c, sl, &v5, &v3);
    }
    else if (index != 1 && index != 0)
    {
        struct craft_stack smelt = STACK_EMPTY;

        if (smelt_result(v5.item, v5.damage, &smelt))
        {
            craft_stack_free(&smelt);
            moved = merge_item_stack(c, &v5, 0, 1, 0);
        }
        else if (furnace_fuel(&v5))
        {
            moved = merge_item_stack(c, &v5, 1, 2, 0);
        }
        else if (index >= 3 && index < 30)
        {
            moved = merge_item_stack(c, &v5, 30, 39, 0);
        }
        else if (index >= 30 && index < 39)
        {
            moved = merge_item_stack(c, &v5, 3, 30, 0);
        }
    }
    else
    {
        moved = merge_item_stack(c, &v5, 3, 39, 0);
    }

    if (!moved)
    {
        /* ContainerFurnace returns without moving when a slot in 3..29 holds
         * something that is neither smeltable nor a fuel. */
        slot_put(c, sl, &v5);
        craft_stack_free(&v3);
        return 0;
    }

    return transfer_tail(c, sl, &v5, &v3, out);
}

/* ContainerChest.transferStackInSlot: no onPickupFromSlot and no count check.
 * ContainerHopper.transferStackInSlot is the same body over its
 * getSizeInventory (5) where the chest reads numRows * 9. ContainerDispenser's
 * adds the count check (nothing moved answers null) and the plain Slot's
 * onPickupFromSlot (onSlotChanged, no state here). */
static int transfer_chest(struct container *c, int index, struct craft_stack *out)
{
    struct slot *sl = &c->slots[index];
    int split = c->kind == CONTAINER_CHEST ? c->chest_size / 9 * 9 : c->chest_size;

    if (!slot_has(sl)) return 0;

    struct craft_stack v5 = STACK_EMPTY;
    stack_move(&v5, slot_stack(sl));
    struct craft_stack v3 = STACK_EMPTY;
    craft_stack_copy(&v3, &v5);

    if (index < split)
    {
        if (!merge_item_stack(c, &v5, split, c->nslots, 1))
        {
            slot_put(c, sl, &v5);
            craft_stack_free(&v3);
            return 0;
        }
    }
    else if (!merge_item_stack(c, &v5, 0, split, 0))
    {
        slot_put(c, sl, &v5);
        craft_stack_free(&v3);
        return 0;
    }

    int moved = v5.count != v3.count;

    if (v5.count == 0) slot_put(c, sl, NULL);
    else
    {
        struct craft_stack back = STACK_EMPTY;
        craft_stack_copy(&back, &v5);
        slot_put(c, sl, &back);
    }

    craft_stack_free(&v5);

    if (c->kind == CONTAINER_DISPENSER && !moved)
    {
        craft_stack_free(&v3);
        return 0;
    }

    stack_move(out, &v3);
    return 1;
}

/* Container.transferStackInSlot, dispatched on the kind. */
static int transfer(struct container *c, int index, struct craft_stack *out)
{
    switch (c->kind)
    {
        case CONTAINER_PLAYER: return transfer_player(c, index, out);
        case CONTAINER_WORKBENCH: return transfer_workbench(c, index, out);
        case CONTAINER_FURNACE: return transfer_furnace(c, index, out);
        case CONTAINER_MERCHANT: return transfer_merchant(c, index, out);
        default: return transfer_chest(c, index, out);
    }
}

/* ---- slotClick ------------------------------------------------------- */

static void drop_push(struct container *c, struct craft_stack *s)
{
    /* EntityPlayer.func_146097_a returns null for a null or empty stack, so
     * nothing is dropped and nothing is recorded. */
    if (craft_slot_empty(s) || s->count == 0)
    {
        craft_stack_free(s);
        return;
    }

    if (c->ndrops >= CONTAINER_MAX_DROPS)
    {
        fprintf(stderr, "container: more than %d drops in one click\n", CONTAINER_MAX_DROPS);
        craft_stack_free(s);
        return;
    }

    craft_stack_free(&c->drops[c->ndrops]);
    *&c->drops[c->ndrops] = *s;
    s->item = -1;
    s->count = 0;
    s->damage = 0;
    s->tag = 0;
    ++c->ndrops;
}

static void drag_reset(struct container *c)
{
    c->drag_state = 0;
    c->drag_count = 0;
}

static void drag_add(struct container *c, struct slot *sl)
{
    for (int i = 0; i < c->drag_count; ++i)
    {
        if (c->drag_slot[i] == sl->number) return;
    }

    if (c->drag_count < CONTAINER_MAX_SLOTS) c->drag_slot[c->drag_count++] = sl->number;
}

/* Container.func_94525_a. */
static void drag_share(int button, int size, struct craft_stack *s, int have)
{
    switch (button)
    {
        case 0:
            s->count = mh_floor_float((float)s->count / (float)size);
            break;

        case 1:
            s->count = 1;
            break;

        default:
            break;
    }

    s->count += have;
}

/* Container.slotClick, once; *retry is set where vanilla ends a shift-click
 * with retrySlotClick (slot_click_inner runs it, after this returns). */
static int slot_click_once(struct container *c, int slot, int button, int mode, struct craft_stack *ret, int *retry)
{
    struct craft_stack *cursor = &c->cursor;

    if (mode == 5)
    {
        int old = c->drag_state;
        c->drag_state = button & 3;

        if ((old != 1 || c->drag_state != 2) && old != c->drag_state)
        {
            drag_reset(c);
        }
        else if (craft_slot_empty(cursor))
        {
            drag_reset(c);
        }
        else if (c->drag_state == 0)
        {
            c->drag_button = (button >> 2) & 3;

            if (c->drag_button == 0 || c->drag_button == 1)
            {
                c->drag_state = 1;
                c->drag_count = 0;
            }
            else
            {
                drag_reset(c);
            }
        }
        else if (c->drag_state == 1)
        {
            struct slot *sl = slot >= 0 && slot < c->nslots ? &c->slots[slot] : NULL;

            if (sl != NULL && drag_matches(sl, cursor, 1) && slot_valid(sl, cursor)
                && cursor->count > c->drag_count && can_drag_into(c, sl))
            {
                drag_add(c, sl);
            }
        }
        else if (c->drag_state == 2)
        {
            if (c->drag_count > 0)
            {
                struct craft_stack cur = STACK_EMPTY;
                stack_copy(&cur, cursor);
                int left = cursor->count;

                for (int i = 0; i < c->drag_count; ++i)
                {
                    struct slot *sl = &c->slots[c->drag_slot[i]];

                    if (drag_matches(sl, cursor, 1) && slot_valid(sl, cursor)
                        && cursor->count >= c->drag_count && can_drag_into(c, sl))
                    {
                        struct craft_stack v12 = STACK_EMPTY;
                        craft_stack_copy(&v12, &cur);
                        int have = slot_has(sl) ? slot_stack(sl)->count : 0;
                        drag_share(c->drag_button, c->drag_count, &v12, have);

                        if (v12.count > stack_max(&v12)) v12.count = stack_max(&v12);
                        if (v12.count > slot_limit(sl)) v12.count = slot_limit(sl);

                        left -= v12.count - have;
                        slot_put(c, sl, &v12);
                    }
                }

                cur.count = left;

                if (cur.count <= 0)
                {
                    craft_stack_free(&cur);
                    craft_stack_free(cursor);
                }
                else
                {
                    stack_move(cursor, &cur);
                }
            }

            drag_reset(c);
        }
        else
        {
            drag_reset(c);
        }
    }
    else if (c->drag_state != 0)
    {
        drag_reset(c);
    }
    else if ((mode == 0 || mode == 1) && (button == 0 || button == 1))
    {
        if (slot == -999)
        {
            if (!craft_slot_empty(cursor))
            {
                if (button == 0)
                {
                    drop_push(c, cursor);
                    craft_stack_free(cursor);
                }

                if (button == 1)
                {
                    struct craft_stack one = STACK_EMPTY;
                    stack_split(cursor, 1, &one);
                    drop_push(c, &one);

                    if (cursor->count == 0) craft_stack_free(cursor);
                }
            }
        }
        else if (mode == 1)
        {
            if (slot < 0) return 0;

            struct slot *sl = slot < c->nslots ? &c->slots[slot] : NULL;

            if (sl != NULL && slot_can_take(sl))
            {
                struct craft_stack got = STACK_EMPTY;

                if (transfer(c, slot, &got))
                {
                    stack_copy(ret, &got);

                    if (slot_has(sl) && slot_stack(sl)->item == got.item)
                    {
                        *retry = 1;
                    }

                    craft_stack_free(&got);
                    return 1;
                }
            }
        }
        else
        {
            if (slot < 0) return 0;

            struct slot *sl = slot < c->nslots ? &c->slots[slot] : NULL;

            if (sl != NULL)
            {
                struct craft_stack *v17 = slot_stack(sl);
                struct craft_stack *v20 = cursor;
                int retset = 0;

                if (!craft_slot_empty(v17))
                {
                    stack_copy(ret, v17);
                    retset = 1;
                }

                if (craft_slot_empty(v17))
                {
                    if (!craft_slot_empty(v20) && slot_valid(sl, v20))
                    {
                        int amount = button == 0 ? v20->count : 1;

                        if (amount > slot_limit(sl)) amount = slot_limit(sl);

                        if (v20->count >= amount)
                        {
                            struct craft_stack piece = STACK_EMPTY;
                            stack_split(v20, amount, &piece);
                            slot_put(c, sl, &piece);
                        }

                        if (v20->count == 0) craft_stack_free(cursor);
                    }
                }
                else if (slot_can_take(sl))
                {
                    if (craft_slot_empty(v20))
                    {
                        int amount = button == 0 ? v17->count : (v17->count + 1) / 2;
                        int before = v17->count;
                        struct craft_stack taken = STACK_EMPTY;
                        slot_decr(c, sl, amount, &taken);
                        /* var17.stackSize == 0 reads the object the slot
                         * held: taken whole it is the cursor's stack now
                         * (its size kept, so no putStack(null) and no
                         * merchant reset ahead of useRecipe), split it is
                         * what stays in the slot */
                        int left = taken.count == before ? taken.count : v17->count;
                        stack_move(cursor, &taken);

                        if (left == 0) slot_put(c, sl, NULL);

                        slot_on_pickup(c, sl, cursor);
                    }
                    else if (slot_valid(sl, v20))
                    {
                        if (stack_same_item(v17, v20) && stack_tags_equal(v17, v20))
                        {
                            int amount = button == 0 ? v20->count : 1;

                            if (amount > slot_limit(sl) - v17->count) amount = slot_limit(sl) - v17->count;
                            if (amount > stack_max(v20) - v17->count) amount = stack_max(v20) - v17->count;

                            v20->count -= amount;

                            if (v20->count == 0) craft_stack_free(cursor);

                            v17->count += amount;
                        }
                        else if (v20->count <= slot_limit(sl))
                        {
                            /* Java swaps the two objects: putStack(var20) puts
                             * the cursor's stack in the slot and setItemStack
                             * (var17) puts the slot's old stack on the cursor.
                             * v17 points into the slot, so it is taken out
                             * first. */
                            struct craft_stack old = STACK_EMPTY;
                            stack_move(&old, v17);
                            struct craft_stack swap = STACK_EMPTY;
                            craft_stack_copy(&swap, v20);
                            slot_put(c, sl, &swap);
                            stack_move(cursor, &old);
                        }
                    }
                    else if (v17->item == v20->item && stack_max(v20) > 1
                             && (!stack_subtypes(v17) || v17->damage == v20->damage)
                             && stack_tags_equal(v17, v20))
                    {
                        int amount = v17->count;

                        if (amount > 0 && amount + v20->count <= stack_max(v20))
                        {
                            v20->count += amount;
                            struct craft_stack taken = STACK_EMPTY;
                            slot_decr(c, sl, amount, &taken);
                            /* var17 = decrStackSize(k1): its size, read
                             * before the copy is freed */
                            int taken_count = taken.count;
                            craft_stack_free(&taken);

                            if (taken_count == 0) slot_put(c, sl, NULL);

                            slot_on_pickup(c, sl, cursor);
                        }
                    }
                }

                /* the click's closing onSlotChanged, whatever it did */
                slot_changed(c, sl);
                return retset;
            }
        }
    }
    else if (mode == 2 && button >= 0 && button < 9)
    {
        struct slot *sl = slot < c->nslots ? &c->slots[slot] : NULL;

        if (sl == NULL) return 0;

        if (slot_can_take(sl))
        {
            int has17 = !craft_slot_empty(&c->player.slot[button]);
            /* getFirstEmptyStack while the hotbar slot still holds its stack
             * (getStackInSlot is a reference, not a take) */
            int first_empty = player_first_empty(&c->player);
            struct craft_stack v17 = STACK_EMPTY;
            if (has17) stack_move(&v17, &c->player.slot[button]);
            else stack_new(&v17, -1, 0, 0);

            int valid = craft_slot_empty(&v17) || (sl->inv == &c->player && slot_valid(sl, &v17));
            int free_slot = -1;

            if (!valid)
            {
                free_slot = first_empty;
                valid = valid || free_slot > -1;
            }

            if (slot_has(sl) && valid)
            {
                struct craft_stack v23 = STACK_EMPTY;
                craft_stack_copy(&v23, slot_stack(sl));
                struct craft_stack copy = STACK_EMPTY;
                craft_stack_copy(&copy, &v23);
                inv_set(c, &c->player, button, &copy);

                if ((sl->inv != &c->player || !slot_valid(sl, &v17)) && !craft_slot_empty(&v17))
                {
                    if (free_slot > -1)
                    {
                        player_add(&c->player, &v17, c->creative);
                        struct craft_stack taken = STACK_EMPTY;
                        slot_decr(c, sl, v23.count, &taken);
                        craft_stack_free(&taken);
                        slot_put(c, sl, NULL);
                        slot_on_pickup(c, sl, &v23);
                    }
                }
                else
                {
                    struct craft_stack taken = STACK_EMPTY;
                    slot_decr(c, sl, v23.count, &taken);
                    craft_stack_free(&taken);
                    slot_put(c, sl, &v17);
                    slot_on_pickup(c, sl, &v23);
                }

                craft_stack_free(&v23);
                craft_stack_free(&v17);
            }
            else if (!slot_has(sl) && !craft_slot_empty(&v17) && slot_valid(sl, &v17))
            {
                inv_set(c, &c->player, button, NULL);
                slot_put(c, sl, &v17);
                craft_stack_free(&v17);
            }
            else
            {
                /* nothing happens; the hotbar stack goes back where it was */
                struct craft_stack back = STACK_EMPTY;
                craft_stack_copy(&back, &v17);
                inv_set(c, &c->player, button, &back);
                craft_stack_free(&v17);
            }
        }
    }
    else if (mode == 3 && c->creative && craft_slot_empty(cursor) && slot >= 0 && slot < c->nslots)
    {
        struct slot *sl = &c->slots[slot];

        if (slot_has(sl))
        {
            struct craft_stack copy = STACK_EMPTY;
            craft_stack_copy(&copy, slot_stack(sl));
            copy.count = stack_max(&copy);
            stack_move(cursor, &copy);
        }
    }
    else if (mode == 4 && craft_slot_empty(cursor) && slot >= 0 && slot < c->nslots)
    {
        struct slot *sl = &c->slots[slot];

        if (slot_has(sl) && slot_can_take(sl))
        {
            int amount = button == 0 ? 1 : slot_stack(sl)->count;
            struct craft_stack taken = STACK_EMPTY;
            slot_decr(c, sl, amount, &taken);
            slot_on_pickup(c, sl, &taken);
            drop_push(c, &taken);
        }
    }
    else if (mode == 6 && slot >= 0 && slot < c->nslots)
    {
        struct slot *sl = &c->slots[slot];
        struct craft_stack *v17 = cursor;

        /* the branch ends in detectAndSendChanges, collected or not */
        c->click_detect = 1;

        if (!craft_slot_empty(v17) && (sl == NULL || !slot_has(sl) || !slot_can_take(sl)))
        {
            int start = button == 0 ? 0 : c->nslots - 1;
            int step = button == 0 ? 1 : -1;

            for (int pass = 0; pass < 2; ++pass)
            {
                for (int i = start; i >= 0 && i < c->nslots && v17->count < stack_max(v17); i += step)
                {
                    struct slot *s2 = &c->slots[i];

                    if (slot_has(s2) && drag_matches(s2, v17, 1) && slot_can_take(s2)
                        && can_collect_from(c, s2)
                        && (pass != 0 || slot_stack(s2)->count != stack_max(slot_stack(s2))))
                    {
                        int amount = stack_max(v17) - v17->count;

                        if (amount > slot_stack(s2)->count) amount = slot_stack(s2)->count;

                        struct craft_stack taken = STACK_EMPTY;
                        slot_decr(c, s2, amount, &taken);
                        v17->count += amount;

                        if (taken.count <= 0) slot_put(c, s2, NULL);

                        slot_on_pickup(c, s2, &taken);
                        craft_stack_free(&taken);
                    }
                }
            }
        }
    }

    return 0;
}

/* Container.slotClick. A shift-click that moved a stack while the slot still
 * holds the same item ends in retrySlotClick, slotClick(slot, button, 1) with
 * its result dropped; that call is the last thing the click does (the stack
 * it hands back was copied before), so the retries run here as a loop. */
static int slot_click_inner(struct container *c, int slot, int button, int mode, struct craft_stack *ret)
{
    int retry = 0;
    int result = slot_click_once(c, slot, button, mode, ret, &retry);

    while (retry)
    {
        struct craft_stack ignored = STACK_EMPTY;

        stack_new(&ignored, -1, 0, 0);
        retry = 0;
        slot_click_once(c, slot, button, 1, &ignored, &retry);
        craft_stack_free(&ignored);
    }

    return result;
}

int container_slot_click(struct container *c, int slot, int button, int mode, struct craft_stack *ret)
{
    for (int i = 0; i < c->ndrops; ++i) craft_stack_free(&c->drops[i]);

    c->ndrops = 0;
    return slot_click_inner(c, slot, button, mode, ret);
}

/* ---- close ----------------------------------------------------------- */

void container_close_client(struct container *c)
{
    /* the client world's onContainerClosed: the base spills the cursor and
     * ContainerPlayer its grid; ContainerWorkbench's and ContainerMerchant's
     * spills are behind !isClient */
    for (int i = 0; i < c->ndrops; ++i) craft_stack_free(&c->drops[i]);
    c->ndrops = 0;

    if (!craft_slot_empty(&c->cursor))
    {
        drop_push(c, &c->cursor);
        craft_stack_free(&c->cursor);
    }
    stack_new(&c->cursor, -1, 0, 0);

    if (c->kind == CONTAINER_PLAYER)
    {
        for (int i = 0; i < 4; ++i)
        {
            struct craft_stack out = STACK_EMPTY;

            if (inv_take(&c->grid, i, &out)) drop_push(c, &out);
        }

        inv_set(c, &c->result, 0, NULL);
    }
}

void container_close(struct container *c)
{
    /* Container.onContainerClosed, then the container's own. */
    if (!craft_slot_empty(&c->cursor))
    {
        drop_push(c, &c->cursor);
        craft_stack_free(&c->cursor);
    }

    if (c->kind == CONTAINER_PLAYER)
    {
        for (int i = 0; i < 4; ++i)
        {
            struct craft_stack out = STACK_EMPTY;

            if (inv_take(&c->grid, i, &out)) drop_push(c, &out);
        }

        inv_set(c, &c->result, 0, NULL);
    }
    else if (c->kind == CONTAINER_WORKBENCH)
    {
        for (int i = 0; i < 9; ++i)
        {
            struct craft_stack out = STACK_EMPTY;

            if (inv_take(&c->grid, i, &out)) drop_push(c, &out);
        }
    }
    else if (c->kind == CONTAINER_MERCHANT)
    {
        /* ContainerMerchant.onContainerClosed: setCustomer(null) between the
         * two supers (the first spills the cursor above), then the buy slots'
         * drops. */
        for (int i = 0; i < 2; ++i)
        {
            struct craft_stack out = STACK_EMPTY;

            if (inv_take(&c->merchant, i, &out)) drop_push(c, &out);
        }
    }
}