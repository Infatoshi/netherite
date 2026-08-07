/* game/container_live.c - see container_live.h. Vanilla sources:
 * Container.slotClick / mergeItemStack / retrySlotClick, ContainerPlayer /
 * ContainerWorkbench / ContainerFurnace / ContainerChest transferStackInSlot,
 * SlotCrafting, TileEntityFurnace.isItemFuel. Stack arithmetic reuses the
 * verified cc_* helpers from blaze container_click.h; furnace/chest mutation
 * goes through the live wrappers. */
#include "game/container_live.h"
#include "game/runtime.h"

#include "container_click.h"
#include "crafting_recipes_full.h"
#include "inventory_stack_rules.h"
#include "items_tools_armor.h"

#include <string.h>

/* ---- slot classes ------------------------------------------------------- */

static int is_inv(int s)     { return s >= 0 && s < GMC_INV_SLOTS; }
static int is_grid(int s)    { return s >= GMC_GRID0 && s < GMC_RESULT; }
static int is_furnace(int s) { return s >= GMC_FURNACE0 && s < GMC_ARMOR0; }
static int is_armor(int s)   { return s >= GMC_ARMOR0 && s < GMC_ARMOR0 + ISR_ARMOR_SLOTS; }
static int is_chest(int s)   { return s >= GMC_CHEST0 && s < GMC_CHEST0 + GMC_CHEST_SLOTS; }
static int is_brewing(int s) { return s >= GMC_BREWING0 && s < GMC_BREWING0 + GMC_BREWING_SLOTS; }
static int is_enchant(int s)  { return s >= GMC_ENCHANT0 && s < GMC_ENCHANT0 + GMC_ENCHANT_SLOTS; }
static int is_enchant_button(int s) { return s >= GMC_ENCHANT_BUTTON0 && s < GMC_ENCHANT_BUTTON0 + 3; }

/* GMC armor id -> IsrInv index (36..39). */
static int armor_isr(int s) { return ISR_ARMOR0 + (s - GMC_ARMOR0); }

/* EntityLiving.getSlotForItemStack + Item.isValidArmor for the armor piece index
 * (0 feet .. 3 head). Elytra (443) is valid only in the chest slot. */
static int armor_item_valid(int item, int armor_idx)
{
    if (item <= 0) return 0;
    if (item == ISR_ELYTRA_ITEM) return armor_idx == ITA_SLOT_CHEST;
    return ita_armor_slot(item) == armor_idx;
}

/* Grid cell usable for the open container: player screen = vanilla 2x2 (row-major
 * cells 0,1,3,4 of the 3x3), crafting table = all 9, furnace/chest = none. */
static int grid_cell_usable(const GmRuntime *r, int cell)
{
    if (r->container == 1) return 1;
    if (r->container == 2 || r->container == 3 || r->container == 4
            || r->container == 5) return 0;
    return cell == 0 || cell == 1 || cell == 3 || cell == 4;
}

static int slot_usable(const GmRuntime *r, int s)
{
    if (is_inv(s)) return 1;
    if (is_grid(s)) return grid_cell_usable(r, s - GMC_GRID0);
    if (s == GMC_RESULT)
        return r->container != 2 && r->container != 3 && r->container != 4
            && r->container != 5;
    if (is_furnace(s)) return r->container == 2 && r->active_furnace >= 0;
    /* Armor only on the player inventory screen (ContainerPlayer). */
    if (is_armor(s)) return r->container == 0;
    if (is_chest(s)) return r->container == 3 && r->active_chest >= 0;
    if (is_brewing(s))
        return r->container == 4 && r->active_static_container >= 0;
    if (is_enchant(s))
        return r->container == 5 && r->enchanting.open;
    return 0;
}

static ChestLive *ct_chest(GmRuntime *r)
{
    if (r->container != 3 || r->active_chest < 0) return 0;
    return &r->chests[r->active_chest].state;
}

static ICStack ct_chest_get(const GmRuntime *r, int s)
{
    if (r->container != 3 || r->active_chest < 0 || !is_chest(s)) return ic_empty();
    const ChestLive *c = &r->chests[r->active_chest].state;
    return chest_live_get(c, s - GMC_CHEST0);
}

