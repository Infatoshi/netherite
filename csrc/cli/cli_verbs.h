/* nether CLI: the verbs in their own files, and what flywheel shares with eval (csrc/cli/cli_*.c). */
#ifndef NETH_CLI_VERBS_H
#define NETH_CLI_VERBS_H

#include "cli.h"
#include "cli_stats.h"

int v_eval(int argc, char **argv);
int v_ab(int argc, char **argv);
int v_gate(int argc, char **argv);
int v_harvest(int argc, char **argv);
int v_flywheel(int argc, char **argv);
int v_stats(int argc, char **argv);

/* one model's frozen eval through ntloop --score, kept in a cache keyed by everything that decides its episodes:
 * the model's bytes, the trainer binary's bytes, the derived run file's content and the flags */
struct ce_job {
    const char *verb;
    const char *bin;          /* ntloop */
    const char *run;          /* the derived run file */
    const struct sv *args;    /* ntloop flags but --run, --init, --out, --score-json */
    const char *gpu;          /* CUDA_VISIBLE_DEVICES for it, NULL inherits */
    const char *cache;        /* the cache's directory */
    int no_cache;
    double timeout_s;
};
/* model evaluated (or found in the cache): its directory in dir (PATH_MAX), *hit 1 when cached; 0 or the failure */
int ce_eval_model(const struct ce_job *j, const char *model, char *dir, int *hit);
/* a split file's seeds as "A,B,C" (lines of seeds, # comments; or a splits.json's "eval_seeds"); 0 or 2 */
int ce_split_seeds(const char *verb, const char *path, const char *key, struct sb *out);
/* the comparison written to json (when given) and printed */
int ce_report(const char *cand_dir, const char *base_dir, const char *json, struct cs_run *cr, struct cs_summary *cs,
              struct cs_run *br, struct cs_summary *bs, struct cs_compare *cmp, int *have_base);

#endif
