// Verbatim MC 1.11.2 combat damage math (vanilla ground truth). Sources:
//   net/minecraft/util/CombatRules.java
//   net/minecraft/util/math/MathHelper.java (clamp)
//   net/minecraft/enchantment/EnchantmentDamage.java (calcDamageByCreature type 0)
//   net/minecraft/enchantment/EnchantmentProtection.java (calcModifierDamage Type.ALL)
//   net/minecraft/entity/EntityLivingBase.java applyArmorCalculations + applyPotionDamageCalculations
// Matrix baked identically to core/combat_math.h. Output: float raw-bits hex (%08x) per cell.
public class Golden {

    // ---- verbatim MathHelper.clamp(float) ----
    static float clamp(float num, float min, float max) {
        return num < min ? min : (num > max ? max : num);
    }

    // ---- verbatim CombatRules.java ----
    static float getDamageAfterAbsorb(float damage, float totalArmor, float toughnessAttribute) {
        float f = 2.0F + toughnessAttribute / 4.0F;
        float f1 = clamp(totalArmor - damage / f, totalArmor * 0.2F, 20.0F);
        return damage * (1.0F - f1 / 25.0F);
    }

    static float getDamageAfterMagicAbsorb(float p_188401_0_, float p_188401_1_) {
        float f = clamp(p_188401_1_, 0.0F, 20.0F);
        return p_188401_0_ * (1.0F - f / 25.0F);
    }

    // ---- verbatim EnchantmentDamage.calcDamageByCreature (damageType==0) ----
    static float calcSharpnessBonus(int level) {
        return level <= 0 ? 0.0F : 1.0F + (float)Math.max(0, level - 1) * 0.5F;
    }

    // ---- verbatim EnchantmentProtection.calcModifierDamage (Type.ALL, generic melee) ----
    static int calcProtModifier(int level) {
        return level;
    }

    static float weaponRaw(int weaponIdx) {
        float[] toolDmg = { 0.0F, 0.0F, 1.0F, 2.0F, 3.0F, 3.0F, 3.0F };
        int[] sharp = { 0, 0, 0, 0, 0, 1, 5 };
        // EntityPlayer.applyEntityAttributes overrides ATTACK_DAMAGE to 1.0.
        float base = 1.0F;
        float sword = (weaponIdx == 0) ? 0.0F : (3.0F + toolDmg[weaponIdx]);
        return base + sword + calcSharpnessBonus(sharp[weaponIdx]);
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

    // verbatim EntityLivingBase.applyArmorCalculations + applyPotionDamageCalculations (no resistance)
    static float finalDamage(float raw, int armorIdx) {
        float damage = raw;
        damage = getDamageAfterAbsorb(damage, (float)armorPts(armorIdx), armorToughness(armorIdx));
        int k = protSum(armorIdx);
        if (k > 0)
            damage = getDamageAfterMagicAbsorb(damage, (float)k);
        return damage;
    }

    static void emitFloat(float v) {
        System.out.printf("%08x%n", Float.floatToRawIntBits(v));
    }

    public static void main(String[] args) {
        int sel = args.length > 0 ? Integer.parseInt(args[0]) : -1;
        int nWeapons = 7, nArmors = 6;
        if (sel >= 0) {
            float raw = weaponRaw(sel / nArmors);
            emitFloat(finalDamage(raw, sel % nArmors));
        } else {
            for (int w = 0; w < nWeapons; ++w) {
                float raw = weaponRaw(w);
                for (int a = 0; a < nArmors; ++a)
                    emitFloat(finalDamage(raw, a));
            }
        }
    }
}
