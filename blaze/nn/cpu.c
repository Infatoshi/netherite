/* FP32 C reference for the Blaze policy.
 * Direct loops, fixed arenas, no allocation in forward/sample/update. */
#include "fixture.h"
#include "model.h"
#include "cpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const float kAdamBeta1 = 0.9f;
static const float kAdamBeta2 = 0.999f;
static const float kAdamEps = 1e-8f;

static char g_err[512] = "";

static void set_err(const char *msg) {
  snprintf(g_err, sizeof(g_err), "%s", msg);
}

const char *nn_cpu_last_error(void) { return g_err; }

/* Validate every NnConfig field. Returns 0 on success. */
static int validate_config(const NnConfig *cfg) {
  if (!cfg) {
    set_err("null config");
    return -1;
  }
  if (!isfinite(cfg->lr) || !isfinite(cfg->ppo_clip) ||
      !isfinite(cfg->value_coef) || !isfinite(cfg->entropy_coef) ||
      !isfinite(cfg->grad_limit)) {
    set_err("config field is non-finite");
    return -1;
  }
  if (cfg->lr < 0.f) {
    set_err("lr must be >= 0");
    return -1;
  }
  if (cfg->ppo_clip < 0.f || cfg->ppo_clip >= 1.f) {
    set_err("ppo_clip must be >= 0 and < 1");
    return -1;
  }
  if (cfg->value_coef < 0.f) {
    set_err("value_coef must be >= 0");
    return -1;
  }
  if (cfg->entropy_coef < 0.f) {
    set_err("entropy_coef must be >= 0");
    return -1;
  }
  if (!(cfg->grad_limit > 0.f)) {
    set_err("grad_limit must be > 0");
    return -1;
  }
  return 0;
}

/* Each action must lie in [0, head_width). */
static int validate_actions(const int32_t *acts, int n) {
  for (int ni = 0; ni < n; ++ni) {
    for (int h = 0; h < NN_N_HEAD; ++h) {
      const int32_t a = acts[(size_t)ni * NN_N_HEAD + h];
      if (a < 0 || a >= NN_HEAD_WIDTHS[h]) {
        snprintf(g_err, sizeof(g_err),
                 "action out of range: sample %d head %d value %d width %d", ni,
                 h, (int)a, NN_HEAD_WIDTHS[h]);
        return -1;
      }
    }
  }
  return 0;
}

/* ---- handle ---- */
struct NnCpu {
  int max_n;
  NnConfig cfg;
  int64_t adam_t;

  /* Weight tensors (owned pointers into param_blob or separate). */
  float *t[NN_T_COUNT];
  float *param_blob; /* contiguous params */
  float *grad_blob;
  float *adam_m;
  float *adam_v;
  size_t n_params;

  /* Activation arenas [max_n, ...] */
  float *act_conv1;  /* [N,32,16,30] post-ReLU */
  float *act_conv2;  /* [N,64,7,14] post-ReLU */
  float *act_fc_in;  /* [N,6299] */
  float *act_hidden; /* [N,256] post-ReLU */
  float *act_logits; /* [N,34] */
  float *act_value;  /* [N] */
  /* Pre-ReLU for backward masks (store pre value; mask = pre>0) */
  float *pre_conv1;
  float *pre_conv2;
  float *pre_hidden;

  /* Backward arenas */
  float *d_logits;
  float *d_value;
  float *d_hidden;
  float *d_fc_in;
  float *d_conv2;
  float *d_conv1;

  /* Cached last batch size for fixture layer copy */
  int last_n;
};

/* Tensor base offsets into param_blob. */
static size_t tensor_offset(int tid) {
  size_t off = 0;
  for (int i = 0; i < tid; ++i)
    off += NN_TENSOR_FLOATS[i];
  return off;
}

static float *tensor_ptr(NnCpu *nn, int tid) {
  return nn->param_blob + tensor_offset(tid);
}

static float *grad_ptr(NnCpu *nn, int tid) {
  return nn->grad_blob + tensor_offset(tid);
}

static void zero_grads(NnCpu *nn) {
  memset(nn->grad_blob, 0, nn->n_params * sizeof(float));
}

/* ---- forward kernels ---- */

static float read_plane_u8(const uint8_t *obs, int n, int ic, int ih, int iw) {
  const size_t base =
      ((size_t)n * NN_N_CH + (size_t)ic) * (size_t)NN_CAM_H * (size_t)NN_CAM_W;
  float x = (float)obs[base + (size_t)ih * NN_CAM_W + (size_t)iw];
  if (ic == NN_DEPTH_CH0 || ic == NN_DEPTH_CH1)
    x *= (1.f / 255.f);
  return x;
}

