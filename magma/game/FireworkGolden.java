import java.util.Random;

public final class FireworkGolden {
    static String bits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }
    public static void main(String[] args) {
        long seed48=0x123456789abcl;
        Random random=new Random(seed48^0x5deece66dL);
        double vx=random.nextGaussian()*.001D;
        double vz=random.nextGaussian()*.001D;
        double vy=.05D;
        int lifetime=10*3+random.nextInt(6)+random.nextInt(7);
        System.out.printf("S %s %s %s %d%n",bits(vx),bits(vy),bits(vz),lifetime);
        vx*=1.15D;vz*=1.15D;vy+=.04D;
        double x=40.0D+vx,y=100.0D+vy,z=40.0D+vz;
        System.out.printf("T %s %s %s %s %s %s 1%n",
            bits(x),bits(y),bits(z),bits(vx),bits(vy),bits(vz));
        System.out.println("firework: PASS (RNG, motion, recipes, boost, damage, render/events)");
    }
}
