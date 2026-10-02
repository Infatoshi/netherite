#ifndef NETHERITE_DEV
#error dev.c must only be built with NETHERITE_DEV
#endif

#include "comparator.h"
#include "dev.h"
#include "env.h"
#include "riding.h"
#include "player.h"
#include "survival.h"
#include "serverreplay.h"
#include "randomtick.h"
#include "combat.h"
#include "ghasts.h"
#include "tape.h"
#include "blockcb.h"
#include "drops.h"
#include "grave.h"
#include "world.h"
#include "hostiles.h"
#include "hostiles_spider.h"
#include "villagers.h"
#include "potion.h"
#include "spawning.h"
#include "tileticks.h"
#include "dragon.h"
#include "items.h"
#include "lightning.h"
#include "place.h"
#include "tileentity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int integer(const struct jval *c, const char *key, int def)
{
    int64_t n;
    return json_int(json_get(c, key), &n) ? (int)n : def;
}

static int int_at(const struct jval *v, int def)
{
    int64_t n;
    return json_int(v, &n) ? (int)n : def;
}

static int real(const struct jval *c, const char *key, double *out)
{
    uint64_t bits;
    if (!json_double(json_get(c, key), &bits)) return 0;
    memcpy(out, &bits, 8);
    return 1;
}

/* Dev.java's stack(c, item, count) tag: ItemStack.addEnchantment per "ench"
 * entry in order, ItemArmor.func_82813_b for "color", setStackDisplayName
 * for "name". */
static int dev_stack_tag(const struct jval *c)
{
    int tag = 0;
    const struct jval *ench = json_get(c, "ench");
    for (int i = 0; ench && i < json_len(ench); ++i)
        tag = itag_add_ench(tag, int_at(json_at(json_at(ench, i), 0), 0), int_at(json_at(json_at(ench, i), 1), 0));
    if (json_get(c, "color")) tag = itag_set_color(tag, integer(c, "color", 0));
    const char *name = json_str(json_get(c, "name"));
    if (name) tag = itag_set_name(tag, name);
    return tag;
}

static int error(char *buf, int size, const char *what)
{
    snprintf(buf, (size_t)size, "%s", what);
    return 0;
}

void dev_end(struct serverreplay *sr)
{
    free(sr->dev_writes);
    sr->dev_writes = NULL;
    sr->ndev_writes = sr->d->nextra;
    sr->dev_writes_dim = serverreplay_player_dim(sr);
    sr->dev_block_count = (int)sr->blk_count;
    if (sr->d->nextra > 0)
    {
        sr->dev_writes = malloc((size_t)sr->d->nextra * sizeof *sr->dev_writes);
        memcpy(sr->dev_writes, sr->d->extra, (size_t)sr->d->nextra * sizeof *sr->dev_writes);
    }
    sr->d->nextra = 0;
}

/* EntityList.createEntityFromNBT + setLocationAndAngles + onSpawnWithEgg +
 * spawnEntityInWorld, the oracle's Dev summon op. The constructor draws run on
 * the OTHER slot holding the SERVER stream (the pin the spawner's adopt path
 * uses), and the world Random the egg path reads is the server world's.
 * CaveSpider and Silverfish joined the op on both sides (Dev.java's list, this
 * kind table) through the same hostile constructor path. */
/* The summon's "equip" entries: setCurrentItemOrArmor(slot, stack) after
 * onSpawnWithEgg (0 the hand, 1..4 boots to helmet), on the entity just
 * summoned (the newest of its kind). A stack in hand brings its own
 * attackDamage modifier ("Weapon modifier" / "Tool modifier",
 * Item.field_111210_e), which EntityLivingBase.onUpdate's equipment pass
 * applies before the entity can attack; it replaces a spawn weapon's. */
