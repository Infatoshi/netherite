package netherite.oracle;

import com.google.gson.JsonObject;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.boss.EntityDragon;
import net.minecraft.entity.item.EntityEnderCrystal;
import net.minecraft.entity.item.EntityFallingBlock;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityItemFrame;
import net.minecraft.entity.item.EntityPainting;
import net.minecraft.entity.item.EntityTNTPrimed;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.entity.projectile.EntityLargeFireball;
import net.minecraft.entity.projectile.EntitySmallFireball;
import net.minecraft.init.Blocks;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.Vec3;
import net.minecraft.world.World;
import net.minecraft.world.chunk.Chunk;
import net.minecraft.world.chunk.storage.ExtendedBlockStorage;

/**
 * The pick and the ray shapes, the reference for csrc/engine/raytrace.c
 * (raytrace_collision, raytrace_blocks) and csrc/engine/pick.c (the entity
 * pass), checked by csrc/tests/test_raypick.c.
 *
 * Block cases. On the client world (WorldClient: the pick's world, where
 * BlockPortal never rewrites metadata), in the air 200 blocks up over the
 * player, a 5x5x5 room is written straight into ExtendedBlockStorage (no
 * onBlockAdded, no neighbour updates, no light): the target block at the
 * centre in one state, its neighbourhood drawn from a palette that exercises
 * the shapes that read neighbours (panes, fences, walls, chests, vines, doors,
 * the portal). Every state of every block with a state-dependent shape is a
 * target (stairs, rails, vines, ladders 2..5, trapdoors, panes and iron bars,
 * the portal, signs, skulls, snow layers, chests, cocoa, levers, pressure
 * plates, anvils, cakes, stems, the tripwire and its hook, slabs, doors,
 * buttons, fences, walls, fence gates, torches, pistons, beds, carpets and
 * the constant-shape ones), except the states whose shape is whatever the
 * shared Block object held last (a ladder at 0, 1, 6 up; a button or piston
 * direction 6 or 7; the moving piston). Each case fires seeded rays through
 * the room and records, per ray, Block.collisionRayTrace on the target,
 * World.func_147447_a(start, end, false, false, true) (the pick's
 * rayTrace) and func_147447_a(start, end, false, true, false) (the arrow's).
 *
 * Entity cases. Collidable and non-collidable entities of every kind the
 * pick meets (a zombie, large and small fireballs, falling blocks, primed
 * TNT, ender crystals, paintings, item frames, the dragon and its seven
 * parts, items, orbs, arrows) are built on the client world, placed with
 * setPosition (hanging ones by setDirection, dragon parts by their own
 * setPosition) and added to their chunks with Chunk.addEntity, so
 * getEntitiesWithinAABBExcludingEntity returns them in the game's order
 * (chunk x, chunk z, slice, then the dragon's parts after it). The entity
 * pass of EntityRenderer.getMouseOver then runs as written there, over that
 * list, for a synthetic viewer (eye, look, the viewer box the search region
 * grows from, the reach 3.0, the block distance var4 and whether
 * objectMouseOver was set), and the case records every entity's
 * canBeCollidedWith, getCollisionBorderSize and bounding box, the list, and
 * the pass's pick: the entity, var9 (the hit vector) and var12.
 *
 * Run: make run SEED=1 CLASS=RayProbe NAME=rp-1 CMD='{"out":"/abs/out/java/raypick/rp-1"}'
 *
 * cases.bin.gz, little endian: per block case a byte 1, int id, int meta,
 * 125 cells (dx, dy, dz in -2..2, x outer, z inner) of u16 id and u8 meta,
 * the six bounds setBlockBoundsBasedOnState leaves (doubles), int rays, and
 * per ray six doubles (start, end) and three results (u8 type 0 null, 1 block,
 * 2 miss; int x, y, z; u8 side; three doubles hitVec). Per entity case a
 * byte 2, int n, per entity (u8 kind, int a, int b, int c, int d, three
 * doubles position, u8 collidable, float border, six doubles box), int m and
 * the m list indices, the eye, the look, the viewer box, var2, var4, u8
 * haveMouseOver, then int chosen (-1 none), three doubles var9, double var12,
 * u8 replaced. A byte 0 ends the file.
 */
final class RayProbe
{
    private RayProbe() {}