static GmRuntimeStaticContainer *ct_brewing(GmRuntime *r)
{
    if (!r || r->container != 4 || r->active_static_container < 0
            || r->active_static_container >= r->static_containers_cap)
        return NULL;
    GmRuntimeStaticContainer *stand =
        &r->static_containers[r->active_static_container];
    return stand->active && stand->block == 117 ? stand : NULL;
}

static ICStack ct_brewing_get(const GmRuntime *r, int s)
{
    if (!r || !is_brewing(s) || r->container != 4
            || r->active_static_container < 0
            || r->active_static_container >= r->static_containers_cap)
        return ic_empty();
    const GmRuntimeStaticContainer *stand =
        &r->static_containers[r->active_static_container];
    if (!stand->active || stand->block != 117) return ic_empty();
    return stand->slots[s - GMC_BREWING0];
}

/* ---- direct stack access (inventory + grid + armor) --------------------- */

static ICStack ct_get(const GmRuntime *r, int s)
{
    if (is_inv(s))   return isr_get_stack(&r->player.inv, s);
    if (is_armor(s)) return isr_get_stack(&r->player.inv, armor_isr(s));
    if (is_grid(s))  return r->craft_grid[s - GMC_GRID0];
    if (is_enchant(s)) return r->enchanting.slots[s - GMC_ENCHANT0];
    return ic_empty();
}

static void ct_set(GmRuntime *r, int s, ICStack v)
{
    cc_normalize(&v);
    if (is_inv(s))        isr_set_stack(&r->player.inv, s, v);
    else if (is_armor(s)) isr_set_stack(&r->player.inv, armor_isr(s), v);
    else if (is_grid(s))  r->craft_grid[s - GMC_GRID0] = v;
    else if (is_enchant(s))
        enchanting_live_set_slot(
            &r->enchanting, r->world, s - GMC_ENCHANT0, v);
    if (is_armor(s) && armor_isr(s) == ISR_ARMOR_CHEST) {
        ICStack chest = isr_get_stack(&r->player.inv, ISR_ARMOR_CHEST);
        r->player.elytra_equipped = isr_elytra_usable(&chest);
    }
}

static FurnaceLive *ct_furnace(GmRuntime *r)
{
    if (r->container != 2 || r->active_furnace < 0) return 0;
    return &r->furnaces[r->active_furnace].state;
}

static ICStack ct_furnace_get(const GmRuntime *r, int s)
{
    if (r->container != 2 || r->active_furnace < 0) return ic_empty();
    const FurnaceLive *f = &r->furnaces[r->active_furnace].state;
    SRStack v = s == GMC_FURNACE0     ? f->input
              : s == GMC_FURNACE0 + 1 ? f->fuel
                                      : f->output;
    if (sr_isEmpty(v)) return ic_empty();
    return ic_mk(v.item, v.count, v.meta);
}

/* ---- vanilla dropItem: a REAL item entity in front of the player -------- */

static void ct_drop(GmRuntime *r, ICStack s)
{
    if (cc_is_empty(&s)) return;
    /* Keep StoredEnchantments on EntityItem (do not strip to item/count/meta). */
    gm_live_spawn_stack(&r->entities,
                        r->player.ent.posX + (double)r->ox,
                        r->player.ent.posY + 1.3,
                        r->player.ent.posZ + (double)r->oz,
                        s, 40);
}

/* ---- craft result ------------------------------------------------------- */

static ICStack grid_match(const GmRuntime *r)
{
    static CRRecipe recipes[CRF_NRECIPES];
    static int nrecipes = -1;
    if (nrecipes < 0) nrecipes = crf_build(recipes);

    CRStack grid[9];
    int any = 0;
    for (int i = 0; i < 9; ++i) {
        const ICStack *c = &r->craft_grid[i];
        grid[i] = cc_is_empty(c) ? crf_empty() : crf_mk(c->item, 1, c->meta);
        any |= !cc_is_empty(c);
    }
    if (!any) return ic_empty();
    CRStack res = crf_findMatching(recipes, nrecipes, grid);
    if (crf_isEmpty(res) || res.item == (i32)0xffffffff) return ic_empty();
    return ic_mk(res.item, res.count, res.meta);
}

