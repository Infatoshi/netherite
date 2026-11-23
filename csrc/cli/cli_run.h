/* nether CLI: run files and the native trainer, shared by train, eval and flywheel (csrc/cli/cli_run.c).
 *
 * A derived run file is the given one with --set KEY=VALUE overrides (and, for an eval, the fidelity file's forced
 * keys) applied, written into the run's own output directory: configs/ never collects temporary files. */
#ifndef NETH_CLI_RUN_H
#define NETH_CLI_RUN_H

#include "cli.h"
#include "cli_yaml.h"

struct cr_trainer {
    char variant[64];       /* "" the default 16x16 build */
    char bin[PATH_MAX];     /* out/runtime/train/[V/]ntloop, absolute */
    char ntinit[PATH_MAX];  /* its ntinit */
};

/* the trainer for VARIANT (NULL: from the run file's client.observation, as nether train always chose), built by
 * make when missing (gpu: the CUDA_DEV the build queries the architecture of, "" none); need_init builds ntinit too */
int cr_trainer_find(const char *verb, const char *variant, const struct cy *run, const char *gpu, int need_init,
                    struct cr_trainer *t);

/* src with sets (KEY=VALUE each) and, when fid is given (a fidelity YAML file: set: forced keys, args: ntloop flags,
 * refuse: flags an exact eval never takes), its forced keys applied, written to dst; fid's args appended to fargs.
 * Notes on what was changed go to stdout. 0, or 2 on a refusal or an error. */
int cr_derive(const char *verb, const char *src, const struct sv *sets, const char *fid, const char *dst, struct sv *fargs);
/* the fidelity file's refuse list against extra ntloop args: 0, or 2 (printed) */
int cr_refused(const char *verb, const char *fid, const struct sv *extra);

/* a run file found from here or from the checkout, absolute in out (PATH_MAX); 0 or 2 */
int cr_path(const char *verb, const char *p, char *out);

/* the eval's size when a gym or a flag does not give it: the fidelity file's eval.KEY (episodes, seed), copied to
 * out; 0, or 2 (printed: no such key, so C holds no default of its own) */
int cr_eval_default(const char *verb, const char *fid, const char *key, char *out, size_t n);

/* a run file's pipeline.start made absolute the way the engine resolves it for a file in src_dir (worldconf.c
 * wconf_start_path: src_dir, or configs/'s parent for a file in configs/), so a copy written elsewhere starts there */
void cr_start_abs(struct cy *t, const char *src_dir);

/* a section's keys as ntloop flags: KEY (with _ as -) VALUE each, flags: its words as they are; skip names keys left
 * out (a gym's eval section and a composed env's eval section alike); 0, or 2 (printed) */
int cr_section_flags(const char *verb, const struct cy *y, const char *sec, struct sv *out, const char *const *skip);

#endif
