package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.security.MessageDigest;
import net.minecraft.client.Minecraft;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;

/** A segment boundary is a vanilla Save and Quit, followed by a copy of its save. */
final class Checkpoint
{
    private Checkpoint() {}

    static JsonObject manifest(File dir) throws IOException
    {
        File f = new File(dir, "manifest.json");
        if (!f.isFile() || !new File(dir, "save/level.dat").isFile())
            throw new IOException("checkpoint is missing its manifest or save: " + dir);
        byte[] bytes = Files.readAllBytes(f.toPath());
        return Rows.parse(new String(bytes, "UTF-8"));
    }

    /** Both files verify() reads exist: a cheap existence probe without the exception. */
    static boolean complete(File dir)
    {
        return new File(dir, "manifest.json").isFile() && new File(dir, "save/level.dat").isFile();
    }

    static long verify(File dir, long seed, JsonObject world, boolean dev) throws IOException
    {
        JsonObject m = manifest(dir);
        if (m.get("seed").getAsLong() != seed || !m.getAsJsonObject("world").equals(world))
            throw new IOException("checkpoint seed or world differs: " + dir);
        if (m.get("dev").getAsBoolean() && !dev)
            throw new IOException("checkpoint needs --dev: " + dir);
        return m.get("totalTime").getAsLong();
    }

    static void install(File dir, File target) throws IOException
    {
        copyTree(new File(dir, "save"), target);
    }

    static void copyTree(File src, File dst) throws IOException
    {
        if (src.isDirectory())
        {
            if (!dst.exists() && !dst.mkdirs()) throw new IOException("cannot create " + dst);
            File[] children = src.listFiles();
            if (children == null) throw new IOException("cannot list " + src);
            for (File c : children) copyTree(c, new File(dst, c.getName()));
        }
        else
        {
            Files.copy(src.toPath(), dst.toPath());
            // a shared out/ tree keeps its recordings read-only; the installed
            // save is this run's working copy and must be writable (the game
            // rewrites session.lock and level.dat on every launch)
            File f = dst.toPath().toFile();
            if (!f.canWrite() && !f.setWritable(true)) throw new IOException("cannot make writable: " + dst);
        }
    }

    static String sha256(File file)
    {
        try
        {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            FileInputStream in = new FileInputStream(file);
            byte[] b = new byte[65536];
            int n;
            while ((n = in.read(b)) != -1) md.update(b, 0, n);
            in.close();
            StringBuilder out = new StringBuilder();
            for (byte v : md.digest()) out.append(String.format("%02x", v & 255));
            return out.toString();
        }
        catch (Exception e) { throw new IllegalStateException("sha256 " + file, e); }
    }

    /** The server block writes of a --from run's join ticks, "dim,x,y,z": the roundtrip exempts them. */
    static final java.util.Set<String> joinWrites = new java.util.HashSet<String>();

    static void recordJoinWrites()
    {
        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(net.minecraft.world.World w, int x, int y, int z, int id, int meta)
            {
                if (!Oracle.joined) joinWrites.add(w.provider.dimensionId + "," + x + "," + y + "," + z);
            }
        };
    }

    static JsonObject quiescence(Minecraft mc, IntegratedServer server)
    {
        JsonObject q = new JsonObject();
        EntityPlayerMP p = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        q.addProperty("guiClosed", mc.currentScreen == null);
        q.addProperty("onGround", p.onGround);
        q.addProperty("noPortal", Snapshot.floatField(mc.thePlayer, "timeInPortal") == 0.0F);
        q.addProperty("noItemUse", p.getItemInUse() == null);
        JsonArray inFlight = new JsonArray();
        for (WorldServer w : server.worldServers)
        {
            if (w == null) continue;
            for (Object o : w.loadedEntityList)
            {
                Entity e = (Entity)o;
                String cls = e.getClass().getSimpleName();
                if (cls.equals("EntityEnderEye") || cls.equals("EntityArrow") || cls.contains("Fireball"))
                    inFlight.add(new JsonPrimitive(cls + ":" + e.getEntityId()));
            }
        }
        q.add("inFlight", inFlight);
        return q;
    }

    static void save(Minecraft mc, File dir, String name) throws Exception
    {
        if (Oracle.tapePath == null) throw new IOException("save needs a tape");
        if (dir.exists()) throw new IOException("checkpoint already exists: " + dir);
        IntegratedServer server = mc.getIntegratedServer();
        if (server == null || server.getConfigurationManager().playerEntityList.isEmpty())
            throw new IOException("save needs a joined player");
        EntityPlayerMP p = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        JsonObject q = quiescence(mc, server);
        JsonObject m = new JsonObject();
        m.addProperty("kind", "netherite-checkpoint");
        m.addProperty("v", 1);
        m.addProperty("name", name);
        m.addProperty("seed", Oracle.seed);
        m.add("world", WorldConf.json());
        m.addProperty("harness", Oracle.harness);
        m.addProperty("totalTime", server.worldServers[0].getWorldInfo().getWorldTotalTime());
        m.addProperty("dimension", p.dimension);
        JsonArray pos = new JsonArray();
        pos.add(new JsonPrimitive(p.posX)); pos.add(new JsonPrimitive(p.posY)); pos.add(new JsonPrimitive(p.posZ));
        m.add("position", pos);
        m.addProperty("dev", Oracle.dev);
        if (Oracle.fromDir != null)
        {
            JsonObject parent = new JsonObject();
            parent.addProperty("name", manifest(new File(Oracle.fromDir)).get("name").getAsString());
            parent.addProperty("manifestSha256", sha256(new File(Oracle.fromDir, "manifest.json")));
            m.add("parent", parent);
        }
        else m.add("parent", new JsonObject());
        m.add("quiescence", q);

        // the roundtrip gate's reference: the state this save is of
        if (!Oracle.saveNoPre)
        {
            JsonObject snap = new JsonObject();
            snap.addProperty("out", new File(dir, "pre").getPath());
            Snapshot.run(server, snap);
        }

        // The server is parked between tick pairs. This is MinecraftServer's
        // Save and Quit path, including player data, all dimensions and flush.
        server.stopServer();
        Rows.close();
        File source = new File(new File(mc.mcDataDir, "saves"), Oracle.worldName);
        File copy = new File(dir, "save");
        copyTree(source, copy);
        JsonObject tape = new JsonObject();
        // The producing tape travels with the checkpoint: the manifest's
        // relative path stays valid in another lane or checkout, and the
        // digest says which bytes it is.
        Files.copy(new File(Oracle.tapePath).toPath(), new File(dir, "tape.jsonl").toPath());
        tape.addProperty("path", "tape.jsonl");
        tape.addProperty("sha256", sha256(new File(dir, "tape.jsonl")));
        m.add("tape", tape);
        Files.write(new File(dir, "manifest.json").toPath(), m.toString().getBytes("UTF-8"));
        System.out.println("ORACLE CHECKPOINT " + dir + " totalTime=" + m.get("totalTime"));
    }
}
