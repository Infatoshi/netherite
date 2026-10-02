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
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.MathHelper;
import net.minecraft.world.WorldServer;

/**
 * The entity probe: item entities and XP orbs ticking in a bare region, the
 * reference for the native port of EntityItem.onUpdate and EntityXPOrb.onUpdate
 * (gravity, pushOutOfBlocks, moveEntity, slipperiness friction, the ground
 * bounce, the lava hop, fire and lava damage, water acceleration, the merge
 * search, despawn at age 6000).
 *
 * One case per run: a raw region (Probe.rawChunks, population off) far from
 * spawn, loaded cx-major like the move probe, scattered with the shape table
 * from Move.java plus ice, placed with setBlock flag 2 before anything else.
 * Then entities are spawned from one local Random(opseed) in groups of six:
 * four items, two orbs (kind = i % 3 == 2), one shared anchor so the same-item
 * stacks sit a couple of blocks apart and merge. The initial motion comes from
 * the same Random, and the entity constructors draw hoverStart / rotationYaw /
 * initial motion from Det's OTHER Math stream and their per-entity Random from
 * Det's OTHER seeder, so the native replay loads the three streams from
 * start.txt and spends them in spawn order.
 *
 * The tick is the World.updateEntities entity pass: in spawn order, one
 * updateEntityWithOptionalForce call per live entity per tick, dead entities
 * removed the way the world removes them (the chunk's list, then the loaded
 * list), nothing else in the world ticking. The server is parked while run()
 * executes on its own thread, so no other system touches the streams.
 *
 * Output DIR/manifest.json, DIR/shapes.bin, DIR/spawns.bin, DIR/ticks.bin.gz,
 * DIR/digest.txt.gz, DIR/removals.txt.gz, DIR/start.txt, DIR/end.txt.
 */
public final class EntityProbe
{
    /** The shapes table: Move.java's 31 rows plus ice (79), for slipperiness. */
    static final int[][] SHAPES = {
        {44, 0, 1}, {44, 8, 1}, {53, 0, 8}, {85, 0, 0}, {102, 0, 0}, {139, 0, 0},
        {78, 0, 8}, {81, 0, 0}, {88, 0, 0}, {60, 0, 0}, {65, 3, 1}, {65, 4, 1},
        {106, 0, 0}, {30, 0, 0}, {171, 0, 0}, {96, 0, 16}, {107, 0, 8}, {26, 0, 12},
        {92, 0, 0}, {20, 0, 0}, {1, 0, 0}, {9, 0, 0}, {11, 0, 0}, {106, 7, 1},
        {106, 8, 1}, {19, 0, 0}, {111, 0, 0}, {11, 2, 1}, {0, 0, 0}, {78, 0, 1},
        {92, 0, 0}, {79, 0, 0},
    };
    static final int SURFACE_BAND = 3;

    static final int SPAWN_BYTES = 90;
    static final int TICK_BYTES = 95;

