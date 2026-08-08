package qrl;

import java.util.List;
import java.util.HashMap;
import java.util.Map;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Blocks;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.biome.Biome;
import net.minecraft.world.biome.BiomeProvider;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.IChunkGenerator;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.ChunkProviderOverworld;
import net.minecraft.world.gen.layer.GenLayer;
import net.minecraft.world.gen.layer.IntCache;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Oracle for WorldServer.createSpawnPosition's biome reservoir selection. */
public final class SpawnPositionGolden {
    private SpawnPositionGolden() {}

    /** Minimal real-overworld block view used by WorldProvider's spawn test. */
    private static final class SpawnWorld extends World {
        private final Map<Long, Chunk> chunks = new HashMap<Long, Chunk>();
        private final IChunkGenerator generator;

        SpawnWorld(long seed) {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(seed, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "spawn-position-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            provider.setWorld(this);
            generator = new ChunkProviderOverworld(this, seed, true, "");
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public Biome getBiome(BlockPos pos) {
            return provider.getBiomeProvider().getBiome(pos);
        }
        private Chunk generatedChunk(int cx, int cz) {
            long key = ((long)cx & 0xffffffffL)
                | (((long)cz & 0xffffffffL) << 32);
            Chunk chunk = chunks.get(key);
            if (chunk == null) {
                chunk = generator.provideChunk(cx, cz);
                chunks.put(key, chunk);
            }
            return chunk;
        }
        public IBlockState getBlockState(BlockPos pos) {
            Chunk chunk = generatedChunk(pos.getX() >> 4, pos.getZ() >> 4);
            return chunk.getBlockState(pos);
        }
        int playerY(int x, int z) {
            Chunk chunk = generatedChunk(x >> 4, z >> 4);
            for (int y = chunk.getTopFilledSegment() + 15; y >= 0; --y) {
                BlockPos pos = new BlockPos(x, y, z);
                IBlockState state = chunk.getBlockState(pos);
                if (state.getMaterial().blocksMovement()
                        && !state.getBlock().isLeaves(state, this, pos)
                        && !state.getBlock().isFoliage(this, pos))
                    return y + 1;
            }
            return 0;
        }
    }

    public static void main(String[] args) {
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: SpawnPositionGolden SEED");
        }
        Bootstrap.register();
        long seed = Long.parseLong(args[0]);
        GenLayer genBiomes = GenLayer.initializeAllBiomeGenerators(
            seed, WorldType.DEFAULT, null)[0];
        List<Biome> allowed = BiomeProvider.allowedBiomes;
        Random random = new Random(seed);
        int range = 256;
        int minX = -range >> 2;
        int minZ = -range >> 2;
        int maxX = range >> 2;
        int maxZ = range >> 2;
        int width = maxX - minX + 1;
        int height = maxZ - minZ + 1;
        IntCache.resetIntCache();
        int[] biomes = genBiomes.getInts(minX, minZ, width, height);
        int chosenX = 8, chosenZ = 8, matches = 0;
        boolean found = false;
        for (int index = 0; index < width * height; ++index) {
            int x = (minX + index % width) << 2;
            int z = (minZ + index / width) << 2;
            Biome biome = Biome.getBiome(biomes[index]);
            if (allowed.contains(biome)
                    && (!found || random.nextInt(matches + 1) == 0)) {
                chosenX = x;
                chosenZ = z;
                ++matches;
                found = true;
            }
        }
        SpawnWorld world = new SpawnWorld(seed);
        int finalX = chosenX, finalZ = chosenZ, attempts = 0;
        StringBuilder tested = new StringBuilder();
        while (true) {
            BlockPos probe = new BlockPos(finalX, 0, finalZ);
            IBlockState ground = world.getGroundAboveSeaLevel(probe);
            boolean suitable = world.getBiome(probe).ignorePlayerSpawnSuitability()
                || ground.getBlock() == Blocks.GRASS;
            if (tested.length() != 0) tested.append(';');
            tested.append(finalX).append(',').append(finalZ).append(',')
                .append(Block.getIdFromBlock(ground.getBlock()));
            if (suitable) break;
            finalX += random.nextInt(64) - random.nextInt(64);
            finalZ += random.nextInt(64) - random.nextInt(64);
            if (++attempts == 1000) break;
        }
        int finalY = world.provider.getAverageGroundLevel();
        int playerY = world.playerY(finalX, finalZ);
        System.out.printf(
            "{\"seed\":%d,\"found\":%s,\"x\":%d,\"z\":%d,"
                + "\"matches\":%d,\"final_x\":%d,\"final_y\":%d,"
                + "\"final_z\":%d,\"player_y\":%d,\"attempts\":%d,"
                + "\"tested\":\"%s\"}%n",
            seed, found, chosenX, chosenZ, matches, finalX, finalY, finalZ,
            playerY, attempts, tested);
    }
}
