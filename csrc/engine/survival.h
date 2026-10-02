/* The player survival state, ported from FoodStats, the EntityPlayer /
 * EntityPlayerMP damage and death chain, EntityLivingBase's base tick, item
 * and orb pickup, EntityPlayer.dropOneItem and the eating flow. See the
 * report for the boundary. The structs ride inside client_player and
 * server_player; the tick entry points in player.c call into this file at the
 * spots the vanilla chain runs the survival code.
 */
#ifndef NETHERITE_SURVIVAL_H
#define NETHERITE_SURVIVAL_H

#include "itemtag.h"
#include <stddef.h>
#include <stdint.h>

#include "det.h"
#include "particles_live.h"
#include "potion.h"
#include "trades.h"

struct act;
struct client_player;
struct particles_live;
struct server_player;
struct ie_world;
struct ie_ent;
struct an_world;
struct world;
struct s2c_pkt;
struct chat_out;

/* The damage sources the player can take, in DamageSource order. */
enum
{
    SURV_IN_FIRE = 0, SURV_ON_FIRE, SURV_LAVA, SURV_IN_WALL, SURV_DROWN,
    SURV_STARVE, SURV_CACTUS, SURV_FALL, SURV_OUT_OF_WORLD, SURV_GENERIC,
    SURV_MOB, SURV_EXPLOSION, SURV_MAGIC,
    /* the EntityDamageSourceIndirect kinds (projectiles: blockable, not
     * difficulty scaled) and causeThornsDamage (magic, blockable, scaled
     * when the wearer is a mob) */
    SURV_ARROW, SURV_THROWN, SURV_FIREBALL, SURV_THORNS,
    /* DamageSource.anvil: blockable, not scaled, the helmet's share */
    SURV_ANVIL, SURV_KINDS
};

/* EntityPlayer.damageEntity's hunger cost per source (DamageSource.hungerDamage,
 * 0.3 for every source this module reaches). */
#define SURV_HUNGER_DAMAGE 0.3F

/* FoodStats, one per player. */
struct surv_food
{
    int level;              /* foodLevel */
    float saturation;       /* foodSaturationLevel */
    float exhaustion;       /* foodExhaustionLevel */
    int timer;              /* foodTimer */
};

/* One ItemStack, as the inventory and the eating flow need it. gen counts the
 * writes that replaced the stack object: itemInUse holds a pointer, so a gen
 * bump is what clears it. count 0 is an empty slot. tag is the stack's tag
 * compound in the item tag store (itemtag.h), 0 for none: every write that
 * sets item, damage and count sets it too (surv_stack_set), so a slot never
 * keeps the tag of the stack it held before. */
struct surv_stack
{
    int item, damage, count;
    int gen;
    int tag;
};

/* EnchantmentHelper.getEnchantmentLevel(id, stack): the first entry's level,
 * 0 for an empty stack or none. */
int surv_ench_level(const struct surv_stack *st, int id);

/* EntityPlayerSP.onLivingUpdate's screen close in a portal. */
struct client_player;
void surv_client_portal_close(struct client_player *p);

struct server_player;

/* ItemStack.damageItem(amount, player): unbreaking, the break effect, the
 * break stat. 1 when the stack broke (its count went down by one). */
int surv_damage_stack(struct server_player *p, struct surv_stack *st, int amount);
void surv_destroy_current_item(struct server_player *p);

/* Item.getMaxItemUseDuration of a stack (0 for an empty one). */
int surv_item_use_duration(const struct surv_stack *st);

/* the S08s one pump answers (a volley of ender pearls landing in one tick
 * sends one each) */
#define S08_CONFIRM_MAX 16

/* The C03 family as sendMotionUpdates builds it. */
enum { C03_STOP = 1, C03_POS = 2, C03_LOOK = 3, C03_POSLOOK = 4 };

struct c03
{
    int kind;
    /* C03PacketPlayer field names: c = x, d = minY, e = z, f = posY (stance). */
    double c, d, e, f;
    float yaw, pitch;
    int on_ground;
};

/* One packet of the client tick's input block (Minecraft.runTick's
 * consumers), as the client sent it. */
enum
{
    CPK_C09 = 1,        /* syncCurrentPlayItem inside a consumer: v the slot */
    CPK_C16,            /* the inventory key's C16: v the state */
    CPK_C07,            /* v the status; x y z side for the dig statuses */
    CPK_C08,            /* v 1 the in-air (food) form, 2 the block form, 3
                         * the block form with an empty hand; x y z side and
                         * the hit vector ride along; stack is the held
                         * stack the packet carries: the client's copy after
                         * onBlockActivated, before the placement */
    CPK_C02_ATTACK,     /* C02 USE_ENTITY ATTACK: v the entity id */
    CPK_C02_USE,        /* C02 USE_ENTITY INTERACT: v the entity id */
};

struct client_pkt
{
    int kind;
    int v;
    int x, y, z, side;
    float hx, hy, hz;
    struct surv_stack stack;
};

/* Java's send queue grows; one tick of an agent's press counts stays far
 * under this, and more stops the process rather than dropping a packet. */
#define CLIENT_PKT_CAP 256

/* What one client tick sent the server, in arrival order: the C09 hotbar sync
 * (updateController), the pump's C06 confirms, the gui ops (the respawn C16,
 * the C0E clicks, the C17, the C0D), then the input block's packets in the
 * order its consumers sent them (pkt), then the C0B actions and the C03
 * (sendMotionUpdates, inside the client's world tick). */
