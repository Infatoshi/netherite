/* lane/dragon: the End as its player sees it, for the vector env's dragon macro (vecenv.h NETH_VEC_DRAGON). Read
 * from the client world's copies (what the server sent the client, as the screen draws it), at the step's last tick,
 * and only in the End (a zeroed record elsewhere: nothing of the scan runs outside it). Kept beside neth_rl_state, not
 * in it, so neth_rl_state's and neth_rl_result's layouts (mcsr-speedrun's ctypes) stay as they are.
 *
 *   crystals   the ender crystal copies in view, nearest first: within END_VIEW_DEG of the look, the line from the
 *              eye to the crystal's middle clear of blocks (never through a wall or a pillar's side); held is every
 *              crystal copy the client has (the tracker sends each within 256 blocks: the island's), the books' count
 *   dragon     its copy's id (0 none), its health as the boss bar shows it (the DataWatcher's, which the HUD draws),
 *              dying (its health 0: the death's rise and beams, visible from anywhere on the island), its head's and
 *              body's middles and whether the head's middle is in view (the same test)
 *   endermen   the enderman copies in view within END_ENDER_RANGE blocks (the middle of the box, EntityEnderman's
 *              shouldAttackPlayer's point: never hold the look on one) */
#ifndef NETHERITE_RUNTIME_ENDVIEW_H
#define NETHERITE_RUNTIME_ENDVIEW_H

#include <stdint.h>

#define NETH_RL_END_CRYSTALS 16
#define NETH_RL_END_ENDERS 8

struct neth_rl_end {
    int32_t on;                 /* 1: the player is in the End (the rest is filled) */
    int32_t ncrystal, held;     /* crystals in view; crystal copies the client holds */
    struct { int32_t id; float x, y, z, d; } crystal[NETH_RL_END_CRYSTALS];
    int32_t dragon_id, dragon_view, dragon_dying;
    float dragon_health;
    float head[3], body[3];     /* the middles of the head's and the body's boxes */
    float pos[3];               /* lane/dragon2: the dragon's own position (where its death's portal forms:
                                 * EntityDragon.createEnderPortal at its x, z; its parts stand off it) */
    float head_h;               /* the head part's height (its box) */
    int32_t nender;
    struct { int32_t id; float x, y, z, d; } ender[NETH_RL_END_ENDERS];
    float ender_near;           /* the books' only (vecdragon.c dragon_books; the macro never reads it: it is not in
                                 * view): the feet to the nearest enderman copy's feet, seen or not (0 none) */
    /* lane/dragon2, the books' only (the macro never reads these): the server player's CombatTracker as the death
     * message reads it (chatmsg.c chatmsg_death; kept past the death's reset): the last hit's DamageSource family
     * (chatmsg.h DT_*) and its entity's name (living.h kinds, 64 the dragon, -1 none), the hit before the longest
     * fall (func_94544_f's: its family, name and the fall's length; fall_dt -1 none), the server's health */
    int32_t cm_dt, cm_name, fall_dt, fall_name;
    float fall_len, cm_damage, sv_health;
    float held_at[NETH_RL_END_CRYSTALS][3];   /* the books' only: every crystal copy's feet (the first held of them) */
    int32_t held_id[NETH_RL_END_CRYSTALS];    /* their ids: the macro reads only whether a crystal it saw still has its
                                            * copy (gone within the loaded chunks is its blast, seen and heard) */
    int32_t beam_id;                       /* lane/dragon2: the healing beam's crystal while the dragon is in view (its
                                            * end is drawn: RenderDragon), 0 none; its middle */
    float beam[3];
    /* lane/dragon3, the books' only (the macro never reads these: the server's): the dragon's healing crystal (1 one,
     * its place), the dragon's ticks (they stop while its chunks are not all loaded) and its health */
    int32_t sv_heal;
    float sv_heal_at[3];
    int32_t sv_ticks;
    float sv_dragon_hp;
    /* lane/chain2, the books' only: the server's crystals in the world and not dead (those whose chunks it has loaded),
     * the first NETH_RL_END_CRYSTALS of them by place */
    int32_t sv_ncry;
    float sv_cry[NETH_RL_END_CRYSTALS][3];
    float sv_pos[3];            /* lane/dleg, the books' only: the server's dragon's place (0 none) */
    /* lane/hill-rnn-hotbar, the books' only: server crystals marked dead (pillar floor x,z for strict crystal reward) */
    int32_t sv_dead_ncry;
    int32_t sv_dead_pos[NETH_RL_END_CRYSTALS][2];
};

struct session;
struct client_player;
void end_scan(struct neth_rl_end *o, struct session *ss, struct client_player *cp);
/* env id's record, as its last step left it (rlbind.c; read where its state is: the act and step hooks) */
struct nwrl;
const struct neth_rl_end *neth_rl_end_view(const struct nwrl *h, int id);

#endif
