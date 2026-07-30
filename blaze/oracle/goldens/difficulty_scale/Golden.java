// Verbatim MC 1.11.2 difficulty damage scaling ground truth. Eval-pure: no game launch.
//
// Logic copied VERBATIM from the decompiled oracle:
//   net/minecraft/entity/player/EntityPlayer.java   attackEntityFrom difficultyScaled (1100-1118)
//   net/minecraft/world/EnumDifficulty.java         PEACEFUL=0 EASY=1 NORMAL=2 HARD=3
//   net/minecraft/world/DifficultyInstance.java     calculateAdditionalDifficulty + clamped
//   net/minecraft/util/math/MathHelper.java          clamp(float)
//   net/minecraft/util/FoodStats.java                starve gate (90-92)
//   net/minecraft/potion/Potion.java                 HUNGER exhaustion + POISON isReady
//
// Battery constants + emit order match core/difficulty_scale.h. Output: %016x (u32 zero-extended).
public class Golden {
    static final int PEACEFUL = 0, EASY = 1, NORMAL = 2, HARD = 3;

    // ---- MathHelper.clamp(float) ----
    static float clamp(float num, float min, float max) {
        return num < min ? min : (num > max ? max : num);
    }

    // EntityPlayer.attackEntityFrom when source.isDifficultyScaled().
    // regionalDifficulty is not used by the vanilla player path (API parity only).
    static float scalePlayerDamage(float rawDamage, int difficultyId, boolean isDifficultyScaled,
                                   float regionalDifficulty) {
        float amount = rawDamage;
        if (!isDifficultyScaled) return amount;
        if (difficultyId == PEACEFUL) amount = 0.0F;
        if (difficultyId == EASY) amount = Math.min(amount / 2.0F + 1.0F, amount);
        if (difficultyId == HARD) amount = amount * 3.0F / 2.0F;
        return amount;
    }

    static int damageApplies(float scaledAmount) {
        return scaledAmount == 0.0F ? 0 : 1;
    }

    // DifficultyInstance.calculateAdditionalDifficulty VERBATIM.
    static float additionalDifficulty(int difficultyId, long worldTime, long chunkInhabitedTime,
                                      float moonPhaseFactor) {
        if (difficultyId == PEACEFUL) return 0.0F;
        boolean flag = difficultyId == HARD;
        float f = 0.75F;
        float f1 = clamp(((float)worldTime + -72000.0F) / 1440000.0F, 0.0F, 1.0F) * 0.25F;
        f = f + f1;
        float f2 = 0.0F;
        f2 = f2 + clamp((float)chunkInhabitedTime / 3600000.0F, 0.0F, 1.0F) * (flag ? 1.0F : 0.75F);
        f2 = f2 + clamp(moonPhaseFactor * 0.25F, 0.0F, f1);
        if (difficultyId == EASY) f2 *= 0.5F;
        f = f + f2;
        return (float)difficultyId * f;
    }

    static float clampedAdditionalDifficulty(float additional) {
        return additional < 2.0F ? 0.0F
             : (additional > 4.0F ? 1.0F : (additional - 2.0F) / 2.0F);
    }

    // FoodStats starve condition.
    static int starveApplies(float health, int difficultyId) {
        return (health > 10.0F
                || difficultyId == HARD
                || (health > 1.0F && difficultyId == NORMAL)) ? 1 : 0;
    }

    // Potion HUNGER performEffect exhaustion.
    static float hungerExhaustion(int amplifier) {
        return 0.005F * (float)(amplifier + 1);
    }

    // Potion POISON isReady.
    static int poisonIsReady(int duration, int amplifier) {
        int j = 25 >> amplifier;
        return j > 0 ? (duration % j == 0 ? 1 : 0) : 1;
    }

    // ---- battery tables (core/difficulty_scale.h) ----
    static final float[] RAW = {
        0.0F, 0.5F, 1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 8.0F, 10.0F, 15.0F, 20.0F, 50.0F
    };
    static final float[] STARVE_HP = {
        20.0F, 11.0F, 10.0F, 9.0F, 2.0F, 1.0F, 0.5F, 0.0F
    };
    static final int[] POISON_DUR = { 0, 1, 12, 25, 26, 50, 75, 100, 125, 200 };

    // {difficultyId, worldTime, inhabited, moon}
    static final Object[][] REGIONAL = {
        {0, 0L, 0L, 0.0F},
        {0, 24000L * 100, 3600000L, 1.0F},
        {1, 0L, 0L, 0.0F},
        {1, 72000L, 0L, 0.0F},
        {1, 1440000L, 0L, 0.5F},
        {1, 2880000L, 3600000L, 1.0F},
        {2, 0L, 0L, 0.0F},
        {2, 72000L, 0L, 0.0F},
        {2, 720000L, 1800000L, 0.25F},
        {2, 1440000L, 3600000L, 1.0F},
        {2, 2880000L, 7200000L, 1.0F},
        {3, 0L, 0L, 0.0F},
        {3, 72000L, 0L, 0.0F},
        {3, 720000L, 900000L, 0.5F},
        {3, 1440000L, 3600000L, 1.0F},
        {3, 2880000L, 7200000L, 1.0F}
    };

    static void emit(StringBuilder sb, long u32) {
        sb.append(String.format("%016x", u32 & 0xFFFFFFFFL)).append('\n');
    }
    static void emitF(StringBuilder sb, float v) { emit(sb, Float.floatToRawIntBits(v)); }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();

        for (int ri = 0; ri < RAW.length; ++ri) {
            float raw = RAW[ri];
            for (int di = 0; di < 4; ++di) {
                for (int si = 0; si < 2; ++si) {
                    float scaled = scalePlayerDamage(raw, di, si != 0, 0.0F);
                    emitF(sb, scaled);
                    emit(sb, damageApplies(scaled));
                }
            }
        }

        for (int i = 0; i < REGIONAL.length; ++i) {
            Object[] s = REGIONAL[i];
            float add = additionalDifficulty(
                    (Integer)s[0], (Long)s[1], (Long)s[2], (Float)s[3]);
            emitF(sb, add);
            emitF(sb, clampedAdditionalDifficulty(add));
        }

        for (int hi = 0; hi < STARVE_HP.length; ++hi) {
            float hp = STARVE_HP[hi];
            for (int di = 0; di < 4; ++di)
                emit(sb, starveApplies(hp, di));
        }

        for (int ai = 0; ai < 5; ++ai)
            emitF(sb, hungerExhaustion(ai));

        for (int pi = 0; pi < POISON_DUR.length; ++pi) {
            int dur = POISON_DUR[pi];
            for (int ai = 0; ai < 4; ++ai)
                emit(sb, poisonIsReady(dur, ai));
        }

        System.out.print(sb.toString());
    }
}
