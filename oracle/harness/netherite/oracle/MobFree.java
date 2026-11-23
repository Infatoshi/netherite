package netherite.oracle;

import com.google.gson.JsonObject;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import net.minecraft.client.Minecraft;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * Tape setup for the server-replay lanes (the native tape replay drives the
 * whole server, not just the players). Run it in a script just before
 * {"cmd":"run","class":"Snapshot"}:
 *
 *   {"cmd":"run","class":"MobFree"}
 *   {"cmd":"run","class":"Snapshot"}
 *   {"n":2400,"act":{...}}
 *
 * What it does, in this order:
 *
 *   - doMobSpawning false in every world, so no mob comes back through
 *     SpawnerAnimals (WorldServer.tick's spawner section and population both
 *     read it);
 *   - every entity except players removed from every world: the server worlds
 *     through the same path World.updateEntities uses (chunk list, entity list,
 *     onEntityRemoved) and the client world directly, so the client's next tick
 *     does not collide with a mob the server already dropped. weatherEffects
 *     (lightning) go too;
 *   - ChunkProviderServer.loadChunkOnProvideRequest false in every world and the
 *     unload queue emptied: the tick must stay inside the chunks the snapshot
 *     carries, or the world generator would draw (and block writes land in
 *     chunks the native side does not have);
 *   - the chunks the tape will walk preloaded: a (2*radius+1)^2 square of chunks
 *     around the parked player's chunk, so crossing a chunk border creates
 *     PlayerInstances over chunks that are already loaded and generates
 *     nothing;
 *   - the entities population just spawned removed the same way (the player
 *     must not be pushed by a cow the walk was never meant to include);
 *   - Rows.blkCount zeroed (Rows.blkHash is left alone: the snapshot records
 *     the chain and the native side continues it), so the first row's w.bc is
 *     the first replayed tick's writes and nothing else.
 *
 * Optional command keys: "mobs" (true keeps doMobSpawning on after the purge,
 * "generateNether" (true keeps vanilla Nether chunk generation on for travel),
 * "generateEnd" (the same for the End),
 * so the following tape starts with only the player and naturally spawns mobs),
 * "radius" (default 16), "rain" (true sets every world
 * raining with "rainTime" ticks left, default 12000; the Nether and the End have
 * no chunks but their WorldServer.tick still runs updateWeather, so forcing them
 * too is what makes the three-worlds timeline observable in the row's d.sw) and
 * "thunder" (the same for thundering).
 * The weather strengths ramp the vanilla way; forcing it here is how a tape
 * carries a weather change inside a few hundred ticks.
 *
 * Runs on the client thread between frames, while the server thread is parked,
 * so no tick can interleave: the state the snapshot then records is exactly
 * what this leaves behind.
 */
final class MobFree
{
    static final boolean MUTATES = true;
    private MobFree() {}

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 16;
        boolean mobs = cmd.has("mobs") && cmd.get("mobs").getAsBoolean();
        boolean generateNether = cmd.has("generateNether") && cmd.get("generateNether").getAsBoolean();
        boolean generateEnd = cmd.has("generateEnd") && cmd.get("generateEnd").getAsBoolean();
        Minecraft mc = Oracle.mc;
        JsonObject out = new JsonObject();
        int removed = 0;

        for (int d = 0; d < server.worldServers.length; ++d)
        {
            WorldServer ws = server.worldServers[d];
            if (ws == null) continue;
            if (!mobs) ws.getGameRules().setOrCreateGameRule("doMobSpawning", "false");
            removed += purge(ws);
            Object cps = Snapshot.objField(ws, "theChunkProviderServer");
            if (!(generateNether && ws.provider.dimensionId == -1) && !(generateEnd && ws.provider.dimensionId == 1))
                Snapshot.field(cps, "loadChunkOnProvideRequest").setBoolean(cps, false);
            Set unload = (Set)Snapshot.objField(cps, "chunksToUnload");
            unload.clear();
        }

        // The client world's own copy of the entities: the server-side removal
        // only reaches it through S13PacketDestroyEntities, which the client
        // processes during the next server tick's network phase, i.e. after the
        // first replayed client tick. Drop them here instead.
        int clientRemoved = 0;

        if (mc != null && mc.theWorld != null)
        {
            List ents = new ArrayList(mc.theWorld.loadedEntityList);

            for (Object o : ents)
            {
                Entity e = (Entity)o;
                if (e instanceof EntityPlayer) continue;
                mc.theWorld.loadedEntityList.remove(e);
                e.setDead();
                ++clientRemoved;
            }
        }

        // The chunks the walk reaches must be loaded before the snapshot. The
        // player walks at most a few chunks, but its view window is 17x17, so
        // radius 16 around the start covers every active set the tape can build.
        WorldServer over = server.worldServers[0];
        EntityPlayerMP parked = over.playerEntities.isEmpty() ? null : (EntityPlayerMP)over.playerEntities.get(0);
        int loaded = 0;

        if (parked != null && radius > 0)
        {
            int pcx = (int)Math.floor(parked.posX / 16.0D);
            int pcz = (int)Math.floor(parked.posZ / 16.0D);

            for (int dx = -radius; dx <= radius; ++dx)
                for (int dz = -radius; dz <= radius; ++dz)
                {
                    boolean had = over.theChunkProviderServer.chunkExists(pcx + dx, pcz + dz);
                    over.theChunkProviderServer.loadChunk(pcx + dx, pcz + dz);
                    if (!had) ++loaded;
                }
        }

        // population can spawn mobs of its own; take them out too
        for (int d = 0; d < server.worldServers.length; ++d)
        {
            WorldServer ws = server.worldServers[d];
            if (ws == null) continue;
            removed += purge(ws);
            Object cps = Snapshot.objField(ws, "theChunkProviderServer");
            ((Set)Snapshot.objField(cps, "chunksToUnload")).clear();
        }

        if (cmd.has("rain") && cmd.get("rain").getAsBoolean())
        {
            int t = cmd.has("rainTime") ? cmd.get("rainTime").getAsInt() : 12000;

            for (WorldServer x : server.worldServers)
            {
                if (x == null) continue;
                x.getWorldInfo().setRaining(true);
                x.getWorldInfo().setRainTime(t);
            }
        }

        if (cmd.has("thunder") && cmd.get("thunder").getAsBoolean())
        {
            int t = cmd.has("thunderTime") ? cmd.get("thunderTime").getAsInt() : 12000;

            for (WorldServer x : server.worldServers)
            {
                if (x == null) continue;
                x.getWorldInfo().setThundering(true);
                x.getWorldInfo().setThunderTime(t);
            }
        }

        // the first row must not carry the setup's own block writes
        Snapshot.findField(Rows.class, "blkCount").setInt(null, 0);

        int ents = 0;
        StringBuilder who = new StringBuilder();

        for (WorldServer ws : server.worldServers)
        {
            if (ws == null) continue;
            ents += ws.loadedEntityList.size();

            for (Object o : ws.loadedEntityList)
            {
                if (!(o instanceof EntityPlayer)) who.append(((Entity)o).getClass().getSimpleName()).append(',');
            }
        }

        out.addProperty("removed", removed);
        out.addProperty("clientRemoved", clientRemoved);
        out.addProperty("preloaded", loaded);
        out.addProperty("radius", radius);
        out.addProperty("mobSpawning", over.getGameRules().getGameRuleStringValue("doMobSpawning"));
        out.addProperty("entities", ents);
        out.addProperty("classes", who.toString());
        System.out.println("ORACLE MOBFREE removed=" + removed + " client=" + clientRemoved + " preloaded=" + loaded
            + " ents=" + ents + " spawnRule=" + over.getGameRules().getGameRuleStringValue("doMobSpawning")
            + " classes=" + who);
        return out;
    }

    /** Every entity but the players, through World.updateEntities' own removal
     * path: the chunk's section list, the world's list, onEntityRemoved. */
    static int purge(WorldServer ws) throws Exception
    {
        List ents = new ArrayList(ws.loadedEntityList);
        int n = 0;

        for (Object o : ents)
        {
            Entity e = (Entity)o;
            if (e instanceof EntityPlayer) continue;
            int cx = e.chunkCoordX, cz = e.chunkCoordZ;

            if (e.addedToChunk && ws.getChunkProvider().chunkExists(cx, cz))
            {
                Chunk c = ws.getChunkFromChunkCoords(cx, cz);
                c.removeEntity(e);
            }

            ws.loadedEntityList.remove(e);
            for (Class<?> k = ws.getClass(); k != null; k = k.getSuperclass())
            {
                try
                {
                    java.lang.reflect.Method m = k.getDeclaredMethod("onEntityRemoved", Entity.class);
                    m.setAccessible(true);
                    m.invoke(ws, e);
                    break;
                }
                catch (NoSuchMethodException ex)
                {
                    // keep walking up: WorldServer declares it, World does too
                }
            }
            e.setDead();
            ++n;
        }

        for (Object o : new ArrayList(ws.weatherEffects))
        {
            Entity e = (Entity)o;
            ws.weatherEffects.remove(e);
            e.setDead();
            ++n;
        }

        return n;
    }
}
