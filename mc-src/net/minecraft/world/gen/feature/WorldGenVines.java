package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.init.Blocks;
import net.minecraft.util.Direction;
import net.minecraft.util.Facing;
import net.minecraft.world.World;

public class WorldGenVines extends WorldGenerator
{
    private static final String __OBFID = "CL_00000439";

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        int l = p_76484_3_;

        for (int i1 = p_76484_5_; p_76484_4_ < 128; ++p_76484_4_)
        {
            if (p_76484_1_.isAirBlock(p_76484_3_, p_76484_4_, p_76484_5_))
            {
                for (int j1 = 2; j1 <= 5; ++j1)
                {
                    if (Blocks.vine.canPlaceBlockOnSide(p_76484_1_, p_76484_3_, p_76484_4_, p_76484_5_, j1))
                    {
                        p_76484_1_.setBlock(p_76484_3_, p_76484_4_, p_76484_5_, Blocks.vine, 1 << Direction.facingToDirection[Facing.oppositeSide[j1]], 2);
                        break;
                    }
                }
            }
            else
            {
                p_76484_3_ = l + p_76484_2_.nextInt(4) - p_76484_2_.nextInt(4);
                p_76484_5_ = i1 + p_76484_2_.nextInt(4) - p_76484_2_.nextInt(4);
            }
        }

        return true;
    }
}