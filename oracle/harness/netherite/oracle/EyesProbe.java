package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityEnderEye;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.ChunkPosition;
import net.minecraft.world.WorldServer;

/** Raw-region EntityEnderEye flight, one line for each living eye per tick. */
public final class EyesProbe
{
    private EyesProbe() {}

    public static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread thread = new Thread(new Runnable() {
            public void run() {
                try { result[0] = dump(server, cmd); }
                catch (Exception e) { error[0] = e; }
            }
        }, "Oracle EyesProbe");
        thread.start();
        thread.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    private static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        WorldServer ws = server.worldServers[0];
        Probe.rawChunks = true;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        int[][] starts = {{1608, 1608}, {-1608, 808}};
        ChunkPosition[] target = new ChunkPosition[2];
        for (int i = 0; i < 2; ++i)
        {
            for (int cx = (starts[i][0] >> 4) - 5; cx <= (starts[i][0] >> 4) + 5; ++cx)
                for (int cz = (starts[i][1] >> 4) - 5; cz <= (starts[i][1] >> 4) + 5; ++cz)
                    ws.getChunkFromChunkCoords(cx, cz);
            target[i] = ws.findClosestStructure("Stronghold", starts[i][0], 120, starts[i][1]);
            if (target[i] == null) throw new IllegalStateException("no stronghold");
        }
        for (int i = 0; i < 2; ++i)
        {
            int x = target[i].field_151329_a + 10;
            int z = target[i].field_151328_c + 10;
            for (int cx = (x >> 4) - 5; cx <= (x >> 4) + 5; ++cx)
                for (int cz = (z >> 4) - 5; cz <= (z >> 4) + 5; ++cz)
                    ws.getChunkFromChunkCoords(cx, cz);
        }
        ChunkPosition[] closeTarget = new ChunkPosition[2];
        for (int i = 0; i < 2; ++i)
            closeTarget[i] = ws.findClosestStructure("Stronghold",
                target[i].field_151329_a + 10, 120, target[i].field_151328_c + 10);

        ProjectileProbe.writeDetState(new File(dir, "start.txt"), ws.getSeed());
        PrintWriter regions = writer(new File(dir, "regions.txt"));
        for (int i = 0; i < 2; ++i)
        {
            regions.println((starts[i][0] >> 4) + " " + (starts[i][1] >> 4));
            regions.println(((target[i].field_151329_a + 10) >> 4) + " " +
                            ((target[i].field_151328_c + 10) >> 4));
        }
        regions.close();

        Field timer = field("despawnTimer");
        Field drop = field("shatterOrDrop");
        int itemsBefore = 0;
        for (Object e : ws.loadedEntityList) if (e instanceof EntityItem) ++itemsBefore;
        List<EntityEnderEye> eyes = new ArrayList<EntityEnderEye>();
        PrintWriter startsOut = writer(new File(dir, "starts.txt"));
        for (int i = 0; i < 16; ++i)
        {
            int j = i % 2;
            boolean near = i >= 8;
            double x = near ? closeTarget[j].field_151329_a + 3.5 : starts[j][0] + 0.5 + i;
            double z = near ? closeTarget[j].field_151328_c + 3.5 : starts[j][1] + 0.5 + i;
            double y = near ? 120.0 : 120.0 + i;
            ChunkPosition flightTarget = near ? ws.findClosestStructure("Stronghold", (int)x, (int)y, (int)z) : target[j];
            EntityEnderEye eye = new EntityEnderEye(ws, x, y, z);
            eye.moveTowards(flightTarget.field_151329_a, flightTarget.field_151327_b,
                            flightTarget.field_151328_c);
            if (!ws.spawnEntityInWorld(eye)) throw new IllegalStateException("eye did not spawn");
            eyes.add(eye);
            startsOut.println(i + " " + Double.toHexString(x) + " " + Double.toHexString(y) + " " +
                              Double.toHexString(z) + " " + flightTarget.field_151329_a + " " +
                              flightTarget.field_151327_b + " " + flightTarget.field_151328_c + " " +
                              eye.getEntityId() + " " + (drop.getBoolean(eye) ? 1 : 0));
        }
        startsOut.close();