    /* entity kinds, as test_raypick numbers them */
    static final int K_ZOMBIE = 1, K_LARGE_FIREBALL = 2, K_SMALL_FIREBALL = 3, K_FALLING = 4, K_TNT = 5,
        K_CRYSTAL = 6, K_PAINTING = 7, K_FRAME = 8, K_DRAGON = 9, K_PART = 10, K_ITEM = 11, K_ORB = 12,
        K_ARROW = 13;

    static DataOutputStream out;

    static void i32(int v) throws Exception { out.writeInt(Integer.reverseBytes(v)); }
    static void f64(double v) throws Exception { out.writeLong(Long.reverseBytes(Double.doubleToRawLongBits(v))); }
    static void f32(float v) throws Exception { out.writeInt(Integer.reverseBytes(Float.floatToRawIntBits(v))); }
    static void u8(int v) throws Exception { out.writeByte(v); }
    static void u16(int v) throws Exception { out.writeShort(Short.reverseBytes((short)v)); }

    static JsonObject run(IntegratedServer server, JsonObject cmd) throws Exception
    {
        File d = new File(cmd.get("out").getAsString());
        d.mkdirs();
        long seed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 1L;
        int rays = cmd.has("rays") ? cmd.get("rays").getAsInt() : 12;
        int entityCases = cmd.has("entity_cases") ? cmd.get("entity_cases").getAsInt() : 1500;

        Minecraft mc = Minecraft.getMinecraft();
        World w = mc.theWorld;
        int px = (int)Math.floor(mc.thePlayer.posX), pz = (int)Math.floor(mc.thePlayer.posZ);
        int py = 200;
        Random r = new Random(seed);
        /* the room's chunks, made on the client (an S21 does the same) when
         * the join has not sent them yet */
        for (int cx = (px - 12) >> 4; cx <= (px + 12) >> 4; ++cx)
            for (int cz = (pz - 12) >> 4; cz <= (pz + 12) >> 4; ++cz)
                if (w.getChunkFromChunkCoords(cx, cz) instanceof net.minecraft.world.chunk.EmptyChunk) ((WorldClient)w).doPreChunk(cx, cz, true);

        out = new DataOutputStream(new BufferedOutputStream(new GZIPOutputStream(new FileOutputStream(new File(d, "cases.bin.gz")))));
        int blockCases = 0, rayCount = 0, hits = 0, stairsHits = 0;

        List<int[]> targets = targets();
        for (int[] t : targets)
        {
            int id = t[0], meta = t[1];
            int rooms = neighbourSensitive(id) ? 6 : 2;
            for (int room = 0; room < rooms; ++room)
            {
                int[][][] ids = new int[5][5][5], metas = new int[5][5][5];
                fillRoom(r, id, meta, ids, metas, room);
                for (int a = 0; a < 5; ++a)
                    for (int b = 0; b < 5; ++b)
                        for (int c = 0; c < 5; ++c)
                            setRaw(w, px + a - 2, py + b - 2, pz + c - 2, ids[a][b][c], metas[a][b][c]);

                Block blk = w.getBlock(px, py, pz);
                u8(1);
                i32(id);
                i32(meta);
                for (int a = 0; a < 5; ++a)
                    for (int b = 0; b < 5; ++b)
                        for (int c = 0; c < 5; ++c)
                        {
                            u16(ids[a][b][c]);
                            u8(metas[a][b][c]);
                        }
                blk.setBlockBoundsBasedOnState(w, px, py, pz);
                f64(blk.getBlockBoundsMinX()); f64(blk.getBlockBoundsMinY()); f64(blk.getBlockBoundsMinZ());
                f64(blk.getBlockBoundsMaxX()); f64(blk.getBlockBoundsMaxY()); f64(blk.getBlockBoundsMaxZ());
                i32(rays);
                for (int k = 0; k < rays; ++k)
                {
                    double[] se = ray(r, px, py, pz);
                    for (int q = 0; q < 6; ++q) f64(se[q]);
                    MovingObjectPosition m1 = blk.collisionRayTrace(w, px, py, pz,
                        Vec3.createVectorHelper(se[0], se[1], se[2]), Vec3.createVectorHelper(se[3], se[4], se[5]));
                    MovingObjectPosition m2 = w.func_147447_a(Vec3.createVectorHelper(se[0], se[1], se[2]),
                        Vec3.createVectorHelper(se[3], se[4], se[5]), false, false, true);
                    MovingObjectPosition m3 = w.func_147447_a(Vec3.createVectorHelper(se[0], se[1], se[2]),
                        Vec3.createVectorHelper(se[3], se[4], se[5]), false, true, false);
                    mop(m1);
                    mop(m2);
                    mop(m3);
                    ++rayCount;
                    if (m1 != null)
                    {
                        ++hits;
                        if (Block.getIdFromBlock(blk) == 53 || blk instanceof net.minecraft.block.BlockStairs) ++stairsHits;
                    }
                }
                ++blockCases;
            }
        }
        for (int a = 0; a < 5; ++a)
            for (int b = 0; b < 5; ++b)
                for (int c = 0; c < 5; ++c)
                    setRaw(w, px + a - 2, py + b - 2, pz + c - 2, 0, 0);

        int picked = 0, byKind[] = new int[14];
        for (int k = 0; k < entityCases; ++k)
        {
            int got = entityCase(r, w, px + 0.5, py + 0.5, pz + 0.5);
            if (got > 0)
            {
                ++picked;
                ++byKind[got];
            }
        }
        u8(0);
        out.close();

        JsonObject m = new JsonObject();
        m.addProperty("kind", "raypick");
        m.addProperty("seed", server.worldServers[0].getSeed());
        m.addProperty("opseed", seed);
        m.addProperty("origin", px + "," + py + "," + pz);
        m.addProperty("block_cases", blockCases);
        m.addProperty("rays", rayCount);
        m.addProperty("hits", hits);
        m.addProperty("stairs_hits", stairsHits);
        m.addProperty("entity_cases", entityCases);
        m.addProperty("entity_picks", picked);
        StringBuilder sb = new StringBuilder();
        for (int i = 1; i < byKind.length; ++i) sb.append(i > 1 ? "," : "").append(byKind[i]);
        m.addProperty("picks_by_kind", sb.toString());
        Snapshot.writeFile(new File(d, "manifest.json"), m.toString());
        return m;
    }

