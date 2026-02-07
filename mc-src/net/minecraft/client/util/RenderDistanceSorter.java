package net.minecraft.client.util;

import com.google.common.collect.ComparisonChain;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Comparator;
import net.minecraft.client.renderer.RenderList;

@SideOnly(Side.CLIENT)
public class RenderDistanceSorter implements Comparator
{
    int field_152632_a;
    int field_152633_b;
    private static final String __OBFID = "CL_00000945";

    public RenderDistanceSorter(int p_i1051_1_, int p_i1051_2_)
    {
        this.field_152632_a = p_i1051_1_;
        this.field_152633_b = p_i1051_2_;
    }

    public int compare(RenderList p_compare_1_, RenderList p_compare_2_)
    {
        int i = p_compare_1_.renderChunkX - this.field_152632_a;
        int j = p_compare_1_.renderChunkZ - this.field_152633_b;
        int k = p_compare_2_.renderChunkX - this.field_152632_a;
        int l = p_compare_2_.renderChunkZ - this.field_152633_b;
        int i1 = i * i + j * j;
        int j1 = k * k + l * l;
        return ComparisonChain.start().compare(j1, i1).result();
    }

    public int compare(Object p_compare_1_, Object p_compare_2_)
    {
        return this.compare((RenderList)p_compare_1_, (RenderList)p_compare_2_);
    }
}