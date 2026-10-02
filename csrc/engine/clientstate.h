/* The client-side state that changes what a frame shows (work item C5), each
 * piece stepped once per client tick the way Minecraft.runTick reaches it:
 *
 *   updateController's packets: S06 (EntityPlayerSP.setPlayerSPHealth), S19
 *     (EntityLivingBase.handleHealthUpdate), S0B animation 1
 *     (performHurtAnimation), S20 (the movementSpeed attribute), S2C (a client
 *     EntityLightningBolt, WorldClient.addWeatherEffect);
 *   updateRenderer: updateFovModifierHand over AbstractClientPlayer's
 *     getFOVMultiplier (EntityPlayerSP.getFOVMultiplier in 1.7.10);
 *   runTick's lastLightningBolt countdown, then updateEntities: the weather
 *     effects (EntityLightningBolt.onUpdate's client branch), then each living
 *     entity's onEntityUpdate (hurtTime, hurtResistantTime) and the player's
 *     onLivingUpdate (the portal timer and its trigger-sound draw).
 *
 * Checked between RenderStateProbe frame rows by test_rendertick (its "cs"
 * block, oracle/harness/netherite/oracle/ClientStateProbe.java); the live client
 * (play/play.c) runs the same functions. Nothing here allocates. */
#ifndef NETHERITE_CLIENTSTATE_H
#define NETHERITE_CLIENTSTATE_H

#include <stdint.h>

#include "det.h"
#include "jrand.h"

/* ------------------------------------------------------------------ FOV */

/* What getFOVMultiplier reads: capabilities.isFlying and getWalkSpeed(), the
 * movementSpeed attribute's value, and the item in use (a bow, and
 * getItemInUseDuration). */
struct cs_fov_in {
    int flying;
    float walk_speed;       /* PlayerCapabilities.walkSpeed, 0.1F */
    double move_speed;      /* getEntityAttribute(movementSpeed).getAttributeValue() */
    int bow;                /* isUsingItem() && getItemInUse().getItem() == Items.bow */
    int use_duration;       /* getItemInUseDuration() */
};

float clientstate_fov_multiplier(const struct cs_fov_in *in);

/* EntityRenderer.updateFovModifierHand: the smoothed multiplier the
 * projection reads (fovModifierHand and its previous value). */
void clientstate_fov_hand(float *fmh, float *fmhp, float mult);

/* The client's movementSpeed attribute instance as the S20 handler and
 * setSprinting leave it: the base and the modifiers in the order they were
 * applied (ModifiableAttributeInstance.computeValue walks operation 0, 1, 2,
 * each in that order). */
#define CS_ATTR_MODS 8
struct cs_attr_mod { int64_t msb, lsb; int op; double amount; };
struct cs_attr {
    double base;
    int n;
    struct cs_attr_mod m[CS_ATTR_MODS];
};

double clientstate_attr_value(const struct cs_attr *a);
/* applyModifier (a modifier already on by UUID is refused, as Java throws)
 * and removeModifier */
void clientstate_attr_apply(struct cs_attr *a, const struct cs_attr_mod *m);
void clientstate_attr_remove(struct cs_attr *a, int64_t msb, int64_t lsb);
/* EntityLivingBase.setSprinting's attribute half: the unsaved "Sprinting
 * speed boost" (0.30000001192092896, operation 2) on or off */
void clientstate_attr_sprint(struct cs_attr *a, int sprinting);
/* The modifier Potion.applyAttributesModifiersToEntity makes for the speed
 * (id 1) or slowness (id 2) effect at AMPLIFIER; 0 for any other id. */
int clientstate_potion_speed_mod(int id, int amplifier, struct cs_attr_mod *out);

/* --------------------------------------------------------------- portal */

/* EntityPlayerSP's timeInPortal/prevTimeInPortal and Entity's inPortal and
 * timeUntilPortal on the client player. */
struct cs_portal {
    float time, prev;
    int in_portal;
    int time_until;
};

/* Entity.setInPortal from BlockPortal.onEntityCollidedWithBlock (the
 * client's own moveEntity; EntityPlayer.getPortalCooldown is 10). */
void clientstate_set_in_portal(struct cs_portal *p);

