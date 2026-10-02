/* See entity_nbt.h. */
#include "entity_nbt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nbtbin.h"
#include "nbtw.h"
#include "items.h"
#include "living.h"
#include "serverreplay.h"

/* A deep copy of a subtree, through the canonical text: the snapshot's trees
 * are the replay's, and the writer hands its own tree to nbt_free. */
static nbt *copy_tree(const nbt *v)
{
    char *text = nbt_render(v);
    nbt *out = nbt_parse(text);
    free(text);
    return out;
}

/* The snapshot's player NBT (player_server.nbt's "nbt" compound). */
static const nbt *player_tag(const struct server_player *p)
{
    return p->snapshot_tree ? nbt_get((const nbt *)p->snapshot_tree, "nbt") : NULL;
}

/* One constant field of the player's NBT, copied (an empty compound when the
 * snapshot has none). */
static void player_copy(struct nbtw *w, const struct nbt_key *k, const struct server_player *p, const char *key)
{
    const nbt *tag = player_tag(p);
    const nbt *v = tag ? nbt_get(tag, key) : NULL;

    if (v) nbtw_tree_k(w, k, v, copy_tree);
    else
    {
        nbtw_comp_k(w, k);
        nbtw_end(w);
    }
}

/* One slot of InventoryPlayer.writeToNBT or saveInventoryToNBT, with
 * ItemStack.writeToNBT's tag when the stack has one. */
static void stack_slot(struct nbtw *w, int slot, const struct surv_stack *st)
{
    nbtw_add_comp(w);
    nbtw_byte(w, "Slot", slot);
    nbtw_short(w, "id", (short)st->item);
    nbtw_byte(w, "Count", st->count);
    nbtw_short(w, "Damage", (short)st->damage);
    itag_w(w, st->tag);
    nbtw_end(w);
}

/* InventoryPlayer.writeToNBT: the live main slots, then armor at 100..103. */
static void player_inventory(struct nbtw *w, const struct server_player *p)
{
    nbtw_list(w, "Inventory");

    for (int i = 0; i < 40; ++i)
        if (p->sv.inv[i].count > 0) stack_slot(w, i < 36 ? i : i + 64, &p->sv.inv[i]);

    nbtw_end(w);
}

/* InventoryEnderChest.saveInventoryToNBT: the live ender slots. */
static void player_ender_items(struct nbtw *w, const struct server_player *p)
{
    nbtw_list(w, "EnderItems");

    for (int i = 0; i < 27; ++i)
        if (p->sv.ender[i].count > 0) stack_slot(w, i, &p->sv.ender[i]);

    nbtw_end(w);
}

/* Entity.writeToNBT's own block, in Java's insertion order. */
void ent_w_base(struct nbtw *w, const struct entity *e, int dim, int air, int portal_cooldown,
                int64_t uuid_msb, int64_t uuid_lsb, float yaw, float pitch)
{
    nbtw_double3(w, NBTW_K("Pos"), e->pos_x, e->pos_y + (double)e->y_size, e->pos_z);
    nbtw_double3(w, NBTW_K("Motion"), e->motion_x, e->motion_y, e->motion_z);
    nbtw_float2(w, NBTW_K("Rotation"), yaw, pitch);
    nbtw_float(w, "FallDistance", e->fall_distance);
    nbtw_short(w, "Fire", (short)e->fire);
    nbtw_short(w, "Air", (short)air);
    nbtw_byte(w, "OnGround", e->on_ground ? 1 : 0);
    nbtw_int(w, "Dimension", dim);
    nbtw_byte(w, "Invulnerable", 0);
    nbtw_int(w, "PortalCooldown", portal_cooldown);
    nbtw_long(w, "UUIDMost", uuid_msb);
    nbtw_long(w, "UUIDLeast", uuid_lsb);
}

nbt *ent_nbt_base(const struct entity *e, int dim, int air, int64_t uuid_msb, int64_t uuid_lsb,
                  float yaw, float pitch)
{
    nbt *t = nbt_new_compound();
    struct nbtw w;
    nbtw_tree(&w, t);
    ent_w_base(&w, e, dim, air, 0, uuid_msb, uuid_lsb, yaw, pitch);
    return t;
}

/* PlayerCapabilities.writeCapabilitiesToNBT: the seven keys in the order it
 * sets them, because the binary writer's iteration order depends on the
 * insertion order (a tree copied through the canonical text would be sorted). */
