/* The player survival state, ported from FoodStats, the EntityPlayer /
 * EntityPlayerMP damage and death chain, EntityLivingBase's base tick,
 * EntityPlayer.dropOneItem, the eating flow, InventoryPlayer's add and drop,
 * EntityItem.onCollideWithPlayer, EntityXPOrb.onCollideWithPlayer,
 * ServerConfigurationManager.respawnPlayer and Minecraft
 * setDimensionAndSpawnPlayer. See survival.h for the boundary and the report
 * for the assumptions. */
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comparator.h"
#include "lang.h"
#include "player.h"
#include "gui_input.h"
#include "chatcmd.h"
#include "env.h"
#include "riding.h"
#include "leash.h"
#include "pick.h"
#include "pickobj.h"
#include "serverreplay.h"
#include "randomtick.h"
#include "item_entity.h"
#include "trace.h"
#include "blocks.h"
#include "dig.h"
#include "drops.h"
#include "harvest.h"
#include "place.h"
#include "items.h"
#include "itemuse.h"
#include "activate.h"
#include "living.h"
#include "hostiles_silverfish.h"
#include "tileentity.h"
#include "jmath.h"
#include "smath.h"
#include "raytrace.h"
#include "combat.h"
#include "chatmsg.h"
#include "pick.h"
#include "pickobj.h"
#include "sleep.h"
#include "biomes.h"
#include "features_lakes.h" /* biome_at */
#include "blockcb.h"
#include "randomtick.h"
#include "tnt.h"
#include "serverreplay.h"
#include "throw.h"
#include "smelting.h"
#include "particles_live.h"
#include "stats_table.h"
#include "pickblock.h"
#include "tape.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The items whose finished use is not food (ItemSoup is food with a bowl). */
enum { IT_BUCKET = 325, IT_MILK = 335, IT_BOWL = 281, IT_STEW = 282, IT_POTION = 373,
       IT_GLASS_BOTTLE = 374, IT_NAME_TAG = 421 };
static struct living *surv_find_living(int entity_id);

#define MAT_WATER 6
#define MAT_LAVA 7
#define BLOCK_BED 26

/* The player container's 45 slots: 0-4 craft, 5-8 armor (slot 5 is
 * armorInventory[3], ContainerPlayer's getSizeInventory()-1-var), 9-35 main
 * 9-35, 36-44 main 0-8. -1 the craft slots. */
static int container_slot_inventory(int s)
{
    if (s >= 9 && s <= 35) return s;
    if (s >= 36 && s <= 44) return s - 36;
    if (s >= 5 && s <= 8) return 36 + (8 - s);
    return -1;
}

/* ---------------------------------------------------------------- helpers */

static int ceiling_float_int(float v)
{
    int i = (int)v;
    return v > (float)i ? i + 1 : i;
}

/* Math.round(float): (int)Math.floor(f + 0.5F). */
static int round_float(float f)
{
    return mh_floor((double)f + 0.5);
}

/* MathHelper.sqrt_double: (float)Math.sqrt(double). */
static float sqrt_double(double v)
{
    return (float)sqrt(v);
}

/* Entity.isEntityInsideOpaqueBlock. */
static int inside_opaque_block(struct world *w, const struct entity *e, float eye_height)
{
    for (int i = 0; i < 8; ++i)
    {
        float dx = ((float)((i >> 0) % 2) - 0.5F) * e->width * 0.8F;
        float dy = ((float)((i >> 1) % 2) - 0.5F) * 0.1F;
        float dz = ((float)((i >> 2) % 2) - 0.5F) * e->width * 0.8F;
        int x = mh_floor(e->pos_x + (double)dx);
        int y = mh_floor((double)e->pos_y + (double)eye_height + (double)dy);
        int z = mh_floor(e->pos_z + (double)dz);

        if (BLOCKS[world_get_block(w, x, y, z) & 4095].normal_cube) return 1;
    }

    return 0;
}

/* Entity.isInsideOfMaterial(material): the eye cell plus the liquid height
 * fraction. */
static int inside_of_material(struct world *w, const struct entity *e, float eye_height, int material)
{
    double eye = (double)e->pos_y + (double)eye_height;
    int x = mh_floor(e->pos_x);
    int y = mh_floor((double)(float)mh_floor(eye));
    int z = mh_floor(e->pos_z);
    int id = world_get_block(w, x, y, z) & 4095;

    if (!BLOCKS[id].exists || BLOCKS[id].material != material) return 0;

    int meta = world_get_meta(w, x, y, z);
    float var8 = (float)(meta >= 8 ? 0 : meta + 1) / 9.0F - 0.11111111F;
    float var9 = (float)(y + 1) - var8;
    return eye < (double)var9;
}

/* World.isMaterialInBB. */
static int bb_has_material(struct world *w, struct aabb b, int material)
{
    int x0 = mh_floor(b.min_x), x1 = mh_floor(b.max_x + 1.0);
    int y0 = mh_floor(b.min_y), y1 = mh_floor(b.max_y + 1.0);
    int z0 = mh_floor(b.min_z), z1 = mh_floor(b.max_z + 1.0);

    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
            {
                int id = world_get_block(w, x, y, z) & 4095;
                if (BLOCKS[id].exists && BLOCKS[id].material == material) return 1;
            }

    return 0;
}

/* EntityLivingBase.getTotalArmorValue over the armor slots. */
static int total_armor_value(const struct surv_state *sv)
{
    int v = 0;

    for (int i = 0; i < 4; ++i)
    {
        const struct surv_stack *st = &sv->inv[36 + i];

        if (st->count > 0 && ITEMS[st->item].exists && ITEMS[st->item].kind == ITEM_ARMOR)
            v += ITEMS[st->item].damage_reduce;
    }

    return v;
}

int surv_client_armor_value(const struct client_player *p)
{
    return total_armor_value(&p->sv);
}

int surv_client_in_water(const struct client_player *p)
{
    /* EntityPlayer.getEyeHeight is 0.12 on the client player, whose posY is
     * already the eye (yOffset 1.62); EntityPlayerMP's 1.62 is the server's */
    return inside_of_material(p->e.world, &p->e, 0.12F, MAT_WATER);
}

static struct surv_stack empty_stack(void)
{
    struct surv_stack st = {0};
    return st;
}

/* ItemStack.areItemStacksEqual: the size, the item, the damage and the tag
 * (ItemStack.areItemStackTagsEqual, an index compare in the tag store). */
static int stack_eq(const struct surv_stack *a, const struct surv_stack *b)
{
    return a->count == b->count && a->item == b->item && a->damage == b->damage && a->tag == b->tag;
}

int surv_ench_level(const struct surv_stack *st, int id)
{
    if (st == NULL || st->count <= 0) return 0;
    return itag_ench_level(st->tag, id);
}

/* A craft stack (the container's copy) as the survival stack it mirrors: the
 * item, damage and count, and the tag with them, so a slot written from the
 * container never keeps the tag of the stack it held before. */
static void surv_from_craft(struct surv_stack *ss, const struct craft_stack *cs)
{
    ss->item = cs->count > 0 ? cs->item : 0;
    ss->damage = cs->count > 0 ? cs->damage : 0;
    ss->count = cs->count > 0 ? cs->count : 0;
    ss->tag = cs->count > 0 ? cs->tag : 0;
}

/* What the player container's slot s holds on the server, as
 * detectAndSendChanges compares it: the inventory's slots, the 2x2 grid
 * (slots 1 to 4, the own container's) and the result (slot 0: a SlotCrafting
 * is never sent, so it stays empty here). */
static struct surv_stack server_slot_stack(struct server_player *p, int s)
{
    struct surv_stack st = {0};
    int inv = container_slot_inventory(s);

    if (inv >= 0) return p->sv.inv[inv];
    if (s >= 1 && s <= 4)
    {
        const struct craft_stack *cs = container_slot(&p->own_container, s);
        if (cs != NULL) surv_from_craft(&st, cs);
    }
    return st;
}

/* The container's copy of a survival stack (-1 for an empty slot). */
static struct craft_stack craft_from_surv(const struct surv_stack *st)
{
    return (struct craft_stack){st->count > 0 ? st->item : -1, st->count,
                                st->count > 0 ? st->damage : 0, st->count > 0 ? st->tag : 0};
}

/* The container's copy of a tile entity's slot. */
static struct craft_stack craft_from_te(const struct te_stack *ts)
{
    return (struct craft_stack){ts->count > 0 ? ts->item : -1, ts->count,
                                ts->count > 0 ? ts->damage : 0, ts->count > 0 ? ts->tag : 0};
}

/* The damage sources' hunger cost (DamageSource.hungerDamage). */
static const float SRC_HUNGER[SURV_KINDS] = {
    [SURV_IN_FIRE] = 0.3F, [SURV_ON_FIRE] = 0.0F, [SURV_LAVA] = 0.3F, [SURV_IN_WALL] = 0.0F,
    [SURV_DROWN] = 0.0F, [SURV_STARVE] = 0.0F, [SURV_CACTUS] = 0.3F, [SURV_FALL] = 0.0F,
    [SURV_OUT_OF_WORLD] = 0.0F, [SURV_GENERIC] = 0.0F,
    [SURV_MOB] = 0.3F, [SURV_EXPLOSION] = 0.3F,
    /* DamageSource.magic: setDamageBypassesArmor zeroes the hunger damage */
    [SURV_MAGIC] = 0.0F,
    [SURV_ARROW] = 0.3F, [SURV_THROWN] = 0.3F, [SURV_FIREBALL] = 0.3F, [SURV_THORNS] = 0.3F,
    [SURV_ANVIL] = 0.3F,
};

static int src_unblockable(int source)
{
    return source == SURV_ON_FIRE || source == SURV_IN_WALL || source == SURV_DROWN ||
           source == SURV_STARVE || source == SURV_FALL || source == SURV_OUT_OF_WORLD ||
           source == SURV_GENERIC || source == SURV_MAGIC;
}

/* EntityLivingBase.isOnLadder. */
static int surv_on_ladder(struct world *w, double pos_x, double pos_z, struct aabb bb)
{
    int id = world_get_block(w, mh_floor(pos_x), mh_floor(bb.min_y), mh_floor(pos_z)) & 4095;
    return id == 65 || id == 106;
}

/* --------------------------------------------------------------- s2c queue */

struct s2c_pkt *s2c_add(struct s2c_queue *q)
{
    if (q->n >= S2C_MAX)
    {
        fprintf(stderr, "s2c: more than %d packets to the player in one tick\n", S2C_MAX);
        abort();
    }
    struct s2c_pkt *pkt = &q->q[q->n++];

    memset(pkt, 0, sizeof *pkt);
    return pkt;
}

void *s2c_side_new(struct s2c_queue *q, struct s2c_pkt *pkt, size_t bytes)
{
    unsigned char *side;

    if (pkt == &nw_scratch->s2c_discard) side = nw_scratch->s2c_side_discard;
    else
    {
        if ((size_t)q->nside + bytes > sizeof q->side)
        {
            fprintf(stderr, "s2c: the tick's packets past %zu out-of-line bytes\n", sizeof q->side);
            abort();
        }
        pkt->side_off = q->nside;
        pkt->side_len = (int)bytes;
        side = &q->side[q->nside];
        q->nside += (int)((bytes + 7) & ~(size_t)7);
    }
    memset(side, 0, bytes);
    return side;
}

const void *s2c_side(const struct s2c_queue *q, const struct s2c_pkt *pkt)
{
    return pkt->side_len > 0 ? &q->side[pkt->side_off] : NULL;
}

#define s2c_ring (nw_env->survival.s2c)

void s2c_reset(void)
{
    for (int i = 0; i < 3; ++i) s2c_ring.q[i].n = s2c_ring.q[i].nside = 0;
    s2c_ring.out = 0;
    s2c_ring.sent = 1;
    s2c_ring.in = 2;
}

struct s2c_queue *s2c_sent_queue(void)
{
    return &s2c_ring.q[s2c_ring.sent];
}

/* the used part of a queue, into another */
static void s2c_copy(struct s2c_queue *to, const struct s2c_queue *from)
{
    to->n = from->n;
    to->nside = from->nside;
    memcpy(to->q, from->q, (size_t)from->n * sizeof *from->q);
    memcpy(to->side, from->side, (size_t)from->nside);
}

/* the queue neither the sent nor the client's index names */
static int s2c_free(void)
{
    int i = 0;
    while (i == s2c_ring.sent || i == s2c_ring.in) ++i;
    return i;
}

struct s2c_queue *s2c_out(void)
{
    if (s2c_ring.out == s2c_ring.sent || s2c_ring.out == s2c_ring.in)
    {
        int f = s2c_free();
        PHASE_SUB(PS_S2CCOPY, s2c_copy(&s2c_ring.q[f], &s2c_ring.q[s2c_ring.out]));
        s2c_ring.out = f;
    }
    return &s2c_ring.q[s2c_ring.out];
}

const struct s2c_queue *s2c_out_peek(void)
{
    return &s2c_ring.q[s2c_ring.out];
}

void s2c_clear(void)
{
    if (s2c_ring.out == s2c_ring.sent || s2c_ring.out == s2c_ring.in) s2c_ring.out = s2c_free();
    s2c_ring.q[s2c_ring.out].n = s2c_ring.q[s2c_ring.out].nside = 0;
}

const struct s2c_queue *s2c_in(void)
{
    return s2c_ring.in >= 0 ? &s2c_ring.q[s2c_ring.in] : NULL;
}

void s2c_drain(void)
{
    s2c_ring.in = -1;
}

/* An S2E in what the last server tick sent: the client's pump closes its
 * container screen at the next tick's start (play.c records that tick's
 * input block, which then runs). */
int s2c_close_pending(void)
{
    const struct s2c_queue *q = s2c_sent_queue();
    for (int i = 0; q != NULL && i < q->n; ++i)
        if (q->q[i].kind == PK_S2E) return 1;
    return 0;
}

void s2c_receive(void)
{
    s2c_ring.in = s2c_ring.sent;
}

/* S1CPacketEntityMetadata(id, watcher, false) holds the server DataWatcher's
 * own WatchableObjects (getChanged), and the integrated server's local
 * channel hands the packet over without serializing it: the client's pump
 * reads each changed object's value as it stands then, after the network
 * work that followed the send (a tracker pass's health, then the fall that
 * kills: the client dies a row before the S06; pfc-s1clive-s7) */
void s2c_in_watch_live(const struct server_player *sp)
{
    if (s2c_ring.in < 0) return;
    struct s2c_queue *q = &s2c_ring.q[s2c_ring.in];
    for (int i = 0; i < q->n; ++i)
    {
        if (q->q[i].kind == PK_S1C) q->q[i].f0 = sp->sv.health;
        else if (q->q[i].kind == PK_S1C_ABS) q->q[i].f0 = sp->sv.absorption;
    }
}

void s2c_send(void)
{
    s2c_ring.sent = s2c_ring.out;
}

/* the twin's on_potion_event writes the S1D / S1E straight onto the player's
 * s2c queue; a no-op here today, kept as the potion events' flush point */
static void sr_flush_potions(struct server_player *p)
{
    (void)p;
}

/* S30PacketWindowItems for window 0: inventoryContainer's every slot, the
 * 40 inventory stacks and the 2x2 grid (slots 1 to 4) after them; the
 * result slot is the client's own recomputation */
static void s30_fill(struct server_player *p, struct surv_stack *out)
{
    for (int i = 0; i < 40; ++i) out[i] = p->sv.inv[i];
    for (int g = 0; g < 4; ++g) out[40 + g] = server_slot_stack(p, 1 + g);
}

void surv_server_sync_inventory(struct server_player *p)
{
    struct s2c_queue *q = s2c_out();
    struct s2c_pkt *pkt = s2c_add(q);
    pkt->kind = PK_S30;

    struct surv_stack *inv = s2c_side_new(q, pkt, S30_STACKS * sizeof *inv);
    s30_fill(p, inv);

    p->sv.last_health = -1.0E8F;

    /* then the S09 with inventory.currentItem */
    pkt = s2c_add(q);
    pkt->kind = PK_S09;
    pkt->i0 = p->sv.current_item;
}

static void queue_s06(struct server_player *p)
{
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S06;
    pkt->f0 = p->sv.health;
    pkt->i0 = p->sv.food.level;
    pkt->f1 = p->sv.food.saturation;
}

static void queue_s1f(struct server_player *p, float progress, int total, int level)
{
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S1F;
    pkt->f0 = progress;
    pkt->i0 = total;
    pkt->i1 = level;
}

static struct s2c_pkt *queue_s2f(struct server_player *p, int slot)
{
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S2F;
    pkt->slot = slot;
    /* the open window's id: the client takes it only while that window is
     * its own open one */
    pkt->pwin = p->open_container != NULL && p->open_container != &p->own_container ? p->open_container->window_id : 0;
    pkt->i0 = p->sv.mirror[slot].item;
    pkt->i1 = p->sv.mirror[slot].damage;
    pkt->i2 = p->sv.mirror[slot].count;
    pkt->st = p->sv.mirror[slot];
    return pkt;
}

/* ------------------------------------------------------------ one-time setup */

void surv_world_setup(det_state *det, int difficulty, int keep_inventory, struct ie_world *iew)
{
    surv.det = det;
    surv.difficulty = difficulty;
    surv.keep_inventory = keep_inventory;
    surv.iew = iew;
    surv.extra_iew = NULL;
    surv.anw = NULL;
}

/* Per-tick weather handoff for World.canLightningStrikeAt: isRaining and the
 * world the sky probes read. servertick_tick's is_raining is the strength
 * (> 0.2); on a movement world the harness leaves raining 0. */
void surv_server_set_weather(int raining, struct world *sky_world)
{
    surv.raining = raining;
    surv.sky_world = sky_world;
}

/* ------------------------------------------------------------ achievements */

/* AchievementList's parents, in the enum's order (see survival.h; the
 * generated STAT_TABLE_ACH_PARENT is the same list, checked below). */
const int8_t ach_parent[ACH_COUNT] = {
    -1,                     /* openInventory, independent */
    ACH_OPEN_INVENTORY,     /* mineWood */
    ACH_MINE_WOOD,          /* buildWorkBench */
    ACH_BUILD_WORK_BENCH,   /* buildPickaxe */
    ACH_BUILD_PICKAXE,      /* buildFurnace */
    ACH_BUILD_FURNACE,      /* acquireIron */
    ACH_BUILD_WORK_BENCH,   /* buildHoe */
    ACH_BUILD_HOE,          /* makeBread */
    ACH_BUILD_HOE,          /* bakeCake */
    ACH_BUILD_PICKAXE,      /* buildBetterPickaxe */
    ACH_BUILD_FURNACE,      /* cookFish */
    ACH_ACQUIRE_IRON,       /* onARail */
    ACH_BUILD_WORK_BENCH,   /* buildSword */
    ACH_BUILD_SWORD,        /* killEnemy */
    ACH_BUILD_SWORD,        /* killCow */
    ACH_KILL_COW,           /* flyPig */
    ACH_KILL_ENEMY,         /* snipeSkeleton */
    ACH_ACQUIRE_IRON,       /* diamonds */
    ACH_DIAMONDS,           /* diamondsToYou */
    ACH_DIAMONDS,           /* portal */
    ACH_PORTAL,             /* ghast */
    ACH_PORTAL,             /* blazeRod */
    ACH_BLAZE_ROD,          /* potion */
    ACH_BLAZE_ROD,          /* theEnd */
    ACH_THE_END,            /* theEnd2 */
    ACH_DIAMONDS,           /* enchantments */
    ACH_ENCHANTMENTS,       /* overkill */
    ACH_ENCHANTMENTS,       /* bookcase */
    ACH_KILL_COW,           /* breedCow */
    ACH_THE_END2,           /* spawnWither */
    ACH_SPAWN_WITHER,       /* killWither */
    ACH_KILL_WITHER,        /* fullBeacon */
    ACH_THE_END,            /* exploreAllBiomes */
};

_Static_assert(ACH_COUNT == STAT_TABLE_NACH, "the achievement enum is AchievementList's");
_Static_assert(STAT_KILL_ENTITY - STAT_LEAVE_GAME == STAT_TABLE_NGENERAL, "the general stats are StatList's");
_Static_assert(EGG_COUNT == STAT_TABLE_NEGG, "the egg enum is EntityList.entityEggs");

/* living.h's kind constants per EntityList egg mapping. */
const int8_t egg_kind[EGG_COUNT] = {
    HK_CREEPER, HK_SKELETON, HK_SPIDER, HK_ZOMBIE, SK_SLIME,
    GK_GHAST, HK_PIGMAN, HK_ENDERMAN, HK_CAVE_SPIDER, HK_SILVERFISH,
    HK_BLAZE, SK_MAGMA_CUBE, AK_BAT, HK_WITCH,
    AK_PIG, AK_SHEEP, AK_COW, AK_CHICKEN, AK_SQUID, -1,
    AK_MOOSHROOM, -1, -1, VK_VILLAGER,
};

/* The toast's en_US text per achievement (I18n.format at frame time in the
 * oracle): "achievement.<name>" and "achievement.<name>.desc", the hint's
 * key name already substituted. */
static const char *const ach_desc_keys[ACH_COUNT] = {
    "achievement.openInventory.desc",
    "achievement.mineWood.desc",
    "achievement.buildWorkBench.desc",
    "achievement.buildPickaxe.desc",
    "achievement.buildFurnace.desc",
    "achievement.acquireIron.desc",
    "achievement.buildHoe.desc",
    "achievement.makeBread.desc",
    "achievement.bakeCake.desc",
    "achievement.buildBetterPickaxe.desc",
    "achievement.cookFish.desc",
    "achievement.onARail.desc",
    "achievement.buildSword.desc",
    "achievement.killEnemy.desc",
    "achievement.killCow.desc",
    "achievement.flyPig.desc",
    "achievement.snipeSkeleton.desc",
    "achievement.diamonds.desc",
    "achievement.diamondsToYou.desc",
    "achievement.portal.desc",
    "achievement.ghast.desc",
    "achievement.blazeRod.desc",
    "achievement.potion.desc",
    "achievement.theEnd.desc",
    "achievement.theEnd2.desc",
    "achievement.enchantments.desc",
    "achievement.overkill.desc",
    "achievement.bookcase.desc",
    "achievement.breedCow.desc",
    "achievement.spawnWither.desc",
    "achievement.killWither.desc",
    "achievement.fullBeacon.desc",
    "achievement.exploreAllBiomes.desc",
};

const char *surv_ach_id(int a)
{
    return STAT_TABLE_ACH[a];
}

int surv_ach_by_id(const char *id)
{
    for (int a = 0; a < ACH_COUNT; ++a)
        if (!strcmp(id, STAT_TABLE_ACH[a])) return a;
    return -1;
}

const char *surv_ach_name(int a)
{
    return lang_text(STAT_TABLE_ACH[a]);
}

const char *surv_ach_desc(int a)
{
    return lang_text(ach_desc_keys[a]);
}

/* the en_US name of an achievement's statName key (its statId) */
const char *surv_ach_lang(const char *key)
{
    for (int a = 0; a < ACH_COUNT; ++a)
        if (!strcmp(key, STAT_TABLE_ACH[a])) return lang_text(STAT_TABLE_ACH[a]);
    return NULL;
}

/* AchievementList's setSpecial calls */
int surv_ach_special(int a)
{
    return a == ACH_ON_A_RAIL || a == ACH_FLY_PIG || a == ACH_SNIPE_SKELETON || a == ACH_GHAST ||
           a == ACH_THE_END || a == ACH_THE_END2 || a == ACH_OVERKILL || a == ACH_FULL_BEACON ||
           a == ACH_EXPLORE_ALL_BIOMES;
}


/* The achievement's theItemStack (AchievementList's display item): the item
 * id the icon atlas and RenderItem draw, with the metadata for the ones that
 * carry it. */
const surv_ach_item ach_item[ACH_COUNT] = {
    {340, 0},   /* openInventory: Items.book */
    {17, 0},    /* mineWood: Blocks.log */
    {58, 0},    /* buildWorkBench: Blocks.crafting_table */
    {270, 0},   /* buildPickaxe: Items.wooden_pickaxe */
    {61, 0},    /* buildFurnace: Blocks.furnace */
    {265, 0},   /* acquireIron: Items.iron_ingot */
    {290, 0},   /* buildHoe: Items.wooden_hoe */
    {297, 0},   /* makeBread: Items.bread */
    {354, 0},   /* bakeCake: Items.cake */
    {274, 0},   /* buildBetterPickaxe: Items.stone_pickaxe */
    {350, 0},   /* cookFish: Items.cooked_fished */
    {66, 0},    /* onARail: Blocks.rail */
    {268, 0},   /* buildSword: Items.wooden_sword */
    {352, 0},   /* killEnemy: Items.bone */
    {334, 0},   /* killCow: Items.leather */
    {329, 0},   /* flyPig: Items.saddle */
    {261, 0},   /* snipeSkeleton: Items.bow */
    {56, 0},    /* diamonds: Blocks.diamond_ore */
    {264, 0},   /* diamondsToYou: Items.diamond */
    {49, 0},    /* portal: Blocks.obsidian */
    {370, 0},   /* ghast: Items.ghast_tear */
    {369, 0},   /* blazeRod: Items.blaze_rod */
    {373, 0},   /* potion: Items.potionitem */
    {381, 0},   /* theEnd: Items.ender_eye */
    {122, 0},   /* theEnd2: Blocks.dragon_egg */
    {116, 0},   /* enchantments: Blocks.enchanting_table */
    {276, 0},   /* overkill: Items.diamond_sword */
    {47, 0},    /* bookcase: Blocks.bookshelf */
    {296, 0},   /* breedCow: Items.wheat */
    {397, 1},   /* spawnWither: new ItemStack(Items.skull, 1, 1) */
    {399, 0},   /* killWither: Items.nether_star */
    {138, 0},   /* fullBeacon: Blocks.beacon */
    {301, 0},   /* exploreAllBiomes: Items.diamond_boots */
};

/* The GuiAchievement texts: unlock = "Achievement get!" + the achievement's
 * name; hint = the name + the description. */
#define TOAST_TITLE_UNLOCK lang_text("achievement.get")

/* IMob: EntityMob, EntitySlime, EntityGhast (and EntityGiantZombie, never
 * spawned here). The kill path checks the victim's kind against this. */
int surv_is_imob(int kind)
{
    switch (kind)
    {
    case HK_ZOMBIE: case HK_SKELETON: case HK_CREEPER: case HK_SPIDER:
    case GK_GHAST: case HK_PIGMAN: case HK_ENDERMAN: case HK_CAVE_SPIDER:
    case HK_SILVERFISH: case HK_BLAZE: case HK_WITCH:
    case SK_SLIME: case SK_MAGMA_CUBE:
        return 1;
    default:
        return 0;
    }
}

/* The EntityEggInfo of one victim kind (EntityList.getEntityID's mapping),
 * -1 when the kind has no egg. */
int surv_egg_of_kind(int kind)
{
    if (kind < 0) return -1;
    for (int i = 0; i < EGG_COUNT; ++i)
        if (egg_kind[i] == kind) return i;
    return -1;
}

/* StatFileWriter.hasAchievementUnlocked. */
static int ach_unlocked(const struct surv_stats *st, int a)
{
    return st->v[a] > 0;
}

/* StatFileWriter.canUnlockAchievement. */
static int ach_can_unlock(const struct surv_stats *st, int a)
{
    int parent = ACH_PARENT(a);
    return parent < 0 || ach_unlocked(st, parent);
}

/* The registered stat behind a per-id array slot (StatList's arrays after
 * replaceAllSimilarBlocks), as a store id; -1 when the slot is null. Every
 * other id is its own. */
static int stat_resolve(int stat)
{
    if (stat < STAT_MINE_BLOCK) return stat >= 0 ? stat : -1;
    if (stat >= STAT_COUNT) return -1;

    const short *tab;
    int base;

    if (stat <= STAT_MINE_BLOCK_LAST) { tab = STAT_TABLE_MINE; base = STAT_MINE_BLOCK; }
    else if (stat <= STAT_USE_ITEM_LAST) { tab = STAT_TABLE_USE; base = STAT_USE_ITEM; }
    else if (stat <= STAT_CRAFT_ITEM_LAST) { tab = STAT_TABLE_CRAFT; base = STAT_CRAFT_ITEM; }
    else { tab = STAT_TABLE_BREAK; base = STAT_BREAK_ITEM; }

    int c = tab[stat - base];
    return c < 0 ? -1 : base + c;
}

/* The S37 payload ring (surv_s37): a send writes the next slot, the client
 * reads it on the packet's arrival in the same tick pair. */
#define s37_ring (nw_env->survival.s37_ring)
#define s37_next (nw_env->survival.s37_next)

static struct surv_s37 *s37_open(struct s2c_pkt *pkt)
{
    unsigned seq = ++s37_next;
    int slot = (int)(seq % SURV_S37_RING);
    struct surv_s37 *box = &s37_ring[slot];

    box->seq = seq;
    box->n = 0;
    memset(pkt, 0, sizeof *pkt);
    pkt->kind = PK_S37;
    pkt->s37_slot = slot;
    pkt->s37_seq = seq;
    return box;
}

static void s37_put(struct surv_s37 *box, int id, int val)
{
    if (box->n >= SURV_S37_MAX)
    {
        fprintf(stderr, "survival: an S37 over %d stats\n", SURV_S37_MAX);
        abort();
    }

    box->id[box->n] = id;
    box->val[box->n] = val;
    ++box->n;
}

void surv_add_stat(struct server_player *p, int stat, int n)
{
    struct surv_stats *st = &p->sv.stats;

    /* EntityPlayerMP.addStat's null check: an unregistered per-id slot */
    stat = stat_resolve(stat);
    if (stat < 0) return;

    /* StatFileWriter.func_150871_b: an achievement counts only when it can
     * unlock */
    if (stat < ACH_COUNT && !ach_can_unlock(st, stat)) return;

    /* StatisticsFile.func_150873_a: the store, the dirty set, and the 0 -> n
     * move of an achievement */
    int before = st->v[stat];
    st->v[stat] = before + n;
    STAT_SET(st->present, stat);
    STAT_SET(st->dirty, stat);

    if (stat < ACH_COUNT && before == 0 && before + n > 0)
    {
        st->ach_dirty = 1;
        /* the integrated server announces it (func_147136_ar) */
        chatmsg_achievement(p, stat);
    }

    /* EntityPlayerMP.addStat's tail: func_150879_e, then func_150876_a */
    if (st->ach_dirty) surv_stats_flush(p, p->server_tick - p->paused_ticks);
}

void surv_stats_flush(struct server_player *p, int server_tick)
{
    struct surv_stats *st = &p->sv.stats;
    struct surv_s37 *box = s37_open(s2c_add(s2c_out()));

    if (st->ach_dirty || server_tick - st->last_send_tick > 300)
    {
        st->last_send_tick = server_tick;

        /* func_150878_c: the dirty set, cleared with field_150886_g; each
         * entry goes out with writeStat's value (the map's iteration order is
         * StatBase's identity hash, and the client applies each entry on its
         * own, so the store order stands in) */
        for (int s = 0; s < STAT_COUNT; ++s)
            if (STAT_BIT(st->dirty, s)) s37_put(box, s, st->v[s]);

        memset(st->dirty, 0, sizeof st->dirty);
        st->ach_dirty = 0;
    }

}

void surv_stats_login(struct server_player *p)
{
    struct surv_stats *st = &p->sv.stats;

    /* func_150877_d: every stat the file holds is dirty */
    for (int s = 0; s < STAT_COUNT; ++s)
        if (STAT_BIT(st->present, s)) STAT_SET(st->dirty, s);

    /* func_150884_b: every unlocked achievement, taken out of the set */
    struct surv_s37 *box = s37_open(s2c_add(s2c_out()));

    for (int a = 0; a < ACH_COUNT; ++a)
        if (ach_unlocked(st, a))
        {
            s37_put(box, a, st->v[a]);
            STAT_CLR(st->dirty, a);
        }

}

/* The breeder's credit (EntityAIMate.spawnBaby): triggerAchievement of
 * animalsBred, then breedCow for an EntityCow (the mooshroom is one). */
void surv_bred(struct server_player *p, int kind)
{
    if (p == NULL) return;
    surv_add_stat(p, STAT_ANIMALS_BRED, 1);
    if (kind == AK_COW || kind == AK_MOOSHROOM) surv_add_stat(p, ACH_BREED_COW, 1);
}

/* EntitySkeleton.onDeath's tail, the arrow's shooter being this player. */
void surv_skeleton_shot(struct server_player *p, double sx, double sz)
{
    if (p == NULL) return;
    double dx = p->e.pos_x - sx;
    double dz = p->e.pos_z - sz;
    if (dx * dx + dz * dz >= 2500.0) surv_add_stat(p, ACH_SNIPE_SKELETON, 1);
}

/* --------------------------------------------------------- stat names */

static const char *const STAT_PREFIX[4] = {
    "stat.mineBlock.", "stat.useItem.", "stat.craftItem.", "stat.breakItem.",
};

const char *surv_stat_name(int stat, char *buf, size_t n)
{
    if (stat < 0 || stat >= STAT_COUNT) snprintf(buf, n, "?");
    else if (stat < ACH_COUNT) snprintf(buf, n, "%s", STAT_TABLE_ACH[stat]);
    else if (stat < STAT_KILL_ENTITY) snprintf(buf, n, "%s", STAT_TABLE_GENERAL[stat - STAT_LEAVE_GAME]);
    else if (stat <= STAT_KILL_ENTITY_LAST)
        snprintf(buf, n, "stat.killEntity.%s", STAT_TABLE_EGG[stat - STAT_KILL_ENTITY].name);
    else if (stat <= STAT_ENTITY_KILLED_BY_LAST)
        snprintf(buf, n, "stat.entityKilledBy.%s", STAT_TABLE_EGG[stat - STAT_ENTITY_KILLED_BY].name);
    else
    {
        int t = (stat - STAT_MINE_BLOCK) / 4096;
        snprintf(buf, n, "%s%d", STAT_PREFIX[t], (stat - STAT_MINE_BLOCK) % 4096);
    }

    return buf;
}

/* StatList.func_151177_a over the store's ids. A per-id name counts only when
 * its array slot holds its own stat (an aliased slot's orphan object is never
 * incremented and a file never names it). */
int surv_stat_by_name(const char *name)
{
    for (int a = 0; a < ACH_COUNT; ++a)
        if (!strcmp(name, STAT_TABLE_ACH[a])) return a;

    for (int g = 0; g < STAT_TABLE_NGENERAL; ++g)
        if (!strcmp(name, STAT_TABLE_GENERAL[g])) return STAT_LEAVE_GAME + g;

    static const char kill[] = "stat.killEntity.", killed[] = "stat.entityKilledBy.";

    for (int e = 0; e < EGG_COUNT; ++e)
    {
        if (!strncmp(name, kill, sizeof kill - 1) && !strcmp(name + sizeof kill - 1, STAT_TABLE_EGG[e].name))
            return STAT_KILL_ENTITY + e;
        if (!strncmp(name, killed, sizeof killed - 1) && !strcmp(name + sizeof killed - 1, STAT_TABLE_EGG[e].name))
            return STAT_ENTITY_KILLED_BY + e;
    }

    for (int t = 0; t < 4; ++t)
    {
        size_t len = strlen(STAT_PREFIX[t]);
        if (strncmp(name, STAT_PREFIX[t], len)) continue;

        char *end;
        long id = strtol(name + len, &end, 10);
        if (*end || id < 0 || id > 4095) return -1;

        int s = STAT_MINE_BLOCK + t * 4096 + (int)id;
        return stat_resolve(s) == s ? s : -1;
    }

    return -1;
}

/* ------------------------------------------------ exploreAllBiomes, tracker */

/* The progress set's member for a biome id: the first id carrying that
 * biomeName, so two ids with one name are one member. */
static int biome_member(int id)
{
    const char *name = BIOMES[id].name;
    if (name == NULL) return -1;
    for (int i = 0; i < id; ++i)
        if (BIOMES[i].name != NULL && !strcmp(BIOMES[i].name, name)) return i;
    return id;
}

static int explored_count(const struct surv_stats *st)
{
    int n = 0;
    for (int i = 0; i < 256; ++i) n += (st->explored[i >> 3] >> (i & 7)) & 1;
    return n;
}

/* EntityPlayerMP.func_147098_j: the biome under the player joins the
 * progress set (the entry made with value 0 when there is none), and the
 * achievement fires once the set holds every field_150597_n name. */
static void surv_explore_biome(struct server_player *p)
{
    struct surv_stats *st = &p->sv.stats;
    struct world *w = p->e.world;
    if (w == NULL) return;

    int id = biome_at(w, (int)floor(p->e.pos_x), (int)floor(p->e.pos_z));
    if (id < 0 || id > 255) return;
    int m = biome_member(id);
    if (m < 0) return;

    if (!st->has_progress)
    {
        /* func_150872_a: the entry, with no dirty mark */
        st->has_progress = 1;
        STAT_SET(st->present, ACH_EXPLORE_ALL_BIOMES);
    }

    st->explored[m >> 3] |= (uint8_t)(1u << (m & 7));

    if (!ach_can_unlock(st, ACH_EXPLORE_ALL_BIOMES) || explored_count(st) != STAT_TABLE_NEXPLORE) return;

    for (int i = 0; i < STAT_TABLE_NEXPLORE; ++i)
    {
        int e = biome_member(STAT_TABLE_EXPLORE[i]);
        if (e < 0 || !((st->explored[e >> 3] >> (e & 7)) & 1)) return;
    }

    surv_add_stat(p, ACH_EXPLORE_ALL_BIOMES, 1);
}

#define surv_attacker (nw_env->survival.attacker)

void surv_set_attacker(int kind)
{
    surv_attacker = kind;
}

void surv_set_attacker_ref(uint64_t ref)
{
    nw_env->survival.attacker_ref = ref;
}

void surv_set_attacker_msg(int dmg, int name)
{
    nw_env->survival.attacker_msg_set = 1;
    nw_env->survival.attacker_dmg = dmg;
    nw_env->survival.attacker_name = name;
}

/* CombatTracker.func_94549_h: the entries clear once the fighter is dead or
 * the last one is over 100 ticks old (300 in combat). */
static void combat_reset(struct server_player *p)
{
    struct surv_combat *c = &p->sv.combat;
    int limit = c->in_combat ? 300 : 100;

    if (c->active && (p->sv.health <= 0.0F || p->sv.ticks_existed - c->last_tick > limit))
    {
        c->active = 0;
        c->in_combat = 0;
        c->n = 0;
    }
}

/* CombatTracker.func_94547_a, from EntityPlayer.damageEntity after its
 * setHealth: the reset check first (a killing blow clears the older entries,
 * the fighter being dead by then), then the entry. */
static void combat_entry(struct server_player *p, int attacker, float damage)
{
    struct surv_combat *c = &p->sv.combat;

    combat_reset(p);

    /* the first entry since the clear: func_94544_f's and func_94550_c's
     * running answers start empty */
    if (c->n == 0)
    {
        c->has_fall = 0;
        c->k_best = c->k_player = -1;
        c->n_best = c->n_player = -1;
    }

    struct surv_combat_entry e;
    e.kind = (int16_t)attacker;
    e.damage = damage;
    e.dtype = (int8_t)c->cur_dtype;
    e.name = (int16_t)c->cur_name;
    e.fall = p->e.fall_distance;
    /* func_94545_a: isOnLadder's block (a ladder or vines), else isInWater */
    {
        const struct entity *en = &p->e;
        int id = world_get_block(en->world, mh_floor(en->pos_x), mh_floor(en->bounding_box.min_y),
                                 mh_floor(en->pos_z)) & 4095;
        e.label = (int8_t)(id == 65 ? CM_LABEL_LADDER : id == 106 ? CM_LABEL_VINES :
                           en->in_water ? CM_LABEL_WATER : CM_LABEL_NONE);
    }

    /* func_94544_f's loop at this entry: a fall (or the void) longer than
     * the longest so far answers with the entry before it */
    {
        float fd = e.dtype == DT_OUT_OF_WORLD ? 3.4028235e38F : e.fall;
        if ((e.dtype == DT_FALL || e.dtype == DT_OUT_OF_WORLD) && fd > 0.0F && (!c->has_fall || fd > c->fall_len))
        {
            c->fall_entry = c->n > 0 ? c->last : e;
            c->has_fall = 1;
            c->fall_len = fd;
        }
    }

    /* func_94550_c over attacker kinds (func_94060_bK's killer) */
    if (e.kind == SK_PLAYER && (c->k_player < 0 || e.damage > c->kd_player))
    {
        c->kd_player = e.damage;
        c->k_player = e.kind;
    }
    if (e.kind >= 0 && (c->k_best < 0 || e.damage > c->kd_best))
    {
        c->kd_best = e.damage;
        c->k_best = e.kind;
    }
    chatmsg_combat_add(c, &e);

    c->last = e;
    if (c->n < INT_MAX) ++c->n;
    c->last_tick = p->sv.ticks_existed;
    c->active = 1;

    /* func_94559_f: a living attacker, and the fighter alive after the hit */
    if (attacker >= 0 && !c->in_combat && p->sv.health > 0.0F) c->in_combat = 1;
}

/* EntityLivingBase.func_94060_bK: the tracker's best living attacker (a
 * player when it dealt at least a third of the best), else attackingPlayer
 * (never set on a player here), else the revenge target. */
static int combat_killer(const struct server_player *p)
{
    const struct surv_combat *c = &p->sv.combat;

    if (c->n > 0 && c->k_player >= 0 && c->kd_player >= c->kd_best / 3.0F) return SK_PLAYER;
    if (c->n > 0 && c->k_best >= 0) return c->k_best;
    return c->revenge_kind;
}

/* --------------------------------------------------------- the file forms */

static char *text_copy(const char *t)
{
    size_t n = strlen(t) + 1;
    char *c = malloc(n);
    if (c != NULL) memcpy(c, t, n);
    return c;
}

/* The biome id (a progress member) of a biomeName, -1 unknown. */
static int biome_by_name(const char *name)
{
    for (int i = 0; i < 256; ++i)
        if (BIOMES[i].name != NULL && !strcmp(BIOMES[i].name, name)) return i;
    return -1;
}

/* One map of the vanilla file form into (v, present) and, for the server's
 * own file, the progress set: StatisticsFile.func_150881_a. */
static void stats_read_map(const struct jval *o, int32_t *v, uint8_t *present, struct surv_stats *progress)
{
    for (int i = 0; o != NULL && i < o->nfields; ++i)
    {
        int s = surv_stat_by_name(o->fields[i].key);
        if (s < 0) continue;

        const struct jval *val = o->fields[i].val;
        int64_t x = 0;

        if (val->kind == J_NUM) json_int(val, &x);
        else if (val->kind == J_OBJ)
        {
            const struct jval *vv = json_get(val, "value");
            if (vv != NULL && vv->kind == J_NUM) json_int(vv, &x);

            /* the progress: only exploreAllBiomes carries a class
             * (func_150953_b(JsonSerializableSet.class)) */
            const struct jval *pr = json_get(val, "progress");
            if (pr != NULL && progress != NULL && s == ACH_EXPLORE_ALL_BIOMES)
            {
                progress->has_progress = 1;
                memset(progress->explored, 0, sizeof progress->explored);

                for (int k = 0; k < json_len(pr); ++k)
                {
                    const char *name = json_str(json_at(pr, k));
                    int b = name != NULL ? biome_by_name(name) : -1;
                    if (b >= 0) progress->explored[b >> 3] |= (uint8_t)(1u << (b & 7));
                }
            }
        }

        v[s] = (int32_t)x;
        STAT_SET(present, s);
    }
}

int surv_stats_load_file(struct surv_stats *st, const char *json_text)
{
    char *copy = text_copy(json_text);
    struct jval *root = copy ? json_parse(copy) : NULL;
    if (root == NULL || root->kind != J_OBJ) { json_free(root); return 1; }

    /* the map's clear and putAll */
    memset(st->v, 0, sizeof st->v);
    memset(st->present, 0, sizeof st->present);
    memset(st->explored, 0, sizeof st->explored);
    st->has_progress = 0;
    stats_read_map(root, st->v, st->present, st);
    json_free(root);
    return 0;
}

int surv_stats_load_snapshot(struct server_player *sp, struct client_player *cp,
                             const char *json_text, int server_tick)
{
    char *copy = text_copy(json_text);
    struct jval *root = copy ? json_parse(copy) : NULL;
    if (root == NULL || root->kind != J_OBJ) { json_free(root); return 1; }

    if (sp != NULL)
    {
        struct surv_stats *st = &sp->sv.stats;
        int64_t x = 0, tc = 0;

        memset(st->v, 0, sizeof st->v);
        memset(st->present, 0, sizeof st->present);
        memset(st->dirty, 0, sizeof st->dirty);
        memset(st->explored, 0, sizeof st->explored);
        st->has_progress = 0;
        stats_read_map(json_get(root, "file"), st->v, st->present, st);

        const struct jval *d = json_get(root, "dirty");
        for (int i = 0; i < json_len(d); ++i)
        {
            int s = surv_stat_by_name(json_str(json_at(d, i)));
            if (s >= 0) STAT_SET(st->dirty, s);
        }

        json_int(json_get(root, "tickCounter"), &tc);
        if (json_int(json_get(root, "lastSend"), &x)) st->last_send_tick = (int)(x - tc) + server_tick;
        const struct jval *ad = json_get(root, "achDirty");
        st->ach_dirty = ad != NULL && ad->kind == J_BOOL && ad->boolean;
    }

    const struct jval *c = json_get(root, "client");
    if (cp != NULL && c != NULL)
    {
        struct surv_stats *st = &cp->sv.stats;
        memset(st->client_v, 0, sizeof st->client_v);
        memset(st->client_present, 0, sizeof st->client_present);
        stats_read_map(json_get(c, "file"), st->client_v, st->client_present, NULL);
        ++st->client_writes;

        const struct jval *h = json_get(c, "has");
        if (h != NULL && h->kind == J_BOOL) st->has_stats = h->boolean;
        h = json_get(c, "hint");
        if (h != NULL && h->kind == J_BOOL) st->show_hint = h->boolean;
    }

    json_free(root);
    return 0;
}

/* One map against the oracle's: every oracle entry held with its value (and
 * the progress set), and no native entry the oracle lacks. */
static int stats_cmp_map(const struct jval *o, const int32_t *v, const uint8_t *present,
                         const struct surv_stats *progress, const char *what, char *why, size_t n)
{
    uint8_t seen[STAT_BITS];
    char name[64];

    memset(seen, 0, sizeof seen);

    for (int i = 0; o != NULL && i < o->nfields; ++i)
    {
        const char *key = o->fields[i].key;
        int s = surv_stat_by_name(key);
        if (s < 0) { snprintf(why, n, "%s: the oracle's %s has no native id", what, key); return 1; }
        STAT_SET(seen, s);

        const struct jval *val = o->fields[i].val;
        int64_t x = 0;
        const struct jval *pr = NULL;

        if (val->kind == J_NUM) json_int(val, &x);
        else
        {
            json_int(json_get(val, "value"), &x);
            pr = json_get(val, "progress");
        }

        if (!STAT_BIT(present, s)) { snprintf(why, n, "%s: %s missing (oracle %lld)", what, key, (long long)x); return 1; }
        if (v[s] != x) { snprintf(why, n, "%s: %s oracle %lld native %d", what, key, (long long)x, v[s]); return 1; }

        if (progress != NULL && s == ACH_EXPLORE_ALL_BIOMES)
        {
            if ((pr != NULL) != (progress->has_progress != 0))
            {
                snprintf(why, n, "%s: %s progress oracle %s native %s", what, key, pr ? "set" : "none",
                         progress->has_progress ? "set" : "none");
                return 1;
            }

            uint8_t want[32];
            memset(want, 0, sizeof want);
            for (int k = 0; pr != NULL && k < json_len(pr); ++k)
            {
                int b = biome_by_name(json_str(json_at(pr, k)));
                if (b < 0) { snprintf(why, n, "%s: unknown biome %s", what, json_str(json_at(pr, k))); return 1; }
                want[b >> 3] |= (uint8_t)(1u << (b & 7));
            }

            for (int b = 0; b < 256; ++b)
                if (((want[b >> 3] ^ progress->explored[b >> 3]) >> (b & 7)) & 1)
                {
                    snprintf(why, n, "%s: %s progress %s %s", what, key, BIOMES[b].name,
                             (want[b >> 3] >> (b & 7)) & 1 ? "missing" : "extra");
                    return 1;
                }
        }
    }

    for (int s = 0; s < STAT_COUNT; ++s)
        if (STAT_BIT(present, s) && !STAT_BIT(seen, s))
        {
            snprintf(why, n, "%s: native %s = %d, the oracle has no entry", what, surv_stat_name(s, name, sizeof name), v[s]);
            return 1;
        }

    return 0;
}

int surv_stats_compare(const struct server_player *sp, const struct client_player *cp,
                       const char *json_line, int server_tick, char *why, size_t n)
{
    char *copy = text_copy(json_line);
    struct jval *root = copy ? json_parse(copy) : NULL;
    int bad = 0;
    char name[64];

    if (root == NULL || root->kind != J_OBJ) { snprintf(why, n, "the stats line does not parse"); json_free(root); return 1; }

    const struct surv_stats *st = &sp->sv.stats;
    bad = stats_cmp_map(json_get(root, "file"), st->v, st->present, st, "file", why, n);

    if (!bad)
    {
        uint8_t want[STAT_BITS];
        const struct jval *d = json_get(root, "dirty");
        memset(want, 0, sizeof want);

        for (int i = 0; i < json_len(d) && !bad; ++i)
        {
            int s = surv_stat_by_name(json_str(json_at(d, i)));
            if (s < 0) { snprintf(why, n, "dirty: the oracle's %s has no native id", json_str(json_at(d, i))); bad = 1; }
            else STAT_SET(want, s);
        }

        for (int s = 0; s < STAT_COUNT && !bad; ++s)
            if (STAT_BIT(want, s) != STAT_BIT(st->dirty, s))
            {
                snprintf(why, n, "dirty: %s oracle %d native %d", surv_stat_name(s, name, sizeof name),
                         STAT_BIT(want, s), STAT_BIT(st->dirty, s));
                bad = 1;
            }
    }

    if (!bad)
    {
        int64_t ls = 0, tc = 0;
        json_int(json_get(root, "lastSend"), &ls);
        json_int(json_get(root, "tickCounter"), &tc);
        const struct jval *ad = json_get(root, "achDirty");
        int want_ad = ad != NULL && ad->kind == J_BOOL && ad->boolean;

        if (ls - tc != (int64_t)(st->last_send_tick - server_tick))
        {
            snprintf(why, n, "lastSend - tickCounter: oracle %lld native %d", (long long)(ls - tc),
                     st->last_send_tick - server_tick);
            bad = 1;
        }
        else if (want_ad != (st->ach_dirty != 0))
        {
            snprintf(why, n, "achDirty: oracle %d native %d", want_ad, st->ach_dirty);
            bad = 1;
        }
    }

    const struct jval *c = json_get(root, "client");
    if (!bad && c != NULL && cp != NULL)
    {
        const struct surv_stats *cs = &cp->sv.stats;
        bad = stats_cmp_map(json_get(c, "file"), cs->client_v, cs->client_present, NULL, "client.file", why, n);

        const struct jval *h = json_get(c, "has");
        if (!bad && h != NULL && h->kind == J_BOOL && h->boolean != (cs->has_stats != 0))
        {
            snprintf(why, n, "client.has: oracle %d native %d", h->boolean, cs->has_stats);
            bad = 1;
        }

        h = json_get(c, "hint");
        if (!bad && h != NULL && h->kind == J_BOOL && h->boolean != (cs->show_hint != 0))
        {
            snprintf(why, n, "client.hint: oracle %d native %d", h->boolean, cs->show_hint);
            bad = 1;
        }
    }

    json_free(root);
    return bad;
}

/* The toast's clock: agent-mode Minecraft.getSystemTime is Oracle.tick * 50,
 * and the client replay's Oracle.tick is the row tick. */
static long long toast_clock(const struct client_player *p)
{
    return p->client_row_tick * 50L;
}

/* The GuiAchievement unlock (func_146256_a): "Achievement get!" over the
 * achievement's name, its clock now. */
static void toast_unlock(struct client_player *p, int ach)
{
    struct surv_stats *st = &p->sv.stats;

    st->toast_ach = ach;
    st->toast_l = toast_clock(p);
    st->toast_desc = 0;
    st->toast_title = TOAST_TITLE_UNLOCK;
    st->toast_sub = surv_ach_name(ach);
}

/* The GuiAchievement hint (func_146255_b): the name over the description,
 * its clock 2500 ahead. */
static void toast_hint(struct client_player *p)
{
    struct surv_stats *st = &p->sv.stats;

    st->toast_ach = ACH_OPEN_INVENTORY;
    st->toast_l = toast_clock(p) + 2500L;
    st->toast_desc = 1;
    st->toast_title = surv_ach_name(ACH_OPEN_INVENTORY);
    st->toast_sub = surv_ach_desc(ACH_OPEN_INVENTORY);
}

void surv_client_join_hint(struct client_player *p, long long join_ms)
{
    struct surv_stats *st = &p->sv.stats;

    if (!st->has_stats || !st->show_hint || st->toast_ach >= 0) return;
    for (int id = 0; id < ACH_COUNT; ++id)
        if (st->client_v[id] > 0) return;
    toast_hint(p);
    st->toast_l = join_ms + 2500L;
}

void surv_client_apply_s37(struct client_player *p, const struct s2c_pkt *pkt)
{
    struct surv_stats *st = &p->sv.stats;
    const struct surv_s37 *box = &s37_ring[pkt->s37_slot % SURV_S37_RING];
    int any_ach = 0;

    if (box->seq != pkt->s37_seq)
    {
        fprintf(stderr, "survival: an S37 read after its ring slot was reused\n");
        abort();
    }

    /* handleStatistics' loop: per entry, the toast when the entry is an
     * achievement the mirror did not hold (the writeStat check reads the
     * value the entry is about to overwrite), then the mirror's
     * func_150873_a */
    for (int i = 0; i < box->n; ++i)
    {
        int id = box->id[i];
        int value = box->val[i];

        if (id < ACH_COUNT && value > 0)
        {
            if (st->has_stats && st->client_v[id] == 0)
            {
                toast_unlock(p, id);
                if (id == ACH_OPEN_INVENTORY) st->show_hint = 0;
            }

            any_ach = 1;
        }

        st->client_v[id] = value;
        STAT_SET(st->client_present, id);
        ++st->client_writes;
    }

    if (!st->has_stats && !any_ach && st->show_hint)
        toast_hint(p);

    st->has_stats = 1;
}

/* The toast's draw state at frame time, func_146254_a's gate and its clock
 * clear: 1 while the toast is on screen. The player pointer's null test is
 * the caller's (a live client always has one). */
void surv_client_toast_json(struct client_player *p, long long now_ms, int *on)
{
    struct surv_stats *st = &p->sv.stats;

    if (st->toast_ach < 0 || st->toast_l == 0)
    {
        *on = 0;
        return;
    }

    double var1 = (double)(now_ms - st->toast_l) / 3000.0;
    int visible;

    if (!st->toast_desc)
    {
        if (var1 < 0.0 || var1 > 1.0)
        {
            /* func_146254_a's return path clears the clock */
            st->toast_l = 0;
            visible = 0;
        }
        else visible = 1;
    }
    else if (var1 > 0.5)
    {
        /* the hint's draw caps var1 (the slide stops at half) but keeps
         * drawing until an unlock replaces it */
        visible = 1;
    }
    else visible = 1;

    *on = visible;
}

/* --------------------------------------------------------- load, snapshots */

/* The canonical tree's scalars, the same getters player.c carries. */
static const char *scalar_text(const void *comp, const char *key, char *buf, size_t n)
{
    const nbt *v = nbt_get((const nbt *)comp, key);

    if (!v) return NULL;

    char *text = nbt_render(v);
    snprintf(buf, n, "%s", text);
    free(text);
    size_t len = strlen(buf);
    if (len >= 2 && buf[0] == '"' && buf[len - 1] == '"') { buf[len - 1] = 0; return buf + 1; }
    return buf;
}

static int get_f(const void *comp, const char *key, float *out)
{
    char buf[64];
    const char *s = scalar_text(comp, key, buf, sizeof buf);

    if (!s || s[0] != 'f' || s[1] != ':') return 0;

    uint32_t bits = (uint32_t)strtoul(s + 2, NULL, 16);
    memcpy(out, &bits, 4);
    return 1;
}

static int get_i(const void *comp, const char *key, int *out)
{
    char buf[64];
    const char *s = scalar_text(comp, key, buf, sizeof buf);

    if (!s || s[1] != ':') return 0;

    *out = (int)strtol(s + 2, NULL, 10);
    return 1;
}

/* InventoryEnderChest.loadInventoryFromNBT over the EnderItems list. */
static void read_ender_inventory(struct surv_state *sv, const nbt *tag);

/* ItemStack.readFromNBT's tag: the whole compound, into the tag store. */
static void read_ench(struct surv_stack *st, const nbt *e)
{
    st->tag = itag_from_item(e);
}

/* InventoryPlayer.readFromNBT over the tag's Inventory list. */
static void read_inventory(struct surv_state *sv, const nbt *tag)
{
    const nbt *list = nbt_get(tag, "Inventory");

    if (!list || nbt_kind(list) != NBT_LIST) return;

    for (int i = 0; i < nbt_list_size(list); ++i)
    {
        const nbt *e = nbt_list_get(list, i);
        int slot = 0, item = 0, count = 0, damage = 0;

        if (!e || nbt_kind(e) != NBT_COMPOUND) continue;
        if (!get_i(e, "Slot", &slot) || !get_i(e, "id", &item) || !get_i(e, "Count", &count))
            continue;
        if (!get_i(e, "Damage", &damage)) damage = 0;

        /* InventoryPlayer.readFromNBT: the armor pieces ride at Slot 100..103
         * and land on armorInventory[0..3], the surv_state's inv[36..39] */
        if (slot >= 100 && slot < 104) slot -= 100 - 36;

        if (slot < 0 || slot >= 40 || count <= 0) continue;

        sv->inv[slot].item = item;
        sv->inv[slot].damage = damage;
        sv->inv[slot].count = count;
        read_ench(&sv->inv[slot], e);
    }
}

static void load_common(struct surv_state *sv, const nbt *tag)
{
    /* EntityLivingBase.readEntityFromNBT: HealF (the float health) wins over
     * Health (the short ceil writeEntityToNBT stores) */
    float hf = 0.0F;
    int ih = 0;
    if (get_f(tag, "HealF", &hf)) sv->health = hf;
    else { get_i(tag, "Health", &ih); sv->health = (float)ih; }
    get_i(tag, "foodLevel", &sv->food.level);
    get_f(tag, "foodSaturationLevel", &sv->food.saturation);
    get_f(tag, "foodExhaustionLevel", &sv->food.exhaustion);
    get_i(tag, "foodTickTimer", &sv->food.timer);
    get_f(tag, "XpP", &sv->xp_progress);
    get_i(tag, "XpLevel", &sv->xp_level);
    get_i(tag, "XpTotal", &sv->xp_total);
    get_i(tag, "Score", &sv->score);
    get_i(tag, "Air", &sv->air);
    get_f(tag, "AbsorptionAmount", &sv->absorption);
    read_inventory(sv, tag);
    read_ender_inventory(sv, tag);
}

/* InventoryEnderChest.loadInventoryFromNBT over the EnderItems list. */
static void read_ender_inventory(struct surv_state *sv, const nbt *tag)
{
    const nbt *list = nbt_get(tag, "EnderItems");

    if (!list || nbt_kind(list) != NBT_LIST) return;

    for (int i = 0; i < nbt_list_size(list); ++i)
    {
        const nbt *e = nbt_list_get(list, i);
        int slot = 0, item = 0, count = 0, damage = 0;

        if (!e || nbt_kind(e) != NBT_COMPOUND) continue;
        if (!get_i(e, "Slot", &slot) || !get_i(e, "id", &item) || !get_i(e, "Count", &count))
            continue;
        if (!get_i(e, "Damage", &damage)) damage = 0;

        if (slot < 0 || slot >= 27 || count <= 0) continue;

        sv->ender[slot].item = item;
        sv->ender[slot].damage = damage;
        sv->ender[slot].count = count;
        read_ench(&sv->ender[slot], e);
    }
}

static void sp_attack_from(void *self, int source, float amount);
static int sp_fire_time(void *self, int ticks);
static void sp_fizz(void *self);
static void sp_swim_sound(void *self);
static void sp_walking_block(void *self, int x, int y, int z, int id);

static double pend_d(const nbt *pk, const char *key)
{
    uint64_t b = nbt_double_bits(nbt_get(pk, key));
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

static float pend_f(const nbt *pk, const char *key)
{
    uint32_t b = nbt_float_bits(nbt_get(pk, key));
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

static int pend_i(const nbt *pk, const char *key)
{
    return (int)nbt_int_value(nbt_get(pk, key));
}

/* An ItemStack's canonical NBT (Snapshot.packetStacks) as a surv_stack. */
static void pend_stack(const nbt *item, struct surv_stack *st)
{
    memset(st, 0, sizeof *st);
    if (item == NULL || nbt_get(item, "id") == NULL) return;
    st->item = pend_i(item, "id");
    st->damage = pend_i(item, "Damage");
    st->count = pend_i(item, "Count");
    st->tag = itag_from_item(item);
}

/* player_client.nbt's "pending": every packet the client had received but
 * not pumped at the snapshot (the server's last tick sent them), in queue
 * order, as the s2c records the pump applies. The client player's own
 * entity packets only (the client world's other entities carry no row
 * state); an S20 is the pump's pending_s20 (surv_client_pending_s20). */
static int surv_client_pending_all(const nbt *pend, int self, struct s2c_queue *q)
{
    int n0 = q->n;
    for (int i = 0; i < nbt_list_size(pend) && q->n < S2C_MAX; ++i)
    {
        const nbt *pk = nbt_list_get(pend, i);
        const char *cls = nbt_string_value(nbt_get(pk, "cls"));
        if (cls == NULL) continue;
        struct s2c_pkt *k = &q->q[q->n];
        memset(k, 0, sizeof *k);
        if (!strcmp(cls, "S06PacketUpdateHealth"))
        {
            k->kind = PK_S06;
            k->f0 = pend_f(pk, "field_149336_a");
            k->i0 = pend_i(pk, "field_149334_b");
            k->f1 = pend_f(pk, "field_149335_c");
        }
        else if (!strcmp(cls, "S2FPacketSetSlot"))
        {
            struct surv_stack st;
            pend_stack(nbt_get(pk, "field_149178_c.nbt"), &st);
            k->kind = PK_S2F;
            k->window = pend_i(pk, "field_149179_a");
            k->slot = pend_i(pk, "field_149177_b");
            k->i0 = st.count > 0 ? st.item : 0;
            k->i1 = st.damage;
            k->i2 = st.count;
            k->st = st;
        }
        else if (!strcmp(cls, "S12PacketEntityVelocity"))
        {
            if (pend_i(pk, "field_149417_a") != self) continue;
            k->kind = PK_S12;
            k->f0 = (double)pend_i(pk, "field_149415_b") / 8000.0;
            k->f1 = (double)pend_i(pk, "field_149416_c") / 8000.0;
            k->f2 = (double)pend_i(pk, "field_149414_d") / 8000.0;
        }
        else if (!strcmp(cls, "S08PacketPlayerPosLook"))
        {
            k->kind = PK_S08;
            k->f0 = pend_d(pk, "field_148940_a");
            k->f1 = pend_d(pk, "field_148938_b");
            k->f2 = pend_d(pk, "field_148939_c");
            k->f3 = pend_f(pk, "field_148936_d");
            k->f4 = pend_f(pk, "field_148937_e");
            k->i0 = pend_i(pk, "field_148935_f");
        }
        else if (!strcmp(cls, "S19PacketEntityStatus"))
        {
            /* EntityPlayer.handleHealthUpdate's 9: the item use finished */
            if (pend_i(pk, "field_149164_a") != self || pend_i(pk, "field_149163_b") != 9) continue;
            k->kind = PK_S19;
        }
        else if (!strcmp(cls, "S1DPacketEntityEffect"))
        {
            if (pend_i(pk, "field_149434_a") != self) continue;
            k->kind = PK_S1D;
            k->i0 = pend_i(pk, "field_149432_b") & 255;
            k->i1 = pend_i(pk, "field_149433_c");
            k->i2 = pend_i(pk, "field_149431_d");
        }
        else if (!strcmp(cls, "S1EPacketRemoveEntityEffect"))
        {
            if (pend_i(pk, "field_149079_a") != self) continue;
            k->kind = PK_S1E;
            k->i0 = pend_i(pk, "field_149078_b");
        }
        else if (!strcmp(cls, "S1BPacketEntityAttach"))
        {
            if (pend_i(pk, "field_149408_a") != 0 || pend_i(pk, "field_149406_b") != self) continue;
            k->kind = PK_S1B;
            k->i0 = pend_i(pk, "field_149407_c");
        }
        else if (!strcmp(cls, "S28PacketEffect"))
        {
            /* type a, the block c d e, the data b; a global one (f) is
             * playBroadcastSound, which draws nothing */
            if (pend_i(pk, "field_149246_f")) continue;
            k->kind = PK_S28;
            k->i0 = pend_i(pk, "field_149251_a");
            k->i1 = pend_i(pk, "field_149249_b");
            k->f0 = pend_i(pk, "field_149250_c");
            k->f1 = pend_i(pk, "field_149247_d");
            k->f2 = pend_i(pk, "field_149248_e");
        }
        else if (!strcmp(cls, "S2BPacketChangeGameState"))
        {
            k->kind = PK_S2B;
            k->i0 = pend_i(pk, "field_149140_b");
        }
        else if (!strcmp(cls, "S0BPacketAnimation"))
        {
            /* the player's own wake, and the crit animations of any entity */
            int anim = pend_i(pk, "field_148980_b");
            if (pend_i(pk, "field_148981_a") != self && anim != 4 && anim != 5) continue;
            k->kind = PK_S0B;
            k->i0 = anim;
            k->i1 = pend_i(pk, "field_148981_a");
        }
        else if (!strcmp(cls, "S0APacketUseBed"))
        {
            if (pend_i(pk, "field_149097_a") != self) continue;
            k->kind = PK_S0A;
            k->i0 = pend_i(pk, "field_149095_b");
            k->i1 = pend_i(pk, "field_149096_c");
            k->i2 = pend_i(pk, "field_149094_d");
        }
        else if (!strcmp(cls, "S31PacketWindowProperty"))
        {
            k->kind = PK_S31;
            k->i0 = pend_i(pk, "field_149186_a");
            k->i1 = pend_i(pk, "field_149184_b");
            k->i2 = pend_i(pk, "field_149185_c");
        }
        else if (!strcmp(cls, "S2EPacketCloseWindow"))
        {
            k->kind = PK_S2E;
            k->i0 = pend_i(pk, "field_148896_a");
        }
        else if (!strcmp(cls, "S2DPacketOpenWindow"))
        {
            /* NetHandlerPlayClient.handleOpenWindow's inventory types */
            int type = pend_i(pk, "field_148907_b");
            int kind = type == 0 ? CONTAINER_CHEST : type == 1 ? CONTAINER_WORKBENCH : type == 2 ? CONTAINER_FURNACE :
                       type == 3 || type == 10 ? CONTAINER_DISPENSER : type == 6 ? CONTAINER_MERCHANT :
                       type == 9 ? CONTAINER_HOPPER : -1;
            if (kind < 0) continue;
            k->kind = PK_S2D;
            k->i0 = pend_i(pk, "field_148909_a");
            k->i1 = kind;
            k->i2 = pend_i(pk, "field_148905_d");
        }
        else if (!strcmp(cls, "S30PacketWindowItems") && pend_i(pk, "field_148914_a") != 0)
        {
            /* an open window's S30: its tile slots, as the S2Fs the native
             * window model takes them in (the 36 player slots after them
             * are the client's already) */
            const nbt *items = nbt_get(pk, "field_148913_b.nbt");
            int total = items ? nbt_list_size(items) : 0;
            int window = pend_i(pk, "field_148914_a");
            for (int s2 = 0; s2 < total - 36 && q->n < S2C_MAX; ++s2)
            {
                struct surv_stack st;
                pend_stack(nbt_list_get(items, s2), &st);
                struct s2c_pkt *k2 = &q->q[q->n++];
                memset(k2, 0, sizeof *k2);
                k2->kind = PK_S2F;
                k2->window = window;
                k2->slot = s2;
                k2->i0 = st.count > 0 ? st.item : 0;
                k2->i1 = st.damage;
                k2->i2 = st.count;
                k2->st = st;
            }
            continue;
        }
        else if (!strcmp(cls, "S30PacketWindowItems"))
        {
            if (pend_i(pk, "field_148914_a") != 0) continue;
            const nbt *items = nbt_get(pk, "field_148913_b.nbt");
            k->kind = PK_S30;
            struct surv_stack *kinv = s2c_side_new(q, k, S30_STACKS * sizeof *kinv);
            for (int s = 0; s < (items ? nbt_list_size(items) : 0); ++s)
            {
                int inv = container_slot_inventory(s);
                if (inv >= 0) pend_stack(nbt_list_get(items, s), &kinv[inv]);
                else if (s >= 1 && s <= 4) pend_stack(nbt_list_get(items, s), &kinv[40 + s - 1]);
            }
        }
        else if (!strcmp(cls, "S07PacketRespawn"))
        {
            k->kind = PK_S07;
            k->i0 = pend_i(pk, "field_149088_a");
        }
        else continue;
        ++q->n;
    }
    return q->n - n0;
}

/* The client player's entity id (Entity.entityId, EntityState's "rt"). */
static int surv_client_self(const nbt *tree)
{
    const nbt *id = nbt_get(nbt_get(nbt_get(tree, "rt"), "f"), "field_145783_c");
    return id ? (int)nbt_int_value(id) : -1;
}

void surv_client_pending_s20(const void *tree, struct client_player *p)
{
    const nbt *pend = nbt_get((const nbt *)tree, "pending");
    int self = surv_client_self((const nbt *)tree);
    for (int i = 0; i < (pend ? nbt_list_size(pend) : 0); ++i)
    {
        const nbt *pk = nbt_list_get(pend, i);
        const char *cls = nbt_string_value(nbt_get(pk, "cls"));
        if (cls == NULL || strcmp(cls, "S20PacketEntityProperties") || pend_i(pk, "field_149445_a") != self) continue;
        p->pending_s20 = 1;
        p->pending_s20_mod = pend_i(pk, "sprintMod");
        /* the potions' modifiers the packet carries (a rejoin's client
         * has none of its own yet: pfc-potattr-g26); a snapshot that names
         * neither keeps them as the client holds them */
        p->pending_s20_slow = p->mod_slow;
        p->pending_s20_speed = p->mod_speed;
        if (nbt_get(pk, "speedAmp") || nbt_get(pk, "slowAmp"))
        {
            p->pending_s20_slow = nbt_get(pk, "slowAmp") ? pend_i(pk, "slowAmp") : -1;
            p->pending_s20_speed = nbt_get(pk, "speedAmp") ? pend_i(pk, "speedAmp") : -1;
        }
    }
}

int surv_client_pending(const void *tree, struct s2c_queue *q)
{
    const nbt *pend = nbt_get((const nbt *)tree, "pending");
    if (pend != NULL) return surv_client_pending_all(pend, surv_client_self((const nbt *)tree), q);
    const nbt *f = nbt_get((const nbt *)tree, "fields");
    const nbt *l = f ? nbt_get(f, "pendingS06") : NULL;
    int n = l ? nbt_list_size(l) : 0;

    for (int i = 0; i < n && q->n < S2C_MAX; ++i)
    {
        const nbt *a = nbt_list_get(l, i);
        struct s2c_pkt *pkt = &q->q[q->n++];
        uint32_t hb = nbt_float_bits(nbt_list_get(a, 0)), sb = nbt_float_bits(nbt_list_get(a, 2));

        float h, sat;

        memcpy(&h, &hb, sizeof h);
        memcpy(&sat, &sb, sizeof sat);
        memset(pkt, 0, sizeof *pkt);
        pkt->kind = PK_S06;
        pkt->f0 = h;
        pkt->i0 = (int)nbt_int_value(nbt_list_get(a, 1));
        pkt->f1 = sat;
    }
    return n;
}

int surv_client_load(struct client_player *p, const void *tree)
{
    const nbt *t = (const nbt *)tree;
    struct surv_state *sv = &p->sv;
    const nbt *f = nbt_get(t, "fields");
    const nbt *tag = nbt_get(t, "nbt");

    if (!f || !tag) return 0;

    memset(sv, 0, sizeof *sv);
    sv->max_hurt_resistant = 20;
    sv->max_hurt_time = 10;
    sv->using_slot = -1;
    potion_map_init(&sv->potions);

    load_common(sv, tag);
    sv->hud_prev_food = 20; /* the client never calls FoodStats.onUpdate */
    /* the client player's own effects (a mid-run snapshot's; the S1Ds put
     * them there) */
    {
        const nbt *fx = nbt_get(tag, "ActiveEffects");
        for (int i = 0; fx && i < nbt_list_size(fx); ++i)
        {
            const nbt *c = nbt_list_get(fx, i);
            struct potion_effect eff = {
                .id = (uint8_t)nbt_int_value(nbt_get(c, "Id")),
                .amplifier = (int8_t)nbt_int_value(nbt_get(c, "Amplifier")),
                .duration = (int)nbt_int_value(nbt_get(c, "Duration")),
                .is_splash = 0,
                .is_ambient = (uint8_t)nbt_int_value(nbt_get(c, "Ambient")),
            };
            potion_map_put(&sv->potions, &eff);
        }
        const nbt *nu = nbt_get(nbt_get(nbt_get(t, "rt"), "f"), "potionsNeedUpdate");
        if (nu) sv->potions_need_update = (uint8_t)nbt_int_value(nu);
    }

    /* the statistics mirror past the join: its S37 has arrived (the map is
     * the snapshot's stats.json, surv_stats_load_snapshot), no toast up */
    sv->stats.has_stats = 1;
    sv->stats.show_hint = 1;
    sv->stats.toast_ach = -1;

    for (int i = 0; i < 40; ++i) sv->inv[i].gen = ++sv->gen_counter;

    get_i(tag, "HurtTime", &sv->hurt_time);
    get_i(tag, "AttackTime", &sv->attack_time);
    get_i(tag, "DeathTime", &sv->death_time);
    get_i(f, "hurtResistantTime", &sv->hurt_resistant_time);
    get_i(f, "recentlyHit", &sv->recently_hit);
    get_i(f, "xpCooldown", &sv->xp_cooldown);
    get_i(f, "hasSetHealth", &sv->has_set_health);
    get_i(f, "currentItem", &sv->current_item);
    /* the client player's own Random (a snapshot before lane/useitems has
     * none): ItemStack.damageItem's Unbreaking roll on the client draws it */
    if (nbt_get(f, "entityRand")) sv->erand.r.seed = (uint64_t)nbt_int_value(nbt_get(f, "entityRand"));

    /* PlayerControllerMP.currentPlayerItem: the join's first updateController
     * already sent the C09 for the slot the S09 set, so the controller holds
     * the loaded slot (a checkpoint start's may be nonzero) */
    p->hotbar = sv->current_item;
    p->synced_item = sv->current_item;
    get_i(f, "itemInUseSlot", &sv->using_slot);
    get_i(f, "itemInUseCount", &sv->using_count);

    if (sv->using_slot >= 0 && sv->using_slot < 40 && sv->using_count > 0)
        sv->using_gen = sv->inv[sv->using_slot].gen;
    else
        sv->using_slot = -1;

    p->e.fire = 0;   /* the client's base tick zeroes it */
    return 1;
}

int surv_server_load(struct server_player *p, const void *tree)
{
    const nbt *t = (const nbt *)tree;
    struct surv_state *sv = &p->sv;
    const nbt *f = nbt_get(t, "fields");
    const nbt *tag = nbt_get(t, "nbt");

    if (!f || !tag) return 0;

    memset(sv, 0, sizeof *sv);
    sv->max_hurt_resistant = 20;
    sv->max_hurt_time = 10;
    sv->using_slot = -1;
    potion_map_init(&sv->potions);   /* the server's copy stays empty */

    int iv = 0;
    float fv = 0.0F;

    load_common(sv, tag);
    get_i(tag, "HurtTime", &sv->hurt_time);
    get_i(tag, "AttackTime", &sv->attack_time);
    get_i(tag, "DeathTime", &sv->death_time);
    get_i(tag, "Fire", &iv);
    p->e.fire = iv;
    get_i(tag, "SpawnX", &sv->spawn_x);
    get_i(tag, "SpawnY", &sv->spawn_y);
    get_i(tag, "SpawnZ", &sv->spawn_z);
    get_i(tag, "SpawnForced", &sv->spawn_forced);
    sv->has_spawn = nbt_get(tag, "SpawnX") != NULL;

    get_i(f, "hurtResistantTime", &sv->hurt_resistant_time);
    get_i(f, "recentlyHit", &sv->recently_hit);
    get_i(f, "xpCooldown", &sv->xp_cooldown);
    get_i(f, "ticksExisted", &sv->ticks_existed);
    get_i(f, "dimension", &p->dimension);

    /* a fresh StatisticsFile until the snapshot's stats.json says otherwise
     * (surv_stats_load_snapshot); the CombatTracker and the revenge target
     * start empty (not in the snapshot) */
    sv->stats.toast_ach = -1;
    sv->stats.last_send_tick = -300;
    sv->combat.revenge_kind = -1;
    sv->combat.revenge_ref = 0;
    /* Entity.velocityChanged: a hit this tick (setBeenAttacked) whose S12
     * the tracker entry sends in the next tick's entity pass */
    get_i(nbt_get(nbt_get(t, "rt"), "f"), "velocityChanged", &sv->velocity_changed);
    /* a snapshot between the death and the respawn: onDeath has run (a
     * dead player takes no damage), and onDeathUpdate's setDead at deathTime
     * 20 takes it out of loadedEntityList in the next entity pass */
    get_i(nbt_get(nbt_get(t, "rt"), "f"), "isDead", &sv->dead);
    /* Entity.inWater and firstUpdate as the join ticks left them: a player
     * who joined in water has entered it already, so the first update plays
     * no splash on the player's Random */
    {
        int in_water = -1, first = -1;
        if (get_i(nbt_get(nbt_get(t, "rt"), "f"), "inWater", &in_water)) p->e.in_water = (uint8_t)(in_water != 0);
        if (get_i(nbt_get(nbt_get(t, "rt"), "f"), "firstUpdate", &first)) p->e.first_update = first != 0;
    }
    get_i(f, "field_147101_bU", &sv->respawn_protect);
    get_i(f, "lastFoodLevel", &sv->last_food);
    get_i(f, "wasHungry", &sv->was_hungry);
    get_i(f, "currentItem", &sv->current_item);
    if (nbt_get(f, "entityRand")) sv->erand.r.seed = (uint64_t)nbt_int_value(nbt_get(f, "entityRand"));
    /* an old snapshot without the gate reads as a fresh player's
     * (-1e8: the first tick sends, and no join S06 is in flight) */
    fv = -1.0E8F;
    get_f(f, "lastHealth", &fv);
    sv->last_health = fv;
    get_f(f, "lastDamage", &sv->last_damage);
    get_i(f, "currentItem", &sv->current_item);
    get_i(f, "itemInUseSlot", &sv->using_slot);
    get_i(f, "itemInUseCount", &sv->using_count);

    if (sv->using_slot >= 0 && sv->using_slot < 40 && sv->using_count > 0)
        sv->using_gen = sv->inv[sv->using_slot].gen;
    else
        sv->using_slot = -1;

    /* the container's mirror as the join left it */
    for (int s = 0; s < 45; ++s)
    {
        int i = container_slot_inventory(s);
        sv->mirror[s] = i >= 0 ? sv->inv[i] : empty_stack();
    }
    /* or as a mid-run snapshot recorded it (Container.inventoryItemStacks:
     * a change detectAndSendChanges has not sent yet) */
    {
        const nbt *m = nbt_get(t, "mirror");
        for (int s = 0; m && s < 45 && s < nbt_list_size(m); ++s)
        {
            struct surv_stack st;
            pend_stack(nbt_list_get(m, s), &st);
            sv->mirror[s] = st.count > 0 ? st : empty_stack();
        }
    }

    /* the attack_from callback Entity.moveEntity reaches */
    p->e.self = p;
    p->e.attack_from = sp_attack_from;
    p->e.fire_time = sp_fire_time;
    p->e.fizz = sp_fizz;
    p->e.swim_sound = sp_swim_sound;

    /* Block.onEntityWalking at the walking step's base cell: the redstone
     * ore's light-up draws from the world Random (the server's own) */
    p->e.walking_block = sp_walking_block;

    return 1;
}

/* ------------------------------------------------------------ the inventory */

/* InventoryPlayer.storePartialItemStack: what did not fit. */
static int store_partial_item_stack(struct surv_state *sv, struct surv_stack *st)
{
    const struct item_def *d = &ITEMS[st->item];
    int max = d->exists ? d->max_stack_size : 64;

    if (max == 1)
    {
        int slot = -1;

        for (int i = 0; i < 36; ++i)
            if (sv->inv[i].count == 0) { slot = i; break; }

        if (slot < 0) return st->count;

        sv->inv[slot] = *st;
        sv->inv[slot].gen = ++sv->gen_counter;
        return 0;
    }

    int slot = -1;

    for (int i = 0; i < 36; ++i)
    {
        struct surv_stack *s = &sv->inv[i];

        if (s->count > 0 && s->item == st->item && s->count < max && s->count < 64 &&
            (!ITEMS[s->item].has_subtypes || s->damage == st->damage) && s->tag == st->tag)
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
        for (int i = 0; i < 36; ++i)
            if (sv->inv[i].count == 0) { slot = i; break; }

    if (slot < 0) return st->count;

    struct surv_stack *s = &sv->inv[slot];

    if (s->count == 0)
    {
        /* new ItemStack(item, 0, damage), plus a copy of the tag */
        s->item = st->item;
        s->damage = st->damage;
        s->tag = st->tag;
        s->gen = ++sv->gen_counter;
    }

    int move = st->count;

    if (move > max - s->count) move = max - s->count;
    if (move > 64 - s->count) move = 64 - s->count;

    if (move == 0) return st->count;

    st->count -= move;
    s->count += move;
    return st->count;
}

/* InventoryPlayer.addItemStackToInventory. 1 when the stack fit. */
static int add_item_stack_to_inventory(struct surv_state *sv, struct surv_stack *st)
{
    if (st->count == 0 || !ITEMS[st->item].exists) return 0;

    if (ITEMS[st->item].max_damage > 0 && st->damage > 0)
    {
        for (int i = 0; i < 36; ++i)
        {
            if (sv->inv[i].count == 0)
            {
                sv->inv[i] = *st;
                sv->inv[i].gen = ++sv->gen_counter;
                st->count = 0;
                return 1;
            }
        }

        return 0;
    }

    int before;

    do
    {
        before = st->count;
        st->count = store_partial_item_stack(sv, st);
    } while (st->count > 0 && st->count < before);

    return st->count < before;
}

/* InventoryPlayer.decrStackSize. */
static struct surv_stack decr_stack_size(struct surv_state *sv, int slot, int n)
{
    struct surv_stack out = empty_stack();

    if (sv->inv[slot].count == 0) return out;

    if (sv->inv[slot].count <= n)
    {
        out = sv->inv[slot];
        sv->inv[slot] = empty_stack();
        sv->inv[slot].gen = ++sv->gen_counter;
        return out;
    }

    out = sv->inv[slot];
    out.count = n;
    sv->inv[slot].count -= n;
    return out;
}

/* InventoryPlayer.dropAllItems, through func_146097_a. */
static void drop_all_items(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    for (int i = 0; i < 40; ++i)
    {
        if (sv->inv[i].count > 0)
        {
            throw_item(p, &sv->inv[i], 1);
            sv->inv[i] = empty_stack();
            sv->inv[i].gen = ++sv->gen_counter;
        }
    }
}

/* A window other than the inventory opens on the server: the inventory
 * container's list stays as it is until the window closes. */
static void gui_window_opened(struct server_player *p)
{
    if (p->sv.mirror0_valid) return;
    memcpy(p->sv.mirror0, p->sv.mirror, sizeof p->sv.mirror0);
    p->sv.mirror0_valid = 1;
}

/* closeContainer's openContainer = inventoryContainer: its own list again */
static void gui_window_closed(struct server_player *p)
{
    if (!p->sv.mirror0_valid) return;
    memcpy(p->sv.mirror, p->sv.mirror0, sizeof p->sv.mirror);
    p->sv.mirror0_valid = 0;
}

/* Container.detectAndSendChanges over the player container's 45 slots. */
static void detect_and_send_changes(struct server_player *p)
{
    struct surv_state *sv = &p->sv;
    /* another window has no craft result, grid or armour slots */
    int first = p->open_container != NULL && p->open_container != &p->own_container ? 9 : 0;

    for (int s = first; s < 45; ++s)
    {
        struct surv_stack st = server_slot_stack(p, s);

        if (!stack_eq(&st, &sv->mirror[s]))
        {
            sv->mirror[s] = st;
            queue_s2f(p, s);
        }
    }
}

/* EntityPlayerMP.onItemPickup's openContainer.detectAndSendChanges: with a
 * tile window open, the window's tile slots, the player's slots, then
 * ContainerFurnace's S31 tail (surv_server_world_update's order). */
static void pickup_detect_and_send_changes(struct server_player *p);

/* -------------------------------------------------------------------- food */

/* EntityPlayer.addExhaustion (FoodStats.addExhaustion). */
static void add_exhaustion(struct surv_state *sv, float v)
{
    float e = sv->food.exhaustion + v;
    sv->food.exhaustion = e > 40.0F ? 40.0F : e;
}

void surv_add_exhaustion(struct server_player *p, float amount)
{
    add_exhaustion(&p->sv, amount);
}

/* FoodStats.addStats(int, float). */
static void food_add_stats(struct surv_state *sv, int n, float mod)
{
    int level = sv->food.level + n;

    sv->food.level = level < 20 ? level : 20;
    float sat = sv->food.saturation + (float)n * mod * 2.0F;
    sv->food.saturation = sat < (float)sv->food.level ? sat : (float)sv->food.level;
    if (sv->food.saturation < 0.0F) sv->food.saturation = 0.0F;
}

/* ItemFood.func_151686_a through onEaten. */

/* EntityPlayer.xpBarCap. */
static int xp_bar_cap(const struct surv_state *sv)
{
    if (sv->xp_level >= 30) return 62 + (sv->xp_level - 30) * 7;
    if (sv->xp_level >= 15) return 17 + (sv->xp_level - 15) * 3;
    return 17;
}

/* EntityPlayer.addExperience. */
void surv_server_add_xp(struct server_player *p, int points)
{
    struct surv_state *sv = &p->sv;

    sv->score += points; /* EntityPlayer.addExperience calls addScore first. */

    sv->xp_progress += (float)points / (float)xp_bar_cap(sv);

    for (sv->xp_total += points; sv->xp_progress >= 1.0F; sv->xp_progress /= (float)xp_bar_cap(sv))
    {
        sv->xp_progress = (sv->xp_progress - 1.0F) * (float)xp_bar_cap(sv);
        sv->xp_level += 1;
    }
}

/* --------------------------------------------------------- damage pipeline */

/* EntityLivingBase.renderBrokenItemStack on the server: the break sound's
 * World.rand pitch, then five particles' entity Random and Math.random
 * draws. */
static void break_stack_fx(struct server_player *p)
{
    /* the live World.rand: a mob's turn in the pass swaps it into the mob
     * pool, and blockcb_env names whichever copy is live */
    jrand *wr = nw_env->blockcb.env.world_rand != NULL ? nw_env->blockcb.env.world_rand : surv.world_rand;
    if (wr) (void)jr_float(wr);

    for (int i = 0; i < 5; ++i)
    {
        (void)det_rng_float(&p->sv.erand);
        (void)det_math_random_role(surv.det, DET_SERVER);
        (void)det_rng_float(&p->sv.erand);
        (void)det_rng_float(&p->sv.erand);
    }
}

/* ItemStack.damageItem(amount, player) for a survival player:
 * attemptDamageItem's unbreaking rolls on the player's Random
 * (EnchantmentDurability.negateDamage: an armour piece first keeps the damage
 * on nextFloat() < 0.6F, then nextInt(level + 1) > 0 negates it), then on a
 * break renderBrokenItemStack, the stack shrinks by one with its damage reset
 * and the item's break stat. 1 when it broke; the caller empties a slot left
 * at count 0 where Java does (destroyCurrentEquippedItem and the like). */
int surv_damage_stack(struct server_player *p, struct surv_stack *st, int amount)
{
    if (st->count <= 0 || st->item < 0 || st->item >= 4096 || ITEMS[st->item].max_damage <= 0) return 0;

    if (amount > 0)
    {
        int level = surv_ench_level(st, 34);
        int negated = 0;
        for (int i = 0; level > 0 && i < amount; ++i)
        {
            if (ITEMS[st->item].kind == ITEM_ARMOR && det_rng_float(&p->sv.erand) < 0.6F) continue;
            if (det_rng_int_n(&p->sv.erand, level + 1) > 0) ++negated;
        }
        amount -= negated;
        if (amount <= 0) return 0;
    }

    st->damage += amount;
    if (st->damage <= ITEMS[st->item].max_damage) return 0;

    break_stack_fx(p);
    --st->count;
    surv_add_stat(p, SURV_STAT_BREAK(st->item), 1);
    if (st->count < 0) st->count = 0;
    st->damage = 0;
    return 1;
}

/* EntityPlayer.destroyCurrentEquippedItem: the hotbar slot in hand is set
 * to null (a new stack object, so an item in use lets go). */
void surv_destroy_current_item(struct server_player *p)
{
    struct surv_state *sv = &p->sv;
    if (sv->current_item < 0 || sv->current_item >= 9) return;
    sv->inv[sv->current_item] = empty_stack();
    sv->inv[sv->current_item].gen = ++sv->gen_counter;
}

/* InventoryPlayer.damageArmor: a quarter of the hit, at least 1, on every
 * ItemArmor piece through ItemStack.damageItem; a piece past its maximum
 * breaks and leaves its slot. */
static void damage_armor(struct server_player *p, float amount)
{
    struct surv_state *sv = &p->sv;

    amount /= 4.0F;
    if (amount < 1.0F) amount = 1.0F;

    for (int i = 36; i < 40; ++i)
    {
        struct surv_stack *st = &sv->inv[i];

        if (st->count <= 0 || ITEMS[st->item].kind != ITEM_ARMOR) continue;

        (void)surv_damage_stack(p, st, (int)amount);

        if (st->count == 0)
        {
            *st = empty_stack();
            st->gen = ++sv->gen_counter;
        }
    }
}

/* EntityPlayer.isBlocking: the item in use is a sword (EnumAction.block). */
static int server_blocking(const struct surv_state *sv)
{
    if (sv->using_slot < 0 || sv->using_slot >= 40) return 0;

    const struct surv_stack *st = &sv->inv[sv->using_slot];
    return st->count > 0 && st->gen == sv->using_gen && ITEMS[st->item].kind == ITEM_SWORD;
}

/* EnchantmentHelper.getMaxEnchantmentLevel(id, getLastActiveItems()): the
 * player's four armour slots. */
static int armor_max_level(const struct surv_state *sv, int id)
{
    int best = 0;
    for (int i = 36; i < 40; ++i)
    {
        int lv = surv_ench_level(&sv->inv[i], id);
        if (lv > best) best = lv;
    }
    return best;
}

/* Entity.setFire(seconds) on the server player: 20 ticks a second through
 * EnchantmentProtection.getFireTimeForEntity (Fire Protection on the
 * armour), and the fire only rises. */
static void sp_set_fire(struct server_player *p, int seconds)
{
    int ticks = seconds * 20;
    int lv = armor_max_level(&p->sv, 1);
    if (lv > 0) ticks -= mh_floor((double)((float)ticks * (float)lv * 0.15F));
    if (p->e.fire < ticks) p->e.fire = ticks;
}

/* The same, as Entity.moveEntity's standing-in-fire setFire(8) reaches it. */
static int sp_fire_time(void *self, int ticks)
{
    struct server_player *p = self;
    int lv = armor_max_level(&p->sv, 1);
    if (lv > 0) ticks -= mh_floor((double)((float)ticks * (float)lv * 0.15F));
    return ticks;
}

/* EnchantmentHelper.getEnchantmentModifierDamage over the armour
 * (EntityPlayer.getLastActiveItems): EnchantmentProtection.calcModifierDamage
 * summed over every piece's ench list (protection, fire, feather falling,
 * blast, projectile; outOfWorld, which canHarmInCreative, gets none), capped
 * at 25, then half of it plus one enchantmentRand draw. */
static int armor_modifier_damage(const struct surv_state *sv, int source)
{
    int dm = 0;
    int fire = source == SURV_IN_FIRE || source == SURV_ON_FIRE || source == SURV_LAVA ||
               source == SURV_FIREBALL;
    int projectile = source == SURV_ARROW || source == SURV_THROWN || source == SURV_FIREBALL;

    if (source != SURV_OUT_OF_WORLD)
        for (int i = 36; i < 40; ++i)
        {
            const struct surv_stack *st = &sv->inv[i];
            if (st->count <= 0) continue;
            for (int e = 0; e < itag_nench(st->tag); ++e)
            {
                int id = itag_ench_at(st->tag, e).id, lv = itag_ench_at(st->tag, e).lvl;
                float f = (float)(6 + lv * lv) / 3.0F;
                if (id == 0) dm += mh_floor((double)(f * 0.75F));
                else if (id == 1 && fire) dm += mh_floor((double)(f * 1.25F));
                else if (id == 2 && source == SURV_FALL) dm += mh_floor((double)(f * 2.5F));
                else if (id == 3 && source == SURV_EXPLOSION) dm += mh_floor((double)(f * 1.5F));
                else if (id == 4 && projectile) dm += mh_floor((double)(f * 1.5F));
            }
        }

    if (dm > 25) dm = 25;

    const char *name = "./net/minecraft/enchantment/EnchantmentHelper.java:enchantmentRand";
    det_split *enchantment = det_split_find(surv.det, name);
    if (enchantment == NULL) enchantment = det_split_random(surv.det, name);
    return ((dm + 1) >> 1) + det_split_int_n(surv.det, enchantment, (dm >> 1) + 1);
}

/* EntityPlayer.damageEntity, down through applyArmorCalculations and
 * applyPotionDamageCalculations: the sword block, the armour (and its wear),
 * then, for every source but the absolute starve, resistance and the armour's
 * protection enchantments; the landed damage makes the CombatTracker entry. */
static void damage_entity(struct server_player *p, int source, float amount, int attacker)
{
    struct surv_state *sv = &p->sv;

    if (!src_unblockable(source) && server_blocking(sv) && amount > 0.0F)
        amount = (1.0F + amount) * 0.5F;

    if (!src_unblockable(source))
    {
        int armor = total_armor_value(sv);
        float var4 = amount * (float)(25 - armor);
        damage_armor(p, amount);
        amount = var4 / 25.0F;
    }

    if (source != SURV_STARVE)
    {
        /* resistance: every source except outOfWorld is reduced by
         * (25 - (amp+1)*5) / 25. The player's effects live on the replay's
         * player twin. */
        if (source != SURV_OUT_OF_WORLD && p->replay != NULL && p->replay->player_livh != 0)
        {
            const struct potion_effect *res = living_get_potion_effect(lv_get(p->replay->player_livh), POT_RESISTANCE);
            if (res != NULL)
                amount = amount * (float)(25 - (res->amplifier + 1) * 5) / 25.0F;
        }

        if (amount <= 0.0F) amount = 0.0F;
        else
        {
            int var3 = armor_modifier_damage(sv, source);
            if (var3 > 20) var3 = 20;
            if (var3 > 0 && var3 <= 20) amount = amount * (float)(25 - var3) / 25.0F;
        }
    }

    float var3 = amount;
    float left = amount - sv->absorption;

    if (left < 0.0F) left = 0.0F;
    sv->absorption -= (var3 - left);

    if (left != 0.0F)
    {
        add_exhaustion(sv, SRC_HUNGER[source]);
        sv->health -= left;
        if (sv->health < 0.0F) sv->health = 0.0F; /* EntityLivingBase.setHealth clamps */
        combat_entry(p, attacker, left);
    }
}

/* EntityLivingBase.onDeath's state byte plus the attackedAtYaw draw: one
 * SERVER Math draw per landed hit with no attacker. */
static void surv_hurt_state(struct server_player *p)
{
    det_math_random_role(surv.det, DET_SERVER);
}

/* EntityPlayerMP.onDeath, on every hit that takes the health to 0 (a dev
 * health set on a dying player dies again: a second message) */
void surv_server_death(struct server_player *p, int source)
{
    /* the death message, CombatTracker.func_151521_b, to every player */
    chatmsg_death(p);

    if (!surv.keep_inventory) drop_all_items(p);

    /* func_94060_bK's killer: its egg's stat.entityKilledBy.<name> */
    int killer = combat_killer(p);
    int egg = surv_egg_of_kind(killer);
    if (egg >= 0) surv_add_stat(p, STAT_ENTITY_KILLED_BY + egg, 1);
    /* var6.addToPlayerScore(this, scoreValue): a player killer (the player
     * itself: its own TNT, its own arrow) counts a player kill */
    if (killer == SK_PLAYER) surv_add_stat(p, STAT_PLAYER_KILLS, 1);

    /* EntityPlayerMP.onDeath's deaths counter, then the tracker's reset
     * (the fighter is dead) */
    surv_add_stat(p, STAT_DEATHS, 1);
    combat_reset(p);
}

/* EntityPlayerMP.attackEntityFrom down. 1 when the hit landed. */
int surv_server_damage(struct server_player *p, int source, float amount)
{
    return surv_server_damage_by(p, source, amount, NULL);
}

/* EntityPlayer.attackEntityFrom's difficulty scaling: the amount scales before
 * the 0 check and before every state write (isDifficultyScaled is
 * EntityDamageSource's entity != null && EntityLivingBase && !EntityPlayer,
 * plus setExplosionSource's own setDifficultyScaled on both branches; arrows
 * and fireballs are EntityDamageSourceIndirect over the projectile itself, so
 * they stay unscaled). */
static float surv_scale_difficulty(float amount)
{
    if (surv.difficulty == 0) return 0.0F;
    if (surv.difficulty == 1) return amount / 2.0F + 1.0F;
    if (surv.difficulty == 3) return amount * 3.0F / 2.0F;
    return amount;
}

/* The same with DamageSource.getEntity() at attacker_xz (x, z): the landed
 * hit's knockback away from it (EntityLivingBase.knockBack over the player's
 * own Random) in place of the random attackedAtYaw draw. */
int surv_server_damage_by(struct server_player *p, int source, float amount, const double *attacker_xz)
{
    struct surv_state *sv = &p->sv;

    /* DamageSource.getEntity()'s kind for this call only */
    int attacker = surv_attacker;
    uint64_t attacker_ref = nw_env->survival.attacker_ref;
    surv_attacker = -1;
    nw_env->survival.attacker_ref = 0;

    /* the CombatEntry's source for the death message: the caller's family
     * and entity name, else the survival source's and the attacker's */
    {
        int msg = nw_env->survival.attacker_msg_set;
        int dmg = msg ? nw_env->survival.attacker_dmg : -1;
        int name = msg ? nw_env->survival.attacker_name : -1;
        nw_env->survival.attacker_msg_set = 0;
        if (!msg || name < 0)
            name = attacker == SK_PLAYER ? SK_PLAYER
                 : attacker >= 0 && attacker != SURV_ATTACKER_NO_EGG ? attacker
                 : attacker >= 0 && attacker_ref != 0 && lv_get(attacker_ref) ? lv_get(attacker_ref)->kind : name;
        sv->combat.cur_dtype = chatmsg_dtype(source, dmg, name);
        sv->combat.cur_name = name;
    }

    if (sv->respawn_protect > 0 && source != SURV_OUT_OF_WORLD) return 0;
    if (sv->dead || sv->health <= 0.0F) return 0;

    /* EntityPlayer.attackEntityFrom wakes a sleeping player */
    if (sv->sleeping) sleep_server_wake(p, 1, 1, 0);

    /* isDifficultyScaled: an EntityDamageSource over a living non-player
     * (a mob's melee, its thorns) and the explosions; the indirect kinds'
     * source entity is the projectile, so they stay unscaled */
    if (source == SURV_MOB || source == SURV_EXPLOSION || source == SURV_THORNS)
        amount = surv_scale_difficulty(amount);

    /* a hit scaled or thrown at 0 is refused before any state is touched
     * (a snowball, an egg, peaceful) */
    if (amount == 0.0F) return 0;

    /* the damageTaken counter: after the difficulty scaling and the zero
     * check, before the hurt-resistance sub-damage branch, on the full
     * amount */
    surv_add_stat(p, STAT_DAMAGE_TAKEN, (int)(amount * 10.0F + 0.5F));

    /* EntityLivingBase.attackEntityFrom: fire damage (inFire, onFire, lava,
     * a fireball) is refused under Fire Resistance, after the counter above
     * and before any other state. The effects live on the player twin. */
    if ((source == SURV_IN_FIRE || source == SURV_ON_FIRE || source == SURV_LAVA || source == SURV_FIREBALL) &&
        p->replay != NULL && p->replay->player_livh != 0 &&
        living_get_potion_effect(lv_get(p->replay->player_livh), POT_FIRE_RESISTANCE) != NULL)
        return 0;

    /* EntityLivingBase.attackEntityFrom: DamageSource.anvil on a worn
     * helmet (getEquipmentInSlot(4), the armour's slot 3) damages it by
     * (int)(amount * 4 + nextFloat() * amount * 2) and keeps three
     * quarters of the hit */
    if (source == SURV_ANVIL && sv->inv[39].item > 0)
    {
        float f = det_rng_float(&sv->erand);
        (void)surv_damage_stack(p, &sv->inv[39], (int)(amount * 4.0F + f * amount * 2.0F));
        amount *= 0.75F;
    }

    if ((float)sv->hurt_resistant_time > (float)sv->max_hurt_resistant / 2.0F)
    {
        if (amount <= sv->last_damage) return 0;
        damage_entity(p, source, amount - sv->last_damage, attacker);
        sv->last_damage = amount;
    }
    else
    {
        sv->last_damage = amount;
        sv->hurt_resistant_time = sv->max_hurt_resistant;
        damage_entity(p, source, amount, attacker);
        sv->hurt_time = sv->max_hurt_time = 10;

        /* setEntityState(this, 2): the tracker's func_151248_b sends the
         * S19 to the player itself as well (a player transferred out of the
         * End has no entry in its world's tracker: nothing) */
        if (!surv_player_untracked(p))
        {
            struct s2c_pkt *st = s2c_add(s2c_out());
            st->kind = PK_S19;
            st->i1 = 2;
        }

        /* setBeenAttacked: not for the sub-damage path (var3 false) and not
         * for drowning. The velocityChanged roll is a draw on the player's
         * own Random (knockbackResistance is 0, so it always lands true). */
        if (source != SURV_DROWN)
        {
            sv->velocity_changed = 1;
            (void)det_rng_double(&sv->erand);
        }

        if (attacker_xz != NULL)
        {
            double dx = attacker_xz[0] - p->e.pos_x;
            double dz = attacker_xz[1] - p->e.pos_z;

            while (dx * dx + dz * dz < 1.0E-4)
            {
                double x1 = det_math_random_role(surv.det, DET_SERVER);
                double x2 = det_math_random_role(surv.det, DET_SERVER);
                dx = (x1 - x2) * 0.01;
                double z1 = det_math_random_role(surv.det, DET_SERVER);
                double z2 = det_math_random_role(surv.det, DET_SERVER);
                dz = (z1 - z2) * 0.01;
            }

            /* knockBack: the player's knockbackResistance is 0 */
            if (det_rng_double(&sv->erand) >= 0.0)
            {
                float len = (float)sqrt(dx * dx + dz * dz);
                p->e.motion_x /= 2.0;
                p->e.motion_y /= 2.0;
                p->e.motion_z /= 2.0;
                p->e.motion_x -= dx / (double)len * (double)0.4F;
                p->e.motion_y += (double)0.4F;
                p->e.motion_z -= dz / (double)len * (double)0.4F;
                if (p->e.motion_y > 0.4000000059604645) p->e.motion_y = 0.4000000059604645;
            }
        }
        else surv_hurt_state(p);

        /* the hurt or death sound: attackEntityFrom plays it on every landed
         * hit (var3) and getSoundPitch draws two floats from the player's own
         * Random (getHurtSound and getDeathSound are never null for a player
         * and EntityPlayer does not override getSoundPitch) */
        (void)det_rng_float(&sv->erand);
        (void)det_rng_float(&sv->erand);
    }

    /* attackEntityFrom's setRevengeTarget: a living attacker */
    if (attacker >= 0)
    {
        sv->combat.revenge_kind = attacker;
        sv->combat.revenge_ref = attacker_ref;
        sv->combat.revenge_name = sv->combat.cur_name;
        sv->combat.revenge_timer = sv->ticks_existed;
    }

    if (sv->health <= 0.0F) surv_server_death(p, source);

    return 1;
}

/* EntityLivingBase.fall through EntityPlayer.fall, from handleFalling. */
void surv_server_fall(struct server_player *p, float distance)
{
    /* EntityLivingBase.updateFallState calls the landing block before
     * EntityPlayer.fall. BlockFarmland consumes one World.rand float even
     * when the fall is too short to trample it. */
    struct entity *e = &p->e;
    int x = (int)floor(e->pos_x);
    int y = (int)floor(e->pos_y - 0.20000000298023224 - (double)e->y_offset);
    int z = (int)floor(e->pos_z);
    /* the fall's dust over a block that is not air */
    int under = world_get_block(e->world, x, y, z) & 4095;
    if (BLOCKS[under].exists && BLOCKS[under].material != 0 && distance > 3.0F)
        env_aux_sfx(e->world, 2006, x, y, z, ceiling_float_int(distance - 3.0F));
    if ((world_get_block(e->world, x, y, z) & 4095) == 60 && surv.world_rand &&
        jr_float(surv.world_rand) < distance - 0.5F)
        world_set_block(e->world, x, y, z, 3, 0, 3);

    /* EntityPlayer.fall: the distanceFallen stat (survival never flies),
     * Math.round of the double, before EntityLivingBase.fall */
    if (distance >= 2.0F)
        surv_add_stat(p, STAT_DISTANCE_FALLEN, (int)(long long)floor((double)distance * 100.0 + 0.5));

    int var4 = ceiling_float_int(distance - 3.0F);

    if (var4 > 0)
    {
        /* playSound and the step sound draw nothing the rows read */
        surv_server_damage(p, SURV_FALL, (float)var4);
    }
}

/* EntityXPOrb.getXPSplit. */
static int xp_split(int v)
{
    if (v >= 2477) return 2477;
    if (v >= 1237) return 1237;
    if (v >= 617) return 617;
    if (v >= 307) return 307;
    if (v >= 149) return 149;
    if (v >= 73) return 73;
    if (v >= 37) return 37;
    if (v >= 17) return 17;
    if (v >= 7) return 7;
    if (v >= 3) return 3;
    return 1;
}

/* EntityPlayer.getExperiencePoints, keepInventory off. */
static int death_xp_value(const struct surv_state *sv)
{
    if (surv.keep_inventory) return 0;

    int v = sv->xp_level * 7;
    return v > 100 ? 100 : v;
}


/* EntityLivingBase.onDeathUpdate: the XP orbs at deathTime 20, then setDead. */
static void surv_seed_container(struct container *c, struct server_player *p);
void surv_server_container_closed(struct server_player *p, struct container *c);

static void on_death_update(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    ++sv->death_time;

    if (sv->death_time != 20) return;

    int left = death_xp_value(sv);

    while (left > 0)
    {
        int part = xp_split(left);
        left -= part;
        ie_ent *en = ie_spawn_orb(surv.iew, p->e.pos_x, p->e.pos_y, p->e.pos_z, part);
        ie_added_to_world(surv.iew, en);
    }

    surv_server_set_dead(p);

    /* onDeathUpdate's death particles at deathTime 20, after setDead: 20
     * clouds, each three rand.nextGaussian and three rand.nextFloat draws
     * (the same block EntityLivingBase runs for mobs); they go nowhere on the
     * server, but the draws consume the stream */
    for (int i = 0; i < 20; ++i)
    {
        (void)det_rng_gaussian(&sv->erand);
        (void)det_rng_gaussian(&sv->erand);
        (void)det_rng_gaussian(&sv->erand);
        (void)det_rng_float(&sv->erand);
        (void)det_rng_float(&sv->erand);
        (void)det_rng_float(&sv->erand);
    }

    /* (one burst: two merged lanes each added it, and a dead player's
     * later drop, a cursor it closes on, drew from the doubled stream;
     * pfc-deadburst-g22) */
    sv->dead = 1;
}

/* ------------------------------------------------------------ drops, pickup */

/* EntityPlayer.func_146097_a: the EntityItem spawn and its motion. thrown is
 * the dropAllItems form (a random direction), else the dropOneItem form
 * (along the look, plus the small random spread). The spread draws come from
 * the player's own Random, which the snapshot does not carry; its draws land
 * on nothing the rows read. */
int surv_player_untracked(const struct server_player *p)
{
    return p->offworld || (p->replay != NULL && p->replay->player_unlisted && !p->sv.removed);
}

struct ie_ent *throw_item(struct server_player *p, const struct surv_stack *st, int thrown)
{
    ie_ent *en = ie_spawn_item(surv.iew, p->e.pos_x,
                               p->e.pos_y - 0.30000001192092896 + (double)1.62F,
                               p->e.pos_z, st->item, st->damage, st->count);

    if (!en) return NULL;
    en->stack_tag = st->tag;
    en->delay = 40;

    if (thrown)
    {
        float var6 = det_rng_float(&p->sv.erand) * 0.5F;
        float var7 = det_rng_float(&p->sv.erand) * (float)M_PI * 2.0F;
        en->e.motion_x = (double)(-mh_sin(var7) * var6);
        en->e.motion_z = (double)(mh_cos(var7) * var6);
        en->e.motion_y = 0.20000000298023224;
    }
    else
    {
        float var5 = 0.3F;
        en->e.motion_x = (double)(-mh_sin(p->rotation_yaw / 180.0F * (float)M_PI) *
                                  mh_cos(p->rotation_pitch / 180.0F * (float)M_PI) * var5);
        en->e.motion_z = (double)(mh_cos(p->rotation_yaw / 180.0F * (float)M_PI) *
                                  mh_cos(p->rotation_pitch / 180.0F * (float)M_PI) * var5);
        en->e.motion_y = (double)(-mh_sin(p->rotation_pitch / 180.0F * (float)M_PI) * var5 + 0.1F);
        var5 = 0.02F;
        float var6 = det_rng_float(&p->sv.erand) * (float)M_PI * 2.0F;
        var5 *= det_rng_float(&p->sv.erand);
        /* StrictMath.cos and sin (fdlibm): libm's cos is an ulp off for some
         * angles, which moved a thrown stack (gold-g19-s42 row 84) */
        en->e.motion_x += fd_cos((double)var6) * (double)var5;
        float spread_y = det_rng_float(&p->sv.erand) - det_rng_float(&p->sv.erand);
        en->e.motion_y += (double)(spread_y * 0.1F);
        en->e.motion_z += fd_sin((double)var6) * (double)var5;
    }

    ie_added_to_world(surv.iew, en);
    /* func_146097_a's dropStat, after joinEntityItemWithWorld: every
     * non-empty throw (the Q drops, the container spills, the death drop) */
    surv_add_stat(p, STAT_DROP, 1);
    return en;
}

/* EntityPlayer.dropOneItem, over the C07 drop statuses: the dropStat rides
 * func_146097_a (throw_item), so an empty slot counts nothing. */
static void drop_one_item(struct server_player *p, int all)
{
    struct surv_state *sv = &p->sv;
    struct surv_stack *cur = &sv->inv[sv->current_item];
    struct surv_stack st = decr_stack_size(sv, sv->current_item,
                                           all && cur->count > 0 ? cur->count : 1);

    if (st.count > 0)
    {
        /* EntityPlayer.dropOneItem: func_146097_a(stack, false, true)
         * names the thrower. Container spills and the death drop go through
         * other func_146097_a calls that leave it unnamed. */
        struct ie_ent *dropped = throw_item(p, &st, 0);
        if (dropped) dropped->thrower = "Player";
    }
}

/* EntityPlayer.dropPlayerItemWithRandomChoice(stack, false) on the client
 * player: func_146097_a builds an EntityItem in the client world (the Entity
 * constructor's id, Random and UUID, then EntityItem's four Math.random) and
 * draws the player's Random four times for its motion;
 * EntityClientPlayerMP.joinEntityItemWithWorld discards it. */
static void client_throw_item_draws(struct surv_state *sv)
{
    (void)det_next_entity_id_role(surv.det, DET_CLIENT);
    (void)det_new_random_role(surv.det, DET_CLIENT);
    {
        int64_t msb = 0, lsb = 0;
        det_uuid_role(surv.det, DET_CLIENT, &msb, &lsb);
    }
    for (int i = 0; i < 4; ++i) (void)det_math_random_role(surv.det, DET_CLIENT);
    for (int i = 0; i < 4; ++i) (void)det_rng_float(&sv->erand);
}


/* inventory.addItemStackToInventory(stack), else
 * dropPlayerItemWithRandomChoice(stack, false): what ItemBucket.func_150910_a,
 * ItemGlassBottle and EntityCow.interact do with the filled item. server
 * picks the side: the server spawns the EntityItem, the client only draws. */
static void give_or_drop(struct server_player *sp, struct surv_state *sv, struct surv_stack *st)
{
    if (st->count == 0 || add_item_stack_to_inventory(sv, st)) return;
    if (sp != NULL) (void)throw_item(sp, st, 0);
    else client_throw_item_draws(sv);
}

/* EntityPlayer.onLivingUpdate's collideWithPlayer query. The item list the
 * query walks is World.getEntitiesWithinAABBExcludingEntity's: the chunks
 * the expanded box reaches, x outer and z inner, each chunk's sections
 * (2.0-upward-and-downward bands) bottom to top, the entities in the order
 * they were added to the section. ie_get_entities_within_aabb is that
 * order; the pool's list order (the adoption order) is not. */
/* EntityPlayerMP.onItemPickup's S0D: the row's record (the playable
 * client's pickup effect), and the tracked copy's on the client */
static void surv_s0d(struct server_player *p, int id)
{
    if (p->ns0d >= S0D_MAX) list_full("S0D pickups in one tick", S0D_MAX);
    p->s0d_ids[p->ns0d++] = id;
    if (p->replay) pickobj_server_collect(p->replay, id);
}

static void collide_with_player_pool(struct server_player *p, ie_world *iew, int with_peer)
{
    struct surv_state *sv = &p->sv;
    struct aabb var4;

    if (sv->health <= 0.0F) return;

    var4 = ride_server_pickup_box(p);

    /* with the peer pool (the living world's drops) the one walk takes both,
     * each section in the order its entities joined it: a cow's leather
     * lying among thrown blocks is picked up in Java's list order */
    IE_QUERY_LIST(hits);
    int nhits = with_peer ? ie_entities_within_aabb_peer(iew, &var4, hits, IE_MAX_ENTITIES)
                          : ie_get_entities_within_aabb(iew, &var4, NULL, hits, IE_MAX_ENTITIES);

    for (int i = 0; i < nhits; ++i)
    {
        ie_ent *en = hits[i];

            if (en->is_dead) continue;
        if (!aabb_intersects(&en->e.bounding_box, &var4)) continue;

        if (en->kind == IE_ITEM)
        {
            /* EntityItem.onCollideWithPlayer; the thrower is this player, so
             * the age gate always passes */
            if (en->delay == 0 && en->stack_count > 0)
            {
                struct surv_stack st = {.item = en->stack_item, .damage = en->stack_damage, .count = en->stack_count,
                                        .tag = en->stack_tag};
                int took = add_item_stack_to_inventory(sv, &st);

                /* the entity's own ItemStack is the one the inventory
                 * shrinks, in place: a partial fill whose last pass moved
                 * nothing returns false and still leaves the rest */
                if (!took) en->stack_count = st.count;
                if (took) en->stack_count = st.count;
                /* the stack a dying mob's drop shares with its equipment slot
                 * (or a mob's pickup record) shrinks there too */
                if (en->stack_count_link.owner) *stack_link_count(en->stack_count_link) = en->stack_count;
                if (took)
                {
                    pickup_detect_and_send_changes(p);
                    /* onItemPickup's S0D, even for a partial take */
                    surv_s0d(p, en->entity_id);

                    /* the pickup achievements, one check per item regardless
                     * of which fired (EntityItem.onCollideWithPlayer) */
                    if (en->stack_item == 17 || en->stack_item == 162)
                        surv_add_stat(p, ACH_MINE_WOOD, 1);
                    if (en->stack_item == 334) surv_add_stat(p, ACH_KILL_COW, 1);
                    if (en->stack_item == 264) surv_add_stat(p, ACH_DIAMONDS, 1);
                    if (en->stack_item == 369) surv_add_stat(p, ACH_BLAZE_ROD, 1);
                    /* diamondsToYou: an Owner other than this player. The
                     * owner is a name; the only named item is the dev give's
                     * own throw, so the branch never fires on these tapes. */

                    if (en->stack_count <= 0) en->is_dead = 1;
                }
            }
        }
        else if (en->kind == IE_ORB)
        {
            /* EntityXPOrb.onCollideWithPlayer */
            if (en->delay == 0 && sv->xp_cooldown == 0)
            {
                sv->xp_cooldown = 2;
                /* EntityPlayerMP.onItemPickup: the container's changes go out
                 * now (the pickup sound's pitch floats are the orb's own),
                 * and the S0D */
                pickup_detect_and_send_changes(p);
                surv_s0d(p, en->entity_id);
                surv_server_add_xp(p, en->xp_value);
                en->is_dead = 1;
            }
        }
        else if (en->kind == IE_ARROW)
        {
            /* EntityArrow.onCollideWithPlayer: only a grounded, settled arrow
             * a player shot (canBePickedUp 1) goes into the inventory; a mob's
             * arrow, or one still flying, stays */
            if (en->in_ground && en->shake <= 0 && en->can_be_picked_up == 1)
            {
                struct surv_stack st = {.item = 262, .count = 1};

                if (add_item_stack_to_inventory(sv, &st) && st.count <= 0)
                {
                    pickup_detect_and_send_changes(p);
                    surv_s0d(p, en->entity_id);
                    (void)det_rng_float(&en->rand);   /* the pop sound's pitch */
                    (void)det_rng_float(&en->rand);
                    en->is_dead = 1;
                }
            }
        }
        /* every other projectile keeps Entity.onCollideWithPlayer's empty body */
    }
}

/* ------------------------------------------------------- server tick pieces */

/* EntityLivingBase.updatePotionEffects, server side. The server player's
 * activePotionsMap lives on the replay's player twin (the living the splash
 * and bite paths write to), so the sync works both ways: the twin's health
 * and absorption come in from surv, the hunger and saturation effects run
 * against the FoodStats on surv (Potion's performEffect branches for the
 * EntityPlayer), and regen's heal lands on the twin's health which this
 * copies back out. The twin's rand is the player's Entity.rand (sv.erand, the
 * snapshot carries it), so the particle draws advance the true stream. */
static void surv_server_potion_tick(struct server_player *p)
{
    struct serverreplay *sr = p->replay;

    if (sr == NULL || sr->player_livh == 0 || p->sv.removed) return;

    struct living *l = lv_get(sr->player_livh);
    struct surv_state *sv = &p->sv;

    l->health = sv->health;
    l->absorption = sv->absorption;
    l->rand = sv->erand;

    /* the hunger and the saturation branches of Potion.applyPotionEffect,
     * which the twin's living body cannot run: they touch the FoodStats.
     * PotionEffect.onUpdate performs before it decrements, so an effect
     * performs at its durations D..1, the last one on the tick that removes
     * it: read which are ready before the update. The FoodStats changes
     * commute with the rest of the potion pass (none of it reads food). */
    int food_ids[8], food_amps[8], nfood = 0;
    uint8_t pids[POT_COUNT];
    int npids = potion_map_order(&l->potions, pids);
    for (int i = 0; i < npids; ++i)
    {
        const struct potion_effect *n = &l->potions.eff[pids[i]];
        if ((n->id == POT_HUNGER || n->id == POT_SATURATION) && n->duration > 0
            && potion_is_ready(n->id, n->duration, n->amplifier) && nfood < 8)
        {
            food_ids[nfood] = n->id;
            food_amps[nfood++] = n->amplifier;
        }
    }

    living_update_potion_effects(l, surv.det);

    for (int i = 0; i < nfood; ++i)
    {
        if (food_ids[i] == POT_HUNGER) surv_add_exhaustion(p, 0.025F * (float)(food_amps[i] + 1));
        else food_add_stats(sv, food_amps[i] + 1, 1.0F);
    }

    sv->erand = l->rand;

    if (l->health != sv->health || l->absorption != sv->absorption)
    {
        sv->health = l->health;
        sv->absorption = l->absorption;
    }
}

/* World.canLightningStrikeAt, the rain half of Entity.isWet, over the weather
 * scalars the harness copies from the live servertick (fire.c keeps its own
 * copy for the spread, this one rides the survival module's world). The sky
 * world is NULL on the movement tapes, whose harness never carries rain. */
static int surv_can_lightning_strike_at(struct world *w, int x, int y, int z)
{
    if (!surv.raining || surv.sky_world == NULL) return 0;
    /* the player's own world: the Nether's and the End's never rain
     * (hasNoSky: updateWeather leaves their rain strength 0), and the
     * overworld's column at the same x z is not theirs */
    if (w != NULL && w->dim != 0) return 0;
    if (!world_can_block_see_the_sky(surv.sky_world, x, y, z)) return 0;
    if (world_get_precipitation_height(surv.sky_world, x, z) > y) return 0;

    int biome = biome_at(surv.sky_world, x, z);

    if (BIOMES[biome].snow) return 0;
    if (world_can_snow_at(surv.sky_world, x, y, z, 0)) return 0;

    return BIOMES[biome].lightning;
}

/* Entity.isWet: inWater or canLightningStrikeAt at either of the body's
 * top and bottom cells (Entity.height 1.8 on the player). */
static int surv_is_wet(struct world *w, const struct entity *e)
{
    if (e->in_water) return 1;

    int x = (int)floor(e->pos_x);
    int y = (int)floor(e->pos_y);
    int z = (int)floor(e->pos_z);

    return surv_can_lightning_strike_at(w, x, y, z) ||
           surv_can_lightning_strike_at(w, x, y + (int)e->height, z);
}

/* Entity.onEntityUpdate and EntityLivingBase.onEntityUpdate's survival half. */
void surv_server_base_tick(struct server_player *p)
{
    struct entity *e = &p->e;
    struct world *w = e->world;
    struct surv_state *sv = &p->sv;

    /* Entity.onEntityUpdate: handleWaterMovement runs unconditionally here
     * (only updateFallState's own call is gated on !isInWater) */
    /* the sprint's blockcrack particles: two draws on the entity's own Random
     * when the block under a sprinting, dry entity is not air */
    if (p->sprinting && !e->in_water)
    {
        int bx = (int)floor(e->pos_x);
        int by = (int)floor(e->pos_y - 0.20000000298023224 - (double)e->y_offset);
        int bz = (int)floor(e->pos_z);
        int id = world_get_block(w, bx, by, bz) & 4095;

        if (BLOCKS[id].exists && BLOCKS[id].material != 0)
        {
            (void)det_rng_float(&p->sv.erand);
            (void)det_rng_float(&p->sv.erand);
        }
    }

    sp_handle_water_movement(e, &p->sv.erand);

    if (e->in_water) e->fire = 0;

    if (e->fire > 0)
    {
        if (e->fire % 20 == 0)
        {
            surv_server_damage(p, SURV_ON_FIRE, 1.0F);
        }

        --e->fire;
    }

    if (bb_has_material(w, aabb_expand(e->bounding_box, -0.10000000149011612, -0.4000000059604645,
                                       -0.10000000149011612), MAT_LAVA))
    {
        /* setOnFireFromLava */
        surv_server_damage(p, SURV_LAVA, 4.0F);
        sp_set_fire(p, 15);
        e->fall_distance *= 0.5F;
    }

    /* Entity.onEntityUpdate's void check: EntityLivingBase.kill is
     * attackEntityFrom(outOfWorld, 4), a hit every hurt-resistance window */
    if (e->pos_y < -64.0) surv_server_damage(p, SURV_OUT_OF_WORLD, 4.0F);

    /* setFlag(0, fire > 0): the burning bit the tracker and the client read */
    sv->fire = e->fire > 0;

    /* EntityLivingBase.onEntityUpdate: each block asks isEntityAlive
     * again (a suffocation hit that kills refills the air below) */
    if (!sv->dead && sv->health > 0.0F && !sv->sleeping && inside_opaque_block(w, e, 1.62F))
    {
        surv_server_damage(p, SURV_IN_WALL, 1.0F);
    }

    if (!sv->dead && sv->health > 0.0F)
    {

        /* canBreatheUnderwater is false and disableDamage is off; a
         * water-breathing effect (it lives on the twin) keeps the air, else
         * decreaseAirSupply: a respiration level over the armour keeps it on
         * the player's nextInt(level + 1) > 0 */
        const struct living *twin = p->replay != NULL && p->replay->player_livh != 0 ? lv_get(p->replay->player_livh) : NULL;
        if (inside_of_material(w, e, 1.62F, MAT_WATER) && !(twin != NULL && living_is_potion_active(twin, POT_WATER_BREATHING)))
        {
            /* decreaseAirSupply: Respiration on the armour keeps the air on
             * a nextInt(level + 1) above 0, the player's own Random */
            int resp = armor_max_level(sv, 5);
            if (!(resp > 0 && det_rng_int_n(&sv->erand, resp + 1) > 0)) --sv->air;

            if (sv->air == -20)
            {
                if (!surv.drown_fast) sv->air = 0;
                /* the eight bubbles: three nextFloat() - nextFloat() offsets
                 * each on the player's own Random */
                for (int i = 0; i < 8 * 6; ++i) (void)det_rng_float(&sv->erand);
                trace("drown", "iid", sv->air, surv_server_damage(p, SURV_DROWN, 2.0F), e->pos_y);
            }
        }
        else
        {
            if (!inside_of_material(w, e, 1.62F, MAT_WATER)) sv->air = 300;
        }

        /* the head in water: a rider of a living (the pig) lets go
         * (mountEntity(null)) */
        if (p->ridingh != 0 && inside_of_material(w, e, 1.62F, MAT_WATER)) ride_server_dismount(p);

        /* EntityLivingBase.onEntityUpdate's isWet extinguish: inWater or the
         * rain strike test at either body cell */
        if (surv_is_wet(w, e)) e->fire = 0;
    }
    else
    {
        /* a dead body isEntityAlive() == false: the air branch's else refills
         * it (EntityLivingBase.onEntityUpdate) */
        sv->air = 300;
    }

    if (sv->attack_time > 0) --sv->attack_time;

    if (sv->hurt_time > 0) --sv->hurt_time;

    if (sv->health <= 0.0F) on_death_update(p);

    if (sv->recently_hit > 0) --sv->recently_hit;

    /* EntityLivingBase.onEntityUpdate: the revenge target is dropped once it
     * is not alive (an exploded creeper, a killed mob), else after 100
     * ticks, after Entity.onEntityUpdate's fire and lava (a death there still
     * names it); a target known by kind only (no living in the pool) keeps
     * the timeout */
    if (sv->combat.revenge_kind >= 0 &&
        ((sv->combat.revenge_ref != 0 && !living_is_alive(lv_get(sv->combat.revenge_ref))) ||
         sv->ticks_existed - sv->combat.revenge_timer > 100))
    {
        sv->combat.revenge_kind = -1;
        sv->combat.revenge_ref = 0;
    }

    surv_server_potion_tick(p);

    e->first_update = 0;
}

/* ItemFood.onFoodEaten plus ItemAppleGold.onFoodEaten, server side: the
 * potion effects a finished food applies to the player, through the twin's
 * addPotionEffect (the S1D rides the on_potion_event callback). Returns 1
 * when an addPotionEffect ran. The probability draw is world.rand.nextFloat()
 * taken whenever the item's potionId > 0, after the burp draw. */
static int sr_pot_food_effect(struct server_player *p, int item, int meta)
{
    struct serverreplay *sr = p->replay;

    if (sr == NULL || sr->player_livh == 0) return 0;

    return potion_apply_food(lv_get(sr->player_livh), item, meta, surv.det, surv.world_rand) > 0;
}

/* EntityPlayer.updateItemUse: the particles and sound still advance RNG. */
static void server_eat_fx(struct server_player *p, int count)
{
    for (int i = 0; i < count; ++i)
    {
        (void)det_rng_float(&p->sv.erand);
        (void)det_math_random_role(surv.det, DET_SERVER);
        (void)det_rng_float(&p->sv.erand);
        (void)det_rng_float(&p->sv.erand);
    }
    (void)det_rng_int_n(&p->sv.erand, 2);
    (void)det_rng_float(&p->sv.erand);
    (void)det_rng_float(&p->sv.erand);
}

/* The finished drink's onEaten, server side: ItemBucketMilk (the stack
 * shrinks, clearActivePotions on the twin, whose onFinishedPotionEffect
 * sends each S1E; an empty stack becomes a bucket) and ItemPotion (the
 * stack shrinks, addPotionEffect per PotionHelper effect in list order; an
 * empty stack becomes a glass bottle, otherwise a bottle goes into the
 * inventory). onItemUseFinish then stores a replaced stack. */
static void server_drink_finish(struct server_player *p, struct surv_stack *cur)
{
    struct surv_state *sv = &p->sv;
    struct serverreplay *sr = p->replay;
    struct living *l = sr ? lv_get(sr->player_livh) : NULL;
    int item = cur->item;

    --cur->count;

    if (l != NULL)
    {
        l->health = sv->health;
        l->absorption = sv->absorption;
        if (item == IT_MILK)
        {
            living_clear_active_potions(l, surv.det);
        }
        else
        {
            struct potion_effect effs[16];
            int n = potion_get_effects_for_damage(cur->damage, effs, 16);
            for (int i = 0; i < n; ++i) living_add_potion_effect(l, &effs[i], surv.det);
        }
        sr_flush_potions(p);
        /* the twin's attribute and absorption changes land on the player */
        sv->health = l->health;
        sv->absorption = l->absorption;
    }

    struct surv_stack empty = {.item = item == IT_MILK ? IT_BUCKET : IT_GLASS_BOTTLE, .count = 1};
    if (cur->count <= 0)
    {
        *cur = empty;
        cur->gen = ++sv->gen_counter;
    }
    else if (item == IT_POTION)
    {
        (void)add_item_stack_to_inventory(sv, &empty);
    }
}

/* EntityPlayer.onUpdate's itemInUse block plus xpCooldown, server side. */
void surv_server_eat_tick(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    if (sv->xp_cooldown > 0) --sv->xp_cooldown;

    if (sv->using_slot < 0) return;

    struct surv_stack *cur = &sv->inv[sv->current_item];

    if (cur->count > 0 && sv->current_item == sv->using_slot && cur->gen == sv->using_gen)
    {
        /* updateItemUse: the drink's random.drink pitch is one World.rand
         * float (EnumAction.drink: milk and potions) */
        int drink = cur->item == IT_MILK || cur->item == IT_POTION;

        if (sv->using_count <= 25 && sv->using_count % 4 == 0 && ITEMS[cur->item].kind == ITEM_FOOD)
            server_eat_fx(p, 5);
        if (sv->using_count <= 25 && sv->using_count % 4 == 0 && drink && surv.world_rand)
            (void)jr_float(surv.world_rand);

        if (--sv->using_count == 0 && drink)
        {
            struct s2c_pkt *pkt = s2c_add(s2c_out());
            pkt->kind = PK_S19;
            if (surv.world_rand) (void)jr_float(surv.world_rand);
            server_drink_finish(p, cur);
            sv->using_slot = -1;
            sv->using_gen = 0;
        }
        else if (sv->using_count == 0 && surv_item_use_duration(cur) == 72000)
        {
            /* a sword's block or a drawn bow held its whole 72000 ticks:
             * EntityPlayerMP.onItemUseFinish's S19, then updateItemUse(16)
             * (nothing for EnumAction.block and bow), Item.onEaten and
             * ItemBow.onEaten hand the stack back unchanged, and
             * clearItemInUse */
            struct s2c_pkt *pkt = s2c_add(s2c_out());
            pkt->kind = PK_S19;
            sv->using_slot = -1;
            sv->using_gen = 0;
        }
        else if (sv->using_count == 0)
        {
            /* EntityPlayerMP.onItemUseFinish: the S19 status first */
            struct s2c_pkt *pkt = s2c_add(s2c_out());
            pkt->kind = PK_S19;

            server_eat_fx(p, 16);

            int before = cur->count;
            cur->count -= 1;

            food_add_stats(&p->sv, ITEMS[cur->item].heal_amount, ITEMS[cur->item].saturation);
            if (surv.world_rand) (void)jr_float(surv.world_rand);

            /* ItemFood.onFoodEaten: the food's potionId > 0 draws once for the
             * probability, then addPotionEffect on a hit. ItemAppleGold runs
             * this path for the meta-0 apple (its registration chain set the
             * ItemFood potion fields on the same instance: regen 100 amp1,
             * p=1.0) and handles the meta-1 apple's own list before the super
             * call, so the meta-1 apple draws nothing. */
            if (sr_pot_food_effect(p, cur->item, cur->damage))
            {
                sr_flush_potions(p);

                /* addPotionEffect's absorption branch ran on the twin, whose
                 * health/absorption the next potion tick overwrites from sv:
                 * carry the applied values out now or the +4 is lost. */
                struct serverreplay *sr = p->replay;
                if (sr && sr->player_livh)
                {
                    sv->health = lv_get(sr->player_livh)->health;
                    sv->absorption = lv_get(sr->player_livh)->absorption;
                }
            }

            if (cur->item == IT_STEW)
            {
                /* ItemSoup.onEaten: new ItemStack(Items.bowl) */
                *cur = empty_stack();
                cur->item = IT_BOWL;
                cur->count = 1;
                cur->gen = ++sv->gen_counter;
            }
            else if (cur->count != before && cur->count == 0)
            {
                *cur = empty_stack();
                cur->gen = ++sv->gen_counter;
            }

            sv->using_slot = -1;
            sv->using_gen = 0;
        }
    }
    else
    {
        sv->using_slot = -1;
        sv->using_gen = 0;
        sv->using_count = 0;
    }
}

/* FoodStats.onUpdate. */
void surv_server_food_tick(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    if (sv->food.exhaustion > 4.0F)
    {
        sv->food.exhaustion -= 4.0F;

        if (sv->food.saturation > 0.0F)
        {
            sv->food.saturation -= 1.0F;
            if (sv->food.saturation < 0.0F) sv->food.saturation = 0.0F;
        }
        else if (surv.difficulty != 0)
        {
            sv->food.level = sv->food.level > 0 ? sv->food.level - 1 : 0;
        }
    }

    if (sv->food.level >= 18 && sv->health > 0.0F && sv->health < 20.0F)
    {
        ++sv->food.timer;

        if (sv->food.timer >= 80)
        {
            p->sv.health = p->sv.health + 1.0F > 20.0F ? 20.0F : p->sv.health + 1.0F;
            add_exhaustion(sv, 3.0F);
            sv->food.timer = 0;
        }
    }
    else if (sv->food.level <= 0)
    {
        ++sv->food.timer;

        if (sv->food.timer >= 80)
        {
            if (sv->health > 10.0F || surv.difficulty == 3 ||
                (sv->health > 1.0F && surv.difficulty == 2))
            {
                surv_server_damage(p, SURV_STARVE, 1.0F);
            }

            sv->food.timer = 0;
        }
    }
    else
    {
        sv->food.timer = 0;
    }

    trace("food", "ffiii", sv->food.exhaustion, sv->food.saturation, sv->food.level, sv->food.timer, 0);
}

/* The EntityPlayer.onUpdate tail and the S06/S1F gates, in processPlayer's
 * order. */
static int gui_can_interact(struct server_player *p);

void surv_server_food_and_gate(struct server_player *p)
{
    /* EntityPlayer.onUpdate after super.onUpdate: the open window's
     * canInteractWith once more (a villager whose trade task an explosion
     * reset later in this tick's world pass lets go here) */
    if (p->open_container == &p->wb_container && !gui_can_interact(p))
        surv_server_close_screen(p);

    surv_server_food_tick(p);

    /* EntityPlayer.onUpdate's minutesPlayed counter, right after the food
     * tick, under the !isClient gate this server half already is */
    surv_add_stat(p, STAT_MINUTES_PLAYED, 1);

    struct surv_state *sv = &p->sv;
    int hungry = sv->food.saturation == 0.0F;

    if (sv->health != sv->last_health || sv->last_food != sv->food.level ||
        hungry != (sv->was_hungry != 0))
    {
        queue_s06(p);
        sv->last_health = sv->health;
        sv->last_food = sv->food.level;
        sv->was_hungry = hungry;
    }

    if (sv->xp_total != sv->last_xp_total)
    {
        queue_s1f(p, sv->xp_progress, sv->xp_total, sv->xp_level);
        sv->last_xp_total = sv->xp_total;
    }

    /* onUpdateEntity's `ticksExisted % 20 * 5 == 0` (every 20th tick) biome
     * check while exploreAllBiomes is locked */
    if (sv->ticks_existed % 20 * 5 == 0 && !ach_unlocked(&sv->stats, ACH_EXPLORE_ALL_BIOMES))
        surv_explore_biome(p);
}

/* EntityLivingBase.onUpdate's server half after the equipment check: the
 * CombatTracker's reset every 20th tick. */
void surv_server_combat_tick(struct server_player *p)
{
    /* the arrow count lives on the player twin (the arrow's hit sets it) */
    if (p->replay != NULL && p->replay->player_livh != 0) living_arrow_decay(lv_get(p->replay->player_livh));
    if (p->sv.ticks_existed % 20 == 0) combat_reset(p);
}

/* EntityPlayer.onLivingUpdate's head: at PEACEFUL, below max health, with
 * naturalRegeneration (always true here), heal(1.0F) every 20 ticks
 * (ticksExisted % 20 * 12 == 0); heal only acts on a live body. */
void surv_server_peaceful_heal(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    if (surv.difficulty == 0 && sv->health < 20.0F && sv->ticks_existed % 20 * 12 == 0 && sv->health > 0.0F)
        sv->health = sv->health + 1.0F > 20.0F ? 20.0F : sv->health + 1.0F;
}

/* EntityPlayer.verifyRespawnCoordinates through BlockBed.func_149977_a. */
int surv_verify_respawn(struct world *w, int bx, int by, int bz, int *rx, int *rz)
{
    if ((world_get_block(w, bx, by, bz) & 4095) != BLOCK_BED) return 0;

    return surv_bed_safe_spot(w, bx, by, bz, rx, rz);
}

int surv_bed_safe_spot(struct world *w, int bx, int by, int bz, int *rx, int *rz)
{
    static const int offs[4][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}};
    int dir = world_get_meta(w, bx, by, bz) & 3;

    for (int var7 = 0; var7 <= 1; ++var7)
    {
        int x0 = bx - offs[dir][0] * var7 - 1;
        int z0 = bz - offs[dir][1] * var7 - 1;

        for (int x = x0; x <= x0 + 2; ++x)
        {
            for (int z = z0; z <= z0 + 2; ++z)
            {
                int id = world_get_block(w, x, by, z) & 4095;
                int id_up = world_get_block(w, x, by + 1, z) & 4095;

                /* World.doesBlockHaveSolidTopSurface: the stair, slab,
                 * hopper and full snow-layer tops count */
                if (place_solid_top_surface(w, x, by - 1, z) && !MATERIALS[BLOCKS[id].material].is_opaque &&
                    !MATERIALS[BLOCKS[id_up].material].is_opaque)
                {
                    *rx = x;
                    *rz = z;
                    return 1;
                }
            }
        }
    }

    return 0;
}

void surv_clone_take(struct surv_clone *k, const struct surv_state *sv)
{
    memcpy(k->inv, sv->inv, sizeof k->inv);
    k->current_item = sv->current_item;
    k->health = sv->health;
    k->food = sv->food;
    k->xp_level = sv->xp_level;
    k->xp_total = sv->xp_total;
    k->xp_progress = sv->xp_progress;
    k->score = sv->score;
}

/* The state zeroed but its StatisticsFile, which outlives the entity. */
static void surv_state_clear_but_stats(struct surv_state *sv)
{
    size_t a = offsetof(struct surv_state, stats), b = a + sizeof sv->stats;

    memset(sv, 0, a);
    memset((char *)sv + b, 0, sizeof *sv - b);
}

/* new EntityPlayerMP's state: its constructor's Det draws, the fresh
 * survival fields and the entity's own (the bed spawn kept for the caller). */
void surv_server_fresh_state(struct server_player *p)
{
    struct surv_state *sv = &p->sv;
    /* the new object's previousEquipment is empty: its attribute map holds
     * no held-item modifier until its first onUpdate */
    p->held_attr_item = -1;
    /* keepInventory: clonePlayer(copyWhen true) carries the inventory and the
     * XP over to the fresh player (EntityPlayer.clonePlayer). The stacks are
     * re-hashed: the container's change detection matches on gen. */
    struct surv_clone *keep ENV_LOCAL = envstack_take(sizeof *keep);
    int keep_inv = surv.keep_inventory;

    if (keep_inv) surv_clone_take(keep, sv);

    int has_spawn = sv->has_spawn;
    int sx = sv->spawn_x, sy = sv->spawn_y, sz = sv->spawn_z, sf = sv->spawn_forced;
    /* clonePlayer's last line, after a death and after the End exit alike:
     * theInventoryEnderChest is the old player's */
    struct surv_stack *ender ENV_LOCAL = envstack_take(sizeof sv->ender);
    memcpy(ender, sv->ender, sizeof sv->ender);

    /* the StatisticsFile outlives the entity (func_152602_a's per-UUID map) */
    surv_state_clear_but_stats(sv);
    memcpy(sv->ender, ender, sizeof sv->ender);
    sv->food.level = 20;
    sv->food.saturation = 5.0F;
    sv->health = 20.0F;
    sv->air = 300;
    sv->max_hurt_resistant = 20;
    sv->max_hurt_time = 10;
    sv->respawn_protect = 60;
    sv->last_health = -1.0F;
    sv->last_food = -1;
    sv->last_xp_total = -1;
    sv->using_slot = -1;
    potion_map_init(&sv->potions);   /* the server's copy stays empty */

    sv->has_spawn = has_spawn;
    sv->spawn_x = sx;
    sv->spawn_y = sy;
    sv->spawn_z = sz;
    sv->spawn_forced = sf;

    if (keep_inv)
    {
        /* EntityPlayer.clonePlayer(copyWhen): the inventory and the XP/score;
         * the client's own fresh object is untouched (its mirror re-syncs from
         * the S2F window items). */
        for (int i = 0; i < 40; ++i)
        {
            sv->inv[i] = keep->inv[i];
            if (sv->inv[i].count > 0) sv->inv[i].gen = ++sv->gen_counter;
        }

        sv->xp_level = keep->xp_level;
        sv->xp_total = keep->xp_total;
        sv->xp_progress = keep->xp_progress;
        sv->score = keep->score;
    }
    sv->combat.revenge_kind = -1;
    sv->combat.revenge_ref = 0;

    /* the fresh entity constructor's Det draws */
    (void)det_next_entity_id_role(surv.det, DET_SERVER);
    sv->erand = det_new_random_role(surv.det, DET_SERVER);
    {
        int64_t msb = 0, lsb = 0;
        det_uuid_role(surv.det, DET_SERVER, &msb, &lsb);
    }
    /* the EntityLivingBase constructor's three Math draws */
    det_math_random_role(surv.det, DET_SERVER);
    det_math_random_role(surv.det, DET_SERVER);
    det_math_random_role(surv.det, DET_SERVER);

    p->e.width = 0.6F;
    p->e.height = 1.8F;
    p->e.step_height = 0.0F;
    p->e.y_offset = 0.0F;
    p->e.motion_x = 0.0;
    p->e.motion_y = 0.0;
    p->e.motion_z = 0.0;
    p->e.fall_distance = 0.0F;
    p->e.fire = 0;
    p->e.y_size = 0.0F;
    p->e.on_ground = 0;
    p->e.first_update = 1;
    /* the Entity constructor's step counters: the fresh player's first
     * swim-sound step (two draws on its Random) comes after one block */
    p->e.distance_walked_modified = 0.0F;
    p->e.distance_walked_on_step_modified = 0.0F;
    p->e.next_step_distance = 1;
    /* EntityPlayerMP.currentWindowId: the new player's first window is 1 */
    p->window_id = 0;
    p->rotation_yaw = 0.0F;
    p->rotation_pitch = 0.0F;
    p->prev_rotation_yaw = 0.0F;
    p->prev_rotation_pitch = 0.0F;
    p->move_speed = 0.10000000149011612;
    p->jump_movement_factor = 0.02F;
    p->attr_dirty = 0;
    /* the new entity's attribute map and its new tracker entry */
    p->pot_slow = p->pot_speed = -1;
    p->trk_ticks = 0;
    p->trk_w_valid = 0;
    p->sprinting = 0;
    p->sneaking = 0;
    /* the new entity's portal state (a player dead in a Nether portal kept
     * its cooldown); clonePlayer(copyWhen) carries teleportDirection alone */
    p->portal.time_until_portal = 0;
    p->portal.portal_counter = 0;
    p->portal.in_portal = 0;
    p->portal.teleport_direction = 0;
    p->limbo = 0;
    /* the fresh player's own ItemInWorldManager: no dig in progress (the
     * old one's machine would read, and load, the block it was digging) */
    nw_env->survival.dig_active = 0;
    nw_env->survival.dig_ticked = 0;
}

/* ServerConfigurationManager.respawnPlayer. */
void surv_server_fresh(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    /* removePlayerEntityDangerously's setDead in the old world */
    surv_server_set_dead(p);

    /* the old object stays in the world it died in, where its pearls still
     * find it */
    p->life_dim[p->life & 7] = p->dimension;
    ++p->life;

    /* (a death in another dimension leaves it here: the fresh player is the
     * overworld's) */
    if (p->replay != NULL) serverreplay_respawn_leave(p->replay);
    /* removePlayerEntityDangerously's removeEntityFromAllTrackingPlayers:
     * the entries that took the dead player since its removal (spawned or
     * moved four blocks) let the old object go; the new player's entry
     * makes every entry try it */
    if (p->replay != NULL) combat_player_removed(p->replay);
    struct world *w = p->e.world;
    surv_server_fresh_state(p);
    if (p->replay != NULL) serverreplay_player_fresh(p->replay);

    int has_spawn = sv->has_spawn;
    int sx = sv->spawn_x, sy = sv->spawn_y, sz = sv->spawn_z;

    double x = (double)((int)p->e.pos_x) + 0.5;
    double y = (double)(int)p->e.pos_y;
    double z = (double)((int)p->e.pos_z) + 0.5;

    if (p->replay != NULL)
    {
        /* the fresh EntityPlayerMP constructor's placement, bed or not: the
         * world spawn with the spawn-protection fuzz on the new Random, then
         * the top solid or liquid block */
        struct world *ow = &p->replay->pop.world;
        int wx = p->replay->d->spawner.spawn_x
                 + det_rng_int_n(&sv->erand, 20) - 10;
        int wz = p->replay->d->spawner.spawn_z
                 + det_rng_int_n(&sv->erand, 20) - 10;
        x = (double)wx + 0.5;
        z = (double)wz + 0.5;
        y = (double)sr_top_solid_or_liquid(ow, wx, wz);
    }

    if (has_spawn)
    {
        int rx, rz;

        if (surv_verify_respawn(w, sx, sy, sz, &rx, &rz))
        {
            /* setLocationAndAngles((double)((float)posX + 0.5F), ...): the
             * sums are Java floats */
            x = (double)((float)rx + 0.5F);
            y = (double)((float)sy + 0.1F);
            z = (double)((float)rz + 0.5F);
            sv->has_spawn = 1;   /* setSpawnChunk(bed, forced) again */
        }
        else
        {
            /* tile.bed.notValid; clonePlayer does not carry the spawn chunk,
             * so the fresh player has none */
            struct s2c_pkt *pkt = s2c_add(s2c_out());
            pkt->kind = PK_S2B;
            pkt->i0 = 0;
            sv->has_spawn = 0;
        }
    }

    p->e.pos_x = x;
    p->e.pos_y = y;
    p->e.pos_z = z;

    /* setLocationAndAngles lands the bounding box at the spawn before the
     * vanilla's own push-up loop reads it */
    entity_set_position(&p->e, x, y, z);

    if (p->replay != NULL) serverreplay_respawn_load(p->replay, p->e.pos_x, p->e.pos_z);

    while (!world_colliding_boxes_empty(w, p->e.bounding_box))
    {
        p->e.pos_y += 1.0;
        entity_set_position(&p->e, p->e.pos_x, p->e.pos_y, p->e.pos_z);
    }

    /* the respawn packets, in send order: S07, the S08 from
     * setPlayerLocation, then the S1F with the fresh XP */
    /* the fresh player joins loadedEntityList at its tail */
    if (p->replay != NULL) serverreplay_respawn_to_tail(p->replay);

    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S07;
    if (p->replay != NULL) pickobj_server_mark_s07(p->replay);

    p->has_moved = 0;
    p->last_pos_x = p->e.pos_x;
    p->last_pos_y = p->e.pos_y;
    p->last_pos_z = p->e.pos_z;

    pkt = s2c_add(s2c_out());
    pkt->kind = PK_S08;
    pkt->f0 = p->e.pos_x;
    pkt->f1 = (double)p->e.pos_y + 1.6200000047683716;
    pkt->f2 = p->e.pos_z;
    pkt->f3 = 0.0F;
    pkt->f4 = 0.0F;

    queue_s1f(p, 0.0F, 0, 0);

    /* updateTimeAndWeatherForPlayer: the overworld's time as the network
     * tick finds it */
    if (p->replay != NULL)
    {
        const struct servertick *st0 = &p->replay->w[0].st;
        p->s03_sent = 1;
        p->s03_rule = st0->daylight_cycle;
        p->s03_total = st0->total_time;
        p->s03_day = st0->world_time;
    }

    if (p->replay != NULL) serverreplay_respawn_join(p->replay, p->e.pos_x, p->e.pos_z);
    /* spawnEntityInWorld's trackEntity: the entries try the new player */
    if (p->replay != NULL) pickobj_server_respawned(p->replay);

    /* the StatisticsFile is the configuration manager's per-UUID object
     * (func_152602_a), kept by the fresh player; respawnPlayer sends no
     * func_150884_b map (only playerLoggedIn does) */
}

/* The dig machine the server's C07 0/1/2 statuses drive, one per server
 * player for the tape's length (one dig target at a time, as the
 * PlayerControllerMP state it answers). The env is dig.c's own; the case's
 * begin runs on the first status 0 of a target. */
#define surv_digenv (nw_env->survival.digenv)
#define surv_digenv_ready (nw_env->survival.digenv_ready)
#define surv_dig_active (nw_env->survival.dig_active)
/* an op's own update ran this tick pair */
#define surv_dig_ticked (nw_env->survival.dig_ticked)
/* the erand seed the current op's prand forked from */
#define surv_dig_fork (nw_env->survival.dig_fork)

/* hand the dig machine's prand back to the server erand, but only the draws
 * this op made: an op that drew nothing must not move the erand (the erand
 * may have advanced elsewhere between the fork and here, and the fork is
 * refreshed from the live erand at every op, so a stale seed can never
 * regress the stream) */
static void dig_hand_back(struct server_player *p, const dig_env *d)
{
    if (((int64_t)d->p.prand.seed & ((1LL << 48) - 1)) != surv_dig_fork)
        p->sv.erand.r.seed = d->p.prand.seed;
}

/* The client's EffectRenderer fxLayers (surv_fx, the environment's), ticked
 * and drawn by the playable client. Spawned by the dig flow below; play.c
 * ticks and draws it. The spawns' world follows the client player's (a
 * respawn's world switch clears the list the way EffectRenderer.clearEffects
 * does). */
static void surv_fx_ensure(struct client_player *p)
{
    if (!surv_fx.det) particles_live_init(&surv_fx, p->e.world, surv.det);
    if (surv_fx.world != p->e.world)
    {
        surv_fx.world = p->e.world;
        surv_fx.n = 0;
    }
    /* doSpawnParticle's 16-block gate is from mc.renderViewEntity, the
     * client player where it stands at the spawn */
    surv_fx.view_x = p->e.pos_x;
    surv_fx.view_y = p->e.pos_y;
    surv_fx.view_z = p->e.pos_z;
    /* WorldClient.rand, once known (player.h) */
    surv_fx.world_rand = p->cw_rand_known ? &p->cw_rand : NULL;
}

struct particles_live *surv_client_fx(struct client_player *p)
{
    surv_fx_ensure(p);
    return &surv_fx;
}

void surv_client_fx_spawn(struct client_player *p, const char *name, double x, double y, double z,
                          double vx, double vy, double vz)
{
    surv_fx_ensure(p);
    (void)particles_live_spawn(&surv_fx, name, x, y, z, vx, vy, vz);
}

/* ItemStack.damageItem(amount, player) on the client player's copy: the
 * Unbreaking rolls on its Random (EnchantmentDurability.negateDamage; the
 * armour branch is the server's), then on a break renderBrokenItemStack and
 * the stack shrinks by one with its damage reset (the break stat is not
 * independent, so EntityClientPlayerMP.addStat drops it). 1 when it broke;
 * the caller empties a slot left at count 0 where Java does. */
static int client_damage_stack(struct client_player *p, struct surv_stack *st, int amount)
{
    if (st->count <= 0 || st->item <= 0 || st->item >= 4096 || ITEMS[st->item].max_damage <= 0) return 0;

    int level = surv_ench_level(st, 34);
    int negated = 0;
    for (int i = 0; level > 0 && i < amount; ++i)
        if (det_rng_int_n(&p->sv.erand, level + 1) > 0) ++negated;
    amount -= negated;
    if (amount <= 0) return 0;

    st->damage += amount;
    if (st->damage <= ITEMS[st->item].max_damage) return 0;

    surv_client_render_broken(p, st);
    if (--st->count < 0) st->count = 0;
    st->damage = 0;
    return 1;
}

/* EnchantmentHelper.getAquaAffinityModifier: the highest Aqua Affinity
 * level over getLastActiveItems, the player's four armour slots. */
static int aqua_affinity(const struct surv_state *sv)
{
    for (int i = 36; i < 40; ++i)
        if (surv_ench_level(&sv->inv[i], 6) > 0) return 1;
    return 0;
}

/* The dig machine's pose refresh: whatever the C07s read (the held stack,
 * its Efficiency, Unbreaking, Silk Touch and Fortune levels, the pose). */
static void surv_dig_pose(dig_env *d, struct server_player *p)
{
    struct surv_stack *cur = &p->sv.inv[p->sv.current_item];

    d->p.held_item = cur->count > 0 ? cur->item : 0;
    d->p.held_damage = cur->damage;
    d->p.held_count = cur->count;
    d->p.eff = surv_ench_level(cur, 32);
    d->p.unbr = surv_ench_level(cur, 34);
    d->p.silk = surv_ench_level(cur, 33);
    d->p.fortune = surv_ench_level(cur, 35);
    d->p.on_ground = p->e.on_ground;
    /* EntityPlayer.getBreakSpeed tests isInsideOfMaterial(water) at
     * EntityPlayerMP's 1.62 eye, not the body's inWater, and Aqua Affinity
     * on any armour piece cancels the /5 */
    d->p.in_water = inside_of_material(p->e.world, &p->e, 1.62F, MAT_WATER) && !aqua_affinity(&p->sv);
    d->p.haste = 0;
    d->p.fatigue = 0;
    d->p.exhaustion = p->sv.food.exhaustion;
}

/* The dig op's stat fires, adopted into the stat table: stat_mine is the
 * mineBlock.<block> counter (the block as the case clicked it), stat_use /
 * stat_break the useItem/breakItem counters on the held item (ItemStack.
 * func_150999_a and damageItem). The flags reset per case in dig.c, and the
 * survival driver never runs a case end mid-tape, so adopt-then-zero. */
static void surv_dig_adopt_stats(dig_env *d, struct server_player *p)
{
    /* the stack's own item: a stack that broke in onBlockDestroyed left the
     * hand (held_was) but still counts its use and its break */
    int item = d->p.held_item != 0 ? d->p.held_item : d->p.held_was;

    if (d->p.stat_mine)
    {
        /* Block.harvestBlock's mineBlockStatArray[getIdFromBlock(this)] */
        surv_add_stat(p, SURV_STAT_MINE(d->p.stat_mine_block), 1);
        d->p.stat_mine = 0;
    }
    if (d->p.stat_use)
    {
        surv_add_stat(p, SURV_STAT_USE(item), 1);
        d->p.stat_use = 0;
    }
    if (d->p.stat_break)
    {
        surv_add_stat(p, SURV_STAT_BREAK(item), 1);
        d->p.stat_break = 0;
    }
}

/* The dig machine's drops leave dig_env.ents as records; the replay adopts
 * each onto the entity list (sr_on_spawn's shape), with the draws dig.c
 * already spent. */
static void surv_dig_adopt(dig_env *d, struct server_player *p)
{
    if (!surv.iew) return;

    for (int i = 0; i < d->nents; ++i)
    {
        dig_ent *e = &d->ents[i];

        if (e->kind == 3)
        {
            /* BlockSilverfish.onBlockDestroyedByPlayer's spawn. The dig
             * machine spent the identity draws at the break point and the
             * record carries them, so construct through a fake det that maps
             * DET_OTHER onto the real SERVER streams (the established
             * constructor pattern): living_init's three ctor math draws land
             * on the real math stream, while the identity the fake draws is
             * overwritten from the record. */
            /* the dig machine already spent the seeder draws (id, rand seed,
             * uuid) at the break point; only living_init's three ctor math
             * draws are new, so the fake maps only the math stream */
            det_state fake = *surv.det;
            fake.splits = NULL;
            /* the dig machine spent the three draws at the record already */
            fake.math[DET_OTHER] = e->fish_math;
            det_set_role(&fake, DET_OTHER);
            struct living *l = living_alloc();
            if (l == NULL) continue;
            living_init(l, p->e.world, HK_SILVERFISH, &fake);
            l->an = surv.anw;
            l->dimension = surv.anw ? surv.anw->dimension : 0;
            silverfish_construct(l, surv.det);
            living_set_location_and_angles(l, e->x, e->y, e->z, e->yaw, 0.0F);
            l->entity_id = e->entity_id;
            l->uuid_msb = e->uuid_msb;
            l->uuid_lsb = e->uuid_lsb;
            det_rng_set_seed(&l->rand, (int64_t)e->rand_state);
            if (surv.anw) an_add_living(surv.anw, l, surv.anw->n);
            /* spawnEntityInWorld's EntityTracker.trackEntity */
            if (surv.anw && surv.anw->track_spawn)
                surv.anw->track_spawn(surv.anw, l, surv.anw->constructor_ctx);
            /* BlockSilverfish.onBlockDestroyedByPlayer (fish A) /
             * dropBlockAsItemWithChance (fish B) each call
             * EntityLiving.spawnExplosionParticle on the new fish: 20 x
             * (3 gaussian + 3 float) on the fish's own Random */
            for (int particle = 0; particle < 20; ++particle)
            {
                det_rng_gaussian(&l->rand);
                det_rng_gaussian(&l->rand);
                det_rng_gaussian(&l->rand);
                det_rng_float(&l->rand);
                det_rng_float(&l->rand);
                det_rng_float(&l->rand);
            }
            continue;
        }

        if (e->kind == 2)
        {
            ie_ent *en = ie_adopt_orb(surv.iew, e->entity_id, e->uuid_msb, e->uuid_lsb, e->rand_state, e->x, e->y, e->z,
                                      e->mx, e->my, e->mz, e->yaw, e->xp);
            if (en == NULL) continue;
            ie_added_to_world(surv.iew, en);
        }
        else
        {
            if (e->item == 0xffff) continue;

            ie_ent *en = ie_adopt_item(surv.iew, e->entity_id, e->uuid_msb, e->uuid_lsb, e->rand_state, e->x, e->y, e->z,
                                       e->mx, e->my, e->mz, e->yaw, e->hover, e->item, e->damage,
                                       e->count, e->tag);
            if (en == NULL) continue;
            /* Block.dropBlockAsItem_do's 10-tick pickup delay (the 40 is
             * the player's own throw's); a container spill's EntityItem
             * keeps the constructor's 0 */
            en->delay = e->spill ? 0 : 10;
            ie_added_to_world(surv.iew, en);
        }
    }

    d->nents = 0;
    (void)p;
}

/* A chunk the dig machine's world reads provides (a dig that crossed a
 * dimension reads its old target in the new world): its structure offers
 * reseed World.rand (MapGenStructure's canSpawnStructureAtCoords through
 * World.setRandomSeed), which is the machine's copy while it runs, so the
 * reseed is handed back with the copy instead of being overwritten by it. */
static jrand *surv_dig_bind_populate(dig_env *d)
{
    jrand *prev = populate_world_rand;
    if (surv.world_rand != NULL && prev == surv.world_rand) populate_world_rand = &d->wr;
    return prev;
}

/* NetHandlerPlayServer.processPlayerDigging: statuses 0 (begin), 1 (cancel),
 * 2 (finish) through dig.c's drive ops; 3, 4 (the drops) and 5
 * (stopUsingItem) as before. */
static void process_c07(struct server_player *p, int status, int x, int y, int z, int side)
{
    if (status == 4) { drop_one_item(p, 0); return; }
    if (status == 3) { drop_one_item(p, 1); return; }

    if (status == 5)
    {
        /* stopUsingItem: the held item's onPlayerStoppedUsing, then the
         * clear */
        throw_server_release(p);
        p->sv.using_slot = -1;
        p->sv.using_gen = 0;
        p->sv.using_count = 0;
        return;
    }

    /* the reach check (the handler returns before any dig op) and the build
     * limit */
    {
        double dx = p->e.pos_x - ((double)x + 0.5);
        double dy = p->e.pos_y - ((double)y + 0.5) + 1.5;
        double dz = p->e.pos_z - ((double)z + 0.5);
        if (dx * dx + dy * dy + dz * dz > 36.0 || y >= 256) return;
    }

    if (!surv_digenv_ready)
    {
        dig_init(&surv_digenv, p->e.world, surv.det, 0);
        surv_digenv.role = DET_SERVER;
        surv_digenv.block_rands = 1;
        surv_digenv.net_ops = 1;
        surv_digenv_ready = 1;
    }

    dig_env *d = &surv_digenv;
    surv_dig_pose(d, p);
    if (surv.world_rand) d->wr.seed = surv.world_rand->seed;

    /* the dig machine reads the world Random as the server's own world
     * tick's Random left it */
    /* the machine's prand IS the server player's rand in Java (the tool
     * damage and break draws run on it): fork it from the live erand at
     * every op; the hand-backs below return only what this op drew, so an
     * op that draws nothing leaves the erand where the tick left it */
    surv_dig_fork = p->sv.erand.r.seed;
    d->p.prand.seed = surv_dig_fork;
    /* a break's pops (the other door half) draw on the machine's World.rand
     * copy and land in its records, in order with the break's own drops */
    struct blockcb_env saved_env = nw_env->blockcb.env;
    dig_bind_blockcb(d);
    jrand *pop_rand = surv_dig_bind_populate(d);
    if (status == 0)
    {
        int new_target = !surv_dig_active || d->x != x || d->y != y || d->z != z;

        if (new_target)
        {
            struct surv_stack *cur = &p->sv.inv[p->sv.current_item];
            int64_t opseed = surv.world_rand
                ? (int64_t)(surv.world_rand->seed ^ 0x5DEECE66DULL) : 0;
            /* the ItemInWorldManager lives as long as the player: a new
             * target keeps its counters and an armed finish (a delayed
             * break still completes, and blocks a second arming) */
            dig_mgr keep = d->m;
            dig_case_begin(d, 0, x, y, z, cur->count > 0 ? cur->item : 0,
                           cur->damage, cur->count, d->p.eff, d->p.unbr, d->p.silk, d->p.fortune, p->e.on_ground,
                           d->p.in_water, 0, 0, opseed,
                           (int64_t)p->sv.erand.r.seed ^ 0x5DEECE66DULL);
            if (surv_dig_active) d->m = keep;
            d->p.exhaustion = p->sv.food.exhaustion;
            surv_dig_active = 1;
            dig_op_click(d, x, y, z, side);
        }
        else
        {
            /* ItemInWorldManager.onBlockClicked runs for every start
             * status, the same block again too: the fire goes out, the
             * progress restarts from this tick's counter */
            dig_op_click(d, x, y, z, side);
        }

        /* the dig machine drew from its own copy of the player's Random
         * (the Unbreaking negations and the renderBrokenItemStack break
         * particles inside damage_held): hand the advanced seed back so the
         * server's erand stays the one stream Java drew from */
        dig_hand_back(p, d);
        if (surv.world_rand) surv.world_rand->seed = d->wr.seed;
        surv_dig_adopt(d, p);
        surv_dig_adopt_stats(d, p);
        d->nents = 0;
    }
    else if (status == 1)
    {
        dig_op(d, DIG_OP_CANCEL, d->x, d->y, d->z);
        dig_hand_back(p, d);
        if (surv.world_rand) surv.world_rand->seed = d->wr.seed;
        surv_dig_adopt(d, p);
        surv_dig_adopt_stats(d, p);
        d->nents = 0;
    }
    else if (status == 2)
    {
        dig_op(d, DIG_OP_FINISH, x, y, z);
        dig_hand_back(p, d);
        if (surv.world_rand) surv.world_rand->seed = d->wr.seed;
        surv_dig_adopt(d, p);
        surv_dig_adopt_stats(d, p);
        d->nents = 0;
    }
    populate_world_rand = pop_rand;
    nw_env->blockcb.env = saved_env;

    /* statuses 1 and 2: a block the server still has goes back to the
     * client (the S23 its prediction broke; a finish that leaves a block
     * there, ice's water) */
    if ((status == 1 || status == 2) && p->replay != NULL && y >= 0 && y < 256)
    {
        int left = world_get_block(p->e.world, x, y, z) & 4095;
        if (BLOCKS[left].exists && BLOCKS[left].material != 0) serverreplay_resend_block(p->replay, x, y, z);
    }

    surv_dig_ticked = 1;

    /* the dig machine's held-stack damage and exhaustion are the server
     * player's own */
    struct surv_stack *cur = &p->sv.inv[p->sv.current_item];

    if (cur->count > 0 && d->p.held_item == cur->item)
    {
        cur->damage = d->p.held_damage;
        cur->count = d->p.held_count;

        if (cur->count <= 0) *cur = empty_stack();
    }
    else if (cur->count > 0 && d->p.held_was == cur->item)
    {
        /* the held stack broke inside the dig machine this tick: vanilla
         * destroyed it in place (EntityPlayer.destroyCurrentEquippedItem) */
        *cur = empty_stack();
        d->p.held_was = 0;
    }
    else if (d->p.held_item == 0 && cur->count == 0)
    {
        /* the hand: nothing */
    }

    if (d->p.exhaustion > 0.0f) { p->sv.food.exhaustion = d->p.exhaustion; }

    /* a finish or an abandon that leaves a block there answers with its S23:
     * a break the client predicted and the server has not done yet (the
     * delayed harvest) comes back to the client world */
    if ((status == 1 || status == 2) && p->replay != NULL && (world_get_block(p->e.world, x, y, z) & 4095) != 0)
        serverreplay_resend_block(p->replay, x, y, z);
}

/* ItemInWorldManager.tryUseItem through ItemFood.onItemRightClick, and the
 * placement/activation form's server half (processPlayerBlockPlacement):
 * the food C08 (the 255 in-air form) sets the item use; the block C08 runs
 * activateBlockOrUseItem over the mouseover cell the packet carries. */
static int ids_workbench(void)
{
    return harvest_block_id("minecraft:crafting_table");
}

/* The client container's player inventory seeded from the client's mirror
 * (the S2F copies). */
static void surv_seed_client_container(struct container *c, struct client_player *p)
{
    for (int i = 0; i < 40; ++i)
    {
        struct craft_stack cs = craft_from_surv(&p->sv.inv[i]);
        container_set_player(c, i, &cs);
    }
}

/* The server container's player inventory seeded from the player's own
 * stacks (the click machine's copy of the truth). */
static void surv_seed_container(struct container *c, struct server_player *p)
{
    for (int i = 0; i < 40; ++i)
    {
        struct craft_stack cs = craft_from_surv(&p->sv.inv[i]);
        container_set_player(c, i, &cs);
    }
}

/* An open window's S2F: the stack and its tag (the packet writes the whole
 * ItemStack, Packet.writeItemStackToBuffer's NBT included). */
static void gui_queue_slot(struct server_player *p, int slot, const struct craft_stack *s)
{
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S2F;
    pkt->window = p->window_id;
    pkt->slot = slot;
    pkt->i0 = s && s->count > 0 ? s->item : -1;
    pkt->i1 = s && s->count > 0 ? s->damage : 0;
    pkt->i2 = s && s->count > 0 ? s->count : 0;
    pkt->st.tag = s && s->count > 0 ? s->tag : 0;
}

/* SlotFurnace.onCrafting's server half: the output's smelting experience
 * times field_75228_b, the fraction rounded up by Math.random, then the orbs
 * at the player's feet plus half a block up and south. */
static void gui_furnace_xp(void *ctx, const struct craft_stack *s, int n)
{
    struct server_player *p = ctx;
    float factor = smelt_experience(s->item, s->damage);

    if (factor == 0.0F) n = 0;
    else if (factor < 1.0F)
    {
        float v = (float)n * factor;
        int whole = (int)v;
        if (v < (float)whole) --whole;                    /* MathHelper.floor_float */
        int up = (int)v;
        if (v > (float)up) ++up;                          /* ceiling_float_int */
        if (whole < up && (float)det_math_random_role(surv.det, DET_SERVER) < v - (float)whole)
            ++whole;
        n = whole;
    }

    int role = surv.iew->role;
    surv.iew->role = DET_SERVER;
    while (n > 0)
    {
        int part = xp_split(n);
        n -= part;
        ie_ent *en = ie_spawn_orb(surv.iew, p->e.pos_x, p->e.pos_y + 0.5, p->e.pos_z + 0.5, part);
        ie_added_to_world(surv.iew, en);
    }
    surv.iew->role = role;
}

/* SlotCrafting.onCrafting's / SlotFurnace.onCrafting's stat half: ItemStack.
 * onCrafting's craftItem counter first (with the counter BEFORE the reset,
 * Java's order), then the achievement chain per output:
 *
 *   SlotCrafting: buildWorkBench (crafting_table 58), buildPickaxe (an
 *   ItemPickaxe), buildFurnace (furnace 61), buildHoe (an ItemHoe),
 *   makeBread (bread 297), bakeCake (cake 354), buildBetterPickaxe (an
 *   ItemPickaxe whose material is not wood), buildSword (an ItemSword),
 *   enchantments (enchanting_table 116), bookcase (bookshelf 47)
 *   SlotFurnace: acquireIron (iron_ingot 265), cookFish (cooked_fished 350)
 */
static void gui_crafted(void *ctx, const struct craft_stack *s, int n)
{
    struct server_player *p = ctx;
    int item = s->item;

    surv_add_stat(p, SURV_STAT_CRAFT(item), n);

    switch (item)
    {
    case 58:  surv_add_stat(p, ACH_BUILD_WORK_BENCH, 1); break;
    case 61:  surv_add_stat(p, ACH_BUILD_FURNACE, 1); break;
    case 297: surv_add_stat(p, ACH_MAKE_BREAD, 1); break;
    case 354: surv_add_stat(p, ACH_BAKE_CAKE, 1); break;
    case 116: surv_add_stat(p, ACH_ENCHANTMENTS, 1); break;
    case 47:  surv_add_stat(p, ACH_BOOKCASE, 1); break;
    case 265: surv_add_stat(p, ACH_ACQUIRE_IRON, 1); break;
    case 350: surv_add_stat(p, ACH_COOK_FISH, 1); break;
    default:
    {
        if (item < 0 || item >= (int)(sizeof ITEMS / sizeof ITEMS[0])) break;
        const struct item_def *def = &ITEMS[item];

        if (!strcmp(def->class_name, "ItemPickaxe"))
        {
            surv_add_stat(p, ACH_BUILD_PICKAXE, 1);
            if (def->tool_material != 0)
                surv_add_stat(p, ACH_BUILD_BETTER_PICKAXE, 1);
        }
        if (!strcmp(def->class_name, "ItemHoe"))
            surv_add_stat(p, ACH_BUILD_HOE, 1);
        if (!strcmp(def->class_name, "ItemSword"))
            surv_add_stat(p, ACH_BUILD_SWORD, 1);
        break;
    }
    }
}

void surv_server_hook_own_container(struct server_player *p)
{
    /* the player container's SlotCrafting: the same stat half */
    p->own_container.crafted = gui_crafted;
    p->own_container.crafted_ctx = p;
}

/* The world the open window's container reads: the one it was opened in
 * (a portal leaves the window's tile behind; NULL, the player's own). */
static struct world *gui_world(const struct server_player *p)
{
    return p->gui_world != NULL ? p->gui_world : p->e.world;
}

/* The container's tile slots are the tile entity's own inventory in Java:
 * whatever the tile pass changed since the window opened is what a click
 * sees. The tile slot behind window slot i: the chest's (the second half's
 * past 27 in a double chest), the furnace's, the dispenser's (a dropper's
 * too: TileEntityDropper extends TileEntityDispenser) or the hopper's. NULL
 * for the ender window, another kind, or a missing tile; *owner_out gets the
 * tile entity. */
static struct te_stack *gui_te_slot(struct server_player *p, int i, struct tile_entity **owner_out)
{
    struct container *c = p->open_container;
    if (!c || p->gui_ender || i < 0 || i >= (c->kind == CONTAINER_FURNACE ? 3 : c->chest_size)) return NULL;
    if (c->kind != CONTAINER_FURNACE && !container_kind_tile_inv(c->kind)) return NULL;
    struct tile_entity *owner = c->kind == CONTAINER_CHEST && i >= 27 ?
        (p->gui_has_pair ? world_tile_entity(gui_world(p), p->gui_pair_x, p->gui_y, p->gui_pair_z) : NULL) :
        world_tile_entity(gui_world(p), p->gui_x, p->gui_y, p->gui_z);
    if (!owner) return NULL;
    if (owner_out) *owner_out = owner;
    switch (c->kind)
    {
        case CONTAINER_FURNACE: return owner->kind == TE_FURNACE ? &owner->u.furnace.slots[i] : NULL;
        case CONTAINER_DISPENSER: return owner->kind == TE_DISPENSER ? &owner->u.dispenser.slots[i] : NULL;
        case CONTAINER_HOPPER: return owner->kind == TE_HOPPER ? &owner->u.hopper.slots[i] : NULL;
        default: return owner->kind == TE_CHEST ? &owner->u.chest.slots[i % 27] : NULL;
    }
}

static void gui_container_set(struct container *c, int i, struct craft_stack *cs)
{
    if (c->kind == CONTAINER_FURNACE) container_set_furnace(c, i, cs);
    else container_set_chest(c, i, cs);
}

static void gui_tile_pull(struct server_player *p)
{
    struct container *c = p->open_container;
    if (!c || (c->kind != CONTAINER_FURNACE && !container_kind_tile_inv(c->kind))) return;

    /* the ender window's chest slots are the player's own ender inventory */
    if (p->gui_ender)
    {
        for (int i = 0; i < c->chest_size; ++i)
        {
            struct craft_stack cs = craft_from_surv(&p->sv.ender[i]);
            container_set_chest(c, i, &cs);
        }
        return;
    }

    if (!world_tile_entity(gui_world(p), p->gui_x, p->gui_y, p->gui_z)) return;
    int n = c->kind == CONTAINER_FURNACE ? 3 : c->chest_size;
    for (int i = 0; i < n; ++i)
    {
        const struct te_stack *ts = gui_te_slot(p, i, NULL);
        if (!ts) continue;
        struct craft_stack cs = craft_from_te(ts);
        gui_container_set(c, i, &cs);
    }
}

/* The open tile window's tile slot i (NULL for the ender window, another
 * kind, or a missing tile). */
static const struct te_stack *gui_tile_slot(struct server_player *p, int i)
{
    return gui_te_slot(p, i, NULL);
}

static int gui_tile_count(const struct server_player *p)
{
    const struct container *c = p->open_container;
    return !c ? 0 : c->kind == CONTAINER_FURNACE ? 3 : container_kind_tile_inv(c->kind) ? c->chest_size : 0;
}

/* Container.detectAndSendChanges's inventoryItemStacks for the tile slots:
 * the window's open S2Fs and every click's detectAndSendChanges (its S2Fs
 * held back by isChangingQuantityOnly) leave them equal to the tile. */
static void gui_tile_record(struct server_player *p)
{
    for (int i = 0; i < gui_tile_count(p); ++i)
    {
        const struct te_stack *ts = gui_tile_slot(p, i);
        if (!ts) continue;
        p->gui_sent[i].item = ts->count > 0 ? ts->item : -1;
        p->gui_sent[i].count = ts->count > 0 ? ts->count : 0;
        p->gui_sent[i].damage = ts->count > 0 ? ts->damage : 0;
        p->gui_sent[i].tag = ts->count > 0 ? ts->tag : 0;
    }
}

/* ContainerFurnace.detectAndSendChanges's tail: an S31 for each of the
 * furnace's three progress fields that moved since the last send (0
 * field_145961_j the cook time, 1 field_145956_a the burn time, 2
 * field_145963_i the fuel's total), then the last-sent values follow the
 * tile. The window's crafter is its one player. */
static void gui_furnace_progress(struct server_player *p)
{
    struct container *c = p->open_container;
    if (!c || c->kind != CONTAINER_FURNACE) return;
    struct tile_entity *te = world_tile_entity(gui_world(p), p->gui_x, p->gui_y, p->gui_z);
    if (!te || te->kind != TE_FURNACE) return;
    int now[3] = {te->u.furnace.cook_time, te->u.furnace.burn_time, te->u.furnace.fuel_total};
    for (int k = 0; k < 3; ++k)
    {
        if (c->furnace_progress[k] == now[k]) continue;
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S31;
        pkt->i0 = c->window_id;
        pkt->i1 = k;
        pkt->i2 = now[k];
    }
    for (int k = 0; k < 3; ++k) c->furnace_progress[k] = now[k];
}

/* EntityPlayerMP.onUpdate's openContainer.detectAndSendChanges over the tile
 * slots: whatever the tile pass changed since the last send (a smelt, the
 * fuel burning down) goes to the client container as an S2F. */
static void gui_tile_detect(struct server_player *p)
{
    struct container *c = p->open_container;
    for (int i = 0; i < gui_tile_count(p); ++i)
    {
        const struct te_stack *ts = gui_tile_slot(p, i);
        if (!ts) continue;
        int item = ts->count > 0 ? ts->item : -1;
        int count = ts->count > 0 ? ts->count : 0;
        int damage = ts->count > 0 ? ts->damage : 0;
        int tag = ts->count > 0 ? ts->tag : 0;
        if (p->gui_sent[i].item == item && p->gui_sent[i].count == count &&
            p->gui_sent[i].damage == damage && p->gui_sent[i].tag == tag) continue;
        p->gui_sent[i].item = item; p->gui_sent[i].count = count; p->gui_sent[i].damage = damage;
        p->gui_sent[i].tag = tag;
        struct craft_stack cs = {item, count, damage, tag};
        gui_container_set(c, i, &cs);
        gui_queue_slot(p, i, container_slot(c, i));
    }
}

static void pickup_detect_and_send_changes(struct server_player *p)
{
    gui_tile_detect(p);
    detect_and_send_changes(p);
    gui_furnace_progress(p);
}

void surv_server_gui_pull(struct server_player *p)
{
    gui_tile_pull(p);
}

/* Container.addCraftingToCrafters' sendContainerAndContentsToPlayer: the
 * window's S30 carries the player's 36 slots after the container's own, and
 * the cursor's S2F follows, so a client copy that drifted from the server's
 * (a refused click's cursor that a close in the same tick spilled) is set
 * back. The new container's slot copies are the current stacks. */
static void gui_send_player_slots(struct server_player *p)
{
    for (int s = 9; s < 45; ++s)
    {
        p->sv.mirror[s] = server_slot_stack(p, s);
        queue_s2f(p, s)->s30 = 1;
    }
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S2F;
    pkt->window = -1;
    pkt->slot = -1;
    pkt->i0 = p->sv.cursor.count > 0 ? p->sv.cursor.item : -1;
    pkt->i1 = p->sv.cursor.count > 0 ? p->sv.cursor.damage : 0;
    pkt->i2 = p->sv.cursor.count > 0 ? p->sv.cursor.count : 0;
    pkt->st = p->sv.cursor;
}

static void close_container(struct server_player *p, int screen);

/* EntityPlayerMP's window opens over a tile: displayGUIChest (a chest or a
 * double chest's InventoryLargeChest), func_146101_a (the furnace),
 * func_146102_a (the dispenser, and the dropper under its own window type)
 * and func_146093_a (the hopper). Each bumps the window id, sends the S2D,
 * builds the container over the tile and adds the player as its crafter:
 * the S30 of every slot (the tile slots' S2Fs here, then the player's) and,
 * for the furnace, the three S31 progress
 * values (its first detectAndSendChanges sends the nonzero ones, then
 * addCraftingToCrafters sends all three again; the client keeps the last). */
static void gui_open_tile(struct server_player *p, int kind, int x, int y, int z,
                          int px, int pz, int has_pair)
{
    struct tile_entity *te = world_tile_entity(p->e.world, x, y, z);
    if (!te || (kind == CONTAINER_CHEST && te->kind != TE_CHEST) ||
        (kind == CONTAINER_FURNACE && te->kind != TE_FURNACE) ||
        (kind == CONTAINER_DISPENSER && te->kind != TE_DISPENSER) ||
        (kind == CONTAINER_HOPPER && te->kind != TE_HOPPER)) return;
    /* EntityPlayerMP.getNextWindowId */
    p->window_id = p->window_id % 100 + 1;
    if (p->open_container == &p->wb_container) container_free(&p->wb_container);
    container_init(&p->wb_container, kind, kind == CONTAINER_CHEST ? has_pair ? 54 : 27 : 0, 0);
    surv_seed_container(&p->wb_container, p);
    p->wb_container.window_id = p->window_id;
    p->open_container = &p->wb_container;
    gui_window_opened(p);
    p->gui_x = x; p->gui_y = y; p->gui_z = z;
    p->gui_world = p->e.world;
    p->gui_pair_x = px; p->gui_pair_z = pz; p->gui_has_pair = has_pair;
    p->gui_ender = 0;
    /* the tiles the container holds (canInteractWith's getTileEntity == this) */
    p->gui_te = te;
    p->gui_pair_te = has_pair ? world_tile_entity(p->e.world, px, y, pz) : NULL;
    /* SlotCrafting.onCrafting / SlotFurnace.onCrafting's stat half: the
     * craftItem counter and the achievement chain (acquireIron, cookFish) */
    p->wb_container.crafted = gui_crafted;
    p->wb_container.crafted_ctx = p;
    if (kind == CONTAINER_FURNACE)
    {
        p->wb_container.furnace_xp = gui_furnace_xp;
        p->wb_container.furnace_xp_ctx = p;
    }
    /* ContainerChest's openInventory already ran in the activation
     * (act_live_block's chest_open); the close is act_live_chest_close. The
     * dispenser has no openInventory call, the hopper's is empty. */
    int n = kind == CONTAINER_FURNACE ? 3 : p->wb_container.chest_size;
    for (int i = 0; i < n; ++i)
    {
        const struct te_stack *ts = gui_te_slot(p, i, NULL);
        if (!ts) continue;
        struct craft_stack cs = craft_from_te(ts);
        gui_container_set(&p->wb_container, i, &cs);
    }
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S2D; pkt->i0 = p->window_id; pkt->i1 = kind;
    pkt->i2 = p->wb_container.chest_size;
    if (kind == CONTAINER_DISPENSER)
    {
        /* the S2D's title, TileEntityDispenser.getInventoryName: the
         * dropper's own key (a custom name needs the anvil, off) */
        const char *t = (world_get_block(p->e.world, x, y, z) & 4095) == 158 ? "container.dropper"
                                                                              : "container.dispenser";
        memcpy(s2c_side_new(s2c_out(), pkt, strlen(t) + 1), t, strlen(t) + 1);
    }
    for (int i = 0; i < n; ++i)
    {
        struct craft_stack *s = container_slot(&p->wb_container, i);
        if (s && s->count > 0) gui_queue_slot(p, i, s);
    }
    gui_send_player_slots(p);
    gui_tile_record(p);
    if (kind == CONTAINER_FURNACE)
    {
        /* the first detectAndSendChanges from the zeroed last values, then
         * addCraftingToCrafters' three sends */
        gui_furnace_progress(p);
        for (int k = 0; k < 3; ++k)
        {
            struct s2c_pkt *pr = s2c_add(s2c_out());
            pr->kind = PK_S31;
            pr->i0 = p->window_id;
            pr->i1 = k;
            pr->i2 = p->wb_container.furnace_progress[k];
        }
    }
}

static void gui_tile_changed(struct server_player *p)
{
    struct container *c = p->open_container;
    if (!c || (c->kind != CONTAINER_FURNACE && !container_kind_tile_inv(c->kind))) return;

    /* the ender window's chest slots write the player's own ender inventory */
    if (p->gui_ender)
    {
        for (int i = 0; i < c->chest_size; ++i)
        {
            struct craft_stack *s = container_slot(c, i);
            struct surv_stack *st = &p->sv.ender[i];
            int item = s && s->count > 0 ? s->item : -1;
            int count = s && s->count > 0 ? s->count : 0;
            int damage = s && s->count > 0 ? s->damage : 0;
            int tag = s && s->count > 0 ? s->tag : 0;
            if (st->item == item && st->count == count && st->damage == damage && st->tag == tag) continue;
            st->item = item; st->count = count; st->damage = damage; st->tag = tag;
            gui_queue_slot(p, i, s);
        }
        return;
    }

    if (!world_tile_entity(gui_world(p), p->gui_x, p->gui_y, p->gui_z)) return;
    int n = c->kind == CONTAINER_FURNACE ? 3 : c->chest_size;
    int changed = 0;
    for (int i = 0; i < n; ++i)
    {
        struct craft_stack *s = container_slot(c, i);
        struct tile_entity *owner = NULL;
        struct te_stack *ts = gui_te_slot(p, i, &owner);
        if (!ts) continue;
        int item = s && s->count > 0 ? s->item : -1;
        int count = s && s->count > 0 ? s->count : 0;
        int damage = s && s->count > 0 ? s->damage : 0;
        int tag = s && s->count > 0 ? s->tag : 0;
        if (ts->item == item && ts->count == count && ts->damage == damage && ts->tag == tag) continue;
        ts->item = item; ts->count = count; ts->damage = damage; ts->tag = tag;
        free(owner->raw); owner->raw = NULL;
        gui_queue_slot(p, i, s);
        changed = 1;
    }
    /* the slot writes' markDirty (TileEntity.onInventoryChanged; an
     * InventoryLargeChest's reaches its upper chest, then its lower):
     * World.func_147453_f wakes the comparators around the tile. Java calls
     * it for each setInventorySlotContents, decrStackSize and onSlotChanged
     * of the click; the comparator's schedule dedupes all but the first of
     * a tick, so one call after the click leaves the same pending ticks
     * (only the entry id counter, which no row carries, moves less). */
    if (changed)
    {
        struct world *w = p->e.world;
        comparator_notify(w, p->gui_x, p->gui_y, p->gui_z, world_get_block(w, p->gui_x, p->gui_y, p->gui_z));
        if (p->gui_has_pair)
            comparator_notify(w, p->gui_pair_x, p->gui_y, p->gui_pair_z,
                              world_get_block(w, p->gui_pair_x, p->gui_y, p->gui_pair_z));
    }
}

/* EntityPlayerMP.displayGUIMerchant's useRecipe half: the villager's
 * useRecipe on the recipe the result slot's pickup matched. */
static void surv_merchant_use(void *ctx, int recipe_index)
{
    struct living *l = ctx;
    villager_player_trade(l, recipe_index);
}

/* SlotMerchantResult.onCrafting's ItemStack.onCrafting: the craftItem
 * counter (Item.onCreated does nothing for a traded item). */
static void gui_merchant_crafted(void *ctx, const struct craft_stack *s, int n)
{
    surv_add_stat(ctx, SURV_STAT_CRAFT(s->item), n);
}

/* EntityVillager.func_110297_a_ (the sell slot's yes/no sound) on the
 * container's reset. */
static void surv_merchant_sound(void *ctx, const struct craft_stack *sell)
{
    struct living *l = ctx;
    villager_func_110297_a_(l, sell != NULL && sell->count > 0);
}

/* The server's ContainerMerchant over the villager's offers, and the
 * villager's customer (setCustomer). */
static void gui_merchant_container(struct server_player *p, struct living *l)
{
    if (p->open_container == &p->wb_container) container_free(&p->wb_container);
    container_init(&p->wb_container, CONTAINER_MERCHANT, 0, 0);
    surv_seed_container(&p->wb_container, p);
    p->wb_container.window_id = p->window_id;
    p->wb_container.recipes = &lv_villager(l)->recipes;
    p->wb_container.merchant_use = surv_merchant_use;
    p->wb_container.merchant_use_ctx = l;
    p->wb_container.merchant_sound = surv_merchant_sound;
    p->wb_container.merchant_sound_ctx = l;
    p->wb_container.merchant_crafted = gui_merchant_crafted;
    p->wb_container.merchant_crafted_ctx = p;
    p->open_container = &p->wb_container;
    gui_window_opened(p);

    l->buying_player = 1;
    /* EntityPlayer.getCommandSenderName is the session's "Player" (Main.java's
     * --username) */
    snprintf(lv_villager(l)->buying_player_name, sizeof lv_villager(l)->buying_player_name, "%s", "Player");
    /* the container close resets it (setCustomer(null)) */
}

/* EntityPlayerMP.displayGUIMerchant: the window id bump, the container and
 * its addCraftingToCrafters (the slots' S2Fs), then the S2D and the S3F
 * recipe list. */
static void gui_open_merchant(struct server_player *p, struct living *l)
{
    /* getRecipes: the first trade builds the offer list (the shuffle Random
     * the shared an_world field points at) */
    villager_get_recipes(l, l->an->shuf_rand);

    /* EntityPlayerMP.getNextWindowId */
    p->window_id = p->window_id % 100 + 1;
    gui_merchant_container(p, l);

    /* addCraftingToCrafters runs ahead of the S2D here: its S30 and the
     * first detectAndSendChanges' S2Fs name a window the client has not
     * opened yet, which drops them (a count the client predicted stays) */
    for (int i = 0; i < 3; ++i)
    {
        struct craft_stack *s = container_slot(&p->wb_container, i);
        if (s && s->count > 0) gui_queue_slot(p, i, s);
    }
    gui_send_player_slots(p);

    struct s2c_queue *q = s2c_out();
    struct s2c_pkt *pkt = s2c_add(q);
    pkt->kind = PK_S2D;
    pkt->i0 = p->window_id;
    pkt->i1 = CONTAINER_MERCHANT;
    pkt->i2 = 3;
    /* EntityVillager.interact's displayGUIMerchant(this, getCustomNameTag()),
     * the title out of line when there is one */
    if (l->custom_name[0] != '\0')
    {
        size_t len = strlen(l->custom_name);
        if (len > S2C_TITLE - 1) len = S2C_TITLE - 1;
        memcpy(s2c_side_new(q, pkt, len + 1), l->custom_name, len);
    }

    /* the S3F MC|TrList: the villager's offer list as the client reads it */
    struct s2c_pkt *tr = s2c_add(q);
    tr->kind = PK_S3F;
    tr->i0 = p->window_id;
    tr->ntrlist = lv_villager(l)->recipes.n;
    int ntr = tr->ntrlist < TRADES_MAX ? tr->ntrlist : TRADES_MAX;
    struct s2c_trade *trlist = ntr > 0 ? s2c_side_new(q, tr, (size_t)ntr * sizeof *trlist) : NULL;
    for (int i = 0; i < ntr; ++i)
    {
        const struct trade_recipe *r = &lv_villager(l)->recipes.r[i];
        trlist[i].item = r->buy.item;
        trlist[i].count = r->buy.count;
        trlist[i].damage = r->buy.damage;
        trlist[i].tag = r->buy.tag;
        trlist[i].has_buy_b = r->has_buy_b;
        if (r->has_buy_b)
        {
            trlist[i].item_b = r->buy_b.item;
            trlist[i].count_b = r->buy_b.count;
            trlist[i].damage_b = r->buy_b.damage;
            trlist[i].tag_b = r->buy_b.tag;
        }
        trlist[i].sell_item = r->sell.item;
        trlist[i].sell_count = r->sell.count;
        trlist[i].sell_damage = r->sell.damage;
        trlist[i].sell_tag = r->sell.tag;
        trlist[i].disabled = trades_is_disabled(r);
    }
}

/* displayGUIChest over a chest or a double chest (InventoryLargeChest):
 * an open window is closed first (closeScreen: the S2E and
 * onContainerClosed, a chest window's closeInventory), then
 * ContainerChest's constructor runs openInventory, the upper half first,
 * and the window opens. Two use presses in one tick close and reopen the
 * same chest, and the block events of the tick (world_block_event) keep
 * its count at the close's. */
static void gui_open_chest(struct server_player *p, int x, int y, int z, int px, int pz, int has_pair)
{
    struct tile_entity *te = world_tile_entity(p->e.world, x, y, z);
    if (!te || te->kind != TE_CHEST) return;
    if (p->open_container != &p->own_container) close_container(p, 1);
    act_live_chest_open(p->e.world, world_tile_entity(p->e.world, x, y, z));
    if (has_pair)
    {
        struct tile_entity *other = world_tile_entity(p->e.world, px, y, pz);
        if (other) act_live_chest_open(p->e.world, other);
    }
    gui_open_tile(p, CONTAINER_CHEST, x, y, z, px, pz, has_pair);
}

/* BlockEnderChest.onBlockActivated's func_146031_a and displayGUIChest(
 * InventoryEnderChest): the chest becomes the associated one first, so an
 * open ender window's closeScreen counts the new chest down and forgets it;
 * then a CONTAINER_CHEST with 27 chest slots over the player's own ender
 * inventory, whose constructor's openInventory counts the associated chest
 * (if any is left) up. The close runs func_145970_b. */
static void gui_open_ender(struct server_player *p, int x, int y, int z)
{
    p->ender_assoc = 1;
    p->ender_ax = x; p->ender_ay = y; p->ender_az = z;
    if (p->open_container != &p->own_container) close_container(p, 1);
    if (p->ender_assoc)
    {
        struct tile_entity *te = world_tile_entity(p->e.world, x, y, z);

        if (te && te->kind == TE_ENDER_CHEST)
        {
            ++te->u.chest.players_using;
            world_block_event(p->e.world, x, y, z, 130, 1, te->u.chest.players_using);
            env_block_event(p->e.world, x, y, z, 130, 1, te->u.chest.players_using);
        }
    }

    p->window_id = p->window_id % 100 + 1;
    if (p->open_container == &p->wb_container) container_free(&p->wb_container);
    container_init(&p->wb_container, CONTAINER_CHEST, 27, 0);
    surv_seed_container(&p->wb_container, p);
    p->wb_container.window_id = p->window_id;
    p->open_container = &p->wb_container;
    gui_window_opened(p);
    p->gui_x = x; p->gui_y = y; p->gui_z = z;
    p->gui_world = p->e.world;
    p->gui_pair_x = 0; p->gui_pair_z = 0;
    p->gui_has_pair = 0;
    p->gui_ender = 1;
    /* InventoryEnderChest.associatedChest */
    p->gui_te = world_tile_entity(p->e.world, x, y, z);
    p->gui_pair_te = NULL;

    for (int i = 0; i < 27; ++i)
    {
        struct craft_stack cs = craft_from_surv(&p->sv.ender[i]);
        container_set_chest(&p->wb_container, i, &cs);
    }

    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S2D; pkt->i0 = p->window_id; pkt->i1 = CONTAINER_CHEST;
    pkt->i2 = 27;
    for (int i = 0; i < 27; ++i)
    {
        struct craft_stack *s = container_slot(&p->wb_container, i);
        if (s && s->count > 0) gui_queue_slot(p, i, s);
    }
    gui_send_player_slots(p);
}

/* ------------------------------------------------ a snapshot's open window */

static struct craft_stack gui_cs_from_nbt(const nbt *item)
{
    struct craft_stack cs = {-1, 0, 0, 0};
    if (item == NULL || nbt_get(item, "id") == NULL) return cs;
    cs.item = (int)nbt_int_value(nbt_get(item, "id"));
    cs.count = (int)nbt_int_value(nbt_get(item, "Count"));
    cs.damage = (int)nbt_int_value(nbt_get(item, "Damage"));
    cs.tag = itag_from_item(item);
    if (cs.count <= 0) cs.item = -1;
    return cs;
}

static int gui_kind_of(const char *cls)
{
    if (!cls) return -1;
    if (!strcmp(cls, "ContainerPlayer")) return CONTAINER_PLAYER;
    if (!strcmp(cls, "ContainerWorkbench")) return CONTAINER_WORKBENCH;
    if (!strcmp(cls, "ContainerFurnace")) return CONTAINER_FURNACE;
    if (!strcmp(cls, "ContainerChest")) return CONTAINER_CHEST;
    if (!strcmp(cls, "ContainerDispenser")) return CONTAINER_DISPENSER;
    if (!strcmp(cls, "ContainerHopper")) return CONTAINER_HOPPER;
    return -1;
}

/* The grid, the furnace's progress and the cursor of a restored window. */
static void gui_fill(struct container *c, int kind, const nbt *g)
{
    const nbt *slots = nbt_get(g, "slots");
    int grid = kind == CONTAINER_PLAYER ? 4 : kind == CONTAINER_WORKBENCH ? 9 : 0;
    for (int i = 0; i < grid && slots && i + 1 < nbt_list_size(slots); ++i)
    {
        struct craft_stack cs = gui_cs_from_nbt(nbt_list_get(slots, i + 1));
        container_set_grid(c, i, &cs);
    }
    const nbt *fur = nbt_get(g, "fur");
    int n = 0;
    const int *fv = fur ? nbt_int_array(fur, &n) : NULL;
    for (int k = 0; fv && k < 3 && k < n; ++k) c->furnace_progress[k] = fv[k];
    struct craft_stack cur = gui_cs_from_nbt(nbt_get(g, "cursor"));
    container_set_cursor(c, &cur);
}

/* A restored ContainerMerchant's InventoryMerchant: the three stacks, then
 * currentRecipeIndex and currentRecipe as the snapshot recorded them (no
 * resetRecipeAndSlots: the slots are already what it left). */
static void gui_merchant_fill(struct container *c, const nbt *g)
{
    const nbt *slots = nbt_get(g, "slots");
    for (int i = 0; i < 3 && slots && i < nbt_list_size(slots); ++i)
    {
        struct craft_stack cs = gui_cs_from_nbt(nbt_list_get(slots, i));
        container_set_merchant(c, i, &cs);
    }
    c->current_recipe_index = (int)nbt_int_value(nbt_get(g, "trIdx"));
    c->current_recipe = nbt_get(g, "trCur") ? (int)nbt_int_value(nbt_get(g, "trCur")) : -1;
    struct craft_stack cur = gui_cs_from_nbt(nbt_get(g, "cursor"));
    container_set_cursor(c, &cur);
}

void surv_gui_load(struct client_player *cp, struct server_player *sp, const void *ctree, const void *stree)
{
    /* the server's open window (player_server.nbt's gui): the tile window
     * built as its open built it, without the packets its open sent long
     * ago; the recorded window id, grid, cursor and progress, and the tile
     * slots' last sent stacks (openMirror) */
    const nbt *g = nbt_get((const nbt *)stree, "gui");
    const nbt *kv = g ? nbt_get(g, "kind") : NULL;
    int kind = kv ? gui_kind_of(nbt_string_value(kv)) : -1;
    if (kind == CONTAINER_PLAYER) gui_fill(&sp->own_container, kind, g);
    else if (kv && !strcmp(nbt_string_value(kv), "ContainerMerchant"))
    {
        /* the villager the window trades with (its offers and customer) */
        int mid = (int)nbt_int_value(nbt_get(g, "merchant"));
        struct living *l = NULL;
        for (int i = 0; surv.anw && i < surv.anw->n && !l; ++i)
        {
            struct an_ent *en = an_ent_at(surv.anw->slot[i]);
            if (en->used && en->is_living && en->livh && lv_get(en->livh)->entity_id == mid) l = lv_get(en->livh);
        }
        if (l)
        {
            sp->window_id = (int)nbt_int_value(nbt_get(g, "window"));
            gui_merchant_container(sp, l);
            gui_merchant_fill(&sp->wb_container, g);
            const nbt *m = nbt_get((const nbt *)stree, "openMirror");
            for (int i = 0; m && i < 3 && i < nbt_list_size(m); ++i)
            {
                struct craft_stack cs = gui_cs_from_nbt(nbt_list_get(m, i));
                sp->gui_sent[i].item = cs.count > 0 ? cs.item : 0;
                sp->gui_sent[i].count = cs.count;
                sp->gui_sent[i].damage = cs.damage;
                sp->gui_sent[i].tag = cs.tag;
            }
        }
    }
    else if (kind >= 0)
    {
        int n = 0;
        const int *t = nbt_int_array(nbt_get(g, "tiles"), &n);
        int ender = nbt_get(g, "ender") != NULL;
        int saved = s2c_out()->n;
        if (kind == CONTAINER_WORKBENCH)
        {
            container_free(&sp->wb_container);
            container_init(&sp->wb_container, CONTAINER_WORKBENCH, 0, 0);
            surv_seed_container(&sp->wb_container, sp);
            sp->open_container = &sp->wb_container;
            gui_window_opened(sp);
            if (t && n >= 3) { sp->gui_x = t[0]; sp->gui_y = t[1]; sp->gui_z = t[2]; sp->gui_world = NULL; }
            sp->wb_container.crafted = gui_crafted;
            sp->wb_container.crafted_ctx = sp;
        }
        else if (ender && t && n >= 3) gui_open_ender(sp, t[0], t[1], t[2]);
        else if (kind == CONTAINER_CHEST && t && n >= 6) gui_open_tile(sp, kind, t[0], t[1], t[2], t[3], t[5], 1);
        else if (t && n >= 3) gui_open_tile(sp, kind, t[0], t[1], t[2], 0, 0, 0);
        s2c_out()->n = saved;
        int window = (int)nbt_int_value(nbt_get(g, "window"));
        sp->window_id = window;
        sp->wb_container.window_id = window;
        gui_fill(&sp->wb_container, kind, g);
        const nbt *m = nbt_get((const nbt *)stree, "openMirror");
        int tiles_n = kind == CONTAINER_FURNACE ? 3 : kind == CONTAINER_WORKBENCH ? 0 : sp->wb_container.chest_size;
        for (int i = 0; m && i < tiles_n && i < 54 && i < nbt_list_size(m); ++i)
        {
            struct craft_stack cs = gui_cs_from_nbt(nbt_list_get(m, i));
            sp->gui_sent[i].item = cs.count > 0 ? cs.item : 0;
            sp->gui_sent[i].count = cs.count;
            sp->gui_sent[i].damage = cs.damage;
            sp->gui_sent[i].tag = cs.tag;
        }
    }

    /* the client's screen and its copy of the window */
    const nbt *cg = nbt_get((const nbt *)ctree, "gui");
    if (!cg) return;
    const nbt *scv = nbt_get(cg, "screen"), *ckv = nbt_get(cg, "kind");
    const char *screen = scv ? nbt_string_value(scv) : NULL;
    int ckind = gui_kind_of(ckv ? nbt_string_value(ckv) : NULL);
    /* the trade screen over the client's own ContainerMerchant: the
     * NpcMerchant's list the S3F gave it */
    if (screen && !strcmp(screen, "GuiMerchant") && ckv && !strcmp(nbt_string_value(ckv), "ContainerMerchant"))
    {
        cp->screen_inventory = 1;
        cp->in_game_has_focus = 0;
        container_free(&cp->wb_container);
        container_init(&cp->wb_container, CONTAINER_MERCHANT, 0, 0);
        surv_seed_client_container(&cp->wb_container, cp);
        cp->wb_container.window_id = (int)nbt_int_value(nbt_get(cg, "window"));
        if (nbt_get(cg, "trList"))
        {
            trades_from_nbt(&cp->wb_container.client_recipes, nbt_get(cg, "trList"));
            cp->wb_container.recipes = &cp->wb_container.client_recipes;
        }
        gui_merchant_fill(&cp->wb_container, cg);
        cp->open_container = &cp->wb_container;
    }
    /* the screens the client puts up on its own */
    if (screen && !strcmp(screen, "GuiGameOver")) cp->screen_gameover = 1;
    if (screen && !strcmp(screen, "GuiSleepMP"))
    {
        cp->screen_sleep = 1;
        cp->in_game_has_focus = 0;
        gui_chat_open(cp, "", 1, GC_SCREEN_W, GC_SCREEN_H);
    }
    if (screen && !strcmp(screen, "GuiWinGame")) { cp->screen_credits = 1; cp->credits_clock = 0; cp->in_game_has_focus = 0; }
    if (screen && !strcmp(screen, "GuiChat"))
    {
        /* the snapshot carries no GuiChat text: an empty one */
        cp->screen_chat = 1;
        cp->in_game_has_focus = 0;
        gui_chat_open(cp, "", 0, GC_SCREEN_W, GC_SCREEN_H);
    }
    if (screen && ckind >= 0 && (!strcmp(screen, "GuiInventory") || !strcmp(screen, "GuiChest") ||
                                 !strcmp(screen, "GuiFurnace") || !strcmp(screen, "GuiDispenser") ||
                                 !strcmp(screen, "GuiHopper") || !strcmp(screen, "GuiCrafting")))
    {
        cp->screen_inventory = 1;
        cp->in_game_has_focus = 0;
        if (ckind == CONTAINER_PLAYER)
        {
            cp->open_container = &cp->own_container;
            gui_fill(&cp->own_container, ckind, cg);
        }
        else
        {
            const nbt *slots = nbt_get(cg, "slots");
            int total = slots ? nbt_list_size(slots) : 0;
            int size = ckind == CONTAINER_CHEST ? total - 36 : 0;
            container_free(&cp->wb_container);
            container_init(&cp->wb_container, ckind, size, 0);
            surv_seed_client_container(&cp->wb_container, cp);
            cp->wb_container.window_id = (int)nbt_int_value(nbt_get(cg, "window"));
            cp->open_container = &cp->wb_container;
            int tiles_n = ckind == CONTAINER_FURNACE ? 3 : ckind == CONTAINER_CHEST ? size :
                          ckind == CONTAINER_DISPENSER ? 9 : ckind == CONTAINER_HOPPER ? 5 : 0;
            for (int i = 0; i < tiles_n && i < total; ++i)
            {
                struct craft_stack cs = gui_cs_from_nbt(nbt_list_get(slots, i));
                if (ckind == CONTAINER_FURNACE) container_set_furnace(&cp->wb_container, i, &cs);
                else container_set_chest(&cp->wb_container, i, &cs);
            }
            gui_fill(&cp->wb_container, ckind, cg);
        }
    }
    /* InventoryPlayer's cursor, the client's own copy */
    struct craft_stack cur = gui_cs_from_nbt(nbt_get(cg, "cursor"));
    cp->sv.cursor.item = cur.count > 0 ? cur.item : 0;
    cp->sv.cursor.count = cur.count;
    cp->sv.cursor.damage = cur.damage;
    cp->sv.cursor.tag = cur.tag;
}

/* The ender window's close: InventoryEnderChest.closeInventory, the
 * associated chest's func_145970_b (the count down, with no floor, and its
 * block event: the world's queue and the clients' S24), then
 * associatedChest = null. */
static void gui_close_ender(struct server_player *p)
{
    if (p->ender_assoc)
    {
        struct tile_entity *te = world_tile_entity(p->e.world, p->ender_ax, p->ender_ay, p->ender_az);

        if (te && te->kind == TE_ENDER_CHEST)
        {
            --te->u.chest.players_using;
            world_block_event(p->e.world, te->x, te->y, te->z, 130, 1, te->u.chest.players_using);
            env_block_event(p->e.world, te->x, te->y, te->z, 130, 1, te->u.chest.players_using);
        }
    }

    p->ender_assoc = 0;
}

/* Item.getMaxItemUseDuration: food, milk and potions (splash ones too) 32,
 * the bow and the swords 72000, everything else 0. */
int surv_item_use_duration(const struct surv_stack *st)
{
    if (st->count <= 0 || st->item <= 0 || st->item >= 4096 || !ITEMS[st->item].exists) return 0;
    if (ITEMS[st->item].kind == ITEM_FOOD || st->item == IT_MILK || st->item == IT_POTION) return 32;
    if (st->item == 261 || ITEMS[st->item].kind == ITEM_SWORD) return 72000;
    return 0;
}

/* EntityPlayerMP.sendContainerToPlayer(inventoryContainer): the S30 of the
 * player container (the cursor's S2F -1 carries nothing here), with no
 * health resend (that is syncPlayerInventory's). */
void surv_server_send_container(struct server_player *p)
{
    struct s2c_queue *q = s2c_out();
    struct s2c_pkt *pkt = s2c_add(q);
    pkt->kind = PK_S30;
    struct surv_stack *inv = s2c_side_new(q, pkt, S30_STACKS * sizeof *inv);
    s30_fill(p, inv);
    /* sendContainerAndContentsToPlayer's tail: the cursor's S2F (window -1),
     * which a screen the client closed without a C0D (a portal's) left on
     * the server */
    pkt = s2c_add(s2c_out());
    pkt->kind = PK_S2F;
    pkt->window = -1;
    pkt->slot = -1;
    pkt->i0 = p->sv.cursor.count > 0 ? p->sv.cursor.item : -1;
    pkt->i1 = p->sv.cursor.count > 0 ? p->sv.cursor.damage : 0;
    pkt->i2 = p->sv.cursor.count > 0 ? p->sv.cursor.count : 0;
    pkt->st = p->sv.cursor;
}

/* EntityPlayer.setItemInUse on a stack not already in use. */
static void set_item_in_use(struct surv_state *sv, int slot, int duration)
{
    struct surv_stack *cur = &sv->inv[slot];
    if (sv->using_slot >= 0 && sv->using_slot == slot && sv->using_gen == cur->gen) return;
    sv->using_count = duration;
    sv->using_slot = slot;
    sv->using_gen = cur->gen;
}

static void server_air_use(struct server_player *p);
static int armor_right_click(struct surv_state *sv, struct surv_stack *held);

static void process_c08(struct server_player *p, int kind, int x, int y, int z, int side,
                        float hx, float hy, float hz)
{
    struct surv_state *sv = &p->sv;
    struct surv_stack *cur = &sv->inv[sv->current_item];

    if (kind == 0)
    {
        /* the in-air form: ItemInWorldManager.tryUseItem */
        if (cur->count == 0) return;

        struct surv_stack before = *cur;
        server_air_use(p);

        /* tryUseItem's answer: true when the use returned another stack, a
         * changed size or damage, or a stack with a use duration; then a
         * player not using an item gets sendContainerToPlayer */
        cur = &sv->inv[sv->current_item];
        if (cur->gen == before.gen && cur->item == before.item && cur->count == before.count &&
            cur->damage == before.damage && surv_item_use_duration(cur) <= 0)
            return;
        if (sv->using_slot >= 0) return;
        surv_server_send_container(p);
        return;
    }

    /* the block form: activateBlockOrUseItem at (x,y,z) */
    surv_server_use_packet(p, x, y, z, side, hx, hy, hz);

    /* processPlayerBlockPlacement's tail: the S23 of the clicked block and
     * of its neighbour on the clicked side, which puts back whatever the
     * client predicted there and the server did not do (a bucket's water
     * placed along the client's newer look) */
    if (p->replay != NULL)
    {
        static const int dx[6] = {0, 0, 0, 0, -1, 1}, dy[6] = {-1, 1, 0, 0, 0, 0}, dz[6] = {0, 0, -1, 1, 0, 0};
        serverreplay_resend_block(p->replay, x, y, z);
        if (side >= 0 && side < 6) serverreplay_resend_block(p->replay, x + dx[side], y + dy[side], z + dz[side]);
    }
}

/* The held stack's onItemRightClick, server side. */
static void server_air_use(struct server_player *p)
{
    struct surv_state *sv = &p->sv;
    struct surv_stack *cur = &sv->inv[sv->current_item];

    {
        const struct item_def *d = &ITEMS[cur->item];
        if (d->exists && d->kind == ITEM_FOOD)
        {
            if (!(sv->food.level < 20 || d->always_edible)) return;
            if (sv->using_slot >= 0 && sv->using_slot == sv->current_item && sv->using_gen == cur->gen) return;
            sv->using_count = 32;
            sv->using_slot = sv->current_item;
            sv->using_gen = cur->gen;
            return;
        }
        if (d->exists && d->kind == ITEM_SWORD)
        {
            /* ItemSword.onItemRightClick: setItemInUse(stack, 72000) */
            if (!(sv->using_slot >= 0 && sv->using_slot == sv->current_item && sv->using_gen == cur->gen))
            {
                sv->using_count = 72000;
                sv->using_slot = sv->current_item;
                sv->using_gen = cur->gen;
            }
            return;
        }
        /* ItemBucketMilk and a drinkable ItemPotion: setItemInUse(stack, 32) */
        if (cur->item == IT_MILK || (cur->item == IT_POTION && !(cur->damage & 16384)))
        {
            set_item_in_use(sv, sv->current_item, 32);
            return;
        }
        if (throw_server_use(p)) return;
        if (ride_carrot_right_click(p)) return;
        if (armor_right_click(sv, cur)) return;
        if (surv.world_rand)
        {
            struct iu_player ip = {p->e.pos_x, p->e.pos_y, p->e.pos_z,
                                   p->e.y_offset, p->rotation_yaw, p->rotation_pitch, 1, 0};
            struct iu_stack stack = {cur->item, cur->count, cur->damage}, give;
            struct randomtick_env drop_env;
            if (p->replay != NULL) serverreplay_drop_env_begin(p->replay, &drop_env);
            itemuse_set_live_det(surv.det, DET_SERVER);
            (void)itemuse_try_use_item(p->e.world, &ip, &stack, surv.world_rand, &give);
            itemuse_set_live_det(NULL, DET_SERVER);
            if (p->replay != NULL) serverreplay_drop_env_end(&drop_env);
            /* the filled bucket or bottle a stack gives up, inside
             * onItemRightClick: the held slot keeps its (smaller) stack, so
             * the add never lands there */
            if (give.item != 0)
            {
                struct surv_stack st = {.item = give.item, .count = give.count, .damage = give.damage};
                give_or_drop(p, sv, &st);
                cur = &sv->inv[sv->current_item];
            }
            /* tryUseItem counts no useItem stat (only tryPlaceItemIntoWorld,
             * hitEntity and onBlockDestroyed do) */
            if (stack.item != cur->item || stack.count != cur->count || stack.damage != cur->damage)
            {
                /* a use that returns another item returns a new ItemStack,
                 * with no tag */
                if (stack.item != cur->item) cur->tag = 0;
                cur->item = stack.item; cur->count = stack.count; cur->damage = stack.damage;
                cur->gen = ++sv->gen_counter;
            }
        }
    }
}

/* The items that are no ItemBlock but place a block through their own
 * onItemUse on both sides (place.c's item_door_on_item_use and
 * item_seeds_on_item_use): ItemDoor.func_150924_a's two halves, the crops,
 * stems and nether wart planted on their soil. */
static int surv_place_item_class(const char *cls)
{
    return !strcmp(cls, "ItemDoor") || !strcmp(cls, "ItemSeeds") || !strcmp(cls, "ItemSeedFood");
}

/* EntityAnimal.isBreedingItem per kind: wheat, a pig's carrot, a chicken's
 * ItemSeeds (the wheat, pumpkin and melon seeds and the nether wart). */
static int animal_breeding_item(int kind, int item)
{
    switch (kind)
    {
        case AK_PIG: return item == 391;
        case AK_CHICKEN: return item == 295 || item == 361 || item == 362 || item == 372;
        case AK_COW: case AK_MOOSHROOM: case AK_SHEEP: return item == 296;
    }
    return 0;
}

/* EntityPlayer.interactWith(entity) for the passive animals, on either side:
 * EntityLiving.interactFirst -> interact (EntityMooshroom's bowl, EntityCow's
 * bucket, EntitySheep's shears, EntityAnimal's breeding item), and the stack
 * bookkeeping after it. The client reads the server's copy of the animal
 * (growing age, love timer, fleece), since the replay has no client entities.
 * The server side also runs the effects: the love timer and the wool drops.
 * Returns interactWith's result (the shears return false: the sheep calls
 * super.interact, and ItemShears has no itemInteractionForEntity). */
static int entity_interact(struct surv_state *sv, int slot, struct living *l, int server,
                           struct server_player *sp, struct client_player *cp)
{
    if (!l) return 0;
    struct surv_stack *held = &sv->inv[slot];
    if (held->count == 0) return 0;

    /* EntityZombie.interact: a plain golden apple on a zombie villager under
     * weakness is used up on either side, and the server starts the cure
     * (startConversion(rand.nextInt(2401) + 3600)) */
    if (l->kind == HK_ZOMBIE && held->item == 322 && held->damage == 0 && l->zombie_is_villager &&
        living_is_potion_active(l, POT_WEAKNESS))
    {
        if (--held->count <= 0)
        {
            *held = empty_stack();
            held->gen = ++sv->gen_counter;
        }
        if (server) zombie_start_conversion(l, det_rng_int_n(&l->rand, 2401) + 3600, l->an != NULL ? l->an->det : NULL);
        return 1;
    }

    if (l->kind == AK_MOOSHROOM && held->item == 281 && l->growing_age >= 0)
    {
        struct surv_stack stew = {.item = 282, .count = 1};
        if (held->count == 1)
        {
            *held = stew;
            held->gen = ++sv->gen_counter;
            return 1;
        }
        if (add_item_stack_to_inventory(sv, &stew))
        {
            --held->count;
            return 1;
        }
    }

    /* EntityMooshroom.interact's shears: the mooshroom goes, a plain cow
     * takes its place, health and body yaw, five red mushrooms spawn at its
     * top (EntityItem's own constructor: no pickup delay) and the shears wear
     * one point; true on both sides */
    if (l->kind == AK_MOOSHROOM && held->item == 359 && l->growing_age >= 0)
    {
        /* the client copy's setDead and largeexplode are the caller's
         * (surv_client_entity_interact) */
        if (server && surv.anw)
        {
            living_set_dead(l);
            struct an_world *an = surv.anw;
            if (an->constructor_hook) an->constructor_hook(an, 1, an->constructor_ctx);
            struct living *cow = an_spawn_living(an, AK_COW, an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z,
                                                 l->rotation_yaw, l->rotation_pitch, 0, 0, 0, 0, 0, 0);
            if (an->constructor_hook) an->constructor_hook(an, 0, an->constructor_ctx);
            if (cow)
            {
                living_set_health(cow, l->health);
                cow->render_yaw_offset = l->render_yaw_offset;
                /* spawnEntityInWorld's tracker entry and S0F, now in the
                 * network tick */
                if (an->track_spawn) an->track_spawn(an, cow, an->constructor_ctx);
            }
            for (int i = 0; i < 5; ++i)
                (void)an_spawn_item(an, an->n, l->e.pos_x, l->e.pos_y + (double)l->e.height, l->e.pos_z,
                                    40, 0, 1, 0);
            /* ItemStack.damageItem(1, player); interactWith's
             * interactFirst branch then drops a worn-out stack
             * (destroyCurrentEquippedItem) */
            if (surv_damage_stack(sp, held, 1) && held->count <= 0) surv_destroy_current_item(sp);
        }
        return 1;
    }

    /* EntityCreeper.interact: flint and steel. The fire.ignite pitch draws
     * the creeper's Random on either side (the client's copy is not
     * modelled); the server lights the fuse (func_146079_cb, watcher 18)
     * and wears the flint one point. The client's answer is super's false,
     * so its right click goes on to the in-air item use. */
    if (l->kind == HK_CREEPER && held->item == 259)
    {
        if (!server) return 0;
        (void)det_rng_float(&l->rand);
        l->creeper_ignited = 1;
        /* damageItem(1, player), then interactWith's
         * destroyCurrentEquippedItem on a worn-out stack */
        if (surv_damage_stack(sp, held, 1) && held->count <= 0) surv_destroy_current_item(sp);
        return 1;
    }

    if ((l->kind == AK_COW || l->kind == AK_MOOSHROOM) && held->item == 325)
    {
        if (held->count-- == 1)
        {
            held->item = 335;
            held->count = 1;
            held->damage = 0;
            held->tag = 0;
            held->gen = ++sv->gen_counter;
        }
        else
        {
            struct surv_stack milk = {.item = 335, .count = 1};
            give_or_drop(server ? sp : NULL, sv, &milk);
        }
        return 1;
    }

    if (l->kind == AK_SHEEP && held->item == 359 && !(l->data_watcher_16 & 16) &&
        l->growing_age >= 0)
    {
        if (server)
        {
            l->data_watcher_16 |= 16;
            int count = 1 + det_rng_int_n(&l->rand, 3);
            for (int i = 0; i < count; ++i)
            {
                /* Entity.entityDropItem(wool of the fleece color, 1.0F) */
                ie_ent *drop = an_spawn_item(surv.anw, surv.anw->n, l->e.pos_x,
                        l->e.pos_y + 1.0, l->e.pos_z, 35, l->data_watcher_16 & 15, 1, 10);
                if (!drop) continue;
                drop->e.motion_y += (double)(det_rng_float(&l->rand) * 0.05F);
                float a = det_rng_float(&l->rand);
                float b = det_rng_float(&l->rand);
                drop->e.motion_x += (double)((a - b) * 0.1F);
                a = det_rng_float(&l->rand);
                b = det_rng_float(&l->rand);
                drop->e.motion_z += (double)((a - b) * 0.1F);
            }
        }
        /* ItemStack.damageItem(1, player) on either side; the sheep's
         * interact answers false, so a worn-out stack stays at count 0 */
        if (server) (void)surv_damage_stack(sp, held, 1);
        else (void)client_damage_stack(cp, held, 1);
    }

    /* ItemDye.itemInteractionForEntity, reached through interactWith only
     * when interactFirst returned false (the sheep's shears branch above
     * does not apply): dye the fleece when the sheep is unsheared and the
     * color differs, consume one dye on a change, and always return true
     * for a sheep (BlockColored.func_150032_b is ~damage & 15, and
     * setFleeceColor writes watcher 16 as sheared-bit | color). The client
     * runs it on its own copy of the sheep, which the replay does not
     * have: only its stack changes here, the server's call dyes. */
    if (l->kind == AK_SHEEP && held->item == 351)
    {
        int color = ~held->damage & 15;

        if (!(l->data_watcher_16 & 16) && (l->data_watcher_16 & 15) != color)
        {
            if (server) l->data_watcher_16 = (l->data_watcher_16 & 240) | (color & 15);

            if (--held->count <= 0) *held = empty_stack();
        }

        return 1;
    }

    if (animal_breeding_item(l->kind, held->item) && l->growing_age == 0 && l->in_love <= 0)
    {
        if (--held->count <= 0) *held = empty_stack();
        if (server)
        {
            /* func_146082_f: the love timer; the S19 heart state is the
             * client's particles only */
            l->in_love = 600;
            l->love_player = 1;
            l->entity_to_attack = 0;
        }
        return 1;
    }
    return 0;
}

/* ItemNameTag.itemInteractionForEntity, reached through interactWith when
 * interactFirst answered false: a tag with a display name names any
 * EntityLiving (setCustomNameTag, and func_110163_bv's persistenceRequired)
 * and shrinks by one; an unnamed tag (every tag a chest or a trade hands
 * out, the anvil being off) does nothing. The client's copy of the entity
 * is the server's, so only the server side writes it. */
static int name_tag_interact(struct surv_stack *held, struct living *l, int server)
{
    char name[sizeof l->custom_name];
    if (l == NULL || held->count <= 0 || held->item != IT_NAME_TAG || !itag_name(held->tag, name, sizeof name))
        return 0;
    if (server)
    {
        snprintf(l->custom_name, sizeof l->custom_name, "%s", name);
        l->persistence_required = 1;
    }
    --held->count;
    return 1;
}

/* NetHandlerPlayServer.processUseEntity's INTERACT: canEntityBeSeen's
 * eye-to-eye ray trace (on the server world: a Teleporter portal block it
 * crosses takes its axis meta), the entity in reach (6 blocks, 3 when
 * hidden), then EntityPlayerMP.interactWith. */
static void process_c02_interact(struct server_player *p, int entity_id)
{
    struct living *l = surv_find_living(entity_id);
    if (!l)
    {
        /* a leash knot: EntityLeashKnot.interactFirst */
        if (leash_knot_interact(p, entity_id)) return;
        /* a tracked object: the item frame's interactFirst */
        struct surv_stack *held = &p->sv.inv[p->sv.current_item];
        int count = held->count;
        if (p->replay != NULL &&
            pickobj_server_interact(p->replay, entity_id, count > 0 ? held->item : 0, held->damage, held->tag, &count) &&
            count != held->count)
        {
            held->count = count;
            if (count <= 0) *held = empty_stack();
        }
        return;
    }
    struct rt_mop mop;
    int blocked = raytrace_blocks(p->e.world, p->e.pos_x, p->e.pos_y + 1.62F, p->e.pos_z,
                                  l->e.pos_x, l->e.pos_y + (double)(l->e.height * 0.85F), l->e.pos_z,
                                  0, 0, 0, &mop);
    if (!living_is_alive(l)) return;
    double dx = p->e.pos_x - l->e.pos_x;
    double dy = p->e.pos_y - l->e.pos_y;
    double dz = p->e.pos_z - l->e.pos_z;
    if (dx*dx + dy*dy + dz*dz >= (blocked ? 9.0 : 36.0)) return;

    /* EntityLiving.interactFirst's lead half comes first; interactWith then
     * destroys a used-up stack */
    {
        struct surv_stack *held = &p->sv.inv[p->sv.current_item];
        int had = held->count > 0;
        if (leash_interact_first(held, l, 1))
        {
            if (had && held->count <= 0) surv_destroy_current_item(p);
            return;
        }
    }

    /* EntityVillager.interact (through EntityLiving.interactFirst's villager
     * branch): not the spawn egg in hand, an adult, not already trading. */
    if (l->kind == VK_VILLAGER)
    {
        struct surv_stack *held = &p->sv.inv[p->sv.current_item];

        if (held->count > 0 && held->item == 383) return;
        if (l->growing_age < 0 || l->buying_player)
        {
            if (name_tag_interact(held, l, 1) && held->count <= 0) surv_destroy_current_item(p);
            return;
        }

        gui_open_merchant(p, l);
        return;
    }

    /* the pig: EntityAnimal.interact's breeding (entity_interact), then
     * EntityPig.interact's mount, then the held item's
     * itemInteractionForEntity (ItemSaddle), whose used-up stack
     * interactWith destroys */
    if (entity_interact(&p->sv, p->sv.current_item, l, 1, p, NULL)) return;
    struct surv_stack *held = &p->sv.inv[p->sv.current_item];
    if (l->kind == AK_PIG)
    {
        if (ride_pig_interact(p, l)) return;
        if (ride_saddle_interact(held, l, 1))
        {
            if (held->count <= 0) surv_destroy_current_item(p);
            return;
        }
    }
    if (name_tag_interact(held, l, 1) && held->count <= 0) surv_destroy_current_item(p);
}

/* The block C08's server half: the activation (a workbench opens its
 * window) or the held ItemBlock's placement, over the server's own
 * machine. The dig machine's env doubles as the machine here: the world
 * Random continues, the Det role is the server's. */
void surv_server_use_packet(struct server_player *p, int x, int y, int z, int side,
                            float hx, float hy, float hz)
{
    if (!surv_digenv_ready)
    {
        dig_init(&surv_digenv, p->e.world, surv.det, 0);
        surv_digenv.role = DET_SERVER;
        surv_digenv.block_rands = 1;
        surv_digenv.net_ops = 1;
        surv_digenv_ready = 1;
    }

    int id = world_get_block(p->e.world, x, y, z) & 4095;

    /* processPlayerBlockPlacement's reach and height checks */
    double dx = p->e.pos_x - ((double)x + 0.5);
    double dy = p->e.pos_y - ((double)y + 0.5);
    double dz = p->e.pos_z - ((double)z + 0.5);
    if (y >= 256 || (y >= 255 && side == 1))
    {
        chatmsg_build_too_high(p);
        return;
    }
    if (!p->has_moved || dx*dx + dy*dy + dz*dz >= 64.0) return;

    /* ItemInWorldManager.activateBlockOrUseItem: Block.onBlockActivated
     * first unless sneaking with an item; a container block opens its window */
    struct surv_stack *cur = &p->sv.inv[p->sv.current_item];

    /* BlockBed.onBlockActivated: the sleep machine over the live player */
    if (id == BLOCK_BED && (!p->sneaking || cur->count == 0))
    {
        sleep_bed_activate(p, x, y, z);
        return;
    }

    /* BlockTNT.onBlockActivated: flint and steel primes it */
    if (id == 46 && !p->sneaking && tnt_server_activate(p, x, y, z)) return;

    /* BlockCauldron.onBlockActivated's leather branch (activate.c's machine
     * carries no stack tag): ItemArmor.removeColor takes display.color out
     * and leaves the display compound, then func_150024_a(level - 1), the
     * flag-2 metadata write (updateComparatorOutput reaches no comparator).
     * The branch does not test hasColor, so an undyed piece empties the
     * cauldron too. Level 0 falls through to the machine's false. */
    if (id == 118 && (!p->sneaking || cur->count == 0) && cur->count > 0 &&
        cur->item >= 298 && cur->item <= 301 && world_get_meta(p->e.world, x, y, z) > 0)
    {
        int level = world_get_meta(p->e.world, x, y, z) - 1;

        cur->tag = itag_remove_color(cur->tag);
        world_set_meta(p->e.world, x, y, z, level < 0 ? 0 : level > 3 ? 3 : level, 2);
        return;
    }

    /* BlockFence.onBlockActivated: ItemLead.func_150909_a; when it tied
     * nothing, a held lead's ItemLead.onItemUse runs it again on the fence
     * (render type 11) and answers true, the useItem stat */
    if ((id == 85 || id == 113) && (!p->sneaking || cur->count == 0))
    {
        if (leash_tie_to_fence(p, x, y, z)) return;
        if (cur->count > 0 && cur->item == 420)
        {
            (void)leash_tie_to_fence(p, x, y, z);
            surv_add_stat(p, SURV_STAT_USE(420), 1);
            return;
        }
    }

    int inv_item[40], inv_damage[40], inv_count[40];

    for (int i = 0; i < 40; ++i)
    {
        inv_item[i] = p->sv.inv[i].item;
        inv_damage[i] = p->sv.inv[i].damage;
        inv_count[i] = p->sv.inv[i].count;
    }

    struct act_live_env lin = {surv.world_rand, surv.det, surv.iew,
                               p->sv.food.level, p->sv.food.saturation,
                               cur->count > 0 ? cur->item : 0, cur->damage, cur->count,
                               inv_item, inv_damage, inv_count,
                               0, 0, 0, 0};
    struct act_live_env lout;
    struct act_live_result ar = act_live_block(p->e.world, &lin, &lout, x, y, z, side,
                              hx, hy, hz, p->rotation_yaw, p->sneaking);
    if (lout.held_item != cur->item || lout.held_damage != cur->damage ||
        lout.held_count != cur->count)
    {
        if (lout.held_item != cur->item) cur->tag = 0;   /* a new ItemStack */
        cur->item = lout.held_item; cur->damage = lout.held_damage; cur->count = lout.held_count;
        if (cur->count > 0) cur->gen = ++p->sv.gen_counter;
    }
    if (lout.add_count > 0 && lout.add_slot > 0 && lout.add_slot < 40)
    {
        /* BlockCauldron's bottle: sendContainerToPlayer after the add */
        p->c08_full_sync = 1;
        struct surv_stack *add = &p->sv.inv[lout.add_slot];
        add->item = lout.add_item;
        add->damage = lout.add_damage;
        add->count = lout.add_count;
        add->gen = ++p->sv.gen_counter;
    }
    if (lout.food_level != p->sv.food.level || lout.food_sat != p->sv.food.saturation)
    {
        p->sv.food.level = lout.food_level;
        p->sv.food.saturation = lout.food_sat;
    }
    if (ar.activated)
    {
        if (ar.gui == 1) gui_open_chest(p, ar.x, ar.y, ar.z, 0, 0, 0);
        else if (ar.gui == 2) gui_open_chest(p, ar.ux, ar.y, ar.uz, ar.px, ar.pz, 1);
        else if (ar.gui == 3 && (id == BLK_FURNACE || id == BLK_LIT_FURNACE))
            gui_open_tile(p, CONTAINER_FURNACE, x, y, z, 0, 0, 0);
        else if (ar.gui == 4)
            gui_open_ender(p, x, y, z);
        else if (ar.gui == 3 && (id == BLK_DISPENSER || id == BLK_DROPPER))
            gui_open_tile(p, CONTAINER_DISPENSER, x, y, z, 0, 0, 0);
        else if (ar.gui == 3 && id == BLK_HOPPER)
            gui_open_tile(p, CONTAINER_HOPPER, x, y, z, 0, 0, 0);
        else if (ar.gui == 3 && id == ids_workbench())
        {
            /* the container: the same window id the client's screen uses */
            p->window_id = p->window_id % 100 + 1;

            /* displayGUIWorkbench: a new ContainerWorkbench with the new
             * window id every time (one still open, whose screen the client
             * closed without a C0D, is dropped with its grid: no close) */
            container_free(&p->wb_container);
            container_init(&p->wb_container, CONTAINER_WORKBENCH, 0, 0);
            surv_seed_container(&p->wb_container, p);
            /* SlotCrafting.onCrafting's stat half (the client's copy must
             * not carry it: the stats are the server's truth) */
            p->wb_container.crafted = gui_crafted;
            p->wb_container.crafted_ctx = p;
            p->wb_container.window_id = p->window_id;
            p->open_container = &p->wb_container;
            gui_window_opened(p);

            /* ContainerWorkbench's posX/posY/posZ (canInteractWith) */
            p->gui_x = x; p->gui_y = y; p->gui_z = z;
            p->gui_world = p->e.world;
            p->gui_ender = 0;

            struct s2c_pkt *pkt = s2c_add(s2c_out());
            pkt->kind = PK_S2D;
            pkt->i0 = p->window_id;
            pkt->i1 = CONTAINER_WORKBENCH;
            gui_send_player_slots(p);
        }
        return;
    }

    /* the held ItemBlock's placement: tryPlaceItemIntoWorld's server half
     * (the tape's pickaxe table place). The placement's own Random streams
     * continue the live ones: the world Random's seed rides in, its state
     * comes back; the Math.random stream and the Item.itemRand split are
     * the server role's own (the placement's drops draw from them). */
    if (cur->count == 0) return;

    /* ItemHangingEntity.onItemUse, the item frame and the painting: false
     * on the top and the bottom; else the entity is built facing
     * Direction.facingToDirection[side], hangs when onValidSurface holds
     * (one item spent), and the answer is true either way (the useItem
     * stat) */
    if (cur->item == 389 || cur->item == 321)
    {
        static const int facing_to_dir[6] = {-1, -1, 2, 0, 1, 3};
        int item = cur->item;
        if (side < 2 || side > 5) return;
        if (p->replay != NULL &&
            serverreplay_place_hanging(p->replay, item == 321 ? FH_PAINTING : FH_FRAME, x, y, z, facing_to_dir[side]))
        {
            if (--cur->count <= 0) *cur = empty_stack();
        }
        surv_add_stat(p, SURV_STAT_USE(item), 1);
        return;
    }

    /* ItemBed.onItemUse, ItemSign.onItemUse, ItemSkull.onItemUse,
     * ItemReed.onItemUse (string's tripwire, the reeds, the cake, the pot),
     * ItemRedstone.onItemUse (the dust), ItemDoor.onItemUse and the
     * ItemSeeds and ItemSeedFood plantings are place.c's, like the ItemBlock
     * placements */
    const char *cls = ITEMS[cur->item].class_name;
    int bed_item = cls != NULL && (!strcmp(cls, "ItemBed") || !strcmp(cls, "ItemSign") || !strcmp(cls, "ItemSkull") ||
                                   !strcmp(cls, "ItemReed") || !strcmp(cls, "ItemRedstone") || surv_place_item_class(cls));

    if (ITEMS[cur->item].kind != ITEM_BLOCK && !bed_item)
    {
        if (surv.world_rand)
        {
            struct iu_player ip = {p->e.pos_x, p->e.pos_y, p->e.pos_z,
                                   p->e.y_offset, p->rotation_yaw, p->rotation_pitch, 1, 0};
            struct iu_stack stack = {cur->item, cur->count, cur->damage};
            int ip_item = cur->item;
            /* bone meal on a double plant drops a copy (BlockDoublePlant's
             * func_149853_b): Block.dropBlockAsItem_do on World.rand into the
             * block environment's entity list, as a block pop in this phase */
            struct randomtick_env saved_rt, rt = {surv.world_rand, surv.det, DET_SERVER, blockcb_env_drop_sink, NULL};
            int dye_drop = cur->item == 351 && nw_env->blockcb.env.item_drop != NULL;
            if (dye_drop) { randomtick_tick_save(&saved_rt); randomtick_tick_load(&rt); }
            itemuse_set_live_det(surv.det, DET_SERVER);
            int used = itemuse_activate_block_or_use_item(p->e.world, &ip, &stack,
                                                          x, y, z, side, hx, hy, hz, surv.world_rand);
            itemuse_set_live_det(NULL, DET_SERVER);
            if (dye_drop) randomtick_tick_load(&saved_rt);
            if (stack.item != cur->item || stack.count != cur->count || stack.damage != cur->damage)
            {
                if (stack.item != cur->item) cur->tag = 0;   /* a new ItemStack */
                cur->item = stack.item; cur->count = stack.count; cur->damage = stack.damage;
                cur->gen = ++p->sv.gen_counter;
            }
            /* ItemHoe.onItemUse's and ItemFlintAndSteel.onItemUse's tail:
             * damageItem(1, player); a tool worn out leaves the slot
             * (processPlayerBlockPlacement's stackSize == 0 test) */
            const char *ucls = ITEMS[cur->item].class_name;
            if (used && ucls != NULL && (!strcmp(ucls, "ItemHoe") || !strcmp(ucls, "ItemFlintAndSteel")) &&
                surv_damage_stack(p, cur, 1) && cur->count <= 0)
            {
                *cur = empty_stack();
                cur->gen = ++p->sv.gen_counter;
            }
            /* ItemStack.tryPlaceItemIntoWorld: the useItem counter on success */
            if (used) surv_add_stat(p, SURV_STAT_USE(ip_item), 1);
        }
        return;
    }

    struct place_stack ps = {cur->item, cur->count, cur->damage};
    struct placer pl = {p->e.pos_x, p->e.pos_y, p->e.pos_z, p->rotation_yaw, p->rotation_pitch,
                        p->e.y_offset};
    place_set_case(surv.world_rand ? (int64_t)(surv.world_rand->seed ^ 0x5DEECE66DULL) : 0, surv.det,
                   &surv.det->math[DET_SERVER], NULL);
    /* the placed block's callbacks (a torch that cannot stay pops) draw
     * World.rand, which is the case's own stream until it is handed back */
    jrand *prev_env_wr = nw_env->blockcb.env.world_rand;
    if (surv.world_rand != NULL && prev_env_wr == surv.world_rand) nw_env->blockcb.env.world_rand = place_case_rand();
    int used = place_try(p->e.world, &pl, &ps, x, y, z, side, hx, hy, hz);
    nw_env->blockcb.env.world_rand = prev_env_wr;

    if (surv.world_rand) surv.world_rand->seed = place_world_state();

    /* ItemStack.tryPlaceItemIntoWorld: the useItem counter on success */
    if (used) surv_add_stat(p, SURV_STAT_USE(cur->item), 1);

    if (ps.count != cur->count)
    {
        cur->count = ps.count;

        if (cur->count <= 0) *cur = empty_stack();
    }
}


/* The dig machine's per-tick driver: EntityPlayerMP.onUpdate's
 * updateBlockRemoving head, which runs in the world tick, before the tick's
 * C07 packets are processed (a tick whose C07 ops already updated skips
 * this). The drops the update's break spawns join the entity list. */
/* ItemInWorldManager.setWorld from transferPlayerToDimension: a dig in
 * progress keeps its state and reads the new world from then on. */
void surv_dig_set_world(struct world *w)
{
    if (surv_digenv_ready) surv_digenv.w = w;
}

void surv_server_dig_update(struct server_player *p)
{
    if (!surv_digenv_ready || !surv_dig_active || surv_dig_ticked) return;

    dig_env *d = &surv_digenv;
    surv_dig_pose(d, p);
    if (surv.world_rand) d->wr.seed = surv.world_rand->seed;
    /* the same fork-and-hand-back contract as the C07 processor: a delayed
     * break's draws land here */
    surv_dig_fork = p->sv.erand.r.seed;
    d->p.prand.seed = surv_dig_fork;
    jrand *pop_rand = surv_dig_bind_populate(d);
    dig_op(d, DIG_OP_TICK, d->x, d->y, d->z);
    populate_world_rand = pop_rand;
    dig_hand_back(p, d);
    p->sv.food.exhaustion = d->p.exhaustion;
    if (surv.world_rand) surv.world_rand->seed = d->wr.seed;
    surv_dig_adopt(d, p);
    surv_dig_adopt_stats(d, p);

    /* the break's held-stack damage lands on the server's stack */
    struct surv_stack *cur = &p->sv.inv[p->sv.current_item];

    if (cur->count > 0 && d->p.held_item == cur->item)
    {
        cur->damage = d->p.held_damage;
        cur->count = d->p.held_count;

        if (cur->count <= 0) *cur = empty_stack();
    }
    else if (cur->count > 0 && d->p.held_was == cur->item)
    {
        /* the held stack broke inside the dig machine this tick: vanilla
         * destroyed it in place (EntityPlayer.destroyCurrentEquippedItem) */
        *cur = empty_stack();
        d->p.held_was = 0;
    }
}

/* EntityPlayer.onLivingUpdate's pickup query, after travel. A successful
 * EntityPlayerMP.onItemPickup sends the changed slots immediately. */
void surv_server_living_tail(struct server_player *p)
{
    if (surv.iew != NULL && surv.extra_iew != NULL && surv.iew->peer == surv.extra_iew && surv.extra_iew->w == surv.iew->w)
    {
        collide_with_player_pool(p, surv.iew, 1);
        return;
    }
    if (surv.iew != NULL) collide_with_player_pool(p, surv.iew, 0);
    if (surv.extra_iew != NULL) collide_with_player_pool(p, surv.extra_iew, 0);
}

/* One server tick's survival parts: the world tick's player part, then the
 * packets that arrive before the C03. player.c calls the movement-side hooks
 * from processPlayer's port. */
/* S12PacketEntityVelocity's field: the motion clamped to +-3.9, times 8000,
 * truncated to an int and wrapped to a short by the packet's own encode, then
 * divided back by 8000 on the client. */
static double s12_axis(double v)
{
    if (v < -3.9) v = -3.9;
    if (v > 3.9) v = 3.9;
    return (double)(int16_t)(int)(v * 8000.0) / 8000.0;
}

/* EntityPlayer.getDistanceSq(x + 0.5, y + 0.5, z + 0.5) <= 64.0, the reach
 * every block-backed window's canInteractWith shares. */
static int gui_within_8(const struct server_player *p, int x, int y, int z)
{
    double dx = p->e.pos_x - ((double)x + 0.5);
    double dy = p->e.pos_y - ((double)y + 0.5);
    double dz = p->e.pos_z - ((double)z + 0.5);
    return dx * dx + dy * dy + dz * dz <= 64.0;
}

/* The open window's Container.canInteractWith:
 *
 *   ContainerWorkbench     the block is still a crafting table, within 8
 *   ContainerChest         IInventory.isUseableByPlayer: TileEntityChest's
 *                          (the tile at its position is still this one,
 *                          within 8), both halves' for an
 *                          InventoryLargeChest, InventoryEnderChest's
 *                          (its associatedChest's func_145971_a, the same
 *                          test)
 *   ContainerFurnace, ContainerDispenser, ContainerHopper
 *                          the tile's isUseableByPlayer, the same test
 *   ContainerMerchant      the villager's customer is still this player
 *                          (EntityAITradePlayer's resetTask clears it)
 *   ContainerPlayer        true */
static int gui_can_interact(struct server_player *p)
{
    struct container *c = p->open_container;

    switch (c->kind)
    {
        case CONTAINER_WORKBENCH:
            return (world_get_block(gui_world(p), p->gui_x, p->gui_y, p->gui_z) & 4095) == ids_workbench() &&
                   gui_within_8(p, p->gui_x, p->gui_y, p->gui_z);
        case CONTAINER_CHEST:
        case CONTAINER_FURNACE:
        case CONTAINER_DISPENSER:
        case CONTAINER_HOPPER:
        {
            const struct tile_entity *te = world_tile_entity(gui_world(p), p->gui_x, p->gui_y, p->gui_z);
            if (te == NULL || te != p->gui_te || !gui_within_8(p, p->gui_x, p->gui_y, p->gui_z)) return 0;
            if (c->kind == CONTAINER_CHEST && p->gui_has_pair)
            {
                const struct tile_entity *other =
                    world_tile_entity(gui_world(p), p->gui_pair_x, p->gui_y, p->gui_pair_z);
                return other != NULL && other == p->gui_pair_te &&
                       gui_within_8(p, p->gui_pair_x, p->gui_y, p->gui_pair_z);
            }
            return 1;
        }
        case CONTAINER_MERCHANT:
        {
            const struct living *l = c->merchant_use_ctx;
            return l != NULL && l->buying_player;
        }
        default:
            return 1;
    }
}

/* EntityPlayerMP.onUpdate in the world's entity pass: its head is the dig
 * machine's updateBlockRemoving, then the respawn and hurt timers and the
 * container's detectAndSendChanges. */
void surv_server_world_update(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    if (sv->dead) return;

    /* updateEntityWithOptionalForce's ++ticksExisted before onUpdate */
    ++sv->ticks_existed;

    surv_dig_ticked = 0;
    surv_server_dig_update(p);
    if (sv->respawn_protect > 0) --sv->respawn_protect;
    if (sv->hurt_resistant_time > 0) --sv->hurt_resistant_time;
    gui_tile_detect(p);
    detect_and_send_changes(p);
    gui_furnace_progress(p);

    /* EntityPlayerMP.onUpdate: the open window's canInteractWith; a false
     * answer is closeScreen (the S2E, onContainerClosed) and the inventory
     * container again. */
    surv_server_container_check(p);
}

void surv_server_container_check(struct server_player *p)
{
    if (p->open_container == &p->wb_container && !gui_can_interact(p))
        surv_server_close_screen(p);
}

/* ItemStack.areItemStacksEqual over a click's returned stacks */
static int click_ret_equal(const struct craft_stack *a, const struct craft_stack *b)
{
    int ea = a->item < 0 || a->count <= 0, eb = b->item < 0 || b->count <= 0;

    if (ea || eb) return ea && eb;
    return a->item == b->item && a->count == b->count && a->damage == b->damage && a->tag == b->tag;
}

/* EntityPlayerMP.sendContainerAndContentsToPlayer: the window's S30 (the
 * tile slots, then the player's 36, or the inventory window's 45, sent in
 * the player container's numbering) and the cursor's S2F (window -1). The
 * container's slot copies stay as they were (only detectAndSendChanges
 * moves them), and a tile window's S30 reaches the client only while that
 * window is still its open one (a close in the same tick drops it). */
static void click_resend(struct server_player *p)
{
    struct container *c = p->open_container;
    int tile = c != NULL && c != &p->own_container;

    for (int i = 0; tile && i < gui_tile_count(p); ++i)
        gui_queue_slot(p, i, container_slot(c, i));
    for (int s = tile ? 9 : 0; s < 45; ++s)
    {
        struct surv_stack now = server_slot_stack(p, s);
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S2F;
        pkt->slot = s;
        pkt->pwin = tile ? c->window_id : 0;
        pkt->s30 = 1;
        pkt->i0 = now.item;
        pkt->i1 = now.damage;
        pkt->i2 = now.count;
        pkt->st = now;
    }
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S2F;
    pkt->window = -1;
    pkt->slot = -1;
    pkt->i0 = p->sv.cursor.count > 0 ? p->sv.cursor.item : -1;
    pkt->i1 = p->sv.cursor.count > 0 ? p->sv.cursor.damage : 0;
    pkt->i2 = p->sv.cursor.count > 0 ? p->sv.cursor.count : 0;
    pkt->st = p->sv.cursor;
}

static void close_container(struct server_player *p, int screen);

/* NetHandlerPlayServer.processPlayerBlockPlacement: one C08 and its tail. */
static void surv_server_c08(struct server_player *p, const struct client_pkt *k)
{
    struct surv_state *sv = &p->sv;

    /* processPlayerBlockPlacement's in-air form returns before its tail
     * when the hand is empty (a C02 before it may have used the stack up:
     * the flint and steel worn out on a creeper) */
    if (k->v == 1 && sv->inv[sv->current_item].count <= 0) return;

    if (k->v == 1) process_c08(p, 0, 0, 0, 0, 0, 0.0F, 0.0F, 0.0F);
    else if (k->v >= 2) process_c08(p, 1, k->x, k->y, k->z, k->side, k->hx, k->hy, k->hz);

    /* processPlayerBlockPlacement's tail when the held item has no use
     * duration: detectAndSendChanges under isChangingQuantityOnly, so
     * every changed slot's copy moves without an S2F (a bed blast's
     * armour wear never reaches the client), then the held slot alone
     * when it differs from the stack the packet carried, the client's
     * copy before the placement (after a refused placement the client
     * keeps its predicted count, which only this S2F repairs; a drop the
     * same tick made before this packet never reaches the client). The
     * air use runs the same tail (its own sendContainerToPlayer, when
     * tryUseItem answered true, is process_c08's); the cauldron's bottle
     * keeps the full send, standing for its sendContainerToPlayer. */
    struct surv_stack *held = &sv->inv[sv->current_item];
    int no_duration = surv_item_use_duration(held) == 0;   /* getMaxItemUseDuration: a sword's is 72000 */
    int full_sync = p->c08_full_sync;
    p->c08_full_sync = 0;
    if (no_duration && full_sync)
        detect_and_send_changes(p);
    else if (no_duration)
    {
        int hs = 36 + sv->current_item;
        struct surv_stack claimed = k->stack.count > 0 ? k->stack : empty_stack();
        struct surv_stack now = held->count > 0 ? *held : empty_stack();

        for (int s = 0; s < 45; ++s)
            sv->mirror[s] = server_slot_stack(p, s);

        if (!stack_eq(&now, &claimed)) queue_s2f(p, hs);
    }
}

void surv_server_tick_start(struct server_player *p, const struct client_out *co, const struct act *a)
{
    struct surv_state *sv = &p->sv;

    if (!p->dev_prequeued) s2c_clear();
    p->dev_prequeued = 0;
    /* the network tick: the queue stays open until server_player_tick ends */
    p->net_phase = 1;
    surv_dig_ticked = 0;

    /* the world tick: the player's EntityPlayerMP.onUpdate. The whole-server
     * replay runs it at the player's place in its entity pass
     * (surv_server_world_update); the movement tapes have no pass */
    if (p->replay == NULL) surv_server_world_update(p);

    /* the tracker pass: after the player's onUpdate and its container S2Fs,
     * EntityTrackerEntry answers velocityChanged with an S12 carrying the
     * motion as of now (the movement packets and func_111190_b ran before it) */
    if (sv->velocity_changed && !surv_player_untracked(p))
    {
        struct s2c_pkt *pkt12 = s2c_add(s2c_out());
        pkt12->kind = PK_S12;
        pkt12->f0 = s12_axis(p->e.motion_x);
        pkt12->f1 = s12_axis(p->e.motion_y);
        pkt12->f2 = s12_axis(p->e.motion_z);
        sv->velocity_changed = 0;
    }

    if (surv.iew && !surv.entity_tick_off) ie_tick(surv.iew, 0, NULL, 0, NULL);

    /* onNetworkTick */
    ++sv->net_tick_count;

    if (co->c09_slot >= 0 && co->c09_slot < 9) sv->current_item = co->c09_slot;
    server_player_process_confirms(p, co);

    /* processClientStatus: the gui ops' PERFORM_RESPAWN runs on the C16's
     * arrival; the respawn keeps its health gate and the End exit's
     * conquered respawn waits for the credits' C16 the way master's flow
     * does */
    /* (a respawn after the row's close arrives after its C0D, below) */
    int c16_late = a != NULL && a->gui_respawn_after_close;
    if (co->c16 && !c16_late)
    {
        if (sv->health <= 0.0F) surv_server_fresh(p);
        else if (p->conquered_end && p->replay != NULL) serverreplay_respawn_conquered(p->replay);
    }

    /* the chat screen's packets, sent from its input ahead of the tick's
     * others: processChatMessage and processTabComplete (chatcmd.c) */
    for (int i = 0; co->chat != NULL && i < co->chat->n; ++i)
    {
        if (co->chat->kind[i] == 0) chatcmd_message(p, co->chat->text[i]);
        else chatcmd_tab(p, co->chat->text[i]);
    }
    chatcmd_network_tick(p);

    /* the row's window clicks, NetHandlerPlayServer.processClickWindow's
     * server half: every click, then the S2Fs the changes queue; the C17
     * MC|TrSels (ContainerMerchant.setCurrentRecipeIndex) in their places
     * among them, each ahead of a close at its place */
    int nclose = 0;   /* the row's closes applied (a closed screen's keys click after one) */
    for (int i = 0, refused = 0; a != NULL && i <= a->clicks; ++i)
    {
        struct craft_stack ret;

        for (int j = 0; j < co->ntrsel; ++j)
        {
            if (co->trsel[j].at != i && !(i == a->clicks && co->trsel[j].at > i)) continue;
            struct container *c = p->open_container;

            if (c != NULL && c->kind == CONTAINER_MERCHANT && co->trsel[j].window == c->window_id)
                container_merchant_set_recipe_index(c, co->trsel[j].index);
        }
        if (i == a->clicks) continue;
        for (; nclose < a->closes && a->close_at[nclose] <= i; ++nclose)
        {
            /* the C0D ahead of this click: processCloseWindow; another
             * window's refusal went with it (the inventory container keeps
             * its own player set) */
            if (p->open_container != &p->own_container) refused = 0;
            close_container(p, 0);
        }
        if (refused) continue;

        gui_tile_pull(p);
        int took = surv_server_click(p, a, i, &ret);
        /* the double click's own detectAndSendChanges inside slotClick,
         * before processClickWindow raises isChangingQuantityOnly: every
         * changed slot's S2F, a change the tick made before the click
         * (armour worn by a hit) included */
        if (p->open_container != NULL && p->open_container->click_detect)
        {
            p->open_container->click_detect = 0;
            pickup_detect_and_send_changes(p);
        }
        gui_tile_changed(p);
        gui_tile_record(p);
        /* slotClick's mode 6 (a double click) ends in its own
         * detectAndSendChanges, before processClickWindow sets
         * isChangingQuantityOnly: every changed slot goes out, a change the
         * tick made before the click too */
        if (took && a->gui_click[i].mode == 6 && a->gui_click[i].slot >= 0)
        {
            gui_tile_detect(p);
            detect_and_send_changes(p);
        }
        if (!took) { /* the packet was dropped: nothing answers it */ }
        else if (co->click_sent == NULL || !co->click_sent[i] || click_ret_equal(&co->click_ret[i], &ret))
        {
            /* the stack the packet claimed is the server's: the confirm,
             * then detectAndSendChanges under isChangingQuantityOnly (every
             * changed slot's copy moves, no S2F: a change the tick made
             * before the click, armour worn by a hit, never reaches the
             * client) and updateHeldItem, silent under the same flag */
            for (int s = 0; s < 45; ++s)
                p->sv.mirror[s] = server_slot_stack(p, s);
            /* ContainerFurnace.detectAndSendChanges's progress sends */
            gui_furnace_progress(p);
        }
        else
        {
            /* it is not: the refusal, setPlayerIsPresent(false) (the row's
             * later clicks are refused until the client's C0F), and
             * sendContainerAndContentsToPlayer: the S30 of every slot and
             * the cursor's S2F, and no detectAndSendChanges, so no furnace
             * progress either (a fuel click the tick's burn had outdated
             * sent it a tick early natively; seed-1 S18 row 5150) */
            click_resend(p);
            refused = 1;
        }
    }

    /* the ["close"] op's server half: processCloseWindow (the C0D), whose
     * closeContainer runs onContainerClosed for every window: the base
     * spills the cursor (the player's own window 0 too), then the player
     * container's 2x2 grid and the workbench's 9 are the player's own
     * throws, and openContainer resets to the inventory container */
    int rest = a == NULL ? 0 : a->closes ? a->closes - nclose : a->gui_close;
    if (rest > 0 || co->c0d_close)
    {
        close_container(p, 0);
    }
    /* a second C0D (a close key after the close, the same tick) closes the
     * inventory container again: its spill is empty by then */
    for (int k = 1; k < rest; ++k) close_container(p, 0);
    if (co->c16 && c16_late && sv->health <= 0.0F) surv_server_fresh(p);

    /* the input block's packets in the order its consumers sent them
     * (Minecraft.runTick: the inventory key, the drops, the using branch's
     * stop, the attack presses, the use presses, the held use, the dig
     * tick; a consumer's syncCurrentPlayItem C09 rides just ahead of its
     * own packet) */
    for (int i = 0; i < co->npkt; ++i)
    {
        const struct client_pkt *k = &co->pkt[i];

        switch (k->kind)
        {
            case CPK_C09:
                /* processHeldItemChange */
                if (k->v >= 0 && k->v < 9) sv->current_item = k->v;
                break;
            case CPK_C16:
                /* state 2 OPEN_INVENTORY_ACHIEVEMENT: triggerAchievement */
                if (k->v == 2) surv_add_stat(p, ACH_OPEN_INVENTORY, 1);
                break;
            case CPK_C07:
                process_c07(p, k->v, k->x, k->y, k->z, k->side);
                break;
            case CPK_C02_USE:
                process_c02_interact(p, k->v);
                break;
            case CPK_C02_ATTACK:
                /* the client sends each ATTACK before its C03 move, so the
                 * server checks reach at the previous authoritative
                 * position */
                combat_server_attack(p->replay, k->v);
                break;
            case CPK_C08:
                surv_server_c08(p, k);
                break;
        }
    }
}

/* EntityPlayerMP.closeContainer (the C0D's processCloseWindow; with screen,
 * closeScreen, which sends the S2E first: EntityPlayer.onUpdate's
 * canInteractWith close): the container's onContainerClosed, and
 * openContainer resets to the inventory container. */
static void close_container(struct server_player *p, int screen)
{
    struct container *c = p->open_container;

    if (screen && c != &p->own_container)
    {
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S2E;
        pkt->i0 = c->window_id;
    }

    surv_server_container_closed(p, c);

    if (c != &p->own_container)
    {
        container_free(c);
        p->open_container = &p->own_container;
        surv_seed_container(&p->own_container, p);
        gui_window_closed(p);
    }
}

/* Container.onContainerClosed on the server, per kind: the base spills the
 * cursor, the player container's 2x2 grid and the workbench's 9 are the
 * player's own throws, a chest's lid count drops (in the world the window
 * opened in), a merchant lets its customer go. openContainer stays. */
void surv_server_container_closed(struct server_player *p, struct container *c)
{
    surv_seed_container(c, p);
    container_close(c);
    /* the cursor went with the spill (InventoryPlayer.setItemStack(null)) */
    surv_from_craft(&p->sv.cursor, &c->cursor);

    for (int i = 0; i < c->ndrops; ++i)
        throw_item(p, &(struct surv_stack){.item = c->drops[i].item, .damage = c->drops[i].damage,
                                           .count = c->drops[i].count, .tag = c->drops[i].tag}, 0);

    c->ndrops = 0;

    if (c->kind == CONTAINER_CHEST)
    {
        if (p->gui_ender) gui_close_ender(p);
        else
        {
            struct tile_entity *te = world_tile_entity(gui_world(p), p->gui_x, p->gui_y, p->gui_z);
            struct tile_entity *other = p->gui_has_pair ?
                world_tile_entity(gui_world(p), p->gui_pair_x, p->gui_y, p->gui_pair_z) : NULL;
            if (te) { act_live_chest_close(gui_world(p), te); free(te->raw); te->raw = NULL; }
            if (other) { act_live_chest_close(gui_world(p), other); free(other->raw); other->raw = NULL; }
        }
    }

    if (c->kind == CONTAINER_MERCHANT)
    {
        /* ContainerMerchant.onContainerClosed's setCustomer(null) */
        struct living *l = c->merchant_use_ctx;
        if (l) { l->buying_player = 0; lv_villager(l)->buying_player_name[0] = 0; }
        c->merchant_use_ctx = NULL;
    }
}

/* EntityPlayer.setDead: inventoryContainer.onContainerClosed, then the open
 * window's (the inventory's again when no other is open, with nothing left
 * to spill). Reached from onDeathUpdate at deathTime 20 and from
 * World.removePlayerEntityDangerously (respawnPlayer's old player,
 * transferPlayerToDimension) and World.removeEntity (the End's exit
 * portal). The cursor, InventoryPlayer's one stack, which the open window
 * holds here, goes with the inventory container's close; openContainer
 * stays and no S2E goes out, so a later close closes the window again. */
void surv_server_set_dead(struct server_player *p)
{
    struct container *own = &p->own_container, *c = p->open_container;

    if (c != own)
    {
        craft_stack_free(&own->cursor);
        own->cursor = c->cursor;
        c->cursor = (struct craft_stack){-1, 0, 0, 0};
    }
    surv_server_container_closed(p, own);
    if (c != own) surv_server_container_closed(p, c);
}

void surv_server_close_screen(struct server_player *p)
{
    close_container(p, 1);
}

/* EntityPlayer.jump's exhaustion. Java's jump() runs for a dead player too
 * (isDead stays false until the deathTime 20 setDead), no health gate. */
void surv_server_jump(struct server_player *p)
{
    struct surv_state *sv = &p->sv;

    /* EntityPlayer.jump's jumpStat rides the exhaustion */
    surv_add_stat(p, STAT_JUMP, 1);
    add_exhaustion(sv, p->sprinting && !surv.no_sprint_exhaustion ? 0.8F : 0.2F);

    trace("jump", "i", 0);
}

/* EntityPlayer.addMovementStat after the packet move: the walked/swum/dove/
 * climbed/flown counters ride the same branches the exhaustion draws sit in
 * (climbed only when dy > 0, flown only past 25 hundredths). */
void surv_server_move_stat(struct server_player *p, double dx, double dy, double dz)
{
    struct entity *e = &p->e;
    struct surv_state *sv = &p->sv;
    int var7;

    if (inside_of_material(e->world, e, 1.62F, MAT_WATER))
    {
        var7 = round_float(sqrt_double(dx * dx + dy * dy + dz * dz) * 100.0F);

        if (var7 > 0)
        {
            surv_add_stat(p, STAT_DISTANCE_DOVE, var7);
            add_exhaustion(sv, 0.015F * (float)var7 * 0.01F);
        }
    }
    else if (e->in_water)
    {
        var7 = round_float(sqrt_double(dx * dx + dz * dz) * 100.0F);

        if (var7 > 0)
        {
            surv_add_stat(p, STAT_DISTANCE_SWUM, var7);
            add_exhaustion(sv, 0.015F * (float)var7 * 0.01F);
        }
    }
    else if (surv_on_ladder(e->world, e->pos_x, e->pos_z, e->bounding_box))
    {
        /* climbed: only while moving up */
        if (dy > 0.0) surv_add_stat(p, STAT_DISTANCE_CLIMBED,
                                    (int)(long long)floor(dy * 100.0 + 0.5));
    }
    else if (e->on_ground)
    {
        var7 = round_float(sqrt_double(dx * dx + dz * dz) * 100.0F);

        if (var7 > 0)
        {
            surv_add_stat(p, STAT_DISTANCE_WALKED, var7);
            if (p->sprinting && !surv.no_sprint_exhaustion)
                add_exhaustion(sv, 0.099999994F * (float)var7 * 0.01F);
            else
                add_exhaustion(sv, 0.01F * (float)var7 * 0.01F);
        }
    }
    else
    {
        /* the horizontal distance, as the swim and walk branches */
        var7 = round_float(sqrt_double(dx * dx + dz * dz) * 100.0F);

        if (var7 > 25) surv_add_stat(p, STAT_DISTANCE_FLOWN, var7);
    }

    {
        int inside_water = inside_of_material(e->world, e, 1.62F, MAT_WATER);
        int branch = inside_water ? 2 : e->in_water ? 3 : surv_on_ladder(e->world, e->pos_x, e->pos_z, e->bounding_box) ? 4
            : e->on_ground ? 5 : 6;
        trace("mstat", "iiddd", branch, 0, dx, dy, dz);
    }
}

/* Entity.moveEntity's random.fizz, a burning player moving wet: the
 * pitch's two floats on the player's own Random (EntityPlayer.playSound
 * sends it to no other player). */
static void sp_fizz(void *self)
{
    struct server_player *p = self;

    (void)det_rng_float(&p->sv.erand);
    (void)det_rng_float(&p->sv.erand);
}

/* Entity.moveEntity's in-water step: playSound(getSwimSound(), ...)'s pitch,
 * two floats on the player's own Random. */
static void sp_swim_sound(void *self)
{
    struct server_player *p = self;

    (void)det_rng_float(&p->sv.erand);
    (void)det_rng_float(&p->sv.erand);
}

/* The attack_from callback Entity.moveEntity reaches: the cactus cell and the
 * standing-in-fire block. */
static void sp_attack_from(void *self, int source, float amount)
{
    struct server_player *p = self;

    surv_server_damage(p, source == ENTITY_SRC_CACTUS ? SURV_CACTUS : SURV_IN_FIRE, amount);
}

/* Block.onEntityWalking at the walking step's base cell: the redstone ore's
 * func_150185_e, 18 world-Random floats for the six particle rows and, on
 * the unlit ore, the light-up write (both ore blocks are BlockRedstoneOre;
 * it draws from World.rand, not the entity's).
 * Every other block's body is empty. */
static void sp_walking_block(void *self, int x, int y, int z, int id)
{
    struct server_player *p = self;

    /* the lit ore (74) is the same class: it draws, only 73 is rewritten */
    if ((id != 73 && id != 74) || surv.world_rand == NULL) return;

    /* the draws advance the world Random and stay (the dig machine rides the
     * same stream: the caller copies the state back after each op) */
    for (int i = 0; i < 6; ++i)
    {
        jr_float(surv.world_rand);
        jr_float(surv.world_rand);
        jr_float(surv.world_rand);
    }

    if (id == 73) world_set_block(p->e.world, x, y, z, 74, 0, 3);
}

/* ------------------------------------------------- the block dig and use -
 *
 * The wooden-pickaxe tapes' client half: Minecraft.runTick's mouseover
 * refresh ("pick" section), the attack and use consumers over it, and the
 * PlayerControllerMP dig state machine; and the server half: the C07
 * statuses through dig.c's drive ops and the C08's activation and
 * placement, with the spawned drops adopted onto the replay's entity list.
 *
 * The dig state machine's truth is the server's (driven by the C07s the
 * client queues); the client half computes what those packets carry. The
 * server's held stack and pose the machine reads are the server player's
 * own. */
/* --------------------------------------------------------- client survival */

/* EntityPlayerSP.setPlayerSPHealth on the S06. */
static void client_set_sp_health(struct client_player *p, float v)
{
    struct surv_state *sv = &p->sv;

    if (!sv->has_set_health)
    {
        sv->health = v;
        sv->has_set_health = 1;
        return;
    }

    float var2 = sv->health - v;

    if (var2 <= 0.0F)
    {
        sv->health = v;

        if (var2 < 0.0F) sv->hurt_resistant_time = sv->max_hurt_resistant / 2;
    }
    else
    {
        sv->last_damage = var2;
        sv->hurt_resistant_time = sv->max_hurt_resistant;
        /* the client player's damageEntity is a plain health subtraction */
        sv->health -= var2;
        sv->hurt_time = sv->max_hurt_time = 10;
    }
}

/* EntityLivingBase.handleHealthUpdate(2) on the client player: the hurt
 * clocks, attackedAtYaw 0, the hurt sound's pitch (two floats of the player's
 * own Random), then attackEntityFrom, which EntityClientPlayerMP refuses. */
static void client_hurt_status(struct client_player *p)
{
    struct surv_state *sv = &p->sv;

    p->limb_swing_amount = 1.5F;
    sv->hurt_resistant_time = sv->max_hurt_resistant;
    sv->hurt_time = sv->max_hurt_time = 10;
    (void)det_rng_float(&sv->erand);
    (void)det_rng_float(&sv->erand);
}

/* EntityPlayer.updateItemUse(stack, n) on the client player: a drink
 * (EnumAction.drink: potions, milk) pitches its sound on WorldClient.rand;
 * food (EnumAction.eat) throws n crumbs, each a Math.random and an
 * iconcrack FX at the mouth (the player's own Random places it; the FX
 * builds at the particle setting), then the eat sound on its own Random */
static void client_update_item_use(struct client_player *p, const struct surv_stack *cur, int n)
{
    if (cur->item == IT_MILK || cur->item == IT_POTION)
    {
        if (p->cw_rand_known) (void)det_rng_float(&p->cw_rand);
        return;
    }
    if (ITEMS[cur->item].heal_amount == 0) return;

    char name[40];
    if (ITEMS[cur->item].has_subtypes) snprintf(name, sizeof name, "iconcrack_%d_%d", cur->item, cur->damage);
    else snprintf(name, sizeof name, "iconcrack_%d", cur->item);
    for (int i = 0; i < n; ++i)
    {
        double vy = det_math_random_role(surv.det, DET_CLIENT) * 0.1 + 0.1;
        surv_client_fx_spawn(p, name, p->e.pos_x, p->e.pos_y, p->e.pos_z, 0.0, vy + 0.05, 0.0);
    }
}

/* The eat finish on the S19's handleHealthUpdate(9). */
static void client_on_item_use_finish(struct client_player *p)
{
    struct surv_state *sv = &p->sv;

    if (sv->using_slot < 0) return;

    struct surv_stack *cur = &sv->inv[sv->current_item];
    /* a sword or a bow at the end of its 72000 ticks: updateItemUse(16)
     * does nothing for EnumAction.block and bow, and Item.onEaten /
     * ItemBow.onEaten return the stack as it was */
    if (surv_item_use_duration(cur) == 72000)
    {
        sv->using_slot = -1;
        sv->using_gen = 0;
        sv->using_count = 0;
        return;
    }
    client_update_item_use(p, cur, 16);
    int before = cur->count;
    int item = cur->item;

    cur->count -= 1;

    if (item == IT_MILK || item == IT_POTION)
    {
        /* the client's onEaten: no effects (isClient), the bucket or the
         * glass bottle all the same */
        struct surv_stack empty = {.item = item == IT_MILK ? IT_BUCKET : IT_GLASS_BOTTLE, .count = 1};
        if (cur->count <= 0)
        {
            *cur = empty;
            cur->gen = ++sv->gen_counter;
        }
        else if (item == IT_POTION)
        {
            (void)add_item_stack_to_inventory(sv, &empty);
        }
        sv->using_slot = -1;
        sv->using_gen = 0;
        sv->using_count = 0;
        return;
    }

    const struct item_def *d = &ITEMS[cur->item];
    food_add_stats(sv, d->heal_amount, d->saturation);
    /* ItemFood.onEaten's burp, pitched on WorldClient.rand */
    if (p->cw_rand_known) (void)det_rng_float(&p->cw_rand);

    if (item == IT_STEW)
    {
        *cur = empty_stack();
        cur->item = IT_BOWL;
        cur->count = 1;
        cur->gen = ++sv->gen_counter;
    }
    else if (cur->count != before && cur->count == 0)
    {
        *cur = empty_stack();
        cur->gen = ++sv->gen_counter;
    }

    sv->using_slot = -1;
    sv->using_gen = 0;
    sv->using_count = 0;
}

/* Potion.performEffect, the client player's half of PotionEffect.onUpdate.
 * The client world is isClient, so the saturation branch is skipped and the
 * EntityClientPlayerMP overrides heal with an empty body
 * (EntityClientPlayerMP.java:75), so no potion ever moves the client's own
 * health mirror: the regen heals only through the S06 the server sends after
 * its own tick. Poison and wither draw nothing and change nothing either
 * (the client world's attackEntityFrom returns false at the isClient gate). */
static void client_potion_perform(struct client_player *p, int id, int amplifier)
{
    struct surv_state *sv = &p->sv;

    if (id == POT_HUNGER)
    {
        add_exhaustion(sv, 0.025F * (float)(amplifier + 1));
    }
    (void)id;
}

static void client_container_closed(struct client_player *p, struct container *c);

/* EntityLivingBase.onEntityUpdate's survival half for the client player. */
void surv_client_base_tick(struct client_player *p)
{
    struct surv_state *sv = &p->sv;

    if (sv->attack_time > 0) --sv->attack_time;

    if (sv->hurt_time > 0) --sv->hurt_time;
    /* the client player is not an EntityPlayerMP: its hurtResistantTime ticks
     * in the base tick */
    if (sv->hurt_resistant_time > 0) --sv->hurt_resistant_time;

    if (sv->health <= 0.0F && !sv->dead)
    {
        ++sv->death_time;

        if (sv->death_time == 20)
        {
            sv->dead = 1;
            p->is_dead = 1;
            sv->using_slot = -1;
            /* EntityPlayer.setDead on the client too: inventoryContainer's
             * onContainerClosed (the cursor, whichever window holds it, then
             * the 2x2 grid, each a client drop), then openContainer's, which
             * has nothing left of the player's */
            struct container *own = &p->own_container;
            if (p->open_container != own && p->open_container->cursor.count > 0)
            {
                craft_stack_free(&own->cursor);
                own->cursor = p->open_container->cursor;
                p->open_container->cursor = (struct craft_stack){-1, 0, 0, 0};
            }
            client_container_closed(p, own);
        }
    }

    if (sv->recently_hit > 0) --sv->recently_hit;

    /* EntityLivingBase.updatePotionEffects, client side: the world is isClient,
     * so the expiry remove, the data-watcher half and the 600-tick changed
     * hook are all skipped, but PotionEffect.onUpdate (the performEffect plus
     * the duration decrement) and the mobSpell particle draw still run. The
     * player's rand is sv.erand; the S1D/S1E pump keeps the mirror. The
     * harness delivers each row's s2c one row late, so the S1D the pump just
     * applied (potion_fresh) first runs its PotionEffect.onUpdate on the
     * NEXT row: the client's decrement stream trails the server's by one,
     * which is exactly what the tapes show. */
    uint8_t pids[POT_COUNT];
    int npids = potion_map_order(&sv->potions, pids);
    for (int i = 0; i < npids; ++i)
    {
        struct potion_effect *node = &sv->potions.eff[pids[i]];

        if (!sv->potion_fresh && node->duration > 0)
        {
            client_potion_perform(p, node->id, node->amplifier);
            --node->duration;
        }
    }

    int color = sv->potion_liquid_color;
    int is_ambient = sv->potion_is_ambient;

    /* the watcher's setPotionEffects half ran at the pump, before this tick:
     * the color and the ambient flag already cover the fresh S1D */
    if (color > 0)
    {
        int draw = 0;

        /* the client player's own isInvisible(): no client-side setInvisible
         * ever runs on the player (the watcher half is server-only) and no
         * tape here carries invisibility, so it is false */
        draw = det_rng_bool(&sv->erand);

        if (is_ambient) draw = draw && (int)det_rng_int_n(&sv->erand, 5) == 0;

        if (draw)
        {
            /* the mobSpell (ambient: mobSpellAmbient) particle around the
             * player's box, its colour the watcher's: close to the camera in
             * first person, drawn when the particle setting lets it */
            double r = (double)(color >> 16 & 255) / 255.0;
            double g = (double)(color >> 8 & 255) / 255.0;
            double b = (double)(color >> 0 & 255) / 255.0;
            double x = p->e.pos_x + (det_rng_double(&sv->erand) - 0.5) * (double)p->e.width;
            double y = p->e.pos_y + det_rng_double(&sv->erand) * (double)p->e.height - (double)p->e.y_offset;
            double z = p->e.pos_z + (det_rng_double(&sv->erand) - 0.5) * (double)p->e.width;
            surv_client_fx_spawn(p, is_ambient ? "mobSpellAmbient" : "mobSpell", x, y, z, r, g, b);
        }
    }

    sv->potion_fresh = 0;
}

/* EntityPlayer.onUpdate's itemInUse countdown plus xpCooldown, client side. */
void surv_client_eat_tick(struct client_player *p)
{
    struct surv_state *sv = &p->sv;

    if (sv->xp_cooldown > 0) --sv->xp_cooldown;

    if (sv->using_slot < 0) return;

    struct surv_stack *cur = &sv->inv[sv->current_item];

    if (cur->count > 0 && sv->current_item == sv->using_slot && cur->gen == sv->using_gen)
    {
        /* updateItemUse(5) on every fourth count: sounds and particles, and
         * for food (EnumAction.eat) each particle's Math.random */
        if (sv->using_count <= 25 && sv->using_count % 4 == 0) client_update_item_use(p, cur, 5);
        --sv->using_count;
    }
    else
    {
        sv->using_slot = -1;
        sv->using_gen = 0;
        sv->using_count = 0;
    }
}

/* Minecraft.setDimensionAndSpawnPlayer: the fresh EntityClientPlayerMP. */
/* EntityPlayer.func_146097_a on the client player (a click's drop, a
 * container's close-time spill): the EntityItem is built on the client's
 * streams and EntityClientPlayerMP.joinEntityItemWithWorld drops it, and the
 * client's addStat is EntityPlayer's no-op, so what is left are the draws: the
 * Entity constructor's id, Random and UUID, EntityItem's four Math.random
 * draws, then the look-throw's four on the player's own Random. An empty
 * stack returns null first. */
static void client_drop_draws(struct client_player *p, int count)
{
    if (count <= 0) return;
    (void)det_next_entity_id_role(surv.det, DET_CLIENT);
    (void)det_new_random_role(surv.det, DET_CLIENT);
    {
        int64_t msb = 0, lsb = 0;
        det_uuid_role(surv.det, DET_CLIENT, &msb, &lsb);
    }
    for (int i = 0; i < 4; ++i) (void)det_math_random_role(surv.det, DET_CLIENT);
    for (int i = 0; i < 4; ++i) (void)det_rng_float(&p->sv.erand);
}

/* GuiContainer.onGuiClosed on the client: its container's onContainerClosed
 * with the client player. Container's base drops the cursor's stack (the
 * closeScreen path has emptied it already) and ContainerPlayer drops its 2x2
 * grid and clears the result; the workbench's and merchant's spills are
 * server-only (!isClient). */
static void client_container_closed(struct client_player *p, struct container *c)
{
    if (c == NULL) return;
    container_close_client(c);
    for (int d = 0; d < c->ndrops; ++d) client_drop_draws(p, c->drops[d].count);
    for (int d = 0; d < c->ndrops; ++d) craft_stack_free(&c->drops[d]);
    c->ndrops = 0;
    p->sv.cursor.item = 0;
    p->sv.cursor.damage = 0;
    p->sv.cursor.count = 0;
    p->sv.cursor.tag = 0;
}

/* EntityPlayerSP.onLivingUpdate: in a portal, mc.displayGuiScreen(null) with
 * a screen up. Not closeScreen: no C0D, and openContainer stays as it was
 * (the server keeps its window open too); onGuiClosed spills the cursor on
 * the client; setIngameFocus takes the focus back (leftClickCounter 10000). */
void surv_client_portal_close(struct client_player *p)
{
    if (p->screen_chat)
    {
        /* GuiChat.onGuiClosed resets the chat's scroll, nothing else */
        p->screen_chat = 0;
        p->in_game_has_focus = 1;
        p->left_click_counter = 10000;
        return;
    }
    if (!p->screen_inventory) return;
    client_container_closed(p, cp_gui_container(p));
    p->screen_inventory = 0;
    p->gui_own = 0;
    if (!p->in_game_has_focus)
    {
        p->in_game_has_focus = 1;
        p->left_click_counter = 10000;
    }
}

void surv_client_fresh(struct client_player *p)
{
    struct surv_state *sv = &p->sv;

    /* keepInventory: the client's inventory mirror survives the respawn (the
     * kept stacks; the server's S2F re-sync lands in the same tick pair). */
    struct surv_clone *keepc ENV_LOCAL = envstack_take(sizeof *keepc);
    int keep_inv = surv.keep_inventory;

    if (keep_inv) surv_clone_take(keepc, sv);

    /* a fresh EntityClientPlayerMP: no portal time, and a new
     * InventoryPlayer whose currentItem is 0 (a dimension transfer's S09
     * restores the server's slot in the same pump; a respawn sends none) */
    memset(&p->portal, 0, sizeof p->portal);
    p->hotbar = 0;

    /* the fresh entity constructor's Det draws, CLIENT role */
    (void)det_next_entity_id_role(surv.det, DET_CLIENT);
    sv->erand = det_new_random_role(surv.det, DET_CLIENT);
    {
        int64_t msb = 0, lsb = 0;
        det_uuid_role(surv.det, DET_CLIENT, &msb, &lsb);
    }
    /* the EntityLivingBase constructor's three Math draws */
    det_math_random_role(surv.det, DET_CLIENT);
    det_math_random_role(surv.det, DET_CLIENT);
    det_math_random_role(surv.det, DET_CLIENT);

    int has_spawn = sv->has_spawn;
    int sx = sv->spawn_x, sy = sv->spawn_y, sz = sv->spawn_z, sf = sv->spawn_forced;

    /* the StatFileWriter passes to the fresh player (func_147493_a is given
     * the old player's func_146107_m), and field_147308_k, the settings and
     * the GuiAchievement are not the player's */
    surv_state_clear_but_stats(sv);
    sv->food.level = 20;
    sv->food.saturation = 5.0F;
    sv->health = 20.0F;
    sv->air = 300;
    sv->hud_prev_food = 20;
    sv->max_hurt_resistant = 20;
    sv->max_hurt_time = 10;
    sv->using_slot = -1;
    potion_map_init(&sv->potions);

    sv->has_spawn = has_spawn;
    sv->spawn_x = sx;
    sv->spawn_y = sy;
    sv->spawn_z = sz;
    sv->spawn_forced = sf;

    if (keep_inv)
    {
        for (int i = 0; i < 40; ++i) sv->inv[i] = keepc->inv[i];
    }

    p->e.width = 0.6F;
    p->e.height = 1.8F;
    p->e.step_height = 0.5F;
    p->e.y_offset = 1.62F;
    p->e.motion_x = 0.0;
    p->e.motion_y = 0.0;
    p->e.motion_z = 0.0;
    p->e.fall_distance = 0.0F;
    p->e.fire = 0;
    p->e.y_size = 0.0F;
    p->e.first_update = 1;
    p->e.on_ground = 0;
    p->e.in_water = 0;
    p->e.is_collided_horizontally = 0;
    p->e.is_collided_vertically = 0;

    p->ticks_existed = 0;
    p->sprinting = 0;
    p->sprinting_ticks_left = 0;
    p->sprint_toggle_timer = 0;
    p->fly_toggle_timer = 0;
    p->in_strafe = 0.0F;
    p->in_forward = 0.0F;
    p->in_jump = 0;
    p->in_sneak = 0;
    p->is_jumping = 0;
    p->jump_ticks = 0;
    p->jump_movement_factor = 0.02F;
    p->old_pos_x = 0.0;
    p->old_min_y = 0.0;
    p->old_pos_y = 0.0;
    p->old_pos_z = 0.0;
    p->old_rotation_yaw = 0.0F;
    p->old_rotation_pitch = 0.0F;
    p->was_on_ground = 0;
    p->should_stop_sneaking = 0;
    p->was_sneaking = 0;
    p->ticks_since_move_packet = 0;
    p->move_speed = 0.10000000149011612;
    /* the new EntityClientPlayerMP's attribute map: no modifiers until an S20 */
    p->mod_sprint = 0;
    p->mod_slow = p->mod_speed = -1;
    p->screen_gameover = 0;   /* setDimensionAndSpawnPlayer closes it */
    p->in_game_has_focus = 1;
    p->is_dead = 0;           /* a fresh player, alive again */
    p->riding_id = 0;         /* and on no vehicle (a dead rider's stays with the old one) */
    p->first_client_tick = 2; /* the fresh player's first tick sends no living update and no C03 */

    /* setDimensionAndSpawnPlayer keeps the WorldClient and its chunks on a
     * same-dimension respawn (only removeAllEntities + the fresh player), so
     * the respawn skips the client's first living update: the oracle's
     * tick-608 block holds no client move (the fresh player's first tick
     * contributes nothing), so one join-style skip reproduces the rows */

    /* preparePlayerToSpawn at the spawn point: the position is immediately
     * replaced by the S08 that rides in the same queue */
    entity_set_position(&p->e, p->e.pos_x, p->e.pos_y, p->e.pos_z);
}

/* The s2c pump: updateController's processReceivedPackets, at the head of the
 * client tick. */
void surv_client_pump(struct client_player *p)
{
    const struct s2c_queue *q = s2c_in();

    for (int i = 0; q != NULL && i < q->n; ++i)
    {
        const struct s2c_pkt *k = &q->q[i];

        switch (k->kind)
        {
            case PK_S06:
                client_set_sp_health(p, k->f0);
                p->sv.food.level = k->i0;
                p->sv.food.saturation = k->f1;
                break;

            case PK_S1C:
                /* handleEntityMetadata's index 6: the watched health
                 * itself (setPlayerSPHealth is the S06's; the S06 after it
                 * then finds no difference) */
                p->sv.health = k->f0;
                break;

            case PK_S1C_FLAGS:
                /* handleEntityMetadata's index 0 on the player: its
                 * isBurning (the client's own fire is zeroed each tick) and
                 * isSprinting, the flag alone (the speed modifier is the
                 * S20's; a held sprint key sets it again, setSprinting(true)
                 * with its modifier, and sendMotionUpdates' wasSprinting
                 * never saw the flag drop) */
                p->sv.fire = k->i0 & 1;
                p->sprinting = (k->i0 >> 3) & 1;
                break;

            case PK_S1F:
                /* handleSetExperience: EntityPlayerSP.setXPStats, the
                 * client's own experience, total and level (the HUD's bar
                 * and number read them; no row compares them) */
                p->sv.xp_progress = k->f0;
                p->sv.xp_total = k->i0;
                p->sv.xp_level = k->i1;
                break;

            case PK_S19:
                /* handleEntityStatus -> handleHealthUpdate: 2 the hurt, 9
                 * the item use's finish */
                if (k->i1 == 2) client_hurt_status(p);
                else client_on_item_use_finish(p);
                break;

            case PK_S1D:
                /* NetHandlerPlayClient.handleEntityEffect: new
                 * PotionEffect(id, duration, amplifier) +
                 * setPotionDurationMax + addPotionEffect. The client world is
                 * isClient, so no attribute half runs; onNewPotionEffect /
                 * onChangedPotionEffect still set potionsNeedUpdate (the
                 * vanilla client does run those hooks' common half), and
                 * setPotionDurationMax zeroes isPotionDurationMax only when
                 * the duration is not the 32767 max (no row reads it). */
            {
                struct potion_effect eff;
                memset(&eff, 0, sizeof eff);
                eff.id = (uint8_t)k->i0;
                eff.amplifier = (int8_t)k->i1;
                eff.duration = k->i2;
                eff.is_splash = 0;
                eff.is_ambient = 0;
                eff.duration_max = k->i2 == 32767;
                (void)potion_map_put(&p->sv.potions, &eff);
                p->sv.potions_need_update = 1;
                p->sv.potion_fresh = 1;
            }
            break;

            case PK_S1E:
                /* handleRemoveEntityEffect -> removePotionEffectClient: a
                 * plain map remove, no hooks, no attributes */
                potion_map_remove(&p->sv.potions, k->i0, NULL);
                break;

            case PK_S2F:
            {
                if (k->window == -1)
                {
                    /* handleSetSlot's window -1: inventory.setItemStack */
                    struct surv_stack cur = {0};
                    if (k->i2 > 0) { cur.item = k->i0; cur.damage = k->i1; cur.count = k->i2; cur.tag = k->st.tag; }
                    p->sv.cursor = cur;
                    if (p->open_container != NULL)
                    {
                        craft_stack_free(&p->open_container->cursor);
                        p->open_container->cursor = k->i2 > 0 ? craft_from_surv(&cur) : (struct craft_stack){-1, 0, 0, 0};
                    }
                    break;
                }
                if (k->window > 0)
                {
                    struct container *c = p->open_container;
                    if (c && c->window_id == k->window)
                    {
                        struct craft_stack cs = {k->i2 > 0 ? k->i0 : -1, k->i2, k->i1, k->i2 > 0 ? k->st.tag : 0};
                        if (container_kind_tile_inv(c->kind) && k->slot < c->chest_size)
                            container_set_chest(c, k->slot, &cs);
                        else if (c->kind == CONTAINER_FURNACE && k->slot < 3)
                            container_set_furnace(c, k->slot, &cs);
                    }
                    break;
                }
                /* detect_and_send_changes records player-container slot
                 * numbers for the inventory mirror, even while another
                 * window is open; a tile window's S30 part needs its window */
                if (k->pwin > 0 && (p->open_container == NULL || p->open_container->window_id != k->pwin)) break;
                /* handleSetSlot's window 0 under another window: only the
                 * hotbar's 36..44 (inventoryContainer.putStackInSlot) */
                if (k->pwin == 0 && p->open_container != NULL && p->open_container != &p->own_container &&
                    !(k->slot >= 36 && k->slot < 45)) break;
                int i2 = container_slot_inventory(k->slot);

                /* the 2x2 grid's slots, into the inventory container when
                 * it is the open one (handleSetSlot's window 0 case) */
                if (k->slot >= 1 && k->slot <= 4 && p->open_container == &p->own_container)
                {
                    struct craft_stack cs = {k->i2 > 0 ? k->i0 : -1, k->i2, k->i2 > 0 ? k->i1 : 0,
                                             k->i2 > 0 ? k->st.tag : 0};
                    container_set_grid(&p->own_container, k->slot - 1, &cs);
                    break;
                }

                if (i2 >= 0)
                {
                    /* handleSetSlot: a hotbar slot whose count the packet
                     * raises pops (animationsToGo 5); the packet's stack is
                     * a new object, so any other slot's count is gone */
                    if (i2 < 36)
                        p->inv_anim[i2] = !k->s30 && k->slot >= 36 && k->slot < 45 && k->i2 > 0 &&
                                          (p->sv.inv[i2].count <= 0 || p->sv.inv[i2].count < k->i2) ? 5 : 0;
                    p->sv.inv[i2].item = k->i0;
                    p->sv.inv[i2].damage = k->i1;
                    p->sv.inv[i2].count = k->i2;
                    /* the packet's stack carries its tag */
                    p->sv.inv[i2].tag = k->st.tag;
                    p->sv.inv[i2].gen = ++p->sv.gen_counter;
                }

                break;
            }

            case PK_S07:
                /* handleRespawn: another dimension gets a new, empty
                 * WorldClient (doneLoadingTerrain off until the S08), then
                 * setDimensionAndSpawnPlayer's fresh player */
                if (k->i0 != p->dimension)
                {
                    p->dimension = k->i0;
                    if (p->on_world_change) p->on_world_change(p->world_change_ctx, k->i0);
                    /* new WorldClient: World's updateLCG (a Det.newRandom's nextInt)
                     * and its rand, two seeder longs */
                    client_player_new_world_rand(p);
                    /* its GuiDownloadTerrain replaces the screen up (a GuiChat,
                     * an inventory or a container's: onGuiClosed with the old
                     * player, no C0D), and the S08's displayGuiScreen(null)
                     * takes the focus back */
                    surv_client_portal_close(p);
                    /* the fresh player spawns into an empty WorldClient: its
                     * spawnEntityInWorld adds it to the blank chunk, which
                     * leaves addedToChunk off, so its first tick is skipped
                     * like any fresh player's; the tail of that tick sets
                     * it (ChunkProviderClient.chunkExists is always true),
                     * and it updates every tick after, chunk or not */
                    surv_client_fresh(p);
                }
                else surv_client_fresh(p);
                break;

            case PK_S30:
                /* handleWindowItems for window 0: every slot of the player
                 * container */
                memset(p->inv_anim, 0, sizeof p->inv_anim);
                for (int i2 = 0; i2 < 40; ++i2)
                {
                    p->sv.inv[i2] = ((const struct surv_stack *)s2c_side(q, k))[i2];
                    p->sv.inv[i2].gen = ++p->sv.gen_counter;
                }
                /* putStacksInSlots reaches the 2x2 grid too (a grid the
                 * client dropped at a portal's close, the server's still
                 * full), each set its onCraftMatrixChanged */
                for (int g = 0; g < 4; ++g)
                {
                    struct craft_stack cs = craft_from_surv(&((const struct surv_stack *)s2c_side(q, k))[40 + g]);
                    container_set_grid(&p->own_container, g, &cs);
                }
                break;

            case PK_S12:
                /* NetHandlerPlayClient.handleEntityVelocity: setVelocity */
                p->e.motion_x = k->f0;
                p->e.motion_y = k->f1;
                p->e.motion_z = k->f2;
                break;

            case PK_S24:
                /* handleBlockAction: World.addBlockEvent, the block's
                 * onBlockEventReceived: a chest's getTileEntity takes
                 * numPlayersUsing (receiveClientEvent(1, n)) */
                if (p->pickobj != NULL && (k->i0 == 54 || k->i0 == 146 || k->i0 == 130) && k->i1 == 1)
                {
                    int bx = (int)k->f0, by = (int)k->f1, bz = (int)k->f2;
                    struct client_te *e = client_te_get(&p->pickobj->ctes, surv.det, bx, by, bz,
                                                        world_get_block(p->e.world, bx, by, bz));
                    if (e != NULL && e->id == k->i0) e->players = k->i2;
                }
                /* BlockNote.onBlockEventReceived: the sound's pitch is
                 * Math.pow, no draw; the note particle through the
                 * particle setting's gate */
                if (k->i0 == 25 && surv.det)
                    (void)particles_live_spawn(surv_client_fx(p), "note", (double)(int)k->f0 + 0.5,
                                               (double)(int)k->f1 + 1.2, (double)(int)k->f2 + 0.5,
                                               (double)k->i2 / 24.0, 0.0, 0.0);
                break;

            case PK_S28:
                /* handleEffect: RenderGlobal.playAuxSFX (particles_live_aux_sfx) */
                if (surv.det)
                    particles_live_aux_sfx(surv_client_fx(p), k->i0, (int)k->f0, (int)k->f1, (int)k->f2, k->i1);
                break;

            case PK_S27:
                /* handleExplosion: the client's Explosion and its
                 * doExplosionB(true), then the knockback */
                {
                    const struct s2c_s27 *x = s2c_side(q, k);
                    if (x != NULL)
                    {
                        surv_fx_ensure(p);
                        particles_live_explosion_packet(&surv_fx, x->x, x->y, x->z, x->size, x->naffected, x->xp,
                                                        x->nxp);
                    }
                }
                p->e.motion_x += k->f0;
                p->e.motion_y += k->f1;
                p->e.motion_z += k->f2;
                break;

            case PK_S08:
                /* NetHandlerPlayClient.handlePlayerPosLook */
                p->e.y_size = 0.0F;
                p->e.motion_x = 0.0;
                p->e.motion_y = 0.0;
                p->e.motion_z = 0.0;
                entity_set_pos_rot(&p->e, (double)k->f0, (double)k->f1, (double)k->f2,
                                   &p->rotation_yaw, &p->rotation_pitch,
                                   &p->prev_rotation_yaw, &p->prev_rotation_pitch,
                                   k->f3, k->f4);
                p->confirm_og = k->i0;
                /* the C06 is built here, from the position the S08 left
                 * (a later S0A in the same pump moves the player again);
                 * each S08 of the pump sends its own */
                if (p->pending_confirm < S08_CONFIRM_MAX)
                {
                    struct c03 *c6 = &p->confirm_pkt[p->pending_confirm++];
                    memset(c6, 0, sizeof *c6);
                    c6->kind = C03_POSLOOK;
                    c6->c = p->e.pos_x;
                    c6->d = p->e.bounding_box.min_y;
                    c6->e = p->e.pos_z;
                    c6->f = p->e.pos_y;
                    c6->yaw = p->rotation_yaw;
                    c6->pitch = p->rotation_pitch;
                    c6->on_ground = k->i0;
                }
                break;

            case PK_S0A:
                sleep_client_use_bed(p, k->i0, k->i1, k->i2);
                break;

            case PK_S02:
                /* handleChat: GuiNewChat's line (the live client draws it):
                 * the parts after the S02's JSON (chatcomp.h) */
                if (p->s02_n < (int)(sizeof p->s02_nparts / sizeof p->s02_nparts[0]))
                {
                    int n = p->s02_n++;
                    p->s02_nparts[n] = 0;
                    p->s02_id[n] = 0;
                    const char *side = s2c_side(q, k);
                    if (side != NULL)
                    {
                        size_t json = strlen(side) + 1;
                        size_t rest = (size_t)k->side_len - json;
                        if (rest <= sizeof p->s02_text[n])
                        {
                            memcpy(p->s02_text[n], side + json, rest);
                            p->s02_nparts[n] = k->i0;
                        }
                    }
                }
                break;

            case PK_S1C_ABS:
                /* handleEntityMetadata: the client player's absorption */
                p->sv.absorption = (float)k->f0;
                break;

            case PK_S1C_POT:
                /* handleEntityMetadata: the client player's potion colour
                 * and ambience, what its updatePotionEffects draws by */
                p->sv.potion_liquid_color = k->i0;
                p->sv.potion_is_ambient = (uint8_t)k->i1;
                break;

            case PK_S3A:
                /* handleTabComplete: func_146406_a with a GuiChat up */
                if (p->screen_chat || p->screen_sleep)
                {
                    const char *items[GC_TABS];
                    const char *side = s2c_side(q, k);
                    int n = 0;
                    for (int i = 0; side != NULL && i < k->i0 && n < GC_TABS; ++i)
                    {
                        items[n++] = side;
                        side += strlen(side) + 1;
                    }
                    gui_chat_tab_reply(p, items, n);
                }
                break;

            case PK_S09:
                /* handleHeldItemChange: inventory.currentItem, a hotbar
                 * slot only (no C09: the controller keeps its own) */
                if (k->i0 >= 0 && k->i0 < 9)
                {
                    p->hotbar = k->i0;
                    p->sv.current_item = k->i0;
                }
                break;

            case PK_S1B:
                ride_client_s1b(p, k->i0);
                break;

            case PK_S2B:
                /* handleChangeGameState 4: displayGuiScreen(new GuiWinGame()),
                 * which takes the focus and unpresses every key */
                if (k->i0 == 4)
                {
                    p->screen_chat = 0;
                    p->screen_credits = 1;
                    p->credits_clock = 0;
                    p->in_game_has_focus = 0;
                    memset(&p->keys, 0, sizeof p->keys);
                }
                break;

            case PK_S2C:
                /* handleSpawnGlobalEntity: the client's EntityLightningBolt,
                 * the Entity constructor's draws (the bolt's own Random
                 * takes the rest) */
                if (surv.det)
                {
                    det_next_entity_id_role(surv.det, DET_CLIENT);
                    (void)det_new_random_role(surv.det, DET_CLIENT);
                    int64_t msb, lsb;
                    det_uuid_role(surv.det, DET_CLIENT, &msb, &lsb);
                }
                break;

            case PK_S0B:
                if (k->i0 == 2) sleep_client_wake(p);
                /* handleAnimation's 4 and 5: an EntityCrit2FX ("crit",
                 * "magicCrit") on the client's copy of the entity, none
                 * when the client does not have it */
                else if ((k->i0 == 4 || k->i0 == 5) && combat_fx_target(p, k->i1, &(struct plive_target){0}))
                {
                    surv_fx_ensure(p);
                    surv_fx.target = combat_fx_target;
                    surv_fx.target_ctx = p;
                    particles_live_crit(&surv_fx, k->i1, k->i0 == 5);
                }
                break;

            case PK_S37:
                surv_client_apply_s37(p, k);
                break;

            case PK_S2D:
                /* handleOpenWindow's type 3: the new TileEntityDispenser's
                 * own Random (a dropper's window is the same type) */
                if (k->i1 == CONTAINER_DISPENSER && surv.det) (void)det_new_random_role(surv.det, DET_CLIENT);
                container_free(&p->wb_container);
                container_init(&p->wb_container, k->i1 ? k->i1 : CONTAINER_WORKBENCH, k->i2, 0);
                surv_seed_client_container(&p->wb_container, p);
                p->wb_container.window_id = k->i0;
                snprintf(p->wb_container.title, sizeof p->wb_container.title, "%s",
                         s2c_side(q, k) ? (const char *)s2c_side(q, k) : "");
                p->open_container = &p->wb_container;
                p->screen_inventory = 1;
                p->screen_chat = 0;
                p->gui_own = 0;
                /* displayGuiScreen's setIngameNotInFocus: every key binding
                 * unpressed (KeyBinding.unPressAllKeys), the focus gone */
                if (p->in_game_has_focus)
                {
                    memset(&p->keys, 0, sizeof p->keys);
                    p->in_game_has_focus = 0;
                }
                break;

            case PK_S3F:
                /* NetHandlerPlayClient's MC|TrList handler: the recipe list
                 * the merchant screen draws and trades against, only while
                 * that screen is up and the window matches. */
                {
                    struct container *c = p->open_container;

                    /* currentScreen instanceof GuiMerchant */
                    if (c != NULL && p->screen_inventory && !p->gui_own && c->kind == CONTAINER_MERCHANT &&
                        c->window_id == k->i0)
                    {
                        c->client_recipes.n = k->ntrlist < TRADES_MAX ? k->ntrlist : TRADES_MAX;
                        const struct s2c_trade *trlist = s2c_side(q, k);
                        for (int i = 0; i < c->client_recipes.n; ++i)
                        {
                            struct trade_recipe *r = &c->client_recipes.r[i];
                            memset(r, 0, sizeof *r);
                            r->buy.item = trlist[i].item;
                            r->buy.count = trlist[i].count;
                            r->buy.damage = trlist[i].damage;
                            r->buy.tag = trlist[i].tag;
                            r->has_buy_b = trlist[i].has_buy_b;
                            if (r->has_buy_b)
                            {
                                r->buy_b.item = trlist[i].item_b;
                                r->buy_b.count = trlist[i].count_b;
                                r->buy_b.damage = trlist[i].damage_b;
                                r->buy_b.tag = trlist[i].tag_b;
                            }
                            r->sell.item = trlist[i].sell_item;
                            r->sell.count = trlist[i].sell_count;
                            r->sell.damage = trlist[i].sell_damage;
                            r->sell.tag = trlist[i].sell_tag;
                            r->max_uses = 7;
                            /* readRecipiesFromPacketBuffer: a disabled offer's
                             * func_82785_h, toolUses = maxTradeUses */
                            if (trlist[i].disabled) r->uses = r->max_uses;
                        }
                        c->recipes = &c->client_recipes;
                        container_merchant_reset(c);
                    }
                }
                break;

            /* handleCloseWindow: closeScreenNoPacket, the client's own
             * close without the C0D. The cursor's stack empties silently
             * (the server's closeContainer spilled it) and the container
             * is the inventory one again. */
            case PK_S31:
                /* handleWindowProperty: the open container's updateProgressBar
                 * when the window matches; only ContainerFurnace keeps them */
                if (p->open_container != NULL && p->open_container->window_id == k->i0 &&
                    p->open_container->kind == CONTAINER_FURNACE && k->i1 >= 0 && k->i1 < 3)
                    p->open_container->furnace_progress[k->i1] = k->i2;
                break;

            case PK_S2E:
            {
                /* closeScreenNoPacket: the cursor emptied, openContainer the
                 * inventory one; then EntityPlayerSP.closeScreen's
                 * displayGuiScreen(null) closes whatever screen is up
                 * (onGuiClosed on its container, then setIngameFocus) */
                struct container *shown = p->screen_inventory ? cp_gui_container(p) : NULL;
                if (p->open_container != NULL)
                {
                    craft_stack_free(&p->open_container->cursor);
                    p->open_container->cursor = (struct craft_stack){-1, 0, 0, 0};
                }
                /* InventoryPlayer.itemStack is one stack under every window */
                craft_stack_free(&p->own_container.cursor);
                p->own_container.cursor = (struct craft_stack){-1, 0, 0, 0};
                p->sv.cursor.item = 0;
                p->sv.cursor.damage = 0;
                p->sv.cursor.count = 0;
                p->sv.cursor.tag = 0;
                int was_wb = shown == &p->wb_container;
                if (shown != NULL && !was_wb) client_container_closed(p, shown);
                if (p->open_container == &p->wb_container) container_free(&p->wb_container);
                p->open_container = &p->own_container;
                if ((p->screen_inventory || p->screen_chat) && !p->in_game_has_focus)
                {
                    p->in_game_has_focus = 1;
                    p->left_click_counter = 10000;
                }
                p->screen_inventory = 0;
                p->screen_chat = 0;
                p->gui_own = 0;
                break;
            }
        }
    }

    s2c_drain();
}

struct client_pkt *client_pkt_add(struct client_out *out, int kind)
{
    if (out->npkt == CLIENT_PKT_CAP)
    {
        fprintf(stderr, "survival: more than %d packets in one client tick\n", CLIENT_PKT_CAP);
        abort();
    }
    struct client_pkt *k = &out->pkt[out->npkt++];
    memset(k, 0, sizeof *k);
    k->kind = kind;
    return k;
}

/* syncCurrentPlayItem inside a consumer: the C09 its packet rides behind. */
static void sync_current_play_item(struct client_player *p, struct client_out *out)
{
    if (p->hotbar != p->synced_item)
    {
        p->synced_item = p->hotbar;
        client_pkt_add(out, CPK_C09)->v = p->hotbar;
    }
}

static struct living *surv_find_living(int entity_id)
{
    if (!surv.anw) return NULL;
    for (int i = 0; i < surv.anw->n; ++i)
    {
        struct an_ent *en = an_ent_at(surv.anw->slot[i]);
        if (en && en->used && en->is_living && en->livh && lv_get(en->livh)->entity_id == entity_id)
            return lv_get(en->livh);
    }
    return NULL;
}

/* The client's own copy of the animal (WorldClient's entity): what the
 * client half of interactWith writes to it before the server's S1C does.
 * ItemDye.itemInteractionForEntity dyes an unsheared fleece of another
 * colour, ItemSaddle saddles an unsaddled adult pig, each on the copy's own
 * watcher 16. */
static void client_copy_interact(struct client_player *p, const struct living *l)
{
    const struct surv_stack *held = &p->sv.inv[p->hotbar];
    if (l == NULL || p->combat == NULL || held->count <= 0) return;
    struct client_entity *ce = clientworld_get_entity(&p->combat->client, l->entity_id);
    if (ce == NULL) return;
    if (l->kind == AK_SHEEP && held->item == 351)
    {
        int color = ~held->damage & 15;
        if (!(ce->dw16 & 16) && (ce->dw16 & 15) != color) ce->dw16 = (ce->dw16 & 240) | color;
    }
    else if (l->kind == AK_PIG && held->item == 329 && ce->dw16 == 0 && l->growing_age >= 0) ce->dw16 = 1;
}

/* PlayerControllerMP.interactWithEntitySendPacket's client half. */
static int surv_client_entity_interact(struct client_player *p, struct living *l, struct client_out *out)
{
    client_copy_interact(p, l);
    struct surv_stack *lead = &p->sv.inv[p->hotbar];
    int had = lead->count > 0;
    if (l != NULL && leash_interact_first(lead, l, 0))
    {
        if (had && lead->count <= 0)
        {
            *lead = empty_stack();
            lead->gen = ++p->sv.gen_counter;
        }
        return 1;
    }
    /* EntityVillager.interact on the client: true for an adult living one
     * the player does not sneak at (the trade is the server's: the client
     * copy is never trading), and EntityAgeable's for a spawn egg; the
     * true answer keeps func_147121_ag from the held stack's air use */
    if (l != NULL && l->kind == VK_VILLAGER)
    {
        const struct surv_stack *h = &p->sv.inv[p->hotbar];
        int egg = h->count > 0 && h->item == 383;
        if (egg || (living_is_alive(l) && l->growing_age >= 0 && !(p->in_sneak && !p->sv.sleeping))) return 1;
    }
    /* EntityCreeper.interact swings before its isRemote test:
     * EntityClientPlayerMP.swingItem queues a C0A */
    if (l && l->kind == HK_CREEPER && p->sv.inv[p->hotbar].count > 0 && p->sv.inv[p->hotbar].item == 259)
    {
        ++out->nc0a;
        client_swing_item(p);
    }
    /* EntityVillager.interact answers true on the client too (the trade
     * opens on the server): no spawn egg in hand, alive, an adult, not
     * the client's villager is never trading (no sneak test in 1.7.10); no
     * item use follows */
    if (l && l->kind == VK_VILLAGER)
    {
        const struct surv_stack *held = &p->sv.inv[p->hotbar];
        if (!(held->count > 0 && held->item == 383) && living_is_alive(l) && l->growing_age >= 0)
            return 1;
    }
    int shears = l && l->kind == AK_MOOSHROOM && p->sv.inv[p->hotbar].count > 0 &&
                 p->sv.inv[p->hotbar].item == 359 && l->growing_age >= 0;
    /* the client reads the server's copy of an animal, whose love a hit this
     * tick has not reached yet; the client's own copy lost it to the
     * attack press's local attackEntityFrom (EntityAnimal's override) */
    int love = l ? l->in_love : 0, hit = 0;
    for (int i = 0; l != NULL && l->kind < AK_KINDS && i < out->npkt; ++i)
        hit |= out->pkt[i].kind == CPK_C02_ATTACK && out->pkt[i].v == l->entity_id;
    if (hit) l->in_love = 0;
    int used = entity_interact(&p->sv, p->hotbar, l, 0, NULL, p);
    if (hit) l->in_love = love;
    if (used)
    {
        /* EntityMooshroom.interact's shears on the client copy: setDead and
         * a largeexplode at its middle */
        if (shears)
            surv_client_fx_spawn(p, "largeexplode", l->e.pos_x, l->e.pos_y + (double)(l->e.height / 2.0F),
                                 l->e.pos_z, 0.0, 0.0, 0.0);
        return 1;
    }
    /* the client's pig never mounts (EntityPig.interact is server-only);
     * ItemSaddle runs on the client's copy of the pig; a named tag on any */
    struct surv_stack *held = &p->sv.inv[p->hotbar];
    if (l == NULL) return 0;
    if (!(l->kind == AK_PIG && ride_saddle_interact(held, l, 0)) && !name_tag_interact(held, l, 0)) return 0;
    if (held->count <= 0)
    {
        *held = empty_stack();
        held->gen = ++p->sv.gen_counter;
    }
    return 1;
}

/* ItemArmor.onItemRightClick, on either side: the piece goes on when its
 * armour slot (getArmorPosition: 36 + 3 - armorType) is empty, and the
 * held stack is spent. */
static int armor_right_click(struct surv_state *sv, struct surv_stack *held)
{
    if (held->count <= 0 || !ITEMS[held->item].exists || ITEMS[held->item].kind != ITEM_ARMOR) return 0;
    struct surv_stack *slot = &sv->inv[36 + 3 - ITEMS[held->item].armor_type];
    if (slot->count <= 0)
    {
        *slot = *held;
        slot->gen = ++sv->gen_counter;
        *held = empty_stack();
        held->gen = ++sv->gen_counter;
    }
    return 1;
}

/* Minecraft.func_147121_ag: entity, block, then in-air item use. Each
 * path's controller call (interactWithEntitySendPacket, onPlayerRightClick,
 * sendUseItem) syncs the held slot first; a click at nothing with an empty
 * hand syncs nothing, so a hotbar change that tick waits for the next
 * updateController. */
static void right_click_mouse(struct client_player *p, struct client_out *out)
{
    p->right_click_delay = 4;
    struct surv_stack *cur = &p->sv.inv[p->hotbar];
    /* each sender (interactWithEntitySendPacket, onPlayerRightClick,
     * sendUseItem) syncs first: a press that sends nothing sends no C09 */
    if (surv_mo.cur.entity_id)
    {
        sync_current_play_item(p, out);
        client_pkt_add(out, CPK_C02_USE)->v = surv_mo.cur.entity_id;
        if (surv_client_entity_interact(p, surv_find_living(surv_mo.cur.entity_id), out)) return;
        /* EntityLeashKnot.interactFirst answers true on the client */
        if (pickobj_client_kind(p, surv_mo.cur.entity_id) == PK_KNOT) return;
        if (pickobj_client_interact(p, surv_mo.cur.entity_id)) return;
    }
    if (surv_mo.cur.num &&
        (world_get_block(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z) & 4095) != 0)
    {
        sync_current_play_item(p, out);
        if (surv_client_use_pressed(p, out) || cur->count == 0) return;
    }

    if (cur->count == 0) return;
    sync_current_play_item(p, out);
    {
        struct client_pkt *k = client_pkt_add(out, CPK_C08);
        k->v = 1;
        k->stack = *cur;
    }

    /* sendUseItem answers true when useItemRightClick returned another
     * stack or changed its size, and func_147121_ag then calls
     * resetEquippedProgress2: the held item drops and rises again */
    int use_count = cur->count, use_gen = cur->gen, use_item = cur->item;

    /* sendUseItem's client-side onItemRightClick of a piece of armour */
    if (armor_right_click(&p->sv, cur))
    {
        if (cur->count != use_count || cur->gen != use_gen || cur->item != use_item) p->equip_reset = 1;
        return;
    }

    /* ItemFood.onItemRightClick client-side */
    if (cur->count > 0)
    {
        const struct item_def *d = &ITEMS[cur->item];

        if (d->exists && d->kind == ITEM_FOOD &&
            (p->sv.food.level < 20 || d->always_edible) &&
            !(p->sv.using_slot >= 0 && p->sv.using_slot == p->hotbar && p->sv.using_gen == cur->gen))
        {
            p->sv.using_count = 32;
            p->sv.using_slot = p->hotbar;
            p->sv.using_gen = cur->gen;
        }
        else if (d->exists && d->kind == ITEM_SWORD)
        {
            /* ItemSword.onItemRightClick on the client */
            if (!(p->sv.using_slot >= 0 && p->sv.using_slot == p->hotbar && p->sv.using_gen == cur->gen))
            {
                p->sv.using_count = 72000;
                p->sv.using_slot = p->hotbar;
                p->sv.using_gen = cur->gen;
            }
        }
        else if (cur->item == IT_MILK || (cur->item == IT_POTION && !(cur->damage & 16384)))
        {
            /* ItemBucketMilk / ItemPotion.onItemRightClick: setItemInUse(32) */
            set_item_in_use(&p->sv, p->hotbar, 32);
        }
        else if (cur->item == 261)
        {
            /* ItemBow.onItemRightClick on the client: setItemInUse(72000)
             * while the inventory holds an arrow (InventoryPlayer.hasItem) */
            int arrow = 0;
            for (int i = 0; i < 36 && !arrow; ++i) arrow = p->sv.inv[i].count > 0 && p->sv.inv[i].item == 262;
            if (arrow) set_item_in_use(&p->sv, p->hotbar, 72000);
        }
        else if (d->exists && d->kind != ITEM_FOOD && !throw_client_use(p))
        {
            struct iu_player ip = {p->e.pos_x, p->e.pos_y, p->e.pos_z,
                                   p->e.y_offset, p->rotation_yaw, p->rotation_pitch, 1, 0};
            struct iu_stack stack = {cur->item, cur->count, cur->damage}, give;
            jrand local = {0};
            itemuse_set_live_det(surv.det, DET_CLIENT);
            /* the client world's own Random (the Nether bucket's fizz); a
             * block callback the prediction sets off draws the client's
             * Math.random */
            det_state *prev_det = nw_env->blockcb.env.det;
            int prev_role = nw_env->blockcb.env.role;
            nw_env->blockcb.env.det = surv.det;
            nw_env->blockcb.env.role = DET_CLIENT;
            itemuse_try_use_item(p->e.world, &ip, &stack, p->cw_rand_known ? &p->cw_rand.r : &local, &give);
            nw_env->blockcb.env.det = prev_det;
            nw_env->blockcb.env.role = prev_role;
            itemuse_set_live_det(NULL, DET_CLIENT);
            if (give.item != 0)
            {
                struct surv_stack st = {.item = give.item, .count = give.count, .damage = give.damage};
                give_or_drop(NULL, &p->sv, &st);
            }
            if (stack.item != cur->item || stack.count != cur->count || stack.damage != cur->damage)
            {
                if (stack.item != cur->item) cur->tag = 0;   /* a new ItemStack */
                cur->item = stack.item; cur->count = stack.count; cur->damage = stack.damage;
                cur->gen = ++p->sv.gen_counter;
            }
        }
    }
    if (cur->count != use_count || cur->gen != use_gen || cur->item != use_item) p->equip_reset = 1;
}

int surv_client_use_pressed(struct client_player *p, struct client_out *out);

/* EntityClientPlayerMP.closeScreenNoPacket and the displayGuiScreen(null)
 * after it; 1 when that shows the GuiGameOver (a dead player), whose
 * allowUserInput is false. */
static int client_close_screen(struct client_player *p)
{
    /* closeScreenNoPacket: this.inventory.setItemStack(null) empties the
     * client's own cursor silently (the SERVER's closeContainer is what
     * drops the stack as an item); the next click's sync reads it */
    struct container *shown = cp_gui_container(p);
    craft_stack_free(&p->open_container->cursor);
    p->open_container->cursor = (struct craft_stack){-1, 0, 0, 0};
    craft_stack_free(&shown->cursor);
    shown->cursor = (struct craft_stack){-1, 0, 0, 0};
    p->sv.cursor.item = 0;
    p->sv.cursor.damage = 0;
    p->sv.cursor.count = 0;
    p->sv.cursor.tag = 0;
    /* then displayGuiScreen(null): GuiContainer.onGuiClosed runs the
     * screen's container's onContainerClosed on the client
     * (ContainerPlayer's grid spill) */
    if (shown != &p->wb_container) client_container_closed(p, shown);
    p->gui_own = 0;

    if (p->open_container != &p->own_container)
    {
        /* the client's own close drops nothing (the cursor's spill is
         * the server's); its workbench container dies here */
        if (p->open_container == &p->wb_container) container_free(&p->wb_container);

        p->open_container = &p->own_container;
        /* InventoryPlayer.itemStack is one stack under every window: a
         * cursor an S2F set while this one was not open goes too */
        craft_stack_free(&p->own_container.cursor);
        p->own_container.cursor = (struct craft_stack){-1, 0, 0, 0};
    }

    p->screen_inventory = 0;

    /* displayGuiScreen(null) for a dead player is the GuiGameOver, at
     * once: its allowUserInput is false, so no game input this tick */
    if (p->sv.health <= 0.0F)
    {
        p->screen_gameover = 1;
        p->in_game_has_focus = 0;
        return 1;
    }
    /* displayGuiScreen(null)'s setIngameFocus: the focus back, and the left
     * click counter at 10000 (the input block's decrement follows) */
    if (!p->in_game_has_focus)
    {
        p->in_game_has_focus = 1;
        p->left_click_counter = 10000;
    }
    return 0;
}

float surv_credits_end(const struct client_player *p)
{
    return p->credits_end > 0.0F ? p->credits_end : (float)(569 * 12 + 240 + 240 + 24) / 0.5F;
}

/* Minecraft.displayGuiScreen(new GuiChat()): the screen replaces the one up
 * (a GuiInventory's close runs its container's onContainerClosed, openContainer
 * stays), then setIngameNotInFocus. */
static void surv_client_open_chat(struct client_player *p, const char *preset)
{
    if (p->screen_inventory)
    {
        client_container_closed(p, cp_gui_container(p));
        p->screen_inventory = 0;
        p->gui_own = 0;
    }
    p->screen_chat = 1;
    gui_chat_open(p, preset, 0, GC_SCREEN_W, GC_SCREEN_H);
    if (p->in_game_has_focus) memset(&p->keys, 0, sizeof p->keys);
    p->in_game_has_focus = 0;
}

/* One client tick's survival parts, in runTick order: the right-click delay,
 * the screen check, the gui ops and the input consumers. The act's look and
 * the s2c pump ran before this. */
void surv_client_input(struct client_player *p, const struct act *a, struct client_out *out)
{
    /* the input block runs below, when nothing returns first (a game-over
     * screen this tick opened, the credits, a dead player's close) */
    p->input_ran = 0;
    if (p->right_click_delay > 0) --p->right_click_delay;

    /* runTick's "pick" section: the mouseover, before every consumer (made
     * ahead of the row's packets by client_player_pick) */
    if (!p->picked && p->e.world != NULL) surv_client_mouseover(p);

    /* runTick's screen substitution: the game-over screen, GuiSleepMP while
     * the player sleeps (opening it unpresses every key), and its close */
    if (!p->screen_inventory && !p->screen_gameover && !p->screen_sleep && !p->screen_chat)
    {
        if (p->sv.health <= 0.0F)
        {
            /* displayGuiScreen(GuiGameOver)'s setIngameNotInFocus: every key
             * unpressed (a sneak ends before the dead player's last move) */
            p->screen_gameover = 1;
            if (p->in_game_has_focus) memset(&p->keys, 0, sizeof p->keys);
            p->in_game_has_focus = 0;
        }
        else if (p->sv.sleeping)
        {
            p->screen_sleep = 1;
            gui_chat_open(p, "", 1, GC_SCREEN_W, GC_SCREEN_H);
            p->in_game_has_focus = 0;
            memset(&p->keys, 0, sizeof p->keys);
        }
    }
    else if (p->screen_sleep && !p->sv.sleeping)
    {
        p->screen_sleep = 0;
        p->in_game_has_focus = 1;
        p->left_click_counter = 10000;
    }

    if (p->screen_inventory || p->screen_gameover || p->screen_sleep || p->screen_credits || p->screen_chat)
        p->left_click_counter = 10000;

    /* an agent's gui ops, as the screen's handleInput finds it (Act.applyGui) */
    if (a->agent != NULL) act_agent_gui(p, (struct act *)a);

    /* GuiWinGame: its escape (the ["respawn"] op, the same C16 and close
     * EntityClientPlayerMP.respawnPlayer makes) sends PERFORM_RESPAWN and
     * closes the screen; the game input then runs in the same tick */
    if (p->screen_credits && a->gui_respawn)
    {
        out->c16 = 1;
        p->screen_credits = 0;
        p->in_game_has_focus = 1;
        p->left_click_counter = 10000;
    }

    /* GuiWinGame.updateScreen: the clock, and past the roll's end
     * func_146574_g's PERFORM_RESPAWN and close, as the escape above */
    if (p->screen_credits)
    {
        ++p->credits_clock;
        if ((float)p->credits_clock > surv_credits_end(p))
        {
            out->c16 = 1;
            p->screen_credits = 0;
            p->in_game_has_focus = 1;
            p->left_click_counter = 10000;
        }
    }

    if (p->screen_credits) return;   /* GuiWinGame.allowUserInput is false */

    /* the gui ops, run while a screen is open */
    if (p->screen_gameover && a->gui_respawn)
    {
        /* EntityClientPlayerMP.respawnPlayer: the C16, then
         * displayGuiScreen(null), which reopens the screen while the health
         * is still 0 (until the fresh player lands); a health the server
         * set again closes it */
        p->screen_gameover = p->sv.health <= 0.0F;
        out->c16 = 1;
    }

    if (p->screen_gameover) return;   /* GuiGameOver.allowUserInput is false */

    /* the ["wake"] op: GuiSleepMP.actionPerformed(button 1), func_146418_g's
     * C0B action 3; the screen closes when the S0B's wake lands */
    if (p->screen_sleep && a->gui_wake) out->c0b_wake = 1;

    /* the ["chat"] op: GuiScreen.handleInput over GuiChat or GuiSleepMP
     * (gui_chat.c), then updateScreen while it stays up. A close is
     * displayGuiScreen(null): onGuiClosed, then setIngameFocus */
    if (p->screen_chat || p->screen_sleep)
    {
        if (a->has_chat)
        {
            int close = 0, wake = 0;
            gui_chat_run(p, &a->chat, &close, &wake);
            if (wake && p->screen_sleep) out->c0b_wake = 1;
            if (close && p->screen_chat)
            {
                p->screen_chat = 0;
                p->in_game_has_focus = 1;
                p->left_click_counter = 10000;
            }
        }
        if (p->screen_chat || p->screen_sleep) gui_chat_update(p);
    }

    /* the ["close"] op: EntityClientPlayerMP.closeScreen (the C0D) and
     * closeScreenNoPacket (the cursor's stack is dropped by the SERVER's
     * closeContainer; the client's own cursor empties silently and its
     * container is the inventory one again) */
    /* the row's ordered window clicks, PlayerControllerMP.windowClick's
     * client half: they land on the open container and queue the C0E. A
     * close in the same row comes after them (nothing is clicked once the
     * screen is gone), as the server half runs them. */
    if (a->gui_in != NULL)
    {
        /* the playable client's raw input: GuiContainer's handlers make the
         * row's clicks here, each predicted as it is made (the act is the
         * caller's own, written to the tape after the tick) */
        gui_input_run(a->gui_in, p, (struct act *)a, out,
                      a->gui_in->has_clock ? a->gui_in->clock : p->client_row_tick * 50);
    }
    /* the row's ["trsel"] ops, in their places among the clicks ahead of
     * the first close (the raw screen input's after its clicks):
     * GuiMerchant.actionPerformed's own index change and its C17; the
     * client's container resets with it (the client's func_110297_a_ is the
     * NpcMerchant no-op) */
    int first = a->closes ? a->close_at[0] : a->clicks;
    for (int i = 0; i <= first; ++i)
    {
        for (int j = 0; j < a->trsels && j < 8; ++j)
        {
            if (a->gui_in != NULL ? i != first : a->trsel[j].at != i && !(i == first && a->trsel[j].at > i))
                continue;
            struct container *c = p->open_container;

            if (c != NULL && c->kind == CONTAINER_MERCHANT && c->window_id == a->trsel[j].window &&
                out->ntrsel < 8)
            {
                container_merchant_set_recipe_index(c, a->trsel[j].index);
                out->trsel[out->ntrsel].window = a->trsel[j].window;
                out->trsel[out->ntrsel].index = a->trsel[j].index;
                out->trsel[out->ntrsel].at = a->gui_in != NULL ? first : a->trsel[j].at;
                ++out->ntrsel;
            }
        }
        if (i < first && a->gui_in == NULL) surv_client_click(p, a, i, out);
    }

    if (a->gui_close)
    {
        /* a dead player's close opens the GuiGameOver at once; the keys
         * after it are still the closed screen's (gui_input.h), and a
         * respawn op after the close is the reopened screen's: its C16 */
        int dead = client_close_screen(p);
        if (a->gui_in != NULL) gui_input_closed(a->gui_in, p, (struct act *)a, out);
        else
            for (int i = first; i < a->clicks; ++i) surv_client_click_closed(p, a, i, out);
        if (dead)
        {
            if (a->gui_respawn_after_close) out->c16 = 1;
            return;
        }
    }

    /* GuiContainer.updateScreen, after the screen's handleInput (the row's
     * clicks land first): a dead player's container screen closes through
     * EntityClientPlayerMP.closeScreen, the C0D and then
     * closeScreenNoPacket, whose displayGuiScreen(null) is the GuiGameOver.
     * GuiInventory's own updateScreen does not call it: its screen stays */
    if (p->screen_inventory && cp_gui_container(p) != &p->own_container && p->sv.health <= 0.0F)
    {
        out->c0d_close = 1;
        client_close_screen(p);
        return;
    }

    /* the input block: runTick runs it when no screen is up or the screen
     * allows it (GuiInventory); a container screen an S2D opened at the
     * tick's start keeps it from running, whatever the act carries */
    /* an agent's keys at the input block (Act.applyInput) */
    if (a->agent != NULL) act_agent_input(p, (struct act *)a);
    p->input_ran = a->has_in && !(p->screen_inventory && cp_gui_container(p) != &p->own_container);
    if (p->input_ran)
    {
        p->keys = a->keys;

        /* session_parse_act refuses a row outside 0..8 */
        if (a->hb >= 0 && a->hb <= 8) p->hotbar = a->hb;
        else if (a->hb_live)
        {
            /* the playable client: the mouse loop's wheel turns the slot the
             * pump left (InventoryPlayer.changeCurrentItem per notch); the
             * tape gets the result */
            int hb = p->hotbar;
            for (int i = 0; i < a->hb_wheel; ++i) hb = (hb + 8) % 9;
            for (int i = 0; i < -a->hb_wheel; ++i) hb = (hb + 1) % 9;
            p->hotbar = hb;
            ((struct act *)a)->hb = hb;
        }

        p->in_game_has_focus = a->focus;
        p->left_click_counter = a->lcc;
        p->ctrl = a->ctrl;

        /* Act.applyOpts: the settings the keyboard loop's F-keys changed */
        if (a->has_opts)
        {
            if (a->o_tpv >= 0) p->opt_tpv = a->o_tpv;
            if (a->o_hide >= 0) p->opt_hide = a->o_hide;
            if (a->o_smooth >= 0) p->opt_smooth = a->o_smooth;
            if (a->o_dbg >= 0) p->opt_dbg = a->o_dbg;
            if (a->o_rd >= 0) p->opt_rd = a->o_rd;
        }
    }

    /* the vanilla consumers, in runTick's order */
    for (int i = 0; i < 9; ++i)
        if (p->keys.presses[K_HOTBAR + i] > 0)
        {
            --p->keys.presses[K_HOTBAR + i];
            p->hotbar = i;
        }

    /* the client's inventory.currentItem is the hotbar itself */
    p->sv.current_item = p->hotbar;

    while (p->keys.presses[K_INVENTORY] > 0)
    {
        --p->keys.presses[K_INVENTORY];
        p->screen_inventory = 1;
        /* GuiInventory over inventoryContainer; its GuiContainer.initGui
         * makes that the player's openContainer (a window a portal closed
         * without a C0D stops being the client's) */
        if (p->open_container == &p->wb_container) container_free(&p->wb_container);
        p->open_container = &p->own_container;
        p->gui_own = 0;
        /* displayGuiScreen's setIngameNotInFocus: every binding unpressed,
         * so no consumer after this one sees a press this tick */
        if (p->in_game_has_focus) memset(&p->keys, 0, sizeof p->keys);
        p->in_game_has_focus = 0;
        /* Minecraft.runTick's keyBindInventory branch: the C16
         * OPEN_INVENTORY_ACHIEVEMENT unless the game is creative (the native
         * port has no creative mode) */
        client_pkt_add(out, CPK_C16)->v = 2;
    }

    while (p->keys.presses[K_DROP] > 0)
    {
        --p->keys.presses[K_DROP];

        /* EntityClientPlayerMP.dropOneItem: a bare C07; the server drops
         * from its own currentItem, the last C09's */
        client_pkt_add(out, CPK_C07)->v = p->ctrl ? 3 : 4;
    }

    /* keyBindChat, then keyBindCommand with no screen up: displayGuiScreen
     * (GuiChat) (chatVisibility is FULL in the oracle's options), which
     * replaces any screen and takes the focus, unpressing every key */
    while (p->keys.presses[K_CHAT] > 0)
    {
        --p->keys.presses[K_CHAT];
        surv_client_open_chat(p, "");
    }

    if (!p->screen_inventory && !p->screen_chat && p->keys.presses[K_COMMAND] > 0)
    {
        --p->keys.presses[K_COMMAND];
        surv_client_open_chat(p, "/");
    }

    if (p->sv.using_slot >= 0)
    {
        if (!p->keys.held[K_USE])
        {
            /* onStoppedUsingItem */
            sync_current_play_item(p, out);
            client_pkt_add(out, CPK_C07)->v = 5;

            /* stopUsingItem: the bow's client-side release */
            throw_client_release(p);
            p->sv.using_slot = -1;
            p->sv.using_gen = 0;
            p->sv.using_count = 0;
        }

        /* the using branch swallows the attack, use and pick presses */
        p->keys.presses[K_ATTACK] = 0;
        p->keys.presses[K_USE] = 0;
        p->keys.presses[K_PICK] = 0;
    }
    else
    {
        /* runTick's other branch: the attack presses (func_147116_af) first,
         * in game focus only as the dig's own gate */
        if (p->in_game_has_focus && !p->screen_inventory && !p->screen_gameover && !p->screen_sleep && !p->screen_chat)
            while (p->keys.presses[K_ATTACK] > 0)
            {
                --p->keys.presses[K_ATTACK];
                surv_client_attack_pressed(p, out);
            }

        if (!p->screen_inventory && !p->screen_gameover && !p->screen_sleep && !p->screen_chat)
        {
            /* then the use presses (func_147121_ag) */
            while (p->keys.presses[K_USE] > 0)
            {
                --p->keys.presses[K_USE];
                right_click_mouse(p, out);
            }

            /* runTick's keyBindPickBlock loop, after the use presses */
            while (p->keys.presses[K_PICK] > 0)
            {
                --p->keys.presses[K_PICK];
                surv_client_pick_block(p);
            }

            /* the held use's re-fire, with no screen up and in game focus
             * (EntityPlayerSP.onLivingUpdate's use handling) */
            if (p->keys.held[K_USE] && p->right_click_delay == 0 && p->sv.using_slot < 0)
                right_click_mouse(p, out);
        }
        else
        {
            p->keys.presses[K_USE] = 0;
            p->keys.presses[K_PICK] = 0;
        }
    }

    /* the block dig (func_147115_a), once per tick after the consumers, in
     * game only (its own gates) */
    if (p->in_game_has_focus && !p->screen_inventory && !p->screen_gameover && !p->screen_sleep && !p->screen_chat)
        surv_client_dig_tick(p, p->keys.held[K_ATTACK] != 0, out);
    else
    {
        /* the held path's gate fails: the reset half alone (its
         * leftClickCounter zeroing is inside dig_tick) */
        surv_client_dig_tick(p, 0, out);
    }
}

/* The client half of a row's ["click", window, slot, button, mode] op:
 * PlayerControllerMP.windowClick, in the gui-input phase (the screen is up).
 * The click lands on the client's own container copy, whose inventories the
 * S2F mirror keeps in step with the server's, and the C0E goes out. */
#define CLICK_STACK_EMPTY {-1, 0, 0, 0}

static void client_click_on(struct client_player *p, struct container *c, const struct act *a, int i,
                            struct client_out *out);

void surv_client_click(struct client_player *p, const struct act *a, int i, struct client_out *out)
{
    struct container *c = cp_gui_container(p);

    out->click_sent = p->click_sent;
    out->click_ret = p->click_ret;
    p->click_sent[i] = 0;

    /* a tick no screen took: the op did not run (a harness before
     * lane/agentscreens wrote an agent's ops raw on such ticks; the harness
     * now refuses them) */
    if (c == NULL || !p->screen_inventory) return;

    /* a click the vanilla client's container screen never makes (Act.java
     * badClick refuses it, and its replay diverges on a row that carries
     * one): none on another window, none on a slot the window does not
     * have or on none where slotClick reads one (the hotbar swap, a drag's
     * slot step) */
    int s = a->gui_click[i].slot, b = a->gui_click[i].button, m = a->gui_click[i].mode;
    if (a->gui_click[i].window != c->window_id)
    {
        snprintf(p->refused, sizeof p->refused, "click window %d is not the open window %d",
                 a->gui_click[i].window, c->window_id);
        return;
    }
    if (s >= c->nslots || (s < 0 && s != -1 && s != -999))
    {
        snprintf(p->refused, sizeof p->refused, "click slot %d outside window %d's %d slots",
                 s, c->window_id, c->nslots);
        return;
    }
    if (s < 0 && (m == 2 || (m == 5 && (b & 3) == 1)))
    {
        snprintf(p->refused, sizeof p->refused, "click slot %d in mode %d", s, m);
        return;
    }
    client_click_on(p, c, a, i, out);
}

/* A click a closed screen's keys made (gui_input_closed; a tape's click
 * after a ["close"] in its row): PlayerControllerMP.windowClick predicts on
 * player.openContainer, the inventory container since closeScreen, with
 * the old window's slot number (the C0E keeps the old window's id: the
 * server drops it unless that was window 0) */
void surv_client_click_closed(struct client_player *p, const struct act *a, int i, struct client_out *out)
{
    struct container *c = &p->own_container;

    out->click_sent = p->click_sent;
    out->click_ret = p->click_ret;
    p->click_sent[i] = 0;
    int s = a->gui_click[i].slot;
    if (s < 0 || s >= c->nslots)
    {
        /* Container.slotClick reads the slot: the vanilla client throws */
        snprintf(p->refused, sizeof p->refused, "a closed screen's click slot %d outside the inventory's %d", s,
                 c->nslots);
        return;
    }
    client_click_on(p, c, a, i, out);
}

static void client_click_on(struct client_player *p, struct container *c, const struct act *a, int i,
                            struct client_out *out)
{
    /* the client's own stack mirror moved since the last click (the S2Fs,
     * the pickups): the container's copy follows it, as the client's
     * InventoryPlayer is the container's inventory */
    for (int s = 0; s < 40; ++s)
    {
        struct craft_stack cs = craft_from_surv(&p->sv.inv[s]);
        container_set_player(c, s, &cs);
    }

    struct craft_stack ret = CLICK_STACK_EMPTY;
    container_slot_click(c, a->gui_click[i].slot, a->gui_click[i].button,
                         a->gui_click[i].mode, &ret);
    /* the C0E carries what the click returned */
    p->click_sent[i] = 1;
    p->click_ret[i] = ret;
    craft_stack_free(&ret);

    /* the drops the click made on the client (the cursor on slot -999, the
     * Q key's mode 4): dropPlayerItemWithRandomChoice on the client player */
    for (int d = 0; d < c->ndrops; ++d) client_drop_draws(p, c->drops[d].count);
    for (int d = 0; d < c->ndrops; ++d) craft_stack_free(&c->drops[d]);
    c->ndrops = 0;

    /* the cursor: the client's own (the rows' cp.cur) */
    surv_from_craft(&p->sv.cursor, &c->cursor);

    /* the client's own container is over the client's sv.inv: copy it back */
    for (int s = 0; s < 40; ++s)
    {
        struct craft_stack *cs = &c->player.slot[s];
        struct surv_stack *ss = &p->sv.inv[s];

        surv_from_craft(ss, cs);

        if (cs->count > 0 && (ss->gen == 0)) ss->gen = ++p->sv.gen_counter;
    }
}

/* The server half of a row's click op: NetHandlerPlayServer.processClickWindow
 * over the server's own container (the window id must match, as the
 * packet's own check does), then the S2F slots the changed stack mirrors
 * queue (detectAndSendChanges). The transaction confirm changes nothing the
 * rows read. The server container's inventories are the server player's
 * sv.inv; the click mutates the container's copy, which copies back. */
int surv_server_click(struct server_player *p, const struct act *a, int i, struct craft_stack *out_ret)
{
    struct container *c = p->open_container;

    *out_ret = (struct craft_stack)CLICK_STACK_EMPTY;
    if (c == NULL) return 0;

    /* processClickWindow's window test: a click on another window (a
     * GuiInventory's window 0 while the server holds a chest open) is
     * dropped whole, with no confirm and no resend */
    if (a->gui_click[i].window != c->window_id)
    {
        return 0;
    }

    /* the world tick's pickups moved sv.inv since the last click: the
     * container's copy follows (the same InventoryPlayer object in Java) */
    surv_seed_container(c, p);

    struct craft_stack ret = CLICK_STACK_EMPTY;
    container_slot_click(c, a->gui_click[i].slot, a->gui_click[i].button,
                         a->gui_click[i].mode, &ret);
    *out_ret = ret;
    craft_stack_free(&ret);

    /* The drops a click makes land in the world at once: Java's
     * dropPlayerItemWithRandomChoice runs inside slotClick and spawns the
     * EntityItem there (the cursor drop on slot -999 and the Q key's mode 4),
     * so the entity, its id and the draws it makes exist this tick. The
     * container's own close-time spill (container_close) is thrown by the
     * ["close"] handler below, not here. */
    for (int d = 0; d < c->ndrops; ++d)
        throw_item(p, &(struct surv_stack){.item = c->drops[d].item, .damage = c->drops[d].damage,
                                           .count = c->drops[d].count, .tag = c->drops[d].tag}, 0);
    c->ndrops = 0;

    surv_from_craft(&p->sv.cursor, &c->cursor);

    /* the container's player inventory is the server's own: copy it back */
    for (int s = 0; s < 40; ++s)
    {
        struct craft_stack *cs = &c->player.slot[s];
        struct surv_stack *ss = &p->sv.inv[s];

        surv_from_craft(ss, cs);

        if (cs->count > 0 && ss->gen == 0) ss->gen = ++p->sv.gen_counter;
    }

    /* the ender window's chest slots are the ender inventory: copy it back */
    if (p->gui_ender)
    {
        for (int s = 0; s < c->chest_size && s < 27; ++s)
        {
            struct craft_stack *cs = &c->chest.slot[s];
            struct surv_stack *ss = &p->sv.ender[s];

            surv_from_craft(ss, cs);

            if (cs->count > 0 && ss->gen == 0) ss->gen = ++p->sv.gen_counter;
        }
    }

    /* the grid's stacks ride the same S2F slots the container walk covers */
    return 1;
}

/* The client tick's survival parts, after the act's look: updateController
 * (syncCurrentPlayItem and the s2c pump), the screens, the gui ops and the
 * input consumers. */
void surv_client_tick_start(struct client_player *p, const struct act *a, struct client_out *out)
{
    /* updateController's syncCurrentPlayItem uses the hotbar as the previous
     * tick's input left it; a paused game skips updateController, the
     * packets included (they wait for the next unpaused tick) */
    if (!p->game_paused && p->hotbar != p->synced_item)
    {
        p->synced_item = p->hotbar;
        out->c09_slot = p->hotbar;
    }

    p->s02_n = 0;
    p->chat_out.n = 0;
    p->chat_fx.nscroll = 0;
    out->chat = &p->chat_out;
    if (!p->game_paused)
    {
        surv_client_pump(p);
        pickobj_client_pump_positions(p);
    }
    /* the S08's C06 acknowledgement is dispatched the moment the S08 is
     * handled (scheduleOutboundPacket writes it immediately), so the server's
     * networkTick of the same row processes it before the capture */
    if (p->pending_confirm)
    {
        out->has_confirm_c03 = p->pending_confirm;
        memcpy(out->confirm_c03, p->confirm_pkt, sizeof out->confirm_c03);
        p->pending_confirm = 0;
    }
    if (p->pending_confirm2)
    {
        out->has_confirm2_c03 = 1;
        out->confirm2_c03 = p->confirm2_pkt;
        p->pending_confirm2 = 0;
    }
    surv_client_input(p, a, out);
}

/* The client's mouseover, EntityLivingBase.rayTrace(4.5, 1.0): the ray
 * starts at the renderViewEntity's position vector (posX, posY, posZ), whose
 * posY is already the standing eye (the client's posY carries the S08's
 * +1.62), and runs along getLook(1.0). func_147447_a's flags are the
 * rayTrace call's: stopOnLiquid false, ignoreBlockWithoutBoundingBox false,
 * returnLastUncollidableBlock true. The server's C07 0/1/2 statuses read
 * the same mouseover (the client state machine only queues what the
 * server's coordinates say), so it is the environment's (surv_mo, env.h). */
void surv_client_mouseover(struct client_player *p)
{
    surv_mo.prev = surv_mo.cur.num;
    surv_mo.prev_copy = surv_mo.cur;

    struct rt_mop mop;
    double ex = p->e.pos_x;
    double ey = p->e.pos_y;
    double ez = p->e.pos_z;

    /* EntityLivingBase.getLook(1.0), the rotation's own cos/sin pair */
    float yaw = p->rotation_yaw, pitch = p->rotation_pitch;
    float cy = mh_cos(-yaw * 0.017453292F - (float)M_PI);
    float sy = mh_sin(-yaw * 0.017453292F - (float)M_PI);
    float cp = -mh_cos(-pitch * 0.017453292F);
    float sp = mh_sin(-pitch * 0.017453292F);
    double dx = (double)(sy * cp);
    double dy = (double)sp;
    double dz = (double)(cy * cp);

    int hit = raytrace_blocks(p->e.world, ex, ey, ez,
                              ex + dx * 4.5, ey + dy * 4.5, ez + dz * 4.5,
                              0, 0, 1, &mop);

    /* a snapshot's first tick: the client world has no chunks yet (the
     * terrain the server sent lands with the next tick's packets), so
     * func_147447_a walks air and returns the MISS at its last cell, over
     * 3 blocks out (a pick, a click or a use sees nothing to hit) */
    if (p->first_client_tick == 0)
    {
        hit = 1;
        mop.hit = 1;
        mop.type = RT_MISS;
        mop.hx = ex + dx * 4.5;
        mop.hy = ey + dy * 4.5;
        mop.hz = ez + dz * 4.5;
    }

    surv_mo.cur.num = (hit && mop.hit && mop.type != RT_MISS) ? 1 : 0;
    surv_mo.cur.entity_id = 0;
    surv_mo.cur.is_miss = hit && mop.hit && mop.type == RT_MISS;

    if (surv_mo.cur.num)
    {
        surv_mo.cur.x = mop.x;
        surv_mo.cur.y = mop.y;
        surv_mo.cur.z = mop.z;
        surv_mo.cur.side = mop.type;
        surv_mo.cur.hx = (float)(mop.hx - (double)mop.x);
        surv_mo.cur.hy = (float)(mop.hy - (double)mop.y);
        surv_mo.cur.hz = (float)(mop.hz - (double)mop.z);
    }

    /* getMouseOver: the survival reach 4.5 caps the entity reach var2 at
     * 3.0; var4 is the block result's hitVec distance (Vec3.distanceTo, a
     * float) whenever the trace returned one, a MISS included */
    int have_mo = hit && mop.hit;
    double var4 = have_mo ? pick_distance(mop.hx, mop.hy, mop.hz, ex, ey, ez) : 3.0;
    int entity_id = combat_pick_entity(p, ex, ey, ez, dx, dy, dz, 3.0, var4, have_mo);
    if (entity_id)
    {
        surv_mo.cur.num = 0;
        surv_mo.cur.is_miss = 0;
        surv_mo.cur.entity_id = entity_id;
    }
}

/* Minecraft.func_147112_ai for a survival player: a block under the cursor
 * (an entity or a miss is creative-only), its pick item, and
 * InventoryPlayer.func_146030_a(item, damage, hasSubtypes, false): the first
 * main-inventory slot holding the item (and the damage when the item has
 * subtypes) becomes the current item when it is a hotbar slot; nothing
 * otherwise. The C09 goes out with the next updateController. */
void surv_client_pick_block(struct client_player *p)
{
    if (!surv_mo.cur.num || surv_mo.cur.entity_id != 0 || p->e.world == NULL) return;

    int item, damage, sub;
    if (!pick_block_item(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z, &item, &damage, &sub))
        return;

    for (int i = 0; i < 36; ++i)
    {
        const struct surv_stack *st = &p->sv.inv[i];
        if (st->count <= 0 || st->item != item || (sub && st->damage != damage)) continue;

        if (i < 9)
        {
            p->hotbar = i;
            p->sv.current_item = i;
        }
        return;
    }
}

/* The PlayerControllerMP dig state machine, the client's copy (struct
 * surv_dig_state, survival.h). It reads the mouseover and the held stack (the
 * client mirror), queues the C07s, and keeps the progress the client's own
 * swing logic shows. */
#define surv_dig (nw_env->survival.dig)

static int dig_rt_i(const nbt *o, const char *key, int dflt)
{
    const nbt *v = o ? nbt_get(o, key) : NULL;
    return v ? (int)nbt_int_value(v) : dflt;
}

static float dig_rt_f(const nbt *o, const char *key, float dflt)
{
    const nbt *v = o ? nbt_get(o, key) : NULL;
    if (!v) return dflt;
    uint32_t b = nbt_float_bits(v);
    float f;
    memcpy(&f, &b, sizeof f);
    return f;
}

void surv_dig_load(struct client_player *cp, struct server_player *sp, const void *ctree, const void *stree)
{
    /* PlayerControllerMP: the client's dig and the hotbar slot it last
     * sent (syncCurrentPlayItem's currentPlayerItem) */
    const nbt *ctl = nbt_get((const nbt *)ctree, "ctl");
    if (ctl)
    {
        cp->synced_item = dig_rt_i(ctl, "currentPlayerItem", cp->synced_item);
        surv_dig.is_hitting = dig_rt_i(ctl, "isHittingBlock", 0);
        surv_dig.cur_x = dig_rt_i(ctl, "currentBlockX", -1);
        surv_dig.cur_y = dig_rt_i(ctl, "currentBlockY", -1);
        surv_dig.cur_z = dig_rt_i(ctl, "currentblockZ", -1);
        surv_dig.hit_delay = dig_rt_i(ctl, "blockHitDelay", 0);
        surv_dig.cur_damage = dig_rt_f(ctl, "curBlockDamageMP", 0.0F);
        surv_dig.step_counter = dig_rt_f(ctl, "stepSoundTickCounter", 0.0F);
        const nbt *hv = nbt_get(ctl, "currentItemHittingBlock");
        const char *hs = hv ? nbt_string_value(hv) : NULL;
        int item = 0, count = 0, damage = 0;
        if (hs && !strncmp(hs, "is:", 3)) sscanf(hs + 3, "%d,%d,%d", &item, &count, &damage);
        surv_dig.held_item = item;
        surv_dig.held_damage = damage;
        /* the snapshot's stack string carries no tag: the stack being hit
         * is the held one when its item and damage match */
        const struct surv_stack *hh = &cp->sv.inv[cp->hotbar];
        surv_dig.held_tag = hh->count > 0 && hh->item == item && hh->damage == damage ? hh->tag : 0;
    }

    /* ItemInWorldManager: a dig the server is timing (or a finish it is
     * waiting out): the machine resumes on the recorded target and counters */
    const nbt *iiw = nbt_get((const nbt *)stree, "iiw");
    if (!iiw) return;
    int destroying = dig_rt_i(iiw, "isDestroyingBlock", 0);
    int finish = dig_rt_i(iiw, "receivedFinishDiggingPacket", 0);
    /* an idle manager that has dug before: updateBlockRemoving still counts
     * curblockDamage every tick, and a STOP for the last block (the client
     * resumes a dig on the same block and tool without a new START) is
     * judged against its initialDamage */
    int idle = !destroying && !finish && dig_rt_i(iiw, "initialDamage", 0) != 0;
    if (!destroying && !finish && !idle) return;
    if (!surv_digenv_ready)
    {
        dig_init(&surv_digenv, sp->e.world, surv.det, 0);
        surv_digenv.role = DET_SERVER;
        surv_digenv.block_rands = 1;
        surv_digenv.net_ops = 1;
        surv_digenv_ready = 1;
    }
    dig_env *d = &surv_digenv;
    surv_dig_pose(d, sp);
    int part = destroying || idle;
    int x = part ? dig_rt_i(iiw, "partiallyDestroyedBlockX", -1) : dig_rt_i(iiw, "posX", -1);
    int y = part ? dig_rt_i(iiw, "partiallyDestroyedBlockY", -1) : dig_rt_i(iiw, "posY", -1);
    int z = part ? dig_rt_i(iiw, "partiallyDestroyedBlockZ", -1) : dig_rt_i(iiw, "posZ", -1);
    dig_case_begin(d, 0, x, y, z, d->p.held_item, d->p.held_damage, d->p.held_count, d->p.eff, d->p.unbr,
                   d->p.silk, d->p.fortune, sp->e.on_ground, d->p.in_water, 0, 0, 0, 0);
    d->p.exhaustion = sp->sv.food.exhaustion;
    d->m.is_destroying = destroying;
    d->m.initial_damage = dig_rt_i(iiw, "initialDamage", 0);
    d->m.part_x = dig_rt_i(iiw, "partiallyDestroyedBlockX", -1);
    d->m.part_y = dig_rt_i(iiw, "partiallyDestroyedBlockY", -1);
    d->m.part_z = dig_rt_i(iiw, "partiallyDestroyedBlockZ", -1);
    d->m.curblock_damage = dig_rt_i(iiw, "curblockDamage", 0);
    d->m.finish = finish;
    d->m.pos_x = dig_rt_i(iiw, "posX", -1);
    d->m.pos_y = dig_rt_i(iiw, "posY", -1);
    d->m.pos_z = dig_rt_i(iiw, "posZ", -1);
    d->m.initial_block = dig_rt_i(iiw, "initialBlockDamage", 0);
    d->m.durability = dig_rt_i(iiw, "durabilityRemainingOnBlock", -1);
    /* an idle manager's last block may lie in a chunk that has unloaded
     * since: reading it must not load it */
    int here = world_chunk_loaded(sp->e.world, x >> 4, z >> 4);
    d->m.initial_id = here ? world_get_block(sp->e.world, x, y, z) & 4095 : 0;
    d->m.initial_meta = here ? world_get_meta(sp->e.world, x, y, z) : 0;
    surv_dig_active = 1;
}

int surv_client_dig_stage(int *x, int *y, int *z)
{
    if (!surv_dig.is_hitting) return -1;
    int stage = (int)(surv_dig.cur_damage * 10.0F) - 1;
    if (stage < 0 || stage > 9) return -1;
    *x = surv_dig.cur_x;
    *y = surv_dig.cur_y;
    *z = surv_dig.cur_z;
    return stage;
}

/* WorldClient.setBlockToAir predicts the break before its C07 reaches the
 * server. Whole-server tapes give this player a separate client world. */
static void client_destroy(struct client_player *p, int x, int y, int z);

static void surv_c07(struct client_out *out, int status, int x, int y, int z, int side)
{
    struct client_pkt *k = client_pkt_add(out, CPK_C07);
    k->v = status;
    k->x = x;
    k->y = y;
    k->z = z;
    k->side = side;
}

static int surv_same_tool_and_block(struct client_player *p)
{
    /* PlayerControllerMP.sameToolAndBlock: x/y/z are the caller's, the stack
     * pair compares the item, the tag (ItemStack.areItemStackTagsEqual: an
     * Efficiency pickaxe and a plain one, two differently named stacks,
     * restart the dig) and, for a stack that cannot take damage, the
     * damage. isHittingBlock is NOT part of it: after a resetBlockRemoving
     * the client keeps silently accumulating on the same block (no status 0)
     * once the mouseover returns, exactly as vanilla does. A damageable held
     * stack (a pickaxe whose damage the server's S2F just moved) still counts
     * as the same tool, so the dig is not restarted. */
    const struct surv_stack *held = &p->sv.inv[p->hotbar];

    return surv_dig.held_item == held->item && surv_dig.held_tag == held->tag &&
        (ITEMS[held->item].max_damage > 0 || surv_dig.held_damage == held->damage);
}

/* EntityLivingBase.renderBrokenItemStack on the client player: the break
 * sound's pitch on WorldClient.rand (when the client carries one), then five
 * iconcrack particles, each drawing a float and a Det.math double for its
 * velocity and two floats for its offset from the player's Random, both
 * turned by the look (Vec3.rotateAroundX, rotateAroundY), placed at the eye
 * (posY + EntityPlayer.getEyeHeight's 0.12F). */
void surv_client_render_broken(struct client_player *p, const struct surv_stack *broken)
{
    surv_fx_ensure(p);
    if (surv_fx.world_rand) (void)det_rng_float(surv_fx.world_rand);

    char name[32];
    snprintf(name, sizeof name, "iconcrack_%d", broken->item);
    float ax = -p->rotation_pitch * (float)M_PI / 180.0F;
    float ay = -p->rotation_yaw * (float)M_PI / 180.0F;
    float cx = mh_cos(ax), sx = mh_sin(ax), cy = mh_cos(ay), sy = mh_sin(ay);

    for (int i = 0; i < 5; ++i)
    {
        double vx = ((double)det_rng_float(&p->sv.erand) - 0.5) * 0.1;
        double vy = surv.det ? det_math_random_role(surv.det, DET_CLIENT) * 0.1 + 0.1 : 0.1;
        double vz = 0.0;
        double t = vy * (double)cx + vz * (double)sx;
        vz = vz * (double)cx - vy * (double)sx;
        vy = t;
        t = vx * (double)cy + vz * (double)sy;
        vz = vz * (double)cy - vx * (double)sy;
        vx = t;

        double ox = ((double)det_rng_float(&p->sv.erand) - 0.5) * 0.3;
        double oy = (double)(-det_rng_float(&p->sv.erand)) * 0.6 - 0.3;
        double oz = 0.6;
        t = oy * (double)cx + oz * (double)sx;
        oz = oz * (double)cx - oy * (double)sx;
        oy = t;
        t = ox * (double)cy + oz * (double)sy;
        oz = oz * (double)cy - ox * (double)sy;
        ox = t;

        if (surv.det)
            surv_client_fx_spawn(p, name, ox + p->e.pos_x, oy + (p->e.pos_y + (double)0.12F), oz + p->e.pos_z,
                                 vx, vy + 0.05, vz);
    }
}

static void client_destroy(struct client_player *p, int x, int y, int z)
{
    int id = world_get_block(p->e.world, x, y, z) & 4095;
    if (id == 0) return;

    /* onPlayerDestroyBlock's playAuxSFX(2001), before setBlockToAir: the 64
     * break particles and their Det draws */
    surv_fx_ensure(p);
    int meta = world_get_meta(p->e.world, x, y, z);
    particles_live_destroy(&surv_fx, x, y, z, id, meta & 15);

    world_set_block(p->e.world, x, y, z, 0, 0, 0);
    /* Chunk.func_150807_a on the client: the old block's tile entity leaves
     * at once (removeTileEntity), before the tick's updateEntities (a broken
     * spawner's client half ran once more; seed-1 S22 row 2183) */
    if (p->pickobj != NULL) client_te_cell(&p->pickobj->ctes, NULL, x, y, z, id, meta, 0, 0);

    struct surv_stack *held = &p->sv.inv[p->hotbar];
    if (held->count <= 0) return;

    /* the held item's onBlockDestroyed: ItemShears wears 1 on leaves, web,
     * tall grass, vines and tripwire whatever their hardness; ItemTool 1 and
     * ItemSword 2 on a block whose hardness is not 0 */
    int kind = ITEMS[held->item].kind;
    const char *cls = ITEMS[held->item].class_name;
    int damage;
    if (cls != NULL && !strcmp(cls, "ItemShears"))
        damage = !strcmp(MATERIALS[BLOCKS[id].material].name, "leaves") || id == 30 || id == 31 || id == 106 ||
                 id == 132;
    else if (BLOCKS[id].hardness == 0.0F) return;
    else damage = kind == ITEM_TOOL ? 1 : (kind == ITEM_SWORD ? 2 : 0);
    if (damage == 0) return;
    /* ItemStack.damageItem on the client's copy (the Unbreaking rolls on the
     * client player's Random, the stack's tag being the S2F's), then
     * onPlayerDestroyBlock's destroyCurrentEquippedItem of a used-up stack */
    if (client_damage_stack(p, held, damage) && held->count == 0) *held = empty_stack();
}

/* The client's getCurrentPlayerStrVsBlock inputs: the held stack and its
 * Efficiency, the ground flag, and isInsideOfMaterial(water) at
 * EntityPlayer.getEyeHeight's 0.12 over the client's posY, cancelled by
 * Aqua Affinity. Haste and Mining Fatigue are not read on the client path. */
static void client_harvest_player(const struct client_player *p, struct harvest_player *hp)
{
    const struct surv_stack *held = &p->sv.inv[p->hotbar];

    memset(hp, 0, sizeof *hp);
    /* an empty slot (item -1) is getCurrentItem() == null: no held item */
    hp->held_item = held->item < 0 ? 0 : held->item;
    hp->held_enchant = surv_ench_level(held, 32);
    hp->on_ground = p->e.on_ground;
    hp->in_water = inside_of_material(p->e.world, &p->e, 0.12F, MAT_WATER) && !aqua_affinity(&p->sv);
}

/* BlockRedstoneOre.func_150185_e on the client world (clicked, walked on,
 * used): func_150186_m's particle rows on WorldClient.rand, then the unlit
 * ore lights in the client's own copy */
static void client_redstone_touch(struct client_player *p, int x, int y, int z, int id)
{
    particles_live_redstone_sparkle(surv_client_fx(p), x, y, z);
    if (id == 73) world_set_block(p->e.world, x, y, z, 74, 0, 3);
}

/* BlockDragonEgg.func_150019_m on the client world (clicked or used): the
 * teleport search on WorldClient.rand, and at the first air cell the 128
 * portal particles between the two cells instead of the move */
static void client_dragon_egg(struct client_player *p, int x, int y, int z)
{
    struct world *w = p->e.world;
    if ((world_get_block(w, x, y, z) & 4095) != 122 || !p->cw_rand_known) return;
    det_rng *r = &p->cw_rand;
    struct particles_live *pl = surv_client_fx(p);
    for (int var5 = 0; var5 < 1000; ++var5)
    {
        int var6 = x + det_rng_int_n(r, 16);
        var6 -= det_rng_int_n(r, 16);
        int var7 = y + det_rng_int_n(r, 8);
        var7 -= det_rng_int_n(r, 8);
        int var8 = z + det_rng_int_n(r, 16);
        var8 -= det_rng_int_n(r, 16);
        int id = var7 >= 0 && var7 < 256 ? world_get_block(w, var6, var7, var8) & 4095 : 0;
        if (BLOCKS[id].exists && BLOCKS[id].material != 0) continue;
        for (int var10 = 0; var10 < 128; ++var10)
        {
            double var11 = det_rng_double(r);
            float var13 = (det_rng_float(r) - 0.5F) * 0.2F;
            float var14 = (det_rng_float(r) - 0.5F) * 0.2F;
            float var15 = (det_rng_float(r) - 0.5F) * 0.2F;
            double var16 = (double)var6 + (double)(x - var6) * var11 + (det_rng_double(r) - 0.5) * 1.0 + 0.5;
            double var18 = (double)var7 + (double)(y - var7) * var11 + det_rng_double(r) * 1.0 - 0.5;
            double var20 = (double)var8 + (double)(z - var8) * var11 + (det_rng_double(r) - 0.5) * 1.0 + 0.5;
            (void)particles_live_spawn(pl, "portal", var16, var18, var20, (double)var13, (double)var14,
                                       (double)var15);
        }
        return;
    }
}

/* Block.onBlockClicked on the client world: the bodies that act there */
static void client_block_clicked(struct client_player *p, int id, int x, int y, int z)
{
    if (id == 73 || id == 74) client_redstone_touch(p, x, y, z, id);
    else if (id == 122) client_dragon_egg(p, x, y, z);
}

/* the client player's Block.onEntityWalking (Entity.moveEntity's step) */
static void cp_walking_block(void *self, int x, int y, int z, int id)
{
    if (id == 73 || id == 74) client_redstone_touch(self, x, y, z, id);
}

void surv_client_bind_walking(struct client_player *p)
{
    p->e.walking_block = cp_walking_block;
}

/* PlayerControllerMP.clickBlock, the survival paths (no creative, no
 * adventure): the abandon C07 1, the begin C07 0, then either the instant
 * destroy (a relative hardness of 1 or more) or the begin. The block's
 * onBlockClicked and the destroy itself are the server's C07 processing. */
static void surv_client_click_block(struct client_player *p, struct client_out *out)
{
    int same = surv_same_tool_and_block(p) &&
        surv_dig.cur_x == surv_mo.cur.x && surv_dig.cur_y == surv_mo.cur.y &&
        surv_dig.cur_z == surv_mo.cur.z;

    /* a press on the block already being hit (the attack button let go and
     * pressed again in one dig) changes nothing */
    if (surv_dig.is_hitting && same) return;

    if (surv_dig.is_hitting && !same)
    {
        /* C07PacketPlayerDigging(1, currentBlockX/Y/Z, side): the abandon */
        surv_c07(out, 1, surv_dig.cur_x, surv_dig.cur_y, surv_dig.cur_z, surv_mo.cur.side);
    }

    surv_c07(out, 0, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z, surv_mo.cur.side);

    int id = world_get_block(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z) & 4095;
    /* the block's onBlockClicked, at no damage yet */
    if (id != 0 && surv_dig.cur_damage == 0.0F)
    {
        client_block_clicked(p, id, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z);
        id = world_get_block(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z) & 4095;
    }
    int solid = id != 0 && BLOCKS[id].hardness >= 0.0F;

    struct harvest_player hp;
    client_harvest_player(p, &hp);
    float rel = solid ? player_relative_block_hardness(&hp, id) : 0.0F;

    if (solid && rel >= 1.0F)
    {
        /* onPlayerDestroyBlock right away, no C07 2 (the server's
         * onBlockClicked breaks what it finds instant by its own reckoning) */
        client_destroy(p, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z);
        surv_dig.is_hitting = 0;
        surv_dig.cur_damage = 0.0F;
        surv_dig.step_counter = 0.0F;
        surv_dig.cur_y = -1;   /* onPlayerDestroyBlock's currentBlockY = -1 */
        return;
    }

    surv_dig.is_hitting = 1;
    surv_dig.cur_x = surv_mo.cur.x;
    surv_dig.cur_y = surv_mo.cur.y;
    surv_dig.cur_z = surv_mo.cur.z;
    surv_dig.held_item = p->sv.inv[p->hotbar].item;
    surv_dig.held_damage = p->sv.inv[p->hotbar].damage;
    surv_dig.held_tag = p->sv.inv[p->hotbar].tag;
    surv_dig.cur_damage = 0.0F;
    surv_dig.step_counter = 0.0F;
}

/* PlayerControllerMP.onPlayerDamageBlock, the per-tick held path. Its
 * syncCurrentPlayItem comes first: a hotbar change made this tick reaches the
 * server as a C09 ahead of the dig's C07s. */
static void surv_client_damage_block(struct client_player *p, struct client_out *out)
{
    sync_current_play_item(p, out);

    if (surv_dig.hit_delay > 0)
    {
        --surv_dig.hit_delay;
        return;
    }

    int same = surv_same_tool_and_block(p) &&
        surv_dig.cur_x == surv_mo.cur.x && surv_dig.cur_y == surv_mo.cur.y &&
        surv_dig.cur_z == surv_mo.cur.z;

    if (same)
    {
        int id = world_get_block(p->e.world, surv_dig.cur_x, surv_dig.cur_y, surv_dig.cur_z) & 4095;

        if (id == 0)
        {
            surv_dig.is_hitting = 0;
            return;
        }

        struct harvest_player hp;
        client_harvest_player(p, &hp);
        surv_dig.cur_damage += player_relative_block_hardness(&hp, id);
        surv_dig.step_counter += 1.0F;

        if (surv_dig.cur_damage >= 1.0F)
        {
            surv_dig.is_hitting = 0;
            surv_c07(out, 2, surv_dig.cur_x, surv_dig.cur_y, surv_dig.cur_z, surv_mo.cur.side);
            client_destroy(p, surv_dig.cur_x, surv_dig.cur_y, surv_dig.cur_z);
            surv_dig.cur_damage = 0.0F;
            surv_dig.step_counter = 0.0F;
            surv_dig.hit_delay = 5;
            surv_dig.cur_y = -1;   /* onPlayerDestroyBlock's currentBlockY = -1 */
        }
    }
    else
    {
        surv_client_click_block(p, out);
    }
}

/* Minecraft.func_147115_a(attack held): once per tick, after the consumers.
 * The flag is the held attack key: not held, only the reset runs. */
void surv_client_dig_tick(struct client_player *p, int attack_held, struct client_out *out)
{
    if (!attack_held) p->left_click_counter = 0;

    if (p->left_click_counter > 0) return;

    if (attack_held && surv_mo.cur.num)
    {
        int id = world_get_block(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z) & 4095;

        if (id != 0)
        {
            surv_client_damage_block(p, out);
            /* isCurrentToolAdventureModeExempt holds in survival: the hit
             * particles, then the swing */
            surv_fx_ensure(p);
            particles_live_hit(&surv_fx, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z,
                               surv_mo.cur.side);
            client_swing_item(p);
        }
        /* a block the mouseover saw, gone to air since (this runTick's
         * packets or a neighbour's drop): neither the damage nor the reset */
        return;
    }

    /* resetBlockRemoving, when a dig was in progress */
    if (surv_dig.is_hitting) surv_c07(out, 1, surv_dig.cur_x, surv_dig.cur_y, surv_dig.cur_z, surv_mo.cur.side);

    surv_dig.is_hitting = 0;
    surv_dig.cur_damage = 0.0F;
}

/* The attack press (func_147116_af), before the held tick's own step. */
void surv_client_attack_pressed(struct client_player *p, struct client_out *out)
{
    if (p->left_click_counter > 0) return;
    ++out->nc0a;
    client_swing_item(p);
    if (surv_mo.cur.entity_id)
    {
        sync_current_play_item(p, out);
        client_pkt_add(out, CPK_C02_ATTACK)->v = surv_mo.cur.entity_id;
        /* PlayerControllerMP.attackEntity's local attack: on a crystal copy
         * attackEntityFrom is true, so a sprinting player takes the
         * knockback's own half; the large fireball's deflect is combat.c's */
        int knock = 0;
        if (pickobj_client_attack(p, surv_mo.cur.entity_id, &knock))
        {
            if (knock)
            {
                p->e.motion_x *= 0.6;
                p->e.motion_z *= 0.6;
                player_client_set_sprinting(p, 0);
            }
        }
        else combat_client_attack(p, surv_mo.cur.entity_id);
        return;
    }
    if (surv_mo.cur.num)
    {
        int id = world_get_block(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z) & 4095;

        if (id != 0)
        {
            surv_client_click_block(p, out);
            return;
        }
    }
    if (!surv_mo.cur.is_miss) p->left_click_counter = 10;
}

/* The use press (func_147121_ag) over a block: the C08 block form. The
 * crafting table's client stack decrement is predicted in this row; the
 * server's placed block reaches the client copy in the next row. */

/* Block.onBlockActivated's answer on the client world (World.isRemote), for
 * every block that overrides it; the base Block answers false. Only the
 * answer: the door, trapdoor and gate toggles are client_toggle_openable's,
 * the cake's and the pot's are surv_client_use_pressed's, and the other
 * client-side writes these bodies make (a button's or a repeater's meta,
 * the redstone ore's glow) are not predicted. The
 * enchanting table, anvil, brewing stand and beacon are plain blocks while
 * their config.yaml switches are off, which native keeps (WorldConf.UNSUPPORTED). */
static int client_block_activated(struct client_player *p, int id, int meta, int x, int y, int z)
{
    const struct surv_stack *held = &p->sv.inv[p->hotbar];

    switch (id)
    {
        /* isRemote: true */
        case 23: case 158:                      /* dispenser, dropper */
        case 25:                                /* note block */
        case 26:                                /* bed */
        case 54: case 146:                      /* chest, trapped chest */
        case 58:                                /* crafting table */
        case 61: case 62:                       /* furnace */
        case 69:                                /* lever */
        case 85: case 113:                      /* BlockFence: isRemote ? true */
        case 118:                               /* cauldron */
        case 154:                               /* hopper */
            return 1;
        /* true on either side */
        case 64: case 71:                       /* doors (an iron one answers true untouched) */
        case 77: case 143:                      /* buttons */
        case 92:                                /* cake */
        case 93: case 94:                       /* repeater */
        case 96:                                /* trapdoor */
        case 107:                               /* fence gate */
        case 122:                               /* dragon egg */
        case 130:                               /* ender chest: every branch */
        case 137:                               /* command block */
        case 149: case 150:                     /* comparator */
            return 1;
        case 84:                                /* jukebox: a disc inside */
            return meta != 0;
        case 46:                                /* TNT: flint and steel */
            return held->count > 0 && held->item == 259;
        case 140:                               /* flower pot: an empty pot and a plant */
        {
            if (held->count <= 0 || ITEMS[held->item].kind != ITEM_BLOCK) return 0;
            struct tile_entity *te = world_tile_entity(p->e.world, x, y, z);
            if (te == NULL || te->kind != TE_FLOWER_POT || te->u.pot.item > 0) return 0;
            int b = ITEMS[held->item].block_id;
            return b == 37 || b == 38 || b == 81 || b == 39 || b == 40 || b == 6 || b == 32 ||
                   (b == 31 && held->damage == 2);
        }
        default:
            /* the stairs answer for their model block (planks, cobblestone,
             * bricks, stone bricks, nether brick, sandstone, quartz): false;
             * pistons, redstone ore (after its glow) and the switched-off
             * blocks: false */
            return 0;
    }
}

/* BlockDoor, BlockTrapDoor and BlockFenceGate.onBlockActivated on the client
 * world: the metadata toggle (flag 2) happens there, before the C08 and
 * before this tick's move, so the client player collides with the opened
 * (or closed) block in the same row; the server's S23 later writes the same
 * value. The iron door only answers true. playAuxSFXAtEntity(1003) reaches
 * RenderGlobal.playAuxSFX on the client: one Math.random for the open or
 * close sound, then the client world's rand for its pitch. */
static void client_toggle_openable(struct client_player *p, int id, int x, int y, int z)
{
    struct world *w = p->e.world;

    if (id == 64)
    {
        /* func_150012_g: the lower half's meta, the upper flag */
        int m = world_get_meta(w, x, y, z);
        int upper = (m & 8) != 0;
        int lower = upper ? world_get_meta(w, x, y - 1, z) : m;
        int var11 = (lower & 7) ^ 4;

        world_set_meta(w, x, upper ? y - 1 : y, z, var11, 2);
    }
    else if (id == 96)
    {
        world_set_meta(w, x, y, z, world_get_meta(w, x, y, z) ^ 4, 2);
    }
    else if (id == 107)
    {
        int var10 = world_get_meta(w, x, y, z);

        if ((var10 & 4) != 0)
            world_set_meta(w, x, y, z, var10 & -5, 2);
        else
        {
            int var11 = mh_floor((double)(p->rotation_yaw * 4.0F / 360.0F) + 0.5) & 3;

            if ((var10 & 3) == (var11 + 2) % 4) var10 = var11;
            world_set_meta(w, x, y, z, var10 | 4, 2);
        }
    }
    else return;

    (void)det_math_random_role(surv.det, DET_CLIENT);
    if (p->cw_rand_known) (void)det_rng_float(&p->cw_rand);
}

/* The client prediction's entity check (World.checkNoEntityCollision on the
 * client world): the client player's own box (not for ItemBlock.onItemUse, which
 * excludes its placer), then the client's copies of the other entities that
 * set preventEntitySpawning. */
static int surv_client_entity_in(void *ctx, struct world *w, const struct aabb *box, int skip_placer)
{
    const struct client_player *p = ctx;
    (void)w;
    return (!skip_placer && aabb_intersects(box, &p->e.bounding_box)) || combat_client_prevents(p, box);
}

int surv_client_use_pressed(struct client_player *p, struct client_out *out)
{
    if (!surv_mo.cur.num) return 0;

    int id = world_get_block(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z) & 4095;

    if (id == 0) return 0;

    struct surv_stack *held = &p->sv.inv[p->hotbar];
    /* player.isSneaking(): the movement input's sneak as the last living
     * update left it (the click runs ahead of this tick's) */
    int may_activate = !(p->in_sneak && !p->sv.sleeping) || held->count == 0;
    int activated = may_activate &&
                    client_block_activated(p, id, world_get_meta(p->e.world,
                                      surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z),
                                      surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z);
    /* the bodies of onBlockActivated that act on the client world: the
     * redstone ore's touch (then false), the dragon egg's teleport search */
    if (may_activate) client_block_clicked(p, id, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z);

    /* BlockTNT.onBlockActivated on the client: flint and steel clears the
     * block locally and wears the held stack (the entity is the server's),
     * before the C08, which carries the worn stack */
    if (activated && id == 46)
    {
        world_set_block(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z, 0, 0, 2);
        /* Minecraft.func_147121_ag then drops a count-0 stack */
        if (client_damage_stack(p, held, 1) && held->count <= 0) *held = empty_stack();
    }

    /* BlockCake.func_150036_b's client half: the client runs its own
     * onBlockActivated (canEat(false): food < 20), adds the slice's stats and
     * the metadata step, and the server repeats it from the C08 */
    if (id == 92 && activated && p->sv.food.level < 20)
    {
        food_add_stats(&p->sv, 2, 0.1F);

        int meta = world_get_meta(p->e.world, surv_mo.cur.x, surv_mo.cur.y,
                                  surv_mo.cur.z);

        if (meta + 1 >= 6)
            world_set_block(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z, 0, 0, 3);
        else
            world_set_meta(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z,
                           meta + 1, 2);
    }

    if (activated) client_toggle_openable(p, id, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z);

    /* BlockFlowerPot.onBlockActivated's client half: the client plants the
     * held flower into an empty pot itself (TE content, meta step, the
     * stack consumed), then the server repeats it from the C08 */
    if (id == 140 && activated && held->count > 0 && ITEMS[held->item].exists
        && ITEMS[held->item].kind == ITEM_BLOCK)
    {
        static const int plantable[] = {37, 38, 81, 39, 40, 6, 32};

        int ok = 0;

        for (size_t k = 0; k < sizeof plantable / sizeof plantable[0]; ++k)
            ok |= ITEMS[held->item].block_id == plantable[k];

        if (ok || (ITEMS[held->item].block_id == 31 && held->damage == 2))
        {
            struct tile_entity *te = world_tile_entity(p->e.world, surv_mo.cur.x,
                                                       surv_mo.cur.y, surv_mo.cur.z);

            if (te != NULL && te->u.pot.item < 0)
            {
                te->u.pot.item = held->item;
                te->u.pot.data = held->damage;
                world_set_meta(p->e.world, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z,
                               held->damage, 2);

                if (--held->count <= 0) *held = empty_stack();
            }
        }
    }

    /* PlayerControllerMP.onPlayerRightClick: a held ItemBlock whose
     * func_150936_a refuses the target (the client world's
     * canPlaceEntityOnSide, the client's copies of the entities counted: a
     * baby zombie's copy keeps the adult box) answers false before the C08,
     * with no swing; func_147121_ag then sends the in-air use */
    if (!activated && held->count > 0)
    {
        struct place_stack ps = {held->item, held->count, held->damage};
        void *prev_ctx;
        place_entity_hook prev_hook = place_get_entity_hook(&prev_ctx);
        place_set_entity_hook(surv_client_entity_in, p);
        int send = place_item_block_precheck(p->e.world, &ps, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z,
                                             surv_mo.cur.side);
        place_set_entity_hook(prev_hook, prev_ctx);
        if (!send) return 0;
    }

    {
        struct client_pkt *k = client_pkt_add(out, CPK_C08);
        k->v = p->sv.inv[p->hotbar].count > 0 ? 2 : 3;
        k->stack = p->sv.inv[p->hotbar];
        k->x = surv_mo.cur.x;
        k->y = surv_mo.cur.y;
        k->z = surv_mo.cur.z;
        k->side = surv_mo.cur.side;
        k->hx = surv_mo.cur.hx;
        k->hy = surv_mo.cur.hy;
        k->hz = surv_mo.cur.hz;
    }

    /* func_147121_ag swings when onPlayerRightClick answers true: the
     * block's onBlockActivated, else the held stack's tryPlaceItemIntoWorld
     * (an ItemBlock that cannot go on that side answers false before the
     * C08). A stack the placement shrinks resets the equip animation. The
     * items place_try does not carry answer from itemuse below. */
    int placed = 0;
    if (!activated && held->count > 0)
    {
        struct placer pl = {p->e.pos_x, p->e.pos_y, p->e.pos_z, p->rotation_yaw, p->rotation_pitch,
                            p->e.y_offset};
        struct place_stack ps = {held->item, held->count, held->damage};
        placed = place_would_use(p->e.world, &pl, &ps, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z,
                                 surv_mo.cur.side);
    }
    if (activated || placed > 0)
    {
        client_swing_item(p);
        if (placed == 2) p->equip_reset = 1;
    }

    /* ItemBlock.tryPlaceItemIntoWorld decrements the client's stack when the
     * block is placed (the table, or any stack place_would_use says the
     * placement shrinks). The server's block write reaches this client copy
     * on the next row, but the inventory change is visible in this row. */

    if (activated) return 1;

    /* ItemHangingEntity.onItemUse on the client: the frame's or the
     * painting's constructor draws and onValidSurface on the client's side
     * of the world; a hung one spends the client's item, and the true
     * answer swings */
    if (held->count > 0 && (held->item == 389 || held->item == 321))
    {
        static const int facing_to_dir[6] = {-1, -1, 2, 0, 1, 3};
        int side = surv_mo.cur.side;
        if (side < 2 || side > 5) return 0;
        if (pickobj_client_hanging_fits(p, surv.det, held->item == 321 ? FH_PAINTING : FH_FRAME, surv_mo.cur.x,
                                        surv_mo.cur.y, surv_mo.cur.z, facing_to_dir[side]))
        {
            if (--held->count <= 0) *held = empty_stack();
            else
            {
                p->equip_reset = 1;
                held->gen = ++p->sv.gen_counter;
            }
        }
        client_swing_item(p);
        return 1;
    }

    /* ItemReed (string, the reeds, the cake, the pot), ItemRedstone (the
     * dust), ItemDoor and the seeds (ItemSeeds, ItemSeedFood) place a block
     * through their own onItemUse, predicted like an ItemBlock's */
    int reed = held->count > 0 && ITEMS[held->item].class_name != NULL &&
               (!strcmp(ITEMS[held->item].class_name, "ItemReed") ||
                !strcmp(ITEMS[held->item].class_name, "ItemRedstone") ||
                surv_place_item_class(ITEMS[held->item].class_name));

    if (held->count > 0 && ITEMS[held->item].kind != ITEM_BLOCK && !reed)
    {
        struct iu_player ip = {p->e.pos_x, p->e.pos_y, p->e.pos_z,
                               p->e.y_offset, p->rotation_yaw, p->rotation_pitch, 1, 0};
        struct iu_stack stack = {held->item, held->count, held->damage};
        jrand local = {0};
        itemuse_set_live_det(surv.det, DET_CLIENT);
        int used = itemuse_activate_block_or_use_item(p->e.world, &ip, &stack,
                          surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z,
                          surv_mo.cur.side, surv_mo.cur.hx, surv_mo.cur.hy,
                          surv_mo.cur.hz, p->cw_rand_known ? &p->cw_rand.r : &local);
        itemuse_set_live_det(NULL, DET_CLIENT);
        if (used && placed < 0) client_swing_item(p);
        if (stack.item != held->item || stack.count != held->count || stack.damage != held->damage)
        {
            if (stack.item != held->item) held->tag = 0;   /* a new ItemStack */
            held->item = stack.item; held->count = stack.count; held->damage = stack.damage;
            held->gen = ++p->sv.gen_counter;
        }
        /* ItemFlintAndSteel.onItemUse's damageItem(1, player) on the client
         * copy; Minecraft.func_147121_ag drops a count-0 stack */
        if (used && held->item == 259 && client_damage_stack(p, held, 1) && held->count <= 0)
            *held = empty_stack();
        return used;
    }

    /* ItemBlock.onItemUse on the client world: PlayerControllerMP
     * .onPlayerRightClick's survival branch runs the placement predictively
     * and the success shrinks the client stack this row (the torch). The
     * server's own copy shrinks when the C08 arrives. A torch's placement
     * draws nothing from any Random; the case seed 0 only backs paths that
     * would (the rails, the reed), which no current client-side placement
     * reaches. */
    if (held->count > 0 && (ITEMS[held->item].kind == ITEM_BLOCK || reed) && held->item != 111)
    {
        struct place_stack ps = {held->item, held->count, held->damage};
        struct placer pl = {p->e.pos_x, p->e.pos_y, p->e.pos_z, p->rotation_yaw, p->rotation_pitch,
                            p->e.y_offset};
        place_set_case(0, surv.det, &surv.det->math[DET_CLIENT], NULL);
        /* the client world's canPlaceEntityOnSide sees the client player */
        void *prev_ctx;
        place_entity_hook prev_hook = place_get_entity_hook(&prev_ctx);
        place_set_entity_hook(surv_client_entity_in, p);
        /* the clicked cell and its six neighbours before the placement: the
         * placed block's client tile entity (clientworld.h) */
        static const int NB[7][3] = {{0, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};
        int before[7][2];
        for (int k = 0; k < 7; ++k)
        {
            int bx = surv_mo.cur.x + NB[k][0], by = surv_mo.cur.y + NB[k][1], bz = surv_mo.cur.z + NB[k][2];
            before[k][0] = world_get_block(p->e.world, bx, by, bz);
            before[k][1] = world_get_meta(p->e.world, bx, by, bz);
        }
        place_try(p->e.world, &pl, &ps, surv_mo.cur.x, surv_mo.cur.y, surv_mo.cur.z,
                  surv_mo.cur.side, surv_mo.cur.hx, surv_mo.cur.hy, surv_mo.cur.hz);
        place_set_entity_hook(prev_hook, prev_ctx);
        if (p->pickobj != NULL)
            for (int k = 0; k < 7; ++k)
            {
                int bx = surv_mo.cur.x + NB[k][0], by = surv_mo.cur.y + NB[k][1], bz = surv_mo.cur.z + NB[k][2];
                /* the placement's own func_150806_e drew the dispenser's
                 * Random (place.c) */
                client_te_cell(&p->pickobj->ctes, NULL, bx, by, bz, before[k][0], before[k][1],
                               world_get_block(p->e.world, bx, by, bz), world_get_meta(p->e.world, bx, by, bz));
            }

        if (ps.count != held->count)
        {
            held->count = ps.count;
            held->gen = ++p->sv.gen_counter;
            if (held->count <= 0) *held = empty_stack();
            return 1;
        }
        return 0;
    }
    return 0;
}

void surv_sync_inventory(struct server_player *p)
{
    /* inventoryContainer.detectAndSendChanges, whichever window is open:
     * the inventory container's own list, window 0's numbering */
    if (!p->sv.mirror0_valid)
    {
        struct container *open = p->open_container;
        p->open_container = &p->own_container;
        detect_and_send_changes(p);
        p->open_container = open;
        return;
    }
    for (int s = 0; s < 45; ++s)
    {
        struct surv_stack st = server_slot_stack(p, s);
        if (stack_eq(&st, &p->sv.mirror0[s])) continue;
        p->sv.mirror0[s] = st;
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S2F;
        pkt->slot = s;
        pkt->i0 = st.item;
        pkt->i1 = st.damage;
        pkt->i2 = st.count;
        pkt->st = st;
    }
}
