package com.netherite.mod;

import net.minecraft.block.Block;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.network.ClientPlayerEntity;
import net.minecraft.client.world.ClientChunkManager;
import net.minecraft.entity.Entity;
import net.minecraft.entity.LivingEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.registry.Registries;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Box;
import net.minecraft.world.chunk.ChunkStatus;

import java.nio.MappedByteBuffer;
import java.util.List;

/**
 * Exports player state and nearby entities to shared memory.
 * Runs on END_CLIENT_TICK.
 */
public class StateExporter {
    public static final StateExporter INSTANCE = new StateExporter();

    private static final int MAGIC = 0x4E455453; // "NETS"
    private static final int SHMEM_SIZE = 64 * 1024; // 64KB
    private static final int MAX_ENTITIES = 32;
    private static final double ENTITY_RANGE = 16.0;
    private static final long FNV_OFFSET_BASIS = 0xcbf29ce484222325L;
    private static final long FNV_PRIME = 0x100000001b3L;
    private static final int BLOCK_SAMPLE_RADIUS_XZ = 8;
    private static final int BLOCK_SAMPLE_STEP_XZ = 2;
    private static final int BLOCK_SAMPLE_MIN_DY = -2;
    private static final int BLOCK_SAMPLE_MAX_DY = 6;
    private static final int BLOCK_SAMPLE_COUNT_XZ =
            ((BLOCK_SAMPLE_RADIUS_XZ * 2) / BLOCK_SAMPLE_STEP_XZ) + 1;
    private static final int BLOCK_SAMPLE_COUNT_Y =
            BLOCK_SAMPLE_MAX_DY - BLOCK_SAMPLE_MIN_DY + 1;
    private static final int BLOCK_SAMPLE_COUNT =
            BLOCK_SAMPLE_COUNT_XZ * BLOCK_SAMPLE_COUNT_XZ * BLOCK_SAMPLE_COUNT_Y;
    private static final int CHUNK_SAMPLE_RADIUS = 2;
    private static final int PROFILE_INTERVAL = 500; // Log timing every N ticks

    private ShmemBuffer shmem;
    private int tickCount = 0;
    private int lastWrittenTick = 0;  // The tick value written to shmem (for frame sync)
    private PosixSemaphore stateSemaphore;  // For low-latency Python signaling
    private boolean useSemaphore = false;

    // Profiling accumulators (nanoseconds)
    private long profileWorldSampleNs = 0;
    private long profileEntityScanNs = 0;
    private long profileShmemWriteNs = 0;
    private long profileTotalNs = 0;
    private int profileCount = 0;

    public void init(int instanceId) {
        shmem = new ShmemBuffer("netherite_state_" + instanceId, SHMEM_SIZE);

        // Initialize POSIX semaphore for low-latency signaling
        useSemaphore = Boolean.getBoolean("netherite.use_semaphore");
        if (useSemaphore) {
            stateSemaphore = new PosixSemaphore("/netherite_state_" + instanceId);
            if (stateSemaphore.open()) {
                NetheriteMod.LOGGER.info("StateExporter: semaphore enabled for instance {}", instanceId);
            } else {
                NetheriteMod.LOGGER.warn("StateExporter: semaphore failed, falling back to polling");
                useSemaphore = false;
            }
        }
        // Write init-ready flag so launcher knows we're up
        MappedByteBuffer buf = shmem.getBuffer();
        buf.position(0);
        buf.putInt(MAGIC);           // offset 0: magic
        buf.putInt(0);               // offset 4: tick
        buf.putInt(0);               // offset 8: data_size
        buf.putInt(1);               // offset 12: init_ready=1
        ShmemBuffer.forceIfEnabled(buf);
        NetheriteMod.LOGGER.info("StateExporter: shmem ready for instance {}", instanceId);
    }

    public void resetTickCounter() {
        tickCount = 0;
        lastWrittenTick = 0;
    }

    public int getTickCount() {
        return tickCount;
    }

    /**
     * Returns the tick value that was last written to shmem.
     * Use this for frame sync - it matches the state tick in shmem.
     */
    public int getLastWrittenTick() {
        return lastWrittenTick;
    }

    public long currentWorldFingerprint(MinecraftClient mc) {
        if (mc.player == null || mc.world == null) {
            return 0L;
        }
        return sampleWorldFingerprint(mc, mc.player);
    }

    public int currentChunkMask(MinecraftClient mc) {
        if (mc.player == null || mc.world == null) {
            return 0;
        }
        return sampleChunkMask(mc, mc.player);
    }

