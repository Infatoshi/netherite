package netherite.oracle;

import com.google.gson.JsonArray;
import com.google.gson.JsonObject;
import com.google.gson.JsonElement;
import com.google.gson.JsonPrimitive;
import com.mojang.authlib.GameProfile;
import java.io.BufferedOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.zip.GZIPOutputStream;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.effect.EntityLightningBolt;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.monster.EntityCreeper;
import net.minecraft.entity.monster.EntityEnderman;
import net.minecraft.entity.monster.EntitySkeleton;
import net.minecraft.entity.monster.EntitySilverfish;
import net.minecraft.entity.monster.EntitySpider;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.monster.EntityPigZombie;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.util.DamageSource;
import net.minecraft.server.integrated.IntegratedServer;
import net.minecraft.util.MathHelper;
import net.minecraft.util.DamageSource;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;

/**
 * The hostile mob probe: the four common overworld monsters of 1.7.10
 * (EntityZombie, EntitySkeleton, EntityCreeper, EntitySpider) and the probe's
 * player ticking in a flattened raw region, the reference for the native port
 * of EntityMob, EntityCreature's targeting, the hostile AI tasks, the arrows
 * and the explosions.
 *
 * One case per run: a raw region (Probe.rawChunks, population off) chosen for a
 * flat, dry 5x5-chunk area, flattened to one grass level with a dirt body. On it
 * the arena: a roofed cobble room (its interior is dark all day), a wall with
 * two wooden doors, a water pool and a cobble wall. The sky is the case's
 * (skylightSubtracted 0 is daylight, 11 is night), so the daylight runs burn the
 * zombies and skeletons in the open and the night runs are the fight.
 *
 * The mobs are spawned the way SpawnerAnimals does it: a fresh instance by name
 * (EntityList.createEntityByName), setLocationAndAngles, the probe's per-case
 * state flags, onSpawnWithEgg(null), world.spawnEntityInWorld. The player is a
 * concrete EntityPlayer (HostileProbe.ProbePlayer) added the same way, so it
 * lands in the world's playerEntities and the mobs' target queries find it.
 *
 * The tick is the World.updateEntities entity pass over the probe's own list in
 * spawn order: the tick bookkeeping, onUpdate, then the chunk membership, dead
 * entities removed the way the world removes them, and entities a tick spawns
 * (arrows, drops, XP orbs, reinforcement zombies, spider jockeys) absorbed at
 * the end of the list and ticked in the same pass.
 *
 * Every live entity is recorded after every tick: a 64-bit FNV-1a hash of the
 * canonical NBT text StructuresProbe.canon writes, plus the state the next tick
 * reads that NBT does not carry (the AI's tick counter and executing entries and
 * each task's counter, PathNavigate's path and stuck-detection state, the move,
 * look, jump and body helpers, the attack state, the per-kind state: the
 * zombie's conversion and child and villager flags, the skeleton's type and
 * arrow-attack counters, the creeper's fuse, the spider's climb flag, the
 * equipment). Every 64th tick and the final tick carry the full canonical NBT
 * text. The explosions get their own stream.
 *
 * Output DIR/manifest.json, DIR/shapes.bin, DIR/spawns.bin, DIR/ticks.bin.gz,
 * DIR/digest.txt.gz, DIR/nbt64.txt.gz, DIR/spawns.txt.gz, DIR/removals.txt.gz,
 * DIR/events.txt.gz, DIR/start.txt, DIR/end.txt, DIR/final.bin.gz.
 */
public final class HostileProbe
{
    /** The hostile kinds the probe spawns, in the manifest's order. */
static final String[] KINDS = {"Zombie", "Skeleton", "Creeper", "Spider", "Player", "Enderman", "Witch", "Silverfish", "PigZombie", "Blaze",
        "CaveSpider"};

    static final int KIND_ZOMBIE = 0, KIND_SKELETON = 1, KIND_CREEPER = 2, KIND_SPIDER = 3, KIND_PLAYER = 4,
        KIND_ENDERMAN = 5, KIND_WITCH = 6, KIND_SILVERFISH = 7, KIND_PIGMAN = 8, KIND_BLAZE = 9,
        KIND_CAVE_SPIDER = 10;

    /** The fixed per-entity base state record (living.c's an_write_state). */
    static final int ENT_STATE_BYTES = 272;
    /** The hostile extension hostiles.c appends (332 shared + 44 old-AI + 32 pigman). */
    static final int ENT_EXTRA_BYTES = 408;
    /** The enderman or silverfish old-AI path block. */
    static final int ENDERMAN_EXTRA_BYTES = 44;
    static final int HOSTILE_STATE_BYTES = ENT_STATE_BYTES + ENT_EXTRA_BYTES;
    static final int SPAWN_BYTES = 128;

    static final long FNV_OFFSET = 0xcbf29ce484222325L;
    static final long FNV_PRIME = 0x100000001b3L;

    /** A concrete EntityPlayer: EntityPlayer has no abstract method left. */
    public static class ProbePlayer extends EntityPlayer
    {
        public ProbePlayer(World p_i45324_1_)
        {
            super(p_i45324_1_, new GameProfile(null, "ProbePlayer"));
        }

        public void addChatMessage(net.minecraft.util.IChatComponent p_145747_1_)
        {
        }

        public boolean canCommandSenderUseCommand(int p_70003_1_, String p_70003_2_)
        {
            return true;
        }

        public net.minecraft.util.ChunkCoordinates getPlayerCoordinates()
        {
            return new net.minecraft.util.ChunkCoordinates(MathHelper.floor_double(this.posX),
                MathHelper.floor_double(this.posY), MathHelper.floor_double(this.posZ));
        }
    }

    private HostileProbe() {}

