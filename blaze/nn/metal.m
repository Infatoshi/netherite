/* FP32 Metal policy backend for the fixed Blaze model.
 * MPSGraph owns forward, AD, gradient clip, and Adam.
 * Graphs compile once at create. No MTLBuffer alloc in forward/sample/update. */
#import "metal.h"
#import "fixture.h"
#import "model.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShadersGraph/MetalPerformanceShadersGraph.h>

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

const char *nn_metal_last_error(void) { return g_err; }

/* ---- config / action validation (match cpu.c) ---- */

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

static size_t tensor_offset(int tid) {
  size_t off = 0;
  for (int i = 0; i < tid; ++i)
    off += NN_TENSOR_FLOATS[i];
  return off;
}

/* ---- host sample (same as cpu.c sample_core) ---- */

/* Mix handle sample_step into rng_seed. nn_hash_u01 stays 4-arg. */
static uint64_t mix_sample_seed(uint64_t rng_seed, uint64_t step) {
  return rng_seed ^ (step * 0xD1B54A32D192ED03ULL);
}

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
        float best =
            row[0] + nn_gumbel0(nn_hash_u01(seed, (uint32_t)ni, (uint32_t)h, 0u));
        for (int c = 1; c < w; ++c) {
          const float s =
              row[c] +
              nn_gumbel0(nn_hash_u01(seed, (uint32_t)ni, (uint32_t)h, (uint32_t)c));
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

/* ---- handle ---- */

struct NnMetal {
  int batch_n;
  NnConfig cfg;
  uint64_t sample_step; /* Gumbel decision nonce; not reset on lr-only set_config */
  int64_t adam_t;
  size_t n_params;

  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  MPSGraphDevice *mpsDevice;

  /* Compiled graphs */
  MPSGraph *fwdGraph;
  MPSGraph *updGraph;
  MPSGraphExecutable *fwdExec;
  MPSGraphExecutable *updExec;

  /* Forward placeholders / targets */
  MPSGraphTensor *fwdPlanesPh;
  MPSGraphTensor *fwdScalarsPh;
  MPSGraphTensor *fwdWPh[NN_T_COUNT];
  MPSGraphTensor *fwdLogits;
  MPSGraphTensor *fwdValues;

  /* Update placeholders / targets */
  MPSGraphTensor *updPlanesPh;
  MPSGraphTensor *updScalarsPh;
  MPSGraphTensor *updActsPh;
  MPSGraphTensor *updOldLogpPh;
  MPSGraphTensor *updAdvPh;
  MPSGraphTensor *updRetPh;
  MPSGraphTensor *updWPh[NN_T_COUNT];
  MPSGraphTensor *updMPh[NN_T_COUNT];
  MPSGraphTensor *updVPh[NN_T_COUNT];
  MPSGraphTensor *updLrPh;
  MPSGraphTensor *updClipPh;
  MPSGraphTensor *updVcoefPh;
  MPSGraphTensor *updEcoefPh;
  MPSGraphTensor *updGradLimPh;
  MPSGraphTensor *updB1PowPh;
  MPSGraphTensor *updB2PowPh;
  MPSGraphTensor *updNewW[NN_T_COUNT];
  MPSGraphTensor *updNewM[NN_T_COUNT];
  MPSGraphTensor *updNewV[NN_T_COUNT];
  MPSGraphTensor *updPolicyLoss;
  MPSGraphTensor *updValueLoss;
  MPSGraphTensor *updEntMean;
  MPSGraphTensor *updTotalLoss;
  MPSGraphTensor *updGradNorm;
  MPSGraphTensor *updApproxKl;
  MPSGraphTensor *updClipfrac;

  /* Device buffers (allocated once) */
  id<MTLBuffer> wBuf[NN_T_COUNT];
  id<MTLBuffer> mBuf[NN_T_COUNT];
  id<MTLBuffer> vBuf[NN_T_COUNT];
  /* Separate Adam write targets (no in-place read/write hazard). */
  id<MTLBuffer> wOutBuf[NN_T_COUNT];
  id<MTLBuffer> mOutBuf[NN_T_COUNT];
  id<MTLBuffer> vOutBuf[NN_T_COUNT];
  id<MTLBuffer> planesBuf;   /* float32 [N,18,H,W] */
  id<MTLBuffer> scalarsBuf;  /* float32 [N,27] */
  id<MTLBuffer> actsBuf;     /* int32 [N,9] */
  id<MTLBuffer> oldLogpBuf;  /* float32 [N] */
  id<MTLBuffer> advBuf;      /* float32 [N] */
  id<MTLBuffer> retBuf;      /* float32 [N] */
  id<MTLBuffer> logitsBuf;   /* float32 [N,34] */
  id<MTLBuffer> valuesBuf;   /* float32 [N] */
  id<MTLBuffer> lrBuf;
  id<MTLBuffer> clipBuf;
  id<MTLBuffer> vcoefBuf;
  id<MTLBuffer> ecoefBuf;
  id<MTLBuffer> gradLimBuf;
  id<MTLBuffer> b1PowBuf;
  id<MTLBuffer> b2PowBuf;
  id<MTLBuffer> policyLossBuf;
  id<MTLBuffer> valueLossBuf;
  id<MTLBuffer> entMeanBuf;
  id<MTLBuffer> totalLossBuf;
  id<MTLBuffer> gradNormBuf;
  id<MTLBuffer> approxKlBuf;
  id<MTLBuffer> clipfracBuf;

  /* Prebuilt TensorData wrappers (no new MTLBuffer after create) */
  MPSGraphTensorData *fwdInPlanesTD;
  MPSGraphTensorData *fwdInScalarsTD;
  MPSGraphTensorData *fwdInWTD[NN_T_COUNT];
  MPSGraphTensorData *fwdOutLogitsTD;
  MPSGraphTensorData *fwdOutValuesTD;

  MPSGraphTensorData *updInPlanesTD;
  MPSGraphTensorData *updInScalarsTD;
  MPSGraphTensorData *updInActsTD;
  MPSGraphTensorData *updInOldLogpTD;
  MPSGraphTensorData *updInAdvTD;
  MPSGraphTensorData *updInRetTD;
  MPSGraphTensorData *updInWTD[NN_T_COUNT];
  MPSGraphTensorData *updInMTD[NN_T_COUNT];
  MPSGraphTensorData *updInVTD[NN_T_COUNT];
  MPSGraphTensorData *updInLrTD;
  MPSGraphTensorData *updInClipTD;
  MPSGraphTensorData *updInVcoefTD;
  MPSGraphTensorData *updInEcoefTD;
  MPSGraphTensorData *updInGradLimTD;
  MPSGraphTensorData *updInB1PowTD;
  MPSGraphTensorData *updInB2PowTD;
  MPSGraphTensorData *updOutWTD[NN_T_COUNT];
  MPSGraphTensorData *updOutMTD[NN_T_COUNT];
  MPSGraphTensorData *updOutVTD[NN_T_COUNT];
  MPSGraphTensorData *updOutPolicyTD;
  MPSGraphTensorData *updOutValueTD;
  MPSGraphTensorData *updOutEntTD;
  MPSGraphTensorData *updOutTotalTD;
  MPSGraphTensorData *updOutGradNormTD;
  MPSGraphTensorData *updOutApproxKlTD;
  MPSGraphTensorData *updOutClipfracTD;

  NSArray<MPSGraphTensorData *> *fwdInputs;
  NSArray<MPSGraphTensorData *> *fwdResults;
  NSArray<MPSGraphTensorData *> *updInputs;
  NSArray<MPSGraphTensorData *> *updResults;

  /* Host param mirror for load/save and init */
  float *hostParams;
  float *t[NN_T_COUNT];
};

/* ---- helpers ---- */

static id<MTLBuffer> make_buf(id<MTLDevice> dev, size_t nbytes, const void *init) {
  id<MTLBuffer> b =
      [dev newBufferWithLength:nbytes > 0 ? nbytes : 4
                       options:MTLResourceStorageModeShared];
  if (b && init && nbytes > 0)
    memcpy(b.contents, init, nbytes);
  else if (b)
    memset(b.contents, 0, b.length);
  return b;
}

static MPSShape *shape_from_dims(const int32_t *d, int ndim) {
  NSMutableArray *a = [NSMutableArray arrayWithCapacity:(NSUInteger)ndim];
  for (int i = 0; i < ndim; ++i)
    [a addObject:@(d[i])];
  return a;
}

static MPSShape *tensor_shape(int tid) {
  return shape_from_dims(NN_TENSOR_SHAPE[tid], NN_TENSOR_NDIM[tid]);
}

static MPSGraphTensor *ph_f32(MPSGraph *g, MPSShape *shape, NSString *name) {
  return [g placeholderWithShape:shape dataType:MPSDataTypeFloat32 name:name];
}

static MPSGraphTensor *linear_fwd(MPSGraph *g, MPSGraphTensor *x,
                                  MPSGraphTensor *W, MPSGraphTensor *b,
                                  NSString *name) {
  /* W [out,in], x [N,in] -> y = x @ W^T + b */
  MPSGraphTensor *Wt =
      [g transposeTensor:W dimension:0 withDimension:1 name:nil];
  MPSGraphTensor *y =
      [g matrixMultiplicationWithPrimaryTensor:x secondaryTensor:Wt name:name];
  if (b)
    y = [g additionWithPrimaryTensor:y secondaryTensor:b name:nil];
  return y;
}

static MPSGraphTensor *
policy_forward_body(MPSGraph *g, MPSGraphTensor *planes, MPSGraphTensor *scalars,
                    MPSGraphTensor *const *W, int N, MPSGraphTensor **logits_out,
                    MPSGraphTensor **values_out) {
  MPSGraphConvolution2DOpDescriptor *c1desc = [MPSGraphConvolution2DOpDescriptor
      descriptorWithStrideInX:NN_S1
                    strideInY:NN_S1
              dilationRateInX:1
              dilationRateInY:1
                       groups:1
                 paddingStyle:MPSGraphPaddingStyleTF_VALID
                   dataLayout:MPSGraphTensorNamedDataLayoutNCHW
                weightsLayout:MPSGraphTensorNamedDataLayoutOIHW];
  MPSGraphConvolution2DOpDescriptor *c2desc = [MPSGraphConvolution2DOpDescriptor
      descriptorWithStrideInX:NN_S2
                    strideInY:NN_S2
              dilationRateInX:1
              dilationRateInY:1
                       groups:1
                 paddingStyle:MPSGraphPaddingStyleTF_VALID
                   dataLayout:MPSGraphTensorNamedDataLayoutNCHW
                weightsLayout:MPSGraphTensorNamedDataLayoutOIHW];

  MPSGraphTensor *c1 =
      [g convolution2DWithSourceTensor:planes
                         weightsTensor:W[NN_T_CONV1_W]
                            descriptor:c1desc
                                  name:@"conv1"];
  MPSGraphTensor *b1r =
      [g reshapeTensor:W[NN_T_CONV1_B]
             withShape:@[ @1, @(NN_C_OUT1), @1, @1 ]
                  name:nil];
  c1 = [g additionWithPrimaryTensor:c1 secondaryTensor:b1r name:nil];
  c1 = [g reLUWithTensor:c1 name:@"relu1"];

  MPSGraphTensor *c2 =
      [g convolution2DWithSourceTensor:c1
                         weightsTensor:W[NN_T_CONV2_W]
                            descriptor:c2desc
                                  name:@"conv2"];
  MPSGraphTensor *b2r =
      [g reshapeTensor:W[NN_T_CONV2_B]
             withShape:@[ @1, @(NN_C_OUT2), @1, @1 ]
                  name:nil];
  c2 = [g additionWithPrimaryTensor:c2 secondaryTensor:b2r name:nil];
  c2 = [g reLUWithTensor:c2 name:@"relu2"];

  MPSGraphTensor *flat =
      [g reshapeTensor:c2 withShape:@[ @(N), @(NN_FLAT) ] name:@"flat"];
  MPSGraphTensor *fc_in =
      [g concatTensors:@[ flat, scalars ] dimension:1 name:@"fc_in"];

  MPSGraphTensor *hid =
      linear_fwd(g, fc_in, W[NN_T_FC_W], W[NN_T_FC_B], @"fc");
  hid = [g reLUWithTensor:hid name:@"relu_fc"];

  MPSGraphTensor *logits =
      linear_fwd(g, hid, W[NN_T_HEADS_W], W[NN_T_HEADS_B], @"heads");
  MPSGraphTensor *val =
      linear_fwd(g, hid, W[NN_T_VALUE_W], W[NN_T_VALUE_B], @"value");
  val = [g reshapeTensor:val withShape:@[ @(N) ] name:@"value_flat"];

  if (logits_out)
    *logits_out = logits;
  if (values_out)
    *values_out = val;
  return logits;
}

/* Multi-head categorical logp + entropy from packed logits and fixed acts.
 * softMax path: reductionMaximum AD is incomplete on this SDK. */
static void head_logp_entropy(MPSGraph *g, MPSGraphTensor *logits,
                              MPSGraphTensor *acts, int N,
                              MPSGraphTensor **logp_out,
                              MPSGraphTensor **ent_out) {
  MPSGraphTensor *logp = [g constantWithScalar:0.0
                                         shape:@[ @(N) ]
                                      dataType:MPSDataTypeFloat32];
  MPSGraphTensor *ent = [g constantWithScalar:0.0
                                        shape:@[ @(N) ]
                                     dataType:MPSDataTypeFloat32];
  MPSGraphTensor *eps =
      [g constantWithScalar:1e-20 dataType:MPSDataTypeFloat32];

  for (int h = 0; h < NN_N_HEAD; ++h) {
    const int w = NN_HEAD_WIDTHS[h];
    const int off = NN_HEAD_OFF[h];
    MPSGraphTensor *row =
        [g sliceTensor:logits dimension:1 start:off length:w name:nil];
    MPSGraphTensor *p = [g softMaxWithTensor:row axis:1 name:nil];
    MPSGraphTensor *log_p = [g logarithmWithTensor:
                                 [g additionWithPrimaryTensor:p
                                              secondaryTensor:eps
                                                         name:nil]
                                              name:nil];

    MPSGraphTensor *a_col =
        [g sliceTensor:acts dimension:1 start:h length:1 name:nil];
    MPSGraphTensor *a_flat =
        [g reshapeTensor:a_col withShape:@[ @(N) ] name:nil];
    MPSGraphTensor *oh =
        [g oneHotWithIndicesTensor:a_flat
                             depth:(NSUInteger)w
                          dataType:MPSDataTypeFloat32
                           onValue:1.0
                          offValue:0.0
                              name:nil];
    MPSGraphTensor *lp_h = [g reductionSumWithTensor:
                                 [g multiplicationWithPrimaryTensor:oh
                                                    secondaryTensor:log_p
                                                               name:nil]
                                                axis:1
                                                name:nil];
    lp_h = [g reshapeTensor:lp_h withShape:@[ @(N) ] name:nil];
    logp = [g additionWithPrimaryTensor:logp secondaryTensor:lp_h name:nil];

    MPSGraphTensor *plogp =
        [g multiplicationWithPrimaryTensor:p secondaryTensor:log_p name:nil];
    MPSGraphTensor *eh = [g negativeWithTensor:
                                [g reductionSumWithTensor:plogp axis:1 name:nil]
                                          name:nil];
    eh = [g reshapeTensor:eh withShape:@[ @(N) ] name:nil];
    ent = [g additionWithPrimaryTensor:ent secondaryTensor:eh name:nil];
  }
  *logp_out = logp;
  *ent_out = ent;
}

static int build_forward_graph(NnMetal *nn) {
  const int N = nn->batch_n;
  MPSGraph *g = [MPSGraph new];
  nn->fwdGraph = g;

  nn->fwdPlanesPh =
      ph_f32(g, @[ @(N), @(NN_N_CH), @(NN_CAM_H), @(NN_CAM_W) ], @"planes");
  nn->fwdScalarsPh = ph_f32(g, @[ @(N), @(NN_N_SCAL) ], @"scalars");
  for (int t = 0; t < NN_T_COUNT; ++t) {
    nn->fwdWPh[t] =
        ph_f32(g, tensor_shape(t),
               [NSString stringWithUTF8String:NN_TENSOR_NAMES[t]]);
  }

  MPSGraphTensor *W[NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t)
    W[t] = nn->fwdWPh[t];

  {
    MPSGraphTensor *logits = nil;
    MPSGraphTensor *values = nil;
    policy_forward_body(g, nn->fwdPlanesPh, nn->fwdScalarsPh, W, N, &logits,
                        &values);
    nn->fwdLogits = logits;
    nn->fwdValues = values;
  }

  /* Compile */
  NSMutableDictionary *feeds = [NSMutableDictionary dictionary];
  void (^add_feed)(MPSGraphTensor *) = ^(MPSGraphTensor *ph) {
    feeds[ph] = [[MPSGraphShapedType alloc] initWithShape:ph.shape
                                                 dataType:ph.dataType];
  };
  add_feed(nn->fwdPlanesPh);
  add_feed(nn->fwdScalarsPh);
  for (int t = 0; t < NN_T_COUNT; ++t)
    add_feed(nn->fwdWPh[t]);

  NSArray *targets = @[ nn->fwdLogits, nn->fwdValues ];
  /* Compilation can otherwise outlive an unused executable and race process
   * teardown. The blocking descriptor property is available from macOS 13. */
  MPSGraphCompilationDescriptor *compilation = [MPSGraphCompilationDescriptor new];
  if (@available(macOS 13.0, *))
    compilation.waitForCompilationCompletion = YES;
  nn->fwdExec = [g compileWithDevice:nn->mpsDevice
                               feeds:feeds
                       targetTensors:targets
                    targetOperations:nil
               compilationDescriptor:compilation];
  if (!nn->fwdExec) {
    set_err("forward graph compile failed");
    return -1;
  }
  return 0;
}

static int build_update_graph(NnMetal *nn) {
  const int N = nn->batch_n;
  MPSGraph *g = [MPSGraph new];
  nn->updGraph = g;

  nn->updPlanesPh =
      ph_f32(g, @[ @(N), @(NN_N_CH), @(NN_CAM_H), @(NN_CAM_W) ], @"u_planes");
  nn->updScalarsPh = ph_f32(g, @[ @(N), @(NN_N_SCAL) ], @"u_scalars");
  nn->updActsPh =
      [g placeholderWithShape:@[ @(N), @(NN_N_HEAD) ]
                     dataType:MPSDataTypeInt32
                         name:@"u_acts"];
  nn->updOldLogpPh = ph_f32(g, @[ @(N) ], @"u_old_logp");
  nn->updAdvPh = ph_f32(g, @[ @(N) ], @"u_adv");
  nn->updRetPh = ph_f32(g, @[ @(N) ], @"u_ret");
  for (int t = 0; t < NN_T_COUNT; ++t) {
    NSString *nm = [NSString stringWithUTF8String:NN_TENSOR_NAMES[t]];
    nn->updWPh[t] = ph_f32(g, tensor_shape(t), [nm stringByAppendingString:@"_w"]);
    nn->updMPh[t] = ph_f32(g, tensor_shape(t), [nm stringByAppendingString:@"_m"]);
    nn->updVPh[t] = ph_f32(g, tensor_shape(t), [nm stringByAppendingString:@"_v"]);
  }
  nn->updLrPh = ph_f32(g, @[], @"lr");
  nn->updClipPh = ph_f32(g, @[], @"clip");
  nn->updVcoefPh = ph_f32(g, @[], @"vcoef");
  nn->updEcoefPh = ph_f32(g, @[], @"ecoef");
  nn->updGradLimPh = ph_f32(g, @[], @"grad_lim");
  nn->updB1PowPh = ph_f32(g, @[], @"b1pow");
  nn->updB2PowPh = ph_f32(g, @[], @"b2pow");

  MPSGraphTensor *W[NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t)
    W[t] = nn->updWPh[t];

  MPSGraphTensor *logits = nil;
  MPSGraphTensor *values = nil;
  policy_forward_body(g, nn->updPlanesPh, nn->updScalarsPh, W, N, &logits,
                      &values);

  MPSGraphTensor *logp = nil;
  MPSGraphTensor *ent = nil;
  head_logp_entropy(g, logits, nn->updActsPh, N, &logp, &ent);

  /* PPO losses (means) */
  MPSGraphTensor *invN =
      [g constantWithScalar:(1.0 / (double)N) dataType:MPSDataTypeFloat32];
  MPSGraphTensor *one =
      [g constantWithScalar:1.0 dataType:MPSDataTypeFloat32];
  MPSGraphTensor *ratio = [g exponentWithTensor:
                                 [g subtractionWithPrimaryTensor:logp
                                                 secondaryTensor:nn->updOldLogpPh
                                                            name:nil]
                                           name:nil];
  MPSGraphTensor *lo =
      [g subtractionWithPrimaryTensor:one secondaryTensor:nn->updClipPh name:nil];
  MPSGraphTensor *hi =
      [g additionWithPrimaryTensor:one secondaryTensor:nn->updClipPh name:nil];
  /* Exact clipped PPO without minimum/select. MPSGraph AD gives zero policy
   * gradient at equal minimum branches and cannot differentiate select here.
   * Split by advantage sign with ReLU:
   *   adv >= 0: min(ratio, hi) * adv
   *   adv <  0: max(ratio, lo) * adv
   * At ratio==1 this has the CPU contract gradient. */
  MPSGraphTensor *above_hi = [g reLUWithTensor:
                                    [g subtractionWithPrimaryTensor:ratio
                                                    secondaryTensor:hi
                                                               name:nil]
                                            name:nil];
  MPSGraphTensor *ratio_cap_hi =
      [g subtractionWithPrimaryTensor:ratio secondaryTensor:above_hi name:nil];
  MPSGraphTensor *below_lo = [g reLUWithTensor:
                                    [g subtractionWithPrimaryTensor:lo
                                                    secondaryTensor:ratio
                                                               name:nil]
                                            name:nil];
  MPSGraphTensor *ratio_cap_lo =
      [g additionWithPrimaryTensor:ratio secondaryTensor:below_lo name:nil];
  MPSGraphTensor *adv_pos = [g reLUWithTensor:nn->updAdvPh name:nil];
  MPSGraphTensor *adv_neg = [g negativeWithTensor:
                                   [g reLUWithTensor:
                                          [g negativeWithTensor:nn->updAdvPh
                                                           name:nil]
                                                     name:nil]
                                             name:nil];
  MPSGraphTensor *obj = [g additionWithPrimaryTensor:
                                [g multiplicationWithPrimaryTensor:ratio_cap_hi
                                                     secondaryTensor:adv_pos
                                                                name:nil]
                                         secondaryTensor:
                                             [g multiplicationWithPrimaryTensor:
                                                    ratio_cap_lo
                                                    secondaryTensor:adv_neg
                                                    name:nil]
                                                    name:nil];
  MPSGraphTensor *neg_obj = [g negativeWithTensor:obj name:nil];
  MPSGraphTensor *policy_loss =
      [g multiplicationWithPrimaryTensor:
              [g reductionSumWithTensor:neg_obj
                                   axes:nil
                                   name:nil]
                          secondaryTensor:invN
                                     name:nil];
  MPSGraphTensor *vdiff =
      [g subtractionWithPrimaryTensor:values secondaryTensor:nn->updRetPh name:nil];
  MPSGraphTensor *vsq = [g squareWithTensor:vdiff name:nil];
  MPSGraphTensor *value_mse =
      [g multiplicationWithPrimaryTensor:
              [g reductionSumWithTensor:vsq axes:nil name:nil]
                          secondaryTensor:invN
                                     name:nil];
  MPSGraphTensor *value_loss =
      [g multiplicationWithPrimaryTensor:nn->updVcoefPh
                         secondaryTensor:value_mse
                                    name:nil];
  MPSGraphTensor *ent_mean =
      [g multiplicationWithPrimaryTensor:
              [g reductionSumWithTensor:ent axes:nil name:nil]
                          secondaryTensor:invN
                                     name:nil];
  MPSGraphTensor *ent_term =
      [g multiplicationWithPrimaryTensor:nn->updEcoefPh
                         secondaryTensor:ent_mean
                                    name:nil];
  MPSGraphTensor *total =
      [g subtractionWithPrimaryTensor:
              [g additionWithPrimaryTensor:policy_loss
                           secondaryTensor:value_loss
                                      name:nil]
                      secondaryTensor:ent_term
                                 name:nil];

  nn->updPolicyLoss = policy_loss;
  nn->updValueLoss = value_loss;
  nn->updEntMean = ent_mean;
  nn->updTotalLoss = total;

  /* Diagnostic only. Not ancestors of `total`; AD / Adam ignore them. */
  {
    MPSGraphTensor *log_ratio = [g logarithmWithTensor:ratio name:nil];
    MPSGraphTensor *ratio_m1 =
        [g subtractionWithPrimaryTensor:ratio secondaryTensor:one name:nil];
    MPSGraphTensor *kl_i =
        [g subtractionWithPrimaryTensor:ratio_m1
                        secondaryTensor:log_ratio
                                   name:nil];
    nn->updApproxKl =
        [g multiplicationWithPrimaryTensor:
                [g reductionSumWithTensor:kl_i axes:nil name:nil]
                           secondaryTensor:invN
                                      name:nil];
    MPSGraphTensor *zero_f =
        [g constantWithScalar:0.0 dataType:MPSDataTypeFloat32];
    MPSGraphTensor *is_clip =
        [g greaterThanWithPrimaryTensor:[g absoluteWithTensor:ratio_m1
                                                         name:nil]
                        secondaryTensor:nn->updClipPh
                                   name:nil];
    MPSGraphTensor *clip_f = [g selectWithPredicateTensor:is_clip
                                      truePredicateTensor:one
                                     falsePredicateTensor:zero_f
                                                     name:nil];
    nn->updClipfrac =
        [g multiplicationWithPrimaryTensor:
                [g reductionSumWithTensor:clip_f axes:nil name:nil]
                           secondaryTensor:invN
                                      name:nil];
  }

  /* Gradients of total w.r.t. each weight placeholder */
  NSMutableArray *wlist = [NSMutableArray arrayWithCapacity:NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t)
    [wlist addObject:nn->updWPh[t]];
  NSDictionary<MPSGraphTensor *, MPSGraphTensor *> *grads =
      [g gradientForPrimaryTensor:total withTensors:wlist name:@"grads"];

  /* Global L2 norm + clip */
  MPSGraphTensor *sumsq =
      [g constantWithScalar:0.0 dataType:MPSDataTypeFloat32];
  MPSGraphTensor *gT[NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t) {
    gT[t] = grads[nn->updWPh[t]];
    if (!gT[t]) {
      set_err("missing gradient tensor");
      return -1;
    }
    MPSGraphTensor *gsq = [g squareWithTensor:gT[t] name:nil];
    MPSGraphTensor *s =
        [g reductionSumWithTensor:gsq axes:nil name:nil];
    sumsq = [g additionWithPrimaryTensor:sumsq secondaryTensor:s name:nil];
  }
  MPSGraphTensor *norm = [g squareRootWithTensor:sumsq name:nil];
  nn->updGradNorm = norm;

  MPSGraphTensor *eps6 =
      [g constantWithScalar:1e-6 dataType:MPSDataTypeFloat32];
  MPSGraphTensor *scale_num = nn->updGradLimPh;
  MPSGraphTensor *scale_den =
      [g additionWithPrimaryTensor:norm secondaryTensor:eps6 name:nil];
  MPSGraphTensor *scale =
      [g divisionWithPrimaryTensor:scale_num secondaryTensor:scale_den name:nil];
  MPSGraphTensor *need_clip =
      [g greaterThanWithPrimaryTensor:norm
                      secondaryTensor:nn->updGradLimPh
                                 name:nil];
  MPSGraphTensor *one_f =
      [g constantWithScalar:1.0 dataType:MPSDataTypeFloat32];
  MPSGraphTensor *clip_scale =
      [g selectWithPredicateTensor:need_clip
               truePredicateTensor:scale
              falsePredicateTensor:one_f
                              name:nil];

  /* Adam: match cpu.c
   * m = b1*m + (1-b1)*g
   * v = b2*v + (1-b2)*g^2
   * mhat = m/(1-b1^t); vhat = v/(1-b2^t)
   * w -= lr * mhat / (sqrt(vhat)+eps)
   * Host feeds b1pow=beta1^t, b2pow=beta2^t for the NEW step t. */
  MPSGraphTensor *b1 = [g constantWithScalar:(double)kAdamBeta1
                                    dataType:MPSDataTypeFloat32];
  MPSGraphTensor *b2 = [g constantWithScalar:(double)kAdamBeta2
                                    dataType:MPSDataTypeFloat32];
  MPSGraphTensor *omb1 =
      [g constantWithScalar:(double)(1.f - kAdamBeta1)
                   dataType:MPSDataTypeFloat32];
  MPSGraphTensor *omb2 =
      [g constantWithScalar:(double)(1.f - kAdamBeta2)
                   dataType:MPSDataTypeFloat32];
  MPSGraphTensor *aeps =
      [g constantWithScalar:(double)kAdamEps dataType:MPSDataTypeFloat32];
  MPSGraphTensor *bc1 =
      [g subtractionWithPrimaryTensor:one_f
                      secondaryTensor:nn->updB1PowPh
                                 name:nil];
  MPSGraphTensor *bc2 =
      [g subtractionWithPrimaryTensor:one_f
                      secondaryTensor:nn->updB2PowPh
                                 name:nil];

  for (int t = 0; t < NN_T_COUNT; ++t) {
    MPSGraphTensor *gc =
        [g multiplicationWithPrimaryTensor:gT[t]
                           secondaryTensor:clip_scale
                                      name:nil];
    MPSGraphTensor *m_new = [g additionWithPrimaryTensor:
                                   [g multiplicationWithPrimaryTensor:b1
                                                      secondaryTensor:nn->updMPh[t]
                                                                 name:nil]
                                         secondaryTensor:
                                             [g multiplicationWithPrimaryTensor:omb1
                                                                secondaryTensor:gc
                                                                           name:nil]
                                                    name:nil];
    MPSGraphTensor *g2 = [g squareWithTensor:gc name:nil];
    MPSGraphTensor *v_new = [g additionWithPrimaryTensor:
                                   [g multiplicationWithPrimaryTensor:b2
                                                      secondaryTensor:nn->updVPh[t]
                                                                 name:nil]
                                         secondaryTensor:
                                             [g multiplicationWithPrimaryTensor:omb2
                                                                secondaryTensor:g2
                                                                           name:nil]
                                                    name:nil];
    MPSGraphTensor *mhat =
        [g divisionWithPrimaryTensor:m_new secondaryTensor:bc1 name:nil];
    MPSGraphTensor *vhat =
        [g divisionWithPrimaryTensor:v_new secondaryTensor:bc2 name:nil];
    MPSGraphTensor *denom =
        [g additionWithPrimaryTensor:[g squareRootWithTensor:vhat name:nil]
                     secondaryTensor:aeps
                                name:nil];
    MPSGraphTensor *step =
        [g multiplicationWithPrimaryTensor:nn->updLrPh
                           secondaryTensor:
                               [g divisionWithPrimaryTensor:mhat
                                            secondaryTensor:denom
                                                       name:nil]
                                      name:nil];
    MPSGraphTensor *w_new =
        [g subtractionWithPrimaryTensor:nn->updWPh[t]
                        secondaryTensor:step
                                   name:nil];
    nn->updNewW[t] = w_new;
    nn->updNewM[t] = m_new;
    nn->updNewV[t] = v_new;
  }

  /* Compile feeds and targets */
  NSMutableDictionary *feeds = [NSMutableDictionary dictionary];
  void (^add_feed)(MPSGraphTensor *) = ^(MPSGraphTensor *ph) {
    feeds[ph] = [[MPSGraphShapedType alloc] initWithShape:ph.shape
                                                 dataType:ph.dataType];
  };
  add_feed(nn->updPlanesPh);
  add_feed(nn->updScalarsPh);
  add_feed(nn->updActsPh);
  add_feed(nn->updOldLogpPh);
  add_feed(nn->updAdvPh);
  add_feed(nn->updRetPh);
  for (int t = 0; t < NN_T_COUNT; ++t) {
    add_feed(nn->updWPh[t]);
    add_feed(nn->updMPh[t]);
    add_feed(nn->updVPh[t]);
  }
  add_feed(nn->updLrPh);
  add_feed(nn->updClipPh);
  add_feed(nn->updVcoefPh);
  add_feed(nn->updEcoefPh);
  add_feed(nn->updGradLimPh);
  add_feed(nn->updB1PowPh);
  add_feed(nn->updB2PowPh);

  NSMutableArray *targets = [NSMutableArray array];
  for (int t = 0; t < NN_T_COUNT; ++t) {
    [targets addObject:nn->updNewW[t]];
    [targets addObject:nn->updNewM[t]];
    [targets addObject:nn->updNewV[t]];
  }
  [targets addObject:nn->updPolicyLoss];
  [targets addObject:nn->updValueLoss];
  [targets addObject:nn->updEntMean];
  [targets addObject:nn->updTotalLoss];
  [targets addObject:nn->updGradNorm];
  [targets addObject:nn->updApproxKl];
  [targets addObject:nn->updClipfrac];

  MPSGraphCompilationDescriptor *compilation = [MPSGraphCompilationDescriptor new];
  if (@available(macOS 13.0, *))
    compilation.waitForCompilationCompletion = YES;
  nn->updExec = [g compileWithDevice:nn->mpsDevice
                               feeds:feeds
                       targetTensors:targets
                    targetOperations:nil
               compilationDescriptor:compilation];
  if (!nn->updExec) {
    set_err("update graph compile failed");
    return -1;
  }
  return 0;
}

/* Build TensorData arrays matching executable feed/target order. */
static int build_tensor_data(NnMetal *nn) {
  const int N = nn->batch_n;

  nn->fwdInPlanesTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->planesBuf
                  shape:@[ @(N), @(NN_N_CH), @(NN_CAM_H), @(NN_CAM_W) ]
               dataType:MPSDataTypeFloat32];
  nn->fwdInScalarsTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->scalarsBuf
                  shape:@[ @(N), @(NN_N_SCAL) ]
               dataType:MPSDataTypeFloat32];
  for (int t = 0; t < NN_T_COUNT; ++t) {
    nn->fwdInWTD[t] = [[MPSGraphTensorData alloc]
        initWithMTLBuffer:nn->wBuf[t]
                    shape:tensor_shape(t)
                 dataType:MPSDataTypeFloat32];
  }
  nn->fwdOutLogitsTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->logitsBuf
                  shape:@[ @(N), @(NN_N_LOGITS) ]
               dataType:MPSDataTypeFloat32];
  nn->fwdOutValuesTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->valuesBuf
                  shape:@[ @(N) ]
               dataType:MPSDataTypeFloat32];

  /* Map feedTensors order for forward executable */
  {
    NSArray<MPSGraphTensor *> *feeds = nn->fwdExec.feedTensors;
    NSMutableArray *ins =
        [NSMutableArray arrayWithCapacity:feeds.count];
    NSDictionary *map = @{
      (id)nn->fwdPlanesPh : nn->fwdInPlanesTD,
      (id)nn->fwdScalarsPh : nn->fwdInScalarsTD,
    };
    NSMutableDictionary *md = [map mutableCopy];
    for (int t = 0; t < NN_T_COUNT; ++t)
      md[(id)nn->fwdWPh[t]] = nn->fwdInWTD[t];
    for (MPSGraphTensor *ph in feeds) {
      MPSGraphTensorData *td = md[(id)ph];
      if (!td) {
        set_err("forward feed tensor map miss");
        return -1;
      }
      [ins addObject:td];
    }
    nn->fwdInputs = ins;

    NSArray<MPSGraphTensor *> *tgts = nn->fwdExec.targetTensors;
    NSMutableArray *outs = [NSMutableArray arrayWithCapacity:tgts.count];
    NSDictionary *omap = @{
      (id)nn->fwdLogits : nn->fwdOutLogitsTD,
      (id)nn->fwdValues : nn->fwdOutValuesTD,
    };
    for (MPSGraphTensor *tt in tgts) {
      MPSGraphTensorData *td = omap[(id)tt];
      if (!td) {
        set_err("forward target tensor map miss");
        return -1;
      }
      [outs addObject:td];
    }
    nn->fwdResults = outs;
  }

  nn->updInPlanesTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->planesBuf
                  shape:@[ @(N), @(NN_N_CH), @(NN_CAM_H), @(NN_CAM_W) ]
               dataType:MPSDataTypeFloat32];
  nn->updInScalarsTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->scalarsBuf
                  shape:@[ @(N), @(NN_N_SCAL) ]
               dataType:MPSDataTypeFloat32];
  nn->updInActsTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->actsBuf
                  shape:@[ @(N), @(NN_N_HEAD) ]
               dataType:MPSDataTypeInt32];
  nn->updInOldLogpTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->oldLogpBuf
                  shape:@[ @(N) ]
               dataType:MPSDataTypeFloat32];
  nn->updInAdvTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->advBuf
                  shape:@[ @(N) ]
               dataType:MPSDataTypeFloat32];
  nn->updInRetTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->retBuf
                  shape:@[ @(N) ]
               dataType:MPSDataTypeFloat32];
  for (int t = 0; t < NN_T_COUNT; ++t) {
    nn->updInWTD[t] = [[MPSGraphTensorData alloc]
        initWithMTLBuffer:nn->wBuf[t]
                    shape:tensor_shape(t)
                 dataType:MPSDataTypeFloat32];
    nn->updInMTD[t] = [[MPSGraphTensorData alloc]
        initWithMTLBuffer:nn->mBuf[t]
                    shape:tensor_shape(t)
                 dataType:MPSDataTypeFloat32];
    nn->updInVTD[t] = [[MPSGraphTensorData alloc]
        initWithMTLBuffer:nn->vBuf[t]
                    shape:tensor_shape(t)
                 dataType:MPSDataTypeFloat32];
    nn->updOutWTD[t] = [[MPSGraphTensorData alloc]
        initWithMTLBuffer:nn->wOutBuf[t]
                    shape:tensor_shape(t)
                 dataType:MPSDataTypeFloat32];
    nn->updOutMTD[t] = [[MPSGraphTensorData alloc]
        initWithMTLBuffer:nn->mOutBuf[t]
                    shape:tensor_shape(t)
                 dataType:MPSDataTypeFloat32];
    nn->updOutVTD[t] = [[MPSGraphTensorData alloc]
        initWithMTLBuffer:nn->vOutBuf[t]
                    shape:tensor_shape(t)
                 dataType:MPSDataTypeFloat32];
  }
  nn->updInLrTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->lrBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updInClipTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->clipBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updInVcoefTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->vcoefBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updInEcoefTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->ecoefBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updInGradLimTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->gradLimBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updInB1PowTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->b1PowBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updInB2PowTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->b2PowBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updOutPolicyTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->policyLossBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updOutValueTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->valueLossBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updOutEntTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->entMeanBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updOutTotalTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->totalLossBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updOutGradNormTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->gradNormBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updOutApproxKlTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->approxKlBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];
  nn->updOutClipfracTD = [[MPSGraphTensorData alloc]
      initWithMTLBuffer:nn->clipfracBuf
                  shape:@[]
               dataType:MPSDataTypeFloat32];

  {
    NSArray<MPSGraphTensor *> *feeds = nn->updExec.feedTensors;
    NSMutableDictionary *md = [NSMutableDictionary dictionary];
    md[(id)nn->updPlanesPh] = nn->updInPlanesTD;
    md[(id)nn->updScalarsPh] = nn->updInScalarsTD;
    md[(id)nn->updActsPh] = nn->updInActsTD;
    md[(id)nn->updOldLogpPh] = nn->updInOldLogpTD;
    md[(id)nn->updAdvPh] = nn->updInAdvTD;
    md[(id)nn->updRetPh] = nn->updInRetTD;
    for (int t = 0; t < NN_T_COUNT; ++t) {
      md[(id)nn->updWPh[t]] = nn->updInWTD[t];
      md[(id)nn->updMPh[t]] = nn->updInMTD[t];
      md[(id)nn->updVPh[t]] = nn->updInVTD[t];
    }
    md[(id)nn->updLrPh] = nn->updInLrTD;
    md[(id)nn->updClipPh] = nn->updInClipTD;
    md[(id)nn->updVcoefPh] = nn->updInVcoefTD;
    md[(id)nn->updEcoefPh] = nn->updInEcoefTD;
    md[(id)nn->updGradLimPh] = nn->updInGradLimTD;
    md[(id)nn->updB1PowPh] = nn->updInB1PowTD;
    md[(id)nn->updB2PowPh] = nn->updInB2PowTD;

    NSMutableArray *ins = [NSMutableArray arrayWithCapacity:feeds.count];
    for (MPSGraphTensor *ph in feeds) {
      MPSGraphTensorData *td = md[(id)ph];
      if (!td) {
        set_err("update feed tensor map miss");
        return -1;
      }
      [ins addObject:td];
    }
    nn->updInputs = ins;

    NSArray<MPSGraphTensor *> *tgts = nn->updExec.targetTensors;
    NSMutableDictionary *om = [NSMutableDictionary dictionary];
    for (int t = 0; t < NN_T_COUNT; ++t) {
      om[(id)nn->updNewW[t]] = nn->updOutWTD[t];
      om[(id)nn->updNewM[t]] = nn->updOutMTD[t];
      om[(id)nn->updNewV[t]] = nn->updOutVTD[t];
    }
    om[(id)nn->updPolicyLoss] = nn->updOutPolicyTD;
    om[(id)nn->updValueLoss] = nn->updOutValueTD;
    om[(id)nn->updEntMean] = nn->updOutEntTD;
    om[(id)nn->updTotalLoss] = nn->updOutTotalTD;
    om[(id)nn->updGradNorm] = nn->updOutGradNormTD;
    om[(id)nn->updApproxKl] = nn->updOutApproxKlTD;
    om[(id)nn->updClipfrac] = nn->updOutClipfracTD;

    NSMutableArray *outs = [NSMutableArray arrayWithCapacity:tgts.count];
    for (MPSGraphTensor *tt in tgts) {
      MPSGraphTensorData *td = om[(id)tt];
      if (!td) {
        set_err("update target tensor map miss");
        return -1;
      }
      [outs addObject:td];
    }
    nn->updResults = outs;
  }
  return 0;
}

