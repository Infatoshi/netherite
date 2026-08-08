/* game/container_live.c - see container_live.h. Vanilla sources:
 * Container.slotClick / mergeItemStack / retrySlotClick, ContainerPlayer /
 * ContainerWorkbench / ContainerFurnace / ContainerChest transferStackInSlot,
 * SlotCrafting, TileEntityFurnace.isItemFuel. Stack arithmetic reuses the
 * verified cc_* helpers from blaze container_click.h; furnace/chest mutation
 * goes through the live wrappers. */
#include "game/container_live.h"
#include "game/runtime.h"
#include "game/crafting_special.h"

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
static int is_offhand(int s) { return s == GMC_OFFHAND; }
static int is_chest(int s)   {
    return (s >= GMC_CHEST0 && s < GMC_CHEST0 + GMC_CHEST_SLOTS)
        || (s >= GMC_CHEST_EXTRA0
            && s < GMC_CHEST_EXTRA0 + GMC_CHEST_SLOTS);
}
static int is_brewing(int s) { return s >= GMC_BREWING0 && s < GMC_BREWING0 + GMC_BREWING_SLOTS; }
static int is_enchant(int s)  { return s >= GMC_ENCHANT0 && s < GMC_ENCHANT0 + GMC_ENCHANT_SLOTS; }
static int is_enchant_button(int s) { return s >= GMC_ENCHANT_BUTTON0 && s < GMC_ENCHANT_BUTTON0 + 3; }
static int is_anvil(int s) { return s >= GMC_ANVIL0 && s < GMC_ANVIL0 + GMC_ANVIL_SLOTS; }
static int is_merchant(int s) { return s >= GMC_MERCHANT0 && s < GMC_MERCHANT0 + GMC_MERCHANT_SLOTS; }
static int is_horse(int s) { return s >= GMC_HORSE0 && s < GMC_HORSE0 + GMC_HORSE_SLOTS; }
static int is_beacon(int s) { return s == GMC_BEACON0; }
static int is_beacon_button(int s) {
    return s >= GMC_BEACON_CONFIRM && s < GMC_OFFHAND;
}

static int horse_inventory_size(
        const GmRuntime *r, const GmHorseState *horse) {
    GmLlamaState llama;
    if (!r || !horse) return 0;
    if (horse->type == EW_TYPE_LLAMA) {
        if (!gm_mobs_get_llama_state(
                &r->mobs, r->active_horse_eid, &llama))
            return 0;
        return horse->chested ? 2 + 3 * llama.strength : 2;
    }
    return horse->chested ? 17 : 2;
}
static int is_merchant_button(int s) {
    return s == GMC_MERCHANT_PREV || s == GMC_MERCHANT_NEXT;
}

static int chest_slot_index(int s)
{
    if (s >= GMC_CHEST0 && s < GMC_CHEST0 + GMC_CHEST_SLOTS)
        return s - GMC_CHEST0;
    if (s >= GMC_CHEST_EXTRA0
            && s < GMC_CHEST_EXTRA0 + GMC_CHEST_SLOTS)
        return GMC_CHEST_SLOTS + s - GMC_CHEST_EXTRA0;
    return -1;
}

static int ct_chest_slot_count(const GmRuntime *r)
{
    if (!r) return GMC_CHEST_SLOTS;
    if (r->container == 10) return GMC_LARGE_CHEST_SLOTS;
    if (r->container == 13) return 9;
    if (r->container == 14) return 5;
    return GMC_CHEST_SLOTS;
}

/* GMC armor id -> IsrInv index (36..39). */
static int armor_isr(int s) { return ISR_ARMOR0 + (s - GMC_ARMOR0); }

/* EntityLiving.getSlotForItemStack + Item.isValidArmor for the armor piece index
 * (0 feet .. 3 head). Elytra (443) is valid only in the chest slot. */
static int armor_item_valid(int item, int armor_idx)
{
    if (item <= 0) return 0;
    if (item == 86 || item == 397) return armor_idx == ITA_SLOT_HEAD;
    if (item == ISR_ELYTRA_ITEM) return armor_idx == ITA_SLOT_CHEST;
    return ita_armor_slot(item) == armor_idx;
}

/* Container.slotClick's branch for a slot that rejects the cursor is looser
 * than ItemStack.areItemsEqual: damage is compared only for subtype-bearing
 * items, while the complete represented NBT payload must still match. */
static int invalid_slot_merge_match(const ICStack *slot, const ICStack *cursor)
{
    if (!slot || !cursor || cc_is_empty(slot) || cc_is_empty(cursor)
            || slot->item != cursor->item
            || cc_max_stack_size(cursor->item, cursor->meta) <= 1
            || (isr_has_subtypes(slot->item)
                && slot->meta != cursor->meta)
            || slot->repair_cost != cursor->repair_cost
            || slot->custom_name != cursor->custom_name
            || slot->tag_id != cursor->tag_id)
        return 0;
    return ic_enchants_equal(slot, cursor);
}

/* ---- ContainerRepair --------------------------------------------------- */

static int anvil_max_damage(int item)
{
    ITAStack stack = ita_mk(item, 0);
    int damage = ita_stack_max_damage(&stack);
    if (damage > 0) return damage;
    if (item == 261) return 384; /* bow */
    if (item == 259) return 64;  /* flint and steel */
    if (item == 442) return 336; /* shield */
    return 0;
}

static int anvil_repair_material(int item)
{
    int material = ita_tool_material(item);
    int armor = ita_armor_material(item);
    if (material == ITA_MAT_WOOD) return 5;
    if (material == ITA_MAT_STONE) return 4;
    if (material == ITA_MAT_IRON) return 265;
    if (material == ITA_MAT_DIAMOND) return 264;
    if (material == ITA_MAT_GOLD) return 266;
    if (armor == ITA_ARM_LEATHER) return 334;
    if (armor == ITA_ARM_CHAIN || armor == ITA_ARM_IRON) return 265;
    if (armor == ITA_ARM_DIAMOND) return 264;
    if (armor == ITA_ARM_GOLD) return 266;
    if (item == 442) return 5;
    if (item == 443) return 334;
    return 0;
}

static int anvil_item_caps(int item)
{
    int kind = et_item_kind_from_id(item);
    if (kind >= 0) return et_item_caps(kind);
    if (item == 443) return ET_T_WEARABLE | ET_T_BREAKABLE;
    if (item == 442) return ET_T_WEARABLE | ET_T_BREAKABLE;
    if (anvil_max_damage(item) > 0) return ET_T_BREAKABLE;
    return 0;
}

int gm_enchantment_can_apply(int enchant_id, int item)
{
    int ndefs = 0;
    const EtDef *defs = et_defs(&ndefs);
    int index = et_find_def(enchant_id, defs, ndefs);
    int caps = anvil_item_caps(item);
    if (index < 0) return 0;
    if (item == IC_ENCHANTED_BOOK) return 1;
    /* EnchantmentThorns overrides canApply: every ItemArmor is accepted even
     * though its registry type is ARMOR_CHEST. */
    if (enchant_id == 7 && (caps & ET_T_ARMOR)) return 1;
    return et_type_matches(defs[index].type_bits, caps);
}

int gm_enchantments_compatible(int a, int b)
{
    int ndefs = 0;
    const EtDef *defs = et_defs(&ndefs);
    int ai = et_find_def(a, defs, ndefs);
    int bi = et_find_def(b, defs, ndefs);
    return ai >= 0 && bi >= 0
        && et_can_apply_together(&defs[ai], &defs[bi]);
}

int gm_enchantment_max_level(int id)
{
    int ndefs = 0;
    const EtDef *defs = et_defs(&ndefs);
    int index = et_find_def(id, defs, ndefs);
    return index < 0 ? 0 : defs[index].max_level;
}

static int anvil_enchant_cost(int id, int from_book)
{
    int ndefs = 0;
    const EtDef *defs = et_defs(&ndefs);
    int index = et_find_def(id, defs, ndefs);
    int cost;
    if (index < 0) return 0;
    cost = defs[index].weight == 10 ? 1
         : defs[index].weight == 5 ? 2
         : defs[index].weight == 2 ? 4 : 8;
    return from_book && cost > 1 ? cost / 2 : cost;
}

static int anvil_find_enchant(const ICStack *stack, int id)
{
    for (int i = 0; i < stack->n_enchants; ++i)
        if (stack->enchants[i].id == id) return i;
    return -1;
}

void gm_anvil_live_init(GmAnvilLive *a)
{
    if (!a) return;
    memset(a, 0, sizeof *a);
    for (int i = 0; i < 3; ++i) a->slots[i] = ic_empty();
}

void gm_anvil_live_open(GmAnvilLive *a, int wx, int wy, int wz)
{
    if (!a) return;
    a->open = 1;
    a->wx = wx;
    a->wy = wy;
    a->wz = wz;
    a->maximum_cost = 0;
    a->material_cost = 0;
    a->repaired_name = 0;
    for (int i = 0; i < 3; ++i) a->slots[i] = ic_empty();
}

