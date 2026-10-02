package netherite.oracle;

import com.google.gson.JsonObject;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityClientPlayerMP;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.FoodStats;
import net.minecraft.world.WorldServer;

/**
 * One-run survival state setup, run with
 *
 *   {"cmd":"run","class":"SurvivalSetup","preset":"drown"}
 *
 * between tick pairs (the server is parked; like Snapshot it works on its own
 * thread so no CLIENT or SERVER RNG stream moves). A preset
 *
 *   - removes every non-player entity and turns mob spawning, rain and thunder
 *     off for the length of the run (no spawner draws, no weather block
 *     changes),
 *   - builds a stone arena over the spawn: a floor one block above the highest
 *     terrain in a 41x41 footprint, with everything above it cleared to air,
 *   - builds the preset's hazard (a water pit, a lava pool, a cactus, a fall
 *     pillar, a head-height slab, a bed) and pins the player's survival state
 *     (food, saturation, exhaustion, health, air, fire, inventory, XP,
 *     position) on both the server player and the client player, so a Snapshot
 *     taken right after carries the whole state.
 *
 * The result reports the spawn cell, the floor level, and every hazard cell it
 * placed, so the tape scripts know where to walk.
 */
final class SurvivalSetup
{
    static final boolean MUTATES = true;
    private SurvivalSetup() {}

