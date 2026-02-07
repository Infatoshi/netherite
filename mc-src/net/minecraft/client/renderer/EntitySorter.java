package net.minecraft.client.renderer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Comparator;
import net.minecraft.entity.Entity;

@SideOnly(Side.CLIENT)
public class EntitySorter implements Comparator
{
    /** Entity position X */
    private double entityPosX;
    /** Entity position Y */
    private double entityPosY;
    /** Entity position Z */
    private double entityPosZ;
    private static final String __OBFID = "CL_00000944";

    public EntitySorter(Entity p_i1242_1_)
    {
        this.entityPosX = -p_i1242_1_.posX;
        this.entityPosY = -p_i1242_1_.posY;
        this.entityPosZ = -p_i1242_1_.posZ;
    }

    public int compare(WorldRenderer p_compare_1_, WorldRenderer p_compare_2_)
    {
        double d0 = (double)p_compare_1_.posXPlus + this.entityPosX;
        double d1 = (double)p_compare_1_.posYPlus + this.entityPosY;
        double d2 = (double)p_compare_1_.posZPlus + this.entityPosZ;
        double d3 = (double)p_compare_2_.posXPlus + this.entityPosX;
        double d4 = (double)p_compare_2_.posYPlus + this.entityPosY;
        double d5 = (double)p_compare_2_.posZPlus + this.entityPosZ;
        return (int)((d0 * d0 + d1 * d1 + d2 * d2 - (d3 * d3 + d4 * d4 + d5 * d5)) * 1024.0D);
    }

    public int compare(Object p_compare_1_, Object p_compare_2_)
    {
        return this.compare((WorldRenderer)p_compare_1_, (WorldRenderer)p_compare_2_);
    }
}