    static final class CompactGzip extends GZIPOutputStream
    {
        CompactGzip(OutputStream out, int size) throws java.io.IOException
        {
            super(out, size);
            this.def.setLevel(9);
        }
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
                    error[0] = e;
                }
            }
        }, "Oracle HostileProbe");
        t.start();
        t.join();
        if (error[0] != null) throw error[0];
        return result[0];
    }

    static int parseKind(String s)
    {
        s = s.trim().toLowerCase();
        if (s.equals("zombie") || s.equals("0")) return KIND_ZOMBIE;
        if (s.equals("skeleton") || s.equals("1")) return KIND_SKELETON;
        if (s.equals("creeper") || s.equals("2")) return KIND_CREEPER;
        if (s.equals("spider") || s.equals("3")) return KIND_SPIDER;
        if (s.equals("player") || s.equals("4")) return KIND_PLAYER;
        if (s.equals("enderman") || s.equals("5")) return KIND_ENDERMAN;
if (s.equals("witch") || s.equals("6")) return KIND_WITCH;
        if (s.equals("silverfish") || s.equals("7")) return KIND_SILVERFISH;
        if (s.equals("pigman") || s.equals("pigzombie") || s.equals("8")) return KIND_PIGMAN;
        if (s.equals("blaze") || s.equals("9")) return KIND_BLAZE;
        if (s.equals("cavespider") || s.equals("10")) return KIND_CAVE_SPIDER;
        throw new IllegalArgumentException("unknown kind: " + s);
    }

    static JsonObject dump(IntegratedServer server, JsonObject cmd) throws Exception
    {
        int cx = cmd.get("cx").getAsInt(), cz = cmd.get("cz").getAsInt();
        int radius = cmd.has("radius") ? cmd.get("radius").getAsInt() : 2;
        int ring = cmd.has("ring") ? cmd.get("ring").getAsInt() : 6;
        int mobs = cmd.has("mobs") ? cmd.get("mobs").getAsInt() : 220;
        int ticks = cmd.has("ticks") ? cmd.get("ticks").getAsInt() : 2400;
        long opseed = cmd.has("opseed") ? cmd.get("opseed").getAsLong() : 11L;
        int night = cmd.has("night") ? cmd.get("night").getAsInt() : 0;
        int only = cmd.has("only") ? cmd.get("only").getAsInt() : -1;   /* dev: one hostile kind and the player */
        int nbtEvery = cmd.has("nbtEvery") ? cmd.get("nbtEvery").getAsInt() : 64;
        File dir = new File(cmd.get("out").getAsString());
        dir.mkdirs();

        Trace.restart();   /* the trace holds this run, not the world's startup */

        int dim = cmd.has("dim") ? cmd.get("dim").getAsInt() : 0;
        WorldServer ws = server.worldServerForDimension(dim);
        long seed = ws.getSeed();
        Probe.rawChunks = true;

        // The world is pinned: NORMAL difficulty (HARD would break doors and
        // spawn ZOMBIE reinforcements, PEACEFUL would kill every mob), and the
        // sky the case asked for. The server is parked, so both stay put.
        ws.difficultySetting = EnumDifficulty.NORMAL;
        ws.skylightSubtracted = night == 1 ? 11 : 0;

        // The probe player normally stays out of World.playerEntities: the
        // realm's tracker casts every entry of that list to EntityPlayerMP when
        // an entity spawns. EntityEnderman.findPlayerToAttack reaches a player
        // through getClosestVulnerablePlayerToEntity, which reads that list, so
        // an enderman run puts the probe player in it and takes the tracker out
        // of the loop for the run. The manifest records which shape the run had.
        final boolean playerInPlayers = cmd.has("playerInPlayers") && cmd.get("playerInPlayers").getAsBoolean();

        if (playerInPlayers)
        {
            try
            {
                java.lang.reflect.Field trackerField = WorldServer.class.getDeclaredField("theEntityTracker");
                trackerField.setAccessible(true);
                trackerField.set(ws, new SlimeProbe.SilentTracker(ws));
            }
            catch (Exception e)
            {
                throw new IllegalStateException("could not replace the entity tracker", e);
            }
        }

        List<Integer> spawnKinds = new ArrayList<Integer>();

        if (cmd.has("kinds"))
        {
            JsonElement ke = cmd.get("kinds");

            if (ke.isJsonArray())
            {
                for (JsonElement elem : ke.getAsJsonArray())
                {
                    spawnKinds.add(Integer.valueOf(parseKind(elem.getAsString())));
                }
            }
            else
            {
                String[] parts = ke.getAsString().split(",");

                for (String part : parts)
                {
                    if (!part.trim().isEmpty()) spawnKinds.add(Integer.valueOf(parseKind(part)));
                }
            }
        }
        else if (only >= 0)
        {
            spawnKinds.add(Integer.valueOf(only));
        }
        else
        {
            spawnKinds.add(Integer.valueOf(KIND_ZOMBIE));
            spawnKinds.add(Integer.valueOf(KIND_SKELETON));
            spawnKinds.add(Integer.valueOf(KIND_CREEPER));
            spawnKinds.add(Integer.valueOf(KIND_SPIDER));
        }

        // Load the region first, then search it for the flattest, dry area.
        int x0 = cx - radius - ring, x1 = cx + radius + ring;
        int z0 = cz - radius - ring, z1 = cz + radius + ring;
        JsonArray loaded = new JsonArray();

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                if (ws.getChunkFromChunkCoords(lx, lz) == null) throw new IllegalStateException("chunk (" + lx + "," + lz + ") did not load");
            }
        }

        int area = 2 * radius + 1;
        int bestX = cx, bestZ = cz, bestHi = 0, bestSpan = Integer.MAX_VALUE, bestRank = 2;

        for (int ax = x0 + radius; ax <= x1 - radius; ++ax)
        {
            for (int az = z0 + radius; az <= z1 - radius; ++az)
            {
                int lo = 999, hi = -999;

                for (int lx = ax - radius; lx <= ax + radius; ++lx)
                {
                    for (int lz = az - radius; lz <= az + radius; ++lz)
                    {
                        for (int sx = 0; sx < 16; ++sx)
                        {
                            for (int sz = 0; sz < 16; ++sz)
                            {
                                int h = heightOf(ws, lx * 16 + sx, lz * 16 + sz);
                                if (h < lo) lo = h;
                                if (h > hi) hi = h;
                            }
                        }
                    }
                }

                int rank = lo >= 64 ? 0 : 1;

                if (rank < bestRank || (rank == bestRank && hi - lo < bestSpan))
                {
                    bestRank = rank;
                    bestSpan = hi - lo;
                    bestX = ax;
                    bestZ = az;
                    bestHi = hi;
                }
            }
        }

        cx = bestX;
        cz = bestZ;

        x0 = cx - radius - ring;
        x1 = cx + radius + ring;
        z0 = cz - radius - ring;
        z1 = cz + radius + ring;

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

        boolean pigmanNether = dim == -1 && spawnKinds.contains(Integer.valueOf(KIND_PIGMAN));
        int platformY = pigmanNether ? 160 : bestHi + 1;
        int bx0 = (cx - radius) * 16, bz0 = (cz - radius) * 16;
        int width = area * 16;

        int entitiesBefore = ws.loadedEntityList.size();

        // ------------------------------------------------------------ shapes
        OutputStream shapesRaw = new BufferedOutputStream(new FileOutputStream(new File(dir, "shapes.bin")), 1 << 16);
        byte[] shape = new byte[16];
        int shapeCount = 0;

        for (int x = bx0; x < bx0 + width; ++x)
        {
            for (int z = bz0; z < bz0 + width; ++z)
            {
                int h = heightOf(ws, x, z);

                if (pigmanNether)
                {
                    shapeCount += place(ws, shapesRaw, shape, x, platformY - 1, z, 2, 0);
                }
                else
                {
                    for (int y = h; y < platformY; ++y) shapeCount += place(ws, shapesRaw, shape, x, y, z, y == platformY - 1 ? 2 : 3, 0);
                }
            }
        }

        // The water pool: a 7x7 hole three deep in the corner.
        int poolX = bx0 + 2, poolZ = bz0 + 2;

        for (int x = 0; x < 7; ++x)
        {
            for (int z = 0; z < 7; ++z)
            {
                for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, poolX + x, platformY - y, poolZ + z, pigmanNether ? 11 : 9, 0);
            }
        }

        if (pigmanNether)
        {
            for (int x = 15; x <= 18; ++x)
            {
                for (int z = 5; z <= 7; ++z)
                    shapeCount += place(ws, shapesRaw, shape, bx0 + x, platformY, bz0 + z, 51, 0);
            }
        }

        // The dark room: 11x9 cobble walls three high, a cobble roof, its
        // interior in shadow all day. Room origin (roomX, roomZ) is its north
        // west corner cell.
        int roomX = bx0 + 8, roomZ = bz0 + 8, roomW = 11, roomD = 9;

        for (int x = 0; x < roomW; ++x)
        {
            for (int z = 0; z < roomD; ++z)
            {
                boolean edge = x == 0 || x == roomW - 1 || z == 0 || z == roomD - 1;

                if (!edge) continue;

                for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, roomX + x, platformY + y, roomZ + z, 4, 0);
            }
        }

        for (int x = 0; x < roomW; ++x)
        {
            for (int z = 0; z < roomD; ++z) shapeCount += place(ws, shapesRaw, shape, roomX + x, platformY + 4, roomZ + z, 4, 0);
        }

        // The doorway on the south wall, two cells wide, with the two blocks of
        // one closed wooden door in each column so a zombie pathing in has to
        // deal with them.
        int doorX = roomX + 5, doorZ = roomZ + roomD - 1;

        for (int d = 0; d < 2; ++d)
        {
            for (int y = 1; y <= 3; ++y) shapeCount += place(ws, shapesRaw, shape, doorX + d, platformY + y, doorZ, 0, 0);

            shapeCount += place(ws, shapesRaw, shape, doorX + d, platformY + 1, doorZ, 64, 1);
            shapeCount += place(ws, shapesRaw, shape, doorX + d, platformY + 2, doorZ, 64, 9);
        }

        // A cobble wall two high across the middle of the open area, so the
        // pathfinder and the leap have something to work against.
        for (int z = 0; z < 12; ++z)
        {
            shapeCount += place(ws, shapesRaw, shape, bx0 + 40, platformY + 1, bz0 + 40 + z, 4, 0);
            shapeCount += place(ws, shapesRaw, shape, bx0 + 40, platformY + 2, bz0 + 40 + z, 4, 0);
        }

        // A 4x4 web patch in the open quarter, only when the run spawns
        // spiders: EntitySpider.setInWeb is an empty override (a spider keeps
        // its speed in a web), the other kinds are slowed. The web sits away
        // from the pool (bx0+2..8), the room (bx0+8..19) and the wall column
        // (bx0+40), so nothing spawns inside it except by chance.
        boolean hasSpider = spawnKinds.contains(Integer.valueOf(KIND_SPIDER))
            || spawnKinds.contains(Integer.valueOf(KIND_CAVE_SPIDER));

        if (hasSpider)
        {
            for (int x = 0; x < 4; ++x)
            {
                for (int z = 0; z < 4; ++z)
                {
                    shapeCount += place(ws, shapesRaw, shape, bx0 + 58 + x, platformY, bz0 + 58 + z, 30, 0);
                }
            }
        }

        // The wall around the border of the area, three high: a monster jumps a
        // one-high fence, and a monster that leaves the arena would path into
        // chunks the probe never loaded. For blazes, the walls are 16 high and
        // topped with a glass ceiling so fireballs and hovering blazes stay inside.
        boolean hasBlaze = spawnKinds.contains(Integer.valueOf(KIND_BLAZE));
        int wallH = hasBlaze ? 16 : 3;
        int y0 = hasBlaze ? -3 : 1;

        for (int x = 0; x < width; ++x)
        {
            for (int y = y0; y <= wallH; ++y)
            {
                shapeCount += place(ws, shapesRaw, shape, bx0 + x, platformY + y, bz0, 4, 0);
                shapeCount += place(ws, shapesRaw, shape, bx0 + x, platformY + y, bz0 + width - 1, 4, 0);
            }
        }

        for (int z = 0; z < width; ++z)
        {
            for (int y = y0; y <= wallH; ++y)
            {
                shapeCount += place(ws, shapesRaw, shape, bx0, platformY + y, bz0 + z, 4, 0);
                shapeCount += place(ws, shapesRaw, shape, bx0 + width - 1, platformY + y, bz0 + z, 4, 0);
            }
        }

        if (hasBlaze)
        {
            for (int x = 0; x < width; ++x)
            {
                for (int z = 0; z < width; ++z)
                {
                    shapeCount += place(ws, shapesRaw, shape, bx0 + x, platformY + wallH, bz0 + z, 20, 0);
                }
            }
        }

        // Silverfish cases have eggs near the player and in the room. Stone
        // beside the arena also lets an idle fish exercise its hiding path.
        if (spawnKinds.size() == 1 && spawnKinds.get(0).intValue() == KIND_SILVERFISH)
        {
            for (int x = 0; x < 12; ++x)
            {
                for (int z = 0; z < 12; ++z)
                {
                    if ((x + z) % 16 == 0)
                        shapeCount += place(ws, shapesRaw, shape, bx0 + 11 + x, platformY, bz0 + 1 + z, 97, 0);
                }
            }
            for (int x = 0; x < roomW; ++x)
            {
                if (x % 2 == 0 && x < roomW - 1)
                    shapeCount += place(ws, shapesRaw, shape, roomX + x, platformY + 1, roomZ, 97, 1);
            }
        }

        shapesRaw.close();

        // EntityAIMoveThroughVillage asks VillageCollection.findNearestVillage
        // (range 0) for a village at the entity. The native engine has no
        // village collection, so the probe refuses an arena that holds one.
        for (int qx = bx0; qx < bx0 + width; qx += 8)
        {
            for (int qz = bz0; qz < bz0 + width; qz += 8)
            {
                if (ws.villageCollectionObj.findNearestVillage(qx, platformY, qz, 0) != null)
                    throw new IllegalStateException("the arena holds a village at (" + qx + "," + qz + ")");
            }
        }

        // ------------------------------------------------------------- start
        writeDetState(new File(dir, "start.txt"), seed, ws);

        // ------------------------------------------------------------ spawns
        Random r = new Random(opseed);
        Map<Entity, Integer> spawnIndex = new IdentityHashMap<Entity, Integer>();
        List<Entity> list = new ArrayList<Entity>();
        byte[] spawnBuf = new byte[SPAWN_BYTES];
        OutputStream spawnsOut = new BufferedOutputStream(new FileOutputStream(new File(dir, "spawns.bin")), 1 << 16);
        GZIPOutputStream spawnsTxtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "spawns.txt.gz")), 1 << 16);
        PrintWriter sw = new PrintWriter(new OutputStreamWriter(spawnsTxtGz, "UTF-8"));

        int[] spawnedKind = new int[16];
        int jockeyCount = 0;

        // The player stands in the open, just outside the room's door.
        double playerX = bx0 + 14.5D, playerZ = bz0 + 4.5D, playerY = platformY;

        for (int i = 0; i < mobs; ++i)
        {
            // i == 0 is the player marker
            int kind = i == 0 ? KIND_PLAYER : spawnKinds.get((i - 1) % spawnKinds.size()).intValue();
            double x, y, z;

            if (kind == KIND_PLAYER)
            {
                x = playerX;
                y = playerY;
                z = playerZ;
            }
            else if (kind == KIND_SILVERFISH)
            {
                x = i % 10 == 1 ? playerX : playerX + 1 + r.nextInt(5);
                z = i % 10 == 1 ? playerZ : playerZ + 1 + r.nextInt(5);
                y = platformY + 1;
            }
            else if (i % 5 == 0)
            {
                // inside the dark room
                x = roomX + 1 + 0.5D + r.nextInt(roomW - 2);
                z = roomZ + 1 + 0.5D + r.nextInt(roomD - 2);
                y = platformY + 1;
            }
            else if (i % 7 == 3)
            {
                // right next to the player, so an attack lands early
                x = playerX + 1 + r.nextInt(3);
                z = playerZ + 1 + r.nextInt(3);
                y = platformY + 1;
            }
            else
            {
                x = bx0 + 3 + 0.5D + r.nextInt(width - 6);
                z = bz0 + 3 + 0.5D + r.nextInt(width - 6);
                y = platformY + 1;
            }

            float yaw = r.nextFloat() * 360.0F;

            Entity e;

            if (kind == KIND_PLAYER)
            {
                e = new ProbePlayer(ws);
            }
            else
            {
                e = EntityList.createEntityByName(KINDS[kind], ws);
            }

            if (e == null) throw new IllegalStateException("no entity for " + KINDS[kind]);

            boolean child = kind == KIND_ZOMBIE && i % 9 == 4;
            boolean villager = kind == KIND_ZOMBIE && i % 11 == 6;
            boolean wither = kind == KIND_SKELETON && i % 13 == 8;
            boolean charged = kind == KIND_CREEPER && i % 17 == 10;

            e.setLocationAndAngles(x, y, z, yaw, 0.0F);

            if (kind == KIND_ZOMBIE)
            {
                if (child) ((EntityZombie)e).setChild(true);
                if (villager) ((EntityZombie)e).setVillager(true);
            }

            if (kind == KIND_SKELETON && wither) ((EntitySkeleton)e).setSkeletonType(1);

            if (kind == KIND_CREEPER && charged)
            {
                ((EntityCreeper)e).onStruckByLightning(new EntityLightningBolt(ws, x, y, z));
            }

            if (kind == KIND_PLAYER)
            {
                // The probe's player takes the whole damage path (attackEntityFrom,
                // the knockback, hurtResistantTime) but its max health is raised so
                // it survives the run and keeps the mobs targeted and attacking.
                ((EntityLivingBase)e).getEntityAttribute(net.minecraft.entity.SharedMonsterAttributes.maxHealth).setBaseValue(2000.0D);
                ((EntityLivingBase)e).setHealth(2000.0F);
            }

            if (kind != KIND_PLAYER)
            {
                ((EntityLiving)e).onSpawnWithEgg(null);

                // The probe's arena is tens of thousands of blocks from the
                // harness's parked player, so a mob whose canDespawn is true
                // would leave the world on its first tick. Every mob the probe
                // spawns takes the same persistence a spawn egg gives, and the
                // manifest says so.
                ((EntityLiving)e).func_110163_bv();
            }

            if (kind == KIND_PLAYER)
            {
                // The probe's player joins the chunk's entity list,
                // loadedEntityList and playerEntities. The tracker's cast of
                // playerEntities entries to EntityPlayerMP never runs: the
                // server is parked while this probe holds the world, so
                // nothing iterates the list except the mobs' own target
                // queries, and EntitySpider.findPlayerToAttack reaches the
                // player only through World.getClosestVulnerablePlayerToEntity,
                // which walks playerEntities. The manifest says so.
                net.minecraft.world.chunk.Chunk chunk = ws.getChunkFromChunkCoords(
                    MathHelper.floor_double(e.posX / 16.0D), MathHelper.floor_double(e.posZ / 16.0D));
                chunk.addEntity(e);
                ws.loadedEntityList.add(e);
                if (playerInPlayers) ws.playerEntities.add(e);
                e.addedToChunk = true;
            }
            else if (!ws.spawnEntityInWorld(e)) throw new IllegalStateException("entity " + i + " did not spawn at (" + x + "," + y + "," + z + ")");

            if (kind == KIND_SPIDER && e.riddenByEntity != null) ++jockeyCount;

            recordSpawn(spawnBuf, spawnsOut, sw, spawnIndex, list, ws, e, kind, child, villager, wither, charged);
            ++spawnedKind[kind];

            // A spawn-time construct (a chicken jockey, a spider's rider) lands
            // in loadedEntityList: absorb it exactly like the tick's own spawns.
            for (int a = entitiesBefore; a < ws.loadedEntityList.size(); ++a)
            {
                Entity n = (Entity)ws.loadedEntityList.get(a);

                if (spawnIndex.containsKey(n)) continue;

                int nk = kindOf(n);

                if (nk < 0) throw new IllegalStateException("the setup spawned a " + n.getClass().getSimpleName());

                recordSpawn(spawnBuf, spawnsOut, sw, spawnIndex, list, ws, n, nk, false, false, false, false, true);
                if (n instanceof EntityLivingBase) ++spawnedKind[nk];
            }
        }

        spawnsOut.close();
        sw.close();
        spawnsTxtGz.close();

        if (ws.loadedEntityList.size() != entitiesBefore + list.size())
        {
            throw new IllegalStateException("the world holds " + ws.loadedEntityList.size() + " entities, "
                + entitiesBefore + " before the setup and " + list.size() + " spawned");
        }

        // -------------------------------------------------------------- ticks
        boolean silverfishOnly = spawnKinds.size() == 1 && spawnKinds.get(0).intValue() == KIND_SILVERFISH;
        OutputStream tickFile = new FileOutputStream(new File(dir, "ticks.bin.gz"));
        GZIPOutputStream ticksGz = silverfishOnly ? new CompactGzip(tickFile, 1 << 16) : new GZIPOutputStream(tickFile, 1 << 16);
        OutputStream ticksOut = new BufferedOutputStream(ticksGz, 1 << 16);
        GZIPOutputStream digestGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "digest.txt.gz")), 1 << 16);
        PrintWriter dw = new PrintWriter(new OutputStreamWriter(digestGz, "UTF-8"));
        GZIPOutputStream remGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "removals.txt.gz")), 1 << 16);
        PrintWriter rw = new PrintWriter(new OutputStreamWriter(remGz, "UTF-8"));
        GZIPOutputStream nbtGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "nbt64.txt.gz")), 1 << 16);
        PrintWriter nw = new PrintWriter(new OutputStreamWriter(nbtGz, "UTF-8"));
        GZIPOutputStream evGz = new GZIPOutputStream(new FileOutputStream(new File(dir, "events.txt.gz")), 1 << 16);
        PrintWriter ew = new PrintWriter(new OutputStreamWriter(evGz, "UTF-8"));

        int chunksBefore = ws.theChunkProviderServer.getLoadedChunkCount();
        int[] deathsKind = new int[16];
        int[] birthsKind = new int[16];
        int[] dropsItem = new int[3001];
        int orbCount = 0, arrowCount = 0, creeperBlast = 0, explosionCount = 0, damageToPlayer = 0, playerDeaths = 0;
        int dropCount = 0;
        int maxList = list.size();
        int absorbed = ws.loadedEntityList.size();
        byte[] entRec = new byte[HOSTILE_STATE_BYTES];
        int firstDeathTick = -1;
        double playerHealthMin = 20.0;
        EntityPlayer player = null;

        for (int i = 0; i < list.size(); ++i) if (list.get(i) instanceof EntityPlayer) player = (EntityPlayer)list.get(i);

        if (player != null) playerHealthMin = player.getHealth();


        for (int t = 0; t < ticks; ++t)
        {
            if (t == 20 && spawnKinds.contains(Integer.valueOf(KIND_PIGMAN)))
            {
                Entity victim = null;
                double bestDistance = Double.POSITIVE_INFINITY;
                for (Entity candidate : list)
                {
                    if (candidate instanceof EntityPigZombie && !candidate.isDead)
                    {
                        double dx = candidate.posX - player.posX, dz = candidate.posZ - player.posZ;
                        double distance = dx * dx + dz * dz;
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            victim = candidate;
                        }
                    }
                }
                if (victim != null) victim.attackEntityFrom(DamageSource.causePlayerDamage(player), 1.0F);
            }
            if (t == 0 && spawnKinds.size() == 1 && spawnKinds.get(0).intValue() == KIND_SILVERFISH)
            {
                for (Entity e : list)
                    if (e instanceof EntitySilverfish) ((EntitySilverfish)e).attackEntityFrom(DamageSource.magic, 1.0F);
            }
            if (night == 2)
            {
                ws.skylightSubtracted = t < ticks / 2 ? 0 : 11;
            }

            Trace.t("tick", Integer.valueOf(t));
            for (int i = 0; i < list.size(); ++i)
            {
                Entity e = list.get(i);

                if (e.isDead) continue;

                double hpBefore = e instanceof EntityLivingBase ? ((EntityLivingBase)e).getHealth() : 0.0D;
                double playerHpBefore = player != null ? player.getHealth() : 0.0D;
                boolean creeperSwelled = e instanceof EntityCreeper && ((EntityCreeper)e).getCreeperState() > 0;
                updateEntity(ws, e);

                // EntityCreeper.func_146077_cc: the blast, then setDead. A
                // creeper that dies with health left and the fuse lit went off.
                if (creeperSwelled && e.isDead && ((EntityLivingBase)e).getHealth() > 0.0F)
                {
                    float size = ((EntityCreeper)e).getPowered() ? (float)(intOf(e, "explosionRadius") * 2) : (float)intOf(e, "explosionRadius");
                    ew.println(t + " explosion " + Double.toString(e.posX) + " " + Double.toString(e.posY) + " "
                        + Double.toString(e.posZ) + " " + Float.toString(size));
                    ++explosionCount;
                }

                if (ws.theChunkProviderServer.getLoadedChunkCount() != chunksBefore)
                    throw new IllegalStateException("tick " + t + " entity " + spawnIndex.get(e).intValue() + " "
                        + e.getClass().getSimpleName() + " generated a chunk: a path search reached outside the loaded ring");

                if (player != null && player.getHealth() < playerHpBefore)
                {
                    ++damageToPlayer;
                    ew.println(t + " damage " + spawnIndex.get(player).intValue() + " " + hex(Float.floatToRawIntBits((float)playerHpBefore))
                        + " " + hex(Float.floatToRawIntBits(player.getHealth())));
                }

                if (ws.loadedEntityList.size() > absorbed)
                {
                    for (int a = absorbed; a < ws.loadedEntityList.size(); ++a)
                    {
                        Entity n = (Entity)ws.loadedEntityList.get(a);

                        if (spawnIndex.containsKey(n)) continue;

                        spawnIndex.put(n, Integer.valueOf(list.size()));
                        list.add(n);

                        if (n instanceof EntityLivingBase)
                        {
                            ++birthsKind[kindIndex(n)];
                        }
                        else if (n instanceof EntityArrow)
                        {
                            ++arrowCount;
                            ew.println(t + " arrow " + (list.size() - 1) + " " + n.getEntityId() + " " + e.getEntityId());
                        }
                        else
                        {
                            ItemStack st = itemStackOf(n);

                            if (st != null)
                            {
                                int id = net.minecraft.item.Item.getIdFromItem(st.getItem());
                                if (id >= 0 && id < dropsItem.length) ++dropsItem[id];
                                ++dropCount;
                            }
                            else if (n instanceof EntityXPOrb)
                            {
                                ++orbCount;
                            }
                        }

                        if (list.size() > maxList) maxList = list.size();
                    }

                    absorbed = ws.loadedEntityList.size();
                }

                if (e.isDead)
                {
                    int si = spawnIndex.get(e).intValue();


                    if (e instanceof EntityLivingBase) ++deathsKind[kindIndex(e)];
                    if (firstDeathTick < 0) firstDeathTick = t;
                    if (isPlayer(e)) ++playerDeaths;

                    if (e.addedToChunk && ws.theChunkProviderServer.chunkExists(e.chunkCoordX, e.chunkCoordZ))
                    {
                        ws.getChunkFromChunkCoords(e.chunkCoordX, e.chunkCoordZ).removeEntity(e);
                    }

                    list.remove(i--);

                    rw.println(t + " " + si + " " + e.getEntityId() + " 1 " + (e instanceof EntityLivingBase
                        ? hex(Float.floatToRawIntBits(((EntityLivingBase)e).getHealth())) : "-1") + " " + intField(e, "fire"));
                }
            }

            byte[] head = new byte[8];
            int hp = 0;
            hp = le32(head, hp, t);
            hp = le32(head, hp, list.size());
            ticksOut.write(head, 0, 8);

            for (int i = 0; i < list.size(); ++i)
            {
                writeState(entRec, t, list.get(i), spawnIndex, ws, ew);
                ticksOut.write(entRec, 0, HOSTILE_STATE_BYTES);

                if (isPlayer(list.get(i)) && ((EntityLivingBase)list.get(i)).getHealth() < playerHealthMin)
                {
                    playerHealthMin = ((EntityLivingBase)list.get(i)).getHealth();
                }
            }

            if (nbtEvery > 0 && (t < 8 || t % nbtEvery == nbtEvery - 1 || t == ticks - 1))
            {
                nw.println("t " + t);

                for (int i = 0; i < list.size(); ++i)
                {
                    Entity e = list.get(i);
                    nw.println(spawnIndex.get(e).intValue() + " " + canon(e));
                }
            }

            writeDetLine(dw, t, ws);

            if (ws.theChunkProviderServer.getLoadedChunkCount() != chunksBefore)
            {
                StringBuilder sb = new StringBuilder();
                List<?> lc = ws.theChunkProviderServer.func_152380_a();

                for (int k = 0; k < lc.size(); ++k)
                {
                    net.minecraft.world.chunk.Chunk c = (net.minecraft.world.chunk.Chunk)lc.get(k);

                    if (c.xPosition < x0 || c.xPosition > x1 || c.zPosition < z0 || c.zPosition > z1)
                        sb.append(" (").append(c.xPosition).append(',').append(c.zPosition).append(')');
                }

                throw new IllegalStateException("tick " + t + ": the world generated chunks: " + sb.toString());
            }
        }

        ticksOut.close();
        ticksGz.close();
        dw.close();
        digestGz.close();
        rw.close();
        remGz.close();
        nw.close();
        nbtGz.close();
        ew.close();
        evGz.close();

        // The probe player leaves the world's player list before the server
        // resumes: nothing outside the probe may hold it as an EntityPlayerMP.
        if (playerInPlayers && player != null) ws.playerEntities.remove(player);

        writeDetState(new File(dir, "end.txt"), seed, ws);

        if (ws.theChunkProviderServer.getLoadedChunkCount() != chunksBefore)
        {
            throw new IllegalStateException("the world generated chunks during the ticks: "
                + chunksBefore + " chunks before, " + ws.theChunkProviderServer.getLoadedChunkCount() + " after");
        }

        // --------------------------------------------------------- final state
        DataOutputStream fin = new DataOutputStream(new GZIPOutputStream(new FileOutputStream(new File(dir, "final.bin.gz")), 1 << 16));
        byte[] chunkBytes = new byte[Probe.CHUNK_BYTES];

        for (int lx = x0; lx <= x1; ++lx)
        {
            for (int lz = z0; lz <= z1; ++lz)
            {
                net.minecraft.world.chunk.Chunk c = ws.getChunkFromChunkCoords(lx, lz);
                Probe.fillChunkBytes(c, chunkBytes);
                fin.writeInt(lx);
                fin.writeInt(lz);
                fin.write(chunkBytes, 0, 65536 * 3);
                fin.write(chunkBytes, 65536 * 5, 1024 + 1024 + 4);
                fin.writeByte(chunkBytes[65536 * 5 + 2052]);
                fin.writeByte(chunkBytes[65536 * 5 + 2053]);
            }
        }

        fin.close();

        // ------------------------------------------------------------ manifest
        JsonObject m = new JsonObject();
        m.addProperty("seed", seed);
        m.addProperty("dim", dim);
        StringBuilder hb = new StringBuilder();

        for (int i = 0; i < ws.playerEntities.size(); ++i)
        {
            EntityPlayer hp2 = (EntityPlayer)ws.playerEntities.get(i);
            hb.append(hb.length() == 0 ? "" : " ").append(Double.toString(hp2.posX)).append(' ')
                .append(Double.toString(hp2.posY)).append(' ').append(Double.toString(hp2.posZ))
                .append(' ').append(hp2.getClass().getName());
        }

        m.addProperty("harness_players", hb.length() == 0 ? "(none)" : hb.toString());
        m.addProperty("kind", "hostiles");
        m.addProperty("cx", cx);
        m.addProperty("cz", cz);
        m.addProperty("radius", radius);
        m.addProperty("ring", ring);
        m.addProperty("platformY", platformY);
        m.addProperty("skylightSubtracted", ws.skylightSubtracted);
        m.addProperty("difficulty", ws.difficultySetting.getDifficultyId());
        m.addProperty("mobs", mobs);
        m.addProperty("ticks", ticks);
        m.addProperty("opseed", opseed);
        m.addProperty("night", night);
        m.addProperty("game_rules", "mobGriefing " + ws.getGameRules().getGameRuleStringValue("mobGriefing")
            + ", doFireTick " + ws.getGameRules().getGameRuleStringValue("doFireTick")
            + ", doTileDrops " + ws.getGameRules().getGameRuleStringValue("doTileDrops")
            + ", doMobLoot " + ws.getGameRules().getGameRuleStringValue("doMobLoot")
            + ", naturalRegeneration " + ws.getGameRules().getGameRuleStringValue("naturalRegeneration"));
        m.addProperty("only", only);
        if (cmd.has("kinds")) m.addProperty("kinds_filter", cmd.get("kinds").toString());
        m.addProperty("nbtEvery", nbtEvery);
        m.addProperty("room_x", roomX);
        m.addProperty("room_z", roomZ);
        m.addProperty("room_w", roomW);
        m.addProperty("room_d", roomD);
        m.addProperty("order", "cx-major: for cx in " + x0 + ".." + x1 + ", for cz in " + z0 + ".." + z1);
        m.add("loaded", loaded);
