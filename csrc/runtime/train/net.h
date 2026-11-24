/* The native trainer's net (SPEC.md "The environment as configuration", item 6; decided 2026-10-29): mcsr-speedrun's
 * fastppo.py Small, RGB, no residual blocks, LSTM or picks, at one of its widths and casts, chosen at compile time
 * (lane/ntwidth): NETH_NET_WM the width multiplier (fastppo's wm: every convolution's channels and the MLP's width times
 * it; 1 is the brute recipe's net) and NETH_NET_STEM 0 for the 16x16 cast or 1 for the 64x64 cast with the stem (a learned
 * 2x downsampling first: Small(h=64, w=64, rgb=True, wm, stem=True)). A build is one variant (-DNETH_NET_WM=2 -DNETH_NET_STEM=1);
 * the default is the brute recipe's, 16x16 at wm 1. fastppo.py is the reference; mcsr-speedrun's scripts/ntref.py
 * writes the weights and fixtures this header describes (its MODEL fresh:wm=W[,stem=1] for every variant).
 *
 * The forward, per sample (S the cast's side, 16 or 64; I conv1's input side, S without the stem, S / 2 with it):
 *   x  [3][S][S]           the cast's RGB bytes [S][S][3] (row 0 at the top) / 255, channels first
 *   s  [16][S/2][S/2]      NETH_NET_STEM only: relu(conv 4x4, stride 2, pad 1), the stem
 *   x1 [32 wm][I/2][I/2]   relu(conv 4x4, stride 2, pad 1)
 *   x2 [64 wm][I/4][I/4]   relu(conv 4x4, stride 2, pad 1)
 *   x3 [64 wm][I/8][I/8]   relu(conv 3x3, stride 2, pad 1)
 *   z0 [NETH_NET_FLAT]           x3 flattened channel-major (c * (I/8)^2 + y * I/8 + x), then the 155 scalars and the run's features'
 *   z1 [256 wm]            relu(linear)
 *   z2 [256 wm]            relu(linear)
 *   cam [2][41]            linear: the pitch bin's and the yaw bin's logits
 *   keys [14]              linear: the keys' logits (sigmoid: the probability each is held)
 *   value                  linear
 * At wm 1 and 16x16 (the brute recipe): 32, 64, 64 channels at 8x8, 4x4, 2x2, z0 411, 256 wide, 0.27M parameters.
 * The action (the vector env's NETH_VEC_ACTION 16): the two camera bins, then the 14 keys as 0 or 1. fastppo's
 * sampler draws each camera bin by Gumbel-max (argmax of logit - log(-log u)) and each key as u < sigmoid(logit);
 * logp is the camera's two log-softmax terms plus each key's log-sigmoid of its sign.
 *
 * The weight blob (.ntw), little-endian. lane/audit-fixes: NTW2, what every writer makes (neth_checkpoint.c
 * neth_ntw_write): the 4 bytes "NTW2", int32 H, H bytes of header text, int32 tensor count (NETH_NET_TENSORS: 16, 18 with
 * the stem, 2 more with the hotbar or NETH_NET_RNN), then each tensor in NETH_NET_ORDER as int32 rank, int32 dims, fp32
 * data in PyTorch's layout (a conv [out][in][kh][kw], a linear [out][in]). The header is the lines "variant V" (the
 * build's word, NETH_NET_NAME), "features F" (NETH_NET_FEATURES' words the net reads, in their order, or none;
 * lane/audit-feat: the run's set, neth_net_fs) and "scalars N" (NETH_NET_NS), each ended by \n, then NUL bytes to a
 * multiple of 4. NTW1, the old form: the 8 bytes "NTW1\0\0\0\0" in place of "NTW2", H and the header; it never said which
 * inputs it read, so it loads only when it reads the base 155 scalars (ntinit --migrate states an old file's features
 * and writes it as NTW2). One parser reads both (neth_checkpoint.c neth_ntw_parse): fc1's columns for each feature the
 * file names go to the run's place for it, the run's others zero; a file naming a feature the run lacks is refused. */
#ifndef NETH_NET_NET_H
#define NETH_NET_NET_H

#include <stdint.h>

/* Opt-in BF16 storage and H2D transfer for rollout scalar buffers (S->sc, S->psc), default 0 (bit-exact fp32);
 * zero-copy S->psc = S->sc aliasing when --zero-sc is off, default 1 since lane/unify-2026-11-21 (the same digests
 * as 7c3274a7: it removes a copy, not a value; -DNETH_PSC_ALIAS=0 for the separate buffer). */
