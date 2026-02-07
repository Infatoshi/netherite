package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.init.Blocks;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.util.WeightedRandomChestContent;
import net.minecraft.world.World;

public class WorldGeneratorBonusChest extends WorldGenerator
{
    /** Instance of WeightedRandomChestContent what will randomly generate items into the Bonus Chest. */
    private final WeightedRandomChestContent[] theBonusChestGenerator;
    /** Value of this int will determine how much items gonna generate in Bonus Chest. */
    private final int itemsToGenerateInBonusChest;
    private static final String __OBFID = "CL_00000403";

    public WorldGeneratorBonusChest(WeightedRandomChestContent[] p_i2010_1_, int p_i2010_2_)
    {
        this.theBonusChestGenerator = p_i2010_1_;
        this.itemsToGenerateInBonusChest = p_i2010_2_;
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        Block block;

        do
        {
            block = p_76484_1_.getBlock(p_76484_3_, p_76484_4_, p_76484_5_);
            if (!block.isAir(p_76484_1_, p_76484_3_, p_76484_4_, p_76484_5_) && !block.isLeaves(p_76484_1_, p_76484_3_, p_76484_4_, p_76484_5_)) break;
            p_76484_4_--;
        } while (p_76484_4_ > 1);

        if (p_76484_4_ < 1)
        {
            return false;
        }
        else
        {
            ++p_76484_4_;

            for (int l = 0; l < 4; ++l)
            {
                int i1 = p_76484_3_ + p_76484_2_.nextInt(4) - p_76484_2_.nextInt(4);
                int j1 = p_76484_4_ + p_76484_2_.nextInt(3) - p_76484_2_.nextInt(3);
                int k1 = p_76484_5_ + p_76484_2_.nextInt(4) - p_76484_2_.nextInt(4);

                if (p_76484_1_.isAirBlock(i1, j1, k1) && World.doesBlockHaveSolidTopSurface(p_76484_1_, i1, j1 - 1, k1))
                {
                    p_76484_1_.setBlock(i1, j1, k1, Blocks.chest, 0, 2);
                    TileEntityChest tileentitychest = (TileEntityChest)p_76484_1_.getTileEntity(i1, j1, k1);

                    if (tileentitychest != null && tileentitychest != null)
                    {
                        WeightedRandomChestContent.generateChestContents(p_76484_2_, this.theBonusChestGenerator, tileentitychest, this.itemsToGenerateInBonusChest);
                    }

                    if (p_76484_1_.isAirBlock(i1 - 1, j1, k1) && World.doesBlockHaveSolidTopSurface(p_76484_1_, i1 - 1, j1 - 1, k1))
                    {
                        p_76484_1_.setBlock(i1 - 1, j1, k1, Blocks.torch, 0, 2);
                    }

                    if (p_76484_1_.isAirBlock(i1 + 1, j1, k1) && World.doesBlockHaveSolidTopSurface(p_76484_1_, i1 - 1, j1 - 1, k1))
                    {
                        p_76484_1_.setBlock(i1 + 1, j1, k1, Blocks.torch, 0, 2);
                    }

                    if (p_76484_1_.isAirBlock(i1, j1, k1 - 1) && World.doesBlockHaveSolidTopSurface(p_76484_1_, i1 - 1, j1 - 1, k1))
                    {
                        p_76484_1_.setBlock(i1, j1, k1 - 1, Blocks.torch, 0, 2);
                    }

                    if (p_76484_1_.isAirBlock(i1, j1, k1 + 1) && World.doesBlockHaveSolidTopSurface(p_76484_1_, i1 - 1, j1 - 1, k1))
                    {
                        p_76484_1_.setBlock(i1, j1, k1 + 1, Blocks.torch, 0, 2);
                    }

                    return true;
                }
            }

            return false;
        }
    }
}