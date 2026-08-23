/* Magma-side explosion residual units. Same Java cites as blaze/env/test_explosions.c. */
#include "player_survival.h"
#include "entity_spine.h"
#include "mc_blocks.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "explosion_live.h"
#pragma GCC diagnostic pop

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

static int bits_eq_f(float a, float b) {
    uint32_t ua, ub;
    memcpy(&ua, &a, 4);
    memcpy(&ub, &b, 4);
    return ua == ub;
}

int main(void) {
    float dens, dmg;
    u16 grid[EX_VOL];
    int sc = 0;

    expect(bits_eq_f(exl_creeper_size(0), 3.0f),
           "unpowered creeper size is explosionRadius 3.0F");
    expect(bits_eq_f(exl_creeper_size(1), 6.0f),
           "powered creeper size is 3.0F * 2.0F (EntityCreeper.java:308-310)");
    expect(exl_creeper_on_struck_by_lightning(EW_TYPE_CREEPER, &sc) && sc == 1,
           "onStruckByLightning sets POWERED (EntityCreeper.java:274-277)");
    expect(exl_creeper_powered(EW_TYPE_CREEPER, sc),
           "screaming alias is powered on creeper");
    expect(!exl_creeper_powered(EW_TYPE_ENDERMAN, 1),
           "enderman screaming is not creeper powered");

    ex_fill(grid, mc_state(BLK_AIR, 0));
    dens = ex_block_density(grid, 0, 0, 0, 8.0, 8.0, 8.0,
                            7.7, 8.0, 7.7, 8.3, 9.8, 8.3);
    expect(bits_eq_f(dens, 1.0f), "air AABB getBlockDensity is 1.0F");

    ex_fill(grid, mc_state(BLK_STONE, 0));
    dens = ex_block_density(grid, 0, 0, 0, 8.0, 8.0, 8.0,
                            0.2, 0.2, 0.2, 0.8, 1.8, 0.8);
    expect(bits_eq_f(dens, 0.0f), "stone-occluded getBlockDensity is 0.0F");

    {
        float dens_slab, dens_stone, dens_water;
        u16 g2[EX_VOL];
        ex_fill(g2, mc_state(BLK_AIR, 0));
        ex_set(g2, 8, 8, 8, mc_state(BLK_STONE_SLAB, 0));
        dens_slab = ex_block_density(g2, 0, 0, 0, 8.5, 8.75, 8.5,
                                     10.2, 8.0, 8.2, 10.8, 9.8, 8.8);
        ex_set(g2, 8, 8, 8, mc_state(BLK_STONE, 0));
        dens_stone = ex_block_density(g2, 0, 0, 0, 8.5, 8.75, 8.5,
                                      10.2, 8.0, 8.2, 10.8, 9.8, 8.8);
        ex_fill(g2, mc_state(BLK_WATER, 0));
        dens_water = ex_block_density(g2, 0, 0, 0, 8.5, 8.75, 8.5,
                                      10.2, 8.0, 8.2, 10.8, 9.8, 8.8);
        expect(bits_eq_f(dens_water, 1.0f),
               "water stopOnLiquid false does not block density");
        expect(dens_slab > dens_stone,
               "bottom slab occludes less than a full cube");
        expect(dens_slab > 0.0f, "side rays can miss a bottom-slab AABB");
        expect(!ex_is_full_block(BLK_STONE_SLAB) && ex_is_full_block(BLK_STONE),
               "isFullBlock: stone yes, slab no");
    }

    {
        JavaRandom r;
        u64 before;
        int i, n = 0;
        jrand_set(&r, 1);
        before = r.seed;
        expect(!exl_flaming_place(&r, 0, 1),
               "isFlaming short-circuit: non-air skips nextInt");
        expect(r.seed == before, "non-air does not consume explosionRNG");
        jrand_set(&r, 0);
        for (i = 0; i < 12; ++i)
            if (exl_flaming_place(&r, 1, 1)) ++n;
        expect(n > 0 && n < 12, "explosionRNG nextInt(3)==0 sometimes places fire");
    }

    {
        IsrInv inv;
        ICStack chest;
        isr_init(&inv);
        expect(psv_blast_prot_max(&inv) == 0, "empty armor blast prot max is 0");
        expect(psv_explosion_enchant_mod(&inv) == 0,
               "empty armor explosion enchant mod is 0");
        expect(psv_explosion_after_magic(10.0f, 0) == 10.0f,
               "unenchanted magic absorb is identity");
        chest = ic_mk(307, 1, 0);
        chest.n_enchants = 1;
        chest.enchants[0].id = PSV_ENCH_BLAST;
        chest.enchants[0].level = 4;
        isr_set_stack(&inv, ISR_ARMOR0 + 2, chest);
        expect(psv_blast_prot_max(&inv) == 4, "getMaxEnchantmentLevel BLAST is 4");
        expect(psv_explosion_enchant_mod(&inv) == 8,
               "calcModifierDamage EXPLOSION is level*2");
        expect(ex_blast_reduction(1.0, 0) == 1.0,
               "EnchantmentProtection level 0 is identity");
        expect(ex_blast_reduction(10.0, 4) < 10.0,
               "getBlastDamageReduction level 4 reduces knockback");
    }

    dmg = ex_entity_damage(8.0, 8.0, 8.0, 8.0, 8.0, 8.0, EXL_RADIUS, 1.0f);
    expect(dmg > 0.0f, "center size-3 damage is positive");
    return fails ? 1 : 0;
}
