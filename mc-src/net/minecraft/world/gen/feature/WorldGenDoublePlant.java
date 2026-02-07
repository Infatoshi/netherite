package net.minecraft.world.gen.feature;

import java.util.Random;
import net.minecraft.init.Blocks;
import net.minecraft.world.World;

public class WorldGenDoublePlant extends WorldGenerator
{
    private int field_150549_a;
    private static final String __OBFID = "CL_00000408";

    public void func_150548_a(int p_150548_1_)
    {
        this.field_150549_a = p_150548_1_;
    }

    public boolean generate(World p_76484_1_, Random p_76484_2_, int p_76484_3_, int p_76484_4_, int p_76484_5_)
    {
        boolean flag = false;

        for (int l = 0; l < 64; ++l)
        {
            int i1 = p_76484_3_ + p_76484_2_.nextInt(8) - p_76484_2_.nextInt(8);
            int j1 = p_76484_4_ + p_76484_2_.nextInt(4) - p_76484_2_.nextInt(4);
            int k1 = p_76484_5_ + p_76484_2_.nextInt(8) - p_76484_2_.nextInt(8);

            if (p_76484_1_.isAirBlock(i1, j1, k1) && (!p_76484_1_.provider.hasNoSky || j1 < 254) && Blocks.double_plant.canPlaceBlockAt(p_76484_1_, i1, j1, k1))
            {
                Blocks.double_plant.func_149889_c(p_76484_1_, i1, j1, k1, this.field_150549_a, 2);
                flag = true;
            }
        }

        return flag;
    }
}