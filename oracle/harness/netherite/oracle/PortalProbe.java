package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.Block;
import net.minecraft.block.BlockPortal;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.Direction;
import net.minecraft.util.LongHashMap;
import net.minecraft.util.MathHelper;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.Teleporter;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.WorldSettings;

public final class PortalProbe
{
    private PortalProbe() {}

    static Field fPortalCounter;
    static Field fInPortal;

    static
    {
        try
        {
            fPortalCounter = Entity.class.getDeclaredField("portalCounter");
            fPortalCounter.setAccessible(true);
            fInPortal = Entity.class.getDeclaredField("inPortal");
            fInPortal.setAccessible(true);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static int getPortalCounter(Entity e)
    {
        try { return fPortalCounter.getInt(e); } catch (Exception ex) { throw new RuntimeException(ex); }
    }

    static void setPortalCounter(Entity e, int v)
    {
        try { fPortalCounter.setInt(e, v); } catch (Exception ex) { throw new RuntimeException(ex); }
    }

    static class WriteRecord
    {
        byte dim;
        int x;
        short y;
        int z;
        short id;
        byte meta;

        WriteRecord(byte dim, int x, short y, int z, short id, byte meta)
        {
            this.dim = dim;
            this.x = x;
            this.y = y;
            this.z = z;
            this.id = id;
            this.meta = meta;
        }
    }

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
        }, "Oracle PortalProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static long randState(Random r)
    {
        try
        {
            Field f = Random.class.getDeclaredField("seed");
            f.setAccessible(true);
            AtomicLong a = (AtomicLong)f.get(r);
            return a.get();
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static LongHashMap getCache(Teleporter tp)
    {
        try
        {
            Field f = Teleporter.class.getDeclaredField("destinationCoordinateCache");
            f.setAccessible(true);
            return (LongHashMap)f.get(tp);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static List getKeys(Teleporter tp)
    {
        try
        {
            Field f = Teleporter.class.getDeclaredField("destinationCoordinateKeys");
            f.setAccessible(true);
            return (List)f.get(tp);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static void resetTeleporter(Teleporter tp, long seed)
    {
        try
        {
            List keys = getKeys(tp);
            LongHashMap cache = getCache(tp);
            for (Object k : keys)
            {
                cache.remove(((Long)k).longValue());
            }
            keys.clear();
            getRand(tp).setSeed(seed);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static Random getRand(Teleporter tp)
    {
        try
        {
            Field f = Teleporter.class.getDeclaredField("random");
            f.setAccessible(true);
            return (Random)f.get(tp);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static void writeLe16(DataOutputStream dos, int v) throws Exception
    {
        dos.writeByte(v & 0xff);
        dos.writeByte((v >> 8) & 0xff);
    }

    static void writeLe32(DataOutputStream dos, int v) throws Exception
    {
        dos.writeByte(v & 0xff);
        dos.writeByte((v >> 8) & 0xff);
        dos.writeByte((v >> 16) & 0xff);
        dos.writeByte((v >> 24) & 0xff);
    }

    static void writeLe64(DataOutputStream dos, long v) throws Exception
    {
        for (int i = 0; i < 8; ++i)
        {
            dos.writeByte((int)(v & 0xff));
            v >>>= 8;
        }
    }

    static void writeLeFloat(DataOutputStream dos, float f) throws Exception
    {
        writeLe32(dos, Float.floatToRawIntBits(f));
    }

    static void writeLeDouble(DataOutputStream dos, double d) throws Exception
    {
        writeLe64(dos, Double.doubleToRawLongBits(d));
    }

    static void buildPortalFrame(WorldServer ws, int ox, int oy, int oz, int w, int h, int axis)
    {
        int dx = (axis == 1) ? 1 : 0;
        int dz = (axis == 1) ? 0 : 1;

        // Clear interior and frame area
        for (int i = 0; i <= w + 1; ++i)
        {
            for (int j = 0; j <= h + 1; ++j)
            {
                int bx = ox + i * dx;
                int by = oy + j;
                int bz = oz + i * dz;
                boolean isBorder = (i == 0 || i == w + 1 || j == 0 || j == h + 1);
                ws.setBlock(bx, by, bz, isBorder ? Blocks.obsidian : Blocks.air, 0, 2);
            }
        }

        // Fill portal blocks
        for (int i = 1; i <= w; ++i)
        {
            for (int j = 1; j <= h; ++j)
            {
                int bx = ox + i * dx;
                int by = oy + j;
                int bz = oz + i * dz;
                ws.setBlock(bx, by, bz, Blocks.portal, axis, 2);
            }
        }
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        int targetCases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 350;
        long probeSeed = cmd.has("seed") ? cmd.get("seed").getAsLong() : 2L;
        Random rng = new Random(probeSeed ^ 0x9071a15L);

        Probe.rawChunks = true;

        WorldServer ow = server.worldServers[0];
        WorldServer nether = server.worldServers[1];
        WorldServer end = server.worldServers[2];

        // Preload working chunks in all 3 dimensions (raw region far from spawn)
        for (int cx = 110; cx <= 140; ++cx)
        {
            for (int cz = 110; cz <= 140; ++cz)
            {
                ow.getChunkFromChunkCoords(cx, cz);
            }
        }
        for (int cx = 10; cx <= 22; ++cx)
        {
            for (int cz = 10; cz <= 22; ++cz)
            {
                nether.getChunkFromChunkCoords(cx, cz);
            }
        }
        for (int cx = -2; cx <= 8; ++cx)
        {
            for (int cz = -2; cz <= 2; ++cz)
            {
                end.getChunkFromChunkCoords(cx, cz);
            }
        }

        resetTeleporter(ow.getDefaultTeleporter(), probeSeed);
        resetTeleporter(nether.getDefaultTeleporter(), probeSeed);
        resetTeleporter(end.getDefaultTeleporter(), probeSeed);

        EntityPlayerMP player = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        player.theItemInWorldManager.setGameType(WorldSettings.GameType.SURVIVAL);
        player.capabilities.isCreativeMode = false;
        player.capabilities.disableDamage = false;

        DataOutputStream dosCases = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin"))));
        DataOutputStream dosWrites = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin"))));
        DataOutputStream dosCache = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "cache.bin"))));

        final List<WriteRecord> recordedWrites = new ArrayList<WriteRecord>();
        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                recordedWrites.add(new WriteRecord((byte)w.provider.dimensionId, x, (short)y, z, (short)id, (byte)meta));
            }
        };

        int totalWrites = 0;
        int totalCacheEntries = 0;

        int numOwToNether = (targetCases * 140) / 350;
        int numNetherToOw = (targetCases * 140) / 350;
        int numOwToEnd = (targetCases * 35) / 350;
        int numEndToOw = targetCases - numOwToNether - numNetherToOw - numOwToEnd;

        int writtenCases = 0;

        for (int caseIdx = 0; caseIdx < targetCases; ++caseIdx)
        {
            int kind;
            if (caseIdx < numOwToNether) kind = 0; // OW -> Nether
            else if (caseIdx < numOwToNether + numNetherToOw) kind = 1; // Nether -> OW
            else if (caseIdx < numOwToNether + numNetherToOw + numOwToEnd) kind = 2; // OW -> End
            else kind = 3; // End -> OW

            int fromDim = 0;
            int toDim = -1;
            WorldServer fromWorld = ow;
            WorldServer toWorld = nether;

            if (kind == 0)
            {
                fromDim = 0;
                toDim = -1;
                fromWorld = ow;
                toWorld = nether;
            }
            else if (kind == 1)
            {
                fromDim = -1;
                toDim = 0;
                fromWorld = nether;
                toWorld = ow;
            }
            else if (kind == 2)
            {
                fromDim = 0;
                toDim = 1;
                fromWorld = ow;
                toWorld = end;
            }
            else
            {
                fromDim = 1;
                toDim = 0;
                fromWorld = end;
                toWorld = ow;
            }

            int mode = (caseIdx % 4 == 0) ? 1 : 0; // mix of creative (0 ticks) and survival (80 ticks)
            player.capabilities.isCreativeMode = (mode == 1);
            player.capabilities.disableDamage = (mode == 1);
            player.theItemInWorldManager.setGameType(mode == 1 ? WorldSettings.GameType.CREATIVE : WorldSettings.GameType.SURVIVAL);

            // Determine frame dimensions
            int frameW = 2 + (caseIdx % 20); // 2..21
            int frameH = 3 + ((caseIdx * 7) % 19); // 3..21
            int frameAxis = 1 + (caseIdx % 2); // 1 or 2
            int hasFrame = (kind == 0 || kind == 1) ? 1 : 0;

            int fx = 0, fy = 70, fz = 0;
            if (kind == 0)
            {
                int gridX = (caseIdx % 10) - 5;
                int gridZ = (caseIdx / 10) - 7;
                fx = 2000 + gridX * 36;
                fz = 2000 + gridZ * 36;
                if (caseIdx > 0 && caseIdx % 5 == 0)
                {
                    fx = 2000 + ((caseIdx - 1) % 10 - 5) * 36;
                    fz = 2000 + ((caseIdx - 1) / 10 - 7) * 36;
                }
            }
            else if (kind == 1)
            {
                int gridX = ((caseIdx - numOwToNether) % 7) - 3;
                int gridZ = ((caseIdx - numOwToNether) / 7) - 3;
                fx = 250 + gridX * 8;
                fz = 250 + gridZ * 8;
                if (caseIdx > numOwToNether && caseIdx % 4 == 0)
                {
                    fx = 250 + (((caseIdx - 1) - numOwToNether) % 7 - 3) * 8;
                    fz = 250 + (((caseIdx - 1) - numOwToNether) / 7 - 3) * 8;
                }
            }

            if (hasFrame == 1)
            {
                buildPortalFrame(fromWorld, fx, fy, fz, frameW, frameH, frameAxis);
            }
            else if (kind == 2)
            {
                fx = 2020; fy = 70; fz = 2020;
                fromWorld.setBlock(fx, fy, fz, Blocks.end_portal, 0, 2);
            }
            else if (kind == 3)
            {
                fx = 0; fy = 60; fz = 0;
                fromWorld.setBlock(fx, fy, fz, Blocks.end_portal, 0, 2);
            }

            // Entity starting pose
            double startX, startY, startZ;
            if (hasFrame == 1)
            {
                int dx = (frameAxis == 1) ? 1 : 0;
                int dz = (frameAxis == 1) ? 0 : 1;
                startX = fx + 1.0 * dx + 0.5;
                startY = fy + 1.0;
                startZ = fz + 1.0 * dz + 0.5;
            }
            else
            {
                startX = fx + 0.5;
                startY = fy + 0.5;
                startZ = fz + 0.5;
            }

            float startYaw = (float)(rng.nextDouble() * 360.0 - 180.0);
            float startPitch = (float)(rng.nextDouble() * 60.0 - 30.0);
            double startMotionX = (rng.nextDouble() - 0.5) * 0.2;
            double startMotionY = 0.0;
            double startMotionZ = (rng.nextDouble() - 0.5) * 0.2;

            if (player.dimension != fromDim)
            {
                WorldServer oldWorld = server.worldServerForDimension(player.dimension);
                oldWorld.removePlayerEntityDangerously(player);
                player.isDead = false;
                player.dimension = fromDim;
                player.setWorld(fromWorld);
                fromWorld.spawnEntityInWorld(player);
            }
            player.dimension = fromDim;
            player.setWorld(fromWorld);
            player.setPosition(startX, startY, startZ);
            player.prevPosX = startX - (rng.nextDouble() - 0.5) * 0.5;
            player.prevPosY = startY;
            player.prevPosZ = startZ - (rng.nextDouble() - 0.5) * 0.5;
            player.motionX = startMotionX;
            player.motionY = startMotionY;
            player.motionZ = startMotionZ;
            player.rotationYaw = startYaw;
            player.rotationPitch = startPitch;

            int initialCounter = (mode == 0) ? (rng.nextInt(20)) : 0;
            int initialTimeUntil = 0;
            setPortalCounter(player, initialCounter);
            player.timeUntilPortal = initialTimeUntil;

            Teleporter destTp = toWorld.getDefaultTeleporter();
            long destTpRandBefore = randState(getRand(destTp));
            int destTpCacheCountBefore = getKeys(destTp).size();

            recordedWrites.clear();
            int firstWriteIndex = totalWrites;
            int firstCacheIndex = totalCacheEntries;

            long worldTime = fromWorld.getTotalWorldTime();

            if (caseIdx > 0 && caseIdx % 20 == 0)
            {
                worldTime += 700L;
                ow.getWorldInfo().incrementTotalWorldTime(worldTime);
                destTp.removeStalePortalLocations(worldTime);
            }


            int preTicks = (mode == 1) ? 0 : (80 - initialCounter);
            if (preTicks < 0) preTicks = 0;

            int blockX = MathHelper.floor_double(startX);
            int blockY = MathHelper.floor_double(startY);
            int blockZ = MathHelper.floor_double(startZ);
            Block portalBlock = fromWorld.getBlock(blockX, blockY, blockZ);

            portalBlock.onEntityCollidedWithBlock(fromWorld, blockX, blockY, blockZ, player);
            int initialTeleportDir = player.getTeleportDirection();

            if (kind == 0 || kind == 1)
            {
                for (int t = 0; t <= preTicks; ++t)
                {
                    portalBlock.onEntityCollidedWithBlock(fromWorld, blockX, blockY, blockZ, player);
                    if (getPortalCounter(player) >= player.getMaxInPortalTime())
                    {
                        setPortalCounter(player, player.getMaxInPortalTime());
                        player.timeUntilPortal = player.getPortalCooldown();
                        player.travelToDimension(toDim);
                        break;
                    }
                    else
                    {
                        setPortalCounter(player, getPortalCounter(player) + 1);
                    }
                }
            }
            else if (kind == 2)
            {
                portalBlock.onEntityCollidedWithBlock(fromWorld, blockX, blockY, blockZ, player);
            }
            else
            {
                portalBlock.onEntityCollidedWithBlock(fromWorld, blockX, blockY, blockZ, player);
                if (player.playerConqueredTheEnd)
                {
                    player = server.getConfigurationManager().respawnPlayer(player, 0, true);
                }
            }


            double endX = player.posX;
            double endY = player.posY;
            double endZ = player.posZ;
            float endYaw = player.rotationYaw;
            float endPitch = player.rotationPitch;
            double endMotionX = player.motionX;
            double endMotionY = player.motionY;
            double endMotionZ = player.motionZ;
            int endPortalCounter = getPortalCounter(player);
            int endTimeUntilPortal = player.timeUntilPortal;

            long destTpRandAfter = randState(getRand(destTp));
            List destKeys = getKeys(destTp);
            LongHashMap destCache = getCache(destTp);
            int destTpCacheCountAfter = destKeys.size();

            int numWrites = recordedWrites.size();
            totalWrites += numWrites;

            byte portalCreated = (byte)((kind < 2 && numWrites > 0) ? 1 : 0);

            for (WriteRecord wr : recordedWrites)
            {
                dosWrites.writeByte(wr.dim);
                writeLe32(dosWrites, wr.x);
                writeLe16(dosWrites, wr.y);
                writeLe32(dosWrites, wr.z);
                writeLe16(dosWrites, wr.id);
                dosWrites.writeByte(wr.meta);
            }

            int numCacheThisCase = destKeys.size();
            totalCacheEntries += numCacheThisCase;
            for (int ki = 0; ki < destKeys.size(); ++ki)
            {
                long k = ((Long)destKeys.get(ki)).longValue();
                Teleporter.PortalPosition pp = (Teleporter.PortalPosition)destCache.getValueByKey(k);
                writeLe64(dosCache, k);
                writeLe32(dosCache, pp.posX);
                writeLe32(dosCache, pp.posY);
                writeLe32(dosCache, pp.posZ);
                writeLe64(dosCache, pp.lastUpdateTime);
            }

            writeLe32(dosCases, caseIdx);
            dosCases.writeByte(kind);
            dosCases.writeByte(mode);
            dosCases.writeByte(fromDim);
            dosCases.writeByte(toDim);
            writeLeDouble(dosCases, startX);
            writeLeDouble(dosCases, startY);
            writeLeDouble(dosCases, startZ);
            writeLeFloat(dosCases, startYaw);
            writeLeFloat(dosCases, startPitch);
            writeLeDouble(dosCases, startMotionX);
            writeLeDouble(dosCases, startMotionY);
            writeLeDouble(dosCases, startMotionZ);
            writeLe32(dosCases, initialCounter);
            writeLe32(dosCases, initialTimeUntil);
            writeLe32(dosCases, initialTeleportDir);
            dosCases.writeByte(hasFrame);
            dosCases.writeByte(frameW);
            dosCases.writeByte(frameH);
            dosCases.writeByte(frameAxis);
            writeLe32(dosCases, fx);
            writeLe32(dosCases, fy);
            writeLe32(dosCases, fz);
            writeLe64(dosCases, worldTime);
            writeLe64(dosCases, destTpRandBefore);
            writeLe32(dosCases, destTpCacheCountBefore);

            writeLeDouble(dosCases, endX);
            writeLeDouble(dosCases, endY);
            writeLeDouble(dosCases, endZ);
            writeLeFloat(dosCases, endYaw);
            writeLeFloat(dosCases, endPitch);
            writeLeDouble(dosCases, endMotionX);
            writeLeDouble(dosCases, endMotionY);
            writeLeDouble(dosCases, endMotionZ);
            writeLe32(dosCases, endPortalCounter);
            writeLe32(dosCases, endTimeUntilPortal);
            writeLe64(dosCases, destTpRandAfter);
            writeLe32(dosCases, destTpCacheCountAfter);
            dosCases.writeByte(portalCreated);
            writeLe32(dosCases, firstWriteIndex);
            writeLe32(dosCases, numWrites);
            writeLe32(dosCases, firstCacheIndex);
            writeLe32(dosCases, numCacheThisCase);

            writtenCases++;
        }

        dosCases.flush(); dosCases.close();
        dosWrites.flush(); dosWrites.close();
        dosCache.flush(); dosCache.close();
        Rows.writeListener = null;

        JsonObject manifest = new JsonObject();
        manifest.addProperty("kind", "netherite-portals");
        manifest.addProperty("seed", probeSeed);
        manifest.addProperty("cases", writtenCases);
        manifest.addProperty("writes", totalWrites);
        manifest.addProperty("cache_entries", totalCacheEntries);
        manifest.addProperty("case_bytes", 205);
        manifest.addProperty("write_bytes", 14);
        manifest.addProperty("cache_bytes", 28);

        PrintWriter pw = new PrintWriter(new FileWriter(new File(dir, "manifest.json")));
        pw.println(manifest.toString());
        pw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", writtenCases);
        res.addProperty("writes", totalWrites);
        res.addProperty("cache_entries", totalCacheEntries);
        return res;
    }
}