/* Convert uint8 planes to float32 with depth scale into shared buffer. */
static void upload_planes(NnMetal *nn, const uint8_t *planes, int n) {
  float *dst = (float *)nn->planesBuf.contents;
  const size_t plane_n =
      (size_t)n * (size_t)NN_N_CH * (size_t)NN_CAM_H * (size_t)NN_CAM_W;
  for (size_t i = 0; i < plane_n; ++i) {
    /* recover (ni, c, h, w) for depth scale */
    size_t rem = i;
    const size_t hw = (size_t)NN_CAM_H * NN_CAM_W;
    const size_t chw = (size_t)NN_N_CH * hw;
    const int ni = (int)(rem / chw);
    rem %= chw;
    const int c = (int)(rem / hw);
    float x = (float)planes[i];
    if (c == NN_DEPTH_CH0 || c == NN_DEPTH_CH1)
      x *= (1.f / 255.f);
    dst[i] = x;
    (void)ni;
  }
}

static void sync_host_params_from_device(NnMetal *nn) {
  size_t off = 0;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    size_t n = NN_TENSOR_FLOATS[t];
    memcpy(nn->hostParams + off, nn->wBuf[t].contents, n * sizeof(float));
    off += n;
  }
}

static void sync_device_params_from_host(NnMetal *nn) {
  size_t off = 0;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    size_t n = NN_TENSOR_FLOATS[t];
    memcpy(nn->wBuf[t].contents, nn->hostParams + off, n * sizeof(float));
    off += n;
  }
}

