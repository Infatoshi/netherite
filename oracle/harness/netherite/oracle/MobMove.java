package netherite.oracle;

import com.google.gson.JsonObject;
import net.minecraft.entity.Entity;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.WorldServer;

/** Move one already spawned server mob between tape ticks for an attack case. */
final class MobMove
{
    static final boolean MUTATES = true;
    private MobMove() {}

    static JsonObject run(IntegratedServer server, JsonObject cmd)
    {
        int id = cmd.get("id").getAsInt();
        double x = cmd.get("x").getAsDouble();
        double y = cmd.get("y").getAsDouble();
        double z = cmd.get("z").getAsDouble();
        WorldServer world = server.worldServers[0];

        for (Object o : world.loadedEntityList)
        {
            Entity e = (Entity)o;
            if (e.getEntityId() != id) continue;
            e.setPosition(x, y, z);
            JsonObject out = new JsonObject();
            out.addProperty("id", id);
            return out;
        }

        throw new IllegalArgumentException("MobMove: entity " + id + " is not loaded");
    }
}