static void conv1_forward(NnCpu *nn, const uint8_t *obs, int n) {
  const float *w = nn->t[NN_T_CONV1_W];
  const float *b = nn->t[NN_T_CONV1_B];
  for (int ni = 0; ni < n; ++ni) {
    for (int oc = 0; oc < NN_C_OUT1; ++oc) {
      const float *wc = w + oc * (NN_N_CH * NN_K1 * NN_K1);
      for (int oh = 0; oh < NN_H1; ++oh) {
        for (int ow = 0; ow < NN_W1; ++ow) {
          const int ih0 = oh * NN_S1;
          const int iw0 = ow * NN_S1;
          float acc = b[oc];
          for (int ic = 0; ic < NN_N_CH; ++ic) {
            for (int kh = 0; kh < NN_K1; ++kh) {
              for (int kw = 0; kw < NN_K1; ++kw) {
                float x = read_plane_u8(obs, ni, ic, ih0 + kh, iw0 + kw);
                acc += x * wc[(ic * NN_K1 + kh) * NN_K1 + kw];
              }
            }
          }
          const size_t idx =
              ((((size_t)ni * NN_C_OUT1 + oc) * NN_H1 + oh) * NN_W1 + ow);
          nn->pre_conv1[idx] = acc;
          nn->act_conv1[idx] = acc > 0.f ? acc : 0.f;
        }
      }
    }
  }
}

static void conv2_forward(NnCpu *nn, int n) {
  const float *w = nn->t[NN_T_CONV2_W];
  const float *b = nn->t[NN_T_CONV2_B];
  for (int ni = 0; ni < n; ++ni) {
    for (int oc = 0; oc < NN_C_OUT2; ++oc) {
      const float *wc = w + oc * (NN_C_OUT1 * NN_K2 * NN_K2);
      for (int oh = 0; oh < NN_H2; ++oh) {
        for (int ow = 0; ow < NN_W2; ++ow) {
          const int ih0 = oh * NN_S2;
          const int iw0 = ow * NN_S2;
          float acc = b[oc];
          for (int ic = 0; ic < NN_C_OUT1; ++ic) {
            const size_t base =
                ((size_t)ni * NN_C_OUT1 + ic) * NN_H1 * NN_W1;
            for (int kh = 0; kh < NN_K2; ++kh) {
              for (int kw = 0; kw < NN_K2; ++kw) {
                acc += nn->act_conv1[base + (size_t)(ih0 + kh) * NN_W1 +
                                     (size_t)(iw0 + kw)] *
                       wc[(ic * NN_K2 + kh) * NN_K2 + kw];
              }
            }
          }
          const size_t idx =
              ((((size_t)ni * NN_C_OUT2 + oc) * NN_H2 + oh) * NN_W2 + ow);
          nn->pre_conv2[idx] = acc;
          nn->act_conv2[idx] = acc > 0.f ? acc : 0.f;
        }
      }
    }
  }
}

static void pack_fc_in(NnCpu *nn, const float *scal, int n) {
  for (int ni = 0; ni < n; ++ni) {
    float *row = nn->act_fc_in + (size_t)ni * NN_FC_IN;
    for (int j = 0; j < NN_FLAT; ++j) {
      const int c = j / (NN_H2 * NN_W2);
      const int rem = j % (NN_H2 * NN_W2);
      const int h = rem / NN_W2;
      const int w = rem % NN_W2;
      row[j] = nn->act_conv2[(((size_t)ni * NN_C_OUT2 + c) * NN_H2 + h) * NN_W2 +
                             w];
    }
    for (int s = 0; s < NN_N_SCAL; ++s)
      row[NN_FLAT + s] = scal[(size_t)ni * NN_N_SCAL + s];
  }
}

static void fc_forward(NnCpu *nn, int n) {
  const float *w = nn->t[NN_T_FC_W]; /* [256, 6299] */
  const float *b = nn->t[NN_T_FC_B];
  for (int ni = 0; ni < n; ++ni) {
    const float *x = nn->act_fc_in + (size_t)ni * NN_FC_IN;
    float *y = nn->act_hidden + (size_t)ni * NN_FC_OUT;
    float *pre = nn->pre_hidden + (size_t)ni * NN_FC_OUT;
    for (int o = 0; o < NN_FC_OUT; ++o) {
      const float *wr = w + (size_t)o * NN_FC_IN;
      float acc = b[o];
      for (int i = 0; i < NN_FC_IN; ++i)
        acc += wr[i] * x[i];
      pre[o] = acc;
      y[o] = acc > 0.f ? acc : 0.f;
    }
  }
}

