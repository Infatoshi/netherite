package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTTagByte;
import net.minecraft.nbt.NBTTagByteArray;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagDouble;
import net.minecraft.nbt.NBTTagFloat;
import net.minecraft.nbt.NBTTagInt;
import net.minecraft.nbt.NBTTagIntArray;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.nbt.NBTTagLong;
import net.minecraft.nbt.NBTTagShort;
import net.minecraft.nbt.NBTTagString;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.gen.ChunkProviderGenerate;
import net.minecraft.world.gen.ChunkProviderHell;
import net.minecraft.world.gen.structure.MapGenMineshaft;
import net.minecraft.world.gen.structure.MapGenNetherBridge;
import net.minecraft.world.gen.structure.MapGenScatteredFeature;
import net.minecraft.world.gen.structure.MapGenStronghold;
import net.minecraft.world.gen.structure.MapGenStructure;
import net.minecraft.world.gen.structure.MapGenStructureData;
import net.minecraft.world.gen.structure.MapGenStructureIO;
import net.minecraft.world.gen.structure.MapGenVillage;
import net.minecraft.world.gen.structure.StructureStart;

/**
 * Every structure start the real game computes for a region, the reference for
 * the native ports of mineshafts, strongholds, temples, villages and the Nether
 * fortress. A start (which chunk it begins in, its tree of pieces with bounding
 * boxes and settings) is a pure function of the world seed and the start chunk;
 * blocks are placed later, during population, and are not part of this probe.
 *
 * The dimension comes from the command's "dim" (default 0, the overworld): 0
 * offers the region to four fresh generators (mineshaft, stronghold, temple,
 * village) on the overworld, and -1 offers it to a fresh MapGenNetherBridge on
 * the world server for dimension -1. A manifest records "dim" only when it is
 * not 0, so an overworld dump stays byte-identical.
 *
 * Every fresh generator is handed a private empty MapGenStructureData, so it
 * neither reads nor writes the world's saved structure data, then every chunk
 * of the region is offered to each of them with the world's own chunk provider.
 * That is exactly how the game
 * discovers starts: generating a chunk tests every chunk within 8 of it, once,
 * and a chunk that did not spawn a structure is re-tested on the next one. A
 * candidate's RNG is seeded from its own coordinates (MapGenBase.func_151539_a),
 * so the outcome does not depend on the order chunks are offered. Starts whose
 * start chunk lies up to 8 chunks outside the region are found too; that is the
 * same set a real run that generated exactly this region would hold.
 *
 * Runs on its own thread (the OTHER role) while the server is parked, so no
 * CLIENT or SERVER RNG stream moves (MapGenBase seeds its Random per generator
 * from Det at construction).
 *
 * Output DIR/starts.jsonl, one line per start, sorted by type then cx then cz:
 *
 *   {"type":"Mineshaft","cx":12,"cz":-7,"nbt":<canonical start NBT>}
 *
 * and DIR/manifest.json with the seed, the region, the count per type and the
 * stronghold positions. The canonical NBT form: compound = object with keys
 * sorted by String.compareTo, list = array, scalars = strings carrying the type,
 * "b:1" "s:1" "i:-3" "l:5" "f:23800000" (raw float bits, 8 lowercase hex digits)
 * "d:..." (raw double bits, 16), "str:text", "ba:1,2,3", "ia:1,2,3" (empty
 * "ba:" / "ia:"). No whitespace anywhere.
 */
