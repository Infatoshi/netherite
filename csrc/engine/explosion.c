#include "explosion.h"
#include "jmath.h"
#include "env.h"
#include "spill.h"
#include "blockcb.h"
#include "blocks.h"
#include "drops.h"
#include "entityquery.h"
#include "jorder.h"
#include "living.h"
#include "combatench.h"
#include "raytrace.h"
#include "trace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_TNT 46

#define H_16 16

/* MathHelper.sqrt_double: the square root rounded to float, widened back. */
static double sqrt_double(double v)
{
    return (double)(float)sqrt(v);
}

static int material_is_air(int id)
{
    return strcmp(MATERIALS[BLOCKS[id & 4095].material].name, "air") == 0;
}

/* The damaged entity's health path: EntityItem.attackEntityFrom and
 * EntityXPOrb.attackEntityFrom share the body the item pass already carries
 * (setBeenAttacked, health down by the amount, dead at or below 0). The
 * nether-star guard never applies: the probe's stack table does not carry
 * that item, and no other source reaches this path. */
static void attack_from_explosion(ie_ent *en, float amount)
{
    /* EntitySmallFireball.attackEntityFrom: false, nothing set */
    if (en->kind == IE_SMALL_FIREBALL) return;

    en->e.velocity_changed = 1; /* setBeenAttacked */

    /* EntityFireball.attackEntityFrom: setBeenAttacked and, the explosion's
     * source carrying no entity (newExplosion gets null), nothing else - no
     * health change. A primed TNT, an arrow and a throwable keep
     * Entity.attackEntityFrom (setBeenAttacked, false): only items and orbs
     * have health */
    if (en->kind != IE_ITEM && en->kind != IE_ORB) return;

    en->health = (int)((float)en->health - amount);

    if (en->health <= 0) en->is_dead = 1;
}

void expl_attack_ie(void *ent, float amount)
{
    attack_from_explosion(ent, amount);
}

/* World.getBlockDensity(center, box), over raytrace.c. */
static float block_density(struct world *w, double cx, double cy, double cz, struct aabb box)
{
    double var3 = 1.0 / ((box.max_x - box.min_x) * 2.0 + 1.0);
    double var5 = 1.0 / ((box.max_y - box.min_y) * 2.0 + 1.0);
    double var7 = 1.0 / ((box.max_z - box.min_z) * 2.0 + 1.0);

    if (!(var3 >= 0.0 && var5 >= 0.0 && var7 >= 0.0)) return 0.0F;

    int var9 = 0, var10 = 0;

    for (float var11 = 0.0F; var11 <= 1.0F; var11 = (float)((double)var11 + var3))
    {
        for (float var12 = 0.0F; var12 <= 1.0F; var12 = (float)((double)var12 + var5))
        {
            for (float var13 = 0.0F; var13 <= 1.0F; var13 = (float)((double)var13 + var7))
            {
                double var14 = box.min_x + (box.max_x - box.min_x) * (double)var11;
                double var16 = box.min_y + (box.max_y - box.min_y) * (double)var12;
                double var18 = box.min_z + (box.max_z - box.min_z) * (double)var13;
                struct rt_mop hit;

                /* rayTraceBlocks(sample, center) = func_147447_a(false, false, false) */
                if (!raytrace_blocks(w, var14, var16, var18, cx, cy, cz, 0, 0, 0, &hit)) ++var9;

                ++var10;
            }
        }
    }

    return (float)var9 / (float)var10;
}

/* One live drop: Block.dropBlockAsItemWithChance over drops.c's table, with
 * the Math.random stream swapped into a shadow jrand for the call (the drop
 * engine draws that stream from a pointer; det's stream is what it mirrors),
 * then the recorded entities built the way their constructors built them -
 * the same four math draws each, the same seeder draws for the per-entity
 * Random and UUID and the same entity id. The caller passes the streams
 * explicitly; blockcb.c's callback drops go through blockcb_env instead. */
