package qrl;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityAgeable;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.passive.EntityRabbit;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.IChunkGenerator;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.ChunkProviderOverworld;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct oracle for ChunkProviderOverworld.populate's initial-animal tail. */
public final class WorldgenAnimalsGolden {
    private WorldgenAnimalsGolden() {}

    private static final class PopulationWorld extends World {
        private final Map<Long, Chunk> chunks = new HashMap<Long, Chunk>();
        private final IChunkGenerator generator;

        PopulationWorld(long seed) {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(seed, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "worldgen-animals-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            provider.setWorld(this);
            generator = new ChunkProviderOverworld(this, seed, true, "");
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return chunks.containsKey(key(x, z));
        }
        public Biome getBiome(BlockPos pos) {
            return provider.getBiomeProvider().getBiome(pos);
        }
        private static long key(int x, int z) {
            return ((long)x & 0xffffffffL) | (((long)z & 0xffffffffL) << 32);
        }
        public Chunk getChunkFromChunkCoords(int x, int z) {
            Chunk chunk = chunks.get(key(x, z));
            if (chunk == null) {
                chunk = generator.provideChunk(x, z);
                chunks.put(key(x, z), chunk);
            }
            return chunk;
        }
        void populate(int x, int z) {
            for (int dx = 0; dx <= 1; ++dx)
                for (int dz = 0; dz <= 1; ++dz)
                    getChunkFromChunkCoords(x + dx, z + dz);
            generator.populate(x, z);
        }
    }

    public static void main(String[] args) {
        if (args.length != 4)
            throw new IllegalArgumentException(
                "usage: WorldgenAnimalsGolden SEED CX CZ WORLD_RAND_SEED");
        Bootstrap.register();
        long seed = Long.parseLong(args[0]);
        int cx = Integer.parseInt(args[1]);
        int cz = Integer.parseInt(args[2]);
        long worldRandomSeed = Long.parseLong(args[3]);
        PopulationWorld world = new PopulationWorld(seed);
        world.rand.setSeed(worldRandomSeed);
        world.populate(cx, cz);
        List<EntityLiving> entities = new ArrayList<EntityLiving>();
        for (Entity entity : world.loadedEntityList)
            if (entity instanceof EntityLiving)
                entities.add((EntityLiving)entity);
        entities.sort(Comparator.comparingInt(Entity::getEntityId));
        for (EntityLiving entity : entities) {
            int rabbitType = entity instanceof EntityRabbit
                ? ((EntityRabbit)entity).getRabbitType() : -1;
            int growingAge = entity instanceof EntityAgeable
                ? ((EntityAgeable)entity).getGrowingAge() : 0;
            System.out.printf("E %s %.17g %.17g %.17g %.9g %d %d%n",
                entity.getName(), entity.posX, entity.posY, entity.posZ,
                entity.rotationYaw, rabbitType, growingAge);
        }
        System.out.printf("END count=%d world_seed48=%d%n", entities.size(),
            world.rand.nextLong());
    }
}
