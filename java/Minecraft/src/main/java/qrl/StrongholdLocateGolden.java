package qrl;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.init.Bootstrap;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.WorldType;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.gen.layer.GenLayer;
import net.minecraft.world.gen.layer.IntCache;

/** Direct 1.11.2 stronghold ring and biome-relocation oracle. */
public final class StrongholdLocateGolden {
    private static BlockPos find(
            GenLayer layer, int x, int z, List<Biome> allowed, Random random) {
        int range = 112;
        int x0 = x - range >> 2, z0 = z - range >> 2;
        int x1 = x + range >> 2, z1 = z + range >> 2;
        int width = x1 - x0 + 1, height = z1 - z0 + 1;
        IntCache.resetIntCache();
        int[] values = layer.getInts(x0, z0, width, height);
        BlockPos result = null;
        int choices = 0;
        for (int index = 0; index < width * height; ++index) {
            Biome biome = Biome.getBiome(values[index]);
            if (allowed.contains(biome)
                    && (result == null || random.nextInt(choices + 1) == 0)) {
                result = new BlockPos(
                    (x0 + index % width) << 2, 0,
                    (z0 + index / width) << 2);
                ++choices;
            }
        }
        return result;
    }

    public static void main(String[] args) {
        Bootstrap.register();
        long seed = args.length == 0 ? 0L : Long.parseLong(args[0]);
        GenLayer[] layers = GenLayer.initializeAllBiomeGenerators(
            seed, WorldType.DEFAULT, null);
        List<Biome> allowed = new ArrayList<Biome>();
        for (Biome biome : Biome.REGISTRY)
            if (biome != null && biome.getBaseHeight() > 0.0F)
                allowed.add(biome);
        Random random = new Random(seed);
        double angle = random.nextDouble() * Math.PI * 2.0D;
        int ring = 0, inRing = 0, spread = 3;
        for (int index = 0; index < 128; ++index) {
            double distance = 128.0D + ring * 192.0D
                + (random.nextDouble() - 0.5D) * 80.0D;
            int rawX = (int)Math.round(Math.cos(angle) * distance);
            int rawZ = (int)Math.round(Math.sin(angle) * distance);
            BlockPos moved = find(
                layers[0], (rawX << 4) + 8, (rawZ << 4) + 8,
                allowed, random);
            int chunkX = moved == null ? rawX : moved.getX() >> 4;
            int chunkZ = moved == null ? rawZ : moved.getZ() >> 4;
            System.out.printf("%d %d %d %d %d%n",
                index, rawX, rawZ, chunkX, chunkZ);
            angle += Math.PI * 2.0D / spread;
            if (++inRing == spread) {
                ++ring;
                inRing = 0;
                spread += 2 * spread / (ring + 1);
                spread = Math.min(spread, 128 - index);
                angle += random.nextDouble() * Math.PI * 2.0D;
            }
        }
    }
}