#ifndef NETH_SC_BF16
#define NETH_SC_BF16 0
#endif
#ifndef NETH_PSC_ALIAS
#define NETH_PSC_ALIAS 1
#endif
#if NETH_SC_BF16
typedef uint16_t neth_sc_t;
#else
typedef float neth_sc_t;
#endif

static inline uint16_t neth_f32_to_bf16_host(float x)
{
    uint32_t u;
    __builtin_memcpy(&u, &x, 4);
    if ((u & 0x7fffffffu) > 0x7f800000u) return (uint16_t)((u >> 16) | 0x0040u);
    return (uint16_t)((u + 0x7fffu + ((u >> 16) & 1u)) >> 16);
}
static inline float neth_bf16_to_f32_host(uint16_t h)
{
    uint32_t u = (uint32_t)h << 16;
    float x;
    __builtin_memcpy(&x, &u, 4);
    return x;
}

#ifndef NETH_NET_WM
#define NETH_NET_WM 1
#endif
#ifndef NETH_NET_STEM
#define NETH_NET_STEM 0
#endif
/* lane/nthotbar: 1, a fourth head, the hotbar: a 10-way categorical (0 keep the slot, k the number key k, slot k - 1)
 * after the keys in the action (NETH_NET_ACTION 17: the vector env's opts.hotbar 1 reads it as the player's number key that
 * tick), its logits linear over z2 (hot_w [10][D], hot_b [10], the blob's last two tensors), drawn by Gumbel-max as a
 * camera head is and its log-softmax term added to logp after the keys' ((cam + keys) + hot); the variant's name gets
 * " hotbar" and its build word .hb (actor.mk). 0 (the default): net.h as before, byte for byte */
#ifndef NETH_NET_HOTBAR
#define NETH_NET_HOTBAR 0
#endif

/* NETH_NET_RNN 1 (lane/ntgru): a minGRU of width D after fc2 (Feng et al. 2024, "Were RNNs All We Needed?"; fastppo's Small
 * gru=1), the heads reading z2 + h instead of z2:
 *   G  [2D]   linear(z2): rnn_w [2D][D] (rows 0..D-1 the gate's, D..2D-1 the candidate's), rnn_b [2D]
 *   h' [D]    h + sigmoid(G[0..D)) * (G[D..2D) - h), h the env's state before the step (zero where an episode starts)
 *   y  [D]    z2 + h', the heads' input
 * Both gates read the step's input alone, so the learner's T steps are one GEMM and an elementwise scan. After fc2,
 * not in its place: the feed-forward net stays a sub-function (the candidate's half zero at a fresh start: h stays 0
 * and the net is NETH_NET_RNN 0's), memory is added on top of what the brute recipe already trains, and the arms differ by
 * the layer alone. The blob's two tensors go last (NETH_NET_TENSORS + 2). 0: the feed-forward net, unchanged */
#ifndef NETH_NET_RNN
#define NETH_NET_RNN 0
#endif

/* the actor's and learner's 16-bit path (lane/nth16, prec.cuh): 0 bf16 with fp32 accumulation (the default), 1 fp16
 * with fp32, 2 fp16 with fp16, 3 fp16 with fp16 added into fp32 every NETH_NET_PROMOTE k-steps, 4 fp16 with fp16 but the
 * weights' gradients in fp32; a build's name gets .mN */
#ifndef NETH_NET_MMA
#define NETH_NET_MMA 0
#endif
/* the backward's loss scale on the fp16 paths (prec.cuh): at 1024 the stem's and conv1's input gradients sat in fp16's
 * subnormals (s1's stem gradient 2.3e-1 off fp32 at NETH_NET_MMA 2); 32768 where the weights' gradients sum in fp32 (1, 4),
 * 8192 where they sum in fp16 (2, 3: an entry past 8 overflows, k_adam then skips the update) */
#ifndef NETH_NET_LSCALE
#define NETH_NET_LSCALE (NETH_NET_MMA == 0 ? 1.0f : NETH_NET_MMA == 2 || NETH_NET_MMA == 3 ? 8192.0f : 32768.0f)
#endif

