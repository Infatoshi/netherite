/* Distillation to fewer macros (lane/ntbc; GOAL.md "The route"): ntloop --teacher W.ntw drives the envs with a teacher
 * (the actor with W's weights and the run's macros, its level), and trains the learner's net, the student, on what the
 * engine stepped: cross-entropy on the act each step became (vecenv.h neth_vec_label: the camera's two bins and the
 * keys), the value's squared error to the teacher's value. The student is trained for a lower macro level, the macro
 * switches it will run with (--level): a step one of those macros drove is the macro's at the student's level too and
 * is masked, except its first (Macros.chosen: the policy's own action started it, and the label is the teacher's
 * action); a step the policy drove is labelled with the teacher's action; a step a macro outside the level drove is
 * labelled with its act, so the student learns to do what the macro did. A step whose act has GUI ops is masked (the
 * 16-wide action cannot say them); a hotbar change is dropped from its label (nor this; lane/nthotbar: with net.h's
 * NETH_NET_HOTBAR head it is the label's hotbar element, the slot's number key, and the teacher's own steps' is keep), a
 * look between bins goes to the nearest bin. The student's scalars are the teacher's with what the student would have seen in place of the
 * previous action's fields (vecenv.c scalars: the last macro kind, camera bins and keys): its own previous label, and
 * the kind only of a macro at its level.
 *
 * A net.h blob narrower than the build's (a wm 1 teacher in a wm 2 build) is widened exactly: the added channels'
 * and units' weights zero, so they stay zero through every relu and the heads read the same sums. */
#ifndef NETH_NET_DISTILL_H
#define NETH_NET_DISTILL_H

#include <stddef.h>
#include <stdint.h>

#include "learn.h"

struct neth_vec_bufs;
struct neth_distill;

/* the books a row: the summed losses over the Adam steps since the last read, and the labels' census since the start */
/* ce_cam ce_keys acc_cam acc_keys vl weight valid steps; NETH_NET_HOTBAR: then ce_hot acc_hot (the weighted mean of the
 * hotbar's cross-entropy and argmax hits), switch (the weight of labels that change the slot) and switch_hit (of those,
 * the weight whose argmax is the label's slot) */
#define NETH_DISTILL_NBOOK (8 + 4 * NETH_NET_HOTBAR)
#define NETH_DISTILL_NCLASS 11   /* none surface loot mine craft fight seek strip food screen other */
#define NETH_DISTILL_NSTAGE 4    /* lane/ladder2: start wooden stone iron */
struct neth_distill_census {
    int64_t steps[NETH_DISTILL_NCLASS], trained[NETH_DISTILL_NCLASS], gui[NETH_DISTILL_NCLASS], hotbar[NETH_DISTILL_NCLASS], far[NETH_DISTILL_NCLASS],
        press[NETH_DISTILL_NCLASS], exact[NETH_DISTILL_NCLASS];
    int64_t invalid;    /* reset steps */
    double res_abs[NETH_DISTILL_NCLASS]; /* the summed |residual| of both bins (degrees) */
    int64_t prev_ok, prev_bad;  /* the check: the student's previous-action fields against the env's own where they
                                 * must agree (the last two steps the teacher's policy drove: the label is its action) */
    int64_t student, agree;     /* --beta: the decision steps the student drove; of the policy's steps it drove, the
                                 * ones whose action was the teacher's label (camera bins and keys) */
    int64_t hotlab[NETH_DISTILL_NCLASS]; /* NETH_NET_HOTBAR: the trained labels that change the hotbar slot (the hotbar's element) */
    /* lane/ladder2: the same by the env's stage, the furthest of what it has held this episode (the 32 items' counts
     * in its scalars): start (no pickaxe yet: logs, planks, the table), wooden (a wooden pickaxe: to stone), stone (a
     * stone pickaxe: to the furnace and iron), iron (an iron ingot); per stage and driver the steps, the trained
     * labels, the GUI, hotbar and far flags, and the trained labels that change the slot */
    int64_t st[NETH_DISTILL_NSTAGE][NETH_DISTILL_NCLASS][6];
    /* lane/ladder4 (runs with the dk feature; zero otherwise): the trained labels by the dig the state showed, none, the hit
     * without a crack, a crack; each [labels, of them holding the attack] */
    int64_t dig[3][2];
    /* lane/ladder6 (neth_distill_labeller): the decision steps of envs playing at the student's level, the labeller's labels on
     * them by stage, and the level's steps masked (student_labels shadow) */
    int64_t lsteps, shadow[NETH_DISTILL_NSTAGE], lmasked;
    /* lane/ladder12: the craft macro's steps (mbit NETH_VEC_CRAFT) by half, none (not a crafting plan), the table, the
     * recipe (neth_vec_label.half); each [steps, trained, the state under a container screen (gv runs), hotbar
     * changes, use (a right click under a screen) in the macro's act, the inventory key (the close under one) in it] */
    int64_t half[5][6];     /* [3]: the recipe's first step, counted apart (NETH_VEC_RECIPEKEY's label); [4] the
                             * table half's first step with no screen (lane/ladder15, NETH_VEC_TABLEKEY's label) */
    /* lane/ladder13 (runs with the gv feature): the recipe half's labels by the screen the state showed (none, the inventory, a
     * crafting table, a furnace, another), [0] the first step's (half 3), [1] the later steps' (half 2), [2] of those
     * trained (--recipe-hold's jump) */
    int64_t rscr[5][3];
    /* lane/ladder14 (runs with the gv feature): the labels on states under the inventory, a crafting table, a furnace ([0] [1] [2]),
     * by who opened the window (the step whose act made the screen show: [0] not seen, [1] the craft macro, [2] the
     * policy, [3] another macro) and by the step's label (the driver: [0] the policy's step, the teacher's action, [1]
     * the recipe's first step (half 3), [2] its later steps (half 2), [3] another craft step, [4] the screen macro, [5]
     * another macro); each [steps, trained, the label's jump, its inventory key (the close), a click (mine or place),
     * the state NETH_VEC_LBL_FITS] */
    int64_t win[3][4][6][6];
    int64_t wrel[3];    /* lane/ladder14 (--window-label): labels relabelled as the jump, as no key, as the close */
    int64_t tlabs;      /* lane/ladder15 (--table-label): policy steps relabelled as the table key */
    int64_t seekkey[3]; /* lane/ladder16 (NETH_VEC_SEEKKEY at the level): the seek's first steps labelled key 13, its
                         * later steps (the level's macro: untrained), the policy's steps relabelled key 13
                         * (--seek-label: the start holding) */
    int64_t seekwalk[4];    /* lane/ladder18 (half 8, actions.seek_walk or seek_dig policy): the seek's steps in the
                             * policy's half labelled with its act at the teacher's macros, beside the student's level
                             * (the walk labeller, NETH_VEC_LBL_SHADOW), of those far turns (NETH_VEC_LBL_FAR), and
                             * trained (weight above 0 after masking) */
    int64_t x2[4];      /* lane/ladder17 (actions.recipe_2x2 clicks, label half 9, at a NETH_VEC_NORECIPE level): the
                         * craft macro's 2x2 recipe steps, of them trained; the 2x2 labeller's steps (the student's own
                         * inventory), of them trained */
};