struct craft_stack;
struct client_out
{
    int has_confirm_c03;                /* how many: one per S08 the pump handled */
    struct c03 confirm_c03[S08_CONFIRM_MAX]; /* S08's immediate C06 acknowledgements */
    int has_confirm2_c03;               /* a second S08 processed by the same
                                         * pump (the tp's S08 and the % 20
                                         * keepalive S08 arrive together) */
    struct c03 confirm2_c03;
    int c09_slot;                       /* updateController's C09; -1: none this tick */
    int c16;                            /* the gui ops' C16 PERFORM_RESPAWN (1), 0 none */
    int c0d;                            /* a C0D no act op made (GuiContainer.updateScreen's close for a dead player) */
    int nc0a;                           /* C0A swing animations */
    int c0d_close;                      /* GuiContainer.updateScreen's closeScreen C0D
                                         * for a dead player */
    int c0b_wake;                      /* GuiSleepMP's C0B action 3, sent from the
                                         * screen's input ahead of the tick's
                                         * own C0B actions and C03 */
    int nc0b, c0b[4];
    int has_c03;
    struct c03 c03;
    int ntrsel;                         /* the C17 MC|TrSels, each after the */
    struct { int window, index, at; } trsel[8];   /* row's first `at` clicks */
    /* the riding player's C0C after its C05 (EntityClientPlayerMP.onUpdate) */
    int has_c0c, c0c_jump, c0c_sneak;
    float c0c_strafe, c0c_forward;
    /* each of the row's C0E window clicks: whether the client sent it and
     * the stack its own slotClick returned, which the packet carries
     * (the client player's arrays, for this tick) */
    const unsigned char *click_sent;
    const struct craft_stack *click_ret;
    /* the chat screen's C01s and C14s, in send order (the client player's
     * outbox, for this tick) */
    const struct chat_out *chat;
    /* the input block's packets in send order (last: a new tick clears the
     * fields above it and only resets npkt) */
    int npkt;
    struct client_pkt pkt[CLIENT_PKT_CAP];
};

/* Appends one input-block packet (cleared, kind set) to the tick's list. */
struct client_pkt *client_pkt_add(struct client_out *out, int kind);

/* The survival state both players carry. The client mirrors what S06, S1F and
 * the S2F slot packets told it; the server owns the truth. */
/* ------------------------------------------------------------ achievements
 *
 * The StatisticsFile the 1.7.10 server keeps per player (out of player NBT,
 * in stats/<uuid>.json) and the StatList stats its S37 statistics packets
 * name. The native store is one counter per stat id in the enum below (the
 * per-id tables addressed through stats_table.h, so replaceAllSimilarBlocks
 * and the null entries are the registry's), with the map's entry set, the
 * dirty set field_150888_e and the exploreAllBiomes progress set, mirroring
 * StatFileWriter/StatisticsFile: an achievement counts only when its parent
 * chain is unlocked; every accepted add marks its stat dirty; the 0 -> n move
 * of an achievement sets field_150886_g and EntityPlayerMP.addStat flushes an
 * S37 of the whole dirty set right there. Nothing else sends one on these
 * tapes (the C16 REQUEST_STATS comes from GuiStats only). The client mirror
 * (the client's own StatFileWriter, fed by handleStatistics) drives the
 * GuiAchievement toast.
 */
enum
{
    ACH_OPEN_INVENTORY = 0,     /* independent */
    ACH_MINE_WOOD,              /* parent openInventory */
    ACH_BUILD_WORK_BENCH,       /* mineWood */
    ACH_BUILD_PICKAXE,          /* buildWorkBench */
    ACH_BUILD_FURNACE,          /* buildPickaxe */
    ACH_ACQUIRE_IRON,           /* buildFurnace */
    ACH_BUILD_HOE,              /* buildWorkBench */
    ACH_MAKE_BREAD,             /* buildHoe */
    ACH_BAKE_CAKE,              /* buildHoe */
    ACH_BUILD_BETTER_PICKAXE,   /* buildPickaxe */
    ACH_COOK_FISH,              /* buildFurnace */
    ACH_ON_A_RAIL,              /* acquireIron */
    ACH_BUILD_SWORD,            /* buildWorkBench */
    ACH_KILL_ENEMY,             /* buildSword */
    ACH_KILL_COW,               /* buildSword */
    ACH_FLY_PIG,                /* killCow */
    ACH_SNIPE_SKELETON,         /* killEnemy */
    ACH_DIAMONDS,               /* acquireIron */
    ACH_DIAMONDS_TO_YOU,        /* diamonds */
    ACH_PORTAL,                 /* diamonds */
    ACH_GHAST,                  /* portal */
    ACH_BLAZE_ROD,              /* portal */
    ACH_POTION,                 /* blazeRod */
    ACH_THE_END,                /* blazeRod */
    ACH_THE_END2,               /* theEnd */
    ACH_ENCHANTMENTS,           /* diamonds */
    ACH_OVERKILL,               /* enchantments */
    ACH_BOOKCASE,               /* enchantments */
    ACH_BREED_COW,              /* killCow */
    ACH_SPAWN_WITHER,           /* theEnd2 */
    ACH_KILL_WITHER,            /* spawnWither */
    ACH_FULL_BEACON,            /* killWither */
    ACH_EXPLORE_ALL_BIOMES,     /* theEnd */
    ACH_COUNT
};

/* The achievement's parent (the chain canUnlockAchievement walks), -1
 * independent. Order and parents are AchievementList's registerStat calls
 * (stats_table.h). */
#define ACH_PARENT(a) ((int)(ach_parent[(a)]))
extern const int8_t ach_parent[ACH_COUNT];

/* EntityList.entityEggs in its iteration order (stats_table.h): each egg
 * registers stat.killEntity.<name> and stat.entityKilledBy.<name>. Wolf,
 * ocelot and horse are switched off and never reach a kill path here. */
enum
{
    EGG_CREEPER = 0, EGG_SKELETON, EGG_SPIDER, EGG_ZOMBIE, EGG_SLIME,
    EGG_GHAST, EGG_PIG_ZOMBIE, EGG_ENDERMAN, EGG_CAVE_SPIDER, EGG_SILVERFISH,
    EGG_BLAZE, EGG_MAGMA_CUBE, EGG_BAT, EGG_WITCH,
    EGG_PIG, EGG_SHEEP, EGG_COW, EGG_CHICKEN, EGG_SQUID, EGG_WOLF,
    EGG_MOOSHROOM, EGG_OCELOT, EGG_HORSE, EGG_VILLAGER,
    EGG_COUNT
};

/* The entity-list kind (living.h) per egg entry, -1 for none. */
extern const int8_t egg_kind[EGG_COUNT];

/* Every stat id the store keeps: the achievements 0..ACH_COUNT-1, then
 * StatList.generalStats in registration order, then the two per-egg stats,
 * then the per-id tables (block or item id added; an id whose array entry is
 * null or aliased goes through stats_table.h first). */