static int dev_summon_equip(struct serverreplay *sr, int kind, const struct jval *equip)
{
    struct living *l = NULL;
    for (int i = sr->d->anw.n - 1; i >= 0 && l == NULL; --i)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
        if (en->is_living && en->livh != 0 && lv_get(en->livh)->kind == kind) l = lv_get(en->livh);
    }
    if (l == NULL) return 0;
    for (int k = 0; k < json_len(equip); ++k)
    {
        const struct jval *entry = json_at(equip, k);
        int slot = integer(entry, "slot", -1), item = integer(entry, "item", 0);
        if (slot < 0 || slot > 4) return 0;
        struct equip_slot *eq = &l->equip[slot];
        eq->id = item;
        eq->count = 1;
        eq->damage = integer(entry, "meta", 0);
        eq->tag = dev_stack_tag(entry);
        /* an optional "drop": setEquipmentDropChance(slot, drop) */
        double drop;
        if (real(entry, "drop", &drop)) l->equipment_drop_chances[slot] = (float)drop;
        if (slot != 0) continue;
        struct attr_mod w_mod;
        memset(&w_mod, 0, sizeof w_mod);
        w_mod.uuid_msb = -3799650116634706120LL;
        w_mod.uuid_lsb = -6586616428615387697LL;
        attrs_remove(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
        if (item > 0 && item < 4096 && (ITEMS[item].kind == ITEM_SWORD || ITEMS[item].kind == ITEM_TOOL))
        {
            w_mod.amount = (double)ITEMS[item].damage;
            w_mod.operation = 0;
            w_mod.from_item = 1;
            attrs_apply(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
        }
    }
    return 1;
}

static int dev_summon(struct serverreplay *sr, int kind, double x, double y, double z,
                      int child, int size)
{
    if (!sr->mobs_enabled)
    {
        spawner_init(&sr->d->spawner, &sr->pop.world, &SR_DET(sr), &ST_RAND(&sr->w[0].st));
        an_init(&sr->d->anw, &sr->pop.world, &SR_DET(sr));
        /* an_init clears the pool: Collections.shuffle's shared Random
         * (a villager's offer shuffle) is the replay's */
        sr->d->anw.shuf_rand = &SR_SHUF(sr);
        /* and WorldServer's village collection and siege, which the
         * villagers' updateAITick and the siege read through it */
        sr->d->anw.village_collection = &sr->vc;
        sr->d->anw.village_siege = &sr->vs;
        sr->d->anw.process_dead = 1;
        sr->d->anw.iew.role = DET_SERVER;
        spawner_set_scene(&sr->d->spawner, sr->pop.world.seed, sr->w[0].st.difficulty,
                          sr->cal_month, sr->cal_day, 0, 0, 0);
        sr->mobs_enabled = 1;
        serverreplay_bind_player(sr, sr->player, sr->player_entity_id);
        /* the client's entity world, which the entity mouseover reads */
        if (sr->combat_cp) combat_bind(sr, sr->combat_cp);
    }
    /* EntityGhast reads playerEntities through the ghast lane's gh_world,
     * which the Nether's pool carries from its load and any other world's
     * gets with its first ghast (serverreplay.c mirrors the player into it
     * every pass) */
    if (kind == GK_GHAST && sr->d->anw.user_data == NULL)
    {
        struct gh_world *gw = calloc(1, sizeof *gw);
        if (!gw) return 0;
        gw->an = &sr->d->anw;
        sr->d->anw.user_data = gw;
        /* and the ghast lane's impact handler, as the Nether's pool has from
         * its load: EntityLargeFireball.onImpact's damage and explosion
         * (living.c's own callback covers only the small fireball) */
        sr->d->anw.iew.on_fireball_impact = gh_fireball_impact;
    }
    /* The zombie and the two slimes go through the spawner's record path (the
     * egg draws live there); the record's box comes back through
     * sr_adopt_mob. */
    /* the pigman, and a skeleton in the Nether (its wither roll), take the
     * same record path as the natural fortress spawns */
    if (kind == HK_ZOMBIE || kind == SK_SLIME || kind == SK_MAGMA_CUBE || kind == HK_PIGMAN ||
        (kind == HK_SKELETON && serverreplay_player_dim(sr) == -1))
        return serverreplay_summon_hostile(sr, kind, x, y, z, child, size);
    det_state fake = SR_DET(sr);
    fake.splits = NULL;
    fake.seeder[DET_OTHER] = SR_DET(sr).seeder[DET_SERVER];
    fake.math[DET_OTHER] = SR_DET(sr).math[DET_SERVER];
    fake.next_id[DET_OTHER] = SR_DET(sr).next_id[DET_SERVER];
    det_state *real = sr->d->anw.det;
    sr->d->anw.det = &fake;
    sr->d->anw.iew.det = &fake;
    /* onSpawnWithEgg draws from the live World.rand of the player's world (a
     * sheep's fleece color, a villager's profession) */
    jrand *wr = serverreplay_world_rand(sr, serverreplay_player_dim(sr));
    sr->d->anw.iew.world_rand.r = *wr;
    /* EntityList.createEntityFromNBT's constructor, the readFromNBT of the
     * bare tag (nothing it reads differs from the constructor's but
     * PersistenceRequired), then onSpawnWithEgg at the local difficulty */
    int hostile = kind == HK_ZOMBIE || kind == HK_SKELETON || kind == HK_CREEPER ||
                  kind == HK_SPIDER || kind == HK_CAVE_SPIDER || kind == HK_SILVERFISH ||
                  kind == HK_ENDERMAN || kind == HK_WITCH;
    struct living *l;
    if (kind == GK_GHAST)
        l = gh_spawn_ghast(&sr->d->anw, sr->d->anw.n, x, y, z, 0.0F, 0.0F);
    else if (kind == VK_VILLAGER)
    {
        /* new EntityVillager(world): profession 0, then onSpawnWithEgg's
         * worldObj.rand.nextInt(5) (the world stream the fake-det dance
         * keeps) after the base EntityLiving.onSpawnWithEgg's follow-range
         * gaussian and uuid (the fake OTHER role, the server's streams) */
        l = vil_spawn_living(&sr->d->anw, kind, sr->d->anw.n, x, y, z, 0.0F, 0.0F, child ? -24000 : 0,
                             0, 0, 0, 1);
    }
    else if (hostile)
    {
        sr->d->spawner.world_time = sr->w[0].st.world_time;
        /* World.difficultySetting, which the egg path reads (a difficulty op
         * in the same header batch has not reached the living world yet) */
        sr->d->anw.difficulty = sr->w[0].st.difficulty;
        sr->d->spawner.difficulty = sr->w[0].st.difficulty;
        float diff = spawner_local_difficulty(&sr->d->spawner, x, y, z);
        l = hostile_spawn(&sr->d->anw, kind, sr->d->anw.n, x, y, z, 0.0F, 0.0F, 0.0, 0.0, 0.0, 0, 0, 0, 0, diff);
        if (l) l->persistence_required = 0;
    }
    else
    {
        /* the onSpawnWithEgg difficulty draws read World.func_147473_b at the
         * spawn point (canPickUpLoot, door breaking, the zombie armor bonus) */
        sr->d->anw.spawn_diff_factor = spawner_local_difficulty(&sr->d->spawner, x, y, z);
        l = an_spawn_living(&sr->d->anw, kind, sr->d->anw.n, x, y, z,
                            0.0F, 0.0F, child ? -24000 : 0, 0, 0, 0, 0, 1);
    }
    /* Dev.java's child is EntityAgeable.readEntityFromNBT's Age -24000, the
     * half-size setScale before setLocationAndAngles: Java's placement
     * re-centers the box the shrink moved, the spawn's lands after it */
    if (l && child) living_set_location_and_angles(l, x, y, z, l->rotation_yaw, l->rotation_pitch);
    *wr = sr->d->anw.iew.world_rand.r;
    sr->d->anw.spawn_diff_factor = 0.0F;
    SR_DET(sr).seeder[DET_SERVER] = fake.seeder[DET_OTHER];
    SR_DET(sr).math[DET_SERVER] = fake.math[DET_OTHER];
    SR_DET(sr).next_id[DET_SERVER] = fake.next_id[DET_OTHER];
    sr->d->anw.det = real;
    sr->d->anw.iew.det = real;
    if (!l) return 0;
    /* readFromNBT of the bare tag: Entity.dimension takes the absent
     * Dimension key's 0 whatever world the constructor saw, and the air
     * supply the absent Air short's 0 (the first update out of water puts
     * back 300; in water it counts down from 0) */
    l->dimension = 0;
    l->air = 0;
    /* EntityBat.readEntityFromNBT: the absent BatFlags byte is 0, so a
     * summoned bat is not hanging */
    if (kind == AK_BAT) l->data_watcher_16 = 0;
    struct an_ent *en = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
    serverreplay_track_entity(sr, 2, en);
    /* spawnEntityInWorld's EntityTracker.addEntityToTracker: the entry and
     * its S0F take the summoned pose, before the first tick moves it (a
     * crowd's push would otherwise put the move into the S0F) */
    combat_track_spawn(sr, l);
    /* spawnEntityInWorld puts it in loadedEntityList: World.countEntities
     * counts a summoned blaze or ghast toward the natural spawner's cap, and
     * its box blocks a natural spawn */
    int sk = sr_spawn_kind(kind);
    if (sk >= 0) spawner_register_living(&sr->d->spawner, sk, l->entity_id, &l->e);
    else spawner_track(&sr->d->spawner, l->entity_id, &l->e);
    sr->native_entities = sr->d->iew.n + sr->d->fhw.n + sr->d->anw.n;
    return 1;
}

/* Dev.java's "jockey" op: the summon form of EntitySpider.onSpawnWithEgg's
 * 1-in-100 spider jockey and EntityZombie.onSpawnWithEgg's chicken jockey.
 * Each attempt is the vehicle's (or the rider's) constructor,
 * setLocationAndAngles and onSpawnWithEgg; a miss is dropped without joining
 * the world (its draws stay spent), the first hit joins it. The egg path
 * spawns its partner into the world before the op's own spawnEntityInWorld,
 * so the partner comes first in loadedEntityList and in its chunk. */
static int dev_jockey(struct serverreplay *sr, const char *kind, double x, double y, double z, int attempts)
{
    int spider = strcmp(kind, "spider_skeleton") == 0;
    if (!spider && strcmp(kind, "baby_zombie_chicken") != 0) return 0;

    if (!sr->mobs_enabled)
    {
        spawner_init(&sr->d->spawner, &sr->pop.world, &SR_DET(sr), &ST_RAND(&sr->w[0].st));
        an_init(&sr->d->anw, &sr->pop.world, &SR_DET(sr));
        /* an_init clears the pool: Collections.shuffle's shared Random
         * (a villager's offer shuffle) is the replay's */
        sr->d->anw.shuf_rand = &SR_SHUF(sr);
        /* and WorldServer's village collection and siege, which the
         * villagers' updateAITick and the siege read through it */
        sr->d->anw.village_collection = &sr->vc;
        sr->d->anw.village_siege = &sr->vs;
        sr->d->anw.process_dead = 1;
        sr->d->anw.update_ridden = 1;
        sr->d->anw.iew.role = DET_SERVER;
        spawner_set_scene(&sr->d->spawner, sr->pop.world.seed, sr->w[0].st.difficulty,
                          sr->cal_month, sr->cal_day, 0, 0, 0);
        sr->mobs_enabled = 1;
        serverreplay_bind_player(sr, sr->player, sr->player_entity_id);
        if (sr->combat_cp) combat_bind(sr, sr->combat_cp);
    }

    /* the zombie's egg path lives in the spawner's record path (the summon
     * op's zombie route), chicken jockey included */
    if (!spider) return serverreplay_summon_chicken_jockey(sr, x, y, z, attempts);

    det_state fake = SR_DET(sr);
    fake.splits = NULL;
    fake.seeder[DET_OTHER] = SR_DET(sr).seeder[DET_SERVER];
    fake.math[DET_OTHER] = SR_DET(sr).math[DET_SERVER];
    fake.next_id[DET_OTHER] = SR_DET(sr).next_id[DET_SERVER];
    det_state *real = sr->d->anw.det;
    sr->d->anw.det = &fake;
    sr->d->anw.iew.det = &fake;
    jrand *wr = serverreplay_world_rand(sr, serverreplay_player_dim(sr));
    sr->d->anw.iew.world_rand.r = *wr;
    sr->d->spawner.world_time = sr->w[0].st.world_time;
    /* World.difficultySetting, which the egg path reads (a difficulty op
     * in the same header batch has not reached the living world yet) */
    sr->d->anw.difficulty = sr->w[0].st.difficulty;
    sr->d->spawner.difficulty = sr->w[0].st.difficulty;
    float diff = spawner_local_difficulty(&sr->d->spawner, x, y, z);

    struct living *l = NULL;
    for (int attempt = 0; attempt < attempts && l == NULL; ++attempt)
    {
        struct living *s = living_alloc();
        if (!s) abort();
        living_init(s, &sr->pop.world, HK_SPIDER, &fake);
        s->an = &sr->d->anw;
        s->dimension = sr->d->anw.dimension;
        s->difficulty_factor = diff;
        spider_construct(s, &fake);
        living_set_location_and_angles(s, x, y, z, 0.0F, 0.0F);
        /* the hit's skeleton is constructed, chunk-added and mounted here */
        if (living_on_spawn_with_egg_ret(s, &fake) != NULL) l = s;
        else living_release(s);
    }

    *wr = sr->d->anw.iew.world_rand.r;
    SR_DET(sr).seeder[DET_SERVER] = fake.seeder[DET_OTHER];
    SR_DET(sr).math[DET_SERVER] = fake.math[DET_OTHER];
    SR_DET(sr).next_id[DET_SERVER] = fake.next_id[DET_OTHER];
    sr->d->anw.det = real;
    sr->d->anw.iew.det = real;
    if (l == NULL) return 0;

    /* the skeleton's spawnEntityInWorld (inside the egg path), then the
     * spider's */
    struct living *rider = lv_get(l->ridden_by_entity);
    egg_take_pending_partner();
    struct an_ent *ren = an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]);
    struct an_ent *sen = an_add_living_list(&sr->d->anw, l, sr->d->anw.n);
    an_add_living_chunk(&sr->d->anw, sen, l);
    l->persistence_required = 0;
    rider->persistence_required = 0;
    /* loadedEntityList appends: the rider's higher id comes first, so the
     * post-snapshot id order does not apply to this pair */
    int by_id = sr->order_new_by_id;
    sr->order_new_by_id = 0;
    serverreplay_track_entity(sr, 2, ren);
    serverreplay_track_entity(sr, 2, sen);
    sr->order_new_by_id = by_id;
    /* EntityTracker.addEntityToTracker in each spawnEntityInWorld: the
     * skeleton's spawn at the spider's pose, then the spider's */
    combat_track_spawn(sr, rider);
    combat_track_spawn(sr, l);
    spawner_track(&sr->d->spawner, rider->entity_id, &rider->e);
    spawner_track(&sr->d->spawner, l->entity_id, &l->e);
    sr->native_entities = sr->d->iew.n + sr->d->fhw.n + sr->d->anw.n;
    return 1;
}

