package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.server.management.PlayerManager;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.WorldServer;

/**
 * Chunk load, populate and unload order recorder: every ChunkProviderServer
 * event during a real run, the reference for csrc/engine/chunkload.c.
 *
 * Turned on with --chunklog DIR (a Main flag), before the server starts, so
 * the spawn area (MinecraftServer.initialWorldChunkLoad's 625 chunks) is
 * recorded. The hooks in ChunkProviderServer call the static on* methods; each
 * is a no-op unless the recorder is on. One JSON line per event to
 * DIR/events.jsonl:
 *
 *   {"t":tick,"k":kind,"cx":cx,"cz":cz,"pop":id[,"id":callId][,"site":"..."]}
 *
 * k is one of
 *   load    ChunkProviderServer.loadChunk provided a chunk (pop: the populate
 *           call the load happened inside, -1 at the root)
 *   pop     ChunkProviderServer.populate entered (id: its own call id, in call
 *           order; pop: the populate call it happened inside, -1 at the root)
 *   mark    unloadChunksIfNotNearSpawn queued the chunk for unload
 *   unmark  loadChunk removed the chunk from the unload queue
 *   unload  unloadQueuedChunks unloaded the chunk
 * t is MinecraftServer's tickCounter: 0 during server startup, n during server
 * tick n. The unload order comes from the provider's chunksToUnload set, a
 * Collections.newSetFromMap(new ConcurrentHashMap()) of Long, so its iteration
 * order is the JDK 8 ConcurrentHashMap order. A native populate replays the
 * loads recorded with its call id as their pop value.
 *
 * Each load also carries site: the caller of loadChunk (PlayerManager's chunk
 * watchers, the spawn loop, tickUpdates, tickBlocks, updateEntities, and so
 * on), taken from the loadChunk call stack. A site the native engine cannot
 * produce itself (a population feature writing into an unloaded chunk, water
 * flowing off the loaded edge) is replayed from the recording; playermgr,
 * spawn and login loads are produced structurally.
 *
 * The run's script ends with a ChunkLoadProbe run command, which closes the
 * events file and writes DIR/manifest.json and DIR/tape.jsonl (a copy of the
 * run's tape; the per-tick player positions the native replay needs are the
 * rows' sp.x/sp.y/sp.z).
 */
public final class ChunkLoadProbe
{
    static File dir;
    static boolean on;
    static PrintWriter tape;
    static int nextPopId, insidePopulateLoads;
    static int eventCount, loads, populates, unloads, marks, unmarks;
    /** The populate calls on this thread's stack, innermost last. */
    static final ArrayList<Integer> stack = new ArrayList<Integer>();
    /** Shadow of the provider's chunksToUnload: events are written only when the set changes. */
    static final Set<Long> shadow = new HashSet<Long>();
    /** Site registry: first-appearance order, so the ids are deterministic. */
    static final ArrayList<String> sites = new ArrayList<String>();
    static final Map<String, Integer> siteIds = new HashMap<String, Integer>();
    static final ArrayList<String> siteCounts = new ArrayList<String>();

    private ChunkLoadProbe() {}

    /** Main flag --chunklog DIR: record every chunk event from server start. */
    public static void begin(String path) throws IOException
    {
        dir = new File(path);
        dir.mkdirs();
        on = true;
    }

    static void emit(String kind, int cx, int cz, int pop) throws IOException
    {
        JsonObject o = new JsonObject();
        o.addProperty("t", MinecraftServer.getServer().getTickCounter());
        o.addProperty("k", kind);
        o.addProperty("cx", cx);
        o.addProperty("cz", cz);
        o.addProperty("pop", pop);
        tape.println(o.toString());
        ++eventCount;
    }

    static void openTape() throws IOException
    {
        tape = new PrintWriter(new OutputStreamWriter(new BufferedOutputStream(new FileOutputStream(new File(dir, "events.jsonl")), 1 << 16), "UTF-8"));
    }

    // ---------------------------------------------------------------- hooks