#ifdef __cplusplus
extern "C" {
#endif

/* a distiller for n envs in G groups of gs, segments of T, the student's macro switches level, the value's weight vf;
 * NULL with err */
struct neth_distill *neth_distill_new(int n, int G, int gs, int T, int level, float vf, char *err, size_t en);
void neth_distill_free(struct neth_distill *D);

/* the step envs lo .. lo + gs - 1 just made (after recv, before launch overwrites the actions): its labels on the
 * host from the env's buffers (b->label, kind, truncations, the actions it was sent) */
void neth_distill_took(struct neth_distill *D, const struct neth_vec_bufs *b, int lo, int gs);
/* those labels into segment seg's step t (seg the loop's 2 g + which), on stream s */
void neth_distill_stage_step(struct neth_distill *D, int seg, int t, int lo, int gs, void *s);
/* segment seg's update on the learner's stream: the student's scalars, then epochs x the minibatches of perm
 * [epochs][N], each neth_learn_grad with the distillation's loss and neth_learn_adam; *step counts Adam's steps */
void neth_distill_learn(struct neth_distill *D, neth_learn *ln, const neth_learn_hyper *h, int epochs, int mb, const int32_t *perm, int seg,
               const uint8_t *obs, const neth_sc_t *sc, const float *val, float *w, float *grad, float *m, float *v,
               int *step, void *s);
/* DAgger's way (--beta B): each episode the student drives with probability B (drawn at its end for the next, seed
 * seeds it), the teacher's forward run on every state all the same and its action the label. neth_distill_launch runs both on
 * a group step: the teacher into the distiller's own buffers (its value into io's, the value's target), the student
 * into io's actions and logp, then each env's action the one its driver chose; the teacher's actions copied back for
 * neth_distill_took. teacher and student are loop.h's struct neth_policy */
void neth_distill_dagger(struct neth_distill *D, double beta, uint64_t seed);
/* --soft 1: the teacher's own distribution as the target wherever the label is the teacher's action (its camera bins'
 * softmax, its keys' sigmoids), from the teacher's forward on the learner's minibatch (the learner's layout, host,
 * NETH_LEARN_NPARAM: the teacher's weights; its raw scalars); the macros' acts stay hard labels */
void neth_distill_soft(struct neth_distill *D, const float *teacher_flat);
/* --hot-weight W (NETH_NET_HOTBAR; lane/nthotbar): the hotbar's cross-entropy gradient on a label that changes the slot
 * (a removed macro's number key, about 3 in 10,000 decisions) times W; keep labels stay at 1 (default W 1) */
void neth_distill_hot_weight(struct neth_distill *D, float w);
/* lane/rawpix (--student-zero LO:N | ore): the student's scalars LO .. LO + N - 1 read 0 in its loss (the teacher reads
 * the env's), so a student whose blob has zero weights on them (a .dk blob in a .dk.or build) keeps them zero: a
 * pixels-only student of a teacher that sees the seen-ore scalars. Off (N 0) by default */
void neth_distill_student_zero(struct neth_distill *D, int lo, int n);
/* lane/rawpix (--student-prev env): the student's previous-action fields (the last macro kind, camera bins and keys) the
 * env's own, the action the driver sent, as a policy reads them when it drives itself, instead of the rewrite from the
 * labels (the act the engine stepped: with the task's keys masked and the look's nearest bin, 98% of a raw-controls
 * run's steps differ from the action sent). Off by default */
void neth_distill_student_prev_env(struct neth_distill *D, int on);
/* --hold-weight W (lane/ladder3): the attack key's BCE gradient times W on a removed macro's label that holds the attack
 * (the mine lock's and the strip's digs: in 1.7.10 one tick without attack resets the block's damage, so a key the
 * student samples at 0.95 keeps a 23-tick dig 31% of the time). The weighted optimum's odds are W times the labels':
 * the held attack trained into the sampled policy, not decoded greedily at play (ntloop --greedy keys). Default 1: no change.
 * scope (--hold-scope): 0 all, every removed macro's held attack (the strip's tunnels too); 1 lock, the mine lock's
 * alone; 2 dig, the mine lock's where the step before held the attack too (a dig under way, not its start); 3 digs
 * (lane/ladder5), dig's and the seek's continuing dig too (the seek removed at L3) */
void neth_distill_hold_weight(struct neth_distill *D, float w, int scope);
int neth_distill_beta_on(const struct neth_distill *D);
/* lane/ladder10 (actions.gui cursor, with --beta): the teacher's key 12 cleared before it is sent or labels a step: the
 * inventory key there (vecenv.h NETH_VEC_KEY_INV), a key the teacher's net never had a use for and samples at random */
void neth_distill_gui(struct neth_distill *D);
/* lane/ladder10 (--gui-weight W, runs with the gv feature): a label whose state showed an open container screen weighs W in the loss
 * (its share of the minibatch's sum of weights), so the few steps a craft spends under a screen (its clicks and close,
 * about 15 an episode against 24,000) are not drowned by the rest. Default 1: no change */
void neth_distill_gui_weight(struct neth_distill *D, float w);
/* lane/ladder17 (--x2-weight W, distill.x2_weight): a 2x2 recipe's label (half 9 at a NETH_VEC_NORECIPE level: the craft
 * macro's clicks and close, and the 2x2 labeller's) weighs W, ladder10's gui_weight for those labels alone (L4bE learned
 * the 2x2 kit's clicks at gui_weight 10, its craft labels 8% of the steps; at L6a they are about 3.5%). Default 1 */
void neth_distill_x2_weight(struct neth_distill *D, float w);
/* lane/ladder13 (--recipe-hold on, distill.recipe_hold; NETH_VEC_RECIPEKEY levels, gv runs): the recipe's later
 * steps under a container screen labelled as its first is, the jump key alone, weight 1. Under NETH_VEC_RECIPEKEY the
 * env reads the policy's jump at every call of the recipe half (vecenv.c craft(): a call without it hands the window
 * back), and the smelt's wait is hundreds of such calls; without this those steps are the level's macro's and masked,
 * and the student is never taught to keep the jump held (L4eT-s1 at seed 7: 0.94 of a run's 0.95 smelts handed back
 * mid-way, the macro's ingots taken 0.004 a run). on 2 (--recipe-hold close): the step the macro closes the window
 * (its label says the inventory key) labelled as that key alone, not the jump: on 1 teaches the student never to close
 * a window, and one left untidy by a hand-off holds it (L4f's 34M checkpoint: 11,928 steps a run under a screen) */
void neth_distill_recipe_hold(struct neth_distill *D, int on);
/* lane/ladder14 (--window-label on, distill.window_label; NETH_VEC_NOTABLE and NETH_VEC_RECIPEKEY levels, gv runs):
 * on a state under the inventory, a crafting table or a furnace the policy opened, the teacher's env's screen macro
 * (it closes the window: at the teacher's macros every label there is that close, 2,605 of 2,605 under the student's
 * own tables against the jump on 2,189 of 2,189 first steps under the macro's) relabelled: where the window is tidy and
 * the plan fits it (vecenv.h NETH_VEC_LBL_FITS) as the jump alone, NOTABLE's recipe start; else, where the step
 * before held the inventory key (the window the key opened), as no key at all: the close is the key's press, and a
 * key held from the opening step presses nothing (gui_policy acts on its edge), so the student taught the key again
 * there holds it under the window. Weight 1. on 2 (--window-label level, with --labeller: the student's episodes at its
 * level): also the policy's own steps under its own window, at the level and at the teacher's macros (their teacher
 * label the teacher's keys, its close cleared: at L4f 126,822 steps under its tables, jump 48%, clicks 77%, close 0;
 * W1-s1 at 16383, 128,633 under its own inventory, jump 93%), the same way, and the close
 * (the inventory key alone) where neither holds. --student-labels counts a window label as the labeller's (shadow keeps
 * it on the level's episodes, none drops it) */
void neth_distill_window_label(struct neth_distill *D, int on);
/* lane/ladder15 (--table-label on, distill.table_label; NETH_VEC_NOTABLE and NETH_VEC_TABLEKEY levels, with the env's
 * actions.table_chance on): a policy step (no macro) labelled half 5, no screen open while craft_plan names a crafting
 * plan, labelled as the table key (key 12 alone, no look, weight 1). The teacher's craft macro starts on every fifth
 * step, so without this a crafting plan's state says the key once (the start's step, half 4) and the teacher's own act
 * on the steps before it: L4gC-s1 at 35M took 4.9 of a run's 7,400 such steps */
void neth_distill_table_label(struct neth_distill *D, int on);
/* lane/ladder16 (--seek-label on, distill.seek_label; a NETH_VEC_SEEKKEY level with the seek off, the env's
 * actions.seek_chance on): a policy step (no macro) labelled half 7, the seek's start holding, labelled as the seek key
 * (key 13 alone, no look, weight 1), kept on the student's own episodes at its level (--student-labels shadow counts it
 * as the labeller's, as the window labels). The teacher's seek starts within five steps of its start holding, and the
 * student's own steps there are masked: L5aK-s1's kept student held the start 434 policy steps a run and pressed the
 * key 0.91 times */
void neth_distill_seek_label(struct neth_distill *D, int on);
/* lane/ladder15 (actions.table_key): NETH_VEC_TABLEKEY's key, the table half's first step's label and table_label's:
 * NETH_VEC_KEY_INV (12, the default) or NETH_VEC_KEY_PAUSED (13: the teacher's own key 13 cleared, as neth_distill_gui clears
 * its key 12) */
void neth_distill_table_key(struct neth_distill *D, int k);
/* lane/ladder6 (--labeller, with --beta): the student's episodes play at its level (the loop sets each env's level from
 * neth_distill_drive at each episode's start: vecenv.h neth_vec_level) with the removed macro as the labeller beside them; a
 * labeller's label (NETH_VEC_LBL_SHADOW) trains as that macro's act does, the student's own action the next step's
 * previous action (it is what the env stepped). sonly 1 (--student-labels shadow): on those episodes only the labeller's
 * labels train (the teacher's labels there masked); 0 the teacher labels the rest as on any episode; 2 (--student-labels
 * none, the control) no label on those episodes trains, the labeller's neither. lshare
 * (--level-share): of the student's episodes, the share drawn at its level (driver 2), the rest at the teacher's
 * macros as without the labeller (driver 1) */
void neth_distill_labeller(struct neth_distill *D, int sonly, double lshare);
/* each env's driver this episode (0 the teacher, 1 the student, 2 the student at its level), NULL without --beta */
const uint8_t *neth_distill_drive(const struct neth_distill *D);
int neth_distill_launch(struct neth_distill *D, void *teacher, void *student, const void *io, void *s);

/* the books since the last call (out [NETH_DISTILL_NBOOK]), read and zeroed on the learner's stream s (waits for it) */
void neth_distill_books(struct neth_distill *D, double *out, void *s);
const struct neth_distill_census *neth_distill_census(const struct neth_distill *D);
/* one JSON object of the census and the books (DIR/distill.jsonl's row), and the census as text */
void neth_distill_row(struct neth_distill *D, void *file, int64_t env_steps, double wall);
void neth_distill_print(const struct neth_distill *D);

/* net.h's blob of any width (NETH_NET_WM) at or under the build's into the build's, its added weights zero; malloc'd, NULL
 * with err (a wider blob, another stem, a bad file). lane/audit-fixes: name (the teacher's path) starts the messages,
 * its scalars are neth_checkpoint.c neth_ntw_parse's and the result NTW2 */
unsigned char *neth_distill_widen(const unsigned char *b, size_t len, const char *name, size_t *out, char *err, size_t en);

#ifdef __cplusplus
}
#endif

#endif
