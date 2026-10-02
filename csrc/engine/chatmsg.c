/* The chat lines the server sends the player (chatmsg.h). */
#include "chatmsg.h"

#include <stdio.h>
#include <string.h>

#include "chatcomp.h"
#include "envstack.h"
#include "living.h"
#include "player.h"
#include "survival.h"

/* the oracle's player (oracle/harness/netherite/oracle/Main.java: --username
 * Player); every session plays as it */
#define CM_PLAYER_NAME "Player"

static const char *const DT_NAME[DT_COUNT] = {
    "inFire", "onFire", "lava", "inWall", "drown", "starve", "cactus", "fall",
    "outOfWorld", "generic", "magic", "wither", "anvil", "fallingBlock",
    "mob", "player", "thorns", "explosion.player",
    "arrow", "fireball", "thrown", "indirectMagic",
    "explosion",
};

static int is_player_name(int name)
{
    return name == SK_PLAYER || name == HK_PLAYER;
}

/* instanceof EntityLivingBase: every name but a shooterless projectile */
static int is_living_name(int name)
{
    return name >= 0 && name != CM_NAME_ARROW;
}

int chatmsg_dtype(int surv_source, int dmg, int name)
{
    if (dmg >= 0)
        switch (dmg)
        {
            case DMG_GENERIC: return DT_GENERIC;
            case DMG_IN_FIRE: return DT_IN_FIRE;
            case DMG_ON_FIRE: return DT_ON_FIRE;
            case DMG_CACTUS: return DT_CACTUS;
            case DMG_FALL: return DT_FALL;
            case DMG_DROWN: return DT_DROWN;
            case DMG_LAVA: return DT_LAVA;
            case DMG_IN_WALL: return DT_IN_WALL;
            case DMG_OUT_OF_WORLD: return DT_OUT_OF_WORLD;
            case DMG_MAGIC: return DT_MAGIC;
            case DMG_WITHER: return DT_WITHER;
            case DMG_STARVE: return DT_STARVE;
            case DMG_MOB: return is_player_name(name) ? DT_PLAYER : DT_MOB;
            case DMG_FIREBALL: return DT_FIREBALL;
            case DMG_EXPLOSION: return name >= 0 ? DT_EXPLOSION_PLAYER : DT_EXPLOSION;
            case DMG_THROWN: return DT_THROWN;
            case DMG_ARROW: return DT_ARROW;
            case DMG_THORNS: return DT_THORNS;
            case DMG_INDIRECT_MAGIC: return DT_INDIRECT_MAGIC;
            case DMG_ANVIL: return DT_ANVIL;
            default: break;
        }
    switch (surv_source)
    {
        case SURV_IN_FIRE: return DT_IN_FIRE;
        case SURV_ON_FIRE: return DT_ON_FIRE;
        case SURV_LAVA: return DT_LAVA;
        case SURV_IN_WALL: return DT_IN_WALL;
        case SURV_DROWN: return DT_DROWN;
        case SURV_STARVE: return DT_STARVE;
        case SURV_CACTUS: return DT_CACTUS;
        case SURV_FALL: return DT_FALL;
        case SURV_OUT_OF_WORLD: return DT_OUT_OF_WORLD;
        case SURV_MOB: return is_player_name(name) ? DT_PLAYER : DT_MOB;
        case SURV_EXPLOSION: return name >= 0 ? DT_EXPLOSION_PLAYER : DT_EXPLOSION;
        case SURV_MAGIC: return DT_MAGIC;
        case SURV_ARROW: return DT_ARROW;
        case SURV_THROWN: return DT_THROWN;
        case SURV_FIREBALL: return DT_FIREBALL;
        case SURV_THORNS: return DT_THORNS;
        case SURV_ANVIL: return DT_ANVIL;
        default: return DT_GENERIC;
    }
}

/* EntityPlayer.func_145748_c_: ScorePlayerTeam.formatPlayerName of no team
 * is the name, with a SUGGEST_COMMAND "/msg <name> " click */
