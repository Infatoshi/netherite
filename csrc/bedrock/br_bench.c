#include "bedrock.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    printf("usage: bedrock_bench [--mode parity|speed|train|suite|bwd|all]\n"
           "  [--sim bedrock|PATH] [--algo ppo-bedrock|PATH] [--env NAME|PATH]\n"
           "  [--set KEY=VALUE ...] [--worlds N] [--hidden H] [--iters I] [--nvfp4 0|1] [--bwd fp16|fp8|nvfp4]\n");
}

static int parse_env_id(const char *s) {
    for (int i = 0; i < BR_ENV_COUNT; i++) if (!strcmp(s, br_env_name(i))) return i;
    return atoi(s);
}

static int parse_bwd_id(const char *s) {
    if (!strcmp(s, "fp8") || !strcmp(s, "fp8_e4m3")) return BR_BWD_FP8;
    if (!strcmp(s, "nvfp4") || !strcmp(s, "nvfp4_sr")) return BR_BWD_NVFP4;
    return BR_BWD_FP16;
}

static int parse_fwd_nvfp4(const char *s) {
    if (!strcmp(s, "fp16") || !strcmp(s, "0") || !strcmp(s, "off")) return 0;
    return 1;
}

static void trim_ws(char *s) {
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

/* Apply a dotted YAML key/value pair into cfg & iters; returns 1 if recognized, 0 otherwise */
static int apply_kv(const char *key, const char *val, br_train_cfg_t *cfg, int *iters, int strict) {
    if (!strcmp(key, "world.cow_seeds"))               { cfg->cow_seeds = atoi(val); return 1; }
    if (!strcmp(key, "world.persisting_l2_mb"))        { cfg->persisting_l2_mb = atoi(val); return 1; }
    if (!strcmp(key, "client.ticks_per_step"))         { cfg->ticks_per_step = atoi(val); return 1; }
    if (!strcmp(key, "pipeline.worlds"))               { cfg->n_worlds = atoi(val); return 1; }
    if (!strcmp(key, "pipeline.queue_chunk_q"))        { cfg->chunk_q = atoi(val); return 1; }
    if (!strcmp(key, "pipeline.episode_max_ticks"))    { cfg->episode_max_ticks = atoi(val); return 1; }
    if (!strcmp(key, "net.hidden") || !strcmp(key, "policy.hidden")) { cfg->hidden = atoi(val); return 1; }
    if (!strcmp(key, "net.forward_precision") || !strcmp(key, "policy.trunk_precision")) {
        cfg->use_nvfp4 = parse_fwd_nvfp4(val); return 1;
    }
    if (!strcmp(key, "net.backward_precision") || !strcmp(key, "policy.backward_precision")) {
        cfg->bwd_prec = parse_bwd_id(val); return 1;
    }
    if (!strcmp(key, "ppo.lr"))                        { cfg->lr = (float)atof(val); return 1; }
    if (!strcmp(key, "ppo.ent"))                       { cfg->ent_coef = (float)atof(val); return 1; }
    if (!strcmp(key, "ppo.clip"))                      { cfg->clip_eps = (float)atof(val); return 1; }
    if (!strcmp(key, "ppo.gamma"))                     { cfg->gamma = (float)atof(val); return 1; }
    if (!strcmp(key, "ppo.lam"))                       { cfg->gae_lam = (float)atof(val); return 1; }
    if (!strcmp(key, "ppo.epochs") || !strcmp(key, "policy.ppo_epochs")) { cfg->ppo_epochs = atoi(val); return 1; }
    if (!strcmp(key, "ppo.rollout_t") || !strcmp(key, "policy.rollout_t")) { cfg->rollout_t = atoi(val); return 1; }
    if (!strcmp(key, "ppo.grad_loss_scale"))           { cfg->grad_loss_scale = (float)atof(val); return 1; }
    if (!strcmp(key, "run.iters"))                     { *iters = atoi(val); return 1; }
    if (!strcmp(key, "run.seed"))                      { cfg->seed = strtoull(val, NULL, 0); return 1; }
    if (!strcmp(key, "name") || !strcmp(key, "policy.bedrock.env_id")) {
        cfg->env_id = parse_env_id(val); return 1;
    }
    if (!strcmp(key, "episode.seconds")) {
        int sec = atoi(val);
        if (sec > 0) cfg->episode_max_ticks = sec * 20;
        return 1;
    }
    if (!strcmp(key, "policy.bedrock.target_count"))   { cfg->target_count = atoi(val); return 1; }
    if (!strcmp(key, "policy.bedrock.break_ticks"))    { cfg->break_ticks = atoi(val); return 1; }
    if (strict) {
        fprintf(stderr, "bedrock_bench: unknown --set key '%s'\n", key);
        exit(2);
    }
    return 0;
}

static void resolve_yaml_path(const char *dir, const char *name_or_path, char *out, size_t out_sz) {
    if (strchr(name_or_path, '/') || (strlen(name_or_path) > 5 && !strcmp(name_or_path + strlen(name_or_path) - 5, ".yaml")))
        snprintf(out, out_sz, "%s", name_or_path);
    else
        snprintf(out, out_sz, "configs/%s/%s.yaml", dir, name_or_path);
}

/* Minimal indentation-aware YAML reader for configs/sim, configs/algo, and configs/envs */
static int load_yaml_file(const char *path, br_train_cfg_t *cfg, int *iters, int required) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (required) {
            fprintf(stderr, "bedrock_bench: cannot open YAML '%s'\n", path);
            exit(2);
        }
        return 0;
    }
    char stack[6][64];
    int  indents[6] = {-1, -1, -1, -1, -1, -1};
    int  depth = 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        int ind = 0;
        while (line[ind] == ' ') ind++;
        char *p = line + ind;
        trim_ws(p);
        if (*p == '\0' || *p == '-') continue;
        char *colon = strchr(p, ':');
        if (!colon) continue;
        *colon = '\0';
        char key[64];
        snprintf(key, sizeof(key), "%.63s", p);
        trim_ws(key);
        char *val = colon + 1;
        trim_ws(val);

        while (depth > 0 && indents[depth - 1] >= ind) depth--;
        if (*val == '\0') {
            if (depth < 6) {
                snprintf(stack[depth], sizeof(stack[depth]), "%.63s", key);
                indents[depth] = ind;
                depth++;
            }
        } else {
            char full[512];
            size_t pos = 0;
            full[0] = '\0';
            for (int d = 0; d < depth && pos + 2 < sizeof(full); d++) {
                int w = snprintf(full + pos, sizeof(full) - pos, "%s.", stack[d]);
                if (w > 0) pos += (size_t)w;
            }
            if (pos < sizeof(full)) snprintf(full + pos, sizeof(full) - pos, "%s", key);
            apply_kv(full, val, cfg, iters, 0);
        }
    }
    fclose(fp);
    return 1;
}