    /** ChunkProviderServer.loadChunk, every call, after the unload-set removal:
     * newChunk distinguishes the provide path from the already-loaded one. */
    public static void onLoadChunk(WorldServer ws, int cx, int cz, boolean newChunk)
    {
        if (!on || ws == null || ws.provider.dimensionId != 0) return;

        try
        {
            if (tape == null) openTape();
            long key = ChunkCoordIntPair.chunkXZ2Int(cx, cz);

            if (shadow.remove(Long.valueOf(key)))
            {
                emit("unmark", cx, cz, popTop());
                ++unmarks;
            }

            if (newChunk)
            {
                int pop = popTop();

                if (pop != -1) ++insidePopulateLoads;
                JsonObject o = new JsonObject();
                o.addProperty("t", MinecraftServer.getServer().getTickCounter());
                o.addProperty("k", "load");
                o.addProperty("cx", cx);
                o.addProperty("cz", cz);
                o.addProperty("pop", pop);
                String site = siteOf();
                o.addProperty("site", site);
                tape.println(o.toString());
                ++eventCount;
                ++loads;
                countSite(site);
            }
        }
        catch (IOException e)
        {
            throw new RuntimeException(e);
        }
    }

    /** ChunkProviderServer.populate, on entry. */
    public static void onPopulateEnter(WorldServer ws, int cx, int cz)
    {
        if (!on || ws == null || ws.provider.dimensionId != 0) return;

        try
        {
            if (tape == null) openTape();
            int id = nextPopId++;
            JsonObject o = new JsonObject();
            o.addProperty("t", MinecraftServer.getServer().getTickCounter());
            o.addProperty("k", "pop");
            o.addProperty("id", id);
            o.addProperty("cx", cx);
            o.addProperty("cz", cz);
            o.addProperty("pop", popTop());
            tape.println(o.toString());
            ++eventCount;
            ++populates;
            stack.add(Integer.valueOf(id));
        }
        catch (IOException e)
        {
            throw new RuntimeException(e);
        }
    }

    /** ChunkProviderServer.populate, on exit. */
    public static void onPopulateExit(WorldServer ws)
    {
        if (!on || ws == null || ws.provider.dimensionId != 0) return;
        stack.remove(stack.size() - 1);
    }

    /** ChunkProviderServer.unloadChunksIfNotNearSpawn: the chunk is queued for
     * unload when it is farther than 128 blocks from the spawn point (or the
     * provider's world cannot respawn here, which the overworld can). */
    public static void onMark(WorldServer ws, int cx, int cz)
    {
        if (!on || ws == null || ws.provider.dimensionId != 0) return;

        try
        {
            if (tape == null) openTape();

            if (ws.provider.canRespawnHere())
            {
                ChunkCoordinates sp = ws.getSpawnPoint();
                int dx = cx * 16 + 8 - sp.posX;
                int dz = cz * 16 + 8 - sp.posZ;

                if (dx >= -128 && dx <= 128 && dz >= -128 && dz <= 128) return;
            }

            if (shadow.add(Long.valueOf(ChunkCoordIntPair.chunkXZ2Int(cx, cz))))
            {
                emit("mark", cx, cz, popTop());
                ++marks;
            }
        }
        catch (IOException e)
        {
            throw new RuntimeException(e);
        }
    }

    /** ChunkProviderServer.unloadQueuedChunks: the chunk was unloaded. */
    public static void onUnload(WorldServer ws, long key)
    {
        if (!on || ws == null || ws.provider.dimensionId != 0) return;

        try
        {
            if (tape == null) openTape();
            shadow.remove(Long.valueOf(key));
            emit("unload", (int)key, (int)(key >> 32), popTop());
            ++unloads;
        }
        catch (IOException e)
        {
            throw new RuntimeException(e);
        }
    }

    static int popTop()
    {
        return stack.isEmpty() ? -1 : stack.get(stack.size() - 1).intValue();
    }

    /** The caller of loadChunk, as a stable string: the first frame walking
     * outward from the hook that names one of the known load sites. The frames
     * between are the provide path (World.getChunk*, provideChunk), which says
     * nothing about who asked for the chunk. */
    static String siteOf()
    {
        StackTraceElement[] st = Thread.currentThread().getStackTrace();

        for (int i = 2; i < st.length; ++i)
        {
            String c = st[i].getClassName();
            String m = st[i].getMethodName();

            if (c.equals("net.minecraft.world.gen.ChunkProviderServer")) continue;

            if (c.equals("net.minecraft.server.management.PlayerManager")) return "playermgr";

            if (c.equals("net.minecraft.server.MinecraftServer") && m.equals("initialWorldChunkLoad")) return "spawn";

            if (c.equals("net.minecraft.server.management.ServerConfigurationManager")) return "login";

            if (c.equals("net.minecraft.world.gen.ChunkProviderGenerate")) return "populate";

            if (c.equals("net.minecraft.world.WorldServer"))
            {
                if (m.equals("createSpawnPosition")) return "spawnsearch";
                if (m.equals("tickUpdates")) return "tickPending";
                if (m.equals("func_147456_g")) return "tickBlocks";
                if (m.equals("updateEntities")) return "entities";
            }

            if (c.equals("net.minecraft.world.SpawnerAnimals")) return "mobspawn";

            if (c.equals("net.minecraft.network.NetworkSystem") || c.equals("net.minecraft.network.NetHandlerPlayServer")) return "network";

            if (c.equals("net.minecraft.village.VillageCollection") || c.equals("net.minecraft.village.VillageSiege")) return "village";

            if (c.equals("net.minecraft.world.Teleporter")) return "portal";

            if (c.equals("net.minecraft.tileentity.TileEntity")) return "tileentity";
        }

        // fall back: the outermost frame of the tick path, for the registry
        return fallbackSite(st);
    }