    static JsonObject run(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        if (!Oracle.dev || Oracle.mode != Oracle.AGENT) throw new IllegalStateException("SurvivalSetup requires --dev agent mode");
        final JsonObject[] result = new JsonObject[1];
        final Exception[] error = new Exception[1];
        Thread t = new Thread(new Runnable()
        {
            public void run()
            {
                try
                {
                    result[0] = setup(server, cmd);
                }
                catch (Exception e)
                {
                    error[0] = e;
                }
            }
        }, "Oracle SurvivalSetup");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    // ------------------------------------------------------------------ state

    static WorldServer ws;
    static EntityPlayerMP sp;
    static EntityClientPlayerMP cp;
    static int sx, sz, gy;
    static JsonObject out;

    static JsonObject setup(IntegratedServer server, JsonObject cmd) throws Exception
    {
        Minecraft mc = Oracle.mc;
        if (mc == null || mc.thePlayer == null) throw new IllegalStateException("SurvivalSetup: the join has not happened yet");
        ws = server.worldServers[0];
        sp = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        cp = mc.thePlayer;
        out = new JsonObject();

        ChunkCoordinates spawn = ws.getSpawnPoint();
        sx = spawn.posX;
        sz = spawn.posZ;

        clearWorld();
        clearClientWorld();
        ws.getGameRules().setOrCreateGameRule("doMobSpawning", "false");
        ws.getWorldInfo().setRaining(false);
        ws.getWorldInfo().setThundering(false);
        ws.getWorldInfo().setRainTime(1000000);
        ws.getWorldInfo().setThunderTime(1000000);

        String preset = cmd.has("preset") ? cmd.get("preset").getAsString() : "";

        // the client world must carry the spawn chunks already: the arena's
        // blocks are mirrored into it directly, because the S22 packets the
        // server sends only land on later ticks and the player would fall
        // through the not-yet-there floor
        for (int[] c : new int[][] {{sx, sz}, {sx - 20, sz - 20}, {sx + 20, sz - 20}, {sx - 20, sz + 20}, {sx + 20, sz + 20}})
        {
            if (!mc.theWorld.blockExists(c[0], 0, c[1]))
                throw new IllegalStateException("SurvivalSetup: the client world has no chunk at " + c[0] + "," + c[1]
                    + " - run a few ticks before the setup");
        }

        buildArena("death".equals(preset));
        out.addProperty("preset", preset);
        out.addProperty("spawnX", sx);
        out.addProperty("spawnZ", sz);
        out.addProperty("gy", gy);

        if ("food".equals(preset))
        {
            placePlayers(sx + 0.5, gy + 1, sz + 0.5);
            state(20, 0.0F, 0.0F, 0, 19.0F, 300, 0);
        }
        else if ("starve".equals(preset))
        {
            placePlayers(sx + 0.5, gy + 1, sz + 0.5);
            state(1, 0.0F, 0.0F, 0, 3.0F, 300, 0);
        }
        else if ("fall20".equals(preset))
        {
            pillar(6, 6, 20);
            state(20, 5.0F, 0.0F, 0, 20.0F, 300, 0);
            placePlayers(sx + 6 + 0.5, gy + 20 + 1, sz + 6 + 0.5);
        }
        else if ("fall12".equals(preset))
        {
            pillar(6, 6, 12);
            state(20, 5.0F, 0.0F, 0, 20.0F, 300, 0);
            placePlayers(sx + 6 + 0.5, gy + 12 + 1, sz + 6 + 0.5);
        }
        else if ("drown".equals(preset))
        {
            waterPit(4, 4);
            state(20, 5.0F, 0.0F, 0, 20.0F, 60, 0);
            placePlayers(sx + 0.5, gy + 1, sz + 0.5);
            bed(-8, -8);
        }
        else if ("lava".equals(preset))
        {
            lavaPit(4, 4);
            state(20, 5.0F, 0.0F, 0, 20.0F, 300, 0);
            placePlayers(sx + 0.5, gy + 1, sz + 0.5);
            bed(-8, -8);
        }
        else if ("cactus".equals(preset))
        {
            cactus(-5, -5);
            state(20, 5.0F, 0.0F, 0, 20.0F, 300, 0);
            placePlayers(sx + 0.5, gy + 1, sz + 0.5);
        }
        else if ("suffocate".equals(preset))
        {
            slab(0, 0);
            state(20, 5.0F, 0.0F, 0, 20.0F, 300, 0);
            placePlayers(sx + 0.5, gy + 1, sz + 0.5);
            bed(-8, -8);
        }
        else if ("eat".equals(preset))
        {
            give(Items.bread, 2, 0, 0);
            state(10, 0.0F, 0.0F, 0, 19.0F, 300, 0);
            placePlayers(sx + 0.5, gy + 1, sz + 0.5);
        }
        else if ("drop".equals(preset))
        {
            give(Item.getItemFromBlock(Blocks.dirt), 5, 0, 0);
            give(Items.bread, 1, 0, 1);
            state(10, 0.0F, 0.0F, 0, 19.0F, 300, 0);
            placePlayers(sx + 0.5, gy + 1, sz + 0.5);
        }
        else if ("death".equals(preset))
        {
            pillar(6, 6, 24);
            bed(-8, -8);
            if (!cmd.has("bare") || !cmd.get("bare").getAsBoolean())
            {
                give(Item.getItemFromBlock(Blocks.dirt), 5, 0, 0);
                sp.addExperience(200);
            }
            state(20, 5.0F, 0.0F, 0, 20.0F, 300, 0);
            placePlayers(sx + 6 + 0.5, gy + 24 + 1, sz + 6 + 0.5);
        }
        else
        {
            throw new IllegalArgumentException("SurvivalSetup: unknown preset " + preset);
        }

        return out;
    }

    // ------------------------------------------------------------------ pieces

    /** Every non-player entity out of the world. removeEntity only marks
     * dead; the purge updateEntities would run later has not happened yet
     * when the snapshot is taken, so do it here the same way updateEntities
     * does: into unloadedEntityList, then its remove step. */
    static void clearWorld() throws Exception
    {
        List<Entity> dead = new ArrayList<Entity>();

        for (Object o : ws.loadedEntityList)
        {
            Entity e = (Entity)o;

            if (e instanceof EntityPlayer) continue;
            e.setDead();
            dead.add(e);
        }

        List unloaded = (List)field(ws, "unloadedEntityList").get(ws);
        unloaded.addAll(dead);
    }

    /** The client world's mob copies out too: their moveEntity calls would
     * land in a trace file and the native replay simulates no client mobs.
     * WorldClient.removeEntity takes the entity out of its entityList at
     * once, so no purge tick is needed. */
    static void clearClientWorld() throws Exception
    {
        net.minecraft.client.multiplayer.WorldClient cw = Oracle.mc.theWorld;
        Set all = new HashSet((Set)field(cw, "entityList").get(cw));

        for (Object o : all)
        {
            Entity e = (Entity)o;

            if (e instanceof net.minecraft.client.entity.EntityClientPlayerMP) continue;
            cw.removeEntity(e);
        }

        // the re-entry queue would spawn them right back
        ((java.util.Collection)field(cw, "entitySpawnQueue").get(cw)).clear();
    }

    /** The stone floor: one block above the highest terrain in the footprint,
     * with everything above it over the footprint turned to air. */
    static void buildArena(boolean tallClear)
    {
        int half = 20;
        int top = 0;

        for (int dx = -half; dx <= half; ++dx)
        {
            for (int dz = -half; dz <= half; ++dz)
            {
                int t = ws.getTopSolidOrLiquidBlock(sx + dx, sz + dz);

                if (t > top) top = t;
            }
        }

        gy = top + 1;

        for (int dx = -half; dx <= half; ++dx)
        {
            for (int dz = -half; dz <= half; ++dz)
            {
                set(sx + dx, gy, sz + dz, Blocks.stone, 0);

                for (int y = gy + 1; y <= gy + (tallClear ? 64 : 10); ++y)
                {
                    if (ws.getBlock(sx + dx, y, sz + dz).getMaterial() != Material.air)
                    {
                        set(sx + dx, y, sz + dz, Blocks.air, 0);
                    }
                }
            }
        }

        // a two-high rim on the perimeter: no scripted walk leaves the floor
        for (int i = -half; i <= half; ++i)
        {
            set(sx + i, gy + 1, sz - half, Blocks.stone, 0);
            set(sx + i, gy + 2, sz - half, Blocks.stone, 0);
            set(sx + i, gy + 1, sz + half, Blocks.stone, 0);
            set(sx + i, gy + 2, sz + half, Blocks.stone, 0);
            set(sx - half, gy + 1, sz + i, Blocks.stone, 0);
            set(sx - half, gy + 2, sz + i, Blocks.stone, 0);
            set(sx + half, gy + 1, sz + i, Blocks.stone, 0);
            set(sx + half, gy + 2, sz + i, Blocks.stone, 0);
        }
    }

    static void set(int x, int y, int z, Block b, int meta)
    {
        ws.setBlock(x, y, z, b, meta, 3);
        Oracle.mc.theWorld.setBlock(x, y, z, b, meta, 3);
    }

    /** The two players at (x, y, z): position only, rotation comes from the
     * tape's first look. */
    static void placePlayers(double x, double y, double z)
    {
        // the client player's yOffset is 1.62: its box hangs that far below
        // posY, so the same feet position needs a higher posY on the client
        sp.setPosition(x, y, z);
        cp.setPosition(x, y + 1.6200000047683716, z);
    }

    /** Food, saturation, exhaustion, the food timer, health, air and fire,
     * on both players. */
    static void state(int food, float sat, float exh, int timer, float hp, int air, int fire) throws Exception
    {
        apply(sp, food, sat, exh, timer, hp, air, fire);
        apply(cp, food, sat, exh, timer, hp, air, fire);
    }

    static void apply(Object player, int food, float sat, float exh, int timer, float hp, int air, int fire) throws Exception
    {
        FoodStats fs = player instanceof EntityPlayerMP
            ? ((EntityPlayerMP)player).getFoodStats()
            : ((EntityClientPlayerMP)player).getFoodStats();

        field(fs, "foodLevel").setInt(fs, food);
        field(fs, "foodSaturationLevel").setFloat(fs, sat);
        field(fs, "foodExhaustionLevel").setFloat(fs, exh);
        field(fs, "foodTimer").setInt(fs, timer);

        if (player instanceof EntityPlayerMP) ((EntityPlayerMP)player).setHealth(hp);
        else ((EntityClientPlayerMP)player).setHealth(hp);

        ((Entity)player).setAir(air);
        field(player, "fire").setInt(player, fire);
    }

    static Field field(Object target, String name) throws Exception
    {
        for (Class<?> k = target.getClass(); k != null; k = k.getSuperclass())
        {
            try
            {
                Field f = k.getDeclaredField(name);
                f.setAccessible(true);
                return f;
            }
            catch (NoSuchFieldException e)
            {
                // keep walking up
            }
        }

        throw new NoSuchFieldException(name);
    }

    /** One stack into both players' slot i. */
    static void give(Item item, int count, int damage, int slot)
    {
        sp.inventory.mainInventory[slot] = new ItemStack(item, count, damage);
        cp.inventory.mainInventory[slot] = new ItemStack(item, count, damage);
    }

    /** A 1x1 pillar of stone with its top at gy + h, at (sx+dx, sz+dz). */
    static void pillar(int dx, int dz, int h)
    {
        int px = sx + dx, pz = sz + dz;

        for (int y = gy - 2; y <= gy + h; ++y) set(px, y, pz, Blocks.stone, 0);

        out.addProperty("pillarX", px + 0.5);
        out.addProperty("pillarZ", pz + 0.5);
        out.addProperty("pillarY", gy + h + 1);
    }

    /** A 2x2 water shaft, 5 deep: stone shaft and floor, water in the four
     * cells above the floor, open at arena level. */
    static void waterPit(int dx, int dz)
    {
        int px = sx + dx, pz = sz + dz;

        for (int y = gy - 5; y <= gy; ++y)
        {
            for (int x = -1; x <= 2; ++x)
            {
                for (int z = -1; z <= 2; ++z)
                {
                    set(px + x, y, pz + z, Blocks.stone, 0);
                }
            }
        }

        for (int y = gy - 4; y <= gy - 1; ++y)
        {
            for (int x = 0; x <= 1; ++x)
            {
                for (int z = 0; z <= 1; ++z)
                {
                    set(px + x, y, pz + z, Blocks.water, 0);
                }
            }
        }

        for (int x = 0; x <= 1; ++x)
        {
            for (int z = 0; z <= 1; ++z)
            {
                set(px + x, gy, pz + z, Blocks.air, 0);
            }
        }

        out.addProperty("pitX", px);
        out.addProperty("pitZ", pz);
    }

    /** A 2x2 lava pool one deep in the arena floor, stone ring around it. */
    static void lavaPit(int dx, int dz)
    {
        int px = sx + dx, pz = sz + dz;

        for (int y = gy - 2; y <= gy; ++y)
        {
            for (int x = -1; x <= 2; ++x)
            {
                for (int z = -1; z <= 2; ++z)
                {
                    set(px + x, y, pz + z, Blocks.stone, 0);
                }
            }
        }

        for (int x = 0; x <= 1; ++x)
        {
            for (int z = 0; z <= 1; ++z)
            {
                set(px + x, gy - 1, pz + z, Blocks.lava, 0);
                set(px + x, gy, pz + z, Blocks.air, 0);
            }
        }

        out.addProperty("lavaX", px);
        out.addProperty("lavaZ", pz);
    }

    /** A two-high cactus on sand in a 3x3 recess whose floor is one below the
     * arena floor, so the cactus's horizontal neighbours are air. */
    static void cactus(int dx, int dz)
    {
        int px = sx + dx, pz = sz + dz;

        for (int x = -1; x <= 1; ++x)
        {
            for (int z = -1; z <= 1; ++z)
            {
                set(px + x, gy, pz + z, Blocks.air, 0);
                set(px + x, gy - 1, pz + z, x == 0 && z == 0 ? Blocks.sand : Blocks.stone, 0);
            }
        }

        set(px, gy, pz, Blocks.cactus, 0);
        set(px, gy + 1, pz, Blocks.cactus, 0);
        out.addProperty("cactusX", px);
        out.addProperty("cactusZ", pz);
    }

    /** A three-by-three stone roof at head height over (sx+dx, sz+dz): the
     * player suffocates under it, and the anti-stuck push-out (whose sample
     * points reach one block less far than the suffocation's eye samples)
     * cannot walk it out of the roof's shadow. */
    static void slab(int dx, int dz)
    {
        for (int x = -1; x <= 1; ++x)
        {
            for (int z = -1; z <= 1; ++z)
            {
                set(sx + dx + x, gy + 2, sz + dz + z, Blocks.stone, 0);
                set(sx + dx + x, gy + 3, sz + dz + z, Blocks.stone, 0);
            }
        }

        out.addProperty("slabX", sx + dx);
        out.addProperty("slabZ", sz + dz);
    }

    /** A bed at (sx+dx, gy, sz+dz) facing south, foot first, in a 3x3 pocket
     * of the arena floor: the respawn scan needs cells at the bed's level with
     * solid ground below and air at and above them, so the floor around the
     * bed is cut away and re-laid one block down. */
    static void bed(int dx, int dz)
    {
        int px = sx + dx, pz = sz + dz;

        for (int x = -1; x <= 1; ++x)
        {
            for (int z = -1; z <= 1; ++z)
            {
                set(px + x, gy, pz + z, Blocks.air, 0);
                set(px + x, gy - 1, pz + z, Blocks.stone, 0);
            }
        }

        set(px, gy, pz, Blocks.bed, 0);
        set(px, gy, pz + 1, Blocks.bed, 8);
        sp.setSpawnChunk(new ChunkCoordinates(px, gy, pz), true);
        out.addProperty("bedX", px);
        out.addProperty("bedZ", pz);
    }
}
