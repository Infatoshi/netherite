/* EntityLivingBase, EntityLiving, EntityCreature, EntityAgeable and EntityAnimal
 * (the living entity framework the farm animals run on), ported from
 * oracle/src/entity/EntityLivingBase.java, EntityLiving.java, EntityCreature.java,
 * EntityAgeable.java and passive/EntityAnimal.java, plus the fields of Entity the
 * AI and the living tick reach.
 *
 * The entity record embeds entity.h's struct entity as its first member: moveEntity
 * and the collision code drive it, and every virtual they reach is a callback over
 * e.self = the living entity.
 *
 * Attributes: net.minecraft.entity.ai.attributes. The map is the registration
 * order (BaseAttributeMap.attributesByName is a LinkedHashMap, so
 * getAllAttributes() walks it in insertion order: maxHealth, knockbackResistance,
 * movementSpeed, then followRange from EntityLiving), the value computation is
 * ModifiableAttributeInstance.computeValue (operation 0 adds, 1 adds base * amount,
 * 2 multiplies by 1 + amount, then the attribute's clamp), and the modifiers keep
 * the order the AttributeModifier NBT list needs (func_111122_c walks the three
 * operation sets in order 0, 1, 2).
 *
 * Every random draw in these paths comes from the entity's own det_rng (the
 * per-entity Random Det.newRandom made), the world's rand, or Det's role streams:
 * the probe's entities live in the OTHER role. */
#ifndef NETHERITE_LIVING_H
#define NETHERITE_LIVING_H

#include "itemtag.h"
#include "arena.h"
#include <stdint.h>

#include "aabb.h"
#include "det.h"
#include "entity.h"
#include "item_entity.h"
#include "trades.h"
#include "potion.h"
#include "enchant.h"
#include "envstack.h"

struct world;
struct nbt;
struct village;
struct village_collection;
struct village_siege;
struct server_player;

enum { AK_PIG = 0, AK_COW = 1, AK_MOOSHROOM = 2, AK_CHICKEN = 3, AK_SHEEP = 4, AK_KINDS = 5, VK_VILLAGER = 5, VK_IRON_GOLEM = 6, SK_SLIME = 7, SK_MAGMA_CUBE = 8, SK_PLAYER = 9, HK_ZOMBIE = 10, HK_SKELETON = 11, HK_CREEPER = 12, HK_SPIDER = 13, HK_PLAYER = 14, GK_GHAST = 15, AK_SQUID = 16, AK_BAT = 17, HK_ENDERMAN = 18, HK_WITCH = 19, HK_SILVERFISH = 20, HK_PIGMAN = 21, HK_BLAZE = 22, HK_CAVE_SPIDER = 23, HK_KINDS = 24 };

#define IS_SLIME_KIND(k) ((k) == SK_SLIME || (k) == SK_MAGMA_CUBE)

/* The DamageSource families the tick and the AI can reach. DMG_MOB is
 * causeMobDamage and causePlayerDamage (an EntityDamageSource over the
 * attacker); DMG_ARROW, DMG_THROWN, DMG_FIREBALL and DMG_INDIRECT_MAGIC are the
 * EntityDamageSourceIndirect kinds (causeArrowDamage, causeThrownDamage,
 * causeFireballDamage, causeIndirectMagicDamage), whose getEntity() is the
 * shooter or thrower the callers pass as the attacker; DMG_THORNS is
 * causeThornsDamage (an EntityDamageSource over the armour's wearer, magic). */
enum {
    DMG_GENERIC = 0, DMG_IN_FIRE, DMG_ON_FIRE, DMG_CACTUS, DMG_FALL, DMG_DROWN, DMG_LAVA,
    DMG_IN_WALL, DMG_OUT_OF_WORLD, DMG_MAGIC, DMG_WITHER, DMG_STARVE, DMG_MOB,
    DMG_FIREBALL, DMG_EXPLOSION, DMG_THROWN, DMG_ARROW, DMG_THORNS, DMG_INDIRECT_MAGIC,
    /* DamageSource.anvil: a landing anvil's hit (blockable, no entity);
     * DamageSource.fallingBlock never lands, only the anvil hurts */
    DMG_ANVIL
};

/* instanceof EntityDamageSourceIndirect. */
static inline int dmg_is_indirect(int source)
{
    return source == DMG_ARROW || source == DMG_THROWN || source == DMG_FIREBALL ||
           source == DMG_INDIRECT_MAGIC;
}

/* DamageSource.isProjectile. */
static inline int dmg_is_projectile(int source)
{
    return source == DMG_ARROW || source == DMG_THROWN || source == DMG_FIREBALL;
}

/* DamageSource.isMagicDamage. */
static inline int dmg_is_magic(int source)
{
    return source == DMG_MAGIC || source == DMG_INDIRECT_MAGIC || source == DMG_THORNS;
}

/* The five attributes SharedMonsterAttributes registers, in registration order. */
enum { ATTR_MAX_HEALTH = 0, ATTR_KNOCKBACK_RESISTANCE = 1, ATTR_MOVEMENT_SPEED = 2, ATTR_FOLLOW_RANGE = 3, ATTR_ATTACK_DAMAGE = 4, ATTR_SPAWN_REINFORCEMENTS = 5, ATTR_COUNT = 6, ATTR_MAX_MODS = 8 };

/* AttributeModifier.name as an id (attr_name_id, attr_name): the names the
 * engine gives its modifiers are a table made before main (ids below
 * ATTR_NAMES_FIXED), any other (a potion's with its amplifier, one a
 * snapshot's NBT carries) the environment's own table (env.h attr_names). A
 * modifier is 32 bytes instead of 80 (lane/entmem). */
enum {
    MODN_NONE = 0, MODN_RANDOM_SPAWN_BONUS, MODN_RANDOM_ZOMBIE_SPAWN_BONUS, MODN_LEADER_ZOMBIE_BONUS,
    MODN_ZOMBIE_CALLER_CHARGE, MODN_ZOMBIE_CALLEE_CHARGE, MODN_FLEEING_SPEED_BONUS, MODN_ATTACKING_SPEED_BOOST,
    MODN_DRINKING_SPEED_PENALTY, MODN_BABY_SPEED_BOOST, MODN_WEAPON_MODIFIER, MODN_TOOL_MODIFIER, ATTR_NAMES_FIXED
};
/* the environment's names past the fixed ones, of at most ATTR_NAME_LEN - 1 bytes */
#define ATTR_NAMES_ENV 96
#define ATTR_NAME_LEN 40
uint16_t attr_name_id(const char *name);
const char *attr_name(uint16_t id);

struct attr_mod {
    double amount;
    int64_t uuid_msb, uuid_lsb;
    uint16_t name;
    uint8_t operation;
    uint8_t saved;
    /* A modifier the held item contributes ("Weapon modifier", "Tool
     * modifier"): EntityLivingBase.writeEntityToNBT removes the active items'
     * modifiers before writing the Attributes tag and re-applies them after, so
     * one of these never reaches the NBT (not in the list, and not counted when
     * the tag decides whether to write "Modifiers" at all). */
    uint8_t from_item;
};