ICStack gm_container_result(const struct GmRuntime *r)
{
    if (!r || r->container == 2 || r->container == 3 || r->container == 4)
        return ic_empty();
    return grid_match(r);
}

/* SlotCrafting.onTake: consume one item from every non-empty grid cell. */
static void grid_consume_one(GmRuntime *r)
{
    for (int i = 0; i < 9; ++i)
        if (!cc_is_empty(&r->craft_grid[i]))
            cc_shrink(&r->craft_grid[i], 1);
}

/* ---- mergeItemStack over an explicit slot-id order ----------------------
 * Vanilla Container.mergeItemStack(start,end,reverse) ported onto ordered id
 * lists (our hotbar/main ranges are laid out inversely to the vanilla container
 * indices, so each transfer builds the exact vanilla visit order explicitly). */

static int ct_merge_order(GmRuntime *r, ICStack *stack, const int *order, int n)
{
    int flag = 0;
    if (cc_is_stackable(stack)) {
        for (int k = 0; k < n && !cc_is_empty(stack); ++k) {
            int s = order[k];
            ICStack v = ct_get(r, s);
            if (cc_is_empty(&v) || !cc_stack_match(&v, stack)) continue;
            i32 max_size = cc_slot_stack_limit();
            i32 ms = cc_max_stack_size(stack->item, stack->meta);
            if (ms < max_size) max_size = ms;
            i32 j = v.count + stack->count;
            if (j <= max_size) {
                stack->count = 0;
                v.count = j;
                cc_normalize(stack);
                flag = 1;
            } else if (v.count < max_size) {
                cc_shrink(stack, max_size - v.count);
                v.count = max_size;
                flag = 1;
            }
            ct_set(r, s, v);
        }
    }
    if (!cc_is_empty(stack)) {
        for (int k = 0; k < n; ++k) {
            int s = order[k];
            ICStack v = ct_get(r, s);
            if (!cc_is_empty(&v)) continue;
            i32 lim = cc_slot_stack_limit();
            ct_set(r, s, cc_split_stack(stack, stack->count > lim ? lim : stack->count));
            flag = 1;
            break;
        }
    }
    return flag;
}

/* Visit orders (built once): vanilla container indices ascend main-then-hotbar,
 * so "main first" = our 9..35 then 0..8, and the reverse merge used by result /
 * furnace-output slots is our hotbar 8..0 then main 35..9. */
static void order_main_then_hotbar(int *o) { int n = 0; for (int s = 9; s < 36; ++s) o[n++] = s; for (int s = 0; s < 9; ++s) o[n++] = s; }
static void order_reverse_all(int *o)      { int n = 0; for (int s = 8; s >= 0; --s) o[n++] = s; for (int s = 35; s >= 9; --s) o[n++] = s; }
static void order_hotbar(int *o)           { for (int s = 0; s < 9; ++s) o[s] = s; }
static void order_main(int *o)             { for (int s = 9; s < 36; ++s) o[s - 9] = s; }

/* ---- transferStackInSlot (QUICK_MOVE, one attempt) ----------------------
 * Returns the original stack when something moved (vanilla contract), empty
 * when nothing moved. */