    static void mop(MovingObjectPosition m) throws Exception
    {
        if (m == null)
        {
            u8(0); i32(0); i32(0); i32(0); u8(0); f64(0); f64(0); f64(0);
            return;
        }
        u8(m.typeOfHit == MovingObjectPosition.MovingObjectType.BLOCK ? 1 : 2);
        i32(m.blockX); i32(m.blockY); i32(m.blockZ);
        u8(m.sideHit);
        f64(m.hitVec.xCoord); f64(m.hitVec.yCoord); f64(m.hitVec.zCoord);
    }

    /** ExtendedBlockStorage writes: no callbacks, no light, no tile entity. */
    static void setRaw(World w, int x, int y, int z, int id, int meta)
    {
        Chunk ch = w.getChunkFromChunkCoords(x >> 4, z >> 4);
        ExtendedBlockStorage[] st = ch.getBlockStorageArray();
        if (st[y >> 4] == null)
        {
            if (id == 0) return;
            st[y >> 4] = new ExtendedBlockStorage(y >> 4 << 4, !w.provider.hasNoSky);
        }
        st[y >> 4].func_150818_a(x & 15, y & 15, z & 15, Block.getBlockById(id));
        st[y >> 4].setExtBlockMetadata(x & 15, y & 15, z & 15, meta);
    }

    static final int[] STAIRS = {53, 67, 108, 109, 114, 128, 134, 135, 136, 156, 163, 164};

    static List<int[]> targets()
    {
        List<int[]> t = new ArrayList<int[]>();
        int[] allMetas = {27, 28, 66, 157, 106, 96, 101, 102, 160, 90, 63, 68, 144, 78, 54, 146, 127, 69,
            70, 72, 147, 148, 145, 92, 104, 105, 132, 131, 44, 126, 64, 71, 85, 113, 139, 107, 50, 75, 76,
            26, 171, 151, 154, 119, 175, 43, 125};
        for (int id : STAIRS)
            for (int m = 0; m < 16; ++m) t.add(new int[] {id, m});
        for (int id : allMetas)
            for (int m = 0; m < 16; ++m) t.add(new int[] {id, m});
        for (int m = 2; m <= 5; ++m) t.add(new int[] {65, m});
        for (int id : new int[] {77, 143})
            for (int m : new int[] {1, 2, 3, 4, 9, 10, 11, 12}) t.add(new int[] {id, m});
        for (int id : new int[] {29, 33, 34})
            for (int m : new int[] {0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13}) t.add(new int[] {id, m});
        for (int id : new int[] {1, 20, 18, 30, 31, 37, 59, 81, 88, 111, 116, 130, 140, 122})
            t.add(new int[] {id, 0});
        return t;
    }

