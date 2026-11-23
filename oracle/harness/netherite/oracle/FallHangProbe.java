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
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.block.BlockFalling;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityHanging;
import net.minecraft.entity.item.EntityFallingBlock;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityItemFrame;
import net.minecraft.entity.item.EntityPainting;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemHangingEntity;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.Direction;
import net.minecraft.util.MathHelper;
import net.minecraft.world.IWorldAccess;
import net.minecraft.world.WorldServer;
import net.minecraft.world.chunk.Chunk;

/**
 * The falling-block and hanging-entity probe: sand, gravel, anvils and the
 * dragon egg falling, paintings and item frames hanging, ticked the way the
 * server ticks them. The native port replays the script and is compared
 * against every stream here.
 *
 * The region loads raw (Probe.rawChunks, population off) around a far anchor,
 * in cx-major order, like Probe and EntityProbe. On top of it the probe plays
 * a script over the tick loop. Per probe tick, in order: the world's total
 * time advances by one (WorldServer.tick does this before its tickPending
 * phase), the tick's setBlock and fallInstantly flag ops run, the tick's
 * hanging placements run through ItemHangingEntity.onItemUse, the tick's
 * NBT-loaded falling entities spawn, WorldServer.tickUpdates(false) processes
 * the scheduled tick list, and the probe's own entity list ticks the way
 * World.updateEntities ticks it (EntityProbe.updateEntity, spawn order, dead
 * entities removed the way the world removes them). Entities spawned this
 * tick join the list in spawn order before the entity pass, so a falling
 * block spawned by a scheduled tick runs its first tick in the same tick.
 * Every EntityItem the run spawns is recorded in itemdrops.bin and taken out
 * of the world at once: it never ticks. Nothing else in the world ticks; the
 * server is parked, so every Det draw is the OTHER role's.
 *
 * Script contents, generated from Random(opseed) before the loop:
 *
 * - 80 walls (stone, 6 wide x 6 tall) on a 10x8 slot grid of stride 10 x 12
 *   at y 150, built over ticks 0..7. Rows 0..4 are painting walls with two
 *   placement slots each (slot 1 fits every art, slot 2 only the 16 and 32
 *   pixel arts); rows 5..7 are frame walls with four slots, some frames
 *   carrying a displayed item and a rotation. Placements run from tick 30,
 *   two per tick.
 * - 240 falling scenes on a 6x12 slot grid of stride 12, rows 0..5 at y 78
 *   and rows 6..11 at y 108, built one per tick from tick 5. Every scene
 *   first clears its slot envelope (a 9x9 column from y-9 to y+9, flag 2),
 *   so a slot can be reused. Kinds: pit (a sand or gravel column on a stone
 *   roof over a basin, the roof cell dug out later), torch (the column above
 *   a torch, so the fall lands in the torch cell and breaks into an item),
 *   water (the column above water cells in a sealed basin), anvil, dragon
 *   egg, instant (BlockFalling.field_149832_M set for the tick window, the
 *   column teleports straight down), stack (sand, gravel, sand over one
 *   roof, all falling together).
 * - a shaft at (fx0+90, fz0+90) dug from y 90 to y 0 with the bedrock removed,
 *   for the time-out falls, and three NBT-loaded falling entities: an anvil
 *   with Time 595 over the shaft (time-out while still high, DropItem on), a
 *   sand with DropItem off into the shaft (time-out without a drop), and an
 *   anvil with TileEntityData over a normal basin (the tile entity data
 *   field rides the entity NBT).
 * - wall removals: every third hanging loses its support cell from tick 500
 *   on, so its next 100-tick validity check breaks it and drops the painting
 *   or the frame and its displayed item.
 *
 * Every block write the run makes is recorded through Rows.onBlock
 * (writes.bin). Output DIR/manifest.json, DIR/script.bin, DIR/hang.bin,
 * DIR/nbtspawns.bin, DIR/writes.bin, DIR/spawns.bin, DIR/itemdrops.bin,
 * DIR/removals.txt.gz, DIR/sfx.txt.gz, DIR/ticks.bin.gz, DIR/digest.txt.gz,
 * DIR/hashes.txt, DIR/nbt64.txt.gz, DIR/start.txt, DIR/end.txt,
 * DIR/final.bin.gz. The layout strings are in the manifest.
 */
public final class FallHangProbe
{
    /** Falling scene kinds. */
    static final int K_PIT = 0, K_TORCH = 1, K_WATER = 2, K_ANVIL = 3, K_EGG = 4, K_INSTANT = 5, K_STACK = 6;

    /** Hanging placement kinds. */
    static final int H_PAINTING = 0, H_FRAME = 1;

    static final int FALL_SLOTS = 72;          /* 6 columns x 12 rows */
    static final int WALLS = 80;               /* 10 columns x 8 rows */
    static final int PAINTING_WALL_ROWS = 5;   /* wall rows 0..4 */
    static final int SCENES = 240;

    static final int SPAWN_BYTES = 86;
    static final int FALL_BYTES = 97;
    static final int HANG_BYTES = 71;
    static final int DROP_BYTES = 82;
    static final int WRITE_BYTES = 16;

    /** The frame display items: id, damage base, damage width (0 = fixed). */
    static final int[][] FRAME_ITEMS = {{280, 0, 0}, {35, 0, 16}, {331, 0, 16}, {263, 0, 2}, {264, 0, 0}, {288, 0, 0}};

