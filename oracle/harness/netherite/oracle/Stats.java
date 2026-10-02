package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import net.minecraft.client.Minecraft;
import net.minecraft.client.network.NetHandlerPlayClient;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatFileWriter;
import net.minecraft.stats.StatisticsFile;

/**
 * The player's statistics, the state stats/<uuid>.json saves and the S37
 * packets send: the server player's StatisticsFile (its map in the vanilla
 * file form, StatisticsFile.func_150880_a, with keys and progress sets
 * sorted; the dirty set field_150888_e as sorted stat ids; field_150885_f,
 * the tick counter of the last send, and field_150886_g) and the client's
 * StatFileWriter mirror with NetHandlerPlayClient.field_147308_k.
 *
 * Snapshot writes it as stats.json. {"cmd":"run","class":"Stats"} appends one
 * line {"tick": Oracle.tick, ...} to stats.jsonl beside the tape (or in
 * "out"): the first dump of a run truncates the file. The native replay
 * compares its own store against each line at that tick. Read-only: never
 * draws from a Random.
 */
final class Stats
{
    private Stats() {}

    private static boolean written;

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File dir = new File(cmd.has("out") ? cmd.get("out").getAsString() : Snapshot.defaultOut());
        dir.mkdirs();
        JsonObject o = new JsonObject();
        o.addProperty("tick", Oracle.tick);
        JsonObject s = state(server);
        for (Map.Entry<String, JsonElement> e : s.entrySet()) o.add(e.getKey(), e.getValue());
        File f = new File(dir, "stats.jsonl");
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f, written), "UTF-8"));
        w.println(o.toString());
        w.close();
        written = true;
        System.out.println("ORACLE STATS " + f + " t=" + Oracle.tick);
        JsonObject r = new JsonObject();
        r.addProperty("file", f.getPath());
        r.addProperty("tick", Oracle.tick);
        return r;
    }

    static JsonObject state(IntegratedServer server) throws Exception
    {
        List players = server.getConfigurationManager().playerEntityList;
        if (players.isEmpty()) throw new IllegalStateException("Stats: no server player");
        EntityPlayerMP p = (EntityPlayerMP)players.get(0);
        StatisticsFile sf = p.func_147099_x();
        JsonObject o = new JsonObject();
        o.add("file", file(sf));
        List<String> dirty = new ArrayList<String>();
        for (Object st : (Set)get(StatisticsFile.class, sf, "field_150888_e")) dirty.add(((StatBase)st).statId);
        Collections.sort(dirty);
        JsonArray d = new JsonArray();
        for (String id : dirty) d.add(new com.google.gson.JsonPrimitive(id));
        o.add("dirty", d);
        o.addProperty("lastSend", (Integer)get(StatisticsFile.class, sf, "field_150885_f"));
        o.addProperty("achDirty", (Boolean)get(StatisticsFile.class, sf, "field_150886_g"));
        o.addProperty("tickCounter", server.getTickCounter());

        Minecraft mc = Oracle.mc;
        if (mc != null && mc.thePlayer != null)
        {
            JsonObject c = new JsonObject();
            c.add("file", file(mc.thePlayer.func_146107_m()));
            NetHandlerPlayClient nh = mc.getNetHandler();
            if (nh != null) c.addProperty("has", (Boolean)get(NetHandlerPlayClient.class, nh, "field_147308_k"));
            c.addProperty("hint", mc.gameSettings.showInventoryAchievementHint);
            o.add("client", c);
        }
        return o;
    }

    /** The vanilla file form with the keys and each progress array sorted. */
    static JsonObject file(StatFileWriter w) throws Exception
    {
        Map map = (Map)get(StatFileWriter.class, w, "field_150875_a");
        JsonObject raw = new JsonParser().parse(StatisticsFile.func_150880_a(map)).getAsJsonObject();
        TreeMap<String, JsonElement> sorted = new TreeMap<String, JsonElement>();
        for (Map.Entry<String, JsonElement> e : raw.entrySet()) sorted.put(e.getKey(), e.getValue());
        JsonObject out = new JsonObject();
        for (Map.Entry<String, JsonElement> e : sorted.entrySet())
        {
            JsonElement v = e.getValue();
            if (v.isJsonObject() && v.getAsJsonObject().has("progress") && v.getAsJsonObject().get("progress").isJsonArray())
            {
                List<String> names = new ArrayList<String>();
                for (JsonElement n : v.getAsJsonObject().getAsJsonArray("progress")) names.add(n.getAsString());
                Collections.sort(names);
                JsonArray a = new JsonArray();
                for (String n : names) a.add(new com.google.gson.JsonPrimitive(n));
                JsonObject vo = new JsonObject();
                vo.add("value", v.getAsJsonObject().get("value"));
                vo.add("progress", a);
                v = vo;
            }
            out.add(e.getKey(), v);
        }
        return out;
    }

    static Object get(Class c, Object o, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f.get(o);
    }
}
