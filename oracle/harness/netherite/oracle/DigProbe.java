package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import com.mojang.authlib.GameProfile;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.enchantment.Enchantment;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.FoodStats;
import net.minecraft.potion.Potion;
import net.minecraft.potion.PotionEffect;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.server.management.ItemInWorldManager;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatFileWriter;
import net.minecraft.stats.StatList;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.world.IWorldAccess;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.chunk.Chunk;

/**
 * The digging probe: the server's block-breaking state machine
 * (ItemInWorldManager.onBlockClicked, updateBlockRemoving with its durability
 * progress and the delayed break through uncheckedTryHarvestBlock,
 * cancelDestroyingBlock, tryHarvestBlock, removeBlock), driven the way
 * NetHandlerPlayServer drives it for a C07 dig, on a raw region scattered
 * with every breakable block kind, with a probe-owned EntityPlayerMP in
 * survival, not added to the world, holding a controlled stack.
 *
 * One run: a raw region (Probe.rawChunks, population off) far from spawn,
 * loaded cx-major, scattered with dig sites: a stone support with one target
 * block on it, three blocks apart in both axes, so a break's neighbour ring
 * never reaches another site. Every target kind is there: the ores, logs,
 * leaves, glass, ice, tall grass, wheat, wall torches, beds, doors, double
 * plants, chests (their tile entity filled per case) and spawners, plus
 * plain stone, dirt, sand and bedrock (which never breaks). Each site is dug
 * at most once, so a case never has to rebuild its target; the region is
 * recorded into start.bin.gz first and the native side replays from that.
 *
 * Per case: the held stack (empty hand, shears, every tool class and
 * material, with Efficiency, Silk Touch, Fortune and Unbreaking), the pose
 * (on the ground beside the target, off the ground, or in a water pit whose
 * eye cell is water), haste and mining fatigue, then the drive: tick 0
 * onBlockClicked, then per tick uncheckedTryHarvestBlock (the client's finish
 * packet), a cancel, or nothing, with updateBlockRemoving after every op and
 * the world's total time advanced one tick per dig tick, until the target is
 * air or the case cancels or reaches max ticks.
 *
 * Per tick the probe records the destroyBlockInWorldPartially calls (the dig
 * progress events), per case every block write (Rows.writeListener), every
 * spawned item and XP entity, the held stack after, the food exhaustion and
 * the three stat counters, the World.rand, Math.random, seeder and
 * entity-ID states, the chest's own Random, and an FNV-1a 64 hash of the 3x3
 * chunks around the target every 64 cases; final.bin.gz is the whole region
 * after the last case.
 *
 * Output DIR/manifest.json, DIR/start.bin.gz, DIR/cases.bin, DIR/ticks.bin,
 * DIR/writes.bin, DIR/ents.bin, DIR/caseout.bin, DIR/hash.bin, DIR/final.bin.gz.
 * The layouts are the manifest's layout strings.
 */
public final class DigProbe
{
    /** The kinds: {block id, number of metas, weight drawn}. The meta of a
     * site is drawn at scatter time and lives in the region. */
    static final int[][] KINDS = {
        {1, 1},     /* stone */
        {3, 1},     /* dirt */
        {12, 1},    /* sand */
        {14, 1},    /* gold ore */
        {15, 1},    /* iron ore */
        {16, 1},    /* coal ore */
        {21, 1},    /* lapis ore */
        {56, 1},    /* diamond ore */
        {73, 1},    /* redstone ore */
        {17, 12},   /* logs */
        {18, 4},    /* leaves */
        {20, 1},    /* glass */
        {79, 1},    /* ice */
        {31, 3},    /* tall grass */
        {59, 8},    /* wheat */
        {50, 4},    /* wall torches, metas 1..4 */
        {64, 4},    /* wooden doors, lower metas 0..3 */
        {26, 4},    /* beds, foot metas 0..3 */
        {175, 6},   /* double plants, lower variants 0..5 */
        {54, 1},    /* chests */
        {52, 1},    /* mob spawners */
        {7, 1},     /* bedrock, which never breaks */
    };

    /** The held pool rows: {item id, silk row, fortune row}. The efficiency
     * (0..5), unbreaking (0..3) and stack count are drawn per case; the silk
     * and fortune rows are picked between, never both. */
    static final int[][] HELD = {
        {0, 0, 0},      /* the empty hand */
        {359, 0, 0},    /* shears */
        {270, 0, 0},    /* wooden pickaxe */
        {274, 0, 0},    /* stone pickaxe */
        {257, 0, 0},    /* iron pickaxe */
        {278, 0, 0},    /* diamond pickaxe */
        {271, 0, 0},    /* wooden axe */
        {275, 0, 0},    /* stone axe */
        {258, 0, 0},    /* iron axe */
        {279, 0, 0},    /* diamond axe */
        {269, 0, 0},    /* wooden shovel */
        {273, 0, 0},    /* stone shovel */
        {256, 0, 0},    /* iron shovel */
        {277, 0, 0},    /* diamond shovel */
        {268, 0, 0},    /* wooden sword */
        {267, 0, 0},    /* iron sword */
        {276, 0, 0},    /* diamond sword */
        {278, 1, 0},    /* diamond pickaxe, silk touch */
        {278, 0, 1},    /* diamond pickaxe, fortune 1 */
        {278, 0, 2},    /* diamond pickaxe, fortune 2 */
        {278, 0, 3},    /* diamond pickaxe, fortune 3 */
        {277, 1, 0},    /* diamond shovel, silk touch */
        {276, 1, 0},    /* diamond sword, silk touch */
    };