static void player_abilities(struct nbtw *w, const struct server_player *p)
{
    static const char *BYTE_KEYS[5] = {"invulnerable", "flying", "mayfly", "instabuild", "mayBuild"};
    static const char *FLOAT_KEYS[2] = {"flySpeed", "walkSpeed"};
    const nbt *src = player_tag(p);
    const nbt *ab = src ? nbt_get(src, "abilities") : NULL;

    nbtw_comp(w, "abilities");

    for (int i = 0; i < 5; ++i)
    {
        const nbt *v = ab ? nbt_get(ab, BYTE_KEYS[i]) : NULL;
        nbtw_byte_k(w, nbt_key_of(BYTE_KEYS[i]), (signed char)(v ? nbt_int_value(v) : 0));
    }

    for (int i = 0; i < 2; ++i)
    {
        const nbt *v = ab ? nbt_get(ab, FLOAT_KEYS[i]) : NULL;
        float f = 0.0F;

        if (v)
        {
            uint32_t bits = nbt_float_bits(v);
            memcpy(&f, &bits, sizeof f);
        }

        nbtw_float_k(w, nbt_key_of(FLOAT_KEYS[i]), f);
    }

    nbtw_end(w);
}

/* The Java 8 HashSet bucket of an AttributeModifier (its hashCode is the
 * UUID's) in the default 16-bucket table: the op-0 set iterates by it. */
static int modifier_bucket(int64_t msb, int64_t lsb)
{
    uint64_t hilo = (uint64_t)msb ^ (uint64_t)lsb;
    uint32_t h = (uint32_t)(hilo >> 32) ^ (uint32_t)hilo;
    h ^= h >> 16;
    return (int)(h & 15);
}

/* One saved AttributeModifier, writeAttributeModifierToNBT. */
static void modifier_w(struct nbtw *w, const struct attr_mod *m)
{
    nbtw_add_comp(w);
    nbtw_string(w, "Name", attr_name(m->name));
    nbtw_double(w, "Amount", m->amount);
    nbtw_int(w, "Operation", m->operation);
    nbtw_long(w, "UUIDMost", m->uuid_msb);
    nbtw_long(w, "UUIDLeast", m->uuid_lsb);
    nbtw_end(w);
}

/* generic.attackDamage's Modifiers: the held weapon's "Weapon modifier" or
 * "Tool modifier" (EntityPlayer's lastActiveItems are the armour, so the held
 * item's modifier stays in the map the write sees) and the saved potion
 * modifiers on the player twin (weakness, strength), walked the way
 * ModifiableAttributeInstance.func_111122_c does: operation 0, 1, 2, each
 * operation's HashSet in bucket order. Nothing when there is none. */
static void player_attack_modifiers(struct nbtw *w, const struct server_player *p)
{
    struct attr_mod mods[ATTR_MAX_MODS + 1];
    int n = 0;
    int slot = p->sv.current_item;
    int item = p->held_attr_item;

    if (slot >= 0 && slot < 9 && item >= 0 && item < 4096 &&
        (ITEMS[item].kind == ITEM_TOOL || ITEMS[item].kind == ITEM_SWORD))
    {
        memset(&mods[n], 0, sizeof mods[n]);
        mods[n].name = attr_name_id(ITEMS[item].kind == ITEM_SWORD ? "Weapon modifier" : "Tool modifier");
        mods[n].amount = (double)ITEMS[item].damage;
        mods[n].operation = 0;
        mods[n].uuid_msb = -3801225194067177672LL;
        mods[n].uuid_lsb = -6586624321849018929LL;
        mods[n].saved = 1;
        ++n;
    }
    if (p->replay != NULL && p->replay->player_livh != 0)
    {
        const struct attr_instance *a = &lv_get(p->replay->player_livh)->attrs.a[ATTR_ATTACK_DAMAGE];
        for (int i = 0; i < a->nmods && n < ATTR_MAX_MODS + 1; ++i)
            if (a->mods[i].saved) mods[n++] = a->mods[i];
    }
    if (n == 0) return;

    /* ModifiableAttributeInstance.func_111122_c: one HashSet (16 bins) the
     * three operations' sets are added to in operation order, so the NBT
     * walks bin by bin, and inside a bin operation 0 first */
    nbtw_list(w, "Modifiers");
    for (int b = 0; b < 16; ++b)
        for (int op = 0; op < 3; ++op)
            for (int i = 0; i < n; ++i)
            {
                const struct attr_mod *m = &mods[i];
                if (m->operation != op || modifier_bucket(m->uuid_msb, m->uuid_lsb) != b) continue;
                modifier_w(w, m);
            }
    nbtw_end(w);
}

