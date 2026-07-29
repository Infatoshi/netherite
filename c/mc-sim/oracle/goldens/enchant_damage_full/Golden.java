// Verbatim MC 1.11.2 EnchantmentDamage type matrix (vanilla ground truth). Sources:
//   net/minecraft/enchantment/EnchantmentDamage.java
//   net/minecraft/util/CombatRules.java (integration rows)
//   net/minecraft/util/math/MathHelper.java (clamp)
// Matrix baked identically to core/enchant_damage_full.h. Output: raw-bits hex (%08x) per cell.
public class Golden {

    static final int TYPE_SHARPNESS = 0;
    static final int TYPE_SMITE = 1;
    static final int TYPE_BANE = 2;

    static final int CREATURE_UNDEFINED = 0;
    static final int CREATURE_UNDEAD = 1;
    static final int CREATURE_ARTHROPOD = 2;
    static final int CREATURE_ILLAGER = 3;

    static final int NUM_TYPES = 3;
    static final int NUM_LEVELS = 5;
    static final int NUM_CREATURES = 4;

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

    // verbatim EnchantmentDamage.calcDamageByCreature
    static float calcDamageByCreature(int damageType, int level, int creature) {
        return damageType == TYPE_SHARPNESS
            ? (level <= 0 ? 0.0F : 1.0F + (float)Math.max(0, level - 1) * 0.5F)
            : (damageType == TYPE_SMITE && creature == CREATURE_UNDEAD ? (float)level * 2.5F
                : (damageType == TYPE_BANE && creature == CREATURE_ARTHROPOD ? (float)level * 2.5F : 0.0F));
    }

    static int baseEnchantability(int damageType) {
        int[] base = { 1, 5, 5 };
        return base[damageType];
    }

    static int levelEnchantability(int damageType) {
        int[] lvl = { 11, 8, 8 };
        return lvl[damageType];
    }

    static int thresholdEnchantability(int damageType) {
        int[] th = { 20, 20, 20 };
        return th[damageType];
    }

    static int getMinEnchantability(int damageType, int level) {
        return baseEnchantability(damageType) + (level - 1) * levelEnchantability(damageType);
    }

    static int getMaxEnchantability(int damageType, int level) {
        return getMinEnchantability(damageType, level) + thresholdEnchantability(damageType);
    }

    static int getMaxLevel(int damageType) {
        return 5;
    }

    static int canApplyTogether(int typeA, int typeB) {
        return 0;
    }

    static float weaponRawEnchant(float toolDamage, int damageType, int level, int creature) {
        return 2.0F + 3.0F + toolDamage + calcDamageByCreature(damageType, level, creature);
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

    static void emitU32(int v) {
        System.out.printf("%08x%n", v);
    }

    static void emitFloat(float v) {
        emitU32(Float.floatToRawIntBits(v));
    }

    public static void main(String[] args) {
        int t, l, c;

        for (t = 0; t < NUM_TYPES; ++t)
            for (l = 1; l <= NUM_LEVELS; ++l)
                for (c = 0; c < NUM_CREATURES; ++c)
                    emitFloat(calcDamageByCreature(t, l, c));

        for (t = 0; t < NUM_TYPES; ++t)
            for (l = 1; l <= NUM_LEVELS; ++l)
                emitU32(getMinEnchantability(t, l));

        for (t = 0; t < NUM_TYPES; ++t)
            for (l = 1; l <= NUM_LEVELS; ++l)
                emitU32(getMaxEnchantability(t, l));

        for (t = 0; t < NUM_TYPES; ++t)
            emitU32(getMaxLevel(t));

        for (t = 0; t < NUM_TYPES; ++t)
            for (c = 0; c < NUM_TYPES; ++c)
                emitU32(canApplyTogether(t, c));

        emitFloat(weaponRawEnchant(2.0F, TYPE_SMITE, 5, CREATURE_UNDEAD));
        emitFloat(weaponRawEnchant(2.0F, TYPE_SMITE, 5, CREATURE_UNDEFINED));
        emitFloat(weaponRawEnchant(2.0F, TYPE_BANE, 5, CREATURE_ARTHROPOD));
        emitFloat(weaponRawEnchant(2.0F, TYPE_BANE, 5, CREATURE_UNDEAD));
        emitFloat(finalDamage(weaponRawEnchant(3.0F, TYPE_SHARPNESS, 5, CREATURE_UNDEFINED), 4));
        emitFloat(finalDamage(weaponRawEnchant(2.0F, TYPE_SMITE, 5, CREATURE_UNDEAD), 3));
    }
}
