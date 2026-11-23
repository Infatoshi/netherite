/* The env without the macros (lane/macromod; rlbind.mk MACROS=0): vecmods.h's dispatcher with no macro registered, so
 * the policy's action is raw controls only (the keys, the camera's bins, the hotbar element; actions.gui cursor's
 * clicks and inventory key), a death respawned by the env as with the macros. Included by vecenv.c in place of
 * csrc/mods/macros/macros.c. */

static void mac_init(void) {}
static void mac_make(struct neth_vec *v) { (void)v; }
static void mac_books(struct mac_books *b) { (void)b; }
static void mac_decision(struct vec_env *e) { (void)e; }
static int mac_fits(const struct neth_vec *v, struct vec_env *e, const struct neth_rl_state *s)
{
    return (void)v, (void)e, (void)s, 0;
}
static int mac_chance(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, int kind, int half)
{
    return (void)v, (void)e, (void)s, (void)kind, half;
}
static int mac_envpick(const struct neth_vec *v, const struct neth_rl_state *s, const struct neth_rl_act *a, int kind)
{
    return (void)v, (void)s, (void)a, (void)kind, 0;
}
static void mac_mask(const struct neth_vec *v, const struct vec_env *e, const struct neth_rl_state *s, uint8_t *m)
{
    (void)v, (void)e, (void)s, (void)m;
}
static void mac_reset(struct vec_env *e) { (void)e; }
static void mac_end(struct vec_env *e)
{
    e->kind = M_NONE;
    e->age = 0;
}
static void mac_abort(struct vec_env *e) { (void)e; }
static void mac_observe(struct mac_step *c, struct neth_rl_act *a) { (void)c, (void)a; }
static int mac_cand(struct mac_step *c) { return (void)c, 0; }
static void mac_aid(struct mac_step *c, struct neth_rl_act *a, int cand) { (void)c, (void)a, (void)cand; }
static void mac_policy(struct mac_step *c, struct neth_rl_act *a) { (void)c, (void)a; }
static void mac_interrupt(struct mac_step *c) { (void)c; }
static void mac_start(struct mac_step *c) { (void)c; }
static int mac_chosen(const struct mac_step *c) { return (void)c, 0; }
static int mac_drive(struct mac_step *c, struct neth_rl_act *a) { return (void)c, (void)a, 1; }
static int mac_run(struct mac_step *c, struct neth_rl_act *a, int *kind, int *handed)
{
    (void)c, (void)a;
    *kind = M_NONE;
    *handed = 0;
    return 1;
}

/* the macros' own API (vecenv.h, csrc/mods/macros/pearl.h): none here, every call -1 (or NULL), so the trainer and the
 * cases still link */
int neth_vec_bportal(const struct neth_vec *v, int id, struct neth_vec_bportal *out) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_env_level(struct neth_vec *v, int id, int on) { return (void)v, (void)id, (void)on, -1; }
int neth_vec_level(struct neth_vec *v, int level, int labeller, char *err, int en)
{
    (void)v, (void)level, (void)labeller;
    seterr(err, en, "neth_vec_level: a build without the macros (MACROS=0)%s", "");
    return -1;
}
int neth_vec_level_align(struct neth_vec *v, double deg) { return (void)v, (void)deg, -1; }
int neth_vec_level_reach(struct neth_vec *v, double blocks) { return (void)v, (void)blocks, -1; }
int neth_vec_level_walk(struct neth_vec *v, int on) { return (void)v, (void)on, -1; }
int neth_vec_nether(const struct neth_vec *v, int id, struct neth_vec_nether *out) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_kit(const struct neth_vec *v, int id, struct neth_vec_kit *out) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_lava(const struct neth_vec *v, int id, struct neth_vec_lava *out) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_nether_block(const struct neth_vec *v, int id, int x, int y, int z)
{
    return (void)v, (void)id, (void)x, (void)y, (void)z, -1;
}
int neth_vec_obsidian(const struct neth_vec *v, int id, struct neth_vec_obsidian *out) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_pearl(const struct neth_vec *v, int id, int now, struct neth_vec_pearl *out)
{
    return (void)v, (void)id, (void)now, (void)out, -1;
}
int neth_vec_pearl_aim(const struct neth_vec *v, int id, int32_t out[3]) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_pearl_patrol(const struct neth_vec *v, int id, int32_t out[8]) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_pearl_gate(const struct neth_vec *v, int id, int32_t out[3]) { return (void)v, (void)id, (void)out, -1; }
struct pearl_census;
int neth_vec_pearl_census(struct neth_vec *v, int on, int id, struct pearl_census *out);
int neth_vec_pearl_census(struct neth_vec *v, int on, int id, struct pearl_census *out)
{
    return (void)v, (void)on, (void)id, (void)out, -1;
}
int neth_vec_pearl_dbg(const struct neth_vec *v, int id, int32_t out[8]);
int neth_vec_pearl_dbg(const struct neth_vec *v, int id, int32_t out[8]) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_pearl_tune(struct neth_vec *v, const char *name, int value);
int neth_vec_pearl_tune(struct neth_vec *v, const char *name, int value) { return (void)v, (void)name, (void)value, -1; }
const char *neth_vec_pick_name(int k) { return (void)k, NULL; }
const char *neth_vec_macro_switch(int i, int32_t *bit) { return (void)i, (void)bit, NULL; }
int neth_vec_shc_books(const struct neth_vec *v, int64_t *out) { return (void)v, (void)out, -1; }
int neth_vec_shc_plans(const struct neth_vec *v, int64_t *out) { return (void)v, (void)out, -1; }
int neth_vec_strip(const struct neth_vec *v, int id, struct neth_vec_strip *out) { return (void)v, (void)id, (void)out, -1; }
int neth_vec_sh_trace(const struct neth_vec *v, int id, struct neth_vec_sh_trace *out)
{
    return (void)v, (void)id, (void)out, -1;
}
int neth_vec_stronghold_book(const struct neth_vec *v, int id, int last, struct neth_vec_sh_book *out)
{
    return (void)v, (void)id, (void)last, (void)out, -1;
}
