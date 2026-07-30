// Verbatim MC 1.11.2 potion/absorption/fire combat damage ground truth. Eval-pure: no game launch.
//
// Logic copied VERBATIM from the decompiled oracle:
//   net/minecraft/entity/EntityLivingBase.java  applyPotionDamageCalculations(), damageEntity()
//                                               (absorption + setHealth/setAbsorptionAmount),
//                                               attackEntityFrom() (fire + FIRE_RESISTANCE block)
//   net/minecraft/util/CombatRules.java          getDamageAfterAbsorb(), getDamageAfterMagicAbsorb()
//   net/minecraft/util/math/MathHelper.java      clamp(float)
// Armor sets (ARMOR / ARMOR_TOUGHNESS / Protection sums) baked identically to core/combat_math.h
// mc_combat_armor_set (leather/chain/iron/diamond + diamond+ProtIV), reused from goldens/combat_math.
//
// CUT (matches core/potion_effects_combat.h): Forge onLivingHurt / ISpecialArmor, shields, i-frames,
// combat tracker, anvil helmet 0.75x, EntityPlayer.damageEntity variant, night-vision. The battery
// injects prot_sum / resistance / absorption directly (eval-pure). Output format matches
// cpu/potion_effects_combat.c: %016llx (u32 zero-extended), same field/scenario order.
public class Golden {
    // ---- MathHelper.clamp(float) ----
    static float clamp(float num, float min, float max) {
        return num < min ? min : (num > max ? max : num);
    }
    // ---- CombatRules ----
    static float getDamageAfterAbsorb(float damage, float totalArmor, float toughness) {
        float f = 2.0F + toughness / 4.0F;
        float f1 = clamp(totalArmor - damage / f, totalArmor * 0.2F, 20.0F);
        return damage * (1.0F - f1 / 25.0F);
    }
    static float getDamageAfterMagicAbsorb(float damage, float protPts) {
        float f = clamp(protPts, 0.0F, 20.0F);
        return damage * (1.0F - f / 25.0F);
    }

    // ---- armor sets (combat_math.h mc_combat_armor_set) ----
    static final int[]   ARMOR_PTS  = { 0, 7, 12, 15, 20, 20 };
    static final float[] ARMOR_TGH  = { 0.0F, 0.0F, 0.0F, 0.0F, 8.0F, 8.0F };
    static final int[]   ARMOR_PROT = { 0, 0, 0, 0, 0, 16 };
    static final int NUM_ARMOR = 6;

    // EntityLivingBase.applyArmorCalculations (blockable source): CombatRules.getDamageAfterAbsorb.
    static float applyArmor(float raw, int armorPts, float toughness, boolean unblockable) {
        if (unblockable) return raw;
        return getDamageAfterAbsorb(raw, (float) armorPts, toughness);
    }

    // EntityLivingBase.applyPotionDamageCalculations VERBATIM (prot_sum / resistance injected).
    static float applyPotionDamageCalcs(boolean isAbsolute, boolean isOutOfWorld,
                                        boolean resistanceActive, int resistanceAmp,
                                        int protSum, float damage) {
        if (isAbsolute) return damage;
        if (resistanceActive && !isOutOfWorld) {
            int i = (resistanceAmp + 1) * 5;
            int j = 25 - i;
            float f = damage * (float) j;
            damage = f / 25.0F;
        }
        if (damage <= 0.0F) return 0.0F;
        if (protSum > 0) damage = getDamageAfterMagicAbsorb(damage, (float) protSum);
        return damage;
    }

    // EntityLivingBase.attackEntityFrom: fire damage + FIRE_RESISTANCE -> blocked (return false).
    static int fireResistBlocks(boolean isFire, boolean fireResistActive) {
        return (isFire && fireResistActive) ? 0 : 1;
    }

    // EntityLivingBase.damageEntity absorption + health VERBATIM (post armor+potion). setHealth
    // and setAbsorptionAmount clamp to their floors.
    static float[] damageEntityAbsorb(float potionDamage, float absorption, float health) {
        float f = potionDamage;                                     // pre-absorb damage
        float dmg = Math.max(potionDamage - absorption, 0.0F);      // Math.max(dmg - absorb, 0)
        absorption = absorption - (f - dmg);                        // setAbsorptionAmount(...)
        if (absorption < 0.0F) absorption = 0.0F;
        if (dmg != 0.0F) {
            health = health - dmg;                                  // setHealth clamps [0,max]; max unreached
            if (health < 0.0F) health = 0.0F;
            absorption = absorption - dmg;                          // setAbsorptionAmount(absorb - dmg)
            if (absorption < 0.0F) absorption = 0.0F;
        }
        return new float[] { dmg, absorption, health };             // healthDamage, absorption, health
    }

