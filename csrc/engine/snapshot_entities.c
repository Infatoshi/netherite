/* Construct the post-join entity list from the snapshot's class, NBT, ID and
 * private Random. Constructors run against throwaway Det streams: the snapshot
 * already carries the real streams after the Java constructors ran. */
#include "itemtag.h"
#include "leash.h"
#include "serverreplay.h"
#include "env.h"
#include "ai.h"
#include "snap_runtime.h"

#include "hostiles_creeper.h"
#include "hostiles.h"
#include "hostiles_enderman.h"
#include "hostiles_pigman.h"
#include "hostiles_silverfish.h"
#include "slimes.h"
#include "ghasts.h"
#include "villagers.h"
#include "entity_nbt.h"
#include "projectile.h"
#include "fallhang.h"
#include "endfight.h"
#include "blocks.h"
#include "items.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int num(const nbt *o, const char *key)
{
    return (int)nbt_int_value(nbt_get(o, key));
}

static int len(const nbt *v) { return v ? nbt_list_size(v) : 0; }

static float fl(const nbt *o, const char *key)
{
    const nbt *tag = nbt_get(o, key);
    uint32_t bits = tag ? nbt_float_bits(tag) : 0;
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

static double dbl(const nbt *v)
{
    uint64_t bits = v ? nbt_double_bits(v) : 0;
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static float fval(const nbt *v)
{
    uint32_t bits = v ? nbt_float_bits(v) : 0;
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static double atd(const nbt *o, const char *key, int index)
{
    return dbl(nbt_list_get(nbt_get(o, key), index));
}

static float atf(const nbt *o, const char *key, int index)
{
    return fval(nbt_list_get(nbt_get(o, key), index));
}

static int kind_for_class(const char *c)
{
    static const struct { const char *name; int kind; } map[] = {
        {"EntityPig", AK_PIG}, {"EntityCow", AK_COW}, {"EntityMooshroom", AK_MOOSHROOM},
        {"EntityChicken", AK_CHICKEN}, {"EntitySheep", AK_SHEEP},
        {"EntityVillager", VK_VILLAGER}, {"EntityIronGolem", VK_IRON_GOLEM},
        {"EntitySlime", SK_SLIME}, {"EntityMagmaCube", SK_MAGMA_CUBE},
        {"EntityZombie", HK_ZOMBIE}, {"EntitySkeleton", HK_SKELETON},
        {"EntityCreeper", HK_CREEPER}, {"EntitySpider", HK_SPIDER},
        {"EntityCaveSpider", HK_CAVE_SPIDER}, {"EntityGhast", GK_GHAST},
        {"EntitySquid", AK_SQUID}, {"EntityBat", AK_BAT},
        {"EntityEnderman", HK_ENDERMAN}, {"EntityWitch", HK_WITCH},
        {"EntitySilverfish", HK_SILVERFISH}, {"EntityPigZombie", HK_PIGMAN},
        {"EntityBlaze", HK_BLAZE}
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; ++i)
        if (strcmp(c, map[i].name) == 0) return map[i].kind;
    return -1;
}

/* EntityList.getEntityString's names, the reverse of the write side
 * (living_kind_name): the "id" string a Riding compound carries. */
int sr_kind_for_class(const char *c)
{
    static const struct { const char *name; int kind; } names[] = {
        {"Pig", AK_PIG}, {"Cow", AK_COW}, {"MushroomCow", AK_MOOSHROOM},
        {"Chicken", AK_CHICKEN}, {"Sheep", AK_SHEEP},
        {"Villager", VK_VILLAGER}, {"VillagerGolem", VK_IRON_GOLEM},
        {"Slime", SK_SLIME}, {"LavaSlime", SK_MAGMA_CUBE},
        {"Zombie", HK_ZOMBIE}, {"Skeleton", HK_SKELETON},
        {"Creeper", HK_CREEPER}, {"Spider", HK_SPIDER},
        {"CaveSpider", HK_CAVE_SPIDER}, {"Ghast", GK_GHAST},
        {"Squid", AK_SQUID}, {"Bat", AK_BAT},
        {"Enderman", HK_ENDERMAN}, {"Witch", HK_WITCH},
        {"Silverfish", HK_SILVERFISH}, {"PigZombie", HK_PIGMAN},
        {"Blaze", HK_BLAZE}
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i)
        if (strcmp(c, names[i].name) == 0) return names[i].kind;
    return -1;
}

static int spawn_kind(int kind)
{
    static const int kinds[] = {
        [HK_ZOMBIE] = SP_ZOMBIE, [HK_SKELETON] = SP_SKELETON,
        [HK_SPIDER] = SP_SPIDER, [HK_CREEPER] = SP_CREEPER,
        [SK_SLIME] = SP_SLIME, [HK_ENDERMAN] = SP_ENDERMAN,
        [HK_WITCH] = SP_WITCH, [AK_SHEEP] = SP_SHEEP,
        [AK_PIG] = SP_PIG, [AK_CHICKEN] = SP_CHICKEN,
        [AK_COW] = SP_COW, [AK_MOOSHROOM] = SP_MOOSHROOM,
        [AK_BAT] = SP_BAT, [AK_SQUID] = SP_SQUID,
        [GK_GHAST] = SP_GHAST, [HK_PIGMAN] = SP_PIG_ZOMBIE,
        [SK_MAGMA_CUBE] = SP_MAGMA_CUBE, [HK_BLAZE] = SP_BLAZE
    };
    /* World.countEntities counts IMob, EntityAnimal, EntityAmbientCreature
     * and EntityWaterMob; a villager or a golem is none of them, a cave
     * spider or a silverfish is a monster without a spawn list row */
    if (kind == VK_VILLAGER || kind == VK_IRON_GOLEM) return SP_OTHER;
    if (kind == HK_CAVE_SPIDER || kind == HK_SILVERFISH) return SP_KINDS;
    return kind >= 0 && kind < (int)(sizeof kinds / sizeof kinds[0]) ? kinds[kind] : -1;
}

static void construct(struct living *l, det_state *fake)
{
    switch (l->kind)
    {
        case AK_PIG: case AK_COW: case AK_MOOSHROOM: case AK_CHICKEN:
        case AK_SHEEP: case AK_SQUID: case AK_BAT:
            animal_construct(l, fake); break;
        case VK_VILLAGER: villager_construct(l, fake); break;
        case VK_IRON_GOLEM: iron_golem_construct(l, fake); break;
        case SK_SLIME: case SK_MAGMA_CUBE: slime_construct(l, fake); break;
        case HK_ZOMBIE: zombie_construct(l, fake); break;
        case HK_SKELETON: skeleton_construct(l, fake); break;
        case HK_CREEPER: creeper_construct(l, fake); break;
        case HK_SPIDER: case HK_CAVE_SPIDER: spider_construct(l, fake); break;
        case HK_ENDERMAN: enderman_construct(l, fake); break;
        case HK_WITCH: witch_construct(l, fake); break;
        case HK_SILVERFISH: silverfish_construct(l, fake); break;
        case HK_PIGMAN: pigman_construct(l, fake); break;
        case HK_BLAZE: blaze_construct(l, fake); break;
        case GK_GHAST: ghast_construct(l, fake); break;
    }
}

static void restore_attributes(struct living *l, const nbt *tag)
{
    const nbt *a = nbt_get(tag, "Attributes");
    for (int i = 0; i < len(a); ++i)
    {
        const nbt *one = nbt_list_get(a, i);
        const char *name = nbt_string_value(nbt_get(one, "Name"));
        if (!name) continue;
        int j = attrs_index_by_name(name);
        if (j < 0) continue;
        {
            struct attr_instance *dst = &l->attrs.a[j];
            dst->base = dbl(nbt_get(one, "Base"));
            dst->nmods = 0;
            const nbt *mods = nbt_get(one, "Modifiers");
            for (int k = 0; k < len(mods); ++k)
            {
                const nbt *m = nbt_list_get(mods, k);
                struct attr_mod mod = {0};
                const char *mn = nbt_string_value(nbt_get(m, "Name"));
                if (mn) mod.name = attr_name_id(mn);
                mod.amount = dbl(nbt_get(m, "Amount"));
                mod.operation = num(m, "Operation");
                mod.uuid_msb = nbt_int_value(nbt_get(m, "UUIDMost"));
                mod.uuid_lsb = nbt_int_value(nbt_get(m, "UUIDLeast"));
                mod.saved = 1;
                attrs_apply(dst, &mod);
            }
            dst->needs_update = 1;
        }
    }
}

/* Entity.readFromNBT and the kind's readEntityFromNBT: everything a saved
 * living entity carries (not its id or its Random, which NBT does not). */
static void restore_living_nbt(struct living *l, const nbt *tag, int loaded)
{
    l->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    l->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    l->rotation_yaw = atf(tag, "Rotation", 0);
    l->rotation_pitch = atf(tag, "Rotation", 1);
    /* a load from disk ends in Entity.setRotation, which keeps yaw % 360
     * and pitch % 360; the snapshot's live entities were never loaded */
    if (loaded)
    {
        l->rotation_yaw = fmodf(l->rotation_yaw, 360.0F);
        l->rotation_pitch = fmodf(l->rotation_pitch, 360.0F);
    }
    living_set_location_and_angles(l, atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2),
                                   l->rotation_yaw, l->rotation_pitch);
    l->e.motion_x = atd(tag, "Motion", 0);
    l->e.motion_y = atd(tag, "Motion", 1);
    l->e.motion_z = atd(tag, "Motion", 2);
    /* a load: Entity.readFromNBT drops a motion component over 10 */
    if (loaded)
    {
        if (fabs(l->e.motion_x) > 10.0) l->e.motion_x = 0.0;
        if (fabs(l->e.motion_y) > 10.0) l->e.motion_y = 0.0;
        if (fabs(l->e.motion_z) > 10.0) l->e.motion_z = 0.0;
    }
    l->e.fall_distance = fl(tag, "FallDistance");
    l->e.fire = num(tag, "Fire");
    l->e.on_ground = num(tag, "OnGround");
    l->air = num(tag, "Air");
    l->invulnerable = num(tag, "Invulnerable");
    l->time_until_portal = num(tag, "PortalCooldown");
    /* a load keeps the saved Dimension (a summoned mob's 0 wherever it is) */
    if (loaded) l->dimension = num(tag, "Dimension");
    l->health = fl(tag, "HealF");
    l->absorption = fl(tag, "AbsorptionAmount");
    l->hurt_time = num(tag, "HurtTime");
    l->death_time = num(tag, "DeathTime");
    l->attack_time = num(tag, "AttackTime");
    l->can_pick_up_loot = num(tag, "CanPickUpLoot");
    l->persistence_required = num(tag, "PersistenceRequired");
    {
        const nbt *cn = nbt_get(tag, "CustomName");
        const char *s = cn ? nbt_string_value(cn) : NULL;
        snprintf(l->custom_name, sizeof l->custom_name, "%s", s ? s : "");
    }
    leash_read_nbt(l, tag);
    l->growing_age = num(tag, "Age");
    l->in_love = num(tag, "InLove");
    l->profession = num(tag, "Profession");
    l->wealth = num(tag, "Riches");
    l->is_player_created = num(tag, "PlayerCreated");
    l->explosion_power = num(tag, "ExplosionPower");
    restore_attributes(l, tag);
    {
        /* EntityLivingBase.readEntityFromNBT's ActiveEffects block: the map
         * put only, the Attributes list above carries the modifiers. */
        const nbt *fx = nbt_get(tag, "ActiveEffects");
        for (int i = 0; i < len(fx); ++i)
        {
            const nbt *c = nbt_list_get(fx, i);
            struct potion_effect eff = {
                .id = (uint8_t)num(c, "Id"),
                .amplifier = (int8_t)num(c, "Amplifier"),
                .duration = num(c, "Duration"),
                .is_splash = 0,
                .is_ambient = (uint8_t)num(c, "Ambient"),
            };
            potion_map_put(&l->potions, &eff);
        }
        /* a load is a fresh entity: potionsNeedUpdate starts true, so the
         * first update sets the effect colour and its particle roll draws
         * the Random (a snapshot's live entity carries its own flag) */
        if (loaded) l->potions_need_update = 1;
    }
    if (l->growing_age != 0 && l->kind <= AK_SHEEP) living_set_growing_age(l, l->growing_age);
    if (l->kind == AK_SHEEP)
        l->data_watcher_16 = num(tag, "Color") | (num(tag, "Sheared") ? 16 : 0);
    if (l->kind == AK_PIG) l->data_watcher_16 = num(tag, "Saddle");
    if (l->kind == AK_BAT) l->data_watcher_16 = num(tag, "BatFlags");
    if (l->kind == AK_CHICKEN) l->is_chicken_jockey = num(tag, "IsChickenJockey");
    if (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN)
    {
        if (num(tag, "IsBaby")) zombie_set_child(l, 1);
        l->zombie_is_villager = num(tag, "IsVillager");
        /* EntityZombie.readEntityFromNBT's func_146070_a: the flag and
         * the EntityAIBreakDoor task it adds (priority 1, after the rest) */
        int cbd = num(tag, "CanBreakDoors") != 0;
        if (cbd != l->zombie_can_break_doors)
        {
            l->zombie_can_break_doors = cbd;
            if (cbd) ai_add_break_door(l);
            else ai_remove_task(l, AIC_BREAK_DOOR);
        }
        /* ConversionTime > -1 is a cure in progress: a load runs
         * startConversion (the flag, the weakness removal, the strength
         * effect over the one ActiveEffects just put, entity state 16 to no
         * tracker yet); a snapshot's live zombie already carries the effects */
        const nbt *ct = nbt_get(tag, "ConversionTime");
        l->zombie_conversion_time = num(tag, "ConversionTime");
        if (ct != NULL && l->zombie_conversion_time > -1)
        {
            if (loaded) zombie_start_conversion(l, l->zombie_conversion_time, NULL);
            else l->zombie_is_converting = 1;
        }
    }
    if (l->kind == HK_SKELETON) skeleton_set_type(l, num(tag, "SkeletonType"));
    if (l->kind == HK_CREEPER)
    {
        l->creeper_powered = num(tag, "powered");
        l->creeper_ignited = num(tag, "ignited");
        if (nbt_get(tag, "Fuse")) l->creeper_fuse_time = num(tag, "Fuse");
        if (nbt_get(tag, "ExplosionRadius")) l->creeper_explosion_radius = num(tag, "ExplosionRadius");
    }
    if (l->kind == HK_ENDERMAN)
    {
        l->enderman_carried_block = num(tag, "carried");
        l->enderman_carrying_data = num(tag, "carriedData");
    }
    /* EntitySlime.readEntityFromNBT: setSlimeSize(Size + 1), the box, the
     * max health base and a full heal, over the constructor's random size
     * (a chunk load; a snapshot's live slime keeps its recorded state) */
    if (IS_SLIME_KIND(l->kind))
    {
        if (loaded) slime_set_size(l, num(tag, "Size") + 1);
        else l->slime_size = num(tag, "Size") + 1;
    }
    if (l->kind == HK_PIGMAN) l->pigman_anger_level = num(tag, "Anger");
    const nbt *eq = nbt_get(tag, "Equipment");
    for (int i = 0; i < 5 && i < len(eq); ++i)
    {
        const nbt *item = nbt_list_get(eq, i);
        l->equip[i].id = num(item, "id");
        l->equip[i].count = num(item, "Count");
        l->equip[i].damage = num(item, "Damage");
        l->equip[i].tag = itag_from_item(item);
    }
    /* EntityLivingBase.onUpdate's equipment pass: previousEquipment starts
     * empty after a load, so the first update applies the held weapon's or
     * tool's attackDamage modifier before anything can read it */
    {
        int item = l->equip[0].count > 0 ? l->equip[0].id : 0;
        if (item > 0 && item < 4096 && (ITEMS[item].kind == ITEM_SWORD || ITEMS[item].kind == ITEM_TOOL))
        {
            struct attr_mod w_mod;
            memset(&w_mod, 0, sizeof w_mod);
            w_mod.uuid_msb = -3799650116634706120LL;
            w_mod.uuid_lsb = -6586616428615387697LL;
            attrs_remove(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
            w_mod.amount = (double)ITEMS[item].damage;
            w_mod.from_item = 1;
            attrs_apply(&l->attrs.a[ATTR_ATTACK_DAMAGE], &w_mod);
        }
    }
    const nbt *chances = nbt_get(tag, "DropChances");
    for (int i = 0; i < 5 && i < len(chances); ++i)
        l->equipment_drop_chances[i] = fval(nbt_list_get(chances, i));
    /* EntitySkeleton.readEntityFromNBT's setCombatTask runs after
     * EntityLiving's Equipment read: the held bow picks the arrow attack */
    if (l->kind == HK_SKELETON) skeleton_set_combat_task(l);
    if (l->kind == VK_VILLAGER)
    {
        const nbt *offers = nbt_get(tag, "Offers");
        trades_from_nbt(&lv_villager(l)->recipes, offers);
        int nn = lv_villager(l)->recipes.n;
        l->has_recipes = nn > 0;
    }
    /* Entity.readFromNBT's shouldSetPosAfterLoading: setPosition once more,
     * so a size readEntityFromNBT changed (a baby zombie) is centred. A live
     * entity's box is centred on its position too (setPosition, and
     * moveEntity derives the position from the box), so a snapshot's
     * entity gets the same re-centring. */
    entity_set_position(&l->e, l->e.pos_x, l->e.pos_y, l->e.pos_z);
}

static void restore_living(struct living *l, const struct snap_entity *se)
{
    l->entity_id = se->id;
    l->rand.r.seed = se->rand_state;
    l->rand.have_next_next_gaussian = se->has_gauss;
    l->rand.next_next_gaussian = se->has_gauss ? se->gauss : 0.0;
    l->dimension = se->dim;
    /* EntityLivingBase.entityAge and EntityLiving.livingSoundTime are
     * plain fields the NBT never persists; a snapshot that records them
     * sets them (the AI's age gate, EntityAIWander's age >= 100, and the
     * idle sound roll read them from the first tick) */
    if (se->has_age)
    {
        l->entity_age = se->age;
        l->living_sound_time = se->lsf;
    }
    restore_living_nbt(l, se->tag, 0);

    /* the navigator's active path: Java keeps walking it after the snapshot,
     * so the replay must too (EntityLiving.navigator's currentPath, index
     * and speed) */
    if (se->has_path && se->path_n > 0 && se->path_index < se->path_n)
    {
        pathref ph = path_alloc(se->path_n);
        struct path_ent *p = path_at(ph);
        p->length = se->path_n;
        p->n_points = se->path_n;
        p->index = se->path_index;
        for (int i = 0; i < se->path_n; ++i)
        {
            p->pts[i][0] = se->path_pts[i][0];
            p->pts[i][1] = se->path_pts[i][1];
            p->pts[i][2] = se->path_pts[i][2];
        }
        l->nav.path = ph;
        l->nav.speed = se->path_speed;
        l->nav.total_ticks = 0;
        l->nav.ticks_at_last_pos = 0;
    }

    /* a running watch-closest task: the look helper keeps driving the head
     * pitch from it, so its lookTime has to survive the snapshot */
    if (se->has_watch)
    {
        int wc = se->watch_is_player ? 0 : 2;
        for (int i = 0; i < lv_ai(l)->tasks.n; ++i)
        {
            struct ai_task *t = &lv_ai(l)->tasks.entries[i].t;
            if (t->cls != AIC_WATCH_CLOSEST || t->watch_class != wc) continue;
            t->look_time = se->watch_look_time;
            t->is_watching_player = se->watch_is_player;
            if (!se->watch_is_player) t->target = 0;
            if (lv_ai(l)->tasks.nexec < AI_MAX_ENTRIES)
                lv_ai(l)->tasks.executing[lv_ai(l)->tasks.nexec++] = i;
            break;
        }
    }
}

int sr_living_kind(const char *cls)
{
    return kind_for_class(cls);
}

/* EntityList.createEntityFromNBT for a chunk the provider loads from disk:
 * the class's constructor on the server's own streams (the entity id, the
 * Random, the UUID, EntityLivingBase's Math.random values, the kind's draws),
 * then readFromNBT over it. The entity joins the living list and the
 * spawner's count; the caller places it in the pass order. */
/* Entity.travelToDimension's new copy: EntityList.createEntityByName(name,
 * world) on the server's streams, then copyDataFrom's readFromNBT over the
 * old entity's tag. Nothing joins a list here; the caller adds it to the
 * destination world's pools. */
struct living *sr_living_copy_nbt(struct serverreplay *sr, int kind, const nbt *tag, struct world *w)
{
    det_state fake = SR_DET(sr);
    fake.splits = NULL;
    fake.seeder[DET_OTHER] = SR_DET(sr).seeder[DET_SERVER];
    fake.math[DET_OTHER] = SR_DET(sr).math[DET_SERVER];
    fake.next_id[DET_OTHER] = SR_DET(sr).next_id[DET_SERVER];
    struct living *l = living_alloc();
    if (!l) abort();
    living_init(l, w, kind, &fake);
    construct(l, &fake);
    SR_DET(sr).seeder[DET_SERVER] = fake.seeder[DET_OTHER];
    SR_DET(sr).math[DET_SERVER] = fake.math[DET_OTHER];
    SR_DET(sr).next_id[DET_SERVER] = fake.next_id[DET_OTHER];
    restore_living_nbt(l, tag, 1);
    l->dimension = w->dim;
    return l;
}

int sr_spawn_kind(int kind)
{
    return spawn_kind(kind);
}

struct living *sr_living_from_nbt(struct serverreplay *sr, int kind, const nbt *tag)
{
    det_state fake = SR_DET(sr);
    fake.splits = NULL;
    fake.seeder[DET_OTHER] = SR_DET(sr).seeder[DET_SERVER];
    fake.math[DET_OTHER] = SR_DET(sr).math[DET_SERVER];
    fake.next_id[DET_OTHER] = SR_DET(sr).next_id[DET_SERVER];
    struct living *l = living_alloc();
    if (!l) abort();
    /* the world of the dimension whose state is swapped in */
    living_init(l, sr->here == -1 ? &sr->hell.world : sr->here == 1 ? &sr->sky.world : &sr->pop.world,
                kind, &fake);
    l->an = &sr->d->anw;
    l->dimension = sr->d->anw.dimension;
    construct(l, &fake);
    SR_DET(sr).seeder[DET_SERVER] = fake.seeder[DET_OTHER];
    SR_DET(sr).math[DET_SERVER] = fake.math[DET_OTHER];
    SR_DET(sr).next_id[DET_SERVER] = fake.next_id[DET_OTHER];
    restore_living_nbt(l, tag, 1);
    an_add_living(&sr->d->anw, l, sr->d->anw.n);
    int sk = spawn_kind(kind);
    if (sk >= 0) spawner_register_living(&sr->d->spawner, sk, l->entity_id, &l->e);
    return l;
}

int sr_throwable_kind(const char *cls)
{
    static const struct { const char *snap, *list; int kind; } map[] = {
        {"EntitySnowball", "Snowball", IE_SNOWBALL}, {"EntityEgg", "ThrownEgg", IE_EGG},
        {"EntityEnderPearl", "ThrownEnderpearl", IE_ENDER_PEARL},
        {"EntityExpBottle", "ThrownExpBottle", IE_EXP_BOTTLE}, {"EntityPotion", "ThrownPotion", IE_POTION}
    };
    for (size_t i = 0; cls && i < sizeof map / sizeof map[0]; ++i)
        if (strcmp(cls, map[i].snap) == 0 || strcmp(cls, map[i].list) == 0) return map[i].kind;
    return 0;
}

/* EntityThrowable(World): Entity's constructor (the id, the Random and the
 * UUID the caller drew, in that order) and setSize(0.25, 0.25), then
 * Entity.readFromNBT (position, motion, rotation % 360, fall distance, fire,
 * onGround, UUID) and readEntityFromNBT: xTile, yTile, zTile, inTile (an
 * unregistered id reads as air), shake, inGround, ownerName (empty is none).
 * The thrower is looked up by name later (getThrower), the counters
 * (ticksInAir, ticksInGround) start at 0. The entity joins the living
 * world's lists; the caller files it in the pass order. */
ie_ent *sr_throwable_from_nbt(struct serverreplay *sr, int kind, const nbt *tag, int id, det_rng rnd)
{
    det_state junk;
    det_init(&junk);
    det_state *real = sr->d->anw.iew.det;
    sr->d->anw.iew.det = &junk;
    const nbt *potion = nbt_get(tag, "Potion");
    ie_ent *ie = proj_spawn_throwable(&sr->d->anw.iew, kind, atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2),
                                      0.0, 1.0, 0.0, 0.0F, 0.0F, potion ? num(potion, "Damage") : 0);
    sr->d->anw.iew.det = real;
    det_free(&junk);
    if (!ie) return NULL;
    if (potion && nbt_get(potion, "Count")) ie->potion_spent = 1 - num(potion, "Count");
    ie->entity_id = id;
    ie->rand = rnd;
    ie->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    ie->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    ie->e.motion_x = atd(tag, "Motion", 0);
    ie->e.motion_y = atd(tag, "Motion", 1);
    ie->e.motion_z = atd(tag, "Motion", 2);
    if (fabs(ie->e.motion_x) > 10.0) ie->e.motion_x = 0.0;
    if (fabs(ie->e.motion_y) > 10.0) ie->e.motion_y = 0.0;
    if (fabs(ie->e.motion_z) > 10.0) ie->e.motion_z = 0.0;
    ie->prev_x = ie->last_tick_x = ie->e.pos_x;
    ie->prev_y = ie->last_tick_y = ie->e.pos_y;
    ie->prev_z = ie->last_tick_z = ie->e.pos_z;
    ie->rotation_yaw = ie->prev_yaw = fmodf(atf(tag, "Rotation", 0), 360.0F);
    ie->rotation_pitch = ie->prev_pitch = fmodf(atf(tag, "Rotation", 1), 360.0F);
    ie->e.fall_distance = fl(tag, "FallDistance");
    ie->e.fire = num(tag, "Fire");
    ie->e.on_ground = num(tag, "OnGround");
    ie->time_until_portal = num(tag, "PortalCooldown");
    ie->tile_x = (short)num(tag, "xTile");
    ie->tile_y = (short)num(tag, "yTile");
    ie->tile_z = (short)num(tag, "zTile");
    ie->in_tile = num(tag, "inTile") & 255;
    if (!BLOCKS[ie->in_tile].exists) ie->in_tile = 0;
    ie->shake = num(tag, "shake") & 255;
    ie->in_ground = num(tag, "inGround") == 1;
    const nbt *owner = nbt_get(tag, "ownerName");
    const char *name = owner ? nbt_string_value(owner) : NULL;
    if (name && name[0])
    {
        char *copy = malloc(strlen(name) + 1);
        if (!copy) abort();
        memcpy(copy, name, strlen(name) + 1);
        ie->owner_name = copy;
    }
    else ie->owner_name = NULL;
    ie->shooter = ie->shooting_entity = 0;
    ie->shooter_is_player = 0;
    ie->ticks_in_air = ie->ticks_in_ground = 0;
    ie->owner_pending = ie->owner_name != NULL;
    an_adopt_projectile(&sr->d->anw, ie);
    return ie;
}

/* EntityEnderEye(World) (proj_load_ender_eye), then Entity.readFromNBT:
 * the position, the motion (past 10 reads as 0), the rotation (% 360), fall
 * distance, fire, onGround, the portal cooldown, the UUID; the eye reads
 * nothing of its own. The caller sets the Random its constructor drew. The
 * eye joins the living world's lists like a thrown one. */
ie_ent *sr_eye_from_nbt(struct serverreplay *sr, const nbt *tag, int id)
{
    det_state junk;
    det_init(&junk);
    det_state *real = sr->d->anw.iew.det;
    sr->d->anw.iew.det = &junk;
    ie_ent *ie = proj_load_ender_eye(&sr->d->anw.iew, atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2));
    sr->d->anw.iew.det = real;
    det_free(&junk);
    if (!ie) return NULL;
    ie->entity_id = id;
    ie->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    ie->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    ie->e.motion_x = atd(tag, "Motion", 0);
    ie->e.motion_y = atd(tag, "Motion", 1);
    ie->e.motion_z = atd(tag, "Motion", 2);
    if (fabs(ie->e.motion_x) > 10.0) ie->e.motion_x = 0.0;
    if (fabs(ie->e.motion_y) > 10.0) ie->e.motion_y = 0.0;
    if (fabs(ie->e.motion_z) > 10.0) ie->e.motion_z = 0.0;
    ie->prev_x = ie->last_tick_x = ie->e.pos_x;
    ie->prev_y = ie->last_tick_y = ie->e.pos_y;
    ie->prev_z = ie->last_tick_z = ie->e.pos_z;
    ie->rotation_yaw = ie->prev_yaw = fmodf(atf(tag, "Rotation", 0), 360.0F);
    ie->rotation_pitch = ie->prev_pitch = fmodf(atf(tag, "Rotation", 1), 360.0F);
    ie->e.fall_distance = fl(tag, "FallDistance");
    ie->e.fire = num(tag, "Fire");
    ie->e.on_ground = num(tag, "OnGround");
    ie->time_until_portal = num(tag, "PortalCooldown");
    an_adopt_projectile(&sr->d->anw, ie);
    return ie;
}

/* EntityFireball(World) (setSize, its box), then Entity.readFromNBT and
 * EntityFireball.readEntityFromNBT: the tile, inTile, inGround, the motion
 * from "direction", ExplosionPower for the large one. The acceleration, the
 * shooter and the counters are not saved: a loaded fireball has none (a
 * snapshot's runtime block brings them back). The caller drew the
 * constructor's id, Random and UUID. */
ie_ent *sr_fireball_from_nbt(struct serverreplay *sr, int large, const nbt *tag, int id, det_rng rnd)
{
    det_state junk;
    det_init(&junk);
    det_state *real = sr->d->anw.iew.det;
    sr->d->anw.iew.det = &junk;
    ie_ent *ie = proj_spawn_fireball(&sr->d->anw.iew, large ? IE_LARGE_FIREBALL : IE_SMALL_FIREBALL,
                                     atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2), 1.0, 0.0, 0.0);
    sr->d->anw.iew.det = real;
    det_free(&junk);
    if (!ie) return NULL;
    ie->entity_id = id;
    ie->rand = rnd;
    ie->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    ie->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    ie->accel_x = ie->accel_y = ie->accel_z = 0.0;
    ie->e.motion_x = atd(tag, "direction", 0);
    ie->e.motion_y = atd(tag, "direction", 1);
    ie->e.motion_z = atd(tag, "direction", 2);
    ie->rotation_yaw = ie->prev_yaw = fmodf(atf(tag, "Rotation", 0), 360.0F);
    ie->rotation_pitch = ie->prev_pitch = atf(tag, "Rotation", 1);
    ie->e.fall_distance = fl(tag, "FallDistance");
    ie->e.fire = num(tag, "Fire");
    ie->e.on_ground = num(tag, "OnGround");
    ie->time_until_portal = num(tag, "PortalCooldown");
    ie->tile_x = num(tag, "xTile");
    ie->tile_y = num(tag, "yTile");
    ie->tile_z = num(tag, "zTile");
    ie->in_tile = num(tag, "inTile") & 255;
    if (!BLOCKS[ie->in_tile].exists) ie->in_tile = 0;   /* getBlockById: air for an unused id */
    ie->in_ground = num(tag, "inGround") == 1;
    if (large && nbt_get(tag, "ExplosionPower")) ie->explosion_power = num(tag, "ExplosionPower");
    ie->shooter = ie->shooting_entity = 0;
    ie_added_to_world(&sr->d->anw.iew, ie);
    ie->dimension = sr->d->anw.dimension;
    ie->spawn_index = sr->d->anw.n;
    struct an_ent *en = an_ent_alloc();
    if (!en) abort();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = sr->d->anw.n;
    en->ieh = ie_ref(ie);
    an_list_push(&sr->d->anw, en);
    an_chunk_add(&sr->d->anw, en, (int)floor(ie->e.pos_x / 16.0), (int)floor(ie->e.pos_y / 16.0),
                 (int)floor(ie->e.pos_z / 16.0));
    return ie;
}


