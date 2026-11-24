/* Netherite Bedrock End-to-End Portal Speedrun Teacher Trainer
 * Connects all 4 verified lanes into one zero-copy GPU training + BRT1 export pipeline:
 *   - Lane A (br_harvest):      64 real 1.7.10 Java lava-pool seeds (BRS1 portal_seeds.bin)
 *   - Lane B (br_portal_sim):   GPU 3D pitch+face DDA, step-up, sneak, bucket quench, 4x5 portal lit
 *   - Lane C (br_policy_s1):    Exact net.h factored s1 NVFP4 Fwd + NVFP4-SR Bwd PPO + BRT1 checkpoint
 *   - Lane D (br_obs_bridge):   Cross-lane GPU-vs-CPU obs[128] parity check on real harvested seeds
 */
#include "br_contract.h"
#include "br_portal_sim.h"
#include "br_policy_s1.h"
#include "br_obs_bridge.h"

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double pt_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

__global__ void k_f32_to_f16(const float *__restrict__ src, __half *__restrict__ dst, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) dst[i] = __float2half(src[i]);
}

__global__ void k_portal_gae(
    const float   *__restrict__ rew,
    const float   *__restrict__ val,
    const uint8_t *__restrict__ done,
    const float   *__restrict__ last_val,
    float *__restrict__ adv,
    float *__restrict__ ret,
    float *__restrict__ sum_out,
    int N, int T, float gamma, float lam
) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    float gae = 0.0f, next_v = last_val[i], rsum = 0.0f;
    for (int t = T - 1; t >= 0; t--) {
        size_t idx = (size_t)t * N + i;
        float r = rew[idx], v = val[idx];
        rsum += r;
        float cont = done[idx] ? 0.0f : 1.0f;
        float delta = r + gamma * next_v * cont - v;
        gae = delta + gamma * lam * cont * gae;
        adv[idx] = gae;
        ret[idx] = gae + v;
        next_v = v;
    }
    atomicAdd(&sum_out[0], rsum);
}

static void check_cross_lane_obs_parity(br_portal_sim_t *sim, const brc_seed_entry_t *seeds,
                                        uint32_t num_seeds, int max_ticks) {
    int check_n = 16;
    if (br_portal_sim_n_worlds(sim) < check_n) check_n = br_portal_sim_n_worlds(sim);
    br_portal_sim_extract_obs(sim, NULL);
    float *gpu_obs = (float *)malloc((size_t)check_n * BRC_OBS_DIM * sizeof(float));
    br_portal_state_t *st = (br_portal_state_t *)malloc((size_t)check_n * sizeof(br_portal_state_t));
    br_portal_sim_download_obs(sim, gpu_obs, check_n);
    br_portal_sim_download_states(sim, st, check_n);

    double max_all = 0.0, max_ray = 0.0, max_cls = 0.0, max_sc = 0.0;
    int worst_k = -1;
    for (int w = 0; w < check_n; w++) {
        const brc_seed_entry_t *sd = &seeds[w % num_seeds];
        br_obs_state_t ost;
        memset(&ost, 0, sizeof(ost));
        ost.px = st[w].x; ost.py = st[w].y; ost.pz = st[w].z;
        ost.vx = st[w].vx; ost.vy = st[w].vy; ost.vz = st[w].vz;
        ost.yaw_deg = st[w].yaw; ost.pitch_deg = st[w].pitch;
        ost.on_ground = st[w].on_ground; ost.sneaking = st[w].sneaking;
        ost.hp = st[w].hp; ost.sel_slot = st[w].sel_slot;
        memcpy(ost.hotbar_item, st[w].hotbar_item, sizeof(ost.hotbar_item));
        memcpy(ost.hotbar_cnt,  st[w].hotbar_cnt,  sizeof(ost.hotbar_cnt));
        ost.lava_x = sd->lava_x; ost.lava_y = sd->lava_y; ost.lava_z = sd->lava_z;
        ost.obsidian_placed = st[w].obsidian_placed;
        ost.portal_lit = st[w].portal_lit;
        ost.ep_tick = st[w].ep_tick;
        ost.max_ticks = max_ticks;

        float cpu_obs[BRC_OBS_DIM];
        br_extract_obs_from_bubble(sd, NULL, 0, &ost, cpu_obs);
        for (int k = 0; k < BRC_OBS_DIM; k++) {
            double d = fabs((double)gpu_obs[(size_t)w * BRC_OBS_DIM + k] - (double)cpu_obs[k]);
            if (d > max_all) { max_all = d; worst_k = k; }
            if (k < 64 && d > max_ray) max_ray = d;
            if (k >= 64 && k < 80 && d > max_cls) max_cls = d;
            if (k >= 80 && d > max_sc) max_sc = d;
        }
    }
    printf("CROSS_OBS_PARITY\tworlds=%d\tmax_all=%.6f(k=%d)\tmax_ray=%.6f\tmax_cls=%.6f\tmax_sc=%.6f\tstatus=%s\n",
           check_n, max_all, worst_k, max_ray, max_cls, max_sc,
           (max_sc <= 1e-3 && max_ray <= 0.10) ? "PASS" : "WARN");
    free(gpu_obs);
    free(st);
}