static void zero_adam(NnMetal *nn) {
  for (int t = 0; t < NN_T_COUNT; ++t) {
    memset(nn->mBuf[t].contents, 0, NN_TENSOR_FLOATS[t] * sizeof(float));
    memset(nn->vBuf[t].contents, 0, NN_TENSOR_FLOATS[t] * sizeof(float));
  }
  nn->adam_t = 0;
}

/* ---- public API ---- */

NnMetal *nn_metal_create(int batch_n, int device_id, const NnConfig *cfg) {
  g_err[0] = 0;
  if (device_id != 0) {
    set_err("Metal device must be 0");
    return NULL;
  }
  if (batch_n <= 0) {
    set_err("batch_n must be > 0");
    return NULL;
  }
  NnConfig resolved = cfg ? *cfg : nn_config_default();
  if (validate_config(&resolved) != 0)
    return NULL;

  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (!device) {
    set_err("Metal device unavailable");
    return NULL;
  }
  /* MPSGraph availability: try creating a trivial graph */
  MPSGraph *probe = [MPSGraph new];
  if (!probe) {
    set_err("MPSGraph unavailable");
    return NULL;
  }
  probe = nil;

  NnMetal *nn = (NnMetal *)calloc(1, sizeof(NnMetal));
  if (!nn) {
    set_err("oom handle");
    return NULL;
  }
  nn->batch_n = batch_n;
  nn->cfg = resolved;
  nn->sample_step = 0;
  nn->adam_t = 0;
  nn->n_params = nn_model_param_floats();
  nn->device = device;
  nn->queue = [device newCommandQueue];
  if (!nn->queue) {
    set_err("Metal command queue unavailable");
    nn_metal_destroy(nn);
    return NULL;
  }
  nn->mpsDevice = [MPSGraphDevice deviceWithMTLDevice:device];

  nn->hostParams = (float *)calloc(nn->n_params, sizeof(float));
  if (!nn->hostParams) {
    set_err("oom params");
    nn_metal_destroy(nn);
    return NULL;
  }
  for (int t = 0; t < NN_T_COUNT; ++t)
    nn->t[t] = nn->hostParams + tensor_offset(t);
  nn_fixture_init_weights(nn->t, 0xC0FFEEu);

  for (int t = 0; t < NN_T_COUNT; ++t) {
    size_t nbytes = NN_TENSOR_FLOATS[t] * sizeof(float);
    nn->wBuf[t] = make_buf(device, nbytes, nn->t[t]);
    nn->mBuf[t] = make_buf(device, nbytes, NULL);
    nn->vBuf[t] = make_buf(device, nbytes, NULL);
    nn->wOutBuf[t] = make_buf(device, nbytes, NULL);
    nn->mOutBuf[t] = make_buf(device, nbytes, NULL);
    nn->vOutBuf[t] = make_buf(device, nbytes, NULL);
    if (!nn->wBuf[t] || !nn->mBuf[t] || !nn->vBuf[t] || !nn->wOutBuf[t] ||
        !nn->mOutBuf[t] || !nn->vOutBuf[t]) {
      set_err("oom weight buffers");
      nn_metal_destroy(nn);
      return NULL;
    }
  }

  const size_t N = (size_t)batch_n;
  nn->planesBuf = make_buf(
      device, N * NN_N_CH * NN_CAM_H * NN_CAM_W * sizeof(float), NULL);
  nn->scalarsBuf = make_buf(device, N * NN_N_SCAL * sizeof(float), NULL);
  nn->actsBuf = make_buf(device, N * NN_N_HEAD * sizeof(int32_t), NULL);
  nn->oldLogpBuf = make_buf(device, N * sizeof(float), NULL);
  nn->advBuf = make_buf(device, N * sizeof(float), NULL);
  nn->retBuf = make_buf(device, N * sizeof(float), NULL);
  nn->logitsBuf = make_buf(device, N * NN_N_LOGITS * sizeof(float), NULL);
  nn->valuesBuf = make_buf(device, N * sizeof(float), NULL);
  nn->lrBuf = make_buf(device, sizeof(float), NULL);
  nn->clipBuf = make_buf(device, sizeof(float), NULL);
  nn->vcoefBuf = make_buf(device, sizeof(float), NULL);
  nn->ecoefBuf = make_buf(device, sizeof(float), NULL);
  nn->gradLimBuf = make_buf(device, sizeof(float), NULL);
  nn->b1PowBuf = make_buf(device, sizeof(float), NULL);
  nn->b2PowBuf = make_buf(device, sizeof(float), NULL);
  nn->policyLossBuf = make_buf(device, sizeof(float), NULL);
  nn->valueLossBuf = make_buf(device, sizeof(float), NULL);
  nn->entMeanBuf = make_buf(device, sizeof(float), NULL);
  nn->totalLossBuf = make_buf(device, sizeof(float), NULL);
  nn->gradNormBuf = make_buf(device, sizeof(float), NULL);
  nn->approxKlBuf = make_buf(device, sizeof(float), NULL);
  nn->clipfracBuf = make_buf(device, sizeof(float), NULL);

  if (!nn->planesBuf || !nn->scalarsBuf || !nn->actsBuf || !nn->logitsBuf) {
    set_err("oom input buffers");
    nn_metal_destroy(nn);
    return NULL;
  }

  @autoreleasepool {
    if (build_forward_graph(nn) != 0) {
      nn_metal_destroy(nn);
      return NULL;
    }
    if (build_update_graph(nn) != 0) {
      nn_metal_destroy(nn);
      return NULL;
    }
    if (build_tensor_data(nn) != 0) {
      nn_metal_destroy(nn);
      return NULL;
    }
  }

  return nn;
}

