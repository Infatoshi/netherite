package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.EnumCreatureType;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.gen.ChunkProviderHell;
import net.minecraft.world.gen.structure.StructureBoundingBox;
import net.minecraft.world.gen.structure.StructureStart;

/**
 * The Nether's monster spawn lists, the reference for the native
 * ChunkProviderHell.getPossibleCreatures (csrc/engine/spawning.c,
 * spawning_hell_list, checked by csrc/tests/test_fortress_spawn.c).
 *
 * The Nether spawn area loads exactly as SpawnDumpDim loads it (the same
 * loadChunk square, on a thread bound to Det's SERVER role), so the native
 * seedworld_build_dim reproduces the same fortress map. Then, for every start
 * in the map's HashMap order, a grid over its bounding box grown by MARGIN
 * (step STEP on all three axes, points in loaded chunks only, so the queries
 * never generate a chunk) asks ChunkProviderServer.getPossibleCreatures for
 * EnumCreatureType.monster and records which list came back: 0 the hell
 * biome's, 1 the fortress list through hasStructureAt (a component box holds
 * the point), 2 the fortress list through func_142038_b (the first start's
 * box meets the column and nether brick is below). The block below rides
 * along.
 *
 * Run: make run SEED=2 CLASS=FortressSpawnProbe NAME=fs-2 CMD='{"out":"/abs/out/java/fortress/fs-2"}'
 *
 * points.bin.gz: per point x, y, z int32 LE, the list byte, the block below
 * as uint16 LE. manifest.json: the starts (chunk, box) in map order and the
 * two lists' rows.
 */
final class FortressSpawnProbe
{
    private FortressSpawnProbe() {}

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                Thread oldClient = Det.clientThread, oldServer = Det.serverThread;
                boolean oldRender = Det.inRender;
                Det.clientThread = null;
                Det.serverThread = Thread.currentThread();
                Det.inRender = false;
                try
                {
                    result[0] = probe(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
                finally
                {
                    Det.clientThread = oldClient;
                    Det.serverThread = oldServer;
                    Det.inRender = oldRender;
                }
            }
        }, "Oracle FortressSpawnProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonArray rows(List list)
    {
        JsonArray a = new JsonArray();
        for (Object o : list)
        {
            BiomeGenBase.SpawnListEntry e = (BiomeGenBase.SpawnListEntry)o;
            JsonObject r = new JsonObject();
            r.addProperty("class", e.entityClass.getSimpleName());
            r.addProperty("weight", Snapshot.intField(e, "itemWeight"));
            r.addProperty("min", e.minGroupCount);
            r.addProperty("max", e.maxGroupCount);
            a.add(r);
        }
        return a;
    }

    static JsonObject probe(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File d = new File(cmd.get("out").getAsString());
        int step = cmd.has("step") ? cmd.get("step").getAsInt() : 2;
        int margin = cmd.has("margin") ? cmd.get("margin").getAsInt() : 6;
        d.mkdirs();

        WorldServer ws = server.worldServers[1];
        if (ws == null || ws.provider.dimensionId != -1) throw new IllegalStateException("FortressSpawnProbe: no Nether world");
        if (Probe.rawChunks) throw new IllegalStateException("FortressSpawnProbe: Probe.rawChunks is set");

        /* SpawnDumpDim's load: MinecraftServer.initialWorldChunkLoad's square
         * around the dimension's (0, 0) spawn */
        for (int a = -192; a <= 192; a += 16)
            for (int b = -192; b <= 192; b += 16)
                ws.theChunkProviderServer.loadChunk(a >> 4, b >> 4);

        ChunkProviderHell hell = (ChunkProviderHell)Snapshot.objField(ws.theChunkProviderServer, "currentChunkProvider");
        Map map = (Map)Snapshot.objField(hell.genNetherBridge, "structureMap");
        List fortress = hell.genNetherBridge.getSpawnList();
        List starts = new ArrayList(map.values());

        JsonArray jstarts = new JsonArray();
        int points = 0, n1 = 0, n2 = 0;
        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(d, "points.bin.gz"))));

        for (Object o : starts)
        {
            StructureStart s = (StructureStart)o;
            StructureBoundingBox bb = s.getBoundingBox();
            JsonObject js = new JsonObject();
            js.addProperty("cx", s.func_143019_e());
            js.addProperty("cz", s.func_143018_f());
            js.addProperty("box", bb.minX + "," + bb.minY + "," + bb.minZ + "," + bb.maxX + "," + bb.maxY + "," + bb.maxZ);
            js.addProperty("components", s.getComponents().size());
            jstarts.add(js);

            for (int x = bb.minX - margin; x <= bb.maxX + margin; x += step)
                for (int z = bb.minZ - margin; z <= bb.maxZ + margin; z += step)
                {
                    if (!ws.theChunkProviderServer.chunkExists(x >> 4, z >> 4)) continue;

                    for (int y = Math.max(1, bb.minY - margin); y <= Math.min(127, bb.maxY + margin); y += step)
                    {
                        List got = ws.theChunkProviderServer.getPossibleCreatures(EnumCreatureType.monster, x, y, z);
                        int kind = 0;
                        if (got == fortress) kind = hell.genNetherBridge.hasStructureAt(x, y, z) ? 1 : 2;
                        if (kind == 1) ++n1;
                        if (kind == 2) ++n2;
                        int below = Block.getIdFromBlock(ws.getBlock(x, y - 1, z));
                        out.writeInt(Integer.reverseBytes(x));
                        out.writeInt(Integer.reverseBytes(y));
                        out.writeInt(Integer.reverseBytes(z));
                        out.writeByte(kind);
                        out.writeShort(Short.reverseBytes((short)below));
                        ++points;
                    }
                }
        }
        out.close();

        JsonObject m = new JsonObject();
        m.addProperty("kind", "fortress_spawn");
        m.addProperty("seed", ws.getSeed());
        m.addProperty("dim", -1);
        m.addProperty("step", step);
        m.addProperty("margin", margin);
        m.addProperty("points", points);
        m.addProperty("structure", n1);
        m.addProperty("brick", n2);
        m.add("starts", jstarts);
        m.add("fortress", rows(fortress));
        m.add("hell", rows(BiomeGenBase.hell.getSpawnableList(EnumCreatureType.monster)));
        m.addProperty("layout", "points.bin.gz: x, y, z int32 LE, list uint8 (0 biome, 1 hasStructureAt, 2 func_142038_b and nether brick below), block below uint16 LE");
        Snapshot.writeFile(new File(d, "manifest.json"), m.toString());

        JsonObject r = new JsonObject();
        r.addProperty("points", points);
        r.addProperty("structure", n1);
        r.addProperty("brick", n2);
        r.addProperty("starts", starts.size());
        return r;
    }
}
