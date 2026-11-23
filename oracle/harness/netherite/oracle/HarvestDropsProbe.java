package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.enchantment.EnchantmentHelper;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTTagByte;
import net.minecraft.nbt.NBTTagByteArray;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagDouble;
import net.minecraft.nbt.NBTTagFloat;
import net.minecraft.nbt.NBTTagInt;
import net.minecraft.nbt.NBTTagIntArray;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.nbt.NBTTagLong;
import net.minecraft.nbt.NBTTagShort;
import net.minecraft.nbt.NBTTagString;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatList;
import net.minecraft.stats.StatFileWriter;
import net.minecraft.util.FoodStats;
import net.minecraft.world.IWorldAccess;
import net.minecraft.world.WorldServer;

/**
 * Reference for the native port of the player-dependent half of block
 * harvesting: Block.harvestBlock and its overrides (BlockDeadBush,
 * BlockDoublePlant, BlockIce, BlockLeaves, BlockSnow, BlockTallGrass,
 * BlockVine), Block.canSilkHarvest / createStackedBlock and their overrides,
 * EnchantmentHelper.getSilkTouchModifier / getFortuneModifier, the
 * onBlockHarvested overrides, and ItemInWorldManager.tryHarvestBlock's order
 * (removeBlock's onBlockHarvested + setBlockToAir + onBlockDestroyedByPlayer,
 * canHarvestBlock, the held item's onBlockDestroyed and its damage).
 *
 * One case: the probe clears a 3x3x3 cell around a raw block position far from
 * spawn, sets a stone floor at (0,-1,0) (and, for the double plant, the other
 * half of the pair so its onBlockHarvested branch can fire), places the block
 * with its meta, seeds the world Random and the Math.random stream, and calls
 * ItemInWorldManager.tryHarvestBlock as a survival player holding one stack.
 * Everything the break spawned, the blocks it left, the held stack's damage,
 * the player's exhaustion and the three stat counters it moved are recorded.
 *
 * The context is three wide because every harvest override reads at most one
 * cell away (BlockIce the cell below, BlockDoublePlant the cell above or below,
 * BlockLeaves/BlockLog the leaves in a 1-wide cube whose other cells are air).
 *
 * Output DIR/manifest.json and DIR/lines.jsonl, one JSON object per case.
 */
public final class HarvestDropsProbe
{
    /** The block the probe breaks, and the cell below it (the floor). */
    static final int CX = 2002, CY = 100, CZ = 2002;

    /** Metas each block is asked about. */
    static final int METAS = 16;

    /**
     * The half-width, in chunks, of the region the probe loads before the sweep
     * and snapshots around it. Three chunks, because BlockTripWireHook's
     * breakBlock walks up to 41 blocks along its facing (and BlockTripWire up to
     * 41 in two directions): with only the block's own chunk loaded those reads
     * reach an unloaded chunk, and generating it draws from the world Random
     * mid-case, which no case record can carry.
     */
    static final int RADIUS = 3;

    /**
     * Blocks the place-and-harvest cycle cannot reproduce: piston_head and
     * piston_extension need a TileEntityPiston, monster_egg spawns an
     * EntitySilverfish from onBlockDestroyedByPlayer, and breaking tnt primes an
     * EntityTNTPrimed whose constructor belongs to the entity subsystem (the
     * probe records items and experience orbs, not mobs). (drops.c excludes the
     * first two and monster_egg for the same reasons.)
     */
    static final int[] EXTERIOR_MUTATOR = {34, 36, 46, 97};

    /**
     * The held stacks, one per sweep slot: item id (0 = empty), silk touch
     * level, fortune level and whether the stack starts one hit from breaking.
     * Item ids are resolved from the registry in heldItems(), not written here.
     */
    static final String[][] HELD = {
        {"", "0", "0", "0"},                   /* the empty hand */
        {"shears", "0", "0", "0"},
        {"shears", "1", "0", "0"},
        {"diamond_pickaxe", "0", "0", "0"},
        {"diamond_pickaxe", "1", "0", "0"},
        {"diamond_pickaxe", "0", "1", "0"},
        {"diamond_pickaxe", "0", "2", "0"},
        {"diamond_pickaxe", "0", "3", "0"},
        {"diamond_axe", "0", "0", "0"},
        {"diamond_axe", "1", "0", "0"},
        {"diamond_shovel", "0", "0", "0"},
        {"diamond_shovel", "1", "0", "0"},
        {"wooden_pickaxe", "0", "0", "0"},
        {"iron_pickaxe", "1", "0", "0"},
        {"diamond_sword", "0", "0", "0"},
        {"shears", "0", "0", "1"},             /* one hit from breaking */
        {"diamond_pickaxe", "1", "3", "0"},    /* silk beats fortune */
        {"diamond_pickaxe", "0", "0", "1"}     /* one hit from breaking */
    };