struct attr_instance {
    int which;
    int registered;
    double base;
    double cached;
    int needs_update;
    int nmods;
    struct attr_mod mods[ATTR_MAX_MODS];
};

struct attr_map {
    struct attr_instance a[ATTR_COUNT];
};

/* EntityAITasks: the entry list and the executing list, with the tick rate. */
enum {
    AIC_SWIMMING = 0, AIC_PANIC, AIC_CONTROLLED_BY_PLAYER, AIC_MATE, AIC_TEMPT,
    AIC_FOLLOW_PARENT, AIC_WANDER, AIC_WATCH_CLOSEST, AIC_LOOK_IDLE, AIC_EAT_GRASS,
    AIC_AVOID_ENTITY, AIC_TRADE_PLAYER, AIC_LOOK_AT_TRADE_PLAYER, AIC_MOVE_INDOORS,
    AIC_RESTRICT_OPEN_DOOR, AIC_OPEN_DOOR, AIC_MOVE_TOWARDS_RESTRICTION,
    AIC_VILLAGER_MATE, AIC_FOLLOW_GOLEM, AIC_PLAY, AIC_WATCH_CLOSEST2,
    AIC_LOOK_AT_VILLAGER, AIC_MOVE_THROUGH_VILLAGE, AIC_ATTACK_ON_COLLIDE,
    AIC_MOVE_TOWARDS_TARGET, AIC_DEFEND_VILLAGE, AIC_HURT_BY_TARGET,
    AIC_NEAREST_ATTACKABLE_TARGET, AIC_BREAK_DOOR,
    AIC_RESTRICT_SUN, AIC_FLEE_SUN, AIC_ARROW_ATTACK, AIC_CREEPER_SWELL
};

/* The most tasks one list holds: the villager's 15 plus the leash's
 * EntityAIMoveTowardsRestriction (EntityCreature.updateLeashedState); the
 * add functions (ai.c) stop the program before a list would overflow. */
#define AI_MAX_ENTRIES 16

/* A path in the environment's path pool (arena.h): 0 for none, else its
 * slot + 1. path_alloc makes one with one holder, path_hold adds a holder,
 * path_drop lets go (the last holder frees it); path_at reads it (NULL for 0). */
typedef int32_t pathref;
pathref path_alloc(int npoints);
struct path_ent *path_at(pathref h);
void path_hold(pathref h);
void path_drop(pathref h);
/* living.c: drop every path l holds (before l itself is freed) */
struct living;
void living_drop_paths(struct living *l);

/* A reference to a living in the environment's pool: its slot's generation
 * and index under a tag, 0 for none. What a living keeps of another across
 * ticks (its targets, its mount and rider, its attackers, a task's partner)
 * is one of these, never an address: lv_ref makes one, lv_get reads it back
 * (a reference whose generation is not the slot's any more stops the
 * program: the grave releases a living only when nothing refers to it). */
typedef uint64_t lref;
#define LREF_TAG 0x4C56ull

/* One task's state: the fields every class shares, then each class's own
 * (EntityAI* subclasses' fields) over one another in an anonymous union, so a
 * task costs its largest class, not every class at once (lane/entmem: 376
 * bytes to 120). A task reads and writes only its own class's member (ai.c's
 * per-class cases; add_task_full sets only the class's parameters). The
 * shared fields are those several classes use or that code outside the class
 * reads on any task (the paths living_drop_paths lets go of, the village
 * villagers.c looks for). */
struct ai_task {
    int cls;
    int mutex;
    double speed;              /* the class's move speed (panic, tempt, wander ..; farSpeed) */
    lref target;               /* closestEntity, targetMate, parentAnimal, targetEntity .. */
    double x, y, z;            /* the wander/panic/restriction target; the idle look's x, z */
    pathref path;              /* EntityAIAttackOnCollide, EntityAIAvoidEntity */
    pathref mtv_path;          /* EntityAIMoveThroughVillage.entityPathNavigate */
    int32_t front_door;        /* the VillageDoorInfo's door pool slot + 1, 0 none */
    int32_t village_ptr;       /* the Village's index + 1 in the collection, 0 none */
    int target_class;          /* HK_PLAYER, VK_VILLAGER, etc. */
    float watch_chance;        /* EntityAIWatchClosest's chance, EntityAINearestAttackableTarget's targetChance */
    float max_watch_dist;      /* the watch, avoid and move-towards-target distances */
    union {
        struct {               /* EntityAIMate */
            int spawn_baby_delay;
        };
        struct {               /* EntityAITempt */
            int delay_tempt_counter;
            int item_id;       /* the tempt item */
            int scared_by_movement;
            int tempt_running, tempt_old_avoid;
            double tempt_pitch, tempt_yaw;
        };
        struct {               /* EntityAIFollowParent */
            int follow_parent_delay;   /* field_75345_d */
        };
        struct {               /* EntityAIWatchClosest(2), EntityAILookAtTradePlayer, EntityAILookAtVillager */
            int look_time;
            int watch_class;   /* 0 = player, 1 = villager, 2 = living */
            int is_watching_player;
        };
        struct {               /* EntityAILookIdle */
            int idle_time;
        };
        struct {               /* EntityAIEatGrass */
            int eat_grass_timer;   /* field_151502_a */
        };
        struct {               /* EntityAIOpenDoor, EntityAIBreakDoor (EntityAIDoorInteract) */
            int door_x, door_y, door_z;
            float door_pos_x, door_pos_z;   /* the direction vector */
            int has_stopped_door_interaction;
            int door_counter;  /* EntityAIOpenDoor field_75360_j */
            int breaking_time; /* EntityAIBreakDoor */
            int field_75358_j; /* EntityAIBreakDoor */
        };
        struct {               /* EntityAIMoveIndoors */
            int inside_pos_x, inside_pos_z;
        };
        struct {               /* EntityAIVillagerMate */
            int mating_timeout;
            lref target_mate;
        };
        struct {               /* EntityAIFollowGolem */
            int take_golem_rose_tick;
            int took_golem_rose;
            lref target_golem;
        };
        struct {               /* EntityAIPlay */
            int play_time;
            lref target_child;
        };
        struct {               /* EntityAIMoveThroughVillage */
            int nocturnal;
        };
        struct {               /* EntityAITarget: EntityAINearestAttackableTarget, EntityAIHurtByTarget, EntityAIDefendVillage */
            int should_check_sight;
            int nearby_only;
            int target_search_status;  /* targetSearchStatus: 0 unknown, 1 reachable, 2 not */
            int target_search_delay;
            int target_unseen_ticks;   /* field_75298_g */
            int calls_for_help;        /* EntityAIHurtByTarget */
            int revenge_timer_last;    /* EntityAIHurtByTarget field_142052_b */
        };
        struct {               /* EntityAIAvoidEntity */
            double avoid_near_speed;   /* nearSpeed (speed is farSpeed) */
        };
        struct {               /* EntityAIAttackOnCollide */
            int long_memory;
            int attack_tick;
            int collide_cooldown;      /* field_75445_i */
            double collide_px, collide_py, collide_pz;   /* field_151497_i/j/k */
        };
        struct {               /* EntityAIFleeSun */
            double shelter_x, shelter_y, shelter_z;
        };
        struct {               /* EntityAIArrowAttack */
            int ranged_attack_time;
            int field_75318_f;
            int field_96561_g;         /* min interval */
            int max_ranged_attack_time;/* max interval */
            float field_96562_i;       /* max distance */
            float field_82642_h;       /* max distance sq */
        };
        struct {               /* EntityAIControlledByPlayer (the pig's saddle steering, riding.c) */
            float ctl_current_speed;
            int ctl_speed_boosted, ctl_speed_boost_time, ctl_max_speed_boost_time;
        };
    };
};

