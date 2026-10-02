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
import net.minecraft.entity.effect.EntityLightningBolt;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.monster.EntityCreeper;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.entity.monster.EntityPigZombie;
import net.minecraft.init.Blocks;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.WorldServer;
import net.minecraft.util.MathHelper;

/** Isolated weather effects above raw ground and five entity classes. */
public final class LightningProbe
{
    private LightningProbe() {}

    public static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread thread = new Thread(new Runnable() {
            public void run() {
                try { result[0] = dump(server, cmd); }
                catch (Exception e) { error[0] = e; }
            }
        }, "Oracle LightningProbe");
        thread.start();
        thread.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    private static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        WorldServer ws = server.worldServers[0];
        Probe.rawChunks = true;
        int difficulty = cmd.has("difficulty") ? cmd.get("difficulty").getAsInt() : 2;
        boolean fireOff = cmd.has("fire_off") && cmd.get("fire_off").getAsBoolean();
        ws.difficultySetting = EnumDifficulty.getDifficultyEnum(difficulty);
        ws.getGameRules().setOrCreateGameRule("doFireTick", fireOff ? "false" : "true");
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        for (int cx = 90; cx <= 116; ++cx)
            for (int cz = 90; cz <= 110; ++cz) ws.getChunkFromChunkCoords(cx, cz);

        int[] xs = new int[6], ys = new int[6], zs = new int[6];
        PrintWriter places = writer(new File(dir, "places.txt"));
        for (int i = 0; i < 6; ++i)
        {
            xs[i] = 1608 + i * 16;
            zs[i] = 1608;
            ys[i] = ws.getHeightValue(xs[i], zs[i]);
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                {
                    ws.setBlock(xs[i] + dx, ys[i] - 1, zs[i] + dz, Blocks.stone, 0, 2);
                    for (int dy = 0; dy <= 3; ++dy)
                        ws.setBlock(xs[i] + dx, ys[i] + dy, zs[i] + dz, Blocks.air, 0, 2);
                }
            places.println(xs[i] + " " + ys[i] + " " + zs[i]);
        }
        places.close();

        Field fire = Entity.class.getDeclaredField("fire"); fire.setAccessible(true);
        Field health = EntityItem.class.getDeclaredField("health"); health.setAccessible(true);
        Field state = EntityLightningBolt.class.getDeclaredField("lightningState"); state.setAccessible(true);
        Field life = EntityLightningBolt.class.getDeclaredField("boltLivingTime"); life.setAccessible(true);

        Entity[] targets = new Entity[6];
        PrintWriter targetOut = writer(new File(dir, "targets.txt"));
        for (int i = 1; i < 6; ++i)
        {
            Entity target;
            if (i == 1) target = new EntityPig(ws);
            else if (i == 2) target = new EntityVillager(ws);
            else if (i == 3) target = new EntityCreeper(ws);
            else if (i == 4)
                target = new EntityItem(ws, xs[i] + 0.5, ys[i], zs[i] + 0.5,
                                        new ItemStack(Blocks.stone, 1));
            else target = new HostileProbe.ProbePlayer(ws);
            if (i != 4) target.setLocationAndAngles(xs[i] + 0.5, ys[i], zs[i] + 0.5, 0.0F, 0.0F);
            if (i == 5)
            {
                net.minecraft.world.chunk.Chunk chunk = ws.getChunkFromChunkCoords(
                    MathHelper.floor_double(target.posX / 16.0),
                    MathHelper.floor_double(target.posZ / 16.0));
                chunk.addEntity(target);
                ws.loadedEntityList.add(target);
                target.addedToChunk = true;
            }
            else if (!ws.spawnEntityInWorld(target)) throw new IllegalStateException("target spawn " + i);
            targets[i] = target;
            targetOut.println(i + " " + target.getEntityId() + " " +
                Double.toHexString(target.posX) + " " + Double.toHexString(target.posY) + " " +
                Double.toHexString(target.posZ) + " " +
                ProjectileProbe.hex(Det.state(AnimalProbe.entityRand(target))));
        }
        targetOut.close();
        ProjectileProbe.writeDetState(new File(dir, "start.txt"), ws.getSeed());