        PrintWriter ticks = writer(new File(dir, "ticks.txt"));
        for (int t = 0; t < 82; ++t)
        {
            for (int i = 0; i < eyes.size(); ++i)
            {
                EntityEnderEye e = eyes.get(i);
                if (e.isDead) continue;
                ProjectileProbe.updateEntity(ws, e);
                if (!e.isDead)
                    ticks.println(t + " " + i + " " + e.getEntityId() + " " +
                        Double.toHexString(e.posX) + " " + Double.toHexString(e.posY) + " " +
                        Double.toHexString(e.posZ) + " " + Double.toHexString(e.motionX) + " " +
                        Double.toHexString(e.motionY) + " " + Double.toHexString(e.motionZ) + " " +
                        Integer.toHexString(Float.floatToRawIntBits(e.rotationYaw)) + " " +
                        Integer.toHexString(Float.floatToRawIntBits(e.rotationPitch)) + " " +
                        timer.getInt(e) + " " + ProjectileProbe.hex(Det.state(AnimalProbe.entityRand(e))));
            }
        }
        ticks.close();

        int items = 0;
        for (Object e : ws.loadedEntityList) if (e instanceof EntityItem) ++items;
        items -= itemsBefore;
        ProjectileProbe.writeDetState(new File(dir, "end.txt"), ws.getSeed());
        HostileProbe.ProbePlayer thrower = new HostileProbe.ProbePlayer(ws);
        thrower.rotationPitch = -90.0F;
        ItemStack held = new ItemStack(Items.ender_eye, 2);
        ProjectileProbe.writeDetState(new File(dir, "use_start.txt"), ws.getSeed());
        PrintWriter uses = writer(new File(dir, "use.txt"));
        for (int i = 0; i < 2; ++i)
        {
            int j = i;
            double px = i == 0 ? starts[j][0] + 0.5 : closeTarget[j].field_151329_a + 3.5;
            double pz = i == 0 ? starts[j][1] + 0.5 : closeTarget[j].field_151328_c + 3.5;
            thrower.setLocationAndAngles(px, 120.0, pz, 0.0F, -90.0F);
            int before = ws.loadedEntityList.size();
            Items.ender_eye.onItemRightClick(held, ws, thrower);
            EntityEnderEye e = null;
            for (int k = before; k < ws.loadedEntityList.size(); ++k)
                if (ws.loadedEntityList.get(k) instanceof EntityEnderEye)
                    e = (EntityEnderEye)ws.loadedEntityList.get(k);
            if (e == null) throw new IllegalStateException("ItemEnderEye did not spawn");
            uses.println(i + " " + Double.toHexString(thrower.posX) + " " +
                Double.toHexString(thrower.posY) + " " + Double.toHexString(thrower.posZ) + " " +
                Float.toHexString(thrower.yOffset) + " " + e.getEntityId() + " " +
                Double.toHexString(e.posX) + " " + Double.toHexString(e.posY) + " " +
                Double.toHexString(e.posZ) + " " +
                Double.toHexString(field("targetX").getDouble(e)) + " " +
                Double.toHexString(field("targetY").getDouble(e)) + " " +
                Double.toHexString(field("targetZ").getDouble(e)) + " " +
                (drop.getBoolean(e) ? 1 : 0) + " " + held.stackSize + " " +
                ProjectileProbe.hex(Det.state(AnimalProbe.entityRand(e))));
        }
        uses.close();
        ProjectileProbe.writeDetState(new File(dir, "use_end.txt"), ws.getSeed());
        JsonObject m = new JsonObject();
        m.addProperty("kind", "eyes2");
        m.addProperty("seed", ws.getSeed());
        m.addProperty("eyes", eyes.size());
        m.addProperty("drops", items);
        PrintWriter manifest = writer(new File(dir, "manifest.json"));
        manifest.println(m.toString());
        manifest.close();
        JsonObject result = new JsonObject();
        result.addProperty("eyes", eyes.size());
        result.addProperty("drops", items);
        return result;
    }

    private static Field field(String name) throws Exception
    {
        Field f = EntityEnderEye.class.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    private static PrintWriter writer(File f) throws Exception
    {
        return new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
    }
}
