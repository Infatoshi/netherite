/* The trainer's episodes as labelled data (lane/runrec): what ntloop's vector env plays, every episode its record
 * (vecenv.h struct neth_vec_episode: the env, start, length, milestones, the books) and per policy step both levels
 * of action, the policy's (its 16 heads: the camera's two bins and the 14 keys, the craft pick, the macro kind that
 * drove the step, chosen, the driver) and the act the engine stepped (rlbind.h struct neth_rl_act: holds, presses, the
 * look, the hotbar, the gui ops), with the step's reward. The observation is not kept: the episode's tape replays it.
 *
 * The main thread's part a group step: at recv the actions and picks copied aside (rec_group: 8 KB at 128 envs, what
 * the policy's next launch replaces), and once the policy is launched, while the device runs it, each env's step
 * packed into a 32-byte row put in a ring (rec_group_done: 4 KB at 128 envs, the env's buffers hold 316 bytes an
 * env-step). A writer thread at SCHED_IDLE spreads the rows into each env's columns, and at an episode's end keeps
 * it or not by the retention rule (rec_keep) and writes a kept one compressed (zstd). The main thread never waits on
 * it: a full ring drops the group step (the episodes it cuts are not written, and counted). The writer appends each
 * row to its env's episode as it is, and spreads an episode's rows into the columns only when it keeps it. Measured
 * on the dev host (DEVLOG lane/runrec): a writer reading a whole copy of the env's buffers (316 bytes an env-step)
 * and packing every step cost the trainer 0.55 to 0.85% at 16x16 with its CPU 3.4% of one thread (an idle writer,
 * or the main thread's copy alone, cost nothing measurable); the rows put the trainer within abrun's noise of no
 * recording (medians 0.2 to 0.7% under it at 16x16, ranges overlapping; +0.02% at 64x64).
 *
 * The file (DIR/episodes.nrec): a header (struct rec_head, then its text: the config's text whole, ntloop's
 * arguments and the resolved start, key: value lines), then a chunk an episode kept (struct rec_chunk, then zlen
 * bytes of zstd over the packed columns). An episode's packed columns, S its policy steps, in this order:
 *   u8 cam0[S], u8 cam1[S], u16 keys[S] (bit j: action[2 + j]), u8 pick[S], u8 kc[S] (kind | chosen << 3 |
 *   driver << 4), f32 reward[S], u32 hold[S], u32 press[S] (bit j: press[j]), u8 look_mode[S], f32 look0[S],
 *   f32 look1[S], i8 hotbar[S], u8 nops[S]; then each op of each step in order, six i32 (kind window slot button
 *   mode index); then nexc exceptions, each a u32 step index and the step's struct rec_step whole: a step any of
 *   whose fields the columns cannot hold (none in today's heads) overwritten from it.
 * rec_chunk.digest is FNV-1a over the packed columns' bytes (rec_digest), made before they are compressed; the
 * reader recomputes it from what it decompresses (the file's and zstd's integrity). That the columns hold the steps
 * is the codec's, which ntrec check proves against --rec-raw's copy (struct rec_step values, canonical: padding and
 * the ops past nops zero, made apart from the columns). The writer hashes a kept episode's packed bytes only, not
 * every env-step it packs (its CPU is the recording's cost to the trainer: DEVLOG lane/runrec). */
#ifndef NETHERITE_TRAIN_REC_H
#define NETHERITE_TRAIN_REC_H

#include <stddef.h>
#include <stdint.h>

#include "../vecenv.h"

#define REC_MAGIC "NWREC\0\0\1"
/* 2 (lane/ladder): struct rec_chunk's rung and fid at its end (a version 1 chunk is the prefix before them; the
 * reader takes both, a version 1 episode's fidelity the header's) */
/* 3 (lane/ticks-per-step): struct rec_chunk's nsub at its end, and the payload's subs after the exceptions: a step of
 * T > 1 ticks (rec_head.ticks_per_step) wrote only its first tick's act, so a macro's acts on the later ticks were
 * lost and ntrec tape refused it. Each later tick a macro drove is a sub (REC_SUB_BYTES: u32 the step, u32 the tick
 * 1 .. T - 1, the act whole as rec_canon leaves a step's); a later tick with no sub ran the hold-only act of the tick
 * before's holds (vecenv.c vec_act). A version 2 chunk is the prefix before nsub, with no subs */
