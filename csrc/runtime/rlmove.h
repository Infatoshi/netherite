/* The binding's action compiler for moving, mining and looking (lane/moveact,
 * moveact.h), beside rlbind.h: a trainer's higher-level commands made into
 * vanilla input by the engine, tick by tick.
 *
 *   neth_rl_make                 the levers from the config's sim.actions keys
 *                             (hold_attack, look_target, look_ticks)
 *   neth_rl_move_config(cfg)     the levers set again (and the fov and mouse
 *                             sensitivity the frames are drawn and the turns
 *                             made at)
 *   neth_rl_move(ids, moves)     each env's command for its next step: an attack
 *                             press that holds (or a release), a look target
 *                             (a point of the frame the env's last result drew)
 *   neth_rl_move_script(id, p)   every tick env id runs from now on, as a Java
 *                             script line of the compiled agent act (the gate's
 *                             recordings: oracle/tests/moveact-*.jsonl)
 *   neth_rl_move_stats(id, out)  the env's counters since its reset (MVS_*)
 *
 * With both levers off (the default) the binding steps exactly as before;
 * with a lever on and no command, too (a lever only accepts commands). */
#ifndef NETHERITE_RUNTIME_RLMOVE_H
#define NETHERITE_RUNTIME_RLMOVE_H

#include <stdint.h>

struct nwrl;
struct moveact;

struct neth_rl_move_cfg {
    int32_t hold_attack, look_target, look_ticks;
    float fov, sensitivity;     /* 0: 70 and 0.5, vanilla's defaults (the agent's) */
};
/* attack 0 none, 1 press (held until the block breaks or the crosshair leaves
 * it), 2 release; look 0 none, 1 target (u, v: 0..1 from the frame's top left) */
struct neth_rl_move { int32_t attack, look; float u, v; };
#define NETH_RL_MOVE_STATS 9

int neth_rl_move_config(struct nwrl *h, const struct neth_rl_move_cfg *c);
void neth_rl_move_get_config(struct nwrl *h, struct neth_rl_move_cfg *c);
/* moves[0..k) for envs ids[0..k) (idle), taken by their next step; -1 with
 * err for a lever that is off or a target outside the frame */
int neth_rl_move(struct nwrl *h, const int *ids, int k, const struct neth_rl_move *moves, char *err, int en);
int neth_rl_move_script(struct nwrl *h, int id, const char *path);
void neth_rl_move_stats(struct nwrl *h, int id, uint64_t out[NETH_RL_MOVE_STATS]);
/* envs ids[0..k) (idle, after their poll) into out: 1 while a latch holds
 * attack, | 2 while a turn runs */
void neth_rl_move_state(struct nwrl *h, const int *ids, int k, int32_t *out);

/* rlbind.c's side */
struct moveact *rlmove_of(struct nwrl *h);
struct wconf;
/* the frame's size and the config's levers (sim.actions hold_attack, look_target, look_ticks) */
void rlmove_init(struct moveact *m, int w, int h, const struct wconf *wc);

#endif