/* EntityLivingBase.writeEntityToNBT's Attributes: SharedMonsterAttributes
 * writes Name, Base and, when the instance holds any modifier at all, a
 * Modifiers list -- holding only the saved ones, so an unsaved modifier (the
 * sprint speed boost) leaves an empty list behind, which is still a tag. */
static void player_attributes(struct nbtw *w, const struct server_player *p)
{
    const nbt *tag = player_tag(p);
    const nbt *src = tag ? nbt_get(tag, "Attributes") : NULL;
    int n = src ? nbt_list_size(src) : 0;

    nbtw_list(w, "Attributes");

    for (int i = 0; i < n; ++i)
    {
        const nbt *e = nbt_list_get(src, i);
        const nbt *name = nbt_get(e, "Name");
        const nbt *base = nbt_get(e, "Base");
        int moving = name != NULL && nbt_string_value(name) != NULL &&
                     !strcmp(nbt_string_value(name), "generic.movementSpeed");
        int attacking = name != NULL && nbt_string_value(name) != NULL &&
                        !strcmp(nbt_string_value(name), "generic.attackDamage");

        nbtw_add_comp(w);

        if (name) nbtw_put_tree(w, "Name", name, copy_tree);
        else nbtw_string(w, "Name", "");
        if (base) nbtw_put_tree(w, "Base", base, copy_tree);
        else nbtw_double(w, "Base", 0.0);

        /* a movement potion's saved modifier rides the generic.movementSpeed
         * entry (Potion.applyAttributesModifiersToEntity applied it under the
         * potion.moveSpeed N name, amount scaled by amp+1, operation 2); the
         * sprint boost is unsaved and never written, so when only sprint runs
         * the list is empty (one Modifiers tag either way) */
        if (moving && p->sprinting && (p->replay == NULL || p->replay->player_livh == 0))
        {
            nbtw_list(w, "Modifiers");
            nbtw_end(w);
        }
        if (moving && p->replay != NULL && p->replay->player_livh != 0)
        {
            const struct living *l = lv_get(p->replay->player_livh);
            const struct attr_instance *a = &l->attrs.a[ATTR_MOVEMENT_SPEED];
            int saved = 0;

            for (int mi = 0; mi < a->nmods; ++mi) saved += a->mods[mi].saved != 0;

            if (saved > 0 || p->sprinting)
            {
                nbtw_list(w, "Modifiers");
                for (int mi = 0; mi < a->nmods; ++mi)
                    if (a->mods[mi].saved) modifier_w(w, &a->mods[mi]);
                nbtw_end(w);
            }
        }

        if (attacking) player_attack_modifiers(w, p);

        nbtw_end(w);
    }

    nbtw_end(w);
}

/* EntityItem's Item tag: ItemStack.writeToNBT. */
static void item_stack_w(struct nbtw *w, const struct nbt_key *k, int item, int damage, int count, int tag)
{
    nbtw_comp_k(w, k);
    nbtw_short(w, "id", (short)item);
    nbtw_byte(w, "Count", (signed char)count);
    nbtw_short(w, "Damage", (short)damage);
    itag_w(w, tag);
    nbtw_end(w);
}

