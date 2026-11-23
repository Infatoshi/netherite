/* The macros as mods (lane/macromod): the API a macro fills, and the forward declarations across the macros' files.
 *
 * A macro is one C file in csrc/mods/macros that defines one `struct vec_macro` (or two, where one switch starts at two
 * places in the order: food's eating and hunt) and one line in macros.c's MACROS table. Its switch is a NETH_VEC_* bit
 * (vecenv.h, with a comment like the others) and its run-file names its .sw list (the trainer's and brutetab's
 * actions.macros read them through neth_vec_macro_switch). The env core (vecenv.c)
 * calls the macros only through macros.c's dispatcher (mac_*), which calls the hooks below in each phase's order; a
 * hook left NULL is skipped. Every hook tests its own switch (v->macros), as each macro's code always did.
 *
 * The orders are data: each phase walks the table by its own rank column (MACROS' start, cand, observe, drive and label
 * columns; 0 not in that phase). They differ because the code they replaced did (the strip's lava check reads the
 * Nether's state before the Nether's books move it; the label names the strip while the seek it handed off to drives).
 *
 * A step of an env (vecenv.c decode, through mac_*):
 *   observe   books and the step's own reads, every step (the strip's safe cells and its lava hit, the Nether's view, the
 *             obsidian's sources), and a veto of the policy's own act (the strip's dig under the feet, a pigman's hit)
 *   cand      1 when the macro could take the step or is mid-way: no macro's cand, the step is the policy's alone
 *   policy    a step the policy drives, its act changed (the stronghold's eye throw held back)
 *   interrupt a macro's run cut short before the start (the lava escape, the obsidian's douse): 1 when it took the step
 *   start     on a step no macro runs, in start order: 1 when it took the step (e->kind set, its own state begun)
 *   drive     in drive order, the running macro's act: -1 not this macro's run, else 1 when it is over
 *   label     in label order, the running macro's switch for the distiller's label (0 not its), and half its half
 *   end       its run's state cleared (the run over, a death, a hand-off)
 *   abort     before end on a death or the lava escape (its books)
 *   reset     an episode's start
 */
#ifndef NETH_MODS_MACRO_H
#define NETH_MODS_MACRO_H

/* (struct mac_step, what a phase's hooks read of the step, and the dispatcher's mac_* are csrc/runtime/vecmods.h: the
 * core's side) */

/* a run-file name of actions.macros (train/loop.c macro_set) and its bit */
struct mac_switch {
    const char *name;
    int32_t bit;
};

struct vec_macro {
    const char *name;
    int32_t bit;                /* its switch */
    const struct mac_switch *sw;    /* its run-file names, a NULL name last: its own bit's and any switch of its own
                                     * (the craft macro's iron plans, the seek's key); neth_vec_macro_switch lists them */
    void (*observe)(struct mac_step *c, struct neth_rl_act *a);
    int (*cand)(struct mac_step *c);
    void (*aid)(struct mac_step *c, struct neth_rl_act *a, int cand);     /* the policy's own act changed by a run-file
                                                 * option, cand or not (aids.c: policy_hold, policy_place, policy_tool) */
    void (*policy)(struct mac_step *c, struct neth_rl_act *a);
    int (*interrupt)(struct mac_step *c);
    int (*start)(struct mac_step *c);
    int (*drive)(struct mac_step *c, struct neth_rl_act *a);
    uint32_t (*label)(const struct vec_env *e);
    int (*half)(struct mac_step *c, int done);  /* the label's half (neth_vec_label.half) while label named it */
    int (*after)(struct mac_step *c, struct neth_rl_act *a, const struct neth_rl_act *a0);   /* after the drive: 1 when the
                                                 * step was handed back to the policy (a0 its act) */
    void (*end)(struct vec_env *e);
    void (*abort)(struct vec_env *e);
    void (*reset)(struct vec_env *e);
    /* the books and the label's extras (vecmods.h mac_books, mac_fits, mac_chance, mac_envpick, mac_mask, mac_decision) */
    void (*books)(struct mac_books *b);             /* at b->at: MB_RESULT0 (before the reward), MB_RESULT (after the
                                                     * deaths), MB_DECISION (a decision step's act made), MB_GUI (gui
                                                     * cursor's books) */
    void (*decision)(struct vec_env *e);            /* a decision step, before its act is made */
    int (*fits)(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s);    /* NETH_VEC_LBL_FITS */
    int (*chance)(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, int kind, int half);
    int (*envpick)(const struct neth_vec *v, const struct neth_rl_state *s, const struct neth_rl_act *a, int kind);
    void (*mask)(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, uint8_t *m);
    void (*make)(struct neth_vec *v);               /* the vector env's make (its options parsed): its own settings */
};

static void shd_step(const struct neth_vec *vb, struct vec_env *e, const struct neth_rl_state *s, int kind,
                     const struct neth_rl_act *sa, struct neth_vec_label *l);   /* shadow.c (after vecenv.c's vec_label) */
static int nt_sword_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                         const struct neth_rl_state *s, int64_t sticks, int64_t tables, int64_t cobble,
                         int64_t tier);    /* nether.c: craft.c's craft_plan */
static int arm_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                    const struct neth_rl_state *s, int64_t tables);   /* armor.c (lane/rod): craft.c's craft_plan */
static int sn_plan(const struct neth_vec *v, const struct vec_env *e, const struct vec_last *r,
                   const struct neth_rl_state *s, int64_t planks, int64_t sticks, int64_t tables);   /* snow.c (lane/rod) */
static int sn_holds(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                    const struct neth_rl_state *s);    /* snow.c (lane/snowleg, snow_first): stronghold.c's sh_want */
static int sn_waits(const struct neth_vec *v, const struct vec_env *e,
                    const struct vec_last *r);      /* snow.c (lane/snowleg, snow_first): stronghold.c's sh_owns */
static int portal_build(const struct neth_vec *v, struct vec_env *e, const struct vec_last *r,
                        const struct neth_rl_state *s, struct neth_rl_act *a, double ey);     /* portal.c: craft.c's craft */
static int dig_under(const struct neth_vec *v, const struct vec_last *r, const struct neth_rl_state *s);   /* strip.c */
static void x2_label(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s, const struct neth_rl_act *a,
                     int kind, struct neth_vec_label *l);       /* x2label.c: craft.c's books (lane/ladder17) */
static void gui_policy(const struct neth_vec *v, const struct vec_last *r, const int32_t *act, int held12,
                       struct neth_rl_act *a);                     /* vecenv.c (after the folder): x2label.c's check */

#endif