static void heads_forward(NnCpu *nn, int n) {
  const float *w = nn->t[NN_T_HEADS_W]; /* [34, 256] */
  const float *b = nn->t[NN_T_HEADS_B];
  for (int ni = 0; ni < n; ++ni) {
    const float *h = nn->act_hidden + (size_t)ni * NN_FC_OUT;
    float *logits = nn->act_logits + (size_t)ni * NN_N_LOGITS;
    for (int o = 0; o < NN_N_LOGITS; ++o) {
      const float *wr = w + (size_t)o * NN_FC_OUT;
      float acc = b[o];
      for (int i = 0; i < NN_FC_OUT; ++i)
        acc += wr[i] * h[i];
      logits[o] = acc;
    }
  }
}

static void value_forward(NnCpu *nn, int n) {
  const float *w = nn->t[NN_T_VALUE_W];
  const float *b = nn->t[NN_T_VALUE_B];
  for (int ni = 0; ni < n; ++ni) {
    const float *h = nn->act_hidden + (size_t)ni * NN_FC_OUT;
    float acc = b[0];
    for (int i = 0; i < NN_FC_OUT; ++i)
      acc += w[i] * h[i];
    nn->act_value[ni] = acc;
  }
}

static void forward_core(NnCpu *nn, const uint8_t *planes, const float *scalars,
                         int n) {
  conv1_forward(nn, planes, n);
  conv2_forward(nn, n);
  pack_fc_in(nn, scalars, n);
  fc_forward(nn, n);
  heads_forward(nn, n);
  value_forward(nn, n);
  nn->last_n = n;
}

/* ---- sample ---- */

static void sample_core(const float *logits, int n, int mode, uint64_t seed,
                        int32_t *acts, float *logp, float *entropy) {
  for (int ni = 0; ni < n; ++ni) {
    float lp_sum = 0.f;
    float ent_sum = 0.f;
    for (int h = 0; h < NN_N_HEAD; ++h) {
      const int w = NN_HEAD_WIDTHS[h];
      const int off = NN_HEAD_OFF[h];
      const float *row = logits + (size_t)ni * NN_N_LOGITS + off;
      float m = row[0];
      for (int c = 1; c < w; ++c)
        if (row[c] > m)
          m = row[c];
      float ex[NN_W_MAX];
      float sum = 0.f;
      for (int c = 0; c < w; ++c) {
        ex[c] = expf(row[c] - m);
        sum += ex[c];
      }
      const float inv = 1.f / sum;
      const float lse = m + logf(sum);
      int a = 0;
      if (mode == NN_SAMPLE_GREEDY) {
        float best = row[0];
        for (int c = 1; c < w; ++c) {
          if (row[c] > best) {
            best = row[c];
            a = c;
          }
        }
      } else {
        float best = row[0] + nn_gumbel0(nn_hash_u01(seed, (uint32_t)ni,
                                                      (uint32_t)h, 0u));
        for (int c = 1; c < w; ++c) {
          const float s =
              row[c] +
              nn_gumbel0(nn_hash_u01(seed, (uint32_t)ni, (uint32_t)h,
                                     (uint32_t)c));
          if (s > best) {
            best = s;
            a = c;
          }
        }
      }
      acts[(size_t)ni * NN_N_HEAD + h] = a;
      lp_sum += row[a] - lse;
      float eh = 0.f;
      for (int c = 0; c < w; ++c) {
        const float p = ex[c] * inv;
        if (p > 0.f)
          eh -= p * logf(p);
      }
      ent_sum += eh;
    }
    logp[ni] = lp_sum;
    if (entropy)
      entropy[ni] = ent_sum;
  }
}

/* ---- joint logp / entropy / d_logits from fixed acts ---- */

static void logp_entropy_backward(const float *logits, const int32_t *acts,
                                  int n, float *logp, float *entropy,
                                  float *d_logits, const float *d_logp,
                                  const float *d_entropy) {
  /* d_logits accumulates: d_logp[i] * dlogp/dlogit + d_entropy[i] * dent/dlogit */
  memset(d_logits, 0, (size_t)n * NN_N_LOGITS * sizeof(float));
  for (int ni = 0; ni < n; ++ni) {
    float lp_sum = 0.f;
    float ent_sum = 0.f;
    for (int h = 0; h < NN_N_HEAD; ++h) {
      const int w = NN_HEAD_WIDTHS[h];
      const int off = NN_HEAD_OFF[h];
      const float *row = logits + (size_t)ni * NN_N_LOGITS + off;
      float *drow = d_logits + (size_t)ni * NN_N_LOGITS + off;
      float m = row[0];
      for (int c = 1; c < w; ++c)
        if (row[c] > m)
          m = row[c];
      float ex[NN_W_MAX];
      float sum = 0.f;
      for (int c = 0; c < w; ++c) {
        ex[c] = expf(row[c] - m);
        sum += ex[c];
      }
      const float inv = 1.f / sum;
      float p[NN_W_MAX];
      float lse = m + logf(sum);
      float eh = 0.f;
      for (int c = 0; c < w; ++c) {
        p[c] = ex[c] * inv;
        if (p[c] > 0.f)
          eh -= p[c] * logf(p[c]);
      }
      const int a = acts[(size_t)ni * NN_N_HEAD + h];
      lp_sum += row[a] - lse;
      ent_sum += eh;

      const float gl = d_logp ? d_logp[ni] : 0.f;
      const float ge = d_entropy ? d_entropy[ni] : 0.f;
      /* d logp / d z_c = 1[c==a] - p_c */
      for (int c = 0; c < w; ++c) {
        float g = gl * (((c == a) ? 1.f : 0.f) - p[c]);
        /* d H / d z_c = -p_c * (log p_c + H) */
        if (p[c] > 0.f)
          g += ge * (-p[c] * (logf(p[c]) + eh));
        drow[c] += g;
      }
    }
    if (logp)
      logp[ni] = lp_sum;
    if (entropy)
      entropy[ni] = ent_sum;
  }
}

