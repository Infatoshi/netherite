package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import net.minecraft.block.Block;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * The pickaxe tape's aim probe: one small block region (id and meta per
 * non-air cell), read read-only on its own thread while the server is parked,
 * so the script can stand the player exactly beside a trunk and aim the
 * crosshair at a face centre.
 */
final class PickBlocks
{
    private PickBlocks() {}

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try { result[0] = scan(server, cmd); }
                catch (Exception e) { error[0] = e; }
            }
        }, "Oracle PickBlocks");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject scan(IntegratedServer server, JsonObject cmd)
    {
        WorldServer ws = server.worldServers[0];
        int x0 = cmd.get("x0").getAsInt(), x1 = cmd.get("x1").getAsInt();
        int z0 = cmd.get("z0").getAsInt(), z1 = cmd.get("z1").getAsInt();
        int y0 = cmd.has("y0") ? cmd.get("y0").getAsInt() : 55;
        int y1 = cmd.has("y1") ? cmd.get("y1").getAsInt() : 90;

        JsonArray cells = new JsonArray();

        for (int x = x0; x <= x1; ++x)
        {
            for (int z = z0; z <= z1; ++z)
            {
                Chunk ch = ws.getChunkProvider().provideChunk(x >> 4, z >> 4);

                for (int y = y0; y <= y1; ++y)
                {
                    int id = Block.getIdFromBlock(ch.func_150810_a(x & 15, y, z & 15));

                    if (id == 0) continue;

                    int meta = ch.getBlockMetadata(x & 15, y, z & 15);
                    JsonArray c = new JsonArray();
                    c.add(new com.google.gson.JsonPrimitive(Integer.valueOf(x)));
                    c.add(new com.google.gson.JsonPrimitive(Integer.valueOf(y)));
                    c.add(new com.google.gson.JsonPrimitive(Integer.valueOf(z)));
                    c.add(new com.google.gson.JsonPrimitive(Integer.valueOf(id)));
                    c.add(new com.google.gson.JsonPrimitive(Integer.valueOf(meta)));
                    cells.add(c);
                }
            }
        }

        JsonObject out = new JsonObject();
        out.add("cells", cells);
        return out;
    }
}
