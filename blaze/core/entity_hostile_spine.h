/* entity_hostile_spine.h - map hostile AI decisions onto EntityLivingBase intents for entities_world.
 *
 * The five hand-rolled mob AI state machines (zombie/skeleton/creeper/spider/enderman) decide WHAT
 * the mob wants: chase waypoint, face target, stop to melee/swell/draw bow. Body motion is NOT
 * their kinematic grid teleport - it goes through living_base.h (eb_tick_living): gravity, drag,
 * moveRelative, and pcf_entity_move swept AABB. Intents are POD (CUDA-safe, no function pointers).
 *
 * landMovementFactor values are oracle applyEntityAttributes MOVEMENT_SPEED (1.11.2):
 *   zombie 0.23, skeleton 0.25, creeper 0.25, spider 0.3, enderman 0.3.
 *
 * READ-ONLY deps: living_base.h (EntityLivingBase spine), ew_entity_store.h (type tags / store).
 * Used by entities_world.h only; the isolated mob_ai_*.h goldens stay standalone.
 */
#ifndef MC_ENTITY_HOSTILE_SPINE_H
#define MC_ENTITY_HOSTILE_SPINE_H

#include "living_base.h"
#include "ew_entity_store.h"

/* Extend EW_TYPE_* (ew_entity_store.h has NONE/PLAYER/ZOMBIE). Keep ZOMBIE=2 stable.
 * Magma product roster ids must match game/entity_render.c ER_TYPE_* where models exist. */
enum {
    EW_TYPE_SKELETON = 3,
    EW_TYPE_CREEPER  = 4,
    EW_TYPE_SPIDER   = 5,
    EW_TYPE_ENDERMAN = 6,
    EW_TYPE_BLAZE    = 7,
    /* 8 crystal, 9 dragon reserved by magma dragon_live */
    EW_TYPE_SHEEP    = 10,
    EW_TYPE_PIG      = 11,
    EW_TYPE_COW      = 12,
    EW_TYPE_CHICKEN  = 13,
    EW_TYPE_SQUID    = 14,
    EW_TYPE_PIGMAN   = 15,   /* live type; render = zombie + pigman skin */
    EW_TYPE_WOLF     = 16,
    EW_TYPE_OCELOT   = 17,
    EW_TYPE_WITCH    = 23,
    EW_TYPE_GHAST    = 26,
    EW_TYPE_MAGMA    = 27,
    EW_TYPE_WITHER_SKELETON = 32,
    EW_TYPE_SLIME    = 35,
    EW_TYPE_SILVERFISH = 36,
    EW_TYPE_BOAT     = 37,
    EW_TYPE_CAVE_SPIDER = 39,
    EW_TYPE_VILLAGER = 40,
    EW_TYPE_ZOMBIE_VILLAGER = 41,
    /* 42..50 are render-only product view ids (shulker, firework, carts). */
    EW_TYPE_VINDICATOR = 51,
    EW_TYPE_EVOKER = 52,
    EW_TYPE_VEX = 53,
    /* 54 is the render-only Evoker-fangs view. */
    EW_TYPE_GUARDIAN = 55,
    EW_TYPE_ELDER_GUARDIAN = 56,
    EW_TYPE_IRON_GOLEM = 57,
    EW_TYPE_STRAY = 58,
    EW_TYPE_HUSK = 59,
    EW_TYPE_MOOSHROOM = 60,
    EW_TYPE_RABBIT = 61,
    EW_TYPE_POLAR_BEAR = 62,
    EW_TYPE_ENDERMITE = 63,
    EW_TYPE_SNOWMAN = 64,
    EW_TYPE_GIANT = 65,
    EW_TYPE_BAT = 24,
    EW_TYPE_LLAMA = 25,
    /* Product-local AbstractHorse tags. Java EntityList numeric ids are
     * translated only at the import/export boundary. */
    EW_TYPE_HORSE = 68,
    EW_TYPE_DONKEY = 69,
    EW_TYPE_MULE = 70,
    EW_TYPE_SKELETON_HORSE = 71,
    EW_TYPE_ZOMBIE_HORSE = 72
};

/* AI phase tags already in ew_entity_store (IDLE/CHASE/ATTACK). Skeleton "ranged hold" and
 * creeper "swell" both report as EW_AI_ATTACK in the dump (stop body, face player). */

