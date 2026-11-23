package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import java.io.BufferedReader;
import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.zip.GZIPInputStream;
import net.minecraft.server.integrated.IntegratedServer;

/**
 * The checkpoint roundtrip gate (K01): the Snapshot the save op took just
 * before its Save and Quit (CHECKPOINT/pre) against the Snapshot this --from
 * run took after its quiet join (the tape's directory, or "post").
 *
 *   {"cmd":"run","class":"Roundtrip"}
 *
 * Blocks, tile entities, player NBT and entity NBT must be equal. Every other
 * difference must fall under a named reset, and each reset is printed with its
 * count:
 *   chunk-set      a chunk loaded on one side only (the load brings back the
 *                  spawn area and the player's view, not what the run had)
 *   entity-unloaded an entity in a chunk that is loaded on one side only
 *   entity-spawned an entity the post side's join ticks spawned (natural
 *                  spawning; its UUID is new)
 *   join-write     a block the join ticks wrote (random and pending ticks)
 *   light          a light value in a chunk the join ticks wrote to, or on a
 *                  column whose light the load re-checks
 *   rotation-wrap  an entity yaw the load's Entity.setRotation took % 360
 *   chunk-cache    heightMapMinimum and precipitationHeightMap: transient
 *                  Chunk caches the Anvil loader rebuilds
 *   pending-tick   a pending tick in a chunk loaded on one side only, one the
 *                  join ticks ran (due by the post side's TotalTime) or one
 *                  they scheduled (due after the pre side's TotalTime)
 *   stale-save     a pending tick in a chunk the pre side had not modified
 *                  since its last save: Save and Quit skips an unmodified
 *                  chunk, so the load reads the older copy's TileTicks
 *   player-join    player NBT keys the join ticks advance (the player ticks
 *                  in the quiet join): see PLAYER_RESET
 * Read-only: files and Checkpoint.joinWrites. Never draws from a Random.
 */
final class Roundtrip
{
    private Roundtrip() {}

