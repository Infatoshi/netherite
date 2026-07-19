// Verbatim MC 1.11.2 EnchantmentProtection type matrix (vanilla ground truth). Sources:
//   net/minecraft/enchantment/EnchantmentProtection.java
//   net/minecraft/util/CombatRules.java (integration rows)
// Matrix baked identically to core/enchant_protection_full.h. Output: raw-bits hex (%08x) per cell.
public class Golden {

    static final int TYPE_ALL = 0;
    static final int TYPE_FIRE = 1;
    static final int TYPE_FALL = 2;
    static final int TYPE_EXPLOSION = 3;
    static final int TYPE_PROJECTILE = 4;

    static final int DS_GENERIC = 0;
    static final int DS_FIRE = 1;
    static final int DS_FALL = 2;
    static final int DS_EXPLOSION = 3;
    static final int DS_PROJECTILE = 4;
    static final int DS_CREATIVE = 5;

    static final int NUM_TYPES = 5;
    static final int NUM_LEVELS = 4;
    static final int NUM_DS = 6;

    static float clamp(float num, float min, float max) {
        return num < min ? min : (num > max ? max : num);
    }

    static float getDamageAfterAbsorb(float damage, float totalArmor, float toughnessAttribute) {
        float f = 2.0F + toughnessAttribute / 4.0F;
        float f1 = clamp(totalArmor - damage / f, totalArmor * 0.2F, 20.0F);
        return damage * (1.0F - f1 / 25.0F);
    }

    static float getDamageAfterMagicAbsorb(float damage, float protPts) {
        float f = clamp(protPts, 0.0F, 20.0F);
        return damage * (1.0F - f / 25.0F);
    }

    static int typeMinEnchantability(int protType) {
        int[] min = { 1, 10, 5, 5, 3 };
        return min[protType];
    }

    static int typeLevelCost(int protType) {
        int[] cost = { 11, 8, 6, 8, 6 };
        return cost[protType];
    }

    static int getMinEnchantability(int protType, int level) {
        return typeMinEnchantability(protType) + (level - 1) * typeLevelCost(protType);
    }

    static int getMaxEnchantability(int protType, int level) {
        return getMinEnchantability(protType, level) + typeLevelCost(protType);
    }

    static int getMaxLevel(int protType) {
        return 4;
    }

    // verbatim EnchantmentProtection.calcModifierDamage (DamageSource subset)
    static int calcModifierDamage(int protType, int level, int dsScenario) {
        if (dsScenario == DS_CREATIVE)
            return 0;
        if (protType == TYPE_ALL)
            return level;
        if (protType == TYPE_FIRE && dsScenario == DS_FIRE)
            return level * 2;
        if (protType == TYPE_FALL && dsScenario == DS_FALL)
            return level * 3;
        if (protType == TYPE_EXPLOSION && dsScenario == DS_EXPLOSION)
            return level * 2;
        if (protType == TYPE_PROJECTILE && dsScenario == DS_PROJECTILE)
            return level * 2;
        return 0;
    }

    static int canApplyTogether(int typeA, int typeB) {
        if (typeA == typeB)
            return 0;
        if (typeA == TYPE_FALL || typeB == TYPE_FALL)
            return 1;
        return 0;
    }

    static int protCalcModifier(int level, int protType, int dsFlags) {
        if (level <= 0) return 0;
        if (protType == TYPE_ALL) return level;
        if (protType == TYPE_FIRE && (dsFlags & 2) != 0) return level * 2;
        if (protType == TYPE_FALL && (dsFlags & 4) != 0) return level * 3;
        if (protType == TYPE_EXPLOSION && (dsFlags & 8) != 0) return level * 2;
        if (protType == TYPE_PROJECTILE && (dsFlags & 16) != 0) return level * 2;
        return 0;
    }

    static int enchantProtModifier(int protAllChest, int protFireBoots, int dsFlags) {
        int sum = 0;
        sum += protCalcModifier(protAllChest, TYPE_ALL, dsFlags);
        sum += protCalcModifier(protFireBoots, TYPE_FIRE, dsFlags);
        return sum;
    }

    static float weaponRaw(int weaponIdx) {
        float[] toolDmg = { 0.0F, 0.0F, 1.0F, 2.0F, 3.0F, 3.0F, 3.0F };
        float base = 2.0F;
        float sword = (weaponIdx == 0) ? 0.0F : (3.0F + toolDmg[weaponIdx]);
        return base + sword;
    }

    static int armorPts(int armorIdx) {
        int[] ap = { 0, 7, 12, 15, 20, 20 };
        return ap[armorIdx];
    }

    static float armorToughness(int armorIdx) {
        float[] th = { 0.0F, 0.0F, 0.0F, 0.0F, 8.0F, 8.0F };
        return th[armorIdx];
    }

    static int protSum(int armorIdx) {
        int[] pr = { 0, 0, 0, 0, 0, 16 };
        return pr[armorIdx];
    }

    static float finalDamage(float raw, int armorIdx) {
        float damage = raw;
        damage = getDamageAfterAbsorb(damage, (float)armorPts(armorIdx), armorToughness(armorIdx));
        int k = protSum(armorIdx);
        if (k > 0)
            damage = getDamageAfterMagicAbsorb(damage, (float)k);
        return damage;
    }

    static int getFireTimeForEntity(int fireTime, int fireProtLevel) {
        if (fireProtLevel > 0) {
            fireTime -= (int)Math.floor((float)fireTime * (float)fireProtLevel * 0.15F);
        }
        return fireTime;
    }

    static double getBlastDamageReduction(double damage, int blastProtLevel) {
        if (blastProtLevel > 0) {
            damage -= (double)Math.floor(damage * (double)((float)blastProtLevel * 0.15F));
        }
        return damage;
    }

    static void emitU32(int v) {
        System.out.printf("%08x%n", v);
    }

    static void emitFloat(float v) {
        emitU32(Float.floatToRawIntBits(v));
    }

    public static void main(String[] args) {
        int t, l, d;

        for (t = 0; t < NUM_TYPES; ++t)
            for (l = 1; l <= NUM_LEVELS; ++l)
                for (d = 0; d < NUM_DS; ++d)
                    emitU32(calcModifierDamage(t, l, d));

        for (t = 0; t < NUM_TYPES; ++t)
            for (l = 1; l <= NUM_LEVELS; ++l)
                emitU32(getMinEnchantability(t, l));

        for (t = 0; t < NUM_TYPES; ++t)
            for (l = 1; l <= NUM_LEVELS; ++l)
                emitU32(getMaxEnchantability(t, l));

        for (t = 0; t < NUM_TYPES; ++t)
            emitU32(getMaxLevel(t));

        for (t = 0; t < NUM_TYPES; ++t)
            for (d = 0; d < NUM_TYPES; ++d)
                emitU32(canApplyTogether(t, d));

        int modGen = enchantProtModifier(4, 2, 1);
        int modFire = enchantProtModifier(4, 2, 2);
        emitU32(modGen);
        emitU32(modFire);
        emitFloat(getDamageAfterMagicAbsorb(10.0F, (float)modFire));
        emitFloat(finalDamage(weaponRaw(4), 5));
        emitU32(getFireTimeForEntity(100, 4));
        emitFloat((float)getBlastDamageReduction(20.0, 4));
    }
}