    public void tick(MinecraftClient mc) {
        if (shmem == null || mc.player == null || mc.world == null) {
            if (tickCount == 0 && shmem != null) {
                NetheriteMod.LOGGER.info("StateExporter.tick: waiting for player/world (shmem ok, mc.player={}, mc.world={})",
                        mc.player, mc.world);
            }
            return;
        }

        long tickStartNs = System.nanoTime();

        ClientPlayerEntity player = mc.player;
        MappedByteBuffer buf = shmem.getBuffer();
        buf.position(0);

        // Header
        buf.putInt(MAGIC);           // offset 0
        buf.putInt(tickCount);       // offset 4
        buf.putInt(0);               // offset 8: data_size (filled later)
        buf.putInt(0);               // offset 12: ready=0

        // Player state at offset 16
        buf.putDouble(player.getX());         // 16
        buf.putDouble(player.getY());         // 24
        buf.putDouble(player.getZ());         // 32
        buf.putFloat(player.getYaw());        // 40
        buf.putFloat(player.getPitch());      // 44
        buf.putFloat(player.getHealth());     // 48
        buf.putFloat(player.getMaxHealth());  // 52
        buf.putInt(player.getHungerManager().getFoodLevel()); // 56
        buf.putFloat(player.getHungerManager().getSaturationLevel()); // 60
        buf.putInt(player.isOnGround() ? 1 : 0);  // 64
        buf.putInt(player.isTouchingWater() ? 1 : 0); // 68

        // Hotbar at offset 72: 9 slots x 8 bytes = 72 bytes
        for (int i = 0; i < 9; i++) {
            ItemStack stack = player.getInventory().getStack(i);
            if (stack.isEmpty()) {
                buf.putInt(0);  // item_id
                buf.putInt(0);  // count
            } else {
                buf.putInt(Registries.ITEM.getRawId(stack.getItem()));
                buf.putInt(stack.getCount());
            }
        }

        // Profile: world sampling
        long t0 = System.nanoTime();
        long worldFingerprint = sampleWorldFingerprint(mc, player);
        int chunkMask = sampleChunkMask(mc, player);
        int[] worldSample = sampleWorldStates(mc, player);
        long worldSampleNs = System.nanoTime() - t0;

        long actualWorldSeed = mc.getServer() != null ? mc.getServer().getOverworld().getSeed() : 0L;
        int completedRenderChunks = mc.worldRenderer.getCompletedChunkCount();
        int totalRenderChunks = (int) Math.round(mc.worldRenderer.getChunkCount());

        // Server-world reads from the client thread can deadlock the integrated server.
        // Client-side samples above are used for stepping; server fields stay zero here.
        long serverWorldFingerprint = 0L;
        int[] serverWorldSample = new int[BLOCK_SAMPLE_COUNT];

        // Profile: shmem write (world data)
        long t1 = System.nanoTime();
        buf.putLong(worldFingerprint); // 144
        buf.putInt(Integer.bitCount(chunkMask)); // 152
        buf.putInt(chunkMask); // 156
        buf.putLong(actualWorldSeed); // 160
        buf.putInt(completedRenderChunks); // 168
        buf.putInt(totalRenderChunks); // 172
        buf.putInt(BLOCK_SAMPLE_COUNT); // 176
        for (int rawStateId : worldSample) {
            buf.putInt(rawStateId);
        }

        // Server world fingerprint and sample at offset 176 + (BLOCK_SAMPLE_COUNT * 4)
        buf.putLong(serverWorldFingerprint);
        for (int rawStateId : serverWorldSample) {
            buf.putInt(rawStateId);
        }
        long shmemWritePartNs = System.nanoTime() - t1;

        // Profile: entity scanning
        long t2 = System.nanoTime();
        Box scanBox = player.getBoundingBox().expand(ENTITY_RANGE);
        List<Entity> entities = mc.world.getOtherEntities(player, scanBox);
        int count = Math.min(entities.size(), MAX_ENTITIES);
        buf.putInt(count);

        for (int i = 0; i < count; i++) {
            Entity e = entities.get(i);
            buf.putInt(Registries.ENTITY_TYPE.getRawId(e.getType()));
            buf.putDouble(e.getX());
            buf.putDouble(e.getY());
            buf.putDouble(e.getZ());
            buf.putFloat(e instanceof LivingEntity le ? le.getHealth() : 0.0f);
        }
        long entityScanNs = System.nanoTime() - t2;

        // Profile: final shmem flush
        long t3 = System.nanoTime();
        int dataSize = buf.position() - 16;
        buf.putInt(8, dataSize);
        buf.putInt(12, 1);
        ShmemBuffer.forceIfEnabled(buf);

        // Signal Python that state is ready (low-latency path)
        if (useSemaphore && stateSemaphore != null) {
            stateSemaphore.post();
        }
        long shmemFlushNs = System.nanoTime() - t3;

        long totalNs = System.nanoTime() - tickStartNs;

        // Accumulate profiling stats
        profileWorldSampleNs += worldSampleNs;
        profileEntityScanNs += entityScanNs;
        profileShmemWriteNs += shmemWritePartNs + shmemFlushNs;
        profileTotalNs += totalNs;
        profileCount++;

        if (profileCount >= PROFILE_INTERVAL) {
            double avgTotal = profileTotalNs / (double) profileCount / 1000.0;
            double avgWorldSample = profileWorldSampleNs / (double) profileCount / 1000.0;
            double avgEntityScan = profileEntityScanNs / (double) profileCount / 1000.0;
            double avgShmemWrite = profileShmemWriteNs / (double) profileCount / 1000.0;
            NetheriteMod.LOGGER.info(
                "StateExporter profile (n={}): total={}us, worldSample={}us, entityScan={}us, shmemWrite={}us",
                profileCount,
                String.format("%.1f", avgTotal),
                String.format("%.1f", avgWorldSample),
                String.format("%.1f", avgEntityScan),
                String.format("%.1f", avgShmemWrite));
            profileWorldSampleNs = 0;
            profileEntityScanNs = 0;
            profileShmemWriteNs = 0;
            profileTotalNs = 0;
            profileCount = 0;
        }

        // Record what we wrote (for frame sync)
        lastWrittenTick = tickCount;

        if (!WorldController.INSTANCE.isStartLatched()) {
            tickCount++;
        }
    }

