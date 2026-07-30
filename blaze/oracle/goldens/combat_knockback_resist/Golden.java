// Verbatim MC 1.11.2 knockBack + setBeenAttacked + CombatRules toughness battery.
// Sources: EntityLivingBase.java, CombatRules.java, MathHelper.sqrt(double).
// Output format matches cpu/combat_knockback_resist.c (%016x raw bits per line).
public class Golden {

    static float clamp(float num, float min, float max) {
        return num < min ? min : (num > max ? max : num);
    }

    static float getDamageAfterAbsorb(float damage, float totalArmor, float toughnessAttribute) {
        float f = 2.0F + toughnessAttribute / 4.0F;
        float f1 = clamp(totalArmor - damage / f, totalArmor * 0.2F, 20.0F);
        return damage * (1.0F - f1 / 25.0F);
    }

    static float sqrtD(double v) {
        return (float)Math.sqrt(v);
    }

    static class Entity {
        double motionX, motionY, motionZ;
        boolean onGround;
    }

    static class KbScenario {
        float strength;
        double xRatio, zRatio, kbResist, rngDraw;
        double motionX, motionY, motionZ;
        boolean onGround;
        KbScenario(float st, double xr, double zr, double kr, double rng,
                   double mx, double my, double mz, boolean og) {
            strength = st; xRatio = xr; zRatio = zr; kbResist = kr; rngDraw = rng;
            motionX = mx; motionY = my; motionZ = mz; onGround = og;
        }
    }

    static class VcScenario {
        double kbResist, rngDraw;
        VcScenario(double kr, double rng) { kbResist = kr; rngDraw = rng; }
    }

    static class ToughScenario {
        float damage, armor, toughness;
        ToughScenario(float d, float a, float t) { damage = d; armor = a; toughness = t; }
    }

    static int knockBack(Entity e, float strength, double xRatio, double zRatio,
                         double kbResist, double rngDraw) {
        if (rngDraw < kbResist)
            return 0;
        float f = sqrtD(xRatio * xRatio + zRatio * zRatio);
        e.motionX /= 2.0D;
        e.motionZ /= 2.0D;
        e.motionX -= xRatio / (double)f * (double)strength;
        e.motionZ -= zRatio / (double)f * (double)strength;
        if (e.onGround) {
            e.motionY /= 2.0D;
            e.motionY += (double)strength;
            if (e.motionY > 0.4000000059604645D)
                e.motionY = 0.4000000059604645D;
        }
        return 1;
    }

    static int velocityChanged(double kbResist, double rngDraw) {
        return rngDraw >= kbResist ? 1 : 0;
    }

    static void emitU32(int v) {
        System.out.printf("%016x%n", (long)(v & 0xFFFFFFFFL));
    }

    static void emitF32(float v) {
        System.out.printf("%016x%n", (long)(Float.floatToRawIntBits(v) & 0xFFFFFFFFL));
    }

    static void emitF64(double v) {
        System.out.printf("%016x%n", Double.doubleToRawLongBits(v));
    }

    static int armorPointsIron() { return 2 + 6 + 5 + 2; }
    static float armorPointsDiamond() { return 3 + 8 + 6 + 3; }
    static float armorToughnessDiamond() { return 8.0F; }

    public static void main(String[] args) {
        KbScenario[] kb = {
            new KbScenario(0.5f, 1.0, 0.0, 0.0, 0.25, 0.0, 0.0, 0.0, true),
            new KbScenario(0.5f, 1.0, 0.0, 1.0, 0.99, 0.0, 0.0, 0.0, true),
            new KbScenario(0.4f, 0.0, 1.0, 0.5, 0.5, 1.0, 0.2, 0.1, true),
            new KbScenario(0.4f, 0.0, 1.0, 0.5, 0.49999999999999994, 1.0, 0.2, 0.1, true),
            new KbScenario(1.0f, 1.0, 1.0, 0.0, 0.1, 0.5, 0.3, 0.0, false),
            new KbScenario(1.0f, 1.0, 0.0, 0.0, 0.0, 0.0, 0.35, 0.0, true),
            new KbScenario(0.5f, -1.0, 0.0, 0.0, 0.0, 0.0, 0.9, 0.0, true),
            new KbScenario(0.8f, 3.0, 4.0, 0.0, 0.0, 0.0, 0.0, 0.0, true),
            new KbScenario(0.6f, -2.0, -1.0, 0.25, 0.8, 0.1, 0.0, 0.1, true),
            new KbScenario(0.5f, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, true),
            new KbScenario(1.5f, 0.0, -1.0, 0.0, 0.5, -0.2, 0.0, 0.4, true),
            new KbScenario(0.4f, 1.0, 0.0, 0.2, 0.15, 0.0, 0.0, 0.0, false)
        };

        VcScenario[] vc = {
            new VcScenario(0.0, 0.0),
            new VcScenario(0.0, 0.9999999999999999),
            new VcScenario(1.0, 0.5),
            new VcScenario(0.5, 0.5),
            new VcScenario(0.5, 0.49999999999999994),
            new VcScenario(0.75, 0.75)
        };

        ToughScenario[] tough = {
            new ToughScenario(0.0f, 20.0f, 8.0f),
            new ToughScenario(1.0f, 0.0f, 0.0f),
            new ToughScenario(20.0f, 0.0f, 0.0f),
            new ToughScenario(20.0f, 15.0f, 0.0f),
            new ToughScenario(20.0f, 20.0f, 0.0f),
            new ToughScenario(20.0f, 20.0f, 8.0f),
            new ToughScenario(100.0f, 20.0f, 0.0f),
            new ToughScenario(100.0f, 20.0f, 8.0f),
            new ToughScenario(50.0f, 5.0f, 0.0f),
            new ToughScenario(200.0f, 20.0f, 8.0f),
            new ToughScenario(10.0f, 20.0f, 20.0f),
            new ToughScenario(4.0f, 20.0f, 8.0f),
            new ToughScenario(80.0f, 20.0f, 8.0f),
            new ToughScenario(1.0f, 20.0f, 8.0f),
            new ToughScenario(999.0f, 20.0f, 8.0f),
            new ToughScenario(6.6666665f, 12.0f, 3.0f),
            new ToughScenario(3.1415927f, 7.0f, 2.0f),
            new ToughScenario(0.5f, 1.0f, 0.0f)
        };

        for (KbScenario s : kb) {
            Entity e = new Entity();
            e.motionX = s.motionX; e.motionY = s.motionY; e.motionZ = s.motionZ;
            e.onGround = s.onGround;
            int applied = knockBack(e, s.strength, s.xRatio, s.zRatio, s.kbResist, s.rngDraw);
            emitU32(applied);
            emitF64(e.motionX);
            emitF64(e.motionY);
            emitF64(e.motionZ);
        }

        for (VcScenario s : vc)
            emitU32(velocityChanged(s.kbResist, s.rngDraw));

        for (ToughScenario s : tough)
            emitF32(getDamageAfterAbsorb(s.damage, s.armor, s.toughness));

        emitF32(getDamageAfterAbsorb(80.0f, (float)armorPointsIron(), 0.0f));
        emitF32(getDamageAfterAbsorb(80.0f, armorPointsDiamond(), armorToughnessDiamond()));
        emitU32((int)armorPointsDiamond());
    }
}