/* ---- backward through linear layers ---- */

static void heads_backward(NnCpu *nn, int n) {
  const float *w = nn->t[NN_T_HEADS_W];
  float *gw = grad_ptr(nn, NN_T_HEADS_W);
  float *gb = grad_ptr(nn, NN_T_HEADS_B);
  memset(nn->d_hidden, 0, (size_t)n * NN_FC_OUT * sizeof(float));
  for (int ni = 0; ni < n; ++ni) {
    const float *h = nn->act_hidden + (size_t)ni * NN_FC_OUT;
    const float *dlogits = nn->d_logits + (size_t)ni * NN_N_LOGITS;
    float *dh = nn->d_hidden + (size_t)ni * NN_FC_OUT;
    for (int o = 0; o < NN_N_LOGITS; ++o) {
      const float g = dlogits[o];
      gb[o] += g;
      const float *wr = w + (size_t)o * NN_FC_OUT;
      float *gwr = gw + (size_t)o * NN_FC_OUT;
      for (int i = 0; i < NN_FC_OUT; ++i) {
        gwr[i] += g * h[i];
        dh[i] += g * wr[i];
      }
    }
  }
}

static void value_backward(NnCpu *nn, int n) {
  const float *w = nn->t[NN_T_VALUE_W];
  float *gw = grad_ptr(nn, NN_T_VALUE_W);
  float *gb = grad_ptr(nn, NN_T_VALUE_B);
  for (int ni = 0; ni < n; ++ni) {
    const float *h = nn->act_hidden + (size_t)ni * NN_FC_OUT;
    float *dh = nn->d_hidden + (size_t)ni * NN_FC_OUT;
    const float g = nn->d_value[ni];
    gb[0] += g;
    for (int i = 0; i < NN_FC_OUT; ++i) {
      gw[i] += g * h[i];
      dh[i] += g * w[i];
    }
  }
}

static void fc_backward(NnCpu *nn, int n) {
  const float *w = nn->t[NN_T_FC_W];
  float *gw = grad_ptr(nn, NN_T_FC_W);
  float *gb = grad_ptr(nn, NN_T_FC_B);
  memset(nn->d_fc_in, 0, (size_t)n * NN_FC_IN * sizeof(float));
  for (int ni = 0; ni < n; ++ni) {
    const float *x = nn->act_fc_in + (size_t)ni * NN_FC_IN;
    const float *pre = nn->pre_hidden + (size_t)ni * NN_FC_OUT;
    float *dh = nn->d_hidden + (size_t)ni * NN_FC_OUT;
    float *dx = nn->d_fc_in + (size_t)ni * NN_FC_IN;
    /* ReLU backward on hidden */
    for (int o = 0; o < NN_FC_OUT; ++o) {
      if (pre[o] <= 0.f)
        dh[o] = 0.f;
      const float g = dh[o];
      gb[o] += g;
      const float *wr = w + (size_t)o * NN_FC_IN;
      float *gwr = gw + (size_t)o * NN_FC_IN;
      for (int i = 0; i < NN_FC_IN; ++i) {
        gwr[i] += g * x[i];
        dx[i] += g * wr[i];
      }
    }
  }
}

static void unpack_fc_in_backward(NnCpu *nn, int n) {
  /* d_fc_in[:, :FLAT] -> d_conv2; scalars have no grad into model. */
  memset(nn->d_conv2, 0, (size_t)n * NN_C_OUT2 * NN_H2 * NN_W2 * sizeof(float));
  for (int ni = 0; ni < n; ++ni) {
    const float *dx = nn->d_fc_in + (size_t)ni * NN_FC_IN;
    for (int j = 0; j < NN_FLAT; ++j) {
      const int c = j / (NN_H2 * NN_W2);
      const int rem = j % (NN_H2 * NN_W2);
      const int h = rem / NN_W2;
      const int w = rem % NN_W2;
      nn->d_conv2[(((size_t)ni * NN_C_OUT2 + c) * NN_H2 + h) * NN_W2 + w] =
          dx[j];
    }
  }
}