    private long sampleWorldFingerprint(MinecraftClient mc, ClientPlayerEntity player) {
        int[] worldSample = sampleWorldStates(mc, player);
        long hash = FNV_OFFSET_BASIS;
        for (int rawStateId : worldSample) {
            hash ^= rawStateId & 0xffffffffL;
            hash *= FNV_PRIME;
        }
        return hash;
    }

    private int[] sampleWorldStates(MinecraftClient mc, ClientPlayerEntity player) {
        // Use config dimensions if in voxel mode, else use defaults for backward compat
        NetheriteConfig cfg = NetheriteConfig.INSTANCE;
        int minDy, maxDy, radiusXZ, stepXZ;

        if (cfg.needsVoxels()) {
            minDy = -cfg.voxelDown;
            maxDy = cfg.voxelUp;
            radiusXZ = Math.max(cfg.voxelForward, Math.max(cfg.voxelBack,
                       Math.max(cfg.voxelLeft, cfg.voxelRight)));
            stepXZ = 1;  // Full resolution in voxel mode
        } else {
            minDy = BLOCK_SAMPLE_MIN_DY;
            maxDy = BLOCK_SAMPLE_MAX_DY;
            radiusXZ = BLOCK_SAMPLE_RADIUS_XZ;
            stepXZ = BLOCK_SAMPLE_STEP_XZ;
        }

        int countXZ = ((radiusXZ * 2) / stepXZ) + 1;
        int countY = maxDy - minDy + 1;
        int[] sample = new int[countXZ * countXZ * countY];

        int baseX = player.getBlockX();
        int baseY = player.getBlockY();
        int baseZ = player.getBlockZ();
        BlockPos.Mutable pos = new BlockPos.Mutable();
        int index = 0;

        for (int dy = minDy; dy <= maxDy; dy++) {
            for (int dz = -radiusXZ; dz <= radiusXZ; dz += stepXZ) {
                for (int dx = -radiusXZ; dx <= radiusXZ; dx += stepXZ) {
                    pos.set(baseX + dx, baseY + dy, baseZ + dz);
                    sample[index++] = Block.getRawIdFromState(mc.world.getBlockState(pos));
                }
            }
        }

        return sample;
    }

    private int sampleChunkMask(MinecraftClient mc, ClientPlayerEntity player) {
        ClientChunkManager chunkManager = mc.world.getChunkManager();
        int baseChunkX = player.getBlockX() >> 4;
        int baseChunkZ = player.getBlockZ() >> 4;
        int mask = 0;
        int bit = 0;

        for (int dz = -CHUNK_SAMPLE_RADIUS; dz <= CHUNK_SAMPLE_RADIUS; dz++) {
            for (int dx = -CHUNK_SAMPLE_RADIUS; dx <= CHUNK_SAMPLE_RADIUS; dx++) {
                if (chunkManager.getChunk(baseChunkX + dx, baseChunkZ + dz, ChunkStatus.FULL, false) != null) {
                    mask |= 1 << bit;
                }
                bit++;
            }
        }

        return mask;
    }

}