void nn_metal_destroy(NnMetal *nn) {
  if (!nn)
    return;
  free(nn->hostParams);
  nn->hostParams = NULL;
  /* ARC releases ObjC members when the struct is freed only if they are
   * __strong fields; we stored them as plain id in a C struct allocated
   * with calloc, so release explicitly under ARC by assigning nil.
   * Drop TensorData first, then buffers they wrap. */
  nn->fwdInputs = nil;
  nn->fwdResults = nil;
  nn->updInputs = nil;
  nn->updResults = nil;
  nn->fwdExec = nil;
  nn->updExec = nil;
  nn->fwdGraph = nil;
  nn->updGraph = nil;

  nn->fwdInPlanesTD = nil;
  nn->fwdInScalarsTD = nil;
  nn->fwdOutLogitsTD = nil;
  nn->fwdOutValuesTD = nil;
  nn->updInPlanesTD = nil;
  nn->updInScalarsTD = nil;
  nn->updInActsTD = nil;
  nn->updInOldLogpTD = nil;
  nn->updInAdvTD = nil;
  nn->updInRetTD = nil;
  nn->updInLrTD = nil;
  nn->updInClipTD = nil;
  nn->updInVcoefTD = nil;
  nn->updInEcoefTD = nil;
  nn->updInGradLimTD = nil;
  nn->updInB1PowTD = nil;
  nn->updInB2PowTD = nil;
  nn->updOutPolicyTD = nil;
  nn->updOutValueTD = nil;
  nn->updOutEntTD = nil;
  nn->updOutTotalTD = nil;
  nn->updOutGradNormTD = nil;
  nn->updOutApproxKlTD = nil;
  nn->updOutClipfracTD = nil;
  for (int t = 0; t < NN_T_COUNT; ++t) {
    nn->fwdInWTD[t] = nil;
    nn->updInWTD[t] = nil;
    nn->updInMTD[t] = nil;
    nn->updInVTD[t] = nil;
    nn->updOutWTD[t] = nil;
    nn->updOutMTD[t] = nil;
    nn->updOutVTD[t] = nil;
  }

  for (int t = 0; t < NN_T_COUNT; ++t) {
    nn->wBuf[t] = nil;
    nn->mBuf[t] = nil;
    nn->vBuf[t] = nil;
    nn->wOutBuf[t] = nil;
    nn->mOutBuf[t] = nil;
    nn->vOutBuf[t] = nil;
  }
  nn->planesBuf = nil;
  nn->scalarsBuf = nil;
  nn->actsBuf = nil;
  nn->oldLogpBuf = nil;
  nn->advBuf = nil;
  nn->retBuf = nil;
  nn->logitsBuf = nil;
  nn->valuesBuf = nil;
  nn->lrBuf = nil;
  nn->clipBuf = nil;
  nn->vcoefBuf = nil;
  nn->ecoefBuf = nil;
  nn->gradLimBuf = nil;
  nn->b1PowBuf = nil;
  nn->b2PowBuf = nil;
  nn->policyLossBuf = nil;
  nn->valueLossBuf = nil;
  nn->entMeanBuf = nil;
  nn->totalLossBuf = nil;
  nn->gradNormBuf = nil;
  nn->approxKlBuf = nil;
  nn->clipfracBuf = nil;

  nn->queue = nil;
  nn->device = nil;
  nn->mpsDevice = nil;
  free(nn);
}