    /** One entity's position, motion and yaw as raw bits. */
    static void doubles(JsonObject o, Entity e)
    {
        o.addProperty("x", Double.doubleToRawLongBits(e.posX));
        o.addProperty("y", Double.doubleToRawLongBits(e.posY));
        o.addProperty("z", Double.doubleToRawLongBits(e.posZ));
        o.addProperty("mx", Double.doubleToRawLongBits(e.motionX));
        o.addProperty("my", Double.doubleToRawLongBits(e.motionY));
        o.addProperty("mz", Double.doubleToRawLongBits(e.motionZ));
        o.addProperty("yaw", Float.floatToRawIntBits(e.rotationYaw));
    }

    private HarvestDropsProbe() {}

    static long[] opseeds(JsonObject cmd)
    {
        long s = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 3L;

        if (cmd.has("seedcount"))
        {
            int n = cmd.get("seedcount").getAsInt();
            long[] a = new long[n];

            for (int i = 0; i < n; ++i) a[i] = s + i;

            return a;
        }

        // A spread, not a twin: the leaves' sapling roll is 1-in-20 and the
        // apple roll 1-in-200, so one seed would leave the interesting
        // branches unexercised. 8 consecutive seeds.
        long[] a = new long[8];

        for (int i = 0; i < a.length; ++i) a[i] = s + i;

        return a;
    }

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
                    e.printStackTrace();
                    error[0] = e;
                }
            }
        }, "Oracle HarvestDrops");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, final JsonObject cmd) throws Exception
    {
        final File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        final WorldServer ws = server.worldServers[0];
        final int x = cmd.has("x") ? cmd.get("x").getAsInt() : CX;
        final int y = cmd.has("y") ? cmd.get("y").getAsInt() : CY;
        final int z = cmd.has("z") ? cmd.get("z").getAsInt() : CZ;
        caseIndex = 0;
        final int lo = cmd.has("lo") ? cmd.get("lo").getAsInt() : 0;
        final int hi = cmd.has("hi") ? cmd.get("hi").getAsInt() : 4095;
        final int heldMax = cmd.has("heldmax") ? cmd.get("heldmax").getAsInt() : HELD.length;
        final long[] opseeds = opseeds(cmd);
        Probe.rawChunks = true;

        final EntityPlayerMP p = (EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        p.theItemInWorldManager.setGameType(net.minecraft.world.WorldSettings.GameType.SURVIVAL);

        for (int cx = (x >> 4) - RADIUS; cx <= (x >> 4) + RADIUS; ++cx)
        {
            for (int cz = (z >> 4) - RADIUS; cz <= (z >> 4) + RADIUS; ++cz)
            {
                if (ws.getChunkFromChunkCoords(cx, cz) == null) throw new IllegalStateException("chunk (" + cx + "," + cz + ") did not load");
            }
        }

        final PrintWriter lw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "lines.jsonl")), "UTF-8"));

        Counted own = new Counted(MathStream.MATH_SEED);
        MATH = own;
        MathStream math = new MathStream(own);
        math.install();

        int cases = 0, skipped = 0;

        int[] before = snapshot(ws, x >> 4, z >> 4);

        try
        {
            for (int id : ids())
            {
                if (isExterior(id)) continue;
                if (id < lo || id > hi) continue;

                Block b = Block.getBlockById(id);
                int[] heldIds = heldIds();

                for (int meta = 0; meta < METAS; ++meta)
                {
                    for (int hc = 0; hc < heldMax; ++hc)
                    {
                        for (long opseed : opseeds)
                        {
                            if (one(ws, p, x, y, z, id, meta, hc, heldIds[hc], opseed, lw)) ++cases;
                            else ++skipped;
                        }
                    }
                }
            }
        }
        finally
        {
            math.uninstall(own);
        }

        lw.close();

        int[] after = snapshot(ws, x >> 4, z >> 4);

        if (!java.util.Arrays.equals(before, after))
        {
            throw new IllegalStateException("the sweep changed the region: " + where(before, after));
        }

        JsonArray seeds = new JsonArray();

        for (long s : opseeds) seeds.add(new JsonPrimitive(s));

        JsonObject m = new JsonObject();
        m.addProperty("seed", ws.getSeed());
        m.addProperty("kind", "harvestdrops");
        m.addProperty("x", x);
        m.addProperty("y", y);
        m.addProperty("z", z);
        m.addProperty("metas", METAS);
        m.addProperty("held", HELD.length);
        m.add("opseeds", seeds);
        m.addProperty("cases", cases);
        m.addProperty("cases_skipped", skipped);
        m.addProperty("metas_probed", "every registered block id except the exterior mutators is placed at metas 0..15 with World.setBlock. Eighteen blocks normalise their own metadata on placement (BlockTorch and BlockRedstoneTorch fall back to a valid orientation, BlockRailBase normalises a rail's shape, the pistons and the containers rewrite their facing), so a case where the world did not keep the exact block and meta it was given is skipped and has no record (cases_skipped): 209 of the 2672 (block, meta) pairs. The break would otherwise run on a state the request did not name.");
        m.addProperty("math_seed", MathStream.MATH_SEED);
        m.addProperty("blocks", "every registered block id except the exterior mutators below, at metas 0..15; see metas_probed for the cases the world refuses");
        m.addProperty("exterior_mutator", join(EXTERIOR_MUTATOR));
        m.addProperty("exterior_mutator_reason", "not a function of the block, the meta, the held stack and the two Random streams: piston_head and piston_extension need the TileEntityPiston a place-and-harvest cycle does not make, monster_egg spawns an EntitySilverfish from onBlockDestroyedByPlayer and never drops an item, and breaking tnt primes an EntityTNTPrimed whose constructor is the entity subsystem, not this path");
        m.addProperty("held_legend", "held: [<item name or empty>, <silk touch level>, <fortune level>, <starts one hit from breaking>]");
        JsonArray held = new JsonArray();

        for (String[] h : HELD)
        {
            JsonArray e = new JsonArray();
            for (String s : h) e.add(new JsonPrimitive(s));
            held.add(e);
        }

        m.add("held_table", held);
        m.addProperty("context", "the 3x3x3 cells around the block are air except a stone floor at (0,-1,0); for the double plant (block 175) the other half of the pair is placed too, upper at (0,+1,0) when the tested meta is the lower half and lower at (0,-1,0) (over the floor) when it is the upper half. cells records every non-air cell before the break plus the block's own cell even when it is air, left every such cell after; dx/dy/dz are relative to the block.");
        m.addProperty("region", "the chunks in a " + RADIUS + "-chunk radius around the block are loaded before the sweep and compared after it, so no case can generate a chunk: BlockTripWireHook.breakBlock walks up to 41 blocks along its facing and would otherwise reach an unloaded one");
        m.addProperty("case_order", "block id ascending, then meta 0..15, then held 0.." + (HELD.length - 1) + ", then opseed in the manifest's order");
        m.addProperty("layout", "lines.jsonl, one JSON object per case: case, block, meta, held, held_item, silk, fortune, damage0, count0, opseed, can_harvest, cells [[dx,dy,dz,id,meta],...], left [[dx,dy,dz,id,meta],...], held_after [item,damage,count] or null, exhaust (float bits), stat_mine/stat_use/stat_break (1 when that stat moved), world_draws, spawn_draws, ents [{kind,id,damage,count,nbt,xp,x,y,z,mx,my,mz,yaw}]");
        m.addProperty("world_draws", "world Random values the case spent, from the Random seeded to opseed: the placement and the clearing of the previous case are rewound, so this is only the break's own draws (dropBlockAsItemWithChance, the EntityItem offsets, BlockIce's water set, BlockDoublePlant's onBlockHarvested drop)");
        m.addProperty("spawn_draws", "Math.random next(bits) calls the case's entity constructors consumed, from math[OTHER] which the probe replaced with a java.util.Random seeded to math_seed: 8 per entity (four nextDouble)");
        m.addProperty("stats", "stat_mine is StatList.mineBlockStatArray[block] (Block.harvestBlock and the shears overrides and BlockDoublePlant.func_149886_b), stat_use StatList.objectUseStats[held item] (ItemStack.func_150999_a when onBlockDestroyed returned true), stat_break StatList.objectBreakStats[held item] (damageItem breaking the stack). All are read through the player's StatisticsFile before and after the case.");
        m.addProperty("exhaustion", "the player's FoodStats exhaustion is reset to 0.0F from NBT before every case and recorded after, as raw float bits; addExhaustion caps at 40.0F");
        m.addProperty("player", "the server's first player, survival (creativeMode false), main inventory slot 0 holds the case's stack and the slot is cleared after the case");
        m.addProperty("doTileDrops", "the game rule stays at its default (on): every dropBlockAsItem_do spawns its EntityItem");
        m.addProperty("nbt", "canonical NBT of ItemStack.writeToNBT of the dropped stack, the text nbtjson.c parses");
        m.addProperty("draw_timing", "the placement and the clear of the previous case run with the Math.random stream saved and rewound and with the world Random re-seeded to opseed after them, so a case's draws are exactly its own");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", cases);
        return res;
    }

    /**
     * One case: prepare the cell, place the block, break it, record, clear. The
     * record is written to lw, and false is returned when the world refused the
     * placement (the block did not stick) so the case is not counted.
     */
    static boolean one(WorldServer ws, EntityPlayerMP p, int x, int y, int z, int id, int meta,
                       int hc, int heldId, long opseed, PrintWriter lw) throws Exception
    {
        Block b = Block.getBlockById(id);
        Capture cap = new Capture();
        long mathBefore = Det.state(MATH);

        ws.addWorldAccess(cap);

        try
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dz = -1; dz <= 1; ++dz)
                    {
                        if (ws.getBlock(x + dx, y + dy, z + dz) != Blocks.air)
                        {
                            ws.setBlock(x + dx, y + dy, z + dz, Blocks.air, 0, 2);
                        }
                    }
                }
            }

            ws.setBlock(x, y - 1, z, Blocks.stone, 0, 2);

            if (id == 175) // BlockDoublePlant: the pair, so onBlockHarvested can fire
            {
                if ((meta & 8) != 0)
                {
                    ws.setBlock(x, y - 1, z, b, meta & 7, 2);
                }
                else
                {
                    ws.setBlock(x, y + 1, z, b, meta | 8, 2);
                }
            }

            ws.setBlock(x, y, z, b, meta, 2);
        }
        finally
        {
            ws.removeWorldAccess(cap);
        }

        for (Entity e : cap.spawned) takeOut(ws, e);
        cap.spawned.clear();
        rewind(MATH, mathBefore);

        if (ws.getBlock(x, y, z) != b || ws.getBlockMetadata(x, y, z) != meta)
        {
            return false; // the world would not hold this block at this meta
        }

        String[] h = HELD[hc];
        int silk = Integer.parseInt(h[1]);
        int fortune = Integer.parseInt(h[2]);
        boolean worn = "1".equals(h[3]);
        ItemStack held = heldStack(heldId, silk, fortune, worn);
        int damage0 = held == null ? 0 : held.getItemDamage();
        int count0 = held == null ? 0 : held.stackSize;

        for (int i = 0; i < 9; ++i) p.inventory.mainInventory[i] = null;
        p.inventory.mainInventory[0] = held;
        p.inventory.currentItem = 0;
        ITEM_STACK.set(p.inventory, held);

        FoodStats fs = p.getFoodStats();
        NBTTagCompound food = new NBTTagCompound();
        food.setInteger("foodLevel", 20);
        food.setInteger("foodTickTimer", 0);
        food.setFloat("foodSaturationLevel", 5.0F);
        food.setFloat("foodExhaustionLevel", 0.0F);
        fs.readNBT(food);

        StatFileWriter stats = (StatFileWriter)STATS.get(p);
        StatBase statMine = StatList.mineBlockStatArray[id];
        StatBase statUse = heldId == 0 ? null : StatList.objectUseStats[heldId];
        StatBase statBreak = heldId == 0 ? null : StatList.objectBreakStats[heldId];
        int sm0 = statCount(stats, statMine), su0 = statCount(stats, statUse), sb0 = statCount(stats, statBreak);

        p.setPosition(x + 0.5D, y + 1.0D, z + 0.5D);
        boolean canHarvest = p.canHarvestBlock(b);

        List<int[]> cells = grid(ws, x, y, z);

        Random savedRand = ws.rand;
        ws.rand.setSeed(opseed);
        CountedRandom world = new CountedRandom(ws.rand);
        ws.rand = world;
        cap.spawned.clear();
        long drawBefore = MATH.draws;

        ws.addWorldAccess(cap);

        try
        {
            p.theItemInWorldManager.tryHarvestBlock(x, y, z);
        }
        finally
        {
            ws.removeWorldAccess(cap);
            ws.rand = savedRand;
        }

        int spawnDraws = (int)(MATH.draws - drawBefore);
        int worldDraws = world.draws;

        int sm1 = statCount(stats, statMine), su1 = statCount(stats, statUse), sb1 = statCount(stats, statBreak);
        NBTTagCompound after = new NBTTagCompound();
        fs.writeNBT(after);
        List<int[]> left = grid(ws, x, y, z);

        ItemStack heldAfter = p.inventory.mainInventory[0];

        JsonObject l = new JsonObject();
        l.addProperty("case", caseIndex);
        l.addProperty("block", id);
        l.addProperty("meta", meta);
        l.addProperty("held", hc);
        l.addProperty("held_item", heldId);
        l.addProperty("silk", silk);
        l.addProperty("fortune", fortune);
        l.addProperty("damage0", damage0);
        l.addProperty("count0", count0);
        l.addProperty("opseed", opseed);
        l.addProperty("can_harvest", canHarvest ? 1 : 0);
        l.add("cells", cellsJson(cells));
        l.add("left", cellsJson(left));

        if (heldAfter == null)
        {
            l.add("held_after", com.google.gson.JsonNull.INSTANCE);
        }
        else
        {
            JsonArray ha = new JsonArray();
            ha.add(new JsonPrimitive(Item.getIdFromItem(heldAfter.getItem())));
            ha.add(new JsonPrimitive(heldAfter.getItemDamage()));
            ha.add(new JsonPrimitive(heldAfter.stackSize));
            l.add("held_after", ha);
        }

        l.addProperty("exhaust", Float.floatToRawIntBits(after.getFloat("foodExhaustionLevel")));
        l.addProperty("stat_mine", sm1 - sm0);
        l.addProperty("stat_use", su1 - su0);
        l.addProperty("stat_break", sb1 - sb0);
        l.addProperty("world_draws", worldDraws);
        l.addProperty("spawn_draws", spawnDraws);

        JsonArray ents = new JsonArray();

        for (Entity e : cap.spawned)
        {
            JsonObject o = new JsonObject();

            if (e instanceof EntityItem)
            {
                EntityItem ei = (EntityItem)e;
                ItemStack st = ei.getEntityItem();
                Item it = st.getItem();
                int sid = it == null ? 0xffff : Item.getIdFromItem(it);

                if (it == null || Item.getItemById(sid) == null) sid = it == null ? 0xffff : 0xfffe;

                o.addProperty("kind", "item");
                o.addProperty("id", sid);
                o.addProperty("damage", st.getItemDamage());
                o.addProperty("count", st.stackSize);
                o.add("nbt", canon(st.writeToNBT(new NBTTagCompound())));
                doubles(o, e);
            }
            else if (e instanceof EntityXPOrb)
            {
                EntityXPOrb xo = (EntityXPOrb)e;
                o.addProperty("kind", "xp");
                o.addProperty("xp", xo.getXpValue());
                doubles(o, e);
            }
            else
            {
                o.addProperty("kind", "other");
                o.addProperty("class", e.getClass().getSimpleName());
            }

            ents.add(o);
        }

        l.add("ents", ents);

        for (Entity e : cap.spawned) takeOut(ws, e);
        cap.spawned.clear();

        // The clear of the case's residue (an ice block left as water, the
        // companion half, the floor) runs on its own: its own entities go and
        // the Math.random stream goes back to where the case left it.
        long afterBreak = Det.state(MATH);
        Capture restore = new Capture();
        ws.addWorldAccess(restore);

        try
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                for (int dy = -1; dy <= 1; ++dy)
                {
                    for (int dz = -1; dz <= 1; ++dz)
                    {
                        if (ws.getBlock(x + dx, y + dy, z + dz) != Blocks.air)
                        {
                            ws.setBlock(x + dx, y + dy, z + dz, Blocks.air, 0, 2);
                        }
                    }
                }
            }
        }
        finally
        {
            ws.removeWorldAccess(restore);
        }

        for (Entity e : restore.spawned) takeOut(ws, e);
        rewind(MATH, afterBreak);

        p.inventory.mainInventory[0] = null;
        ITEM_STACK.set(p.inventory, null);

        lw.println(l.toString());
        ++caseIndex;
        return true;
    }

    /** Where the case counter lives; the sweep is single threaded. */
    static int caseIndex;

    static int statCount(StatFileWriter w, StatBase b) throws Exception
    {
        if (b == null) return 0;
        return (Integer)WRITE_STAT.invoke(w, b);
    }

    /** ids * 16 + metas of the 3x3 chunks around (cx,cz), chunk-pair major, so
     * the sweep cannot quietly change terrain outside the 3x3x3 it works in. */
    static int[] snapshot(WorldServer ws, int cx, int cz)
    {
        int w = 2 * RADIUS + 1;
        int[] a = new int[w * w * 65536];

        for (int x = -RADIUS; x <= RADIUS; ++x)
        {
            for (int z = -RADIUS; z <= RADIUS; ++z)
            {
                int base = ((x + RADIUS) * w + (z + RADIUS)) * 65536;

                for (int i = 0; i < 65536; ++i)
                {
                    int wx = (cx + x) * 16 + (i >> 12), wy = i & 255, wz = (cz + z) * 16 + ((i >> 8) & 15);
                    a[base + i] = Block.getIdFromBlock(ws.getBlock(wx, wy, wz)) * 16 + ws.getBlockMetadata(wx, wy, wz);
                }
            }
        }

        return a;
    }

    static String where(int[] a, int[] b)
    {
        for (int i = 0; i < a.length; ++i)
        {
            if (a[i] != b[i])
            {
                return "chunk " + ((i >> 16) / (2 * RADIUS + 1) - RADIUS) + "," + ((i >> 16) % (2 * RADIUS + 1) - RADIUS) + " cell " + (i & 65535)
                    + " want " + (a[i] >> 4) + ":" + (a[i] & 15) + " got " + (b[i] >> 4) + ":" + (b[i] & 15);
            }
        }

        return "?";
    }

    static int[] heldIds()
    {
        int[] a = new int[HELD.length];

        for (int i = 0; i < a.length; ++i) a[i] = HELD[i][0].isEmpty() ? 0 : Item.getIdFromItem(item(HELD[i][0]));

        return a;
    }

    static Item item(String name)
    {
        for (Item it : new Item[]{
                Items.shears, Items.diamond_pickaxe, Items.diamond_axe, Items.diamond_shovel,
                Items.wooden_pickaxe, Items.iron_pickaxe, Items.diamond_sword})
        {
            if (Item.itemRegistry.getNameForObject(it).equals("minecraft:" + name)) return it;
        }

        throw new IllegalArgumentException("no item " + name);
    }

    /** The case's held stack: one of the item, at one hit from breaking when
     * worn, with silk touch and/or fortune in the ench tag. */
    static ItemStack heldStack(int itemId, int silk, int fortune, boolean worn)
    {
        if (itemId == 0) return null;

        Item it = Item.getItemById(itemId);
        ItemStack st = new ItemStack(it, 1, worn ? it.getMaxDamage() - 1 : 0);
        List<NBTTagCompound> ench = new ArrayList<NBTTagCompound>();

        if (silk > 0) ench.add(enchant(Enchantment.silkTouch.effectId, silk));
        if (fortune > 0) ench.add(enchant(Enchantment.fortune.effectId, fortune));

        if (!ench.isEmpty())
        {
            NBTTagList list = new NBTTagList();

            for (NBTTagCompound e : ench) list.appendTag(e);

            NBTTagCompound tag = new NBTTagCompound();
            tag.setTag("ench", list);
            st.stackTagCompound = tag;
        }

        return st;
    }

    static NBTTagCompound enchant(int id, int lvl)
    {
        NBTTagCompound e = new NBTTagCompound();
        e.setShort("id", (short)id);
        e.setShort("lvl", (short)lvl);
        return e;
    }

    /** Every non-air cell of the 3x3x3 around (x,y,z), as {dx,dy,dz,id,meta}. The
     * block's own cell is always written, even when it is air: world.setBlock
     * can leave air with a non-zero meta, which setBlockToAir then changes. */
    static List<int[]> grid(WorldServer ws, int x, int y, int z)
    {
        List<int[]> out = new ArrayList<int[]>();

        for (int dx = -1; dx <= 1; ++dx)
        {
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dz = -1; dz <= 1; ++dz)
                {
                    Block b = ws.getBlock(x + dx, y + dy, z + dz);

                    if (b == Blocks.air && !(dx == 0 && dy == 0 && dz == 0)) continue;
                    out.add(new int[]{dx, dy, dz, Block.getIdFromBlock(b), ws.getBlockMetadata(x + dx, y + dy, z + dz)});
                }
            }
        }

        return out;
    }

    static JsonArray cellsJson(List<int[]> cells)
    {
        JsonArray a = new JsonArray();

        for (int[] c : cells)
        {
            JsonArray e = new JsonArray();

            for (int v : c) e.add(new JsonPrimitive(v));

            a.add(e);
        }

        return a;
    }

    /** every registered block id, in id order. */
    static int[] ids()
    {
        List<Integer> out = new ArrayList<Integer>();

        for (int id = 0; id < 4096; ++id)
        {
            if (!Block.blockRegistry.containsID(id)) continue;
            out.add(Integer.valueOf(id));
        }

        int[] a = new int[out.size()];

        for (int i = 0; i < a.length; ++i) a[i] = out.get(i).intValue();

        return a;
    }

    static boolean isExterior(int id)
    {
        for (int x : EXTERIOR_MUTATOR)
        {
            if (x == id) return true;
        }

        return false;
    }

    static String join(int[] a)
    {
        StringBuilder b = new StringBuilder();

        for (int i = 0; i < a.length; ++i)
        {
            if (i != 0) b.append(',');
            b.append(a[i]);
        }

        return b.toString();
    }

    /** A java.util.Random that records how many values it handed out. */
    static final class Counted extends Random
    {
        int draws;

        Counted(long seed) { super(seed); }

        @Override protected int next(int bits)
        {
            ++draws;
            return super.next(bits);
        }

        @Override public synchronized double nextGaussian()
        {
            ++draws;
            return super.nextGaussian();
        }
    }

    /** The probe's own Math.random stream, installed as Det's OTHER stream. */
    static Counted MATH;

    static final class MathStream
    {
        static final long MATH_SEED = 0x6d6174684f7468L;

        final Random mine;
        Random saved;

        MathStream(Random mine)
        {
            this.mine = mine;
            mine.setSeed(MATH_SEED);
        }

        static Field mathField() throws Exception
        {
            Field f = Det.class.getDeclaredField("math");
            f.setAccessible(true);

            if (!Random[].class.isAssignableFrom(f.getType())) throw new IllegalStateException("Det.math is not a Random[]");

            return f;
        }

        void install() throws Exception
        {
            Random[] v = (Random[])mathField().get(null);

            if (v.length != Det.ROLES) throw new IllegalStateException("Det.math has " + v.length + " roles");
            saved = v[Det.OTHER];
            v[Det.OTHER] = mine;
        }

        void uninstall(Random own) throws Exception
        {
            Random[] v = (Random[])mathField().get(null);

            if (v[Det.OTHER] == mine) v[Det.OTHER] = saved;
        }
    }

    /** Every entity the world creates, in spawn order. */
    static final class Capture implements IWorldAccess
    {
        final List<Entity> spawned = new ArrayList<Entity>();

        public void onEntityCreate(Entity e) { spawned.add(e); }
        public void onEntityDestroy(Entity e) {}
        public void markBlockForUpdate(int x, int y, int z) {}
        public void markBlockForRenderUpdate(int x, int y, int z) {}
        public void markBlockRangeForRenderUpdate(int x0, int y0, int z0, int x1, int y1, int z1) {}
        public void playSound(String s, double x, double y, double z, float v, float p) {}
        public void playSoundToNearExcept(EntityPlayer p, String s, double x, double y, double z, float v, float q) {}
        public void spawnParticle(String s, double x, double y, double z, double a, double b, double c) {}
        public void playRecord(String s, int x, int y, int z) {}
        public void broadcastSound(int a, int b, int c, int d, int e) {}
        public void playAuxSFX(EntityPlayer p, int a, int x, int y, int z, int v) {}
        public void destroyBlockPartially(int a, int x, int y, int z, int v) {}
        public void onStaticEntitiesChanged() {}
    }

    /** A Random that counts and forwards, so substituting it for World.rand
     * changes nothing but the count. */
    static final class CountedRandom extends Random
    {
        final Random inner;
        int draws;

        CountedRandom(Random inner)
        {
            super(0L);
            this.inner = inner;
        }

        @Override public int nextInt(int n) { ++draws; return inner.nextInt(n); }
        @Override public float nextFloat() { ++draws; return inner.nextFloat(); }
        @Override public double nextDouble() { ++draws; return inner.nextDouble(); }
        @Override public int nextInt() { ++draws; return inner.nextInt(); }
        @Override public long nextLong() { ++draws; return inner.nextLong(); }
        @Override public boolean nextBoolean() { ++draws; return inner.nextBoolean(); }
        @Override public void nextBytes(byte[] b) { ++draws; inner.nextBytes(b); }
        @Override public double nextGaussian() { ++draws; return inner.nextGaussian(); }

        @Override public synchronized void setSeed(long s)
        {
            if (inner != null) inner.setSeed(s);
        }
    }

    /** Takes an entity out of the world completely. */
    static void takeOut(WorldServer ws, Entity e)
    {
        e.setDead();

        if (e.addedToChunk)
        {
            net.minecraft.world.chunk.Chunk c = ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ);

            if (c != null) c.removeEntity(e);
        }

        ws.loadedEntityList.remove(e);
    }

    static void rewind(Random r, long state) throws Exception
    {
        if (seedField == null)
        {
            seedField = Random.class.getDeclaredField("seed");
            seedField.setAccessible(true);
        }

        ((java.util.concurrent.atomic.AtomicLong)seedField.get(r)).set(state);
    }

    static Field seedField;
    static Field ITEM_STACK;
    static Field STATS;
    static Method WRITE_STAT;

    static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static
    {
        try
        {
            ITEM_STACK = field(net.minecraft.entity.player.InventoryPlayer.class, "itemStack");
            STATS = field(EntityPlayerMP.class, "field_147103_bO");
            WRITE_STAT = StatFileWriter.class.getMethod("writeStat", StatBase.class);
        }
        catch (Exception e)
        {
            throw new ExceptionInInitializerError(e);
        }
    }

    /** Canonical JSON for one NBT tag, no whitespace. */
    static JsonElement canon(NBTBase tag)
    {
        switch (tag.getId())
        {
            case 1: return new JsonPrimitive("b:" + ((NBTTagByte)tag).func_150290_f());
            case 2: return new JsonPrimitive("s:" + ((NBTTagShort)tag).func_150289_e());
            case 3: return new JsonPrimitive("i:" + ((NBTTagInt)tag).func_150287_d());
            case 4: return new JsonPrimitive("l:" + ((NBTTagLong)tag).func_150291_c());
            case 5: return new JsonPrimitive("f:" + hex(Float.floatToRawIntBits(((NBTTagFloat)tag).func_150288_h()) & 4294967295L, 8));
            case 6: return new JsonPrimitive("d:" + hex(Double.doubleToRawLongBits(((NBTTagDouble)tag).func_150286_g()), 16));
            case 7:
            {
                byte[] a = ((NBTTagByteArray)tag).func_150292_c();
                StringBuilder b = new StringBuilder("ba:");

                for (int i = 0; i < a.length; ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(a[i]);
                }

                return new JsonPrimitive(b.toString());
            }
            case 8: return new JsonPrimitive("str:" + ((NBTTagString)tag).func_150285_a_());
            case 9:
            {
                JsonArray out = new JsonArray();

                for (Object o : list((NBTTagList)tag)) out.add(canon((NBTBase)o));

                return out;
            }
            case 10:
            {
                NBTTagCompound c = (NBTTagCompound)tag;
                String[] keys = (String[])c.func_150296_c().toArray(new String[0]);
                java.util.Arrays.sort(keys);
                JsonObject out = new JsonObject();

                for (String k : keys) out.add(k, canon(c.getTag(k)));

                return out;
            }
            case 11:
            {
                int[] a = ((NBTTagIntArray)tag).func_150302_c();
                StringBuilder b = new StringBuilder("ia:");

                for (int i = 0; i < a.length; ++i)
                {
                    if (i != 0) b.append(',');
                    b.append(a[i]);
                }

                return new JsonPrimitive(b.toString());
            }
            default: throw new IllegalStateException("cannot canonicalize NBT tag id " + tag.getId());
        }
    }

    static List list(NBTTagList tag)
    {
        try
        {
            return (List)field(NBTTagList.class, "tagList").get(tag);
        }
        catch (Exception e)
        {
            throw new IllegalStateException("NBTTagList.tagList", e);
        }
    }

    static String hex(long bits, int digits)
    {
        StringBuilder b = new StringBuilder();

        for (int i = digits - 1; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));

        return b.toString();
    }
}