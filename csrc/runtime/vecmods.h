/* The env core's calls into the macros (lane/macromod): vecenv.c calls the macros only through these, which
 * csrc/mods/macros/macros.c (the dispatcher over the macros' registration table) defines, or nomacros.c (the build
 * without the macros: MACROS=0, raw controls only) as no-ops. Included by vecenv.c after struct vec_env, before either
 * definition; every function is static in vecenv.c's translation unit. */
#ifndef NETH_VECMODS_H
#define NETH_VECMODS_H

/* what the macros read of a decision step: the policy's action and pick, the attack it holds, the eye's height, and
 * the step's lava hit (the strip's observe sets it) */
struct mac_step {
    const struct neth_vec *v;
    struct vec_env *e;
    const struct vec_last *r;
    const struct neth_rl_state *s;
    const int32_t *act;
    int yb;                     /* the action's yaw bin */
    int attack;                 /* the policy's attack held (an observe hook may let it go, an aid hold it) */
    int throw_key;              /* the action's key 9 */
    int pick;                   /* the policy's craft pick (lane/picks; 0 none) */
    int picked;                 /* the plan it picked if the craft macro could make it now, else -1 (start only) */
    int lava;                   /* a lava hit this step (the strip's observe: the escape's start) */
    int ph;                     /* the policy's own hotbar change (-1 none; an aid may set it) */
    int kind0;                  /* the kind running before the drive (after only) */
    double ey;                  /* the eye's height */
};

/* the books' calls (mac_books): where in the step, and what they read */
enum { MB_RESULT0, MB_RESULT, MB_DECISION, MB_GUI, MB_LABEL };     /* MB_LABEL: a decision step's label made, before
                                                                     * gui_books (the 2x2 labeller, lane/ladder17) */
struct mac_books {
    int at;
    const struct neth_vec *v;           /* the vector env (an env at a level reads e->lvl ? v->lv : v) */
    struct vec_env *e;
    int id;
    const struct pool_result *r;        /* MB_RESULT0, MB_RESULT */
    const struct neth_rl_state *s;
    const int32_t *act;                 /* MB_DECISION, MB_GUI, MB_LABEL */
    const struct neth_rl_act *a;           /* MB_DECISION, MB_GUI, MB_LABEL: the act the step became */
    int kind;                           /* MB_DECISION, MB_GUI, MB_LABEL */
    struct neth_vec_label *l;           /* MB_DECISION, MB_LABEL: the step's label (the labeller may replace it) */
};

static void mac_init(void);                                         /* once, at the make */
static void mac_make(struct neth_vec *v);                           /* each make, its options parsed */
static void mac_books(struct mac_books *b);
static void mac_decision(struct vec_env *e);                        /* a decision step, before its act */
static int mac_fits(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s);
static int mac_chance(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, int kind, int half);
static int mac_envpick(const struct neth_vec *v, const struct neth_rl_state *s, const struct neth_rl_act *a, int kind);
static void mac_mask(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, uint8_t *m);
static void mac_reset(struct vec_env *e);                           /* an episode's start */
static void mac_end(struct vec_env *e);                             /* the running macro over: no kind */
static void mac_abort(struct vec_env *e);                           /* before mac_end on a death or the lava escape */
static void mac_observe(struct mac_step *c, struct neth_rl_act *a);    /* every decision step: books, the act's vetoes */
static int mac_cand(struct mac_step *c);                            /* 1: a macro could take the step */
static void mac_aid(struct mac_step *c, struct neth_rl_act *a, int cand);
static void mac_policy(struct mac_step *c, struct neth_rl_act *a);     /* a step the policy drives */
static void mac_interrupt(struct mac_step *c);                      /* a run cut short (the lava escape, the douse) */
static void mac_start(struct mac_step *c);                          /* no macro running: the first that starts */
static int mac_chosen(const struct mac_step *c);                    /* the started macro the policy's own act */
static int mac_drive(struct mac_step *c, struct neth_rl_act *a);       /* a tick of the running macro: 1 when over */
static int mac_run(struct mac_step *c, struct neth_rl_act *a, int *kind, int *handed);   /* a decision of it */

#endif