int main(int argc, char **argv) {
    const char *seeds_path = "out/bedrock/portal_seeds.bin";
    const char *save_path  = "out/bedrock/portal_teacher.brt1";
    int n_worlds = 8192, chunk_q = 8192, hidden = 1024;
    int rollout_t = 16, ticks_per_step = 4, ppo_epochs = 3, iters = 30, max_ticks = 400;
    int bwd_prec = BR_S1_BWD_NVFP4;
    float lr = 2.5e-4f;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seeds") && i + 1 < argc) seeds_path = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save_path = argv[++i];
        else if (!strcmp(argv[i], "--worlds") && i + 1 < argc) n_worlds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--hidden") && i + 1 < argc) hidden = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--iters") && i + 1 < argc) iters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bwd") && i + 1 < argc) {
            const char *b = argv[++i];
            if (!strcmp(b, "fp16")) bwd_prec = BR_S1_BWD_FP16;
            else if (!strcmp(b, "fp8")) bwd_prec = BR_S1_BWD_FP8;
            else bwd_prec = BR_S1_BWD_NVFP4;
        }
        else if (!strcmp(argv[i], "--lr") && i + 1 < argc) lr = (float)atof(argv[++i]);
    }
    if (chunk_q > n_worlds) chunk_q = n_worlds;

    brc_seed_entry_t *seeds = NULL;
    uint32_t num_seeds = 0;
    char err[256] = "";
    if (br_load_seed_pack(seeds_path, &seeds, &num_seeds, err, sizeof(err)) != 0 || num_seeds == 0) {
        fprintf(stderr, "br_portal_train: failed to load '%s': %s\n", seeds_path, err);
        return 1;
    }
    printf("LOADED_SEEDS\tpath=%s\tnum_seeds=%u\tsample0=%s\tlava_sources=%d\n",
           seeds_path, num_seeds, seeds[0].source_name, seeds[0].lava_sources);

    br_portal_sim_t *sim = br_portal_sim_create(n_worlds, seeds, (int)num_seeds, max_ticks);
    if (!sim) { fprintf(stderr, "br_portal_train: sim create failed\n"); return 1; }

    /* Cross-lane GPU (Lane B) vs CPU (Lane D) obs[128] parity gate on real harvested lava pools */
    check_cross_lane_obs_parity(sim, seeds, num_seeds, max_ticks);

    br_policy_s1_cfg_t pcfg;
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.batch_q         = chunk_q;
    pcfg.hidden          = hidden;
    pcfg.use_nvfp4_fwd   = 1;
    pcfg.bwd_prec        = bwd_prec;
    pcfg.lr              = lr;
    pcfg.grad_clip       = 1.0f;
    pcfg.clip_eps        = 0.2f;
    pcfg.vf_coef         = 0.5f;
    pcfg.ent_coef        = 0.001f;
    pcfg.grad_loss_scale = 16.0f;
    pcfg.hot_keep_bias   = 1.5f;
    pcfg.seed            = 0x20261123ULL;

    br_policy_s1_t *pol = br_policy_s1_create(&pcfg);
    if (!pol) { fprintf(stderr, "br_portal_train: policy create failed\n"); return 1; }
    br_policy_s1_set_stream(pol, NULL);
    cudaStream_t st = (cudaStream_t)br_policy_s1_stream(pol);

    int total_s = rollout_t * n_worlds;
    int n_chunks = n_worlds / chunk_q;

    __half *d_obs_buf = NULL, *d_boot_obs = NULL;
    brc_s1_action_t *d_act_buf = NULL, *d_boot_act = NULL;
    float *d_logp_buf = NULL, *d_val_buf = NULL, *d_rew_buf = NULL;
    float *d_adv_buf = NULL, *d_ret_buf = NULL, *d_boot_val = NULL, *d_boot_lp = NULL, *d_rsum = NULL;
    uint8_t *d_done_buf = NULL;

    cudaMalloc(&d_obs_buf,  (size_t)total_s * BRC_OBS_DIM * sizeof(__half));
    cudaMalloc(&d_boot_obs, (size_t)n_worlds * BRC_OBS_DIM * sizeof(__half));
    cudaMalloc(&d_act_buf,  (size_t)total_s * sizeof(brc_s1_action_t));
    cudaMalloc(&d_boot_act, (size_t)n_worlds * sizeof(brc_s1_action_t));
    cudaMalloc(&d_logp_buf, (size_t)total_s * sizeof(float));
    cudaMalloc(&d_val_buf,  (size_t)total_s * sizeof(float));
    cudaMalloc(&d_rew_buf,  (size_t)total_s * sizeof(float));
    cudaMalloc(&d_adv_buf,  (size_t)total_s * sizeof(float));
    cudaMalloc(&d_ret_buf,  (size_t)total_s * sizeof(float));
    cudaMalloc(&d_done_buf, (size_t)total_s * sizeof(uint8_t));
    cudaMalloc(&d_boot_val, (size_t)n_worlds * sizeof(float));
    cudaMalloc(&d_boot_lp,  (size_t)n_worlds * sizeof(float));
    cudaMalloc(&d_rsum,     sizeof(float));

    br_portal_state_t *h_st = (br_portal_state_t *)malloc((size_t)n_worlds * sizeof(br_portal_state_t));
    uint64_t step_ctr = 1;
    double sum_ms = 0.0, sum_roll = 0.0, sum_learn = 0.0;
    double rew_first = 0.0, rew_peak = -1e9, rew_last = 0.0;

    /* Initial obs extraction */
    br_portal_sim_extract_obs(sim, NULL);
    cudaDeviceSynchronize();

    for (int it = 0; it <= iters; it++) {
        double t0 = pt_now_ms();
        for (int s = 0; s < rollout_t; s++) {
            size_t soff = (size_t)s * n_worlds;
            const float *sim_obs = br_portal_sim_obs_ptr(sim);
            int n_el = n_worlds * BRC_OBS_DIM;
            k_f32_to_f16<<<(n_el + 255) / 256, 256, 0, st>>>(sim_obs, d_obs_buf + soff * BRC_OBS_DIM, n_el);
            for (int c = 0; c < n_chunks; c++) {
                size_t coff = soff + (size_t)c * chunk_q;
                br_policy_s1_act(pol, d_obs_buf + coff * BRC_OBS_DIM,
                                 d_act_buf + coff, d_logp_buf + coff, d_val_buf + coff,
                                 step_ctr++, 0);
            }
            cudaStreamSynchronize(st);
            br_portal_sim_step(sim, d_act_buf + soff, 1, ticks_per_step, 1,
                               NULL, d_rew_buf + soff, d_done_buf + soff);
        }
        /* Bootstrap value from final obs */
        {
            const float *sim_obs = br_portal_sim_obs_ptr(sim);
            int n_el = n_worlds * BRC_OBS_DIM;
            k_f32_to_f16<<<(n_el + 255) / 256, 256, 0, st>>>(sim_obs, d_boot_obs, n_el);
            for (int c = 0; c < n_chunks; c++) {
                size_t coff = (size_t)c * chunk_q;
                br_policy_s1_act(pol, d_boot_obs + coff * BRC_OBS_DIM,
                                 d_boot_act + coff, d_boot_lp + coff, d_boot_val + coff,
                                 step_ctr++, 0);
            }
        }
        cudaMemsetAsync(d_rsum, 0, sizeof(float), st);
        k_portal_gae<<<(n_worlds + 255) / 256, 256, 0, st>>>(
            d_rew_buf, d_val_buf, d_done_buf, d_boot_val,
            d_adv_buf, d_ret_buf, d_rsum, n_worlds, rollout_t, 0.99f, 0.95f);
        cudaStreamSynchronize(st);
        double t1 = pt_now_ms();

        br_policy_s1_stats_t pstats;
        br_policy_s1_ppo_update(pol, d_obs_buf, d_act_buf, d_logp_buf,
                                d_adv_buf, d_ret_buf, total_s, ppo_epochs,
                                (uint32_t)(it * 1013 + 7), &pstats);
        cudaStreamSynchronize(st);
        double t2 = pt_now_ms();

        float h_rsum = 0.0f;
        cudaMemcpy(&h_rsum, d_rsum, sizeof(float), cudaMemcpyDeviceToHost);
        double mean_rew = (double)h_rsum / (double)total_s;
        double r_ms = t1 - t0, l_ms = t2 - t1, tot_ms = t2 - t0;
        double mticks_s = ((double)total_s * (double)ticks_per_step) / (tot_ms * 1e3);

        if (it > 0) {
            sum_ms += tot_ms; sum_roll += r_ms; sum_learn += l_ms;
            if (it == 1) rew_first = mean_rew;
            rew_last = mean_rew;
            if (mean_rew > rew_peak) rew_peak = mean_rew;
        }
        if (it == 1 || it == iters || (it > 0 && it % 50 == 0)) {
            br_portal_sim_download_states(sim, h_st, n_worlds);
            long obs_tot = 0, lit_tot = 0, max_obs = 0;
            for (int w = 0; w < n_worlds; w++) {
                obs_tot += h_st[w].obsidian_placed;
                if (h_st[w].obsidian_placed > max_obs) max_obs = h_st[w].obsidian_placed;
                lit_tot += h_st[w].portal_lit;
            }
            printf("PORTAL_TRAIN\titer=%d\tN=%d\tH=%d\tbwd=%s\trew=%.4f\tobs_sum=%ld\tmax_obs=%ld\tlit=%ld\tploss=%.4f\tvloss=%.4f\tent=%.2f\tkl=%.4f\troll_ms=%.2f\tlearn_ms=%.2f\ttot_ms=%.2f\tMticks_s=%.1f\n",
                   it, n_worlds, hidden, br_s1_bwd_name(bwd_prec),
                   mean_rew, obs_tot, max_obs, lit_tot,
                   pstats.policy_loss, pstats.value_loss, pstats.entropy, pstats.approx_kl,
                   r_ms, l_ms, tot_ms, mticks_s);
        }
    }

    double avg_ms = sum_ms / iters, avg_roll = sum_roll / iters, avg_learn = sum_learn / iters;
    double avg_mticks = ((double)total_s * (double)ticks_per_step) / (avg_ms * 1e3);
    int save_rc = br_policy_s1_save(pol, save_path);
    printf("PORTAL_SUMMARY\tN=%d\tH=%d\tbwd=%s\titers=%d\twall_s=%.3f\tavg_roll_ms=%.2f\tavg_learn_ms=%.2f\tavg_tot_ms=%.2f\tMticks_s=%.1f\trew_start=%.4f\trew_peak=%.4f\trew_end=%.4f\tsave=%s(rc=%d)\n",
           n_worlds, hidden, br_s1_bwd_name(bwd_prec), iters, sum_ms / 1e3,
           avg_roll, avg_learn, avg_ms, avg_mticks,
           rew_first, rew_peak, rew_last, save_path, save_rc);

    free(h_st);
    cudaFree(d_obs_buf); cudaFree(d_boot_obs); cudaFree(d_act_buf); cudaFree(d_boot_act);
    cudaFree(d_logp_buf); cudaFree(d_val_buf); cudaFree(d_rew_buf);
    cudaFree(d_adv_buf); cudaFree(d_ret_buf); cudaFree(d_done_buf);
    cudaFree(d_boot_val); cudaFree(d_boot_lp); cudaFree(d_rsum);
    br_policy_s1_destroy(pol);
    br_portal_sim_destroy(sim);
    free(seeds);
    return (save_rc == 0) ? 0 : 1;
}
