package netherite.oracle;

import com.google.gson.JsonObject;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/** A one-time saved-world preparation, through the ordinary chunk provider. */
final class PreloadRegion
{
    static final boolean MUTATES = true;

    private static final class Request
    {
        final int cx0, cz0, width, total, perTick;
        final long firstTick;
        int next;

        Request(int cx0, int cz0, int width, int total, int perTick, long firstTick)
        {
            this.cx0 = cx0;
            this.cz0 = cz0;
            this.width = width;
            this.total = total;
            this.perTick = perTick;
            this.firstTick = firstTick;
        }
    }

    private static volatile Request pending;

    static JsonObject run(IntegratedServer server, JsonObject cmd)
    {
        if (pending != null) throw new IllegalStateException("a region preload is already running");
        int cx0 = cmd.get("cx0").getAsInt(), cx1 = cmd.get("cx1").getAsInt();
        int cz0 = cmd.get("cz0").getAsInt(), cz1 = cmd.get("cz1").getAsInt();
        int perTick = cmd.has("per_tick") ? cmd.get("per_tick").getAsInt() : 64;
        long width = (long)cx1 - cx0 + 1, height = (long)cz1 - cz0 + 1;
        long count = width * height;
        if (cx0 < -4096 || cx1 > 4096 || cz0 < -4096 || cz1 > 4096 ||
            width < 1 || height < 1 || count > 8192 || perTick < 1 || perTick > 128)
            throw new IllegalArgumentException("preload region: invalid bounds or per_tick");

        pending = new Request(cx0, cz0, (int)width, (int)count, perTick, Oracle.tick + 1);
        JsonObject result = new JsonObject();
        result.addProperty("queued", count);
        result.addProperty("firstTick", Oracle.tick + 1);
        return result;
    }

    static void atServerTick(MinecraftServer server)
    {
        Request r = pending;
        if (r == null || Oracle.tick < r.firstTick) return;
        WorldServer world = server.worldServers[0];
        for (int n = 0; n < r.perTick && r.next < r.total; ++n, ++r.next)
        {
            int cx = r.cx0 + r.next % r.width;
            int cz = r.cz0 + r.next / r.width;
            Chunk chunk = world.getChunkFromChunkCoords(cx, cz);
            if (chunk == null) throw new IllegalStateException("preload chunk missing at " + cx + "," + cz);
            chunk.setChunkModified();
        }
        if (r.next == r.total) pending = null;
    }
}