static int run_parity_suite(int worlds, int ticks) {
    int fails = 0;
    const char *pnames[2] = {"local-fp32", "fixed-q16"};
    for (int prec = 0; prec < 2; prec++) {
        for (int rung = 0; rung <= 2; rung++) {
            br_fidelity_t rep;
            br_run_fidelity_gate(rung, prec, worlds, ticks, &rep);
            double tol_p50 = (prec == BR_PREC_LOCAL_FP32) ? 1e-4 : 5e-2;
            int ok = (rep.p50_pos_err <= tol_p50) && (rep.ground_match_pct >= 99.0) &&
                     (rung < 2 || rep.win_match_pct >= 99.0);
            if (prec == BR_PREC_LOCAL_FP32 && rep.max_pos_err > 1e-3) ok = 0;
            if (!ok) fails++;
            printf("PARITY\tprec=%s\trung=%d\tworlds=%d\tticks=%d\tp50_pos=%.6f\tp90_pos=%.6f\tmax_pos=%.6f\tgnd_pct=%.2f\twin_pct=%.2f\tcpu_wins=%d\tgpu_wins=%d\tstatus=%s\n",
                   pnames[prec], rung, worlds, ticks,
                   rep.p50_pos_err, rep.p90_pos_err, rep.max_pos_err,
                   rep.ground_match_pct, rep.win_match_pct,
                   rep.cpu_wins, rep.gpu_wins, ok ? "PASS" : "FAIL");
        }
    }
    double cos_sim = 0.0, rmse = 0.0;
    int nv_fail = br_check_nvfp4_gemm(&cos_sim, &rmse);
    if (nv_fail) fails++;
    printf("PARITY_NVFP4\tswizzle=128x4_ue4m3\tepilogue=relu\tcos_sim=%.6f\trmse=%.4f\tstatus=%s\n",
           cos_sim, rmse, nv_fail ? "FAIL" : "PASS");
    return fails;
}

