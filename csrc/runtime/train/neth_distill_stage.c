/* ntloop's distillation stage (--teacher, train/distill.h): the teacher drives as the actor, the learner is the
 * student at its own macro level (--level), learning the act each step became; the labels' options, DAgger's student
 * driver (--beta) and the labeller (a removed macro labelling the student's own episodes at the level). */
#define _GNU_SOURCE
#include "neth_pipe_internal.h"
#include "neth_runconf.h"

int neth_distill_setup(struct neth_pipe *L, const struct neth_runconf *c)
{
    struct neth_learner *R = &L->lr;
    const char *mopts = c->mopts;
    int n = c->n;
    /* lane/audit-feat: the run's feature set has the open window's scalars (net.h's gv) */
    const int gv = (NETH_NET_FEATMASK & NETH_NET_FGV) != 0;
    static const char *const NOGV = "a run without the open window's scalars (the gv feature: --features or the init's header)";
    /* the student's macro level (default the teacher's own: the env's --macros) */
    char err[1024];
    const char *level_s = c->level_s;
    int level = !level_s ? c->macros : (*level_s >= '0' && *level_s <= '9') ? atoi(level_s)
                                                                          : neth_macro_set(level_s, err, sizeof err);
    L->stud_level = level;     /* --eval-every's level */
    if (level < 0 || !(L->dist = neth_distill_new(n, c->groups, L->gs, c->T, level, c->hy.vf, err, sizeof err)))
    {
        printf("FAIL --level %s: %s\n", level_s ? level_s : "", err);
        return 2;
    }
    printf("ntloop: distillation: the teacher %s drives at macros %d; the student (%s, from --init) learns "
           "for macros %d%s\n", c->teacher, c->macros, NETH_NET_VARIANT, level,
           c->soft ? "; --soft: the teacher's distribution the target where its action is the label" : "");
    if (c->soft) neth_distill_soft(L->dist, L->tflat);
    if (c->hotw != 1)
    {
        if (!NETH_NET_HOTBAR || !(c->hotw > 0))
        {
            printf("FAIL --hot-weight %g: %s\n", c->hotw,
                   NETH_NET_HOTBAR ? "not a positive weight" : "a build without the hotbar head (.hb)");
            return 2;
        }
        neth_distill_hot_weight(L->dist, (float)c->hotw);
        printf("ntloop: --hot-weight %g: a label that changes the slot weighs %g in the hotbar's loss\n", c->hotw,
               c->hotw);
    }
    if (c->szero_arg)
    {
        /* --student-zero LO:N | ore */
        int lo, k;
        if (neth_zsc_parse(c->szero_arg, &lo, &k))
        {
            printf("FAIL: --student-zero %s: LO:N inside the run's %d scalars (or ore, a run with the or feature)\n", c->szero_arg,
                   NETH_NET_NS);
            return 2;
        }
        neth_distill_student_zero(L->dist, lo, k);
        printf("lane/rawpix: --student-zero: the student's scalars %d..%d read 0\n", lo, lo + k - 1);
    }
    if (c->sprev_env)
    {
        neth_distill_student_prev_env(L->dist, 1);
        printf("lane/rawpix: --student-prev env: the student's previous-action fields the env's own\n");
    }
    if (c->holdw != 1)
    {
        if (!(c->holdw > 0))
        {
            printf("FAIL --hold-weight %g: not a positive weight\n", c->holdw);
            return 2;
        }
        const char *hscope = c->hscope;
        int hs = !strcmp(hscope, "all")    ? 0
                 : !strcmp(hscope, "lock") ? 1
                 : !strcmp(hscope, "dig")  ? 2
                 : !strcmp(hscope, "digs") ? 3   /* the seek's continuing dig too */
                                           : -1;
        if (hs < 0)
        {
            printf("FAIL --hold-scope %s: all, lock, dig or digs\n", hscope);
            return 2;
        }
        neth_distill_hold_weight(L->dist, (float)c->holdw, hs);
        printf("ntloop: --hold-weight %g --hold-scope %s: %s held attack weighs %g in the attack key's loss\n",
               c->holdw, hscope,
               hs == 0   ? "a removed macro's"
               : hs == 1 ? "the mine lock's"
               : hs == 2 ? "the mine lock's continuing"
                         : "the mine lock's and the seek's continuing",
               c->holdw);
    }
    if (c->guiw != 1)
    {
        if (!(c->guiw > 0) || !gv)
        {
            printf("FAIL --gui-weight %g: %s\n", c->guiw, gv ? "not a positive weight" : NOGV);
            return 2;
        }
        neth_distill_gui_weight(L->dist, (float)c->guiw);
        printf("ntloop: --gui-weight %g: a label under an open container screen weighs %g\n", c->guiw, c->guiw);
    }
    if (c->x2w != 1)
    {
        if (!(c->x2w > 0) || !strstr(mopts, "recipe_2x2=clicks"))
        {
            printf("FAIL --x2-weight %g: %s\n", c->x2w,
                   c->x2w > 0 ? "the env's actions.recipe_2x2 is not clicks (no half 9 labels)" : "not a positive weight");
            return 2;
        }
        neth_distill_x2_weight(L->dist, (float)c->x2w);
        printf("ntloop: --x2-weight %g: a 2x2 recipe's label (half 9) weighs %g\n", c->x2w, c->x2w);
    }
    if (strcmp(c->rhold, "off"))
    {
        int rh = !strcmp(c->rhold, "on") ? 1 : !strcmp(c->rhold, "close") ? 2 : 0;
        if (!rh || !gv || !(level & NETH_VEC_RECIPEKEY))
        {
            printf("FAIL --recipe-hold %s: %s\n", c->rhold,
                   !rh  ? "on, close or off"
                   : !gv ? NOGV
                        : "a level without NETH_VEC_RECIPEKEY");
            return 2;
        }
        neth_distill_recipe_hold(L->dist, rh);
        printf("ntloop: --recipe-hold %s: the recipe's later steps under its window labelled as the jump held%s\n",
               c->rhold, rh == 2 ? ", its close as the inventory key" : "");
    }
    if (strcmp(c->wlab, "off"))
    {
        int wl = !strcmp(c->wlab, "on") ? 1 : !strcmp(c->wlab, "level") ? 2 : 0;
        const char *why = !wl ? "on, level or off"
                          : !gv ? NOGV
                          : !(level & NETH_VEC_NOTABLE) || !(level & NETH_VEC_RECIPEKEY)
                              ? "a level without NETH_VEC_NOTABLE and NETH_VEC_RECIPEKEY"
                          : wl == 2 && !strcmp(c->labeller, "off") ? "level without --labeller (no episode at the level)"
                                                                    : NULL;
        if (why)
        {
            printf("FAIL --window-label %s: %s\n", c->wlab, why);
            return 2;
        }
        neth_distill_window_label(L->dist, wl);
        printf("ntloop: --window-label %s: the policy's own window's screen-macro close relabelled (the jump "
               "where the plan fits, no key after the opening key)%s\n",
               c->wlab, wl == 2 ? "; at the level its own steps there too, the close elsewhere" : "");
    }
    if (strcmp(c->tlab, "off"))
    {
        const char *why = strcmp(c->tlab, "on") ? "on or off"
                          : !(level & NETH_VEC_NOTABLE) || !(level & NETH_VEC_TABLEKEY)
                              ? "a level without NETH_VEC_NOTABLE and NETH_VEC_TABLEKEY"
                          : !strstr(mopts, "table_chance=on") ? "the env's actions.table_chance off (no half 5 labels)"
                                                               : NULL;
        if (why)
        {
            printf("FAIL --table-label %s: %s\n", c->tlab, why);
            return 2;
        }
        neth_distill_table_label(L->dist, 1);
        printf("ntloop: --table-label on: a policy step with no screen and a crafting plan named labelled the "
               "table key (key 12)\n");
    }
    if (strcmp(c->seeklab, "off"))
    {
        const char *why = strcmp(c->seeklab, "on") ? "on or off"
                          : !(level & NETH_VEC_SEEKKEY) || (level & NETH_VEC_SEEK)
                              ? "a level without NETH_VEC_SEEKKEY or with the seek on"
                          : !strstr(mopts, "seek_chance=on") ? "the env's actions.seek_chance off (no half 7 labels)"
                                                              : NULL;
        if (why)
        {
            printf("FAIL --seek-label %s: %s\n", c->seeklab, why);
            return 2;
        }
        neth_distill_seek_label(L->dist, 1);
        printf("ntloop: --seek-label on: a policy step at the level with the seek's start holding labelled the "
               "seek key (key 13)\n");
    }
    if (c->beta > 0)
    {
        /* DAgger's way: the student as a second actor, driving a beta share of the episodes */
        size_t sl;
        unsigned char *sb = neth_ntw_blob(L->flat0, &sl);
        int r = neth_policy_actor(&L->PS, sb, sl, c->groups, L->gs, c->prec == NETH_LEARN_BF16, c->seed + 1, err, sizeof err);
        free(sb);
        if (r || neth_policy_actor_refresh(&L->PS, R->w, R->s) || cudaStreamSynchronize(R->s) != cudaSuccess)
        {
            printf("FAIL --beta: the student's actor: %s\n", r ? err : "its first refresh");
            return 1;
        }
        neth_policy_actor_switch(&L->PS);
        neth_distill_dagger(L->dist, c->beta, c->seed);
        printf("ntloop: --beta %g: the student drives that share of the episodes, the teacher labels every "
               "state\n", c->beta);
    }
    if (strstr(mopts, "gui=cursor"))
    {
        /* key 12 is the inventory key; the teacher's net samples it at random */
        if (!(c->beta > 0))
        {
            printf("FAIL actions.gui cursor with a teacher needs --beta (the teacher's key 12 is cleared in "
                   "DAgger's launch)\n");
            return 1;
        }
        neth_distill_gui(L->dist);
        printf("ntloop: actions.gui cursor: the teacher's key 12 (the inventory key) cleared\n");
    }
    if (strstr(mopts, "table_key=paused"))
    {
        /* NETH_VEC_TABLEKEY's key 13; the teacher's net samples it at random as it does key 12 */
        if (!(c->beta > 0))
        {
            printf("FAIL actions.table_key paused with a teacher needs --beta (the teacher's key 13 is cleared "
                   "in DAgger's launch)\n");
            return 1;
        }
        neth_distill_table_key(L->dist, NETH_VEC_KEY_PAUSED);
        printf("ntloop: actions.table_key paused: the table key is key 13, the teacher's key 13 cleared\n");
    }
    if (strcmp(c->labeller, "off"))
    {
        /* the student's episodes at its level, the removed macro labelling them (mods/macros/shadow.c) */
        const char *slabels = c->slabels;
        int lb = neth_macro_set(c->labeller, err, sizeof err);
        int so = !strcmp(slabels, "shadow") ? 1 : !strcmp(slabels, "teacher") ? 0 : !strcmp(slabels, "none") ? 2 : -1;
        /* or surface (L3b removes it); or craft (L4b removes it) */
        if (lb <= 0 || (lb != NETH_VEC_SEEK && lb != NETH_VEC_SURFACE && lb != NETH_VEC_CRAFT) || (lb & level) ||
            !(c->beta > 0) || so < 0 || !(c->lshare >= 0 && c->lshare <= 1))
        {
            printf("FAIL --labeller %s --student-labels %s --level-share %g: the labeller is seek, surface or craft (a "
                   "macro off at --level), with --beta above 0; student labels teacher, shadow or none; a share 0 to "
                   "1\n", c->labeller, slabels, c->lshare);
            return 2;
        }
        if (neth_vec_level(L->v, level, lb, err, sizeof err) || neth_vec_level_align(L->v, c->lalign) ||
            neth_vec_level_reach(L->v, c->lreach))
        {
            printf("FAIL --labeller (--labeller-align %g, --labeller-reach %g): %s\n", c->lalign, c->lreach, err);
            return 2;
        }
        if (c->lreach > 0)
            printf("ntloop: --labeller-reach %g: the labeller starts only on a seen cell within %g blocks\n", c->lreach,
                   c->lreach);
        if (c->lalign > 0)
            printf("ntloop: --labeller-align %g: the labeller's attack labelled only with the look within %g "
                   "degrees of its aim (or on its cell)\n", c->lalign, c->lalign);
        if (strcmp(c->walklab, "off"))
        {
            /* the seek's walk labelled beside the surface labeller */
            const char *why = strcmp(c->walklab, "on") ? "on or off"
                              : lb != NETH_VEC_SURFACE ? "the labeller is not surface"
                              : !(level & NETH_VEC_SEEKKEY) || (level & NETH_VEC_SEEK)
                                  ? "a level without NETH_VEC_SEEKKEY or with the seek on"
                              : !strstr(mopts, "seek_walk=policy") ? "the env's actions.seek_walk not policy"
                                                                    : NULL;
            if (why || neth_vec_level_walk(L->v, 1))
            {
                printf("FAIL --walk-label %s: %s\n", c->walklab, why ? why : "neth_vec_level_walk");
                return 2;
            }
            printf("ntloop: --walk-label on: on the level's episodes the seek's walk act labels the policy's "
                   "steps where its start holds on a cell past the dig's reach (half 8)\n");
        }
        neth_distill_labeller(L->dist, so, c->lshare);
        const uint8_t *dr = neth_distill_drive(L->dist);
        for (int i = 0; i < n; ++i) neth_vec_env_level(L->v, i, dr[i] == 2);
        L->lab = 1;
        printf("ntloop: --labeller %s --level-share %g: that share of the student's episodes play at macros %d with "
               "%s run beside them, its act the label where it would drive (never stepped), their other steps "
               "labelled by %s; the rest at the teacher's macros\n", c->labeller, c->lshare, level & c->macros,
               c->labeller, so ? "nothing (--student-labels shadow)" : "the teacher");
    }
    return 0;
}

