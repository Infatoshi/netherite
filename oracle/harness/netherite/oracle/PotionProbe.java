package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityAgeable;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.ai.EntityAITasks;
import net.minecraft.entity.ai.EntityJumpHelper;
import net.minecraft.entity.ai.EntityLookHelper;
import net.minecraft.entity.ai.EntityMoveHelper;
import net.minecraft.entity.passive.EntityChicken;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.passive.EntitySheep;
import net.minecraft.entity.projectile.EntityPotion;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.potion.Potion;
import net.minecraft.potion.PotionEffect;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.MathHelper;
import net.minecraft.world.WorldServer;

public final class PotionProbe
{
    static final String[] KINDS = {"Pig", "Cow", "MushroomCow", "Chicken", "Sheep"};
    static final int KIND_PIG = 0, KIND_COW = 1, KIND_MOOSHROOM = 2, KIND_CHICKEN = 3, KIND_SHEEP = 4;

    static final int ENT_STATE_BYTES = 272;
    static final int SPAWN_BYTES = 100;
    static final int EVENT_BYTES = 64;

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    public static class Event
    {
        public int tick;
        public int type; // 1=ADD_EFFECT, 2=CLEAR_EFFECTS, 3=APPLY_FOOD, 4=SPAWN_POTION
        public int targetSi;
        public int potionId;
        public int duration;
        public int amplifier;
        public int ambient;
        public int itemId;
        public int itemDamage;
        public double x, y, z;
        public double mx, my, mz;
        public int potionDamage;

        public static Event addEffect(int tick, int targetSi, int potionId, int duration, int amplifier, int ambient)
        {
            Event e = new Event();
            e.tick = tick;
            e.type = 1;
            e.targetSi = targetSi;
            e.potionId = potionId;
            e.duration = duration;
            e.amplifier = amplifier;
            e.ambient = ambient;
            return e;
        }

        public static Event clearEffects(int tick, int targetSi)
        {
            Event e = new Event();
            e.tick = tick;
            e.type = 2;
            e.targetSi = targetSi;
            return e;
        }

        public static Event applyFood(int tick, int targetSi, int itemId, int itemDamage)
        {
            Event e = new Event();
            e.tick = tick;
            e.type = 3;
            e.targetSi = targetSi;
            e.itemId = itemId;
            e.itemDamage = itemDamage;
            return e;
        }

        public static Event spawnPotion(int tick, double x, double y, double z, double mx, double my, double mz, int potionDamage)
        {
            Event e = new Event();
            e.tick = tick;
            e.type = 4;
            e.x = x;
            e.y = y;
            e.z = z;
            e.mx = mx;
            e.my = my;
            e.mz = mz;
            e.potionDamage = potionDamage;
            return e;
        }
    }