    static final int[] CHEST_ITEMS = {4, 17, 45, 264, 265, 266, 331, 345};
    static final int MAX_TICKS = 160;
    static final long MATH_SEED = 0x6d6174684f7468L;
    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    private DigProbe() {}

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
        }, "Oracle DigProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 2002;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 2002;
        int grid = cmd.has("grid") ? cmd.get("grid").getAsInt() : 120;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 20000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        int spacing = 3;
        int half = grid / 2;
        int bx0 = cx - half, bz0 = cz - half;
        int minBlock = bx0 - 8, maxBlock = bx0 + (grid - 1) * spacing + 8;
        int x0 = (minBlock >> 4) - 1, x1 = (maxBlock >> 4) + 1;
        int z0 = (minBlock >> 4) - 1, z1 = (maxBlock >> 4) + 1;
        JsonArray loaded = new JsonArray();

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

        // the water pit: a 2x2 pool in the region's corner where the in-water
        // poses stand; its cells are never dug and never notified
        int pitX = x0 * 16 + 3, pitZ = z0 * 16 + 3, pitY = 90;
        // ------------------------------------------------------------- sites
        Random r = new Random(opseed);
        int nSites = grid * grid;
        int[] siteKind = new int[nSites];
        int[] siteMeta = new int[nSites];
        int[] siteX = new int[nSites];
        int[] siteY = new int[nSites];
        int[] siteZ = new int[nSites];
        int[] kindCount = new int[KINDS.length];
        int chestKind = -1;

        for (int k = 0; k < KINDS.length; ++k) if (KINDS[k][0] == 54) chestKind = k;

        for (int i = 0; i < nSites; ++i)
        {
            int x = bx0 + (i % grid) * spacing;
            int z = bz0 + (i / grid) * spacing;
int pick = r.nextInt(KINDS.length);
            int[] kind = KINDS[pick];
            int meta = kind[1] > 1 ? r.nextInt(kind[1]) : 0;
            if (pick == 15) meta = 1 + r.nextInt(4);  /* wall torches: metas 1..4 */
            int y = Math.min(99, Math.max(56, ws.getHeightValue(x, z)));

            // lift the site above any water, so no site cell ever touches it
            while (ws.getBlock(x, y, z).getMaterial().isLiquid()) ++y;

            ws.setBlock(x, y, z, Blocks.stone, 0, 2);
            siteKind[i] = pick;
            siteMeta[i] = meta;
            siteX[i] = x;
            siteY[i] = y;
            siteZ[i] = z;
            place(ws, kind[0], meta, x, y, z);
            ++kindCount[pick];
        }

        // the pit's water, placed last
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dz = 0; dz < 2; ++dz)
            {
                ws.setBlock(pitX + dx, pitY, pitZ + dz, Blocks.flowing_water, 0, 2);
            }
        }

        // ------------------------------------------------------------- start
        DataOutputStream startOut = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "start.bin.gz")), 1 << 16));
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        Field gap = field(Chunk.class, "isGapLightingUpdated");
        byte[] cols = new byte[256];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, buf);
                writeLe32(startOut, lx);
                writeLe32(startOut, lz);
                startOut.write(buf);
                for (int i = 0; i < 256; ++i) cols[i] = (byte)(c.updateSkylightColumns[i] ? 1 : 0);
                startOut.write(cols);
                startOut.write(gap.getBoolean(c) ? 1 : 0);
            }
        }

        startOut.close();

        // ------------------------------------------------------------- player
        ItemInWorldManager mgr = new ItemInWorldManager(ws);
        EntityPlayerMP p = new EntityPlayerMP(server, ws,
            new GameProfile(new java.util.UUID(0x64696770726f6265L, 0x6469676c616e6500L), "ProbeDig"), mgr);
        FIELD_GAME_TYPE.set(mgr, WorldSettings.GameType.SURVIVAL);
        p.capabilities.isCreativeMode = false;
        p.capabilities.disableDamage = false;
        p.capabilities.allowEdit = true;
        p.inventory.currentItem = 0;

        Field mathF = field(Det.class, "math");
        Random[] mathV = (Random[])mathF.get(null);
        Field seedF = field(Det.class, "seeder");
        Random[] seedV = (Random[])seedF.get(null);
        Field idF = field(Det.class, "nextId");
        int[] idV = (int[])idF.get(null);
        long mathStart = Det.state(mathV[Det.OTHER]);
        long seederStart = Det.state(seedV[Det.OTHER]);
        int nextIdStart = idV[Det.OTHER];
        java.util.Random chestRand = chestRandom();
        long chestStart = Det.state(chestRand);

        // ------------------------------------------------------------- cases
        Counted MATH = new Counted(MATH_SEED);
        Random savedMath = mathV[Det.OTHER];
        mathV[Det.OTHER] = MATH;

        OutputStream casesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin")), 1 << 16);
        OutputStream ticksOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "ticks.bin")), 1 << 16);
        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        OutputStream entsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "ents.bin")), 1 << 16);
        OutputStream coutOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "caseout.bin")), 1 << 16);
        OutputStream hashOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "hash.bin")), 1 << 16);

        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(World w, int x, int y, int z, int id, int meta)
            {
                try
                {
                    byte[] wr = new byte[19];
                    le32(wr, 0, caseIndex);
                    le32(wr, 4, x);
                    le32(wr, 8, y);
                    le32(wr, 12, z);
                    wr[16] = (byte)id;
                    wr[17] = (byte)(id >> 8);
                    wr[18] = (byte)meta;
                    writesOut.write(wr);
                }
                catch (IOException e)
                {
                    throw new RuntimeException(e);
                }
            }
        };

        Capture cap = new Capture();
        java.util.Random wsRand = ws.rand;
        byte[] trec = new byte[TICK_REC];
        byte[] orec = new byte[OUT_REC];
        byte[] hrec = new byte[HASH_REC];
        int[] order = new int[nSites];

        for (int i = 0; i < nSites; ++i) order[i] = i;
        for (int i = nSites - 1; i > 0; --i)
        {
            int j = r.nextInt(i + 1);
            int t = order[i];
            order[i] = order[j];
            order[j] = t;
        }

        for (int ci = 0; ci < cases; ++ci)
        {
            caseIndex = ci;
            int si = order[ci % nSites];
            int kind = siteKind[si];
            int tx = siteX[si], ty = siteY[si] + 1, tz = siteZ[si];

            // the wheat site digs the crop, not the farmland under it: the
            // crop pop above a broken farmland goes through the tick-machinery
            // drop path, which the native world models draw-only
            if (kind == 14) ++ty;

            int[] h = HELD[r.nextInt(HELD.length)];
            int eff = r.nextInt(6);
            int unbr = r.nextInt(4);
            int silk = h[1] > 0 ? 1 : 0;
            int fortune = silk > 0 ? 0 : h[2];
            int count0 = r.nextInt(8) == 0 ? 2 : 1;
            if (h[0] == 0) count0 = 0;   /* the empty hand records no stack */
            int heldId = h[0];

            /* one armed stack in sixteen arrives near broken: with Unbreaking
             * forced off, one break's damage usually breaks the tool
             * (damageItem: renderBrokenItemStack's draws, stackSize--, the
             * damage reset); max - 2 survives at one below the limit */
            int wearDamage = 0;
            if (heldId != 0 && r.nextInt(16) == 0)
            {
                unbr = 0;
                wearDamage = Item.getItemById(heldId).getMaxDamage() - r.nextInt(3);
            }

            ItemStack held = heldStack(heldId, silk, fortune, eff, unbr, count0);
            if (held != null && wearDamage > 0) held.setItemDamage(wearDamage);
            int damage0 = held == null ? 0 : held.getItemDamage();

            int pose = r.nextInt(12);
            boolean onGround = pose < 7 || pose == 11;
            boolean inWater = pose >= 9;

            if (inWater)
            {
                // the eye at pitY + 0.62 sits inside the meta-0 flowing water,
                // whose surface check puts the top at pitY + 1
                p.setPosition(pitX + r.nextInt(2) + 0.5D, pitY - 1.0D, pitZ + 0.5D);
            }
            else
            {
                p.setPosition(tx + 0.5D, ty + 1.0D, tz + 0.5D);
            }

            p.onGround = onGround;

            int haste = r.nextInt(8) == 0 ? 1 + r.nextInt(2) : 0;
            int fatigue = haste > 0 ? 0 : (r.nextInt(12) == 0 ? 1 + r.nextInt(2) : 0);
            setPotion(p, Potion.digSpeed.id, haste - 1);
            setPotion(p, Potion.digSlowdown.id, fatigue - 1);

            long caseOpseed = r.nextLong();
            long prandSeed = r.nextLong();

            ItemStack[] chestSlots = new ItemStack[27];
            int[] chestSizes = new int[27];
            if (kind == chestKind)
            {
                for (int s = 0; s < 27; ++s)
                {
                    if (r.nextInt(3) == 0)
                    {
                        chestSlots[s] = new ItemStack(Item.getItemById(CHEST_ITEMS[r.nextInt(CHEST_ITEMS.length)]),
                            1 + r.nextInt(24), r.nextInt(12));
                        chestSizes[s] = chestSlots[s].stackSize;
                    }
                }

                TileEntityChest te = (TileEntityChest)ws.getTileEntity(tx, ty, tz);
                if (te == null) throw new IllegalStateException("no chest tile entity at (" + tx + "," + ty + "," + tz + ")");

                for (int s = 0; s < 27; ++s) te.setInventorySlotContents(s, chestSlots[s]);
            }

            long chestBefore = Det.state(chestRand);
            MATH.setSeed(MATH_SEED);
            wsRand.setSeed(caseOpseed);
            prand(p).setSeed(prandSeed);

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
            StatBase statMine = StatList.mineBlockStatArray[Block.getIdFromBlock(ws.getBlock(tx, ty, tz))];
            StatBase statUse = heldId == 0 ? null : StatList.objectUseStats[heldId];
            StatBase statBreak = heldId == 0 ? null : StatList.objectBreakStats[heldId];
            int sm0 = statCount(stats, statMine), su0 = statCount(stats, statUse), sb0 = statCount(stats, statBreak);

            FIELD_DESTROYING.setBoolean(mgr, false);
            FIELD_INITIAL_DAMAGE.setInt(mgr, 0);
            FIELD_PART_X.setInt(mgr, -1);
            FIELD_PART_Y.setInt(mgr, -1);
            FIELD_PART_Z.setInt(mgr, -1);
            FIELD_CURBLOCK.setInt(mgr, 0);
            FIELD_FINISH.setBoolean(mgr, false);
            FIELD_POS_X.setInt(mgr, -1);
            FIELD_POS_Y.setInt(mgr, -1);
            FIELD_POS_Z.setInt(mgr, -1);
            FIELD_INITIAL_BLOCK.setInt(mgr, 0);
            FIELD_DURABILITY.setInt(mgr, -1);

            cap.spawned.clear();
            cap.partial.clear();
            ws.addWorldAccess(cap);

            int[] ops = new int[MAX_TICKS + 1];
            int nops = 0;
            boolean broke = false;

            try
            {
                cap.tick = 0;
                int side = r.nextInt(6);
                mgr.onBlockClicked(tx, ty, tz, side);
                ws.getWorldInfo().incrementTotalWorldTime(ws.getWorldInfo().getWorldTotalTime() + 1);
                mgr.updateBlockRemoving();
                if (ws.getBlock(tx, ty, tz).getMaterial() == Material.air) broke = true;

                for (int tick = 1; tick < MAX_TICKS && !broke; ++tick)
                {
                    int roll = r.nextInt(100);
                    cap.tick = tick;

                    if (roll < 6)
                    {
                        mgr.cancelDestroyingBlock(tx, ty, tz);
                        mgr.updateBlockRemoving();
                        ws.getWorldInfo().incrementTotalWorldTime(ws.getWorldInfo().getWorldTotalTime() + 1);
                        ops[nops++] = OP_CANCEL;
                        break;
                    }
                    else if (roll < 32)
                    {
                        mgr.uncheckedTryHarvestBlock(tx, ty, tz);
                        if (ws.getBlock(tx, ty, tz).getMaterial() == Material.air)
                        {
                            broke = true;
                        }
                        else
                        {
                            mgr.updateBlockRemoving();
                        }
                        ops[nops++] = OP_FINISH;
                    }
                    else
                    {
                        mgr.updateBlockRemoving();
                        if (ws.getBlock(tx, ty, tz).getMaterial() == Material.air) broke = true;
                        ops[nops++] = OP_TICK;
                    }

                    ws.getWorldInfo().incrementTotalWorldTime(ws.getWorldInfo().getWorldTotalTime() + 1);
                }
            }
            finally
            {
                ws.removeWorldAccess(cap);
            }

            ws.getWorldInfo().incrementTotalWorldTime(ws.getWorldInfo().getWorldTotalTime() + 1);

            int sm1 = statCount(stats, statMine), su1 = statCount(stats, statUse), sb1 = statCount(stats, statBreak);
            NBTTagCompound after = new NBTTagCompound();
            fs.writeNBT(after);
            ItemStack heldAfter = p.inventory.mainInventory[0];

            potionsClear(p);

            List<Entity> spawned = new ArrayList<Entity>(cap.spawned);
            for (Entity e : cap.spawned) takeOut(ws, e);
            cap.spawned.clear();

            for (int[] ev : cap.partial)
            {
                le32(trec, 0, ci);
                le32(trec, 4, ev[0]);
                trec[8] = (byte)ev[5];
                le32(trec, 9, ev[1]);
                le32(trec, 13, ev[2]);
                le32(trec, 17, ev[3]);
                trec[21] = (byte)ev[4];
                ticksOut.write(trec);
            }

            byte[] crec = new byte[CASE_BASE + 27 * 5 + 2 + nops];
            le32(crec, 0, kind);
            le32(crec, 4, tx);
            le32(crec, 8, ty);
            le32(crec, 12, tz);
            le16(crec, 16, heldId);
            crec[18] = (byte)count0;
            le16(crec, 19, damage0);
            crec[21] = (byte)eff;
            crec[22] = (byte)unbr;
            crec[23] = (byte)silk;
            crec[24] = (byte)fortune;
            crec[25] = (byte)(onGround ? 1 : 0);
            crec[26] = (byte)(inWater ? 1 : 0);
            crec[27] = (byte)(haste + 1);
            crec[28] = (byte)(fatigue + 1);
            le64(crec, 29, caseOpseed);
            le64(crec, 37, prandSeed);
            crec[45] = (byte)MAX_TICKS;
            crec[46] = (byte)(kind == chestKind ? 1 : 0);
            crec[47] = (byte)(siteMeta[si]);

            for (int s = 0; s < 27; ++s)
            {
                ItemStack cs = chestSlots[s];
                int base = CASE_BASE + s * 5;

                le16(crec, base, cs == null ? 0 : Item.getIdFromItem(cs.getItem()));
                crec[base + 2] = (byte)(cs == null ? 0 : cs.getItemDamage());
                crec[base + 3] = (byte)(cs == null ? 0 : chestSizes[s]);
                crec[base + 4] = (byte)(cs == null ? 0 : 1);
            }

            int at = CASE_BASE + 27 * 5;
            le16(crec, at, nops);
            at += 2;

            for (int ti = 0; ti < nops; ++ti)
            {
                crec[at++] = (byte)ops[ti];
            }

            casesOut.write(crec);

            for (Entity e : spawned)
            {
                byte[] er = new byte[ENT_REC];
                le32(er, 0, ci);
                int ea = 4;

                if (e instanceof EntityItem)
                {
                    EntityItem ei = (EntityItem)e;
                    ItemStack st = ei.getEntityItem();
                    er[ea++] = 1;
                    le16(er, ea, st.getItem() == null ? 0xffff : Item.getIdFromItem(st.getItem())); ea += 2;
                    le16(er, ea, st.getItemDamage()); ea += 2;
                    le16(er, ea, st.stackSize); ea += 2;
                    le16(er, ea, 0); ea += 2;
                    le32(er, ea, e.getEntityId()); ea += 4;
                    entDoubles(er, ea, e);
                    ea += 48;
                    entFloat(er, ea, e.rotationYaw);
                }
                else if (e instanceof EntityXPOrb)
                {
                    EntityXPOrb xo = (EntityXPOrb)e;
                    er[ea++] = 2;
                    le16(er, ea, 0); ea += 2;
                    le16(er, ea, 0); ea += 2;
                    le16(er, ea, 0); ea += 2;
                    le16(er, ea, xo.getXpValue()); ea += 2;
                    le32(er, ea, e.getEntityId()); ea += 4;
                    entDoubles(er, ea, e);
                    ea += 48;
                    entFloat(er, ea, e.rotationYaw);
                }
                else
                {
                    throw new IllegalStateException("dig spawned " + e.getClass().getSimpleName());
                }

                entsOut.write(er);
            }

            le16(orec, 0, heldAfter == null ? 0 : Item.getIdFromItem(heldAfter.getItem()));
            le16(orec, 2, heldAfter == null ? 0 : heldAfter.getItemDamage());
            orec[4] = (byte)(heldAfter == null ? 0 : heldAfter.stackSize);
            le32(orec, 5, Float.floatToRawIntBits(after.getFloat("foodExhaustionLevel")));
            orec[9] = (byte)(sm1 - sm0);
            orec[10] = (byte)(su1 - su0);
            orec[11] = (byte)(sb1 - sb0);
            le64(orec, 12, Det.state(wsRand));
            le64(orec, 20, Det.state(MATH));
            le64(orec, 28, Det.state(seedV[Det.OTHER]));
            le32(orec, 36, idV[Det.OTHER]);
            le64(orec, 40, Det.state(chestRand));
            orec[48] = (byte)(broke ? 1 : 0);
            coutOut.write(orec);

            if ((ci + 1) % 64 == 0 || ci == cases - 1)
            {
                long hv = Probe.hashAround(ws, tx >> 4, tz >> 4, buf);
                le32(hrec, 0, ci);
                le64(hrec, 4, hv);
                hashOut.write(hrec);
            }
        }

        mathV[Det.OTHER] = savedMath;
        Rows.writeListener = null;
        casesOut.close();
        ticksOut.close();
        writesOut.close();
        entsOut.close();
        coutOut.close();
        hashOut.close();

        // ------------------------------------------------------------- final
        DataOutputStream fin = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, buf);
                writeLe32(fin, lx);
                writeLe32(fin, lz);
                fin.write(buf);
                for (int i = 0; i < 256; ++i) cols[i] = (byte)(c.updateSkylightColumns[i] ? 1 : 0);
                fin.write(cols);
                fin.write(gap.getBoolean(c) ? 1 : 0);
            }
        }

        fin.close();

        // ------------------------------------------------------------- manifest
        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "dig");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("grid", grid);
        m.addProperty("spacing", spacing);
        m.addProperty("nsites", nSites);
        m.addProperty("cases", cases);
        m.addProperty("opseed", opseed);
        m.addProperty("math_seed", MATH_SEED);
        m.addProperty("max_ticks", MAX_TICKS);
        m.addProperty("region", "raw chunks, population off, from (" + x0 + "," + z0 + ") to (" + x1 + "," + z1
            + ") in load order; the scatter placed every site's blocks with setBlock flag 2 before start.bin.gz, which is the region the native side loads");
        m.add("loaded", loaded);
        m.addProperty("sites", "grid x grid sites, spacing 3 in x and z: a stone support at the terrain surface with one target on it; each site is dug at most once, in the case order");
        m.addProperty("pit", "a 2x2 flowing-water pool at (" + pitX + "," + pitY + "," + pitZ + "); the in-water poses stand in front of it with the eye cell inside it; nothing digs or notifies near it");
        JsonArray kinds = new JsonArray();
        for (int k = 0; k < KINDS.length; ++k)
        {
            JsonObject ko = new JsonObject();
            ko.addProperty("id", KINDS[k][0]);
            ko.addProperty("metas", KINDS[k][1]);
            ko.addProperty("sites", kindCount[k]);
            kinds.add(ko);
        }
        m.add("kinds", kinds);
        m.addProperty("case_layout", "cases.bin, one record per case: kind int32 LE, target x/y/z int32 LE each, held_item uint16 LE (0 = empty hand), count0 uint8, damage0 uint16 LE, efficiency uint8 0..5, unbreaking uint8 0..3, silk uint8, fortune uint8, on_ground uint8, in_water uint8, haste uint8 (amplifier + 1, 0 = not active), fatigue uint8 (same), opseed int64 LE, player-rand seed int64 LE, max_ticks uint8, chest uint8, meta uint8, then 27 chest slots of item uint16 LE, damage uint8, count uint8, present uint8 each, then nops uint16 LE and nops op bytes");
        m.addProperty("ops", "the op bytes: 0 = updateBlockRemoving only, 1 = cancelDestroyingBlock then updateBlockRemoving and the case ends, 2 = uncheckedTryHarvestBlock then updateBlockRemoving; tick 0 is always onBlockClicked at the target followed by updateBlockRemoving, and every tick also advances the world's total time by one");
        m.addProperty("tick_layout", "ticks.bin: case int32 LE, entity id int32 LE (the dig player's), tick uint8, x/y/z int32 LE each, stage int8, 22 bytes with no padding; one row per destroyBlockInWorldPartially call, in call order");
        m.addProperty("write_layout", "writes.bin: case int32 LE, x/y/z int32 LE each, id uint16 LE, meta uint8; one row per Rows.onBlock the dig drove; the scatter is not included");
        m.addProperty("ent_layout", "ents.bin: case int32 LE, kind uint8 (1 item, 2 xp orb), item uint16 LE, damage uint16 LE, count uint16 LE, xp uint16 LE, entity id int32 LE, x/y/z and motion x/y/z as double LE, yaw float LE; one row per spawned entity in spawn order");
        m.addProperty("caseout_layout", "caseout.bin, one record per case: held_item uint16 LE, held_damage uint16 LE, held_count uint8, exhaustion float bits int32 LE, stat_mine uint8, stat_use uint8, stat_break uint8, world-rand state uint64 LE, math state uint64 LE, seeder state uint64 LE, next entity id int32 LE, chest-rand state uint64 LE, broke uint8, pad 3");
        m.addProperty("hash_layout", "hash.bin: case int32 LE, hash uint64 LE; one row every 64 cases and the last, the FNV-1a 64 of the 3x3 chunks around the target's chunk");
        m.addProperty("streams", "per case the world Random is seeded to opseed, the player's own Random to the recorded seed, and Math.random (Det.math[OTHER]) to math_seed; the seeder, the entity-ID counter and the chest's own Random (BlockChest.field_149955_b) continue across cases; det_start records the math/seeder/id state after the scatter and the player's construction");
        m.addProperty("det_math_start", mathStart);
        m.addProperty("det_seeder_start", seederStart);
        m.addProperty("det_nextid_start", nextIdStart);
        m.addProperty("chest_rand_start", chestStart);
        m.addProperty("player_entity_id", p.getEntityId());
        m.addProperty("held_legend", "held pool row: [item id, silk row, fortune row]; efficiency 0..5, unbreaking 0..3 and the stack count (2 in one case in eight) are drawn per case; one armed stack in sixteen is near broken (damage at max, max - 1 or max - 2, Unbreaking forced off) so the tool-break branch runs");
        m.addProperty("drive", "per tick from the case RNG: 1 in 6 a cancel (C07 status 1; the case ends), 26 in 100 an uncheckedTryHarvestBlock (C07 status 2, the finish packet), else nothing; updateBlockRemoving after every op; the case ends when the target is air or at max_ticks");
        m.addProperty("time", "the world's total time advances one tick per dig tick; nothing on the dig path reads it (checked: only WorldInfo state moves)");
        m.addProperty("chest_items", join(CHEST_ITEMS));
        m.addProperty("creative", "survival only: the creative paths need a NetHandlerPlayServer (tryHarvestBlock's creative branch sends an S23 packet) and do not make the rest free");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", cases);
        res.addProperty("nsites", nSites);
        return res;
    }

    static final int OP_TICK = 0, OP_CANCEL = 1, OP_FINISH = 2;
    static final int CASE_BASE = 48;
    static final int TICK_REC = 22;
    static final int ENT_REC = 69;
    static final int OUT_REC = 52;
    static final int HASH_REC = 12;
    static int caseIndex;

    /** The extra cells a target kind needs around its stone support. */
    static void place(WorldServer ws, int id, int meta, int x, int y, int z)
    {
        int ty = y + 1;

        switch (id)
        {
            case 59: // wheat: farmland under it
                ws.setBlock(x, ty, z, Block.getBlockById(60), 0, 2);
                ws.setBlock(x, ty + 1, z, Block.getBlockById(id), meta, 2);
                break;
            case 50: // wall torch: a stone pillar, the torch standing on it
                ws.setBlock(x, ty, z, Blocks.stone, 0, 2);
                ws.setBlock(x, ty + 1, z, Block.getBlockById(id), 5, 2);
                break;
            case 64: // door: lower then upper
                ws.setBlock(x, ty, z, Block.getBlockById(id), meta, 2);
                ws.setBlock(x, ty + 1, z, Block.getBlockById(id), meta | 8, 2);
                break;
            case 94: // bed: foot then head, facing +x
                ws.setBlock(x, ty, z, Block.getBlockById(id), meta, 2);
                ws.setBlock(x + 1, ty, z, Block.getBlockById(id), meta | 8, 2);
                break;
            case 175: // double plant: lower then upper
                ws.setBlock(x, ty, z, Block.getBlockById(id), meta, 2);
                ws.setBlock(x, ty + 1, z, Block.getBlockById(id), meta | 8, 2);
                break;
            default:
                ws.setBlock(x, ty, z, Block.getBlockById(id), meta, 2);
        }
    }

    static int torchKind()
    {
        for (int k = 0; k < KINDS.length; ++k) if (KINDS[k][0] == 50) return k;
        return -1;
    }

    static java.util.Random chestRandom() throws Exception
    {
        Field f = field(net.minecraft.block.BlockChest.class, "field_149955_b");
        return (java.util.Random)f.get(Blocks.chest);
    }

    /** The player's own Random (Entity.rand, protected): the stack damage
     * draws (attemptDamageItem) come from it. */
    static java.util.Random prand(EntityPlayerMP p) throws Exception
    {
        return (java.util.Random)field(net.minecraft.entity.Entity.class, "rand").get(p);
    }

    /** The active-potion map (EntityLivingBase.activePotionsMap), written
     * directly: addPotionEffect on an EntityPlayerMP sends packets. An
     * amplifier below 0 means the effect is absent. */
    @SuppressWarnings("unchecked")
    static void setPotion(EntityPlayerMP p, int id, int amplifier) throws Exception
    {
        java.util.HashMap map = (java.util.HashMap)field(net.minecraft.entity.EntityLivingBase.class, "activePotionsMap").get(p);

        if (amplifier >= 0)
        {
            map.put(Integer.valueOf(id), new PotionEffect(id, 100000, amplifier));
        }
        else
        {
            map.remove(Integer.valueOf(id));
        }
    }

    @SuppressWarnings("unchecked")
    static void potionsClear(EntityPlayerMP p) throws Exception
    {
        ((java.util.HashMap)field(net.minecraft.entity.EntityLivingBase.class, "activePotionsMap").get(p)).clear();
    }

    static void entDoubles(byte[] out, int at, Entity e)
    {
        leD(out, at, e.posX);
        leD(out, at + 8, e.posY);
        leD(out, at + 16, e.posZ);
        leD(out, at + 24, e.motionX);
        leD(out, at + 32, e.motionY);
        leD(out, at + 40, e.motionZ);
    }

    static void entFloat(byte[] out, int at, float f)
    {
        int bits = Float.floatToRawIntBits(f);
        out[at] = (byte)bits;
        out[at + 1] = (byte)(bits >> 8);
        out[at + 2] = (byte)(bits >> 16);
        out[at + 3] = (byte)(bits >> 24);
    }

    static void leD(byte[] out, int at, double d)
    {
        long v = Double.doubleToRawLongBits(d);
        for (int i = 0; i < 8; ++i) out[at + i] = (byte)(v >> (8 * i));
    }

    static void le32(byte[] out, int at, int v)
    {
        for (int i = 0; i < 4; ++i) out[at + i] = (byte)(v >> (8 * i));
    }

    static void le16(byte[] out, int at, int v)
    {
        out[at] = (byte)v;
        out[at + 1] = (byte)(v >> 8);
    }

    static void le64(byte[] out, int at, long v)
    {
        for (int i = 0; i < 8; ++i) out[at + i] = (byte)(v >> (8 * i));
    }

    static void writeLe32(OutputStream out, int v) throws IOException
    {
        out.write(v);
        out.write(v >> 8);
        out.write(v >> 16);
        out.write(v >> 24);
    }

    /** Takes an entity out of the world completely. */
    static void takeOut(WorldServer ws, Entity e)
    {
        e.setDead();

        if (e.addedToChunk)
        {
            Chunk c = ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ);
            if (c != null) c.removeEntity(e);
        }

        ws.loadedEntityList.remove(e);
    }

    static int statCount(StatFileWriter w, StatBase b) throws Exception
    {
        if (b == null) return 0;
        return (Integer)WRITE_STAT.invoke(w, b);
    }

    /** The case's held stack, with Efficiency, Unbreaking, and Silk or
     * Fortune in the ench tag. */
    static ItemStack heldStack(int itemId, int silk, int fortune, int eff, int unbr, int count)
    {
        if (itemId == 0) return null;

        Item it = Item.getItemById(itemId);
        ItemStack st = new ItemStack(it, count, 0);
        NBTTagList list = new NBTTagList();

        if (silk > 0) list.appendTag(enchant(Enchantment.silkTouch.effectId, 1));
        if (fortune > 0) list.appendTag(enchant(Enchantment.fortune.effectId, fortune));
        if (eff > 0) list.appendTag(enchant(Enchantment.efficiency.effectId, eff));
        if (unbr > 0) list.appendTag(enchant(Enchantment.unbreaking.effectId, unbr));

        if (list.tagCount() > 0)
        {
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

    /** Every entity the world creates, and every dig progress event. */
    static final class Capture implements IWorldAccess
    {
        final List<Entity> spawned = new ArrayList<Entity>();
        final List<int[]> partial = new ArrayList<int[]>();
        int tick;                /* the dig tick the world is on, set by the drive */

        public void onEntityCreate(Entity e) { spawned.add(e); }
        public void onEntityDestroy(Entity e) {}
        public void markBlockForUpdate(int x, int y, int z) {}
        public void markBlockForRenderUpdate(int x, int y, int z) {}
        public void markBlockRangeForRenderUpdate(int x0, int y0, int z0, int x1, int y1, int z1) {}
        public void playSound(String s, double x, double y, double z, float v, float p) {}
        public void playSoundToNearExcept(EntityPlayer p, String s, double x, double y, double z, float v, float q) {}
        public void spawnParticle(String s, double x, double y, double z, double a, double b, double c) {}
        public void playRecord(String s, int x, int y, int z) {}
        public void broadcastSound(int a, int b, int c, int d, int e2) {}
        public void playAuxSFX(EntityPlayer p, int a, int x, int y, int z, int v) {}
        public void destroyBlockPartially(int a, int x, int y, int z, int v) { partial.add(new int[]{a, x, y, z, v, tick}); }
        public void onStaticEntitiesChanged() {}
    }

    static final class Pose
    {
        final double x, y, z;
        Pose(double x, double y, double z) { this.x = x; this.y = y; this.z = z; }
    }

    /** A Random that counts how many values it handed out. */
    static final class Counted extends java.util.Random
    {
        int draws;
        Counted(long seed) { super(seed); }
        @Override protected int next(int bits) { ++draws; return super.next(bits); }
        @Override public synchronized double nextGaussian() { ++draws; return super.nextGaussian(); }
    }

    static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static Random[] mathV(Field f) throws Exception
    {
        return (Random[])f.get(null);
    }

    static Field FIELD_DESTROYING, FIELD_INITIAL_DAMAGE, FIELD_PART_X, FIELD_PART_Y, FIELD_PART_Z, FIELD_CURBLOCK,
        FIELD_FINISH, FIELD_POS_X, FIELD_POS_Y, FIELD_POS_Z, FIELD_INITIAL_BLOCK, FIELD_DURABILITY, FIELD_GAME_TYPE,
        ITEM_STACK, STATS;
    static Method WRITE_STAT;

    static
    {
        try
        {
            FIELD_DESTROYING = field(ItemInWorldManager.class, "isDestroyingBlock");
            FIELD_INITIAL_DAMAGE = field(ItemInWorldManager.class, "initialDamage");
            FIELD_PART_X = field(ItemInWorldManager.class, "partiallyDestroyedBlockX");
            FIELD_PART_Y = field(ItemInWorldManager.class, "partiallyDestroyedBlockY");
            FIELD_PART_Z = field(ItemInWorldManager.class, "partiallyDestroyedBlockZ");
            FIELD_CURBLOCK = field(ItemInWorldManager.class, "curblockDamage");
            FIELD_FINISH = field(ItemInWorldManager.class, "receivedFinishDiggingPacket");
            FIELD_POS_X = field(ItemInWorldManager.class, "posX");
            FIELD_POS_Y = field(ItemInWorldManager.class, "posY");
            FIELD_POS_Z = field(ItemInWorldManager.class, "posZ");
            FIELD_INITIAL_BLOCK = field(ItemInWorldManager.class, "initialBlockDamage");
            FIELD_DURABILITY = field(ItemInWorldManager.class, "durabilityRemainingOnBlock");
            FIELD_GAME_TYPE = field(ItemInWorldManager.class, "gameType");
            ITEM_STACK = field(net.minecraft.entity.player.InventoryPlayer.class, "itemStack");
            STATS = field(EntityPlayerMP.class, "field_147103_bO");
            WRITE_STAT = StatFileWriter.class.getMethod("writeStat", StatBase.class);
        }
        catch (Exception e)
        {
            throw new ExceptionInInitializerError(e);
        }
    }
}