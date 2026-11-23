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
import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.world.WorldServer;

/**
 * The "move" kind of the probe: random Entity.moveEntity calls on raw terrain
 * scattered with the blocks whose collision boxes are not the full cube, as the
 * reference for the native port of the collision physics.
 *
 * The region is loaded exactly as the setblock kind loads one (population off,
 * cx-major with cz inner), so the terrain and the light state are the same kind
 * of bare world. Shapes are placed with setBlock inside the ops area, recorded
 * in shapes.bin, and never removed; the entity then walks through them.
 *
 * Every 50 moves the entity is teleported with setPosition to a new random spot
 * in the ops area, which is how the probe gets unstuck from a pocket it cannot
 * leave and how it covers the whole region. A teleport also resets motion,
 * fallDistance and onGround, so move N+1 starts from a clean state that the
 * native replay can reproduce from moves.bin alone.
 *
 * Nothing here draws from any other RNG: the ACG (r) is local to this probe,
 * ProbeEntity's constructor draws only the per-entity Det.seed() id, and the
 * entity is never ticked, so the CLIENT and SERVER streams do not move.
 */
final class Move
{
    /** Collision offsets a shape box is drawn around, so shapes overlap the walk line. */
    static final double[] BOX_OFFSETS = {0.0D, 0.25D, 0.5D, 0.75D, 1.0D, 1.25D, 1.5D, 1.75D};

    /**
     * How far a drawn y may sit from the terrain surface of its own column
     * before it is pulled down to surface - 1. Without this the 55..100 draw
     * leaves almost every shape floating far above the layer the entity walks
     * in on this terrain, and the collision scan never sees them.
     */
    static final int SURFACE_BAND = 3;

    /** Move sizes, picked by r.nextInt(4). */
    static final double[] SCALES = {0.05D, 0.3D, 1.0D, 3.0D};

    private Move() {}