void gm_anvil_live_recompute(GmAnvilLive *a, int creative)
{
    ICStack left, right, out;
    int work = 0, base = 0, rename_cost = 0;
    int book;
    if (!a) return;
    left = a->slots[0];
    right = a->slots[1];
    a->slots[2] = ic_empty();
    a->maximum_cost = 0;
    a->material_cost = 0;
    if (cc_is_empty(&left)) return;

    out = left;
    if (out.count > 1 && !creative && out.item != 421) out.count = 1;
    base = left.repair_cost + (cc_is_empty(&right) ? 0 : right.repair_cost);
    book = !cc_is_empty(&right) && right.item == IC_ENCHANTED_BOOK
        && right.n_enchants > 0;

    if (!cc_is_empty(&right)) {
        int max_damage = anvil_max_damage(out.item);
        if (max_damage > 0 && right.item == anvil_repair_material(out.item)) {
            int repair = out.meta < max_damage / 4 ? out.meta : max_damage / 4;
            if (repair <= 0) return;
            int used = 0;
            while (repair > 0 && used < right.count) {
                out.meta -= repair;
                ++work;
                ++used;
                repair = out.meta < max_damage / 4
                    ? out.meta : max_damage / 4;
            }
            a->material_cost = used;
        } else {
            if (!book && (out.item != right.item || max_damage <= 0)) return;
            if (max_damage > 0 && !book) {
                int left_good = max_damage - left.meta;
                int right_good = max_damage - right.meta;
                int combined = left_good + right_good + max_damage * 12 / 100;
                int damage = max_damage - combined;
                if (damage < 0) damage = 0;
                if (damage < out.meta) {
                    out.meta = damage;
                    work += 2;
                }
            }
            {
                int applied = 0, rejected = 0;
                for (int ri = 0; ri < right.n_enchants; ++ri) {
                    int id = right.enchants[ri].id;
                    int oi = anvil_find_enchant(&out, id);
                    int old_level = oi >= 0 ? out.enchants[oi].level : 0;
                    int incoming = right.enchants[ri].level;
                    int level = old_level == incoming
                        ? incoming + 1
                        : (incoming > old_level ? incoming : old_level);
                    int allowed = creative || out.item == IC_ENCHANTED_BOOK
                        || gm_enchantment_can_apply(id, left.item);
                    for (int ei = 0; ei < out.n_enchants; ++ei) {
                        if (out.enchants[ei].id != id
                                && !gm_enchantments_compatible(
                                    id, out.enchants[ei].id)) {
                            allowed = 0;
                            ++work;
                        }
                    }
                    if (!allowed) {
                        rejected = 1;
                        continue;
                    }
                    applied = 1;
                    {
                        int max_level = gm_enchantment_max_level(id);
                        if (level > max_level) level = max_level;
                    }
                    if (oi >= 0) {
                        /* ItemEnchantedBook.setEnchantments delegates to
                         * addEnchantment, which never lowers an already stored
                         * synthetic over-max level.  Its work cost still uses
                         * the clamped computed level. */
                        if (out.item != IC_ENCHANTED_BOOK
                                || out.enchants[oi].level < level)
                            out.enchants[oi].level = (i16)level;
                    } else {
                        if (out.n_enchants >= IC_MAX_ENCHANTS) {
                            rejected = 1;
                            applied = 0;
                            continue;
                        }
                        out.enchants[out.n_enchants].id = (i16)id;
                        out.enchants[out.n_enchants].level = (i16)level;
                        ++out.n_enchants;
                    }
                    work += anvil_enchant_cost(id, book) * level;
                }
                if (rejected && !applied) return;
            }
        }
    }

    if (a->repaired_name == 0) {
        if (left.custom_name != 0) {
            rename_cost = 1;
            ++work;
            out.custom_name = 0;
        }
    } else if (a->repaired_name != left.custom_name) {
        rename_cost = 1;
        ++work;
        out.custom_name = a->repaired_name;
    }

    a->maximum_cost = base + work;
    if (work <= 0) {
        /* ContainerRepair retains the summed prior-work cost even though it
         * clears the result when no repair, enchant, or rename work occurred. */
        return;
    }
    if (rename_cost == work && a->maximum_cost >= 40)
        a->maximum_cost = 39;
    if (a->maximum_cost >= 40 && !creative) return;

    {
        int penalty = out.repair_cost;
        if (!cc_is_empty(&right) && penalty < right.repair_cost)
            penalty = right.repair_cost;
        if (rename_cost != work || rename_cost == 0)
            penalty = penalty * 2 + 1;
        out.repair_cost = penalty;
    }
    a->slots[2] = out;
}

void gm_anvil_live_set_name(GmAnvilLive *a, int custom_name, int creative)
{
    if (!a || custom_name < 0) return;
    a->repaired_name = custom_name;
    gm_anvil_live_recompute(a, creative);
}

/* Grid cell usable for the open container: player screen = vanilla 2x2 (row-major
 * cells 0,1,3,4 of the 3x3), crafting table = all 9, furnace/chest = none. */
static int grid_cell_usable(const GmRuntime *r, int cell)
{
    if (r->container == 1) return 1;
    if (r->container == 2 || r->container == 3 || r->container == 4
            || r->container == 5 || r->container == 6
            || r->container == 7 || r->container == 8
            || r->container == 9 || r->container == 10
            || r->container == 11 || r->container == 13
            || r->container == 14) return 0;
    return cell == 0 || cell == 1 || cell == 3 || cell == 4;
}

static int slot_usable(const GmRuntime *r, int s)
{
    if (is_inv(s)) return 1;
    if (is_grid(s)) return grid_cell_usable(r, s - GMC_GRID0);
    if (s == GMC_RESULT)
        return r->container != 2 && r->container != 3 && r->container != 4
            && r->container != 5 && r->container != 6
            && r->container != 7 && r->container != 8
            && r->container != 9 && r->container != 10
            && r->container != 13 && r->container != 14;
    if (is_furnace(s)) return r->container == 2 && r->active_furnace >= 0;
    /* Armor only on the player inventory screen (ContainerPlayer). */
    if (is_armor(s) || is_offhand(s)) return r->container == 0;
    if (is_chest(s))
        return (r->container == 3 || r->container == 9
                || r->container == 10 || r->container == 13
                || r->container == 14)
            && chest_slot_index(s) < ct_chest_slot_count(r)
            && (r->active_chest >= 0
                || r->active_chest == GM_ACTIVE_ENDER_CHEST
                || (r->active_chest == GM_ACTIVE_SHULKER_BOX
                    && r->active_static_container >= 0)
                || ((r->container == 13 || r->container == 14)
                    && r->active_static_container >= 0));
    if (is_brewing(s))
        return r->container == 4 && r->active_static_container >= 0;
    if (is_enchant(s))
        return r->container == 5 && r->enchanting.open;
    if (is_anvil(s))
        return r->container == 6 && r->anvil.open;
    if (is_merchant(s))
        return r->container == 7 && r->active_villager_eid > 0;
    if (is_horse(s) && r->container == 8 && r->active_horse_eid > 0) {
        GmHorseState horse;
        int index = s - GMC_HORSE0;
        if (!gm_mobs_get_horse_state(
                &r->mobs, r->active_horse_eid, &horse))
            return 0;
        return index < horse_inventory_size(r, &horse)
            && !(horse.type == EW_TYPE_LLAMA && index == 0);
    }
    if (is_beacon(s))
        return r->container == 11 && r->active_static_container >= 0
            && r->active_static_container < r->static_containers_cap
            && r->static_containers[r->active_static_container].active
            && r->static_containers[r->active_static_container].block == 138;
    return 0;
}

static ChestLive *ct_chest(GmRuntime *r)
{
    if (r->container != 3 && r->container != 10) return 0;
    if (r->active_chest == GM_ACTIVE_ENDER_CHEST)
        return &r->ender_chest_inventory;
    if (r->active_chest < 0) return 0;
    return &r->chests[r->active_chest].state;
}

static ChestLive *ct_chest_for_index(GmRuntime *r, int slot)
{
    int index;
    if (!r || slot < 0 || slot >= ct_chest_slot_count(r)) return NULL;
    if (r->active_chest == GM_ACTIVE_ENDER_CHEST)
        return slot < GMC_CHEST_SLOTS
            ? &r->ender_chest_inventory : NULL;
    if (r->active_chest < 0) return NULL;
    index = slot < GMC_CHEST_SLOTS
        ? r->active_chest : r->active_chest_pair;
    if (index < 0 || index >= r->chests_cap
            || !r->chests[index].active)
        return NULL;
    return &r->chests[index].state;
}

static GmRuntimeStaticContainer *ct_shulker(GmRuntime *r)
{
    if (!r || r->container != 9
            || r->active_chest != GM_ACTIVE_SHULKER_BOX
            || r->active_static_container < 0
            || r->active_static_container >= r->static_containers_cap)
        return NULL;
    GmRuntimeStaticContainer *box =
        &r->static_containers[r->active_static_container];
    return box->active && box->block >= 219 && box->block <= 234
        ? box : NULL;
}

static GmRuntimeStaticContainer *ct_static_inventory(GmRuntime *r)
{
    GmRuntimeStaticContainer *inventory;
    int valid_block;
    if (!r || (r->container != 13 && r->container != 14)
            || r->active_static_container < 0
            || r->active_static_container >= r->static_containers_cap)
        return NULL;
    inventory = &r->static_containers[r->active_static_container];
    valid_block = r->container == 13
        ? inventory->block == 23 || inventory->block == 158
        : inventory->block == 154;
    return inventory->active && valid_block
            && inventory->size == ct_chest_slot_count(r)
        ? inventory : NULL;
}

