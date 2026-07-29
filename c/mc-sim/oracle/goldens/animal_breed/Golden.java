// Verbatim MC 1.11.2 animal age/love/breed TIMER ground truth (vanilla), driven by the SAME tape
// as core/animal_breed.h. No pathfinding / particles / XP.
// Sources (java/oracle-src/net/minecraft):
//   entity/EntityAgeable.java       growingAge onLivingUpdate server (210-226), setGrowingAge
//                                   (151-156), isChild (241-244), setScaleForAge (249-252),
//                                   baby age -24000 (49).
//   entity/passive/EntityAnimal.java inLove onLivingUpdate (44-65), setInLove=600 (194-199),
//                                   resetInLove (214-217), isInLove (209-212), adult feed (165-169),
//                                   canMateWith (222-225).
//   entity/ai/EntityAIMate.java     spawnBabyDelay>=60 && distSq<9 (82-85); parents age 6000 +
//                                   resetInLove + child -24000 (148-153).
public class Golden {
    static final int IN_LOVE_TICKS  = 600;
    static final int BREED_COOLDOWN = 6000;
    static final int CHILD_AGE      = -24000;
    static final int MATE_DELAY     = 60;
    static final float MATE_DIST_SQ = 9.0F;
    static final int SLOTS          = 3;

    static class Animal {
        boolean present;
        int growingAge;
        int inLove;
        float scale;
        float x, y, z;
    }

    static Animal[] a = new Animal[SLOTS];
    static int spawnBabyDelay;

    static boolean isChild(Animal e) {
        return e.growingAge < 0;
    }

    // EntityAgeable.setScaleForAge (249-252)
    static void setScaleForAge(Animal e, boolean child) {
        e.scale = child ? 0.5F : 1.0F;
    }

    // EntityAgeable.setGrowingAge (151-156)
    static void setGrowingAge(Animal e, int age) {
        e.growingAge = age;
        setScaleForAge(e, age < 0);
    }

    // EntityAgeable.onGrowingAdult (234-236) empty base
    static void onGrowingAdult(Animal e) { }

    // EntityAnimal.resetInLove (214-217)
    static void resetInLove(Animal e) {
        e.inLove = 0;
    }

    // EntityAnimal.isInLove (209-212)
    static boolean isInLove(Animal e) {
        return e.inLove > 0;
    }

    // EntityAnimal.setInLove (194-199)
    static void setInLove(Animal e) {
        e.inLove = IN_LOVE_TICKS;
    }

    // EntityAnimal.processInteract adult feed (165-169)
    static boolean tryFeed(Animal e) {
        if (!e.present) return false;
        if (e.growingAge == 0 && e.inLove <= 0) {
            setInLove(e);
            return true;
        }
        return false;
    }

    // EntityAgeable.onLivingUpdate server (210-226)
    static void ageTick(Animal e) {
        int i = e.growingAge;
        if (i < 0) {
            ++i;
            setGrowingAge(e, i);
            if (i == 0) onGrowingAdult(e);
        } else if (i > 0) {
            --i;
            setGrowingAge(e, i);
        }
    }

    // EntityAnimal.onLivingUpdate love half (48-65)
    static void loveTick(Animal e) {
        if (e.growingAge != 0) e.inLove = 0;
        if (e.inLove > 0) --e.inLove;
    }

    static void livingUpdate(Animal e) {
        if (!e.present) return;
        ageTick(e);
        loveTick(e);
    }

    static float distSq(Animal x, Animal y) {
        float dx = x.x - y.x;
        float dy = x.y - y.y;
        float dz = x.z - y.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // EntityAnimal.canMateWith (222-225)
    static boolean canMateWith(Animal self, Animal other) {
        if (self == other) return false;
        if (!self.present || !other.present) return false;
        return isInLove(self) && isInLove(other);
    }

    // EntityAIMate.spawnBaby (148-153)
    static void spawnBaby() {
        Animal p0 = a[0], p1 = a[1], ch = a[2];
        setGrowingAge(p0, BREED_COOLDOWN);
        setGrowingAge(p1, BREED_COOLDOWN);
        resetInLove(p0);
        resetInLove(p1);
        ch.present = true;
        ch.x = p0.x;
        ch.y = p0.y;
        ch.z = p0.z;
        ch.inLove = 0;
        setGrowingAge(ch, CHILD_AGE);
    }

    // EntityAIMate.updateTask mate gate (76-85), no navigator
    static void mateTick() {
        Animal p0 = a[0], p1 = a[1];
        if (!canMateWith(p0, p1)) {
            spawnBabyDelay = 0;
            return;
        }
        float dsq = distSq(p0, p1);
        if (dsq >= MATE_DIST_SQ) {
            spawnBabyDelay = 0;
            return;
        }
        ++spawnBabyDelay;
        if (spawnBabyDelay >= MATE_DELAY && dsq < MATE_DIST_SQ) {
            spawnBaby();
            spawnBabyDelay = 0;
        }
    }

    static void worldTick() {
        for (int i = 0; i < SLOTS; ++i) livingUpdate(a[i]);
        mateTick();
    }

    static void init() {
        for (int i = 0; i < SLOTS; ++i) {
            a[i] = new Animal();
            a[i].present = false;
            a[i].growingAge = 0;
            a[i].inLove = 0;
            a[i].scale = 1.0F;
            a[i].x = 0.0F;
            a[i].y = 64.0F;
            a[i].z = 0.0F;
        }
        spawnBabyDelay = 0;

        a[0].present = true;
        setGrowingAge(a[0], -40);
        a[0].x = 0.0F;
        a[0].y = 64.0F;
        a[0].z = 0.0F;

        a[1].present = true;
        setGrowingAge(a[1], 0);
        a[1].x = 1.0F;
        a[1].y = 64.0F;
        a[1].z = 0.0F;
    }

    // Mirror of ab_tape_tick: fixed feed schedule
    static void tapeTick(long seed, int tick) {
        if (tick == 5)  tryFeed(a[0]);
        if (tick == 50) tryFeed(a[0]);
        if (tick == 55) tryFeed(a[1]);
        worldTick();
    }

    public static void main(String[] args) {
        long seed  = args.length > 0 ? Long.parseLong(args[0]) : 1L;
        int  ticks = args.length > 1 ? Integer.parseInt(args[1]) : 200;
        init();
        StringBuilder sb = new StringBuilder();
        for (int t = 0; t < ticks; ++t) {
            tapeTick(seed, t);
            for (int i = 0; i < SLOTS; ++i) {
                if (i > 0) sb.append(' ');
                if (a[i].present) {
                    sb.append(a[i].growingAge).append(' ')
                      .append(a[i].inLove).append(' ')
                      .append(isChild(a[i]) ? 1 : 0);
                } else {
                    sb.append("0 0 0");
                }
            }
            sb.append('\n');
        }
        System.out.print(sb);
    }
}
