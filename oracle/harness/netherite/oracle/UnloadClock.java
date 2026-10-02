package netherite.oracle;

import java.util.Map;
import java.util.TreeMap;

import net.minecraft.server.MinecraftServer;
import net.minecraft.world.WorldServer;

/** The clock each join-time chunk unload save used, per chunk, for the
 * snapshot's whole-server replay.
 *
 * WorldServer.tick runs ChunkProviderServer.unloadQueuedChunks BEFORE
 * WorldInfo.incrementTotalWorldTime, and AnvilChunkLoader.writeChunkToNBT
 * writes every pending tick's delay as scheduledTime - getTotalWorldTime().
 * So the delay a chunk's unload save wrote depends on the tick the unload ran
 * at: tick 2's 100-chunk batch (the first unloadQueuedChunks call) saves with
 * the clock still at 1, tick 3's batch with 2. A whole-server replay restores
 * a chunk's pending ticks as delay + total-at-restore, so it needs that exact
 * per-chunk clock or its restored entries land one tick late (the tiletick
 * clock divergence the death recordings hit).
 *
 * The unload set's iteration order is the JDK ConcurrentHashMap's, which the
 * native's chunkset reimplements, but the batch split still depends on the
 * run's own mark history; recording it beats deriving it. Snapshot.worldsState
 * dumps the map into worlds.nbt's w0.unloadClock; serverreplay_load reads it
 * back and the seed-world save uses each chunk's own clock. */
public final class UnloadClock
{
    /** chunkXZ2Int key -> the tickCounter the chunk's unload save ran at. */
    static final Map<Long, Integer> map = new TreeMap<Long, Integer>();

    private UnloadClock() {}

    /** ChunkProviderServer.unloadQueuedChunks, after safeSaveChunk: the tick
     * the save used (the clock is still the previous tick's). */
    public static void onUnload(WorldServer ws, long key)
    {
        if (ws == null || ws.provider.dimensionId != 0) return;
        map.put(Long.valueOf(key), Integer.valueOf(MinecraftServer.getServer().getTickCounter()));
    }

    /** The recorded history, "ia:key,tick,...", or null when nothing unloaded. */
    public static String dump()
    {
        if (map.isEmpty()) return null;
        StringBuilder b = new StringBuilder("ia:");
        int n = 0;
        for (Map.Entry<Long, Integer> e : map.entrySet())
        {
            long key = e.getKey().longValue();
            if (n++ > 0) b.append(',');
            b.append((int)key).append(',').append((int)(key >>> 32)).append(',')
             .append(e.getValue().intValue());
        }
        return b.toString();
    }
}