static void conv2_backward(NnCpu *nn, int n) {
  const float *w = nn->t[NN_T_CONV2_W];
  float *gw = grad_ptr(nn, NN_T_CONV2_W);
  float *gb = grad_ptr(nn, NN_T_CONV2_B);
  memset(nn->d_conv1, 0, (size_t)n * NN_C_OUT1 * NN_H1 * NN_W1 * sizeof(float));

  for (int ni = 0; ni < n; ++ni) {
    for (int oc = 0; oc < NN_C_OUT2; ++oc) {
      const float *wc = w + oc * (NN_C_OUT1 * NN_K2 * NN_K2);
      float *gwc = gw + oc * (NN_C_OUT1 * NN_K2 * NN_K2);
      for (int oh = 0; oh < NN_H2; ++oh) {
        for (int ow = 0; ow < NN_W2; ++ow) {
          const size_t oidx =
              ((((size_t)ni * NN_C_OUT2 + oc) * NN_H2 + oh) * NN_W2 + ow);
          float g = nn->d_conv2[oidx];
          if (nn->pre_conv2[oidx] <= 0.f)
            g = 0.f;
          gb[oc] += g;
          const int ih0 = oh * NN_S2;
          const int iw0 = ow * NN_S2;
          for (int ic = 0; ic < NN_C_OUT1; ++ic) {
            const size_t ibase =
                ((size_t)ni * NN_C_OUT1 + ic) * NN_H1 * NN_W1;
            for (int kh = 0; kh < NN_K2; ++kh) {
              for (int kw = 0; kw < NN_K2; ++kw) {
                const size_t iidx = ibase + (size_t)(ih0 + kh) * NN_W1 +
                                    (size_t)(iw0 + kw);
                const int wi = (ic * NN_K2 + kh) * NN_K2 + kw;
                gwc[wi] += g * nn->act_conv1[iidx];
                nn->d_conv1[iidx] += g * wc[wi];
              }
            }
          }
        }
      }
    }
  }
}

static void conv1_backward(NnCpu *nn, const uint8_t *obs, int n) {
  const float *w = nn->t[NN_T_CONV1_W];
  float *gw = grad_ptr(nn, NN_T_CONV1_W);
  float *gb = grad_ptr(nn, NN_T_CONV1_B);

  for (int ni = 0; ni < n; ++ni) {
    for (int oc = 0; oc < NN_C_OUT1; ++oc) {
      float *gwc = gw + oc * (NN_N_CH * NN_K1 * NN_K1);
      for (int oh = 0; oh < NN_H1; ++oh) {
        for (int ow = 0; ow < NN_W1; ++ow) {
          const size_t oidx =
              ((((size_t)ni * NN_C_OUT1 + oc) * NN_H1 + oh) * NN_W1 + ow);
          float g = nn->d_conv1[oidx];
          if (nn->pre_conv1[oidx] <= 0.f)
            g = 0.f;
          gb[oc] += g;
          const int ih0 = oh * NN_S1;
          const int iw0 = ow * NN_S1;
          for (int ic = 0; ic < NN_N_CH; ++ic) {
            for (int kh = 0; kh < NN_K1; ++kh) {
              for (int kw = 0; kw < NN_K1; ++kw) {
                float x = read_plane_u8(obs, ni, ic, ih0 + kh, iw0 + kw);
                const int wi = (ic * NN_K1 + kh) * NN_K1 + kw;
                gwc[wi] += g * x;
              }
            }
          }
        }
      }
    }
  }
  (void)w;
}

/* ---- Adam + grad clip ---- */

static float grad_l2_norm(const NnCpu *nn) {
  double s = 0.0;
  for (size_t i = 0; i < nn->n_params; ++i) {
    double g = (double)nn->grad_blob[i];
    s += g * g;
  }
  return (float)sqrt(s);
}

static void clip_grads(NnCpu *nn, float limit, float *norm_out) {
  float norm = grad_l2_norm(nn);
  if (norm_out)
    *norm_out = norm;
  if (norm > limit && norm > 0.f) {
    float scale = limit / (norm + 1e-6f);
    for (size_t i = 0; i < nn->n_params; ++i)
      nn->grad_blob[i] *= scale;
  }
}