    private FallHangProbe() {}

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
        }, "Oracle FallHang Probe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    /** One scripted op: kind 0 a setBlock, kind 1 the fallInstantly flag. */
    static final class Op
    {
        int tick, kind, x, y, z, id, meta, flags;
    }

    /** One hanging placement attempt. */
    static final class Place
    {
        int tick, kind, tileX, tileY, tileZ, dir, item, damage, rot;
    }

    /** One NBT-loaded falling entity spawn. */
    static final class NbtSpawn
    {
        int tick;
        NBTTagCompound tag;
    }

    /** Keeps every entity the world spawns and every aux sound. newFrom is
     * the first entry the flush has not taken yet. */
    static final class Capture implements IWorldAccess
    {
        final List<Entity> spawned = new ArrayList<Entity>();
        final List<int[]> sfx = new ArrayList<int[]>();
        int newFrom;

        public void onEntityCreate(Entity e) { spawned.add(e); }
        public void playAuxSFX(EntityPlayer p, int a, int x, int y, int z, int v) { sfx.add(new int[] {a, x, y, z, v}); }
        public void onEntityDestroy(Entity e) {}
        public void markBlockForUpdate(int x, int y, int z) {}
        public void markBlockForRenderUpdate(int x, int y, int z) {}
        public void markBlockRangeForRenderUpdate(int x0, int y0, int z0, int x1, int y1, int z1) {}
        public void playSound(String s, double x, double y, double z, float v, float p) {}
        public void playSoundToNearExcept(EntityPlayer p, String s, double x, double y, double z, float v, float q) {}
        public void spawnParticle(String s, double x, double y, double z, double a, double b, double c) {}
        public void playRecord(String s, int x, int y, int z) {}
        public void broadcastSound(int a, int b, int c, int d, int e) {}
        public void destroyBlockPartially(int a, int x, int y, int z, int v) {}
        public void onStaticEntitiesChanged() {}
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 4;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 2;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 1200;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 3L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        WorldServer ws = server.worldServers[0];
        long seed = ws.getSeed();
        long totalTime0 = ws.getWorldInfo().getWorldTotalTime();
        long worldRandSeed = opseed * 6364136223846793005L + 1442695040888963407L;
        Probe.rawChunks = true;
        BlockFalling.field_149832_M = false;

        int x0 = cx - radius - ring, x1 = cx + radius + ring;
        int z0 = cz - radius - ring, z1 = cz + radius + ring;
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

        Random r = new Random(opseed);
        int fx0 = (cx - radius) * 16, fz0 = (cz - radius) * 16;

        // the spawn-area generation before the probe leaves scheduled ticks
        // (population lakes) in the world's pending list; tickUpdates is
        // world-global, so clear it or cave gravel near spawn falls into the
        // record. Nothing schedules near spawn afterwards.
        ((Set<?>)field(WorldServer.class, "pendingTickListEntriesTreeSet").get(ws)).clear();
        ((Set<?>)field(WorldServer.class, "pendingTickListEntriesHashSet").get(ws)).clear();
        ((List<?>)field(WorldServer.class, "pendingTickListEntriesThisTick").get(ws)).clear();

        List<Op> ops = new ArrayList<Op>();
        List<Place> places = new ArrayList<Place>();
        List<NbtSpawn> nbtSpawns = new ArrayList<NbtSpawn>();

        // ------------------------------------------------------------- walls
        // 10 x 8 wall slots, stride 10 x 12, wall plane at (wx, wy..wy+5, wz),
        // face +z. Rows 0..4 painting walls (two slots), rows 5..7 frame
        // walls (four slots).
        int wxBase = fx0 + 8, wzBase = fz0 + 8, wyWall = 150;
        int hangCounter = 0;
        List<int[]> hangCells = new ArrayList<int[]>(); // the support cells, for the removal script

        for (int w = 0; w < WALLS; ++w)
        {
            int col = w % 10, row = w / 10;
            int wx = wxBase + col * 10, wz = wzBase + row * 12;
            int buildTick = w % 8;

            for (int i = 0; i <= 5; ++i)
            {
                for (int j = 0; j <= 5; ++j) op(ops, buildTick, wx + i, wyWall + j, wz, Block.getIdFromBlock(Blocks.stone), 0, 2);
            }

            if (row < PAINTING_WALL_ROWS)
            {
                for (int s = 0; s < 2; ++s)
                {
                    Place p = new Place();
                    p.tick = 30 + hangCounter / 2;
                    p.kind = H_PAINTING;
                    p.tileX = wx + (s == 0 ? 1 : 4);
                    p.tileY = wyWall + 2;
                    p.tileZ = wz;
                    p.dir = Direction.facingToDirection[3]; // clicked on the +z face
                    places.add(p);
                    hangCells.add(new int[] {p.tileX, p.tileY, p.tileZ});
                    ++hangCounter;
                }
            }
            else
            {
                for (int i = 1; i <= 4; ++i)
                {
                    Place p = new Place();
                    p.tick = 30 + hangCounter / 2;
                    p.kind = H_FRAME;
                    p.tileX = wx + i;
                    p.tileY = wyWall + 2;
                    p.tileZ = wz;
                    p.dir = Direction.facingToDirection[3];

                    if (r.nextInt(3) != 0)
                    {
                        int[] fi = FRAME_ITEMS[r.nextInt(FRAME_ITEMS.length)];
                        p.item = fi[0];
                        p.damage = fi[2] > 0 ? fi[1] + r.nextInt(fi[2]) : fi[1];
                        p.rot = r.nextInt(4);
                    }

                    places.add(p);
                    hangCells.add(new int[] {p.tileX, p.tileY, p.tileZ});
                    ++hangCounter;
                }
            }
        }

        // wall removals: every third hanging's support cell, from tick 500 on
        for (int h = 0; h < hangCells.size(); h += 3)
        {
            int[] s = hangCells.get(h);
            Op o = new Op();
            o.tick = 500 + h / 2;
            o.x = s[0];
            o.y = s[1];
            o.z = s[2];
            o.flags = 3;
            ops.add(o);
        }

        // ------------------------------------------------------ falling slots
        int falls = 0;
        int instantCount = 0;

        for (int i = 0; i < SCENES; ++i)
        {
            int s = i % FALL_SLOTS;
            int col = s % 6, row = s / 6;
            int ax = fx0 + 8 + col * 12, az = fz0 + 8 + row * 12;
            int ay = row < 6 ? 78 : 108;
            int buildTick = 5 + i;
            int digTick = buildTick + 30 + r.nextInt(45);
            int pick = r.nextInt(100);
            int kind = pick < 30 ? K_PIT : pick < 42 ? K_TORCH : pick < 54 ? K_WATER
                     : pick < 70 ? K_ANVIL : pick < 80 ? K_EGG : pick < 88 ? K_INSTANT : K_STACK;

            // the fallInstantly flag is static and world-global: an instant
            // scene's window teleports every other floating column whose
            // update tick lands inside it, so all instant scenes run at the
            // end, one per 4 ticks, when nothing else is scheduled
            if (kind == K_INSTANT)
            {
                buildTick = 1000 + 4 * instantCount++;
                digTick = buildTick;
            }

            clearSlot(ops, buildTick, ax, ay, az);
            falls += scene(ops, r, kind, buildTick, digTick, ax, ay, az);
        }

        // ------------------------------------------------------------- shaft
        int sx = fx0 + 90, sz = fz0 + 90;

        for (int y = 90; y >= 0; --y)
        {
            op(ops, 490, sx, y, sz, 0, 0, 3);
        }

        {
            NbtSpawn n = new NbtSpawn();
            n.tick = 560;
            n.tag = fallNbt(145, 0, 595, 1, 1, 2.0F, 40, sx + 0.5D, 176.5D, sz + 0.5D, false);
            nbtSpawns.add(n);

            n = new NbtSpawn();
            n.tick = 580;
            n.tag = fallNbt(12, 0, 0, 0, 0, 0.0F, 0, sx + 0.5D, 190.5D, sz + 0.5D, false);
            nbtSpawns.add(n);

            // a normal anvil drop over a basin, TileEntityData riding the NBT
            int ex = fx0 + 8 + 4 * 12, ez = fz0 + 8 + 9 * 12, ey = 108;
            clearSlot(ops, 555, ex, ey, ez);
            scene(ops, r, K_PIT, 555, 585, ex, ey, ez);
            n = new NbtSpawn();
            n.tick = 620;
            n.tag = fallNbt(145, 1, 0, 1, 1, 2.0F, 40, ex + 0.5D, ey + 40 + 0.5D, ez + 0.5D, true);
            nbtSpawns.add(n);
        }

        // ------------------------------------------------------ entity state
        List<Entity> list = new ArrayList<Entity>();
        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        Capture capture = new Capture();
        ws.addWorldAccess(capture);

        Field hurtField = field(EntityFallingBlock.class, "field_145809_g");     // hurtEntities
        Field brokeField = field(EntityFallingBlock.class, "field_145808_f");    // broke on landing
        Field hurtMaxField = field(EntityFallingBlock.class, "field_145815_h");
        Field hurtAmtField = field(EntityFallingBlock.class, "field_145816_i");
        Field counterField = field(EntityHanging.class, "tickCounter1");
        Field dropChanceField = field(EntityItemFrame.class, "itemDropChance");
        Field fireField = field(Entity.class, "fire");
        Field pendingField = field(WorldServer.class, "pendingTickListEntriesTreeSet");

        writeDetState(new File(dir, "start.txt"), seed);
        ws.rand.setSeed(worldRandSeed);

        OutputStream scriptOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "script.bin")), 1 << 16);
        OutputStream hangOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "hang.bin")), 1 << 16);
        OutputStream nbtOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "nbtspawns.bin")), 1 << 16);
        OutputStream writesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "writes.bin")), 1 << 16);
        OutputStream spawnOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        OutputStream dropOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "itemdrops.bin")), 1 << 16);
        GZIPOutputStream ticksGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "ticks.bin.gz")), 1 << 16);
        OutputStream ticksOut = new BufferedOutputStream(ticksGz, 1 << 16);
        GZIPOutputStream digestGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "digest.txt.gz")), 1 << 16);
        PrintWriter dw = new PrintWriter(new OutputStreamWriter(digestGz, "UTF-8"));
        GZIPOutputStream remGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "removals.txt.gz")), 1 << 16);
        PrintWriter rw = new PrintWriter(new OutputStreamWriter(remGz, "UTF-8"));
        GZIPOutputStream sfxGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "sfx.txt.gz")), 1 << 16);
        PrintWriter sw = new PrintWriter(new OutputStreamWriter(sfxGz, "UTF-8"));
        GZIPOutputStream nbtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "nbt64.txt.gz")), 1 << 16);
        PrintWriter nw = new PrintWriter(new OutputStreamWriter(nbtGz, "UTF-8"));
        PrintWriter hashW = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "hashes.txt")), "UTF-8"));

        Rows.writeListener = new Rows.WriteListener()
        {
            public void onWrite(net.minecraft.world.World w, int x, int y, int z, int id, int meta)
            {
                byte[] b = new byte[WRITE_BYTES];
                le32(b, 0, x);
                le32(b, 4, y);
                le32(b, 8, z);
                b[12] = (byte)id;
                b[13] = (byte)(id >> 8);
                b[14] = (byte)meta;
                b[15] = 0;

                try
                {
                    writesOut.write(b);
                }
                catch (IOException e)
                {
                    throw new RuntimeException("fallhang write", e);
                }
            }
        };

        // the script files, grouped by tick
        List<Op>[] opsByTick = new List[ticks];
        List<Place>[] placesByTick = new List[ticks];
        List<NbtSpawn>[] nbtByTick = new List[ticks];

        for (int t = 0; t < ticks; ++t)
        {
            opsByTick[t] = new ArrayList<Op>();
            placesByTick[t] = new ArrayList<Place>();
            nbtByTick[t] = new ArrayList<NbtSpawn>();
        }

        for (Op o : ops) if (o.tick < ticks) opsByTick[o.tick].add(o);
        for (Place p : places) if (p.tick < ticks) placesByTick[p.tick].add(p);
        for (NbtSpawn n : nbtSpawns) if (n.tick < ticks) nbtByTick[n.tick].add(n);

        byte[] countHead = new byte[8];

        for (int t = 0; t < ticks; ++t)
        {
            le32(countHead, 0, t);
            le32(countHead, 4, opsByTick[t].size());
            scriptOut.write(countHead);

            for (Op o : opsByTick[t])
            {
                byte[] b = new byte[15];
                b[0] = (byte)o.kind;
                le32(b, 1, o.x);
                le32(b, 5, o.y);
                le32(b, 9, o.z);
                b[13] = (byte)o.id;
                b[14] = (byte)(o.id >> 8);
                scriptOut.write(b);
                scriptOut.write(new byte[] {(byte)o.meta, (byte)o.flags}, 0, 2);
            }

            le32(countHead, 0, t);
            le32(countHead, 4, placesByTick[t].size());
            hangOut.write(countHead);

            for (Place p : placesByTick[t])
            {
                byte[] b = new byte[19];
                b[0] = (byte)p.kind;
                le32(b, 1, p.tileX);
                le32(b, 5, p.tileY);
                le32(b, 9, p.tileZ);
                b[13] = (byte)p.dir;
                b[14] = (byte)p.item;
                b[15] = (byte)(p.item >> 8);
                b[16] = (byte)p.damage;
                b[17] = (byte)(p.damage >> 8);
                b[18] = (byte)p.rot;
                hangOut.write(b);
            }

            le32(countHead, 0, t);
            le32(countHead, 4, nbtByTick[t].size());
            nbtOut.write(countHead);

            for (NbtSpawn n : nbtByTick[t])
            {
                byte[] b = new byte[4];
                byte[] text = StructuresProbe.canon(n.tag).toString().getBytes("UTF-8");
                le32(b, 0, text.length);
                nbtOut.write(b);
                nbtOut.write(text);
            }
        }

        // ------------------------------------------------------------ the run
        ItemHangingEntity paintItem = (ItemHangingEntity)Items.painting;
        ItemHangingEntity frameItem = (ItemHangingEntity)Items.item_frame;
        EntityPlayer player = (EntityPlayer)ws.playerEntities.get(0);
        byte[] rec = new byte[Math.max(FALL_BYTES, HANG_BYTES)];
        int removed = 0, nextSpawn = 0;

        for (int t = 0; t < ticks; ++t)
        {
            ws.getWorldInfo().incrementTotalWorldTime(ws.getWorldInfo().getWorldTotalTime() + 1L);

            for (Op o : opsByTick[t])
            {
                if (o.kind == 1) BlockFalling.field_149832_M = o.meta != 0;
                else ws.setBlock(o.x, o.y, o.z, Block.getBlockById(o.id), o.meta, o.flags);
            }

            for (Place p : placesByTick[t])
            {
                ItemHangingEntity item = p.kind == H_PAINTING ? paintItem : frameItem;
                ItemStack stack = new ItemStack(p.kind == H_PAINTING ? Items.painting : Items.item_frame, 1, 0);
                int before = capture.spawned.size();
                item.onItemUse(stack, player, ws, p.tileX, p.tileY, p.tileZ, Direction.directionToFacing[p.dir], 0.5F, 0.5F, 0.5F);

                for (int k = before; k < capture.spawned.size(); ++k)
                {
                    Entity e = capture.spawned.get(k);

                    if (e instanceof EntityItemFrame && p.item != 0)
                    {
                        EntityItemFrame f = (EntityItemFrame)e;
                        f.setDisplayedItem(new ItemStack(Item.getItemById(p.item), 1, p.damage));
                        f.setItemRotation(p.rot);
                    }
                }
            }

            for (NbtSpawn n : nbtByTick[t])
            {
                EntityFallingBlock e = new EntityFallingBlock(ws);
                e.readFromNBT(n.tag);
                ws.spawnEntityInWorld(e);
            }

            nextSpawn += flush(capture, list, spawnIndex, nextSpawn, spawnOut, dropOut, t, hurtField, hurtMaxField, hurtAmtField);

            ws.tickUpdates(false);
            nextSpawn += flush(capture, list, spawnIndex, nextSpawn, spawnOut, dropOut, t, hurtField, hurtMaxField, hurtAmtField);

            // the entity pass: the list in spawn order, dead entities removed
            // the way the world removes them
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);

                if (e.isDead) continue;

                EntityProbe.updateEntity(ws, e);

                if (e.isDead)
                {
                    boolean falling = e instanceof EntityFallingBlock;
                    int time = falling ? ((EntityFallingBlock)e).field_145812_b : counterField.getInt(e);
                    int reason;

                    if (falling && (time > 100 && (MathHelper.floor_double(e.posY) < 1 || MathHelper.floor_double(e.posY) > 256) || time > 600)) reason = 1;
                    else if (falling) reason = 0;
                    else reason = 10;

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    rw.println(t + " " + spawnIndex.get(e).intValue() + " " + e.getEntityId() + " " + reason + " "
                        + hex(Double.doubleToRawLongBits(e.posY)) + " " + time);
                    list.remove(i--);
                    ++removed;
                }
            }

            nextSpawn += flush(capture, list, spawnIndex, nextSpawn, spawnOut, dropOut, t, hurtField, hurtMaxField, hurtAmtField);

            // one state record per live entity, in list order
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);
                int n = e instanceof EntityFallingBlock
                    ? fallRecord(rec, t, spawnIndex.get(e).intValue(), (EntityFallingBlock)e, hurtField, brokeField, hurtMaxField, hurtAmtField, fireField)
                    : hangRecord(rec, t, spawnIndex.get(e).intValue(), (EntityHanging)e, counterField);
                ticksOut.write(rec, 0, n);
            }

            writeDigest(dw, t, ws, pendingField);

            for (int[] s : capture.sfx)
            {
                sw.println(t + " " + s[0] + " " + s[1] + " " + s[2] + " " + s[3] + " " + s[4]);
            }

            capture.sfx.clear();

            if (t % 64 == 63)
            {
                hashW.println("t " + t + " hash " + hex(hashRegion(ws, fx0 >> 4, fz0 >> 4)));
                writeNbt(nw, t, list, spawnIndex);
            }
        }

        hashW.println("t " + (ticks - 1) + " hash " + hex(hashRegion(ws, fx0 >> 4, fz0 >> 4)));
        writeNbt(nw, ticks - 1, list, spawnIndex);
        writeDetState(new File(dir, "end.txt"), seed);
        BlockFalling.field_149832_M = false;
        ws.removeWorldAccess(capture);
        Rows.writeListener = null;

        scriptOut.close();
        hangOut.close();
        nbtOut.close();
        writesOut.close();
        spawnOut.close();
        dropOut.close();
        ticksOut.close();
        ticksGz.close();
        dw.close();
        digestGz.close();
        rw.close();
        remGz.close();
        sw.flush();
        sw.close();
        sfxGz.close();
        nw.close();
        nbtGz.close();
        hashW.close();

        // the final region, in load order
        Field gap = field(Chunk.class, "isGapLightingUpdated");
        DataOutputStream out = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        byte[] cols = new byte[256];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, buf);
                writeLe32(out, lx);
                writeLe32(out, lz);
                out.write(buf);

                for (int i = 0; i < 256; ++i) cols[i] = (byte)(c.updateSkylightColumns[i] ? 1 : 0);
                out.write(cols);
                out.write(gap.getBoolean(c) ? 1 : 0);
            }
        }

        out.close();

        // ------------------------------------------------------------ manifest
        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "fallhang");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("ticks", ticks);
        m.addProperty("opseed", opseed);
        m.addProperty("totalTime0", totalTime0);
        m.addProperty("worldRandSeed", worldRandSeed);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("tick_order", "per probe tick: worldInfo.incrementTotalWorldTime(+1); the tick's script ops in generation order (kind 0: World.setBlock(x, y, z, id, meta, flags); kind 1: BlockFalling.field_149832_M = meta != 0); the tick's hanging placements through ItemHangingEntity.onItemUse; the tick's NBT spawns (new EntityFallingBlock(world) + readFromNBT + spawnEntityInWorld); WorldServer.tickUpdates(false); the entity pass (EntityProbe.updateEntity over the probe list in spawn order, dead entities out of the world and the list, reason per the removals rule). Entities spawned by the ops, the NBT spawns and tickUpdates join the list in spawn order before the pass; items are taken out at once");
        m.addProperty("script_layout", "script.bin: per tick, tick uint32 LE, op count uint32 LE, then count x 17 bytes: kind uint8 (0 setBlock, 1 fallInstantly flag), x int32 LE, y int32 LE, z int32 LE, id uint16 LE (kind 1: 0), meta uint8 (kind 1: the flag value), flags uint8 (kind 0: the World.setBlock flags)");
        m.addProperty("hang_layout", "hang.bin: per tick, tick uint32 LE, count uint32 LE, then count x 19 bytes: kind uint8 (0 painting, 1 frame), tileX int32 LE, tileY int32 LE, tileZ int32 LE, dir uint8 (the hangingDirection), item uint16 LE (0: the frame stays empty), damage uint16 LE, rot uint8. The attempt runs ItemHangingEntity.onItemUse(fresh 1x stack, the world's player, world, tileX, tileY, tileZ, Direction.directionToFacing[dir], 0.5, 0.5, 0.5); a spawned frame with item != 0 then gets setDisplayedItem(new ItemStack(item, 1, damage)) and setItemRotation(rot)");
        m.addProperty("nbt_layout", "nbtspawns.bin: per tick, tick uint32 LE, count uint32 LE, then count records: length uint32 LE, the canonical NBT text in UTF-8. The replay constructs new EntityFallingBlock(world), feeds the text through Entity.readFromNBT (a compound parsed from the canonical form), and spawns it");
        m.addProperty("spawn_layout", SPAWN_BYTES + " bytes per spawn, in spawn order: spawn_index int32 LE, entity_id int32 LE, kind uint8 (1 falling, 2 painting, 3 frame), pad 3, x, y, z double LE (after the constructor, or setDirection for hangings), yaw float32 LE, pad 4, time int32 LE (the falling Time field, else 0), block uint16 LE (the falling block id, else 0), meta uint8, dropItem uint8, hurtEntities uint8, hurtAmount float32 LE, hurtMax int32 LE, tileEntityData uint8 (the falling NBT field non-null, else 0), dir uint8, tickCounter1 int32 LE (hangings, else 0), tileX int32 LE, tileY int32 LE, tileZ int32 LE (hangings, else 0), art uint16 LE (paintings, the EnumArt ordinal), rot uint8 (frames), item uint16 LE (frames' displayed item, 0 none), damage uint16 LE, pad 5");
        m.addProperty("tick_layout", "ticks.bin.gz, one record per live entity per tick in list order. Falling " + FALL_BYTES + " bytes: tick int32 LE, spawn_index int32 LE, entity_id int32 LE, x, y, z double LE, motionX, motionY, motionZ double LE, yaw float32 LE, fallDistance float32 LE, flags uint8 x 4 (onGround, isCollidedHorizontally, isCollidedVertically, velocityChanged), Time int32 LE, block uint16 LE, meta uint8, dropItem uint8, hurtEntities uint8, hurtAmount float32 LE, hurtMax int32 LE, fire int32 LE, ticksExisted int32 LE. Hanging " + HANG_BYTES + " bytes: tick int32 LE, spawn_index int32 LE, entity_id int32 LE, x, y, z double LE, yaw float32 LE, dir uint8, pad 3, tickCounter1 int32 LE, tileX int32 LE, tileY int32 LE, tileZ int32 LE, art uint16 LE (paintings, the ordinal; frames 0xffff), rot uint8 (frames), item uint16 LE (frames' displayed item, 0xffff none), damage uint16 LE (frames), ticksExisted int32 LE");
        m.addProperty("drop_layout", DROP_BYTES + " bytes per item drop, in drop order: tick int32 LE, spawn_index int32 LE, entity_id int32 LE, item uint16 LE, damage uint16 LE, count uint8, pad uint8, x, y, z double LE, motionX, motionY, motionZ double LE, yaw float32 LE, hoverStart float32 LE, delay int32 LE (delayBeforeCanPickup), age int32 LE");
        m.addProperty("write_layout", WRITE_BYTES + " bytes per write, in call order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE (0xffff for a metadata-only write), meta uint8, pad uint8; every call Rows.onBlock gets that changed something");
        m.addProperty("removals", "removals.txt.gz, one line per removal at the tick it happened: <tick> <spawn_index> <entity_id> <reason> <posY as hex of the raw double bits> <extra>; reason 0 a falling block that died on landing or on its tick-1 block check, 1 a falling block's time-out (the Time field over 100 with the position out of range, or over 600; extra is the Time field), 10 a hanging entity's failed 100-tick validity check (extra is tickCounter1)");
        m.addProperty("drops_rule", "every EntityItem the run spawns, through entityDropItem on landing, on broken hangings and the frame paths, is recorded in itemdrops.bin and taken out of the world at once: it never ticks");
        m.addProperty("sfx", "sfx.txt.gz, one line per aux sound: <tick> <type> <x> <y> <z> <value>; the anvil's playAuxSFX(1022) on landing");
        m.addProperty("digest", "digest.txt.gz, one line per tick: t <tick>, then per role 0..3: role <r> seeder <hex> math <hex> split <hex>, then nextId <the OTHER role's next id>, worldRand <the world Random's 48-bit state hex>, pending <the scheduled tick list size>");
        m.addProperty("hashes", "hashes.txt: every 64th tick (t % 64 == 63) and the final tick: t <tick> hash <FNV-1a 64 over the 9x9 chunks from (fx0 >> 4, fz0 >> 4), dx outer 0..8, dz inner 0..8, chunk bytes as below; an unloaded chunk contributes one 0 byte>");
        m.addProperty("chunk_bytes", "the setblock probe's: ids uint16 LE (65536, index x << 12 | z << 8 | y), metas uint8 (65536), sky light uint8 (65536), block light uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE (both Java index order z << 4 | x), heightMapMinimum int32 LE, section mask uint16 LE");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 LE, cz int32 LE, the chunk bytes above, updateSkylightColumns 256 bytes (0/1), isGapLightingUpdated 1 byte");
        m.addProperty("nbt64", "nbt64.txt.gz: every 64th tick and the final tick, line t <tick>, then one line per live entity in list order: <spawn_index> <the canonical NBT of entity.writeToNBT(new NBTTagCompound())>");
        m.addProperty("regions", "falling slots at (fx0 + 8 + col*12, y 78 for rows 0..5 and 108 for 6..11, fz0 + 8 + row*12); walls at (fx0 + 8 + col*10, y 150..155, fz0 + 8 + row*12) on a 10x8 grid of stride 10 x 12, face +z; the shaft at (fx0 + 90, fz0 + 90) from y 90 to y 0; fx0 = (cx - radius) * 16, fz0 = (cz - radius) * 16");
        m.addProperty("world_rand", "world.rand is set to worldRandSeed at probe start; its 48-bit state is in every digest line and must stay at the seed's state except where a liquid updateTick drew (the water scenes' flow after the sand replaces a water cell)");
        m.addProperty("counts", "falling scenes " + SCENES + ", walls " + WALLS + ", hanging placements " + hangCounter + ", expected fallers " + falls);
        m.addProperty("pending_clear", "the world's pending scheduled tick lists (TreeSet, HashSet, ThisTick) are cleared right after the region loads: the join's spawn-area population leaves lake entries there and tickUpdates is world-global, so without the clear, cave gravel near spawn falls into the record. Nothing else schedules outside the region during the run");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("ticks", ticks);
        res.addProperty("spawned", countSpawned);
        res.addProperty("drops", countDrops);
        res.addProperty("removed", removed);
        res.addProperty("expectedFalls", falls);
        return res;
    }

    /** The scene builds, per kind. (0,0,0) relative is the roof cell the dig
     * removes; the column sits above it. Returns the expected faller count. */
    static int scene(List<Op> ops, Random r, int kind, int buildTick, int digTick, int ax, int ay, int az)
    {
        int sandId = Block.getIdFromBlock(Blocks.sand);
        int gravelId = Block.getIdFromBlock(Blocks.gravel);
        int anvilId = Block.getIdFromBlock(Blocks.anvil);
        int eggId = Block.getIdFromBlock(Blocks.dragon_egg);
        int stoneId = Block.getIdFromBlock(Blocks.stone);
        int torchId = Block.getIdFromBlock(Blocks.torch);
        int waterId = Block.getIdFromBlock(Blocks.water);

        switch (kind)
        {
        case K_PIT:
        {
            // basin: floor 5x5 at ay-8, walls ring to ay-1, roof 5x5 at ay,
            // column of 3..5 on the roof
            basin(ops, buildTick, ax, ay, az, stoneId);
            int h = 3 + r.nextInt(3);
            int id = r.nextInt(2) == 0 ? sandId : gravelId;

            for (int i = 1; i <= h; ++i) op(ops, buildTick, ax, ay + i, az, id, 0, 2);

            op(ops, digTick, ax, ay, az, 0, 0, 3); // the dig
            return h;
        }

        case K_TORCH:
        {
            // stone floor 5x5 at ay-2, torch at ay-1, a floating column of
            // 1..2 above it: the fall passes the torch cell, lands with the
            // torch cell under it and breaks into an item
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) op(ops, buildTick, ax + dx, ay - 2, az + dz, stoneId, 0, 2);

            op(ops, buildTick, ax, ay - 1, az, torchId, 1, 2);
            int h = 1 + r.nextInt(2);
            int id = r.nextInt(2) == 0 ? sandId : gravelId;

            for (int i = 1; i <= h; ++i) op(ops, buildTick, ax, ay + i, az, id, 0, 2);

            return h;
        }

        case K_WATER:
        {
            // sealed basin: floor 5x5 at ay-2, walls ring at ay-1, water in
            // the two cells (-1,-1) and (0,-1), column of 1..2 above (0,0)
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) op(ops, buildTick, ax + dx, ay - 2, az + dz, stoneId, 0, 2);

            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) op(ops, buildTick, ax + dx, ay - 1, az + dz, stoneId, 0, 2);

            op(ops, buildTick, ax - 1, ay - 1, az, waterId, 0, 2);
            op(ops, buildTick, ax, ay - 1, az, waterId, 0, 2);
            int h = 1 + r.nextInt(2);
            int id = r.nextInt(2) == 0 ? sandId : gravelId;

            for (int i = 0; i < h; ++i) op(ops, buildTick, ax, ay + i, az, id, 0, 2);

            return h;
        }

        case K_ANVIL:
        {
            basin(ops, buildTick, ax, ay, az, stoneId);
            int ori = r.nextInt(4), dmg = r.nextInt(3);
            op(ops, buildTick, ax, ay + 1, az, anvilId, ori | dmg << 2, 2);
            op(ops, digTick, ax, ay, az, 0, 0, 3);
            return 1;
        }

        case K_EGG:
        {
            // basin roof at ay, the egg floating one above it: its update
            // tick rate is 5, so it falls five ticks after the build
            basin(ops, buildTick, ax, ay, az, stoneId);
            op(ops, buildTick, ax, ay + 2, az, eggId, 0, 2);
            return 1;
        }

        case K_INSTANT:
        {
            // roof 5x5 at ay-2, a floating column of 2..4 above the air at
            // ay: the fallInstantly flag teleports it straight down. The
            // flag ops go to the build tick and 4 ticks later.
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz) op(ops, buildTick, ax + dx, ay - 2, az + dz, stoneId, 0, 2);

            int h = 2 + r.nextInt(3);
            int id = r.nextInt(2) == 0 ? sandId : gravelId;

            for (int i = 1; i <= h; ++i) op(ops, buildTick, ax, ay + i, az, id, 0, 2);

            flagOp(ops, buildTick, 1);
            flagOp(ops, buildTick + 4, 0);
            return 0;
        }

        case K_STACK:
        {
            basin(ops, buildTick, ax, ay, az, stoneId);
            op(ops, buildTick, ax, ay + 1, az, sandId, 0, 2);
            op(ops, buildTick, ax, ay + 2, az, gravelId, 0, 2);
            op(ops, buildTick, ax, ay + 3, az, sandId, 0, 2);
            op(ops, digTick, ax, ay, az, 0, 0, 3);
            return 3;
        }
        }

        throw new IllegalStateException("kind " + kind);
    }

    /** The scene basin: floor 5x5 at ay-8, walls ring at ay-7..ay-1, roof
     * 5x5 at ay. The interior (3x3, ay-7..ay-1) is left air. */
    static void basin(List<Op> ops, int buildTick, int ax, int ay, int az, int stoneId)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            for (int dz = -2; dz <= 2; ++dz)
            {
                op(ops, buildTick, ax + dx, ay - 8, az + dz, stoneId, 0, 2);
                op(ops, buildTick, ax + dx, ay, az + dz, stoneId, 0, 2);
            }
        }

        for (int dy = -7; dy <= -1; ++dy)
        {
            for (int dx = -2; dx <= 2; ++dx)
            {
                for (int dz = -2; dz <= 2; ++dz)
                {
                    if (dx > -2 && dx < 2 && dz > -2 && dz < 2) continue; // the interior stays air
                    op(ops, buildTick, ax + dx, ay + dy, az + dz, stoneId, 0, 2);
                }
            }
        }
    }

    /** The slot envelope clear: 9x9, y-9..y+9, flag 2. */
    static void clearSlot(List<Op> ops, int buildTick, int ax, int ay, int az)
    {
        for (int dy = -9; dy <= 9; ++dy)
        {
            for (int dx = -4; dx <= 4; ++dx)
            {
                for (int dz = -4; dz <= 4; ++dz) op(ops, buildTick, ax + dx, ay + dy, az + dz, 0, 0, 2);
            }
        }
    }

    static void op(List<Op> ops, int tick, int x, int y, int z, int id, int meta, int flags)
    {
        Op o = new Op();
        o.tick = tick;
        o.kind = 0;
        o.x = x;
        o.y = y;
        o.z = z;
        o.id = id;
        o.meta = meta;
        o.flags = flags;
        ops.add(o);
    }

    static void flagOp(List<Op> ops, int tick, int on)
    {
        Op o = new Op();
        o.tick = tick;
        o.kind = 1;
        o.meta = on;
        ops.add(o);
    }

    /** The NBT one NBT-loaded falling entity spawns from. Pos carries the
     * spawn position; TileID and Data the block; Time, DropItem,
     * HurtEntities, FallHurtAmount and FallHurtMax the tick fields;
     * TileEntityData a junk compound when ted is set. */
    static NBTTagCompound fallNbt(int block, int data, int time, int dropItem, int hurt, float hurtAmount, int hurtMax, double x, double y, double z, boolean ted)
    {
        NBTTagCompound t = new NBTTagCompound();
        NBTTagList pos = new NBTTagList();
        pos.appendTag(new net.minecraft.nbt.NBTTagDouble(x));
        pos.appendTag(new net.minecraft.nbt.NBTTagDouble(y));
        pos.appendTag(new net.minecraft.nbt.NBTTagDouble(z));
        t.setTag("Pos", pos);
        NBTTagList mot = new NBTTagList();
        mot.appendTag(new net.minecraft.nbt.NBTTagDouble(0.0D));
        mot.appendTag(new net.minecraft.nbt.NBTTagDouble(0.0D));
        mot.appendTag(new net.minecraft.nbt.NBTTagDouble(0.0D));
        t.setTag("Motion", mot);
        NBTTagList rot = new NBTTagList();
        rot.appendTag(new net.minecraft.nbt.NBTTagFloat(0.0F));
        rot.appendTag(new net.minecraft.nbt.NBTTagFloat(0.0F));
        t.setTag("Rotation", rot);
        t.setFloat("FallDistance", 0.0F);
        t.setShort("Fire", (short)0);
        t.setShort("Air", (short)300);
        t.setBoolean("OnGround", false);
        t.setInteger("Dimension", 0);
        t.setBoolean("Invulnerable", false);
        t.setInteger("PortalCooldown", 0);
        t.setInteger("TileID", block);
        t.setByte("Tile", (byte)block);
        t.setByte("Data", (byte)data);
        t.setByte("Time", (byte)time);
        t.setBoolean("DropItem", dropItem != 0);
        t.setBoolean("HurtEntities", hurt != 0);
        t.setFloat("FallHurtAmount", hurtAmount);
        t.setInteger("FallHurtMax", hurtMax);

        if (ted)
        {
            NBTTagCompound te = new NBTTagCompound();
            te.setString("id", "Anvil");
            te.setInteger("x", 0);
            te.setInteger("y", 0);
            te.setInteger("z", 0);
            te.setString("Extra", "junk");
            t.setTag("TileEntityData", te);
        }

        return t;
    }

    static int countSpawned, countDrops;

    /** The new entities since the last flush into the probe list, in spawn
     * order; item drops are recorded and taken out. Returns how many were
     * listed. nextSpawn advances only for listed entities: an item takes the
     * index the next listed entity will carry. */
    static int flush(Capture capture, List<Entity> list, Map<Entity, Integer> spawnIndex, int nextSpawn,
                     OutputStream spawnOut, OutputStream dropOut, int t,
                     Field hurtField, Field hurtMaxField, Field hurtAmtField) throws Exception
    {
        int listed = 0;

        while (capture.newFrom < capture.spawned.size())
        {
            Entity e = capture.spawned.get(capture.newFrom++);
            int si = nextSpawn + listed;
            spawnIndex.put(e, Integer.valueOf(si));

            if (e instanceof EntityItem)
            {
                recordDrop(dropOut, t, si, (EntityItem)e);
                ++countDrops;
            }
            else
            {
                recordSpawn(spawnOut, si, e, hurtField, hurtMaxField, hurtAmtField);
                list.add(e);
                ++listed;
                ++countSpawned;
            }
        }

        return listed;
    }

    /** Takes an entity out of the world completely (TickProbe.takeOut). */
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

    static void recordSpawn(OutputStream out, int si, Entity e, Field hurtField, Field hurtMaxField, Field hurtAmtField) throws Exception
    {
        byte[] b = new byte[SPAWN_BYTES];
        int kind = e instanceof EntityFallingBlock ? 1 : e instanceof EntityPainting ? 2 : 3;
        int p = 0;
        p = le32(b, p, si);
        p = le32(b, p, e.getEntityId());
        b[p++] = (byte)kind;
        p += 3;
        p = le64(b, p, Double.doubleToRawLongBits(e.posX));
        p = le64(b, p, Double.doubleToRawLongBits(e.posY));
        p = le64(b, p, Double.doubleToRawLongBits(e.posZ));
        p = le32(b, p, Float.floatToRawIntBits(e.rotationYaw));
        p += 4;

        if (kind == 1)
        {
            EntityFallingBlock f = (EntityFallingBlock)e;
            p = le32(b, p, f.field_145812_b);
            p = le16(b, p, Block.getIdFromBlock(f.func_145805_f()));
            b[p++] = (byte)f.field_145814_a;
            b[p++] = (byte)(f.field_145813_c ? 1 : 0);
            b[p++] = (byte)(hurtField.getBoolean(f) ? 1 : 0);
            p = le32(b, p, Float.floatToRawIntBits(hurtAmtField.getFloat(f)));
            p = le32(b, p, hurtMaxField.getInt(f));
            b[p++] = (byte)(f.field_145810_d != null ? 1 : 0);
            b[p++] = 0; // dir
            p = le32(b, p, 0); // tickCounter1
            p = le32(b, p, 0);
            p = le32(b, p, 0);
            p = le32(b, p, 0);
            p = le16(b, p, 0); // art
            b[p++] = 0; // rot
            p = le16(b, p, 0); // item
            p = le16(b, p, 0); // damage
        }
        else
        {
            EntityHanging h = (EntityHanging)e;
            EntityItemFrame frame = h instanceof EntityItemFrame ? (EntityItemFrame)h : null;
            ItemStack shown = frame != null ? frame.getDisplayedItem() : null;
            p = le32(b, p, 0); // time
            p = le16(b, p, 0); // block
            b[p++] = 0; // meta
            b[p++] = 0; // dropItem
            b[p++] = 0; // hurt
            p = le32(b, p, 0); // hurtAmount
            p = le32(b, p, 0); // hurtMax
            b[p++] = 0; // tileEntityData
            b[p++] = (byte)h.hangingDirection;
            p = le32(b, p, tickCounter(h));
            p = le32(b, p, h.field_146063_b);
            p = le32(b, p, h.field_146064_c);
            p = le32(b, p, h.field_146062_d);
            p = le16(b, p, h instanceof EntityPainting ? ((EntityPainting)h).art.ordinal() : 0xffff);
            b[p++] = frame != null ? (byte)frame.getRotation() : 0;
            p = le16(b, p, shown != null ? Item.getIdFromItem(shown.getItem()) : 0);
            p = le16(b, p, shown != null ? shown.getItemDamage() : 0);
        }

        if (p != SPAWN_BYTES) throw new IllegalStateException("spawn record " + p);

        out.write(b);
    }

    static int tickCounter(EntityHanging h) throws Exception
    {
        return ((Integer)field(EntityHanging.class, "tickCounter1").get(h)).intValue();
    }

    static void recordDrop(OutputStream out, int t, int si, EntityItem e) throws Exception
    {
        byte[] b = new byte[DROP_BYTES];
        int p = 0;
        ItemStack s = e.getEntityItem();
        p = le32(b, p, t);
        p = le32(b, p, si);
        p = le32(b, p, e.getEntityId());
        p = le16(b, p, Item.getIdFromItem(s.getItem()));
        p = le16(b, p, s.getItemDamage());
        b[p++] = (byte)s.stackSize;
        b[p++] = 0;
        p = le64(b, p, Double.doubleToRawLongBits(e.posX));
        p = le64(b, p, Double.doubleToRawLongBits(e.posY));
        p = le64(b, p, Double.doubleToRawLongBits(e.posZ));
        p = le64(b, p, Double.doubleToRawLongBits(e.motionX));
        p = le64(b, p, Double.doubleToRawLongBits(e.motionY));
        p = le64(b, p, Double.doubleToRawLongBits(e.motionZ));
        p = le32(b, p, Float.floatToRawIntBits(e.rotationYaw));
        p = le32(b, p, Float.floatToRawIntBits(e.hoverStart));
        p = le32(b, p, e.delayBeforeCanPickup);
        p = le32(b, p, e.age);

        if (p != DROP_BYTES) throw new IllegalStateException("drop record " + p);

        out.write(b);
    }

    static int fallRecord(byte[] b, int t, int si, EntityFallingBlock f, Field hurtField, Field brokeField, Field hurtMaxField, Field hurtAmtField, Field fireField) throws Exception
    {
        int p = 0;
        p = le32(b, p, t);
        p = le32(b, p, si);
        p = le32(b, p, f.getEntityId());
        p = le64(b, p, Double.doubleToRawLongBits(f.posX));
        p = le64(b, p, Double.doubleToRawLongBits(f.posY));
        p = le64(b, p, Double.doubleToRawLongBits(f.posZ));
        p = le64(b, p, Double.doubleToRawLongBits(f.motionX));
        p = le64(b, p, Double.doubleToRawLongBits(f.motionY));
        p = le64(b, p, Double.doubleToRawLongBits(f.motionZ));
        p = le32(b, p, Float.floatToRawIntBits(f.rotationYaw));
        p = le32(b, p, Float.floatToRawIntBits(f.fallDistance));
        b[p++] = (byte)(f.onGround ? 1 : 0);
        b[p++] = (byte)(f.isCollidedHorizontally ? 1 : 0);
        b[p++] = (byte)(f.isCollidedVertically ? 1 : 0);
        b[p++] = (byte)(f.velocityChanged ? 1 : 0);
        p = le32(b, p, f.field_145812_b);
        p = le16(b, p, Block.getIdFromBlock(f.func_145805_f()));
        b[p++] = (byte)f.field_145814_a;
        b[p++] = (byte)(f.field_145813_c ? 1 : 0);
        b[p++] = (byte)(hurtField.getBoolean(f) ? 1 : 0);
        p = le32(b, p, Float.floatToRawIntBits(hurtAmtField.getFloat(f)));
        p = le32(b, p, hurtMaxField.getInt(f));
        p = le32(b, p, fireField.getInt(f));
        p = le32(b, p, f.ticksExisted);

        if (p != FALL_BYTES) throw new IllegalStateException("fall record " + p);

        return FALL_BYTES;
    }

    static int hangRecord(byte[] b, int t, int si, EntityHanging h, Field counterField) throws Exception
    {
        int p = 0;
        boolean painting = h instanceof EntityPainting;
        EntityItemFrame frame = h instanceof EntityItemFrame ? (EntityItemFrame)h : null;
        p = le32(b, p, t);
        p = le32(b, p, si);
        p = le32(b, p, h.getEntityId());
        p = le64(b, p, Double.doubleToRawLongBits(h.posX));
        p = le64(b, p, Double.doubleToRawLongBits(h.posY));
        p = le64(b, p, Double.doubleToRawLongBits(h.posZ));
        p = le32(b, p, Float.floatToRawIntBits(h.rotationYaw));
        b[p++] = (byte)h.hangingDirection;
        p += 3;
        p = le32(b, p, counterField.getInt(h));
        p = le32(b, p, h.field_146063_b);
        p = le32(b, p, h.field_146064_c);
        p = le32(b, p, h.field_146062_d);
        p = le16(b, p, painting ? ((EntityPainting)h).art.ordinal() : 0xffff);
        b[p++] = frame != null ? (byte)frame.getRotation() : 0;
        p = le16(b, p, frame != null && frame.getDisplayedItem() != null ? Item.getIdFromItem(frame.getDisplayedItem().getItem()) : 0xffff);
        p = le16(b, p, frame != null && frame.getDisplayedItem() != null ? frame.getDisplayedItem().getItemDamage() : 0);
        p = le32(b, p, h.ticksExisted);

        if (p != HANG_BYTES) throw new IllegalStateException("hang record " + p);

        return HANG_BYTES;
    }

    /** The digest line: the Det states per role, the OTHER next id, the
     * world Random's state and the pending tick list size. */
    static void writeDigest(PrintWriter w, int t, WorldServer ws, Field pendingField) throws Exception
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

        b.append(" worldRand ").append(hex(worldRandState(ws.rand)));
        b.append(" pending ").append(((Set<?>)pendingField.get(ws)).size());
        w.println(b.toString());
    }

    static void writeNbt(PrintWriter w, int t, List<Entity> list, Map<Entity, Integer> spawnIndex)
    {
        w.println("t " + t);

        for (Entity e : list)
        {
            NBTTagCompound tag = new NBTTagCompound();
            e.writeToNBT(tag);
            w.println(spawnIndex.get(e).intValue() + " " + StructuresProbe.canon(tag).toString());
        }
    }

    /** FNV-1a 64 over the 9x9 chunks from (chx, chz), dx outer 0..8, dz
     * inner 0..8, the setblock probe's chunk bytes. */
    static long hashRegion(WorldServer ws, int chx, int chz) throws Exception
    {
        byte[] buf = new byte[Probe.CHUNK_BYTES];
        long h = Probe.FNV_OFFSET;

        for (int dx = 0; dx <= 8; ++dx)
        {
            for (int dz = 0; dz <= 8; ++dz)
            {
                int ax = chx + dx, az = chz + dz;

                if (!ws.getChunkProvider().chunkExists(ax, az))
                {
                    h *= Probe.FNV_PRIME;
                    continue;
                }

                Probe.fillChunkBytes(ws.getChunkFromChunkCoords(ax, az), buf);

                for (int i = 0; i < buf.length; ++i) h = (h ^ (buf[i] & 255)) * Probe.FNV_PRIME;
            }
        }

        return h;
    }

    static Field field(Class<?> c, String name) throws Exception
    {
        Field f = c.getDeclaredField(name);
        f.setAccessible(true);
        return f;
    }

    static Field seedField;

    /** java.util.Random's 48-bit internal state. */
    static long worldRandState(Random rand) throws Exception
    {
        if (seedField == null)
        {
            seedField = Random.class.getDeclaredField("seed");
            seedField.setAccessible(true);
        }

        return ((java.util.concurrent.atomic.AtomicLong)seedField.get(rand)).get() & 0xffffffffffffL;
    }

    static String hex(long bits)
    {
        StringBuilder b = new StringBuilder(16);

        for (int i = 15; i >= 0; --i) b.append("0123456789abcdef".charAt((int)((bits >>> (i * 4)) & 15L)));

        return b.toString();
    }

    static void writeDetState(File f, long seed) throws Exception
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

        w.close();
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

    static void writeLe32(OutputStream o, int v) throws IOException
    {
        o.write(v & 255);
        o.write(v >> 8 & 255);
        o.write(v >> 16 & 255);
        o.write(v >> 24 & 255);
    }
}