        EntityLightningBolt[] bolts = new EntityLightningBolt[6];
        PrintWriter spawnOut = writer(new File(dir, "bolts.txt"));
        for (int i = 0; i < 6; ++i)
        {
            bolts[i] = new EntityLightningBolt(ws, xs[i] + 0.5, ys[i], zs[i] + 0.5);
            ws.addWeatherEffect(bolts[i]);
            spawnOut.println(i + " " + bolts[i].getEntityId() + " " + bolts[i].boltVertex + " " +
                life.getInt(bolts[i]) + " " +
                ProjectileProbe.hex(Det.state(AnimalProbe.entityRand(bolts[i]))));
        }
        spawnOut.close();

        PrintWriter rows = writer(new File(dir, "ticks.txt"));
        PrintWriter pigmanNbt = writer(new File(dir, "pigman-nbt.txt"));
        for (int t = 0; t < 30; ++t)
        {
            for (int i = 0; i < 6; ++i)
                if (!bolts[i].isDead) { ++bolts[i].ticksExisted; bolts[i].onUpdate(); }
            /* World.updateEntities removes dead loaded entities after the
             * weather pass. Keep that boundary while leaving live targets
             * stationary for the next bolt tick. */
            for (int i = 1; i < 6; ++i)
                if (targets[i].isDead && targets[i].addedToChunk)
                {
                    ws.getChunkFromChunkCoords(MathHelper.floor_double(targets[i].posX / 16.0),
                        MathHelper.floor_double(targets[i].posZ / 16.0)).removeEntity(targets[i]);
                    ws.loadedEntityList.remove(targets[i]);
                    targets[i].addedToChunk = false;
                }
            for (int i = 0; i < 6; ++i)
            {
                EntityLightningBolt b = bolts[i];
                long mask = 0;
                int bit = 0;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dz = -1; dz <= 1; ++dz, ++bit)
                            if (ws.getBlock(xs[i] + dx, ys[i] + dy, zs[i] + dz) == Blocks.fire)
                                mask |= 1L << bit;
                Entity target = targets[i];
                float hp = target == null ? 0.0F :
                    target instanceof EntityItem ? (float)health.getInt(target) :
                    ((net.minecraft.entity.EntityLivingBase)target).getHealth();
                int powered = target instanceof EntityCreeper && ((EntityCreeper)target).getPowered() ? 1 : 0;
                int pigmen = 0;
                int pigmanId = 0;
                EntityPigZombie pigman = null;
                for (Object e : ws.loadedEntityList)
                    if (e instanceof EntityPigZombie && Math.abs(((Entity)e).posX - (xs[1] + 0.5)) < 3)
                    { ++pigmen; pigmanId = ((Entity)e).getEntityId(); pigman = (EntityPigZombie)e; }
                if (i == 1 && pigman != null)
                {
                    NBTTagCompound tag = new NBTTagCompound();
                    pigman.writeToNBT(tag);
                    pigmanNbt.println(t + " " + StructuresProbe.canon(tag).toString());
                }
                rows.println(t + " " + i + " " + state.getInt(b) + " " + life.getInt(b) + " " +
                    b.boltVertex + " " + (b.isDead ? 1 : 0) + " " + b.ticksExisted + " " +
                    ProjectileProbe.hex(Det.state(AnimalProbe.entityRand(b))) + " " +
                    Long.toHexString(mask) + " " +
                    (target == null ? 0 : target.isDead ? 1 : 0) + " " +
                    Integer.toHexString(Float.floatToRawIntBits(hp)) + " " +
                    (target == null ? 0 : fire.getInt(target)) + " " + powered + " " +
                    pigmen + " " + pigmanId);
            }
        }
        rows.close();
        pigmanNbt.close();
        ProjectileProbe.writeDetState(new File(dir, "end.txt"), ws.getSeed());
        JsonObject m = new JsonObject();
        m.addProperty("kind", "lightning2");
        m.addProperty("seed", ws.getSeed());
        m.addProperty("bolts", 6);
        m.addProperty("ticks", 30);
        m.addProperty("difficulty", difficulty);
        m.addProperty("fire_off", fireOff);
        PrintWriter mw = writer(new File(dir, "manifest.json")); mw.println(m.toString()); mw.close();
        JsonObject result = new JsonObject(); result.addProperty("bolts", 6); return result;
    }

    private static PrintWriter writer(File f) throws Exception
    { return new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8")); }
}