m.addProperty("kinds", "0 Zombie, 1 Skeleton, 2 Creeper, 3 Spider, 4 Player, 5 Enderman, 6 Witch, 7 Silverfish, 8 PigZombie, 9 Blaze, 10 CaveSpider");
        m.addProperty("skylight", "World.skylightSubtracted at the run");
        m.addProperty("shapes_layout", "16 bytes per placement, in placement order: x int32 LE, y int32 LE, z int32 LE, id uint16 LE, meta uint8, pad uint8");
        m.addProperty("spawn_layout", SPAWN_BYTES + " bytes per spawn, in spawn order: spawn_index int32 LE, entity_id int32 LE, kind int32 LE, x, y, z double LE (after setLocationAndAngles), rotationYaw float32 LE, rotationPitch float32 LE, motionX, motionY, motionZ double LE, child int32, villager int32, wither int32, charged int32, growingAge int32, difficultyFactor float32 (World.func_147462_b at the spawn position), health float32 (after onSpawnWithEgg), construct int32 (1 for an entity the setup spawned on its own, absorbed here as a spawn record; the replay must not spawn it again), pad 28 bytes");
        m.addProperty("player_health", "the probe player's maxHealth base is 2000 and its health starts there, so it survives the run; every mob attack still runs the whole attackEntityFrom path");
        m.addProperty("ring", "the loaded ring is wide enough that no path search during the ticks reaches an unloaded chunk; the probe fails the run if one does");
        m.addProperty("persistence", "every mob the setup spawns gets EntityLiving.setPersistenceRequired, so the arena far from the harness player does not despawn it; an entity the setup spawns as a side effect (a chicken jockey) does not, and despawns on the vanilla rule");
        m.addProperty("player_entities", "the probe player joins the chunk entity list and loadedEntityList"
            + (playerInPlayers ? " and World.playerEntities (the entity tracker is replaced for the run so nothing casts it to EntityPlayerMP)"
                               : " but not World.playerEntities (the tracker casts them to EntityPlayerMP)")
            + "; the harness player is far outside the arena");
        m.addProperty("probe_player_in_player_entities", playerInPlayers ? 1 : 0);
        m.addProperty("spawn_draws", "per mob i from Random(opseed), in this order: the placement (i == 0: the player at (bx0+14.5, platformY, bz0+4.5); else i % 5 == 0: x = roomX+1.5+nextInt(roomW-2), z = roomZ+1.5+nextInt(roomD-2), y = platformY+1; else i % 7 == 3: x = playerX+1+nextInt(3), z = playerZ+1+nextInt(3), y = platformY+1; else x = bx0+3.5+nextInt(width-6), z = bz0+3.5+nextInt(width-6), y = platformY+1), yaw = nextFloat()*360. Then kind = i == 0 ? 4 : (i-1) % 4, child = zombie && i % 9 == 4, villager = zombie && i % 11 == 6, wither = skeleton && i % 13 == 8, charged = creeper && i % 17 == 10. Then the constructor, setLocationAndAngles, setChild/setVillager, setSkeletonType(1), onStruckByLightning(new EntityLightningBolt(ws, x, y, z)), onSpawnWithEgg(null), world.spawnEntityInWorld");
        m.addProperty("tick_loop", "per tick t: for (i = 0; i < list.size(); ++i) { skip when isDead; World.updateEntityWithOptionalForce(e, true); absorb every entity the world's loadedEntityList gained during the tick to the end of the list; when the entity just died: the chunk's removeEntity by (chunkCoordX, chunkCoordZ) when addedToChunk and the chunk exists, then list.remove(i--), then the removal line; } then one state record per live entity in the list's order, then the full NBT block when nbtEvery cuts one, then the digest line");
        m.addProperty("ent_state_layout", HOSTILE_STATE_BYTES + " bytes per record = " + ENT_STATE_BYTES + " base (ent_base_layout) + " + ENT_EXTRA_BYTES + " hostile (ent_extra_layout)");
        m.addProperty("ent_base_layout", baseLayout());
        m.addProperty("ent_extra_layout", extraLayout());
        m.addProperty("ticks_layout", "per tick: tick int32 LE, count int32 LE, then count state records");
        m.addProperty("nbt64", "nbt64.txt.gz: the first eight ticks, every " + nbtEvery + "th tick and the final tick, line t <tick>, then one line per live entity in list order: <spawn_index> <the canonical NBT text>");
        m.addProperty("spawns_txt", "spawns.txt.gz: one line per initial spawn: <spawn_index> <entity_id> <kind name> <canonical NBT at spawn>");
        m.addProperty("removals", "removals.txt.gz, one line per removal at the tick it happened: <tick> <spawn_index> <entity_id> 1 <health as hex float bits or -1> <fire>");
        m.addProperty("events_layout", "events.txt.gz: 'damage <tick> <spawn_index> <before hex float> <after hex float>' when the player lost health in a tick; 'arrow <tick> <spawn_index> <entity_id> <shooter entity id>' for every arrow that entered the world; 'explosion <tick> <x> <y> <z> <size>' for every creeper blast");
        m.addProperty("digest", "digest.txt.gz, one line per tick: t <tick>, then per role 0..3: role <r> seeder <hex> math <hex> split <hex>, then nextId <the OTHER role's next id>, worldRand <world.rand state hex>, worldRandGauss <0 or 1>");
        m.addProperty("start_end", "start.txt and end.txt, the DetProbe snapshot format, before the first spawn and after the last tick");
        m.addProperty("final_layout", "final.bin.gz, per loaded chunk in load order: cx int32 BE, cz int32 BE, ids uint16 LE (65536), metas uint8 (65536), heightMap 256 int32 LE, precipitationHeightMap 256 int32 LE, heightMapMinimum int32 LE, section mask uint16 LE");
        StringBuilder hist = new StringBuilder();

        for (int id = 0; id < dropsItem.length; ++id)
        {
            if (dropsItem[id] > 0) hist.append(id).append('x').append(dropsItem[id]).append(' ');
        }

        m.addProperty("drop_ids", "item entities spawned during the run, by item id: " + hist.toString());
        m.addProperty("jockeys", jockeyCount);
        m.addProperty("counts", "entities " + mobs + " (zombie " + spawnedKind[0] + ", skeleton " + spawnedKind[1] + ", creeper "
+ spawnedKind[2] + ", spider " + spawnedKind[3] + ", player " + spawnedKind[4] + (spawnedKind[KIND_WITCH] > 0 ? ", witch " + spawnedKind[KIND_WITCH] : "") + (spawnedKind[KIND_BLAZE] > 0 ? ", blaze " + spawnedKind[KIND_BLAZE] : "") + (spawnedKind[KIND_CAVE_SPIDER] > 0 ? ", cavespider " + spawnedKind[KIND_CAVE_SPIDER] : "") + "), births " + sum(birthsKind) + " (zombie "
            + birthsKind[0] + ", skeleton " + birthsKind[1] + ", creeper " + birthsKind[2] + ", spider " + birthsKind[3] + (birthsKind[KIND_WITCH] > 0 ? ", witch " + birthsKind[KIND_WITCH] : "") + (birthsKind[KIND_BLAZE] > 0 ? ", blaze " + birthsKind[KIND_BLAZE] : "")
            + "), deaths " + sum(deathsKind) + " (zombie " + deathsKind[0] + ", skeleton " + deathsKind[1] + ", creeper "
            + deathsKind[2] + ", spider " + deathsKind[3] + (deathsKind[KIND_WITCH] > 0 ? ", witch " + deathsKind[KIND_WITCH] : "") + (deathsKind[KIND_BLAZE] > 0 ? ", blaze " + deathsKind[KIND_BLAZE] : "") + "), drops " + dropCount + ", orbs " + orbCount + ", arrows " + arrowCount
            + ", explosions " + explosionCount + ", damage to player " + damageToPlayer + " hits, player deaths " + playerDeaths
            + ", max list " + maxList + ", first death tick " + firstDeathTick + ", lowest player health " + playerHealthMin + ", spider jockeys " + jockeyCount);
        PrintWriter mw = new PrintWriter(new OutputStreamWriter(new FileOutputStream(new File(dir, "manifest.json")), "UTF-8"));
        mw.println(m.toString());
        mw.close();

        JsonObject res = new JsonObject();
        res.addProperty("dir", dir.getPath());
        res.addProperty("mobs", mobs);
        res.addProperty("ticks", ticks);
        res.addProperty("shapes", shapeCount);
        res.addProperty("births", sum(birthsKind));
        res.addProperty("deaths", sum(deathsKind));
        res.addProperty("drops", dropCount);
        res.addProperty("orbs", orbCount);
        res.addProperty("arrows", arrowCount);
        res.addProperty("explosions", explosionCount);
        res.addProperty("playerHits", damageToPlayer);
        res.addProperty("playerDeaths", playerDeaths);
        res.addProperty("playerHealthMin", playerHealthMin);
        res.addProperty("maxList", maxList);
        res.addProperty("deathTick", firstDeathTick);
        return res;
    }

    // -------------------------------------------------------------- helpers

    static String baseLayout()
    {
        return "spawn_index int32, entity_id int32, kind int32, nbt_hash uint64 (FNV-1a over the canonical NBT text of entity.writeToNBT), "
            + "entity_age int32, ticks_existed int32, ai_tick_count int32, ai_executing int32, "
            + "task_state int32 x 10 (slot i is the i-th task entry's counter: MATE spawnBabyDelay, TEMPT delayTemptCounter, "
            + "FOLLOW_PARENT field_75345_d, WATCH_CLOSEST lookTime, LOOK_IDLE idleTime, EAT_GRASS field_151502_a, else -1), "
            + "nav_has_path int32, nav_index int32, nav_length int32, nav_total_ticks int32, nav_ticks_at_last_pos int32, nav_speed double, "
            + "nav_lx double, nav_ly double, nav_lz double, nav_hash uint64, "
            + "mh_update int32, mh_x double, mh_y double, mh_z double, mh_speed double, "
            + "look_is_looking int32, look_x double, look_y double, look_z double, look_delta_yaw float32, look_delta_pitch float32, "
            + "jump_is_jumping int32, move_forward float32, move_strafing float32, rotation_yaw_head float32, render_yaw_offset float32, "
            + "living_sound_time int32, body_counter int32, body_yaw float32, sheep_timer int32, egg_timer int32, breeding int32, "
            + "revenge_timer int32, flags int32 (bit 0 addedToChunk, bit 1 onGround), rand_state uint64, pad int32";
    }

    static String extraLayout()
    {
        return "attack_time int32, hurt_time int32, hurt_resistant_time int32, max_hurt_time int32, recently_hit int32, "
            + "health float32 (hex bits), entity_to_attack int32 (spawn index or -1), attack_target int32, last_attacker int32, "
            + "current_target int32, num_ticks_to_chase_target int32, fleeing_tick int32 (EntityCreature), has_attacked int32, "
            + "nav_flags int32 (bit 0 avoidsWater, 1 canSwim, 2 canPassOpenDoors, 3 canPassClosedDoors, 4 avoidSun), "
            + "equipment int32 x 30 (slot 0..4: item id, damage, count), "
            + "zombie_conversion_time int32, zombie_is_child int32, zombie_is_villager int32, zombie_can_break_doors int32, zombie_is_converting int32, "
            + "skeleton_type int32, arrow_ranged_attack_time int32, arrow_field_75318_f int32, "
            + "creeper_last_active_time int32, creeper_time_since_ignited int32, creeper_fuse_time int32, creeper_explosion_radius int32, "
            + "creeper_state int32, creeper_powered int32, creeper_ignited int32, spider_climb int32, "
            + "attack_on_collide_attack_tick int32, attack_on_collide_field_75445_i int32, "
            + "attack_on_collide_px double, attack_on_collide_py double, attack_on_collide_pz double, "
            + "air int32, fire int32, death_time int32, field_70764_aw float32, limb_swing float32, limb_swing_amount float32, "
            + "attr_max_health double, attr_move_speed double, attr_follow_range double, attr_attack_damage double, "
            + "pos_x double, pos_y double, pos_z double, motion_x double, motion_y double, motion_z double, "
            + "rotation_yaw float32, rotation_pitch float32, on_ground int32, silverfish_ally_summon_cooldown int32 (zero for other kinds), "
            + "then a 44-byte path block (enderman fields zero for silverfish, all zero for other kinds): "
            + "carried_block int32 (dataWatcher 16), carrying_data int32 (dataWatcher 17), enderman_stare_timer int32, "
            + "enderman_teleport_delay int32, enderman_flags int32 (bit 0 isAggressive, bit 1 screaming), "
            + "enderman_last_entity_to_attack int32 (spawn index or -1), "
            + "creature_path_has_path int32, creature_path_index int32, creature_path_length int32, "
            + "creature_path_hash uint64 (FNV-1a over the path points, the base layout's nav_hash shape), "
            + "then pigman angerLevel int32, randomSoundDelay int32, lastEntityToAttack int32, "
            + "creature_path_has_path int32, creature_path_index int32, creature_path_length int32, creature_path_hash uint64";
    }

    static int sum(int[] a)
    {
        int s = 0;

        for (int i = 0; i < a.length; ++i) s += a[i];

        return s;
    }

    static int heightOf(WorldServer ws, int x, int z)
    {
        return ws.getHeightValue(x, z);
    }

    static int place(WorldServer ws, OutputStream out, byte[] buf, int x, int y, int z, int id, int meta) throws Exception
    {
        ws.setBlock(x, y, z, Block.getBlockById(id), meta, 2);
        le32(buf, 0, x);
        le32(buf, 4, y);
        le32(buf, 8, z);
        le16(buf, 12, id);
        buf[14] = (byte)meta;
        buf[15] = 0;
        out.write(buf, 0, 16);
        return 1;
    }

    static int kindOf(Entity e)
    {
        String n = EntityList.getEntityString(e);
        if ("Zombie".equals(n)) return KIND_ZOMBIE;
        if ("Skeleton".equals(n)) return KIND_SKELETON;
        if ("Creeper".equals(n)) return KIND_CREEPER;
        if ("Spider".equals(n)) return KIND_SPIDER;
        if ("Enderman".equals(n)) return KIND_ENDERMAN;
if ("Witch".equals(n)) return KIND_WITCH;
        if ("Silverfish".equals(n)) return KIND_SILVERFISH;
        if ("PigZombie".equals(n)) return KIND_PIGMAN;
        if ("Blaze".equals(n)) return KIND_BLAZE;
        if ("CaveSpider".equals(n)) return KIND_CAVE_SPIDER;
        if ("Pig".equals(n)) return 5;
        if ("Cow".equals(n)) return 6;
        if ("MushroomCow".equals(n)) return 7;
        if ("Chicken".equals(n)) return 8;
        if ("Sheep".equals(n)) return 9;
        return -1;
    }

    static double spawnYOf(Entity e) { return e.posY; }

    static String kindName(int kind)
    {
        return kind >= 0 && kind < KINDS.length ? KINDS[kind] : "Other";
    }

    /** One spawn record, and the entity into the probe's list. */
    static void recordSpawn(byte[] spawnBuf, OutputStream spawnsOut, PrintWriter sw, Map<Entity, Integer> spawnIndex,
                            List<Entity> list, WorldServer ws, Entity e, int kind, boolean child, boolean villager,
                            boolean wither, boolean charged) throws Exception
    {
        recordSpawn(spawnBuf, spawnsOut, sw, spawnIndex, list, ws, e, kind, child, villager, wither, charged, false);
    }

    /** The wider form: construct is 1 for an entity the setup spawned on its
     * own (a spider's jockey rider), which the replay must not spawn again. */
    static void recordSpawn(byte[] spawnBuf, OutputStream spawnsOut, PrintWriter sw, Map<Entity, Integer> spawnIndex,
                            List<Entity> list, WorldServer ws, Entity e, int kind, boolean child, boolean villager,
                            boolean wither, boolean charged, boolean construct) throws Exception
    {
        int idx = list.size();
        float difficultyFactor = e instanceof EntityLiving ? ws.func_147462_b(e.posX, e.posY, e.posZ) : 0.0F;

        spawnIndex.put(e, Integer.valueOf(idx));
        list.add(e);

        int p = 0;
        p = le32(spawnBuf, p, idx);
        p = le32(spawnBuf, p, e.getEntityId());
        p = le32(spawnBuf, p, kind);
        p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posX));
        p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posY));
        p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.posZ));
        p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationYaw));
        p = le32(spawnBuf, p, Float.floatToRawIntBits(e.rotationPitch));
        p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionX));
        p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionY));
        p = le64(spawnBuf, p, Double.doubleToRawLongBits(e.motionZ));
        p = le32(spawnBuf, p, child ? 1 : 0);
        p = le32(spawnBuf, p, villager ? 1 : 0);
        p = le32(spawnBuf, p, wither ? 1 : 0);
        p = le32(spawnBuf, p, charged ? 1 : 0);
        p = le32(spawnBuf, p, 0);
        p = le32(spawnBuf, p, Float.floatToRawIntBits(difficultyFactor));
        p = le32(spawnBuf, p, Float.floatToRawIntBits(e instanceof EntityLivingBase ? ((EntityLivingBase)e).getHealth() : 0.0F));
        p = le32(spawnBuf, p, construct ? 1 : 0);

        for (int q = 0; q < 7; ++q) p = le32(spawnBuf, p, 0);

        spawnsOut.write(spawnBuf, 0, SPAWN_BYTES);
        sw.println(idx + " " + e.getEntityId() + " " + kindName(kind) + " " + canon(e));
    }

    static boolean isPlayer(Entity e)
    {
        return e instanceof EntityPlayer;
    }

    /** The kindOf index, clamped into the birth/death tables' range. */
    static int kindIndex(Entity e)
    {
        int k = kindOf(e);
        return k < 0 ? 0 : k;
    }

    static ItemStack itemStackOf(Entity e)
    {
        if (e instanceof EntityItem) return ((EntityItem)e).getEntityItem();
        return null;
    }

    static String canon(Entity e)
    {
        net.minecraft.nbt.NBTTagCompound tag = new net.minecraft.nbt.NBTTagCompound();
        e.writeToNBT(tag);
        return StructuresProbe.canon(tag).toString();
    }

    static long nbtHash(Entity e) throws Exception
    {
        byte[] b = canon(e).getBytes("UTF-8");
        long h = FNV_OFFSET;

        for (int i = 0; i < b.length; ++i) h = (h ^ (b[i] & 255)) * FNV_PRIME;

        return h;
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

    /** The base record, ENT_STATE_BYTES at offset o (living.c's an_write_state). */
    static void writeBase(byte[] b, int o, int tick, Entity e, Map<Entity, Integer> spawnIndex, WorldServer ws) throws Exception
    {
        int p = o;
        EntityLiving lv = e instanceof EntityLiving ? (EntityLiving)e : null;
        EntityLivingBase lb = e instanceof EntityLivingBase ? (EntityLivingBase)e : null;

        p = le32(b, p, spawnIndex.get(e).intValue());
        p = le32(b, p, e.getEntityId());
        p = le32(b, p, kindOf(e));
        p = le64(b, p, nbtHash(e));
        p = le32(b, p, lb != null ? intField(lb, "entityAge") : 0);
        p = le32(b, p, e.ticksExisted);

        Object tasks = lv != null ? field(lv, "tasks") : null;
        int tickCount = tasks != null ? intOf(tasks, "tickCount") : -1;
        p = le32(b, p, tickCount);

        List<?> entries = tasks != null ? (List<?>)field(tasks, "taskEntries") : null;
        List<?> executing = tasks != null ? (List<?>)field(tasks, "executingTaskEntries") : null;
        int mask = 0;
        int[] slots = new int[10];

        for (int i = 0; i < slots.length; ++i) slots[i] = -1;

        if (entries != null)
        {
            for (int i = 0; i < entries.size(); ++i)
            {
                Object action = field(entries.get(i), "action");

                if (executing != null && executing.contains(entries.get(i))) mask |= 1 << i;
                if (i < slots.length) slots[i] = taskCounter(action);
            }
        }

        p = le32(b, p, mask);

        for (int i = 0; i < slots.length; ++i) p = le32(b, p, slots[i]);

        Object nav = lv != null ? lv.getNavigator() : null;
        Object path = nav != null ? field(nav, "currentPath") : null;
        p = le32(b, p, path != null ? 1 : 0);
        p = le32(b, p, path != null ? intOf(path, "currentPathIndex") : 0);
        p = le32(b, p, path != null ? intOf(path, "pathLength") : 0);
        p = le32(b, p, nav != null ? intOf(nav, "totalTicks") : 0);
        p = le32(b, p, nav != null ? intOf(nav, "ticksAtLastPos") : 0);
        p = le64(b, p, nav != null ? Double.doubleToRawLongBits(((Double)field(nav, "speed")).doubleValue()) : 0);
        Object lpc = nav != null ? field(nav, "lastPosCheck") : null;
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).xCoord) : 0);
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).yCoord) : 0);
        p = le64(b, p, lpc != null ? Double.doubleToRawLongBits(((net.minecraft.util.Vec3)lpc).zCoord) : 0);

        long nh = FNV_OFFSET;

        if (path != null)
        {
            Object points = field(path, "points");
            int n = Array.getLength(points);

            for (int i = 0; i < n; ++i)
            {
                Object pt = Array.get(points, i);
                int px = intOf(pt, "xCoord"), py = intOf(pt, "yCoord"), pz = intOf(pt, "zCoord");

                for (int k = 0; k < 4; ++k) nh = (nh ^ ((px >> (8 * k)) & 255)) * FNV_PRIME;
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((py >> (8 * k)) & 255)) * FNV_PRIME;
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((pz >> (8 * k)) & 255)) * FNV_PRIME;
            }
        }

        p = le64(b, p, nh);

        Object mh = lv != null ? lv.getMoveHelper() : null;
        p = le32(b, p, mh != null && boolOf(mh, "update") ? 1 : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posX")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posY")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "posZ")).doubleValue()) : 0);
        p = le64(b, p, mh != null ? Double.doubleToRawLongBits(((Double)field(mh, "speed")).doubleValue()) : 0);

        Object lk = lv != null ? lv.getLookHelper() : null;
        p = le32(b, p, lk != null && boolOf(lk, "isLooking") ? 1 : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posX")).doubleValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posY")).doubleValue()) : 0);
        p = le64(b, p, lk != null ? Double.doubleToRawLongBits(((Double)field(lk, "posZ")).doubleValue()) : 0);
        p = le32(b, p, lk != null ? Float.floatToRawIntBits(((Float)field(lk, "deltaLookYaw")).floatValue()) : 0);
        p = le32(b, p, lk != null ? Float.floatToRawIntBits(((Float)field(lk, "deltaLookPitch")).floatValue()) : 0);

        Object jh = lv != null ? lv.getJumpHelper() : null;
        p = le32(b, p, jh != null && boolOf(jh, "isJumping") ? 1 : 0);
        p = le32(b, p, lv != null ? Float.floatToRawIntBits(lv.moveForward) : 0);
        p = le32(b, p, lv != null ? Float.floatToRawIntBits(lv.moveStrafing) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.rotationYawHead) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.renderYawOffset) : 0);

        p = le32(b, p, lv != null ? lv.livingSoundTime : 0);

        Object bh = lv != null ? field(lv, "bodyHelper") : null;
        p = le32(b, p, bh != null ? intOf(bh, "field_75666_b") : 0);
        p = le32(b, p, bh != null ? Float.floatToRawIntBits(((Float)field(bh, "field_75667_c")).floatValue()) : 0);
        p = le32(b, p, e instanceof net.minecraft.entity.passive.EntitySheep ? intOf(e, "sheepTimer") : -1);
        p = le32(b, p, e instanceof net.minecraft.entity.passive.EntityChicken ? ((net.minecraft.entity.passive.EntityChicken)e).timeUntilNextEgg : -1);
        p = le32(b, p, e instanceof net.minecraft.entity.passive.EntityAnimal ? intOf(e, "breeding") : 0);
        p = le32(b, p, lb != null ? intOf(lb, "revengeTimer") : 0);
        p = le32(b, p, (e.addedToChunk ? 1 : 0) | (e.onGround ? 2 : 0));
        p = le64(b, p, Det.state(lb != null ? lb.getRNG() : (Random)findField(e, "rand").get(e)));
        p = le32(b, p, 0);

        if (p != o + ENT_STATE_BYTES) throw new IllegalStateException("base record wrote " + (p - o) + " bytes, the layout says " + ENT_STATE_BYTES);
    }

    static int taskCounter(Object action) throws Exception
    {
        if (action == null) return -1;

        String c = action.getClass().getSimpleName();

        if (c.equals("EntityAIMate")) return intOf(action, "spawnBabyDelay");
        if (c.equals("EntityAITempt")) return intOf(action, "delayTemptCounter");
        if (c.equals("EntityAIFollowParent")) return intOf(action, "field_75345_d");
        if (c.equals("EntityAIWatchClosest")) return intOf(action, "lookTime");
        if (c.equals("EntityAILookIdle")) return intOf(action, "idleTime");
        if (c.equals("EntityAIEatGrass")) return intOf(action, "field_151502_a");
        return -1;
    }

    /** The hostile extension the base record does not carry. */
    static void writeState(byte[] b, int tick, Entity e, Map<Entity, Integer> spawnIndex, WorldServer ws, PrintWriter ew) throws Exception
    {
        writeBase(b, 0, tick, e, spawnIndex, ws);
        int p = ENT_STATE_BYTES;
        EntityLiving lv = e instanceof EntityLiving ? (EntityLiving)e : null;
        EntityLivingBase lb = e instanceof EntityLivingBase ? (EntityLivingBase)e : null;

        p = le32(b, p, lv != null ? intField(lv, "attackTime") : lb != null ? intField(lb, "attackTime") : 0);
        p = le32(b, p, lb != null ? intField(lb, "hurtTime") : 0);
        p = le32(b, p, lb != null ? intField(lb, "hurtResistantTime") : 0);
        p = le32(b, p, lb != null ? intField(lb, "maxHurtTime") : 0);
        p = le32(b, p, lb != null ? intField(lb, "recentlyHit") : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.getHealth()) : 0);
        p = le32(b, p, indexOf(spawnIndex, e instanceof net.minecraft.entity.EntityCreature
            ? ((net.minecraft.entity.EntityCreature)e).getEntityToAttack() : null));
        p = le32(b, p, indexOf(spawnIndex, lv != null ? lv.getAttackTarget() : null));
        p = le32(b, p, indexOf(spawnIndex, lb != null ? (Entity)field(lb, "lastAttacker") : null));
        p = le32(b, p, indexOf(spawnIndex, lv != null ? (Entity)field(lv, "currentTarget") : null));
        p = le32(b, p, lv != null ? intField(lv, "numTicksToChaseTarget") : 0);
        p = le32(b, p, lv != null ? intField(lv, "fleeingTick") : 0);
        p = le32(b, p, lv != null && boolOf(lv, "hasAttacked") ? 1 : 0);

        int navFlags = 0;

        if (lv != null)
        {
            Object nav = lv.getNavigator();
            if (boolOf(nav, "avoidsWater")) navFlags |= 1;
            if (boolOf(nav, "canSwim")) navFlags |= 2;
            if (boolOf(nav, "canPassOpenWoodenDoors")) navFlags |= 4;
            if (boolOf(nav, "canPassClosedWoodenDoors")) navFlags |= 8;
            if (boolOf(nav, "noSunPathfind")) navFlags |= 16;
        }

        p = le32(b, p, navFlags);

        for (int slot = 0; slot < 5; ++slot)
        {
            ItemStack st = lv != null ? lv.getEquipmentInSlot(slot) : null;
            p = le32(b, p, st == null ? -1 : Item.getIdFromItem(st.getItem()));
            p = le32(b, p, st == null ? 0 : st.getItemDamage());
            p = le32(b, p, st == null ? 0 : st.stackSize);
        }

        // The kind state. A class check, not a cast: the probe's own player and
        // the absorbed children share this record.
        p = le32(b, p, e instanceof EntityZombie ? intOf(e, "conversionTime") : 0);
        p = le32(b, p, e instanceof EntityZombie && ((EntityZombie)e).isChild() ? 1 : 0);
        p = le32(b, p, e instanceof EntityZombie && ((EntityZombie)e).isVillager() ? 1 : 0);
        p = le32(b, p, e instanceof EntityZombie && ((EntityZombie)e).func_146072_bX() ? 1 : 0);
        p = le32(b, p, e instanceof EntityZombie && ((EntityZombie)e).isConverting() ? 1 : 0);
        p = le32(b, p, e instanceof EntitySkeleton ? ((EntitySkeleton)e).getSkeletonType() : -1);
        p = le32(b, p, arrowRangedAttackTime(e));
        p = le32(b, p, arrowField75318f(e));
        p = le32(b, p, e instanceof EntityCreeper ? intOf(e, "lastActiveTime") : 0);
        p = le32(b, p, e instanceof EntityCreeper ? intOf(e, "timeSinceIgnited") : 0);
        p = le32(b, p, e instanceof EntityCreeper ? intOf(e, "fuseTime") : 0);
        p = le32(b, p, e instanceof EntityCreeper ? intOf(e, "explosionRadius") : 0);
        p = le32(b, p, e instanceof EntityCreeper ? ((EntityCreeper)e).getCreeperState() : 0);
        p = le32(b, p, e instanceof EntityCreeper && ((EntityCreeper)e).getPowered() ? 1 : 0);
        p = le32(b, p, e instanceof EntityCreeper && func_146078_ca(e) ? 1 : 0);
        p = le32(b, p, e instanceof EntitySpider && ((EntitySpider)e).isBesideClimbableBlock() ? 1 : 0);
        p = le32(b, p, collideAttackTick(e));
        p = le32(b, p, collideField75445_i(e));
        p = le64(b, p, Double.doubleToRawLongBits(collideField(e, "field_151497_i")));
        p = le64(b, p, Double.doubleToRawLongBits(collideField(e, "field_151495_j")));
        p = le64(b, p, Double.doubleToRawLongBits(collideField(e, "field_151496_k")));
        p = le32(b, p, lb != null ? lb.getAir() : 0);
        p = le32(b, p, intField(e, "fire"));
        p = le32(b, p, lb != null ? intField(lb, "deathTime") : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(((Float)field(lb, "field_70764_aw")).floatValue()) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.limbSwing) : 0);
        p = le32(b, p, lb != null ? Float.floatToRawIntBits(lb.limbSwingAmount) : 0);
        p = le64(b, p, Double.doubleToRawLongBits(lb != null ? attrValue(lb, "maxHealth") : 0.0D));
        p = le64(b, p, Double.doubleToRawLongBits(lb != null ? attrValue(lb, "movementSpeed") : 0.0D));
        p = le64(b, p, Double.doubleToRawLongBits(lb != null ? attrValue(lb, "followRange") : 0.0D));
        p = le64(b, p, Double.doubleToRawLongBits(lb != null ? attrValue(lb, "attackDamage") : 0.0D));
        p = le64(b, p, Double.doubleToRawLongBits(e.posX));
        p = le64(b, p, Double.doubleToRawLongBits(e.posY));
        p = le64(b, p, Double.doubleToRawLongBits(e.posZ));
        p = le64(b, p, Double.doubleToRawLongBits(e.motionX));
        p = le64(b, p, Double.doubleToRawLongBits(e.motionY));
        p = le64(b, p, Double.doubleToRawLongBits(e.motionZ));
        p = le32(b, p, Float.floatToRawIntBits(e.rotationYaw));
        p = le32(b, p, Float.floatToRawIntBits(e.rotationPitch));
        p = le32(b, p, e.onGround ? 1 : 0);
        p = le32(b, p, e instanceof EntitySilverfish ? intOf(e, "allySummonCooldown") : 0);

        if (p != ENT_STATE_BYTES + 332)
        {
            throw new IllegalStateException("the shared state record wrote " + p + " bytes, the layout says "
                + (ENT_STATE_BYTES + 332));
        }

        p = writeEnderman(b, p, e, spawnIndex);
        p = le32(b, p, e instanceof EntityPigZombie ? intOf(e, "angerLevel") : 0);
        p = le32(b, p, e instanceof EntityPigZombie ? intOf(e, "randomSoundDelay") : 0);
        p = le32(b, p, e instanceof EntityPigZombie ? indexOf(spawnIndex, (Entity)field(e, "field_110191_bu")) : -1);
        Object pigmanPath = e instanceof EntityPigZombie ? field(e, "pathToEntity") : null;
        p = le32(b, p, pigmanPath != null ? 1 : 0);
        p = le32(b, p, pigmanPath != null ? intOf(pigmanPath, "currentPathIndex") : 0);
        p = le32(b, p, pigmanPath != null ? intOf(pigmanPath, "pathLength") : 0);
        p = le64(b, p, pathHash(pigmanPath));

        if (p != HOSTILE_STATE_BYTES)
        {
            throw new IllegalStateException("state record wrote " + p + " bytes, the layout says " + HOSTILE_STATE_BYTES);
        }
    }

    /** The enderman block at the tail of the hostile extension: EntityEnderman's
     * data watcher bytes 16 and 17, the stare timer, the teleport delay, the
     * aggressive and screaming flags, lastEntityToAttack, and the EntityCreature
     * pathToEntity the next tick reads. Every other kind writes zeros. */
    static int writeEnderman(byte[] b, int p, Entity e, Map<Entity, Integer> spawnIndex) throws Exception
    {
        if (!(e instanceof EntityEnderman) && !(e instanceof EntitySilverfish))
        {
            for (int i = 0; i < ENDERMAN_EXTRA_BYTES / 4; ++i) p = le32(b, p, 0);

            return p;
        }

        EntityEnderman en = e instanceof EntityEnderman ? (EntityEnderman)e : null;
        Object path = field(e, "pathToEntity");   /* EntityCreature's, private */

        p = le32(b, p, en != null ? Block.getIdFromBlock(en.func_146080_bZ()) : 0);
        p = le32(b, p, en != null ? en.getCarryingData() : 0);
        p = le32(b, p, en != null ? intOf(en, "stareTimer") : 0);
        p = le32(b, p, en != null ? intOf(en, "teleportDelay") : 0);
        p = le32(b, p, en != null ? ((boolOf(en, "isAggressive") ? 1 : 0) | (en.isScreaming() ? 2 : 0)) : 0);
        p = le32(b, p, en != null ? indexOf(spawnIndex, (Entity)field(en, "lastEntityToAttack")) : 0);
        p = le32(b, p, path != null ? 1 : 0);
        p = le32(b, p, path != null ? intOf(path, "currentPathIndex") : 0);
        p = le32(b, p, path != null ? intOf(path, "pathLength") : 0);

        p = le64(b, p, pathHash(path));

        return p;
    }

    static long pathHash(Object path) throws Exception
    {
        long nh = FNV_OFFSET;
        if (path != null)
        {
            Object points = field(path, "points");
            int n = Array.getLength(points);
            for (int i = 0; i < n; ++i)
            {
                Object pt = Array.get(points, i);
                int px = intOf(pt, "xCoord"), py = intOf(pt, "yCoord"), pz = intOf(pt, "zCoord");
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((px >> (8 * k)) & 255)) * FNV_PRIME;
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((py >> (8 * k)) & 255)) * FNV_PRIME;
                for (int k = 0; k < 4; ++k) nh = (nh ^ ((pz >> (8 * k)) & 255)) * FNV_PRIME;
            }
        }
        return nh;
    }

    /** EntityCreeper.func_146078_ca, the private 'ignited' datawatcher bit 18. */
    static boolean func_146078_ca(Entity e) throws Exception
    {
        Object dw = field(e, "dataWatcher");
        Object v = dw.getClass().getMethod("getWatchableObjectByte", int.class).invoke(dw, Integer.valueOf(18));
        return ((Byte)v).byteValue() == 1;
    }

    static int indexOf(Map<Entity, Integer> spawnIndex, Entity e)
    {
        if (e == null) return -1;
        Integer i = spawnIndex.get(e);
        return i == null ? -1 : i.intValue();
    }

    static double attrValue(EntityLivingBase lb, String field) throws Exception
    {
        net.minecraft.entity.ai.attributes.IAttribute attr =
            (net.minecraft.entity.ai.attributes.IAttribute)net.minecraft.entity.SharedMonsterAttributes.class.getField(field).get(null);
        net.minecraft.entity.ai.attributes.IAttributeInstance inst = lb.getEntityAttribute(attr);
        return inst == null ? 0.0D : inst.getAttributeValue();
    }

    /** EntityAIArrowAttack's rangedAttackTime, out of the owning skeleton's task list. */
    static int arrowRangedAttackTime(Entity e) throws Exception
    {
        Object action = findTaskAction(e, "EntityAIArrowAttack");
        return action == null ? 0 : intOf(action, "rangedAttackTime");
    }

    static int arrowField75318f(Entity e) throws Exception
    {
        Object action = findTaskAction(e, "EntityAIArrowAttack");
        return action == null ? 0 : intOf(action, "field_75318_f");
    }

    static int collideAttackTick(Entity e) throws Exception
    {
        Object action = findTaskAction(e, "EntityAIAttackOnCollide");
        return action == null ? 0 : intOf(action, "attackTick");
    }

    static int collideField75445_i(Entity e) throws Exception
    {
        Object action = findTaskAction(e, "EntityAIAttackOnCollide");
        return action == null ? 0 : intOf(action, "field_75445_i");
    }

    static double collideField(Entity e, String name) throws Exception
    {
        Object action = findTaskAction(e, "EntityAIAttackOnCollide");
        return action == null ? 0.0D : ((Double)field(action, name)).doubleValue();
    }

    /** The first task entry whose action is class c, walking both task lists. */
    static Object findTaskAction(Entity e, String c) throws Exception
    {
        if (!(e instanceof EntityLiving)) return null;

        EntityLiving lv = (EntityLiving)e;
        Object[] lists = {field(lv, "tasks"), field(lv, "targetTasks")};

        for (int k = 0; k < 2; ++k)
        {
            List<?> entries = (List<?>)field(lists[k], "taskEntries");

            for (int i = 0; i < entries.size(); ++i)
            {
                Object action = field(entries.get(i), "action");

                if (action != null && action.getClass().getSimpleName().equals(c)) return action;
            }
        }

        return null;
    }

    static Field findField(Object o, String name)
    {
        for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass())
        {
            try
            {
                Field f = c.getDeclaredField(name);
                f.setAccessible(true);
                return f;
            }
            catch (NoSuchFieldException e)
            {
            }
        }

        throw new IllegalStateException("no field " + name + " on " + o.getClass());
    }

    static Object field(Object o, String name)
    {
        try
        {
            return findField(o, name).get(o);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static int intOf(Object o, String name)
    {
        return ((Number)field(o, name)).intValue();
    }

    static boolean boolOf(Object o, String name)
    {
        return ((Boolean)field(o, name)).booleanValue();
    }

    static int intField(Object o, String name)
    {
        try
        {
            return findField(o, name).getInt(o);
        }
        catch (Exception e)
        {
            throw new RuntimeException(e);
        }
    }

    static void writeDetLine(PrintWriter w, int t, WorldServer ws) throws Exception
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

        Object wr = ws.rand;
        b.append(" worldRand ").append(hex(Det.state((Random)wr)));
        b.append(" worldRandGauss ").append(boolOf(wr, "haveNextNextGaussian") ? 1 : 0);
        b.append(" skylight ").append(ws.skylightSubtracted).append(" worldTime ").append(ws.getWorldTime());
        w.println(b.toString());
    }

    static void writeDetState(File f, long seed, WorldServer ws) throws Exception
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

        Object wr = ws.rand;
        b.setLength(0);
        b.append("worldRand ").append(hex(Det.state((Random)wr))).append(' ').append(boolOf(wr, "haveNextNextGaussian") ? 1 : 0);
        w.println(b.toString());

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