/* the labels' census, and the craft labeller's books (zero with another labeller) */
void neth_distill_report(struct neth_pipe *L)
{
    if (!L->dist || L->fail) return;
    neth_distill_print(L->dist);
    if (!L->lab) return;
    int64_t cb[NETH_VEC_SHC_BOOKS];
    if (neth_vec_shc_books(L->v, cb) == 0 && (cb[0] || cb[1] || cb[2]))
        printf("distill: craft labeller: labels %lld from the macro's run (student equal %lld, unequal and rolled "
               "back %lld, unequal and restarted %lld), %lld tidying the window, %lld closing it; runs started %lld, "
               "ended %lld; a press labelled as the release (the key held the step before) %lld\n", (long long)cb[0],
               (long long)cb[7], (long long)cb[3], (long long)cb[4], (long long)cb[1], (long long)cb[2],
               (long long)cb[5], (long long)cb[6], (long long)cb[8]);
    int64_t cp[NETH_VEC_SHC_PLANS];
    if (neth_vec_shc_plans(L->v, cp) == 0)
        for (int p = 0; p < NETH_VEC_SHC_PLANS / 6; ++p)
        {
            const int64_t *q = cp + p * 6;
            if (!(q[0] + q[1] + q[2] + q[3] + q[4])) continue;
            const char *nm = neth_vec_pick_name(p + 1);
            printf("distill: craft labeller: %-16s the inventory key %lld, use %lld, a slot %lld, a GUI op %lld, "
                   "the look or nothing %lld; the student equal %lld\n", nm ? nm : "other", (long long)q[0],
                   (long long)q[1], (long long)q[2], (long long)q[3], (long long)q[4], (long long)q[5]);
        }
}