void ent_w_ie(struct nbtw *w, const ie_ent *en)
{
    /* Entity.air stays at the constructor's 300 for these kinds: none of them
     * is a living entity */
    ent_w_base(w, &en->e, en->e.world ? en->e.world->dim : 0, 300, en->time_until_portal,
               en->uuid_msb, en->uuid_lsb, en->rotation_yaw, en->rotation_pitch);

    switch (en->kind)
    {
        case IE_ITEM:
            nbtw_short(w, "Health", (short)(signed char)en->health);
            nbtw_short(w, "Age", (short)en->age);
            if (en->thrower) nbtw_string(w, "Thrower", en->thrower);
            if (en->owner_name) nbtw_string(w, "Owner", en->owner_name);
            item_stack_w(w, NBTW_K("Item"), en->stack_item, en->stack_damage, en->stack_count, en->stack_tag);
            break;

        case IE_ORB:
            nbtw_short(w, "Health", (short)(signed char)en->health);
            nbtw_short(w, "Age", (short)en->age);
            nbtw_short(w, "Value", (short)en->xp_value);
            break;

        case IE_ARROW:
            nbtw_short(w, "xTile", (short)en->tile_x);
            nbtw_short(w, "yTile", (short)en->tile_y);
            nbtw_short(w, "zTile", (short)en->tile_z);
            nbtw_short(w, "life", (short)en->ticks_in_ground);
            nbtw_byte(w, "inTile", (signed char)en->in_tile);
            nbtw_byte(w, "inData", (signed char)en->in_data);
            nbtw_byte(w, "shake", (signed char)en->shake);
            nbtw_byte(w, "inGround", en->in_ground ? 1 : 0);
            nbtw_byte(w, "pickup", (signed char)en->can_be_picked_up);
            nbtw_double(w, "damage", en->arrow_damage);
            break;

        case IE_TNT:
            nbtw_byte(w, "Fuse", (signed char)en->fuse);
            break;

        case IE_SMALL_FIREBALL:
        case IE_LARGE_FIREBALL:
            nbtw_short(w, "xTile", (short)en->tile_x);
            nbtw_short(w, "yTile", (short)en->tile_y);
            nbtw_short(w, "zTile", (short)en->tile_z);
            nbtw_byte(w, "inTile", (signed char)en->in_tile);
            nbtw_byte(w, "inGround", en->in_ground ? 1 : 0);
            nbtw_double3(w, NBTW_K("direction"), en->e.motion_x, en->e.motion_y, en->e.motion_z);
            /* EntityLargeFireball.writeEntityToNBT */
            if (en->kind == IE_LARGE_FIREBALL)
                nbtw_int(w, "ExplosionPower", en->explosion_power);
            break;

        case IE_ENDER_EYE: /* EntityEnderEye writes nothing of its own */
            break;

        case IE_POTION:
            /* EntityPotion.writeEntityToNBT: EntityThrowable's block plus the
             * potion ItemStack the effect comes from. */
            nbtw_short(w, "xTile", (short)en->tile_x);
            nbtw_short(w, "yTile", (short)en->tile_y);
            nbtw_short(w, "zTile", (short)en->tile_z);
            nbtw_byte(w, "inTile", (signed char)en->in_tile);
            nbtw_byte(w, "shake", (signed char)en->shake);
            nbtw_byte(w, "inGround", en->in_ground ? 1 : 0);
            nbtw_string(w, "ownerName", en->owner_name ? en->owner_name : "");
            item_stack_w(w, NBTW_K("Potion"), 373, en->potion_damage, 1 - en->potion_spent, 0);
            break;

        default: /* the throwables share EntityThrowable's block */
            nbtw_short(w, "xTile", (short)en->tile_x);
            nbtw_short(w, "yTile", (short)en->tile_y);
            nbtw_short(w, "zTile", (short)en->tile_z);
            nbtw_byte(w, "inTile", (signed char)en->in_tile);
            nbtw_byte(w, "shake", (signed char)en->shake);
            nbtw_byte(w, "inGround", en->in_ground ? 1 : 0);
            /* the thrower is only named when a player threw it */
            nbtw_string(w, "ownerName", en->owner_name ? en->owner_name : "");
            break;
    }
}

nbt *ent_nbt_ie(const ie_ent *en)
{
    nbt *t = nbt_new_compound();
    struct nbtw w;
    nbtw_tree(&w, t);
    ent_w_ie(&w, en);
    return t;
}

void ent_nbt_player_uuid(const struct server_player *p, int64_t *msb, int64_t *lsb)
{
    const nbt *tag = player_tag(p);
    const nbt *um = tag ? nbt_get(tag, "UUIDMost") : NULL;
    const nbt *ul = tag ? nbt_get(tag, "UUIDLeast") : NULL;
    *msb = um ? (int64_t)nbt_int_value(um) : 0;
    *lsb = ul ? (int64_t)nbt_int_value(ul) : 0;
}