enum
{
    STAT_LEAVE_GAME = ACH_COUNT,
    STAT_MINUTES_PLAYED,
    STAT_DISTANCE_WALKED,
    STAT_DISTANCE_SWUM,
    STAT_DISTANCE_FALLEN,
    STAT_DISTANCE_CLIMBED,
    STAT_DISTANCE_FLOWN,
    STAT_DISTANCE_DOVE,
    STAT_DISTANCE_MINECART,
    STAT_DISTANCE_BOAT,
    STAT_DISTANCE_PIG,
    STAT_DISTANCE_HORSE,
    STAT_JUMP,
    STAT_DROP,
    STAT_DAMAGE_DEALT,
    STAT_DAMAGE_TAKEN,
    STAT_DEATHS,
    STAT_MOB_KILLS,
    STAT_ANIMALS_BRED,
    STAT_PLAYER_KILLS,
    STAT_FISH_CAUGHT,
    STAT_JUNK_FISHED,
    STAT_TREASURE_FISHED,
    STAT_KILL_ENTITY,               /* + egg index */
    STAT_KILL_ENTITY_LAST = STAT_KILL_ENTITY + EGG_COUNT - 1,
    STAT_ENTITY_KILLED_BY,          /* + egg index */
    STAT_ENTITY_KILLED_BY_LAST = STAT_ENTITY_KILLED_BY + EGG_COUNT - 1,
    STAT_MINE_BLOCK,                /* + block id (4096) */
    STAT_MINE_BLOCK_LAST = STAT_MINE_BLOCK + 4095,
    STAT_USE_ITEM,                  /* + item id (4096) */
    STAT_USE_ITEM_LAST = STAT_USE_ITEM + 4095,
    STAT_CRAFT_ITEM,                /* + item id (4096) */
    STAT_CRAFT_ITEM_LAST = STAT_CRAFT_ITEM + 4095,
    STAT_BREAK_ITEM,                /* + item id (4096) */
    STAT_BREAK_ITEM_LAST = STAT_BREAK_ITEM + 4095,
    STAT_COUNT
};

#define STAT_BITS ((STAT_COUNT + 7) / 8)
#define STAT_BIT(set, s) (((set)[(s) >> 3] >> ((s) & 7)) & 1)
#define STAT_SET(set, s) ((set)[(s) >> 3] |= (uint8_t)(1u << ((s) & 7)))
#define STAT_CLR(set, s) ((set)[(s) >> 3] &= (uint8_t)~(1u << ((s) & 7)))

/* One CombatEntry as the death reads it: the attacker kind (-1 no living
 * attacker, SK_PLAYER a player), the damage, the source's damage type
 * (chatmsg.h DT_*) and entity name (func_151522_h, -1 none), the fighter's
 * fall distance (field_94564_f) and surroundings label (field_94566_e,
 * chatmsg.h CM_LABEL_*) at the hit. */
struct surv_combat_entry
{
    int16_t kind;
    float damage;
    int8_t dtype;
    int16_t name;
    float fall;
    int8_t label;
};

/* EntityPlayerMP's CombatTracker. Java keeps every entry since the last
 * clear (func_94549_h) and reads them only at death; what it reads is kept
 * here as each entry lands, so there is no entry cap: the last entry
 * (func_151521_b), func_94544_f's longest fall and the entry it answers
 * (the one before the fall, the fall itself when it is the first), and
 * func_94550_c's best living and best player by damage (first of equals),
 * once over attacker kinds (func_94060_bK's killer) and once over entity
 * names (the death message's killer). n
 * counts the entries (saturating; the death reads only whether it is 0);
 * the running answers hold while n > 0 (combat_entry starts them afresh at
 * the first entry after a clear); the last entry's tick and the two flags
 * func_94549_h resets on. */
struct surv_combat
{
    int n;
    struct surv_combat_entry last;
    struct surv_combat_entry fall_entry;    /* func_94544_f's var1 */
    float fall_len;                     /* its var4 */
    int has_fall;                       /* var1 != null */
    float kd_best, kd_player;           /* func_94550_c over kinds: var3, var4 */
    int16_t k_best, k_player;           /* the kinds, -1 none */
    float nd_best, nd_player;           /* func_94550_c over names */
    int16_t n_best, n_player;           /* the names, -1 none */
    int cur_dtype, cur_name;            /* the hit being dealt (surv_server_damage_by) */
    int revenge_name;                   /* the revenge target's name id */
    int last_tick;                      /* field_94555_c */
    int in_combat;                      /* field_94552_d */
    int active;                         /* field_94553_e */
    int revenge_kind;                   /* entityLivingToAttack's kind, -1 null */
    uint64_t revenge_ref;               /* its living (living.h lref), 0 unknown */
    int revenge_timer;                  /* revengeTimer */
};

struct surv_stats
{
    /* StatFileWriter.field_150875_a: the value per stat and whether the map
     * holds an entry (an entry can hold 0) */
    int32_t v[STAT_COUNT];
    uint8_t present[STAT_BITS];

    /* StatisticsFile.field_150888_e (the dirty set), field_150886_g (an
     * achievement moved 0 -> n) and field_150885_f (the tick counter of the
     * last send, -300 fresh) */
    uint8_t dirty[STAT_BITS];
    int ach_dirty;
    int last_send_tick;

    /* exploreAllBiomes' JsonSerializableSet of biome names, one bit per
     * biome id (the ids a set member can come from have distinct names);
     * has_progress once func_150872_a made the entry */
    uint8_t explored[32];
    int has_progress;

    /* the client's StatFileWriter mirror, everything the S37s delivered;
     * client_writes counts the writes to it (the snapshot's load, each S37
     * entry): the pool's step skips its compare while the count stands */
    int32_t client_v[STAT_COUNT];
    uint8_t client_present[STAT_BITS];
    uint32_t client_writes;
    int has_stats;                      /* NetHandlerPlayClient.field_147308_k */
    int show_hint;                      /* gameSettings.showInventoryAchievementHint */

    /* GuiAchievement's state, the client side only. ach -1 when none. */
    int toast_ach;
    long long toast_l;
    int toast_desc;
    const char *toast_title;
    const char *toast_sub;
};

