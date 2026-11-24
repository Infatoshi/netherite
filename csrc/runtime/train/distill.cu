/* Distillation to fewer macros (lane/ntbc): train/distill.h. The labels are made on the host from the vector env's
 * buffers at each recv, staged into the group's segment beside its rewards, and read by the learner's update: the
 * student's scalars (k_ssc), then per minibatch the forward (learn.cu's), the cross-entropy and its gradient into the
 * heads (k_bc, through neth_learn_hyper.loss), learn.cu's backward and Adam. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cuda_runtime.h>

#include "../vecenv.h"
#include "distill.h"
#include "loop.h"
#include "net.h"
#include "prec.cuh"

#define HS NETH_LEARN_HSTRIDE
#define LW 8    /* warps a block of k_bc: a warp a sample */
#define NETH_DISTILL_NOHOT -1.0e4f   /* neth_distill_widen's hotbar slots' bias for a teacher without the head: e^-1e4 is 0 in fp32 */

/* a step's label as the learner reads it (96 bytes, 100 with the hotbar) */
#define HOTA (NETH_NET_CAM + NETH_NET_KEYS)     /* NETH_NET_HOTBAR: the label's hotbar element (0 keep, k slot k - 1) */
/* an action's element j as the label holds it: the camera's bins and the hotbar's category as they are, a key 0 or 1 */
#define LBL(x, j) ((j) < NETH_NET_CAM || (j) >= HOTA ? (x) : (x) > 0)
struct neth_distill_rec {
    int32_t a[NETH_NET_ACTION];   /* the label: the camera's two bins, the 14 keys 0 or 1 (, the hotbar's category) */
    int32_t p[4];           /* the student's previous-action fields for this step: pitch bin, yaw bin, keys (bits),
                             * the last macro kind */
    float w;                /* the label's weight: 1 trained, 0 masked */
    int32_t valid;          /* a decision step: the value is trained */
    int32_t soft;           /* the label is the teacher's action: --soft's target its distribution */
    int32_t hold;           /* lane/ladder3: a removed macro's act holding the attack (--hold-weight's) */
};
#define KEY_ATTACK 7                /* the action's keys: forward back left right sprint jump sneak mine (attack) ... */
#define KEY_USE 8                   /* ... place (use) */
#define KEY_JUMP 5

struct neth_distill {
    int n, G, gs, T, level;
    float vf;
    struct neth_distill_rec *hst;    /* page-locked [n]: the step each env just made */
    struct neth_distill_rec **seg;   /* [2 G] device [T][gs] */
    int32_t (*prev)[4];     /* [n]: the student's previous-action fields for each env's next step */
    uint8_t *stage;         /* [n]: lane/ladder2, each env's stage this episode (neth_distill_census st) */
    uint8_t *pdig;          /* [n]: lane/ladder4 (the dk feature), the dig the state before this step showed: bit 0 a crack,
                             * bit 1 the hit (neth_distill_census dig) */
    neth_sc_t *ssc;         /* device [T gs][NETH_NET_NS]: the student's scalars */
    float *cnt;             /* device: the minibatch's weight and valid sums */
    double *books, *hbooks; /* device: the books (doubles: a float sum stops counting at 2^24 samples); page-locked */
    const struct neth_distill_rec *cur;      /* the segment neth_distill_learn trains (the loss's) */
    const float *cur_val;
    struct neth_distill_census cs;
    double acc[NETH_DISTILL_NBOOK];  /* neth_distill_books' last read, for neth_distill_row */
    /* --beta (neth_distill_dagger): each env's driver this episode (1 the student; page-locked), the teacher's actions
     * (device, and page-locked for neth_distill_took), the scratch its logp and the student's value go to */
    double beta;
    uint64_t rng;
    uint8_t *drive, *drive_d;
    int32_t *tact, *tact_h;
    float *tlogp, *sval;
    float *th;              /* (NETH_NET_RNN; lane/ladder8) device [n][NETH_NET_D]: the teacher's own minGRU state, so the
                             * student's (the loop's, which the segments store) is the student's alone */
    /* --soft (neth_distill_soft): the teacher's weights (the learner's layout, device) and its heads on the minibatch */
    float *tw, *thd;
    int thd_n;
    float hotw;             /* --hot-weight (neth_distill_hot_weight): the hotbar's gradient on a label that changes the slot */
    int zlo, zn;            /* lane/rawpix (neth_distill_student_zero): the student's scalars zlo .. zlo + zn - 1 read 0 */
    int penv;               /* lane/rawpix (neth_distill_student_prev_env): the previous-action fields the env's own */
    float holdw;            /* --hold-weight (neth_distill_hold_weight): the attack key's gradient on a removed macro's dig */
    int hscope;             /* --hold-scope: 0 all, 1 lock, 2 dig (distill.h) */
    int gui;                /* lane/ladder10 (neth_distill_gui): the teacher's key 12 cleared */
    float guiw;             /* lane/ladder10 (--gui-weight, neth_distill_gui_weight): a label's weight where the state showed an
                             * open container screen (the gv feature's kind flags) */
    float x2w;              /* lane/ladder17 (--x2-weight, neth_distill_x2_weight): a 2x2 recipe label's weight (half 9) */
    uint8_t *pscr;          /* [n]: lane/ladder10, the state before this step showed one (runs with gv; 0 otherwise);
                             * lane/ladder13: its kind, 1 the inventory, 2 a crafting table, 3 a furnace, 4 another */
    int rhold;              /* lane/ladder13 (--recipe-hold, neth_distill_recipe_hold) */
    int tlab;               /* lane/ladder15 (--table-label, neth_distill_table_label) */
    int slab;               /* lane/ladder16 (--seek-label, neth_distill_seek_label) */
    int tkj;                /* lane/ladder15: NETH_VEC_TABLEKEY's key in the action's keys (neth_distill_table_key) */
    uint8_t *popen;         /* [n]: lane/ladder14, who opened the screen the state shows (neth_distill_census win's opener) */
    int wlab;               /* lane/ladder14 (--window-label, neth_distill_window_label) */
    int lab, sonly;         /* lane/ladder6: --labeller on (the student's episodes at its level), --student-labels
                             * teacher 0, shadow 1, none 2 */
    double lshare;          /* lane/ladder6: --level-share, of the student's episodes the share at its level (drive 2) */
};

