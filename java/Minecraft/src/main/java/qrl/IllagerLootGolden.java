package qrl;

import java.io.File;
import java.lang.reflect.Field;
import java.util.List;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.init.Bootstrap;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.world.storage.loot.LootContext;
import net.minecraft.world.storage.loot.LootTable;
import net.minecraft.world.storage.loot.LootTableList;
import net.minecraft.world.storage.loot.LootTableManager;
import sun.misc.Unsafe;

/** Exact built-in 1.11.2 illager loot generation from a raw entity RNG cursor. */
public final class IllagerLootGolden {
    private static final long MASK = (1L << 48) - 1L;
    private static final long[] SEEDS = {0L, 2L, 95L, 402L, MASK};

    private static final class FixedContext extends LootContext {
        private final int looting;
        private final EntityPlayer killer;

        FixedContext(LootTableManager manager, EntityPlayer player, int level) {
            super(0.0F, null, manager, null, player, null);
            looting = level;
            killer = player;
        }

        @Override
        public int getLootingModifier() {
            return looting;
        }

        @Override
        public Entity getKiller() {
            return killer;
        }
    }

    private static EntityPlayer fakePlayer() throws Exception {
        Field field = Unsafe.class.getDeclaredField("theUnsafe");
        field.setAccessible(true);
        Unsafe unsafe = (Unsafe)field.get(null);
        return (EntityPlayer)unsafe.allocateInstance(EntityPlayerMP.class);
    }

    private static AtomicLong cursor(Random random) throws Exception {
        Field field = Random.class.getDeclaredField("seed");
        field.setAccessible(true);
        return (AtomicLong)field.get(random);
    }

    public static void main(String[] args) throws Exception {
        Bootstrap.register();
        LootTableManager manager = new LootTableManager(
            new File("run/empty-loot-root"));
        EntityPlayer player = fakePlayer();
        String[] names = {"vindicator", "evoker"};
        LootTable[] tables = {
            manager.getLootTableFromLocation(
                LootTableList.ENTITIES_VINDICATION_ILLAGER),
            manager.getLootTableFromLocation(
                LootTableList.ENTITIES_EVOCATION_ILLAGER)
        };
        for (int type = 0; type < names.length; ++type) {
            for (long seed : SEEDS) {
                for (int fixture = 0; fixture < 3; ++fixture) {
                    int looting = fixture == 1 ? 3 : 0;
                    boolean killed = fixture != 2;
                    Random random = new Random(0L);
                    AtomicLong raw = cursor(random);
                    raw.set(seed);
                    FixedContext context = new FixedContext(
                        manager, killed ? player : null, looting);
                    List<ItemStack> stacks = tables[type]
                        .generateLootForPools(random, context);
                    System.out.printf("%s %d %d %d %d",
                        names[type], seed, looting, killed ? 1 : 0,
                        stacks.size());
                    for (ItemStack stack : stacks)
                        System.out.printf(" %d:%d:%d",
                            Item.getIdFromItem(stack.getItem()),
                            stack.getCount(), stack.getMetadata());
                    System.out.printf(" %d%n", raw.get());
                }
            }
        }
    }
}
