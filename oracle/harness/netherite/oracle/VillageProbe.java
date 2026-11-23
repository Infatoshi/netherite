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
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.block.BlockDoor;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityAgeable;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.monster.EntityIronGolem;
import net.minecraft.entity.passive.EntityVillager;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.MathHelper;
import net.minecraft.village.MerchantRecipe;
import net.minecraft.village.MerchantRecipeList;
import net.minecraft.village.Village;
import net.minecraft.village.VillageCollection;
import net.minecraft.village.VillageDoorInfo;
import net.minecraft.village.VillageSiege;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;

public final class VillageProbe
{
    static final String[] KINDS = {"Villager", "VillagerGolem", "Item"};
    static final int KIND_VILLAGER = 0, KIND_GOLEM = 1, KIND_OTHER = 2;

    static final int ENT_STATE_BYTES = 320;
    static final int SPAWN_BYTES = 100;
    static final int VILLAGE_STATE_BYTES = 36;
    static final int BLOCK_WRITE_BYTES = 20;

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    private VillageProbe() {}

    public static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
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
        }, "Oracle VillageProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 0;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 0;
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 5;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 3;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 4800;
        long timestart = cmd.has("timestart") ? cmd.get("timestart").getAsLong() : 11000L;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;
        int nbtEvery = cmd.has("nbtEvery") ? cmd.get("nbtEvery").getAsInt() : 64;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        Trace.restart();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        // Pin Collections.shuffle Random for reproducible trades
        Field shufField = null;
        for (Field f : Collections.class.getDeclaredFields())
        {
            if (f.getType() == Random.class) shufField = f;
        }
        if (shufField != null)
        {
            shufField.setAccessible(true);
            shufField.set(null, new Random(opseed + 999L));
        }

        // Set time and daylight cycle
        ws.getWorldInfo().setWorldTime(timestart);
        ws.getWorldInfo().incrementTotalWorldTime(timestart);
        ws.getGameRules().setOrCreateGameRule("doDaylightCycle", "true");
        ws.calculateInitialSkylight();

        // Load chunks in the specified radius + ring
        int x0 = cx - radius - ring, x1 = cx + radius + ring;
        int z0 = cz - radius - ring, z1 = cz + radius + ring;
        JsonArray loaded = new JsonArray();

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null)
                    throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(lx));
                pair.add(new JsonPrimitive(lz));
                loaded.add(pair);
            }
        }

        int platformY = 64;
        int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;
        int width = (2 * radius + 1) * 16;

        int entitiesBefore = ws.loadedEntityList.size();

        // ------------------------------------------------------------ shapes
        OutputStream shapesRaw = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        byte[] shape = new byte[16];
        int shapeCount = 0;

        // Flatten the entire operational area: dirt up to platformY - 1, grass at platformY - 1
        for (int x = bx0; x < bx0 + width; ++x)
        {
            for (int z = bz0; z < bz0 + width; ++z)
            {
                int h = heightOf(ws, x, z);
                for (int y = h; y < platformY; ++y)
                {
                    shapeCount += place(ws, shapesRaw, shape, x, y, z, y == platformY - 1 ? 2 : 3, 0);
                }
                // Clear any blocks above platform: first 7 blocks unconditionally so no
                // lake air pockets in Java leave raw-terrain dirt in native.
                for (int y = platformY; y <= platformY + 6; ++y)
                {
                    shapeCount += place(ws, shapesRaw, shape, x, y, z, 0, 0);
                }
                for (int y = platformY + 7; y < platformY + 36; ++y)
                {
                    if (ws.getBlock(x, y, z) != Blocks.air || y < heightOf(ws, x, z))
                    {
                        shapeCount += place(ws, shapesRaw, shape, x, y, z, 0, 0);
                    }
                }
            }
        }

        // Layout for 3 villages:
        int[][] villageCenters = {
            {-45, -45},
            {45, -45},
            {-45, 45}
        };

        // In each village, build houses with 80 total doors:
        // 76 doors around courtyard (distance 12-14 from center)
        // 4 outer doors at distance 22 from center (will age out at tick 1201!)
        for (int vi = 0; vi < villageCenters.length; ++vi)
        {
            int vx = villageCenters[vi][0];
            int vz = villageCenters[vi][1];

            // Pave courtyard: 21x21 cobble
            for (int x = vx - 10; x <= vx + 10; ++x)
            {
                for (int z = vz - 10; z <= vz + 10; ++z)
                {
                    shapeCount += place(ws, shapesRaw, shape, x, platformY - 1, z, 4, 0);
                }
            }

            // North terrace: z = vz - 12. 19 doors along x from vx - 9 to vx + 9.
            // Orientation: meta = 1 (checks Z axis).
            for (int dx = -9; dx <= 9; ++dx)
            {
                int x = vx + dx;
                int z = vz - 12;
                shapeCount += place(ws, shapesRaw, shape, x, platformY, z, 64, 1);
                shapeCount += place(ws, shapesRaw, shape, x, platformY + 1, z, 64, 8);
                for (int rz = z - 4; rz < z; ++rz)
                {
                    shapeCount += place(ws, shapesRaw, shape, x, platformY + 2, rz, 4, 0);
                }
            }

            // South terrace: z = vz + 12. 19 doors along x from vx - 9 to vx + 9.
            // Orientation: meta = 3 (checks Z axis).
            for (int dx = -9; dx <= 9; ++dx)
            {
                int x = vx + dx;
                int z = vz + 12;
                shapeCount += place(ws, shapesRaw, shape, x, platformY, z, 64, 3);
                shapeCount += place(ws, shapesRaw, shape, x, platformY + 1, z, 64, 8);
                for (int rz = z + 1; rz <= z + 4; ++rz)
                {
                    shapeCount += place(ws, shapesRaw, shape, x, platformY + 2, rz, 4, 0);
                }
            }

            // West terrace: x = vx - 12. 19 doors along z from vz - 9 to vz + 9.
            // Orientation: meta = 0 (checks X axis).
            for (int dz = -9; dz <= 9; ++dz)
            {
                int x = vx - 12;
                int z = vz + dz;
                shapeCount += place(ws, shapesRaw, shape, x, platformY, z, 64, 0);
                shapeCount += place(ws, shapesRaw, shape, x, platformY + 1, z, 64, 8);
                for (int rx = x - 4; rx < x; ++rx)
                {
                    shapeCount += place(ws, shapesRaw, shape, rx, platformY + 2, z, 4, 0);
                }
            }

            // East terrace: x = vx + 12. 19 doors along z from vz - 9 to vz + 9.
            // Orientation: meta = 2 (checks X axis).
            for (int dz = -9; dz <= 9; ++dz)
            {
                int x = vx + 12;
                int z = vz + dz;
                shapeCount += place(ws, shapesRaw, shape, x, platformY, z, 64, 2);
                shapeCount += place(ws, shapesRaw, shape, x, platformY + 1, z, 64, 8);
                for (int rx = x + 1; rx <= x + 4; ++rx)
                {
                    shapeCount += place(ws, shapesRaw, shape, rx, platformY + 2, z, 4, 0);
                }
            }

            // 4 outer doors at distance 22 (outside 16-block villager scan radius):
            // North outer door: (vx, platformY, vz - 22)
            shapeCount += place(ws, shapesRaw, shape, vx, platformY, vz - 22, 64, 1);
            shapeCount += place(ws, shapesRaw, shape, vx, platformY + 1, vz - 22, 64, 8);
            for (int rz = vz - 26; rz < vz - 22; ++rz) shapeCount += place(ws, shapesRaw, shape, vx, platformY + 2, rz, 4, 0);

            // South outer door: (vx, platformY, vz + 22)
            shapeCount += place(ws, shapesRaw, shape, vx, platformY, vz + 22, 64, 3);
            shapeCount += place(ws, shapesRaw, shape, vx, platformY + 1, vz + 22, 64, 8);
            for (int rz = vz + 23; rz <= vz + 26; ++rz) shapeCount += place(ws, shapesRaw, shape, vx, platformY + 2, rz, 4, 0);

            // West outer door: (vx - 22, platformY, vz)
            shapeCount += place(ws, shapesRaw, shape, vx - 22, platformY, vz, 64, 0);
            shapeCount += place(ws, shapesRaw, shape, vx - 22, platformY + 1, vz, 64, 8);
            for (int rx = vx - 26; rx < vx - 22; ++rx) shapeCount += place(ws, shapesRaw, shape, rx, platformY + 2, vz, 4, 0);

            // East outer door: (vx + 22, platformY, vz)
            shapeCount += place(ws, shapesRaw, shape, vx + 22, platformY, vz, 64, 2);
            shapeCount += place(ws, shapesRaw, shape, vx + 22, platformY + 1, vz, 64, 8);
            for (int rx = vx + 23; rx <= vx + 26; ++rx) shapeCount += place(ws, shapesRaw, shape, rx, platformY + 2, vz, 4, 0);
        }

        shapesRaw.close();

        // ------------------------------------------------------------- start
        writeDetState(new File(dir, "start.txt"), seed, ws);

        // ------------------------------------------------------------ spawns
        Random r = new Random(opseed);
        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        List<Entity> list = new ArrayList<Entity>();
        byte[] spawnBuf = new byte[SPAWN_BYTES];
        OutputStream spawnsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        GZIPOutputStream spawnsTxtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "spawns.txt.gz")), 1 << 16);
        PrintWriter sw = new PrintWriter(new OutputStreamWriter(spawnsTxtGz, "UTF-8"));

        int totalInitialVillagers = 66; // 22 per village * 3
        int entIdx = 0;

        for (int vi = 0; vi < villageCenters.length; ++vi)
        {
            int vx = villageCenters[vi][0];
            int vz = villageCenters[vi][1];

            for (int i = 0; i < 22; ++i)
            {
                int x = vx - 7 + (i % 5) * 3 + (r.nextInt(3) - 1);
                int z = vz - 7 + (i / 5) * 3 + (r.nextInt(3) - 1);
                int y = platformY;
                float yaw = r.nextFloat() * 360.0F;

                Entity e = EntityList.createEntityByName("Villager", ws);
                if (e == null) throw new IllegalStateException("no entity for Villager");

                e.setLocationAndAngles(x + 0.5D, y, z + 0.5D, yaw, 0.0F);

                boolean baby = (i % 7 == 2);
                if (baby) ((EntityAgeable)e).setGrowingAge(-24000 + r.nextInt(1200));

                ((EntityLiving)e).onSpawnWithEgg(null);

                if (!ws.spawnEntityInWorld(e))
                    throw new IllegalStateException("villager " + entIdx + " did not spawn");

                spawnIndex.put(e, Integer.valueOf(entIdx));
                list.add(e);

                writeSpawnRecord(spawnsOut, spawnBuf, entIdx, e, KIND_VILLAGER, 0);
                sw.println(entIdx + " " + e.getEntityId() + " Villager " + canon(e));

                ++entIdx;
            }
        }

        // Initialize doors in VillageCollection by priming positions
        for (int vi = 0; vi < villageCenters.length; ++vi)
        {
            int vx = villageCenters[vi][0];
            int vz = villageCenters[vi][1];
            ws.villageCollectionObj.addVillagerPosition(vx, platformY, vz);
            ws.villageCollectionObj.addVillagerPosition(vx - 8, platformY, vz - 8);
            ws.villageCollectionObj.addVillagerPosition(vx + 8, platformY, vz - 8);
            ws.villageCollectionObj.addVillagerPosition(vx - 8, platformY, vz + 8);
            ws.villageCollectionObj.addVillagerPosition(vx + 8, platformY, vz + 8);
            ws.villageCollectionObj.addVillagerPosition(vx, platformY, vz - 22);
            ws.villageCollectionObj.addVillagerPosition(vx, platformY, vz + 22);
            ws.villageCollectionObj.addVillagerPosition(vx - 22, platformY, vz);
            ws.villageCollectionObj.addVillagerPosition(vx + 22, platformY, vz);
        }

        // Probe player
        EntityPlayerMP player = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        if (!ws.playerEntities.contains(player))
        {
            ws.playerEntities.add(player);
        }
        player.setPosition(-45.0D, platformY, -45.0D);

        Field vsField = World.class.getDeclaredField("villageSiegeObj");
        vsField.setAccessible(true);
        VillageSiege vs = (VillageSiege)vsField.get(ws);

        final List<int[]> tickBlockWrites = new ArrayList<int[]>();
        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta, int flags)
            {
                tickBlockWrites.add(new int[] {x, y, z, meta, flags});
            }
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                tickBlockWrites.add(new int[] {x, y, z, meta, 2});
            }
        };

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
        int birthsCount = 0;
        int golemsSpawned = 0;
        int maxList = list.size();
        int absorbed = ws.loadedEntityList.size();
        byte[] entRec = new byte[ENT_STATE_BYTES];
        byte[] vRec = new byte[VILLAGE_STATE_BYTES];
        byte[] bwRec = new byte[BLOCK_WRITE_BYTES];

        double[][] waypoints = {
            {-45.0, -45.0},
            {-40.0, -45.0},
            {-40.0, -40.0},
            {-45.0, -40.0},
            {-50.0, -40.0},
            {-50.0, -45.0},
            {-50.0, -50.0},
            {-45.0, -50.0}
        };

        for (int t = 0; t < ticks; ++t)
        {
            Trace.t("tick", Integer.valueOf(t));
            tickBlockWrites.clear();

            // 1. Advance world time
            long wt = timestart + t;
            ws.getWorldInfo().setWorldTime(wt);
            ws.getWorldInfo().incrementTotalWorldTime(wt);
            int skylight = ws.calculateSkylightSubtracted(1.0F);
            ws.skylightSubtracted = skylight;

            // 2. Village collection and siege tick
            ws.villageCollectionObj.tick();
            vs.tick();

            // 3. Move/pose probe player
            if (t < 1700)
            {
                int wpIdx = (t / 40) % waypoints.length;
                int nextWp = (wpIdx + 1) % waypoints.length;
                double frac = (t % 40) / 40.0D;
                double px = waypoints[wpIdx][0] + (waypoints[nextWp][0] - waypoints[wpIdx][0]) * frac;
                double pz = waypoints[wpIdx][1] + (waypoints[nextWp][1] - waypoints[wpIdx][1]) * frac;
                player.setPosition(px, platformY, pz);

                if (t % 120 == 60)
                {
                    EntityVillager nearestAdult = null;
                    double bestDist = 16.0D;
                    for (int i = 0; i < list.size(); ++i)
                    {
                        Entity e = list.get(i);
                        if (e instanceof EntityVillager && !e.isDead && ((EntityVillager)e).getGrowingAge() == 0)
                        {
                            double d = player.getDistanceSqToEntity(e);
                            if (d < bestDist)
                            {
                                bestDist = d;
                                nearestAdult = (EntityVillager)e;
                            }
                        }
                    }
                    if (nearestAdult != null)
                    {
                        MerchantRecipeList recipes = nearestAdult.getRecipes(player);
                        if (recipes != null && !recipes.isEmpty())
                        {
                            MerchantRecipe rec = (MerchantRecipe)recipes.get(recipes.size() - 1);
                            nearestAdult.useRecipe(rec);
                        }
                    }
                }
            }
            else
            {
                player.setPosition(-45.0D, platformY, -45.0D);
            }

            // 4. Update entities
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

                        int newIdx = entIdx++;
                        spawnIndex.put(n, Integer.valueOf(newIdx));
                        list.add(n);

                        int nKind = kindOf(n);
                        if (n instanceof EntityIronGolem) ++golemsSpawned;
                        if (n instanceof EntityVillager && ((EntityVillager)n).isChild()) ++birthsCount;

                        writeSpawnRecord(spawnsOut, spawnBuf, newIdx, n, nKind, t);
                        sw.println(newIdx + " " + n.getEntityId() + " " + KINDS[nKind] + " " + canon(n));

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
                    rw.println(t + " " + si + " " + e.getEntityId() + " " + reason + " "
                        + (e instanceof EntityLivingBase ? hex(Float.floatToRawIntBits(((EntityLivingBase)e).getHealth())) : "-1")
                        + " " + intField(e, "fire"));
                }
            }

            // 5. Write tick header: (tick, num_entities, num_villages, num_block_writes)
            List vList = ws.villageCollectionObj.getVillageList();
            byte[] head = new byte[16];
            int hp = 0;
            hp = le32(head, hp, t);
            hp = le32(head, hp, list.size());
            hp = le32(head, hp, vList.size());
            hp = le32(head, hp, tickBlockWrites.size());
            ticksOut.write(head, 0, 16);

            for (int vi = 0; vi < vList.size(); ++vi)
            {
                Village v = (Village)vList.get(vi);
                writeVillageState(vRec, v);
                ticksOut.write(vRec, 0, VILLAGE_STATE_BYTES);
            }

            for (int bwi = 0; bwi < tickBlockWrites.size(); ++bwi)
            {
                int[] bw = tickBlockWrites.get(bwi);
                int bp = 0;
                bp = le32(bwRec, bp, bw[0]);
                bp = le32(bwRec, bp, bw[1]);
                bp = le32(bwRec, bp, bw[2]);
                bp = le32(bwRec, bp, bw[3]);
                bp = le32(bwRec, bp, bw[4]);
                ticksOut.write(bwRec, 0, BLOCK_WRITE_BYTES);
            }

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

        spawnsOut.close();
        sw.close();
        spawnsTxtGz.close();
        ticksOut.close();
        ticksGz.close();
        dw.close();
        digestGz.close();
        rw.close();
        remGz.close();
        nw.close();
        nbtGz.close();

        // --------------------------------------------------------------- end
        writeDetState(new File(dir, "end.txt"), seed, ws);

        if (ws.theChunkProviderServer.getLoadedChunkCount() != chunksBefore)
        {
            throw new IllegalStateException("the world generated chunks during the ticks: "
                + chunksBefore + " before, " + ws.theChunkProviderServer.getLoadedChunkCount() + " after");
        }

        // --------------------------------------------------------- final.bin.gz
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

        // --------------------------------------------------------- manifest.json
        JsonObject m = new JsonObject();
        m.addProperty("kind", "villagers");
        m.addProperty("seed", seed);
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("ticks", ticks);
        m.addProperty("timestart", timestart);
        m.addProperty("opseed", opseed);
        m.addProperty("skylightSubtracted", ws.skylightSubtracted);
        m.add("loaded", loaded);
        m.addProperty("villages", villageCenters.length);
        m.addProperty("initial_villagers", totalInitialVillagers);
        m.addProperty("births", birthsCount);
        m.addProperty("golems_spawned", golemsSpawned);
        m.addProperty("max_list", maxList);

        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("ticks", ticks);
        res.addProperty("births", birthsCount);
        res.addProperty("golems", golemsSpawned);
        res.addProperty("maxList", maxList);
        return res;
    }

    // -------------------------------------------------------------- helpers

    static void writeSpawnRecord(OutputStream out, byte[] buf, int idx, Entity e, int kind, int spawnTick) throws Exception
    {
        int p = 0;
        p = le32(buf, p, idx);
        p = le32(buf, p, e.getEntityId());
        p = le32(buf, p, kind);
        p = le64(buf, p, Double.doubleToRawLongBits(e.posX));
        p = le64(buf, p, Double.doubleToRawLongBits(e.posY));
        p = le64(buf, p, Double.doubleToRawLongBits(e.posZ));
        p = le32(buf, p, Float.floatToRawIntBits(e.rotationYaw));
        p = le32(buf, p, Float.floatToRawIntBits(e.rotationPitch));
        p = le64(buf, p, Double.doubleToRawLongBits(e.motionX));
        p = le64(buf, p, Double.doubleToRawLongBits(e.motionY));
        p = le64(buf, p, Double.doubleToRawLongBits(e.motionZ));
        p = le32(buf, p, e instanceof EntityAgeable ? ((EntityAgeable)e).getGrowingAge() : 0);
        p = le32(buf, p, e instanceof EntityVillager ? ((EntityVillager)e).getProfession() : 0);
        p = le32(buf, p, e instanceof EntityVillager ? intField(e, "wealth") : 0);
        p = le32(buf, p, e instanceof EntityIronGolem ? (((EntityIronGolem)e).isPlayerCreated() ? 1 : 0) : 0);
        p = le32(buf, p, spawnTick);
        while (p < SPAWN_BYTES) buf[p++] = 0;
        out.write(buf, 0, SPAWN_BYTES);
    }

    static void writeVillageState(byte[] b, Village v)
    {
        int p = 0;
        ChunkCoordinates c = v.getCenter();
        p = le32(b, p, c.posX);
        p = le32(b, p, c.posY);
        p = le32(b, p, c.posZ);
        p = le32(b, p, v.getVillageRadius());
        p = le32(b, p, v.getNumVillageDoors());
        p = le32(b, p, v.getNumVillagers());
        p = le32(b, p, intField(v, "numIronGolems"));
        p = le32(b, p, intField(v, "noBreedTicks"));
        p = le32(b, p, intField(v, "lastAddDoorTimestamp"));
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
        int[] slots = new int[16];
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
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "speed")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posX")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posY")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posZ")).doubleValue()) : 0);

        Object lk = lv != null ? lv.getLookHelper() : null;
        p = le32(b, p, lk != null && boolOf(lk, "isLooking") ? 1 : 0);
        p = le32(b, p, lk != null ? Float.floatToRawIntBits(((Float)field(lk, "deltaLookYaw")).floatValue()) : 0);
        p = le32(b, p, lk != null ? Float.floatToRawIntBits(((Float)field(lk, "deltaLookPitch")).floatValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posX")).doubleValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posY")).doubleValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posZ")).doubleValue()) : 0);

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

        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.getHealth()) : 0);
        p = le32(b, p, (e.addedToChunk ? 1 : 0) | (e.onGround ? 2 : 0) | (e.isInWater() ? 4 : 0) | (e.isCollidedHorizontally ? 8 : 0));

        p = le32(b, p, e instanceof EntityAgeable ? ((EntityAgeable)e).getGrowingAge() : 0);
        p = le32(b, p, e instanceof EntityVillager ? ((EntityVillager)e).getProfession() : 0);
        p = le32(b, p, e instanceof EntityVillager ? intField(e, "wealth") : 0);
        p = le32(b, p, e instanceof EntityVillager ? intField(e, "timeUntilReset") : 0);
        p = le32(b, p, e instanceof EntityVillager ? (boolField(e, "needsInitilization") ? 1 : 0) : 0);
        p = le32(b, p, e instanceof EntityVillager ? (((EntityVillager)e).isMating() ? 1 : 0) : 0);
        p = le32(b, p, e instanceof EntityVillager ? (((EntityVillager)e).isPlaying() ? 1 : 0) : 0);
        p = le32(b, p, e instanceof EntityIronGolem ? ((EntityIronGolem)e).getAttackTimer() : 0);
        p = le32(b, p, e instanceof EntityIronGolem ? ((EntityIronGolem)e).getHoldRoseTick() : 0);

        p = le64(b, p, Det.state(e instanceof EntityLiving ? ((EntityLiving)e).getRNG() : entityRand(e)));
        p = le32(b, p, 0); // pad

        if (p != ENT_STATE_BYTES) throw new IllegalStateException("state record wrote " + p + " bytes, expected " + ENT_STATE_BYTES);
    }

    static int kindOf(Entity e)
    {
        if (e instanceof EntityVillager) return KIND_VILLAGER;
        if (e instanceof EntityIronGolem) return KIND_GOLEM;
        return KIND_OTHER;
    }

    static String canon(Entity e)
    {
        net.minecraft.nbt.NBTTagCompound tag = new net.minecraft.nbt.NBTTagCompound();
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

    static int heightOf(WorldServer ws, int x, int z)
    {
        return ws.getHeightValue(x, z);
    }

    static int place(WorldServer ws, OutputStream out, byte[] buf, int x, int y, int z, int id, int meta) throws Exception
    {
        ws.setBlock(x, y, z, Block.getBlockById(id), meta, 2);
        le32(buf, 0, x);
        le32(buf, 4, y);
        le32(buf, 8, z);
        le16(buf, 12, id);
        buf[14] = (byte)meta;
        buf[15] = 0;
        out.write(buf, 0, 16);
        return 1;
    }

    static int taskCounter(Object action) throws Exception
    {
        if (action == null) return -1;
        String c = action.getClass().getSimpleName();
        if (c.equals("EntityAIVillagerMate")) return intOf(action, "matingTimeout");
        if (c.equals("EntityAIFollowGolem")) return intOf(action, "takeGolemRoseTick");
        if (c.equals("EntityAIPlay")) return intOf(action, "playTime");
        if (c.equals("EntityAIOpenDoor")) return intOf(action, "field_75360_j");
        if (c.equals("EntityAILookAtVillager")) return intOf(action, "lookTime");
        if (c.equals("EntityAIWatchClosest") || c.equals("EntityAIWatchClosest2") || c.equals("EntityAILookAtTradePlayer"))
            return intOf(action, "lookTime");
        if (c.equals("EntityAILookIdle")) return intOf(action, "idleTime");
        return 0;
    }

    static Random entityRand(Entity e) throws Exception
    {
        return (Random)findField(e, "rand").get(e);
    }

    static void writeDetState(File f, long seed, WorldServer ws) throws Exception
    {
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(f), "UTF-8"));
        w.println("resetSeed " + seed);
        w.println("worldSeed " + Det.worldSeed);
        StringBuilder b = new StringBuilder("nextId");
        for (int role = 0; role < Det.ROLES; ++role) b.append(" ").append(Det.nextId[role]);
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
                for (int role = 0; role < Det.ROLES; ++role) b.append(" ").append(hex(Det.state(s.d[role])));
                for (int role = 0; role < Det.ROLES; ++role) b.append(" ").append(s.used[role] ? 1 : 0);
                w.println(b.toString());
            }
        }

        Object wr = ws.rand;
        b.setLength(0);
        b.append("worldRand ").append(hex(Det.state((Random)wr))).append(" ").append(boolOf(wr, "haveNextNextGaussian") ? 1 : 0);
        w.println(b.toString());
        w.close();
    }

    static void writeDetLine(PrintWriter w, int tick, WorldServer ws) throws Exception
    {
        StringBuilder b = new StringBuilder("t ");
        b.append(tick);
        for (int r = 0; r < Det.ROLES; ++r)
        {
            b.append(" role ").append(r)
             .append(" seeder ").append(hex(Det.seederState(r)))
             .append(" math ").append(hex(Det.mathState(r)))
             .append(" split ").append(hex(Det.splitState(r)));
        }
        b.append(" nextId ").append(Det.nextId[Det.SERVER]);
        Object wr = ws.rand;
        b.append(" worldRand ").append(hex(Det.state((Random)wr)))
         .append(" worldRandGauss ").append(boolOf(wr, "haveNextNextGaussian") ? 1 : 0);
        w.println(b.toString());
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
            catch (NoSuchFieldException e) {}
        }
        throw new IllegalArgumentException("no field " + name + " in " + o.getClass());
    }

    static Object field(Object o, String name)
    {
        try { return findField(o, name).get(o); }
        catch (Exception e) { throw new RuntimeException(e); }
    }

    static int intField(Object o, String name)
    {
        try { return findField(o, name).getInt(o); }
        catch (Exception e) { throw new RuntimeException(e); }
    }

    static boolean boolField(Object o, String name)
    {
        try { return findField(o, name).getBoolean(o); }
        catch (Exception e) { throw new RuntimeException(e); }
    }

    static int intOf(Object o, String name) throws Exception
    {
        return findField(o, name).getInt(o);
    }

    static boolean boolOf(Object o, String name) throws Exception
    {
        return findField(o, name).getBoolean(o);
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
        le32(a, o, (int)v);
        le32(a, o + 4, (int)(v >> 32));
        return o + 8;
    }
}