#define NETH_NET_SIDE (NETH_NET_STEM ? 64 : 16)     /* the cast's side */
#define NETH_NET_H NETH_NET_SIDE
#define NETH_NET_W NETH_NET_SIDE
#define NETH_NET_C 3
#define NETH_NET_C0 16                        /* the stem's channels (fastppo's Small: 16 at any wm) */
#define NETH_NET_HI (NETH_NET_STEM ? NETH_NET_SIDE / 2 : NETH_NET_SIDE)  /* conv1's input side */
#define NETH_NET_CI (NETH_NET_STEM ? NETH_NET_C0 : NETH_NET_C)  /* conv1's input channels */
#define NETH_NET_O1 (NETH_NET_HI / 2)               /* the convolutions' output sides */
#define NETH_NET_O2 (NETH_NET_HI / 4)
#define NETH_NET_O3 (NETH_NET_HI / 8)
#define NETH_NET_C1 (32 * NETH_NET_WM)
#define NETH_NET_C2 (64 * NETH_NET_WM)
#define NETH_NET_C3 (64 * NETH_NET_WM)
/* The scalar features after the base 155, each a block of the vector env's scalar row (vecenv.h's widths), in the row's
 * order. lane/audit-feat: a run's feature set, not a build's: one build reads any subset (the mask below, bit k the
 * table's k-th, vecenv.h's opts.late_scalars bit for bit), set once a process before any learner, actor or image is
 * made (neth_net_set_features: ntloop's --features, the run file's net.features, the --init file's header; ntinit's
 * --features). A blob without a feature loads with zero weights on its columns (neth_checkpoint.c's parser) and plays
 * as before.
 *   ls (lane/starts) the late game's scalars, the counts the 32 items leave out and the dimension
 *   hs (lane/ladder2 + lane/hill-rnn-hotbar) legal 9-slot HUD hotbar scalars: per slot 12 features directly visible on
 *      the 1.7.10 HUD bar (pickaxe/axe/shovel/sword tier, bow, snowball/arrow/building-block/food log-counts, water
 *      bucket, empty bucket, flint & steel)
 *   dk (lane/ladder4) the dig the screen shows: the crack stage under the crosshair and the client's hit under way, so a
 *      student can tell a dig from any other held attack
 *   or (lane/ladder5) the seek's view: for iron ore, diamond ore and logs in view, the flag, the nearest cell's yaw and
 *      pitch from the look and its distance, so a student without the seek (L3) can be told where the ore it sees is
 *   gv (lane/ladder10) what the open container screen shows: the screen's kind and each cursor cell's stack by class,
 *      so a student that clicks (actions.gui cursor) sees which slot holds what
 *   tk (lane/acrl) the task one-hot: task_item's, then task_item2's, the goal as a person is told it, for one policy
 *      over two tasks
 * The word is the checkpoint header's (NTW2's "features" line), ntloop's and ntinit's --features', and a build name's
 * (actor.mk: the words in V name V's default set; the code is the same for every set). */
#define NETH_NET_LATE_N 14                    /* vecenv.h NETH_VEC_LATE_N */
#define NETH_NET_HSEE_N 108                   /* vecenv.h NETH_VEC_HSEE_N */
#define NETH_NET_DIG_N 2                      /* vecenv.h NETH_VEC_DIG_N */
#define NETH_NET_ORE_N 12                     /* vecenv.h NETH_VEC_ORE_N */
#define NETH_NET_GV_N 708                     /* vecenv.h NETH_VEC_GV_N */
#define NETH_NET_TASK_N 2                     /* vecenv.h NETH_VEC_TASK_N */
#define NETH_NET_BASE_NS 155                  /* vecenv.h NETH_VEC_NSCALAR */
/* X(word, its mask bit, width) */
#define NETH_NET_FEATURES(X)                \
    X("ls", NETH_NET_FLS, NETH_NET_LATE_N)    \
    X("hs", NETH_NET_FHS, NETH_NET_HSEE_N)    \
    X("dk", NETH_NET_FDK, NETH_NET_DIG_N)     \
    X("or", NETH_NET_FOR, NETH_NET_ORE_N)     \
    X("gv", NETH_NET_FGV, NETH_NET_GV_N)      \
    X("tk", NETH_NET_FTK, NETH_NET_TASK_N)
enum { NETH_NET_FLS = 1, NETH_NET_FHS = 2, NETH_NET_FDK = 4, NETH_NET_FOR = 8, NETH_NET_FGV = 16, NETH_NET_FTK = 32,
       NETH_NET_FALL = 63, NETH_NET_NFEAT = 6 };
/* a mask's scalar row, and where feature bit f's block starts in it */
#define NETH_NET_NS_OF(m)                                                                                          \
    (NETH_NET_BASE_NS + ((m) & NETH_NET_FLS ? NETH_NET_LATE_N : 0) + ((m) & NETH_NET_FHS ? NETH_NET_HSEE_N : 0) +     \
     ((m) & NETH_NET_FDK ? NETH_NET_DIG_N : 0) + ((m) & NETH_NET_FOR ? NETH_NET_ORE_N : 0) +                           \
     ((m) & NETH_NET_FGV ? NETH_NET_GV_N : 0) + ((m) & NETH_NET_FTK ? NETH_NET_TASK_N : 0))