int chatmsg_player_name(struct chat_comp *t)
{
    int c = chat_text(t, CM_PLAYER_NAME);
    chat_set_click(t, c, CC_CLICK_SUGGEST_COMMAND, "/msg " CM_PLAYER_NAME " ");
    return c;
}

/* Entity.func_145748_c_: new ChatComponentText(getCommandSenderName()),
 * StatCollector.translateToLocal("entity." + EntityList.getEntityString +
 * ".name"), "generic" for a class EntityList does not name. Custom names
 * (EntityLiving.getCustomNameTag) need a named name tag, which needs an
 * anvil (config.yaml anvil = off). */
int chatmsg_entity_name(struct chat_comp *t, int name)
{
    if (is_player_name(name)) return chatmsg_player_name(t);
    const char *s = name == CM_NAME_DRAGON ? "EnderDragon" : name == CM_NAME_ARROW ? "Arrow" : living_kind_name(name);
    char key[64];
    snprintf(key, sizeof key, "entity.%s.name", s ? s : "generic");
    return chat_text(t, chat_translate(key));
}

/* The row's packet queue, opened first when the tick's world pass sends
 * ahead of the network tick (as the other world-pass senders do). */
static struct s2c_queue *queue_for(struct server_player *p)
{
    if (!p->dev_prequeued && !p->net_phase)
    {
        s2c_clear();
        p->dev_prequeued = 1;
    }
    return s2c_out();
}

void chatmsg_bed(struct server_player *p, int which)
{
    static const char *const key[3] = {"tile.bed.occupied", "tile.bed.noSleep", "tile.bed.notSafe"};
    struct chat_comp *t ENV_LOCAL = chat_comp_new();
    int c = chat_translation(t, key[which]);
    /* EntityPlayerMP.addChatComponentMessage */
    chat_s02_send(queue_for(p), t, c);
}

void chatmsg_achievement(struct server_player *p, int a)
{
    struct chat_comp *t ENV_LOCAL = chat_comp_new();
    int name = chatmsg_player_name(t);

    /* StatBase.func_150951_e over the achievement's statName (a key with no
     * arguments): the copy, grey with a SHOW_ACHIEVEMENT hover of its statId,
     * then Achievement's colour over it */
    const char *id = surv_ach_id(a);
    int stat_name = chat_translation(t, id);
    int v1 = chat_copy(t, stat_name);
    chat_set_color(t, v1, CF_GRAY);
    chat_set_hover(t, v1, CC_HOVER_SHOW_ACHIEVEMENT, chat_text(t, id));
    chat_set_color(t, v1, surv_ach_special(a) ? CF_DARK_PURPLE : CF_GREEN);

    /* StatBase.func_150955_j: "[" + it + "]" wearing its style */
    int v2 = chat_text(t, "[");
    chat_append(t, v2, v1);
    chat_append_text(t, v2, "]");
    chat_set_style(t, v2, chat_style_of(t, v1));

    int c = chat_translation(t, "chat.type.achievement");
    chat_arg(t, c, name);
    chat_arg(t, c, v2);
    /* ServerConfigurationManager.func_148539_a: every player's S02 */
    chat_s02_send(queue_for(p), t, c);
}

void chatmsg_build_too_high(struct server_player *p)
{
    struct chat_comp *t ENV_LOCAL = chat_comp_new();
    int c = chat_translation(t, "build.tooHigh");
    chat_arg_raw(t, c, "256");     /* Integer.valueOf(getBuildLimit()) */
    chat_set_color(t, c, CF_RED);
    chat_s02_send(queue_for(p), t, c);
}

/* CombatTracker.func_94550_c over one more entry: the first entry of the
 * highest damage among players and among livings (instanceof
 * EntityLivingBase, by name). */
void chatmsg_combat_add(struct surv_combat *c, const struct surv_combat_entry *e)
{
    if (is_player_name(e->name) && (c->n_player < 0 || e->damage > c->nd_player))
    {
        c->nd_player = e->damage;
        c->n_player = e->name;
    }
    if (is_living_name(e->name) && (c->n_best < 0 || e->damage > c->nd_best))
    {
        c->nd_best = e->damage;
        c->n_best = e->name;
    }
}