static void adam_step(NnCpu *nn) {
  nn->adam_t += 1;
  const float t = (float)nn->adam_t;
  const float bc1 = 1.f - powf(kAdamBeta1, t);
  const float bc2 = 1.f - powf(kAdamBeta2, t);
  const float lr = nn->cfg.lr;
  for (size_t i = 0; i < nn->n_params; ++i) {
    const float g = nn->grad_blob[i];
    float m = kAdamBeta1 * nn->adam_m[i] + (1.f - kAdamBeta1) * g;
    float v = kAdamBeta2 * nn->adam_v[i] + (1.f - kAdamBeta2) * g * g;
    nn->adam_m[i] = m;
    nn->adam_v[i] = v;
    const float mhat = m / bc1;
    const float vhat = v / bc2;
    nn->param_blob[i] -= lr * mhat / (sqrtf(vhat) + kAdamEps);
  }
}

/* ---- internal CPU API ---- */

NnCpu *nn_cpu_create(int max_n, int device_id, const NnConfig *cfg) {
  g_err[0] = 0;
  if (device_id != 0) {
    set_err("CPU device must be 0");
    return NULL;
  }
  if (max_n <= 0) {
    set_err("max_n must be > 0");
    return NULL;
  }
  NnConfig resolved = cfg ? *cfg : nn_config_default();
  if (validate_config(&resolved) != 0)
    return NULL;
  NnCpu *nn = (NnCpu *)calloc(1, sizeof(NnCpu));
  if (!nn) {
    set_err("oom handle");
    return NULL;
  }
  nn->max_n = max_n;
  nn->cfg = resolved;
  nn->adam_t = 0;
  nn->n_params = nn_model_param_floats();
  nn->last_n = 0;

  nn->param_blob = (float *)calloc(nn->n_params, sizeof(float));
  nn->grad_blob = (float *)calloc(nn->n_params, sizeof(float));
  nn->adam_m = (float *)calloc(nn->n_params, sizeof(float));
  nn->adam_v = (float *)calloc(nn->n_params, sizeof(float));
  if (!nn->param_blob || !nn->grad_blob || !nn->adam_m || !nn->adam_v) {
    set_err("oom params");
    nn_cpu_destroy(nn);
    return NULL;
  }
  for (int t = 0; t < NN_T_COUNT; ++t)
    nn->t[t] = tensor_ptr(nn, t);

  const size_t N = (size_t)max_n;
  nn->act_conv1 = (float *)calloc(N * NN_C_OUT1 * NN_H1 * NN_W1, sizeof(float));
  nn->act_conv2 = (float *)calloc(N * NN_C_OUT2 * NN_H2 * NN_W2, sizeof(float));
  nn->act_fc_in = (float *)calloc(N * NN_FC_IN, sizeof(float));
  nn->act_hidden = (float *)calloc(N * NN_FC_OUT, sizeof(float));
  nn->act_logits = (float *)calloc(N * NN_N_LOGITS, sizeof(float));
  nn->act_value = (float *)calloc(N, sizeof(float));
  nn->pre_conv1 = (float *)calloc(N * NN_C_OUT1 * NN_H1 * NN_W1, sizeof(float));
  nn->pre_conv2 = (float *)calloc(N * NN_C_OUT2 * NN_H2 * NN_W2, sizeof(float));
  nn->pre_hidden = (float *)calloc(N * NN_FC_OUT, sizeof(float));
  nn->d_logits = (float *)calloc(N * NN_N_LOGITS, sizeof(float));
  nn->d_value = (float *)calloc(N, sizeof(float));
  nn->d_hidden = (float *)calloc(N * NN_FC_OUT, sizeof(float));
  nn->d_fc_in = (float *)calloc(N * NN_FC_IN, sizeof(float));
  nn->d_conv2 = (float *)calloc(N * NN_C_OUT2 * NN_H2 * NN_W2, sizeof(float));
  nn->d_conv1 = (float *)calloc(N * NN_C_OUT1 * NN_H1 * NN_W1, sizeof(float));

  if (!nn->act_conv1 || !nn->act_conv2 || !nn->act_fc_in || !nn->act_hidden ||
      !nn->act_logits || !nn->act_value || !nn->pre_conv1 || !nn->pre_conv2 ||
      !nn->pre_hidden || !nn->d_logits || !nn->d_value || !nn->d_hidden ||
      !nn->d_fc_in || !nn->d_conv2 || !nn->d_conv1) {
    set_err("oom arenas");
    nn_cpu_destroy(nn);
    return NULL;
  }

  nn_fixture_init_weights(nn->t, 0xC0FFEEu);
  return nn;
}

