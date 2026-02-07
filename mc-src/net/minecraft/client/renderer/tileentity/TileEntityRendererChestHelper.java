package net.minecraft.client.renderer.tileentity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.init.Blocks;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.tileentity.TileEntityEnderChest;

@SideOnly(Side.CLIENT)
public class TileEntityRendererChestHelper
{
    public static TileEntityRendererChestHelper instance = new TileEntityRendererChestHelper();
    private TileEntityChest field_147717_b = new TileEntityChest(0);
    private TileEntityChest field_147718_c = new TileEntityChest(1);
    private TileEntityEnderChest field_147716_d = new TileEntityEnderChest();
    private static final String __OBFID = "CL_00000946";

    /**
     * Renders a chest at 0,0,0 - used for item rendering
     */
    public void renderChest(Block p_147715_1_, int p_147715_2_, float p_147715_3_)
    {
        if (p_147715_1_ == Blocks.ender_chest)
        {
            TileEntityRendererDispatcher.instance.renderTileEntityAt(this.field_147716_d, 0.0D, 0.0D, 0.0D, 0.0F);
        }
        else if (p_147715_1_ == Blocks.trapped_chest)
        {
            TileEntityRendererDispatcher.instance.renderTileEntityAt(this.field_147718_c, 0.0D, 0.0D, 0.0D, 0.0F);
        }
        else
        {
            TileEntityRendererDispatcher.instance.renderTileEntityAt(this.field_147717_b, 0.0D, 0.0D, 0.0D, 0.0F);
        }
    }
}