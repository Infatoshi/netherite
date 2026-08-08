package qrl;

import com.mojang.authlib.GameProfile;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.ChunkPrimer;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.MapData;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct ItemMap.updateMapData oracle over discriminating terrain planes. */
public final class MapUpdateGolden {
    private static final class MemoryWorld extends World {
        final Map<Long, Chunk> chunks = new HashMap<Long, Chunk>();
        final Map<Long, ChunkPrimer> primers = new HashMap<Long, ChunkPrimer>();

        MemoryWorld() {
            super(new SaveHandlerMP(), new WorldInfo(new WorldSettings(
                0L, GameType.SURVIVAL, true, false, WorldType.DEFAULT),
                "map-update-oracle"), new WorldProviderSurface(),
                new Profiler(), false);
            provider.setWorld(this);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        private static long chunkKey(int x, int z) {
            return ((long)x & 0xffffffffL)
                | (((long)z & 0xffffffffL) << 32);
        }
        public Chunk getChunkFromChunkCoords(int x, int z) {
            long key = chunkKey(x, z);
            Chunk chunk = chunks.get(key);
            if (chunk == null) {
                ChunkPrimer primer = primers.remove(key);
                chunk = primer == null ? new Chunk(this, x, z)
                    : new Chunk(this, primer, x, z);
                chunk.generateSkylightMap();
                chunks.put(key, chunk);
            }
            return chunk;
        }
        void put(int x, int y, int z, IBlockState state) {
            int cx = x >> 4;
            int cz = z >> 4;
            long key = chunkKey(cx, cz);
            Chunk chunk = chunks.get(key);
            if (chunk != null) {
                chunk.setBlockState(new BlockPos(x, y, z), state);
                return;
            }
            ChunkPrimer primer = primers.get(key);
            if (primer == null) {
                primer = new ChunkPrimer();
                primers.put(key, primer);
            }
            primer.setBlockState(x & 15, y, z & 15, state);
        }
    }

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(
                new UUID(0x4d415055504441L, 0x5445474f4c4445L),
                "map-update"));
            setPosition(0.5D, 70.0D, 0.5D);
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    private static void terrain(MemoryWorld world, int kind) {
        for (int x = -128; x < 128; ++x)
            for (int z = -128; z < 128; ++z) {
                int top = kind == 2 ? 60 + ((x + 128) >> 4) : 64;
                world.put(x, top, z, Blocks.STONE.getDefaultState());
                if (kind == 1 && x >= 0) {
                    world.put(x, top + 1, z,
                        Blocks.WATER.getDefaultState());
                    if ((z & 8) != 0)
                        world.put(x, top + 2, z,
                            Blocks.WATER.getDefaultState());
                } else if (kind == 2 && (z & 16) != 0) {
                    world.put(x, top + 1, z,
                        Blocks.GRASS.getDefaultState());
                }
            }
    }

    private static void run(int kind) {
        MemoryWorld world = new MemoryWorld();
        terrain(world, kind);
        TestPlayer player = new TestPlayer(world);
        MapData data = new MapData("map_0");
        data.scale = 0;
        data.xCenter = 0;
        data.zCenter = 0;
        data.dimension = 0;
        for (int tick = 0; tick < 16; ++tick)
            Items.FILLED_MAP.updateMapData(world, player, data);
        StringBuilder hex = new StringBuilder(128 * 128 * 2);
        for (byte value : data.colors)
            hex.append(String.format("%02x", value & 255));
        System.out.printf("U %d %d %s%n", kind,
            data.getMapInfo(player).step, hex.toString());
    }

    public static void main(String[] args) {
        Bootstrap.register();
        for (int kind = 0; kind < 3; ++kind) run(kind);
    }
}
