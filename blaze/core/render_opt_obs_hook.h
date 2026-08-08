/* render_opt_obs_hook: Wave 14 - export tick_compose_full light slice through render-opt kernel.
 *
 * After tcf_init_env (tick 0), extract the TLC light slice (16x64x16 at y=0..63) and apply
 * render-opt kernels/15_light_combine_pack (World.getCombinedLight tail) per cell:
 *   override=0, sky/block from propagated world light.
 *
 * INTERNAL verify (CPU==CUDA). READ-ONLY: tick_compose_full.h. Wired render-opt kernel:
 *   ref/render-opt/kernels/15_light_combine_pack/candidate.c (getCombinedLight). */
#ifndef MC_RENDER_OPT_OBS_HOOK_H
#define MC_RENDER_OPT_OBS_HOOK_H

#include "tick_compose_full.h"

#define ROOH_VOL TLC_SLICE_VOL

/* Verbatim tail of MC World.getCombinedLight (render-opt kernel 15). */
MC_HD static inline i32 rooh_get_combined_light(i32 sky, i32 block, i32 override) {
    i32 j = block;
    if (j < override)
        j = override;
    {
        u32 packed = ((u32)sky << 20) | ((u32)j << 4);
        return (i32)packed;
    }
}

MC_HD static inline void rooh_pack_slice(const u8 *sky, const u8 *blk, i32 *out) {
    int i;
    for (i = 0; i < ROOH_VOL; ++i)
        out[i] = rooh_get_combined_light((i32)sky[i], (i32)blk[i], 0);
}

MC_HD static inline void rooh_snapshot(const World *w, i32 *out) {
    u8 sky[TLC_SLICE_VOL];
    u8 blk[TLC_SLICE_VOL];
    u16 blocks[TLC_SLICE_VOL];
    tlc_extract_slice(w, sky, blk, blocks);
    (void)blocks;
    rooh_pack_slice(sky, blk, out);
}

MC_HD static inline void rooh_init_and_snapshot(Env *e, TcfAux *aux, u64 seed,
                                                ChunkPrimer *primer, CpScratch *sc,
                                                const McSinTable *st, TcfScratch *scratch,
                                                i32 *out) {
    tcf_init_env(e, aux, seed, primer, sc, st, scratch);
    rooh_snapshot(twc_now(e), out);
}

#endif /* MC_RENDER_OPT_OBS_HOOK_H */