/* The S37's map: the (stat, value) pairs of one send, held out of the packet
 * (a packet is copied whole with its queue every tick) in the survival
 * state's ring; the packet carries the slot and the send's sequence. */
#define SURV_S37_MAX 1024
/* The PlayerControllerMP dig state machine, the client's copy (survival.c). */
struct surv_dig_state
{
    int is_hitting;
    int cur_x, cur_y, cur_z;
    int held_item, held_damage, held_tag;   /* currentItemHittingBlock */
    int hit_delay;
    float cur_damage;
    float step_counter;
};

#define SURV_S37_RING 16
struct surv_s37
{
    unsigned seq;
    int n;
    int id[SURV_S37_MAX];
    int val[SURV_S37_MAX];
};

/* StatFileWriter.func_150871_b + StatisticsFile.func_150873_a on the server
 * player: the id through the registry tables, the achievement's parent gate,
 * the store, the dirty mark and the 0 -> n flag, then EntityPlayerMP.addStat's
 * tail: the S37 of the dirty set when the flag is up. */
void surv_add_stat(struct server_player *p, int stat, int n);
/* StatisticsFile.func_150876_a: one S37 of the dirty set (or an empty map),
 * with the 300-tick gate. The tick is the server's own counter
 * (MinecraftServer.getTickCounter). */
void surv_stats_flush(struct server_player *p, int server_tick);
/* StatisticsFile.func_150877_d then func_150884_b at join: every loaded stat
 * dirty, then all unlocked achievements sent and taken out of the set. */
void surv_stats_login(struct server_player *p);
/* The client half: handleStatistics on the S37 (the mirror, the toast queue
 * and field_147308_k). */
void surv_client_apply_s37(struct client_player *p, const struct s2c_pkt *pkt);
/* The GuiAchievement hint a snapshot's client shows from its join: the
 * join's S37 (already in the mirror) held no achievement and the hint is on,
 * so handleStatistics put up "Press 'E' to open your inventory." with its
 * clock at the join (JOIN_MS, the getSystemTime of that S37) plus 2500; the
 * snapshot carries no toast, so the playable client sets it after the load.
 * Nothing when the mirror holds any achievement (an unlock replaced it). */
void surv_client_join_hint(struct client_player *p, long long join_ms);
/* The toast's draw state at frame time: sets *on (func_146254_a's gate) and
 * clears the clock on the expiry return, exactly as the draw does. */
void surv_client_toast_json(struct client_player *p, long long now_ms, int *on);

/* The stat id of one mineBlock/useItem/breakItem/craftItem counter, before
 * the registry lookup surv_add_stat does. */
#define SURV_STAT_MINE(block) (STAT_MINE_BLOCK + (block))
#define SURV_STAT_USE(item) (STAT_USE_ITEM + (item))
#define SURV_STAT_CRAFT(item) (STAT_CRAFT_ITEM + (item))
#define SURV_STAT_BREAK(item) (STAT_BREAK_ITEM + (item))

/* The vanilla statId of a store id ("stat.mineBlock.3"), and back (-1 for a
 * name StatList.func_151177_a does not know). */
const char *surv_stat_name(int stat, char *buf, size_t n);
int surv_stat_by_name(const char *name);

/* StatisticsFile.func_150881_a over a stats/<uuid>.json text into the server
 * player's store (the map replaced; unknown names skipped), and the snapshot's
 * stats.json (the file, the dirty set, the send clock relative to the tick
 * counter, the client mirror). 0 on success. */
int surv_stats_load_file(struct surv_stats *st, const char *json_text);
int surv_stats_load_snapshot(struct server_player *sp, struct client_player *cp,
                             const char *json_text, int server_tick);
/* The store against one line of the oracle's stats.jsonl (Stats.java): the
 * file (values, entries, progress), the dirty set, the send clock relative to
 * the tick counter, field_150886_g and the client mirror. 0 when equal, else
 * 1 with the first difference in why. */
int surv_stats_compare(const struct server_player *sp, const struct client_player *cp,
                       const char *json_line, int server_tick, char *why, size_t n);

/* EntityPlayer.onKillEntity's IMob test and EntityList's egg lookup, for the
 * kill path in serverreplay.c. */
int surv_is_imob(int kind);
int surv_egg_of_kind(int kind);

/* EntityAnimal.func_146083_cb's player credited through EntityAIMate.spawnBaby:
 * animalsBred, and breedCow for a cow or mooshroom. */
void surv_bred(struct server_player *p, int kind);
/* EntitySkeleton.onDeath's snipeSkeleton: the arrow's shooter at 50 blocks
 * or more (horizontal) from the skeleton. */
void surv_skeleton_shot(struct server_player *p, double sx, double sz);
/* The attacker of the next surv_server_damage* call (a living kind, SK_PLAYER
 * or -1), for the CombatTracker entry and the revenge target. */
void surv_set_attacker(int kind);
/* The same attacker's living (a living.h lref, 0 when it has none in the
 * pool): the revenge target whose death clears it. */
void surv_set_attacker_ref(uint64_t ref);
/* The next hit's DamageSource family (living.h DMG_*, -1 from the survival
 * source) and the name of the entity it carries (chatmsg.h; -1 from the
 * attacker kind), for the CombatEntry the death message reads. */
void surv_set_attacker_msg(int dmg, int name);
/* An achievement's statId (achievement.<name>), its en_US name for that key
 * (NULL for any other key) and Achievement.getSpecial. */
const char *surv_ach_id(int a);
/* the achievement of a statId (-1 none), its en_US name and description
 * (the hint's key name in) */
int surv_ach_by_id(const char *id);
const char *surv_ach_name(int a);
const char *surv_ach_desc(int a);
const char *surv_ach_lang(const char *key);
int surv_ach_special(int a);
/* A living attacker with no egg entry (the dragon, the iron golem). */
#define SURV_ATTACKER_NO_EGG 1000

/* AchievementList's theItemStack per achievement (the toast's icon). */
typedef struct { int16_t item; int16_t meta; } surv_ach_item;
extern const surv_ach_item ach_item[ACH_COUNT];

