package qrl;

import com.mojang.authlib.GameProfile;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.init.Items;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.inventory.SlotFurnaceOutput;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.stats.AchievementList;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatList;
import net.minecraft.stats.StatisticsManager;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;
import net.minecraftforge.fml.common.FMLCommonHandler;
import net.minecraftforge.fml.common.eventhandler.Event;
import net.minecraftforge.fml.common.eventhandler.SubscribeEvent;
import net.minecraftforge.fml.common.gameevent.PlayerEvent;

/** Exact SlotFurnaceOutput ordering and side-effect oracle for 1.11.2. */
public final class FurnaceOutputGolden {
    private static final long MULT = 0x5deece66dL;
    private static final long MASK = (1L << 48) - 1L;

    private static final class Timeline {
        final List<String> rows = new ArrayList<String>();
    }

    private static final class MemoryWorld extends World {
        final Timeline timeline;

        MemoryWorld(Timeline timeline) {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "furnace-output-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
            this.provider.setWorld(this);
            this.timeline = timeline;
        }

        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
        public boolean spawnEntity(Entity entity) {
            EntityXPOrb orb = (EntityXPOrb)entity;
            timeline.rows.add("X " + orb.getXpValue());
            return true;
        }
    }

    private static final class TestPlayer extends EntityPlayer {
        final StatisticsManager statistics = new StatisticsManager();
        final Timeline timeline;

        TestPlayer(World world, Timeline timeline) {
            super(world, new GameProfile(
                new UUID(31L, 37L), "furnace-output-test"));
            this.timeline = timeline;
            setPosition(2.25D, 70.0D, -3.5D);
        }

        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
        public void onItemPickup(Entity entity, int count) {}
        public void addStat(StatBase stat, int amount) {
            if (stat == null) return;
            timeline.rows.add("S " + stat.statId + " " + amount);
            statistics.increaseStat(this, stat, amount);
        }
    }

    public static final class Listener {
        final Timeline timeline;

        Listener(Timeline timeline) { this.timeline = timeline; }

        @SubscribeEvent
        public void onEvent(Event raw) {
            if (!(raw instanceof PlayerEvent.ItemSmeltedEvent)) return;
            PlayerEvent.ItemSmeltedEvent event =
                (PlayerEvent.ItemSmeltedEvent)raw;
            ItemStack stack = event.smelting;
            timeline.rows.add("E "
                + Item.getIdFromItem(stack.getItem()) + " "
                + stack.getCount() + " " + stack.getMetadata());
        }
    }

    private static Random mathRandom() throws Exception {
        Class<?> holder =
            Class.forName("java.lang.Math$RandomNumberGeneratorHolder");
        Field field = holder.getDeclaredField("randomNumberGenerator");
        field.setAccessible(true);
        return (Random)field.get(null);
    }

    private static long rawSeed(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return ((AtomicLong)field.get(random)).get() & MASK;
    }

    private static void setSeed48(Random random, long seed48) {
        random.setSeed(seed48 ^ MULT);
    }

    private static void run(
            String tag, ItemStack output, int take, boolean furnacePrerequisite)
            throws Exception {
        Timeline timeline = new Timeline();
        MemoryWorld world = new MemoryWorld(timeline);
        TestPlayer player = new TestPlayer(world, timeline);
        if (furnacePrerequisite) {
            player.statistics.unlockAchievement(
                player, AchievementList.BUILD_FURNACE, 1);
        }
        Listener listener = new Listener(timeline);
        FMLCommonHandler.instance().bus().register(listener);
        try {
            InventoryBasic inventory = new InventoryBasic("furnace", false, 1);
            inventory.setInventorySlotContents(0, output.copy());
            SlotFurnaceOutput slot =
                new SlotFurnaceOutput(player, inventory, 0, 0, 0);
            Random math = mathRandom();
            setSeed48(math, 0x123456789abCL);
            ItemStack removed = slot.decrStackSize(take);
            slot.onTake(player, removed);
            for (String row : timeline.rows)
                System.out.println(tag + " " + row);
            StatBase craft = StatList.getCraftStats(output.getItem());
            System.out.printf(
                "R %s %d %d %d %012x%n",
                tag,
                craft == null ? -1 : player.statistics.readStat(craft),
                player.statistics.readStat(AchievementList.ACQUIRE_IRON),
                player.statistics.readStat(AchievementList.COOK_FISH),
                rawSeed(math));
        } finally {
            FMLCommonHandler.instance().bus().unregister(listener);
        }
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        run("I0", new ItemStack(Items.IRON_INGOT, 5), 3, false);
        run("I1", new ItemStack(Items.IRON_INGOT, 5), 3, true);
        run("F1", new ItemStack(Items.COOKED_FISH, 4, 1), 2, true);
        run("S1", new ItemStack(Blocks.STONE, 4), 4, true);
    }
}