int64_t nn_metal_training_steps(const NnMetal *nn) {
  return nn ? nn->adam_t : -1;
}

int nn_metal_set_config(NnMetal *nn, const NnConfig *cfg) {
  if (!nn || !cfg) {
    set_err("null");
    return -1;
  }
  if (validate_config(cfg) != 0)
    return -1;
  if (cfg->rng_seed != nn->cfg.rng_seed)
    nn->sample_step = 0;
  nn->cfg = *cfg;
  return 0;
}

int nn_metal_forward(NnMetal *nn, const uint8_t *planes, const float *scalars,
                     int n, float *logits, float *values) {
  if (!nn || !planes || !scalars || !logits || !values) {
    set_err("null pointer");
    return -1;
  }
  if (n != nn->batch_n) {
    set_err("batch size mismatch (fixed at create)");
    return -1;
  }

  @autoreleasepool {
    upload_planes(nn, planes, n);
    memcpy(nn->scalarsBuf.contents, scalars,
           (size_t)n * NN_N_SCAL * sizeof(float));

    MPSGraphExecutableExecutionDescriptor *desc =
        [MPSGraphExecutableExecutionDescriptor new];
    desc.waitUntilCompleted = YES;
    [nn->fwdExec runWithMTLCommandQueue:nn->queue
                            inputsArray:nn->fwdInputs
                           resultsArray:nn->fwdResults
                    executionDescriptor:desc];

    memcpy(logits, nn->logitsBuf.contents,
           (size_t)n * NN_N_LOGITS * sizeof(float));
    memcpy(values, nn->valuesBuf.contents, (size_t)n * sizeof(float));
  }
  return 0;
}