struct surv_state
{
    struct surv_food food;
    /* the per-player StatisticsFile (the server's truth, the client's
     * mirror), out of player NBT exactly as the oracle keeps it */
    struct surv_stats stats;
    struct surv_combat combat;          /* the server player's only */
    int ticks_existed;                  /* Entity.ticksExisted, the server player's */
    float health;                       /* data watcher 6 */
    int air;                            /* data watcher 1 */
    int fire;                           /* data watcher 0's burning bit: Entity.onEntityUpdate's setFlag(0, fire > 0) */
    float absorption;                   /* the absorption attribute, 0 without potions */

    int hurt_time, max_hurt_time;
    int attack_time;                   /* EntityLivingBase.attackTime */
    int max_hurt_resistant;
    int hurt_resistant_time;
    float last_damage;                  /* EntityLivingBase.lastDamage */
    int velocity_changed;               /* Entity.velocityChanged: setBeenAttacked
                                         * on a landed hit; the tracker answers
                                         * with an S12 the client pump applies */
    int death_time;
    int recently_hit;
    int xp_cooldown;                    /* EntityPlayer.xpCooldown */

    /* Entity.isDead: onDeathUpdate's setDead, and onEntityUpdate's void
     * death that no damage pipeline reports (no death message reaches the
     * rows) */
    int dead;
    int removed;                        /* World.updateEntities dropped the
                                         * dead player from loadedEntityList */

    int xp_level, xp_total;
    int score;                       /* EntityPlayer data watcher 18 */
    float xp_progress;

    /* itemInUse: the slot and the stack generation it was taken from, with the
     * remaining count; count 0 means not using. */
    int using_count, using_slot, using_gen;
    int gen_counter;                    /* the write count itemInUse's identity tracks */

    struct surv_stack inv[40];          /* mainInventory 0..35, armor 36..39 */
    struct surv_stack ender[27];        /* InventoryEnderChest, slots 0..26 */

    int current_item;                   /* the server's inventory.currentItem */

    /* EntityPlayerMP: the respawn invulnerability and the handler's keep-alive
     * counter, plus the S06/S1F mirrors that decide when they are sent. */
    int respawn_protect;                /* field_147101_bU */
    int net_tick_count;
    int has_set_health;                 /* the client player's gate */
    float last_health;                  /* -1e8 fresh, -1 after a respawn */
    float hud_prev_health;              /* EntityLivingBase.prevHealth for the flash */
    int hud_prev_food;                  /* FoodStats.prevFoodLevel */
    int last_food;                      /* -99999999 fresh */
    int was_hungry;
    int last_xp_total;                  /* -99999999 fresh */

    /* the entity's own Random, seeded by Det.newRandom at construction; the
     * snapshot does not carry the state, and every path that draws from it
     * here feeds nothing the rows read */
    det_rng erand;

    /* the bed this player respawns at, from its NBT (SpawnX/Y/Z, SpawnForced) */
    int has_spawn;
    int spawn_x, spawn_y, spawn_z, spawn_forced;
    /* EntityPlayer.sleeping, sleepTimer and playerLocation (the bed the
     * player last slept in; has_bed 0 is null) */
    int sleeping, sleep_timer;
    int has_bed, bed_x, bed_y, bed_z;

    /* the container's slot mirror for detectAndSendChanges: the player
     * container's 45 slots (0 the craft result, 1-4 the grid, 5-8 armor,
     * 9-44 main 0-35) */
    struct surv_stack mirror[45];
    /* the inventory container's own list while another window is open
     * (EntityPlayerMP.onUpdate detects only openContainer): the window's
     * close brings it back as it was, and the next detect sends what
     * changed since (a death's emptied inventory) */
    struct surv_stack mirror0[45];
    int mirror0_valid;

    /* InventoryPlayer.getItemStack, the cursor (the client's own copy; the
     * server's lives in its open container) */
    struct surv_stack cursor;

    /* The client player's potion mirror: the EntityClientPlayerMP side of
     * activePotionsMap, driven by the S1D / S1E packets the pump applies. The
     * server's own copy stays empty (the server's effects live on the
     * replay's player twin); it is memset with the rest of surv_state. */
    struct potion_map potions;
    uint8_t potions_need_update;
    int potion_liquid_color;
    uint8_t potion_is_ambient;
    /* the S1D nodes the pump added this row: Java drains the packets after
     * the world tick (Minecraft.runTick: updateEntities then
     * processReceivedPackets), so a just-arrived effect does not perform,
     * decrement or color until the next tick */
    uint8_t potion_fresh;
};

/* The s2c packets the survival flow queues, in send order: S06 (health, food,
 * saturation), S1F (experience), S07 (the respawn's world switch), S19 (the
 * eat finish's entity status), S2F (one container slot), S08 (the respawn's
 * position). The S20 and the movement-side S08 corrections keep the dedicated
 * pending fields player.c already applies, at the same pump. */
enum { PK_S06 = 1, PK_S1F, PK_S07, PK_S19, PK_S2F, PK_S08, PK_S12, PK_S2D, PK_S27, PK_S1C,
       PK_S0A, PK_S0B, PK_S1D, PK_S1E, PK_S30, PK_S3F, PK_S2B, PK_S2E, PK_S37, PK_S31,
       PK_S1B /* the player's own mount: i0 the vehicle's id, -1 none */,
       PK_S09 /* the held slot (syncPlayerInventory): i0 */,
       PK_S02 /* a chat line: its JSON and parts out of line (chatcomp.h), i0 the part count */,
       PK_S1C_FLAGS /* the player's own data watcher 0 at a tracker pass that changed it: i0 the byte */,
       PK_S3A /* a tab completion: i0 strings out of line, NUL after each (chatcmd.c) */,
       PK_S1C_ABS /* the player's own data watcher 17 (the absorption) at a tracker pass that changed it: f0 */,
       PK_S28 /* an effect: i0 the type, i1 the data, f0..f2 the block */,
       PK_S2C /* a lightning bolt (S2CPacketSpawnGlobalEntity): f0..f2 its position */,
       PK_S24 /* a block event: i0 the block, i1 the event, i2 its parameter, f0..f2 the block */,
       PK_S1C_POT /* the player's own data watcher 7 and 8 (the potion colour, ambient) at a tracker pass that changed them: i0, i1 */ };

