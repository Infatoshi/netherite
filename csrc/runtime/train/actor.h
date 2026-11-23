/* The native trainer's actor (SPEC.md "The environment as configuration", item 6a; lane/ntactor): net.h's forward
 * and fastppo.py's gumbel sampler as CUDA kernels over a group's raw cast bytes. fastppo.py's ActorStep is the
 * reference (actor_check holds this to mcsr-speedrun's scripts/ntref.py fixture).
 *
 *   neth_actor *a = neth_actor_load("weights.ntw", &err);       the weights on the current device (one copy, read only)
 *   neth_actor_ws *w = neth_actor_ws_new(a, 512, seed);          a stream's scratch for up to 512 samples a call
 *   neth_actor_step(w, B, &io, stream);                       queues the forward and the sampler on stream
 *
 * A workspace serves one stream at a time (its scratch and its RNG counter are the call's); the weights serve any
 * number of workspaces. Every call is graph-capturable: nothing is read from the host after neth_actor_step returns,
 * and the device RNG's counter lives on the device (each call advances it by one), so a captured graph replays with
 * fresh noise. */
#ifndef NETH_NET_ACTOR_H
#define NETH_NET_ACTOR_H

#include <stdint.h>
#include <cuda_runtime_api.h>
#include "net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neth_actor neth_actor;
typedef struct neth_actor_ws neth_actor_ws;

enum {
    NETH_NET_ACTOR_FP32 = 0,  /* fp32 throughout: held to the reference's fp32 outputs (actor_check) */
    NETH_NET_ACTOR_BF16 = 1,  /* the linear layers in bf16 on tensor cores, fp32 accumulation (the PyTorch actor's bf16) */
};

/* one group's call: every pointer on the device; obs [B][16][16][3] (row 0 at the top), sc [B][155] */
typedef struct {
    const uint8_t *obs;
    const neth_sc_t *sc;
    int32_t *act;       /* [B][NETH_NET_ACTION]: the two camera bins, then the 14 keys as 0 or 1 (then the hotbar) */
    float *logp;        /* [B] */
    float *value;       /* [B] */
    float *cam;         /* optional (NULL): the camera logits [B][2][41] */
    float *keys;        /* optional (NULL): the keys' logits [B][14] */
    const float *ucam;  /* optional: the camera's uniforms [B][2][41] in (0, 1); NULL: the device RNG */
    const float *ukey;  /* optional: the keys' uniforms [B][14]; NULL: the device RNG */
    float *hot;         /* net.h's NETH_NET_HOTBAR, optional (NULL): the hotbar's logits [B][10] */
    const float *uhot;  /* NETH_NET_HOTBAR: the hotbar's uniforms [B][10], read where ucam is given */
    /* net.h NETH_NET_RNN 1 (lane/ntgru): the envs' minGRU state [B][NETH_NET_D] (fp32), read and written in place with the step's;
     * first [B] (NULL: none) nonzero where an episode starts at this step, the state taken as zero there. Unused
     * without the layer */
    float *h;
    const uint8_t *first;
} neth_actor_io;

/* NULL on error, with *err (if given) a static message */
neth_actor *neth_actor_load(const char *path, const char **err);
neth_actor *neth_actor_load_blob(const void *blob, size_t len, const char **err);
void neth_actor_free(neth_actor *a);
/* precision of the calls that follow (NETH_NET_ACTOR_FP32 by default) */
void neth_actor_set_mode(neth_actor *a, int mode);
/* new weights into a loaded actor, device to device: w the learner's parameters (learn.h's layout, NETH_LEARN_NPARAM fp32
 * on the device, at the build's net.h variant); one kernel queued on stream, no allocation, graph-capturable. The
 * image is then byte for byte the one neth_actor_load_blob makes of the same weights (lane/ntwidth, for lane ntwire's
 * trainer). Steps queued before it on other streams must be done first: it writes the weights in place. 0, or a
 * CUDA error code */
int neth_actor_reload(neth_actor *a, const float *w, cudaStream_t stream);
/* the bytes of two actors' images that differ (fp32, bf16 and fragment copies); -1 on error or other variants */
long neth_actor_diff(const neth_actor *a, const neth_actor *b);

neth_actor_ws *neth_actor_ws_new(neth_actor *a, int max_b, uint64_t seed);
void neth_actor_ws_free(neth_actor_ws *w);
/* the device RNG's seed and counter (queued on stream: a graph captured after it starts from there) */
int neth_actor_ws_seed(neth_actor_ws *w, uint64_t seed, uint64_t counter, cudaStream_t stream);

/* 0, or a CUDA error code (B over the workspace's max_b: cudaErrorInvalidValue) */
int neth_actor_step(neth_actor_ws *w, int B, const neth_actor_io *io, cudaStream_t stream);

#ifdef __cplusplus
}
#endif
#endif