    static boolean neighbourSensitive(int id)
    {
        switch (id)
        {
            case 101: case 102: case 160: case 85: case 113: case 139: case 54: case 146: case 106:
            case 64: case 71: case 90:
                return true;
        }
        for (int s : STAIRS) if (s == id) return true;
        return false;
    }

    /** What the room's other cells hold: mostly air, then the blocks the
     * shapes above read as neighbours. */
    static final int[] PALETTE = {1, 20, 95, 101, 102, 160, 85, 113, 107, 139, 54, 146, 53, 67, 18, 3, 90,
        13, 44, 65, 106, 50, 78, 89, 5};

    static void fillRoom(Random r, int id, int meta, int[][][] ids, int[][][] metas, int room)
    {
        double air = room == 0 ? 1.0 : 0.55;
        for (int a = 0; a < 5; ++a)
            for (int b = 0; b < 5; ++b)
                for (int c = 0; c < 5; ++c)
                {
                    if (r.nextDouble() < air) continue;
                    ids[a][b][c] = PALETTE[r.nextInt(PALETTE.length)];
                    metas[a][b][c] = r.nextInt(16);
                    /* a ladder's shape at 0, 1 and 6 up is the shared
                     * Block's last one: only the four real facings */
                    if (ids[a][b][c] == 65) metas[a][b][c] = 2 + metas[a][b][c] % 4;
                }
        if (room > 0 && neighbourSensitive(id))
        {
            /* the four sides and above: often the same block, which the chest,
             * the panes, the fences and the portal read */
            int[][] side = {{1, 2, 2}, {3, 2, 2}, {2, 2, 1}, {2, 2, 3}, {2, 3, 2}};
            for (int[] s : side)
                if (r.nextInt(3) == 0)
                {
                    ids[s[0]][s[1]][s[2]] = id;
                    metas[s[0]][s[1]][s[2]] = r.nextInt(16);
                }
        }
        ids[2][2][2] = id;
        metas[2][2][2] = meta;
        if (id == 64 || id == 71)
        {
            if ((meta & 8) != 0)
            {
                ids[2][1][2] = id;
                metas[2][1][2] = r.nextInt(8);
            }
            else
            {
                ids[2][3][2] = id;
                metas[2][3][2] = 8 | r.nextInt(2);
            }
        }
    }

    static double snap16(double v)
    {
        return Math.floor(v * 16.0) / 16.0;
    }

    /** start and end of one ray through the room's centre cell. */
    static double[] ray(Random r, int px, int py, int pz)
    {
        double tx = px + r.nextDouble() * 1.5 - 0.25;
        double ty = py + r.nextDouble() * 1.5 - 0.25;
        double tz = pz + r.nextDouble() * 1.5 - 0.25;
        if (r.nextInt(4) == 0)
        {
            tx = snap16(tx);
            ty = snap16(ty);
            tz = snap16(tz);
        }
        double dx, dy, dz;
        if (r.nextInt(5) == 0)
        {
            do
            {
                dx = r.nextInt(3) - 1;
                dy = r.nextInt(3) - 1;
                dz = r.nextInt(3) - 1;
            }
            while (dx == 0 && dy == 0 && dz == 0);
        }
        else
        {
            dx = r.nextGaussian();
            dy = r.nextGaussian();
            dz = r.nextGaussian();
        }
        double len = Math.sqrt(dx * dx + dy * dy + dz * dz);
        dx /= len;
        dy /= len;
        dz /= len;
        double l1 = 0.1 + r.nextDouble() * 3.4, l2 = r.nextDouble() * 2.0;
        double[] se = {tx - dx * l1, ty - dy * l1, tz - dz * l1, tx + dx * l2, ty + dy * l2, tz + dz * l2};
        if (r.nextInt(8) == 0)
            for (int q = 0; q < 3; ++q) se[q] = snap16(se[q]);
        return se;
    }

    static final String[] ART = {"Kebab", "Pool", "Wanderer", "Match", "Fighters", "Pointer", "Skeleton"};