static ICStack ct_chest_get(const GmRuntime *r, int s)
{
    int slot = chest_slot_index(s);
    if ((r->container != 3 && r->container != 9 && r->container != 10
                && r->container != 13 && r->container != 14)
            || slot < 0 || slot >= ct_chest_slot_count(r))
        return ic_empty();
    if (r->container == 13 || r->container == 14) {
        GmRuntimeStaticContainer *inventory =
            ct_static_inventory((GmRuntime *)r);
        return inventory ? inventory->slots[slot] : ic_empty();
    }
    if (r->active_chest == GM_ACTIVE_ENDER_CHEST)
        return chest_live_get(
            &r->ender_chest_inventory, slot);
    if (r->active_chest == GM_ACTIVE_SHULKER_BOX) {
        GmRuntimeStaticContainer *box = ct_shulker((GmRuntime *)r);
        return box ? box->slots[slot] : ic_empty();
    }
    ChestLive *c = ct_chest_for_index((GmRuntime *)r, slot);
    return c ? chest_live_get(c, slot % GMC_CHEST_SLOTS) : ic_empty();
}

static int ct_chest_present(GmRuntime *r)
{
    return ct_chest(r) != NULL || ct_shulker(r) != NULL
        || ct_static_inventory(r) != NULL;
}

static int ct_chest_insert(GmRuntime *r, int slot, ICStack stack)
{
    if (!r || slot < 0 || slot >= ct_chest_slot_count(r)) return 0;
    GmRuntimeStaticContainer *inventory = ct_static_inventory(r);
    if (inventory) {
        ICStack *dst = &inventory->slots[slot];
        int limit = cc_item_stack_limit(&stack);
        int moved;
        if (cc_is_empty(&stack)) return 0;
        if (limit > 64) limit = 64;
        if (cc_is_empty(dst)) {
            moved = stack.count < limit ? stack.count : limit;
            *dst = ic_with_count(&stack, moved);
        } else if (cc_stack_match(dst, &stack)) {
            moved = limit - dst->count;
            if (moved > stack.count) moved = stack.count;
            if (moved <= 0) return 0;
            dst->count += moved;
        } else {
            return 0;
        }
        gm_runtime_static_container_changed(r);
        return moved;
    }
    ChestLive *ch = ct_chest_for_index(r, slot);
    if (ch) return chest_live_insert(
        ch, slot % GMC_CHEST_SLOTS, stack);
    GmRuntimeStaticContainer *box = ct_shulker(r);
    if (!box || cc_is_empty(&stack)
            || (stack.item >= 219 && stack.item <= 234))
        return 0;
    ICStack *dst = &box->slots[slot];
    int limit = cc_item_stack_limit(&stack);
    if (limit > 64) limit = 64;
    int moved;
    if (cc_is_empty(dst)) {
        moved = stack.count < limit ? stack.count : limit;
        *dst = ic_with_count(&stack, moved);
    } else if (cc_stack_match(dst, &stack)) {
        moved = limit - dst->count;
        if (moved > stack.count) moved = stack.count;
        if (moved <= 0) return 0;
        dst->count += moved;
    } else {
        return 0;
    }
    gm_runtime_shulker_box_changed(r);
    return moved;
}

static ICStack ct_chest_extract(GmRuntime *r, int slot, int amount)
{
    if (!r || slot < 0 || slot >= ct_chest_slot_count(r))
        return ic_empty();
    GmRuntimeStaticContainer *inventory = ct_static_inventory(r);
    if (inventory) {
        ICStack out;
        if (amount <= 0) return ic_empty();
        out = cc_split_stack(&inventory->slots[slot], amount);
        if (!cc_is_empty(&out)) gm_runtime_static_container_changed(r);
        return out;
    }
    ChestLive *ch = ct_chest_for_index(r, slot);
    if (ch) return chest_live_extract(
        ch, slot % GMC_CHEST_SLOTS, amount);
    GmRuntimeStaticContainer *box = ct_shulker(r);
    if (!box || amount <= 0) return ic_empty();
    ICStack out = cc_split_stack(&box->slots[slot], amount);
    if (!cc_is_empty(&out)) gm_runtime_shulker_box_changed(r);
    return out;
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

static int ct_horse_state(const GmRuntime *r, GmHorseState *horse)
{
    return r && horse && r->container == 8 && r->active_horse_eid > 0
        && gm_mobs_get_horse_state(
            &r->mobs, r->active_horse_eid, horse);
}

static ICStack ct_horse_get(const GmRuntime *r, int s)
{
    GmHorseState horse;
    int index = s - GMC_HORSE0;
    if (!is_horse(s) || !ct_horse_state(r, &horse)
            || index >= horse_inventory_size(r, &horse))
        return ic_empty();
    return horse.inventory[index];
}

static GmRuntimeStaticContainer *ct_beacon(GmRuntime *r)
{
    GmRuntimeStaticContainer *beacon;
    if (!r || r->container != 11 || r->active_static_container < 0
            || r->active_static_container >= r->static_containers_cap)
        return NULL;
    beacon = &r->static_containers[r->active_static_container];
    return beacon->active && beacon->block == 138 ? beacon : NULL;
}

static ICStack ct_beacon_get(const GmRuntime *r)
{
    GmRuntimeStaticContainer *beacon = ct_beacon((GmRuntime *)r);
    return beacon ? beacon->slots[0] : ic_empty();
}

static int horse_slot_valid(const GmRuntime *r, int s, const ICStack *stack)
{
    GmHorseState horse;
    int index = s - GMC_HORSE0;
    if (!stack || !is_horse(s) || !ct_horse_state(r, &horse)
            || index >= horse_inventory_size(r, &horse)
            || (horse.type == EW_TYPE_LLAMA && index == 0))
        return 0;
    if (cc_is_empty(stack)) return 1;
    if (index == 0) return stack->item == 329;
    if (index == 1)
        return (horse.type == EW_TYPE_HORSE
                    && stack->item >= 417 && stack->item <= 419)
                || (horse.type == EW_TYPE_LLAMA
                    && stack->item == 171
                    && stack->meta >= 0 && stack->meta <= 15);
    return index >= 2;
}

/* ---- direct stack access (inventory + grid + armor) --------------------- */

static ICStack ct_get(const GmRuntime *r, int s)
{
    if (is_inv(s))   return isr_get_stack(&r->player.inv, s);
    if (is_armor(s)) return isr_get_stack(&r->player.inv, armor_isr(s));
    if (is_offhand(s)) return isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
    if (is_grid(s))  return r->craft_grid[s - GMC_GRID0];
    if (is_enchant(s)) return r->enchanting.slots[s - GMC_ENCHANT0];
    if (is_anvil(s)) return r->anvil.slots[s - GMC_ANVIL0];
    if (is_merchant(s)) return r->merchant_slots[s - GMC_MERCHANT0];
    if (is_horse(s)) return ct_horse_get(r, s);
    if (is_beacon(s)) return ct_beacon_get(r);
    return ic_empty();
}

static void ct_set(GmRuntime *r, int s, ICStack v)
{
    cc_normalize(&v);
    if (is_inv(s))        isr_set_stack(&r->player.inv, s, v);
    else if (is_armor(s)) isr_set_stack(&r->player.inv, armor_isr(s), v);
    else if (is_offhand(s))
        isr_set_stack(&r->player.inv, ISR_OFFHAND_SLOT, v);
    else if (is_grid(s))  r->craft_grid[s - GMC_GRID0] = v;
    else if (is_enchant(s))
        enchanting_live_set_slot(
            &r->enchanting, r->world, s - GMC_ENCHANT0, v);
    else if (is_anvil(s) && s != GMC_ANVIL0 + 2) {
        int index = s - GMC_ANVIL0;
        r->anvil.slots[index] = v;
        if (index == 0) r->anvil.repaired_name = v.custom_name;
        gm_anvil_live_recompute(&r->anvil, r->tape_creative != 0);
    } else if (is_merchant(s) && s != GMC_MERCHANT0 + 2) {
        r->merchant_slots[s - GMC_MERCHANT0] = v;
        gm_runtime_merchant_refresh(r);
    } else if (is_horse(s) && horse_slot_valid(r, s, &v)) {
        (void)gm_mobs_set_horse_inventory(
            &r->mobs, r->active_horse_eid, s - GMC_HORSE0, v);
    } else if (is_beacon(s)) {
        GmRuntimeStaticContainer *beacon = ct_beacon(r);
        if (beacon && (cc_is_empty(&v)
                || (gm_runtime_beacon_payment_item(v.item)
                    && v.count == 1)))
            beacon->slots[0] = v;
    }
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
    return furnace_live_get_ic(f, s - GMC_FURNACE0);
}

/* SlotFurnaceOutput owns XP only for a player take. Hopper extraction keeps
 * calling furnace_live_extract_ic directly and therefore awards none. */
static ICStack ct_furnace_take(
        GmRuntime *r, FurnaceLive *f, int fslot, int amount) {
    ICStack got = furnace_live_extract_ic(f, fslot, amount);
    if (fslot == FURNACE_LIVE_SLOT_OUTPUT && !cc_is_empty(&got))
        (void)gm_runtime_furnace_output_taken(r, got, got.count);
    return got;
}

/* ---- vanilla dropItem: a REAL item entity in front of the player -------- */

static void ct_drop(GmRuntime *r, ICStack s)
{
    if (cc_is_empty(&s)) return;
    /* Forge routes Container's two-argument dropItem calls through the exact
     * forward-toss overload, regardless of that obsolete boolean argument. */
    (void)gm_runtime_drop_player_stack(r, s);
}

/* ---- craft result ------------------------------------------------------- */

static ICStack grid_match(GmRuntime *r)
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
    {
        GmCraftSpecial special;
        if (gm_crafting_special_match(r, r->craft_grid, &special) > 0)
            return special.output;
    }
    CRStack res = crf_findMatching(recipes, nrecipes, grid);
    if (crf_isEmpty(res) || res.item == (i32)0xffffffff) return ic_empty();
    return ic_mk(res.item, res.count, res.meta);
}