static void dev_purge(struct serverreplay *sr)
{
    struct world *w = &sr->pop.world;
    ie_free(&sr->d->iew);
    ie_init(&sr->d->iew, w, &SR_DET(sr));
    sr->d->iew.role = DET_SERVER;
    sr->d->iew.collide_entities = 1;
    fh_free(&sr->d->fhw);
    fh_init(&sr->d->fhw, w, &SR_DET(sr));
    sr->d->fhw.role = DET_SERVER;
    if (sr->mobs_enabled)
    {
        /* the purged livings, and below the player's old twin, are freed
         * once nothing points at them */
        for (int i = 0; i < sr->d->anw.n; ++i)
            if (an_ent_at(sr->d->anw.slot[i])->is_living) grave_bury(lv_get(an_ent_at(sr->d->anw.slot[i])->livh), an_ent_at(sr->d->anw.slot[i]));
        an_free(&sr->d->anw);
        an_init(&sr->d->anw, w, &SR_DET(sr));
        sr->d->anw.shuf_rand = &SR_SHUF(sr);
        /* and WorldServer's village collection and siege, which the
         * villagers' updateAITick and the siege read through it */
        sr->d->anw.village_collection = &sr->vc;
        sr->d->anw.village_siege = &sr->vs;
        sr->d->anw.process_dead = 1;
        sr->d->anw.update_ridden = 1;
        sr->d->anw.iew.role = DET_SERVER;
        /* the spawner starts over empty, but keeps the provider it was
         * built with: the swamp hut's getPossibleCreatures, the chunk
         * loads, the world spawn point the distance rule reads */
        struct spawner keep = sr->d->spawner;
        spawner_free(&sr->d->spawner);
        spawner_init(&sr->d->spawner, w, &SR_DET(sr), &ST_RAND(&sr->w[0].st));
        sr->d->spawner.dim = keep.dim;
        sr->d->spawner.ensure_chunk = keep.ensure_chunk;
        sr->d->spawner.ensure_ctx = keep.ensure_ctx;
        sr->d->spawner.possible_creatures = keep.possible_creatures;
        sr->d->spawner.possible_ctx = keep.possible_ctx;
        spawner_set_scene(&sr->d->spawner, w->seed, sr->w[0].st.difficulty,
                          sr->cal_month, sr->cal_day, keep.spawn_x, keep.spawn_y, keep.spawn_z);
        /* MobFree.purge leaves the player alone: its twin's potion effects
         * and attribute modifiers carry over to the new twin */
        struct living *old = lv_get(sr->player_livh);
        struct an_ent *old_ent = an_deref(sr->player_enth);
        sr->player_livh = 0;
        sr->player_enth = 0;
        serverreplay_bind_player(sr, sr->player, sr->player_entity_id);
        if (old != NULL && sr->player_livh != 0)
        {
            struct living *l = lv_get(sr->player_livh);
            l->potions = old->potions;
            l->potions_need_update = old->potions_need_update;
            l->potion_liquid_color = old->potion_liquid_color;
            l->potion_is_ambient = old->potion_is_ambient;
            l->attr_watch_dirty = old->attr_watch_dirty;
            l->attrs = old->attrs;
        }
        grave_bury(old, old_ent);
    }
    serverreplay_order_players_only(sr);   /* the player is all that is left */
    sr->native_entities = 0;
}