    /** One entity case; returns the chosen entity's kind (0 none). */
    static int entityCase(Random r, World w, double cx, double cy, double cz) throws Exception
    {
        List<Entity> ents = new ArrayList<Entity>();
        List<int[]> info = new ArrayList<int[]>();
        List<double[]> pos = new ArrayList<double[]>();
        List<Entity> chunked = new ArrayList<Entity>();
        int n = 1 + r.nextInt(5);
        double ex = cx + r.nextDouble() * 4.0 - 2.0, ey = cy + r.nextDouble() * 2.0 - 1.0, ez = cz + r.nextDouble() * 4.0 - 2.0;

        for (int i = 0; i < n; ++i)
        {
            int kind = 1 + r.nextInt(13);
            if (kind == K_PART) kind = K_DRAGON;
            double x = ex + r.nextDouble() * 7.0 - 3.5, y = ey + r.nextDouble() * 5.0 - 3.0, z = ez + r.nextDouble() * 7.0 - 3.5;
            if (r.nextInt(6) == 0)
            {
                x = ex + r.nextDouble() * 1.0 - 0.5;
                y = ey - r.nextDouble() * 1.5;
                z = ez + r.nextDouble() * 1.0 - 0.5;
            }
            if (r.nextInt(4) == 0)
            {
                x = Math.floor(x * 32.0) / 32.0;
                y = Math.floor(y * 32.0) / 32.0;
                z = Math.floor(z * 32.0) / 32.0;
            }
            Entity e;
            int a = 0, b = 0, c = 0, dd = 0;
            switch (kind)
            {
                case K_ZOMBIE: e = new EntityZombie(w); break;
                case K_LARGE_FIREBALL: e = new EntityLargeFireball(w, x, y, z, 0.0, 0.0, 1.0); break;
                case K_SMALL_FIREBALL: e = new EntitySmallFireball(w, x, y, z, 0.0, 0.0, 1.0); break;
                case K_FALLING: e = new EntityFallingBlock(w, x, y, z, Blocks.gravel, 0); a = 13; break;
                case K_TNT: e = new EntityTNTPrimed(w, x, y, z, null); break;
                case K_CRYSTAL: e = new EntityEnderCrystal(w, x, y, z); break;
                case K_ITEM: e = new EntityItem(w, x, y, z); break;
                case K_ORB: e = new EntityXPOrb(w, x, y, z, 1); break;
                case K_ARROW: e = new EntityArrow(w, x, y, z); break;
                case K_PAINTING: case K_FRAME:
                {
                    a = (int)Math.floor(x);
                    b = (int)Math.floor(y);
                    c = (int)Math.floor(z);
                    dd = r.nextInt(4);
                    if (kind == K_FRAME) e = new EntityItemFrame(w, a, b, c, dd);
                    else
                    {
                        EntityPainting p = new EntityPainting(w, a, b, c, dd, ART[r.nextInt(ART.length)]);
                        e = p;
                        dd |= p.art.ordinal() << 8;
                    }
                    break;
                }
                default: e = new EntityDragon(w); break;
            }
            if (kind != K_PAINTING && kind != K_FRAME) e.setPosition(x, y, z);
            ents.add(e);
            info.add(new int[] {kind, a, b, c, dd});
            pos.add(new double[] {e.posX, e.posY, e.posZ});
            addToChunk(w, e, chunked);
            if (kind == K_DRAGON)
            {
                Entity[] parts = e.getParts();
                for (int p = 0; p < parts.length; ++p)
                {
                    double qx = x + r.nextDouble() * 8.0 - 4.0, qy = y + r.nextDouble() * 4.0 - 2.0, qz = z + r.nextDouble() * 8.0 - 4.0;
                    parts[p].setPosition(qx, qy, qz);
                    ents.add(parts[p]);
                    info.add(new int[] {K_PART, p, 0, 0, 0});
                    pos.add(new double[] {qx, qy, qz});
                }
            }
        }

        /* the look: at an entity's box (a corner or edge now and then), or anywhere */
        double lx, ly, lz;
        if (r.nextInt(10) < 7)
        {
            Entity t = ents.get(r.nextInt(ents.size()));
            AxisAlignedBB bb = t.boundingBox;
            double gx = bb.minX + (bb.maxX - bb.minX) * r.nextDouble();
            double gy = bb.minY + (bb.maxY - bb.minY) * r.nextDouble();
            double gz = bb.minZ + (bb.maxZ - bb.minZ) * r.nextDouble();
            if (r.nextInt(4) == 0)
            {
                float border = t.getCollisionBorderSize();
                gx = r.nextBoolean() ? bb.minX - border : bb.maxX + border;
                gy = r.nextBoolean() ? bb.minY - border : bb.maxY + border;
            }
            lx = gx - ex;
            ly = gy - ey;
            lz = gz - ez;
        }
        else
        {
            lx = r.nextGaussian();
            ly = r.nextGaussian();
            lz = r.nextGaussian();
        }
        double ll = Math.sqrt(lx * lx + ly * ly + lz * lz);
        lx /= ll;
        ly /= ll;
        lz /= ll;

        double var2 = 3.0;
        boolean haveMo = r.nextInt(10) != 0;
        double var4 = haveMo ? (double)(float)(r.nextDouble() * 4.5) : var2;
        if (haveMo && r.nextInt(5) == 0) var4 = 0.0;
        double fy = ey - 1.62;
        AxisAlignedBB viewer = AxisAlignedBB.getBoundingBox(ex - 0.3, fy, ez - 0.3, ex + 0.3, fy + 1.8, ez + 0.3);

        /* EntityRenderer.getMouseOver's entity pass, as written there */
        Vec3 var6 = Vec3.createVectorHelper(ex, ey, ez);
        Vec3 var8 = var6.addVector(lx * var2, ly * var2, lz * var2);
        Entity pointed = null;
        Vec3 var9 = null;
        float var10 = 1.0F;
        List var11 = w.getEntitiesWithinAABBExcludingEntity(null, viewer.addCoord(lx * var2, ly * var2, lz * var2).expand((double)var10, (double)var10, (double)var10));
        double var12 = var4;

        for (int var14 = 0; var14 < var11.size(); ++var14)
        {
            Entity var15 = (Entity)var11.get(var14);

            if (var15.canBeCollidedWith())
            {
                float var16 = var15.getCollisionBorderSize();
                AxisAlignedBB var17 = var15.boundingBox.expand((double)var16, (double)var16, (double)var16);
                MovingObjectPosition var18 = var17.calculateIntercept(var6, var8);

                if (var17.isVecInside(var6))
                {
                    if (0.0D < var12 || var12 == 0.0D)
                    {
                        pointed = var15;
                        var9 = var18 == null ? var6 : var18.hitVec;
                        var12 = 0.0D;
                    }
                }
                else if (var18 != null)
                {
                    double var19 = var6.distanceTo(var18.hitVec);

                    if (var19 < var12 || var12 == 0.0D)
                    {
                        pointed = var15;
                        var9 = var18.hitVec;
                        var12 = var19;
                    }
                }
            }
        }
        boolean replaced = pointed != null && (var12 < var4 || !haveMo);

        u8(2);
        i32(ents.size());
        for (int i = 0; i < ents.size(); ++i)
        {
            Entity e = ents.get(i);
            int[] in = info.get(i);
            double[] p = pos.get(i);
            u8(in[0]);
            i32(in[1]); i32(in[2]); i32(in[3]); i32(in[4]);
            f64(p[0]); f64(p[1]); f64(p[2]);
            u8(e.canBeCollidedWith() ? 1 : 0);
            f32(e.getCollisionBorderSize());
            AxisAlignedBB bb = e.boundingBox;
            f64(bb.minX); f64(bb.minY); f64(bb.minZ); f64(bb.maxX); f64(bb.maxY); f64(bb.maxZ);
        }
        i32(var11.size());
        int chosen = -1;
        for (int i = 0; i < var11.size(); ++i)
        {
            int idx = ents.indexOf(var11.get(i));
            i32(idx);
        }
        if (pointed != null) chosen = ents.indexOf(pointed);
        f64(ex); f64(ey); f64(ez);
        f64(lx); f64(ly); f64(lz);
        f64(viewer.minX); f64(viewer.minY); f64(viewer.minZ); f64(viewer.maxX); f64(viewer.maxY); f64(viewer.maxZ);
        f64(var2);
        f64(var4);
        u8(haveMo ? 1 : 0);
        i32(chosen);
        f64(var9 == null ? 0.0 : var9.xCoord);
        f64(var9 == null ? 0.0 : var9.yCoord);
        f64(var9 == null ? 0.0 : var9.zCoord);
        f64(var12);
        u8(replaced ? 1 : 0);

        for (Entity e : chunked)
            w.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
        return chosen >= 0 && replaced ? info.get(chosen)[0] : 0;
    }

    static void addToChunk(World w, Entity e, List<Entity> chunked)
    {
        int cx = (int)Math.floor(e.posX / 16.0), cz = (int)Math.floor(e.posZ / 16.0);
        w.getChunkFromChunkCoords(cx, cz).addEntity(e);
        chunked.add(e);
    }
}