ICStack gm_container_result(const struct GmRuntime *r)
{
    if (!r || r->container == 2 || r->container == 3 || r->container == 4
            || r->container == 5 || r->container == 6
            || r->container == 7 || r->container == 8
            || r->container == 9 || r->container == 10
            || r->container == 11 || r->container == 13
            || r->container == 14)
        return ic_empty();
    return grid_match((GmRuntime *)r);
}

/* SlotCrafting.onTake: consume one item, then apply the recipe remainder to
 * that cell, the player inventory, or a forward drop in precisely that order. */
static void grid_consume_one(GmRuntime *r)
{
    GmCraftSpecial special;
    int special_match = gm_crafting_special_match(
        r, r->craft_grid, &special) > 0;
    for (int i = 0; i < 9; ++i) {
        ICStack before = r->craft_grid[i];
        ICStack remainder = special_match
            ? special.remainder[i]
            : ic_crafting_container_item(&before);
        if (!cc_is_empty(&before))
            cc_shrink(&r->craft_grid[i], 1);
        if (cc_is_empty(&remainder)) continue;
        if (cc_is_empty(&r->craft_grid[i])) {
            r->craft_grid[i] = remainder;
        } else if (cc_stack_match(&r->craft_grid[i], &remainder)) {
            r->craft_grid[i].count += remainder.count;
        } else {
            (void)isr_add_item_stack_to_inventory(
                &r->player.inv, &remainder);
            if (!cc_is_empty(&remainder)) ct_drop(r, remainder);
        }
    }
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

static int anvil_output_takeable(const GmRuntime *r)
{
    return r && r->container == 6 && r->anvil.open
        && !cc_is_empty(&r->anvil.slots[2])
        && r->anvil.maximum_cost > 0
        && (r->tape_creative
            || r->player_xp_level >= r->anvil.maximum_cost);
}

static ICStack anvil_take_output(GmRuntime *r)
{
    ICStack result;
    int creative, material;
    if (!anvil_output_takeable(r)) return ic_empty();
    result = r->anvil.slots[2];
    creative = r->tape_creative != 0;
    material = r->anvil.material_cost;
    if (!creative) r->player_xp_level -= r->anvil.maximum_cost;

    if (r->anvil.slots[0].count != 1 && !creative
            && r->anvil.slots[0].item != 421) {
        cc_shrink(&r->anvil.slots[0], 1);
    } else {
        r->anvil.slots[0] = ic_empty();
    }
    if (material > 0 && r->anvil.slots[1].count > material)
        cc_shrink(&r->anvil.slots[1], material);
    else
        r->anvil.slots[1] = ic_empty();
    r->anvil.slots[2] = ic_empty();
    gm_runtime_anvil_finish(r, creative);
    gm_anvil_live_recompute(&r->anvil, creative);
    /* ContainerRepair.onTake assigns maximumCost=0 after the input
     * InventoryBasic callbacks have recomputed their preview. */
    r->anvil.maximum_cost = 0;
    return result;
}

static ICStack merchant_take_output(GmRuntime *r)
{
    ICStack preview, output = ic_empty();
    int xp = 0;
    if (!r || r->container != 7 || r->active_villager_eid <= 0
            || r->merchant_offer_index < 0
            || cc_is_empty(&r->merchant_slots[2]))
        return ic_empty();
    preview = r->merchant_slots[2];
    if (!gm_runtime_villager_trade_execute(
            r, r->active_villager_eid, r->merchant_offer_index,
            &r->merchant_slots[0], &r->merchant_slots[1],
            &output, &xp))
        return ic_empty();
    (void)xp;
    gm_runtime_merchant_refresh(r);
    return cc_is_empty(&output) ? preview : output;
}

/* ---- transferStackInSlot (QUICK_MOVE, one attempt) ----------------------
 * Returns the original stack when something moved (vanilla contract), empty
 * when nothing moved. */
static ICStack ct_transfer(GmRuntime *r, int slot_id)
{
    int order[36];

    if (is_beacon(slot_id)) {
        ICStack v = ct_beacon_get(r);
        ICStack original = v;
        int before;
        if (cc_is_empty(&v)) return ic_empty();
        order_reverse_all(order);
        before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        ct_set(r, slot_id, v);
        return original;
    }

    if (is_horse(slot_id)) {
        ICStack v = ct_horse_get(r, slot_id);
        ICStack original = v;
        int before;
        if (cc_is_empty(&v)) return ic_empty();
        order_reverse_all(order);
        before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        ct_set(r, slot_id, v);
        return original;
    }

    if (is_merchant(slot_id)) {
        ICStack v = r->merchant_slots[slot_id - GMC_MERCHANT0];
        ICStack original = v;
        int before;
        if (cc_is_empty(&v)) return ic_empty();
        order_reverse_all(order);
        before = v.count;
        if (slot_id == GMC_MERCHANT0 + 2) {
            if (!ct_merge_order(r, &v, order, 36)
                    || v.count == before)
                return ic_empty();
            (void)merchant_take_output(r);
            return original;
        }
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        ct_set(r, slot_id, v);
        return original;
    }

    if (is_anvil(slot_id)) {
        ICStack v = r->anvil.slots[slot_id - GMC_ANVIL0];
        if (cc_is_empty(&v)) return ic_empty();
        ICStack original = v;
        order_reverse_all(order);
        if (slot_id == GMC_ANVIL0 + 2) {
            if (!anvil_output_takeable(r)) return ic_empty();
            ICStack rem = v;
            if (!ct_merge_order(r, &rem, order, 36)
                    || !cc_is_empty(&rem))
                return ic_empty();
            (void)anvil_take_output(r);
            return original;
        }
        {
            int before = v.count;
            ct_merge_order(r, &v, order, 36);
            if (v.count == before) return ic_empty();
            ct_set(r, slot_id, v);
            return original;
        }
    }

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
        (void)ct_furnace_take(
            r, f, slot_id - GMC_FURNACE0, before - v.count);
        return original;
    }

    /* ContainerChest: chest slots merge reverse into player inv */
    if (is_chest(slot_id)) {
        ICStack v = ct_chest_get(r, slot_id);
        if (!ct_chest_present(r) || cc_is_empty(&v)) return ic_empty();
        ICStack original = v;
        order_reverse_all(order);
        int before = v.count;
        ct_merge_order(r, &v, order, 36);
        if (v.count == before) return ic_empty();
        (void)ct_chest_extract(
            r, chest_slot_index(slot_id), before - v.count);
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
        if (slot_id - GMC_BREWING0 <= BREWING_LIVE_POTION2)
            (void)gm_runtime_brewed_potion_taken(r, original);
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
    if (is_armor(slot_id) || is_offhand(slot_id)) {
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
            SRStack in = sr_mk(v.item, v.count, v.meta);
            int target = -1;
            SRStack smelted = sr_getSmeltingResultBuiltin(in);
            if (!sr_isEmpty(smelted) && smelted.item != (i32)0xffffffff)
                target = FURNACE_LIVE_SLOT_INPUT;
            else if (sr_getItemBurnTime(in) > 0)
                target = FURNACE_LIVE_SLOT_FUEL;
            if (target >= 0) {
                int moved = furnace_live_insert_ic(f, target, v);
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
        if (v.item == 442) {
            ICStack offhand = ct_get(r, GMC_OFFHAND);
            if (cc_is_empty(&offhand)) {
                /* Slot offHand is an ordinary Slot with the item's own limit;
                 * ContainerPlayer.mergeItemStack therefore moves the whole
                 * shield stack, including synthetic count>1 oracle stacks. */
                ct_set(r, GMC_OFFHAND, v);
                ct_set(r, slot_id, ic_empty());
                return original;
            }
        }
        if (v.item == 86 || v.item == 397) want = ITA_SLOT_HEAD;
        else if (v.item == ISR_ELYTRA_ITEM) want = ITA_SLOT_CHEST;
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
    if (r->container == 3 || r->container == 9 || r->container == 10
            || r->container == 13 || r->container == 14) {
        if (ct_chest_present(r) && is_inv(slot_id)) {
            ICStack rem = v;
            for (int s = 0; s < ct_chest_slot_count(r)
                    && !cc_is_empty(&rem); ++s) {
                int moved = ct_chest_insert(r, s, rem);
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
            ICStack moved;
            if (target == 0 && v.count > 1) {
                /* ContainerEnchantment constructs a fresh one-count stack in
                 * this branch, intentionally dropping the source NBT. Only a
                 * singleton tagged input takes the copy-preserving branch. */
                moved = ic_mk(v.item, 1, v.meta);
                cc_shrink(&v, 1);
            } else {
                int amount = target == 0 ? 1 : v.count;
                moved = cc_split_stack(&v, amount);
            }
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

    /* ContainerRepair inventory source: merge through both input slots in
     * order; the take-only output slot rejects insertion. */
    if (r->container == 6 && is_inv(slot_id)) {
        int targets[2] = {GMC_ANVIL0, GMC_ANVIL0 + 1};
        int before = v.count;
        ct_merge_order(r, &v, targets, 2);
        if (v.count == before) return ic_empty();
        ct_set(r, slot_id, v);
        return original;
    }

    /* ContainerHorseInventory: armor, then saddle, then chest storage. */
    if (r->container == 8 && is_inv(slot_id)) {
        GmHorseState horse;
        int target = -1;
        if (!ct_horse_state(r, &horse)) return ic_empty();
        if (horse.type == EW_TYPE_HORSE && v.count > 0
                && v.item >= 417 && v.item <= 419
                && cc_is_empty(&horse.inventory[1]))
            target = GMC_HORSE0 + 1;
        else if (horse.type == EW_TYPE_LLAMA && v.count > 0
                && v.item == 171 && v.meta >= 0 && v.meta <= 15
                && cc_is_empty(&horse.inventory[1]))
            target = GMC_HORSE0 + 1;
        else if (horse.type != EW_TYPE_LLAMA
                && v.item == 329 && v.count > 0
                && cc_is_empty(&horse.inventory[0]))
            target = GMC_HORSE0;
        if (target >= 0) {
            ICStack one = cc_split_stack(&v, 1);
            ct_set(r, target, one);
            ct_set(r, slot_id, v);
            return original;
        }
        if (horse.chested) {
            int storage[15];
            int storage_count = horse_inventory_size(r, &horse) - 2;
            int before = v.count;
            for (int i = 0; i < storage_count; ++i)
                storage[i] = GMC_HORSE0 + 2 + i;
            ct_merge_order(r, &v, storage, storage_count);
            if (v.count != before) {
                ct_set(r, slot_id, v);
                return original;
            }
        }
        return ic_empty();
    }

    /* ContainerBeacon: the payment slot accepts exactly one valid item.
     * Forge's 1.11.2 transfer fix still leaves main/hotbar routing available
     * when the slot is invalid or already occupied. */
    if (r->container == 11 && is_inv(slot_id)
            && gm_runtime_beacon_payment_item(v.item)) {
        ICStack payment = ct_beacon_get(r);
        if (cc_is_empty(&payment)) {
            ct_set(r, GMC_BEACON0, cc_split_stack(&v, 1));
            ct_set(r, slot_id, v);
            /* Forge 1.11.2 ContainerBeacon returns EMPTY after this
             * successful merge, suppressing Container.retrySlotClick. */
            return ic_empty();
        }
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

static int furnace_player_insert(
        FurnaceLive *furnace, int slot, ICStack stack) {
    if (slot == FURNACE_LIVE_SLOT_FUEL && stack.item == 325) {
        ICStack before = furnace_live_get_ic(furnace, slot);
        if (!cc_is_empty(&before)) return 0;
        stack.count = 1;
        furnace_live_set_ic(furnace, slot, stack);
        return 1;
    }
    return furnace_live_insert_ic(furnace, slot, stack);
}

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
        ICStack inserted = cur;
        inserted.count = amount;
        int moved = furnace_player_insert(f, fslot, inserted);
        if (moved > 0) cc_shrink(&cur, moved);
    } else if (cc_is_empty(&cur)) {
        int take = button == 0 ? v.count : (v.count + 1) / 2;
        ICStack got = ct_furnace_take(r, f, fslot, take);
        if (!cc_is_empty(&got)) cur = got;
    } else if (fslot == FURNACE_LIVE_SLOT_OUTPUT
            ? invalid_slot_merge_match(&v, &cur)
            : cc_stack_match(&v, &cur)) {
        if (fslot == FURNACE_LIVE_SLOT_OUTPUT) {
            /* SlotFurnaceOutput rejects insertion, but Container.slotClick's
             * invalid-slot branch still gathers its whole matching stack into
             * a non-full cursor and then invokes onTake. */
            if (cur.count + v.count
                    <= cc_max_stack_size(cur.item, cur.meta)) {
                ICStack got = ct_furnace_take(r, f, fslot, v.count);
                if (!cc_is_empty(&got)) cur.count += got.count;
            }
        } else {
            int amount = button == 0 ? cur.count : 1;
            ICStack inserted = cur;
            inserted.count = amount;
            int moved = furnace_player_insert(f, fslot, inserted);
            if (moved > 0) cc_shrink(&cur, moved);
        }
    } else {
        /* swap, only when the slot would accept the cursor stack whole */
        if (fslot == FURNACE_LIVE_SLOT_OUTPUT) return;
        if (fslot == FURNACE_LIVE_SLOT_FUEL && cur.item != 325 &&
            sr_getItemBurnTime(sr_mk(cur.item, cur.count, cur.meta)) <= 0)
            return;
        ICStack got = ct_furnace_take(r, f, fslot, v.count);
        int moved = furnace_player_insert(f, fslot, cur);
        if (moved == cur.count) {
            cur = got;
        } else {
            /* could not place the whole cursor: undo */
            if (moved > 0) (void)furnace_live_extract_ic(f, fslot, moved);
            (void)furnace_live_insert_ic(f, fslot, got);
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
    } else if (invalid_slot_merge_match(&res, &cur) &&
               cur.count + res.count <= cc_max_stack_size(cur.item, cur.meta)) {
        cur.count += res.count;
    } else {
        return;
    }
    grid_consume_one(r);
    gm_player_cursor_set(cur);
}

static void click_pickup_anvil(GmRuntime *r, int button)
{
    ICStack res, cur;
    (void)button;
    if (!anvil_output_takeable(r)) return;
    res = r->anvil.slots[2];
    cur = gm_player_cursor();
    if (cc_is_empty(&cur)) {
        cur = res;
    } else if (invalid_slot_merge_match(&res, &cur)
            && cur.count + res.count
                <= cc_max_stack_size(cur.item, cur.meta)) {
        cur.count += res.count;
    } else {
        return;
    }
    (void)anvil_take_output(r);
    gm_player_cursor_set(cur);
}

static void click_pickup_merchant(GmRuntime *r, int button)
{
    ICStack res, cur, taken;
    (void)button;
    res = r->merchant_slots[2];
    if (cc_is_empty(&res)) return;
    cur = gm_player_cursor();
    if (cc_is_empty(&cur)) {
        cur = res;
    } else if (invalid_slot_merge_match(&res, &cur)
            && cur.count + res.count
                <= cc_max_stack_size(cur.item, cur.meta)) {
        cur.count += res.count;
    } else {
        return;
    }
    taken = merchant_take_output(r);
    if (cc_is_empty(&taken)) return;
    gm_player_cursor_set(cur);
}

static void click_pickup_chest(GmRuntime *r, int slot_id, int button)
{
    if (!ct_chest_present(r)) return;
    int cslot = chest_slot_index(slot_id);
    ICStack cur = gm_player_cursor();
    ICStack v = ct_chest_get(r, slot_id);

    if (cc_is_empty(&v)) {
        if (cc_is_empty(&cur)) return;
        int amount = button == 0 ? cur.count : 1;
        /* Deposit retains StoredEnchantments (ic_mk alone would strip them). */
        int moved = ct_chest_insert(r, cslot, ic_with_count(&cur, amount));
        if (moved > 0) cc_shrink(&cur, moved);
    } else if (cc_is_empty(&cur)) {
        int take = button == 0 ? v.count : (v.count + 1) / 2;
        cur = ct_chest_extract(r, cslot, take);
    } else if (cc_stack_match(&v, &cur)) {
        int amount = button == 0 ? cur.count : 1;
        int moved = ct_chest_insert(r, cslot, ic_with_count(&cur, amount));
        if (moved > 0) cc_shrink(&cur, moved);
    } else {
        /* swap whole stacks when the cursor fits the slot limit */
        i32 lim = cc_item_stack_limit(&cur);
        if (cur.count <= lim) {
            ICStack taken = ct_chest_extract(r, cslot, v.count);
            int moved = ct_chest_insert(r, cslot, cur);
            if (moved == cur.count) {
                cur = taken;
            } else {
                if (moved > 0) (void)ct_chest_extract(r, cslot, moved);
                (void)ct_chest_insert(r, cslot, taken);
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
    ICStack taken_for_hook = ic_empty();
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
        taken_for_hook = cur;
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
    if (changed) {
        gm_runtime_brewing_changed(r);
        if (bslot <= BREWING_LIVE_POTION2)
            (void)gm_runtime_brewed_potion_taken(r, taken_for_hook);
    }
}

static void click_pickup(GmRuntime *r, int slot_id, int button)
{
    ICStack cur = gm_player_cursor();
    ICStack v = ct_get(r, slot_id);
    int armor = is_armor(slot_id);
    int enchant = is_enchant(slot_id);
    int horse = is_horse(slot_id);
    int beacon = is_beacon(slot_id);
    int armor_idx = armor ? (slot_id - GMC_ARMOR0) : -1;
    /* Slot.getSlotStackLimit: armor slots accept exactly one item. */
    i32 slot_lim = armor || beacon || (enchant && slot_id == GMC_ENCHANT0)
            || (horse && slot_id < GMC_HORSE0 + 2)
        ? 1 : cc_slot_stack_limit();

    if (cc_is_empty(&v)) {
        if (!cc_is_empty(&cur)) {
            if (armor && !armor_item_valid(cur.item, armor_idx)) return;
            if (enchant && !enchanting_live_slot_valid(
                    slot_id - GMC_ENCHANT0, &cur)) return;
            if (horse && !horse_slot_valid(r, slot_id, &cur)) return;
            if (beacon && !gm_runtime_beacon_payment_item(cur.item)) return;
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
        if (armor || beacon || (enchant && slot_id == GMC_ENCHANT0)) return;
        int j2 = button == 0 ? cur.count : 1;
        i32 lim = cc_item_stack_limit(&cur);
        i32 maxs = cc_max_stack_size(cur.item, cur.meta);
        if (j2 > lim - v.count) j2 = lim - v.count;
        if (j2 > maxs - v.count) j2 = maxs - v.count;
        /* Vanilla deliberately does not clamp j2 at zero here.  Synthetic
         * overstacked saves therefore move the excess back to the cursor. */
        cc_shrink(&cur, j2);
        cc_grow(&v, j2);
    } else {
        if (armor && !armor_item_valid(cur.item, armor_idx)) return;
        if (enchant && !enchanting_live_slot_valid(
                slot_id - GMC_ENCHANT0, &cur)) return;
        if (horse && !horse_slot_valid(r, slot_id, &cur)) return;
        if (beacon && !gm_runtime_beacon_payment_item(cur.item)) return;
        if ((armor || beacon || (horse && slot_id < GMC_HORSE0 + 2))
                && cur.count > 1)
            return; /* swap only whole single pieces */
        i32 lim = armor || beacon || (horse && slot_id < GMC_HORSE0 + 2)
            ? 1 : cc_item_stack_limit(&cur);
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
    if (slot_id == GMC_ANVIL0 + 2) {
        ICStack got = anvil_take_output(r);
        if (!cc_is_empty(&got)) ct_drop(r, got);
        return;
    }
    if (slot_id == GMC_MERCHANT0 + 2) {
        ICStack got = merchant_take_output(r);
        if (!cc_is_empty(&got)) ct_drop(r, got);
        return;
    }
    if (is_furnace(slot_id)) {
        FurnaceLive *f = ct_furnace(r);
        ICStack v = ct_furnace_get(r, slot_id);
        if (!f || cc_is_empty(&v)) return;
        int amount = button == 0 ? 1 : v.count;
        ICStack got = ct_furnace_take(
            r, f, slot_id - GMC_FURNACE0, amount);
        if (!cc_is_empty(&got)) ct_drop(r, got);
        return;
    }
    if (is_chest(slot_id)) {
        ICStack v = ct_chest_get(r, slot_id);
        if (!ct_chest_present(r) || cc_is_empty(&v)) return;
        int amount = button == 0 ? 1 : v.count;
        ICStack got = ct_chest_extract(
            r, chest_slot_index(slot_id), amount);
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
            if (slot_id - GMC_BREWING0 <= BREWING_LIVE_POTION2)
                (void)gm_runtime_brewed_potion_taken(r, got);
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

static ICStack ct_any_get(const GmRuntime *r, int slot_id)
{
    if (slot_id == GMC_RESULT) return grid_match((GmRuntime *)r);
    if (is_furnace(slot_id)) return ct_furnace_get(r, slot_id);
    if (is_chest(slot_id)) return ct_chest_get(r, slot_id);
    if (is_brewing(slot_id)) return ct_brewing_get(r, slot_id);
    return ct_get(r, slot_id);
}

int gm_container_slot_stack(
        const struct GmRuntime *r, int slot_id, ICStack *out)
{
    if (!r || !out || !slot_usable(r, slot_id)) return 0;
    *out = ct_any_get(r, slot_id);
    return 1;
}

static int slot_inventory_group(const GmRuntime *r, int slot)
{
    if (!r || !slot_usable(r, slot)) return 0;
    if (is_inv(slot) || is_armor(slot) || is_offhand(slot)) return 1;
    if (is_grid(slot)) return 2;
    if (slot == GMC_RESULT) return 3;
    if (is_furnace(slot)) return 4;
    if (is_chest(slot)) return 5;
    if (is_brewing(slot)) return 6;
    if (is_enchant(slot)) return 7;
    if (is_anvil(slot))
        return slot == GMC_ANVIL0 + 2 ? 9 : 8;
    if (is_merchant(slot)) return 10;
    if (is_horse(slot)) return 11;
    if (is_beacon(slot)) return 12;
    return 0;
}

int gm_container_slots_same_inventory(
        const struct GmRuntime *r, int first, int second)
{
    int a = slot_inventory_group(r, first);
    return a != 0 && a == slot_inventory_group(r, second);
}

static int ct_slot_limit(
        const GmRuntime *r, int slot_id, const ICStack *stack)
{
    (void)r;
    if (is_armor(slot_id) || is_beacon(slot_id)
            || (is_enchant(slot_id) && slot_id == GMC_ENCHANT0)
            || (is_horse(slot_id) && slot_id < GMC_HORSE0 + 2)
            || (is_brewing(slot_id)
                && slot_id < GMC_BREWING0 + 3)
            || (is_furnace(slot_id)
                && slot_id == GMC_FURNACE0 + FURNACE_LIVE_SLOT_FUEL
                && stack && stack->item == 325))
        return 1;
    return 64;
}

static int ct_slot_valid(
        const GmRuntime *r, int slot_id, const ICStack *stack)
{
    if (!r || !stack || !slot_usable(r, slot_id)) return 0;
    if (cc_is_empty(stack)) return 1;
    if (slot_id == GMC_RESULT || slot_id == GMC_FURNACE0 + 2
            || slot_id == GMC_ANVIL0 + 2
            || slot_id == GMC_MERCHANT0 + 2)
        return 0;
    if (is_armor(slot_id))
        return armor_item_valid(stack->item, slot_id - GMC_ARMOR0);
    if (is_furnace(slot_id)) {
        int fslot = slot_id - GMC_FURNACE0;
        return fslot == FURNACE_LIVE_SLOT_INPUT
            || (fslot == FURNACE_LIVE_SLOT_FUEL
                && (stack->item == 325
                    || sr_getItemBurnTime(sr_mk(
                        stack->item, stack->count, stack->meta)) > 0));
    }
    if (is_chest(slot_id))
        return r->container != 9
            || stack->item < 219 || stack->item > 234;
    if (is_brewing(slot_id))
        return brewing_live_slot_valid(
            slot_id - GMC_BREWING0, stack);
    if (is_enchant(slot_id))
        return enchanting_live_slot_valid(
            slot_id - GMC_ENCHANT0, stack);
    if (is_horse(slot_id)) return horse_slot_valid(r, slot_id, stack);
    if (is_beacon(slot_id))
        return gm_runtime_beacon_payment_item(stack->item);
    return 1;
}

static int ct_slot_can_take(const GmRuntime *r, int slot_id)
{
    if (!r || !slot_usable(r, slot_id)) return 0;
    if (is_armor(slot_id) && !r->tape_creative) {
        ICStack stack = ct_get(r, slot_id);
        if (!cc_is_empty(&stack)
                && ic_enchantment_level(&stack, 10) > 0)
            return 0;
    }
    if (slot_id == GMC_ANVIL0 + 2) return anvil_output_takeable(r);
    return 1;
}

static int ct_any_set(GmRuntime *r, int slot_id, ICStack next)
{
    ICStack before = ct_any_get(r, slot_id);
    cc_normalize(&next);
    if (is_furnace(slot_id)) {
        FurnaceLive *furnace = ct_furnace(r);
        int fslot = slot_id - GMC_FURNACE0;
        if (!furnace || (fslot == FURNACE_LIVE_SLOT_OUTPUT
                && !cc_is_empty(&next))) return 0;
        furnace_live_set_ic(furnace, fslot, next);
        if (fslot == FURNACE_LIVE_SLOT_INPUT
                && (!cc_stack_match(&before, &next)
                    || cc_is_empty(&before) != cc_is_empty(&next))) {
            furnace->total_cook = 200;
            furnace->cook_time = 0;
        }
        return 1;
    }
    if (is_chest(slot_id)) {
        int slot = chest_slot_index(slot_id);
        ICStack removed = ct_chest_extract(r, slot, before.count);
        if (cc_is_empty(&next)) return 1;
        if (ct_chest_insert(r, slot, next) == next.count) return 1;
        (void)ct_chest_extract(r, slot, next.count);
        if (!cc_is_empty(&removed))
            (void)ct_chest_insert(r, slot, removed);
        return 0;
    }
    if (is_brewing(slot_id)) {
        GmRuntimeStaticContainer *stand = ct_brewing(r);
        if (!stand) return 0;
        stand->slots[slot_id - GMC_BREWING0] = next;
        gm_runtime_brewing_changed(r);
        return 1;
    }
    if (slot_id == GMC_RESULT || slot_id == GMC_ANVIL0 + 2
            || slot_id == GMC_MERCHANT0 + 2)
        return cc_is_empty(&next);
    ct_set(r, slot_id, next);
    return 1;
}

static ICStack ct_any_take(GmRuntime *r, int slot_id, int amount)
{
    ICStack stack;
    if (!r || amount <= 0 || !ct_slot_can_take(r, slot_id))
        return ic_empty();
    if (is_furnace(slot_id)) {
        FurnaceLive *furnace = ct_furnace(r);
        return furnace ? ct_furnace_take(
            r, furnace, slot_id - GMC_FURNACE0, amount) : ic_empty();
    }
    if (is_chest(slot_id))
        return ct_chest_extract(r, chest_slot_index(slot_id), amount);
    if (is_brewing(slot_id)) {
        GmRuntimeStaticContainer *stand = ct_brewing(r);
        ICStack taken = stand ? brewing_live_extract(
            stand->slots, slot_id - GMC_BREWING0, amount) : ic_empty();
        if (!cc_is_empty(&taken)) gm_runtime_brewing_changed(r);
        return taken;
    }
    if (slot_id == GMC_ANVIL0 + 2) {
        stack = anvil_take_output(r);
        if (stack.count > amount) stack.count = amount;
        return stack;
    }
    if (slot_id == GMC_MERCHANT0 + 2) {
        stack = merchant_take_output(r);
        if (stack.count > amount) stack.count = amount;
        return stack;
    }
    stack = ct_any_get(r, slot_id);
    {
        ICStack taken = cc_split_stack(&stack, amount);
        if (!cc_is_empty(&taken)) (void)ct_any_set(r, slot_id, stack);
        return taken;
    }
}

static void click_pickup_dispatch(GmRuntime *r, int slot_id, int button)
{
    if (slot_id == GMC_RESULT) click_pickup_result(r, button);
    else if (slot_id == GMC_ANVIL0 + 2) click_pickup_anvil(r, button);
    else if (slot_id == GMC_MERCHANT0 + 2)
        click_pickup_merchant(r, button);
    else if (is_furnace(slot_id)) click_pickup_furnace(r, slot_id, button);
    else if (is_chest(slot_id)) click_pickup_chest(r, slot_id, button);
    else if (is_brewing(slot_id)) click_pickup_brewing(r, slot_id, button);
    else click_pickup(r, slot_id, button);
}

static void container_drag_reset(GmRuntime *r)
{
    r->container_drag_event = 0;
    r->container_drag_mode = 0;
    memset(r->container_drag_slots, 0, sizeof r->container_drag_slots);
}

static int click_quick_craft(GmRuntime *r, int slot_id, int button)
{
    int previous = r->container_drag_event;
    int event = button & 3;
    int mode = (button >> 2) & 3;
    ICStack cursor = gm_player_cursor();
    r->container_drag_event = event;
    if ((previous != 1 || event != 2) && previous != event) {
        container_drag_reset(r);
    } else if (cc_is_empty(&cursor)) {
        container_drag_reset(r);
    } else if (event == 0) {
        r->container_drag_mode = mode;
        if (mode == 0 || mode == 1 || (mode == 2 && r->tape_creative)) {
            r->container_drag_event = 1;
            memset(r->container_drag_slots, 0,
                   sizeof r->container_drag_slots);
        } else {
            container_drag_reset(r);
        }
    } else if (event == 1) {
        int selected = 0;
        ICStack stack;
        for (int slot = 0; slot < GMC_SLOT_COUNT; ++slot)
            selected += r->container_drag_slots[slot] != 0;
        if (slot_id >= 0 && slot_id < GMC_SLOT_COUNT
                && slot_usable(r, slot_id)
                && ct_slot_valid(r, slot_id, &cursor)
                && (r->container_drag_mode == 2
                    || cursor.count > selected)) {
            stack = ct_any_get(r, slot_id);
            if (cc_is_empty(&stack)
                    || (cc_stack_match(&stack, &cursor)
                        && stack.count <= cc_max_stack_size(
                            cursor.item, cursor.meta)))
                r->container_drag_slots[slot_id] = 1;
        }
    } else if (event == 2) {
        int selected = 0;
        int remaining = cursor.count;
        ICStack original = cursor;
        for (int slot = 0; slot < GMC_SLOT_COUNT; ++slot)
            selected += r->container_drag_slots[slot] != 0;
        if (selected > 0) {
            for (int slot = 0; slot < GMC_SLOT_COUNT; ++slot) {
                ICStack before, after;
                int base, target, limit, delta;
                if (!r->container_drag_slots[slot]
                        || !slot_usable(r, slot)
                        || !ct_slot_valid(r, slot, &original)
                        || (r->container_drag_mode != 2
                            && original.count < selected))
                    continue;
                before = ct_any_get(r, slot);
                if (!cc_is_empty(&before)
                        && !cc_stack_match(&before, &original))
                    continue;
                base = cc_is_empty(&before) ? 0 : before.count;
                target = r->container_drag_mode == 0
                    ? original.count / selected
                    : r->container_drag_mode == 1 ? 1
                    : cc_max_stack_size(original.item, original.meta);
                target += base;
                limit = ct_slot_limit(r, slot, &original);
                if (limit > cc_max_stack_size(original.item, original.meta))
                    limit = cc_max_stack_size(original.item, original.meta);
                if (target > limit) target = limit;
                delta = target - base;
                if (delta <= 0) continue;
                after = original;
                after.count = target;
                if (ct_any_set(r, slot, after)) remaining -= delta;
            }
            original.count = remaining;
            cc_normalize(&original);
            gm_player_cursor_set(original);
        }
        container_drag_reset(r);
    } else {
        container_drag_reset(r);
    }
    return 1;
}

static void click_swap(GmRuntime *r, int slot_id, int hotbar)
{
    ICStack hot = ct_get(r, hotbar);
    ICStack clicked = ct_any_get(r, slot_id);
    int limit;
    if (slot_id == hotbar || (cc_is_empty(&hot) && cc_is_empty(&clicked)))
        return;
    if (cc_is_empty(&hot)) {
        ICStack taken;
        if (!ct_slot_can_take(r, slot_id)) return;
        taken = ct_any_take(r, slot_id, clicked.count);
        if (!cc_is_empty(&taken)) {
            ct_set(r, hotbar, taken);
            if (is_brewing(slot_id)
                    && slot_id - GMC_BREWING0 <= BREWING_LIVE_POTION2)
                (void)gm_runtime_brewed_potion_taken(r, taken);
        }
        return;
    }
    if (!ct_slot_valid(r, slot_id, &hot)) return;
    limit = ct_slot_limit(r, slot_id, &hot);
    if (cc_is_empty(&clicked)) {
        ICStack placed = hot;
        if (placed.count > limit) placed.count = limit;
        if (ct_any_set(r, slot_id, placed)) {
            hot.count -= placed.count;
            cc_normalize(&hot);
            ct_set(r, hotbar, hot);
        }
        return;
    }
    if (!ct_slot_can_take(r, slot_id)) return;
    if (hot.count > limit) {
        ICStack placed = hot;
        ICStack old;
        placed.count = limit;
        old = ct_any_take(r, slot_id, clicked.count);
        if (!ct_any_set(r, slot_id, placed)) {
            (void)ct_any_set(r, slot_id, old);
            return;
        }
        if (is_brewing(slot_id)
                && slot_id - GMC_BREWING0 <= BREWING_LIVE_POTION2)
            (void)gm_runtime_brewed_potion_taken(r, old);
        hot.count -= limit;
        ct_set(r, hotbar, hot);
        (void)isr_add_item_stack_to_inventory(&r->player.inv, &old);
        if (!cc_is_empty(&old)) ct_drop(r, old);
    } else {
        ICStack old = ct_any_take(r, slot_id, clicked.count);
        if (ct_any_set(r, slot_id, hot)) {
            ct_set(r, hotbar, old);
            if (is_brewing(slot_id)
                    && slot_id - GMC_BREWING0 <= BREWING_LIVE_POTION2)
                (void)gm_runtime_brewed_potion_taken(r, old);
        } else {
            (void)ct_any_set(r, slot_id, old);
        }
    }
}

static int container_slot_order(const GmRuntime *r, int order[GMC_SLOT_COUNT])
{
    int count = 0;
#define ADD(slot) do { order[count++] = (slot); } while (0)
    if (r->container == 0) {
        static const int player_prefix[] = {
            GMC_RESULT, GMC_GRID0, GMC_GRID0 + 1,
            GMC_GRID0 + 3, GMC_GRID0 + 4,
            GMC_ARMOR0 + 3, GMC_ARMOR0 + 2,
            GMC_ARMOR0 + 1, GMC_ARMOR0,
        };
        for (size_t i = 0; i < sizeof player_prefix / sizeof player_prefix[0]; ++i)
            ADD(player_prefix[i]);
    } else if (r->container == 1) {
        ADD(GMC_RESULT);
        for (int slot = GMC_GRID0; slot < GMC_RESULT; ++slot) ADD(slot);
    } else if (r->container == 2) {
        for (int slot = GMC_FURNACE0; slot < GMC_FURNACE0 + 3; ++slot)
            ADD(slot);
    } else if (r->container == 3 || r->container == 9
            || r->container == 10 || r->container == 13
            || r->container == 14) {
        int slots = ct_chest_slot_count(r);
        for (int slot = 0; slot < slots; ++slot)
            ADD(slot < 27 ? GMC_CHEST0 + slot
                          : GMC_CHEST_EXTRA0 + slot - 27);
    } else if (r->container == 4) {
        for (int slot = GMC_BREWING0; slot < GMC_BREWING0 + 5; ++slot)
            ADD(slot);
    } else if (r->container == 5) {
        ADD(GMC_ENCHANT0); ADD(GMC_ENCHANT0 + 1);
    } else if (r->container == 6) {
        for (int slot = GMC_ANVIL0; slot < GMC_ANVIL0 + 3; ++slot)
            ADD(slot);
    } else if (r->container == 7) {
        for (int slot = GMC_MERCHANT0; slot < GMC_MERCHANT0 + 3; ++slot)
            ADD(slot);
    } else if (r->container == 8) {
        GmHorseState horse;
        int slots = ct_horse_state(r, &horse)
            ? horse_inventory_size(r, &horse) : 0;
        for (int slot = 0; slot < slots; ++slot) ADD(GMC_HORSE0 + slot);
    } else if (r->container == 11) {
        ADD(GMC_BEACON0);
    }
    for (int slot = 9; slot < 36; ++slot) ADD(slot);
    for (int slot = 0; slot < 9; ++slot) ADD(slot);
    if (r->container == 0) ADD(GMC_OFFHAND);
#undef ADD
    return count;
}

int gm_container_ordered_slots(
        const struct GmRuntime *r, int out[GMC_SLOT_COUNT])
{
    if (!r || !out) return 0;
    return container_slot_order(r, out);
}

int gm_container_shift_double_click(
        struct GmRuntime *r, int clicked_slot, ICStack match)
{
    int order[GMC_SLOT_COUNT], count, issued = 0;
    if (!r || cc_is_empty(&match) || !slot_usable(r, clicked_slot)
            || ((r->container == 0 || r->container == 1)
                && clicked_slot == GMC_RESULT))
        return 0;
    count = container_slot_order(r, order);
    for (int index = 0; index < count; ++index) {
        int target = order[index];
        ICStack candidate = ct_any_get(r, target);
        if (!gm_container_slots_same_inventory(r, target, clicked_slot)
                || cc_is_empty(&candidate)
                || !ct_slot_can_take(r, target)
                || !cc_stack_match(&candidate, &match)
                || candidate.count > cc_max_stack_size(
                    match.item, match.meta))
            continue;
        (void)gm_container_click(r, target, 0, CC_CLICK_QUICK_MOVE);
        ++issued;
    }
    return issued;
}

static void click_pickup_all(GmRuntime *r, int slot_id, int button)
{
    ICStack cursor = gm_player_cursor();
    ICStack clicked = ct_any_get(r, slot_id);
    int order[GMC_SLOT_COUNT], count;
    if (cc_is_empty(&cursor)
            || (!cc_is_empty(&clicked) && ct_slot_can_take(r, slot_id)))
        return;
    count = container_slot_order(r, order);
    for (int pass = 0; pass < 2; ++pass) {
        int start = button == 0 ? 0 : count - 1;
        int step = button == 0 ? 1 : -1;
        for (int at = start; at >= 0 && at < count
                && cursor.count < cc_max_stack_size(
                    cursor.item, cursor.meta); at += step) {
            int slot = order[at];
            ICStack stack = ct_any_get(r, slot);
            int stack_max = cc_max_stack_size(stack.item, stack.meta);
            int room, amount;
            if (cc_is_empty(&stack) || !cc_stack_match(&stack, &cursor)
                    || !ct_slot_can_take(r, slot)
                    || stack.count > stack_max
                    || ((r->container == 0 || r->container == 1)
                        && slot == GMC_RESULT)
                    || (pass == 0 && stack.count
                        == stack_max))
                continue;
            room = cc_max_stack_size(cursor.item, cursor.meta) - cursor.count;
            amount = stack.count < room ? stack.count : room;
            stack = ct_any_take(r, slot, amount);
            if (!cc_is_empty(&stack)) {
                cursor.count += stack.count;
                if (is_brewing(slot)
                        && slot - GMC_BREWING0 <= BREWING_LIVE_POTION2)
                    (void)gm_runtime_brewed_potion_taken(r, stack);
            }
        }
    }
    gm_player_cursor_set(cursor);
}

int gm_container_click(struct GmRuntime *r, int slot_id, int button, int click_type)
{
    if (!r) return 0;
    if ((click_type == CC_CLICK_SWAP && (button < 0 || button > 8))
            || (click_type == CC_CLICK_QUICK_CRAFT
                && (button < 0 || button > 10 || (button & 3) == 3))
            || (click_type == CC_CLICK_CLONE
                && (button < 0 || button > 2))
            || (click_type != CC_CLICK_SWAP
                && click_type != CC_CLICK_QUICK_CRAFT
                && click_type != CC_CLICK_CLONE
                && button != 0 && button != 1))
        return 0;
    if (click_type == CC_CLICK_QUICK_CRAFT)
        return click_quick_craft(r, slot_id, button);
    if (r->container_drag_event != 0) {
        container_drag_reset(r);
        return 1;
    }

    if (is_enchant_button(slot_id)) {
        if (r->container != 5 || click_type != CC_CLICK_PICKUP
                || button != 0)
            return 0;
        (void)gm_runtime_enchant_click(
            r, slot_id - GMC_ENCHANT_BUTTON0);
        return 1;
    }

    if (is_merchant_button(slot_id)) {
        int next;
        if (r->container != 7 || click_type != CC_CLICK_PICKUP
                || button != 0)
            return 0;
        next = r->merchant_selected
            + (slot_id == GMC_MERCHANT_NEXT ? 1 : -1);
        (void)gm_runtime_merchant_select(r, next);
        return 1;
    }

    if (is_beacon_button(slot_id)) {
        static const int primary_effects[5] = {1, 3, 11, 8, 5};
        static const int required_levels[5] = {1, 1, 2, 2, 3};
        GmRuntimeStaticContainer *beacon;
        int power;
        if (r->container != 11 || click_type != CC_CLICK_PICKUP
                || button != 0 || !(beacon = ct_beacon(r)))
            return 0;
        if (slot_id == GMC_BEACON_CANCEL) {
            gm_runtime_close_open_container(r);
            return 1;
        }
        if (slot_id == GMC_BEACON_CONFIRM) {
            if (!cc_is_empty(&beacon->slots[0])
                    && gm_runtime_beacon_valid_effect(
                        beacon->beacon_primary)) {
                (void)gm_runtime_beacon_confirm(
                    r, beacon->beacon_primary,
                    beacon->beacon_secondary);
                gm_runtime_close_open_container(r);
            }
            return 1;
        }
        power = slot_id - GMC_BEACON_POWER0;
        if (power < 5) {
            if (beacon->beacon_levels >= required_levels[power])
                beacon->beacon_primary = primary_effects[power];
        } else if (beacon->beacon_levels >= 4) {
            beacon->beacon_secondary = power == 5
                ? 10 : beacon->beacon_primary;
        }
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

    /* ContainerPlayer's armor Slot.canTakeStack rejects every operation that
     * would remove a Binding-cursed piece outside creative mode. This guard
     * sits above PICKUP, QUICK_MOVE, and THROW just like Slot.canTakeStack in
     * Container.slotClick; placing into an empty armor slot remains allowed. */
    if (is_armor(slot_id) && !r->tape_creative
            && click_type != CC_CLICK_PICKUP_ALL) {
        ICStack armor = ct_get(r, slot_id);
        if (!cc_is_empty(&armor)
                && ic_enchantment_level(&armor, 10) > 0)
            return 1;
    }

    switch (click_type) {
    case CC_CLICK_PICKUP:
        click_pickup_dispatch(r, slot_id, button);
        return 1;
    case CC_CLICK_QUICK_MOVE: {
        /* vanilla retrySlotClick: repeat while the same item keeps moving */
        for (int guard = 0; guard < 512; ++guard) {
            ICStack moved = ct_transfer(r, slot_id);
            if (cc_is_empty(&moved)) break;
            ICStack now = slot_id == GMC_RESULT ? grid_match(r)
                        : slot_id == GMC_ANVIL0 + 2
                                                ? r->anvil.slots[2]
                        : is_furnace(slot_id)   ? ct_furnace_get(r, slot_id)
                        : is_chest(slot_id)     ? ct_chest_get(r, slot_id)
                        : is_brewing(slot_id)   ? ct_brewing_get(r, slot_id)
                        : is_beacon(slot_id)    ? ct_beacon_get(r)
                        : is_merchant(slot_id)  ? r->merchant_slots[
                                                    slot_id - GMC_MERCHANT0]
                        : is_horse(slot_id)     ? ct_horse_get(r, slot_id)
                                                : ct_get(r, slot_id);
            if (cc_is_empty(&now) || now.item != moved.item) break;
        }
        return 1;
    }
    case CC_CLICK_THROW:
        click_throw(r, slot_id, button);
        return 1;
    case CC_CLICK_SWAP:
        click_swap(r, slot_id, button);
        return 1;
    case CC_CLICK_CLONE: {
        ICStack stack = ct_any_get(r, slot_id);
        ICStack cursor = gm_player_cursor();
        if (r->tape_creative && cc_is_empty(&cursor)
                && !cc_is_empty(&stack)) {
            stack.count = cc_max_stack_size(stack.item, stack.meta);
            gm_player_cursor_set(stack);
        }
        return 1;
    }
    case CC_CLICK_PICKUP_ALL:
        click_pickup_all(r, slot_id, button);
        return 1;
    default:
        return 0;
    }
}

void gm_container_close(struct GmRuntime *r)
{
    if (!r) return;
    /* Container.onContainerClosed runs first in every vanilla subclass and
     * tosses the carried stack before that subclass drains its own temporary
     * inventory. The ordering is observable through Math/player/entity RNG,
     * EIDs, UUIDs, and loaded-entity order. */
    {
        ICStack cursor = gm_player_cursor();
        if (!cc_is_empty(&cursor)) ct_drop(r, cursor);
        gm_player_cursor_set(ic_empty());
    }
    if (r->container == 11) {
        GmRuntimeStaticContainer *beacon = ct_beacon(r);
        if (beacon && !cc_is_empty(&beacon->slots[0])) {
            ct_drop(r, beacon->slots[0]);
            beacon->slots[0] = ic_empty();
        }
    }
    if (r->container == 5) {
        for (int i = 0; i < 2; ++i) {
            ICStack v = r->enchanting.slots[i];
            r->enchanting.slots[i] = ic_empty();
            if (!cc_is_empty(&v)) ct_drop(r, v);
        }
    }
    if (r->container == 6) {
        for (int i = 0; i < 2; ++i) {
            ICStack v = r->anvil.slots[i];
            r->anvil.slots[i] = ic_empty();
            if (!cc_is_empty(&v)) ct_drop(r, v);
        }
        r->anvil.slots[2] = ic_empty();
        r->anvil.maximum_cost = 0;
        r->anvil.material_cost = 0;
        r->anvil.repaired_name = 0;
    }
    if (r->container == 7) {
        for (int i = 0; i < 2; ++i) {
            ICStack v = r->merchant_slots[i];
            r->merchant_slots[i] = ic_empty();
            if (!cc_is_empty(&v)) ct_drop(r, v);
        }
        r->merchant_slots[2] = ic_empty();
        r->merchant_selected = 0;
        r->merchant_offer_index = -1;
    }
    if (r->container == 0 || r->container == 1) {
        for (int i = 0; i < 9; ++i) {
            ICStack v = r->craft_grid[i];
            r->craft_grid[i] = ic_empty();
            if (!cc_is_empty(&v)) ct_drop(r, v);
        }
    }
}
