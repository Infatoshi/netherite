/* entity_spine.h - CPU==CUDA driver scenario for the EntityLivingBase tick spine (P2).
 *
 * One source (MC_HD) exercised identically on CPU and CUDA. Sets up a stone floor and four
 * living entities (pure drop, forward walker, walking jumper, ballistic drop), ticks them
 * through the living_base.h spine with fixed per-tick AI intents, and dumps every entity's
 * raw pos/motion/onGround bits. runner.py diffs the CPU vs CUDA hex line-for-line (no Java
 * golden - the spine's Java-faithfulness is proven by entity_trace_verify's zombie_drop).
 */
#ifndef MC_ENTITY_SPINE_H
#define MC_ENTITY_SPINE_H

#include "mc.h"
#include "living_base.h"

#define ES_NENT  4
#define ES_TICKS 32
#define ES_PER   7                 /* x,y,z,mx,my,mz,onGround per entity */
#define ES_OUT   (ES_NENT * ES_PER)

MC_HD static inline u64 es_dbits(double d) { union { double d; u64 u; } u; u.d = d; return u.u; }

/* 21x21 stone floor with its top surface at y=64 (block layer y=63). */
MC_HD static inline int es_build_floor(PcfBlock *b) {
    int n = 0, x, z;
    for (x = -10; x <= 10; ++x)
        for (z = -10; z <= 10; ++z) {
            b[n].block_id = 1; /* stone */
            b[n].ox = (double)x; b[n].oy = 63.0; b[n].oz = (double)z;
            b[n].ladder_facing = 0;
            ++n;
        }
    return n;
}

MC_HD static inline void es_run(u64 *out) {
    McSinTable st;
    mc_sin_table_init(&st);
    PcfBlock floor[512];
    int nb = es_build_floor(floor);
    EbLiving e[ES_NENT];
    int i, t;

    elb_init(&e[0], 0.6f, 1.95f,  0.5, 70.0,  0.5);   /* pure gravity drop */
    elb_init(&e[1], 0.6f, 1.95f,  0.5, 64.0,  0.5);   /* forward walker on the floor */
    elb_init(&e[2], 0.6f, 1.95f,  0.5, 64.0, -0.5);   /* walking jumper */
    elb_init(&e[3], 0.6f, 1.95f, -0.5, 68.0,  0.5);   /* ballistic drop (initial h-motion) */
    e[3].base.phys.motionX = 0.2;
    e[3].base.phys.motionZ = 0.1;

    for (t = 0; t < ES_TICKS; ++t) {
        /* fixed AI intents (the external updateEntityActionState slot) */
        e[1].moveForward = 1.0f;
        e[2].moveForward = 0.5f;
        e[2].isJumping   = 1;
        for (i = 0; i < ES_NENT; ++i)
            eb_tick_living(&e[i], 0.6f /*stone slip*/, 0 /*not blocked*/, floor, nb, &st);
    }

    for (i = 0; i < ES_NENT; ++i) {
        int k = i * ES_PER;
        out[k + 0] = es_dbits(e[i].base.phys.posX);
        out[k + 1] = es_dbits(e[i].base.phys.posY);
        out[k + 2] = es_dbits(e[i].base.phys.posZ);
        out[k + 3] = es_dbits(e[i].base.phys.motionX);
        out[k + 4] = es_dbits(e[i].base.phys.motionY);
        out[k + 5] = es_dbits(e[i].base.phys.motionZ);
        out[k + 6] = (u64)e[i].base.phys.onGround;
    }
}

#endif /* MC_ENTITY_SPINE_H */