struct ai_entry {
    int priority;
    struct ai_task t;
};

struct ai_tasks {
    int tick_count;
    int n;
    struct ai_entry entries[AI_MAX_ENTRIES];
    int executing[AI_MAX_ENTRIES];
    int nexec;
};

/* PathEntity: the points, however many the search made (a slab block of
 * cap, taken by path_alloc and given back with the path), and the cursor. */
struct path_ent {
    int length;
    int index;
    int (*pts)[3];
    int cap;
    int n_points;
    int shares;   /* navigators holding it beyond the first (nav_set_path_shared) */
};

struct pf;

struct path_nav {
    int avoids_water, can_swim, can_pass_open_doors, can_pass_closed_doors, no_sun_pathfind;
    double speed;
    int total_ticks, ticks_at_last_pos;
    double lx, ly, lz;
    pathref path;
    struct world *finder_world;   /* the world its searches read (ai.c nav_scratch_take) */
};

struct look_helper {
    float delta_look_yaw, delta_look_pitch;
    int is_looking;
    double x, y, z;
};

struct move_helper {
    double x, y, z, speed;
    int update;
};

struct jump_helper {
    int is_jumping;
};

struct body_helper {
    int counter;
    float yaw;
};

/* One equipment ItemStack: tag is its compound in the item tag store
 * (itemtag.h, 0 for none), the enchantments among it. */
struct equip_slot {
    int id;
    int damage;
    int count;
    int tag;
    struct stack_link count_link;   /* item_entity.h: another living's slot it shares */
};

struct senses {
    lref seen[64];
    int nseen;
    lref unseen[64];
    int nunseen;
};


/* The parts of a living kept beside it rather than in it (the hot record the
 * tick walks stays small): each in its own pool of the environment's arena
 * (arena.h), taken when first reached through lv_ai, lv_villager and
 * lv_player. A living that is not a pool slot (part 0) has none. */
struct living_ai {
    struct ai_tasks tasks, target_tasks;
    /* EntityAIMoveThroughVillage.doorList (field_75413_g) of the one
     * move-through-village task a living has (the iron golem's, the
     * zombie's): kept here, not in every task entry */
    int mtv_visited_doors[16][3];
    int mtv_nvisited_doors;
};

/* EntityVillager's trading state */
struct living_villager {
    struct trade_list recipes;
    char buying_player_name[64];    /* the customer's name, for
                                     * lastBuyingPlayer */
    char last_buying_player[64];    /* EntityVillager.lastBuyingPlayer's name,
                                     * or empty when null */
};

/* the probe's own player's inventory */
struct living_player {
    struct equip_slot player_inv[36];
};

/* EntityLiving and above. */
/* The fields are laid out by how a tick touches them (lane/villscale,
 * out/perf/vs: every load and store of a headline world's livings traced
 * per tick): first the entity and what every living's tick reads, updated
 * or not (the spawner's census, the entity tracker); then what a living
 * whose update runs touches; then the rest, cold, each kind's own state
 * together. A living outside the updated area reads 6 of its lines a tick
 * this way (13 before), an updated animal about 24 (34). Keep a new field
 * with the group that touches it. */
struct living {
    struct entity e;

    /* --- read every tick for every living (census, tracker) --- */
    int kind;
    int entity_id;
    int air;                     /* dataWatcher 1 */
    float rotation_yaw, rotation_pitch;
    float rotation_yaw_head;
    float health;
    int growing_age;             /* EntityAgeable */
    int data_watcher_16;              /* pig saddle / sheep colour / ghast charge;
                                         the bat's hanging bit */
    int potion_liquid_color;
    int arrow_count_in_entity;
    uint8_t flags0;              /* dataWatcher 0 */
    uint8_t is_dead, is_air_borne;
    uint8_t persistence_required;
    uint8_t dw_touched;          /* DataWatcher.updateObject changed a watched value since the tracker's last update (a set and a reset in one tick still count) */
    /* Entity riding: Entity.ridingEntity / Entity.riddenByEntity. The spider
     * jockey's rider is the only mount the probe's world ever holds. */
    lref riding_entity;
    lref ridden_by_entity;

    /* --- touched by a living's update --- */
    struct cell_memo cmemo;        /* what the cells around its box answered (entity.h) */
    int32_t part;                  /* the pool slot + 1 (its parts), 0 for none */
    /* its parts' slots + 1 in their pools (arena.h), 0 until first reached */
    int32_t ai_part, villager_part, player_part;
    struct world *world;
    struct an_world *an;
    det_rng rand;
    uint8_t added_to_chunk, immune_to_fire, in_portal;
    uint8_t is_in_water;
    uint8_t is_jumping;
    uint8_t is_leashed;
    uint8_t potions_need_update;
    uint8_t potion_is_ambient;
    int chunk_coord_x, chunk_coord_y, chunk_coord_z;
    double last_tick_x, last_tick_y, last_tick_z;
    float prev_rotation_yaw, prev_rotation_pitch;
    int portal_counter, time_until_portal;
    float prev_distance_walked_modified;
    int hurt_time, attack_time, hurt_resistant_time;
    int recently_hit;
    lref entity_living_to_attack;
    lref last_attacker;
    lref attacking_player;   /* always NULL: no players in the probe */
    int entity_age;
    float limb_swing, limb_swing_amount, prev_limb_swing_amount;
    float swing_progress, prev_swing_progress;
    float camera_pitch, prev_camera_pitch;
    float prev_rotation_yaw_head;
    float render_yaw_offset, prev_render_yaw_offset;
    float field_110154_aX, field_70764_aw;
    int jump_ticks;
    float move_strafing, move_forward, random_yaw_velocity, land_movement_factor;
    int ticks_existed;
    void (*on_living_update)(struct living *l, det_state *det);

    /* EntityLiving */
    int living_sound_time;
    struct look_helper look;
    struct move_helper move;
    struct jump_helper jump;
    struct body_helper body;
    struct path_nav nav;

    /* EntityAnimal */
    int in_love, breeding;
    int sheep_timer;

    int leash_pending;
    int leash_ai;
    /* EntityLivingBase.onUpdate's previousEquipment pass: the equipment as
     * the last update's head saw it (after onEntityUpdate, before
     * onLivingUpdate), what its S04s left the client copy holding */
    int equip_sent_valid;
    struct equip_slot equip[5];
    struct equip_slot equip_sent[5];

    /* Potion effects: the map's head is read every update, its entries
     * only when it holds any */
    struct potion_map potions;

