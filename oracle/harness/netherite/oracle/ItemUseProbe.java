package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.world.NextTickListEntry;
import net.minecraft.world.WorldServer;

/**
 * Reference for the native port of item use: ItemInWorldManager.tryUseItem and
 * activateBlockOrUseItem's item half, and the onItemUse / onItemRightClick
 * bodies of ItemBucket, ItemDye (bonemeal and cocoa), ItemFlintAndSteel,
 * ItemFireball, ItemLilyPad, ItemGlassBottle and ItemEnderEye.
 *
 * The world is raw terrain (Probe.rawChunks) around a fixed chunk; a scene is
 * a small block arrangement around an anchor, rebuilt with setBlock(flag 2)
 * before every case and recorded once per (scene, variant) as anchor-relative
 * ops. The probe player is the integrated server's own EntityPlayerMP, posed
 * per case: position, yaw/pitch, capabilities/game type and the held stack.
 * A case drives the use exactly as NetHandlerPlayServer does: side 255 is a
 * right click in the air (tryUseItem, so onItemRightClick), any other side is
 * activateBlockOrUseItem (block activation first, then the item's onItemUse);
 * in creative the stack's count and damage are restored around the call, as
 * activateBlockOrUseItem and tryUseItem do.
 *
 * Every write (Rows.onBlock, in call order), the return value, the held stack
 * after (the post-null rule processPlayerBlockPlacement applies), the pending
 * scheduled-tick set after the case and World.rand's 48-bit state after the
 * case are recorded. Entity spawns are not: no case reaches one (the player's
 * inventory has room, no eye of ender flies). Item.itemRand's draws (sound
 * pitch, particles) write no world state and are not recorded.
 *
 * Runs on its own thread (the OTHER role) while the server is parked, so no
 * CLIENT or SERVER RNG stream moves.
 *
 * Output DIR/manifest.json, DIR/scenes.bin, DIR/cases.bin, DIR/writes.bin,
 * DIR/sched.bin. See the layout strings in the manifest.
 */
public final class ItemUseProbe
{
    private ItemUseProbe() {}