/* The setblock and fill ops: Dev's w.setBlock (flag 3, or the fill's flag 2
 * then every neighbour notify), with the setblock's tile data. */
static int dev_set_blocks(struct serverreplay *sr, struct server_player *sp, const struct jval *c,
                          const char *op, char *err, int err_size)
{
    /* the world the player is in, as Dev's p.worldObj (the replay's own view:
     * at a Nether start's first tick the player's entity still names the
     * overworld until its first update) */
    (void)sp;
    struct world *w = serverreplay_player_world(sr);
    jrand *wrand = serverreplay_world_rand(sr, serverreplay_player_dim(sr));
    int id = integer(c, "id", -1), meta = integer(c, "meta", 0);
    if (id < 0 || id >= 4096 || meta < 0 || meta > 15)
        return error(err, err_size, "invalid block id or metadata");
    struct blockcb_env saved = nw_env->blockcb.env;
    nw_env->blockcb.env.world_rand = wrand;
    nw_env->blockcb.env.det = &SR_DET(sr);
    nw_env->blockcb.env.role = DET_SERVER;
    int x0, y0, z0, x1, y1, z1;
    if (!strcmp(op, "setblock"))
    {
        x0 = x1 = integer(c, "x", 30000000);
        y0 = y1 = integer(c, "y", -1);
        z0 = z1 = integer(c, "z", 30000000);
        const char *mode = json_str(json_get(c, "mode"));
        int old = world_get_block(w, x0, y0, z0);
        if (mode && !strcmp(mode, "keep") && old != 0)
        { nw_env->blockcb.env = saved; return error(err, err_size, "setblock keep occupied"); }
        if (mode && !strcmp(mode, "destroy") && old != 0)
        {
            struct drop_case dc = {0};
            struct drop_ent *drops ENV_LOCAL = envstack_take(DROPS_MAX_ENTS * sizeof *drops);
            dc.block = old;
            dc.meta = world_get_meta(w, x0, y0, z0);
            dc.x = x0; dc.y = y0; dc.z = z0;
            /* World.func_147480_a's playAuxSFX(2001) before the drops */
            env_aux_sfx(w, 2001, x0, y0, z0, (old & 4095) + (dc.meta << 12));
            dc.math = &SR_DET(sr).math[DET_SERVER].r;
            int n = drops_break_live(&dc, wrand, 1.0F, drops, DROPS_MAX_ENTS);
            if (n < 0) { nw_env->blockcb.env = saved; return error(err, err_size, "destroy block drop is unsupported"); }
            for (int i = 0; i < n; ++i)
            {
                int eid = det_next_entity_id_role(&SR_DET(sr), DET_SERVER);
                det_rng rng = det_new_random_role(&SR_DET(sr), DET_SERVER);
                int64_t msb, lsb;
                det_uuid_role(&SR_DET(sr), DET_SERVER, &msb, &lsb);
                struct drop_ent *d = &drops[i];
                ie_ent *en = d->kind == DROP_ITEM
                    ? ie_adopt_item(&sr->d->iew, eid, msb, lsb, det_rng_state(&rng), d->x, d->y, d->z,
                                    d->mx, d->my, d->mz, d->yaw, d->hover, d->item, d->damage, d->count, 0)
                    : ie_adopt_orb(&sr->d->iew, eid, msb, lsb, det_rng_state(&rng), d->x, d->y, d->z,
                                   d->mx, d->my, d->mz, d->yaw, d->xp);
                if (!en) { nw_env->blockcb.env = saved; return error(err, err_size, "destroy drop entity limit"); }
                if (d->kind == DROP_ITEM) en->stack_count_link = stack_link_item(en);
                ie_added_to_world(&sr->d->iew, en);
            }
            world_set_block(w, x0, y0, z0, 0, 0, 3);
        }
        if (mode && strcmp(mode, "replace") && strcmp(mode, "keep") && strcmp(mode, "destroy"))
        { nw_env->blockcb.env = saved; return error(err, err_size, "unknown setblock mode"); }
        /* World.setBlock's boolean is Chunk.func_150807_a's "changed"
         * flag: a write over identical block+meta returns false, and the
         * Java Dev op ignores it (it does not throw on a no-op write).
         * The mode checks above are the only errors. */
        (void)world_set_block(w, x0, y0, z0, id, meta, 3);
        const struct jval *tile = json_get(c, "tile");
        if (tile)
        {
            struct tile_entity *te = world_tile_entity(w, x0, y0, z0);
            if (!te || tile->kind != J_OBJ)
            { nw_env->blockcb.env = saved; return error(err, err_size, "setblock tile unavailable"); }
            if (te->kind == TE_MOB_SPAWNER)
            {
                const char *mob = json_str(json_get(tile, "EntityId"));
                if (mob)
                {
                    char *copy = malloc(strlen(mob) + 1);
                    if (!copy) { nw_env->blockcb.env = saved; return error(err, err_size, "spawner id allocation failed"); }
                    strcpy(copy, mob);
                    free(te->u.spawner.mob_id);
                    te->u.spawner.mob_id = copy;
                }
                te->u.spawner.spawn_delay = integer(tile, "Delay", te->u.spawner.spawn_delay);
                te->u.spawner.desc_delay = te->u.spawner.spawn_delay;
                te->u.spawner.min_spawn_delay = integer(tile, "MinSpawnDelay", te->u.spawner.min_spawn_delay);
                te->u.spawner.max_spawn_delay = integer(tile, "MaxSpawnDelay", te->u.spawner.max_spawn_delay);
                te->u.spawner.spawn_count = integer(tile, "SpawnCount", te->u.spawner.spawn_count);
                te->u.spawner.max_nearby_entities = integer(tile, "MaxNearbyEntities", te->u.spawner.max_nearby_entities);
                te->u.spawner.required_player_range = integer(tile, "RequiredPlayerRange", te->u.spawner.required_player_range);
                te->u.spawner.spawn_range = integer(tile, "SpawnRange", te->u.spawner.spawn_range);
            }
            else if (te->kind == TE_FURNACE)
            {
                te->u.furnace.burn_time = integer(tile, "BurnTime", te->u.furnace.burn_time);
                te->u.furnace.cook_time = integer(tile, "CookTime", te->u.furnace.cook_time);
                const struct jval *items = json_get(tile, "Items");
                if (items)
                {
                    for (int slot = 0; slot < 3; ++slot)
                    {
                        memset(&te->u.furnace.slots[slot], 0, sizeof te->u.furnace.slots[slot]);
                        te->u.furnace.slots[slot].item = -1;
                    }
                    for (int row = 0; row < json_len(items); ++row)
                    {
                        const struct jval *it = json_at(items, row);
                        int slot = integer(it, "Slot", -1);
                        if (slot < 0 || slot >= 3)
                        { nw_env->blockcb.env = saved; return error(err, err_size, "invalid furnace slot"); }
                        te->u.furnace.slots[slot].item = integer(it, "id", -1);
                        te->u.furnace.slots[slot].count = integer(it, "Count", 0);
                        te->u.furnace.slots[slot].damage = integer(it, "Damage", 0);
                    }
                }
                /* the Dev op's readFromNBT: currentItemBurnTime is not
                 * saved but recomputed from the fuel slot */
                te->u.furnace.fuel_total = te->u.furnace.slots[1].count > 0 ?
                                           furnace_fuel_value(te->u.furnace.slots[1].item) : 0;
            }
            else if (te->kind == TE_CHEST || te->kind == TE_DISPENSER || te->kind == TE_HOPPER ||
                     te->kind == TE_BREWING_STAND)
            {
                /* the chest's, the dispenser's (the dropper's too), the
                 * hopper's and the brewing stand's readFromNBT: a fresh
                 * slot array, then each listed stack whose Slot byte is in
                 * range (ItemStack.loadItemStackFromNBT: a negative damage
                 * reads as 0) */
                struct te_stack *slots = te->kind == TE_CHEST ? te->u.chest.slots
                                       : te->kind == TE_DISPENSER ? te->u.dispenser.slots
                                       : te->kind == TE_HOPPER ? te->u.hopper.slots : te->u.brewing.slots;
                int nslots = te->kind == TE_CHEST ? CHEST_SLOTS : te->kind == TE_DISPENSER ? DISPENSER_SLOTS
                           : te->kind == TE_HOPPER ? 5 : 4;
                const struct jval *items = json_get(tile, "Items");
                if (items)
                {
                    for (int slot = 0; slot < nslots; ++slot)
                    {
                        memset(&slots[slot], 0, sizeof slots[slot]);
                        slots[slot].item = -1;
                    }
                    for (int row = 0; row < json_len(items); ++row)
                    {
                        const struct jval *it = json_at(items, row);
                        int slot = integer(it, "Slot", -1) & 255;
                        if (slot >= nslots) continue;
                        int damage = integer(it, "Damage", 0);
                        slots[slot].item = integer(it, "id", -1);
                        slots[slot].count = (signed char)integer(it, "Count", 0);
                        slots[slot].damage = damage < 0 ? 0 : damage;
                    }
                }
            }
            else if (te->kind == TE_SIGN)
            {
                /* TileEntitySign.readFromNBT: each line cut to 15 chars */
                for (int line = 0; line < 4; ++line)
                {
                    char key[8];
                    snprintf(key, sizeof key, "Text%d", line + 1);
                    const char *s = json_str(json_get(tile, key));
                    if (!s) continue;
                    size_t n = 0, chars = 0;
                    while (s[n] && chars < 15)
                    {
                        size_t step = 1;
                        while ((s[n + step] & 0xC0) == 0x80) ++step;
                        if (n + step >= sizeof te->sign_text[line]) break;
                        n += step;
                        ++chars;
                    }
                    memcpy(te->sign_text[line], s, n);
                    te->sign_text[line][n] = 0;
                }
            }
            else if (te->kind == TE_SKULL)
            {
                /* TileEntitySkull.readFromNBT's two bytes */
                te->skull_type = (signed char)integer(tile, "SkullType", te->skull_type);
                te->skull_rot = (signed char)integer(tile, "Rot", te->skull_rot);
            }
            else { nw_env->blockcb.env = saved; return error(err, err_size, "unsupported tile data"); }
            free(te->raw);
            te->raw = NULL;
            /* TileEntity.onInventoryChanged: World.func_147453_f */
            comparator_notify(w, x0, y0, z0, world_get_block(w, x0, y0, z0));
        }
    }
    else
    {
        x0 = integer(c, "x0", 30000000); y0 = integer(c, "y0", -1); z0 = integer(c, "z0", 30000000);
        x1 = integer(c, "x1", -30000000); y1 = integer(c, "y1", -1); z1 = integer(c, "z1", -30000000);
        if (x1 < x0 || y1 < y0 || z1 < z0 || (int64_t)(x1-x0+1)*(y1-y0+1)*(z1-z0+1) > 32768)
        { nw_env->blockcb.env = saved; return error(err, err_size, "invalid fill box"); }
        for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
            world_set_block(w, x, y, z, id, meta, 2);
        for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
            world_notify_neighbors(w, x, y, z, id);
    }
    nw_env->blockcb.env = saved;
    return 1;
}

