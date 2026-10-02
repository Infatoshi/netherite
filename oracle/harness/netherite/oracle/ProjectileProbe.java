package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonPrimitive;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.passive.EntityChicken;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.entity.projectile.EntityEgg;
import net.minecraft.entity.projectile.EntityFireball;
import net.minecraft.entity.projectile.EntityLargeFireball;
import net.minecraft.entity.projectile.EntityPotion;
import net.minecraft.entity.projectile.EntitySmallFireball;
import net.minecraft.entity.projectile.EntitySnowball;
import net.minecraft.entity.projectile.EntityThrowable;
import net.minecraft.entity.item.EntityEnderPearl;
import net.minecraft.entity.item.EntityExpBottle;
import net.minecraft.init.Blocks;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.Vec3;
import net.minecraft.world.WorldServer;

public final class ProjectileProbe
{
    static final int[][] SHAPES = {
        {44, 0, 1},  {44, 8, 1},  {126, 0, 1}, {126, 8, 1},
        {85, 0, 0},  {113, 0, 0}, {139, 0, 0}, {139, 1, 0},
        {20, 0, 0},  {18, 0, 4},  {9, 0, 0},   {11, 0, 0},
        {1, 0, 0},   {4, 0, 0},   {5, 0, 0},   {12, 0, 0},
        {13, 0, 0},  {35, 0, 16}, {0, 0, 0}
    };
    static final int SURFACE_BAND = 3;

    static final int SPAWN_BYTES = 104;
    static final int TICK_BYTES = 144;

    private ProjectileProbe() {}

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
        }, "Oracle ProjectileProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

        public static class ProbeLargeFireball extends EntityLargeFireball
    {
        public ProbeLargeFireball(net.minecraft.world.World w, double x, double y, double z, double ax, double ay, double az)
        {
            super(w, x, y, z, ax, ay, az);
        }

        @Override
        protected void onImpact(MovingObjectPosition mop)
        {
            if (!this.worldObj.isClient)
            {
                if (mop.entityHit != null)
                {
                    mop.entityHit.attackEntityFrom(net.minecraft.util.DamageSource.causeFireballDamage(this, this.shootingEntity), 6.0F);
                }
                this.setDead();
            }
        }
    }