/* EntityPlayerSP.onLivingUpdate's portal block. CONFUSION is the confusion
 * effect's duration, or -1 without one. Entering (timeInPortal still 0) plays
 * portal.trigger at rand.nextFloat() * 0.4F + 0.8F: one draw on the player's
 * own Random, made on RAND when it is not NULL. Returns the draws made. */
int clientstate_portal_tick(struct cs_portal *p, int confusion, jrand *rand);

/* ----------------------------------------------------------------- hurt */

/* EntityLivingBase's hurt fields as the client keeps them. */
struct cs_hurt {
    int hurt, max_hurt;         /* hurtTime, maxHurtTime */
    int res, max_res;           /* hurtResistantTime, maxHurtResistantTime (20) */
    float attacked_yaw;         /* attackedAtYaw */
    float limb;                 /* limbSwingAmount */
    float health, last_damage;
};

/* handleHealthUpdate (S19): status 2 is the hurt: limbSwingAmount 1.5, the
 * resistance window, hurtTime = maxHurtTime = 10, attackedAtYaw 0, and the
 * hurt sound's pitch, (rand.nextFloat() - rand.nextFloat()) * 0.2F + 1.0F:
 * two draws on the entity's own Random (RAND may be NULL). Status 3 (death)
 * draws the same two and sets the health to 0. Returns the draws made. */
int clientstate_health_update(struct cs_hurt *h, int status, jrand *rand);

/* performHurtAnimation (S0B animation 1): hurtTime = maxHurtTime = 10,
 * attackedAtYaw 0. */
void clientstate_hurt_animation(struct cs_hurt *h);

/* What EntityPlayer.damageEntity reads on the client besides the amount:
 * blocking with a sword, the total armour value, the resistance effect's
 * amplifier (-1 none), the absorption amount, and
 * EnchantmentHelper.getEnchantmentModifierDamage's result (0 without
 * protection enchantments; the caller makes its enchantmentRand draw). */
struct cs_damage_ctx {
    int blocking, armor, resistance;
    float absorption;
    int ench_mod;
    float max_health;
};

/* EntityPlayerSP.setPlayerSPHealth, behind EntityClientPlayerMP's first-call
 * guard (HAS_SET): a drop sets lastDamage, the resistance window and
 * hurtTime = maxHurtTime = 10 and runs damageEntity(generic, drop); a rise
 * sets the health and half the window. CTX may be NULL (no armour, no
 * effects). Absorption left after the hit is written back to CTX. */
void clientstate_set_sp_health(struct cs_hurt *h, float health, int *has_set, struct cs_damage_ctx *ctx);

/* EntityLivingBase.onEntityUpdate's countdowns (hurtTime, and
 * hurtResistantTime, which only EntityPlayerMP skips). */
void clientstate_hurt_tick(struct cs_hurt *h);

/* EntityLivingBase.moveEntityWithHeading's limb step from the tick's
 * horizontal move (posX - prevPosX, posZ - prevPosZ). */
void clientstate_limb_step(struct cs_hurt *h, double dx, double dz);

/* ------------------------------------------------------------ lightning */

/* A client EntityLightningBolt: lightningState, boltLivingTime, boltVertex,
 * its own Random. */
struct cs_bolt {
    int id;
    int state, living;
    int64_t vertex;
    jrand rand;
    int age;
};

#define CS_BOLTS 16
/* WorldClient.lastLightningBolt and weatherEffects (bolts only: nothing else
 * is a weather effect in 1.7.10). */
struct cs_weather {
    int last_bolt;
    int n;
    struct cs_bolt b[CS_BOLTS];
};

/* NetHandlerPlayClient.handleSpawnGlobalEntity type 1: new
 * EntityLightningBolt on the client (Entity's constructor takes a Random and
 * a UUID from DET's CLIENT seeder, then boltVertex = rand.nextLong() and
 * boltLivingTime = rand.nextInt(3) + 1; the client places no fire), then
 * setEntityId and addWeatherEffect. */
void clientstate_bolt_spawn(struct cs_weather *w, int id, det_state *det);

/* One client tick of the weather: runTick's lastLightningBolt countdown, then
 * updateEntities' weather-effects loop (ticksExisted, onUpdate, the dead
 * removed in place). */
void clientstate_weather_tick(struct cs_weather *w);

#endif
