/* The oracle's determinism layer, netherite.oracle.Det, bit for bit: one seeder
 * and one Math.random stream per role, newRandom, the name-seeded split Randoms
 * that replace vanilla's static shared Randoms, the per-role entity IDs and
 * UUIDs, reset(seed), and the stream digests Rows.java writes (d.sseed, d.smath,
 * d.sstat and the client twins). Vanilla draws from the same distributions; only
 * the seed source changes, so this file holds no game logic.
 *
 * Roles. Java picks the role of a call from the calling thread: Det.role() is
 * the integrated server thread, the client thread unless a frame is being drawn,
 * the client thread while drawing, and OTHER for anything else (netty, chunk
 * IO). The native engine is one thread and has no such tell, so every entry
 * point here takes the role explicitly and the *_cur forms read state->role,
 * which the engine sets on entry to server-tick, client-tick, frame-drawing and
 * other code (det_set_role). One role at a time is exact: no two Java roles ever
 * run at once (the oracle's lockstep parks the server while the client runs, and
 * RENDER code runs on the client thread between ticks), so a single field
 * replaces four threads.
 *
 * Entity IDs are per role with disjoint ranges, exactly as in Java; only
 * DET_SERVER ids ever reach the wire. Java's counter is synchronized because
 * several threads share the array; with one thread the increments do not need
 * a lock, but they do wrap the same way.
 *
 * The split Randoms are seeded by name (Det.splitRandom), not by creation order,
 * because class initialization can race at startup. The digest over them is
 * order free for the same reason: splitState() adds, it does not chain.
 *
 * The states are loaded and saved by the engine's snapshots: det_load and
 * det_split_add set a stream from the 48-bit state Det.state() reads, and the
 * det_*_state functions produce it. reset(seed) is the world-launch path.
 *
 * Det.otherDraws, the count of OTHER-role draws, is written on every OTHER draw
 * and never read anywhere in the oracle, so the port leaves it out. Det.clientNews
 * is --detail forensics for Rows.java and is pure oracle code.
 */
#ifndef NETHERITE_DET_H
#define NETHERITE_DET_H

#include <stdint.h>

#include "jrand.h"

enum { DET_CLIENT = 0, DET_SERVER = 1, DET_OTHER = 2, DET_RENDER = 3, DET_ROLES = 4 };

/* The names are the static names determinize.sh baked in, one path and field per
 * vanilla static Random; the longest in 1.7.10 is 73
 * ("./net/minecraft/tileentity/TileEntityEnchantmentTable.java:field_145923_r"). */
#define DET_NAME_MAX 128

/* java.util.Random: the 48-bit LCG plus the nextGaussian cache that plain
 * java.util.Random carries per instance. */
typedef struct {
    jrand r;
    int have_next_next_gaussian;
    double next_next_gaussian;
} det_rng;

/* Det.SplitRandom: one stream per role, all seeded from the same name. */
typedef struct det_split {
    char name[DET_NAME_MAX];
    det_rng d[DET_ROLES];
    uint8_t used[DET_ROLES];
    struct det_split *next;
} det_split;

typedef struct {
    int64_t world_seed;
    det_rng seeder[DET_ROLES];
    det_rng math[DET_ROLES];
    int32_t next_id[DET_ROLES];
    det_split *splits; /* creation order, the order Det.splits holds */
    int role;          /* the role the engine is currently running as */
} det_state;

void det_init(det_state *s);
void det_free(det_state *s);
void det_set_role(det_state *s, int role);
int det_role(const det_state *s);

/* Det.reset(seed): the world seed, the four seeders and Math streams, the four
 * entity-ID counters, and every registered split. Called once per world launch. */
void det_reset(det_state *s, int64_t seed);

/* Det.mix, the 64-bit splitmix steps both the seeding and the split digest use. */
uint64_t det_mix(uint64_t s, uint64_t k);

/* Java's String.hashCode, which names the split streams. Names are ASCII in
 * vanilla; a byte is one UTF-16 unit, and a non-ASCII name would hash the same
 * way only if the name were Latin-1. */
int32_t det_name_hash(const char *name);

/* java.util.Random, one stream. set_seed is the constructor's scramble and
 * clears the pending Gaussian, as Java's setSeed does. */
void det_rng_set_seed(det_rng *r, int64_t seed);
uint64_t det_rng_state(const det_rng *r);
/* the draws inline (jrand.h's): a client tick alone draws some 6,000
 * (cp_world_tick's display ticks), and the call around each cost as much
 * as the draw */
static inline int32_t det_rng_next(det_rng *r, int bits) { return jr_next(&r->r, bits); }
static inline int32_t det_rng_int(det_rng *r) { return jr_int(&r->r); }
static inline int32_t det_rng_int_n(det_rng *r, int32_t n) { return jr_int_n(&r->r, n); }
static inline int64_t det_rng_long(det_rng *r) { return jr_long(&r->r); }
static inline double det_rng_double(det_rng *r) { return jr_double(&r->r); }
static inline float det_rng_float(det_rng *r) { return jr_float(&r->r); }
static inline int det_rng_bool(det_rng *r) { return jr_bool(&r->r); }
double det_rng_gaussian(det_rng *r);