    /* --- cold: the rest, each kind's own state together --- */
    int (*external_attack)(void *ctx, int source, float amount);
    void *external_attack_ctx;
    /* Entity.setFire on a twin whose state lives elsewhere (the server
     * player): the ticks go to the real entity (same ctx) */
    void (*external_set_fire)(void *ctx, int ticks);
    /* EntityLivingBase.heal on such a twin (a splash of healing on the
     * server player): the healed health goes to the real entity (same ctx) */
    void (*external_heal)(void *ctx, float health);
    /* EntityLivingBase.onDeath's tail (the victim's var3.addToPlayerScore
     * when score, then var2.onKillEntity when kill): the killer's hook,
     * called before the loot (Java's order is score, onKillEntity, then
     * dead and the loot). */
    void (*on_death)(void *ctx, struct living *victim, int source, int score, int kill);
    void *on_death_ctx;

    /* EntityPlayerMP's potion hooks (onNewPotionEffect / onChangedPotionEffect /
     * onFinishedPotionEffect): the player twin reports every add, change and
     * removal so serverreplay can queue the S1D / S1E packets the hook sends.
     * event: 0 new, 1 changed (either flag), 2 finished. */
    int (*on_potion_event)(void *ctx, int event, int id, int amplifier, int duration);
    void *on_potion_event_ctx;

    int player_mp;          /* an EntityPlayerMP (the whole-server player), not a probe's EntityPlayer */
    int64_t uuid_msb, uuid_lsb;

    /* Entity */
    int dimension;
    uint8_t dead, invulnerable;
    int teleport_direction;      /* Entity.teleportDirection, setInPortal's Direction */

    /* EntityLivingBase */
    float absorption;
    int death_time, max_hurt_time, max_hurt_resistant_time;
    int revenge_timer;
    float last_damage, prev_health;
    float attacked_at_yaw;
    int last_attacker_time;
    /* the whole-server player this living twins (serverreplay binds it):
     * its inventory is getHeldItem and getLastActiveItems (the armour), its
     * Random is sv.erand and its fire is the server player's */
    struct server_player *player_sp;
    int experience_value;
    uint8_t can_pick_up_loot;
    /* EntityLiving's custom name (data watcher 10, NBT CustomName), "" for
     * none; ItemNameTag sets it */
    char custom_name[32];
    struct attr_map attrs;

    /* ServersideAttributeMap's dirty set holds a watched instance (the
     * movement speed, the max health): a potion applied or removed its
     * modifier; the player's own tracker entry sends the S20 and clears it */
    uint8_t attr_watch_dirty;

    void (*write_kind_nbt)(struct living *l, struct nbt *tag);
    void (*kind_tick)(struct living *l, det_state *det);
    /* EntityLivingBase.fall, as the kind overrides it: NULL is the base body
     * (damage when the drop passes three), EntityChicken.fall is empty, and
     * EntityPig.fall is the base plus an achievement no world here can award. */
    void (*kind_fall)(struct living *l, float distance);

    /* EntityLiving */
    lref attack_target;
    lref current_target;
    int num_ticks_to_chase_target;
    float default_pitch;

    /* EntityCreature */
    lref entity_to_attack;
    uint8_t has_attacked;
    int fleeing_tick;
    int home_x, home_y, home_z;
    float maximum_home_distance;

    /* EntityAgeable */
    float base_width, base_height;

    /* EntityVillager */
    int profession;
    int wealth;
    int time_until_reset;
    int needs_initialization;
    int is_mating;
    int is_playing;
    int random_tick_divider;
    int is_looking_for_home;
    int32_t village;                /* EntityVillager.villageObj: its index + 1 in
                                     * the world's collection (villagers.h), 0 none */
    int has_recipes;
    int buying_player;              /* EntityVillager.buyingPlayer != null
                                     * (setCustomer): the AI gates and
                                     * isTrading read it */
    int has_last_buying_player;

    /* EntityIronGolem */
    int attack_timer;
    int hold_rose_tick;
    int is_player_created;
    int home_check_timer;

    /* EntitySlime */
    int slime_size;                    /* dataWatcher 16 (getSlimeSize) */
    int slime_jump_delay;
    int slime_was_on_ground;           /* EntitySlime.onUpdate's var1 across super */
    float squish_amount, squish_factor, prev_squish_factor;

    /* EntityPlayer, the probe's own player: the food stats the tick reads and
     * the lastDamage/hurt counters EntityLivingBase already carries */
    int food_level, food_timer, prev_food_level;
    float food_saturation, food_exhaustion;

    /* the kind-specific state */
    int time_until_next_egg;
    int is_chicken_jockey;

    /* EntitySquid */
    float squid_pitch, prev_squid_pitch, squid_yaw, prev_squid_yaw;
    float squid_rotation, prev_squid_rotation;
    float tentacle_angle, last_tentacle_angle;
    float squid_random_motion_speed, squid_rotation_velocity, squid_field_70871_bB;
    float squid_random_motion_vec_x, squid_random_motion_vec_y, squid_random_motion_vec_z;

    /* EntityBat */
    int bat_spawn_x, bat_spawn_y, bat_spawn_z, bat_has_spawn_pos;
    float chicken_dest_pos, chicken_field_70886_e, chicken_field_70884_g, chicken_field_70888_h, chicken_field_70889_i;
    int equipment[5];
    float equipment_drop_chances[5];
    /* Hostile / Zombie state */
    int zombie_conversion_time;
    int zombie_is_child;
    int zombie_is_villager;
    int zombie_can_break_doors;
    int zombie_is_converting;
    int pigman_anger_level;
    int pigman_random_sound_delay;
    lref pigman_last_entity_to_attack;
    int silverfish_ally_summon_cooldown;

    /* EntityLiving's leash (leash.c): the holder (LEASH_PLAYER: the replay's
     * player twin, LEASH_KNOT: leash_knot), the NBT tag readEntityFromNBT
     * kept for recreateLeash (field_110170_bx: 1 by UUID, 2 by knot tile,
     * 3 neither; leash_pending above), and EntityCreature.field_110180_bt
     * (leash_ai above). is_leashed is EntityLiving.isLeashed. */
    int leash_holder;
    uint64_t leash_knot;            /* the knot (fallhang.h fhref), 0 none */
    /* the S1B type 1 packets setLeashedToEntity and clearLeashed(true, ..)
     * sent: recreateLeash after a load sends none, so a watcher's copy keeps
     * what the tracker's first S1B told it */
    uint32_t leash_sends;
    int64_t leash_msb, leash_lsb;
    int leash_x, leash_y, leash_z;

    /* Hostile / Skeleton state */
    int skeleton_type;

    /* EntityWitch */
    int witch_attack_timer;
    int witch_aggressive;
    lref damage_attacker;
    /* set while an arrow the player shot is hitting this living (its onDeath
     * reads the source: EntitySkeleton's snipeSkeleton) */
    int player_arrow_hit;
    /* set while an arrow with no shooter is hitting this living:
     * causeArrowDamage(this, this), so DamageSource.getEntity() is the
     * arrow, a non-living, at (hit_src_x, hit_src_z) (the knockback's origin) */
    int hit_src_arrow;
    double hit_src_x, hit_src_z;
    /* EntityCreature.entityToAttack is a non-living entity that is dead (an
     * EntityMob hit by a shooterless arrow, which died hitting it): the next
     * updateEntityActionState clears it instead of finding a player */
    int entity_to_attack_gone;
    /* EntityAnimal.field_146084_br: the player fed this animal (func_146082_f);
     * transient, never in NBT or a snapshot */
    int love_player;
    int arrow_hit_timer;          /* EntityLivingBase.arrowHitTimer */