int nn_metal_sample(NnMetal *nn, const float *logits, int n, int mode,
                    int32_t *acts, float *logp, float *entropy) {
  if (!nn || !logits || !acts || !logp) {
    set_err("null pointer");
    return -1;
  }
  if (n != nn->batch_n) {
    set_err("batch size mismatch (fixed at create)");
    return -1;
  }
  if (mode != NN_SAMPLE_GUMBEL && mode != NN_SAMPLE_GREEDY) {
    set_err("bad sample mode");
    return -1;
  }
  sample_core(logits, n, mode, mix_sample_seed(nn->cfg.rng_seed, nn->sample_step),
              acts, logp, entropy);
  nn->sample_step++;
  return 0;
}

int nn_metal_update(NnMetal *nn, const uint8_t *planes, const float *scalars,
                    const int32_t *acts, const float *old_logp,
                    const float *advantages, const float *returns, int n,
                    NnUpdateStats *stats) {
  if (!nn || !planes || !scalars || !acts || !old_logp || !advantages ||
      !returns) {
    set_err("null pointer");
    return -1;
  }
  if (n != nn->batch_n) {
    set_err("batch size mismatch (fixed at create)");
    return -1;
  }
  if (validate_actions(acts, n) != 0)
    return -1;

  @autoreleasepool {
    upload_planes(nn, planes, n);
    memcpy(nn->scalarsBuf.contents, scalars,
           (size_t)n * NN_N_SCAL * sizeof(float));
    memcpy(nn->actsBuf.contents, acts,
           (size_t)n * NN_N_HEAD * sizeof(int32_t));
    memcpy(nn->oldLogpBuf.contents, old_logp, (size_t)n * sizeof(float));
    memcpy(nn->advBuf.contents, advantages, (size_t)n * sizeof(float));
    memcpy(nn->retBuf.contents, returns, (size_t)n * sizeof(float));

    nn->adam_t += 1;
    const float t = (float)nn->adam_t;
    float lr = nn->cfg.lr;
    float clip = nn->cfg.ppo_clip;
    float vcoef = nn->cfg.value_coef;
    float ecoef = nn->cfg.entropy_coef;
    float glim = nn->cfg.grad_limit;
    float b1pow = powf(kAdamBeta1, t);
    float b2pow = powf(kAdamBeta2, t);
    memcpy(nn->lrBuf.contents, &lr, sizeof(float));
    memcpy(nn->clipBuf.contents, &clip, sizeof(float));
    memcpy(nn->vcoefBuf.contents, &vcoef, sizeof(float));
    memcpy(nn->ecoefBuf.contents, &ecoef, sizeof(float));
    memcpy(nn->gradLimBuf.contents, &glim, sizeof(float));
    memcpy(nn->b1PowBuf.contents, &b1pow, sizeof(float));
    memcpy(nn->b2PowBuf.contents, &b2pow, sizeof(float));

    MPSGraphExecutableExecutionDescriptor *desc =
        [MPSGraphExecutableExecutionDescriptor new];
    desc.waitUntilCompleted = YES;
    [nn->updExec runWithMTLCommandQueue:nn->queue
                            inputsArray:nn->updInputs
                           resultsArray:nn->updResults
                    executionDescriptor:desc];

    /* Copy new weights and Adam state into resident buffers. */
    for (int t = 0; t < NN_T_COUNT; ++t) {
      size_t nbytes = NN_TENSOR_FLOATS[t] * sizeof(float);
      memcpy(nn->wBuf[t].contents, nn->wOutBuf[t].contents, nbytes);
      memcpy(nn->mBuf[t].contents, nn->mOutBuf[t].contents, nbytes);
      memcpy(nn->vBuf[t].contents, nn->vOutBuf[t].contents, nbytes);
    }
    sync_host_params_from_device(nn);

    if (stats) {
      stats->policy_loss = ((float *)nn->policyLossBuf.contents)[0];
      stats->value_loss = ((float *)nn->valueLossBuf.contents)[0];
      stats->entropy_mean = ((float *)nn->entMeanBuf.contents)[0];
      stats->total_loss = ((float *)nn->totalLossBuf.contents)[0];
      stats->grad_norm = ((float *)nn->gradNormBuf.contents)[0];
      stats->approx_kl = ((float *)nn->approxKlBuf.contents)[0];
      stats->clipfrac = ((float *)nn->clipfracBuf.contents)[0];
    }
  }
  return 0;
}

int nn_metal_save(const NnMetal *nn, const char *path) {
  if (!nn || !path) {
    set_err("null");
    return -1;
  }
  /* Ensure host mirror matches device (update already syncs; load does too). */
  NnMetal *mut = (NnMetal *)nn;
  sync_host_params_from_device(mut);
  const float *tensors[NN_T_COUNT];
  for (int t = 0; t < NN_T_COUNT; ++t)
    tensors[t] = nn->t[t];
  if (nn_fixture_save(path, tensors) != 0) {
    set_err("save failed");
    return -1;
  }
  return 0;
}

int nn_metal_load(NnMetal *nn, const char *path) {
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
  sync_device_params_from_host(nn);
  zero_adam(nn);
  return 0;
}