typedef struct {
    float moveForward;   /* 1 walk, 0 stop; living spine multiplies by 0.98 internally */
    float moveStrafing;  /* skeleton bow-strafe; 0 for melee hostiles */
    int   isJumping;     /* spider leap proxy; 0 until jumpTicks is persisted on the store */
    float yaw;           /* rotationYaw degrees (MC: 0 = +Z) */
} EhsIntent;

MC_HD static inline int ehs_is_hostile(u8 type) {
    return type == EW_TYPE_ZOMBIE || type == EW_TYPE_ZOMBIE_VILLAGER
        || type == EW_TYPE_SKELETON || type == EW_TYPE_CREEPER
        || type == EW_TYPE_SPIDER || type == EW_TYPE_ENDERMAN
        || type == EW_TYPE_WITHER_SKELETON || type == EW_TYPE_BLAZE
        || type == EW_TYPE_WITCH
        || type == EW_TYPE_PIGMAN || type == EW_TYPE_GHAST
        || type == EW_TYPE_MAGMA || type == EW_TYPE_SLIME
        || type == EW_TYPE_SILVERFISH || type == EW_TYPE_CAVE_SPIDER
        || type == EW_TYPE_VINDICATOR || type == EW_TYPE_EVOKER
        || type == EW_TYPE_VEX || type == EW_TYPE_GUARDIAN
        || type == EW_TYPE_ELDER_GUARDIAN
        || type == EW_TYPE_STRAY || type == EW_TYPE_HUSK
        || type == EW_TYPE_ENDERMITE || type == EW_TYPE_GIANT;
}

/* SharedMonsterAttributes.MOVEMENT_SPEED base from oracle applyEntityAttributes. */
MC_HD static inline float ehs_land_speed(u8 type) {
    switch (type) {
    case EW_TYPE_ZOMBIE:   return 0.23000000417232513f;
    case EW_TYPE_ZOMBIE_VILLAGER: return 0.23000000417232513f;
    case EW_TYPE_PIGMAN:   return 0.23000000417232513f;
    case EW_TYPE_SKELETON: return 0.25f;                 /* AbstractSkeleton */
    case EW_TYPE_WITHER_SKELETON: return 0.25f;
    case EW_TYPE_CREEPER:  return 0.25f;
    case EW_TYPE_SPIDER:   return 0.30000001192092896f;
    case EW_TYPE_CAVE_SPIDER: return 0.30000001192092896f;
    case EW_TYPE_ENDERMAN: return 0.30000001192092896f;
    case EW_TYPE_BLAZE:    return 0.23000000417232513f;
    case EW_TYPE_WITCH:    return 0.25f;
    case EW_TYPE_MAGMA:    return 0.20000000298023224f;
    case EW_TYPE_SILVERFISH: return 0.25f;
    case EW_TYPE_SLIME:    return 0.2f;
    case EW_TYPE_VILLAGER: return 0.5f;
    case EW_TYPE_VINDICATOR: return 0.3499999940395355f;
    case EW_TYPE_EVOKER:   return 0.5f;
    case EW_TYPE_VEX:      return 0.7f;
    case EW_TYPE_GUARDIAN: return 0.5f;
    case EW_TYPE_ELDER_GUARDIAN: return 0.30000001192092896f;
    case EW_TYPE_WOLF:     return 0.30000001192092896f;
    case EW_TYPE_OCELOT:   return 0.30000001192092896f;
    case EW_TYPE_IRON_GOLEM: return 0.25f;
    case EW_TYPE_STRAY:    return 0.25f;
    case EW_TYPE_HUSK:     return 0.23000000417232513f;
    case EW_TYPE_MOOSHROOM:return 0.2f;
    case EW_TYPE_RABBIT:   return 0.30000001192092896f;
    case EW_TYPE_POLAR_BEAR:return 0.25f;
    case EW_TYPE_ENDERMITE:return 0.25f;
    case EW_TYPE_SNOWMAN:return 0.20000000298023224f;
    case EW_TYPE_GIANT: return 0.5f;
    case EW_TYPE_LLAMA: return 0.17499999701976776f;
    case EW_TYPE_HORSE: return 0.22499999403953552f;
    case EW_TYPE_DONKEY:
    case EW_TYPE_MULE: return 0.17499999701976776f;
    case EW_TYPE_SKELETON_HORSE:
    case EW_TYPE_ZOMBIE_HORSE: return 0.20000000298023224f;
    default:               return 0.23000000417232513f;
    }
}

