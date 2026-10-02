/* Minecraft 1.7.10 explosions, checked against the oracle's ExplosionProbe
 * dump: WorldServer.newExplosion's body (Explosion.doExplosionA and
 * doExplosionB(false), the call the server makes), over the native world and
 * entity list.
 *
 * The affected set is a java.util.HashSet of ChunkPosition, so the order
 * doExplosionB walks is the JDK 8 HashMap iteration order over
 * ChunkPosition.hashCode; jorder.h computes it. The rays draw from the
 * world's Random (the caller's jrand, continued in place), the fire rolls from
 * the explosion's own Det.newRandom, and the drops - Block's generic
 * dropBlockAsItemWithChance over the per-block table drops.c carries - draw
 * from both, with the Math.random stream kept in det (the drop engine draws
 * from a shadow jrand whose state is swapped in and out, so det's stream is
 * the one advanced).
 *
 * The duration runs under blockcb.h's blockcb_env (the same environment a
 * structure block half installs): the neighbor notification a destroyed
 * block's flag-3 write runs can pop a torch (BlockTorch.onNeighborBlockChange,
 * blockcb.c), whose drop draws from the same streams and whose entity lands
 * in the same list through the env's item_drop sink.
 *
 * Not ported here: an exploder other than null (every block then reads its
 * own resistance), TNT's canDropFromExplosion false (no TNT in the probe),
 * onBlockDestroyedByExplosion (only BlockTNT overrides it), the players'
 * knockback map, the S27PacketExplosion fan-out and the affected-position
 * clear WorldServer.newExplosion does for a non-smoking explosion (the probe
 * records the list before the clear; the packet is not part of the world).
 */
#ifndef NETHERITE_EXPLOSION_H
#define NETHERITE_EXPLOSION_H

struct living;

#include "det.h"
#include "particles_live.h"
#include "jrand.h"
#include "item_entity.h"
#include "jorder.h"
#include "world.h"

struct living;

/* The attenuation negative check: 1 drops the ray march's resistance step.
 * Only the report's "attenuation" mode sets it; the gates run it 0. */

/* Temporary per-case forensics (stderr); the gates leave it 0. */

/* An exploder that lives in the run's ie_world (a primed TNT): the entity
 * pass leaves it out, and a TNT block the blast destroys is primed with its
 * placer (Explosion.getExplosivePlacedBy: 1 the player, 0 none). The caller
 * sets both around the run; NULL and 0 otherwise. */

/* The parts of a finished explosion the test reads: the private rng's final
 * state, and the affected positions twice over - the HashSet iteration order
 * doExplosionB walks (jorder.h) and the order the rays added them, first
 * occurrence first, which the report's insertion-order negative check walks
 * instead. Both arrays are malloc'd; the caller frees them. */
struct expl_result {
    uint64_t rng_state;
    uint64_t wr_mid;        /* the world Random's state after doExplosionA */
    int32_t *affected;
    int n_affected;
    int32_t *affected_first;
    int n_first;
};

void expl_result_free(struct expl_result *r);

/* The damage pass reaches entities outside the run's ie_world when a caller
 * sets extras: one descriptor per outside entity, at its place in Java's
 * merged chunk-section list. attackEntityFrom(explosion) and the motion add
 * go through the callbacks; box, position and eye height are read directly. */
struct expl_extra {
    void *ent;                    /* the caller's tag, passed back untouched */
    const double *pos;            /* posX/Y/Z */
    float eye_height;             /* getEyeHeight() */
    const struct aabb *box;       /* boundingBox, the query and the density */
    void (*attack_from)(void *ent, float amount);
    void (*add_motion)(void *ent, double mx, double my, double mz);
    const struct living *armor;   /* whose getLastActiveItems func_92092_a reads, NULL for none */
};

/* doExplosionA's entity lists, too large for a frame: one per explosion in
 * progress, from the environment's scratch (explosion.c). */
struct an_ent;
#define EXPL_SCRATCH_DEPTH 8
#define EXPL_MAX_EXTRAS 256
/* the most positions one explosion's rays reach (a size-6 blast's sphere is
 * about 2,200), and the affected set's largest table */
#define EXPL_MAX_AFFECTED 16384
#define EXPL_MAX_TABLE (2 * EXPL_MAX_AFFECTED)
struct expl_frame {
    /* doExplosionA's HashSet of positions (jorder.h over this storage):
     * the rays' first adds in order (the set's keys), its membership index
     * and bin counts, and its iteration order */
    int32_t first[3 * EXPL_MAX_AFFECTED];
    int32_t set_index[2 * EXPL_MAX_TABLE];
    int32_t set_count[EXPL_MAX_TABLE];
    int32_t affected[3 * EXPL_MAX_AFFECTED];
    struct blast_ent hits[IE_MAX_ENTITIES];
    struct expl_extra extras[EXPL_MAX_EXTRAS];
    ie_ent *plain[IE_MAX_ENTITIES];
    struct an_ent *an_found[IE_MAX_ENTITIES];   /* the living world's blast query (living.c) */
};
struct expl_frame *expl_scratch_begin(void);
void expl_scratch_end(struct expl_frame **f);
#define EXPL_SCRATCH __attribute__((cleanup(expl_scratch_end)))

typedef int (*expl_extra_query)(void *ctx, struct aabb box, struct expl_extra *out, int cap);

/* An item-pool entity's attackEntityFrom from an explosion with no exploder
 * (an expl_extra attack_from for a pool the pass does not walk itself). */
void expl_attack_ie(void *ent, float amount);

/* One explosion: the body of WorldServer.newExplosion minus the packet work.
 * The world Random is *wr (seeded by the caller, as the probe does per case);
 * the fire rolls and every drop's constructor draws come from det's role
 * streams; spawned entities join iew. exploder is the Explosion's exploder, the
 * entity the blast leaves out of its entity pass and the attacker
 * DamageSource.setExplosionSource carries (NULL for an explosion with none).
 * out, when not NULL, gets the rng state and the two affected orders. The
 * extra_query, when not NULL, appends the caller's outside entities to the
 * damage pass after the ie_world's own. */
void expl_run_extras(struct world *w, det_state *det, int role, jrand *wr,
                     struct ie_world *iew, struct living *exploder,
                     double x, double y, double z, float size, int flaming, int smoking,
                     struct expl_result *out,
                     expl_extra_query extra_query, void *extra_ctx);

void expl_run(struct world *w, det_state *det, int role, jrand *wr,
              struct ie_world *iew, struct living *exploder,
              double x, double y, double z, float size, int flaming, int smoking,
              struct expl_result *out);

/* blockcb_env's item_drop sink for a run over an ie_world: the entity
 * Block.dropBlockAsItem_do built, into that pool's list with the pickup delay
 * and the draws the caller already spent. ctx is the ie_world. The arena
 * installs this one too, then takes the entities it appended into its own tick
 * list (hostiles_creeper.c's absorb), because World.spawnEntityInWorld appends
 * to the one loadedEntityList the probe's own list wraps. */
void expl_env_item_drop(void *ctx, int entity_id, uint64_t rand_state, int64_t uuid_msb, int64_t uuid_lsb,
                        double x, double y, double z, float yaw, float hover,
                        double motion_x, double motion_z, int item, int damage, int count);

/* blockcb_env's item_spill sink over an ie_world: a container's breakBlock
 * spill (spill.h), pickup delay 0. ctx is the ie_world. */
struct spill_item;
void expl_env_item_spill(void *ctx, const struct spill_item *s);

#endif