/* CombatTracker.func_94550_c, then EntityLivingBase.func_94060_bK's
 * fallbacks (attackingPlayer is never set on a player; the revenge target):
 * the killer's name, -1 none. */
static int killer_name(const struct surv_combat *c)
{
    if (c->n_player >= 0 && c->nd_player >= c->nd_best / 3.0F) return c->n_player;
    if (c->n_best >= 0) return c->n_best;
    return c->revenge_kind >= 0 ? c->revenge_name : -1;
}

void chatmsg_death(struct server_player *p)
{
    const struct surv_combat *c = &p->sv.combat;
    struct chat_comp *t ENV_LOCAL = chat_comp_new();
    char key[64];
    int msg;

    if (c->n == 0)
    {
        msg = chat_translation(t, "death.attack.generic");
        chat_arg(t, msg, chatmsg_player_name(t));
    }
    else
    {
        /* func_94544_f: the entry before the longest fall (or the fall
         * itself when it is the first), when that fall was over 5; its
         * second half never answers (its bound, var3, stays 0) */
        const struct surv_combat_entry *var1 = c->has_fall && c->fall_len > 5.0F ? &c->fall_entry : NULL;
        const struct surv_combat_entry *last = &c->last;
        int var4 = last->name;

        if (var1 != NULL && last->dtype == DT_FALL)
        {
            int var6 = var1->name;
            if (var1->dtype != DT_FALL && var1->dtype != DT_OUT_OF_WORLD)
            {
                /* the held item's .item forms need a named item (an anvil) */
                if (var6 >= 0 && (var4 < 0 || var6 != var4))
                {
                    msg = chat_translation(t, "death.fell.assist");
                    chat_arg(t, msg, chatmsg_player_name(t));
                    chat_arg(t, msg, chatmsg_entity_name(t, var6));
                }
                else if (var4 >= 0)
                {
                    msg = chat_translation(t, "death.fell.finish");
                    chat_arg(t, msg, chatmsg_player_name(t));
                    chat_arg(t, msg, chatmsg_entity_name(t, var4));
                }
                else
                {
                    msg = chat_translation(t, "death.fell.killer");
                    chat_arg(t, msg, chatmsg_player_name(t));
                }
            }
            else
            {
                static const char *const label[4] = {"generic", "ladder", "vines", "water"};
                snprintf(key, sizeof key, "death.fell.accident.%s", label[var1->label]);
                msg = chat_translation(t, key);
                chat_arg(t, msg, chatmsg_player_name(t));
            }
        }
        else
        {
            /* the last entry's DamageSource.func_151519_b */
            int dt = last->dtype;
            snprintf(key, sizeof key, "death.attack.%s", DT_NAME[dt]);
            if (dt >= DT_MOB && dt <= DT_EXPLOSION_PLAYER)
            {
                /* EntityDamageSource: the source's entity (its held item's
                 * .item form needs a named item) */
                msg = chat_translation(t, key);
                chat_arg(t, msg, chatmsg_player_name(t));
                chat_arg(t, msg, chatmsg_entity_name(t, var4));
            }
            else if (dt >= DT_ARROW && dt <= DT_INDIRECT_MAGIC)
            {
                /* EntityDamageSourceIndirect: the shooter, or the projectile */
                msg = chat_translation(t, key);
                chat_arg(t, msg, chatmsg_player_name(t));
                chat_arg(t, msg, chatmsg_entity_name(t, var4 >= 0 ? var4 : CM_NAME_ARROW));
            }
            else
            {
                int killer = killer_name(c);
                char pkey[80];
                snprintf(pkey, sizeof pkey, "%s.player", key);
                if (killer >= 0 && chat_can_translate(pkey))
                {
                    msg = chat_translation(t, pkey);
                    chat_arg(t, msg, chatmsg_player_name(t));
                    chat_arg(t, msg, chatmsg_entity_name(t, killer));
                }
                else
                {
                    msg = chat_translation(t, key);
                    chat_arg(t, msg, chatmsg_player_name(t));
                }
            }
        }
    }
    chat_s02_send(queue_for(p), t, msg);
}
