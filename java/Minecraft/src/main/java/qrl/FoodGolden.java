package qrl;

import com.mojang.authlib.GameProfile;
import java.util.UUID;
import java.util.Collections;
import java.util.List;
import java.util.ArrayList;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import java.lang.reflect.Field;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.Entity;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.potion.PotionEffect;
import net.minecraft.potion.Potion;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.AxisAlignedBB;
import net.minecraft.util.EnumParticleTypes;
import net.minecraft.util.SoundCategory;
import net.minecraft.util.SoundEvent;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Direct ItemFood.onItemUseFinish corpus for every 1.11.2 food variant. */
public final class FoodGolden {
    private static final class MemoryWorld extends World {
        int particleCount;
        final double[] particleLast = new double[6];
        final List<String> sounds = new ArrayList<String>();
        MemoryWorld(long randomSeed) {
            super(new SaveHandlerMP(), new WorldInfo(new WorldSettings(
                0L, GameType.SURVIVAL, true, false, WorldType.DEFAULT),
                "food-oracle"), new WorldProviderSurface(),
                new Profiler(), false);
            provider.setWorld(this);
            rand.setSeed(randomSeed);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return BlockPos.ORIGIN; }
        public IBlockState getBlockState(BlockPos pos) {
            return pos.getY() == 64
                ? Blocks.STONE.getDefaultState() : Blocks.AIR.getDefaultState();
        }
        public void updateEntityWithOptionalForce(
                Entity entity, boolean forceUpdate) {}
        public List<AxisAlignedBB> getCollisionBoxes(
                Entity entity, AxisAlignedBB box) {
            return Collections.emptyList();
        }
        public boolean containsAnyLiquid(AxisAlignedBB box) { return false; }
        public void spawnParticle(EnumParticleTypes type,
                double x, double y, double z,
                double vx, double vy, double vz, int... parameters) {
            ++particleCount;
            particleLast[0]=x;particleLast[1]=y;particleLast[2]=z;
            particleLast[3]=vx;particleLast[4]=vy;particleLast[5]=vz;
        }
        public void playSound(EntityPlayer except, double x, double y, double z,
                SoundEvent sound, SoundCategory category,
                float volume, float pitch) {
            sounds.add(SoundEvent.REGISTRY.getNameForObject(sound).toString()
                + "@" + Long.toHexString(Double.doubleToRawLongBits(x))
                + ":" + Long.toHexString(Double.doubleToRawLongBits(y))
                + ":" + Long.toHexString(Double.doubleToRawLongBits(z))
                + ":" + Integer.toHexString(Float.floatToRawIntBits(volume))
                + ":" + Integer.toHexString(Float.floatToRawIntBits(pitch)));
        }
    }

    private static long randomSeed48(Random random) {
        try {
            Field field=Random.class.getDeclaredField("seed");
            field.setAccessible(true);
            return ((AtomicLong)field.get(random)).get();
        } catch (ReflectiveOperationException exception) {
            throw new RuntimeException(exception);
        }
    }

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(new UUID(71L, 73L), "food-test"));
            setPosition(0.5D, 70.0D, 0.5D);
            getRNG().setSeed(654321L);
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
    }

    private static void run(int itemId, int meta, long worldSeed) {
        MemoryWorld world = new MemoryWorld(worldSeed);
        TestPlayer player = new TestPlayer(world);
        player.getFoodStats().setFoodLevel(1);
        player.getFoodStats().setFoodSaturationLevel(0.0F);
        Item item = Item.getItemById(itemId);
        ItemStack result = item.onItemUseFinish(
            new ItemStack(item, 1, meta), world, player);
        StringBuilder effects = new StringBuilder();
        for (PotionEffect effect : player.getActivePotionEffects()) {
            if (effects.length() != 0) effects.append(',');
            effects.append(Potion.getIdFromPotion(effect.getPotion())).append(':')
                .append(effect.getAmplifier()).append(':')
                .append(effect.getDuration());
        }
        if (effects.length() == 0) effects.append('-');
        System.out.printf("F %d %d %d %d %08x %d %d %s %016x %016x %d %012x %d",
            itemId, meta, worldSeed, player.getFoodStats().getFoodLevel(),
            Float.floatToRawIntBits(
                player.getFoodStats().getSaturationLevel()),
            Item.getIdFromItem(result.getItem()), result.getCount(), effects,
            Double.doubleToRawLongBits(player.posX),
            Double.doubleToRawLongBits(player.posZ),
            player.getCooldownTracker().hasCooldown(item) ? 1 : 0,
            randomSeed48(player.getRNG()), world.particleCount);
        for(double value:world.particleLast)
            System.out.printf(" %016x",Double.doubleToRawLongBits(value));
        System.out.printf(" %d ",world.sounds.size());
        if(world.sounds.isEmpty())System.out.print('-');
        else for(int index=0;index<world.sounds.size();++index) {
            if(index>0)System.out.print('/');
            System.out.print(world.sounds.get(index));
        }
        System.out.println();
    }

    public static void main(String[] args) {
        Bootstrap.register();
        int[][] cases = {
            {260,0,123456},{282,0,123456},{297,0,123456},
            {319,0,123456},{320,0,123456},{322,0,123456},{322,1,123456},
            {349,0,123456},{349,1,123456},{349,2,123456},{349,3,123456},
            {350,0,123456},{350,1,123456},{357,0,123456},{360,0,123456},
            {363,0,123456},{364,0,123456},{365,0,123456},{366,0,123456},
            {367,0,123456},{375,0,123456},{391,0,123456},{392,0,123456},
            {393,0,123456},{394,0,123456},{396,0,123456},{400,0,123456},
            {411,0,123456},{412,0,123456},{413,0,123456},{423,0,123456},
            {424,0,123456},{432,0,123456},{434,0,123456},{436,0,123456},
            {365,0,1},{367,0,1},{394,0,1}
        };
        for (int[] value : cases) run(value[0], value[1], value[2]);
    }
}