/* StrictMath.log and StrictMath.sqrt, fdlibm. nextGaussian is
 * sqrt(-2*log(s)/s), and glibc's log disagrees with fdlibm's by an ulp on about
 * 3% of inputs, so the port carries fdlibm's log (det_log) rather than libm's.
 * The square root is the IEEE one, which is correctly rounded on every host. */
double det_log(double x);
double det_sqrt(double x);

/* Det.newRandom, Det.mathRandom, Det.uuid, Det.nextEntityId, and the seeder draw
 * newRandom and uuid make. */
int64_t det_seeder_next_long(det_state *s, int role);
det_rng det_new_random(det_state *s);
det_rng det_new_random_role(det_state *s, int role);
double det_math_random(det_state *s);
double det_math_random_role(det_state *s, int role);
void det_uuid(det_state *s, int64_t *msb, int64_t *lsb);
void det_uuid_role(det_state *s, int role, int64_t *msb, int64_t *lsb);
int32_t det_next_entity_id(det_state *s);
int32_t det_next_entity_id_role(det_state *s, int role);

/* Det.splitRandom(name): register a stream per role and seed it. A vanilla call
 * site keeps the pointer, the way Item.itemRand is a field. */
det_split *det_split_random(det_state *s, const char *name);
det_split *det_split_find(const det_state *s, const char *name);
int det_split_index(const det_state *s, const det_split *sp);
void det_split_reseed(det_state *s, det_split *sp);

/* The SplitRandom draws. Each one marks its role's stream used, which is what
 * puts it into splitState(). */
int32_t det_split_next(det_state *s, det_split *sp, int bits);
int32_t det_split_int(det_state *s, det_split *sp);
int32_t det_split_int_n(det_state *s, det_split *sp, int32_t n);
int64_t det_split_long(det_state *s, det_split *sp);
double det_split_double(det_state *s, det_split *sp);
float det_split_float(det_state *s, det_split *sp);
int det_split_bool(det_state *s, det_split *sp);
double det_split_gaussian(det_state *s, det_split *sp);
void det_split_set_seed(det_state *s, det_split *sp, int64_t seed);
int32_t det_split_next_role(det_state *s, det_split *sp, int role, int bits);
int32_t det_split_int_role(det_state *s, det_split *sp, int role);
int32_t det_split_int_n_role(det_state *s, det_split *sp, int role, int32_t n);
int64_t det_split_long_role(det_state *s, det_split *sp, int role);
double det_split_double_role(det_state *s, det_split *sp, int role);
float det_split_float_role(det_state *s, det_split *sp, int role);
int det_split_bool_role(det_state *s, det_split *sp, int role);
double det_split_gaussian_role(det_state *s, det_split *sp, int role);
void det_split_set_seed_role(det_state *s, det_split *sp, int role, int64_t seed);

/* Det's client consumer pin (Det.PIN_*, lane/clientrand): while the oracle
 * runs, the torch flicker, a spawned client item's EntityItem constructor,
 * the particles' Math.random and Random and EffectRenderer.rand take their
 * values from their own streams, each seeded at the first draw of every
 * client tick t with Det.pinSeed(world seed, k, t) =
 * mix(mix(world seed, 40 + k), t); the shared client streams are still drawn
 * as before. The world seed is the tape header's seed (Oracle.seed). */
enum { DET_PIN_FLICKER = 0, DET_PIN_ITEM, DET_PIN_FX_MATH, DET_PIN_FX_SEED, DET_PIN_EFFECT, DET_PINS };
typedef struct {
    det_rng r;
    int k;
    int64_t seed, at;   /* the world seed; the tick the stream was seeded for */
    int fresh;          /* not seeded yet */
} det_pin;
int64_t det_pin_seed(int64_t world_seed, int k, int64_t t);
void det_pin_init(det_pin *p, int k, int64_t world_seed);
/* Consumer p's stream at client tick t (reseeded when t is not its tick). */
det_rng *det_pin_at(det_pin *p, int64_t t);
/* Det.PIN_SPAWNER (lane/idxclient): the spawner's display mob
 * (MobSpawnerBaseLogic.func_98281_h, made by the renderer) draws its
 * EntityLivingBase constructor's three Math.random from one stream reseeded
 * at each construction with det_pin_seed(world seed, 5, 0); the third is its
 * rotationYaw and rotationYawHead. This returns that yaw. */
enum { DET_PIN_SPAWNER = 5 };
float det_spawner_display_yaw(int64_t world_seed);

/* The digests, d.sseed / d.smath / d.sstat. split_state folds the used streams
 * of that role, order free, by name hash; an untouched stream carries nothing. */
uint64_t det_seeder_state(const det_state *s, int role);
uint64_t det_math_state(const det_state *s, int role);
uint64_t det_split_state(const det_state *s, int role);

/* Snapshots: the streams as Det.state reads them. det_load and det_split_add set
 * each role's 48-bit state directly (the value det_rng_state returns), they do
 * not go through set_seed: java.util.Random.setSeed scrambles its argument, so
 * setSeed(state) is not state. The pending Gaussian of a stream is not part of a
 * state and is cleared, which is what a Java snapshot does too. */
void det_load(det_state *s, int64_t world_seed, const uint64_t seeder[DET_ROLES], const uint64_t math[DET_ROLES],
              const int32_t next_id[DET_ROLES]);
det_split *det_split_add(det_state *s, const char *name, const uint64_t state[DET_ROLES], const uint8_t used[DET_ROLES]);

#endif