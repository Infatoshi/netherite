package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.TreeSet;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.block.BlockEventData;
import net.minecraft.block.BlockJukebox;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.entity.player.InventoryPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.NetHandlerPlayServer;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.server.management.ItemInWorldManager;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatFileWriter;
import net.minecraft.stats.StatList;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityComparator;
import net.minecraft.tileentity.TileEntityFlowerPot;

import net.minecraft.tileentity.TileEntityNote;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.FoodStats;
import net.minecraft.util.IChatComponent;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.NextTickListEntry;

/**
 * The activation probe: the first half of ItemInWorldManager
 * .activateBlockOrUseItem (Block.onBlockActivated and everything it touches),
 * driven exactly as NetHandlerPlayServer drives a C08, on a raw region
 * scattered with every activatable block kind, with a probe-owned
 * EntityPlayerMP in survival posed per case.
 *
 * One case is one activateBlockOrUseItem call. The case draws the target
 * kind and cell, the side and hit offsets, the player's yaw and position,
 * the sneaking flag, the day/night state, the food stats, the inventory
 * fullness and the held stack, plus the tile entity state for the note
 * block, jukebox, flower pot and bed kinds; then the region is set back to
 * that drawn state (the setup, with the write listener off) and the call
 * runs with it on. Every case resets its site, so a case never depends on
 * the previous one except through the recorded streams.
 *
 * The chest, furnace, dispenser and crafting table GUIs: the probe player
 * carries a channel-less NetHandlerPlayServer, so the real displayGUI
 * methods run and their packets queue into the NetworkManager, which the
 * probe clears per case. What the port checks is the server state around
 * the GUI: the chest's numPlayersUsing, its open and close block events
 * (the InventoryLargeChest pairing opens and closes both halves) and the
 * close's onContainerClosed. Each case closes the GUI it opened, the way a
 * client closing the window does.
 *
 * Sleep is recorded, not ported: the bed cases run the real sleepInBedAt
 * and the probe records the player's position and sleeping flag, the bed's
 * metadata and the chat message (occupied, noSleep, notSafe) through a hook
 * on addChatComponentMessage. The occupied-bed search uses a probe-owned
 * dummy EntityPlayerMP held in the world's playerEntities with its sleeping
 * flag and playerLocation set per case. NOT_SAFE never happens (the region
 * holds no mobs). The wrong-dimension explosion runs in a separate dim=-1
 * run; it is the explosion lane's machine.
 *
 * The held pools are per kind, so the second half of activateBlockOrUseItem
 * (ItemStack.tryPlaceItemIntoWorld) is only ever reached with the disc on a
 * meta-0 jukebox (ItemRecord.onItemUse) or the glass bottle on a cauldron
 * whose water level is 0 (the base Item.onItemUse, which returns false).
 * ItemBlock placement, ItemBucket scooping and armor washing never happen.
 * Sneaking is drawn only with an empty hand, so the skip-to-item half with
 * a held stack never happens either (that is lane itemuse).
 *
 * Per case the probe records the return value, every block write, every
 * block event (both queues are cleared after, what the server tick's
 * func_147488_Z drain would leave; none of the events these cases produce
 * has a receiver with world state), every newly scheduled tick entry (the
 * buttons, and the fire the nether explosion places), every spawned entity,
 * the held stack and the whole player inventory after, the food stats
 * after, the player's position and sleeping flag, the tile entity states
 * after (note pitch, jukebox disc, comparator output, both chests'
 * numPlayersUsing), the streams, and a hash of the 3x3 chunks every 64
 * cases; final.bin.gz is the whole region after the last case.
 *
 * Output DIR/manifest.json, DIR/start.bin.gz, DIR/cases.bin,
 * DIR/caseout.bin, DIR/invs.bin, DIR/writes.bin, DIR/events.bin,
 * DIR/ticks.bin, DIR/ents.bin, DIR/hash.bin, DIR/final.bin.gz.
 */
public final class ActivateProbe
{
    /** throwaway trace switch for the setup sweep forensics */
    /** The kinds: {block id, metas drawn per case}. Bed, note, jukebox,
     * pot, chest and cake kinds draw their state per case instead. */
    static final int[][] KINDS = {
        {64, 8},    /*  0 wooden door, lower meta 0..7 */
        {71, 8},    /*  1 iron door */
        {96, 16},   /*  2 wooden trapdoor */
        {107, 16},  /*  3 fence gate */
        {77, 16},   /*  4 stone button */
        {143, 16},  /*  5 wooden button */
        {69, 16},   /*  6 lever */
        {26, 1},    /*  7 bed */
        {92, 6},    /*  8 cake */
        {25, 1},    /*  9 note block */
        {84, 1},    /* 10 jukebox */
        {93, 16},   /* 11 unpowered repeater */
        {94, 16},   /* 12 powered repeater */
        {149, 16},  /* 13 unpowered comparator */
        {150, 16},  /* 14 powered comparator */
        {140, 1},   /* 15 flower pot */
        {118, 4},   /* 16 cauldron */
        {54, 1},    /* 17 chest single */
        {54, 1},    /* 18 chest pair */
        {61, 1},    /* 19 furnace */
        {62, 1},    /* 20 lit furnace */
        {23, 1},    /* 21 dispenser */
        {58, 1},    /* 22 crafting table */
    };