    // ---- items under test ---------------------------------------------------
    static final int I_BUCKET = 0, I_WATER = 1, I_LAVA = 2, I_LILY = 3, I_BOTTLE = 4;
    static final int I_DYE15 = 5, I_DYE3 = 6, I_FLINT = 7, I_FIREBALL = 8, I_EYE = 9;
    static final int N_ITEMS = 10;
    static final String[] ITEM_NAME = {"bucket", "water_bucket", "lava_bucket", "waterlily",
        "glass_bottle", "dye_bonemeal", "dye_cocoa", "flint_and_steel", "fire_charge", "ender_eye"};
    /** 0 = onItemUse (a block side), 1 = onItemRightClick (side 255). */
    static final int[] ITEM_PATH = {1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
    static int[] itemId = new int[N_ITEMS];
    static int[] itemDamage = new int[N_ITEMS];

    // ---- scenes -------------------------------------------------------------
    static final int S_FLOOR = 0, S_NETHER_FLOOR = 1, S_GRASS = 2, S_SAPLING = 3, S_CROPS = 4,
        S_STEM = 5, S_COCOA = 6, S_MUSHROOM = 7, S_LOG = 8, S_WATER = 9, S_LAVA = 10,
        S_NETHERRACK = 11, S_PORTAL = 12, S_ENDFRAME = 13;
    static final int N_SCENES = 14;
    static final String[] SCENE_NAME = {"floor", "nether_floor", "grass", "sapling", "crops",
        "stem", "cocoa", "mushroom", "log", "water", "lava", "netherrack", "portal", "endframe"};
    static final int[] SCENE_VARIANTS = {5, 1, 3, 12, 8, 16, 12, 6, 3, 2, 2, 1, 5, 4};
    static final int[] SCENE_DIM = {0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static final int[][] SCENE_ITEMS = {
        {I_BUCKET, I_WATER, I_LAVA, I_DYE15, I_DYE3, I_FLINT, I_FIREBALL},
        {I_WATER, I_LAVA, I_BUCKET, I_FLINT},
        {I_DYE15, I_FLINT, I_FIREBALL, I_BUCKET, I_WATER},
        {I_DYE15},
        {I_DYE15, I_DYE3},
        {I_DYE15},
        {I_DYE15},
        {I_DYE15},
        {I_DYE3, I_DYE15},
        {I_BUCKET, I_WATER, I_LAVA, I_LILY, I_BOTTLE},
        {I_BUCKET, I_WATER, I_LAVA},
        {I_FLINT, I_FIREBALL, I_DYE15, I_DYE3},
        {I_FLINT, I_FIREBALL},
        {I_EYE},
    };

    static final int TOTAL_SCENE_VARIANTS = 80;
    static final int SLOTS = 84;

    // ---- runtime state ------------------------------------------------------
    static final class SceneOps
    {
        final java.util.List<byte[]> ops = new java.util.ArrayList<byte[]>();
        boolean recorded;
    }

    static SceneOps[][] ops;
    static boolean recording;
    static DataOutputStream sink;
    static int writes;

    static boolean[] sceneBuilt;
    static java.util.Map<Long, Integer>[] sceneBaseBlocks;
    static java.util.Map<Long, Integer>[] sceneChangedBlocks;
    static int activeSlot = -1;
    static int activeAx, activeAy, activeAz;

    static long pack(int dx, int dy, int dz)
    {
        return (((long)(dx + 32768) & 0xffffL) << 32)
             | (((long)(dy + 32768) & 0xffffL) << 16)
             | (((long)(dz + 32768) & 0xffffL));
    }

    static int unpackDx(long k)
    {
        return (int)((k >> 32) & 0xffffL) - 32768;
    }

    static int unpackDy(long k)
    {
        return (int)((k >> 16) & 0xffffL) - 32768;
    }

    static int unpackDz(long k)
    {
        return (int)(k & 0xffffL) - 32768;
    }

    static int slotFor(int scene, int variant)
    {
        int idx = 0;
        for (int s = 0; s < scene; ++s) idx += SCENE_VARIANTS[s];
        return idx + variant;
    }

    static int anchorX(int cx, int radius, int slot)
    {
        return (cx - radius) * 16 + (slot % 7) * 18;
    }

    static int anchorZ(int cz, int radius, int slot)
    {
        return (cz - radius) * 16 + ((slot / 7) % 6) * 18;
    }

    static int anchorY(int slot)
    {
        return slot >= 42 ? 140 : 70;
    }

    static Field field(Class<?> c, String name)
    {
        try
        {
            Field f = c.getDeclaredField(name);
            f.setAccessible(true);
            return f;
        }
        catch (Exception e)
        {
            throw new RuntimeException(c + "." + name, e);
        }
    }

    /** InventoryPlayer.itemStack, the stack getHeldItem() returns. */
    static final Field FIELD_ITEM_STACK = field(net.minecraft.entity.player.InventoryPlayer.class, "itemStack");
    static final Field FIELD_GAMETYPE = field(net.minecraft.server.management.ItemInWorldManager.class, "gameType");
    static final Field FIELD_PENDING_SET = field(WorldServer.class, "pendingTickListEntriesTreeSet");
    static final Field FIELD_PENDING_HASH = field(WorldServer.class, "pendingTickListEntriesHashSet");
    static final Field FIELD_ENTRY_ID = field(NextTickListEntry.class, "tickEntryID");
    static final Field FIELD_NEXT_ID = field(NextTickListEntry.class, "nextTickEntryID");
    static final Field FIELD_RAND_SEED = field(Random.class, "seed");

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
                catch (Throwable e)
                {
                    error[0] = e instanceof Exception ? (Exception)e : new RuntimeException(e);
                }
            }
        }, "Oracle ItemUseProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 2000;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 2000;
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 3;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 1;
        int cases = cmd.has("cases") ? cmd.get("cases").getAsInt() : 20000;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 1L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        WorldServer overworld = server.worldServers[0];
        WorldServer nether = worldFor(server, -1);
        long seed = overworld.getSeed();
        Probe.rawChunks = true;

        int x0 = cx - radius - ring, x1 = cx + radius + ring;
        int z0 = cz - radius - ring, z1 = cz + radius + ring;

        for (int lx = x0; lx <= x1; ++lx)
            for (int lz = z0; lz <= z1; ++lz)
                if (overworld.getChunkFromChunkCoords(lx, lz) == null)
                    throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");

        if (nether != null)
            for (int lx = x0; lx <= x1; ++lx)
                for (int lz = z0; lz <= z1; ++lz)
                    nether.getChunkFromChunkCoords(lx, lz);

        net.minecraft.entity.player.EntityPlayerMP p =
            (net.minecraft.entity.player.EntityPlayerMP)server.getConfigurationManager().playerEntityList.get(0);
        net.minecraft.server.management.ItemInWorldManager mgr = p.theItemInWorldManager;

        initItems();
        ops = new SceneOps[N_SCENES][];

        for (int s = 0; s < N_SCENES; ++s)
        {
            ops[s] = new SceneOps[SCENE_VARIANTS[s]];
            for (int v = 0; v < SCENE_VARIANTS[s]; ++v) ops[s][v] = new SceneOps();
        }

