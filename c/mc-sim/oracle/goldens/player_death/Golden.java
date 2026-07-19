// Verbatim MC 1.11.2 DEATH + natural RESPAWN ground truth (vanilla), driven by the SAME tape as
// core/player_death.h. Difficulty = NORMAL, keepInventory = FALSE, natural respawn (no bed).
// This is player_vitals' Golden (FoodStats.onUpdate + heal/setHealth/fall) EXTENDED with the death
// state machine; the vitals numbers are copied verbatim and unchanged.
// Sources (java/oracle-src/net/minecraft):
//   entity/EntityLivingBase.java  onUpdate death gate getHealth()<=0 -> onDeathUpdate (358-360);
//                                   onDeathUpdate ++deathTime, deathTime==20 (419-423);
//                                   onDeath this.dead=true (1224-1242); ctor setHealth(getMaxHealth())
//                                   (200); setHealth clamp (927-929); heal (910-920); fall (1389-1402).
//   util/FoodStats.java           new FoodStats defaults foodLevel=20 (14) / saturation=5.0 (16) /
//                                   exhaustion=0 (18) / foodTimer=0 (20); NORMAL starve floor 1.0 (90-92).
//   entity/player/EntityPlayer.java shouldHeal (2244-2246).
//   server/management/PlayerList.java recreatePlayerEntity new EntityPlayerMP on respawn (552).
//   util/math/MathHelper.java     ceil (107-111).
import java.util.Locale;

public class Golden {
    static final float MAX_HEALTH = 20.0F;
    static final int   DEATH_TIME = 20;

    // ---- vitals state (FoodStats + EntityLivingBase.health) ----
    static int   foodLevel  = 20;
    static float saturation = 5.0F;
    static float exhaustion = 0.0F;
    static int   foodTimer  = 0;
    static float health     = MAX_HEALTH;

    // ---- death state machine (EntityLivingBase.dead / deathTime + deathCount) ----
    static int dead      = 0;
    static int deaths    = 0;
    static int deathTime = 0;

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

    // EntityLivingBase.fall (1389-1402), no jump-boost/feather-fall, damageMultiplier=1.0F
    static void fallDamage(float distance) {
        int i = ceil(distance - 3.0F);
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

    // pd_respawn: new EntityPlayerMP ctor setHealth(maxHealth) (EntityLivingBase:200) + fresh
    // FoodStats defaults (FoodStats:14/16/18/20). Identical to the initial vitals. deaths preserved.
    static void respawn() {
        foodLevel  = 20;
        saturation = 5.0F;
        exhaustion = 0.0F;
        foodTimer  = 0;
        health     = MAX_HEALTH;
        dead       = 0;
        deathTime  = 0;
    }

    // death gate: onUpdate getHealth()<=0 -> onDeathUpdate (358); onDeath dead=true (1242)
    static void checkDeath() {
        if (dead == 0 && health <= 0.0F) {
            dead       = 1;
            deaths    += 1;
            deathTime  = 0;
        }
    }

    // ---- deterministic death tape (mirror of pd_alive_tick / pd_tape_tick) ----
    static void aliveTick(int tick) {
        if ((deaths & 1) != 0) {
            fallDamage(30.0F);           // scenario B: clean lethal fall at full health/food
            onUpdate();
            return;
        }
        addExhaustion(8.0F);             // scenario A: drain food fast, then finish while starving
        if (foodLevel <= 0 && health <= 17.0F) {
            fallDamage(30.0F);
        }
        onUpdate();
    }

    static void tapeTick(long seed, int tick) {
        if (dead == 0) {
            aliveTick(tick);
            checkDeath();
        } else {
            deathTime += 1;              // onDeathUpdate ++deathTime
            if (deathTime >= DEATH_TIME) respawn();
        }
    }

    public static void main(String[] args) {
        long seed  = args.length > 0 ? Long.parseLong(args[0]) : 1L;
        int  ticks = args.length > 1 ? Integer.parseInt(args[1]) : 700;
        StringBuilder sb = new StringBuilder();
        for (int t = 0; t < ticks; ++t) {
            tapeTick(seed, t);
            sb.append(String.format(Locale.ROOT, "%d %.6f %.6f %d %.6f %d %d %d%n",
                    foodLevel, saturation, exhaustion, foodTimer, health, dead, deaths, deathTime));
        }
        System.out.print(sb);
    }
}
