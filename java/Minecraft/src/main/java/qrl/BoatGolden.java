package qrl;

import com.google.common.base.Predicate;
import com.mojang.authlib.GameProfile;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.annotation.Nullable;
import net.minecraft.block.state.IBlockState;
import net.minecraft.block.Block;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityBoat;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.passive.EntitySquid;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.item.Item;
import net.minecraft.item.ItemBoat;
import net.minecraft.init.Bootstrap;
import net.minecraft.profiler.Profiler;
import net.minecraft.network.Packet;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Deterministic 1.11.2 EntityBoat status and motion boundary oracle. */
public final class BoatGolden {
    static final class MemoryWorld extends World {
        final Map<BlockPos, IBlockState> blocks =
            new HashMap<BlockPos, IBlockState>();
        final List<Entity> spawned = new ArrayList<Entity>();
        final List<Entity> collisions = new ArrayList<Entity>();

        MemoryWorld() { this(false); }

        MemoryWorld(boolean remote) {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "boat-oracle"),
                new WorldProviderSurface(), new Profiler(), remote);
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return new BlockPos(0, 64, 0); }
        public IBlockState getBlockState(BlockPos pos) {
            IBlockState state = blocks.get(pos);
            return state == null ? Blocks.AIR.getDefaultState() : state;
        }
        public List<Entity> getEntitiesInAABBexcluding(
                @Nullable Entity excluded,
                net.minecraft.util.math.AxisAlignedBB box,
                @Nullable Predicate<? super Entity> filter) {
            List<Entity> result = new ArrayList<Entity>();
            for (Entity entity : collisions)
                if (entity != excluded
                        && entity.getEntityBoundingBox().intersectsWith(box)
                        && (filter == null || filter.apply(entity)))
                    result.add(entity);
            return result;
        }
        public void sendPacketToServer(Packet<?> packet) {}
        public boolean spawnEntity(Entity entity) {
            spawned.add(entity);
            return true;
        }
        void put(BlockPos pos, IBlockState state) {
            blocks.put(pos.toImmutable(), state);
        }
    }

    static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(new UUID(1L, 2L), "boat-test"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
        public boolean isUser() { return true; }
    }

    private static Field field(String name) throws Exception {
        Field value = EntityBoat.class.getDeclaredField(name);
        value.setAccessible(true);
        return value;
    }

    private static String dbits(double value) {
        return String.format("%016x", Double.doubleToRawLongBits(value));
    }

    private static String fbits(float value) {
        return String.format("%08x", Float.floatToRawIntBits(value));
    }

    private static void fill(MemoryWorld world, int y,
            IBlockState state) {
        for (int x = -2; x <= 2; ++x)
            for (int z = -2; z <= 2; ++z)
                world.put(new BlockPos(x, y, z), state);
    }

    private static void run(String name, MemoryWorld world, double y,
            double vx, double vy, double vz,
            EntityBoat.Status initialStatus) throws Exception {
        EntityBoat boat = new EntityBoat(world, 0.5D, y, 0.5D);
        boat.motionX = vx;
        boat.motionY = vy;
        boat.motionZ = vz;
        if (initialStatus != null)
            field("status").set(boat, initialStatus);
        boat.onUpdate();
        System.out.printf(
            "%s %s %s %s %s %s %s %s %s %s %s %s %s %d %s%n",
            name,
            field("status").get(boat),
            field("previousStatus").get(boat),
            dbits(boat.posX), dbits(boat.posY), dbits(boat.posZ),
            dbits(boat.motionX), dbits(boat.motionY), dbits(boat.motionZ),
            dbits(field("waterLevel").getDouble(boat)),
            fbits(field("boatGlide").getFloat(boat)),
            fbits(field("momentum").getFloat(boat)),
            dbits(field("lastYd").getDouble(boat)),
            boat.onGround ? 1 : 0, fbits(boat.fallDistance));
    }

    private static void runRider(String name, boolean left, boolean right,
            boolean forward, boolean back) throws Exception {
        MemoryWorld world = new MemoryWorld(true);
        fill(world, 79, Blocks.WATER.getStateFromMeta(0));
        EntityBoat boat = new EntityBoat(world, 0.5D, 79.6D, 0.5D);
        TestPlayer player = new TestPlayer(world);
        player.setPosition(0.5D, 80.0D, 0.5D);
        if (!player.startRiding(boat))
            throw new IllegalStateException("rider fixture did not mount");
        boat.updateInputs(left, right, forward, back);
        boat.onUpdate();
        boat.updatePassenger(player);
        float[] paddles = (float[])field("paddlePositions").get(boat);
        System.out.printf(
            "R%s %s %s %s %s %s %s %s %s %s %s %d %d %s %s %s %s%n",
            name,
            dbits(boat.posX), dbits(boat.posY), dbits(boat.posZ),
            dbits(boat.motionX), dbits(boat.motionY), dbits(boat.motionZ),
            fbits(boat.rotationYaw),
            fbits(field("deltaRotation").getFloat(boat)),
            fbits(paddles[0]), fbits(paddles[1]),
            boat.getPaddleState(0) ? 1 : 0,
            boat.getPaddleState(1) ? 1 : 0,
            dbits(player.posX), dbits(player.posY), dbits(player.posZ),
            fbits(player.rotationYaw));
    }

    private static void runSubmergedEject() throws Exception {
        MemoryWorld world = new MemoryWorld(false);
        for (int y = 79; y <= 90; ++y)
            fill(world, y, Blocks.WATER.getStateFromMeta(0));
        EntityBoat boat = new EntityBoat(world, 0.5D, 79.2D, 0.5D);
        TestPlayer player = new TestPlayer(world);
        player.setPosition(0.5D, 80.0D, 0.5D);
        if (!player.startRiding(boat))
            throw new IllegalStateException("submerged rider did not mount");
        for (int tick = 1; tick <= 60; ++tick) {
            boat.setPosition(0.5D, 79.2D, 0.5D);
            boat.motionX = boat.motionY = boat.motionZ = 0.0D;
            boat.onUpdate();
            if (tick == 59 || tick == 60)
                System.out.printf("U%d %s %s %d%n", tick,
                    field("status").get(boat),
                    fbits(field("outOfControlTicks").getFloat(boat)),
                    player.isRiding() ? 1 : 0);
        }
    }

    private static void runFallBreak(boolean drops) throws Exception {
        MemoryWorld world = new MemoryWorld(false);
        world.getGameRules().setOrCreateGameRule(
            "doEntityDrops", drops ? "true" : "false");
        fill(world, 79, Blocks.STONE.getDefaultState());
        EntityBoat boat = new EntityBoat(world, 0.5D, 80.0D, 0.5D);
        boat.fallDistance = 4.0F;
        boat.motionY = -0.2D;
        boat.onUpdate();
        System.out.printf("F%d %d %d", drops ? 1 : 0,
            boat.isDead ? 1 : 0,
            world.spawned.size());
        for (Entity entity : world.spawned) {
            EntityItem item = (EntityItem)entity;
            System.out.printf(" %d:%d:%d",
                Item.getIdFromItem(item.getEntityItem().getItem()),
                item.getEntityItem().getMetadata(),
                item.getEntityItem().getCount());
        }
        System.out.println();
    }

    private static void runClientLerp() throws Exception {
        MemoryWorld world = new MemoryWorld(true);
        EntityBoat boat = new EntityBoat(world, 0.5D, 80.0D, 0.5D);
        boat.rotationYaw = -170.0F;
        boat.rotationPitch = 10.0F;
        boat.setPositionAndRotationDirect(
            10.25D, 82.75D, -3.5D, 170.0F, -20.0F, 3, false);
        for (int tick = 1; tick <= 10; ++tick) {
            boat.onUpdate();
            if (tick == 1 || tick == 5 || tick == 10)
                System.out.printf("L%d %s %s %s %s %s %d%n", tick,
                    dbits(boat.posX), dbits(boat.posY), dbits(boat.posZ),
                    fbits(boat.rotationYaw), fbits(boat.rotationPitch),
                    field("lerpSteps").getInt(boat));
        }
    }

    private static void runTwoPassengers() throws Exception {
        MemoryWorld world = new MemoryWorld(false);
        EntityBoat boat = new EntityBoat(world, 4.5D, 80.0D, -2.5D);
        boat.rotationYaw = 30.0F;
        field("deltaRotation").setFloat(boat, 5.0F);
        EntityPig first = new EntityPig(world);
        EntityPig second = new EntityPig(world);
        first.setEntityId(100);
        second.setEntityId(101);
        first.rotationYaw = first.prevRotationYaw = 200.0F;
        second.rotationYaw = second.prevRotationYaw = -200.0F;
        first.rotationYawHead = first.renderYawOffset = 12.0F;
        second.rotationYawHead = second.renderYawOffset = -12.0F;
        if (!first.startRiding(boat) || !second.startRiding(boat))
            throw new IllegalStateException("two-passenger fixture did not mount");
        boat.updatePassenger(first);
        boat.updatePassenger(second);
        for (EntityPig pig : new EntityPig[] {first, second})
            System.out.printf("P%d %s %s %s %s %s %s %n",
                pig.getEntityId(), dbits(pig.posX), dbits(pig.posY),
                dbits(pig.posZ), fbits(pig.rotationYaw),
                fbits(pig.getRotationYawHead()),
                fbits(pig.renderYawOffset));
    }

    private static void runAutomaticPassengers() throws Exception {
        MemoryWorld world = new MemoryWorld(false);
        EntityBoat boat = new EntityBoat(world, 0.5D, 80.0D, 0.5D);
        boat.setEntityId(99);
        EntityPig first = new EntityPig(world);
        EntityPig second = new EntityPig(world);
        EntityPig third = new EntityPig(world);
        EntityPig[] pigs = {first, second, third};
        for (int index = 0; index < pigs.length; ++index) {
            pigs[index].setEntityId(100 + index);
            pigs[index].setPosition(0.60D + 0.02D * index, 79.95D, 0.5D);
            world.collisions.add(pigs[index]);
        }
        boat.onUpdate();
        for (EntityPig pig : pigs)
            if (boat.isPassenger(pig)) boat.updatePassenger(pig);
        System.out.printf("A %d %d %d %s %s %s %s%n",
            boat.isPassenger(first) ? 1 : 0,
            boat.isPassenger(second) ? 1 : 0,
            boat.isPassenger(third) ? 1 : 0,
            dbits(boat.motionX), dbits(boat.motionZ),
            dbits(third.motionX), dbits(third.motionZ));
    }

    private static void runSquidPush() throws Exception {
        MemoryWorld world = new MemoryWorld(false);
        EntityBoat boat = new EntityBoat(world, 0.5D, 80.0D, 0.5D);
        boat.setEntityId(99);
        EntitySquid squid = new EntitySquid(world);
        squid.setEntityId(100);
        squid.setPosition(0.6D, 79.95D, 0.5D);
        world.collisions.add(squid);
        boat.onUpdate();
        System.out.printf("S %d %s %s %s %s%n",
            boat.isPassenger(squid) ? 1 : 0,
            dbits(boat.motionX), dbits(boat.motionZ),
            dbits(squid.motionX), dbits(squid.motionZ));
    }

    private static void runBoatPush() throws Exception {
        MemoryWorld world = new MemoryWorld(false);
        EntityBoat boat = new EntityBoat(world, 0.5D, 80.0D, 0.5D);
        EntityBoat other = new EntityBoat(world, 0.6D, 80.0D, 0.5D);
        boat.setEntityId(99);
        other.setEntityId(100);
        world.collisions.add(other);
        boat.onUpdate();
        System.out.printf("B %s %s %s %s%n",
            dbits(boat.motionX), dbits(boat.motionZ),
            dbits(other.motionX), dbits(other.motionZ));
    }

    private static void runItemVariants() throws Exception {
        Field type = ItemBoat.class.getDeclaredField("type");
        type.setAccessible(true);
        int[] items = {333, 444, 445, 446, 447, 448};
        for (int item : items)
            System.out.printf("V %d %d%n", item,
                ((EntityBoat.Type)type.get(Item.getItemById(item))).ordinal());
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();

        run("air", new MemoryWorld(), 80.0D,
            0.2D, 0.1D, -0.15D, null);

        MemoryWorld land = new MemoryWorld();
        fill(land, 79, Blocks.STONE.getDefaultState());
        run("land", land, 80.0D,
            0.2D, 0.0D, -0.15D, null);

        MemoryWorld slime = new MemoryWorld();
        fill(slime, 79, Blocks.SLIME_BLOCK.getDefaultState());
        run("slime", slime, 80.0D,
            0.2D, 0.0D, -0.15D, null);

        MemoryWorld water = new MemoryWorld();
        fill(water, 79, Blocks.WATER.getStateFromMeta(0));
        run("water", water, 79.6D,
            0.2D, 0.0D, -0.15D, null);

        MemoryWorld under = new MemoryWorld();
        fill(under, 79, Blocks.WATER.getStateFromMeta(0));
        run("under", under, 79.2D,
            0.2D, 0.0D, -0.15D, null);

        MemoryWorld flowing = new MemoryWorld();
        fill(flowing, 79, Blocks.FLOWING_WATER.getStateFromMeta(1));
        run("flowing", flowing, 79.1D,
            0.2D, 0.0D, -0.15D, null);

        MemoryWorld entry = new MemoryWorld();
        fill(entry, 79, Blocks.WATER.getStateFromMeta(0));
        run("entry", entry, 79.6D,
            0.2D, -0.2D, -0.15D, EntityBoat.Status.IN_AIR);

        IBlockState[] obstacleStates = {
            Blocks.STONE_SLAB.getStateFromMeta(0),
            Blocks.STONE_SLAB.getStateFromMeta(8),
            Blocks.OAK_STAIRS.getStateFromMeta(0),
            Blocks.OAK_STAIRS.getStateFromMeta(1),
            Blocks.OAK_FENCE.getDefaultState(),
            Blocks.GLASS_PANE.getDefaultState(),
            Blocks.OAK_DOOR.getStateFromMeta(0),
            Blocks.TRAPDOOR.getStateFromMeta(0)
        };
        String[] obstacleNames = {
            "slab_bottom", "slab_top", "stair_east", "stair_west",
            "fence", "pane", "door", "trapdoor"
        };
        for (int obstacle = 0; obstacle < obstacleStates.length; ++obstacle) {
            MemoryWorld shaped = new MemoryWorld();
            shaped.put(new BlockPos(2, 80, 0), obstacleStates[obstacle]);
            run(obstacleNames[obstacle], shaped, 80.0D,
                1.0D, 0.0D, 0.0D, null);
        }
        int[] collisionBlocks = {
            26, 29, 33, 44, 53, 54, 60, 65, 67, 78, 81, 85, 92,
            96, 102, 107, 108, 109, 114, 116, 117, 118, 120, 122,
            126, 127, 128, 130, 134, 135, 136, 139, 140, 144, 145,
            146, 151, 154, 156, 163, 164, 167, 171, 178, 180, 182,
            198, 199, 205, 208
        };
        MemoryWorld collisionWorld = new MemoryWorld();
        for (int id : collisionBlocks) {
            for (int meta = 0; meta < 16; ++meta) {
                if ((id == 29 || id == 33) && (meta & 7) > 5)
                    continue;
                if ((id == 92 && meta > 6)
                        || (id == 118 && meta > 3)
                        || (id == 127 && (meta >> 2) > 2)
                        || (id == 145 && (meta >> 2) > 2)
                        || (id == 154
                            && ((meta & 7) == 1 || (meta & 7) > 5)))
                    continue;
                int[][] layouts = {
                    {2, 80, 0}, {-1, 80, 0}, {0, 80, 2},
                    {0, 80, -1}, {0, 79, 0}
                };
                double[][] motions = {
                    {1.0D, 0.0D, 0.0D}, {-1.0D, 0.0D, 0.0D},
                    {0.0D, 0.0D, 1.0D}, {0.0D, 0.0D, -1.0D},
                    {0.0D, -1.0D, 0.0D}
                };
                for (int layout = 0; layout < layouts.length; ++layout) {
                    collisionWorld.blocks.clear();
                    collisionWorld.put(new BlockPos(
                        layouts[layout][0], layouts[layout][1],
                        layouts[layout][2]),
                        Block.getBlockById(id).getStateFromMeta(meta));
                    run(String.format("M%03d_%02d_%d", id, meta, layout),
                        collisionWorld, layout == 4 ? 81.0D : 80.0D,
                        motions[layout][0], motions[layout][1],
                        motions[layout][2], null);
                }
            }
        }

        /* getActualState-dependent collision shapes.  Each four-bit mask
         * places the same block north/east/south/west of the center, so this
         * exhausts the connected-shape inputs rather than testing only the
         * isolated default state. */
        int[] connectedBlocks = {54, 85, 101, 102, 113, 139, 146, 160};
        int[][] neighbors = {
            {0, 0, -1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0}
        };
        for (int id : connectedBlocks) {
            for (int meta = 0; meta < 16; ++meta) {
                for (int mask = 0; mask < 16; ++mask) {
                    collisionWorld.blocks.clear();
                    IBlockState state = Block.getBlockById(id)
                        .getStateFromMeta(meta);
                    collisionWorld.put(new BlockPos(2, 80, 0), state);
                    for (int side = 0; side < 4; ++side)
                        if ((mask & (1 << side)) != 0)
                            collisionWorld.put(new BlockPos(
                                2 + neighbors[side][0], 80,
                                neighbors[side][2]), state);
                    run(String.format("N%03d_%02d_%02d", id, meta, mask),
                        collisionWorld, 80.0D, 1.0D, 0.0D, 0.0D, null);
                }
            }
        }

        runRider("forward", false, false, true, false);
        runRider("left", true, false, false, false);
        runRider("right_back", false, true, false, true);
        runRider("forward_left", true, false, true, false);
        runSubmergedEject();
        runFallBreak(true);
        runFallBreak(false);
        runClientLerp();
        runTwoPassengers();
        runAutomaticPassengers();
        runSquidPush();
        runBoatPush();
        runItemVariants();
    }
}