void nn_cpu_destroy(NnCpu *nn) {
  if (!nn)
    return;
  free(nn->param_blob);
  free(nn->grad_blob);
  free(nn->adam_m);
  free(nn->adam_v);
  free(nn->act_conv1);
  free(nn->act_conv2);
  free(nn->act_fc_in);
  free(nn->act_hidden);
  free(nn->act_logits);
  free(nn->act_value);
  free(nn->pre_conv1);
  free(nn->pre_conv2);
  free(nn->pre_hidden);
  free(nn->d_logits);
  free(nn->d_value);
  free(nn->d_hidden);
  free(nn->d_fc_in);
  free(nn->d_conv2);
  free(nn->d_conv1);
  free(nn);
}

int nn_cpu_set_config(NnCpu *nn, const NnConfig *cfg) {
  if (!nn || !cfg) {
    set_err("null");
    return -1;
  }
  if (validate_config(cfg) != 0)
    return -1;
  nn->cfg = *cfg;
  return 0;
}

int nn_cpu_forward(NnCpu *nn, const uint8_t *planes, const float *scalars, int n,
                   float *logits, float *values) {
  if (!nn || !planes || !scalars || !logits || !values) {
    set_err("null pointer");
    return -1;
  }
  if (n <= 0 || n > nn->max_n) {
    set_err("n out of range");
    return -1;
  }
  forward_core(nn, planes, scalars, n);
  memcpy(logits, nn->act_logits, (size_t)n * NN_N_LOGITS * sizeof(float));
  memcpy(values, nn->act_value, (size_t)n * sizeof(float));
  return 0;
}

int nn_cpu_sample(NnCpu *nn, const float *logits, int n, int mode, int32_t *acts,
                  float *logp, float *entropy) {
  if (!nn || !logits || !acts || !logp) {
    set_err("null pointer");
    return -1;
  }
  if (n <= 0 || n > nn->max_n) {
    set_err("n out of range");
    return -1;
  }
  if (mode != NN_SAMPLE_GUMBEL && mode != NN_SAMPLE_GREEDY) {
    set_err("bad sample mode");
    return -1;
  }
  sample_core(logits, n, mode, nn->cfg.rng_seed, acts, logp, entropy);
  return 0;
}

int nn_cpu_update(NnCpu *nn, const uint8_t *planes, const float *scalars,
                  const int32_t *acts, const float *old_logp,
                  const float *advantages, const float *returns, int n,
                  NnUpdateStats *stats) {
  if (!nn || !planes || !scalars || !acts || !old_logp || !advantages ||
      !returns) {
    set_err("null pointer");
    return -1;
  }
  if (n <= 0 || n > nn->max_n) {
    set_err("n out of range");
    return -1;
  }
  /* Validate fixed actions before any forward or weight touch. */
  if (validate_actions(acts, n) != 0)
    return -1;

  zero_grads(nn);
  forward_core(nn, planes, scalars, n);

  /* Temps in d_fc_in head (overwritten later by fc_backward). */
  float *logp_buf = nn->d_fc_in;
  float *ent_buf = nn->d_fc_in + n;
  float *d_logp = nn->d_fc_in + 2 * n;
  float *d_ent = nn->d_fc_in + 3 * n;

  /* First pass: compute logp and entropy only (no grad). */
  logp_entropy_backward(nn->act_logits, acts, n, logp_buf, ent_buf, nn->d_logits,
                        NULL, NULL);

  const float inv_n = 1.f / (float)n;
  const float clip = nn->cfg.ppo_clip;
  float policy_loss = 0.f;
  float value_loss = 0.f;
  float ent_mean = 0.f;

  for (int i = 0; i < n; ++i) {
    const float ratio = expf(logp_buf[i] - old_logp[i]);
    const float adv = advantages[i];
    const float surr1 = ratio * adv;
    float r_clip = ratio;
    if (r_clip < 1.f - clip)
      r_clip = 1.f - clip;
    if (r_clip > 1.f + clip)
      r_clip = 1.f + clip;
    const float surr2 = r_clip * adv;
    const float obj = surr1 < surr2 ? surr1 : surr2;
    policy_loss += -obj;

    /* d(-min)/d logp = -d(min)/d ratio * d ratio / d logp
     * d ratio / d logp = ratio
     * When min is surr1: d surr1 / d ratio = adv
     * When min is surr2: if ratio outside clip band, d surr2 / d ratio = 0
     *                    if inside, surr1==surr2, use adv
     */
    float dobj_dratio;
    if (surr1 < surr2) {
      dobj_dratio = adv;
    } else if (surr2 < surr1) {
      /* clipped branch active */
      if (ratio < 1.f - clip || ratio > 1.f + clip)
        dobj_dratio = 0.f;
      else
        dobj_dratio = adv;
    } else {
      dobj_dratio = adv;
    }
    /* loss_i = -obj_i / n  => d loss / d logp = -dobj_dratio * ratio / n */
    d_logp[i] = -dobj_dratio * ratio * inv_n;

    const float diff = nn->act_value[i] - returns[i];
    value_loss += diff * diff;
    nn->d_value[i] = nn->cfg.value_coef * 2.f * diff * inv_n;

    ent_mean += ent_buf[i];
    /* loss includes -entropy_coef * mean(entropy)
     * d loss / d entropy_i = -entropy_coef / n */
    d_ent[i] = -nn->cfg.entropy_coef * inv_n;
  }
  policy_loss *= inv_n;
  value_loss = nn->cfg.value_coef * value_loss * inv_n;
  ent_mean *= inv_n;
  const float total =
      policy_loss + value_loss - nn->cfg.entropy_coef * ent_mean;

  /* Second pass: accumulate d_logits from d_logp and d_ent. */
  logp_entropy_backward(nn->act_logits, acts, n, NULL, NULL, nn->d_logits,
                        d_logp, d_ent);

  /* Full network backprop */
  heads_backward(nn, n);
  value_backward(nn, n);
  fc_backward(nn, n);
  unpack_fc_in_backward(nn, n);
  conv2_backward(nn, n);
  conv1_backward(nn, planes, n);

  float grad_norm = 0.f;
  clip_grads(nn, nn->cfg.grad_limit, &grad_norm);
  adam_step(nn);

  if (stats) {
    stats->policy_loss = policy_loss;
    stats->value_loss = value_loss;
    stats->entropy_mean = ent_mean;
    stats->total_loss = total;
    stats->grad_norm = grad_norm;
  }
  return 0;
}