static int live_drop(struct world *w, det_state *det, int role, jrand *wr,
                     struct ie_world *iew, int block, int meta,
                     int x, int y, int z, float chance)
{
    jrand shadow;
    shadow.seed = det->math[role].r.seed;

    struct drop_case cs;
    cs.block = block;
    cs.meta = meta;
    cs.fortune = 0;
    cs.x = x;
    cs.y = y;
    cs.z = z;
    cs.opseed = 0;
    cs.math = &shadow;

    struct drop_ent *ents ENV_LOCAL = envstack_take(DROPS_MAX_ENTS * sizeof *ents);
    int n = drops_break_live(&cs, wr, chance, ents, DROPS_MAX_ENTS);

    det->math[role].r.seed = shadow.seed;

    for (int i = 0; i < n; ++i)
    {
        ie_ent *en;

        if (nw_env->cfg.explosion_debug)
            fprintf(stderr, "  drop %d block %d at (%d,%d,%d) kind %d item %d\n", i, block, x, y, z, ents[i].kind,
                    ents[i].item);

        if (ents[i].kind == DROP_ITEM)
        {
            en = ie_spawn_item_state(iew, &ents[i]);
            if (en) en->delay = 10; /* dropBlockAsItem_do's delayBeforeCanPickup */
        }
        else
        {
            en = ie_spawn_orb_state(iew, &ents[i]);
        }

        if (en) ie_added_to_world(iew, en);
    }

    return n;
}

/* blockcb_env's item_drop sink for the run: the entity dropBlockAsItem_do
 * built, into the explosion's entity list with the pickup delay. The
 * construction draws are all spent before the sink runs - blockcb.c's
 * drop_item_stack drew the per-entity Random (passed as rand_state), the
 * UUID and the four Math.random values - so the sink draws nothing and the
 * entity takes them as drawn. */
void expl_env_item_drop(void *ctx, int entity_id, uint64_t rand_state, int64_t uuid_msb, int64_t uuid_lsb,
                        double x, double y, double z, float yaw, float hover,
                        double motion_x, double motion_z, int item, int damage, int count)
{
    struct ie_world *iew = ctx;

    /* the Entity boilerplate as the caller drew it: the id, the per-entity
     * Random and the UUID (a blast's neighbour drop, tall grass losing its
     * dirt, is a whole-server entity like the blast's own) */
    ie_ent *en = ie_adopt_item(iew, entity_id, uuid_msb, uuid_lsb, rand_state, x, y, z,
                               motion_x, 0.20000000298023224, motion_z, yaw, hover, item, damage, count, 0);

    if (en == NULL) return;
    en->delay = 10; /* dropBlockAsItem_do's delayBeforeCanPickup */
    ie_added_to_world(iew, en);
}

/* blockcb_env's item_spill sink: a container the blast replaced spills its
 * inventory from breakBlock (spill.c) into the run's entity list, the draws
 * all spent, delayBeforeCanPickup left at the constructor's 0 and the motion
 * the spill's Gaussians gave it. */
void expl_env_item_spill(void *ctx, const struct spill_item *s)
{
    struct ie_world *iew = ctx;

    ie_ent *en = ie_adopt_item(iew, s->entity_id, s->uuid_msb, s->uuid_lsb, s->rand_state, s->x, s->y, s->z,
                               s->mx, s->my, s->mz, s->yaw, s->hover, s->item, s->damage, s->count, s->tag);

    if (en == NULL) return;

    en->delay = 0;
    ie_added_to_world(iew, en);
}

void expl_result_free(struct expl_result *out)
{
    if (!out) return;
    free(out->affected);
    free(out->affected_first);
    out->affected = out->affected_first = NULL;
    out->n_affected = out->n_first = 0;
}

struct expl_frame *expl_scratch_begin(void)
{
    if (nw_scratch->expl_depth == EXPL_SCRATCH_DEPTH) abort();
    return &nw_scratch->expl[nw_scratch->expl_depth++];
}

void expl_scratch_end(struct expl_frame **f)
{
    (void)f;
    --nw_scratch->expl_depth;
}

