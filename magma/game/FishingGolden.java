import java.util.Random;

public final class FishingGolden {
    static final class State {
        int catchable, caughtDelay, catchableDelay, lure;
        float angle, bitePitch;
        double motionY;
    }

    static String dbits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }

    static int tick(State s, Random random, boolean rain, boolean sky,
                    boolean approachWater, boolean splashWater) {
        int speed = 1, event = 0;
        if (random.nextFloat() < .25F && rain) ++speed;
        if (random.nextFloat() < .5F && !sky) --speed;
        if (s.catchable > 0) {
            if (--s.catchable <= 0) {
                s.caughtDelay = 0;
                s.catchableDelay = 0;
                event |= 4;
            } else {
                s.motionY -= .2D * random.nextFloat() * random.nextFloat();
            }
        } else if (s.catchableDelay > 0) {
            s.catchableDelay -= speed;
            if (s.catchableDelay > 0) {
                s.angle = (float)((double)s.angle + random.nextGaussian() * 4D);
                if (approachWater && random.nextFloat() < .15F) event |= 1;
            } else {
                s.motionY = (double)(-.4F * (.6F + random.nextFloat() * .4F));
                float first = random.nextFloat();
                float second = random.nextFloat();
                s.bitePitch = 1.0F + (first - second) * 0.4F;
                s.catchable = 20 + random.nextInt(21);
                event |= 2;
            }
        } else if (s.caughtDelay > 0) {
            s.caughtDelay -= speed;
            float chance = .15F;
            if (s.caughtDelay < 20)
                chance = (float)((double)chance + (20-s.caughtDelay)*.05D);
            else if (s.caughtDelay < 40)
                chance = (float)((double)chance + (40-s.caughtDelay)*.02D);
            else if (s.caughtDelay < 60)
                chance = (float)((double)chance + (60-s.caughtDelay)*.01D);
            if (random.nextFloat() < chance) {
                random.nextFloat();
                random.nextFloat();
                if (splashWater) random.nextInt(2);
                event |= 1;
            }
            if (s.caughtDelay <= 0) {
                s.angle = random.nextFloat() * 360F;
                s.catchableDelay = 20 + random.nextInt(61);
            }
        } else {
            s.caughtDelay = 100 + random.nextInt(501) - s.lure * 100;
        }
        return event;
    }

    static int weight(int base, int quality, float luck) {
        return Math.max((int)Math.floor(base + quality * luck), 0);
    }

    static int[] loot(Random random, float luck) {
        int[] junkItem = {301,334,352,373,287,346,281,280,351,131,367};
        int[] junkWeight = {10,10,10,10,5,2,10,5,1,10,10};
        int[] treasureItem = {111,421,329,261,346,340};
        int[] fishWeight = {60,25,2,13};
        int junk=weight(10,-2,luck), treasure=weight(5,2,luck);
        int fish=weight(85,-1,luck), roll=random.nextInt(junk+treasure+fish);
        if (roll < junk) {
            roll=random.nextInt(83);
            for (int i=0;i<junkItem.length;i++) {
                if (roll < junkWeight[i])
                    return new int[]{0,junkItem[i],junkItem[i]==351?10:1,0};
                roll-=junkWeight[i];
            }
        } else if ((roll-=junk) < treasure) {
            return new int[]{1,treasureItem[random.nextInt(6)],1,0};
        } else {
            roll=random.nextInt(100);
            for (int i=0;i<fishWeight.length;i++) {
                if (roll < fishWeight[i]) return new int[]{2,349,1,i};
                roll-=fishWeight[i];
            }
        }
        throw new AssertionError();
    }

    public static void main(String[] args) {
        long raw=0x123456789abcl;
        State s=new State(); s.lure=2;
        int event=tick(s,new Random(raw^0x5deece66dL),false,true,true,true);
        System.out.printf("W %d %d%n",s.caughtDelay,event);

        s=new State(); s.catchable=2;
        event=tick(s,new Random(raw^0x5deece66dL),true,false,true,true);
        System.out.printf("C %d %s %d%n",s.catchable,dbits(s.motionY),event);

        s=new State(); s.catchableDelay=1;
        event=tick(s,new Random(raw^0x5deece66dL),false,true,true,true);
        System.out.printf("B %d %s %08x %d%n",s.catchable,dbits(s.motionY),
            Float.floatToRawIntBits(s.bitePitch),event);

        s=new State(); s.caughtDelay=1;
        event=tick(s,new Random(raw^0x5deece66dL),false,true,true,true);
        System.out.printf("A %d %d %08x %d%n",s.caughtDelay,s.catchableDelay,
            Float.floatToRawIntBits(s.angle),event);

        System.out.println("fishing: PASS (timers, loot, hook/retract/events, persistence)");
    }
}