    /** Player NBT keys the player's own join ticks may move. */
    static final String[] PLAYER_RESET = {"foodTickTimer", "Motion", "OnGround", "FallDistance", "Pos", "Rotation", "SpawnX", "SpawnY", "SpawnZ", "SpawnForced", "Dimension"};

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        if (Oracle.fromDir == null) throw new IllegalStateException("Roundtrip: not a --from run");
        File pre = new File(cmd.has("pre") ? cmd.get("pre").getAsString() : new File(Oracle.fromDir, "pre").getPath());
        File post = new File(cmd.has("post") ? cmd.get("post").getAsString() : Snapshot.defaultOut());
        return compare(pre, post, Checkpoint.joinWrites);
    }

    static final class Chunk
    {
        int dim, cx, cz;
        byte[] bytes;
        JsonElement tiles;
        boolean modified;
    }

    static Map<String, Chunk> chunks(File dir) throws Exception
    {
        Map<String, Chunk> out = new LinkedHashMap<String, Chunk>();
        BufferedReader state = new BufferedReader(new InputStreamReader(new FileInputStream(new File(dir, "chunkstate.jsonl")), "UTF-8"));
        DataInputStream bin = new DataInputStream(new GZIPInputStream(new FileInputStream(new File(dir, "chunks.bin.gz")), 1 << 16));
        String line;
        while ((line = state.readLine()) != null)
        {
            JsonObject s = Rows.parse(line);
            Chunk c = new Chunk();
            c.dim = s.has("dim") ? s.get("dim").getAsInt() : 0;
            c.cx = s.get("cx").getAsInt();
            c.cz = s.get("cz").getAsInt();
            c.tiles = s.get("tiles");
            c.modified = s.has("modified") && s.get("modified").getAsInt() != 0;
            byte[] head = new byte[24];
            bin.readFully(head);
            c.bytes = new byte[Probe.CHUNK_BYTES];
            bin.readFully(c.bytes);
            out.put(c.dim + "," + c.cx + "," + c.cz, c);
        }
        state.close();
        bin.close();
        return out;
    }

    static final class Ent
    {
        String uuid, cls;
        int dim, player;
        JsonObject nbt;
        String chunk;
    }

    static Map<String, Ent> entities(File dir) throws Exception
    {
        Map<String, Ent> out = new LinkedHashMap<String, Ent>();
        BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(new File(dir, "entities.jsonl")), "UTF-8"));
        String line;
        while ((line = r.readLine()) != null)
        {
            JsonObject o = Rows.parse(line);
            Ent e = new Ent();
            e.cls = o.get("class").getAsString();
            e.dim = o.get("dim").getAsInt();
            e.player = o.get("player").getAsInt();
            e.nbt = o.getAsJsonObject("nbt");
            e.uuid = e.nbt.get("UUIDMost").getAsString() + "/" + e.nbt.get("UUIDLeast").getAsString();
            JsonArray pos = e.nbt.getAsJsonArray("Pos");
            double x = Double.longBitsToDouble(Long.parseUnsignedLong(pos.get(0).getAsString().substring(2), 16));
            double z = Double.longBitsToDouble(Long.parseUnsignedLong(pos.get(2).getAsString().substring(2), 16));
            e.chunk = e.dim + "," + (int)Math.floor(x / 16.0) + "," + (int)Math.floor(z / 16.0);
            out.put(e.player == 1 ? "player" : e.uuid, e);
        }
        r.close();
        return out;
    }

    static Map<String, String> ticks(File dir) throws Exception
    {
        Map<String, String> out = new HashMap<String, String>();
        BufferedReader r = new BufferedReader(new InputStreamReader(new GZIPInputStream(new FileInputStream(new File(dir, "ticks.jsonl.gz"))), "UTF-8"));
        String line;
        while ((line = r.readLine()) != null)
        {
            JsonObject o = Rows.parse(line);
            int dim = o.has("dim") ? o.get("dim").getAsInt() : 0;
            int x = o.get("x").getAsInt(), z = o.get("z").getAsInt();
            String key = dim + "," + x + "," + o.get("y").getAsInt() + "," + z + "," + o.get("id").getAsInt()
                + "," + o.get("t").getAsString() + "," + o.get("pri").getAsInt();
            out.put(key, dim + "," + (x >> 4) + "," + (z >> 4));
        }
        r.close();
        return out;
    }

    static JsonObject compare(File pre, File post, Set<String> joinWrites) throws Exception
    {
        TreeMap<String, Integer> reset = new TreeMap<String, Integer>();
        JsonArray fails = new JsonArray();

        Map<String, Chunk> a = chunks(pre), b = chunks(post);
        Set<String> both = new HashSet<String>(a.keySet());
        both.retainAll(b.keySet());
        bump(reset, "chunk-set", a.size() + b.size() - 2 * both.size());

        // chunks the join ticks wrote to, for the light reset
        Set<String> written = new HashSet<String>();
        for (String w : joinWrites)
        {
            String[] p = w.split(",");
            written.add(p[0] + "," + (Integer.parseInt(p[1]) >> 4) + "," + (Integer.parseInt(p[3]) >> 4));
        }

        int sky = 3 * 65536, blk = 4 * 65536, caches = 5 * 65536;
        for (String k : both)
        {
            Chunk ca = a.get(k), cb = b.get(k);
            for (int i = 0; i < 65536; ++i)
            {
                int x = ca.cx * 16 + (i >> 12), z = ca.cz * 16 + (i >> 8 & 15), y = i & 255;
                boolean id = ca.bytes[2 * i] != cb.bytes[2 * i] || ca.bytes[2 * i + 1] != cb.bytes[2 * i + 1];
                boolean meta = ca.bytes[2 * 65536 + i] != cb.bytes[2 * 65536 + i];
                if (id || meta)
                {
                    if (joinWrites.contains(ca.dim + "," + x + "," + y + "," + z)) bump(reset, "join-write", 1);
                    else fail(fails, "block " + ca.dim + "," + x + "," + y + "," + z + (id ? " id" : " meta"));
                }
                if (ca.bytes[sky + i] != cb.bytes[sky + i] || ca.bytes[blk + i] != cb.bytes[blk + i])
                {
                    if (written.contains(k)) bump(reset, "light", 1);
                    else fail(fails, "light " + ca.dim + "," + x + "," + y + "," + z);
                }
            }
            // heightMap must survive; heightMapMinimum and the precipitation
            // map are caches the loader rebuilds
            for (int i = 0; i < 256 * 4; ++i)
                if (ca.bytes[caches + i] != cb.bytes[caches + i]) { fail(fails, "heightMap chunk " + k); break; }
            for (int i = caches + 256 * 4; i < Probe.CHUNK_BYTES; ++i)
                if (ca.bytes[i] != cb.bytes[i]) { bump(reset, "chunk-cache", 1); break; }
            if (!ca.tiles.equals(cb.tiles)) fail(fails, "tile entities chunk " + k);
        }

        Map<String, Ent> ea = entities(pre), eb = entities(post);
        for (Ent e : ea.values())
        {
            if (e.player == 1) continue;
            Ent f = eb.get(e.uuid);
            if (f == null)
            {
                if (both.contains(e.chunk)) fail(fails, "entity lost " + e.cls + " " + e.uuid);
                else bump(reset, "entity-unloaded", 1);
            }
            else if (!e.nbt.equals(f.nbt))
            {
                JsonObject na = Rows.parse(e.nbt.toString());
                JsonArray rot = na.getAsJsonArray("Rotation");
                float yaw = Float.intBitsToFloat((int)Long.parseLong(rot.get(0).getAsString().substring(2), 16)) % 360.0F;
                JsonArray wrapped = new JsonArray();
                wrapped.add(new com.google.gson.JsonPrimitive(String.format("f:%08x", Float.floatToRawIntBits(yaw))));
                wrapped.add(rot.get(1));
                na.add("Rotation", wrapped);
                if (na.equals(f.nbt)) bump(reset, "rotation-wrap", 1);
                else fail(fails, "entity nbt " + e.cls + " " + e.uuid + ": " + firstDiff(e.nbt, f.nbt));
            }
        }
        for (Ent f : eb.values())
        {
            if (f.player == 1 || ea.containsKey(f.uuid)) continue;
            bump(reset, both.contains(f.chunk) ? "entity-spawned" : "entity-unloaded", 1);
        }

        Ent pa = ea.get("player"), pb = eb.get("player");
        if (pa == null || pb == null) fail(fails, "player missing");
        else
        {
            JsonObject na = Rows.parse(pa.nbt.toString()), nb = Rows.parse(pb.nbt.toString());
            for (String key : PLAYER_RESET)
            {
                JsonElement va = na.remove(key), vb = nb.remove(key);
                if (va == null ? vb != null : !va.equals(vb)) bump(reset, "player-join:" + key, 1);
            }
            if (!na.equals(nb)) fail(fails, "player nbt: " + firstDiff(na, nb));
        }

        // a tick the pre side still had is gone only because a join tick ran
        // it, and a new one was scheduled by a join tick
        long timeA = totalTime(pre), timeB = totalTime(post);
        Map<String, String> ta = ticks(pre), tb = ticks(post);
        for (Map.Entry<String, String> t : ta.entrySet())
            if (!tb.containsKey(t.getKey()))
            {
                if (!both.contains(t.getValue()) || scheduled(t.getKey()) <= timeB) bump(reset, "pending-tick", 1);
                else fail(fails, "pending tick lost " + t.getKey());
            }
        for (Map.Entry<String, String> t : tb.entrySet())
            if (!ta.containsKey(t.getKey()))
            {
                if (!both.contains(t.getValue()) || scheduled(t.getKey()) > timeA) bump(reset, "pending-tick", 1);
                else if (!a.get(t.getValue()).modified) bump(reset, "stale-save", 1);
                else fail(fails, "pending tick new " + t.getKey());
            }

        JsonObject r = new JsonObject();
        r.addProperty("ok", fails.size() == 0);
        r.addProperty("chunksBoth", both.size());
        r.addProperty("entitiesPre", ea.size());
        r.addProperty("entitiesPost", eb.size());
        JsonObject rs = new JsonObject();
        for (Map.Entry<String, Integer> e : reset.entrySet()) rs.addProperty(e.getKey(), e.getValue());
        r.add("reset", rs);
        r.add("fails", fails);
        System.out.println("ORACLE ROUNDTRIP " + (fails.size() == 0 ? "OK" : "FAIL") + " " + r);
        return r;
    }

    static long scheduled(String key)
    {
        String[] p = key.split(",");
        return Long.parseLong(p[5].substring(2));
    }

    static long totalTime(File dir) throws Exception
    {
        byte[] b = java.nio.file.Files.readAllBytes(new File(dir, "worldinfo.nbt").toPath());
        return Long.parseLong(Rows.parse(new String(b, "UTF-8")).get("Time").getAsString().substring(2));
    }

    static void bump(Map<String, Integer> m, String k, int n)
    {
        if (n == 0) return;
        Integer v = m.get(k);
        m.put(k, (v == null ? 0 : v) + n);
    }

    static void fail(JsonArray fails, String what)
    {
        if (fails.size() < 50) fails.add(new com.google.gson.JsonPrimitive(what));
    }

    static String firstDiff(JsonObject a, JsonObject b)
    {
        Set<String> keys = new java.util.TreeSet<String>();
        for (Map.Entry<String, JsonElement> e : a.entrySet()) keys.add(e.getKey());
        for (Map.Entry<String, JsonElement> e : b.entrySet()) keys.add(e.getKey());
        for (String k : keys)
        {
            JsonElement va = a.get(k), vb = b.get(k);
            if (va == null ? vb != null : !va.equals(vb)) return k + " " + va + " -> " + vb;
        }
        return "?";
    }
}
