package qrl;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.EnumParticleTypes;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Exact client-side AbstractHorse status 6/7 particle-call oracle. */
public final class HorseParticleGolden {
    private static final class Row {
        final int id;
        final double x, y, z;
        Row(EnumParticleTypes type, double xIn, double yIn, double zIn) {
            id = type.getParticleID();
            x = xIn;
            y = yIn;
            z = zIn;
        }
    }

    private static final class MemoryWorld extends World {
        final List<Row> rows = new ArrayList<Row>();
        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "horse-particle-oracle"),
                new WorldProviderSurface(), new Profiler(), true);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
        public void spawnParticle(EnumParticleTypes type,
                double x, double y, double z,
                double vx, double vy, double vz, int... parameters) {
            rows.add(new Row(type, x, y, z));
        }
    }

    private static final class TestHorse extends EntityHorse {
        TestHorse(World world) { super(world); }
        void effect(boolean success) { this.spawnHorseParticles(success); }
        void setRawSeed(long seed48) {
            this.rand.setSeed(seed48 ^ 0x5deece66dL);
        }
        Random random() { return this.rand; }
    }

    private static long rawSeed(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & ((1L << 48) - 1L);
    }

    private static void run(String name, long seed48, boolean success)
            throws Exception {
        MemoryWorld world = new MemoryWorld();
        TestHorse horse = new TestHorse(world);
        horse.setPosition(8.25D, 65.2D, 8.75D);
        horse.setRawSeed(seed48);
        horse.effect(success);
        for (int i = 0; i < world.rows.size(); ++i) {
            Row row = world.rows.get(i);
            System.out.printf("%s %d %d %016x %016x %016x%n",
                name, i, row.id,
                Double.doubleToRawLongBits(row.x),
                Double.doubleToRawLongBits(row.y),
                Double.doubleToRawLongBits(row.z));
        }
        System.out.printf("%s seed %012x%n", name, rawSeed(horse.random()));
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        run("H", 0L, true);
        run("S", 1L, false);
    }
}