    // pec_run_full: armor -> potion -> absorb.
    static float[] runFull(float raw, int armorIdx, boolean resistanceActive, int resistanceAmp,
                           float absorption, float health, boolean isAbsolute, boolean isOutOfWorld) {
        float afterArmor = applyArmor(raw, ARMOR_PTS[armorIdx], ARMOR_TGH[armorIdx], false);
        float afterPotion = applyPotionDamageCalcs(isAbsolute, isOutOfWorld, resistanceActive,
                resistanceAmp, ARMOR_PROT[armorIdx], afterArmor);
        float[] ah = damageEntityAbsorb(afterPotion, absorption, health);
        // returns potionOut, healthDamage, absorptionOut, healthOut
        return new float[] { afterPotion, ah[0], ah[1], ah[2] };
    }

    // ---- battery constants (core/potion_effects_combat.h) ----
    static final int NUM_RAW = 6, NUM_RESIST = 5, NUM_POTION_EDGE = 12, NUM_FIRE = 8, NUM_ABSORB = 10;
    static final float[] RAW = { 1.0F, 5.0F, 10.0F, 20.0F, 50.0F, 100.0F };
    static final int[] RESIST_AMP = { -1, 0, 1, 2, 4 };

    // pec_potion_edge_scenario: {is_absolute, is_out_of_world, resistance_active, resistance_amp,
    //                            prot_sum, damage_in}
    static final int[][] POTION_EDGE = {
        {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 1, 1, 0}, {0, 0, 1, 4, 0},
        {0, 1, 1, 0, 0}, {1, 0, 1, 4, 0}, {0, 0, 1, 2, 16}, {0, 0, 0, 0, 16},
        {0, 0, 1, 0, 16}, {0, 0, 1, 4, 16}, {0, 0, 1, 0, 0}, {0, 0, 1, 4, 0},
    };
    static final float[] POTION_EDGE_DMG = {
        10.0F, 10.0F, 10.0F, 100.0F, 20.0F, 50.0F, 30.0F, 30.0F, 5.0F, 200.0F, 0.0F, 1.0F
    };

    // pec_absorb_scenario: {raw, armor_idx, resistance_active, resistance_amp, absorption, health,
    //                       is_absolute, is_out_of_world}
    static final float[][] ABSORB = {
        {10.0F, 3, 0, 0, 0.0F, 20.0F, 0, 0}, {10.0F, 3, 0, 0, 4.0F, 20.0F, 0, 0},
        {10.0F, 3, 0, 0, 10.0F, 20.0F, 0, 0}, {3.0F, 0, 0, 0, 4.0F, 20.0F, 0, 0},
        {4.0F, 0, 0, 0, 4.0F, 20.0F, 0, 0}, {10.0F, 4, 1, 1, 8.0F, 20.0F, 0, 0},
        {80.0F, 4, 0, 0, 16.0F, 20.0F, 0, 0}, {50.0F, 5, 1, 2, 10.0F, 20.0F, 0, 0},
        {20.0F, 2, 1, 0, 0.0F, 20.0F, 0, 0}, {100.0F, 0, 0, 0, 20.0F, 20.0F, 1, 0},
    };

    static void emit(StringBuilder sb, long u32) {
        sb.append(String.format("%016x", u32 & 0xFFFFFFFFL)).append('\n');
    }
    static void emitF(StringBuilder sb, float v) { emit(sb, Float.floatToRawIntBits(v)); }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();

        for (int i = 0; i < NUM_POTION_EDGE; ++i) {
            int[] s = POTION_EDGE[i];
            emitF(sb, applyPotionDamageCalcs(s[0] != 0, s[1] != 0, s[2] != 0, s[3], s[4],
                    POTION_EDGE_DMG[i]));
        }

        for (int i = 0; i < NUM_FIRE; ++i) {
            boolean isFire = (i & 1) != 0;
            boolean fireResist = ((i >> 1) & 1) != 0;
            emit(sb, fireResistBlocks(isFire, fireResist));
        }

        for (int rawI = 0; rawI < NUM_RAW; ++rawI)
            for (int ai = 0; ai < NUM_ARMOR; ++ai)
                for (int ri = 0; ri < NUM_RESIST; ++ri) {
                    float afterArmor = applyArmor(RAW[rawI], ARMOR_PTS[ai], ARMOR_TGH[ai], false);
                    int amp = RESIST_AMP[ri];
                    boolean resistOn = amp >= 0;
                    int useAmp = amp >= 0 ? amp : 0;
                    emitF(sb, applyPotionDamageCalcs(false, false, resistOn, useAmp,
                            ARMOR_PROT[ai], afterArmor));
                }

        for (int i = 0; i < NUM_ABSORB; ++i) {
            float[] s = ABSORB[i];
            float[] r = runFull(s[0], (int) s[1], s[2] != 0, (int) s[3], s[4], s[5],
                    s[6] != 0, s[7] != 0);
            emitF(sb, r[0]);  // potion_out
            emitF(sb, r[1]);  // health_dmg
            emitF(sb, r[2]);  // absorption_out
            emitF(sb, r[3]);  // health_out
        }

        System.out.print(sb);
    }
}
