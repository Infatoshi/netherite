/* Layout and fused act kernels. Host ABI is NCHW u8; device acts are NHWC fp16.
 * See cuda_fable_contract.h. */
#pragma once

#include <cuda_fp16.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* planes: NCHW u8. out: NHWC fp16 with c_pad >= n_ch, extra channels 0.
 * Depth channels (7 and 16) scaled 1/255. */
int nn_layout_obs_to_nhwc(const uint8_t *planes, __half *out, int n, int n_ch,
                          int h, int w, int c_pad, int depth0, int depth1);

/* Filter KCRS fp32 (checkpoint) <-> KRSC fp16 (device). c_in_pad >= c_in. */
int nn_layout_kcrs_to_krsc(const float *kcrs, __half *krsc, int k, int c,
                           int r, int s, int c_pad);
int nn_layout_krsc_to_kcrs(const __half *krsc, float *kcrs, int k, int c,
                           int r, int s, int c_pad);

int nn_layout_f32_to_f16(const float *src, __half *dst, size_t n);
int nn_layout_f16_to_f32(const __half *src, float *dst, size_t n);

/* First n_flat columns of W[out, in] from CHW-flat to HWC-flat. Rest copied. */
int nn_layout_fc_chw_to_hwc(const float *w_chw, float *w_hwc, int out, int in,
                            int c, int h, int w);
int nn_layout_fc_hwc_to_chw(const float *w_hwc, float *w_chw, int out, int in,
                            int c, int h, int w);

/* dpre = dy * (y > 0); db[c] += sum_{n,h,w} dpre. y, dy, dpre NHWC fp16. */
int nn_layout_relu_bwd_bias(const __half *dy, const __half *y, __half *dpre,
                            float *db, int n, int c, int h, int w);

#ifdef __cplusplus
}
#endif