    private PotionProbe() {}

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    result[0] = dump(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle PotionProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static void applyFoodToEntity(EntityLivingBase target, int itemId, int itemDamage)
    {
        if (itemId == 322) // golden apple
        {
            target.addPotionEffect(new PotionEffect(Potion.field_76444_x.id, 2400, 0));
            if (itemDamage > 0) // enchanted golden apple
            {
                target.addPotionEffect(new PotionEffect(Potion.regeneration.id, 600, 4));
                target.addPotionEffect(new PotionEffect(Potion.resistance.id, 6000, 0));
                target.addPotionEffect(new PotionEffect(Potion.fireResistance.id, 6000, 0));
            }
            else
            {
                target.addPotionEffect(new PotionEffect(Potion.regeneration.id, 100, 1));
            }
        }
        else if (itemId == 365) // raw chicken
        {
            target.addPotionEffect(new PotionEffect(Potion.hunger.id, 600, 0));
        }
        else if (itemId == 367) // rotten flesh
        {
            target.addPotionEffect(new PotionEffect(Potion.hunger.id, 600, 0));
        }
        else if (itemId == 375) // spider eye
        {
            target.addPotionEffect(new PotionEffect(Potion.poison.id, 100, 0));
        }
        else if (itemId == 349 && itemDamage == 3) // pufferfish
        {
            target.addPotionEffect(new PotionEffect(Potion.poison.id, 1200, 3));
            target.addPotionEffect(new PotionEffect(Potion.hunger.id, 300, 2));
            target.addPotionEffect(new PotionEffect(Potion.confusion.id, 300, 1));
        }
    }

    static List<Event> buildEvents(int platformY, int bx0, int bz0)
    {
        List<Event> evs = new ArrayList<Event>();

        // Phase 1: Direct application of ALL 23 effects (ticks 0 - 20)
        for (int id = 1; id <= 23; ++id)
        {
            evs.add(Event.addEffect(0, id - 1, id, 300, 0, 0));
            evs.add(Event.addEffect(0, (id - 1) + 23, id, 500, 1, 0));
        }

        // Tick 5: Harm animals with regeneration so they can heal, and apply amp 2 to select animals
        evs.add(Event.addEffect(5, 9, 7, 1, 0, 0));  // Instant Damage on animal 9 (regen 0)
        evs.add(Event.addEffect(5, 32, 7, 1, 0, 0)); // Instant Damage on animal 32 (regen 1)
        evs.add(Event.addEffect(5, 20, 7, 1, 0, 0)); // Instant Damage on animal 20 (wither 0)

        // Amplifier 2 applications
        evs.add(Event.addEffect(5, 46, 1, 400, 2, 0));  // Speed II
        evs.add(Event.addEffect(5, 47, 8, 400, 2, 0));  // Jump II
        evs.add(Event.addEffect(5, 48, 21, 400, 2, 0)); // Health Boost II
        evs.add(Event.addEffect(5, 49, 22, 400, 2, 0)); // Absorption II

        // Phase 2: Short duration effects (ticks 100 - 150)
        evs.add(Event.addEffect(100, 40, 11, 50, 1, 0)); // Resistance amp 1 expiring at 150
        evs.add(Event.addEffect(100, 41, 12, 50, 0, 0)); // Fire Resistance expiring at 150
        evs.add(Event.addEffect(100, 42, 13, 50, 0, 0)); // Water Breathing expiring at 150
        evs.add(Event.addEffect(100, 43, 14, 50, 0, 0)); // Invisibility expiring at 150
        evs.add(Event.addEffect(100, 44, 18, 50, 1, 0)); // Weakness expiring at 150
        evs.add(Event.addEffect(100, 45, 10, 50, 2, 0)); // Regen II expiring at 150

        // Phase 3: Curing batch 1 (ticks 250 - 280)
        evs.add(Event.addEffect(250, 0, 1, 600, 1, 0));
        evs.add(Event.addEffect(250, 1, 2, 600, 1, 0));
        evs.add(Event.addEffect(250, 2, 14, 600, 0, 0));
        evs.add(Event.addEffect(250, 3, 21, 600, 1, 0));
        evs.add(Event.addEffect(250, 4, 22, 600, 1, 0));

        evs.add(Event.clearEffects(280, 0));
        evs.add(Event.clearEffects(280, 1));
        evs.add(Event.clearEffects(280, 2));
        evs.add(Event.clearEffects(280, 3));
        evs.add(Event.clearEffects(280, 4));

        // Phase 4: Food items (tick 350)
        evs.add(Event.applyFood(350, 15, 322, 0)); // normal golden apple
        evs.add(Event.applyFood(350, 16, 322, 1)); // enchanted golden apple
        evs.add(Event.applyFood(350, 17, 365, 0)); // raw chicken
        evs.add(Event.applyFood(350, 18, 367, 0)); // rotten flesh
        evs.add(Event.applyFood(350, 19, 375, 0)); // spider eye
        evs.add(Event.applyFood(350, 20, 349, 3)); // pufferfish

        // Phase 5: Combination / overwrite (ticks 500 - 550)
        evs.add(Event.addEffect(500, 5, 1, 100, 0, 0)); // Speed 100t amp 0
        evs.add(Event.addEffect(520, 5, 1, 300, 0, 0)); // Speed 300t amp 0 (extends duration)
        evs.add(Event.addEffect(540, 5, 1, 150, 1, 0)); // Speed 150t amp 1 (upgrades amplifier)

        // Phase 6: Splash Potions thrown at clusters (ticks 700, 900, 1100, 1300)
        double c0_x = bx0 + 25.0, c0_z = bz0 + 25.0;
        double c1_x = bx0 + 45.0, c1_z = bz0 + 25.0;
        double c2_x = bx0 + 25.0, c2_z = bz0 + 45.0;
        double c3_x = bx0 + 45.0, c3_z = bz0 + 45.0;

        evs.add(Event.spawnPotion(700, c0_x, platformY + 2.5, c0_z, 0.0, -0.4, 0.0, 16388));  // Splash Poison
        evs.add(Event.spawnPotion(900, c1_x, platformY + 2.5, c1_z, 0.0, -0.4, 0.0, 16389));  // Splash Instant Health
        evs.add(Event.spawnPotion(1100, c2_x, platformY + 2.5, c2_z, 0.0, -0.4, 0.0, 16385)); // Splash Speed
        evs.add(Event.spawnPotion(1300, c3_x, platformY + 2.5, c3_z, 0.0, -0.4, 0.0, 16396)); // Splash Instant Damage (Harm)

        // Phase 7: Curing batch 2 at tick 1600
        for (int i = 25; i <= 35; ++i)
        {
            evs.add(Event.clearEffects(1600, i));
        }

        return evs;
    }

    static void writeEvents(File f, List<Event> events) throws Exception
    {
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(f)));
        byte[] buf = new byte[EVENT_BYTES];
        out.writeInt(0x504f544e); // "POTN"
        out.writeInt(events.size());

