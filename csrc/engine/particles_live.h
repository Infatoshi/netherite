/* The client's particles: EffectRenderer's fxLayers, each EntityFX subclass's
 * constructor and onUpdate, and the vanilla spawn sites that feed them, ticked
 * by the playable client and by the particle tick check (test_rendertick) over
 * the recorded scenes, whose "fx" block is the oracle's dump of the same list.
 *
 * Spawn sites: RenderGlobal.doSpawnParticle by name (the particleSetting gate
 * and the 16-block view distance gate), which Block.randomDisplayTick's
 * emitters (torches, furnaces, lit redstone ore, portals, end portals, ender
 * chests, mycelium, liquids, fire), EntityCrit2FX, EntityHugeExplodeFX,
 * EntityLavaFX, Explosion.doExplosionB and playAuxSFX (2002 the splash
 * potion, 2003 the eye of ender) reach; EntityRenderer.addRainParticles and
 * the dig flow's addBlockHitEffects and func_147215_a, which add their FX
 * directly.
 *
 * Each FX construction spends exactly the Det draws the oracle's client
 * spends: the Entity constructor's entity ID, its Random (one seeder long,
 * kept here for the FX's own draws) and its UUID (two seeder longs), then
 * the subclass constructor's Math.random and Random draws in source order.
 * The one unrecoverable state is EffectRenderer.rand's own stream (a single
 * Det.newRandom at startup, never digested): its three nextDouble draws place
 * the hit particles, so the live client seeds its copy locally.
 *
 * The list is one array in insertion order; each FX keeps its getFXLayer, and
 * updateEffects walks the array once per layer, which visits every layer's
 * FX in its own list order and reaches FX spawned during the pass, as the
 * four ArrayLists do. */
#ifndef NETHERITE_PARTICLES_LIVE_H
#define NETHERITE_PARTICLES_LIVE_H

#include "det.h"
#include "entity.h"
#include "jrand.h"
#include "world.h"

struct rb_mesher;

/* EffectRenderer.addEffect drops a layer's oldest FX past 4000 */
#define PLIVE_LAYER_CAP 4000
#define PLIVE_CAP (4 * PLIVE_LAYER_CAP)

enum
{
    PLIVE_DIGGING = 0,
    PLIVE_LARGE_EXPLODE,
    PLIVE_HUGE_EXPLODE,
    PLIVE_SMOKE,        /* EntitySmokeFX ("smoke", "largesmoke") */
    PLIVE_FLAME,        /* EntityFlameFX */
    PLIVE_REDDUST,      /* EntityReddustFX */
    PLIVE_PORTAL,       /* EntityPortalFX */
    PLIVE_AURA,         /* EntityAuraFX ("townaura", "depthsuspend") */
    PLIVE_RAIN,         /* EntityRainFX */
    PLIVE_SPLASH,       /* EntitySplashFX */
    PLIVE_EXPLODE,      /* EntityExplodeFX */
    PLIVE_CRIT,         /* EntityCritFX ("crit", "magicCrit") */
    PLIVE_CRIT2,        /* EntityCrit2FX, the emitter: draws nothing */
    PLIVE_SPELL,        /* EntitySpellParticleFX */
    PLIVE_BREAKING,     /* EntityBreakingFX ("iconcrack_") */
    PLIVE_LAVA,         /* EntityLavaFX */
    PLIVE_DROP,         /* EntityDropParticleFX ("dripWater", "dripLava") */
    PLIVE_SUSPEND,      /* EntitySuspendFX ("suspended") */
    PLIVE_ENCHANT,      /* EntityEnchantmentTableParticleFX ("enchantmenttable") */
    PLIVE_BUBBLE,       /* EntityBubbleFX ("bubble") */
    PLIVE_BLOCKDUST,    /* EntityBlockDustFX ("blockdust_"): an EntityDiggingFX */
    PLIVE_NOTE,         /* EntityNoteFX ("note") */
    PLIVE_HEART,        /* EntityHeartFX ("heart", "angryVillager") */
    PLIVE_KINDS
};

/* The dump's class name for a kind, and back (-1 for a class not ported). */
const char *particles_live_class(int kind);
int particles_live_kind(const char *cls);