static void run_one_train(const br_train_cfg_t *base_cfg, int env_id, int hidden, int iters, int use_nvfp4, int bwd_prec, int summary_only, const char *dump_trace_path) {
    br_train_cfg_t cfg = *base_cfg;
    cfg.env_id    = env_id;
    cfg.hidden    = hidden;
    cfg.use_nvfp4 = use_nvfp4;
    cfg.bwd_prec  = bwd_prec;
    if (cfg.chunk_q <= 0 || cfg.chunk_q > cfg.n_worlds) cfg.chunk_q = (cfg.n_worlds >= 8192) ? 8192 : cfg.n_worlds;
    cfg.seed = (cfg.seed ? cfg.seed : 0x20261123ULL) + (uint64_t)env_id * 17ULL;

    br_trainer_t *tr = br_trainer_create(&cfg);
    br_train_stats_t st;
    br_trainer_step(tr, 0, &st); /* warmup */

    double sum_ms = 0.0, sum_roll = 0.0, sum_learn = 0.0;
    double best_win_pct = 0.0, best_rew = -1e9, first_rew = 0.0, last_rew = 0.0;
    int best_iter = 0;
    for (int it = 1; it <= iters; it++) {
        br_trainer_step(tr, it, &st);
        sum_ms += st.total_ms; sum_roll += st.rollout_ms; sum_learn += st.learner_ms;
        if (it == 1) first_rew = st.mean_step_rew;
        last_rew = st.mean_step_rew;
        if (st.win_rate_pct > best_win_pct) { best_win_pct = st.win_rate_pct; best_iter = it; }
        if (st.mean_step_rew > best_rew) best_rew = st.mean_step_rew;
        if (!summary_only && (it == 1 || it == iters || (it % 5 == 0))) {
            printf("TRAIN\tenv=%s\tfwd=%s\tbwd=%s\tH=%d\tN=%d\titer=%d\tstep_rew=%.4f\twins=%.0f\twin_pct=%.2f\tploss=%.4f\tvloss=%.4f\tent=%.3f\troll_ms=%.2f\tlearn_ms=%.2f\ttot_ms=%.2f\tMticks_s=%.1f\n",
                   br_env_name(env_id), use_nvfp4 ? "nvfp4" : "fp16", br_bwd_name(bwd_prec),
                   hidden, cfg.n_worlds, it, st.mean_step_rew, st.total_wins, st.win_rate_pct,
                   st.policy_loss, st.value_loss, st.entropy,
                   st.rollout_ms, st.learner_ms, st.total_ms, st.msteps_s);
        }
    }
    double avg_ms = sum_ms / iters, avg_roll = sum_roll / iters, avg_learn = sum_learn / iters;
    double avg_mticks = ((double)cfg.n_worlds * cfg.rollout_t * cfg.ticks_per_step) / (avg_ms * 1e3);
    double t_peak_s = (best_iter > 0) ? (best_iter * avg_ms / 1e3) : 0.0;
    printf("SUMMARY\tenv=%s\tfwd=%s\tbwd=%s\tH=%d\tN=%d\titers=%d\twall_s=%.3f\tpeak_s=%.3f\tavg_roll_ms=%.2f\tavg_learn_ms=%.2f\tavg_tot_ms=%.2f\tMticks_s=%.1f\trew_start=%.4f\trew_peak=%.4f\trew_end=%.4f\tpeak_win_pct=%.2f\tpeak_iter=%d\n",
           br_env_name(env_id), use_nvfp4 ? "nvfp4" : "fp16", br_bwd_name(bwd_prec),
           hidden, cfg.n_worlds, iters, sum_ms / 1e3, t_peak_s, avg_roll, avg_learn, avg_ms, avg_mticks,
           first_rew, best_rew, last_rew, best_win_pct, best_iter);
    if (dump_trace_path) {
        int won = br_trainer_dump_trace(tr, dump_trace_path, 90);
        printf("TRACE\tpath=%s\tenv=%s\twon=%d\n", dump_trace_path, br_env_name(env_id), won);
    }
    br_trainer_destroy(tr);
}

