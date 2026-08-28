/* Shape-agnostic cuDNN-graph conv net. NHWC acts, KRSC filters, TF32.
 * One spec per layer. FWD = conv + bias + ReLU. See cuda_fable_contract.h. */
#pragma once

#include "cuda_ws.h"

#include <cudnn.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NnConvLayerSpec {
  int c_in;   /* logical in channels; impl pads to quantum */
  int c_out;
  int k;
  int stride;
  int pad;
  int h_in;
  int w_in;
} NnConvLayerSpec;

typedef struct NnConvNet NnConvNet;

NnConvNet *nn_conv_net_create(cudnnHandle_t dnn, int device, int max_n,
                              const NnConvLayerSpec *layers, int n_layers,
                              NnWsArena *ws);

void nn_conv_net_destroy(NnConvNet *net);

/* Bucket n, build+time plans on miss. n in [1, max_n]. */
int nn_conv_net_prepare(NnConvNet *net, int n);

int nn_conv_net_n_layers(const NnConvNet *net);
int nn_conv_net_c_in_pad(const NnConvNet *net, int layer);
int nn_conv_net_c_out(const NnConvNet *net, int layer);
int nn_conv_net_h_out(const NnConvNet *net, int layer);
int nn_conv_net_w_out(const NnConvNet *net, int layer);

/* x,y NHWC fp32. w KRSC fp32. bias [c_out]. Layer 0 x uses padded C. */
int nn_conv_net_fwd(NnConvNet *net, int layer, const float *x, const float *w,
                    const float *bias, float *y);

/* dpre = dy after ReLU bwd. beta: 0 overwrite, 1 accumulate into dw. */
int nn_conv_net_wgrad(NnConvNet *net, int layer, const float *x,
                      const float *dpre, float *dw, float beta);

/* Skip for layer 0 (returns 0, no-op). */
int nn_conv_net_dgrad(NnConvNet *net, int layer, const float *w,
                      const float *dpre, float *dx);

#ifdef __cplusplus
}
#endif