/* Entity*.setSize defaults used by the living spine box.
 * Slime/magma size is scaled by the caller via ehs_size_scaled when known. */
MC_HD static inline void ehs_size(u8 type, float *width, float *height) {
    switch (type) {
    case EW_TYPE_SPIDER:
        *width = 1.4f; *height = 0.9f;
        break;
    case EW_TYPE_CAVE_SPIDER:
        *width = 0.7f; *height = 0.5f;
        break;
    case EW_TYPE_ENDERMAN:
        *width = 0.6f; *height = 2.9f;
        break;
    case EW_TYPE_WITHER_SKELETON:
        *width = 0.7f; *height = 2.4f;
        break;
    case EW_TYPE_BLAZE:
        *width = 0.6f; *height = 1.8f;
        break;
    case EW_TYPE_GHAST:
        *width = 4.0f; *height = 4.0f;
        break;
    case EW_TYPE_SILVERFISH:
        *width = 0.4f; *height = 0.3f;
        break;
    case EW_TYPE_SLIME:
    case EW_TYPE_MAGMA:
        /* size-2 default; live layer overrides via size[] */
        *width = 1.02f; *height = 1.02f;
        break;
    case EW_TYPE_BOAT:
        *width = 1.375f; *height = 0.5625f;
        break;
    case EW_TYPE_PIG:
        *width = 0.9f; *height = 0.9f;
        break;
    case EW_TYPE_SHEEP:
        *width = 0.9f; *height = 1.3f;
        break;
    case EW_TYPE_COW:
        *width = 0.9f; *height = 1.4f;
        break;
    case EW_TYPE_CHICKEN:
        *width = 0.4f; *height = 0.7f;
        break;
    case EW_TYPE_SQUID:
        *width = 0.8f; *height = 0.8f;
        break;
    case EW_TYPE_WOLF:
        *width = 0.6f; *height = 0.85f;
        break;
    case EW_TYPE_OCELOT:
        *width = 0.6f; *height = 0.7f;
        break;
    case EW_TYPE_VEX:
        *width = 0.4f; *height = 0.8f;
        break;
    case EW_TYPE_GUARDIAN:
        *width = 0.85f; *height = 0.85f;
        break;
    case EW_TYPE_ELDER_GUARDIAN:
        *width = 0.85f * 2.35f; *height = 0.85f * 2.35f;
        break;
    case EW_TYPE_VILLAGER:
        *width = 0.6f; *height = 1.95f;
        break;
    case EW_TYPE_IRON_GOLEM:
        *width = 1.4f; *height = 2.7f;
        break;
    case EW_TYPE_RABBIT:
        *width = 0.4f; *height = 0.5f;
        break;
    case EW_TYPE_POLAR_BEAR:
        *width = 1.3f; *height = 1.4f;
        break;
    case EW_TYPE_MOOSHROOM:
        *width = 0.9f; *height = 1.4f;
        break;
    case EW_TYPE_BAT:
        *width = 0.5f; *height = 0.9f;
        break;
    case EW_TYPE_ENDERMITE:
        *width = 0.4f; *height = 0.3f;
        break;
    case EW_TYPE_SNOWMAN:
        *width = 0.7f; *height = 1.9f;
        break;
    case EW_TYPE_GIANT:
        *width = 3.6f; *height = 11.7f;
        break;
    case EW_TYPE_LLAMA:
        *width = 0.9f; *height = 1.87f;
        break;
    case EW_TYPE_HORSE:
    case EW_TYPE_DONKEY:
    case EW_TYPE_MULE:
    case EW_TYPE_SKELETON_HORSE:
    case EW_TYPE_ZOMBIE_HORSE:
        *width = 1.3964844f; *height = 1.6f;
        break;
    default: /* zombie / skeleton / creeper / pigman */
        *width = 0.6f; *height = 1.95f;
        break;
    }
}