static ICStack ct_transfer(GmRuntime *r, int slot_id)
{
    int order[36];

    /* furnace output / input / fuel sources */
    if (is_furnace(slot_id)) {
        FurnaceLive *f = ct_furnace(r);
        ICStack v = ct_furnace_get(r, slot_id);
        if (!f || cc_is_empty(&v)) return ic_empty();
        ICStack original = v;
        if (slot_id == GMC_FURNACE0 + 2) order_reverse_all(order);
        else                             order_main_then_hotbar(order);
        int before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        (void)furnace_live_extract(f, slot_id - GMC_FURNACE0, before - v.count);
        return original;
    }

    /* ContainerChest: chest slots merge reverse into player inv */
    if (is_chest(slot_id)) {
        ChestLive *ch = ct_chest(r);
        ICStack v = ct_chest_get(r, slot_id);
        if (!ch || cc_is_empty(&v)) return ic_empty();
        ICStack original = v;
        order_reverse_all(order);
        int before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        (void)chest_live_extract(ch, slot_id - GMC_CHEST0, before - v.count);
        return original;
    }

    /* ContainerBrewingStand tile slots merge reverse into player inventory. */
    if (is_brewing(slot_id)) {
        GmRuntimeStaticContainer *stand = ct_brewing(r);
        ICStack v = ct_brewing_get(r, slot_id);
        if (!stand || cc_is_empty(&v)) return ic_empty();
        ICStack original = v;
        order_reverse_all(order);
        int before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        (void)brewing_live_extract(
            stand->slots, slot_id - GMC_BREWING0, before - v.count);
        gm_runtime_brewing_changed(r);
        return original;
    }

    /* ContainerEnchantment table slots merge reverse into player inventory. */
    if (is_enchant(slot_id)) {
        ICStack v = ct_get(r, slot_id);
        if (cc_is_empty(&v)) return ic_empty();
        ICStack original = v;
        order_reverse_all(order);
        int before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        ct_set(r, slot_id, v);
        return original;
    }

    if (slot_id == GMC_RESULT) {
        ICStack res = grid_match(r);
        if (cc_is_empty(&res)) return ic_empty();
        ICStack original = res;
        order_reverse_all(order);
        if (!ct_merge_order(r, &res, order, 36)) return ic_empty();
        if (!cc_is_empty(&res)) {
            /* partially placed results are not left dangling: drop the rest */
            ct_drop(r, res);
        }
        grid_consume_one(r);
        return original;
    }

    ICStack v = ct_get(r, slot_id);
    if (cc_is_empty(&v)) return ic_empty();
    ICStack original = v;

    if (is_grid(slot_id)) {
        order_main_then_hotbar(order);
        int before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        ct_set(r, slot_id, v);
        return original;
    }

    /* armor source: shift-click returns the piece to main-then-hotbar */
    if (is_armor(slot_id)) {
        order_main_then_hotbar(order);
        int before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        ct_set(r, slot_id, v);
        return original;
    }

    /* inventory source */
    if (r->container == 2) {
        FurnaceLive *f = ct_furnace(r);
        if (f) {
            /* smeltable -> input, else fuel -> fuel (vanilla ContainerFurnace) */
            static SRRecipe recipes[SR_NRECIPES];
            static int nrecipes = -1;
            if (nrecipes < 0) nrecipes = sr_build(recipes);
            SRStack in = sr_mk(v.item, v.count, v.meta);
            int target = -1;
            SRStack smelted = sr_getSmeltingResult(recipes, nrecipes, in);
            if (!sr_isEmpty(smelted) && smelted.item != (i32)0xffffffff)
                target = FURNACE_LIVE_SLOT_INPUT;
            else if (sr_getItemBurnTime(in) > 0)
                target = FURNACE_LIVE_SLOT_FUEL;
            if (target >= 0) {
                int moved = furnace_live_insert(f, target, in);
                if (moved > 0) {
                    cc_shrink(&v, moved);
                    ct_set(r, slot_id, v);
                    return original;
                }
                return ic_empty();
            }
        }
        /* not smeltable/fuel: main <-> hotbar swap, same as below */
    }

    /* ContainerPlayer: armor/elytra from inv -> matching empty armor slot. */
    if (r->container == 0 && is_inv(slot_id)) {
        int want = -1;
        if (v.item == ISR_ELYTRA_ITEM) want = ITA_SLOT_CHEST;
        else want = ita_armor_slot(v.item);
        if (want >= 0) {
            int as = GMC_ARMOR0 + want;
            ICStack cur = ct_get(r, as);
            if (cc_is_empty(&cur)) {
                ICStack one = cc_split_stack(&v, 1);
                ct_set(r, as, one);
                ct_set(r, slot_id, v);
                return original;
            }
        }
    }

    /* ContainerChest transferStackInSlot: inv -> chest slots (forward) */
    if (r->container == 3) {
        ChestLive *ch = ct_chest(r);
        if (ch && is_inv(slot_id)) {
            ICStack rem = v;
            for (int s = 0; s < GMC_CHEST_SLOTS && !cc_is_empty(&rem); ++s) {
                int moved = chest_live_insert(ch, s, rem);
                if (moved > 0) cc_shrink(&rem, moved);
            }
            if (rem.count == v.count) return ic_empty();
            ct_set(r, slot_id, rem);
            return original;
        }
    }

    /* ContainerBrewingStand checks ingredient, potion, then fuel. Blaze
     * powder is therefore routed to ingredient before its fuel role. */
    if (r->container == 4 && is_inv(slot_id)) {
        GmRuntimeStaticContainer *stand = ct_brewing(r);
        int target = -1;
        int moved = 0;
        if (stand) {
            if (brewing_live_slot_valid(BREWING_LIVE_INGREDIENT, &v)) {
                target = BREWING_LIVE_INGREDIENT;
            } else if (brewing_live_slot_valid(BREWING_LIVE_POTION0, &v)
                    && v.count == 1) {
                for (int s = 0; s < 3; ++s)
                    if (isr_is_empty(&stand->slots[s])) {
                        target = s;
                        break;
                    }
            } else if (brewing_live_slot_valid(BREWING_LIVE_FUEL, &v)) {
                target = BREWING_LIVE_FUEL;
            }
            if (target >= 0)
                moved = brewing_live_insert(stand->slots, target, v);
        }
        if (moved > 0) {
            cc_shrink(&v, moved);
            ct_set(r, slot_id, v);
            gm_runtime_brewing_changed(r);
            return original;
        }
        if (target >= 0) return ic_empty();
    }

    /* ContainerEnchantment: lapis routes to slot 1; every other item routes
     * one stack member to the single-item table slot. */
    if (r->container == 5 && is_inv(slot_id)) {
        int target = v.item == 351 && v.meta == 4 ? 1 : 0;
        ICStack current = r->enchanting.slots[target];
        if (!enchanting_live_slot_valid(target, &v))
            return ic_empty();
        if (cc_is_empty(&current)) {
            int amount = target == 0 ? 1 : v.count;
            ICStack moved = cc_split_stack(&v, amount);
            ct_set(r, GMC_ENCHANT0 + target, moved);
            ct_set(r, slot_id, v);
            return original;
        }
        if (target == 1 && cc_stack_match(&current, &v)
                && current.count < 64) {
            int amount = v.count < 64 - current.count
                ? v.count : 64 - current.count;
            current.count += amount;
            cc_shrink(&v, amount);
            ct_set(r, GMC_ENCHANT0 + target, current);
            ct_set(r, slot_id, v);
            return original;
        }
        return ic_empty();
    }

    if (slot_id < 9) order_main(order);
    else             order_hotbar(order);
    int n = slot_id < 9 ? 27 : 9;
    int before = v.count;
    ct_merge_order(r, &v, order, n);
    if (v.count == before) return ic_empty();
    ct_set(r, slot_id, v);
    return original;
}