#define NETH_NET_OFF_OF(m, f) NETH_NET_NS_OF((m) & ((f) - 1))
#define NETH_NET_NS_MAX NETH_NET_NS_OF(NETH_NET_FALL)   /* 1001 */
/* the kernels' scalar count: the row padded to 64 (fc1's k chunks, actor.cu's F1K and fc.cuh's FC_K1 were already
 * padded so): 192, 320, 896 or 1024 for the 40 reachable rows, one instantiation each of the kernels that need fc1's K
 * at compile time (NETH_NET_NSP_CASES) */
#define NETH_NET_NSP(ns) (((ns) + 63) / 64 * 64)
#define NETH_NET_NSP_CASES(X) X(192) X(320) X(896) X(1024)
/* the build's default set (actor.mk: the feature words of V's name; none without them) */
#ifndef NETH_NET_FEAT0
#define NETH_NET_FEAT0 0
#endif
/* the process's set: its mask and row. One definition for every object of a binary (weak), fixed once a device copy
 * of it is made (neth_net_dsync; neth_net_set_features after that is refused) */
typedef struct neth_net_feat_s {
    int mask, ns, fixed;
} neth_net_feat_t;
__attribute__((weak)) neth_net_feat_t neth_net_fs = {NETH_NET_FEAT0, NETH_NET_NS_OF(NETH_NET_FEAT0), 0};
/* the set for this process: 0, or -1 when a device copy was made already (the shapes are allocated) */
static inline int neth_net_set_features(int mask)
{
    if (neth_net_fs.fixed && (mask & NETH_NET_FALL) != neth_net_fs.mask) return -1;
    neth_net_fs.mask = mask & NETH_NET_FALL;
    neth_net_fs.ns = NETH_NET_NS_OF(neth_net_fs.mask);
    return 0;
}
/* the scalar row: the process's on the host, each .cu file's __constant__ copy in its kernels (neth_net_dsync copies
 * it: every .cu file whose kernels read it calls it where its state is made) */
#ifdef __CUDACC__
static __constant__ __attribute__((unused)) int neth_net_dns;
static inline void neth_net_dsync(void)
{
    neth_net_fs.fixed = 1;
    cudaMemcpyToSymbol(neth_net_dns, &neth_net_fs.ns, sizeof(int));
}
#endif
#ifdef __CUDA_ARCH__
#define NETH_NET_NS neth_net_dns
#else
#define NETH_NET_NS (neth_net_fs.ns)
#endif
#define NETH_NET_FEATMASK (neth_net_fs.mask)
/* lane/audit-fixes: the build's word as given to make (actor.mk's nt_vflags passes it), the checkpoint header's variant */
#ifndef NETH_NET_NAME
#define NETH_NET_NAME "unnamed"
#endif
/* lane/audit-fixes: neth_ntw_parse's flags: NETH_NTW_FEWER, fewer tensors than the build's (the first ones; ntinit --from,
 * the teacher); NETH_NTW_NARROW, a tensor's first two dims at or under the build's (the teacher at a smaller wm) */
#define NETH_NTW_FEWER 1
#define NETH_NTW_NARROW 2
#define NETH_NET_FLAT0 (NETH_NET_C3 * NETH_NET_O3 * NETH_NET_O3)   /* fc1's columns before the scalars */
#define NETH_NET_FLAT (NETH_NET_FLAT0 + NETH_NET_NS)   /* 411 at wm 1, 16x16 and the base 155 */
#define NETH_NET_D (256 * NETH_NET_WM)
#define NETH_NET_CAM 2
#define NETH_NET_BINS 41
#define NETH_NET_KEYS 14
#define NETH_NET_HOT 10                       /* NETH_NET_HOTBAR's categories: keep, slots 1 to 9 */
#define NETH_NET_HOT_KEEP 4.0f                /* a fresh net's keep bias (ntinit, ntref.py's hot=1), the slots' 0: keep 86% */
#define NETH_NET_ACTION (NETH_NET_CAM + NETH_NET_KEYS + NETH_NET_HOTBAR)    /* 16, 17 with the hotbar */
#define NETH_NET_TENSORS (16 + 2 * NETH_NET_STEM + 2 * NETH_NET_HOTBAR + 2 * NETH_NET_RNN)
/* NETH_NET_HOTBAR with NETH_NET_RNN (lane/ladder8, s1.hb.dk.r1): the hotbar's two tensors, then the minGRU's, last (NETH_NET_ORDER);
 * no fastppo reference holds the two together (actor_check and learn_check fixtures are each one's alone) */