struct live_fx
{
    struct entity e;            /* pos, prev pos, motion, box; moveEntity drives it */
    int kind;
    int layer;                  /* getFXLayer */
    int age, maxage;            /* particleAge, particleMaxAge */
    float scale, gravity;
    float red, green, blue, alpha;
    float jitx, jity;           /* particleTextureJitterX/Y */
    int tix, tiy;               /* particleTextureIndexX/Y */
    int has_icon;               /* particleIcon != null */
    int block_id, block_meta;   /* the digging icon's block, captured at spawn */
    int item_id, item_meta;     /* the breaking icon's item */
    float min_u, max_u, min_v, max_v;
    int brf;                    /* getBrightnessForRender, packed */
    /* EntityLargeExplodeFX: vx = field_70581_a, vq = field_70584_aq,
     * vs = field_70582_as. EntityHugeExplodeFX: tstart = timeSinceStart,
     * tmax = maximumTime. */
    int vx, vq;
    float vs;
    int tstart, tmax;
    /* the scale renderParticle animates from: smokeParticleScale, flameScale,
     * reddustParticleScale, portalParticleScale, initialParticleScale (crit),
     * lavaParticleScale */
    float base_scale;           /* ... noteParticleScale, particleScaleOverTime (heart) */
    double home_x, home_y, home_z;  /* EntityPortalFX.portalPosX/Y/Z,
                                     * EntityEnchantmentTableParticleFX's
                                     * field_70568_aq/ar/as */
    int spell_base;             /* EntitySpellParticleFX.baseSpellTextureIndex */
    int bob, lava;              /* EntityDropParticleFX.bobTimer, materialType is lava */
    int ent_id, life, mlife;    /* EntityCrit2FX: theEntity, currentLife, maximumLife */
    int magic;                  /* EntityCrit2FX.particleName is "magicCrit" */
    int dead;
    det_rng rand;               /* the FX's own Random */
};

/* The entity an EntityCrit2FX follows, as its onUpdate reads it: posX/Z,
 * boundingBox.minY, width, height. The caller answers for an entity id. */
struct plive_target
{
    double x, min_y, z;
    float width, height;
};
typedef int plive_target_fn(void *ctx, int id, struct plive_target *out);

struct particles_live
{
    struct world *world;
    /* the world whose light the FX are lit by (NULL: world); the playable
     * client's is the viewed server world, the one whose light is kept
     * current (its client world's cells are not relit) */
    struct world *light_world;
    det_state *det;
    struct rb_mesher *mesher;   /* the live mesher, for the block colours */
    det_rng effect_rand;        /* EffectRenderer.rand, seeded locally */
    /* Det's client consumer pin (det.h, the playable client's): the FX's
     * Math.random draws, their own Randoms' seeds and EffectRenderer.rand
     * come from the pinned streams at client tick pin_t; the shared client
     * streams are still drawn. Off (0) for the recorded scenes, whose oracle
     * predates the pin. */
    int pinned;
    int64_t pin_t;
    det_pin pin_math, pin_seed, pin_effect;
    int setting;                /* GameSettings.particleSetting: 0 all, 1 decreased, 2 minimal */
    int fancy;                  /* GameSettings.fancyGraphics (addRainParticles halves the rain without) */
    float rain;                 /* the world's rainingStrength (the leaves' drip under rain) */
    double view_x, view_y, view_z;  /* the render view entity's position, doSpawnParticle's gate */
    det_rng *world_rand;        /* WorldClient.rand: setting 1's draw and the "witchMagic" colour */
    plive_target_fn *target;    /* the EntityCrit2FX emitters' entities */
    /* World.spawnParticle's IWorldAccess fan-out, seen before RenderGlobal:
     * called with the name and the six arguments for every call a vanilla
     * World.spawnParticle makes here (not the ones inside playAuxSFX) */
    void (*on_spawn)(void *ctx, const char *name, const double a[6]);
    void *spawn_ctx;
    void *target_ctx;
    struct live_fx fx[PLIVE_CAP];
    int n;
};

void particles_live_init(struct particles_live *pl, struct world *w, det_state *det);

/* The consumer pin on from here (world_seed the tape's seed), at client tick
 * t: called before each tick; particles_live_init keeps it. */
void particles_live_pin(struct particles_live *pl, int64_t world_seed, int64_t t);

/* EffectRenderer.addBlockHitEffects: one EntityDiggingFX on the hit face. */
void particles_live_hit(struct particles_live *pl, int x, int y, int z, int side);

/* EffectRenderer.func_147215_a: the destroy prediction's 64 EntityDiggingFX. */
void particles_live_destroy(struct particles_live *pl, int x, int y, int z, int id, int meta);

/* World.spawnParticle("hugeexplosion"/"largeexplode") through doSpawnParticle's
 * early return: the specials, whatever particleSetting says. */
