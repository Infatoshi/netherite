package qrl;

import net.minecraft.block.Block;
import net.minecraft.block.state.IBlockState;
import net.minecraft.init.Bootstrap;

/** Complete raw block-state MapColor/material census for ItemMap.onUpdate. */
public final class MapColorGolden {
    public static void main(String[] args) {
        Bootstrap.register();
        for (Block block : Block.REGISTRY) {
            int id = Block.getIdFromBlock(block);
            for (int meta = 0; meta < 16; ++meta) {
                try {
                    IBlockState state = block.getStateFromMeta(meta);
                    System.out.printf("M %d %d 1 %d %d%n", id, meta,
                        state.getMapColor().colorIndex,
                        state.getMaterial().isLiquid() ? 1 : 0);
                } catch (IllegalArgumentException invalid) {
                    System.out.printf("M %d %d 0 -1 0%n", id, meta);
                }
            }
        }
    }
}