/* 4 (lane/audit-fixes): struct neth_vec_episode's win at its end, the record in a chunk REC_EP_V3_BYTES + 8 bytes
 * (rec_head.ep_bytes). A version 1 to 3 file's records are the REC_EP_V3_BYTES before win: the reader puts each
 * in a chunk of this version with win 0 */
#define REC_VERSION 4
#define REC_EP_V3_BYTES offsetof(struct neth_vec_episode, win)
#define REC_CHUNK_MAGIC 0x5045574eu   /* "NWEP" */

/* rec_chunk.fid: what of vanilla the episode's world ran without (fidelity.h), 0 vanilla, the one fidelity whose
 * episodes the Java client replays */
enum {
    REC_FID_SPAWNING = 1,       /* fidelity.spawning off */
    REC_FID_RANDOM_TICKS = 2,   /* fidelity.random_ticks off */
    REC_FID_WEATHER = 4,        /* fidelity.weather off */
    REC_FID_MOBS = 8,           /* fidelity.mobs not all */
    REC_FID_SPAWN_TYPES = 16,   /* fidelity.spawn_types not all four */
    REC_FID_SKIP_AMBIENT = 32,  /* fidelity.skip_ambient on */
    REC_FID_DISTANT_TICKS = 64, /* fidelity.distant_tick_radius > 0 */
    REC_FID_UNKNOWN = 1u << 31  /* a version 1 episode of a run that was not vanilla */
};

/* one policy step, canonical (the check's and the digest's form) */
struct rec_step {
    int32_t action[NETH_VEC_ACTION];
    int32_t pick;
    uint8_t kind, chosen, driver, trunc;
    float reward;
    uint32_t pad;               /* 0 (the step in 64-bit words) */
    struct neth_rl_act act;
};

struct rec_head {
    char magic[8];
    uint32_t version, head_bytes, text_bytes;   /* sizeof this, the text after it */
    uint32_t ep_bytes, step_bytes, neth_rl_version; /* sizeof(struct neth_vec_episode), sizeof(struct rec_step) */
    int32_t n, groups, ep_len, ticks_per_step, macros, vanilla;
    int64_t seed;               /* world.seed */
    double t0;                  /* the wall clock at the start (s, CLOCK_REALTIME) */
};

enum { REC_KEEP_GAIN = 1, REC_KEEP_SAMPLE = 2, REC_KEEP_ALL = 4 };

struct rec_chunk {
    uint32_t magic, chunk_bytes;        /* REC_CHUNK_MAGIC, sizeof this */
    uint64_t index;                     /* the episode's number among every episode that ended (kept or not) */
    int32_t env, env_ep;                /* the env, its episode number (its first is 0) */
    uint32_t steps, nops, nexc, keep;   /* policy steps, the ops, the exceptions, why kept (REC_KEEP_*) */
    uint32_t raw_bytes, zlen;           /* the packed columns' bytes, and compressed */
    uint64_t digest;
    struct neth_vec_episode rec;
    /* version 2 (lane/ladder): the ladder's rung the episode ran (its index in the run's ladder.rungs, the header's
     * ladder line; -1 no ladder, or the env's first episode, the make's, at the sim's own fidelity) and its fidelity
     * (REC_FID_*, 0 vanilla) */
    int32_t rung;
    uint32_t fid;
    /* version 3 (lane/ticks-per-step): the subs at the payload's end (raw_bytes and the digest count them) */
    uint32_t nsub, pad3;
};
#define REC_CHUNK_V1_BYTES offsetof(struct rec_chunk, rung)
#define REC_CHUNK_V2_BYTES offsetof(struct rec_chunk, nsub)
#define REC_SUB_BYTES (8 + sizeof(struct neth_rl_act))

/* the retention rule: keep (REC_KEEP_GAIN: an episode that gains a milestone of the mask ms, bit i
 * reward.MILESTONES[i]; REC_KEEP_ALL: every one) and a share sample of the rest, drawn from a hash of the seed, the
 * env and its episode number (the same episodes at any speed). Gaining: from the config's start (start 0, where the
 * player holds nothing) reaching one; from a saved start, whose record's ms holds what the start carried, more
 * milestones at the end than the start's level and one of the mask's among them (the level counts the save mask's,
 * so a carried milestone outside it reads as a gain) */
