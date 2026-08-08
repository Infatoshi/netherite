package qrl;

import java.util.Random;

import net.minecraft.block.Block;
import net.minecraft.block.BlockStairs;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;

/** Executes every intentional no-op in the strict block-callback census. */
public final class BlockCallbackNoopGolden {
    private BlockCallbackNoopGolden() { }

    private static void randomTick(String owner, Block block) {
        block.randomTick(null, null, null, (Random)null);
        System.out.printf("N %s randomTick %d%n",
            owner, Block.getIdFromBlock(block));
    }

    private static void drop(String owner, Block block) {
        block.dropBlockAsItemWithChance(null, null, null, 1.0F, 0);
        System.out.printf("N %s dropBlockAsItemWithChance %d%n",
            owner, Block.getIdFromBlock(block));
    }

    public static void main(String[] args) {
        Bootstrap.register();

        randomTick("BlockBasePressurePlate", Blocks.STONE_PRESSURE_PLATE);
        randomTick("BlockButton", Blocks.STONE_BUTTON);
        randomTick("BlockRailDetector", Blocks.DETECTOR_RAIL);
        randomTick("BlockRedstoneDiode", Blocks.UNPOWERED_REPEATER);
        randomTick("BlockRedstoneTorch", Blocks.REDSTONE_TORCH);
        randomTick("BlockTripWire", Blocks.TRIPWIRE);
        randomTick("BlockTripWireHook", Blocks.TRIPWIRE_HOOK);

        drop("BlockAir", Blocks.AIR);
        drop("BlockBarrier", Blocks.BARRIER);
        drop("BlockShulkerBox", Blocks.WHITE_SHULKER_BOX);
        drop("BlockStructureVoid", Blocks.STRUCTURE_VOID);

        Blocks.OBSERVER.neighborChanged(null, null, null, null, null);
        System.out.printf("N BlockObserver neighborChanged %d%n",
            Block.getIdFromBlock(Blocks.OBSERVER));

        for (Block block : Block.REGISTRY) {
            if (!(block instanceof BlockStairs)) continue;
            int id = Block.getIdFromBlock(block);
            block.breakBlock(null, null, null);
            System.out.printf("E BlockStairs breakBlock %d%n", id);
            block.updateTick(null, null, null, null);
            System.out.printf("E BlockStairs updateTick %d%n", id);
            if (block.onBlockActivated(
                    null, null, null, null, null, null,
                    0.0F, 0.0F, 0.0F))
                throw new AssertionError(
                    "stair model activated for block " + id);
            System.out.printf("E BlockStairs onBlockActivated %d%n", id);
        }
    }
}