    /* EntityBlaze */
    float blaze_height_offset;
    int blaze_height_offset_update_time;
    int blaze_attack_step;

    /* EntityGhast */
    int course_change_cooldown, prev_attack_counter, attack_counter, aggro_cooldown;
    int explosion_power;              /* explosionStrength, read back from NBT */
    double waypoint_x, waypoint_y, waypoint_z;
    int ghast_target;                 /* 1: the target is the ghast world's player (gh_world.player) */
    int ghast_target_is_player;

    /* EntityCreeper: the fuse and the three dataWatcher bytes (16 state, 17
     * powered, 18 ignited) */
    int creeper_last_active_time;
    int creeper_time_since_ignited;
    int creeper_fuse_time;
    int creeper_explosion_radius;
    int creeper_state;
    int creeper_powered;
    int creeper_ignited;

    /* EntityEnderman: the three data watcher bytes (16 carried block, 17 its
     * metadata, 18 screaming), the stare and teleport counters and the last
     * attack target the speed-boost modifier tracks. */
    int enderman_carried_block;
    int enderman_carrying_data;
    int enderman_screaming;
    int enderman_stare_timer;
    int enderman_teleport_delay;
    int enderman_is_aggressive;
    lref enderman_last_entity_to_attack;

    /* EntitySpider / EntityCaveSpider: the old-AI mob's own path, separate
     * from the navigator's (the spider only sets it through
     * World.getPathEntityToEntity / getEntityPathToXYZ). */
    pathref creature_path;


    int spawn_index;
    float difficulty_factor;
};

void living_part_missing(const struct living *l);
void lref_stale(lref h);
/* the count a stack link names, NULL for none, and an item's link to its
 * own count (living.c) */
int *stack_link_count(struct stack_link k);
struct stack_link stack_link_item(const ie_ent *en);

static inline lref lv_ref(const struct living *l)
{
    if (l == NULL) return 0;
    if (__builtin_expect(l->part <= 0, 0)) living_part_missing(l);
    uint32_t i = (uint32_t)(l->part - 1);
    return LREF_TAG << 48 | (uint64_t)(nw_arena()->livings.gen[i] & 0xFFFFu) << 32 | i;
}

static inline struct living *lv_get(lref h)
{
    if (h == 0) return NULL;
    const struct slot_pool *p = &nw_arena()->livings;
    uint32_t i = (uint32_t)h;
    if (__builtin_expect(((h >> 32) & 0xFFFFu) != (p->gen[i] & 0xFFFFu), 0)) lref_stale(h);
    return (struct living *)pool_at(p, (int32_t)i);
}

/* A part, taken from its pool zeroed the first time the living reaches it
 * (arena.c), so a kind that never reads a part holds none; *_peek is the
 * part or NULL, without taking one. */
enum { LV_PART_AI, LV_PART_VILLAGER, LV_PART_PLAYER };
void *living_part_take(const struct living *l, int which);
/* the parts back to their pools and forgotten (living_release, living_init) */
void living_parts_release(struct living *l);

static inline struct living_ai *lv_ai(const struct living *l)
{
    if (__builtin_expect(l->part <= 0, 0)) living_part_missing(l);
    if (__builtin_expect(l->ai_part == 0, 0)) return living_part_take(l, LV_PART_AI);
    return pool_at(&nw_arena()->ai_parts, l->ai_part - 1);
}

static inline struct living_villager *lv_villager(const struct living *l)
{
    if (__builtin_expect(l->part <= 0, 0)) living_part_missing(l);
    if (__builtin_expect(l->villager_part == 0, 0)) return living_part_take(l, LV_PART_VILLAGER);
    return pool_at(&nw_arena()->villager_parts, l->villager_part - 1);
}

static inline struct living_player *lv_player(const struct living *l)
{
    if (__builtin_expect(l->part <= 0, 0)) living_part_missing(l);
    if (__builtin_expect(l->player_part == 0, 0)) return living_part_take(l, LV_PART_PLAYER);
    return pool_at(&nw_arena()->player_parts, l->player_part - 1);
}

static inline struct living_ai *lv_ai_peek(const struct living *l)
{
    return l->ai_part ? pool_at(&nw_arena()->ai_parts, l->ai_part - 1) : NULL;
}

static inline struct living_villager *lv_villager_peek(const struct living *l)
{
    return l->villager_part ? pool_at(&nw_arena()->villager_parts, l->villager_part - 1) : NULL;
}

static inline struct living_player *lv_player_peek(const struct living *l)
{
    return l->player_part ? pool_at(&nw_arena()->player_parts, l->player_part - 1) : NULL;
}

/* ------------------------------------------------------------- attributes */

void attrs_init(struct attr_map *m);
struct attr_instance *attrs_get(struct attr_map *m, int attr);
double attrs_value(struct attr_instance *a);
void attrs_set_base(struct attr_instance *a, double v);
void attrs_apply(struct attr_instance *a, const struct attr_mod *mod);
void attrs_remove(struct attr_instance *a, const struct attr_mod *mod);
void attrs_remove_all(struct attr_instance *a);
/* ServersideAttributeMap.getAttributeInstanceByName: the attribute a saved
 * name picks, or -1 for none. */
int attrs_index_by_name(const char *name);

/* ------------------------------------------------------- the entity lists */

#define AN_MAX_ENTITIES 4096
/* A query's result list: World.getEntitiesWithinAABB returns every match,
 * so the list holds as many as the entity list can (AN_MAX_ENTITIES), taken
 * from the env's scratch stack for the enclosing scope. */
#define AN_QUERY_LIST(name) struct an_ent **name ENV_LOCAL = envstack_take(AN_MAX_ENTITIES * sizeof *name)

struct an_ent {
    int is_living;
    int used;              /* 0 once the entity left the list; entries are pooled */
    int spawn_index;
    lref livh;
    ieref ieh;
    /* the pass-order key when it is not the entity id (see sr_order_push):
     * an entity a chunk load inside a mob's getCanSpawnHere brought in
     * joined the list before that mob, whose id is smaller */
    int order_key;
    /* a portal traveller's old instance left in the destination chunk's
     * list (its addedToChunk cleared by the source world's chunk check): out
     * of the pass's list, dead, until the chunk unloads */
    int ghost;
};

struct an_chunk {
    int cx, cz;
    struct sec_list sec[16];   /* the an_ent slots (at most the world's AN_MAX_ENTITIES) */
};