#define CK(x)                                                                                    \
    do {                                                                                         \
        cudaError_t e_ = (x);                                                                    \
        if (e_ != cudaSuccess) {                                                                 \
            printf("FAIL %s:%d %s: %s\n", __FILE__, __LINE__, #x, cudaGetErrorString(e_));       \
            exit(1);                                                                             \
        }                                                                                        \
    } while (0)

static const char *const CLASS_NAMES[NETH_DISTILL_NCLASS] = {"none", "surface", "loot", "mine", "craft", "fight",
                                                    "seek", "strip", "food", "screen", "other"};
static const char *const STAGE_NAMES[NETH_DISTILL_NSTAGE] = {"start", "wooden", "stone", "iron"};
/* lane/ladder2: the items' places in the scalars (vecenv.c TEACH_ITEMS: log1p of each count, first in the row) */
enum { SCI_WOODEN_PICK = 5, SCI_STONE_PICK = 10, SCI_IRON_INGOT = 16 };

/* the stage the scalars show (neth_distill_census st) */
static int stage_of(const float *sc) {
    return sc[SCI_IRON_INGOT] > 0.0f ? 3 : sc[SCI_STONE_PICK] > 0.0f ? 2 : sc[SCI_WOODEN_PICK] > 0.0f ? 1 : 0;
}

static int class_of(int mbit) {
    switch (mbit) {
    case 0: return 0;
    case NETH_VEC_SURFACE: return 1;
    case NETH_VEC_LOOT: return 2;
    case NETH_VEC_MINE: return 3;
    case NETH_VEC_CRAFT: return 4;
    case NETH_VEC_FIGHT: return 5;
    case NETH_VEC_SEEK: return 6;
    case NETH_VEC_STRIP: return 7;
    case NETH_VEC_FOOD: return 8;
    case NETH_VEC_LBL_SCREEN: return 9;
    default: return 10;
    }
}

extern "C" struct neth_distill *neth_distill_new(int n, int G, int gs, int T, int level, float vf, char *err, size_t en) {
    if (n != G * gs || T <= 0) {
        snprintf(err, en, "neth_distill_new: %d envs are not %d groups of %d", n, G, gs);
        return NULL;
    }
    neth_net_dsync();   /* lane/audit-feat: this file's kernels read the run's scalar row */
    struct neth_distill *D = (struct neth_distill *)calloc(1, sizeof *D);
    D->n = n, D->G = G, D->gs = gs, D->T = T, D->level = level, D->vf = vf, D->hotw = 1.0f, D->holdw = 1.0f;
    /* lane/ladder16 (NETH_VEC_SEEKKEY): the teacher's own key 13 cleared (k_mix), so key 13 is said only at a seek's
     * start; the teacher's env reads no key 13 (actions.table_key inventory), so its trajectory is unchanged */
    if ((level & NETH_VEC_SEEKKEY) && !(level & NETH_VEC_SEEK)) D->gui |= 2;
    CK(cudaMallocHost((void **)&D->hst, (size_t)n * sizeof *D->hst));
    memset(D->hst, 0, (size_t)n * sizeof *D->hst);
    D->seg = (struct neth_distill_rec **)calloc((size_t)2 * G, sizeof *D->seg);
    for (int i = 0; i < 2 * G; ++i) {
        CK(cudaMalloc((void **)&D->seg[i], (size_t)T * gs * sizeof(struct neth_distill_rec)));
        CK(cudaMemset(D->seg[i], 0, (size_t)T * gs * sizeof(struct neth_distill_rec)));
    }
    D->prev = (int32_t(*)[4])calloc((size_t)n, sizeof *D->prev);
    for (int i = 0; i < n; ++i) D->prev[i][0] = D->prev[i][1] = NETH_NET_BINS / 2;
    D->stage = (uint8_t *)calloc((size_t)n, 1);
    D->pdig = (uint8_t *)calloc((size_t)n, 1);
    D->pscr = (uint8_t *)calloc((size_t)n, 1);
    D->popen = (uint8_t *)calloc((size_t)n, 1);
    D->guiw = 1.0f;
    D->x2w = 1.0f;
    D->tkj = NETH_VEC_KEY_INV;  /* lane/ladder15: the table key (neth_distill_table_key) */
    CK(cudaMalloc((void **)&D->ssc, (size_t)T * gs * NETH_NET_NS * sizeof(neth_sc_t)));
    CK(cudaMalloc((void **)&D->cnt, 2 * sizeof(float)));
    CK(cudaMalloc((void **)&D->books, NETH_DISTILL_NBOOK * sizeof(double)));
    CK(cudaMemset(D->books, 0, NETH_DISTILL_NBOOK * sizeof(double)));
    CK(cudaMallocHost((void **)&D->hbooks, NETH_DISTILL_NBOOK * sizeof(double)));
    return D;
}

extern "C" void neth_distill_free(struct neth_distill *D) {
    if (!D) return;
    cudaFreeHost(D->hst);
    for (int i = 0; i < 2 * D->G; ++i) cudaFree(D->seg[i]);
    free(D->seg);
    free(D->prev);
    free(D->stage);
    free(D->pdig);
    free(D->pscr);
    free(D->popen);
    cudaFree(D->ssc), cudaFree(D->cnt), cudaFree(D->books), cudaFreeHost(D->hbooks);
    if (D->drive) {
        cudaFreeHost(D->drive), cudaFree(D->drive_d), cudaFree(D->tact), cudaFreeHost(D->tact_h);
        cudaFree(D->tlogp), cudaFree(D->sval), cudaFree(D->th);
    }
    cudaFree(D->tw), cudaFree(D->thd);
    free(D);
}

static uint64_t xs(uint64_t *s) {
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return *s = x;
}
static int draw_student(struct neth_distill *D) { return (double)(xs(&D->rng) >> 11) * 0x1p-53 < D->beta; }
/* lane/ladder6: a student's episode at its level (2, --level-share's share) or at the teacher's macros (1) */
static int draw_level(struct neth_distill *D) { return (double)(xs(&D->rng) >> 11) * 0x1p-53 < D->lshare ? 2 : 1; }
static int draw_driver(struct neth_distill *D) {
    int s = draw_student(D);
    return s && D->lab ? draw_level(D) : s;
}

extern "C" void neth_distill_dagger(struct neth_distill *D, double beta, uint64_t seed) {
    int n = D->n;
    D->beta = beta;
    D->rng = (seed * 0x9e3779b97f4a7c15ull) ^ 0xd1b54a32d192ed03ull;
    CK(cudaMallocHost((void **)&D->drive, (size_t)n));
    CK(cudaMalloc((void **)&D->drive_d, (size_t)n));
    CK(cudaMalloc((void **)&D->tact, (size_t)n * NETH_NET_ACTION * sizeof(int32_t)));
    CK(cudaMallocHost((void **)&D->tact_h, (size_t)n * NETH_NET_ACTION * sizeof(int32_t)));
    CK(cudaMalloc((void **)&D->tlogp, (size_t)n * sizeof(float)));
    CK(cudaMalloc((void **)&D->sval, (size_t)n * sizeof(float)));
    if (NETH_NET_RNN) {
        CK(cudaMalloc((void **)&D->th, (size_t)n * NETH_NET_D * sizeof(float)));
        CK(cudaMemset(D->th, 0, (size_t)n * NETH_NET_D * sizeof(float)));
    }
    for (int i = 0; i < n; ++i) D->drive[i] = (uint8_t)draw_student(D);
}

extern "C" int neth_distill_beta_on(const struct neth_distill *D) { return D->drive != NULL; }

extern "C" void neth_distill_labeller(struct neth_distill *D, int sonly, double lshare) {
    D->lab = 1;
    D->sonly = sonly;
    D->lshare = lshare;
    /* the first episodes' student drivers (neth_distill_dagger drew them) each at the level or not */
    for (int i = 0; D->drive && i < D->n; ++i)
        if (D->drive[i]) D->drive[i] = (uint8_t)draw_level(D);
}

extern "C" const uint8_t *neth_distill_drive(const struct neth_distill *D) { return D->drive; }

extern "C" void neth_distill_soft(struct neth_distill *D, const float *flat) {
    CK(cudaMalloc((void **)&D->tw, NETH_LEARN_NPARAM * sizeof(float)));
    CK(cudaMemcpy(D->tw, flat, NETH_LEARN_NPARAM * sizeof(float), cudaMemcpyHostToDevice));
}

/* each env's action: the student's (already in act) where it drives, else the teacher's; clr (lane/ladder10, neth_distill_gui):
 * the teacher's key 12 cleared first (its label too) */
__global__ void k_mix(int gs, const uint8_t *drive, int32_t *tact, int32_t *act, int clr) {
    int q = blockIdx.x * blockDim.x + threadIdx.x;
    if (q >= gs * NETH_NET_ACTION) return;
    if ((clr & 1) && q % NETH_NET_ACTION == NETH_NET_CAM + NETH_VEC_KEY_INV) tact[q] = 0;
    if ((clr & 2) && q % NETH_NET_ACTION == NETH_NET_CAM + NETH_VEC_KEY_PAUSED) tact[q] = 0;     /* lane/ladder15 */
    if (!drive[q / NETH_NET_ACTION]) act[q] = tact[q];
}

extern "C" void neth_distill_gui(struct neth_distill *D) { D->gui |= 1; }
extern "C" void neth_distill_table_key(struct neth_distill *D, int k) {     /* lane/ladder15 */
    D->tkj = k;
    if (k == NETH_VEC_KEY_PAUSED) D->gui |= 2;
}
extern "C" void neth_distill_gui_weight(struct neth_distill *D, float w) { D->guiw = w; }
extern "C" void neth_distill_x2_weight(struct neth_distill *D, float w) { D->x2w = w; }
extern "C" void neth_distill_recipe_hold(struct neth_distill *D, int on) { D->rhold = on; }
extern "C" void neth_distill_table_label(struct neth_distill *D, int on) { D->tlab = on; }
extern "C" void neth_distill_seek_label(struct neth_distill *D, int on) { D->slab = on; }
extern "C" void neth_distill_window_label(struct neth_distill *D, int on) { D->wlab = on; }

extern "C" int neth_distill_launch(struct neth_distill *D, void *teacher, void *student, const void *iop, void *stream) {
    cudaStream_t s = (cudaStream_t)stream;
    const struct neth_policy *T = (const struct neth_policy *)teacher, *S = (const struct neth_policy *)student;
    const struct neth_io *io = (const struct neth_io *)iop;
    struct neth_io it = *io, is = *io;
    size_t a0 = (size_t)io->base * NETH_NET_ACTION;
    it.actions = D->tact + a0;
    it.logp = D->tlogp + io->base;
    is.value = D->sval + io->base;
    if (D->th) it.h = D->th + (size_t)io->base * NETH_NET_D;  /* NETH_NET_RNN: the student's state is io's alone */
    if (T->launch(T->ctx, &it, s) || S->launch(S->ctx, &is, s)) return -1;
    CK(cudaMemcpyAsync(D->drive_d + io->base, D->drive + io->base, (size_t)io->n, cudaMemcpyHostToDevice, s));
    k_mix<<<(io->n * NETH_NET_ACTION + 255) / 256, 256, 0, s>>>(io->n, D->drive_d + io->base, D->tact + a0, io->actions,
                                                         D->gui);
    CK(cudaMemcpyAsync(D->tact_h + a0, D->tact + a0, (size_t)io->n * NETH_NET_ACTION * sizeof(int32_t),
                       cudaMemcpyDeviceToHost, s));
    return cudaGetLastError() == cudaSuccess ? 0 : -1;
}

/* the scalars' previous-action fields (vecenv.c scalars: after the 32 items, 9 hotbar and 12 others) */
#define SC_MAC (NETH_VEC_ITEMS + 9 + 12)
#define SC_CAM (SC_MAC + NETH_VEC_MACROS)
#define SC_KEY (SC_CAM + 2 * NETH_VEC_CAM)
static_assert(SC_KEY + NETH_VEC_KEYS == NETH_NET_BASE_NS, "the scalars' layout: the base 155, then the run's features");

/* the env's own previous-action fields in sc against p: equal */
static int prev_equal(const float *sc, const int32_t *p) {
    for (int k = 0; k < NETH_VEC_MACROS; ++k)
        if (sc[SC_MAC + k] != (k == p[3] ? 1.0f : 0.0f)) return 0;
    for (int k = 0; k < 2 * NETH_VEC_CAM; ++k)
        if (sc[SC_CAM + k] != (k == p[0] || k == NETH_VEC_CAM + p[1] ? 1.0f : 0.0f)) return 0;
    for (int k = 0; k < NETH_VEC_KEYS; ++k)
        if (sc[SC_KEY + k] != (float)((p[2] >> k) & 1)) return 0;
    return 1;
}

extern "C" void neth_distill_took(struct neth_distill *D, const struct neth_vec_bufs *b, int lo, int gs) {
    struct neth_distill_census *C = &D->cs;
    for (int i = lo; i < lo + gs; ++i) {
        const struct neth_vec_label *l = &b->label[i];
        /* the action this step was sent, and the teacher's at its state (the same one unless the student drove) */
        const int32_t *sent = b->actions + (size_t)i * NETH_NET_ACTION;
        const int32_t *ta = D->drive ? D->tact_h + (size_t)i * NETH_NET_ACTION : sent;
        int sdrv = D->drive && D->drive[i];
        struct neth_distill_rec *r = &D->hst[i];
        memcpy(r->p, D->prev[i], sizeof r->p);
        int32_t em[NETH_NET_ACTION];   /* what the student emits here: its label, or the teacher's action where masked */
        int mac = 0, own = 0;
        r->soft = 0;
        r->hold = 0;
        if (!(l->flags & NETH_VEC_LBL_VALID)) {
            ++C->invalid;
            memset(r->a, 0, sizeof r->a);
            r->a[0] = r->a[1] = NETH_NET_BINS / 2;
            r->w = 0.0f;
            r->valid = 0;
            memcpy(em, r->a, sizeof em);
        } else {
            int cls = class_of(l->mbit);
            /* lane/ladder10: the screen macro is the level's unless the level turns it off (NETH_VEC_NOSCREEN) */
            /* lane/ladder12: a craft label is the level's unless the level turns its half off (NETH_VEC_NOTABLE,
             * NETH_VEC_NORECIPE) */
            /* the recipe's first step (half 3) is the recipe's but under NETH_VEC_RECIPEKEY, whose jump starts it: there
             * the first step is the student's (labelled the jump key) and the rest of the recipe the level's macro */
            int rkey = (D->level & NETH_VEC_RECIPEKEY) != 0;
            /* lane/ladder15: the table half's first step (half 4) likewise under NETH_VEC_TABLEKEY (with
             * NETH_VEC_NOTABLE), whose key 12 starts it: the student's (labelled key 12), the rest the level's macro */
            int tkey = (D->level & NETH_VEC_TABLEKEY) && (D->level & NETH_VEC_NOTABLE);
            int hf = l->half == 3 && !rkey ? 2 : l->half == 4 && !tkey ? 1 : l->half;
            int halfoff = l->mbit == NETH_VEC_CRAFT && ((hf == 1 && (D->level & NETH_VEC_NOTABLE) && !tkey) ||
                                                        (hf == 2 && (D->level & NETH_VEC_NORECIPE) && !rkey) ||
                                                        hf == 3 || hf == 4 ||
                                                        (hf == 9 && (D->level & NETH_VEC_NORECIPE)));
            /* lane/ladder17 (half 9, actions.recipe_2x2 clicks): a 2x2 recipe's step is never the level's macro under
             * NETH_VEC_NORECIPE: the macro's act (its click, its close as key 12) or the 2x2 labeller's (shadow) */
            /* lane/ladder16: under NETH_VEC_SEEKKEY (with the seek off) the seek's first step (half 6) is the student's
             * (labelled key 13), its later steps the level's macro */
            int skey = (D->level & NETH_VEC_SEEKKEY) && !(D->level & NETH_VEC_SEEK);
            /* lane/ladder18: and a step in the seek's half the policy owns (half 8: actions.seek_walk or seek_dig
             * policy) the student's, labelled with the seek's act */
            int seekin = skey && l->mbit == NETH_VEC_SEEK && l->half != 6 && l->half != 8;
            int inlevel = l->mbit == 0 || ((l->mbit & D->level) && !halfoff) ||
                          (l->mbit == NETH_VEC_LBL_SCREEN && !(D->level & NETH_VEC_NOSCREEN)) || seekin;
            r->valid = 1;
            /* the policy's action is what the student emits here (lane/ladder6: also under a labeller's label, the
             * env having stepped the student's own action) */
            int shd = (l->flags & NETH_VEC_LBL_SHADOW) != 0;
            int pol = l->mbit == 0 || inlevel || shd;
            int sset = 0;   /* lane/ladder16: a seek-key label (--seek-label): --student-labels counts it as the labeller's */
            if (l->mbit == 0 || (inlevel && (l->flags & NETH_VEC_LBL_CHOSEN))) {
                /* the teacher's action (at the student's level the policy acts here, or starts the macro) */
                for (int j = 0; j < NETH_NET_ACTION; ++j) r->a[j] = LBL(ta[j], j);
                r->w = 1.0f;
                r->soft = 1;
                own = l->mbit == 0;
                if (D->tlab && tkey && l->mbit == 0 && l->half == 5) {
                    /* lane/ladder15 (--table-label on): no screen and a crafting plan named: the table key, key 12
                     * alone, no look (where the teacher's craft macro starts on its next fifth step) */
                    r->a[0] = r->a[1] = NETH_VEC_CAM / 2;
                    for (int j = 0; j < NETH_NET_KEYS; ++j) r->a[NETH_NET_CAM + j] = j == D->tkj;
#if NETH_NET_HOTBAR
                    r->a[HOTA] = 0;
#endif
                    r->soft = 0;
                    C->tlabs += 1;
                }
                if (D->slab && skey && l->mbit == 0 && l->half == 7 && !shd) {
                    /* lane/ladder16 (--seek-label on): the seek's start holding at the level: the seek key, key 13
                     * alone, no look (where the teacher's seek starts on its next fifth step) */
                    r->a[0] = r->a[1] = NETH_VEC_CAM / 2;
                    for (int j = 0; j < NETH_NET_KEYS; ++j) r->a[NETH_NET_CAM + j] = j == NETH_VEC_KEY_PAUSED;
#if NETH_NET_HOTBAR
                    r->a[HOTA] = 0;
#endif
                    r->w = 1.0f;
                    r->soft = 0;
                    sset = 1;
                    C->seekkey[2] += 1;
                }
            } else if (inlevel) {
                /* the student's own macro drives this step: nothing to learn */
                for (int j = 0; j < NETH_NET_ACTION; ++j) r->a[j] = LBL(ta[j], j);
                r->w = 0.0f;
                if (D->rhold && rkey && l->mbit == NETH_VEC_CRAFT && l->half == 2 && D->pscr[i]) {
                    /* lane/ladder13 (--recipe-hold): the recipe going on under its window, the jump held; close
                     * (rhold 2): the macro's close (said as the inventory key) labelled as that key instead */
                    int cl = D->rhold == 2 && ((l->keys >> NETH_VEC_KEY_INV) & 1);
                    r->a[0] = r->a[1] = NETH_VEC_CAM / 2;
                    for (int j = 0; j < NETH_NET_KEYS; ++j) r->a[NETH_NET_CAM + j] = cl ? j == NETH_VEC_KEY_INV : j == KEY_JUMP;
#if NETH_NET_HOTBAR
                    r->a[HOTA] = 0;
#endif
                    r->w = 1.0f;
                }
            } else {
                /* a macro the student does not have: its act */
                r->a[0] = l->cam[0];
                r->a[1] = l->cam[1];
                for (int j = 0; j < NETH_NET_KEYS; ++j) r->a[NETH_NET_CAM + j] = (l->keys >> j) & 1;
#if NETH_NET_HOTBAR
                r->a[HOTA] = l->hotbar;     /* lane/nthotbar: the macro's slot change, the student's number key */
#endif
                r->w = (l->flags & NETH_VEC_LBL_GUI) ? 0.0f : 1.0f;
                /* lane/ladder3: the dig's held attack, in --hold-scope */
                /* lane/ladder5: scope 3 (digs) the seek's continuing dig as well (L3 removes the seek) */
                r->hold = r->a[NETH_NET_CAM + KEY_ATTACK] != 0 &&
                          (D->hscope == 0 || l->mbit == NETH_VEC_MINE || (D->hscope == 3 && l->mbit == NETH_VEC_SEEK)) &&
                          (D->hscope < 2 || ((r->p[2] >> KEY_ATTACK) & 1));
                if (l->mbit == NETH_VEC_CRAFT && hf == 3) {
                    /* lane/ladder12 (NETH_VEC_RECIPEKEY): the recipe started: the jump key alone, no look */
                    r->a[0] = r->a[1] = NETH_VEC_CAM / 2;
                    for (int j = 0; j < NETH_NET_KEYS; ++j) r->a[NETH_NET_CAM + j] = j == KEY_JUMP;
#if NETH_NET_HOTBAR
                    r->a[HOTA] = 0;
#endif
                    r->w = 1.0f;
                    r->hold = 0;
                }
                if (l->mbit == NETH_VEC_CRAFT && hf == 4) {
                    /* lane/ladder15 (NETH_VEC_TABLEKEY): the table half started: the table key alone (key 12, or 13
                     * with actions.table_key paused), no look, no slot */
                    r->a[0] = r->a[1] = NETH_VEC_CAM / 2;
                    for (int j = 0; j < NETH_NET_KEYS; ++j) r->a[NETH_NET_CAM + j] = j == D->tkj;
#if NETH_NET_HOTBAR
                    r->a[HOTA] = 0;
#endif
                    r->w = 1.0f;
                    r->hold = 0;
                }
                if (skey && l->mbit == NETH_VEC_SEEK && l->half == 6) {
                    /* lane/ladder16 (NETH_VEC_SEEKKEY): the seek started: key 13 alone, no look, no slot */
                    r->a[0] = r->a[1] = NETH_VEC_CAM / 2;
                    for (int j = 0; j < NETH_NET_KEYS; ++j) r->a[NETH_NET_CAM + j] = j == NETH_VEC_KEY_PAUSED;
#if NETH_NET_HOTBAR
                    r->a[HOTA] = 0;
#endif
                    r->w = 1.0f;
                    r->hold = 0;
                    C->seekkey[0] += 1;
                }
            }
            if (skey && l->mbit == NETH_VEC_SEEK && l->half != 6 && l->half != 8) C->seekkey[1] += 1;
            int wown = D->wlab && D->pscr[i] >= 1 && D->pscr[i] <= 3 && D->popen[i] == 2 &&
                       (D->level & NETH_VEC_NOTABLE) && rkey;
            int wscr = wown && l->mbit == NETH_VEC_LBL_SCREEN;
            /* level (2): also the policy's own step under it (at its level, driver 2: the recipe handed back, or no
             * plan fitting; at the teacher's macros too: W1-s1 had 128,633 such steps under its own inventory, labelled
             * with the teacher's keys, jump 93%, never the close) */
            int wlev = wown && D->wlab == 2 && D->lab && l->mbit == 0;
            int wset = 0;   /* a window label: --student-labels counts it as the labeller's */
            if (wscr || wlev) {
                /* lane/ladder14 (--window-label): the policy's own window: the jump where the recipe would start,
                 * no key where the close would be the opening key held; at the level, the close elsewhere */
                int fit = (l->flags & NETH_VEC_LBL_FITS) != 0, held = (r->p[2] >> NETH_VEC_KEY_INV) & 1;
                if (fit || held || wlev) {
                    wset = 1;
                    r->a[0] = r->a[1] = NETH_VEC_CAM / 2;
                    for (int j = 0; j < NETH_NET_KEYS; ++j)
                        r->a[NETH_NET_CAM + j] = fit ? j == KEY_JUMP : !held && j == NETH_VEC_KEY_INV;
#if NETH_NET_HOTBAR
                    r->a[HOTA] = 0;
#endif
                    r->w = 1.0f;
                    r->hold = 0;
                    r->soft = 0;
                    C->wrel[fit ? 0 : held ? 1 : 2] += 1;
                }
            }
            if (D->lab && D->drive[i] == 2) {
                /* lane/ladder6: an episode at the student's level, the labeller beside it */
                C->lsteps += 1;
                if (shd) C->shadow[D->stage[i]] += 1;
                /* sonly 1 (shadow): the teacher's labels there masked; 2 (none, the control): every label there */
                if ((shd || wset || sset ? D->sonly == 2 : D->sonly != 0) && r->w > 0.0f) {
                    r->w = 0.0f;
                    r->soft = 0;
                    C->lmasked += 1;
                }
            }
            if (D->guiw != 1.0f && D->pscr[i] && r->w > 0.0f) r->w *= D->guiw;    /* lane/ladder10 */
            if (skey && l->mbit == NETH_VEC_SEEK && l->half == 8) {     /* lane/ladder18 */
                C->seekwalk[shd ? 1 : 0] += 1;
                C->seekwalk[2] += (l->flags & NETH_VEC_LBL_FAR) != 0;
                C->seekwalk[3] += r->w > 0.0f;
            }
            if (l->mbit == NETH_VEC_CRAFT && l->half == 9 && (D->level & NETH_VEC_NORECIPE)) {   /* lane/ladder17 */
                C->x2[shd ? 2 : 0] += 1;
                C->x2[shd ? 3 : 1] += r->w > 0.0f;
                if (D->x2w != 1.0f && r->w > 0.0f) r->w *= D->x2w;
            }
#if NETH_NET_HOTBAR
            C->hotlab[cls] += r->w > 0.0f && r->a[HOTA] != 0;
#endif
            /* the previous action the next step shows: the policy's as sent (the student's own where it drove),
             * else the macro's act */
            if (pol)
                for (int j = 0; j < NETH_NET_ACTION; ++j) em[j] = LBL(sent[j], j);
            else
                memcpy(em, r->a, sizeof em);
            if (sdrv) {
                C->student += 1;
                if (l->mbit == 0) C->agree += !memcmp(em, r->a, sizeof em);
            }
            mac = inlevel ? l->kind : 0;
            C->steps[cls] += 1;
            C->trained[cls] += r->w > 0.0f;
            if (l->mbit == NETH_VEC_CRAFT) {    /* lane/ladder12: the craft macro's steps by half */
                int h = l->half <= 4 ? l->half : 0;
                C->half[h][0] += 1;
                C->half[h][1] += r->w > 0.0f;
                C->half[h][2] += D->pscr[i] != 0;
                C->half[h][3] += (l->flags & NETH_VEC_LBL_HOTBAR) != 0;
                C->half[h][4] += (l->keys >> KEY_USE) & 1;      /* the macro's act (r->a is the teacher's in level) */
                C->half[h][5] += (l->keys >> NETH_VEC_KEY_INV) & 1;
                if (h == 3 || h == 2) {     /* lane/ladder13: by the screen the state showed */
                    int64_t *q = C->rscr[D->pscr[i] < 5 ? D->pscr[i] : 4];
                    q[h == 3 ? 0 : 1] += 1;
                    q[2] += h == 2 && r->w > 0.0f;
                }
            }
            if (D->pscr[i] >= 1 && D->pscr[i] <= 3) {
                /* lane/ladder14: under a window, by its opener and the step's label */
                int lc = l->mbit == 0 ? 0 : l->mbit == NETH_VEC_CRAFT ? (l->half == 3 ? 1 : l->half == 2 ? 2 : 3)
                       : l->mbit == NETH_VEC_LBL_SCREEN ? 4 : 5;
                int64_t *q = C->win[D->pscr[i] - 1][D->popen[i] < 4 ? D->popen[i] : 0][lc];
                q[0] += 1;
                q[1] += r->w > 0.0f;
                q[2] += r->a[NETH_NET_CAM + KEY_JUMP] != 0;
                q[3] += r->a[NETH_NET_CAM + NETH_VEC_KEY_INV] != 0;
                q[4] += r->a[NETH_NET_CAM + KEY_ATTACK] != 0 || r->a[NETH_NET_CAM + KEY_USE] != 0;
                q[5] += (l->flags & NETH_VEC_LBL_FITS) != 0;
            }
            C->gui[cls] += (l->flags & NETH_VEC_LBL_GUI) != 0;
            C->hotbar[cls] += (l->flags & NETH_VEC_LBL_HOTBAR) != 0;
            C->far[cls] += (l->flags & NETH_VEC_LBL_FAR) != 0;
            C->press[cls] += (l->flags & NETH_VEC_LBL_PRESS) != 0;
            /* a bin's own change, through the act's float: within a thousandth of a degree */
            C->exact[cls] += fabsf(l->res[0]) < 1e-3f && fabsf(l->res[1]) < 1e-3f &&
                             !(l->flags & (NETH_VEC_LBL_GUI | NETH_VEC_LBL_HOTBAR | NETH_VEC_LBL_PRESS));
            C->res_abs[cls] += fabs((double)l->res[0]) + fabs((double)l->res[1]);
            /* lane/ladder2: by stage (the furthest the episode has held, before this step's result) */
            int64_t *st = C->st[D->stage[i]][cls];
            st[0] += 1;
            st[1] += r->w > 0.0f;
            st[2] += (l->flags & NETH_VEC_LBL_GUI) != 0;
            st[3] += (l->flags & NETH_VEC_LBL_HOTBAR) != 0;
            st[4] += (l->flags & NETH_VEC_LBL_FAR) != 0;
#if NETH_NET_HOTBAR
            st[5] += r->w > 0.0f && r->a[HOTA] != 0;
#endif
            /* lane/ladder4: the trained labels by the dig the state showed (none, the hit alone, a crack), and of
             * those the ones holding the attack */
            if (r->w > 0.0f) {
                int dg = D->pdig[i] & 1 ? 2 : D->pdig[i] >> 1 & 1;
                C->dig[dg][0] += 1;
                C->dig[dg][1] += r->a[NETH_NET_CAM + KEY_ATTACK] != 0;
            }
        }
        if (NETH_NET_FEATMASK & NETH_NET_FDK) {     /* lane/audit-feat: the run's dk block (net.h NETH_NET_OFF_OF) */
            const float *g = b->scalars + (size_t)i * NETH_NET_NS + NETH_NET_OFF_OF(NETH_NET_FEATMASK, NETH_NET_FDK);
            D->pdig[i] = b->truncations[i] ? 0 : (uint8_t)((g[0] > 0.0f) | (g[1] > 0.0f) << 1);
        }
        if (NETH_NET_FEATMASK & NETH_NET_FGV) {
            /* lane/ladder10: the open screen's kind flags (late_scalars bit 4's first four) for the next step's label */
            const float *g = b->scalars + (size_t)i * NETH_NET_NS + NETH_NET_OFF_OF(NETH_NET_FEATMASK, NETH_NET_FGV);
            uint8_t was = D->pscr[i];
            D->pscr[i] = b->truncations[i] ? 0 : g[0] > 0.0f ? 1 : g[1] > 0.0f ? 2 : g[2] > 0.0f ? 3 : g[3] > 0.0f ? 4 : 0;
            /* lane/ladder14: the step that made a screen show opened it */
            if (!D->pscr[i] || !(l->flags & NETH_VEC_LBL_VALID)) D->popen[i] = 0;
            else if (!was || was != D->pscr[i])
                D->popen[i] = l->mbit == NETH_VEC_CRAFT ? 1 : l->mbit == 0 ? 2 : 3;
        }
        {
            int sg = stage_of(b->scalars + (size_t)i * NETH_NET_NS);
            if (b->truncations[i] || !(l->flags & NETH_VEC_LBL_VALID)) D->stage[i] = 0;
            if (!b->truncations[i] && sg > D->stage[i]) D->stage[i] = (uint8_t)sg;
        }
        /* the next step's previous-action fields: the env resets them (the centre, no key, no macro) when the
         * episode ended with this step or this was the reset's */
        int32_t np[4] = {NETH_NET_BINS / 2, NETH_NET_BINS / 2, 0, 0};
        if (r->valid && !b->truncations[i]) {
            np[0] = em[0] < 0 ? 0 : em[0] >= NETH_NET_BINS ? NETH_NET_BINS - 1 : em[0];
            np[1] = em[1] < 0 ? 0 : em[1] >= NETH_NET_BINS ? NETH_NET_BINS - 1 : em[1];
            for (int j = 0; j < NETH_NET_KEYS; ++j) np[2] |= (em[NETH_NET_CAM + j] > 0) << j;
            np[3] = mac;
        }
        /* the check: where the teacher's policy drove this step its label is its action, and the env's own fields
         * for the next step must be the student's */
        if (own && !b->truncations[i])
            (prev_equal(b->scalars + (size_t)i * NETH_NET_NS, np) ? C->prev_ok : C->prev_bad) += 1;
        memcpy(D->prev[i], np, sizeof np);
        if (D->drive && b->truncations[i]) D->drive[i] = (uint8_t)draw_driver(D);  /* the next episode's driver */
    }
}

extern "C" void neth_distill_stage_step(struct neth_distill *D, int seg, int t, int lo, int gs, void *s) {
    CK(cudaMemcpyAsync(D->seg[seg] + (size_t)t * gs, D->hst + lo, (size_t)gs * sizeof(struct neth_distill_rec),
                       cudaMemcpyHostToDevice, (cudaStream_t)s));
}

/* the student's scalars: the teacher's with the record's previous-action fields */
__global__ void k_ssc(long N, const neth_sc_t *sc, const struct neth_distill_rec *rec, neth_sc_t *out, int zlo, int zn, int penv) {
    long q = (long)blockIdx.x * blockDim.x + threadIdx.x;
    if (q >= N * NETH_NET_NS) return;
    long n = q / NETH_NET_NS;
    int j = (int)(q - n * NETH_NET_NS);
    const int32_t *p = rec[n].p;
    float x;
    if (j < SC_MAC || penv) x = neth_sc_f32(sc[q]);
    else if (j < SC_CAM) x = j - SC_MAC == p[3] ? 1.0f : 0.0f;
    else if (j < SC_KEY) x = j - SC_CAM == p[0] || j - SC_CAM == NETH_VEC_CAM + p[1] ? 1.0f : 0.0f;
    else if (j < SC_KEY + NETH_VEC_KEYS) x = (float)((p[2] >> (j - SC_KEY)) & 1);
    else x = neth_sc_f32(sc[q]);     /* lane/ladder2: the late scalars and the hotbar's tools as the env wrote them (they read 0
                         * here before: the keys' shift ran past the 14 keys) */
    out[q] = neth_f32_sc(j >= zlo && j < zlo + zn ? 0.0f : x);
}

/* the minibatch's sums of weight and valid */
__global__ void k_cnt(int b, const int32_t *idx, const struct neth_distill_rec *rec, float *cnt) {
    __shared__ float sw[1024], sv[1024];
    float w = 0.0f, v = 0.0f;
    for (int n = threadIdx.x; n < b; n += blockDim.x) {
        long s = idx ? idx[n] : n;
        w += rec[s].w;
        v += (float)rec[s].valid;
    }
    sw[threadIdx.x] = w, sv[threadIdx.x] = v;
    __syncthreads();
    for (int k = blockDim.x / 2; k > 0; k >>= 1) {
        if ((int)threadIdx.x < k) sw[threadIdx.x] += sw[threadIdx.x + k], sv[threadIdx.x] += sv[threadIdx.x + k];
        __syncthreads();
    }
    if (threadIdx.x == 0) cnt[0] = sw[0], cnt[1] = sv[0];
}

__device__ __forceinline__ float wsum(float x) {
    for (int o = 16; o; o >>= 1) x += __shfl_xor_sync(0xffffffffu, x, o);
    return x;
}
__device__ __forceinline__ float wmax(float x) {
    for (int o = 16; o; o >>= 1) x = fmaxf(x, __shfl_xor_sync(0xffffffffu, x, o));
    return x;
}

/* the loss over a minibatch: sum w (CE of both camera bins + the keys' BCE) / sum w + vf / 2 mean_valid (v - v_t)^2;
 * its gradient over the heads; the books (unnormalised sums) by atomics */
__global__ void k_bc(int b, const int32_t *idx, float *hd, nt16 *dhb, const struct neth_distill_rec *rec, const float *val,
                     const float *cnt, float vf, double *books, const float *thd, float hotw, float holdw) {
    int lane = threadIdx.x & 31, wib = threadIdx.x >> 5, n = blockIdx.x * LW + wib;
    if (n >= b) return;
    long s = idx ? idx[n] : n;
    const struct neth_distill_rec *r = rec + s;
    float *h = hd + (long)n * HS;
    /* --soft: the teacher's heads for this sample where its label is the teacher's action */
    const float *th = thd && r->soft ? thd + (long)n * HS : NULL;
    float w = r->w, wn = w / fmaxf(cnt[0], 1.0f), vn = (float)r->valid / fmaxf(cnt[1], 1.0f);
    int hi = lane + 32 < NETH_NET_BINS;
    float out[2 * NETH_NET_CAM], ce = 0.0f;
    int hit = 1;
#pragma unroll
    for (int c = 0; c < NETH_NET_CAM; c++) {
        const float *l = h + c * NETH_NET_BINS;
        float va = l[lane], vb = hi ? l[lane + 32] : -INFINITY;
        float mx = wmax(fmaxf(va, vb));
        float se = wsum(expf(va - mx) + (hi ? expf(vb - mx) : 0.0f));
        float lse = logf(se) + mx;
        int k = r->a[c];
        k = k < 0 ? 0 : k >= NETH_NET_BINS ? NETH_NET_BINS - 1 : k;
        float lk = __shfl_sync(0xffffffffu, k < 32 ? va : vb, k & 31);
        hit &= lk == mx;
        float pa = expf(va - lse), pb = hi ? expf(vb - lse) : 0.0f;
        /* the target: the label's bin, or (soft) the teacher's softmax */
        float qa = lane == k ? 1.0f : 0.0f, qb = lane + 32 == k ? 1.0f : 0.0f;
        if (th) {
            const float *tl = th + c * NETH_NET_BINS;
            float ta = tl[lane], tb = hi ? tl[lane + 32] : -INFINITY;
            float tm = wmax(fmaxf(ta, tb));
            float ts = wsum(expf(ta - tm) + (hi ? expf(tb - tm) : 0.0f));
            qa = expf(ta - tm) / ts;
            qb = hi ? expf(tb - tm) / ts : 0.0f;
        }
        ce += wsum(qa * (lse - va) + (hi ? qb * (lse - vb) : 0.0f));
        out[2 * c] = wn * (pa - qa);
        out[2 * c + 1] = hi ? wn * (pb - qb) : 0.0f;
    }
    int kl = lane < NETH_NET_KEYS;
    float x = kl ? h[NETH_NET_CAM * NETH_NET_BINS + lane] : 0.0f, y = kl ? (float)r->a[NETH_NET_CAM + lane] : 0.0f;
    int hk = y > 0.5f;  /* the sampled key, the accuracy's */
    if (th && kl) y = 1.0f / (1.0f + expf(-th[NETH_NET_CAM * NETH_NET_BINS + lane]));
    float bce = wsum(kl ? fmaxf(x, 0.0f) - x * y + log1pf(expf(-fabsf(x))) : 0.0f);
    int khit = __all_sync(0xffffffffu, !kl || ((x > 0.0f) == (hk != 0)));
    float gk = (r->hold && lane == KEY_ATTACK ? holdw : 1.0f) * wn * (1.0f / (1.0f + expf(-x)) - y);
#if NETH_NET_HOTBAR
    /* the hotbar: a categorical over lanes 0 .. 9, its target the label's category (soft: the teacher's softmax) */
    int hl = lane < NETH_NET_HOT, kh = r->a[HOTA];
    kh = kh < 0 ? 0 : kh >= NETH_NET_HOT ? NETH_NET_HOT - 1 : kh;
    float xh = hl ? h[NETH_LEARN_HOFF + lane] : -INFINITY, mh = wmax(xh);
    float lseh = logf(wsum(hl ? expf(xh - mh) : 0.0f)) + mh, ph = hl ? expf(xh - lseh) : 0.0f;
    float qh = lane == kh ? 1.0f : 0.0f;
    if (th) {
        float tx = hl ? th[NETH_LEARN_HOFF + lane] : -INFINITY, tm = wmax(tx);
        float ts = wsum(hl ? expf(tx - tm) : 0.0f);     /* every lane in the shuffles */
        qh = hl ? expf(tx - tm) / ts : 0.0f;
    }
    float ceh = wsum(hl ? qh * (lseh - xh) : 0.0f);
    int hhit = __shfl_sync(0xffffffffu, xh, kh) == mh;
    float gh = hl ? (kh ? hotw : 1.0f) * wn * (ph - qh) : 0.0f;
#endif
    float v = h[NETH_LEARN_HEADS - 1], dv = v - val[s];
    __syncwarp();
#pragma unroll
    for (int c = 0; c < NETH_NET_CAM; c++) {
        h[c * NETH_NET_BINS + lane] = out[2 * c];
        if (hi) h[c * NETH_NET_BINS + lane + 32] = out[2 * c + 1];
    }
    if (kl) h[NETH_NET_CAM * NETH_NET_BINS + lane] = gk;
#if NETH_NET_HOTBAR
    if (hl) h[NETH_LEARN_HOFF + lane] = gh;
#endif
    if (lane == 0) h[NETH_LEARN_HEADS - 1] = vf * dv * vn;
    if (lane < HS - NETH_LEARN_HEADS) h[NETH_LEARN_HEADS + lane] = 0.0f;
    if (dhb) {
        __syncwarp();
        nt16 *o = dhb + (long)n * HS;
        for (int j = lane; j < HS; j += 32) o[j] = f2h(h[j] * NETH_NET_LSCALE);
    }
    if (lane == 0) {
        atomicAdd(books + 0, (double)(w * ce));
        atomicAdd(books + 1, (double)(w * bce));
        atomicAdd(books + 2, (double)(w * (float)hit));
        atomicAdd(books + 3, (double)(w * (float)khit));
        atomicAdd(books + 4, (double)((float)r->valid * dv * dv));
        atomicAdd(books + 5, (double)w);
        atomicAdd(books + 6, (double)r->valid);
        atomicAdd(books + 7, 1.0);
#if NETH_NET_HOTBAR
        atomicAdd(books + 8, (double)(w * ceh));
        atomicAdd(books + 9, (double)(w * (float)hhit));
        atomicAdd(books + 10, (double)(kh ? w : 0.0f));
        atomicAdd(books + 11, (double)(kh ? w * (float)hhit : 0.0f));
#endif
    }
}

static void loss_cb(void *ctx, int b, const int32_t *idx, float *hd, void *dhb, void *stream) {
    struct neth_distill *D = (struct neth_distill *)ctx;
    cudaStream_t s = (cudaStream_t)stream;
    k_cnt<<<1, 1024, 0, s>>>(b, idx, D->cur, D->cnt);
    k_bc<<<(b + LW - 1) / LW, LW * 32, 0, s>>>(b, idx, hd, (nt16 *)dhb, D->cur, D->cur_val, D->cnt, D->vf, D->books,
                                               D->tw ? D->thd : NULL, D->hotw, D->holdw);
}

extern "C" void neth_distill_hot_weight(struct neth_distill *D, float w) { D->hotw = w; }
extern "C" void neth_distill_student_zero(struct neth_distill *D, int lo, int n) { D->zlo = lo, D->zn = n; }
extern "C" void neth_distill_student_prev_env(struct neth_distill *D, int on) { D->penv = on != 0; }
extern "C" void neth_distill_hold_weight(struct neth_distill *D, float w, int scope) { D->holdw = w, D->hscope = scope; }

extern "C" void neth_distill_learn(struct neth_distill *D, neth_learn *ln, const neth_learn_hyper *h, int epochs, int mb, const int32_t *perm, int seg,
                          const uint8_t *obs, const neth_sc_t *sc, const float *val, float *w, float *grad, float *m,
                          float *v, int *step, void *stream) {
    cudaStream_t s = (cudaStream_t)stream;
    long N = (long)D->T * D->gs;
    const struct neth_distill_rec *rec = D->seg[seg];
    k_ssc<<<(int)((N * NETH_NET_NS + 255) / 256), 256, 0, s>>>(N, sc, rec, D->ssc, D->zlo, D->zn, D->penv);
    D->cur = rec;
    D->cur_val = val;
    neth_learn_hyper hh = *h;
    hh.loss = loss_cb;
    hh.lctx = D;
    for (int e = 0; e < epochs; e++)
        for (long i = 0; i < N; i += mb) {
            int b = N - i < mb ? (int)(N - i) : mb;
            if (D->tw) {
                /* --soft: the teacher's heads on this minibatch (its own scalars, the env's), kept for the loss */
                if (D->thd_n < b) {
                    cudaFree(D->thd);
                    CK(cudaMalloc((void **)&D->thd, (size_t)b * HS * sizeof(float)));
                    D->thd_n = b;
                }
                const float *th = neth_learn_forward(ln, D->tw, b, perm + (long)e * N + i, obs, sc);
                CK(cudaMemcpyAsync(D->thd, th, (size_t)b * HS * sizeof(float), cudaMemcpyDeviceToDevice, s));
            }
            neth_learn_grad(ln, &hh, 0.0f, w, b, perm + (long)e * N + i, obs, D->ssc, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                     grad, NULL);
            neth_learn_adam(ln, &hh, ++*step, w, grad, m, v, NULL);
        }
}

extern "C" void neth_distill_books(struct neth_distill *D, double *out, void *stream) {
    cudaStream_t s = (cudaStream_t)stream;
    CK(cudaMemcpyAsync(D->hbooks, D->books, NETH_DISTILL_NBOOK * sizeof(double), cudaMemcpyDeviceToHost, s));
    CK(cudaMemsetAsync(D->books, 0, NETH_DISTILL_NBOOK * sizeof(double), s));
    CK(cudaStreamSynchronize(s));
    for (int k = 0; k < NETH_DISTILL_NBOOK; ++k) out[k] = D->acc[k] = D->hbooks[k];
}

extern "C" const struct neth_distill_census *neth_distill_census(const struct neth_distill *D) { return &D->cs; }

extern "C" void neth_distill_row(struct neth_distill *D, void *file, int64_t env_steps, double wall) {
    FILE *f = (FILE *)file;
    const double *a = D->acc;
    double W = a[5] > 0 ? a[5] : 1, V = a[6] > 0 ? a[6] : 1;
    fprintf(f, "{\"env_steps\": %lld, \"wall\": %.3f, \"ce_cam\": %.6g, \"ce_keys\": %.6g, \"acc_cam\": %.6g, "
               "\"acc_keys\": %.6g, \"vl\": %.6g, \"trained\": %.0f, \"samples\": %.0f",
            (long long)env_steps, wall, a[0] / W, a[1] / W, a[2] / W, a[3] / W, a[4] / V, a[5], a[7]);
#if NETH_NET_HOTBAR
    fprintf(f, ", \"ce_hot\": %.6g, \"acc_hot\": %.6g, \"switch\": %.0f, \"acc_switch\": %.6g", a[8] / W, a[9] / W, a[10],
            a[10] > 0 ? a[11] / a[10] : 0.0);
#endif
    const struct neth_distill_census *C = &D->cs;
    if (D->lab)     /* lane/ladder6 */
        fprintf(f, ", \"lsteps\": %lld, \"shadow\": [%lld, %lld, %lld, %lld], \"lmasked\": %lld", (long long)C->lsteps,
                (long long)C->shadow[0], (long long)C->shadow[1], (long long)C->shadow[2], (long long)C->shadow[3],
                (long long)C->lmasked);
    fprintf(f, ", \"prev_ok\": %lld, \"prev_bad\": %lld, \"invalid\": %lld, \"student\": %lld, \"agree\": %lld, ",
            (long long)C->prev_ok, (long long)C->prev_bad, (long long)C->invalid, (long long)C->student,
            (long long)C->agree);
    if (NETH_NET_FEATMASK & NETH_NET_FDK)    /* lane/audit-feat: the run's dk */
        fprintf(f, "\"dig\": [[%lld, %lld], [%lld, %lld], [%lld, %lld]], ", (long long)C->dig[0][0], (long long)C->dig[0][1],
                (long long)C->dig[1][0], (long long)C->dig[1][1], (long long)C->dig[2][0], (long long)C->dig[2][1]);
    if (D->level & (NETH_VEC_NOTABLE | NETH_VEC_NORECIPE)) {   /* lane/ladder12: the craft macro by half */
        fputs("\"halves\": [", f);
        for (int h = 0; h < 5; ++h)     /* lane/ladder15: [4] the table half's first step */
            fprintf(f, "%s[%lld, %lld, %lld, %lld, %lld, %lld]", h ? ", " : "", (long long)C->half[h][0],
                    (long long)C->half[h][1], (long long)C->half[h][2], (long long)C->half[h][3],
                    (long long)C->half[h][4], (long long)C->half[h][5]);
        fputs("], ", f);
    }
    if (D->level & NETH_VEC_NORECIPE) {     /* lane/ladder13: the recipe half by screen */
        fputs("\"rscr\": [", f);
        for (int k = 0; k < 5; ++k)
            fprintf(f, "%s[%lld, %lld, %lld]", k ? ", " : "", (long long)C->rscr[k][0], (long long)C->rscr[k][1],
                    (long long)C->rscr[k][2]);
        fputs("], ", f);
    }
    if (NETH_NET_FEATMASK & NETH_NET_FGV) {     /* lane/audit-feat: the run's gv */
        /* lane/ladder14: win[screen][opener][label] flattened, 6 a cell; wrel */
        fputs("\"win\": [", f);
        const int64_t *q = &C->win[0][0][0][0];
        for (int k = 0; k < 3 * 4 * 6 * 6; ++k) fprintf(f, "%s%lld", k ? ", " : "", (long long)q[k]);
        fprintf(f, "], \"wrel\": [%lld, %lld, %lld], ", (long long)C->wrel[0], (long long)C->wrel[1],
                (long long)C->wrel[2]);
    }
    if (D->tlab) fprintf(f, "\"tlabs\": %lld, ", (long long)C->tlabs);     /* lane/ladder15 */
    if (D->level & NETH_VEC_SEEKKEY)     /* lane/ladder16 */
        fprintf(f, "\"seekkey\": [%lld, %lld, %lld], \"seekwalk\": [%lld, %lld, %lld, %lld], ",
                (long long)C->seekkey[0], (long long)C->seekkey[1], (long long)C->seekkey[2],
                (long long)C->seekwalk[0], (long long)C->seekwalk[1], (long long)C->seekwalk[2],
                (long long)C->seekwalk[3]);     /* lane/ladder18: seekwalk */
    if (C->x2[0] || C->x2[2])   /* lane/ladder17 */
        fprintf(f, "\"x2\": [%lld, %lld, %lld, %lld], ", (long long)C->x2[0], (long long)C->x2[1],
                (long long)C->x2[2], (long long)C->x2[3]);
    fputs("\"census\": {", f);
    int k = 0;
    for (int c = 0; c < NETH_DISTILL_NCLASS; ++c)
        if (C->steps[c])
            fprintf(f, "%s\"%s\": [%lld, %lld, %lld, %lld, %lld, %lld, %lld, %.4g]", k++ ? ", " : "", CLASS_NAMES[c],
                    (long long)C->steps[c], (long long)C->trained[c], (long long)C->gui[c], (long long)C->hotbar[c],
                    (long long)C->far[c], (long long)C->press[c], (long long)C->exact[c],
                    C->res_abs[c] / (double)C->steps[c]);
    fputs("}", f);
#if NETH_NET_HOTBAR
    fputs(", \"hotlab\": {", f);  /* each driver's trained labels that change the slot */
    k = 0;
    for (int c = 0; c < NETH_DISTILL_NCLASS; ++c)
        if (C->steps[c]) fprintf(f, "%s\"%s\": %lld", k++ ? ", " : "", CLASS_NAMES[c], (long long)C->hotlab[c]);
    fputs("}", f);
#endif
    /* lane/ladder2: per stage and driver [steps, trained, gui, hotbar, far, slot-change labels] */
    fputs(", \"stages\": {", f);
    for (int g = 0; g < NETH_DISTILL_NSTAGE; ++g) {
        fprintf(f, "%s\"%s\": {", g ? ", " : "", STAGE_NAMES[g]);
        k = 0;
        for (int c = 0; c < NETH_DISTILL_NCLASS; ++c) {
            const int64_t *st = C->st[g][c];
            if (st[0])
                fprintf(f, "%s\"%s\": [%lld, %lld, %lld, %lld, %lld, %lld]", k++ ? ", " : "", CLASS_NAMES[c],
                        (long long)st[0], (long long)st[1], (long long)st[2], (long long)st[3], (long long)st[4],
                        (long long)st[5]);
        }
        fputs("}", f);
    }
    fputs("}", f);
    fputs("}\n", f);
    fflush(f);
}

extern "C" void neth_distill_print(const struct neth_distill *D) {
    const struct neth_distill_census *C = &D->cs;
    int64_t all = 0;
    for (int c = 0; c < NETH_DISTILL_NCLASS; ++c) all += C->steps[c];
    printf("distill: level %d; %lld decision steps (%lld reset steps), previous-action check %lld equal, %lld not\n",
           D->level, (long long)all, (long long)C->invalid, (long long)C->prev_ok, (long long)C->prev_bad);
    if (D->drive)
        printf("distill: --beta %g: the student drove %lld decision steps; %lld of its own steps were the teacher's "
               "action\n", D->beta, (long long)C->student, (long long)C->agree);
    if (D->lab)
        printf("distill: --labeller: %lld decision steps at the student's level; the labeller's labels %lld (start %lld, "
               "wooden %lld, stone %lld, iron %lld); %lld of the teacher's labels there masked\n", (long long)C->lsteps,
               (long long)(C->shadow[0] + C->shadow[1] + C->shadow[2] + C->shadow[3]), (long long)C->shadow[0],
               (long long)C->shadow[1], (long long)C->shadow[2], (long long)C->shadow[3], (long long)C->lmasked);
    if (D->level & (NETH_VEC_NOTABLE | NETH_VEC_NORECIPE)) {   /* lane/ladder12 */
        /* lane/ladder12 */
        static const char *const HN[5] = {"other", "table", "recipe", "recipe-first", "table-first"};
        printf("distill: the craft macro by half [steps, trained, under a screen, hotbar, use, inventory key]:");
        for (int h = 0; h < 5; ++h)
            printf(" %s %lld %lld %lld %lld %lld %lld;", HN[h], (long long)C->half[h][0], (long long)C->half[h][1],
                   (long long)C->half[h][2], (long long)C->half[h][3], (long long)C->half[h][4],
                   (long long)C->half[h][5]);
        printf("\n");
        /* lane/ladder13 */
        static const char *const SN[5] = {"none", "inventory", "table", "furnace", "other"};
        printf("distill: the recipe half by screen [first steps, later steps, later trained]:");
        for (int k = 0; k < 5; ++k)
            printf(" %s %lld %lld %lld;", SN[k], (long long)C->rscr[k][0], (long long)C->rscr[k][1],
                   (long long)C->rscr[k][2]);
        printf("\n");
    }
    {
        /* lane/ladder14: the labels under a window by its opener and the label's driver (non-zero rows) */
        static const char *const WN[3] = {"inventory", "table", "furnace"};
        static const char *const ON[4] = {"unseen", "craft", "policy", "other"};
        static const char *const LN[6] = {"policy", "recipe-first", "recipe-later", "craft-other", "screen", "macro"};
        for (int w = 0; w < 3; ++w)
            for (int o = 0; o < 4; ++o)
                for (int c = 0; c < 6; ++c) {
                    const int64_t *q = C->win[w][o][c];
                    if (q[0])
                        printf("distill: window %s opened by %s, label %s: steps %lld trained %lld jump %lld close "
                               "%lld click %lld fits %lld\n", WN[w], ON[o], LN[c], (long long)q[0], (long long)q[1],
                               (long long)q[2], (long long)q[3], (long long)q[4], (long long)q[5]);
                }
        if (D->wlab)
            printf("distill: --window-label: %lld labels the jump, %lld no key, %lld the close (level)\n",
                   (long long)C->wrel[0], (long long)C->wrel[1], (long long)C->wrel[2]);
    }
    if (D->tlab)    /* lane/ladder15 */
        printf("distill: --table-label: %lld policy steps labelled the table key\n", (long long)C->tlabs);
    if (D->level & NETH_VEC_SEEKKEY)     /* lane/ladder16 */
        printf("distill: seekkey: %lld seek starts labelled key 13, %lld later seek steps the level's macro, %lld "
               "policy steps with the start holding labelled key 13 (--seek-label)\n",
               (long long)C->seekkey[0], (long long)C->seekkey[1], (long long)C->seekkey[2]);
    if ((D->level & NETH_VEC_SEEKKEY) && (C->seekwalk[0] || C->seekwalk[1]))     /* lane/ladder18 */
        printf("distill: seekwalk: %lld seek steps in the policy's half (the teacher's), %lld walk labels beside the "
               "level, %lld of them far, %lld trained\n",
               (long long)C->seekwalk[0], (long long)C->seekwalk[1], (long long)C->seekwalk[2],
               (long long)C->seekwalk[3]);
    if (C->x2[0] || C->x2[2])   /* lane/ladder17 */
        printf("distill: recipe_2x2 clicks: the craft macro's 2x2 recipe steps %lld (trained %lld), the 2x2 labeller's "
               "on the student's own inventory %lld (trained %lld)\n",
               (long long)C->x2[0], (long long)C->x2[1], (long long)C->x2[2], (long long)C->x2[3]);
    printf("distill: %-8s %10s %6s %8s %6s %6s %6s %6s %6s %8s\n", "driver", "steps", "share", "trained", "gui",
           "hotbar", "far", "press", "exact", "|res|deg");
    for (int c = 0; c < NETH_DISTILL_NCLASS; ++c) {
        if (!C->steps[c]) continue;
        double s = (double)C->steps[c];
        printf("distill: %-8s %10lld %6.4f %8.4f %6.4f %6.4f %6.4f %6.4f %6.4f %8.4f\n", CLASS_NAMES[c],
               (long long)C->steps[c], s / (double)(all ? all : 1), C->trained[c] / s, C->gui[c] / s,
               C->hotbar[c] / s, C->far[c] / s, C->press[c] / s, C->exact[c] / s, C->res_abs[c] / s);
    }
#if NETH_NET_HOTBAR
    int64_t hl = 0;
    for (int c = 0; c < NETH_DISTILL_NCLASS; ++c) hl += C->hotlab[c];
    printf("distill: the hotbar head: %lld trained labels change the slot (%.4f of the decision steps)", (long long)hl,
           (double)hl / (double)(all ? all : 1));
    for (int c = 0; c < NETH_DISTILL_NCLASS; ++c)
        if (C->hotlab[c]) printf(", %s %lld", CLASS_NAMES[c], (long long)C->hotlab[c]);
    printf("\n");
#endif
}

/* ---- the widening ---- */

/* lane/audit-fixes: neth_checkpoint.c's parser and writer (neth_pipe_internal.h): the teacher's header and scalars are
 * the parser's (its fc1 at the run's scalars, each feature it names where the run reads it), the wm and the heads
 * it lacks this widening's */
extern "C" int neth_ntw_parse(const void *data, size_t len, const char *name, int flags, float *t[NETH_NET_TENSORS],
                              int dims[NETH_NET_TENSORS][4], char *err, size_t en);
extern "C" unsigned char *neth_ntw_write(const float *const t[NETH_NET_TENSORS], size_t *len);

extern "C" unsigned char *neth_distill_widen(const unsigned char *b, size_t len, const char *name, size_t *out, char *err,
                                             size_t en) {
    static const int rank[NETH_NET_TENSORS] = {
#define NETH_NET_R(n, r, a, b, c, d) r,
        NETH_NET_ORDER(NETH_NET_R)
#undef NETH_NET_R
    };
    static const int dims[NETH_NET_TENSORS][4] = {
#define NETH_NET_D4(n, r, a, b, c, d) {a, b, c, d},
        NETH_NET_ORDER(NETH_NET_D4)
#undef NETH_NET_D4
    };
    static const char *const names[NETH_NET_TENSORS] = {
#define NETH_NET_N(n, r, a, b, c, d) #n,
        NETH_NET_ORDER(NETH_NET_N)
#undef NETH_NET_N
    };
    /* NETH_NET_HOTBAR (lane/nthotbar): a blob without the hotbar's head takes one that always keeps the slot (its weights
     * 0, its bias 0 for keep and NETH_DISTILL_NOHOT for each slot: never drawn, and logp's hotbar term exactly 0), so a teacher
     * trained without the head acts and scores as it did; NETH_NET_RNN (lane/ladder8): a blob without the minGRU's two
     * tensors (and the hotbar's, before them) takes them zero: h stays 0 and the net is the blob's feed-forward one */
    float *src[NETH_NET_TENSORS];
    int sd[NETH_NET_TENSORS][4];
    int has = neth_ntw_parse(b, len, name, NETH_NTW_FEWER | NETH_NTW_NARROW, src, sd, err, en);
    if (has < 0) return NULL;
    if (!(has == NETH_NET_TENSORS || (NETH_NET_HOTBAR && !NETH_NET_RNN && has == NETH_NET_TENSORS - 2) ||
          (NETH_NET_RNN && has == NETH_NET_TENSORS - 2) || (NETH_NET_RNN && NETH_NET_HOTBAR && has == NETH_NET_TENSORS - 4))) {
        for (int i = 0; i < has; ++i) free(src[i]);
        snprintf(err, en, "%s: %d tensors, not a blob of %d (another stem?)", name, has, NETH_NET_TENSORS);
        return NULL;
    }
    /* the source's dims (the parser's: fc1's input conv3's c3 * O3^2 columns, then the run's scalars) */
    const int fc1 = 2 * NETH_NET_STEM + 6, c3w = 2 * NETH_NET_STEM + 4;
    int c3s = sd[c3w][0], sp = NETH_NET_O3 * NETH_NET_O3;
    float *dst[NETH_NET_TENSORS];
    for (int i = 0; i < NETH_NET_TENSORS; ++i) {
        int r = rank[i];
        size_t m = 1, n = 1;
        for (int k = 0; k < r; ++k) m *= (size_t)dims[i][k], n *= (size_t)sd[i][k];
        dst[i] = (float *)calloc(m, sizeof(float));
        if (!src[i]) {     /* the added hotbar head: hot_w zero, hot_b keep 0 and the slots NETH_DISTILL_NOHOT; the minGRU 0 */
            if (!strcmp(names[i], "hot_b"))
                for (size_t e = 1; e < m; ++e) dst[i][e] = NETH_DISTILL_NOHOT;
            continue;
        }
        int td[4] = {1, 1, 1, 1};
        for (int k = 0; k < r; ++k) td[k] = dims[i][k];
        for (size_t e = 0; e < n; ++e) {
            size_t x = e;
            int ix[4] = {0, 0, 0, 0};
            for (int k = r - 1; k >= 0; --k) ix[k] = (int)(x % (size_t)sd[i][k]), x /= (size_t)sd[i][k];
            if (i == fc1 && ix[1] >= c3s * sp) ix[1] = ix[1] - c3s * sp + NETH_NET_C3 * sp;     /* the scalars' columns */
            size_t t = 0;
            for (int k = 0; k < r; ++k) t = t * (size_t)td[k] + (size_t)ix[k];
            dst[i][t] = src[i][e];
        }
    }
    unsigned char *o = neth_ntw_write(dst, out);
    for (int i = 0; i < NETH_NET_TENSORS; ++i) free(src[i]), free(dst[i]);
    return o;
}