static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.has("cx") ? cmd.get("cx").getAsInt() : 100;
        int cz = cmd.has("cz") ? cmd.get("cz").getAsInt() : 100;
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 8;
        int nProj = cmd.has("projectiles") ? cmd.get("projectiles").getAsInt() : 3200;
        int shapes = cmd.has("shapes") ? cmd.get("shapes").getAsInt() : 600;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 1250;
        boolean keepChickens = cmd.has("keep_chickens") && cmd.get("keep_chickens").getAsBoolean();
        boolean eggOnly = cmd.has("egg_only") && cmd.get("egg_only").getAsBoolean();
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 7L;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

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
                JsonArray pair = new JsonArray();
                pair.add(new JsonPrimitive(lx));
                pair.add(new JsonPrimitive(lz));
                loaded.add(pair);
            }
        }

        int width = (2 * radius + 1) * 16;
        int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;
        Random r = new Random(opseed);

        // ------------------------------------------------------------ shapes
        byte[] shapesBuf = new byte[shapes * 16];
        DataOutputStream shapesOut = new DataOutputStream(new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16));

        for (int i = 0; i < shapes; ++i)
        {
            int x = bx0 + r.nextInt(width);
            int z = bz0 + r.nextInt(width);
            int yDrawn = 55 + r.nextInt(46);
            int pick = r.nextInt(SHAPES.length);
            int[] s = SHAPES[pick];
            int id = s[0];
            int meta = s[1] + (s[2] > 0 ? r.nextInt(s[2]) : 0);
            int surface = Math.min(99, Math.max(56, ws.getHeightValue(x, z)));
            int y = yDrawn <= surface + SURFACE_BAND && yDrawn >= surface - SURFACE_BAND ? yDrawn : surface - 1;

            ws.setBlock(x, y, z, Block.getBlockById(id), meta, 2);
            le32(shapesBuf, i * 16, x);
            le32(shapesBuf, i * 16 + 4, y);
            le32(shapesBuf, i * 16 + 8, z);
            le16(shapesBuf, i * 16 + 12, id);
            shapesBuf[i * 16 + 14] = (byte)meta;
            shapesBuf[i * 16 + 15] = 0;
            shapesOut.write(shapesBuf, i * 16, 16);
        }
        shapesOut.close();

        // ------------------------------------------------------------- start
        writeDetState(new File(dir, "start.txt"), seed);
        ws.rand = Det.newRandom();

        // Reflection fields
        Field fireField = Entity.class.getDeclaredField("fire");
        fireField.setAccessible(true);
        Field healthField = EntityItem.class.getDeclaredField("health");
        healthField.setAccessible(true);
        Field orbHealthField = EntityXPOrb.class.getDeclaredField("xpOrbHealth");
        orbHealthField.setAccessible(true);
        Field arrXTile = EntityArrow.class.getDeclaredField("field_145791_d");
        arrXTile.setAccessible(true);
        Field arrYTile = EntityArrow.class.getDeclaredField("field_145792_e");
        arrYTile.setAccessible(true);
        Field arrZTile = EntityArrow.class.getDeclaredField("field_145789_f");
        arrZTile.setAccessible(true);
        Field arrInTile = EntityArrow.class.getDeclaredField("field_145790_g");
        arrInTile.setAccessible(true);
        Field arrInData = EntityArrow.class.getDeclaredField("inData");
        arrInData.setAccessible(true);
        Field arrInGround = EntityArrow.class.getDeclaredField("inGround");
        arrInGround.setAccessible(true);
        Field arrShake = EntityArrow.class.getDeclaredField("arrowShake");
        arrShake.setAccessible(true);
        Field arrTicksInGround = EntityArrow.class.getDeclaredField("ticksInGround");
        arrTicksInGround.setAccessible(true);
        Field arrTicksInAir = EntityArrow.class.getDeclaredField("ticksInAir");
        arrTicksInAir.setAccessible(true);

        Field thrXTile = EntityThrowable.class.getDeclaredField("field_145788_c");
        thrXTile.setAccessible(true);
        Field thrYTile = EntityThrowable.class.getDeclaredField("field_145786_d");
        thrYTile.setAccessible(true);
        Field thrZTile = EntityThrowable.class.getDeclaredField("field_145787_e");
        thrZTile.setAccessible(true);
        Field thrInTile = EntityThrowable.class.getDeclaredField("field_145785_f");
        thrInTile.setAccessible(true);
        Field thrInGround = EntityThrowable.class.getDeclaredField("inGround");
        thrInGround.setAccessible(true);
        Field thrShake = EntityThrowable.class.getDeclaredField("throwableShake");
        thrShake.setAccessible(true);
        Field thrTicksInGround = EntityThrowable.class.getDeclaredField("ticksInGround");
        thrTicksInGround.setAccessible(true);
        Field thrTicksInAir = EntityThrowable.class.getDeclaredField("ticksInAir");
        thrTicksInAir.setAccessible(true);

        Field fbXTile = EntityFireball.class.getDeclaredField("field_145795_e");
        fbXTile.setAccessible(true);
        Field fbYTile = EntityFireball.class.getDeclaredField("field_145793_f");
        fbYTile.setAccessible(true);
        Field fbZTile = EntityFireball.class.getDeclaredField("field_145794_g");
        fbZTile.setAccessible(true);
        Field fbInTile = EntityFireball.class.getDeclaredField("field_145796_h");
        fbInTile.setAccessible(true);
        Field fbInGround = EntityFireball.class.getDeclaredField("inGround");
        fbInGround.setAccessible(true);
        Field fbTicksAlive = EntityFireball.class.getDeclaredField("ticksAlive");
        fbTicksAlive.setAccessible(true);
        Field fbTicksInAir = EntityFireball.class.getDeclaredField("ticksInAir");
        fbTicksInAir.setAccessible(true);

        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        byte[] spawnBuf = new byte[SPAWN_BYTES];
        OutputStream spawnsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        List<Entity> list = new ArrayList<Entity>();

        // 100 stationary EntityLargeFireball targets (spawn_index 0..99)
        List<EntityLargeFireball> targets = new ArrayList<EntityLargeFireball>();
        for (int k = 0; k < 100; ++k)
        {
            double tx = bx0 + 8 + r.nextInt(width - 16) + 0.5;
            double tz = bz0 + 8 + r.nextInt(width - 16) + 0.5;
            double ty = Math.min(200.0, Math.max(65.0, (double)ws.getHeightValue((int)tx, (int)tz) + 5.0 + r.nextInt(15)));

            EntityLargeFireball target = new ProbeLargeFireball(ws, tx, ty, tz, 0.0, 0.0, 0.0);
            target.motionX = target.motionY = target.motionZ = 0.0;
            target.accelerationX = target.accelerationY = target.accelerationZ = 0.0;
            if (!ws.spawnEntityInWorld(target)) throw new IllegalStateException("target did not spawn");
            targets.add(target);
            spawnIndex.put(target, Integer.valueOf(k));
            list.add(target);

            int p = 0;
            p = le32(spawnBuf, p, k);
            p = le32(spawnBuf, p, target.getEntityId());
            spawnBuf[p++] = 10; // IE_LARGE_FIREBALL
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.posX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.posY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.posZ));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.motionX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.motionY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.motionZ));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(target.rotationYaw));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(target.rotationPitch));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.accelerationX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.accelerationY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(target.accelerationZ));
            spawnBuf[p++] = 0;
            p = le32(spawnBuf, p, 0);
            while (p < SPAWN_BYTES) spawnBuf[p++] = 0;
            spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);
        }

        // 100 stationary EntityItem entities on the ground (spawn_index 100..199)
        for (int k = 0; k < 100; ++k)
        {
            int ix = eggOnly ? bx0 - 80 + k % 10 : bx0 + 8 + r.nextInt(width - 16);
            int iz = eggOnly ? bz0 - 80 + k / 10 : bz0 + 8 + r.nextInt(width - 16);
            int iy = Math.max(1, ws.getHeightValue(ix, iz));

            EntityItem item = new EntityItem(ws, (double)ix + 0.5, (double)iy, (double)iz + 0.5, new ItemStack(Blocks.stone, 1));
            item.motionX = item.motionY = item.motionZ = 0.0;
            if (!ws.spawnEntityInWorld(item)) throw new IllegalStateException("item did not spawn");
            int sidx = 100 + k;
            spawnIndex.put(item, Integer.valueOf(sidx));
            list.add(item);

            int p = 0;
            p = le32(spawnBuf, p, sidx);
            p = le32(spawnBuf, p, item.getEntityId());
            spawnBuf[p++] = 1; // IE_ITEM
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(item.posX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(item.posY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(item.posZ));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(item.motionX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(item.motionY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(item.motionZ));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(item.rotationYaw));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(item.rotationPitch));
            p = le64(spawnBuf, p, 0L);
            p = le64(spawnBuf, p, 0L);
            p = le64(spawnBuf, p, 0L);
            spawnBuf[p++] = 0;
            p = le32(spawnBuf, p, 0);
            while (p < SPAWN_BYTES) spawnBuf[p++] = 0;
            spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);
        }

        // 3200 projectiles (spawn_index 200..3399)
        int[] kindCounts = new int[13];
        int[] hitsBlocks = new int[13];
        int[] hitsEntities = new int[13];
        int[] stuckCounts = new int[13];
        int[] removedCounts = new int[13];

        for (int k = 0; k < nProj; ++k)
        {
            int sidx = 200 + k;
            int type = eggOnly ? 1 : k % 8; // 0..7
            int kind = 0;
            Entity e = null;
            boolean aimAtTarget = (k % 10 == 0);
            boolean shootDown = eggOnly || (type == 5 && (k / 8) % 5 == 0);

            double px, py, pz, dx, dy, dz;

            if (shootDown)
            {
                px = eggOnly ? bx0 + 2 + k % 25 * 3 + 0.5 : bx0 + 8 + r.nextInt(width - 16) + 0.5;
                pz = eggOnly ? bz0 + 2 + k / 25 * 3 + 0.5 : bz0 + 8 + r.nextInt(width - 16) + 0.5;
                py = (double)ws.getHeightValue((int)px, (int)pz) + 0.5;
                dx = 0.0; dy = -1.0; dz = 0.0;
            }
            else if (aimAtTarget)
            {
                EntityLargeFireball target = targets.get(k % 100);
                double dist = 4.0 + r.nextDouble() * 4.0;
                double theta = r.nextDouble() * 2.0 * Math.PI;
                px = target.posX + Math.cos(theta) * dist;
                pz = target.posZ + Math.sin(theta) * dist;
                py = target.posY + (r.nextDouble() - 0.5) * 2.0;
                dx = target.posX - px;
                dy = target.posY - py;
                dz = target.posZ - pz;
            }
            else
            {
                px = bx0 + 8 + r.nextInt(width - 16) + 0.5;
                pz = bz0 + 8 + r.nextInt(width - 16) + 0.5;
                py = (double)ws.getHeightValue((int)px, (int)pz) + 1.0 + r.nextInt(10);
                dx = (r.nextDouble() - 0.5) * 2.0;
                dy = (r.nextDouble() - 0.2) * 1.5;
                dz = (r.nextDouble() - 0.5) * 2.0;
            }

            int potionDmg = 0;
            boolean isCrit = false;

            switch (type)
            {
            case 0:
                kind = 4; // IE_SNOWBALL
                EntitySnowball sb = new EntitySnowball(ws, px, py, pz);
                sb.setThrowableHeading(dx, dy, dz, 1.5F, 1.0F);
                e = sb;
                break;
            case 1:
                kind = 5; // IE_EGG
                EntityEgg egg = new EntityEgg(ws, px, py, pz);
                egg.setThrowableHeading(dx, dy, dz, 1.5F, 1.0F);
                e = egg;

                break;
            case 2:
                kind = 6; // IE_ENDER_PEARL
                EntityEnderPearl ep = new EntityEnderPearl(ws, px, py, pz);
                ep.setThrowableHeading(dx, dy, dz, 1.5F, 1.0F);
                e = ep;
                break;
            case 3:
                kind = 7; // IE_EXP_BOTTLE
                EntityExpBottle eb = new EntityExpBottle(ws, px, py, pz);
                eb.setThrowableHeading(dx, dy, dz, 0.7F, 1.0F);
                e = eb;
                break;
            case 4:
                kind = 8; // IE_POTION
                potionDmg = 16385 + r.nextInt(16);
                EntityPotion pot = new EntityPotion(ws, px, py, pz, potionDmg);
                pot.setThrowableHeading(dx, dy, dz, 0.5F, 1.0F);
                e = pot;
                break;
            case 5:
                kind = 3; // IE_ARROW
                EntityArrow arr = new EntityArrow(ws, px, py, pz);
                if (k % 4 == 0) { isCrit = true; arr.setIsCritical(true); }
                arr.setThrowableHeading(dx, dy, dz, 3.0F, 1.0F);
                e = arr;
                break;
            case 6:
                kind = 10; // IE_LARGE_FIREBALL
                EntityLargeFireball lfb = new ProbeLargeFireball(ws, px, py, pz, dx, dy, dz);
                e = lfb;
                break;
            case 7:
                kind = 9; // IE_SMALL_FIREBALL
                EntitySmallFireball sfb = new EntitySmallFireball(ws, px, py, pz, dx, dy, dz);
                e = sfb;
                break;
            }

            if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("projectile did not spawn");
            spawnIndex.put(e, Integer.valueOf(sidx));
            list.add(e);
            ++kindCounts[kind];

            double ax = 0.0, ay = 0.0, az = 0.0;
            if (e instanceof EntityFireball)
            {
                EntityFireball fb = (EntityFireball)e;
                ax = fb.accelerationX; ay = fb.accelerationY; az = fb.accelerationZ;
            }

            int p = 0;
            p = le32(spawnBuf, p, sidx);
            p = le32(spawnBuf, p, e.getEntityId());
            spawnBuf[p++] = (byte)kind;
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posZ));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionZ));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationYaw));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationPitch));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(ax));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(ay));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(az));
            spawnBuf[p++] = (byte)(isCrit ? 1 : 0);
            p = le32(spawnBuf, p, potionDmg);
            while (p < SPAWN_BYTES) spawnBuf[p++] = 0;
            spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);
        }
        spawnsOut.close();

        // -------------------------------------------------------------- ticks
        GZIPOutputStream ticksGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "ticks.bin.gz")), 1 << 16);
        OutputStream ticksOut = new BufferedOutputStream(ticksGz, 1 << 16);
        GZIPOutputStream digestGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "digest.txt.gz")), 1 << 16);
        PrintWriter dw = new PrintWriter(new OutputStreamWriter(digestGz, "UTF-8"));
        GZIPOutputStream remGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "removals.txt.gz")), 1 << 16);
        PrintWriter rw = new PrintWriter(new OutputStreamWriter(remGz, "UTF-8"));
        GZIPOutputStream nbtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "nbt.jsonl.gz")), 1 << 16);
        PrintWriter nw = new PrintWriter(new OutputStreamWriter(nbtGz, "UTF-8"));
        PrintWriter cw = keepChickens ? new PrintWriter(new OutputStreamWriter(
            new GZIPOutputStream(new FileOutputStream(new File(dir, "chickens.txt.gz"))), "UTF-8")) : null;

        int nextSpawnIdx = 200 + nProj;
        int fourHatches = 0;
        byte[] tickBuf = new byte[TICK_BYTES];

        for (int t = 0; t < ticks; ++t)
        {
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);
                if (e.isDead) continue;

                int k = getKind(e);
                boolean wasInGround = isEntityInGround(e, arrInGround, thrInGround, fbInGround);
                int preLoaded = ws.loadedEntityList.size();
                updateEntity(ws, e);

                boolean nowInGround = isEntityInGround(e, arrInGround, thrInGround, fbInGround);
                if (!wasInGround && nowInGround)
                {
                    ++hitsBlocks[k];
                    if (e instanceof EntityArrow) ++stuckCounts[k];
                }

                // Check new entities spawned into worldObj (e.g. XP orbs from ExpBottle)
                int postLoaded = ws.loadedEntityList.size();
                if (keepChickens && e instanceof EntityEgg && postLoaded - preLoaded == 4) ++fourHatches;
                if (postLoaded > preLoaded)
                {
                    for (int n = preLoaded; n < postLoaded; ++n)
                    {
                        Entity ne = (Entity)ws.loadedEntityList.get(n);
                        if (ne instanceof EntityXPOrb || ne instanceof EntityItem ||
                            (keepChickens && ne instanceof EntityChicken))
                        {
                            int nidx = nextSpawnIdx++;
                            spawnIndex.put(ne, Integer.valueOf(nidx));
                            list.add(ne);
                            if (ne instanceof EntityChicken) ++kindCounts[12];
                        }
                        else
                        {
                            ne.setDead();
                            if (ne.addedToChunk && ws.theChunkProviderServer.chunkExists(ne.chunkCoordX, ne.chunkCoordZ))
                            {
                                ws.getChunkFromChunkCoords(ne.chunkCoordX, ne.chunkCoordZ).removeEntity(ne);
                            }
                        }
                    }
                }

                if (e.isDead)
                {
                    ++removedCounts[k];
                    int reason;
                    if (e instanceof EntityItem || e instanceof EntityXPOrb)
                    {
                        int health = e instanceof EntityItem ? healthField.getInt(e) : orbHealthField.getInt(e);
                        int age = e instanceof EntityItem ? ((EntityItem)e).age : ((EntityXPOrb)e).xpOrbAge;
                        if (health <= 0) reason = 1;
                        else if (age >= 6000) reason = 2;
                        else if (e.posY < -64.0) reason = 3;
                        else reason = 0;
                    }
                    else if (e instanceof EntityArrow && arrTicksInGround.getInt(e) >= 1200) reason = 5;
                    else if (e.posY < -64.0) reason = 3;
                    else reason = 4;

                    if (!wasInGround && !nowInGround && reason == 4)
                    {
                        // Died in air: hit an entity or block
                        // If it hit an entity, target took damage
                        // Check if any target fireball velocityChanged
                        boolean hitEnt = false;
                        for (EntityLargeFireball tfb : targets)
                        {
                            if (tfb.velocityChanged) { hitEnt = true; tfb.velocityChanged = false; break; }
                        }
                        if (hitEnt) ++hitsEntities[k];
                        else ++hitsBlocks[k];
                    }

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }
                    list.remove(i--);

                    Integer sidxObj = spawnIndex.get(e);
                    int sidx = sidxObj != null ? sidxObj.intValue() : -1;
                    rw.println(t + " " + sidx + " " + e.getEntityId() + " " + reason + " " + 0 + " "
                        + fireField.getInt(e) + " " + 0 + " " + hex(Double.doubleToRawLongBits(e.posY)));
                }
            }

            // Write tick record for every live entity
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);
                Integer sidxObj = spawnIndex.get(e);
                int sidx = sidxObj != null ? sidxObj.intValue() : -1;
                int k = getKind(e);

                boolean inGround = isEntityInGround(e, arrInGround, thrInGround, fbInGround);
                int shake = 0, tig = 0, tia = 0, tx = -1, ty = -1, tz = -1, inTile = 0, inData = 0;
                double ax = 0.0, ay = 0.0, az = 0.0;
                boolean isCrit = false;

                if (e instanceof EntityArrow)
                {
                    EntityArrow a = (EntityArrow)e;
                    shake = arrShake.getInt(a);
                    tig = arrTicksInGround.getInt(a);
                    tia = arrTicksInAir.getInt(a);
                    tx = arrXTile.getInt(a);
                    ty = arrYTile.getInt(a);
                    tz = arrZTile.getInt(a);
                    Block b = (Block)arrInTile.get(a);
                    inTile = b != null ? Block.getIdFromBlock(b) : 0;
                    inData = arrInData.getInt(a);
                    isCrit = a.getIsCritical();
                }
                else if (e instanceof EntityThrowable)
                {
                    EntityThrowable th = (EntityThrowable)e;
                    shake = thrShake.getInt(th);
                    tig = thrTicksInGround.getInt(th);
                    tia = thrTicksInAir.getInt(th);
                    tx = thrXTile.getInt(th);
                    ty = thrYTile.getInt(th);
                    tz = thrZTile.getInt(th);
                    Block b = (Block)thrInTile.get(th);
                    inTile = b != null ? Block.getIdFromBlock(b) : 0;
                }
                else if (e instanceof EntityFireball)
                {
                    EntityFireball fb = (EntityFireball)e;
                    tig = fbTicksAlive.getInt(fb);
                    tia = fbTicksInAir.getInt(fb);
                    tx = fbXTile.getInt(fb);
                    ty = fbYTile.getInt(fb);
                    tz = fbZTile.getInt(fb);
                    Block b = (Block)fbInTile.get(fb);
                    inTile = b != null ? Block.getIdFromBlock(b) : 0;
                    ax = fb.accelerationX; ay = fb.accelerationY; az = fb.accelerationZ;
                }

                int flags = (e.onGround ? 1 : 0) | (e.isInWater() ? 2 : 0) | (isCrit ? 4 : 0);

                int p = 0;
                p = le32(tickBuf, p, t);
                p = le32(tickBuf, p, sidx);
                p = le32(tickBuf, p, e.getEntityId());
                tickBuf[p++] = (byte)k;
                p = le64(tickBuf, p, Double.doubleToRawLongBits(e.posX));
                p = le64(tickBuf, p, Double.doubleToRawLongBits(e.posY));
                p = le64(tickBuf, p, Double.doubleToRawLongBits(e.posZ));
                p = le64(tickBuf, p, Double.doubleToRawLongBits(e.motionX));
                p = le64(tickBuf, p, Double.doubleToRawLongBits(e.motionY));
                p = le64(tickBuf, p, Double.doubleToRawLongBits(e.motionZ));
                p = le32(tickBuf, p, Float.floatToRawIntBits(e.rotationYaw));
                p = le32(tickBuf, p, Float.floatToRawIntBits(e.rotationPitch));
                tickBuf[p++] = (byte)(inGround ? 1 : 0);
                tickBuf[p++] = (byte)shake;
                p = le32(tickBuf, p, tig);
                p = le32(tickBuf, p, tia);
                p = le32(tickBuf, p, e.ticksExisted);
                p = le32(tickBuf, p, tx);
                p = le32(tickBuf, p, ty);
                p = le32(tickBuf, p, tz);
                p = le32(tickBuf, p, inTile);
                p = le32(tickBuf, p, inData);
                p = le32(tickBuf, p, fireField.getInt(e));
                tickBuf[p++] = (byte)flags;
                p = le64(tickBuf, p, Double.doubleToRawLongBits(ax));
                p = le64(tickBuf, p, Double.doubleToRawLongBits(ay));
                p = le64(tickBuf, p, Double.doubleToRawLongBits(az));
                while (p < TICK_BYTES) tickBuf[p++] = 0;
                ticksOut.write(tickBuf, 0, TICK_BYTES);

                // Canonical NBT line
                NBTTagCompound tag = new NBTTagCompound();
                e.writeToNBT(tag);
                JsonElement canonElem = StructuresProbe.canon(tag);
                JsonObject nbtRow = new JsonObject();
                nbtRow.addProperty("t", t);
                nbtRow.addProperty("i", sidx);
                nbtRow.add("nbt", canonElem);
                nw.println(nbtRow.toString());
                if (cw != null && e instanceof EntityChicken)
                {
                    EntityChicken chicken = (EntityChicken)e;
                    cw.println(t + " " + sidx + " " + chicken.getGrowingAge() + " " +
                               chicken.timeUntilNextEgg + " " +
                               hex(Det.state(AnimalProbe.entityRand(chicken))));
                }
            }

            writeDetLine(dw, t);
        }

        ticksOut.close();
        ticksGz.close();
        dw.close();
        digestGz.close();
        rw.close();
        remGz.close();
        nw.close();
        nbtGz.close();
        if (cw != null) cw.close();

        writeDetState(new File(dir, "end.txt"), seed);

        // ------------------------------------------------------------ manifest
        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "projectiles");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("projectiles", nProj);
        m.addProperty("shapes", shapes);
        m.addProperty("ticks", ticks);
        if (keepChickens) m.addProperty("keep_chickens", true);
        if (eggOnly) m.addProperty("egg_only", true);
        if (keepChickens) m.addProperty("hatched_chickens", kindCounts[12]);
        if (keepChickens) m.addProperty("four_chicken_impacts", fourHatches);

        JsonObject counts = new JsonObject();
        String[] kindNames = {"", "item", "orb", "arrow", "snowball", "egg", "ender_pearl", "exp_bottle", "potion", "small_fireball", "large_fireball"};
        for (int ki = 3; ki <= 10; ++ki)
        {
            JsonObject ko = new JsonObject();
            ko.addProperty("spawned", kindCounts[ki]);
            ko.addProperty("hits_blocks", hitsBlocks[ki]);
            ko.addProperty("hits_entities", hitsEntities[ki]);
            ko.addProperty("stuck", stuckCounts[ki]);
            ko.addProperty("removed", removedCounts[ki]);
            counts.add(kindNames[ki], ko);
        }
        m.add("counts", counts);

        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.add("counts", counts);
        return res;
    }

    static int getKind(Entity e)
    {
        if (e instanceof EntityArrow) return 3;
        if (e instanceof EntitySnowball) return 4;
        if (e instanceof EntityEgg) return 5;
        if (e instanceof EntityEnderPearl) return 6;
        if (e instanceof EntityExpBottle) return 7;
        if (e instanceof EntityPotion) return 8;
        if (e instanceof EntitySmallFireball) return 9;
        if (e instanceof EntityLargeFireball) return 10;
        if (e instanceof EntityXPOrb) return 2;
        if (e instanceof EntityItem) return 1;
        if (e instanceof EntityChicken) return 12;
        return 0;
    }

    static boolean isEntityInGround(Entity e, Field arr, Field thr, Field fb) throws Exception
    {
        if (e instanceof EntityArrow) return arr.getBoolean(e);
        if (e instanceof EntityThrowable) return thr.getBoolean(e);
        if (e instanceof EntityFireball) return fb.getBoolean(e);
        return false;
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

    static void writeDetLine(PrintWriter w, int t)
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

        w.println(b.toString());
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

    static String hex(long bits)
    {
        String s = Long.toHexString(bits);
        while (s.length() < 16) s = "0" + s;
        return s;
    }

    static int le16(byte[] b, int off, int v)
    {
        b[off] = (byte)v;
        b[off + 1] = (byte)(v >>> 8);
        return off + 2;
    }

    static int le32(byte[] b, int off, int v)
    {
        b[off] = (byte)v;
        b[off + 1] = (byte)(v >>> 8);
        b[off + 2] = (byte)(v >>> 16);
        b[off + 3] = (byte)(v >>> 24);
        return off + 4;
    }

    static int le64(byte[] b, int off, long v)
    {
        le32(b, off, (int)v);
        le32(b, off + 4, (int)(v >>> 32));
        return off + 8;
    }
}