struct an_world {
    struct world *w;
    det_state *det;
    ie_world iew;
    /* The entity list in spawn order. The entries are heap-allocated and the
     * array holds pointers, so the per-chunk section lists (and every AI
     * target) keep valid references when an entry leaves the list. */
    int32_t slot[AN_MAX_ENTITIES];   /* the list entries' pool slots (an_ent_at) */
    int n;
    int process_dead;          /* WorldServer's live list removes picked-up items */
    int update_ridden;         /* World.updateEntity uses updateRidden for mounts */
    /* the difficulty factor the NEXT an_spawn_living(with_egg) uses for
     * its onSpawnWithEgg draws (World.func_147473_b at the spawn point):
     * the summon path sets it; every other path stays 0 */
    float spawn_diff_factor;
    /* the egg path is rebuilding a mob the natural spawner's record path
     * already spawned (sr_adopt_mob): a baby zombie's jockey chicken is a
     * record of its own, and a spider takes its pack's GroupData effect
     * (egg_spider_potion, 0 none) with no draw */
    int egg_adopt;
    int egg_spider_potion;
    int next_spawn_index;
    struct an_chunk *chunks;
    int nchunks, capchunks;
    /* chunks by (cx, cz): an index + 1 per slot (0 empty), linear probing,
     * a power of two at least twice nchunks (a slab block; an_chunk_find) */
    int32_t *chunk_index;
    int chunk_index_cap;
    int skylight;                 /* World.skylightSubtracted */
    int raining;                  /* World.isRaining: getRainStrength(1.0F) > 0.2 */
    int dimension;                /* Entity.dimension for everything the run spawns */
    int difficulty;               /* World.difficultySetting (an_init: NORMAL) */
    double world_rand_state;      /* unused; the item world owns the stream */
    int64_t world_time;
    int has_player;
    double player_x, player_y, player_z;
    /* the world's playerEntities is empty (the whole-server replay's world
     * the player left): the probes' absent player stands for a far-away
     * one, this for none at all */
    int no_players;
    /* the item id in the player's current slot (0 empty): EntityAITempt's
     * getCurrentEquippedItem check */
    int player_held_item;
    /* A living constructor reached from a tick (EntityAIMate.spawnBaby's
     * child) draws its id, seed and Math.random from DET_OTHER; the
     * whole-server replay installs this to run it on the server's streams
     * instead (begin 1 before the constructor, 0 after). NULL in the probes. */
    void (*constructor_hook)(struct an_world *an, int begin, void *ctx);
    void *constructor_ctx;
    /* EntityAIMate.spawnBaby's credit to the player who fed a parent
     * (func_146083_cb): the parent's kind */
    void (*bred_hook)(struct an_world *an, int kind, void *ctx);
    void *bred_ctx;
    /* EntityPlayer.triggerAchievement on the run's player from a mob's tick
     * (EntityLiving.onLivingUpdate's diamondsToYou); ctx is bred_ctx.
     * NULL in the probes. */
    void (*achievement_hook)(struct an_world *an, int stat, void *ctx);
    /* EntityLivingBase.onItemPickup's S0D for an item a mob takes
     * (EntityLiving.onLivingUpdate's loot pickup), to the item's watchers;
     * ctx is bred_ctx. NULL in the probes. */
    void (*collect_hook)(struct an_world *an, int item_entity_id, void *ctx);
    /* EntityTracker.trackEntity from spawnEntityInWorld, for a spawn inside
     * the tick (the whole-server replay's client world); ctx is
     * constructor_ctx. NULL in the probes. */
    void (*track_spawn)(struct an_world *an, struct living *l, void *ctx);
    /* EntityZombie.onKillEntity's infection (the villager is removed, a
     * zombie villager spawned in its place) and convertToVillager (the
     * zombie goes, a villager joins): the whole-server replay's spawn paths;
     * ctx is constructor_ctx. NULL in the probes: nothing happens. */
    void (*on_infect)(struct an_world *an, struct living *zombie, struct living *victim, void *ctx);
    void (*on_convert)(struct an_world *an, struct living *zombie, void *ctx);
    /* EntityZombie.attackEntityFrom's HARD reinforcement once its roll
     * passed: the new zombie's constructor, the caller's position tries, the
     * spawn with its attack target and egg, the two charge modifiers; ctx
     * is constructor_ctx. NULL in the probes: nothing happens. */
    void (*on_reinforce)(struct an_world *an, struct living *caller, struct living *target, det_state *det, void *ctx);
    /* the probe's player living, when the run has one: getClosestPlayer and
     * getClosestVulnerablePlayer read it (playerEntities); NULL falls back to
     * the point above */
    lref playerh;                 /* the probe's player living (lv_get), 0 none */
    struct village_collection *village_collection;
    struct village_siege *village_siege;
    jrand *shuf_rand;
    void (*on_write_cb)(int x, int y, int z, int meta, int flags, void *ctx);
    void *on_write_ctx;
    /* the probe tag: the ghast lane's gh_world, unused by the farm-animal and
     * villager probes */
    void *user_data;

    /* A check's removal recorder: called with every entity that leaves the list,
     * so the removals can be compared against the oracle's removal stream. An
     * entity created and removed inside one tick never shows up in a
     * before/after snapshot, which is why the hook exists. */
    void (*on_remove_cb)(int spawn_index, int entity_id, void *ctx);
    void *on_remove_ctx;

    /* collideWithNearbyEntities past this list: the owner's other pushable
     * entities in box (the End's dragon), each pushing l through its own
     * applyEntityCollision. NULL: none. */
    void (*collide_extra)(void *ctx, struct living *l, const struct aabb *box);
    void *collide_extra_ctx;
    /* whether the extra entity (the dragon) comes before living q in the
     * query's chunk order: its push then lands before q's */
    int (*collide_extra_before)(void *ctx, const struct living *q);

    /* Entity.travelToDimension for a non-player living whose portal counter
     * ran out inside Entity.onEntityUpdate (to is 0 or -1): the owner moves
     * it between its per-world pools. The rest of the entity's update runs
     * on after the call, dead, in the destination world. NULL: the entity
     * stays (the probes). */
    void (*portal_travel)(void *ctx, struct living *l, int to);
    /* called right after each living's update in an_tick_one, before the
     * pass's chunk and list bookkeeping (a traveller's owner swaps its
     * world back here) */
    void (*post_update)(void *ctx);
    void *portal_travel_ctx;
};

/* World.loadedEntityList.add: the list holds AN_MAX_ENTITIES, and a world
 * past it stops the program (Java's list grows; never a silent drop) */
static inline void an_list_push(struct an_world *an, const struct an_ent *en)
{
    if (an->n >= AN_MAX_ENTITIES) list_full("entities in one living world", AN_MAX_ENTITIES);
    an->slot[an->n++] = an_ent_index(en);
}

/* The environment's pools (arena.c): a zeroed living or list entry, and its
 * release. */
struct living *living_alloc(void);
void living_release(struct living *l);
struct an_ent *an_ent_alloc(void);
void an_ent_release(struct an_ent *en);

void an_init(struct an_world *an, struct world *w, det_state *det);
void an_free(struct an_world *an);
void an_add(struct an_world *an, struct an_ent *en);
void an_remove(struct an_world *an, struct an_ent *en);
void an_remove_released(struct an_world *an, struct an_ent *en, const ie_removal *r);
void an_chunk_add(struct an_world *an, struct an_ent *en, int cx, int cy, int cz);
void an_chunk_remove(struct an_world *an, struct an_ent *en, int cy);
/* A living added after the moment Java's spawnEntityInWorld put it in its
 * chunk list (a spawner record adopted after the pass): its stamp then, and
 * its place in the section list back ahead of what joined since. */