void ent_w_player(struct nbtw *w, const struct server_player *p)
{
    const struct entity *e = &p->e;
    int64_t uuid_msb = 0, uuid_lsb = 0;

    {
        const nbt *tag = player_tag(p);
        const nbt *um = tag ? nbt_get(tag, "UUIDMost") : NULL;
        const nbt *ul = tag ? nbt_get(tag, "UUIDLeast") : NULL;

        if (um) uuid_msb = nbt_int_value(um);
        if (ul) uuid_lsb = nbt_int_value(ul);
    }

    ent_w_base(w, e, p->dimension, p->sv.air, p->portal.time_until_portal, uuid_msb, uuid_lsb,
               p->rotation_yaw, p->rotation_pitch);

    /* EntityLivingBase.writeEntityToNBT */
    nbtw_float(w, "HealF", p->sv.health);
    nbtw_short(w, "Health", (short)(int)ceil((double)p->sv.health));
    nbtw_short(w, "HurtTime", (short)p->sv.hurt_time);
    nbtw_short(w, "DeathTime", (short)p->sv.death_time);
    nbtw_short(w, "AttackTime", (short)p->sv.attack_time);
    nbtw_float(w, "AbsorptionAmount", p->sv.absorption);
    player_attributes(w, p);
    /* ActiveEffects is absent with no potion in effect; the server player's
     * activePotionsMap lives on the replay's player twin, written in the map's
     * bucket order (PotionEffect.writeCustomPotionEffectToNBT's keys) */
    if (p->replay != NULL && p->replay->player_livh != 0 &&
        lv_get(p->replay->player_livh)->potions.size > 0)
    {
        const struct potion_map *m = &lv_get(p->replay->player_livh)->potions;

        uint8_t ids[POT_COUNT];
        int n = potion_map_order(m, ids);

        nbtw_list(w, "ActiveEffects");
        for (int i = 0; i < n; ++i)
        {
            const struct potion_effect *pe = &m->eff[ids[i]];
            nbtw_add_comp(w);
            nbtw_byte(w, "Id", (signed char)pe->id);
            nbtw_byte(w, "Amplifier", (signed char)pe->amplifier);
            nbtw_int(w, "Duration", pe->duration);
            nbtw_byte(w, "Ambient", (signed char)(pe->is_ambient != 0));
            nbtw_end(w);
        }
        nbtw_end(w);
    }

    /* EntityPlayer.writeEntityToNBT */
    player_inventory(w, p);
    nbtw_int(w, "SelectedItemSlot", p->sv.current_item);
    nbtw_byte(w, "Sleeping", (int8_t)(p->sv.sleeping != 0));
    nbtw_short(w, "SleepTimer", (short)p->sv.sleep_timer);
    nbtw_float(w, "XpP", p->sv.xp_progress);
    nbtw_int(w, "XpLevel", p->sv.xp_level);
    nbtw_int(w, "XpTotal", p->sv.xp_total);
    nbtw_int(w, "Score", p->sv.score);
    /* EntityPlayer.spawnChunk, the bed the player last woke in */
    if (p->sv.has_spawn)
    {
        nbtw_int(w, "SpawnX", p->sv.spawn_x);
        nbtw_int(w, "SpawnY", p->sv.spawn_y);
        nbtw_int(w, "SpawnZ", p->sv.spawn_z);
        nbtw_byte(w, "SpawnForced", (int8_t)(p->sv.spawn_forced != 0));
    }
    nbtw_int(w, "foodLevel", p->sv.food.level);
    nbtw_int(w, "foodTickTimer", p->sv.food.timer);
    nbtw_float(w, "foodSaturationLevel", p->sv.food.saturation);
    nbtw_float(w, "foodExhaustionLevel", p->sv.food.exhaustion);
    player_abilities(w, p);
    player_ender_items(w, p);

    /* EntityPlayerMP.writeEntityToNBT */
    player_copy(w, NBTW_K("playerGameType"), p, "playerGameType");

    /* Entity.writeToNBT's tail: the vehicle's writeMountToNBT (its id and
     * its whole NBT) while the player rides it */
    if (p->ridingh != 0 && !lv_get(p->ridingh)->is_dead)
    {
        nbtw_comp(w, "Riding");
        nbtw_string(w, "id", living_kind_name(lv_get(p->ridingh)->kind));
        living_write_w(lv_get(p->ridingh), w);
        nbtw_end(w);
    }
}

nbt *ent_nbt_player(const struct server_player *p)
{
    nbt *t = nbt_new_compound();
    struct nbtw w;
    nbtw_tree(&w, t);
    ent_w_player(&w, p);
    return t;
}

uint64_t ent_digest_add(uint64_t h, int entity_id, const nbt *tag)
{
    h = h * 1000003u + (uint64_t)(int64_t)entity_id;
    h = h * 1000003u + nbtbin_long_memo(entity_id, tag);
    return h;
}

uint64_t ent_digest_add_w(uint64_t h, int entity_id, struct nbtw *w)
{
    h = h * 1000003u + (uint64_t)(int64_t)entity_id;
    h = h * 1000003u + nbtw_bin_long_memo(w, entity_id);
    return h;
}