/* ---- slotClick ----------------------------------------------------------- */

static void click_pickup_furnace(GmRuntime *r, int slot_id, int button)
{
    FurnaceLive *f = ct_furnace(r);
    if (!f) return;
    int fslot = slot_id - GMC_FURNACE0;
    ICStack cur = gm_player_cursor();
    ICStack v = ct_furnace_get(r, slot_id);

    if (cc_is_empty(&v)) {
        if (cc_is_empty(&cur)) return;
        int amount = button == 0 ? cur.count : 1;
        int moved = furnace_live_insert(f, fslot, sr_mk(cur.item, amount, cur.meta));
        if (moved > 0) cc_shrink(&cur, moved);
    } else if (cc_is_empty(&cur)) {
        int take = button == 0 ? v.count : (v.count + 1) / 2;
        SRStack got = furnace_live_extract(f, fslot, take);
        if (!sr_isEmpty(got)) cur = ic_mk(got.item, got.count, got.meta);
    } else if (cc_stack_match(&v, &cur)) {
        int amount = button == 0 ? cur.count : 1;
        int moved = furnace_live_insert(f, fslot, sr_mk(cur.item, amount, cur.meta));
        if (moved > 0) cc_shrink(&cur, moved);
    } else {
        /* swap, only when the slot would accept the cursor stack whole */
        if (fslot == FURNACE_LIVE_SLOT_OUTPUT) return;
        if (fslot == FURNACE_LIVE_SLOT_FUEL &&
            sr_getItemBurnTime(sr_mk(cur.item, cur.count, cur.meta)) <= 0)
            return;
        SRStack got = furnace_live_extract(f, fslot, v.count);
        int moved = furnace_live_insert(f, fslot, sr_mk(cur.item, cur.count, cur.meta));
        if (moved == cur.count) {
            cur = ic_mk(got.item, got.count, got.meta);
        } else {
            /* could not place the whole cursor: undo */
            if (moved > 0) (void)furnace_live_extract(f, fslot, moved);
            (void)furnace_live_insert(f, fslot, got);
        }
    }
    gm_player_cursor_set(cur);
}