/* An S3F MC|TrList recipe, out of line in its queue's side store. */
struct s2c_trade
{
    short item; unsigned char count; short damage; int has_buy_b;
    short item_b; unsigned char count_b; short damage_b;
    short sell_item; unsigned char sell_count; short sell_damage;
    int tag, tag_b, sell_tag;   /* the stacks' tags (itemtag.h) */
    int disabled;               /* the list's isRecipeDisabled boolean */
};

/* An S27's explosion (its out-of-line part): the center (the integrated
 * server passes the packet object itself, so the doubles arrive whole), the
 * strength and the affected positions' count (0 when not smoking). */
struct s2c_s27
{
    double x, y, z;
    float size;
    int naffected;
    int nxp;
    struct s27_xp xp[S27_XP_MAX];
};

struct s2c_pkt
{
    int kind;
    int window;                 /* nonzero for an open window's own S2F slots */
    /* doubles: the S08's position fields are Java doubles; the S06/S1F
     * floats land here exactly (float -> double is lossless) */
    double f0, f1, f2, f3, f4;
    int i0, i1, i2;
    int slot;                   /* the S2F's container slot rides here */
    /* a player slot's S2F that is part of window pwin's S30 (a refused
     * click's sendContainerAndContentsToPlayer): handleWindowItems drops it
     * unless that window is the client's open one */
    int pwin;
    /* a player slot's S2F that stands for its part of an S30
     * (handleWindowItems: a new stack, no pickup animation) */
    int s30;
    struct surv_stack st;       /* the S2F's stack tag (ench list, display) */
    int ntrlist;                /* the S3F's recipe count (the list is out of line) */
    /* the S37's map: its slot in the survival state's ring and the send's
     * sequence (surv_s37) */
    int s37_slot;
    unsigned s37_seq;
    /* the packet's out-of-line part in its queue's side store (s2c_side):
     * the S30's 40 stacks, the S3F's recipes, the S2D's title; len 0 none */
    int side_off, side_len;
};

/* a packet's largest out-of-line part: the S3F's full recipe list */
#define S2C_SIDE_MAX_PKT (TRADES_MAX * sizeof(struct s2c_trade))
/* One server tick's packets to the player. Java's send queue grows; a tick
 * of an agent's press bursts (a dozen use presses on a furnace, each an S2D,
 * its window's slots and progress bars) reaches several hundred, and past
 * these the process stops rather than drop a packet (s2c_add, s2c_side_new) */
#define S2C_MAX 1024
/* an S30 of window 0: 40 inventory stacks, then the 2x2 grid */
#define S30_STACKS 44
#define S2C_SIDE_BYTES (S2C_MAX * 64 + 16 * S2C_SIDE_MAX_PKT)
/* the S2D's title: a custom name, at most S2C_TITLE - 1 bytes */
#define S2C_TITLE 64

/* One queue: the packets in send order, then their out-of-line parts, each
 * filled front to back, so what a queue holds is n packets and nside bytes
 * (the rest is never touched). */
struct s2c_queue
{
    int n;
    int nside;
    struct s2c_pkt q[S2C_MAX];
    _Alignas(8) unsigned char side[S2C_SIDE_BYTES];
};

/* The row's three queues, by index: out the server tick writes, sent what
 * the last server tick sent (the next client tick's), in the one the client
 * tick reads. A row hands them on by index (s2c_receive, s2c_send); the
 * server's queue is copied only when the tick appends to it while it is
 * still the sent or the client's one (it is emptied first on almost every
 * tick, which takes a free queue without a copy). */
struct s2c_ring
{
    struct s2c_queue q[3];
    int out, sent, in;                  /* in -1: the client drained its queue */
};

/* The world state the survival code needs on top of the players. */
struct surv_world
{
    det_state *det;                     /* the snapshot's Det streams */
    int difficulty;                     /* 0 peaceful .. 3 hard */
    int keep_inventory;                 /* the gamerule */
    struct ie_world *iew;               /* the entity list, NULL when none */
    struct ie_world *extra_iew;         /* items spawned by living entities */
    struct an_world *anw;              /* live entities the client can target */

    /* The world Random whose state the dig machine's drops continue
     * (World.rand, which on the whole-server replay lives in the replay's
     * servertick). NULL when the caller has none. */
    jrand *world_rand;

    /* the whole-server tape replay owns the entity list's tick as well as the
     * world's, so the survival module leaves the list alone there */
    int entity_tick_off;

    /* negative-check switches, set by the harness before the replay */
    int no_sprint_exhaustion;           /* the sprint move cost at the walk rate */
    int drown_fast;                     /* no air reset at -20 */

    /* WorldServer's weather scalars for World.canLightningStrikeAt, the rain
     * half of Entity.isWet: isRaining (rainingStrength > 0.2) and the world
     * the sky probes read. The harness copies them per tick from the live
     * servertick (the probe worlds read them from worldinfo.nbt); a
     * movement-world harness leaves world NULL and the probes return 0. */
    int raining;                        /* 0 when the strength is 0.2 or less */
    struct world *sky_world;
};

/* The client's EffectRenderer fxLayers (the live particles). Spawned by the
 * dig flow; play.c ticks and draws them. */

/* One-time setup before the replay: the Det streams, the difficulty (0..3),
 * the keepInventory gamerule and the entity list. */
void surv_world_setup(det_state *det, int difficulty, int keep_inventory, struct ie_world *iew);

/* Per-tick weather handoff for the isWet rain test: isRaining (the rain
 * strength over 0.2, 0 otherwise) and the world the sky probes read. */
void surv_server_set_weather(int raining, struct world *sky_world);

/* Load the survival state out of a snapshot player file's canonical tree
 * (the nbt compound under "nbt" and "fields"). 0 on a malformed tree. */
int surv_client_load(struct client_player *p, const void *tree);
/* The packets a snapshot's client had received but not yet pumped
 * ("pending", or the older fields pendingS06), appended to q: the queue the
 * first client tick drains. */
int surv_client_pending(const void *tree, struct s2c_queue *q);
/* The pending S20 on the client player itself (pending_s20 and its sprint
 * modifier), when the snapshot records the whole queue. */