/* the variant's name, as the checks and benches print it */
#define NETH_NET_STR_(x) #x
#define NETH_NET_STR(x) NETH_NET_STR_(x)
#if NETH_NET_HOTBAR
#define NETH_NET_HBNAME " hotbar"
#else
#define NETH_NET_HBNAME ""
#endif
#if NETH_NET_RNN
#define NETH_NET_RNNNAME " rnn"
#else
#define NETH_NET_RNNNAME ""
#endif
#if NETH_NET_MMA == 0
#define NETH_NET_P16NAME "bf16"               /* the 16-bit path (--prec bf16) as the trainer prints it */
#define NETH_NET_VARIANT (NETH_NET_STEM ? "64x64 stem wm " NETH_NET_STR(NETH_NET_WM) NETH_NET_HBNAME NETH_NET_RNNNAME \
                            : "16x16 wm " NETH_NET_STR(NETH_NET_WM) NETH_NET_HBNAME NETH_NET_RNNNAME)
#else
#define NETH_NET_P16NAME "fp16 (NETH_NET_MMA " NETH_NET_STR(NETH_NET_MMA) ")"
#define NETH_NET_VARIANT (NETH_NET_STEM ? "64x64 stem wm " NETH_NET_STR(NETH_NET_WM) " mma " NETH_NET_STR(NETH_NET_MMA) NETH_NET_HBNAME NETH_NET_RNNNAME \
                            : "16x16 wm " NETH_NET_STR(NETH_NET_WM) " mma " NETH_NET_STR(NETH_NET_MMA) NETH_NET_HBNAME NETH_NET_RNNNAME)
#endif

/* the blob's tensors, in order: name, rank, dims */
#if NETH_NET_STEM
#define NETH_NET_ORDER_STEM(X)                                    \
    X(stem_w, 4, NETH_NET_C0, NETH_NET_C, 4, 4)                         \
    X(stem_b, 1, NETH_NET_C0, 0, 0, 0)
#else
#define NETH_NET_ORDER_STEM(X)
#endif
#if NETH_NET_HOTBAR
#define NETH_NET_ORDER_HOT(X)                                     \
    X(hot_w, 2, NETH_NET_HOT, NETH_NET_D, 0, 0)                         \
    X(hot_b, 1, NETH_NET_HOT, 0, 0, 0)
#else
#define NETH_NET_ORDER_HOT(X)
#endif
#if NETH_NET_RNN
#define NETH_NET_ORDER_RNN(X)                                     \
    X(rnn_w, 2, 2 * NETH_NET_D, NETH_NET_D, 0, 0)                       \
    X(rnn_b, 1, 2 * NETH_NET_D, 0, 0, 0)
#else
#define NETH_NET_ORDER_RNN(X)
#endif
#define NETH_NET_ORDER(X)                                         \
    NETH_NET_ORDER_STEM(X)                                        \
    X(conv1_w, 4, NETH_NET_C1, NETH_NET_CI, 4, 4)                       \
    X(conv1_b, 1, NETH_NET_C1, 0, 0, 0)                           \
    X(conv2_w, 4, NETH_NET_C2, NETH_NET_C1, 4, 4)                       \
    X(conv2_b, 1, NETH_NET_C2, 0, 0, 0)                           \
    X(conv3_w, 4, NETH_NET_C3, NETH_NET_C2, 3, 3)                       \
    X(conv3_b, 1, NETH_NET_C3, 0, 0, 0)                           \
    X(fc1_w, 2, NETH_NET_D, NETH_NET_FLAT, 0, 0)                        \
    X(fc1_b, 1, NETH_NET_D, 0, 0, 0)                              \
    X(fc2_w, 2, NETH_NET_D, NETH_NET_D, 0, 0)                           \
    X(fc2_b, 1, NETH_NET_D, 0, 0, 0)                              \
    X(cam_w, 2, NETH_NET_CAM * NETH_NET_BINS, NETH_NET_D, 0, 0)               \
    X(cam_b, 1, NETH_NET_CAM * NETH_NET_BINS, 0, 0, 0)                  \
    X(keys_w, 2, NETH_NET_KEYS, NETH_NET_D, 0, 0)                       \
    X(keys_b, 1, NETH_NET_KEYS, 0, 0, 0)                          \
    X(v_w, 2, 1, NETH_NET_D, 0, 0)                                \
    X(v_b, 1, 1, 0, 0, 0)                                   \
    NETH_NET_ORDER_HOT(X)                                         \
    NETH_NET_ORDER_RNN(X)

#endif