final class StructuresProbe
{
    private StructuresProbe() {}

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    result[0] = probe(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle StructuresProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject probe(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int x0 = cmd.get("x0").getAsInt(), z0 = cmd.get("z0").getAsInt();
        int x1 = cmd.get("x1").getAsInt(), z1 = cmd.get("z1").getAsInt();
        boolean check = cmd.has("check") && cmd.get("check").getAsBoolean();
        int dim = cmd.has("dim") ? cmd.get("dim").getAsInt() : 0;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        WorldServer ws = dim == 0 ? server.worldServers[0] : server.worldServerForDimension(dim);
        long seed = ws.getSeed();
        IChunkProvider provider = ws.getChunkProvider();
        boolean nether = dim == -1;

        MapGenStructure[] gens;
        MapGenStronghold stronghold = null;
        if (nether)
        {
            // ChunkProviderHell's genNetherBridge: the fortress generator of the
            // Nether world, with "Fortress" as its start type name.
            gens = new MapGenStructure[] {new MapGenNetherBridge()};
        }
        else
        {
            MapGenMineshaft mineshaft = new MapGenMineshaft();
            stronghold = new MapGenStronghold();
            MapGenScatteredFeature temple = new MapGenScatteredFeature();
            // The server's terrain generator is ChunkProviderGenerate, whose
            // village generator is the no-argument MapGenVillage: size (terrain
            // type) 0, the default world.
            MapGenVillage village = new MapGenVillage();
            gens = new MapGenStructure[] {mineshaft, stronghold, temple, village};
        }
        Field dataField = field(MapGenStructure.class, "field_143029_e");

        for (MapGenStructure g : gens)
        {
            dataField.set(g, new MapGenStructureData(g.func_143025_a()));
        }

        for (int cx = x0; cx <= x1; ++cx)
        {
            for (int cz = z0; cz <= z1; ++cz)
            {
                for (MapGenStructure g : gens) g.func_151539_a(provider, ws, cx, cz, null);
            }
        }

        List<Start> starts = new ArrayList<Start>();
        JsonObject counts = new JsonObject();
        Field mapField = field(MapGenStructure.class, "structureMap");
        for (MapGenStructure g : gens)
        {
            String type = g.func_143025_a();
            Map map = (Map)mapField.get(g);
            for (Object o : map.entrySet())
            {
                Map.Entry entry = (Map.Entry)o;
                long key = (Long)entry.getKey();
                // ChunkCoordIntPair.chunkXZ2Int: chunk x low 32 bits, chunk z high
                starts.add(new Start(type, (int)key, (int)(key >> 32), (StructureStart)entry.getValue()));
            }
            counts.addProperty(type, map.size());
        }
        Collections.sort(starts);

        Map<String, String> probed = new HashMap<String, String>();
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "starts.jsonl")), "UTF-8"));
        for (Start s : starts)
        {
            JsonElement nbt = canon(s.start.func_143021_a(s.cx, s.cz));
            probed.put(key(s.type, s.cx, s.cz), nbt.toString());
            JsonObject line = new JsonObject();
            line.addProperty("type", s.type);
            line.addProperty("cx", s.cx);
            line.addProperty("cz", s.cz);
            line.add("nbt", nbt);
            w.println(line.toString());
        }
        w.close();

        int checked = 0, mismatches = 0;
        StringBuilder first = new StringBuilder();
        if (check)
        {
            for (Start s : starts)
            {
                NBTTagCompound tag = s.start.func_143021_a(s.cx, s.cz);
                JsonElement want = canon(tag);
                StructureStart back = MapGenStructureIO.func_143035_a(tag, ws);
                ++checked;
                if (back == null || back.func_143019_e() != s.cx || back.func_143018_f() != s.cz
                    || !want.equals(canon(back.func_143021_a(s.cx, s.cz))))
                {
                    ++mismatches;
                    if (mismatches <= 3) first.append(" ").append(s.type).append("(").append(s.cx).append(",").append(s.cz).append(")");
                }
            }
        }

        int worldChecked = 0, worldMissing = 0, worldMismatch = 0, worldChunks = 0;
        JsonArray worldCenters = new JsonArray();
        StringBuilder worldFirst = new StringBuilder();
        if (check)
        {
            // Independent cross-check on the real path: a throwaway terrain
            // generator with map features on (ChunkProviderGenerate in the
            // overworld, ChunkProviderHell in the Nether) generates a 3x3 box of
            // chunks around up to three probed starts of each type through
            // provideChunk, the call the game itself makes. Its own generators
            // (given the same private empty structure data) must then hold
            // exactly the starts this probe found for those chunks. The box has
            // to lie inside the probed region, so every candidate chunk it can
            // reach is one the probe already offered.
            List<int[]> centers = new ArrayList<int[]>();
            for (String t : nether ? new String[] {"Fortress"}
                                   : new String[] {"Mineshaft", "Stronghold", "Temple", "Village"})
            {
                List<Start> fit = new ArrayList<Start>();

                for (Start s : starts)
                {
                    if (s.type.equals(t) && s.cx - 1 >= x0 && s.cx + 1 <= x1 && s.cz - 1 >= z0 && s.cz + 1 <= z1) fit.add(s);
                }

                // up to three per type, spread over the region: first, middle, last
                Set<Integer> pick = new TreeSet<Integer>();
                pick.add(Integer.valueOf(0));
                pick.add(Integer.valueOf(fit.size() / 2));
                pick.add(Integer.valueOf(fit.size() - 1));

                for (Integer i : pick)
                {
                    if (i.intValue() < 0 || i.intValue() >= fit.size()) continue;

                    Start s = fit.get(i.intValue());
                    centers.add(new int[] {s.cx, s.cz});
                    JsonArray c = new JsonArray();
                    c.add(new JsonPrimitive(s.cx));
                    c.add(new JsonPrimitive(s.cz));
                    worldCenters.add(c);
                }
            }

            if (!centers.isEmpty())
            {
                ChunkProviderGenerate real = null;
                ChunkProviderHell hell = null;
                MapGenStructure[] rg;
                if (nether)
                {
                    // Its genNetherBridge is public; the throwaway gets a fresh
                    // one so the world's own generator is untouched.
                    hell = new ChunkProviderHell(ws, seed);
                    hell.genNetherBridge = new MapGenNetherBridge();
                    rg = new MapGenStructure[] {hell.genNetherBridge};
                }
                else
                {
                    real = new ChunkProviderGenerate(ws, seed, true);
                    rg = new MapGenStructure[] {
                        (MapGenStructure)field(ChunkProviderGenerate.class, "mineshaftGenerator").get(real),
                        (MapGenStructure)field(ChunkProviderGenerate.class, "strongholdGenerator").get(real),
                        (MapGenStructure)field(ChunkProviderGenerate.class, "scatteredFeatureGenerator").get(real),
                        (MapGenStructure)field(ChunkProviderGenerate.class, "villageGenerator").get(real)};
                }
                for (MapGenStructure g : rg) dataField.set(g, new MapGenStructureData(g.func_143025_a()));

                Trace.pause();
                for (int[] c : centers)
                {
                    for (int cx = c[0] - 1; cx <= c[0] + 1; ++cx)
                    {
                        for (int cz = c[1] - 1; cz <= c[1] + 1; ++cz)
                        {
                            if (nether) hell.provideChunk(cx, cz);
                            else real.provideChunk(cx, cz);
                            ++worldChunks;
                        }
                    }
                }
                Trace.resume();

                for (MapGenStructure g : rg)
                {
                    String type = g.func_143025_a();
                    for (Object o : ((Map)mapField.get(g)).entrySet())
                    {
                        Map.Entry entry = (Map.Entry)o;
                        long k = (Long)entry.getKey();
                        int cx = (int)k, cz = (int)(k >> 32);
                        String want = probed.get(key(type, cx, cz));
                        ++worldChecked;
                        if (want == null)
                        {
                            ++worldMissing;
                            if (worldMissing <= 3) worldFirst.append(" ").append(type).append("(").append(cx).append(",").append(cz).append(") not probed");
                        }
                        else if (!want.equals(canon(((StructureStart)entry.getValue()).func_143021_a(cx, cz)).toString()))
                        {
                            ++worldMismatch;
                            if (worldMismatch <= 3) worldFirst.append(" ").append(type).append("(").append(cx).append(",").append(cz).append(") differs");
                        }
                    }
                }
            }
        }

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        // Only a non-overworld dimension records which one it was, so an
        // overworld dump is byte-identical to the files made before this key.
        if (dim != 0) m.addProperty("dim", dim);
        m.addProperty("x0", x0);
        m.addProperty("z0", z0);
        m.addProperty("x1", x1);
        m.addProperty("z1", z1);
        m.add("counts", counts);
        JsonArray sh = new JsonArray();
        if (stronghold != null)
        {
            for (ChunkCoordIntPair p : (ChunkCoordIntPair[])field(MapGenStronghold.class, "structureCoords").get(stronghold))
            {
                JsonArray a = new JsonArray();
                a.add(new JsonPrimitive(p.chunkXPos));
                a.add(new JsonPrimitive(p.chunkZPos));
                sh.add(a);
            }
        }
        m.add("stronghold", sh);
        m.addProperty("order", "type, then chunk cx, then cz");
        m.addProperty("canonical", "compound: object with String.compareTo key order; list: array; scalar strings b: s: i: l: f:<8 hex raw float bits> d:<16 hex raw double bits> str: ba: ia:");
        if (check)
        {
            m.addProperty("checked", checked);
            m.addProperty("mismatches", mismatches);
            m.addProperty("worldChecked", worldChecked);
            m.addProperty("worldMissing", worldMissing);
            m.addProperty("worldMismatch", worldMismatch);
            m.addProperty("worldChunks", worldChunks);
            m.add("worldCenters", worldCenters);
        }
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        if (mismatches != 0) throw new IllegalStateException("round trip: " + mismatches + " of " + checked + " starts differ:" + first);
        if (worldMismatch != 0 || worldMissing != 0)
            throw new IllegalStateException("provideChunk cross-check: " + worldMismatch + " of " + worldChecked + " starts differ, " + worldMissing + " not probed:" + worldFirst);

        JsonObject r = new JsonObject();
        r.addProperty("starts", starts.size());
        r.add("counts", counts);
        r.add("stronghold", sh);
        if (check)
        {
            r.addProperty("checked", checked);
            r.addProperty("mismatches", mismatches);
            r.addProperty("worldChecked", worldChecked);
            r.addProperty("worldMissing", worldMissing);
            r.addProperty("worldMismatch", worldMismatch);
            r.addProperty("worldChunks", worldChunks);
            r.add("worldCenters", worldCenters);
        }
        r.addProperty("dir", dir.getPath());
        return r;
    }

    /** The map key of one start: its type and the chunk it begins in. */
    private static String key(String type, int cx, int cz)
    {
        return type + " " + cx + " " + cz;
    }

    /** One start, keyed by the chunk it begins in. */
    private static final class Start implements Comparable<Start>
    {
        final String type;
        final int cx, cz;
        final StructureStart start;

        Start(String type, int cx, int cz, StructureStart start)
        {
            this.type = type;
            this.cx = cx;
            this.cz = cz;
            this.start = start;
        }

        public int compareTo(Start o)
        {
            int c = type.compareTo(o.type);
            if (c != 0) return c;
            c = Integer.compare(cx, o.cx);
            return c != 0 ? c : Integer.compare(cz, o.cz);
        }
    }

    /** Canonical JSON for one NBT tag, no whitespace and no list element type. */
    static JsonElement canon(NBTBase tag)
    {
        switch (tag.getId())
        {
            case 1: return new JsonPrimitive("b:" + ((NBTTagByte)tag).func_150290_f());
            case 2: return new JsonPrimitive("s:" + ((NBTTagShort)tag).func_150289_e());
            case 3: return new JsonPrimitive("i:" + ((NBTTagInt)tag).func_150287_d());
            case 4: return new JsonPrimitive("l:" + ((NBTTagLong)tag).func_150291_c());
            case 5: return new JsonPrimitive("f:" + hex(Float.floatToRawIntBits(((NBTTagFloat)tag).func_150288_h()) & 4294967295L, 8));
            case 6: return new JsonPrimitive("d:" + hex(Double.doubleToRawLongBits(((NBTTagDouble)tag).func_150286_g()), 16));
            case 7:
            {
                byte[] a = ((NBTTagByteArray)tag).func_150292_c();
                StringBuilder b = new StringBuilder("ba:");
                for (int i = 0; i < a.length; ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(a[i]);
                }
                return new JsonPrimitive(b.toString());
            }
            case 8: return new JsonPrimitive("str:" + ((NBTTagString)tag).func_150285_a_());
            case 9:
            {
                JsonArray out = new JsonArray();
                for (Object o : list((NBTTagList)tag)) out.add(canon((NBTBase)o));
                return out;
            }
            case 10:
            {
                NBTTagCompound c = (NBTTagCompound)tag;
                String[] keys = (String[])c.func_150296_c().toArray(new String[0]);
                java.util.Arrays.sort(keys);
                JsonObject out = new JsonObject();
                for (String k : keys) out.add(k, canon(c.getTag(k)));
                return out;
            }
            case 11:
            {
                int[] a = ((NBTTagIntArray)tag).func_150302_c();
                StringBuilder b = new StringBuilder("ia:");
                for (int i = 0; i < a.length; ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(a[i]);
                }
                return new JsonPrimitive(b.toString());
            }
            default: throw new IllegalStateException("cannot canonicalize NBT tag id " + tag.getId());
        }
    }

    private static List list(NBTTagList tag) throws IllegalStateException
    {
        try
        {
            return (List)field(NBTTagList.class, "tagList").get(tag);
        }
        catch (Exception e)
        {
            throw new IllegalStateException("NBTTagList.tagList", e);
        }
    }

    private static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    private static String hex(long bits, int digits)
    {
        StringBuilder b = new StringBuilder();
        for (int i = digits - 1; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));
        return b.toString();
    }
}