void an_chunk_restamp(struct an_world *an, struct an_ent *en, uint64_t stamp);
/* World.getEntitiesWithinAABB(class, box): the filter is a Java class, and
 * EntityMooshroom extends EntityCow, so a cow-class query matches mooshrooms
 * too (the other way round it does not). */
int living_kind_in_class(int kind, int class_kind);
int an_entities_within_aabb(struct an_world *an, int kind, const struct aabb *box, struct an_ent **out, int max_out);
int an_entities_excluding(struct an_world *an, const struct an_ent *exclude, const struct aabb *box, struct an_ent **out, int max_out);
void an_tick(struct an_world *an, int tick);
int an_tick_one(struct an_world *an, struct an_ent *en, int tick);
void an_update_one(struct an_world *an, struct an_ent *en, int tick);
/* The chunk membership World.updateEntityWithOptionalForce keeps, for a tick
 * loop the probe owns (the ghast lane's). */
void an_chunk_membership(struct an_world *an, struct an_ent *en);
struct an_chunk *an_chunk_find(struct an_world *an, int cx, int cz);
/* Chunk (cx, cz)'s entity lists gone when every one is empty (an unloaded
 * chunk's, as vanilla's Chunk goes with its lists): 1 when they went. A
 * lookup finds no chunk there, as it found empty lists before. */
int an_chunk_drop_empty(struct an_world *an, int cx, int cz);

/* The spawn path the probe uses: the record's fields, then the set-up. */
void an_add_living(struct an_world *an, struct living *l, int spawn_index);
struct an_ent *an_add_living_list(struct an_world *an, struct living *l, int spawn_index);
void an_add_living_chunk(struct an_world *an, struct an_ent *en, struct living *l);
struct living *living_on_spawn_with_egg_ret(struct living *l, det_state *det);
void an_construct_kind(struct living *l, det_state *det);

struct living *an_spawn_living(struct an_world *an, int kind, int spawn_index, double x, double y, double z,
                               float yaw, float pitch, int growing_age, int in_love, int saddled, int egg_timer,
                               int sheared, int with_egg);
void an_egg_chicken(struct an_world *an, struct ie_ent *egg);
ie_ent *an_spawn_item(struct an_world *an, int spawn_index, double x, double y, double z, int item, int damage,
                      int count, int delay);

/* ------------------------------------------------------------- the entity */

void living_init(struct living *l, struct world *w, int kind, det_state *det);
void animal_setup(struct living *l, det_state *det);
void living_on_spawn_with_egg(struct living *l, det_state *det);
/* EntitySkeleton.onSpawnWithEgg alone, the spider jockey rider's */
void living_skeleton_on_spawn_with_egg(struct living *l, det_state *det);
void living_set_location_and_angles(struct living *l, double x, double y, double z, float yaw, float pitch);
void living_set_walking_world(jrand *wr);
void living_set_growing_age(struct living *l, int v);
void living_set_base_size(struct living *l, float w, float h);
void living_set_size(struct living *l, float w, float h);
int living_dmg_unblockable(int source);
int living_dmg_absolute(int source);
/* DamageSource.getHungerDamage, the exhaustion a hit costs the player. */
float living_dmg_hunger(int source);
void living_update_entity(struct living *l, det_state *det);
void living_attack_entity_from(struct living *l, int source, float amount, det_state *det);

/* hostiles.c */
void zombie_construct(struct living *l, det_state *det);
struct living *zombie_on_spawn_with_egg(struct living *l, det_state *det);
void zombie_on_living_update(struct living *l, det_state *det);
int zombie_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);
void zombie_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
void player_construct(struct living *l, det_state *det);

/* hostiles_spider.c */
void spider_construct(struct living *l, det_state *det);
void spider_on_living_update(struct living *l, det_state *det);
void spider_update_entity_action_state(struct living *l, det_state *det);
void spider_on_update_tail(struct living *l);
int spider_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);
void spider_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
struct nbtw;
void spider_write_kind_nbt(struct living *l, struct nbtw *w);
int spider_is_potion_applicable(const struct living *l, int potion_id);
struct living *spider_jockey_check(struct living *l, det_state *det);
void spider_take_pending_rider(void);
struct living *spider_spawn_jockey_skeleton(struct an_world *an, struct living *l, det_state *det);
/* hostiles_witch.c */
void witch_construct(struct living *l, det_state *det);
void witch_on_living_update(struct living *l, det_state *det);
void witch_attack_entity_with_ranged_attack(struct living *l, struct living *target, float power);
void witch_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
void witch_set_aggressive(struct living *l, int agg);
int witch_get_aggressive(const struct living *l);

/* hostiles_skeleton.c */
void skeleton_construct(struct living *l, det_state *det);
void skeleton_set_type(struct living *l, int type);
void skeleton_set_combat_task(struct living *l);
void skeleton_on_spawn_with_egg(struct living *l, det_state *det);
void skeleton_on_living_update(struct living *l, det_state *det);
int skeleton_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);
void skeleton_attack_entity_with_ranged_attack(struct living *l, struct living *target, float power);
void skeleton_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
struct nbtw;
void skeleton_write_kind_nbt(struct living *l, struct nbtw *w);

void living_set_health(struct living *l, float v);
float living_max_health(struct living *l);
int living_is_alive(struct living *l);
void living_set_dead(struct living *l);
float living_eye_height(struct living *l);
void living_set_size(struct living *l, float w, float h);
void living_move(struct living *l, double dx, double dy, double dz);

/* The spawner lane's squid and bat: the pieces their chains reach into. */
int living_handle_material_acceleration(struct world *w, struct aabb box, double *mx, double *my, double *mz);
int living_is_pushed_by_water(struct living *l);
int living_closest_player_within(struct living *l, double maxd);
double living_player_distance_squared(struct living *l);
void living_on_living_update(struct living *l, det_state *det);
void living_move_entity_with_heading(struct living *l, float strafe, float forward, det_state *det);
void living_despawn_entity(struct living *l);
int living_is_movement_blocked(struct living *l);
void living_set_jumping(struct living *l, int jumping);
void living_default_on_living_update(struct living *l, det_state *det);
void living_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
void living_drop_equipment(struct living *l, int hit_by_player, int looting, det_state *det);
int living_can_be_pushed(struct living *l);
int living_is_ai_enabled(struct living *l);
float living_get_ai_move_speed(struct living *l);
void living_set_ai_move_speed(struct living *l, float v);
void living_update_ai_tasks(struct living *l, det_state *det);
void living_update_entity_action_state(struct living *l);
/* EntityFlying.moveEntityWithHeading: the air path, moveFlying then moveEntity
 * with the 0.91 drag and no gravity. */