static void click_pickup_result(GmRuntime *r, int button)
{
    (void)button;
    ICStack res = grid_match(r);
    if (cc_is_empty(&res)) return;
    ICStack cur = gm_player_cursor();
    if (cc_is_empty(&cur)) {
        cur = res;
    } else if (cc_stack_match(&cur, &res) &&
               cur.count + res.count <= cc_max_stack_size(cur.item, cur.meta)) {
        cur.count += res.count;
    } else {
        return;
    }
    grid_consume_one(r);
    gm_player_cursor_set(cur);
}

static void click_pickup_chest(GmRuntime *r, int slot_id, int button)
{
    ChestLive *ch = ct_chest(r);
    if (!ch) return;
    int cslot = slot_id - GMC_CHEST0;
    ICStack cur = gm_player_cursor();
    ICStack v = chest_live_get(ch, cslot);

    if (cc_is_empty(&v)) {
        if (cc_is_empty(&cur)) return;
        int amount = button == 0 ? cur.count : 1;
        /* Deposit retains StoredEnchantments (ic_mk alone would strip them). */
        int moved = chest_live_insert(ch, cslot, ic_with_count(&cur, amount));
        if (moved > 0) cc_shrink(&cur, moved);
    } else if (cc_is_empty(&cur)) {
        int take = button == 0 ? v.count : (v.count + 1) / 2;
        cur = chest_live_extract(ch, cslot, take);
    } else if (cc_stack_match(&v, &cur)) {
        int amount = button == 0 ? cur.count : 1;
        int moved = chest_live_insert(ch, cslot, ic_with_count(&cur, amount));
        if (moved > 0) cc_shrink(&cur, moved);
    } else {
        /* swap whole stacks when the cursor fits the slot limit */
        i32 lim = cc_item_stack_limit(&cur);
        if (cur.count <= lim) {
            ICStack taken = chest_live_extract(ch, cslot, v.count);
            int moved = chest_live_insert(ch, cslot, cur);
            if (moved == cur.count) {
                cur = taken;
            } else {
                if (moved > 0) (void)chest_live_extract(ch, cslot, moved);
                (void)chest_live_insert(ch, cslot, taken);
            }
        }
    }
    gm_player_cursor_set(cur);
}

static void click_pickup_brewing(GmRuntime *r, int slot_id, int button)
{
    GmRuntimeStaticContainer *stand = ct_brewing(r);
    int bslot = slot_id - GMC_BREWING0;
    ICStack cur = gm_player_cursor();
    ICStack v = ct_brewing_get(r, slot_id);
    int changed = 0;
    if (!stand) return;
    if (cc_is_empty(&v)) {
        if (cc_is_empty(&cur)) return;
        int amount = button == 0 ? cur.count : 1;
        int moved = brewing_live_insert(
            stand->slots, bslot, ic_with_count(&cur, amount));
        if (moved > 0) { cc_shrink(&cur, moved); changed = 1; }
    } else if (cc_is_empty(&cur)) {
        int take = button == 0 ? v.count : (v.count + 1) / 2;
        cur = brewing_live_extract(stand->slots, bslot, take);
        changed = !cc_is_empty(&cur);
    } else if (cc_stack_match(&v, &cur)) {
        int amount = button == 0 ? cur.count : 1;
        int moved = brewing_live_insert(
            stand->slots, bslot, ic_with_count(&cur, amount));
        if (moved > 0) { cc_shrink(&cur, moved); changed = 1; }
    } else if (brewing_live_slot_valid(bslot, &cur)) {
        ICStack taken = brewing_live_extract(stand->slots, bslot, v.count);
        int moved = brewing_live_insert(stand->slots, bslot, cur);
        if (moved == cur.count) {
            cur = taken;
            changed = 1;
        } else {
            if (moved > 0)
                (void)brewing_live_extract(stand->slots, bslot, moved);
            (void)brewing_live_insert(stand->slots, bslot, taken);
        }
    }
    gm_player_cursor_set(cur);
    if (changed) gm_runtime_brewing_changed(r);
}