/* TileEntityDispenser's Det.newRandom on the server thread */
static void dev_dispenser_random(void *ctx)
{
    (void)det_new_random_role((det_state *)ctx, DET_SERVER);
}

int dev_apply(struct serverreplay *sr, struct server_player *sp, const struct jval *c,
              char *err, int err_size)
{
    const char *op = json_str(json_get(c, "op"));
    if (op == NULL) return error(err, err_size, "missing dev op");

    if (!strcmp(op, "time"))
    {
        const char *mode = json_str(json_get(c, "mode"));
        int value = integer(c, "value", -1);
        if (value < 0) return error(err, err_size, "time requires a nonnegative value");
        for (int i = 0; i < sr->nworlds; ++i)
        {
            struct servertick *st = &sr->w[i].st;
            st->world_time = mode && !strcmp(mode, "add") ? st->world_time + value : value;
        }
        return 1;
    }

    if (!strcmp(op, "weather"))
    {
        const char *type = json_str(json_get(c, "type"));
        struct servertick *st = &sr->w[0].st;
        det_rng weather_rng = det_new_random_role(&SR_DET(sr), DET_SERVER);
        int duration = (300 + det_rng_int_n(&weather_rng, 600)) * 20;
        if (json_get(c, "seconds")) duration = integer(c, "seconds", 600) * 20;
        if (type && !strcmp(type, "clear"))
        {
            st->rain_time = st->thunder_time = 0;
            st->raining = st->thundering = 0;
        }
        else if (type && (!strcmp(type, "rain") || !strcmp(type, "thunder")))
        {
            st->rain_time = duration;
            if (!strcmp(type, "thunder")) st->thunder_time = duration;
            st->raining = 1;
            st->thundering = !strcmp(type, "thunder");
        }
        else return error(err, err_size, "unknown weather type");
        return 1;
    }

    if (!strcmp(op, "gamerule"))
    {
        const char *name = json_str(json_get(c, "name"));
        const char *value = json_str(json_get(c, "value"));
        if (!name || !value) return error(err, err_size, "gamerule needs name and value");
        if (!strcmp(name, "doDaylightCycle"))
        {
            for (int i = 0; i < sr->nworlds; ++i) sr->w[i].st.daylight_cycle = !strcmp(value, "true");
        }
        else if (!strcmp(name, "keepInventory")) surv.keep_inventory = !strcmp(value, "true");
        else return error(err, err_size, "unknown gamerule");
        return 1;
    }

    if (!strcmp(op, "difficulty"))
    {
        int v = (int)integer(c, "value", -1);
        if (v < 0 || v > 3) return error(err, err_size, "difficulty must be 0..3");
        serverreplay_set_difficulty(sr, v);   /* MinecraftServer.func_147139_a */
        return 1;
    }

    if (!strcmp(op, "clear"))
    {
        int item = integer(c, "item", -1);
        for (int i = 0; i < 40; ++i)
        {
            if (sp->sv.inv[i].count > 0 && (item < 0 || sp->sv.inv[i].item == item))
            {
                sp->sv.inv[i].count = 0;
                sp->sv.inv[i].item = -1;
                sp->sv.inv[i].tag = 0;
            }
        }
        return 1;
    }

    if (!strcmp(op, "xp"))
    {
        int levels = integer(c, "levels", 0);
        if (json_get(c, "levels"))
        {
            sp->sv.xp_level += levels;
            if (sp->sv.xp_level < 0) { sp->sv.xp_level = 0; sp->sv.xp_total = 0; sp->sv.xp_progress = 0; }
        }
        else
        {
            int points = integer(c, "points", -1);
            if (points < 0) return error(err, err_size, "xp points must be nonnegative");
            surv_server_add_xp(sp, points); /* addScore included */
        }
        return 1;
    }

    if (!strcmp(op, "setblock") || !strcmp(op, "fill"))
    {
        int rc;
        /* TileEntityDispenser's constructor (a dropper's too) spends one
         * Det.newRandom seeder draw, on the server thread's role here; the
         * placement case's OTHER-role draw stays off for these writes */
        void (*saved_hook)(void *) = te_dispenser_random;
        void *saved_ctx = te_dispenser_random_ctx;
        det_state *saved_case = place_case_det_swap(NULL);
        te_dispenser_random = dev_dispenser_random;
        te_dispenser_random_ctx = &SR_DET(sr);
        struct randomtick_env saved_rt;
        int saved_role = sr->d->iew.role;
        serverreplay_dev_block_env(sr, serverreplay_world_rand(sr, serverreplay_player_dim(sr)), &saved_rt);
        sr->d->iew.role = DET_SERVER;
        rc = dev_set_blocks(sr, sp, c, op, err, err_size);
        sr->d->iew.role = saved_role;
        randomtick_tick_load(&saved_rt);
        te_dispenser_random = saved_hook;
        te_dispenser_random_ctx = saved_ctx;
        place_case_det_swap(saved_case);
        return rc;
    }

    if (!strcmp(op, "give"))
    {
        int item = integer(c, "item", -1), count = integer(c, "count", 1);
        if (item < 0 || count < 1 || count > 64) return error(err, err_size, "invalid give item or count");
        struct surv_stack stack = {0};
        stack.item = item;
        stack.count = count;
        stack.damage = integer(c, "meta", 0);
        stack.tag = dev_stack_tag(c);
        int first = sr->d->iew.n;
        sr->d->iew.role = DET_SERVER;
        throw_item(sp, &stack, 0);
        if (sr->d->iew.n <= first) return error(err, err_size, "give did not construct an item");
        ie_ent_at(sr->d->iew.slot[first])->delay = 0;
        ie_ent_at(sr->d->iew.slot[first])->owner_name = "Player";
        return 1;
    }

    if (!strcmp(op, "summon"))
    {
        static const struct { const char *name; int kind; } kinds[] = {
            {"Pig", AK_PIG}, {"Cow", AK_COW}, {"Sheep", AK_SHEEP}, {"Chicken", AK_CHICKEN},
            {"MushroomCow", AK_MOOSHROOM}, {"Zombie", HK_ZOMBIE}, {"Skeleton", HK_SKELETON},
            {"Creeper", HK_CREEPER}, {"Spider", HK_SPIDER}, {"CaveSpider", HK_CAVE_SPIDER},
            {"Enderman", HK_ENDERMAN},
            {"Slime", SK_SLIME}, {"Witch", HK_WITCH}, {"Bat", AK_BAT}, {"Squid", AK_SQUID},
            {"CaveSpider", HK_CAVE_SPIDER}, {"Silverfish", HK_SILVERFISH},
            {"IronGolem", VK_IRON_GOLEM}, {"VillagerGolem", VK_IRON_GOLEM}, {"ZombiePigman", HK_PIGMAN}, {"Pigman", HK_PIGMAN},
            {"PigZombie", HK_PIGMAN},
            {"Villager", VK_VILLAGER},
            {"Blaze", HK_BLAZE}, {"MagmaCube", SK_MAGMA_CUBE}, {"LavaSlime", SK_MAGMA_CUBE},
            {"Ghast", GK_GHAST}
        };
        const char *name = json_str(json_get(c, "name"));
        double x, y, z;
        if (!name || !real(c, "x", &x) || !real(c, "y", &y) || !real(c, "z", &z))
            return error(err, err_size, "summon requires name, x, y, z");
        int child = integer(c, "child", 0), size = integer(c, "size", 0);
        /* Dev.java: a zombie's or a pigman's IsBaby, an EntityAgeable's Age */
        if (child && strcmp(name, "Zombie") && strcmp(name, "ZombiePigman") && strcmp(name, "Pigman") &&
            strcmp(name, "PigZombie") && strcmp(name, "Pig") && strcmp(name, "Cow") && strcmp(name, "Sheep") &&
            strcmp(name, "Chicken") && strcmp(name, "MushroomCow") && strcmp(name, "Villager"))
            return error(err, err_size, "child is only supported on Zombie, PigZombie and the ageables");
        for (unsigned i = 0; i < sizeof kinds / sizeof kinds[0]; ++i)
            if (!strcmp(name, kinds[i].name))
            {
                if (!dev_summon(sr, kinds[i].kind, x, y, z, child, size))
                    return error(err, err_size, "summon failed");
                /* Entity.readFromNBT of the bare tag: setAir(getShort("Air"))
                 * is 0, not the constructor's 300 (the first tick out of
                 * water puts 300 back; in water it counts down from 0) */
                for (int k = sr->d->anw.n - 1; k >= 0; --k)
                    if (an_ent_at(sr->d->anw.slot[k])->is_living &&
                        lv_get(an_ent_at(sr->d->anw.slot[k])->livh)->kind == kinds[i].kind)
                    {
                        lv_get(an_ent_at(sr->d->anw.slot[k])->livh)->air = 0;
                        break;
                    }
                /* Dev.java's IsVillager and ActiveEffects keys: readFromNBT's
                 * flag and its plain map puts (no attribute modifiers) */
                const struct jval *fx = json_get(c, "effects");
                int villager = integer(c, "villager", 0);
                int loot = integer(c, "loot", -1), breakdoors = integer(c, "breakdoors", -1);
                if (villager && kinds[i].kind != HK_ZOMBIE)
                    return error(err, err_size, "villager is only supported on Zombie");
                if (breakdoors >= 0 && kinds[i].kind != HK_ZOMBIE)
                    return error(err, err_size, "breakdoors is only supported on Zombie");
                if (villager || fx || loot >= 0 || breakdoors >= 0)
                {
                    struct living *l = NULL;
                    for (int k = sr->d->anw.n - 1; k >= 0 && l == NULL; --k)
                        if (an_ent_at(sr->d->anw.slot[k])->is_living && lv_get(an_ent_at(sr->d->anw.slot[k])->livh)->kind == kinds[i].kind)
                            l = lv_get(an_ent_at(sr->d->anw.slot[k])->livh);
                    if (l == NULL) return error(err, err_size, "summon: no summoned living");
                    if (villager) l->zombie_is_villager = 1;
                    /* Dev.java's "loot" (setCanPickUpLoot after the egg's
                     * roll) and "breakdoors" (EntityZombie.func_146070_a:
                     * the flag and the EntityAIBreakDoor task) */
                    if (loot >= 0) l->can_pick_up_loot = loot != 0;
                    if (breakdoors >= 0 && (breakdoors != 0) != (l->zombie_can_break_doors != 0))
                    {
                        l->zombie_can_break_doors = breakdoors != 0;
                        if (breakdoors) ai_add_break_door(l);
                        else ai_remove_task(l, AIC_BREAK_DOOR);
                    }
                    /* EntityLivingBase.potionsNeedUpdate starts true: the
                     * first update sets the effect colour, and the colour's
                     * per-tick particle roll draws the entity's Random */
                    if (fx) l->potions_need_update = 1;
                    for (int k = 0; fx && k < json_len(fx); ++k)
                    {
                        const struct jval *a = json_at(fx, k);
                        struct potion_effect eff = {
                            .id = (uint8_t)int_at(json_at(a, 0), 0),
                            .amplifier = (int8_t)int_at(json_at(a, 1), 0),
                            .duration = int_at(json_at(a, 2), 0),
                        };
                        potion_map_put(&l->potions, &eff);
                    }
                }
                const struct jval *equip = json_get(c, "equip");
                if (equip && !dev_summon_equip(sr, kinds[i].kind, equip))
                    return error(err, err_size, "summon equip: no summoned living or a bad slot");
                return 1;
            }
        return error(err, err_size, "native cannot construct summoned name");
    }

    if (!strcmp(op, "hurt"))
    {
        /* EntityDragon.attackEntityFromPart(part, causePlayerDamage(player), amount) */
        double amount;
        int part = integer(c, "part", 0);
        const char *target = json_str(json_get(c, "target"));
        struct dragon_state *d = serverreplay_dragon(sr);
        if (!target || strcmp(target, "dragon") || part < 0 || part > 6 || !real(c, "amount", &amount) || amount <= 0.0)
            return error(err, err_size, "hurt requires target dragon, part 0..6 and a positive amount");
        if (d == NULL || sp->dimension != 1) return error(err, err_size, "hurt: no dragon in the player's world");
        d->player_x = sp->e.pos_x;
        d->player_y = sp->e.pos_y;
        d->player_z = sp->e.pos_z;
        /* onDeath's addToPlayerScore: the mobKills counter */
        if (dragon_attack_part(d, part, (float)amount, 1)) surv_add_stat(sp, STAT_MOB_KILLS, 1);
        return 1;
    }

    if (!strcmp(op, "hanging"))
    {
        const char *kind = json_str(json_get(c, "kind"));
        double x, y, z;
        int dir;
        if (!kind || (!strcmp(kind, "painting") && !strcmp(kind, "item_frame")) ||
            !real(c, "x", &x) || !real(c, "y", &y) || !real(c, "z", &z) ||
            (dir = integer(c, "dir", -1)) < 0 || dir > 3)
            return error(err, err_size, "hanging requires kind painting|item_frame, x, y, z and dir 0..3");
        int tile_x = (int)x, tile_y = (int)y, tile_z = (int)z;
        /* EntityPainting(World,x,y,z,dir,art) / EntityItemFrame(World,x,y,z,dir),
         * the placement ItemHangingEntity.onItemUse makes, then
         * spawnEntityInWorld. The constructor's Det draws (the id, the Random,
         * the UUID) run on the SERVER slot through the fake-det dance. */
        det_state fake;
        sr_constructor_begin(&fake, &SR_DET(sr), DET_SERVER);
        det_state *saved_det = sr->d->fhw.det;
        int saved_role = sr->d->fhw.role;
        sr->d->fhw.det = &fake;
        sr->d->fhw.role = DET_OTHER;   /* the fake carries the SERVER streams in its OTHER slot */
        fh_ent *fh = !strcmp(kind, "painting")
            ? fh_spawn_painting(&sr->d->fhw, tile_x, tile_y, tile_z, dir)
            : fh_spawn_frame(&sr->d->fhw, tile_x, tile_y, tile_z, dir);
        if (fh && !strcmp(kind, "painting") && json_get(c, "art"))
        {
            /* the 6-arg painting constructor: the 5-arg loop above already
             * drew its candidates pick, this overrides art by title with no
             * extra draw (EntityPainting(World,...,String)) */
            const char *title = json_str(json_get(c, "art"));
            static const char *const titles[26] = {
                "Kebab", "Aztec", "Alban", "Aztec2", "Bomb", "Plant", "Wasteland",
                "Pool", "Courbet", "Sea", "Sunset", "Creebet", "Wanderer", "Graham",
                "Match", "Bust", "Stage", "Void", "SkullAndRoses", "Wither",
                "Fighters", "Pointer", "Pigscene", "BurningSkull", "Skeleton",
                "DonkeyKong"
            };
            fh->art = 0;
            for (int i = 0; i < 26; ++i)
                if (!strcmp(titles[i], title)) { fh->art = i; break; }
            fh_set_direction(fh, dir);
        }
        if (fh && !strcmp(kind, "item_frame") && json_get(c, "item"))
        {
            int item = integer(c, "item", 0);
            if (item <= 0) { sr->d->fhw.det = saved_det; sr->d->fhw.role = saved_role; sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER); return error(err, err_size, "hanging item must be a positive id"); }
            /* setDisplayedItem: the watcher update only, no draw */
            fh->item = item;
            fh->item_damage = integer(c, "meta", 0);
            fh->rot = integer(c, "rot", 0) % 4;
        }
        sr->d->fhw.det = saved_det;
        sr->d->fhw.role = saved_role;
        sr_constructor_end(&SR_DET(sr), &fake, DET_SERVER);
        if (!fh) return error(err, err_size, "hanging did not construct");
        fh_added_to_world(&sr->d->fhw, fh);
        serverreplay_track_entity(sr, 1, fh);
        sr->native_entities = sr->d->iew.n + sr->d->fhw.n + sr->d->anw.n;
        return 1;
    }

    if (!strcmp(op, "jockey"))
    {
        const char *kind = json_str(json_get(c, "kind"));
        double x, y, z;
        int attempts = integer(c, "attempts", 1000);
        if (!kind || attempts < 1 || attempts > 1000000 || !real(c, "x", &x) || !real(c, "y", &y) || !real(c, "z", &z))
            return error(err, err_size, "jockey requires kind, x, y, z and attempts 1..1000000");
        if (strcmp(kind, "spider_skeleton") && strcmp(kind, "baby_zombie_chicken"))
            return error(err, err_size, "jockey kind must be spider_skeleton or baby_zombie_chicken");
        if (!dev_jockey(sr, kind, x, y, z, attempts))
            return error(err, err_size, "jockey did not construct");
        sr->native_entities = sr->d->iew.n + sr->d->fhw.n + sr->d->anw.n;
        return 1;
    }

    if (!strcmp(op, "purge"))
    {
        dev_purge(sr);
        return 1;
    }

    if (!strcmp(op, "orb"))
    {
        /* new EntityXPOrb(world, x, y, z, value), then spawnEntityInWorld */
        double x, y, z;
        int value = integer(c, "value", 0);
        if (!real(c, "x", &x) || !real(c, "y", &y) || !real(c, "z", &z) || value < 1 || value > 32767)
            return error(err, err_size, "orb requires x, y, z and a value 1..32767");
        sr->d->iew.role = DET_SERVER;
        ie_ent *en = ie_spawn_orb(&sr->d->iew, x, y, z, value);
        if (en == NULL) return error(err, err_size, "orb did not construct");
        ie_added_to_world(&sr->d->iew, en);
        return 1;
    }

    if (!strcmp(op, "lightning"))
    {
        /* new EntityLightningBolt(world, x, y, z), then World.addWeatherEffect:
         * the ctor spends its entity id, its own Random, the uuid, then
         * boltVertex (nextLong) and boltLivingTime (nextInt(3) + 1) on that
         * Random, then the fire placement's four nextInt(3) draws (the air
         * check at the strike point draws nothing). state 2's two floats are
         * the first lightning_tick's, not the ctor's. */
        double x, y, z;
        if (!real(c, "x", &x) || !real(c, "y", &y) || !real(c, "z", &z))
            return error(err, err_size, "lightning requires x, y, z");
        int eid = det_next_entity_id_role(&SR_DET(sr), DET_SERVER);
        det_rng rand = det_new_random_role(&SR_DET(sr), DET_SERVER);
        int64_t msb, lsb;
        det_uuid_role(&SR_DET(sr), DET_SERVER, &msb, &lsb);
        int64_t vertex = det_rng_long(&rand);
        int living_time = det_rng_int_n(&rand, 3) + 1;
        /* the player's world (a strike in the Nether burns the Nether) */
        struct sr_world *ws = &sr->w[0];
        for (int i = 0; i < sr->nworlds; ++i)
            if (sr->w[i].dim == serverreplay_player_dim(sr)) ws = &sr->w[i];
        struct world *w = ws->st.w != NULL ? ws->st.w : &sr->pop.world;
        lightning_constructor_fire(w, &rand, x, y, z, ws->st.difficulty, !ws->st.fire_rule_off);
        if (sr->d->nbolts == sr->d->capbolts)
        {
            sr->d->capbolts = sr->d->capbolts ? sr->d->capbolts * 2 : 8;
            sr->d->bolts = realloc(sr->d->bolts, (size_t)sr->d->capbolts * sizeof *sr->d->bolts);
        }
        lightning_adopt(&sr->d->bolts[sr->d->nbolts++], x, y, z, det_rng_state(&rand),
                        vertex, living_time);
        serverreplay_bolt_packet(sr, 0, x, y, z);
        (void)eid;
        (void)msb;
        (void)lsb;
        return 1;
    }

    if (!strcmp(op, "slot"))
    {
        int slot = integer(c, "slot", -1), item = integer(c, "item", -1);
        if (slot < 0 || slot >= 40 || item < 0 || integer(c, "count", 1) < 1 || integer(c, "count", 1) > 64)
            return error(err, err_size, "invalid slot");
        sp->sv.inv[slot].item = item;
        sp->sv.inv[slot].count = integer(c, "count", 1);
        sp->sv.inv[slot].damage = integer(c, "meta", 0);
        sp->sv.inv[slot].tag = dev_stack_tag(c);
        /* Dev.java: p.inventoryContainer.detectAndSendChanges() right after
         * the write, which reaches a dead player's client too */
        sp->dev_prequeued = 1;
        surv_sync_inventory(sp);
        return 1;
    }

    if (!strcmp(op, "state"))
    {
        double v;
        if (real(c, "health", &v)) sp->sv.health = (float)v;
        if (json_get(c, "food")) sp->sv.food.level = integer(c, "food", 20);
        if (real(c, "saturation", &v)) sp->sv.food.saturation = (float)v;
        if (real(c, "exhaustion", &v)) sp->sv.food.exhaustion = (float)v;
        if (json_get(c, "air")) sp->sv.air = integer(c, "air", 300);
        if (json_get(c, "fire")) sp->e.fire = integer(c, "fire", 0);
        if (real(c, "fall", &v)) sp->e.fall_distance = (float)v;
        /* (a dead player out of the world has no tracker entry: nothing
         * goes out) */
        if (json_get(c, "health") && !sp->sv.removed)
        {
            struct s2c_pkt *pkt = s2c_add(s2c_out());
            pkt->kind = PK_S1C;
            pkt->f0 = sp->sv.health;
            sp->dev_prequeued = 1;
            /* setHealth dirties data watcher 6: the world's tracker pass
             * after updateEntities sends the value it holds then */
            sp->dev_s1c_index = s2c_out()->n;
        }
        return 1;
    }

    if (!strcmp(op, "tp"))
    {
        double x, y, z, yaw, pitch;
        if (!real(c, "x", &x) || !real(c, "y", &y) || !real(c, "z", &z))
            return error(err, err_size, "tp requires x, y, z");
        if (!real(c, "yaw", &yaw)) yaw = sp->rotation_yaw;
        if (!real(c, "pitch", &pitch)) pitch = sp->rotation_pitch;
        /* Dev's p.mountEntity(null) first: EntityPlayerMP.mountEntity ends
         * in setPlayerLocation at the current position, so an S08 there
         * leads the destination's (its C06 finds hasMoved false and does
         * not match the destination) */
        /* a riding player steps off the whole way (dismountEntity's spot,
         * the S1B, the S08 where it lands) */
        if (sp->ridingh != 0) ride_server_dismount(sp);
        else
        {
            struct s2c_pkt *here = s2c_add(s2c_out());
            here->kind = PK_S08;
            here->f0 = sp->e.pos_x;
            here->f1 = sp->e.pos_y + 1.6200000047683716;
            here->f2 = sp->e.pos_z;
            here->f3 = sp->rotation_yaw;
            here->f4 = sp->rotation_pitch;
        }
        entity_set_position(&sp->e, x, y, z);
        sp->rotation_yaw = (float)yaw;
        sp->rotation_pitch = (float)pitch;
        sp->prev_rotation_yaw = (float)yaw;
        sp->prev_rotation_pitch = (float)pitch;
        sp->has_moved = 0;
        sp->last_pos_x = x;
        sp->last_pos_y = y;
        sp->last_pos_z = z;
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S08;
        pkt->f0 = x;
        pkt->f1 = y + 1.6200000047683716;
        pkt->f2 = z;
        pkt->f3 = (float)yaw;
        pkt->f4 = (float)pitch;
        sp->dev_prequeued = 1;
        return 1;
    }

    return error(err, err_size, "unknown or unsupported dev op");
}