int main(int argc, char **argv) {
    const char *mode = "all";
    const char *sim_arg = "bedrock", *algo_arg = "ppo-bedrock", *env_arg = NULL, *dump_trace = NULL;
    int sim_explicit = 0, algo_explicit = 0;
    int ticks = 120;

    /* Pass 1: discover --sim / --algo / --env paths before applying CLI / --set overrides */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--sim") && i + 1 < argc)  { sim_arg = argv[++i]; sim_explicit = 1; }
        else if (!strcmp(argv[i], "--algo") && i + 1 < argc) { algo_arg = argv[++i]; algo_explicit = 1; }
        else if (!strcmp(argv[i], "--env") && i + 1 < argc)  { env_arg = argv[++i]; }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { print_usage(); return 0; }
    }

    br_train_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.env_id = BR_ENV_WOOD_PUNCH;
    cfg.n_worlds = 16384; cfg.chunk_q = 8192;
    cfg.cow_seeds = 64; cfg.persisting_l2_mb = 32;
    cfg.episode_max_ticks = 400;
    cfg.hidden = 1024; cfg.rollout_t = 16; cfg.ticks_per_step = 4; cfg.ppo_epochs = 3;
    cfg.lr = 2.5e-4f; cfg.ent_coef = 0.01f; cfg.clip_eps = 0.2f;
    cfg.gamma = 0.99f; cfg.gae_lam = 0.95f; cfg.grad_loss_scale = 16.0f;
    cfg.use_nvfp4 = 1; cfg.bwd_prec = BR_BWD_NVFP4; cfg.seed = 0x20261123ULL;
    int iters = 35;

    /* Load YAML layers in netherite order: sim -> algo -> env */
    char path[256];
    resolve_yaml_path("sim", sim_arg, path, sizeof(path));
    int loaded_sim = load_yaml_file(path, &cfg, &iters, sim_explicit);
    resolve_yaml_path("algo", algo_arg, path, sizeof(path));
    int loaded_algo = load_yaml_file(path, &cfg, &iters, algo_explicit);
    int loaded_env = 0;
    if (env_arg) {
        resolve_yaml_path("envs", env_arg, path, sizeof(path));
        loaded_env = load_yaml_file(path, &cfg, &iters, strchr(env_arg, '/') != NULL);
        if (!loaded_env) cfg.env_id = parse_env_id(env_arg);
    }

    /* Pass 2: apply CLI flags & --set KEY=VALUE overrides (last word!) */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = argv[++i];
        else if (!strcmp(argv[i], "--sim") && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--algo") && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--env") && i + 1 < argc) i++;
        else if (!strcmp(argv[i], "--worlds") && i + 1 < argc) cfg.n_worlds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hidden") && i + 1 < argc) cfg.hidden = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ticks") && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--iters") && i + 1 < argc) iters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nvfp4") && i + 1 < argc) cfg.use_nvfp4 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bwd") && i + 1 < argc) cfg.bwd_prec = parse_bwd_id(argv[++i]);
        else if (!strcmp(argv[i], "--dump-trace") && i + 1 < argc) dump_trace = argv[++i];
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) {
            char kv[256];
            snprintf(kv, sizeof(kv), "%s", argv[++i]);
            char *eq = strchr(kv, '=');
            if (!eq) { fprintf(stderr, "bedrock_bench: --set expects KEY=VALUE, got '%s'\n", kv); return 2; }
            *eq = '\0';
            apply_kv(kv, eq + 1, &cfg, &iters, 1);
        }
    }

    if (!strcmp(mode, "train") || !strcmp(mode, "suite")) {
        printf("CONFIG\tsim=%s(%s)\talgo=%s(%s)\tenv=%s(%s)\tN=%d\tQ=%d\tH=%d\tfwd=%s\tbwd=%s\tlr=%.1e\tgscale=%.1f\tepochs=%d\tT=%d\tK=%d\tmax_t=%d\titers=%d\n",
               sim_arg, loaded_sim ? "yaml" : "def",
               algo_arg, loaded_algo ? "yaml" : "def",
               br_env_name(cfg.env_id), loaded_env ? "yaml" : "def",
               cfg.n_worlds, cfg.chunk_q, cfg.hidden,
               cfg.use_nvfp4 ? "nvfp4" : "fp16", br_bwd_name(cfg.bwd_prec),
               cfg.lr, cfg.grad_loss_scale, cfg.ppo_epochs, cfg.rollout_t, cfg.ticks_per_step,
               cfg.episode_max_ticks, iters);
    }

    int fails = 0;
    if (!strcmp(mode, "parity") || !strcmp(mode, "all")) fails = run_parity_suite(128, ticks);
    if (!strcmp(mode, "speed")  || !strcmp(mode, "all")) br_bench_square_and_queue();
    if (!strcmp(mode, "train")) run_one_train(&cfg, cfg.env_id, cfg.hidden, iters, cfg.use_nvfp4, cfg.bwd_prec, 0, dump_trace);
    if (!strcmp(mode, "bwd") || !strcmp(mode, "all")) {
        const int Hs[3] = {1024, 2048, 4096};
        const int Is[3] = {15, 15, 10};
        for (int hi = 0; hi < 3; hi++) {
            for (int bp = 0; bp < 3; bp++)
                run_one_train(&cfg, BR_ENV_WOOD_PUNCH, Hs[hi], Is[hi], 1, bp, 1, NULL);
        }
    }
    if (!strcmp(mode, "suite") || !strcmp(mode, "all")) {
        for (int e = 0; e < BR_ENV_COUNT; e++) {
            br_train_cfg_t ecfg = cfg;
            int e_iters = iters;
            ecfg.target_count = 0; ecfg.break_ticks = 0;
            resolve_yaml_path("envs", br_env_name(e), path, sizeof(path));
            load_yaml_file(path, &ecfg, &e_iters, 0);
            run_one_train(&ecfg, e, ecfg.hidden, e_iters, ecfg.use_nvfp4, ecfg.bwd_prec, 1, NULL);
        }
    }
    return fails ? 1 : 0;
}