static void click_pickup(GmRuntime *r, int slot_id, int button)
{
    ICStack cur = gm_player_cursor();
    ICStack v = ct_get(r, slot_id);
    int armor = is_armor(slot_id);
    int enchant = is_enchant(slot_id);
    int armor_idx = armor ? (slot_id - GMC_ARMOR0) : -1;
    /* Slot.getSlotStackLimit: armor slots accept exactly one item. */
    i32 slot_lim = armor || (enchant && slot_id == GMC_ENCHANT0)
        ? 1 : cc_slot_stack_limit();

    if (cc_is_empty(&v)) {
        if (!cc_is_empty(&cur)) {
            if (armor && !armor_item_valid(cur.item, armor_idx)) return;
            if (enchant && !enchanting_live_slot_valid(
                    slot_id - GMC_ENCHANT0, &cur)) return;
            int l2 = button == 0 ? cur.count : 1;
            if (l2 > slot_lim) l2 = (int)slot_lim;
            i32 lim = cc_item_stack_limit(&cur);
            if (l2 > lim) l2 = lim;
            v = cc_split_stack(&cur, l2);
        }
    } else if (cc_is_empty(&cur)) {
        int k2 = button == 0 ? v.count : (v.count + 1) / 2;
        cur = cc_decr_slot(&v, k2);
    } else if (cc_stack_match(&v, &cur)) {
        if (armor || (enchant && slot_id == GMC_ENCHANT0)) return;
        int j2 = button == 0 ? cur.count : 1;
        i32 lim = cc_item_stack_limit(&cur);
        i32 maxs = cc_max_stack_size(cur.item, cur.meta);
        if (j2 > lim - v.count) j2 = lim - v.count;
        if (j2 > maxs - v.count) j2 = maxs - v.count;
        if (j2 > 0) {
            cc_shrink(&cur, j2);
            cc_grow(&v, j2);
        }
    } else {
        if (armor && !armor_item_valid(cur.item, armor_idx)) return;
        if (enchant && !enchanting_live_slot_valid(
                slot_id - GMC_ENCHANT0, &cur)) return;
        if (armor && cur.count > 1) return; /* swap only whole single pieces */
        i32 lim = armor ? 1 : cc_item_stack_limit(&cur);
        if (cur.count <= lim) {
            ICStack tmp = v;
            v = cur;
            cur = tmp;
        }
    }
    ct_set(r, slot_id, v);
    gm_player_cursor_set(cur);
}

static void click_throw(GmRuntime *r, int slot_id, int button)
{
    ICStack cur = gm_player_cursor();
    if (!cc_is_empty(&cur)) return; /* vanilla: THROW only with an empty cursor */

    if (slot_id == GMC_RESULT) {
        ICStack res = grid_match(r);
        if (cc_is_empty(&res)) return;
        grid_consume_one(r);
        ct_drop(r, res);
        return;
    }
    if (is_furnace(slot_id)) {
        FurnaceLive *f = ct_furnace(r);
        ICStack v = ct_furnace_get(r, slot_id);
        if (!f || cc_is_empty(&v)) return;
        int amount = button == 0 ? 1 : v.count;
        SRStack got = furnace_live_extract(f, slot_id - GMC_FURNACE0, amount);
        if (!sr_isEmpty(got)) ct_drop(r, ic_mk(got.item, got.count, got.meta));
        return;
    }
    if (is_chest(slot_id)) {
        ChestLive *ch = ct_chest(r);
        ICStack v = ct_chest_get(r, slot_id);
        if (!ch || cc_is_empty(&v)) return;
        int amount = button == 0 ? 1 : v.count;
        ICStack got = chest_live_extract(ch, slot_id - GMC_CHEST0, amount);
        if (!cc_is_empty(&got)) ct_drop(r, got);
        return;
    }
    if (is_brewing(slot_id)) {
        GmRuntimeStaticContainer *stand = ct_brewing(r);
        ICStack v = ct_brewing_get(r, slot_id);
        if (!stand || cc_is_empty(&v)) return;
        int amount = button == 0 ? 1 : v.count;
        ICStack got = brewing_live_extract(
            stand->slots, slot_id - GMC_BREWING0, amount);
        if (!cc_is_empty(&got)) {
            ct_drop(r, got);
            gm_runtime_brewing_changed(r);
        }
        return;
    }
    ICStack v = ct_get(r, slot_id);
    if (cc_is_empty(&v)) return;
    int amount = button == 0 ? 1 : v.count;
    ICStack dropped = cc_decr_slot(&v, amount);
    ct_set(r, slot_id, v);
    ct_drop(r, dropped);
}