struct rec_rule {
    int keep;           /* REC_KEEP_GAIN, REC_KEEP_ALL or 0 (the sample alone) */
    double sample;
    int level;          /* zstd's */
    uint32_t ms;        /* REC_KEEP_GAIN's milestones */
};
/* the default gain mask: every milestone but the wooden and stone pickaxes, which the base macros reach in 23 to 94%
 * of a run's episodes (the furnace and later ones in 0 to 20%: DEVLOG lane/runrec) */
#define REC_MS_DEFAULT (((1u << NETH_VEC_MILESTONES) - 1) & ~3u)

struct rec_opts {
    const char *path;           /* the file (appended to a new one; an existing one is refused) */
    const char *raw;            /* the check's copy (NULL: none): every kept episode's struct rec_step values as
                                 * copied, a struct rec_chunk before each (ntrec check) */
    struct rec_rule rule;
    const char *text;           /* the header's text */
    struct rec_head head;       /* n .. t0 filled by the caller */
    uint64_t seed;              /* the sample's hash seed */
    size_t ring_mb;             /* the ring (0: 64 MB, about half a second of the group steps at 350k env-steps/s) */
};

struct rec;
struct rec *rec_open(const struct rec_opts *o, char *err, size_t en);
/* group g's step from recv (envs lo .. lo + gs - 1), was 0 on the group's first recv (the make's step, no action
 * sent); eps the episode records recv made (the group's that ended with this step), k of them. Its slot in the ring
 * and the actions and picks (which the policy's next launch replaces) staged now; the rows (the act, the reward, the
 * macro's books, the truncation, packed) at rec_group_done, which the trainer calls before it sends the group: after
 * the policy's launch, so the packing overlaps the device instead of lengthening the group's turn. rung[q] and fid[q]
 * (lane/ladder) are eps[q]'s rec_chunk.rung and fid (NULL: -1 and 0) */
void rec_group(struct rec *r, int g, int lo, int gs, int was, const struct neth_vec_bufs *b,
               const struct neth_vec_episode *eps, int k, const int32_t *rung, const uint32_t *fid);
void rec_group_done(struct rec *r, int g, const struct neth_vec_bufs *b);
/* a fidelity's REC_FID_* code (0 vanilla) */
struct fidelity;
uint32_t rec_fid_code(const struct fidelity *f);
/* the writer finishes what was queued, the file is closed; out (when not NULL) its books: [0] episodes ended,
 * [1] kept, [2] env-steps packed, [3] env-steps kept, [4] packed bytes kept, [5] compressed bytes kept, [6] group
 * steps dropped (ring full), [7] episodes ended but cut (by a dropped group step, or no record found), [8] the
 * writer's busy seconds, [9] bytes written (with the chunk headers), [10] episodes unfinished at the close, [11] 1
 * the writer ran at SCHED_IDLE, 0 at the default priority, -1 refused */
void rec_close(struct rec *r, double out[12]);

/* the codec (the reader's too) */
int rec_keep(const struct rec_rule *u, uint64_t seed, const struct neth_vec_episode *e, int env_ep);
uint64_t rec_digest(const void *p, size_t n);
void rec_canon(struct rec_step *s);
/* an act canonical as rec_canon leaves a step's (padding and the ops past nops zero): a sub's (lane/ticks-per-step) */
void rec_canon_act(struct neth_rl_act *a);
/* steps[0..S) packed into out (rec_packed_max bytes at most); its bytes, the ops and exceptions into *nops, *nexc */
size_t rec_packed_max(uint32_t S);
size_t rec_pack(const struct rec_step *steps, uint32_t S, uint8_t *out, uint32_t *nops, uint32_t *nexc);
/* the packed columns back into steps[0..S); 0, or -1 when the bytes do not hold S steps, nops ops and nexc
 * exceptions */
int rec_unpack(const uint8_t *in, size_t len, uint32_t S, uint32_t nops, uint32_t nexc, struct rec_step *steps);
/* the packed columns' offsets, bytes and names (the ops and the exceptions last); how many (at most 16) */
int rec_columns(uint32_t S, uint32_t nops, uint32_t nexc, size_t off[], size_t len[], const char *names[]);

#endif