    static String fallbackSite(StackTraceElement[] st)
    {
        for (int i = st.length - 1; i > 2; --i)
        {
            String c = st[i].getClassName();

            if (c.startsWith("netherite.oracle") || c.equals("java.lang.Thread")) continue;
            return c.substring(c.lastIndexOf('.') + 1) + "." + st[i].getMethodName();
        }

        return "?";
    }

    static void countSite(String site)
    {
        Integer id = siteIds.get(site);

        if (id == null)
        {
            sites.add(site);
            siteCounts.add("0");
            id = Integer.valueOf(sites.size() - 1);
            siteIds.put(site, id);
        }

        int n = Integer.parseInt(siteCounts.get(id.intValue())) + 1;
        siteCounts.set(id.intValue(), Integer.toString(n));
    }

    // ---------------------------------------------------------------- close

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File out = new File(cmd.get("out").getAsString());

        if (!out.equals(dir)) throw new IllegalArgumentException("chunkload: cmd out " + out + " is not --chunklog " + dir);

        if (tape != null) { tape.flush(); tape.close(); tape = null; }
        WorldServer ws = server.worldServers[0];
        ChunkCoordinates spawn = ws.getSpawnPoint();
        int radius = ((Integer)field(PlayerManager.class, "playerViewRadius").get(ws.getPlayerManager())).intValue();

        // the run's tape, copied next to the events: the native replay reads its
        // rows' sp positions (the positions the C03 replay needs)
        copy(Oracle.tapePath, new File(dir, "tape.jsonl"));

        JsonObject m = new JsonObject();
        m.addProperty("seed", ws.getSeed());
        m.addProperty("kind", "chunkload");
        m.addProperty("ticks", server.getTickCounter());
        m.addProperty("spawnX", spawn.posX);
        m.addProperty("spawnY", spawn.posY);
        m.addProperty("spawnZ", spawn.posZ);
        m.addProperty("viewRadius", radius);
        m.addProperty("events", eventCount);
        m.addProperty("loads", loads);
        m.addProperty("populates", populates);
        m.addProperty("unloads", unloads);
        m.addProperty("marks", marks);
        m.addProperty("unmarks", unmarks);
        m.addProperty("insidePopulateLoads", insidePopulateLoads);
        m.addProperty("events_layout", "events.jsonl, one JSON object per line: t (server tickCounter; 0 during startup), k (load, pop, mark, unmark, unload), cx, cz; pop is the enclosing populate call id (-1 at the root); pop events also carry id, their own call id in call order; load events also carry site, the caller from the sites list");
        m.addProperty("positions", "tape.jsonl rows, sp.x/sp.y/sp.z: the server player's position at the end of each server tick t (row t has no sp before the player logs in; the login tick is the first row with sp). The native replay's tick t uses row t-1's sp as the C03 position");
        m.addProperty("sites", sites.size());
        m.addProperty("sites_list", joinCounts());
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("events", eventCount);
        res.addProperty("loads", loads);
        res.addProperty("populates", populates);
        res.addProperty("unloads", unloads);
        res.addProperty("insidePopulateLoads", insidePopulateLoads);
        return res;
    }

    static String joinCounts()
    {
        StringBuilder b = new StringBuilder();

        for (int i = 0; i < sites.size(); ++i) b.append(i == 0 ? "" : ",").append(sites.get(i)).append(':').append(siteCounts.get(i));
        return b.toString();
    }

    static void copy(String from, File to) throws IOException
    {
        InputStream in = new FileInputStream(from);
        FileOutputStream out = new FileOutputStream(to);
        byte[] buf = new byte[1 << 16];

        for (int r; (r = in.read(buf)) > 0; ) out.write(buf, 0, r);
        out.close();
        in.close();
    }

    private static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }
}