void living_move_entity_with_heading_flying(struct living *l, float strafe, float forward);
void living_face_entity(struct living *l, struct living *other, float yaw_speed, float pitch_speed);
float living_update_rotation(float cur, float want, float max_inc);
int living_max_safe_point_tries(struct living *l);
/* EntityLivingBase.fall: the body a kind's own kind_fall hook runs first. */
void living_fall(struct living *l, float distance);
void living_set_revenge_target(struct living *l, struct living *target);
/* EntityLivingBase.onUpdate's arrowHitTimer */
void living_arrow_decay(struct living *l);
/* EntityZombie.startConversion (the golden apple's cure). */
void zombie_start_conversion(struct living *l, int ticks, det_state *det);
void living_dismount(struct living *l);
void living_mount(struct living *l, struct living *vehicle);
void living_update_rotation_head(struct living *l);
void living_despawn(struct living *l);
int living_is_inside_of_material(struct living *l, int material);
int living_is_on_ladder(struct living *l);
void living_mark_attacked(struct living *l);
int living_attack_entity_from_attacker(struct living *l, struct living *attacker, int source, float amount, det_state *det);
/* The same without the kind dispatch: what a kind's attackEntityFrom override
 * calls for its super. */
int living_attack_entity_from_attacker_base(struct living *l, struct living *attacker, int source, float amount,
                                            det_state *det);

void living_write_nbt(struct living *l, struct nbt *tag);
/* living_write_nbt through a writer (nbtw.h): a tree or the digest's bytes. */
struct nbtw;
void living_write_w(struct living *l, struct nbtw *w);
uint64_t living_nbt_hash(struct living *l);
const char *living_kind_name(int kind);
void living_nbt_text(struct living *l, char **out);
uint64_t item_entity_nbt_hash(ie_ent *en);
void item_entity_nbt_text(ie_ent *en, char **out);
void an_write_state(struct an_world *an, const struct an_ent *en, unsigned char *rec);
#define AN_STATE_BYTES 272
void living_init(struct living *l, struct world *w, int kind, det_state *det);
void item_entity_write_nbt(ie_ent *en, struct nbt *tag);

/* The protected EntityLivingBase bodies the ghast's own paths run, exposed for
 * ghasts.c: Entity.damageEntity's tail (the armour/potion/absorption half),
 * Entity.setBeenAttacked, EntityLiving.despawnEntity, and the old-AI idle
 * sound roll the ghast's onEntityUpdate carries. */
void living_damage_entity_pub(struct living *l, int source, float amount);
void living_set_been_attacked_pub(struct living *l);
void living_despawn_entity_pub(struct living *l);
int living_dmg_unblockable(int source);
void living_apply_entity_collision_pub(struct living *l, struct living *other);

/* The canonical-NBT helpers the kind writers share. */
struct nbt *nbt_double_list3(double a, double b, double c);
struct nbt *nbt_float_list2(float a, float b);
uint64_t fnv_text(const char *text);

/* ai.c */
void ai_setup_kind(struct living *l, det_state *det);
void ai_tasks_update(struct living *l, struct ai_tasks *t);
void body_helper_update(struct living *l);
void look_helper_update(struct living *l);
void move_helper_update(struct living *l);
void jump_helper_do(struct living *l);
void senses_clear(struct living *l);
int senses_can_see(struct living *l, struct living *other);
void nav_update(struct living *l);
int nav_try_move_to_xyz(struct living *l, double x, double y, double z, double speed);
int nav_try_move_to_entity(struct living *l, struct living *other, double speed);
int nav_no_path(struct living *l);
/* PathNavigate.setPath with a PathEntity another navigator holds: the one
 * object is shared, as EntityZombie.onLivingUpdate hands its path to the
 * chicken it rides. */
int nav_set_path_shared(struct living *l, pathref p, double speed);
void nav_clear_path(struct living *l);

/* villagers.c */
void villager_construct(struct living *l, det_state *det);
void villager_on_spawn_with_egg(struct living *l, det_state *det);
void villager_update_ai_tick(struct living *l, det_state *det);
void villager_on_living_update(struct living *l, det_state *det);
void villager_revenge_village(struct living *l, struct living *target);
void villager_death_village(struct living *l, struct living *attacker);
void iron_golem_death_village(struct living *l);

void iron_golem_construct(struct living *l, det_state *det);
void iron_golem_on_living_update(struct living *l, det_state *det);
void iron_golem_update_ai_tick(struct living *l, det_state *det);
int iron_golem_attack_entity_as_mob(struct living *l, struct living *target, det_state *det);
/* IMob: the kinds IMob.mobSelector accepts */
int ai_is_imob_kind(int kind);

/* animals.c */
void animal_construct(struct living *l, det_state *det);
void animal_eat_grass_bonus(struct living *l);
void animal_on_spawn_with_egg(struct living *l, det_state *det);
struct living *animal_create_child(struct living *l, struct living *mate, det_state *det);
void animal_kind_tick(struct living *l, det_state *det);
struct nbtw;
void animal_write_kind_nbt(struct living *l, struct nbtw *w);
void animal_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
float animal_get_block_path_weight(struct living *l, int x, int y, int z);

/* The world helpers living.c shares with the animal kinds. */
int living_world_full_block_light_value(struct world *w, int subtracted, int x, int y, int z);
float living_light_brightness(struct world *w, int subtracted, int x, int y, int z);
int living_mob_loot(struct world *w);
int living_is_client_world(struct living *l);
/* !worldObj.isClient && worldObj.difficultySetting == PEACEFUL: the check
 * EntityMob, EntitySlime and EntityGhast remove themselves on. */
int living_server_peaceful(struct living *l);
int living_is_burning(struct living *l);
ie_ent *an_spawn_orb(struct an_world *an, int xp, double x, double y, double z);
void an_adopt_projectile(struct an_world *an, ie_ent *ie);
/* EntitySmallFireball.onImpact on a living or the player (the an_world's
 * default impact handler) */
struct ie_ent;
void an_fireball_impact(struct ie_world *iew, struct ie_ent *en, void *hit, int hit_kind);
ie_ent *an_spawn_arrow(struct an_world *an, struct living *shooter, struct living *target, float speed, float inaccuracy);
int living_is_movement_blocked(struct living *l);
int living_handle_lava_movement(struct living *l);
int living_is_in_water(struct living *l);
/* Entity.isWet: inWater, or an open column at the feet or the head in rain
 * (the probe's world never rains, so the two canLightningStrikeAt terms are 0) */
int living_is_wet(struct living *l);
/* World.isAnyLiquid over a box. */
int living_world_is_any_liquid(struct world *w, struct aabb box);
void living_set_fire(struct living *l, int seconds);
void living_heal(struct living *l, float v);
void living_damage_entity_apply(struct living *l, int source, float amount, det_state *det);
void living_damage_entity(struct living *l, int source, float amount, det_state *det);
void living_set_absorption(struct living *l, float v);
void living_set_invisible(struct living *l, int inv);
int living_is_invisible(const struct living *l);
ie_ent *an_spawn_potion(struct an_world *an, int spawn_index, double x, double y, double z,
                        double mx, double my, double mz, int damage);

/* EntityBlaze */
void blaze_construct(struct living *l, det_state *det);
void blaze_on_living_update(struct living *l, det_state *det);
void blaze_update_entity_action_state(struct living *l, det_state *det);
void blaze_fall(struct living *l, float dist);
void blaze_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det);
void blaze_set_on_fire(struct living *l, int on);
int blaze_is_on_fire(const struct living *l);

#endif