        for (Event ev : events)
        {
            Arrays.fill(buf, (byte)0);
            le32(buf, 0, ev.tick);
            le32(buf, 4, ev.type);

            if (ev.type == 1)
            {
                le32(buf, 8, ev.targetSi);
                le32(buf, 12, ev.potionId);
                le32(buf, 16, ev.duration);
                le32(buf, 20, ev.amplifier);
                le32(buf, 24, ev.ambient);
            }
            else if (ev.type == 2)
            {
                le32(buf, 8, ev.targetSi);
            }
            else if (ev.type == 3)
            {
                le32(buf, 8, ev.targetSi);
                le32(buf, 12, ev.itemId);
                le32(buf, 16, ev.itemDamage);
            }
            else if (ev.type == 4)
            {
                le64(buf, 8, Double.doubleToRawLongBits(ev.x));
                le64(buf, 16, Double.doubleToRawLongBits(ev.y));
                le64(buf, 24, Double.doubleToRawLongBits(ev.z));
                le64(buf, 32, Double.doubleToRawLongBits(ev.mx));
                le64(buf, 40, Double.doubleToRawLongBits(ev.my));
                le64(buf, 48, Double.doubleToRawLongBits(ev.mz));
                le32(buf, 56, ev.potionDamage);
            }
            out.write(buf);
        }
        out.close();
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 0;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 0;
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 1;
        int animals = cmd.has("animals") ? cmd.get("animals").getAsInt() : 50;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 2400;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;
        int nbtEvery = cmd.has("nbtEvery") ? cmd.get("nbtEvery").getAsInt() : 64;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        Trace.restart();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        int x0 = cx - radius - ring, x1 = cx + radius + ring;
        int z0 = cz - radius - ring, z1 = cz + radius + ring;
        JsonArray loaded = new JsonArray();

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
            }
        }

        int area = 2 * radius + 1;
        int bestX = cx, bestZ = cz, bestHi = 0, bestSpan = Integer.MAX_VALUE, bestRank = 2;

        for (int ax = x0 + radius; ax <= x1 - radius; ++ax)
        {
            for (int az = z0 + radius; az <= z1 - radius; ++az)
            {
                int lo = 999, hi = -999;

                for (int lx = ax - radius; lx <= ax + radius; ++lx)
                {
                    for (int lz = az - radius; lz <= az + radius; ++lz)
                    {
                        for (int sx = 0; sx < 16; ++sx)
                        {
                            for (int sz = 0; sz < 16; ++sz)
                            {
                                int h = heightOf(ws, lx * 16 + sx, lz * 16 + sz);
                                if (h < lo) lo = h;
                                if (h > hi) hi = h;
                            }
                        }
                    }
                }

                int rank = lo >= 64 ? 0 : 1;

                if (rank < bestRank || (rank == bestRank && hi - lo < bestSpan))
                {
                    bestRank = rank;
                    bestSpan = hi - lo;
                    bestX = ax;
                    bestZ = az;
                    bestHi = hi;
                }
            }
        }

        cx = bestX;
        cz = bestZ;

        x0 = cx - radius - ring;
        x1 = cx + radius + ring;
        z0 = cz - radius - ring;
        z1 = cz + radius + ring;

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(lx));
                pair.add(new JsonPrimitive(lz));
                loaded.add(pair);
            }
        }

        int platformY = bestHi + 1;
        int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;
        int width = area * 16;

        int entitiesBefore = ws.loadedEntityList.size();

        OutputStream shapesRaw = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        byte[] shape = new byte[16];
        int shapeCount = 0;

        for (int x = bx0; x < bx0 + width; ++x)
        {
            for (int z = bz0; z < bz0 + width; ++z)
            {
                int h = heightOf(ws, x, z);
                for (int y = h; y < platformY; ++y) shapeCount += place(ws, shapesRaw, shape, x, y, z, y == platformY - 1 ? 2 : 3, 0);
            }
        }

        int poolX = bx0 + 6, poolZ = bz0 + 6;
        int lavaX = bx0 + width - 12, lavaZ = bz0 + width - 12;

        for (int x = 0; x < 7; ++x)
        {
            for (int z = 0; z < 7; ++z)
            {
                for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, poolX + x, platformY - y, poolZ + z, 9, 0);
            }
        }

        for (int x = 0; x < 5; ++x)
        {
            for (int z = 0; z < 5; ++z)
            {
                for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, lavaX + x, platformY - y, lavaZ + z, 11, 0);
            }
        }

        // Fences around the platform
        for (int x = bx0; x < bx0 + width; ++x)
        {
            shapeCount += place(ws, shapesRaw, shape, x, platformY, bz0, 85, 0);
            shapeCount += place(ws, shapesRaw, shape, x, platformY, bz0 + width - 1, 85, 0);
        }

        for (int z = bz0; z < bz0 + width; ++z)
        {
            shapeCount += place(ws, shapesRaw, shape, bx0, platformY, z, 85, 0);
            shapeCount += place(ws, shapesRaw, shape, bx0 + width - 1, platformY, z, 85, 0);
        }

        shapesRaw.close();

        writeDetState(new File(dir, "start.txt"), seed, ws);

        // ------------------------------------------------------------ spawns
        Random r = new Random(opseed);
        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        List<Entity> list = new ArrayList<Entity>();
        byte[] spawnBuf = new byte[SPAWN_BYTES];
        OutputStream spawnsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        GZIPOutputStream spawnsTxtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "spawns.txt.gz")), 1 << 16);
        PrintWriter sw = new PrintWriter(new OutputStreamWriter(spawnsTxtGz, "UTF-8"));

        double[] clusterX = {bx0 + 25.0, bx0 + 45.0, bx0 + 25.0, bx0 + 45.0};
        double[] clusterZ = {bz0 + 25.0, bz0 + 25.0, bz0 + 45.0, bz0 + 45.0};

        for (int i = 0; i < animals; ++i)
        {
            int kind = (i / 10) % KINDS.length;
            int c = (i / 12) % 4;
            double x = clusterX[c] + (double)((i % 4) - 1.5) * 1.2;
            double z = clusterZ[c] + (double)(((i / 4) % 3) - 1.0) * 1.2;
            double y = platformY + 1.0;
            float yaw = (float)(r.nextFloat() * 360.0F);

            Entity e = EntityList.createEntityByName(KINDS[kind], ws);
            if (e == null) throw new IllegalStateException("no entity for " + KINDS[kind]);

            e.setLocationAndAngles(x, y, z, yaw, 0.0F);
            ((EntityLiving)e).onSpawnWithEgg(null);

            if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("entity " + i + " did not spawn");

            spawnIndex.put(e, Integer.valueOf(i));
            list.add(e);

            int p = 0;
            p = le32(spawnBuf, p, i);
            p = le32(spawnBuf, p, e.getEntityId());
            p = le32(spawnBuf, p, kind);
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posZ));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationYaw));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationPitch));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionZ));
            p = le32(spawnBuf, p, ((EntityAgeable)e).getGrowingAge());
            p = le32(spawnBuf, p, intField(e, "inLove"));
            p = le32(spawnBuf, p, 0); // saddled
            p = le32(spawnBuf, p, 0); // egg_timer
            p = le32(spawnBuf, p, 0); // sheared
            spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);

            sw.println(i + " " + e.getEntityId() + " " + KINDS[kind] + " " + canon(e));
        }

        spawnsOut.close();
        sw.close();
        spawnsTxtGz.close();

        // Build and write events.bin
        List<Event> events = buildEvents(platformY, bx0, bz0);
        writeEvents(new File(dir, "events.bin"), events);

        if (ws.loadedEntityList.size() != entitiesBefore + list.size())
        {
            throw new IllegalStateException("the world holds " + ws.loadedEntityList.size() + " entities, "
                + entitiesBefore + " before the setup and " + list.size() + " spawned");
        }

        // -------------------------------------------------------------- ticks
        GZIPOutputStream ticksGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "ticks.bin.gz")), 1 << 16);
        OutputStream ticksOut = new BufferedOutputStream(ticksGz, 1 << 16);
        GZIPOutputStream digestGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "digest.txt.gz")), 1 << 16);
        PrintWriter dw = new PrintWriter(new OutputStreamWriter(digestGz, "UTF-8"));
        GZIPOutputStream remGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "removals.txt.gz")), 1 << 16);
        PrintWriter rw = new PrintWriter(new OutputStreamWriter(remGz, "UTF-8"));
        GZIPOutputStream nbtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "nbt64.txt.gz")), 1 << 16);
        PrintWriter nw = new PrintWriter(new OutputStreamWriter(nbtGz, "UTF-8"));

        int chunksBefore = ws.theChunkProviderServer.getLoadedChunkCount();
        int maxList = list.size();
        int absorbed = ws.loadedEntityList.size();
        byte[] entRec = new byte[ENT_STATE_BYTES];

        for (int t = 0; t < ticks; ++t)
        {
            Trace.t("tick", Integer.valueOf(t));

            // Execute scheduled events for tick t
            for (Event ev : events)
            {
                if (ev.tick != t) continue;

                if (ev.type == 1) // ADD_EFFECT
                {
                    for (Entity e : list)
                    {
                        if (spawnIndex.get(e).intValue() == ev.targetSi && e instanceof EntityLivingBase)
                        {
                            ((EntityLivingBase)e).addPotionEffect(new PotionEffect(ev.potionId, ev.duration, ev.amplifier, ev.ambient != 0));
                            break;
                        }
                    }
                }
                else if (ev.type == 2) // CLEAR_EFFECTS
                {
                    for (Entity e : list)
                    {
                        if (spawnIndex.get(e).intValue() == ev.targetSi && e instanceof EntityLivingBase)
                        {
                            ((EntityLivingBase)e).clearActivePotions();
                            break;
                        }
                    }
                }
                else if (ev.type == 3) // APPLY_FOOD
                {
                    for (Entity e : list)
                    {
                        if (spawnIndex.get(e).intValue() == ev.targetSi && e instanceof EntityLivingBase)
                        {
                            applyFoodToEntity((EntityLivingBase)e, ev.itemId, ev.itemDamage);
                            break;
                        }
                    }
                }
                else if (ev.type == 4) // SPAWN_POTION
                {
                    EntityPotion pot = new EntityPotion(ws, ev.x, ev.y, ev.z, ev.potionDamage);
                    pot.motionX = ev.mx;
                    pot.motionY = ev.my;
                    pot.motionZ = ev.mz;
                    ws.spawnEntityInWorld(pot);
                    int si = list.size();
                    spawnIndex.put(pot, Integer.valueOf(si));
                    list.add(pot);
                    absorbed = ws.loadedEntityList.size();
                }
            }

            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);
                if (e.isDead) continue;

                updateEntity(ws, e);

                if (ws.loadedEntityList.size() > absorbed)
                {
                    for (int a = absorbed; a < ws.loadedEntityList.size(); ++a)
                    {
                        Entity n = (Entity)ws.loadedEntityList.get(a);
                        if (spawnIndex.containsKey(n)) continue;

                        spawnIndex.put(n, Integer.valueOf(list.size()));
                        list.add(n);
                        if (list.size() > maxList) maxList = list.size();
                    }
                    absorbed = ws.loadedEntityList.size();
                }

                if (e.isDead)
                {
                    int reason = 1;
                    int si = spawnIndex.get(e).intValue();

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    list.remove(i--);

                    rw.println(t + " " + si + " " + e.getEntityId() + " " + reason + " " + (e instanceof EntityLivingBase
                        ? hex(Float.floatToRawIntBits(((EntityLivingBase)e).getHealth())) : "-1") + " " + intField(e, "fire"));
                }
            }

            byte[] head = new byte[8];
            int hp = 0;
            hp = le32(head, hp, t);
            hp = le32(head, hp, list.size());
            ticksOut.write(head, 0, 8);

            for (int i = 0; i < list.size(); ++i)
            {
                writeState(entRec, t, list.get(i), spawnIndex, ws);
                ticksOut.write(entRec, 0, ENT_STATE_BYTES);
            }

            if (nbtEvery > 0 && (t < 8 || t % nbtEvery == nbtEvery - 1 || t == ticks - 1))
            {
                nw.println("t " + t);
                for (int i = 0; i < list.size(); ++i)
                {
                    Entity e = list.get(i);
                    nw.println(spawnIndex.get(e).intValue() + " " + canon(e));
                }
            }

            writeDetLine(dw, t, ws);
        }

        ticksOut.close();
        ticksGz.close();
        dw.close();
        digestGz.close();
        rw.close();
        remGz.close();
        nw.close();
        nbtGz.close();

        writeDetState(new File(dir, "end.txt"), seed, ws);

        if (ws.theChunkProviderServer.getLoadedChunkCount() != chunksBefore)
        {
            throw new IllegalStateException("the world generated chunks during the ticks");
        }

        DataOutputStream fin = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] chunkBytes = new byte[Probe.CHUNK_BYTES];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                net.minecraft.world.chunk.Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, chunkBytes);
                fin.writeInt(lx);
                fin.writeInt(lz);
                fin.write(chunkBytes, 0, 65536 * 3);
                fin.write(chunkBytes, 65536 * 5, 1024 + 1024 + 4);
                fin.writeByte(chunkBytes[65536 * 5 + 2052]);
                fin.writeByte(chunkBytes[65536 * 5 + 2053]);
            }
        }
        fin.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "potions");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("platformY", platformY);
        m.addProperty("skylightSubtracted", ws.skylightSubtracted);
        m.addProperty("animals", animals);
        m.addProperty("ticks", ticks);
        m.addProperty("opseed", opseed);
        m.addProperty("nbtEvery", nbtEvery);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);

        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        return m;
    }

    static int kindOf(Entity e)
    {
        if (e instanceof EntityPig) return KIND_PIG;
        if (e instanceof net.minecraft.entity.passive.EntityMooshroom) return KIND_MOOSHROOM;
        if (e instanceof net.minecraft.entity.passive.EntityCow) return KIND_COW;
        if (e instanceof EntityChicken) return KIND_CHICKEN;
        if (e instanceof EntitySheep) return KIND_SHEEP;
        return -1;
    }

    static int heightOf(WorldServer ws, int x, int z)
    {
        net.minecraft.world.chunk.Chunk c = ws.getChunkFromChunkCoords(x >> 4, z >> 4);
        return c.getHeightValue(x & 15, z & 15);
    }

    static int place(WorldServer ws, OutputStream out, byte[] rec, int x, int y, int z, int id, int meta) throws Exception
    {
        ws.setBlock(x, y, z, Block.getBlockById(id), meta, 2);
        int p = 0;
        p = le32(rec, p, x);
        p = le32(rec, p, y);
        p = le32(rec, p, z);
        p = le16(rec, p, id);
        rec[p++] = (byte)meta;
        rec[p++] = 0;
        out.write(rec, 0, 16);
        return 1;
    }

    static String canon(Entity e)
    {
        NBTTagCompound tag = new NBTTagCompound();
        e.writeToNBT(tag);
        return StructuresProbe.canon(tag).toString();
    }

    static long nbtHash(Entity e) throws Exception
    {
        byte[] b = canon(e).getBytes("UTF-8");
        long h = FNV_OFFSET;
        for (int i = 0; i < b.length; ++i) h = (h ^ (b[i] & 255)) * FNV_PRIME;
        return h;
    }

    static void updateEntity(WorldServer ws, Entity e)
    {
        e.lastTickPosX = e.posX;
        e.lastTickPosY = e.posY;
        e.lastTickPosZ = e.posZ;
        e.prevRotationYaw = e.rotationYaw;
        e.prevRotationPitch = e.rotationPitch;

        if (e.addedToChunk)
        {
            ++e.ticksExisted;
            e.onUpdate();
        }

        if (Double.isNaN(e.posX) || Double.isInfinite(e.posX)) e.posX = e.lastTickPosX;
        if (Double.isNaN(e.posY) || Double.isInfinite(e.posY)) e.posY = e.lastTickPosY;
        if (Double.isNaN(e.posZ) || Double.isInfinite(e.posZ)) e.posZ = e.lastTickPosZ;
        if (Double.isNaN((double)e.rotationPitch) || Double.isInfinite((double)e.rotationPitch)) e.rotationPitch = e.prevRotationPitch;
        if (Double.isNaN((double)e.rotationYaw) || Double.isInfinite((double)e.rotationYaw)) e.rotationYaw = e.prevRotationYaw;

        int var6 = MathHelper.floor_double(e.posX / 16.0D);
        int var7 = MathHelper.floor_double(e.posY / 16.0D);
        int var8 = MathHelper.floor_double(e.posZ / 16.0D);

        if (!e.addedToChunk || e.chunkCoordX != var6 || e.chunkCoordY != var7 || e.chunkCoordZ != var8)
        {
            if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
            {
                ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntityAtIndex(e, e.chunkCoordY);
            }

            if (ws.theChunkProviderServer.chunkExists(var6, var8))
            {
                e.addedToChunk = true;
                ws.getChunkFromChunkCoords(var6, var8).addEntity(e);
            }
            else
            {
                e.addedToChunk = false;
            }
        }
    }

    static void writeState(byte[] b, int tick, Entity e, Map<Entity, Integer> spawnIndex, WorldServer ws) throws Exception
    {
        int p = 0;
        EntityLiving lv = e instanceof EntityLiving ? (EntityLiving)e : null;
        EntityLivingBase lb = e instanceof EntityLivingBase ? (EntityLivingBase)e : null;

        p = le32(b, p, spawnIndex.get(e).intValue());
        p = le32(b, p, e.getEntityId());
        p = le32(b, p, kindOf(e));
        p = le64(b, p, nbtHash(e));
        p = le32(b, p, lb != null ? intField(lb, "entityAge") : 0);
        p = le32(b, p, e.ticksExisted);

        Object tasks = lv != null ? field(lv, "tasks") : null;
        int tickCount = tasks != null ? intOf(tasks, "tickCount") : -1;
        p = le32(b, p, tickCount);

        List<?> entries = tasks != null ? (List<?>)field(tasks, "taskEntries") : null;
        List<?> executing = tasks != null ? (List<?>)field(tasks, "executingTaskEntries") : null;
        int mask = 0;
        int[] slots = new int[10];

        for (int i = 0; i < slots.length; ++i) slots[i] = -1;

        if (entries != null)
        {
            for (int i = 0; i < entries.size(); ++i)
            {
                Object action = field(entries.get(i), "action");
                if (executing != null && executing.contains(entries.get(i))) mask |= 1 << i;
                if (i < slots.length) slots[i] = taskCounter(action);
            }
        }

        p = le32(b, p, mask);
        for (int i = 0; i < slots.length; ++i) p = le32(b, p, slots[i]);

        Object nav = lv != null ? lv.getNavigator() : null;
        Object path = nav != null ? field(nav, "currentPath") : null;
        p = le32(b, p, path != null ? 1 : 0);
        p = le32(b, p, path != null ? intOf(path, "currentPathIndex") : 0);
        p = le32(b, p, path != null ? intOf(path, "pathLength") : 0);
        p = le32(b, p, nav != null ? intOf(nav, "totalTicks") : 0);
        p = le32(b, p, nav != null ? intOf(nav, "ticksAtLastPos") : 0);
        p = le64(b, p, nav != null ? Double.doubleToRawLongBits(((Double)field(nav, "speed")).doubleValue()) : 0);
        Object lpc = nav != null ? field(nav, "lastPosCheck") : null;
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).xCoord) : 0);
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).yCoord) : 0);
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).zCoord) : 0);

        long nh = FNV_OFFSET;

        if (path != null)
        {
            Object points = field(path, "points");
            int n = Array.getLength(points);

            for (int i = 0; i < n; ++i)
            {
                Object pt = Array.get(points, i);
                int px = intOf(pt, "xCoord"), py = intOf(pt, "yCoord"), pz = intOf(pt, "zCoord");

                for (int k = 0; k < 4; ++k) nh = (nh ^ ((px >> (8 * k)) & 255)) * FNV_PRIME;
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((py >> (8 * k)) & 255)) * FNV_PRIME;
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((pz >> (8 * k)) & 255)) * FNV_PRIME;
            }
        }

        p = le64(b, p, nh);

        Object mh = lv != null ? lv.getMoveHelper() : null;
        p = le32(b, p, mh != null && boolOf(mh, "update") ? 1 : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posX")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posY")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posZ")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "speed")).doubleValue()) : 0);

        Object lk = lv != null ? lv.getLookHelper() : null;
        p = le32(b, p, lk != null && boolOf(lk, "isLooking") ? 1 : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posX")).doubleValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posY")).doubleValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posZ")).doubleValue()) : 0);
        p = le32(b, p, lk != null ? Float.floatToRawIntBits(((Float)field(lk, "deltaLookYaw")).floatValue()) : 0);
        p = le32(b, p, lk != null ? Float.floatToRawIntBits(((Float)field(lk, "deltaLookPitch")).floatValue()) : 0);

        Object jh = lv != null ? lv.getJumpHelper() : null;
        p = le32(b, p, jh != null && boolOf(jh, "isJumping") ? 1 : 0);
        p = le32(b, p, lv != null ? Float.floatToRawIntBits(lv.moveForward) : 0);
        p = le32(b, p, lv != null ? Float.floatToRawIntBits(lv.moveStrafing) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.rotationYawHead) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.renderYawOffset) : 0);

        p = le32(b, p, lv != null ? lv.livingSoundTime : 0);

        Object bh = lv != null ? field(lv, "bodyHelper") : null;
        p = le32(b, p, bh != null ? intOf(bh, "field_75666_b") : 0);
        p = le32(b, p, bh != null ? Float.floatToRawIntBits(((Float)field(bh, "field_75667_c")).floatValue()) : 0);
        p = le32(b, p, e instanceof EntitySheep ? intOf(e, "sheepTimer") : -1);
        p = le32(b, p, e instanceof EntityChicken ? ((EntityChicken)e).timeUntilNextEgg : -1);
        p = le32(b, p, e instanceof net.minecraft.entity.passive.EntityAnimal ? intOf(e, "breeding") : 0);
        p = le32(b, p, lb != null ? intOf(lb, "revengeTimer") : 0);
        p = le32(b, p, (e.addedToChunk ? 1 : 0) | (e.onGround ? 2 : 0));
        p = le64(b, p, Det.state(e instanceof EntityLiving ? ((EntityLiving)e).getRNG() : entityRand(e)));
        p = le32(b, p, 0);

        if (p != ENT_STATE_BYTES) throw new IllegalStateException("state record wrote " + p + " bytes, the layout says " + ENT_STATE_BYTES);
    }

    static Random entityRand(Entity e) throws Exception
    {
        return (Random)findField(e, "rand").get(e);
    }

    static int taskCounter(Object action) throws Exception
    {
        if (action == null) return -1;
        String name = action.getClass().getSimpleName();
        if ("EntityAIMate".equals(name)) return intOf(action, "spawnBabyDelay");
        if ("EntityAITempt".equals(name)) return intOf(action, "delayTemptCounter");
        if ("EntityAIFollowParent".equals(name)) return intOf(action, "field_75345_d");
        if ("EntityAIWatchClosest".equals(name)) return intOf(action, "lookTime");
        if ("EntityAILookIdle".equals(name)) return intOf(action, "idleTime");
        if ("EntityAIEatGrass".equals(name)) return intOf(action, "field_151502_a");
        return -1;
    }

    static long hashLook(Object look) throws Exception
    {
        long h = FNV_OFFSET;
        h = (h ^ (boolOf(look, "isLooking") ? 1 : 0)) * FNV_PRIME;
        h = (h ^ Double.doubleToRawLongBits(((Double)field(look, "posX")).doubleValue())) * FNV_PRIME;
        h = (h ^ Double.doubleToRawLongBits(((Double)field(look, "posY")).doubleValue())) * FNV_PRIME;
        h = (h ^ Double.doubleToRawLongBits(((Double)field(look, "posZ")).doubleValue())) * FNV_PRIME;
        h = (h ^ Float.floatToRawIntBits(((Float)field(look, "deltaLookYaw")).floatValue())) * FNV_PRIME;
        h = (h ^ Float.floatToRawIntBits(((Float)field(look, "deltaLookPitch")).floatValue())) * FNV_PRIME;
        return h;
    }

    static Field findField(Object o, String name)
    {
        for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass())
        {
            try
            {
                Field f = c.getDeclaredField(name);
                f.setAccessible(true);
                return f;
            }
            catch (NoSuchFieldException e)
            {
            }
        }
        throw new IllegalStateException("no field " + name + " on " + o.getClass());
    }

    static Object field(Object o, String name)
    {
        try
        {
            return findField(o, name).get(o);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static int intField(Object o, String name)
    {
        Object v = field(o, name);
        return v instanceof Number ? ((Number)v).intValue() : 0;
    }

    static int intOf(Object o, String name)
    {
        return intField(o, name);
    }

    static boolean boolOf(Object o, String name)
    {
        Object v = field(o, name);
        return v instanceof Boolean && ((Boolean)v).booleanValue();
    }

    static void writeDetLine(PrintWriter w, int t, WorldServer ws) throws Exception
    {
        StringBuilder b = new StringBuilder();
        b.append("t ").append(t);

        for (int role = 0; role < Det.ROLES; ++role)
        {
            b.append(" role ").append(role).append(' ').append(hex(Det.seederState(role))).append(' ')
                .append(hex(Det.mathState(role))).append(' ').append(hex(Det.splitState(role)));
        }

        synchronized (Det.nextId)
        {
            b.append(" nextId ").append(Det.nextId[Det.OTHER]);
        }

        Object wr = ws.rand;
        b.append(" worldRand ").append(hex(Det.state((Random)wr)));
        b.append(" worldRandGauss ").append(boolOf(wr, "haveNextNextGaussian") ? 1 : 0);
        w.println(b.toString());
    }

    static void writeDetState(File f, long seed, WorldServer ws) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
        w.println("resetSeed " + seed);
        w.println("worldSeed " + Det.worldSeed);
        StringBuilder b = new StringBuilder("nextId");

        for (int role = 0; role < Det.ROLES; ++role) b.append(' ').append(Det.nextId[role]);

        w.println(b.toString());

        for (int role = 0; role < Det.ROLES; ++role)
        {
            w.println("digest " + role + " " + hex(Det.seederState(role)) + " " + hex(Det.mathState(role)) + " " + hex(Det.splitState(role)));
        }

        synchronized (Det.class)
        {
            for (Det.SplitRandom s : Det.splits)
            {
                b.setLength(0);
                b.append("split ").append(s.name);

                for (int role = 0; role < Det.ROLES; ++role) b.append(' ').append(hex(Det.state(s.d[role])));

                for (int role = 0; role < Det.ROLES; ++role) b.append(' ').append(s.used[role] ? 1 : 0);

                w.println(b.toString());
            }
        }

        Object wr = ws.rand;
        b.setLength(0);
        b.append("worldRand ").append(hex(Det.state((Random)wr))).append(' ').append(boolOf(wr, "haveNextNextGaussian") ? 1 : 0);
        w.println(b.toString());

        w.close();
    }

    static String hex(long bits)
    {
        StringBuilder b = new StringBuilder(16);
        for (int i = 15; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));
        return b.toString();
    }

    static int le16(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
        return o + 2;
    }

    static int le32(byte[] a, int o, int v)
    {
        for (int i = 0; i < 4; ++i) a[o + i] = (byte)(v >> (8 * i));
        return o + 4;
    }

    static int le64(byte[] a, int o, long v)
    {
        for (int i = 0; i < 8; ++i) a[o + i] = (byte)(v >> (8 * i));
        return o + 8;
    }
}