    static final int K_DOOR_W = 0, K_DOOR_I = 1, K_TRAP_W = 2, K_GATE = 3,
        K_BTN_STONE = 4, K_BTN_WOOD = 5, K_LEVER = 6, K_BED = 7, K_CAKE = 8, K_NOTE = 9,
        K_JUKEBOX = 10, K_REP_U = 11, K_REP_P = 12, K_CMP_U = 13, K_CMP_P = 14, K_POT = 15,
        K_CAULDRON = 16, K_CHEST = 17, K_CHEST2 = 18, K_FURNACE = 19, K_FURNACE_LIT = 20,
        K_DISPENSER = 21, K_WORKBENCH = 22;

    /** The held pool rows: {item id, damage drawn 0..n-1}. */
    static final int[][] HELD = {
        {0, 1},         /*  0 the empty hand */
        {6, 6},         /*  1 sapling */
        {37, 1},        /*  2 yellow flower */
        {38, 1},        /*  3 red flower */
        {39, 1},        /*  4 brown mushroom */
        {40, 1},        /*  5 red mushroom */
        {32, 1},        /*  6 dead bush */
        {81, 1},        /*  7 cactus */
        {325, 1},       /*  8 empty bucket */
        {326, 1},       /*  9 water bucket */
        {374, 1},       /* 10 glass bottle */
        {2256, 1},      /* 11 record 13 */
        {2257, 1},      /* 12 record cat */
        {1, 1},         /* 13 stone block */
    };
    static final int[] POOL_DEFAULT = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    static final int[] POOL_JUKEBOX = {0, 10, 11, 12};
    static final int[] POOL_POT = {0, 1, 2, 3, 4, 5, 6, 7};
    static final int[] POOL_CAULDRON = {0, 9, 10};

    static final int CHEST_FILLER = 4;    /* cobblestone, the inventory-full filler */
    static final long MATH_SEED = 0x6d6174684f7468L;
    static final int CASE_REC = 104;
    static final int OUT_REC = 116;
    static final int INV_REC = 180;
    static final int EVENT_REC = 20;
    static final int TICK_REC = 40;
    static final int ENT_REC = 69;
    static final int HASH_REC = 12;
    static final double D02 = 0.20000000298023224D;
    static final double D01 = 0.10000000149011612D;

    static final int[] CHAT = new int[1];
    static int caseIndex;

    /** The Det Math.random stream, swapped for the run. */
    static Random[] mathV;
    static Field mathField;
    static Field seedField;
    static Field idField;

    static
    {
        try
        {
            mathField = field(Det.class, "math");
            seedField = field(Det.class, "seeder");
            idField = field(Det.class, "nextId");
            mathV = (Random[])mathField.get(null);
        }
        catch (Exception e)
        {
            throw new ExceptionInInitializerError(e);
        }
    }