/* EntityEnderEye(World) (EntityList.createEntityFromNBT's "EyeOfEnderSignal"):
 * Entity's constructor (the caller drew the id, the Random and the UUID) and
 * setSize(0.25, 0.25), then Entity.readFromNBT: the position, the motion (past
 * 10 reads as 0), the rotation (setRotation keeps it modulo 360, the previous
 * rotation as read), the fall distance, fire, onGround and the portal
 * cooldown. The eye's own readEntityFromNBT reads nothing, so targetX/Y/Z,
 * despawnTimer and shatterOrDrop keep the constructor's zeros: a reloaded
 * eye heads for (0, 0), falls toward y 0 and shatters 81 updates later. */
ie_ent *sr_ender_eye_from_nbt(struct serverreplay *sr, const nbt *tag, int id, det_rng rnd)
{
    det_state junk;
    det_init(&junk);
    det_state *real = sr->d->anw.iew.det;
    sr->d->anw.iew.det = &junk;
    ie_ent *ie = proj_spawn_ender_eye(&sr->d->anw.iew, atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2),
                                      atd(tag, "Pos", 0), 0, atd(tag, "Pos", 2));
    sr->d->anw.iew.det = real;
    det_free(&junk);
    if (!ie) return NULL;
    ie->entity_id = id;
    ie->rand = rnd;
    ie->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    ie->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    ie->e.motion_x = atd(tag, "Motion", 0);
    ie->e.motion_y = atd(tag, "Motion", 1);
    ie->e.motion_z = atd(tag, "Motion", 2);
    if (fabs(ie->e.motion_x) > 10.0) ie->e.motion_x = 0.0;
    if (fabs(ie->e.motion_y) > 10.0) ie->e.motion_y = 0.0;
    if (fabs(ie->e.motion_z) > 10.0) ie->e.motion_z = 0.0;
    ie->prev_x = ie->last_tick_x = ie->e.pos_x;
    ie->prev_y = ie->last_tick_y = ie->e.pos_y;
    ie->prev_z = ie->last_tick_z = ie->e.pos_z;
    ie->prev_yaw = atf(tag, "Rotation", 0);
    ie->prev_pitch = atf(tag, "Rotation", 1);
    ie->rotation_yaw = fmodf(ie->prev_yaw, 360.0F);
    ie->rotation_pitch = fmodf(ie->prev_pitch, 360.0F);
    ie->e.fall_distance = fl(tag, "FallDistance");
    ie->e.fire = num(tag, "Fire");
    ie->e.on_ground = num(tag, "OnGround");
    ie->time_until_portal = num(tag, "PortalCooldown");
    ie->eye_target_x = ie->eye_target_y = ie->eye_target_z = 0.0;
    ie->eye_timer = 0;
    ie->eye_drop = 0;
    an_adopt_projectile(&sr->d->anw, ie);
    return ie;
}

