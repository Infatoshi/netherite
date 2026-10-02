/* The chat lines the server sends the player (S02PacketChat), built as
 * vanilla builds their components (chatcomp.h): the death message
 * (EntityPlayerMP.onDeath over CombatTracker.func_151521_b), the bed's
 * refusals (BlockBed.onBlockActivated), the achievement announcement
 * (StatisticsFile.func_150873_a) and the build height refusal
 * (NetHandlerPlayServer.processPlayerBlockPlacement). */
#ifndef NETHERITE_CHATMSG_H
#define NETHERITE_CHATMSG_H

struct server_player;
struct chat_comp;

/* DamageSource.damageType of a CombatEntry's source, the names the death
 * keys are made of (death.attack.<type>). */
enum
{
    DT_IN_FIRE, DT_ON_FIRE, DT_LAVA, DT_IN_WALL, DT_DROWN, DT_STARVE, DT_CACTUS, DT_FALL,
    DT_OUT_OF_WORLD, DT_GENERIC, DT_MAGIC, DT_WITHER, DT_ANVIL, DT_FALLING_BLOCK,
    /* EntityDamageSource: causeMobDamage, causePlayerDamage, causeThornsDamage
     * and setExplosionSource with a placer */
    DT_MOB, DT_PLAYER, DT_THORNS, DT_EXPLOSION_PLAYER,
    /* EntityDamageSourceIndirect */
    DT_ARROW, DT_FIREBALL, DT_THROWN, DT_INDIRECT_MAGIC,
    /* setExplosionSource without a placer: a plain DamageSource */
    DT_EXPLOSION,
    DT_COUNT
};

/* The entity a source names (DamageSource.getEntity(), or an indirect
 * source's projectile when it has no shooter): a living kind (living.h;
 * SK_PLAYER and HK_PLAYER are the player), or one of these; -1 none. */
enum { CM_NAME_DRAGON = 64, CM_NAME_ARROW };

/* CombatEntry.func_94566_e, the fighter's surroundings at the hit
 * (CombatTracker.func_94545_a): none, a ladder, vines, water. */
enum { CM_LABEL_NONE, CM_LABEL_LADDER, CM_LABEL_VINES, CM_LABEL_WATER };

/* The damage type of a survival.h source (SURV_*) with a DamageSource
 * family from living.h (DMG_*, -1 unknown) and whether it names an
 * entity. */
int chatmsg_dtype(int surv_source, int dmg, int has_entity);

/* EntityPlayer.func_145748_c_ (the player's name with its /msg suggestion)
 * and Entity.func_145748_c_ for a name id: the component's slot. */
int chatmsg_player_name(struct chat_comp *t);
int chatmsg_entity_name(struct chat_comp *t, int name);

/* BlockBed.onBlockActivated's lines: tile.bed.occupied, noSleep, notSafe. */
enum { CM_BED_OCCUPIED, CM_BED_NO_SLEEP, CM_BED_NOT_SAFE };
void chatmsg_bed(struct server_player *p, int which);
/* StatisticsFile.func_150873_a's chat.type.achievement for achievement a. */
void chatmsg_achievement(struct server_player *p, int a);
/* processPlayerBlockPlacement's build.tooHigh (red), the build limit 256. */
void chatmsg_build_too_high(struct server_player *p);
/* EntityPlayerMP.onDeath's broadcast of CombatTracker.func_151521_b. */
void chatmsg_death(struct server_player *p);

/* func_94550_c's best living and best player over entity names, as a
 * CombatEntry lands (survival.c combat_entry). */
struct surv_combat;
struct surv_combat_entry;
void chatmsg_combat_add(struct surv_combat *c, const struct surv_combat_entry *e);

#endif