    private EntityProbe() {}

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
        }, "Oracle EntityProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 1;
        int entities = cmd.has("entities") ? cmd.get("entities").getAsInt() : 240;
        int shapes = cmd.has("shapes") ? cmd.get("shapes").getAsInt() : 600;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 1200;
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
            shapesOut.write(shapesBuf, i * 16, 16);
        }

        shapesOut.close();

        // ------------------------------------------------------------- start
        // the Det state the spawn loop starts from, before the first spawn
        writeDetState(new File(dir, "start.txt"), seed);

        // ----------------------------------------------------------- entities
        int[] itemIds = itemIds();
        int nItems = itemIds.length;

        Field fireField = Entity.class.getDeclaredField("fire");
        fireField.setAccessible(true);
        Field healthField = EntityItem.class.getDeclaredField("health");
        healthField.setAccessible(true);
        Field orbHealthField = EntityXPOrb.class.getDeclaredField("xpOrbHealth");
        orbHealthField.setAccessible(true);

        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        byte[] spawnBuf = new byte[SPAWN_BYTES];
        OutputStream spawnsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        // The probe's own list, in spawn order. The world's loadedEntityList
        // also holds the initial mob spawns near the world spawn, and nothing
        // but these entities is ever ticked, so the pass walks this list the
        // way World.updateEntities walks its list.
        List<Entity> list = new ArrayList<Entity>();
        int ax = 0, az = 0, gItem = 0, gDamage = 0;

        // The liquid columns to drop entities into, measured after the shapes
        // are placed: columns whose surface cell (the height map) is water or
        // lava. Used by the every-17th entity, which is dropped straight onto
        // a liquid surface instead of next to the group anchor.
        int[] waterColumns = liquidColumns(ws, bx0, bz0, width, 9, 8);
        int[] lavaColumns = liquidColumns(ws, bx0, bz0, width, 11, 10);

        for (int i = 0; i < entities; ++i)
        {
            boolean orb = i % 3 == 2;

            // The group of six: one anchor, one shared item for the four item
            // entities of the group, all four thrown gently so the merge
            // search finds them; the group's first entity keeps a wide throw
            // so item motion coverage stays.
            if (i % 6 == 0)
            {
                ax = bx0 + r.nextInt(width);
                az = bz0 + r.nextInt(width);

                int pick = r.nextInt(nItems);
                Item git = Item.getItemById(itemIds[pick]);
                gItem = itemIds[pick];
                gDamage = git.getHasSubtypes() ? r.nextInt(16) : 0;
            }

            int x, z, y;
            boolean liquidDrop = i % 17 == 8 && i >= 6;
            boolean wantWater = i % 34 == 8;

            if (liquidDrop)
            {
                int[] cols = wantWater ? waterColumns : lavaColumns;

                if (cols.length > 0)
                {
                    int c = r.nextInt(cols.length / 2) * 2;
                    x = cols[c];
                    z = cols[c + 1];
                    y = clamp(ws.getHeightValue(x, z), 1, 254);
                }
                else
                {
                    x = ax + r.nextInt(3) - 1;
                    z = az + r.nextInt(3) - 1;
                    y = clamp(ws.getHeightValue(x, z) + r.nextInt(7) - 2, 1, 254);
                    liquidDrop = false;
                }
            }
            else
            {
                x = ax + r.nextInt(3) - 1;
                z = az + r.nextInt(3) - 1;
                y = clamp(ws.getHeightValue(x, z) + r.nextInt(7) - 2, 1, 254);
            }

            Entity e;
            int itemId = 0, damage = 0, count = 0, xp = 0;

            if (!orb)
            {
                itemId = gItem;
                damage = gDamage;
                count = r.nextInt(5) + 1;

                int delay = 0;
                if (i % 5 == 3) delay = r.nextInt(10);

                EntityItem ei = new EntityItem(ws, x + 0.5D, y, z + 0.5D);
                ei.setEntityItemStack(new ItemStack(Item.getItemById(itemId), count, damage));

                if (delay > 0) ei.delayBeforeCanPickup = delay;
                if (i % 13 == 5) ei.age = 6000 - r.nextInt(1200);

                e = ei;
            }
            else
            {
                xp = r.nextInt(250) + 1;
                EntityXPOrb xo = new EntityXPOrb(ws, x + 0.5D, y, z + 0.5D, xp);
                if (i % 13 == 5) xo.xpOrbAge = 6000 - r.nextInt(1200);
                e = xo;
            }

            double mx, my, mz;

            if (!orb && i % 6 != 0 && !liquidDrop)
            {
                mx = (r.nextDouble() - 0.5D) * 0.05D;
                my = r.nextDouble() * 0.02D;
                mz = (r.nextDouble() - 0.5D) * 0.05D;
            }
            else
            {
                mx = (r.nextDouble() - 0.5D) * 0.3D;
                my = r.nextDouble() * 0.2D;
                mz = (r.nextDouble() - 0.5D) * 0.3D;
            }

            e.motionX = mx;
            e.motionY = my;
            e.motionZ = mz;

            if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("entity " + i + " did not spawn at (" + x + "," + y + "," + z + ")");

            spawnIndex.put(e, Integer.valueOf(i));
            list.add(e);

            int p = 0;
            p = le32(spawnBuf, p, i);
            p = le32(spawnBuf, p, e.getEntityId());
            spawnBuf[p++] = (byte)(orb ? 2 : 1);
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posZ));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionX));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionY));
            p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionZ));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationYaw));
            p = le32(spawnBuf, p, Float.floatToRawIntBits(e instanceof EntityItem ? ((EntityItem)e).hoverStart : 0.0F));
            p = le32(spawnBuf, p, itemId);
            p = le32(spawnBuf, p, damage);
            p = le32(spawnBuf, p, count);
            p = le32(spawnBuf, p, xp);
            p = le32(spawnBuf, p, e instanceof EntityItem ? ((EntityItem)e).age : ((EntityXPOrb)e).xpOrbAge);
            p = le32(spawnBuf, p, e instanceof EntityItem ? ((EntityItem)e).delayBeforeCanPickup : ((EntityXPOrb)e).field_70532_c);
            spawnBuf[p++] = 0;
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

        int merges = 0, burns = 0, despawns = 0, voids = 0;
        int waterBBTicks = 0, lavaCellTicks = 0;

        for (int t = 0; t < ticks; ++t)
        {
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);

                if (e.isDead) continue;

                updateEntity(ws, e);

                if (e.isDead)
                {
                    int reason;
                    int health = e instanceof EntityItem ? healthField.getInt(e) : orbHealthField.getInt(e);
                    int age = e instanceof EntityItem ? ((EntityItem)e).age : ((EntityXPOrb)e).xpOrbAge;

                    if (health <= 0) { reason = 1; ++burns; }
                    else if (age >= 6000) { reason = 2; ++despawns; }
                    else if (e.posY < -64.0D) { reason = 3; ++voids; }
                    else { reason = 0; ++merges; }

                    // Remove the way the world's tick loop does: the chunk's
                    // list by the entity's last chunk coords, then the loaded
                    // list, so the next tick starts where vanilla is.
                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    list.remove(i--);

                    rw.println(t + " " + spawnIndex.get(e).intValue() + " " + e.getEntityId() + " " + reason + " " + health + " "
                        + fireField.getInt(e) + " " + age + " " + hex(Double.doubleToRawLongBits(e.posY)));
                }
            }

            // the state of every live entity, in the list's order
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);
                boolean item = e instanceof EntityItem;
                boolean waterBB = ws.isMaterialInBB(e.boundingBox, Material.water);
                boolean lavaBB = ws.isMaterialInBB(e.boundingBox.expand(-0.10000000149011612D, -0.4000000059604645D, -0.10000000149011612D), Material.lava);

                if (waterBB) ++waterBBTicks;
                if (ws.getBlock(MathHelper.floor_double(e.posX), MathHelper.floor_double(e.posY), MathHelper.floor_double(e.posZ)).getMaterial() == Material.lava) ++lavaCellTicks;

                byte[] rec = new byte[TICK_BYTES];
                int p = 0;
                p = le32(rec, p, t);
                Integer si = spawnIndex.get(e);
                if (si == null) throw new IllegalStateException("unknown entity " + e.getClass().getSimpleName() + " id " + e.getEntityId() + " at " + e.posX + "," + e.posY + "," + e.posZ);
                p = le32(rec, p, si.intValue());
                p = le32(rec, p, e.getEntityId());
                p = le64(rec, p, Double.doubleToRawLongBits(e.posX));
                p = le64(rec, p, Double.doubleToRawLongBits(e.posY));
                p = le64(rec, p, Double.doubleToRawLongBits(e.posZ));
                p = le64(rec, p, Double.doubleToRawLongBits(e.motionX));
                p = le64(rec, p, Double.doubleToRawLongBits(e.motionY));
                p = le64(rec, p, Double.doubleToRawLongBits(e.motionZ));
                p = le32(rec, p, Float.floatToRawIntBits(e.rotationYaw));
                p = le32(rec, p, item ? ((EntityItem)e).age : ((EntityXPOrb)e).xpOrbAge);
                p = le32(rec, p, item ? ((EntityItem)e).delayBeforeCanPickup : ((EntityXPOrb)e).field_70532_c);
                p = le32(rec, p, fireField.getInt(e));
                p = le32(rec, p, e.ticksExisted);
                rec[p++] = (byte)(e.onGround ? 1 : 0);
                rec[p++] = (byte)(e.isCollidedHorizontally ? 1 : 0);
                rec[p++] = (byte)(e.isCollidedVertically ? 1 : 0);
                rec[p++] = (byte)(e.noClip ? 1 : 0);
                rec[p++] = (byte)(waterBB ? 1 : 0);
                rec[p++] = (byte)(lavaBB ? 1 : 0);
                rec[p++] = (byte)(e.velocityChanged ? 1 : 0);
                p = le16(rec, p, item ? ((EntityItem)e).getEntityItem().getItemDamage() : ((EntityXPOrb)e).xpColor);
                p = le16(rec, p, item ? ((EntityItem)e).getEntityItem().stackSize : 0);
                p = le32(rec, p, 0);
                ticksOut.write(rec, 0, TICK_BYTES);
            }

            writeDetLine(dw, t);
        }

        ticksOut.close();
        ticksGz.close();
        dw.close();
        digestGz.close();
        rw.close();
        remGz.close();

        writeDetState(new File(dir, "end.txt"), seed);

        // ------------------------------------------------------------ manifest
        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "items");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("entities", entities);
        m.addProperty("shapes", shapes);
        m.addProperty("ticks", ticks);
        m.addProperty("opseed", opseed);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("shapes_table", Move.SHAPES.length + " rows, Move.java's table plus row 31 = ice (79, meta base 0, no width draw)");
        m.addProperty("shapes_layout", "16 bytes per shape, in placement order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8 (0..15), pad uint8 (always 0)");
        m.addProperty("shapes_draws", "per shape i from Random(opseed), in this order: x = (cx-radius)*16 + nextInt((2*radius+1)*16); z the same with cz; yDrawn = 55 + nextInt(46); pick = nextInt(32); meta = the row's base + nextInt(width) when the row's width is not 0, else the base. Then y = yDrawn when yDrawn is within SURFACE_BAND = " + SURFACE_BAND + " of the column surface (min(99, max(56, heightValue(x, z)))), and y = surface - 1 otherwise. Then setBlock(x, y, z, id, meta, 2)");
        m.addProperty("spawn_layout", SPAWN_BYTES + " bytes per spawn, in spawn order: spawn_index int32 LE, entity_id int32 LE, kind uint8 (1 item, 2 orb), x, y, z double LE (the position after the constructor's setPosition), motionX, motionY, motionZ double LE (after the probe's override), rotationYaw float32 LE, hoverStart float32 LE, item uint32 LE, damage uint32 LE, count uint32 LE, xp uint32 LE (0 for items), age int32 LE (age for items, xpOrbAge for orbs), delay uint32 LE (delayBeforeCanPickup for items, field_70532_c for orbs), pad uint8");
        m.addProperty("spawn_draws", "per entity i from Random(opseed), in this order: kind = (i % 3 == 2) is an orb, drawn by the index (no draw); when i % 6 == 0 the group start: ax = (cx-radius)*16 + nextInt((2*radius+1)*16), az the same with cz, then the group's stack: gPick = nextInt(nItems) (nItems = " + nItems + "), gDamage = nextInt(16) when the picked item has getHasSubtypes() else 0 (no further draw). The four item entities of the group all carry the group stack. Then the position: liquidDrop = (i % 17 == 8 && i >= 6) and wantWater = (i % 34 == 8), both by the index (no draws); a liquid drop picks cols[nextInt(cols.length / 2) * 2] from the precomputed water or lava column list and y = clamp(heightValue(x, z), 1, 254); anything else draws x = ax + nextInt(3) - 1, z = az + nextInt(3) - 1, y = clamp(heightValue(x, z) + nextInt(7) - 2, 1, 254) (also when the liquid list is empty, and liquidDrop is then false). For an item: count = nextInt(5) + 1; when i % 5 == 3, delayBeforeCanPickup = nextInt(10); when i % 13 == 5, age = 6000 - nextInt(1200). For an orb: xp = nextInt(250) + 1; when i % 13 == 5, xpOrbAge = 6000 - nextInt(1200). Then always the motion override: an item that is neither its group's first entity nor a liquid drop gets mx = (nextDouble() - 0.5) * 0.05, my = nextDouble() * 0.02, mz = (nextDouble() - 0.5) * 0.05 (so the group's four items stay close enough for the merge search); everything else, orbs and liquid drops included, gets mx = (nextDouble() - 0.5) * 0.3, my = nextDouble() * 0.2, mz = (nextDouble() - 0.5) * 0.3. The constructor's own draws are Det.math[OTHER] (four nextDouble per entity: EntityItem hoverStart, rotationYaw, motionX, motionZ; EntityXPOrb rotationYaw, motionX, motionY, motionZ), Det.seeder[OTHER] (Det.newRandom's nextLong and Det.uuid's two nextLong per entity) and the per-entity Random from Det.newRandom. The probe's Random(opseed) draws none of those. The liquid column lists are scanned once after the shapes are placed (x-major over the ops area, columns whose height-map cell is block 9 or 8 for water, 11 or 10 for lava) and cost no draws");
        m.addProperty("item_ids", "every id Item.getItemById returns a non-null item for, id 0..32000 ascending, " + nItems + " ids; stacks carry no tag compound");
        m.addProperty("tick_loop", "per tick t: for (i = 0; i < list.size(); ++i) { skip when isDead; updateEntityWithOptionalForce(e, true); when it died: the chunk's removeEntity by (chunkCoordX, chunkCoordZ) when addedToChunk and the chunk exists, then list.remove(i--), then the removal record; } then one tick record per surviving entity in the list's order, then the digest line. The list is the probe's own, in spawn order; the world's loadedEntityList also holds the world's initial mob spawns and is never walked");
        m.addProperty("tick_layout", "per record: tick int32 LE, spawn_index int32 LE, entity_id int32 LE, x, y, z double LE, motionX, motionY, motionZ double LE, yaw float32 LE, age int32 LE (age for items, xpOrbAge for orbs), delay int32 LE (delayBeforeCanPickup for items, field_70532_c for orbs), fire int32 LE, ticksExisted int32 LE, flags uint8 x 7 (onGround, isCollidedHorizontally, isCollidedVertically, noClip, waterBB, lavaBB, velocityChanged, each 0 or 1), short1 uint16 LE (damage for items, xpColor for orbs), short2 uint16 LE (count for items, 0 for orbs), pad uint32 LE; " + TICK_BYTES + " bytes per record");
        m.addProperty("water_and_lava_flags", "waterBB is World.isMaterialInBB(boundingBox, water) and lavaBB is isMaterialInBB(boundingBox.expand(-0.1, -0.4, -0.1), lava), both read-only scans recorded after the tick; they are the boxes handleWaterMovement's flow push and handleLavaMovement see");
        m.addProperty("removals", "removals.txt.gz, one line per removal at the tick it happened: <tick> <spawn_index> <entity_id> <reason> <health> <fire> <age> <posY as hex of the raw double bits>; reason 0 merged (died inside combineItems), 1 burned (health at or below 0, the fire or lava damage path), 2 despawned (age reached 6000), 3 void (posY below -64). Classification checks health first, then age, then posY");        m.addProperty("digest", "digest.txt.gz, one line per tick: t <tick>, then per role 0..3: role <r> seeder <hex> math <hex> split <hex>, then nextId <the OTHER role's next id>. The states are Det.seederState / mathState / splitState after the tick's pass");
        m.addProperty("start_end", "start.txt and end.txt, the DetProbe snapshot format (resetSeed, worldSeed, nextId, digest per role, split per registered stream), before the first spawn and after the last tick");
        m.addProperty("roles", "the probe runs on its own thread, so every Det draw in the run is the OTHER role's: the entity constructors (math), Det.newRandom and Det.uuid (seeder) and the per-entity Random the ticks draw from (rand)");
        m.addProperty("counts", "merges " + merges + ", burns " + burns + ", despawns " + despawns + ", void " + voids + ", waterBBTicks " + waterBBTicks + ", lavaCellTicks " + lavaCellTicks);
        m.addProperty("world_rand", "world.rand is never drawn: chunk generation runs before any Det stream moves (raw chunks, no population), setBlock's light pass draws nothing, and the entity pass reads no world Random");
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("entities", entities);
        res.addProperty("shapes", shapes);
        res.addProperty("ticks", ticks);
        res.addProperty("merges", merges);
        res.addProperty("burns", burns);
        res.addProperty("despawns", despawns);
        res.addProperty("void", voids);
        res.addProperty("waterBBTicks", waterBBTicks);
        res.addProperty("lavaCellTicks", lavaCellTicks);
        return res;
    }

    /**
     * The world's entity pass for one entity, exactly
     * World.updateEntityWithOptionalForce(e, true): the tick bookkeeping, the
     * update itself when the entity is in a chunk, then the chunk membership
     * update. The NaN restore and theriddenByEntity half of the Java method
     * have nothing to do here (no riding), and the world's own tick loop calls
     * this only for !isDead entities.
     */
    static void updateEntity(WorldServer ws, Entity e)
    {
        int var3 = MathHelper.floor_double(e.posX);
        int var4 = MathHelper.floor_double(e.posZ);
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
                ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
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

    /**
     * The x,z pairs of every column in the ops area whose surface cell (the
     * height map) is one of the two liquid block ids (static or flowing),
     * scanned after the shapes are placed, in x-major order.
     */
    static int[] liquidColumns(WorldServer ws, int bx0, int bz0, int width, int id0, int id1)
    {
        List<Integer> out = new ArrayList<Integer>();

        for (int x = bx0; x < bx0 + width; ++x)
        {
            for (int z = bz0; z < bz0 + width; ++z)
            {
                int y = ws.getHeightValue(x, z);
                int id = Block.getIdFromBlock(ws.getBlock(x, y, z));

                if (id == id0 || id == id1)
                {
                    out.add(Integer.valueOf(x));
                    out.add(Integer.valueOf(z));
                }
            }
        }

        int[] a = new int[out.size()];

        for (int i = 0; i < a.length; ++i) a[i] = out.get(i).intValue();

        return a;
    }

    /** Every id Item.getItemById returns a non-null item for, ascending. */
    static int[] itemIds()
    {
        List<Integer> out = new ArrayList<Integer>();

        for (int id = 0; id <= 32000; ++id)
        {
            if (Item.getItemById(id) != null) out.add(Integer.valueOf(id));
        }

        int[] a = new int[out.size()];

        for (int i = 0; i < a.length; ++i) a[i] = out.get(i).intValue();

        return a;
    }

    static int clamp(int v, int lo, int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    /** One digest line: the three Det states per role plus the OTHER next id. */
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

    /** The DetProbe snapshot format, at the call site. */
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