/* EntityThrowable.getThrower: the player of the thrower's name, once it is in
 * the throwable's world (the replay has one player, named Player). */
void sr_throwable_resolve(struct serverreplay *sr, ie_ent *ie)
{
    if (ie->shooter != 0 || ie->owner_name == NULL || sr->player_livh == 0 || sr->player == NULL ||
        sr->player->dimension != sr->here || strcmp(ie->owner_name, "Player") != 0)
        return;
    ie->shooter = ie->shooting_entity = lv_ref(lv_get(sr->player_livh));
    ie->shooter_is_player = 1;
    ie->shooter_life = sr->player->life;
    ie->owner_pending = 0;
}

int sr_load_snapshot_entities(struct serverreplay *sr, const struct snapshot *snap)
{
    det_state fake;
    det_init(&fake);
    /* The entities are constructed into the current dimension's pools; the
     * snapshot lists them in worldServers order, so the pass enters each
     * dimension once, at its group's head. An unsupported kind is refused by
     * name. */
    int last_dim = -999;
    struct world *dim_world = &sr->pop.world;
    int heal = -1;              /* the dragon's healingEnderCrystal, by id */
    for (int i = 0; i < snap->nents; ++i)
    {
        const struct snap_entity *se = &snap->ents[i];
        const nbt *tag = se->tag;
        if (se->dim != last_dim)
        {
            serverreplay_enter(sr, se->dim);
            last_dim = se->dim;
            dim_world = se->dim == -1 ? &sr->hell.world : se->dim == 1 ? &sr->sky.world : &sr->pop.world;
        }
        /* the player's entry, bound to the server player later */
        if (se->player) { serverreplay_track_entity(sr, 3, NULL); continue; }
        if (strcmp(se->cls, "EntityDragon") == 0)
        {
            if (se->dim != 1 || se->dragon == NULL)
            {
                snprintf(sr->err, sizeof sr->err, "EntityDragon needs the End and its dragon state (a newer snapshot)");
                det_free(&fake);
                return 0;
            }
            endfight_snapshot_dragon(sr->end, dim_world, &SR_DET(sr), se);
            serverreplay_end_listed(sr, -1);
            heal = se->dragon->heal;
            continue;
        }
        if (strcmp(se->cls, "EntityEnderCrystal") == 0)
        {
            int ci = se->dim == 1 ? endfight_snapshot_crystal(sr->end, se) : -1;
            if (ci < 0) { snprintf(sr->err, sizeof sr->err, "ender crystal outside the End or pool full"); det_free(&fake); return 0; }
            serverreplay_end_listed(sr, ci);
            continue;
        }
        int kind = kind_for_class(se->cls);
        if (kind >= 0)
        {
            struct living *l = living_alloc();
            if (!l) abort();
            living_init(l, dim_world, kind, &fake);
            l->an = &sr->d->anw;
            /* the constructor's draws on the entity's own Random set fields
             * no NBT carries (the squid's rotationVelocity, the chicken's egg
             * timer): replay them from the state it was born with */
            if (se->has_born)
            {
                l->rand.r.seed = se->rand_born;
                l->rand.have_next_next_gaussian = 0;
            }
            construct(l, &fake);
            restore_living(l, se);
            /* the village it last looked up and the countdown to the next
             * lookup (EntityVillager.updateAITick, EntityIronGolem's) */
            if (se->has_vil)
            {
                l->village = vc_village_ref(l->an->village_collection, se->vil[0] >= 0 && se->vil[0] < sr->vc.num_villages ? vc_list_at(&sr->vc, se->vil[0]) : NULL);
                if (kind == VK_VILLAGER)
                {
                    l->random_tick_divider = se->vil[1];
                    l->is_looking_for_home = se->vil[2];
                }
                else l->home_check_timer = se->vil[1];
            }
            an_add_living(&sr->d->anw, l, sr->d->anw.n);
            serverreplay_track_entity(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
            int sk = spawn_kind(kind);
            if (sk >= 0) spawner_register_living(&sr->d->spawner, sk, l->entity_id, &l->e);
            continue;
        }
        if (strcmp(se->cls, "EntityItem") == 0 || strcmp(se->cls, "EntityXPOrb") == 0)
        {
            int orb = strcmp(se->cls, "EntityXPOrb") == 0;
            const nbt *item = nbt_get(tag, "Item");
            ie_ent *en = orb
                ? ie_adopt_orb(&sr->d->iew, se->id, nbt_int_value(nbt_get(tag, "UUIDMost")),
                    nbt_int_value(nbt_get(tag, "UUIDLeast")), se->rand_state,
                    atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2),
                    atd(tag, "Motion", 0), atd(tag, "Motion", 1), atd(tag, "Motion", 2),
                    atf(tag, "Rotation", 0), num(tag, "Value"))
                : ie_adopt_item(&sr->d->iew, se->id, nbt_int_value(nbt_get(tag, "UUIDMost")),
                    nbt_int_value(nbt_get(tag, "UUIDLeast")), se->rand_state,
                    atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2),
                    atd(tag, "Motion", 0), atd(tag, "Motion", 1), atd(tag, "Motion", 2),
                    atf(tag, "Rotation", 0), 0.0F,
                    num(item, "id"), num(item, "Damage"), num(item, "Count"), itag_from_item(item));
            if (!en) { snprintf(sr->err, sizeof sr->err, "item pool full"); det_free(&fake); return 0; }
            if (orb && se->width == 0.25F) ie_orb_nbt_size(en);
            en->age = num(tag, "Age");
            en->health = num(tag, "Health");
            en->e.fire = num(tag, "Fire");
            en->e.fall_distance = fl(tag, "FallDistance");
            en->e.on_ground = num(tag, "OnGround");
            en->time_until_portal = num(tag, "PortalCooldown");
            if (!orb)
            {
                const nbt *thrower = nbt_get(tag, "Thrower");
                en->thrower = thrower ? nbt_string_value(thrower) : NULL;
                const nbt *owner = nbt_get(tag, "Owner");
                en->owner_name = owner ? nbt_string_value(owner) : NULL;
            }
            ie_added_to_world(&sr->d->iew, en);
            serverreplay_track_entity(sr, 0, en);
            continue;
        }
        if (strcmp(se->cls, "EntityTNTPrimed") == 0)
        {
            /* EntityTNTPrimed(World) then readEntityFromNBT's Fuse; the
             * igniter (tntPlacedBy, not saved) is the runtime's: the player
             * or no one ("e:-1"), the only igniters here */
            const char *by = se->rt ? json_str(json_get(json_get(se->rt, "f"), "tntPlacedBy")) : NULL;
            if (by && !strncmp(by, "str:", 4)) by += 4;
            ie_ent *en = ie_adopt_tnt(&sr->d->iew, se->id, nbt_int_value(nbt_get(tag, "UUIDMost")),
                                      nbt_int_value(nbt_get(tag, "UUIDLeast")), se->rand_state,
                                      atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2),
                                      atd(tag, "Motion", 0), atd(tag, "Motion", 1), atd(tag, "Motion", 2),
                                      num(tag, "Fuse"), by != NULL && !strncmp(by, "e:", 2) && strtol(by + 2, NULL, 10) >= 0);
            if (!en) { snprintf(sr->err, sizeof sr->err, "item pool full"); det_free(&fake); return 0; }
            en->e.fire = num(tag, "Fire");
            en->e.fall_distance = fl(tag, "FallDistance");
            en->e.on_ground = num(tag, "OnGround");
            en->time_until_portal = num(tag, "PortalCooldown");
            ie_added_to_world(&sr->d->iew, en);
            serverreplay_track_entity(sr, 0, en);
            continue;
        }
        if (strcmp(se->cls, "EntityFallingBlock") == 0)
        {
            det_state *real = sr->d->fhw.det;
            sr->d->fhw.det = &fake;
            fh_ent *en = fh_spawn_falling_nbt(&sr->d->fhw, tag);
            sr->d->fhw.det = real;
            if (!en) { snprintf(sr->err, sizeof sr->err, "falling pool full"); det_free(&fake); return 0; }
            en->entity_id = se->id;
            en->rand.r.seed = se->rand_state;
            en->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
            en->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
            /* a block that fell this session was made by the positioned
             * constructor: preventEntitySpawning, setSize(0.98, 0.98) and
             * yOffset half the height, its box centred there by setPosition */
            if (se->width == 0.98F)
            {
                en->e.prevent_entity_spawning = 1;
                entity_set_size(&en->e, 0.98F, 0.98F);
                en->e.y_offset = en->e.height / 2.0F;
                entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
            }
            fh_added_to_world(&sr->d->fhw, en);
            serverreplay_track_entity(sr, 1, en);
            continue;
        }
        if (strcmp(se->cls, "EntityPainting") == 0 || strcmp(se->cls, "EntityItemFrame") == 0)
        {
            det_state *real = sr->d->fhw.det;
            sr->d->fhw.det = &fake;
            fh_ent *en = fh_spawn_hanging_nbt(&sr->d->fhw, se->cls[6] == 'P' ? FH_PAINTING : FH_FRAME, tag);
            sr->d->fhw.det = real;
            if (!en) { snprintf(sr->err, sizeof sr->err, "hanging pool full"); det_free(&fake); return 0; }
            en->entity_id = se->id;
            en->rand.r.seed = se->rand_state;
            en->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
            en->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
            fh_added_to_world(&sr->d->fhw, en);
            serverreplay_track_entity(sr, 1, en);
            continue;
        }
        if (strcmp(se->cls, "EntityLeashKnot") == 0)
        {
            /* a knot is never saved (writeToNBTOptional is false), so only a
             * mid-run snapshot holds one: EntityLeashKnot(World, x, y, z) at
             * its fence, the tile its position is centred on */
            det_state *real = sr->d->fhw.det;
            sr->d->fhw.det = &fake;
            fh_ent *en = fh_spawn_knot(&sr->d->fhw, (int)floor(atd(tag, "Pos", 0)), (int)floor(atd(tag, "Pos", 1)),
                                       (int)floor(atd(tag, "Pos", 2)));
            sr->d->fhw.det = real;
            if (!en) { snprintf(sr->err, sizeof sr->err, "hanging pool full"); det_free(&fake); return 0; }
            en->entity_id = se->id;
            en->rand.r.seed = se->rand_state;
            en->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
            en->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
            fh_added_to_world(&sr->d->fhw, en);
            serverreplay_track_entity(sr, 1, en);
            continue;
        }
        int tkind = sr_throwable_kind(se->cls);
        if (tkind)
        {
            det_rng rnd;
            memset(&rnd, 0, sizeof rnd);
            rnd.r.seed = se->rand_state;
            rnd.have_next_next_gaussian = se->has_gauss;
            rnd.next_next_gaussian = se->has_gauss ? se->gauss : 0.0;
            ie_ent *ie = sr_throwable_from_nbt(sr, tkind, tag, se->id, rnd);
            if (!ie) { snprintf(sr->err, sizeof sr->err, "throwable pool full"); det_free(&fake); return 0; }
            /* a snapshot taken mid-flight: the counters and the resolved
             * thrower Snapshot.java recorded */
            if (se->has_throwable)
            {
                ie->ticks_in_air = se->ticks_in_air;
                ie->ticks_in_ground = se->ticks_in_ground;
                ie->ticks_existed = se->ticks_existed;
            }
            serverreplay_track_entity(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
            continue;
        }
        if (strcmp(se->cls, "EntityEnderEye") == 0)
        {
            /* an eye in flight (a start joined from a save that held one):
             * the snapshot's runtime block (sr_apply_runtime) brings back its
             * target, timer and drop flag */
            ie_ent *ie = sr_eye_from_nbt(sr, tag, se->id);
            if (!ie) { snprintf(sr->err, sizeof sr->err, "eye pool full"); det_free(&fake); return 0; }
            ie->rand.r.seed = se->rand_state;
            ie->rand.have_next_next_gaussian = se->has_gauss;
            ie->rand.next_next_gaussian = se->has_gauss ? se->gauss : 0.0;
            serverreplay_track_entity(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
            continue;
        }
        if (strcmp(se->cls, "EntitySmallFireball") == 0 || strcmp(se->cls, "EntityLargeFireball") == 0)
        {
            det_rng rnd;
            memset(&rnd, 0, sizeof rnd);
            rnd.r.seed = se->rand_state;
            rnd.have_next_next_gaussian = se->has_gauss;
            rnd.next_next_gaussian = se->has_gauss ? se->gauss : 0.0;
            ie_ent *ie = sr_fireball_from_nbt(sr, se->cls[6] == 'L', tag, se->id, rnd);
            if (!ie) { snprintf(sr->err, sizeof sr->err, "fireball pool full"); det_free(&fake); return 0; }
            serverreplay_track_entity(sr, 2, an_ent_at(sr->d->anw.slot[sr->d->anw.n - 1]));
            continue;
        }
        if (strcmp(se->cls, "EntityArrow") == 0)
        {
            /* EntityArrow(World), then Entity.readFromNBT and the arrow's own
             * readEntityFromNBT; its shooter, crit flag and knockback are not
             * saved, so a loaded arrow has none */
            det_state *real = sr->d->anw.iew.det;
            sr->d->anw.iew.det = &fake;
            ie_ent *ie = proj_spawn_arrow(&sr->d->anw.iew, atd(tag, "Pos", 0), atd(tag, "Pos", 1), atd(tag, "Pos", 2),
                                          0.0, 1.0, 0.0, 0.0F, 0.0F);
            sr->d->anw.iew.det = real;
            if (!ie) { snprintf(sr->err, sizeof sr->err, "arrow pool full"); det_free(&fake); return 0; }
            ie->entity_id = se->id;
            ie->rand.r.seed = se->rand_state;
            ie->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
            ie->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
            ie->e.motion_x = atd(tag, "Motion", 0);
            ie->e.motion_y = atd(tag, "Motion", 1);
            ie->e.motion_z = atd(tag, "Motion", 2);
            ie->rotation_yaw = ie->prev_yaw = fmodf(atf(tag, "Rotation", 0), 360.0F);
            ie->rotation_pitch = ie->prev_pitch = atf(tag, "Rotation", 1);
            ie->e.fall_distance = fl(tag, "FallDistance");
            ie->e.fire = num(tag, "Fire");
            ie->e.on_ground = num(tag, "OnGround");
            ie->tile_x = num(tag, "xTile");
            ie->tile_y = num(tag, "yTile");
            ie->tile_z = num(tag, "zTile");
            ie->ticks_in_ground = num(tag, "life");
            ie->in_tile = num(tag, "inTile") & 255;
            ie->in_data = num(tag, "inData") & 255;
            ie->shake = num(tag, "shake") & 255;
            ie->in_ground = num(tag, "inGround") == 1;
            if (nbt_get(tag, "damage")) ie->arrow_damage = dbl(nbt_get(tag, "damage"));
            if (nbt_get(tag, "pickup")) ie->can_be_picked_up = num(tag, "pickup");
            ie->shooting_entity = 0;
            ie_added_to_world(&sr->d->anw.iew, ie);
            ie->spawn_index = sr->d->anw.n;
            struct an_ent *en = an_ent_alloc();
            if (!en) abort();
            en->used = 1;
            en->is_living = 0;
            en->spawn_index = sr->d->anw.n;
            en->ieh = ie_ref(ie);
            an_list_push(&sr->d->anw, en);
            an_chunk_add(&sr->d->anw, en, (int)floor(ie->e.pos_x / 16.0), (int)floor(ie->e.pos_y / 16.0),
                         (int)floor(ie->e.pos_z / 16.0));
            serverreplay_track_entity(sr, 2, en);
            continue;
        }
        snprintf(sr->err, sizeof sr->err, "unsupported snapshot entity %s", se->cls);
        det_free(&fake);
        return 0;
    }
    /* a rider's NBT carries its vehicle ("Riding", Entity.writeMountToNBT)
     * while both are in the loaded list as entries of their own: link the
     * rider to the living whose UUID the Riding compound names (a spider
     * jockey loaded from a region file); riders and vehicles share a world,
     * so each world's own list */
    for (int w = 0; w < sr->nworlds; ++w)
    {
        serverreplay_enter(sr, sr->w[w].dim);
        for (int i = 0; i < sr->d->anw.n; ++i)
        {
            struct an_ent *en = an_ent_at(sr->d->anw.slot[i]);
            if (!en->is_living || lv_get(lv_get(en->livh)->riding_entity) != NULL) continue;
            const struct snap_entity *se = NULL;
            for (int k = 0; k < snap->nents && !se; ++k)
                if (snap->ents[k].id == lv_get(en->livh)->entity_id) se = &snap->ents[k];
            const nbt *ride = se ? nbt_get(se->tag, "Riding") : NULL;
            if (!ride) continue;
            int64_t msb = nbt_int_value(nbt_get(ride, "UUIDMost")), lsb = nbt_int_value(nbt_get(ride, "UUIDLeast"));
            for (int k = 0; k < sr->d->anw.n; ++k)
            {
                struct an_ent *v = an_ent_at(sr->d->anw.slot[k]);
                if (v == en || !v->is_living || lv_get(v->livh)->uuid_msb != msb || lv_get(v->livh)->uuid_lsb != lsb) continue;
                lv_get(en->livh)->riding_entity = lv_ref(lv_get(v->livh));
                lv_get(v->livh)->ridden_by_entity = lv_ref(lv_get(en->livh));
                break;
            }
        }
    }
    /* the ghosts: each built like a listed living (the constructor on
     * throwaway streams, its id, Random and NBT), in its world but in no
     * list, chunk or count, as sr_cl_event leaves an unloaded chunk's
     * living for whoever still holds it; the runtime below resolves ids to
     * them and gives them their own runtime state */
    sr->nghosts = 0;
    for (int i = 0; i < snap->nghosts; ++i)
    {
        const struct snap_entity *se = &snap->ghosts[i];
        int kind = kind_for_class(se->cls);
        if (kind < 0 || sr->nghosts >= (int)(sizeof sr->ghosts / sizeof sr->ghosts[0])) continue;
        serverreplay_enter(sr, se->dim);
        struct living *l = living_alloc();
        if (!l) abort();
        living_init(l, se->dim == -1 ? &sr->hell.world : se->dim == 1 ? &sr->sky.world : &sr->pop.world, kind, &fake);
        l->an = &sr->d->anw;
        if (se->has_born)
        {
            l->rand.r.seed = se->rand_born;
            l->rand.have_next_next_gaussian = 0;
        }
        construct(l, &fake);
        restore_living(l, se);
        sr->ghosts[sr->nghosts++] = lv_ref(l);
    }
    if (heal >= 0) endfight_resolve_heal(sr->end, heal);
    serverreplay_resolve_village_agressors(sr);
    /* the runtime fields, the data watcher and the AI a mid-run snapshot
     * carries (Snapshot.java's "rt"), references resolved over the whole list */
    if (!sr_apply_runtime(sr, snap)) { det_free(&fake); return 0; }
    serverreplay_enter(sr, 0);
    sr->native_entities = sr->d->iew.n + sr->d->fhw.n + sr->d->anw.n;
    det_free(&fake);
    return 1;
}
