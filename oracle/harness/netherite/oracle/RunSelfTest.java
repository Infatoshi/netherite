package netherite.oracle;

import com.google.gson.JsonObject;
import net.minecraft.server.integrated.IntegratedServer;

/** Smoke test for the generic `run` command: echoes its input and the world seed. */
final class RunSelfTest
{
    private RunSelfTest() {}

    static JsonObject run(IntegratedServer server, JsonObject cmd)
    {
        JsonObject r = new JsonObject();
        r.addProperty("seed", server.worldServers[0].getSeed());
        r.add("echo", cmd);
        return r;
    }
}