    static JsonObject dump(final IntegratedServer server, final JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 1;
        int ops = cmd.has("ops") ? cmd.get("ops").getAsInt() : 5000;
        int shapes = cmd.has("shapes") ? cmd.get("shapes").getAsInt() : 400;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 1L;
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
            // Draw within the band the spec asks for (55..100) first, so the
            // draws stay in the recorded order, then pull the shape down onto
            // its own column's surface when the band draw would leave it
            // floating far above the only layer the entity walks in. A shape
            // is kept where it was drawn when the drawn y is within
            // SURFACE_BAND of the terrain surface of the column it lands on.
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
        SHAPE_RECORDS_READY = true;

        // How much of the scatter the entity has any chance of touching, measured
        // before a single move runs. A shape is usable when it sits at or just
        // below/above a terrain surface the entity can stand on: every teleport
        // puts it at a column's heightMap value plus -1..4, and it then falls,
        // so a shape within a few blocks of the height map of any column in the
        // ops area is reachable.
        int[] shapeX = new int[shapes], shapeY = new int[shapes], shapeZ = new int[shapes], shapeId = new int[shapes];
        for (int i = 0; i < shapes; ++i)
        {
            shapeX[i] = leRead32(shapesBuf, i * 16);
            shapeY[i] = leRead32(shapesBuf, i * 16 + 4);
            shapeZ[i] = leRead32(shapesBuf, i * 16 + 8);
            shapeId[i] = leRead16(shapesBuf, i * 16 + 12);
        }

        // The shape scatter is drawn over the ops area, not only the loaded
        // square, so the height maps are only read for shapes inside it.
        JsonObject reach = Move.reach(ws, shapeX, shapeY, shapeZ, shapeId, shapes, bx0, bz0, width);

        // ------------------------------------------------------------ entity
        ProbeEntity e = new ProbeEntity(ws);
        int sx = bx0 + r.nextInt(width), sz = bz0 + r.nextInt(width);
        e.setPosition(sx + 0.5D, (double)ws.getHeightValue(sx, sz), sz + 0.5D);
        if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("entity did not spawn at (" + sx + "," + sz + ")");

        // The recorded state after each move. All of these are public or
        // package-visible fields of Entity; fire is private, so it is read back
        // through the field it is declared as.
        Field fireField = Entity.class.getDeclaredField("fire");
        fireField.setAccessible(true);

        byte[] rec = new byte[MOVE_BYTES];
        OutputStream movesOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "moves.bin")), 1 << 16);
        int onGround = 0, collidedH = 0, stepped = 0, inWeb = 0, fire = 0, teleports = 0;
        int air = 0, climbed = 0;

        for (int i = 0; i < ops; ++i)
        {
            boolean tp = i % 50 == 0;

            if (tp)
            {
                int tx = bx0 + r.nextInt(width), tz = bz0 + r.nextInt(width);
                double ty = (double)ws.getHeightValue(tx, tz) + (double)(r.nextInt(6) - 1);
                e.setPosition((double)tx + 0.5D, ty, (double)tz + 0.5D);
                e.motionX = e.motionY = e.motionZ = 0.0D;
                e.fallDistance = 0.0F;
                e.onGround = false;
                ++teleports;
            }

            int sc = r.nextInt(4);
            double s = SCALES[sc];
            double dx = (r.nextDouble() - 0.5D) * s;
            double dz = (r.nextDouble() - 0.5D) * s;
            double dy = (r.nextDouble() - 0.7D) * s;

            boolean onGroundBefore = e.onGround;
            boolean collidedHorizontallyBefore = e.isCollidedHorizontally;
            double posYBefore = e.posY;
            e.moveEntity(dx, dy, dz);
            // The climb the step branch makes shows up as posY rising on a move
            // that had walked into a wall while on the ground. ySize is NOT the
            // signal: in this decompile only setPositionAndRotation writes
            // ySize, and moveEntity just decays it, so it stays 0 forever here.
            boolean steppedThisMove = e.posY > posYBefore + 1.0E-9D && onGroundBefore;

            AxisAlignedBB bb = e.boundingBox;
            int p = 0;
            rec[p++] = (byte)(tp ? 1 : 0);
            p = le64(rec, p, Double.doubleToRawLongBits(dx));
            p = le64(rec, p, Double.doubleToRawLongBits(dy));
            p = le64(rec, p, Double.doubleToRawLongBits(dz));
            p = le64(rec, p, Double.doubleToRawLongBits(e.posX));
            p = le64(rec, p, Double.doubleToRawLongBits(e.posY));
            p = le64(rec, p, Double.doubleToRawLongBits(e.posZ));
            p = le64(rec, p, Double.doubleToRawLongBits(bb.minX));
            p = le64(rec, p, Double.doubleToRawLongBits(bb.minY));
            p = le64(rec, p, Double.doubleToRawLongBits(bb.minZ));
            p = le64(rec, p, Double.doubleToRawLongBits(bb.maxX));
            p = le64(rec, p, Double.doubleToRawLongBits(bb.maxY));
            p = le64(rec, p, Double.doubleToRawLongBits(bb.maxZ));
            p = le64(rec, p, Double.doubleToRawLongBits(e.motionX));
            p = le64(rec, p, Double.doubleToRawLongBits(e.motionY));
            p = le64(rec, p, Double.doubleToRawLongBits(e.motionZ));
            p = le32(rec, p, Float.floatToRawIntBits(e.ySize));
            p = le32(rec, p, Float.floatToRawIntBits(e.fallDistance));
            p = le32(rec, p, Float.floatToRawIntBits(e.distanceWalkedModified));
            p = le32(rec, p, Float.floatToRawIntBits(e.distanceWalkedOnStepModified));
            p = le32(rec, p, nextStepDistance(e));
            p = le32(rec, p, fireField.getInt(e));
            rec[p++] = (byte)(e.onGround ? 1 : 0);
            rec[p++] = (byte)(e.isCollidedHorizontally ? 1 : 0);
            rec[p++] = (byte)(e.isCollidedVertically ? 1 : 0);
            rec[p++] = (byte)(e.isCollided ? 1 : 0);
            rec[p++] = (byte)(isInWeb(e) ? 1 : 0);
            movesOut.write(rec);

            if (e.onGround) ++onGround;
            if (e.isCollidedHorizontally) ++collidedH;
            if (steppedThisMove) ++stepped;
            if (isInWeb(e)) ++inWeb;
            if (fireField.getInt(e) > 0) ++fire;
            if (!e.onGround) ++air;
            if (e.posY > posYBefore + 1.0E-9D) ++climbed;
        }
        movesOut.close();

        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("kind", "move");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("ops", ops);
        m.addProperty("shapes", shapes);
        m.addProperty("opseed", opseed);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
        m.addProperty("shapes_layout", "16 bytes per shape, in placement order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8 (0..15), pad uint8 (always 0). 400 shapes are 6400 bytes.");
        m.addProperty("shapes_draws", "per shape i from Random(opseed), in this order: x = (cx-radius)*16 + nextInt((2*radius+1)*16); z the same with cz; yDrawn = 55 + nextInt(46); pick = nextInt(SHAPES.length) = nextInt(31) selects a row of the shapes table; meta = the row's base + nextInt(width) when the row's width is not 0, else the base. Then y = yDrawn when yDrawn is within SURFACE_BAND = " + SURFACE_BAND + " of the column surface (min(99, max(56, heightValue(x, z)))), and y = surface - 1 otherwise, so the scatter lands in the layer the entity walks in instead of floating where nothing reaches it; y is the recorded (and placed) value. Then setBlock(x, y, z, id, meta, 2)");
        m.addProperty("shapes_table", shapesTable());
        m.addProperty("entity", "netherite.oracle.ProbeEntity extends net.minecraft.entity.Entity, width 0.6, height 1.8, stepHeight 0.5, yOffset 0, noClip false, field_70135_K true (the constructor default), entityInit/readEntityFromNBT/writeEntityToNBT empty; spawned with World.spawnEntityInWorld at (sx + 0.5, heightValue(sx, sz), sz + 0.5) with sx = (cx-radius)*16 + nextInt((2*radius+1)*16) and sz the same with cz, drawn right after the shapes; never ticked, only moveEntity and setPosition are called on it");
        m.addProperty("moves_layout", movesLayout());
        m.addProperty("move_draws", "per op i from the same Random(opseed): if i % 50 == 0 a teleport first: tx = (cx-radius)*16 + nextInt((2*radius+1)*16), tz the same with cz, ty = heightValue(tx, tz) + nextInt(6) - 1, setPosition(tx + 0.5, ty, tz + 0.5), motionX/Y/Z = 0, fallDistance = 0, onGround = false; then sc = nextInt(4), s = {0.05, 0.3, 1.0, 3.0}[sc], dx = (nextDouble() - 0.5) * s, dz = (nextDouble() - 0.5) * s, dy = (nextDouble() - 0.7) * s, moveEntity(dx, dy, dz)");
        m.addProperty("state_after", "every field is read right after moveEntity returns and before the next op; ySize, fallDistance, distanceWalkedModified, distanceWalkedOnStepModified and nextStepDistance are floats (distanceWalkedModified/distanceWalkedOnStepModified as (float) of the double expression, so a native replay must round the same way), nextStepDistance is the private int behind Entity.nextStepDistance, fire is the private int behind Entity.fire (clamped to int32 range), and the five flags are the Entity boolean fields of the same names plus isInWeb");
        m.addProperty("teleport_every", 50);
        m.addProperty("teleport_resets", "setPosition only, plus motionX/Y/Z = 0, fallDistance = 0 and onGround = false; ySize, isCollided*, isInWeb, distanceWalkedModified, distanceWalkedOnStepModified, nextStepDistance and fire are NOT reset and carry across the teleport");
        m.addProperty("height_map", "the teleport y is the chunk heightMap value (World.getHeightValue), the same int32 the setblock kind's chunk_bytes layout records; the entity does not adjust ySize or snap to the ground on a teleport, so it can start inside terrain or in the air");
        m.addProperty("counts", "onGround " + onGround + ", collidedHorizontally " + collidedH + ", steppedUp " + stepped + ", inWeb " + inWeb + ", firePositive " + fire + ", teleports " + teleports + ", offGround " + air + ", posYRose " + climbed);
        m.addProperty("counts_meaning", "moves whose recorded state has the field set. steppedUp counts moves that had the entity on the ground before the move and left it higher than it started, which is what Entity.moveEntity's step branch produces; ySize is not the signal because in this decompile only setPositionAndRotation writes it (moveEntity only multiplies it by 0.4), so it is 0 for every move here. inWeb is read from the protected Entity.isInWeb, which BlockWeb.onEntityCollidedWithBlock sets through func_145775_I; the entity must be inside a web block for that to happen. firePositive counts fire > 0: the entity sits inside a lava or fire block and never leaves, because the move deltas are drawn around zero and the shape fills the cell");
        m.addProperty("next_step_distance", "recorded as the int the native side must keep for the walking-distance branch; it only changes when distanceWalkedOnStepModified passes it AND the block under the entity is not air. That branch also calls Block.onEntityWalking, but every shape the table places has the vanilla no-op body, so the only observable effect is nextStepDistance itself, which is recorded");
        m.addProperty("y_size", "ySize enters and leaves every move at 0: the probe never calls setPositionAndRotation (the only writer), and moveEntity's first statement multiplies it by 0.4. It is recorded anyway, raw, because a native moveEntity has to reproduce the field even when it never changes");
        PrintWriter w = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        w.println(m.toString());
        w.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("chunks", loaded.size());
        res.addProperty("ops", ops);
        res.addProperty("shapes", shapes);
        res.addProperty("onGround", onGround);
        res.addProperty("collidedHorizontally", collidedH);
        res.addProperty("steppedUp", stepped);
        res.addProperty("inWeb", inWeb);
        res.addProperty("firePositive", fire);
        res.addProperty("teleports", teleports);
        res.addProperty("offGround", air);
        res.addProperty("posYRose", climbed);
        res.add("shapeReach", reach);
        return res;
    }

    /**
     * How much of the scatter the entity can reach, measured before the first
     * move. A shape is inside the ops area when the entity's teleports can land
     * next to it; for those, the gap is the shape's y minus the terrain height
     * of its own column (shape x, shape z). A teleport puts the entity at a
     * height map value plus -1..4 and the entity then falls, so a shape whose
     * column surface is within a few blocks of the shape's y is one the entity
     * gets to walk into. Also counted: shapes outside the loaded square, where
     * the height map is not defined for them.
     */
    static JsonObject reach(WorldServer ws, int[] sx, int[] sy, int[] sz, int[] ids, int shapes,
                            int bx0, int bz0, int width)
    {
        int atSurface = 0, belowSurface = 0, aboveSurface = 0;
        int walkableBlocks = 0, noCollisionBlocks = 0, unknownColumn = 0;

        for (int i = 0; i < shapes; ++i)
        {
            boolean inside = sx[i] >= bx0 && sx[i] < bx0 + width && sz[i] >= bz0 && sz[i] < bz0 + width;

            if (!inside) ++unknownColumn;
            else
            {
                int surface = Math.min(99, Math.max(56, ws.getHeightValue(sx[i], sz[i])));
                int gap = sy[i] - surface;

                if (gap >= -3 && gap <= 3) ++atSurface;
                else if (gap < -3) ++belowSurface;
                else ++aboveSurface;
            }

            if (ids[i] != 0 && ids[i] != 92) ++walkableBlocks;
            else ++noCollisionBlocks;
        }

        JsonObject o = new JsonObject();
        o.addProperty("shapes", shapes);
        o.addProperty("shapesOnOrNearTheirColumnSurface", atSurface);
        o.addProperty("shapesBelowTheirColumnSurface", belowSurface);
        o.addProperty("shapesAboveTheirColumnSurface", aboveSurface);
        o.addProperty("shapesOutsideTheOpsArea", unknownColumn);
        o.addProperty("shapesWithSomeCollisionBox", walkableBlocks);
        o.addProperty("shapesWithoutCollisionBox", noCollisionBlocks);
        o.addProperty("note", "a teleport puts the entity at the column height map plus -1..4 and it then falls, so the count that matters is on-or-near; the probe walks 5000 moves over 100 teleports spread across the area, so most shapes on the surface are met repeatedly");
        return o;
    }

    private static int leRead32(byte[] a, int o)
    {
        return (a[o] & 255) | (a[o + 1] & 255) << 8 | (a[o + 2] & 255) << 16 | (a[o + 3] & 255) << 24;
    }

    private static int leRead16(byte[] a, int o)
    {
        return (a[o] & 255) | (a[o + 1] & 255) << 8;
    }

    /** Set once shapes.bin is on disk, so the manifest can say whether it landed. */
    static boolean SHAPE_RECORDS_READY;

    /** The int behind Entity.nextStepDistance, which is private. */
    private static int nextStepDistance(Object e) throws Exception
    {
        return NEXT_STEP_DISTANCE.getInt(e);
    }

    /** The Entity.inWeb boolean, which is protected. */
    private static boolean isInWeb(Object e) throws Exception
    {
        return IN_WEB.getBoolean(e);
    }

    static final Field NEXT_STEP_DISTANCE;
    static final Field IN_WEB;

    static
    {
        try
        {
            NEXT_STEP_DISTANCE = Entity.class.getDeclaredField("nextStepDistance");
            NEXT_STEP_DISTANCE.setAccessible(true);
            IN_WEB = Entity.class.getDeclaredField("isInWeb");
            IN_WEB.setAccessible(true);
        }
        catch (Exception e)
        {
            throw new ExceptionInInitializerError(e);
        }
    }

    /** The shapes table as text, so the manifest states exactly what was placed. */
    private static String shapesTable()
    {
        StringBuilder b = new StringBuilder("row:picked by nextInt(31); id (name), meta base + nextInt(n) (n = 0 draws the base only)");
        for (int i = 0; i < SHAPES.length; ++i)
        {
            int[] s = SHAPES[i];
            b.append("; ").append(i).append(": ").append(s[0]).append(' ').append(name(s[0])).append(", ");
            if (s[2] > 0) b.append(s[1]).append(" + nextInt(").append(s[2]).append(')');
            else b.append(s[1]);
        }
        return b.toString();
    }

    private static String name(int id)
    {
        switch (id)
        {
            case 1: return "stone";
            case 9: return "water";
            case 11: return "lava";
            case 19: return "sponge";
            case 20: return "glass";
            case 26: return "bed";
            case 30: return "web";
            case 44: return "stone_slab";
            case 53: return "oak_stairs";
            case 60: return "farmland";
            case 65: return "ladder";
            case 78: return "snow_layer";
            case 81: return "cactus";
            case 85: return "fence";
            case 88: return "soul_sand";
            case 92: return "cake";
            case 96: return "trapdoor";
            case 102: return "glass_pane";
            case 106: return "vine";
            case 107: return "fence_gate";
            case 111: return "waterlily";
            case 139: return "cobblestone_wall";
            case 171: return "carpet";
            case 0: return "air";
            default: return "?";
        }
    }

    /**
     * id, meta base, meta width, in the order nextInt(SHAPES.length) picks
     * them. The two slab metas are separate rows because 8 is the flipped (top)
     * slab; the ladder and vine rows pin one meta each so all six of their
     * collision-box variants (MetaBasedOnFacing) are covered across the table.
     * Rows 24 to 30 are block ids no entity collides with (fire, cake's table
     * block 92 is the cake itself, 0 is air, the three extras are vanilla
     * blocks the shape scatter can also drop); they are here so the collision
     * scan sees a wider mix of shapes that add nothing. Row 30 is snow again
     * with a full-height meta to break the layer boxes into a full cube.
     */
    static final int[][] SHAPES = {
        {44, 0, 1}, {44, 8, 1}, {53, 0, 8}, {85, 0, 0}, {102, 0, 0}, {139, 0, 0},
        {78, 0, 8}, {81, 0, 0}, {88, 0, 0}, {60, 0, 0}, {65, 3, 1}, {65, 4, 1},
        {106, 0, 0}, {30, 0, 0}, {171, 0, 0}, {96, 0, 16}, {107, 0, 8}, {26, 0, 12},
        {92, 0, 0}, {20, 0, 0}, {1, 0, 0}, {9, 0, 0}, {11, 0, 0}, {106, 7, 1},
        {106, 8, 1}, {19, 0, 0}, {111, 0, 0}, {11, 2, 1}, {0, 0, 0}, {78, 0, 1},
        {92, 0, 0},
    };

    static final String MOVES_LAYOUT_HEAD =
        "per op: flag uint8 (bit 0: a teleport ran before this move; no other bits are set), dx double LE, dy double LE, dz double LE (the inputs to moveEntity, all raw bits), posX, posY, posZ double LE, minX, minY, minZ, maxX, maxY, maxZ double LE (boundingBox after the move), motionX, motionY, motionZ double LE (after the move), then";
    static final String MOVES_LAYOUT_TAIL =
        ", nextStepDistance int32 LE (the private Entity.nextStepDistance), fire int32 LE (the private Entity.fire), onGround uint8, isCollidedHorizontally uint8, isCollidedVertically uint8, isCollided uint8, isInWeb uint8 (each 0 or 1), and no padding at the end";

    private static String movesLayout()
    {
        return MOVES_LAYOUT_HEAD + " ySize float32 LE, fallDistance float32 LE, distanceWalkedModified float32 LE,"
            + " distanceWalkedOnStepModified float32 LE" + MOVES_LAYOUT_TAIL
            + "; " + MOVE_BYTES + " bytes per move";
    }

    /**
     * Bytes per move: flag 1, three input doubles 24, three position doubles
     * 24, six bounding box doubles 48, three motion doubles 24, four floats 16,
     * two ints 8, five flags 5.
     */
    static final int MOVE_BYTES = 1 + 3 * 8 + 3 * 8 + 6 * 8 + 3 * 8 + 4 * 4 + 2 * 4 + 5;

    private static int le32(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
        a[o + 2] = (byte)(v >> 16);
        a[o + 3] = (byte)(v >> 24);
        return o + 4;
    }

    private static void le16(byte[] a, int o, int v)
    {
        a[o] = (byte)v;
        a[o + 1] = (byte)(v >> 8);
    }

    private static int le64(byte[] a, int o, long v)
    {
        for (int i = 0; i < 8; ++i) a[o + i] = (byte)(v >> (8 * i));
        return o + 8;
    }
}