/* The action compiler for moving, mining and looking (lane/moveact): what
 * a trainer's higher-level action becomes in vanilla input, tick by tick,
 * on the worker that steps the env (pool.h act_hook), so a step of several
 * ticks compiles each of its ticks against the state that tick sees.
 *
 *   hold_attack  an attack press holds the attack key until the block it
 *                was pressed on breaks (the client world's block there
 *                changes), the crosshair leaves it (another block, an
 *                entity, nothing), a screen opens, or the trainer releases
 *                (a release command). A press on an entity or on nothing
 *                is one click (held for that tick only).
 *   look_target  the trainer names a point of the frame it saw (u, v in 0..1
 *                from the top left; a coarse grid's cell is its centre) and
 *                the camera turns there over look_ticks ticks, as mouse
 *                deltas: whole mouse counts at the sensitivity's step
 *                (EntityRenderer: f = s * 0.6 + 0.2, f^3 * 8 * 0.15 degrees a
 *                count; 0.15 at the default 0.5), the remaining turn split
 *                evenly over the ticks left and recomputed each tick from
 *                where the camera is, the pitch held to -90..90 as
 *                setAngles does. The point's direction is the frame's
 *                projection (EntityRenderer.getFOVModifier at partial tick
 *                1: the fov option times the movement multiplier, 60/70
 *                under water; the frame's aspect) from the look at the
 *                command's first tick, the frame's own (frames are drawn at
 *                partial tick 1). The multiplier is the current one, not
 *                fovModifierHand's smoothed value (a sprint start is half
 *                way there in one tick). While a turn runs it replaces the
 *                act's own look; a new target replaces the turn.
 *
 * Everything it emits is the agent form a person's keys and mouse make
 * (pool.h struct pool_act: holds, presses, look deltas), so a compiled run
 * is a script the Java oracle replays: moveact_script writes each tick's
 * compiled act as a script line ({"n":1,"act":{...}}). With neither lever
 * on and no script open the hook leaves every act alone (the pipeline's
 * rows and frames as before). */
#ifndef NETHERITE_RUNTIME_MOVEACT_H
#define NETHERITE_RUNTIME_MOVEACT_H

#include <stddef.h>
#include <stdint.h>

struct pool_act;
struct session;

struct moveact_cfg {
    int hold_attack, look_target;
    int look_ticks;             /* >= 1 */
    float fov;                  /* GameSettings.fovSetting * 40 + 70 (70) */
    float sensitivity;          /* GameSettings.mouseSensitivity (0.5) */
    int obs_w, obs_h;           /* the frame the targets are points of */
};

/* the trainer's command for an env's next step (rlbind.h struct nwrl_move) */
enum { MV_ATTACK_NONE, MV_ATTACK_PRESS, MV_ATTACK_RELEASE };
enum { MV_LOOK_NONE, MV_LOOK_TARGET };
struct moveact_cmd {
    int32_t attack;             /* MV_ATTACK_* */
    int32_t look;               /* MV_LOOK_* */
    float u, v;                 /* MV_LOOK_TARGET: the frame point, 0..1 from the top left */
};

/* per env counters since its reset */
enum {
    MVS_PRESSES,                /* attack presses compiled */
    MVS_HOLDS,                  /* of them latched on a block */
    MVS_END_BROKEN, MVS_END_TARGET, MVS_END_RELEASE, MVS_END_SCREEN,
    MVS_HOLD_TICKS,             /* ticks the latch held attack */
    MVS_TURNS,                  /* look targets taken */
    MVS_TURN_TICKS,             /* ticks a turn moved the camera */
    MVS_N
};

struct moveact;
struct moveact *moveact_new(int n);
void moveact_free(struct moveact *m);
void moveact_config(struct moveact *m, const struct moveact_cfg *c);
void moveact_get_config(const struct moveact *m, struct moveact_cfg *c);
/* the env's latch, turn, pending command and counters cleared (after a reset) */
void moveact_reset(struct moveact *m, int id);
/* env id's command for its next step (the env idle); -1 with err when it
 * names a lever that is off or a target outside the frame */
int moveact_command(struct moveact *m, int id, const struct moveact_cmd *c, char *err, size_t n);
/* each tick env id runs from now on as a script line appended to path (a
 * Java script's agent acts, one tick each); NULL closes it. 0 or -1 */
int moveact_script(struct moveact *m, int id, const char *path);
void moveact_stats(const struct moveact *m, int id, uint64_t out[MVS_N]);
/* env id now (idle): 1 while a latch holds attack, | 2 while a turn runs */
int moveact_state(const struct moveact *m, int id);
/* pool.h act_hook (ctx: the struct moveact) */
int moveact_hook(void *ctx, int id, struct session *ss, int k, const struct pool_act *src, int hold_only,
                 struct pool_act *out);

#endif