/* EntitySlime.setSlimeSize: width = height = 0.51000005 * size. */
MC_HD static inline void ehs_size_scaled(u8 type, int slime_size, float *width, float *height) {
    if ((type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA) && slime_size > 0) {
        float s = 0.51000005f * (float)slime_size;
        *width = s; *height = s;
        return;
    }
    ehs_size(type, width, height);
}

/* yaw (deg) so forward=1 moveRelative heads toward (dx,dz): dir = (-sin, cos). Same as ew_yaw_toward. */
MC_HD static inline float ehs_yaw_toward(double dx, double dz) {
    return (float)(atan2(-dx, dz) * 180.0 / MC_PI);
}

/* Map AI SM decision -> living-base intents. Does NOT write position (no kinematic teleport).
 *
 * type nuances (collapsed onto IDLE/CHASE/ATTACK for the dump schema):
 *   zombie/spider/enderman ATTACK : stop, face player (melee windup)
 *   creeper ATTACK                : swell - stop, face player
 *   skeleton ATTACK               : ranged hold - stop (or mild backstrafe), face player
 *   CHASE/IDLE with moving=1      : walk toward path waypoint, face path
 *   moving=0                      : intents zeroed except yaw already set by caller path
 */
MC_HD static inline void ehs_intent_from_ai(u8 type, u32 ai_state, int moving,
                                           double x, double z,
                                           double path_tx, double path_tz,
                                           double px, double pz,
                                           EhsIntent *out) {
    out->moveForward = 0.0f;
    out->moveStrafing = 0.0f;
    out->isJumping = 0;
    out->yaw = 0.0f;

    if (ai_state == EW_AI_ATTACK) {
        out->yaw = ehs_yaw_toward(px - x, pz - z);
        out->moveForward = 0.0f;
        /* Skeleton EntityAIAttackRangedBow: hold position while drawing; optional strafe. */
        if (type == EW_TYPE_SKELETON) {
            out->moveStrafing = 0.0f;
            out->moveForward = 0.0f;
        }
        (void)path_tx; (void)path_tz; (void)moving;
        return;
    }

    if (moving) {
        double dx = path_tx - x;
        double dz = path_tz - z;
        out->yaw = ehs_yaw_toward(dx, dz);
        out->moveForward = 1.0f;
        /* Spider leap is a motionY impulse in the isolated AI kernel; without jumpTicks on the
         * store we leave isJumping=0 so the spine does not spam jump every tick. */
        out->isJumping = 0;
        (void)type;
        (void)px; (void)pz;
        return;
    }

    /* Stationary non-attack (should not be common): face path if any. */
    out->yaw = ehs_yaw_toward(path_tx - x, path_tz - z);
}

/* Hydrate an EbLiving from the SoA store slot, apply intents + type speed, ready for eb_tick_living. */
MC_HD static inline void ehs_load_living(EbLiving *liv, const EwStore *s, int i,
                                         const EhsIntent *intent) {
    float w, h;
    u8 type = s->type[i];
    ehs_size(type, &w, &h);
    elb_init(liv, w, h, s->x[i], s->y[i], s->z[i]);
    liv->base.phys.motionX = s->vx[i];
    liv->base.phys.motionY = s->vy[i];
    liv->base.phys.motionZ = s->vz[i];
    liv->base.phys.onGround = s->on_ground[i] ? 1 : 0;
    liv->base.rotationYaw = intent->yaw;
    liv->landMovementFactor = ehs_land_speed(type);
    liv->jumpMovementFactor = 0.02f;
    liv->isServerWorld = 1;
    liv->moveForward = intent->moveForward;
    liv->moveStrafing = intent->moveStrafing;
    liv->isJumping = intent->isJumping;
    liv->jumpTicks = 0;
}

/* Write living spine results back into the SoA next buffer. */
MC_HD static inline void ehs_store_living(EwStore *s, int i, const EbLiving *liv) {
    s->x[i] = liv->base.phys.posX;
    s->y[i] = liv->base.phys.posY;
    s->z[i] = liv->base.phys.posZ;
    s->vx[i] = liv->base.phys.motionX;
    s->vy[i] = liv->base.phys.motionY;
    s->vz[i] = liv->base.phys.motionZ;
    s->on_ground[i] = (u8)(liv->base.phys.onGround ? 1 : 0);
    s->yaw[i] = liv->base.rotationYaw;
}

#endif /* MC_ENTITY_HOSTILE_SPINE_H */