int nn_cpu_save(const NnCpu *nn, const char *path) {
  if (!nn || !path) {
    set_err("null");
    return -1;
  }
  const float *tensors[NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t)
    tensors[t] = nn->t[t];
  if (nn_fixture_save(path, tensors) != 0) {
    set_err("save failed");
    return -1;
  }
  return 0;
}

int nn_cpu_load(NnCpu *nn, const char *path) {
  if (!nn || !path) {
    set_err("null");
    return -1;
  }
  float *tensors[NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t)
    tensors[t] = nn->t[t];
  if (nn_fixture_load(path, tensors) != 0) {
    set_err("load failed");
    return -1;
  }
  /* Schema-1 checkpoint is weights only; live Adam moments stay as-is unless
   * the caller resets them. Clear moments so a fresh optim path is defined. */
  memset(nn->adam_m, 0, nn->n_params * sizeof(float));
  memset(nn->adam_v, 0, nn->n_params * sizeof(float));
  nn->adam_t = 0;
  return 0;
}

/* ---- fixture param / layer helpers (implemented here; know NnCpu layout) ---- */

int nn_cpu_fixture_get_params(const NnCpu *nn, float *dst, size_t count) {
  if (!nn || !dst || count != nn->n_params)
    return -1;
  memcpy(dst, nn->param_blob, count * sizeof(float));
  return 0;
}

int nn_cpu_fixture_set_params(NnCpu *nn, const float *src, size_t count) {
  if (!nn || !src || count != nn->n_params)
    return -1;
  memcpy(nn->param_blob, src, count * sizeof(float));
  return 0;
}

int nn_cpu_fixture_get_grads(const NnCpu *nn, float *dst, size_t count) {
  if (!nn || !dst || count != nn->n_params)
    return -1;
  memcpy(dst, nn->grad_blob, count * sizeof(float));
  return 0;
}

size_t nn_cpu_fixture_copy_layer(const NnCpu *nn, int layer, int sample,
                                 float *dst, size_t max_n) {
  if (!nn || !dst || sample < 0 || sample >= nn->last_n)
    return 0;
  const float *src = NULL;
  size_t count = 0;
  switch (layer) {
  case 0:
    src = nn->act_conv1 +
          (size_t)sample * NN_C_OUT1 * NN_H1 * NN_W1;
    count = (size_t)NN_C_OUT1 * NN_H1 * NN_W1;
    break;
  case 1:
    src = nn->act_conv2 +
          (size_t)sample * NN_C_OUT2 * NN_H2 * NN_W2;
    count = (size_t)NN_C_OUT2 * NN_H2 * NN_W2;
    break;
  case 2:
    src = nn->act_fc_in + (size_t)sample * NN_FC_IN;
    count = (size_t)NN_FC_IN;
    break;
  case 3:
    src = nn->act_hidden + (size_t)sample * NN_FC_OUT;
    count = (size_t)NN_FC_OUT;
    break;
  case 4:
    src = nn->act_logits + (size_t)sample * NN_N_LOGITS;
    count = (size_t)NN_N_LOGITS;
    break;
  case 5:
    src = nn->act_value + (size_t)sample;
    count = 1;
    break;
  default:
    return 0;
  }
  if (count > max_n)
    count = max_n;
  memcpy(dst, src, count * sizeof(float));
  return count;
}