void surv_client_pending_s20(const void *tree, struct client_player *p);
/* The digs in progress a snapshot recorded (player_client.nbt's ctl, the
 * PlayerControllerMP; player_server.nbt's iiw, the ItemInWorldManager) and
 * the hotbar slot the client last sent; after surv_world_setup. */
void surv_dig_load(struct client_player *cp, struct server_player *sp, const void *ctree, const void *stree);
/* The windows open at a snapshot (the players' gui records): the server's
 * container, window id, grid, cursor, progress and last sent tile slots;
 * the client's screen, its copy of the window and its cursor; after
 * surv_world_setup and the containers' own seeding. */
void surv_gui_load(struct client_player *cp, struct server_player *sp, const void *ctree, const void *stree);
int surv_server_load(struct server_player *p, const void *tree);

/* The fresh EntityPlayerMP ServerConfigurationManager.respawnPlayer builds. */
void surv_server_fresh(struct server_player *p);
/* new EntityPlayerMP's state alone: the constructor's Det draws and the fresh fields. */
void surv_server_fresh_state(struct server_player *p);
/* EntityPlayer.verifyRespawnCoordinates (a bed at bx, by, bz and a safe spot
 * beside it, into rx, rz): 0 when the bed is gone or boxed in. */
int surv_verify_respawn(struct world *w, int bx, int by, int bz, int *rx, int *rz);

/* What EntityPlayer.clonePlayer can carry over to the fresh player, kept
 * across the reset instead of the whole state (whose StatisticsFile alone
 * is over 100 KB). */
struct surv_clone {
    struct surv_stack inv[40];
    int current_item;
    float health;
    struct surv_food food;
    int xp_level, xp_total;
    float xp_progress;
    int score;
};
void surv_clone_take(struct surv_clone *k, const struct surv_state *sv);

/* The fresh EntityClientPlayerMP setDimensionAndSpawnPlayer builds, on the S07
 * that rides ahead of the respawn's S08. */
/* EntityPlayerSP.onLivingUpdate's portal close of an open screen */
void surv_client_portal_close(struct client_player *p);
void surv_client_fresh(struct client_player *p);

/* One client tick's survival parts, in the order the vanilla chain runs them:
 * the packet pump (updateController), the screens, the gui ops and the input
 * consumers. */
void surv_client_tick_start(struct client_player *p, const struct act *a, struct client_out *out);

/* One row's ["click", window, slot, button, mode] op, applied where
 * PlayerControllerMP.windowClick runs (the gui-input phase, while a screen is
 * up). The click lands on the client's open container (its own copy of the
 * inventories, driven by the S2F mirror) and goes out as the C0E; the server
 * applies the same click on its own container in surv_server_click, below.
 * window < 0 selects the open container. */
void surv_client_click(struct client_player *p, const struct act *a, int i, struct client_out *out);
void surv_client_click_closed(struct client_player *p, const struct act *a, int i, struct client_out *out);

/* The server half of a row's clicks: NetHandlerPlayServer.processClickWindow
 * on the server player's open container, then detectAndSendChanges (the S2F
 * slots the client mirror reads). */
int surv_server_click(struct server_player *p, const struct act *a, int i, struct craft_stack *ret);

/* EntityLivingBase.onEntityUpdate's survival half for the client player: the
 * hurt and death timers that open (and close) the game-over screen. */
void surv_client_base_tick(struct client_player *p);

/* One server tick: the world tick's player part, then the packets. player.c
 * calls the movement-side hooks (the eat countdown, the pickup query, the
 * movement stat) from processPlayer's port. a carries the row's window
 * clicks; NULL when the caller has none. */
void surv_server_tick_start(struct server_player *p, const struct client_out *co, const struct act *a);
void surv_server_close_screen(struct server_player *p);
void surv_server_set_dead(struct server_player *p);

/* EntityPlayerMP.onUpdate, the player's own turn in World.updateEntities. */
void surv_server_world_update(struct server_player *p);
/* Container.canInteractWith over the open window, closeScreen on false:
 * EntityPlayerMP.onUpdate's (the entity pass) and EntityPlayer.onUpdate's
 * (processPlayer's onUpdateEntity, after the living update). */
void surv_server_container_check(struct server_player *p);

/* Container.detectAndSendChanges over the player container, the call the dev
 * slot and clear ops make directly (Dev.java's p.inventoryContainer
 * .detectAndSendChanges()), which reaches the client even while the player is
 * dead (surv_server_world_update returns early then). */
void surv_sync_inventory(struct server_player *p);

/* The damage pipeline, EntityPlayerMP.attackEntityFrom down. 1 when it took. */
int surv_server_damage(struct server_player *p, int source, float amount);
int surv_server_damage_by(struct server_player *p, int source, float amount, const double *attacker_xz);

/* EntityPlayer.jump's exhaustion, called from the processPlayer jump site. */
void surv_server_jump(struct server_player *p);
void surv_server_add_xp(struct server_player *p, int points);

/* EntityPlayer.addMovementStat, called after the packet move. */
void surv_server_move_stat(struct server_player *p, double dx, double dy, double dz);

/* survival.c's tick pieces player.c calls. */
void surv_client_pump(struct client_player *p);
/* GuiWinGame's roll end: the clock updateScreen closes the screen past
 * (client_player.credits_end, else the oracle's 854x480 at scale 2). */
float surv_credits_end(const struct client_player *p);
/* EntityPlayerSP.onLivingUpdate's portal close: displayGuiScreen(null)
 * without a C0D, the screen's client-side onContainerClosed. */
void surv_client_portal_close(struct client_player *p);
int surv_client_armor_value(const struct client_player *p);
int surv_client_in_water(const struct client_player *p);

/* The rest of the survival flow, in tick order. The client half: the eat
 * countdown (PlayerControllerMP.updateController's item-use branch) and the
 * updateController pump. The server half: the world tick's EntityLivingBase
 * base and EntityPlayerMP tails, the eat flow, FoodStats and the S06 gate,
 * Entity.fall, the drop's EntityItem spawn, the queue helper and the throw
 * the C07 drop status lands on. */
void surv_client_eat_tick(struct client_player *p);
void surv_server_base_tick(struct server_player *p);
void surv_server_living_tail(struct server_player *p);
void surv_server_food_and_gate(struct server_player *p);
void surv_server_combat_tick(struct server_player *p);
void surv_server_peaceful_heal(struct server_player *p);
/* The server player's own container (window 0, the 2x2 grid): its
 * SlotCrafting's stat half, after the harness's container_init. */