    private ActivateProbe() {}

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
        }, "Oracle ActivateProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int dim = cmd.has("dim") ? cmd.get("dim").getAsInt() : 0;
        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 3002;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 3002;
        int grid = cmd.has("grid") ? cmd.get("grid").getAsInt() : 100;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 20000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 21L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();
        WorldServer ws = server.worldServers[dim == 0 ? 0 : (dim == -1 ? 1 : 2)];
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

        // ------------------------------------------------------------- sites
        Random r = new Random(opseed);
        int nSites = grid * grid;
        int[] siteKind = new int[nSites];
        int[] siteX = new int[nSites];
        int[] siteY = new int[nSites];
        int[] siteZ = new int[nSites];
        int[] kindCount = new int[KINDS.length];
        boolean hell = dim != 0;
        int supportId = hell ? 87 : 1;   /* netherrack / stone */

        for (int i = 0; i < nSites; ++i)
        {
            int x = bx0 + (i % grid) * spacing;
            int z = bz0 + (i / grid) * spacing;
            int pick = hell ? K_BED : r.nextInt(KINDS.length);
            int y = siteBase(ws, dim, x, z);

            siteKind[i] = pick;
            siteX[i] = x;
            siteY[i] = y;
            siteZ[i] = z;
            setupSite(ws, dim, pick, x, y, z, supportId);
            ++kindCount[pick];
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

        // ------------------------------------------------------------- players
        ItemInWorldManager mgr = new ItemInWorldManager(ws);
        EntityPlayerMP p = new EntityPlayerMP(server, ws,
            new com.mojang.authlib.GameProfile(new java.util.UUID(0x61637470726f6265L, 0x6163746c616e65L), "ProbeActivate"), mgr)
        {
            @Override
            public void addChatComponentMessage(IChatComponent msg)
            {
                String s = msg instanceof net.minecraft.util.ChatComponentTranslation
                    ? ((net.minecraft.util.ChatComponentTranslation)msg).getKey()
                    : msg.getUnformattedText();

                if (s.contains("tile.bed.occupied")) CHAT[0] = 1;
                else if (s.contains("tile.bed.noSleep")) CHAT[0] = 2;
                else if (s.contains("tile.bed.notSafe")) CHAT[0] = 3;
                else CHAT[0] = 4;
                super.addChatComponentMessage(msg);
            }
        };
        field(ItemInWorldManager.class, "gameType").set(mgr, WorldSettings.GameType.SURVIVAL);
        p.capabilities.isCreativeMode = false;
        p.capabilities.disableDamage = false;
        p.capabilities.allowEdit = true;
        p.inventory.currentItem = 0;

        // the channel-less net handler: the GUI opens and the chat messages
        // queue their packets and never reach the wire
        NetworkManager net = new NetworkManager(true);
        new NetHandlerPlayServer((MinecraftServer)server, net, p);

        // the dummy sleeper the occupied-bed search looks for: in the world's
        // playerEntities, its sleeping flag and playerLocation set per case
        EntityPlayerMP dummy = new EntityPlayerMP(server, ws,
            new com.mojang.authlib.GameProfile(new java.util.UUID(0x62656464756d6d79L, 0x736c6565706572L), "ProbeSleeper"), new ItemInWorldManager(ws));
        List<EntityPlayer> players = (List<EntityPlayer>)field(World.class, "playerEntities").get(ws);
        players.add(dummy);

        long mathStart = Det.state(mathV[Det.OTHER]);
        long seederStart = Det.state(((Random[])seedField.get(null))[Det.OTHER]);
        int nextIdStart = ((int[])idField.get(null))[Det.OTHER];
        Field tickEntryCounter = field(NextTickListEntry.class, "nextTickEntryID");
        long tickEntryStart = tickEntryCounter.getLong(null);
        long totalTime = ws.getWorldInfo().getWorldTotalTime();
        StatFileWriter stats = (StatFileWriter)field(EntityPlayerMP.class, "field_147103_bO").get(p);

        // ------------------------------------------------------------- files
        OutputStream casesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin")), 1 << 16);
        OutputStream outOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "caseout.bin")), 1 << 16);
        OutputStream invOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "invs.bin")), 1 << 16);
        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        OutputStream eventsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "events.bin")), 1 << 16);
        OutputStream ticksOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "ticks.bin")), 1 << 16);
        OutputStream entsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "ents.bin")), 1 << 16);
        OutputStream hashOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "hash.bin")), 1 << 16);

        Rows.WriteListener wl = new Rows.WriteListener()
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
        byte[] crec = new byte[CASE_REC];
        byte[] orec = new byte[OUT_REC];
        byte[] irec = new byte[INV_REC];
        byte[] trec = new byte[TICK_REC];
        byte[] hrec = new byte[HASH_REC];
        Field teNote = field(TileEntityNote.class, "field_145879_a");
        Field tePotItem = field(TileEntityFlowerPot.class, "field_145967_a");
        Field tePotData = field(TileEntityFlowerPot.class, "field_145968_i");
        Field teDisc = field(BlockJukebox.TileEntityJukebox.class, "field_145858_a");
        Field teCount = field(TileEntityChest.class, "field_145987_o");
        Field evQueue = field(WorldServer.class, "field_147490_S");
        Field evCur = field(WorldServer.class, "field_147489_T");
        Field pendingTree = field(WorldServer.class, "pendingTickListEntriesTreeSet");
        Field tickTime = field(NextTickListEntry.class, "tickEntryID");
        Field tickBlock = field(NextTickListEntry.class, "field_151352_g");
        Field evX = field(BlockEventData.class, "coordX");
        Field evY = field(BlockEventData.class, "coordY");
        Field evZ = field(BlockEventData.class, "coordZ");
        Field evBlock = field(BlockEventData.class, "field_151344_d");
        Field evId = field(BlockEventData.class, "eventID");
        Field evParam = field(BlockEventData.class, "eventParameter");
        Field playerSleeping = field(EntityPlayer.class, "sleeping");
        Field netQueue = field(NetworkManager.class, "outboundPacketsQueue");
        Method setSizeM = Entity.class.getDeclaredMethod("setSize", Float.TYPE, Float.TYPE);
        setSizeM.setAccessible(true);
        Method calcSky = World.class.getDeclaredMethod("calculateSkylightSubtracted", Float.TYPE);
        calcSky.setAccessible(true);
        TreeSet pending = (TreeSet)pendingTree.get(ws);
        Object evLists = evQueue.get(ws);
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
            CHAT[0] = 0;
            int si = order[ci % nSites];
            int kind = siteKind[si];
            int id = KINDS[kind][0];
            int bx = siteX[si], by = siteY[si], bz = siteZ[si];
            int half2 = (kind == K_DOOR_W || kind == K_DOOR_I || kind == K_BED || kind == K_CHEST2) ? r.nextInt(2) : 0;
            int tx = bx, ty = by + 1, tz = bz;

            if ((kind == K_DOOR_W || kind == K_DOOR_I) && half2 == 1) ty = by + 2;
            else if (kind == K_BED && half2 == 1) tx = bx + 1;
            else if (kind == K_CHEST2 && half2 == 1) tx = bx + 1;

            // ------------------------------------------------- case state
            int meta = KINDS[kind][1] > 1 ? r.nextInt(KINDS[kind][1]) : 0;
            int bedDir = 0, footOcc = 0, headOcc = 0;
            int potFull = 0, potItem = 0, potData = 0;
            int jmeta = 0, jdisc = 0, notePitch = 0;

            if (kind == K_BED)
            {
                bedDir = r.nextInt(4);
                footOcc = r.nextInt(2);
                headOcc = r.nextInt(2);
                meta = bedDir;
            }
            else if (kind == K_POT)
            {
                potFull = r.nextInt(2);

                if (potFull == 1)
                {
                    int[] row = HELD[1 + r.nextInt(7)];
                    potItem = row[0];
                    potData = row[1] > 1 ? r.nextInt(row[1]) : 0;
                    meta = potData;
                }
            }
            else if (kind == K_JUKEBOX)
            {
                jmeta = r.nextInt(2);
                jdisc = jmeta == 1 ? (r.nextInt(2) == 0 ? 2256 : 2257) : 0;
                meta = jmeta;
            }
            else if (kind == K_NOTE)
            {
                notePitch = r.nextInt(25);
            }

            int chestBlocked = kind == K_CHEST && r.nextInt(3) == 0 ? 1 : 0;

            setupCase(ws, dim, kind, bx, by, bz, id, meta, bedDir, footOcc, headOcc,
                potFull, potItem, potData, jmeta, jdisc, notePitch, chestBlocked, teNote, teDisc);

            // ------------------------------------------------- pose and draws
            int heldRow = drawHeld(r, kind);
            int heldId = HELD[heldRow][0];
            int heldDamage = HELD[heldRow][1] > 1 ? r.nextInt(HELD[heldRow][1]) : 0;
            int heldCount = heldId == 0 ? 0 : (r.nextInt(8) == 0 ? 2 : 1);
            boolean sneak = heldId == 0 && r.nextInt(4) == 0;
            int pose = kind == K_BED ? r.nextInt(3) : 0;
            int yawKind = r.nextInt(4);
            float yaw = yawKind == 3 ? (float)(r.nextInt(360) - 180) : r.nextFloat() * 360.0F - 180.0F;
            double pxx = (double)tx + 0.5D, pyy = (double)ty + 1.0D, pzz = (double)tz + 0.5D;

            if (kind == K_BED)
            {
                if (pose == 2)
                {
                    pxx = (double)tx + 0.5D + 4.0D + r.nextDouble() * 4.0D;
                }
                else if (pose == 1)
                {
                    pxx = (double)tx + 0.5D + (r.nextDouble() * 3.0D - 1.5D);
                    pyy = (double)ty + r.nextDouble();
                }
            }

            setSizeM.invoke(p, Float.valueOf(0.6F), Float.valueOf(1.8F));
            p.yOffset = 0.0F;
            p.motionX = p.motionY = p.motionZ = 0.0D;
            p.playerLocation = null;
            p.setPosition(pxx, pyy, pzz);
            p.rotationYaw = yaw;
            p.rotationPitch = 0.0F;
            p.setSneaking(sneak);

            int day = r.nextInt(2);
            ws.setWorldTime(day == 1 ? 18000L : 1000L);
            calcSky.invoke(ws, Float.valueOf(1.0F));

            int side = r.nextInt(6);
            float vx = r.nextFloat(), vy = r.nextFloat(), vz = r.nextFloat();
            long caseOpseed = r.nextLong();
            int invFull = r.nextInt(2);
            int foodLevel = r.nextInt(21);
            float foodSat = r.nextFloat() * 5.0F;
            int dummyAt = kind == K_BED ? r.nextInt(2) : 0;

            ItemStack held = heldId == 0 ? null : new ItemStack(Item.getItemById(heldId), heldCount, heldDamage);
            field(InventoryPlayer.class, "itemStack").set(p.inventory, null);

            for (int s = 0; s < 36; ++s) p.inventory.mainInventory[s] = null;
            p.inventory.mainInventory[0] = held;
            p.inventory.currentItem = 0;

            if (invFull == 1)
            {
                for (int s = 1; s < 36; ++s) p.inventory.mainInventory[s] = new ItemStack(Item.getItemById(CHEST_FILLER), 64, 0);
            }

            FoodStats fs = p.getFoodStats();
            NBTTagCompound food = new NBTTagCompound();
            food.setInteger("foodLevel", foodLevel);
            food.setInteger("foodTickTimer", 0);
            food.setFloat("foodSaturationLevel", foodSat);
            food.setFloat("foodExhaustionLevel", 0.0F);
            fs.readNBT(food);

            // the dummy sleeper: at this bed's head when drawn, never elsewhere
            playerSleeping.setBoolean(dummy, false);
            dummy.playerLocation = null;

            if (dummyAt == 1)
            {
                playerSleeping.setBoolean(dummy, true);
                dummy.playerLocation = new ChunkCoordinates(bx + 1, ty, bz);
            }

            // ------------------------------------------------- the call
            ws.addWorldAccess(cap);
            cap.spawned.clear();

            java.util.Random wsRand = ws.rand;
            wsRand.setSeed(caseOpseed);
            mathV[Det.OTHER].setSeed(MATH_SEED);

            Object lists = evLists;
            ((List)Array.get(lists, 0)).clear();
            ((List)Array.get(lists, 1)).clear();
            evCur.setInt(ws, 0);

            long idBefore = tickEntryCounter.getLong(null);
            int statUse = 0;

            if (heldId != 0)
            {
                statUse = -writeStat(stats, StatList.objectUseStats[heldId]);
            }

            Rows.writeListener = wl;
            boolean ret = false;

            try
            {
                ret = mgr.activateBlockOrUseItem(p, ws, p.inventory.getCurrentItem(), tx, ty, tz, side, vx, vy, vz);
            }
            finally
            {
                Rows.writeListener = null;
            }

            if (heldId != 0)
            {
                statUse += writeStat(stats, StatList.objectUseStats[heldId]);
            }

            // ------------------------------------------------- the GUI close
            boolean opened = false;

            if (kind == K_CHEST || kind == K_CHEST2 || kind == K_FURNACE || kind == K_FURNACE_LIT
                || kind == K_DISPENSER || kind == K_WORKBENCH)
            {
                opened = p.openContainer != p.inventoryContainer;
            }

            if (opened)
            {
                p.openContainer.onContainerClosed(p);
                p.openContainer = p.inventoryContainer;
            }

            // the case's queue rows; both queues are cleared after, the drain
            // the server tick does (no receiver here has a world effect)
            List q = (List)Array.get(lists, evCur.getInt(ws));

            for (Object o : q)
            {
                byte[] er = new byte[EVENT_REC];
                le32(er, 0, ci);
                le32(er, 4, evX.getInt(o));
                le32(er, 8, evY.getInt(o));
                le32(er, 12, evZ.getInt(o));
                le16(er, 16, Block.getIdFromBlock((Block)evBlock.get(o)));
                er[18] = (byte)evId.getInt(o);
                er[19] = (byte)evParam.getInt(o);
                eventsOut.write(er);
            }

            ((List)Array.get(lists, 0)).clear();
            ((List)Array.get(lists, 1)).clear();
            ((java.util.Queue)netQueue.get(net)).clear();

            // ------------------------------------------------- tile entities
            int chestNum = -1, chestPairNum = -1;

            if (kind == K_CHEST || kind == K_CHEST2)
            {
                chestNum = teCount.getInt(ws.getTileEntity(bx, by + 1, bz));

                if (kind == K_CHEST2) chestPairNum = teCount.getInt(ws.getTileEntity(bx + 1, by + 1, bz));
            }

            int potTeItem = -1, potTeData = 0, noteTe = -1, cmpOut = -1, jdItem = -1, jdCount = 0;

            if (kind == K_POT)
            {
                TileEntityFlowerPot te = (TileEntityFlowerPot)ws.getTileEntity(bx, by + 1, bz);
                potTeItem = te.func_145965_a() == null ? -1 : Item.getIdFromItem(te.func_145965_a());
                potTeData = te.func_145966_b();
            }
            else if (kind == K_NOTE)
            {
                TileEntityNote te = (TileEntityNote)ws.getTileEntity(bx, by + 1, bz);
                noteTe = te == null ? -1 : teNote.getByte(te);
            }
            else if (kind == K_JUKEBOX)
            {
                BlockJukebox.TileEntityJukebox te = (BlockJukebox.TileEntityJukebox)ws.getTileEntity(bx, by + 1, bz);
                ItemStack d = te.func_145856_a();
                jdItem = d == null ? -1 : Item.getIdFromItem(d.getItem());
                jdCount = d == null ? 0 : d.stackSize;
            }
            else if (kind == K_CMP_U || kind == K_CMP_P)
            {
                TileEntityComparator te = (TileEntityComparator)ws.getTileEntity(bx, by + 1, bz);
                cmpOut = te.func_145996_a();
            }

            // ------------------------------------------------- scheduled ticks
            int nNew = 0;

            for (Object o : pending)
            {
                NextTickListEntry e2 = (NextTickListEntry)o;
                long tid = tickTime.getLong(e2);

                if (tid >= idBefore)
                {
                    le32(trec, 0, ci);
                    le32(trec, 4, e2.xCoord);
                    le32(trec, 8, e2.yCoord);
                    le32(trec, 12, e2.zCoord);
                    le16(trec, 16, Block.getIdFromBlock((Block)tickBlock.get(e2)));
                    le64(trec, 18, e2.scheduledTime);
                    le32(trec, 26, e2.priority);
                    le64(trec, 30, tid);
                    ticksOut.write(trec);
                    ++nNew;
                }
            }

            // ------------------------------------------------- spawned entities
            List<Entity> spawned = new ArrayList<Entity>(cap.spawned);
            ws.removeWorldAccess(cap);
            cap.spawned.clear();

            for (Entity e : spawned) takeOut(ws, e);

            for (Entity e : spawned)
            {
                if (!(e instanceof EntityItem)) throw new IllegalStateException("activate spawned " + e.getClass().getSimpleName());
                EntityItem ei = (EntityItem)e;
                ItemStack st = ei.getEntityItem();
                byte[] er = new byte[ENT_REC];
                le32(er, 0, ci);
                er[4] = 1;
                le16(er, 5, st.getItem() == null ? 0xffff : Item.getIdFromItem(st.getItem()));
                le16(er, 7, st.getItemDamage());
                le16(er, 9, st.stackSize);
                le16(er, 11, 0);
                le32(er, 13, e.getEntityId());
                entDoubles(er, 17, e);
                entFloat(er, 65, e.rotationYaw);
                entsOut.write(er);
            }

            // ------------------------------------------------- case out
            ItemStack heldAfter = p.inventory.mainInventory[0];
            NBTTagCompound after = new NBTTagCompound();
            fs.writeNBT(after);

            le32(crec, 0, kind);
            le32(crec, 4, tx);
            le32(crec, 8, ty);
            le32(crec, 12, tz);
            crec[16] = (byte)half2;
            crec[17] = (byte)side;
            entFloat(crec, 18, vx);
            entFloat(crec, 22, vy);
            entFloat(crec, 26, vz);
            entFloat(crec, 30, yaw);
            leD(crec, 34, pxx);
            leD(crec, 42, pyy);
            leD(crec, 50, pzz);
            crec[58] = (byte)(sneak ? 1 : 0);
            crec[59] = (byte)day;
            crec[60] = (byte)foodLevel;
            entFloat(crec, 61, foodSat);
            crec[65] = (byte)invFull;
            crec[66] = (byte)meta;
            crec[67] = (byte)bedDir;
            crec[68] = (byte)footOcc;
            crec[69] = (byte)headOcc;
            crec[70] = (byte)potFull;
            le16(crec, 71, potItem);
            crec[73] = (byte)potData;
            crec[74] = (byte)jmeta;
            le16(crec, 75, jdisc);
            crec[77] = (byte)notePitch;
            le16(crec, 78, heldId);
            le16(crec, 80, heldDamage);
            crec[82] = (byte)heldCount;
            le64(crec, 83, caseOpseed);
            crec[91] = (byte)dummyAt;
            crec[92] = (byte)pose;
            crec[93] = (byte)yawKind;
            crec[94] = (byte)chestBlocked;
            crec[95] = 0;
            crec[96] = 0;
            crec[97] = 0;
            crec[98] = 0;
            crec[99] = 0;
            crec[100] = 0;
            crec[101] = 0;
            crec[102] = 0;
            crec[103] = 0;
            casesOut.write(crec);

            orec[0] = (byte)(ret ? 1 : 0);
            orec[1] = (byte)CHAT[0];
            orec[2] = (byte)(opened ? 1 : 0);
            orec[3] = (byte)statUse;
            le16(orec, 4, heldAfter == null ? 0 : Item.getIdFromItem(heldAfter.getItem()));
            le16(orec, 6, heldAfter == null ? 0 : heldAfter.getItemDamage());
            orec[8] = (byte)(heldAfter == null ? 0 : heldAfter.stackSize);
            orec[9] = (byte)after.getInteger("foodLevel");
            le32(orec, 10, Float.floatToRawIntBits(after.getFloat("foodSaturationLevel")));
            le32(orec, 14, Float.floatToRawIntBits(after.getFloat("foodExhaustionLevel")));
            orec[18] = (byte)(p.isPlayerSleeping() ? 1 : 0);
            leD(orec, 19, p.posX);
            leD(orec, 27, p.posY);
            leD(orec, 35, p.posZ);
            le32(orec, 43, chestNum);
            le32(orec, 47, chestPairNum);
            le32(orec, 51, potTeItem);
            le32(orec, 55, potTeData);
            le32(orec, 59, noteTe);
            le32(orec, 63, jdItem);
            le32(orec, 67, jdCount);
            le32(orec, 71, cmpOut);
            le64(orec, 75, Det.state(wsRand));
            le64(orec, 83, Det.state(mathV[Det.OTHER]));
            le64(orec, 91, Det.state(((Random[])seedField.get(null))[Det.OTHER]));
            le32(orec, 99, ((int[])idField.get(null))[Det.OTHER]);
            le64(orec, 103, tickEntryCounter.getLong(null));
            le32(orec, 111, nNew);
            outOut.write(orec);

            for (int s = 0; s < 36; ++s)
            {
                ItemStack st = p.inventory.mainInventory[s];
                le16(irec, s * 5, st == null ? 0 : Item.getIdFromItem(st.getItem()));
                le16(irec, s * 5 + 2, st == null ? 0 : st.getItemDamage());
                irec[s * 5 + 4] = (byte)(st == null ? 0 : st.stackSize);
            }
            invOut.write(irec);

            // ------------------------------------------------- the 3x3 hash
            if ((ci + 1) % 64 == 0 || ci == cases - 1)
            {
                long hv = Probe.hashAround(ws, tx >> 4, tz >> 4, buf);
                le32(hrec, 0, ci);
                le64(hrec, 4, hv);
                hashOut.write(hrec);
            }
        }

        casesOut.close();
        outOut.close();
        invOut.close();
        writesOut.close();
        eventsOut.close();
        ticksOut.close();
        entsOut.close();
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
        m.addProperty("kind", "activate");
        m.addProperty("dim", dim);
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("grid", grid);
        m.addProperty("spacing", spacing);
        m.addProperty("nsites", nSites);
        m.addProperty("cases", cases);
        m.addProperty("opseed", opseed);
        m.addProperty("math_seed", MATH_SEED);
        m.addProperty("total_time", totalTime);
        m.addProperty("next_tick_entry_start", tickEntryStart);
        m.addProperty("det_math_start", mathStart);
        m.addProperty("det_seeder_start", seederStart);
        m.addProperty("det_nextid_start", nextIdStart);
        m.addProperty("player_entity_id", p.getEntityId());
        m.addProperty("dummy_entity_id", dummy.getEntityId());
        m.addProperty("region", "raw chunks, population off, from (" + x0 + "," + z0 + ") to (" + x1 + "," + z1
            + ") in load order; the scatter placed every site's support and first blocks with setBlock flag 2 before start.bin.gz, which is the region the native side loads");
        m.add("loaded", loaded);
        m.addProperty("sites", "grid x grid sites, spacing 3 in x and z: a support block at the terrain surface (the Nether: netherrack at y 64) with the kind's block on it; every site is revisited in the shuffled case order");
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
        m.addProperty("case_layout", "cases.bin, one 104-byte record per case: kind int32 LE, target x/y/z int32 LE each, half uint8 (doors, beds and chest pairs: 0 the lower/foot/left cell, 1 the upper/head/right), side uint8, hit vx/vy/vz float LE, yaw float LE, pose x/y/z double LE, sneak uint8, day uint8 (0 day, 1 night), food_level uint8, food_sat float LE, inventory_full uint8, meta uint8, bed_dir uint8, foot_occupied uint8, head_occupied uint8, pot_full uint8, pot_item uint16 LE, pot_data uint8, jukebox_meta uint8, jukebox_disc uint16 LE, note_pitch uint8, held_item uint16 LE, held_damage uint16 LE, held_count uint8, opseed int64 LE, dummy_sleeping uint8, pose_kind uint8, pad 3");
        m.addProperty("out_layout", "caseout.bin, one 116-byte record per case: return uint8, chat uint8 (0 none, 1 occupied, 2 noSleep, 3 notSafe, 4 other), gui uint8, stat_use uint8, held_item uint16 LE, held_damage uint16 LE, held_count uint8, food_level uint8, food_saturation float LE, food_exhaustion float LE, sleeping uint8, pos x/y/z double LE, chest_num int32 LE, chest_pair_num int32 LE, pot_item int32 LE (-1 none), pot_data int32 LE, note_pitch int32 LE (-1 no tile entity), disc_item int32 LE (-1 none), disc_count int32 LE, comparator_out int32 LE (-1 no tile entity), world-rand state uint64 LE, math state uint64 LE, seeder state uint64 LE, next entity id int32 LE, next tick entry id int64 LE, new tick entries int32 LE");
        m.addProperty("inv_layout", "invs.bin, one 180-byte record per case: the player's 36 main slots after, item uint16 LE, damage uint16 LE, count uint8 each (slot 0 is the held stack)");
        m.addProperty("write_layout", "writes.bin: case int32 LE, x/y/z int32 LE each, id uint16 LE, meta uint8; one row per Rows.onBlock while the call ran (setBlock passes the new id, the metadata path passes 65535); the setup writes are not recorded");
        m.addProperty("event_layout", "events.bin: case int32 LE, x/y/z int32 LE each, block uint16 LE, event id uint8, parameter uint8; one row per queued block event of the case in add order; both queues are cleared after (the drain's receivers have no world effect for these events)");
        m.addProperty("tick_layout", "ticks.bin: case int32 LE, x/y/z int32 LE each, block uint16 LE, time int64 LE, priority int32 LE, entry id int64 LE; one row per tick entry scheduled this case, in tree order; the pending set is never consumed, so the entries accumulate");
        m.addProperty("ent_layout", "ents.bin: case int32 LE, kind uint8 (1 item), item uint16 LE, damage uint16 LE, count uint16 LE, xp uint16 LE, entity id int32 LE, x/y/z and motion x/y/z as double LE, yaw float LE; one row per spawned entity in spawn order");
        m.addProperty("hash_layout", "hash.bin: case int32 LE, hash uint64 LE; one row every 64 cases and the last, the FNV-1a 64 of the 3x3 chunks around the target's chunk");
        m.addProperty("streams", "per case the world Random is seeded to opseed, Math.random (Det.math[OTHER]) to math_seed, and the world time to 1000 (day) or 18000 (night) with calculateSkylightSubtracted(1.0F) after; the seeder, the entity-ID counter and NextTickListEntry.nextTickEntryID continue across cases; det_start records their state after the scatter and the two players' construction; the total time never moves");
        m.addProperty("held_legend", "held pool row: [item id, damage count]; empty hand 0, sapling 6 damage 0..5, flowers 37/38, mushrooms 39/40, dead bush 32, cactus 81, empty bucket 325, water bucket 326, glass bottle 374, record 13 2256, record cat 2257, stone 1; jukebox sites draw from {0, 10, 11, 12}, flower pot sites from {0..7}, cauldron sites from {0, 9, 10}, the rest from the full pool; the stack count is 2 in one case in eight");
        m.addProperty("sneak", "sneaking is drawn only with the empty hand, one case in four of those; sneaking with a held stack skips onBlockActivated and goes to the item half, which is lane itemuse");
        m.addProperty("setup", "per case the site's cells are set back to the drawn state with setBlock flag 2 before the call: the doors' and beds' both halves, the cake, cauldron, trapdoor, gate, lever, button, repeater and comparator metas, the pot's meta and tile entity, the jukebox's meta and disc, the note's tile entity pitch; the case's writes.bin rows are the call's own");
        m.addProperty("gui", "each case closes the GUI it opened (onContainerClosed plus openContainer reset), the way a client closing the window does; the packets queue into a channel-less NetworkManager and are dropped per case");
        m.addProperty("nether", "a dim=-1 run holds bed sites only (the support is netherrack at y 64, both halves re-set per case); the wrong-dimension branch removes both halves and runs newExplosion(null, 5.0F, flaming, smoking), the explosion lane's machine; NOT_SAFE never happens (no mobs) and NOT_POSSIBLE_HERE is unreachable behind canRespawnHere");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", cases);
        res.addProperty("nsites", nSites);
        return res;
    }

    /* ------------------------------------------------------- site placement */

    /** The support cell's y for a site: the terrain surface, lifted above
     * water; the Nether uses a fixed 64 over carved ground. */
    static int siteBase(WorldServer ws, int dim, int x, int z)
    {
        if (dim != 0) return 64;
        int y = Math.min(99, Math.max(56, ws.getHeightValue(x, z)));

        while (ws.getBlock(x, y, z).getMaterial().isLiquid()) ++y;
        return y;
    }

    /** The scatter: the support and the kind's cells, before start.bin.gz. */
    static void setupSite(WorldServer ws, int dim, int kind, int x, int y, int z, int supportId)
    {
        if (dim != 0)
        {
            // carve a pocket: the support and three air cells above it
            ws.setBlock(x, y, z, Block.getBlockById(supportId), 0, 2);

            for (int dy = 1; dy <= 3; ++dy) ws.setBlock(x, y + dy, z, Blocks.air, 0, 2);
        }
        else
        {
            ws.setBlock(x, y, z, Blocks.stone, 0, 2);
        }

        int ty = y + 1;

        switch (kind)
        {
            case K_DOOR_W:
            case K_DOOR_I:
                ws.setBlock(x, ty, z, Block.getBlockById(KINDS[kind][0]), 0, 2);
                ws.setBlock(x, ty + 1, z, Block.getBlockById(kind == K_DOOR_W ? 64 : 71), 8, 2);
                break;
            case K_BED:
                if (dim != 0)
                {
                    ws.setBlock(x + 1, y, z, Block.getBlockById(supportId), 0, 2);
                    ws.setBlock(x + 1, ty, z, Blocks.air, 0, 2);
                }
                ws.setBlock(x, ty, z, Blocks.bed, 0, 2);
                ws.setBlock(x + 1, ty, z, Blocks.bed, 8, 2);
                break;
            case K_CHEST2:
                ws.setBlock(x + 1, y, z, Blocks.stone, 0, 2);
                ws.setBlock(x + 1, ty, z, Blocks.chest, 0, 2);
                ws.setBlock(x, ty, z, Blocks.chest, 0, 2);
                break;
            case K_POT:
            case K_JUKEBOX:
            case K_NOTE:
            case K_CHEST:
                break;
            default:
                ws.setBlock(x, ty, z, Block.getBlockById(KINDS[kind][0]), 0, 2);
        }
    }

    /** One case's setup: the site's cells back to the drawn state, before
     * the call and with the write listener off. */
    static void setupCase(WorldServer ws, int dim, int kind, int bx, int by, int bz, int id, int meta,
        int bedDir, int footOcc, int headOcc, int potFull, int potItem, int potData,
        int jmeta, int jdisc, int notePitch, int chestBlocked, Field teNote, Field teDisc) throws Exception
    {
        int ty = by + 1;

        switch (kind)
        {
            case K_DOOR_W:
            case K_DOOR_I:
                ws.setBlock(bx, ty, bz, Block.getBlockById(id), meta, 2);
                ws.setBlock(bx, ty + 1, bz, Block.getBlockById(id), (meta & 3) | 8, 2);
                break;
            case K_BED:
                if (dim != 0) ws.setBlock(bx, by, bz, Block.getBlockById(87), 0, 2);
                ws.setBlock(bx, ty, bz, Blocks.bed, bedDir | (footOcc != 0 ? 4 : 0), 2);
                ws.setBlock(bx + 1, ty, bz, Blocks.bed, bedDir | 8 | (headOcc != 0 ? 4 : 0), 2);
                break;
            case K_CHEST:
                ws.setBlock(bx, ty, bz, Blocks.chest, 0, 2);
                ws.setBlock(bx, by + 2, bz, chestBlocked == 1 ? Blocks.stone : Blocks.air, 0, 2);
                break;
            case K_CHEST2:
                ws.setBlock(bx, ty, bz, Blocks.chest, 0, 2);
                ws.setBlock(bx + 1, ty, bz, Blocks.chest, 0, 2);
                break;
            case K_POT:
                ws.setBlock(bx, ty, bz, Blocks.flower_pot, potFull == 1 ? potData : 0, 2);
                {
                    TileEntityFlowerPot te = (TileEntityFlowerPot)ws.getTileEntity(bx, ty, bz);
                    te.func_145964_a(potFull == 1 ? Item.getItemById(potItem) : null, potFull == 1 ? potData : 0);
                }
                break;
            case K_JUKEBOX:
                ws.setBlock(bx, ty, bz, Blocks.jukebox, jmeta, 2);
                ((BlockJukebox.TileEntityJukebox)ws.getTileEntity(bx, ty, bz)).func_145857_a(
                    jdisc == 0 ? null : new ItemStack(Item.getItemById(jdisc), 1, 0));
                break;
            case K_NOTE:
                ws.setBlock(bx, ty, bz, Blocks.noteblock, 0, 2);
                teNote.setByte(ws.getTileEntity(bx, ty, bz), (byte)notePitch);
                break;
            default:
                ws.setBlock(bx, ty, bz, Block.getBlockById(id), meta, 2);
            }
    }

    /* ------------------------------------------------------- the case draws */

    static int drawHeld(Random r, int kind)
    {
        int[] pool = kind == K_JUKEBOX ? POOL_JUKEBOX
            : kind == K_POT ? POOL_POT
            : kind == K_CAULDRON ? POOL_CAULDRON : POOL_DEFAULT;
        return pool[r.nextInt(pool.length)];
    }

    /* ------------------------------------------------------------- helpers */

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

    /** Every entity the world creates while the case ran. */
    static final class Capture implements net.minecraft.world.IWorldAccess
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
        public void broadcastSound(int a, int b, int c, int d, int e2) {}
        public void playAuxSFX(EntityPlayer p, int a, int x, int y, int z, int v) {}
        public void destroyBlockPartially(int a, int x, int y, int z, int v) {}
        public void onStaticEntitiesChanged() {}
    }

    static int writeStat(StatFileWriter w, StatBase b) throws Exception
    {
        if (b == null) return 0;
        return (Integer)WRITE_STAT.invoke(w, b);
    }

    static Method WRITE_STAT;

    static
    {
        try
        {
            WRITE_STAT = StatFileWriter.class.getMethod("writeStat", StatBase.class);
        }
        catch (Exception e)
        {
            throw new ExceptionInInitializerError(e);
        }
    }

    static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static void le32(byte[] out, int at, int v)
    {
        out[at] = (byte)v;
        out[at + 1] = (byte)(v >> 8);
        out[at + 2] = (byte)(v >> 16);
        out[at + 3] = (byte)(v >> 24);
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

    static void leD(byte[] out, int at, double d)
    {
        long v = Double.doubleToRawLongBits(d);
        for (int i = 0; i < 8; ++i) out[at + i] = (byte)(v >> (8 * i));
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

    static void writeLe32(OutputStream out, int v) throws IOException
    {
        out.write(v & 255);
        out.write(v >> 8 & 255);
        out.write(v >> 16 & 255);
        out.write(v >> 24 & 255);
    }
}