void expl_run_extras(struct world *w, det_state *det, int role, jrand *wr,
                     struct ie_world *iew, struct living *exploder,
                     double x, double y, double z, float size, int flaming, int smoking,
                     struct expl_result *out,
                     expl_extra_query extra_query, void *extra_ctx)
{
    /* ------------------------------------------------------ doExplosionA */
    /* this explosion's frame of the environment's scratch */
    struct expl_frame *fr EXPL_SCRATCH = expl_scratch_begin();
    float var1 = size;
    struct jord_set3 set;
    jord_set3_init(&set, fr->first, EXPL_MAX_AFFECTED, fr->set_index, 2 * EXPL_MAX_TABLE, fr->set_count,
                   EXPL_MAX_TABLE);

    det_rng explosion_rng = det_new_random_role(det, role); /* the ctor's Det.newRandom */

    /* the insertion order the negative check walks: the rays' first adds,
     * which are the set's keys */
    int32_t *first_out = fr->first;

    for (int var3 = 0; var3 < H_16; ++var3)
    {
        for (int var4 = 0; var4 < H_16; ++var4)
        {
            for (int var5 = 0; var5 < H_16; ++var5)
            {
                if (var3 == 0 || var3 == H_16 - 1 || var4 == 0 || var4 == H_16 - 1 || var5 == 0 || var5 == H_16 - 1)
                {
                    double var6 = (double)((float)var3 / ((float)H_16 - 1.0F) * 2.0F - 1.0F);
                    double var8 = (double)((float)var4 / ((float)H_16 - 1.0F) * 2.0F - 1.0F);
                    double var10 = (double)((float)var5 / ((float)H_16 - 1.0F) * 2.0F - 1.0F);
                    double var12 = sqrt(var6 * var6 + var8 * var8 + var10 * var10);
                    var6 /= var12;
                    var8 /= var12;
                    var10 /= var12;
                    float var14 = size * (0.7F + jr_float(wr) * 0.6F);
                    double var15 = x, var17 = y, var19 = z;

                    for (float var21 = 0.3F; var14 > 0.0F; var14 -= var21 * 0.75F)
                    {
                        int var22 = mh_floor(var15);
                        int var23 = mh_floor(var17);
                        int var24 = mh_floor(var19);
                        int var25 = world_get_block(w, var22, var23, var24) & 4095;

                        if (!material_is_air(var25))
                        {
                            /* the exploder is always null: the block's own
                             * resistance, blockResistance / 5.0F. The lane's
                             * attenuation negative check drops the step. */
                            if (!nw_env->cfg.explosion_skip_attenuation)
                            {
                                float var26 = BLOCKS[var25].resistance / 5.0F;
                                var14 -= (var26 + 0.3F) * var21;
                            }
                        }

                        if (var14 > 0.0F)
                        {
                            jord_set3_add(&set, var22, var23, var24);
                        }

                        var15 += var6 * (double)var21;
                        var17 += var8 * (double)var21;
                        var19 += var10 * (double)var21;
                    }
                }
            }
        }
    }

    int32_t *affected = fr->affected;
    int n_first = set.n;
    int n_affected = jord_set3_order(&set, affected);

    float doubled = var1 * 2.0F;
    uint64_t wr_mid = wr->seed;

    int var3e = mh_floor(x - (double)doubled - 1.0);
    int var4e = mh_floor(x + (double)doubled + 1.0);
    int var5e = mh_floor(y - (double)doubled - 1.0);
    int var29e = mh_floor(y + (double)doubled + 1.0);
    int var7e = mh_floor(z - (double)doubled - 1.0);
    int var30e = mh_floor(z + (double)doubled + 1.0);
    struct aabb box = aabb_make(var3e, var5e, var7e, var4e, var29e, var30e);

    /* The entity pass walks one list: this pool's chunk sections, or - when the
     * pool is only the item half of a bigger world - the owner's answer
     * (an_world.c's hook, in World.loadedEntityList order, items and livings
     * together). */
    struct blast_ent *hits = fr->hits;
    int nhits = 0;
    struct expl_extra *extras = fr->extras;
    int n_extras = 0;

    if (extra_query != NULL)
    {
        ie_ent **plain = fr->plain;
        int found = entityquery_in_box(iew, box, NULL, plain, IE_MAX_ENTITIES);

        nhits = 0;
        for (int i = 0; i < found && i < IE_MAX_ENTITIES; ++i)
        {
            if (plain[i] == expl_exclude_ie) continue;
            hits[nhits].e = &plain[i]->e;
            hits[nhits].ie = plain[i];
            hits[nhits].liver = NULL;
            ++nhits;
        }
        n_extras = extra_query(extra_ctx, box, extras, EXPL_MAX_EXTRAS);
    }
    else if (iew->blast_entities != NULL)
    {
        int found = iew->blast_entities(iew, box, exploder ? &exploder->e : NULL, hits, IE_MAX_ENTITIES);
        nhits = found < IE_MAX_ENTITIES ? found : IE_MAX_ENTITIES;
    }
    else
    {
        ie_ent **plain = fr->plain;
        int found = entityquery_in_box(iew, box, NULL, plain, IE_MAX_ENTITIES);
        nhits = found < IE_MAX_ENTITIES ? found : IE_MAX_ENTITIES;

        for (int i = 0; i < nhits; ++i)
        {
            hits[i].e = &plain[i]->e;
            hits[i].ie = plain[i];
            hits[i].liver = NULL;
        }
    }

    for (int i = 0; i < nhits + n_extras; ++i)
    {
        int is_extra = (i >= nhits);
        const struct expl_extra *ex = is_extra ? &extras[i - nhits] : NULL;
        struct entity *hite = is_extra ? NULL : hits[i].e;
        struct living *liver = is_extra ? NULL : hits[i].liver;

        double eye = is_extra ? (double)ex->eye_height : (liver != NULL ? (double)living_eye_height(liver) : 0.0);
        double px = is_extra ? ex->pos[0] : hite->pos_x;
        double py = is_extra ? ex->pos[1] : hite->pos_y;
        double pz = is_extra ? ex->pos[2] : hite->pos_z;

        double var13 = sqrt_double((px - x) * (px - x) +
                                   (py - y) * (py - y) +
                                   (pz - z) * (pz - z)) / (double)doubled;

        if (var13 <= 1.0)
        {
            double var15 = px - x;
            double var17 = py + eye - y;
            double var19 = pz - z;
            double var33 = sqrt_double(var15 * var15 + var17 * var17 + var19 * var19);

            if (var33 != 0.0)
            {
                var15 /= var33;
                var17 /= var33;
                var19 /= var33;
                struct aabb hbox = is_extra ? *ex->box : hite->bounding_box;
                double var34 = (double)block_density(w, x, y, z, hbox);
                double var35 = (1.0 - var13) * var34;
                float amount = (float)(int)((var35 * var35 + var35) / 2.0 * 8.0 * (double)doubled + 1.0);

                if (is_extra)
                {
                    ex->attack_from(ex->ent, amount);
                }
                else if (liver != NULL)
                {
                    /* DamageSource.setExplosionSource(explosion): the
                     * "explosion.player" source when the explosion has an
                     * exploder, whose entity is the attacker the damage and the
                     * revenge target read */
                    living_attack_entity_from_attacker(liver, exploder, DMG_EXPLOSION, amount, det);
                }
                else
                {
                    attack_from_explosion(hits[i].ie, amount);
                }

                /* EnchantmentProtection.func_92092_a: Blast Protection on
                 * the entity's last active items (a living's; items, orbs
                 * and projectiles have none) shortens the push. The
                 * player's field_77288_k entry, the S27's knockback, keeps
                 * the unshortened var35 */
                const struct living *armor = is_extra ? ex->armor : liver;
                double var27 = ench_blast_protection(armor, var35);
                double mx = var15 * var27;
                double my = var17 * var27;
                double mz = var19 * var27;
                nw_env->explosion.raw_push[0] = var15 * var35;
                nw_env->explosion.raw_push[1] = var17 * var35;
                nw_env->explosion.raw_push[2] = var19 * var35;
                if (liver != NULL && liver->player_sp != NULL)
                {
                    /* the player twin's push reaches the S27 through its
                     * motion (the living pass's blast_pending): the part
                     * the armour took off goes back into the S27 */
                    nw_env->explosion.player_gap[0] += var15 * (var35 - var27);
                    nw_env->explosion.player_gap[1] += var17 * (var35 - var27);
                    nw_env->explosion.player_gap[2] += var19 * (var35 - var27);
                }

                if (is_extra)
                {
                    ex->add_motion(ex->ent, mx, my, mz);
                }
                else
                {
                    hite->motion_x += mx;
                    hite->motion_y += my;
                    hite->motion_z += mz;
                }
            }
        }
    }

    /* The duration runs under blockcb.h's blockcb_env, the one environment a
     * structure block half installs too: the callbacks the setBlock fan-out
     * reaches (a torch losing its wall, fire's onBlockAdded, the lava-water
     * fizz) draw from this run's streams and drop into this run's entity
     * list. */
    struct blockcb_env saved_env = nw_env->blockcb.env;
    nw_env->blockcb.env.world_rand = wr;
    nw_env->blockcb.env.det = det;
    nw_env->blockcb.env.item_drop = expl_env_item_drop;
    nw_env->blockcb.env.ctx = iew;
    nw_env->blockcb.env.item_spill = expl_env_item_spill;
    nw_env->blockcb.env.spill_role = role;

    /* ------------------------------------------------------ doExplosionB */
    (void)jr_float(wr); /* the sound's pitch: two draws */
    (void)jr_float(wr);

    int nxp = 0;
    const int *outer_listed = nw_env->explosion.listed;
    int outer_nlisted = nw_env->explosion.nlisted;
    if (smoking)
    {
        nw_env->explosion.listed = affected;
        nw_env->explosion.nlisted = n_affected;
        for (int i = 0; i < n_affected; ++i)
        {
            int var4b = affected[i * 3], var5b = affected[i * 3 + 1], var6b = affected[i * 3 + 2];
            int var7b = world_get_block(w, var4b, var5b, var6b) & 4095;

            /* the S27's client runs this list over the blocks as they stand
             * before the blast: the ones whose drop draws there too */
            if (nxp < S27_XP_MAX && (var7b == 16 || var7b == 21 || var7b == 56 || var7b == 73 || var7b == 74 ||
                                     var7b == 129 || var7b == 153 || var7b == 52))
                nw_env->explosion.client_xp[nxp++] = (struct s27_xp){i, var7b};

            if (nw_env->cfg.explosion_debug) fprintf(stderr, "  destroy (%d,%d,%d) id %d\n", var4b, var5b, var6b, var7b);

            if (!material_is_air(var7b))
            {
                /* canDropFromExplosion: Block's true, BlockTNT's false */
                float chance = 1.0F / var1;
                if (var7b != ID_TNT)
                    live_drop(w, det, role, wr, iew, var7b, world_get_meta(w, var4b, var5b, var6b), var4b, var5b,
                              var6b, chance);

                if (nw_env->cfg.explosion_debug)
                    fprintf(stderr, "  destroy (%d,%d,%d) id %d\n", var4b, var5b, var6b, var7b);

                world_set_block(w, var4b, var5b, var6b, 0, 0, 3);

                /* onBlockDestroyedByExplosion: Block's body is empty;
                 * BlockTNT primes a short-fused TNT with the blast's placer */
                if (var7b == ID_TNT)
                {
                    ie_ent *tnt = ie_spawn_tnt(iew, (double)((float)var4b + 0.5F), (double)((float)var5b + 0.5F),
                                               (double)((float)var6b + 0.5F), expl_placed_by_player);

                    if (tnt != NULL)
                    {
                        /* getExplosivePlacedBy: the TNT's placer, or the
                         * living exploder (a creeper) */
                        if (!expl_placed_by_player)
                            tnt->tnt_placer = exploder != NULL ? lv_ref(exploder) : expl_placer;
                        tnt->fuse = jr_int_n(wr, tnt->fuse / 4) + tnt->fuse / 8;
                        ie_added_to_world(iew, tnt);
                    }
                }
            }
        }
    }

    nw_env->explosion.listed = outer_listed;
    nw_env->explosion.nlisted = outer_nlisted;

    if (flaming)
    {
        for (int i = 0; i < n_affected; ++i)
        {
            int var4c = affected[i * 3], var5c = affected[i * 3 + 1], var6c = affected[i * 3 + 2];
            int var7c = world_get_block(w, var4c, var5c, var6c) & 4095;
            int var24 = world_get_block(w, var4c, var5c - 1, var6c) & 4095;

            /* var24.func_149730_j(): the block's opaque field, which the
             * constructor set from the virtual isOpaqueCube(); blocks.h
             * carries that value as opaque_cube. */
            if (material_is_air(var7c) && BLOCKS[var24 & 4095].opaque_cube && det_rng_int_n(&explosion_rng, 3) == 0)
            {
                world_set_block(w, var4c, var5c, var6c, 51, 0, 3);
            }
        }
    }

    nw_env->blockcb.env = saved_env;

    if (nw_env->explosion.on_done)
        nw_env->explosion.on_done(nw_env->explosion.on_done_ctx, w, x, y, z, size, smoking ? n_affected : 0,
                                  nw_env->explosion.client_xp, nxp);

    if (out)
    {
        /* the probe keeps the lists past the frame: copies it frees */
        out->rng_state = det_rng_state(&explosion_rng);
        out->wr_mid = wr_mid;
        out->affected = malloc((size_t)(n_affected ? n_affected : 1) * 3 * sizeof(int32_t));
        memcpy(out->affected, affected, (size_t)n_affected * 3 * sizeof(int32_t));
        out->n_affected = n_affected;
        out->affected_first = malloc((size_t)(n_first ? n_first : 1) * 3 * sizeof(int32_t));
        memcpy(out->affected_first, first_out, (size_t)n_first * 3 * sizeof(int32_t));
        out->n_first = n_first;
    }
}

/* The signature master calls: no outside entities. */
void expl_run(struct world *w, det_state *det, int role, jrand *wr,
              struct ie_world *iew, struct living *exploder,
              double x, double y, double z, float size, int flaming, int smoking,
              struct expl_result *out)
{
    expl_run_extras(w, det, role, wr, iew, exploder, x, y, z, size, flaming, smoking, out, NULL, NULL);
}