int gm_container_click(struct GmRuntime *r, int slot_id, int button, int click_type)
{
    if (!r) return 0;
    if (button != 0 && button != 1) return 0;

    if (is_enchant_button(slot_id)) {
        if (r->container != 5 || click_type != CC_CLICK_PICKUP
                || button != 0)
            return 0;
        (void)gm_runtime_enchant_click(
            r, slot_id - GMC_ENCHANT_BUTTON0);
        return 1;
    }

    if (slot_id == GMC_OUTSIDE) {
        if (click_type != CC_CLICK_PICKUP) return 0;
        ICStack cur = gm_player_cursor();
        if (!cc_is_empty(&cur)) {
            if (button == 0) {
                ct_drop(r, cur);
                cur = ic_empty();
            } else {
                ct_drop(r, cc_split_stack(&cur, 1));
            }
            gm_player_cursor_set(cur);
        }
        return 1;
    }

    if (!slot_usable(r, slot_id)) return 0;

    switch (click_type) {
    case CC_CLICK_PICKUP:
        if (slot_id == GMC_RESULT)      click_pickup_result(r, button);
        else if (is_furnace(slot_id))   click_pickup_furnace(r, slot_id, button);
        else if (is_chest(slot_id))     click_pickup_chest(r, slot_id, button);
        else if (is_brewing(slot_id))   click_pickup_brewing(r, slot_id, button);
        else                            click_pickup(r, slot_id, button);
        return 1;
    case CC_CLICK_QUICK_MOVE: {
        /* vanilla retrySlotClick: repeat while the same item keeps moving */
        for (int guard = 0; guard < 512; ++guard) {
            ICStack moved = ct_transfer(r, slot_id);
            if (cc_is_empty(&moved)) break;
            ICStack now = slot_id == GMC_RESULT ? grid_match(r)
                        : is_furnace(slot_id)   ? ct_furnace_get(r, slot_id)
                        : is_chest(slot_id)     ? ct_chest_get(r, slot_id)
                        : is_brewing(slot_id)   ? ct_brewing_get(r, slot_id)
                                                : ct_get(r, slot_id);
            if (cc_is_empty(&now) || now.item != moved.item) break;
        }
        return 1;
    }
    case CC_CLICK_THROW:
        click_throw(r, slot_id, button);
        return 1;
    default:
        return 0;
    }
}

void gm_container_close(struct GmRuntime *r)
{
    if (!r) return;
    if (r->container == 5) {
        for (int i = 0; i < 2; ++i) {
            ICStack v = r->enchanting.slots[i];
            r->enchanting.slots[i] = ic_empty();
            if (cc_is_empty(&v)) continue;
            (void)isr_add_item_stack_to_inventory(&r->player.inv, &v);
            if (!cc_is_empty(&v)) ct_drop(r, v);
        }
    }
    for (int i = 0; i < 9; ++i) {
        ICStack v = r->craft_grid[i];
        r->craft_grid[i] = ic_empty();
        if (cc_is_empty(&v)) continue;
        (void)isr_add_item_stack_to_inventory(&r->player.inv, &v);
        if (!cc_is_empty(&v)) ct_drop(r, v);
    }
    ICStack cur = gm_player_cursor();
    if (!cc_is_empty(&cur)) {
        (void)isr_add_item_stack_to_inventory(&r->player.inv, &cur);
        if (!cc_is_empty(&cur)) ct_drop(r, cur);
        gm_player_cursor_set(ic_empty());
    }
}