        sceneBuilt = new boolean[TOTAL_SCENE_VARIANTS];
        sceneBaseBlocks = new java.util.Map[TOTAL_SCENE_VARIANTS];
        sceneChangedBlocks = new java.util.Map[TOTAL_SCENE_VARIANTS];
        for (int i = 0; i < TOTAL_SCENE_VARIANTS; ++i)
        {
            sceneBaseBlocks[i] = new java.util.HashMap<Long, Integer>();
            sceneChangedBlocks[i] = new java.util.LinkedHashMap<Long, Integer>();
        }
        activeSlot = -1;

        OutputStream scenesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "scenes.bin")), 1 << 16);
        DataOutputStream casesOut = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "cases.bin")), 1 << 16));
        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        DataOutputStream schedOut = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "sched.bin")), 1 << 16));

        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(net.minecraft.world.World w, int x, int y, int z, int id, int meta)
            {
                if (sink == null) return;
                try
                {
                    sink.writeInt(x);
                    sink.writeInt(y);
                    sink.writeInt(z);
                    sink.writeShort(id);
                    sink.writeByte(meta);
                }
                catch (Exception e)
                {
                    throw new RuntimeException("itemuse write", e);
                }
                ++writes;

                if (activeSlot >= 0)
                {
                    int dx = x - activeAx;
                    int dy = y - activeAy;
                    int dz = z - activeAz;
                    long k = pack(dx, dy, dz);
                    if (!sceneChangedBlocks[activeSlot].containsKey(k))
                    {
                        Integer orig = sceneBaseBlocks[activeSlot].get(k);
                        if (orig == null) orig = 0;
                        sceneChangedBlocks[activeSlot].put(k, orig);
                    }
                }
            }
        };

        Random r = new Random(opseed);
        int totalWrites = 0, totalSched = 0;
        int effects = 0, retTrue = 0;

        try
        {
            for (int i = 0; i < cases; ++i)
            {
                int si = r.nextInt(N_SCENES);
                int variant = r.nextInt(SCENE_VARIANTS[si]);
                int[] its = SCENE_ITEMS[si];
                int ii = its[r.nextInt(its.length)];
                int dim = SCENE_DIM[si];
                WorldServer ws = dim == -1 ? nether : overworld;

                if (ws == null) { --i; continue; }

                // creative 1 in 4, adventure (allowEdit false) 1 in 8; the held damage
                // and count, and for a block use the side and the hit offsets
                boolean creative = r.nextInt(4) == 0;
                boolean adventure = r.nextInt(8) == 0;
                int heldDamage = ITEM_PATH[ii] == 1 && (ii == I_DYE15 || ii == I_DYE3) ? itemDamage[ii]
                    : (itemDamage[ii] + (ii == I_FLINT ? r.nextInt(5) : 0));
                int heldCount = 1 + r.nextInt(3);
                int side = ITEM_PATH[ii] == 1 ? 255 : r.nextInt(6);
                float hx = r.nextInt(16) / 16.0F, hy = r.nextInt(16) / 16.0F, hz = r.nextInt(16) / 16.0F;
                long caseSeed = r.nextLong();
                long totalTime = 1L;

                int slot = slotFor(si, variant);
                int ax = anchorX(cx, radius, slot);
                int az = anchorZ(cz, radius, slot);
                int ay = anchorY(slot);

                SceneOps so = ops[si][variant];
                Build b = new Build(ax, ay, az, so, slot);

                if (!sceneBuilt[slot])
                {
                    sceneBuilt[slot] = true;
                    recording = true;
                    scene(b, ws, si, variant);
                    recording = false;

                    byte[] head = new byte[4];
                    le32(head, 0, so.ops.size());
                    scenesOut.write(head);
                    for (byte[] o : so.ops) scenesOut.write(o);
                }
                else
                {
                    java.util.List<byte[]> restoreOps = new java.util.ArrayList<byte[]>();
                    for (java.util.Map.Entry<Long, Integer> entry : sceneChangedBlocks[slot].entrySet())
                    {
                        long k = entry.getKey();
                        int orig = entry.getValue();
                        int dx = unpackDx(k);
                        int dy = unpackDy(k);
                        int dz = unpackDz(k);
                        int origId = orig & 0xffff;
                        int origMeta = (orig >> 16) & 0xff;

                        int curId = Block.getIdFromBlock(ws.getBlock(ax + dx, ay + dy, az + dz));
                        int curMeta = ws.getBlockMetadata(ax + dx, ay + dy, az + dz);

                        if (curId != origId || curMeta != origMeta)
                        {
                            ws.setBlock(ax + dx, ay + dy, az + dz, Block.getBlockById(origId), origMeta, 2);
                            byte[] rop = new byte[7];
                            rop[0] = (byte)dx;
                            rop[1] = (byte)dy;
                            rop[2] = (byte)dz;
                            rop[3] = (byte)origId;
                            rop[4] = (byte)(origId >> 8);
                            rop[5] = (byte)origMeta;
                            restoreOps.add(rop);
                        }
                    }
                    sceneChangedBlocks[slot].clear();

                    byte[] head = new byte[4];
                    le32(head, 0, restoreOps.size());
                    scenesOut.write(head);
                    for (byte[] o : restoreOps) scenesOut.write(o);
                }

                // the case's starting state
                ((java.util.Set)FIELD_PENDING_SET.get(ws)).clear();
                ((java.util.Set)FIELD_PENDING_HASH.get(ws)).clear();
                ws.getWorldInfo().incrementTotalWorldTime(totalTime);
                long entryBefore = FIELD_NEXT_ID.getLong(null);

                net.minecraft.entity.player.InventoryPlayer inv = p.inventory;
                for (int k = 0; k < 9; ++k) inv.mainInventory[k] = null;
                inv.currentItem = 0;
                ItemStack held = new ItemStack(net.minecraft.item.Item.getItemById(itemId[ii]), heldCount, heldDamage);
                inv.mainInventory[0] = held;
                FIELD_ITEM_STACK.set(inv, held);
                p.capabilities.allowEdit = !adventure;
                p.capabilities.isCreativeMode = creative;
                FIELD_GAMETYPE.set(mgr, creative ? net.minecraft.world.WorldSettings.GameType.CREATIVE : net.minecraft.world.WorldSettings.GameType.SURVIVAL);

                int tx = ax, ty = ay, tz = az;
                if (ITEM_PATH[ii] == 1) aim(ws, p, ax, ay, az, r);

                ws.rand.setSeed(caseSeed);
                int w0 = writes;
                sink = new DataOutputStream(writesOut);
                activeSlot = slot;
                activeAx = ax;
                activeAy = ay;
                activeAz = az;
                boolean out;
                try
                {
                    if (ITEM_PATH[ii] == 1)
                        out = mgr.tryUseItem(p, ws, held);
                    else
                        out = mgr.activateBlockOrUseItem(p, ws, held, tx, ty, tz, side, hx, hy, hz);
                }
                finally
                {
                    sink.flush();
                    sink = null;
                    activeSlot = -1;
                }
                int nw = writes - w0;

                // held stack after, with processPlayerBlockPlacement's post-null rule
                ItemStack after = inv.mainInventory[0];
                int outId = 0, outCount = 0, outDamage = 0;
                if (after != null)
                {
                    if (after.stackSize == 0) inv.mainInventory[0] = null;
                    else
                    {
                        outId = net.minecraft.item.Item.getIdFromItem(after.getItem());
                        outCount = after.stackSize;
                        outDamage = after.getItemDamage();
                    }
                }
                if (out) ++retTrue;
                if (nw > 0) ++effects;

                java.util.List<NextTickListEntry> sched = new java.util.ArrayList<NextTickListEntry>();
                for (Object o : (java.util.Set)FIELD_PENDING_SET.get(ws)) sched.add((NextTickListEntry)o);
                for (NextTickListEntry e : sched)
                {
                    schedOut.writeInt(e.xCoord);
                    schedOut.writeInt(e.yCoord);
                    schedOut.writeInt(e.zCoord);
                    schedOut.writeShort(Block.getIdFromBlock(e.func_151351_a()));
                    schedOut.writeLong(e.scheduledTime);
                    schedOut.writeInt(e.priority);
                    schedOut.writeLong(FIELD_ENTRY_ID.getLong(e));
                }
                totalSched += sched.size();

                casesOut.writeShort(si);
                casesOut.writeShort(variant);
                casesOut.writeByte(dim);
                casesOut.writeByte(side);
                casesOut.writeShort(itemId[ii]);
                casesOut.writeByte(heldCount);
                casesOut.writeShort(heldDamage);
                casesOut.writeInt(tx);
                casesOut.writeInt(ty);
                casesOut.writeInt(tz);
                casesOut.writeFloat(hx);
                casesOut.writeFloat(hy);
                casesOut.writeFloat(hz);
                casesOut.writeDouble(p.posX);
                casesOut.writeDouble(p.posY);
                casesOut.writeDouble(p.posZ);
                casesOut.writeFloat(p.rotationYaw);
                casesOut.writeFloat(p.rotationPitch);
                casesOut.writeByte(adventure ? 0 : 1);
                casesOut.writeByte(creative ? 1 : 0);
                casesOut.writeLong(totalTime);
                casesOut.writeLong(entryBefore);
                casesOut.writeLong(caseSeed);
                casesOut.writeByte(out ? 1 : 0);
                casesOut.writeShort(outId);
                casesOut.writeByte(outCount);
                casesOut.writeShort(outDamage);
                casesOut.writeLong(randState(ws.rand));
                casesOut.writeInt(nw);
                casesOut.writeInt(sched.size());

                totalWrites += nw;

            }
        }
        finally
        {
            Rows.writeListener = null;
            scenesOut.close();
            casesOut.close();
            writesOut.close();
            schedOut.close();
        }

        // restore the player and clear the anchor of the last case
        p.inventory.mainInventory[0] = null;
        p.inventory.currentItem = 0;
        FIELD_ITEM_STACK.set(p.inventory, null);
        p.capabilities.allowEdit = true;
        p.capabilities.isCreativeMode = false;

        JsonArray sceneList = new JsonArray();
        for (int s = 0; s < N_SCENES; ++s)
        {
            JsonObject o = new JsonObject();
            o.addProperty("name", SCENE_NAME[s]);
            o.addProperty("variants", SCENE_VARIANTS[s]);
            o.addProperty("dim", SCENE_DIM[s]);
            JsonArray its = new JsonArray();
            for (int k : SCENE_ITEMS[s]) its.add(new JsonPrimitive(ITEM_NAME[k]));
            o.add("items", its);
            sceneList.add(o);
        }

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "itemuse");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("cases", cases);
        m.addProperty("opseed", opseed);
        m.addProperty("writes", totalWrites);
        m.addProperty("scheduled", totalSched);
        m.addProperty("effects", effects);
        m.addProperty("returns_true", retTrue);
        m.add("items", itemList());
        m.add("scenes", sceneList);
        m.addProperty("anchors", "84 slots: 7 columns x 6 rows of stride 18 starting at (cx-radius)*16, y 70 for slots "
            + "0..41 and y 140 for 42..83; each (scene, variant) has a dedicated slot");
        m.addProperty("scenes_layout", "scenes.bin: per case: op count uint32 LE, then each op 7 bytes: dx int8, dy int8, dz int8, "
            + "id uint16 LE, meta uint8, pad; every op is World.setBlock(anchor + offset, id, meta, 2)");
        m.addProperty("case_layout", "cases.bin, big-endian, per case: scene u16, variant u16, dim i8, side u8 (255 = air "
            + "right click), held item id u16, held count u8, held damage u16, target x i32, y i32, z i32, hit x f32, y f32, "
            + "z f32, player posX f64, posY f64, posZ f64, yaw f32, pitch f32, allow_edit u8, creative u8, world total time "
            + "i64, entry id before i64, world rand seed i64, return u8, out item id u16, out count u8, out damage u16, "
            + "world rand state after i64, write count u32, scheduled count u32");
        m.addProperty("write_layout", "writes.bin, big-endian, per write: x i32, y i32, z i32, id i16 (-1 for a "
            + "metadata-only write, id 0 for air), meta u8");
        m.addProperty("sched_layout", "sched.bin, big-endian, per scheduled entry in tree order: x i32, y i32, z i32, "
            + "block u16, scheduledTime i64, priority i32, entry id i64");
        m.addProperty("driving", "side 255: ItemInWorldManager.tryUseItem (ItemStack.useItemRightClick); other sides: "
            + "activateBlockOrUseItem (Block.onBlockActivated, then ItemStack.tryPlaceItemIntoWorld), the same calls "
            + "NetHandlerPlayServer.processPlayerBlockPlacement makes. The post-null rule (a held stack of size 0 becomes "
            + "null) is applied as processPlayerBlockPlacement does");
        m.addProperty("player", "the integrated server's own EntityPlayerMP: main inventory slots 0..8 cleared, slot 0 "
            + "the held stack (count, damage drawn), currentItem 0, the itemStack field set; capabilities.allowEdit from "
            + "the adventure draw and isCreativeMode from the creative draw, the manager's gameType set to match");
        m.addProperty("ray", "a side-255 case poses the player so the look ray (5 long, eye at posY + 1.62 - yOffset, "
            + "yOffset 0) points at the anchor from a random offset 2..3 blocks away in x/z at eye height anchor + 1.5, "
            + "with up to 5 degrees of jitter; the ray's target and side are whatever func_147447_a returns");
        m.addProperty("rand", "the recorded 48-bit state is java.util.Random's internal seed after the case. Item.itemRand "
            + "(sound pitch, particles) draws spend no world state and are not recorded; no case spawns an entity");
        PrintWriter w = new PrintWriter(new FileWriter(new File(dir, "manifest.json")));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("cases", cases);
        res.addProperty("writes", totalWrites);
        res.addProperty("scheduled", totalSched);
        res.addProperty("effects", effects);
        return res;
    }

    static WorldServer worldFor(IntegratedServer server, int dim)
    {
        for (WorldServer w : server.worldServers) if (w != null && w.provider.dimensionId == dim) return w;
        return null;
    }

    static JsonArray itemList()
    {
        JsonArray a = new JsonArray();
        for (int i = 0; i < N_ITEMS; ++i)
        {
            JsonObject o = new JsonObject();
            o.addProperty("name", ITEM_NAME[i]);
            o.addProperty("id", itemId[i]);
            o.addProperty("damage", itemDamage[i]);
            o.addProperty("path", ITEM_PATH[i] == 1 ? "onItemRightClick" : "onItemUse");
            a.add(o);
        }
        return a;
    }

    static void initItems()
    {
        itemId[I_BUCKET] = id(Items.bucket);
        itemId[I_WATER] = id(Items.water_bucket);
        itemId[I_LAVA] = id(Items.lava_bucket);
        itemId[I_LILY] = id(net.minecraft.item.Item.getItemFromBlock(Blocks.waterlily));
        itemId[I_BOTTLE] = id(Items.glass_bottle);
        itemId[I_DYE15] = id(Items.dye);
        itemId[I_DYE3] = id(Items.dye);
        itemId[I_FLINT] = id(Items.flint_and_steel);
        itemId[I_FIREBALL] = id(Items.fire_charge);
        itemId[I_EYE] = id(Items.ender_eye);
        itemDamage[I_DYE15] = 15;
        itemDamage[I_DYE3] = 3;
    }

    static int id(net.minecraft.item.Item it)
    {
        return it == null ? 0 : net.minecraft.item.Item.getIdFromItem(it);
    }



    /** The case's look ray: the eye at the anchor + (dx, 1.5, dz) looking at the anchor centre with jitter. */
    static void aim(WorldServer ws, net.minecraft.entity.player.EntityPlayerMP p, int ax, int ay, int az, Random r)
    {
        int dx, dz;
        do
        {
            dx = r.nextInt(7) - 3;
            dz = r.nextInt(7) - 3;
        }
        while (Math.abs(dx) + Math.abs(dz) < 2);

        double ex = ax + 0.5 + dx, ey = ay + 2.5, ez = az + 0.5 + dz;
        double tx = ax + 0.5, ty = ay + 0.5, tz = az + 0.5;
        double ddx = tx - ex, ddy = ty - ey, ddz = tz - ez;
        double dist = Math.sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
        double horiz = Math.sqrt(ddx * ddx + ddz * ddz);
        float pitch = (float)(-Math.asin(ddy / dist) * 180.0D / Math.PI);
        float yaw = (float)(Math.atan2(-ddx, ddz) * 180.0D / Math.PI);
        yaw += (r.nextInt(11) - 5);
        pitch += (r.nextInt(11) - 5);

        p.setPosition(ex, ey - 1.62D, ez);
        p.rotationYaw = yaw;
        p.rotationPitch = pitch;
        p.prevRotationYaw = yaw;
        p.prevRotationPitch = pitch;
        p.prevPosX = ex;
        p.prevPosY = ey - 1.62D;
        p.prevPosZ = ez;
    }

    // ---- scene builders -----------------------------------------------------

    static void set(WorldServer ws, Build b, int dx, int dy, int dz, Block block, int meta)
    {
        ws.setBlock(b.ax + dx, b.ay + dy, b.az + dz, block, meta, 2);

        if (recording)
        {
            int id = Block.getIdFromBlock(block);
            byte[] o = new byte[7];
            o[0] = (byte)dx;
            o[1] = (byte)dy;
            o[2] = (byte)dz;
            o[3] = (byte)id;
            o[4] = (byte)(id >> 8);
            o[5] = (byte)meta;
            b.so.ops.add(o);

            long k = pack(dx, dy, dz);
            int v = (id & 0xffff) | ((meta & 0xff) << 16);
            sceneBaseBlocks[b.slot].put(k, v);
        }
    }

    static final class Build
    {
        final int ax, ay, az;
        final SceneOps so;
        final int slot;

        Build(int ax, int ay, int az, SceneOps so, int slot)
        {
            this.ax = ax;
            this.ay = ay;
            this.az = az;
            this.so = so;
            this.slot = slot;
        }
    }

    static void clear(WorldServer ws, Build b, int hx, int yBot, int yTop)
    {
        for (int dy = yBot; dy <= yTop; ++dy)
            for (int dz = -hx; dz <= hx; ++dz)
                for (int dx = -hx; dx <= hx; ++dx) set(ws, b, dx, dy, dz, Blocks.air, 0);
    }

    static void floor(WorldServer ws, Build b, int hx, int y, Block block, int meta)
    {
        for (int dz = -hx; dz <= hx; ++dz)
            for (int dx = -hx; dx <= hx; ++dx) set(ws, b, dx, y, dz, block, meta);
    }

    static final Block[] FLOOR_BLOCKS = {Blocks.stone, Blocks.dirt, Blocks.planks, Blocks.cobblestone, Blocks.obsidian};

    static void scene(Build b, WorldServer ws, int scene, int variant)
    {
        switch (scene)
        {
        case S_FLOOR:
        {
            clear(ws, b, 3, -1, 6);
            Block f = FLOOR_BLOCKS[variant % FLOOR_BLOCKS.length];
            floor(ws, b, 3, -1, f, 0);
            set(ws, b, 0, 0, 0, f, 0);
            break;
        }

        case S_NETHER_FLOOR:
        {
            clear(ws, b, 3, -1, 6);
            floor(ws, b, 3, -1, Blocks.netherrack, 0);
            set(ws, b, 0, 0, 0, Blocks.netherrack, 0);
            break;
        }

        case S_GRASS:
        {
            clear(ws, b, 8, -2, 4);
            floor(ws, b, 8, -2, Blocks.dirt, 0);
            floor(ws, b, 8, -1, Blocks.grass, 0);
            set(ws, b, 0, 0, 0, Blocks.grass, 0);

            if (variant == 1)
            {
                set(ws, b, 2, 0, 2, Blocks.tallgrass, 1);
                set(ws, b, -3, 0, 1, Blocks.red_flower, 0);
                set(ws, b, 1, 0, -3, Blocks.yellow_flower, 0);
            }
            else if (variant == 2)
            {
                floor(ws, b, 3, 3, Blocks.stone, 0);
            }
            break;
        }

        case S_SAPLING:
        {
            clear(ws, b, 4, -3, 34);
            floor(ws, b, 4, -1, Blocks.dirt, 0);
            int type = variant / 2;
            int grown = variant % 2;
            set(ws, b, 0, 0, 0, Blocks.sapling, type | (grown << 3));
            break;
        }

        case S_CROPS:
        {
            clear(ws, b, 3, -1, 4);
            floor(ws, b, 3, -1, Blocks.farmland, 1);
            set(ws, b, 0, 0, 0, Blocks.wheat, variant);
            break;
        }

        case S_STEM:
        {
            clear(ws, b, 3, -1, 3);
            floor(ws, b, 3, -1, Blocks.farmland, 1);
            Block stem = (variant / 8) == 0 ? Blocks.pumpkin_stem : Blocks.melon_stem;
            set(ws, b, 0, 0, 0, stem, variant % 8);
            break;
        }

        case S_COCOA:
        {
            clear(ws, b, 3, -1, 3);
            floor(ws, b, 3, -1, Blocks.dirt, 0);
            int dir = variant / 3;
            int stage = variant % 3;
            set(ws, b, 0, 0, 0, Blocks.cocoa, (stage << 2) | dir);
            set(ws, b, -DCOCOA_X[dir], 0, -DCOCOA_Z[dir], Blocks.log, 3);
            break;
        }

        case S_MUSHROOM:
        {
            int type = variant / 3, shape = variant % 3;
            clear(ws, b, 4, -1, 10);
            floor(ws, b, 4, -1, Blocks.dirt, 0);
            set(ws, b, 0, 0, 0, type == 0 ? Blocks.brown_mushroom : Blocks.red_mushroom, 0);

            if (shape == 1)
                floor(ws, b, 2, 8, Blocks.stone, 0);
            else if (shape == 2)
            {
                for (int dz = -2; dz <= 2; ++dz)
                {
                    set(ws, b, 2, 1, dz, Blocks.stone, 0);
                    set(ws, b, -2, 1, dz, Blocks.stone, 0);
                    set(ws, b, dz, 1, 2, Blocks.stone, 0);
                    set(ws, b, dz, 1, -2, Blocks.stone, 0);
                }
            }
            break;
        }

        case S_LOG:
        {
            clear(ws, b, 3, -1, 3);
            floor(ws, b, 3, -1, Blocks.dirt, 0);
            set(ws, b, 0, 0, 0, Blocks.log, variant == 1 ? 0 : 3);
            if (variant == 2) set(ws, b, 0, 0, -1, Blocks.stone, 0);
            break;
        }

        case S_WATER:
        {
            clear(ws, b, 3, -1, 3);
            floor(ws, b, 3, -1, Blocks.stone, 0);
            if (variant == 1) ring(ws, b, 2, 0, 1, Blocks.stone);
            set(ws, b, 0, 0, 0, Blocks.water, 0);
            break;
        }

        case S_LAVA:
        {
            clear(ws, b, 3, -1, 3);
            floor(ws, b, 3, -1, Blocks.stone, 0);
            if (variant == 1) ring(ws, b, 2, 0, 1, Blocks.stone);
            set(ws, b, 0, 0, 0, Blocks.lava, 0);
            break;
        }

        case S_NETHERRACK:
        {
            clear(ws, b, 4, -1, 6);
            floor(ws, b, 4, -1, Blocks.netherrack, 0);
            set(ws, b, 0, 0, 0, Blocks.netherrack, 0);
            break;
        }

        case S_PORTAL:
            portalScene(b, ws, variant);
            break;

        case S_ENDFRAME:
            endframeScene(b, ws, variant);
            break;

        default:
            throw new IllegalStateException("scene " + scene);
        }
    }

    /** A one-thick wall ring at layer y, from radius r outward. */
    static void ring(WorldServer ws, Build b, int r, int y, int thick, Block block)
    {
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx)
                if (Math.abs(dx) == r || Math.abs(dz) == r)
                    for (int t = 0; t < thick; ++t) set(ws, b, dx, y + t, dz, block, 0);
    }

    static final int[] DCOCOA_X = {0, -1, 0, 1};
    static final int[] DCOCOA_Z = {1, 0, -1, 0};

    /**
     * A nether portal frame. Variants 0 and 3..4 are x-width planes (portal
     * meta 1), variant 1 is a z-width plane (meta 2), variant 2 misses a
     * bottom obsidian. (0, 0, 0) is the bottom obsidian cell the flint clicks;
     * the interior is at y 1..g.
     */
    static void portalScene(Build b, WorldServer ws, int variant)
    {
        clear(ws, b, 4, -1, 8);
        floor(ws, b, 4, -1, Blocks.stone, 0);

        if (variant == 1)
        {
            // z-width: interior z in {-1, 0}, y 1..3
            for (int y = 0; y <= 4; ++y)
            {
                set(ws, b, 0, y, -2, Blocks.obsidian, 0);
                set(ws, b, 0, y, 1, Blocks.obsidian, 0);
            }
            for (int z = -2; z <= 1; ++z)
            {
                set(ws, b, 0, 0, z, Blocks.obsidian, 0);
                set(ws, b, 0, 4, z, Blocks.obsidian, 0);
            }
            return;
        }

        int w = variant == 4 ? 1 : 2;    // interior x in {-(w-1) .. 0}
        int xa = -(w - 1);
        for (int y = 0; y <= 4; ++y)
        {
            set(ws, b, xa - 1, y, 0, Blocks.obsidian, 0);
            set(ws, b, 1, y, 0, Blocks.obsidian, 0);
        }
        for (int x = xa - 1; x <= 1; ++x)
        {
            set(ws, b, x, 0, 0, Blocks.obsidian, 0);
            set(ws, b, x, 4, 0, Blocks.obsidian, 0);
        }
        if (variant == 2) set(ws, b, xa, 0, 0, Blocks.stone, 0);      // bottom gap
        if (variant == 3)
        {
            set(ws, b, 0, 2, 0, Blocks.portal, 1);
            set(ws, b, 0, 3, 0, Blocks.portal, 1);
        }
    }

    /** An end portal frame ring: (|x| == 2, |z| <= 1) or (|z| == 2, |x| <= 1). */
    static void endframeScene(Build b, WorldServer ws, int variant)
    {
        clear(ws, b, 4, -1, 3);
        floor(ws, b, 4, -1, Blocks.end_stone, 0);

        for (int dz = -2; dz <= 2; ++dz)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                boolean fx = Math.abs(dx) == 2 && Math.abs(dz) <= 1;
                boolean fz = Math.abs(dz) == 2 && Math.abs(dx) <= 1;
                if (!fx && !fz) continue;
                if (variant == 2 && dx == -2 && dz == 1) continue;    // a missing ring frame
                int meta = 4;                                          // has an eye
                if (variant == 1 && dx == 2 && dz == 0) meta = 0;      // one missing eye
                if (dx == -2 && dz == 0) meta = variant == 3 ? 7 : 3;  // the clicked frame, at the anchor
                set(ws, b, dx + 2, 0, dz, Blocks.end_portal_frame, meta);
            }
        }
    }

    // ---- little io helpers --------------------------------------------------

    static long randState(Random r) throws Exception
    {
        Object seed = FIELD_RAND_SEED.get(r);
        return ((java.util.concurrent.atomic.AtomicLong)seed).get();
    }

    static void le32(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
        a[o + 2] = (byte)(v >> 16);
        a[o + 3] = (byte)(v >> 24);
    }
}