// Verbatim MC 1.11.2 vitals ground truth (vanilla), driven by the SAME exhaustion tape as
// core/player_vitals.h. Difficulty = NORMAL, naturalRegeneration = true.
// Sources (java/oracle-src/net/minecraft):
//   util/FoodStats.java             onUpdate, addExhaustion, foodLevel=20/saturation=5/timer.
//   entity/EntityLivingBase.java    heal (910-920), setHealth clamp (927-929),
//                                   fall -> ceil(dist-3-jumpBoost)*mult FALL damage (1389-1402).
//   entity/player/EntityPlayer.java shouldHeal = health>0 && health<max (2244-2246).
//   util/math/MathHelper.java       ceil (107-111).
// Starve floor on NORMAL = 1.0F (FoodStats.java:90-92). maxHealth = 20.
// No feather-fall and damageMultiplier=1.0F; amplifier -1 means no Jump Boost.
import java.util.Locale;

public class Golden {
    static final float MAX_HEALTH = 20.0F;

    // ---- vitals state (FoodStats + EntityLivingBase.health) ----
    static int   foodLevel  = 20;
    static float saturation = 5.0F;
    static float exhaustion = 0.0F;
    static int   foodTimer  = 0;
    static float health     = MAX_HEALTH;

    // FoodStats.addExhaustion (148-151)
    static void addExhaustion(float in) {
        exhaustion = Math.min(exhaustion + in, 40.0F);
    }

    // EntityPlayer.shouldHeal (2244-2246)
    static boolean shouldHeal() {
        return health > 0.0F && health < MAX_HEALTH;
    }

    // EntityLivingBase.setHealth (927-929): clamp(h, 0, maxHealth)
    static void setHealth(float h) {
        health = h < 0.0F ? 0.0F : (h > MAX_HEALTH ? MAX_HEALTH : h);
    }

    // EntityLivingBase.heal (910-920)
    static void heal(float amount) {
        if (amount <= 0.0F) return;
        float f = health;
        if (f > 0.0F) setHealth(f + amount);
    }

    // direct STARVE/FALL damage
    static void attack(float amount) {
        setHealth(health - amount);
    }

    // MathHelper.ceil(float) (107-111)
    static int ceil(float value) {
        int i = (int) value;
        return value > (float) i ? i + 1 : i;
    }

    // EntityLivingBase.fall (1389-1402), no feather-fall, damageMultiplier=1.0F
    static void fallDamage(float distance, int jumpBoostAmplifier) {
        float boost = jumpBoostAmplifier < 0
                ? 0.0F : (float) (jumpBoostAmplifier + 1);
        int i = ceil(distance - 3.0F - boost);
        if (i > 0) attack((float) i);
    }

    // FoodStats.onUpdate (40-102), NORMAL, naturalRegeneration=true
    static void onUpdate() {
        if (exhaustion > 4.0F) {
            exhaustion -= 4.0F;
            if (saturation > 0.0F) {
                saturation = Math.max(saturation - 1.0F, 0.0F);
            } else {                         // NORMAL != PEACEFUL
                foodLevel = Math.max(foodLevel - 1, 0);
            }
        }

        boolean flag = true;                 // naturalRegeneration
        if (flag && saturation > 0.0F && shouldHeal() && foodLevel >= 20) {
            ++foodTimer;
            if (foodTimer >= 10) {
                float f = Math.min(saturation, 6.0F);
                heal(f / 6.0F);
                addExhaustion(f);
                foodTimer = 0;
            }
        } else if (flag && foodLevel >= 18 && shouldHeal()) {
            ++foodTimer;
            if (foodTimer >= 80) {
                heal(1.0F);
                addExhaustion(6.0F);
                foodTimer = 0;
            }
        } else if (foodLevel <= 0) {
            ++foodTimer;
            if (foodTimer >= 80) {
                // NORMAL: health > 10 || (health > 1) -> floor 1.0F
                if (health > 10.0F || health > 1.0F) {
                    attack(1.0F);
                }
                foodTimer = 0;
            }
        } else {
            foodTimer = 0;
        }
    }

    // ---- deterministic tape (mirror of pv_hash / pv_tape_tick) ----
    static long hash(long x) {
        x += 0x9e3779b97f4a7c15L;
        x = (x ^ (x >>> 30)) * 0xbf58476d1ce4e5b9L;
        x = (x ^ (x >>> 27)) * 0x94d049bb133111ebL;
        x =  x ^ (x >>> 31);
        return x;
    }

    static float tapeExhaustion(long h) {
        switch ((int) (h & 3L)) {
            case 0:  return 0.0F;
            case 1:  return 0.05F;
            case 2:  return 0.1F;
            default: return 0.2F;
        }
    }

    static void tapeTick(long seed, int tick, int jumpBoostAmplifier) {
        long h = hash(seed * 0x100000001b3L + (tick & 0xffffffffL));
        addExhaustion(tapeExhaustion(h));
        if (((h >>> 8) & 255L) == 0L) {                          // ~1/256 ticks
            float distance = (float) (4L + ((h >>> 12) % 4L));   // 4..7 -> 1..4 dmg
            fallDamage(distance, jumpBoostAmplifier);
        }
        onUpdate();
    }

    public static void main(String[] args) {
        long seed  = args.length > 0 ? Long.parseLong(args[0]) : 1L;
        int  ticks = args.length > 1 ? Integer.parseInt(args[1]) : 400;
        int jumpBoostAmplifier = args.length > 2 ? Integer.parseInt(args[2]) : -1;
        StringBuilder sb = new StringBuilder();
        for (int t = 0; t < ticks; ++t) {
            tapeTick(seed, t, jumpBoostAmplifier);
            sb.append(String.format(Locale.ROOT, "%d %.6f %.6f %d %.6f%n",
                    foodLevel, saturation, exhaustion, foodTimer, health));
        }
        System.out.print(sb);
    }
}