void particles_live_explosion(struct particles_live *pl, const char *kind,
                              double x, double y, double z, double mx, double my, double mz);

/* RenderGlobal.doSpawnParticle(name, x, y, z, vx, vy, vz): the FX it adds, or
 * NULL where it returns null (the setting, the distance, a name not ported;
 * iconcrack_ID_META is the only parameterised name). */
struct live_fx *particles_live_spawn(struct particles_live *pl, const char *name,
                                     double x, double y, double z, double vx, double vy, double vz);

/* Block.randomDisplayTick's particle overrides for block id at (x, y, z),
 * var5 the doVoidFogParticles Random; the ones that draw the world's own
 * Random (lit redstone ore) take pl->world_rand. */
void particles_live_display_tick(struct particles_live *pl, int id, int x, int y, int z, det_rng *var5);
/* The blocks whose randomDisplayTick particles_live_display_tick runs (its
 * chain of ids): any other block's does nothing and draws nothing from
 * var5, so a caller may skip the call (player.c's thousand cells a tick). */
static inline int particles_live_display_acts(int id)
{
    switch (id)
    {
    case 8: case 9: case 10: case 11: case 18: case 50: case 51: case 62: case 74: case 76:
    case 90: case 110: case 116: case 117: case 119: case 130: case 161:
        return 1;
    default:
        return 0;
    }
}

/* EntityCrit2FX(world, entity, "crit" or "magicCrit"): the S0B animation's
 * emitter; its constructor runs one onUpdate. */
void particles_live_crit(struct particles_live *pl, int entity_id, int magic);

/* RenderGlobal.playAuxSFX's particle halves: 2002 (the splash potion, data the
 * potion damage) and 2003 (the eye of ender), over pl->world_rand. */
void particles_live_aux(struct particles_live *pl, int id, int x, int y, int z, int data);

/* NetHandlerPlayClient.handleExplosion: the client's Explosion (isSmoking,
 * the constructor's) and its doExplosionB(true): the sound's two draws and
 * each affected block's five on WorldClient.rand when the client carries one
 * (the explode and smoke particles the block loop asks for are never made at
 * the minimal setting agent mode pins), and the huge or large explosion. */
/* RenderGlobal.playAuxSFX, all of it: the sounds' pitch draws on
 * pl->world_rand, 1003's Math.random pick, 2000's dispenser smoke, 2001's
 * block break (particles_live_destroy), 2002 and 2003 (particles_live_aux),
 * 2004's spawner burst, 2005's bonemeal (ItemDye.func_150918_a on
 * Item.itemRand) and 2006's fall dust. */
/* BlockRedstoneOre.func_150186_m over pl->world_rand (nothing without it). */
void particles_live_redstone_sparkle(struct particles_live *pl, int x, int y, int z);
void particles_live_aux_sfx(struct particles_live *pl, int id, int x, int y, int z, int data);
/* The S27's blocks whose dropBlockAsItemWithChance draws on the client
 * world too (BlockOre's and BlockRedstoneOre's xp roll, BlockMobSpawner's):
 * each one's index in the packet's list and its block id, in list order */
#define S27_XP_MAX 32
struct s27_xp { int index, block; };
void particles_live_explosion_packet(struct particles_live *pl, double x, double y, double z, float size,
                                     int naffected, const struct s27_xp *xp, int nxp);

/* Explosion.doExplosionB(true)'s particle half on the client: the huge or
 * large explosion, then each affected block's "explode" and "smoke" draws. */
void particles_live_explosion_b(struct particles_live *pl, double x, double y, double z, float size,
                                const int (*blocks)[3], int nblocks);

/* EntityRenderer.addRainParticles at rain strength var1 (getRainStrength(1)),
 * fancy graphics or not, over the renderer's own Random seeded by the
 * tick's rendererUpdateCount; the rain sound's draws included. */
void particles_live_rain(struct particles_live *pl, float rain, int fancy, int renderer_update_count,
                         int *rain_sound_counter);

/* EffectRenderer.updateEffects, after the entity updates in runTick. */
void particles_live_tick(struct particles_live *pl);

/* getBrightnessForRender at partial tick pt for every FX (the subclasses'
 * overrides included). */
void particles_live_light_pt(struct particles_live *pl, float pt);
void particles_live_light(struct particles_live *pl);

int particles_live_count(const struct particles_live *pl);
const struct live_fx *particles_live_get(const struct particles_live *pl, int i);

#endif
