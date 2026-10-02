package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.world.IWorldAccess;
import net.minecraft.world.World;

/**
 * The render-state recorder's particle spawn log (--renderstate): an
 * IWorldAccess put first in the client world's worldAccesses list, ahead of
 * RenderGlobal, so each World.spawnParticle and World.playAuxSFX on the
 * client is logged before RenderGlobal acts on it. Every entry carries the
 * call's arguments as raw bits, the client world's Random and the client
 * Det streams (seeder, Math.random, entity IDs) as they stand before the
 * call (after it for onEntityCreate, whose constructor has run), and the vanilla caller that reached World (the stack frame below
 * World), so the native particle tick can replay each spawn from the very
 * state it started in. The log holds the current tick's calls: tickStart
 * clears it, and RenderStateProbe writes it into the tick's frame row with
 * the tick-start state of the same streams.
 */
final class ParticleLog implements IWorldAccess
{
    static JsonArray log = new JsonArray();
    static JsonObject tick0;
    static World installed;

    private ParticleLog() {}

    static JsonObject streams(World w)
    {
        JsonObject o = new JsonObject();
        o.addProperty("cw", Rows.hex(Det.state(w.rand)));
        o.addProperty("ds", Rows.hex(Det.seederState(Det.CLIENT)));
        o.addProperty("dm", Rows.hex(Det.mathState(Det.CLIENT)));
        o.addProperty("did", Det.nextId[Det.CLIENT]);
        return o;
    }

    /** Oracle.preTick: a fresh log for the tick, the streams as it starts. */
    static void tickStart(Minecraft mc)
    {
        log = new JsonArray();
        tick0 = null;
        WorldClient w = mc.theWorld;
        if (w == null) return;
        if (w != installed)
        {
            List l = (List)RenderStateProbe.getf(w, "worldAccesses");
            l.add(0, new ParticleLog());
            installed = w;
        }
        tick0 = streams(w);
        tick0.addProperty("t", (int)Oracle.tick);
    }

    /** The vanilla method that called into World: the first frame past World's own. */
    static String caller()
    {
        StackTraceElement[] st = new Throwable().getStackTrace();
        StringBuilder b = new StringBuilder();
        int n = 0;
        for (int i = 3; i < st.length && n < 3; ++i)
        {
            String c = st[i].getClassName().replaceAll(".*\\.", "");
            if (c.equals("World") || c.equals("WorldClient")) continue;
            b.append(n++ > 0 ? "<" : "").append(c).append(".").append(st[i].getMethodName());
        }
        return b.toString();
    }

    public void spawnParticle(String name, double x, double y, double z, double vx, double vy, double vz)
    {
        JsonObject o = streams(installed);
        o.addProperty("p", name);
        JsonArray a = new JsonArray();
        for (double v : new double[] {x, y, z, vx, vy, vz}) a.add(new com.google.gson.JsonPrimitive(RenderStateProbe.d(v)));
        o.add("a", a);
        o.addProperty("src", caller());
        log.add(o);
    }

    public void playAuxSFX(EntityPlayer p, int id, int x, int y, int z, int data)
    {
        JsonObject o = streams(installed);
        o.addProperty("aux", id);
        JsonArray a = new JsonArray();
        for (int v : new int[] {x, y, z, data}) a.add(new com.google.gson.JsonPrimitive(v));
        o.add("a", a);
        o.addProperty("src", caller());
        log.add(o);
    }

    public void markBlockForUpdate(int x, int y, int z) {}
    public void markBlockForRenderUpdate(int x, int y, int z) {}
    public void markBlockRangeForRenderUpdate(int x0, int y0, int z0, int x1, int y1, int z1) {}
    public void playSound(String s, double x, double y, double z, float v, float p) {}
    public void playSoundToNearExcept(EntityPlayer e, String s, double x, double y, double z, float v, float p) {}
    /** A client entity joining the world (a spawn packet): the streams after
     *  its constructor, which drew the entity ID, its Random and its UUID */
    public void onEntityCreate(Entity e)
    {
        JsonObject o = streams(installed);
        o.addProperty("ent", e.getClass().getSimpleName());
        o.addProperty("id", e.getEntityId());
        o.addProperty("src", caller());
        log.add(o);
    }
    public void onEntityDestroy(Entity e) {}
    public void playRecord(String s, int x, int y, int z) {}
    public void broadcastSound(int id, int x, int y, int z, int data) {}
    public void destroyBlockPartially(int id, int x, int y, int z, int p) {}
    public void onStaticEntitiesChanged() {}
}