void surv_server_hook_own_container(struct server_player *p);
/* Minecraft.func_147112_ai, survival: the middle click's hotbar pick. */
void surv_client_pick_block(struct client_player *p);
void surv_server_eat_tick(struct server_player *p);
void surv_server_fall(struct server_player *p, float distance);
struct ie_ent *throw_item(struct server_player *p, const struct surv_stack *st, int thrown);
/* The queue's next packet, zeroed, for the caller to fill in place; past
 * the queue's end the packet is dropped (a scratch slot). */
struct s2c_pkt *s2c_add(struct s2c_queue *q);
/* The player has no entry in its world's EntityTracker: transferred out of
 * the End (never spawned into the destination) or unlisted by its ghost
 * chunk's unload (removeEntityFromAllTrackingPlayers took its own entry).
 * The entry's own packets to it (S12, the S19 status, S1C, S20) do not go
 * out and the dirt waits. */
int surv_player_untracked(const struct server_player *p);
/* EntityLivingBase.renderBrokenItemStack's draws on the client player. */
void surv_client_render_broken(struct client_player *p, const struct surv_stack *broken);
/* World.spawnParticle on the client world: RenderGlobal.doSpawnParticle
 * into the client's EffectRenderer (surv_fx). */
/* The client's EffectRenderer (surv_fx), made for the client player's
 * world, its spawn gate's view at the player. */
struct particles_live *surv_client_fx(struct client_player *p);
/* the client player's Block.onEntityWalking (the redstone ore's touch) */
void surv_client_bind_walking(struct client_player *p);
void surv_client_fx_spawn(struct client_player *p, const char *name, double x, double y, double z,
                          double vx, double vy, double vz);
/* The packet's out-of-line part, bytes long (at most S2C_SIDE_MAX_PKT),
 * zeroed, in q's side store; a dropped packet's is a scratch one. */
void *s2c_side_new(struct s2c_queue *q, struct s2c_pkt *pkt, size_t bytes);
/* The packet's out-of-line part, NULL when it has none. */
const void *s2c_side(const struct s2c_queue *q, const struct s2c_pkt *pkt);

/* The ring (struct s2c_ring, the environment's): a session's opening empties
 * all three; the sent queue is what the snapshot's client had pending. */
void s2c_reset(void);
struct s2c_queue *s2c_sent_queue(void);
/* The server's queue to append to or edit (made its own first), and to read. */
struct s2c_queue *s2c_out(void);
const struct s2c_queue *s2c_out_peek(void);
/* The server's queue emptied (the tick's first packet opens it). */
void s2c_clear(void);
/* The client's queue: what the last server tick sent; NULL once the pump
 * has drained it (s2c_drain) this row. */
const struct s2c_queue *s2c_in(void);
void s2c_drain(void);
/* The row's hand-offs: the client tick takes what was sent (its start), the
 * server tick's queue becomes the sent one (its end). */
void s2c_receive(void);
/* the received queue's S1Cs of the player's own data watcher read its values
 * as they stand now (the integrated server hands the WatchableObjects over
 * unserialized) */
void s2c_in_watch_live(const struct server_player *sp);
/* an S2E is in the sent queue (the next pump closes a container screen) */
int s2c_close_pending(void);
void s2c_send(void);

/* ServerConfigurationManager.syncPlayerInventory: the player container's
 * S30 window items, then setPlayerHealthUpdated. */
void surv_server_sync_inventory(struct server_player *p);
/* EntityPlayerMP.sendContainerToPlayer(inventoryContainer): the S30 and the
 * cursor's S2F, no S09 (Container.addCraftingToCrafters' own) */
void surv_server_send_container(struct server_player *p);

/* The open furnace or chest window's container copy re-read from its tiles
 * (Java's container slots are the tile's own): what a row compares. */
void surv_server_gui_pull(struct server_player *p);

/* The client's objectMouseOver: the block form only (the tapes' items are
 * not canBeCollidedWith, so the entity half never wins), num 0 on a miss. */
struct surv_mouseover_pos
{
    int num;
    int entity_id;       /* C02 target when the entity ray wins; 0 is no entity */
    int is_miss;         /* MovingObjectPosition.MISS still has an object */
    int x, y, z, side;
    float hx, hy, hz;
};

struct surv_mouseover
{
    struct surv_mouseover_pos cur;
    struct surv_mouseover_pos prev_copy;
    int prev;
    int entity_id;
};

/* The block dig, wooden-pickaxe tapes: the client's objectMouseOver
 * (EntityLivingBase.rayTrace at 4.5 reach over the shared native world), the
 * PlayerControllerMP dig state machine (clickBlock / onPlayerDamageBlock /
 * resetBlockRemoving, the C07s it queues), the server's C07 status 0/1/2
 * through dig.c's drive ops on the server player's held stack and pose, and
 * the server's held-stack damage the break's own flow spends. The client
 * mouseover is refreshed where runTick's "pick" section runs it, so callers
 * refresh it once per tick before the consumers. */
void surv_client_mouseover(struct client_player *p);
/* RenderGlobal's local player destroy progress. A negative stage is absent. */
int surv_client_dig_stage(int *x, int *y, int *z);
void surv_client_dig_tick(struct client_player *p, int attack_held, struct client_out *out);
void surv_client_attack_pressed(struct client_player *p, struct client_out *out);
int surv_client_use_pressed(struct client_player *p, struct client_out *out);
void surv_server_dig_packet(struct server_player *p, int status);
void surv_server_use_packet(struct server_player *p, int x, int y, int z, int side,
                            float hx, float hy, float hz);
void surv_server_c08_tail(struct server_player *p);
void surv_server_dig_update(struct server_player *p);
void surv_dig_set_world(struct world *w);
void surv_add_exhaustion(struct server_player *p, float amount);
/* BlockBed.func_149977_a(world, x, y, z, 0): the first cell around the bed a
 * player can stand in; 0 when there is none. */
int surv_bed_safe_spot(struct world *w, int bx, int by, int bz, int *rx, int *rz);

#endif
