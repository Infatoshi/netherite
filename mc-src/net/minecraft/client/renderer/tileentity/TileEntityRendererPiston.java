package net.minecraft.client.renderer.tileentity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.block.Block;
import net.minecraft.block.BlockPistonBase;
import net.minecraft.block.material.Material;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.RenderBlocks;
import net.minecraft.client.renderer.RenderHelper;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.init.Blocks;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityPiston;
import net.minecraft.world.World;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class TileEntityRendererPiston extends TileEntitySpecialRenderer
{
    private RenderBlocks field_147516_b;
    private static final String __OBFID = "CL_00000969";

    public void renderTileEntityAt(TileEntityPiston p_147500_1_, double p_147500_2_, double p_147500_4_, double p_147500_6_, float p_147500_8_)
    {
        Block block = p_147500_1_.getStoredBlockID();

        if (block.getMaterial() != Material.air && p_147500_1_.func_145860_a(p_147500_8_) < 1.0F)
        {
            Tessellator tessellator = Tessellator.instance;
            this.bindTexture(TextureMap.locationBlocksTexture);
            RenderHelper.disableStandardItemLighting();
            GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);
            GL11.glEnable(GL11.GL_BLEND);
            GL11.glDisable(GL11.GL_CULL_FACE);

            if (Minecraft.isAmbientOcclusionEnabled())
            {
                GL11.glShadeModel(GL11.GL_SMOOTH);
            }
            else
            {
                GL11.glShadeModel(GL11.GL_FLAT);
            }

            tessellator.startDrawingQuads();
            tessellator.setTranslation((double)((float)p_147500_2_ - (float)p_147500_1_.xCoord + p_147500_1_.func_145865_b(p_147500_8_)), (double)((float)p_147500_4_ - (float)p_147500_1_.yCoord + p_147500_1_.func_145862_c(p_147500_8_)), (double)((float)p_147500_6_ - (float)p_147500_1_.zCoord + p_147500_1_.func_145859_d(p_147500_8_)));
            tessellator.setColorOpaque_F(1.0F, 1.0F, 1.0F);

            if (block == Blocks.piston_head && p_147500_1_.func_145860_a(p_147500_8_) < 0.5F)
            {
                this.field_147516_b.renderPistonExtensionAllFaces(block, p_147500_1_.xCoord, p_147500_1_.yCoord, p_147500_1_.zCoord, false);
            }
            else if (p_147500_1_.func_145867_d() && !p_147500_1_.isExtending())
            {
                Blocks.piston_head.func_150086_a(((BlockPistonBase)block).getPistonExtensionTexture());
                this.field_147516_b.renderPistonExtensionAllFaces(Blocks.piston_head, p_147500_1_.xCoord, p_147500_1_.yCoord, p_147500_1_.zCoord, p_147500_1_.func_145860_a(p_147500_8_) < 0.5F);
                Blocks.piston_head.func_150087_e();
                tessellator.setTranslation((double)((float)p_147500_2_ - (float)p_147500_1_.xCoord), (double)((float)p_147500_4_ - (float)p_147500_1_.yCoord), (double)((float)p_147500_6_ - (float)p_147500_1_.zCoord));
                this.field_147516_b.renderPistonBaseAllFaces(block, p_147500_1_.xCoord, p_147500_1_.yCoord, p_147500_1_.zCoord);
            }
            else
            {
                this.field_147516_b.renderBlockAllFaces(block, p_147500_1_.xCoord, p_147500_1_.yCoord, p_147500_1_.zCoord);
            }

            tessellator.setTranslation(0.0D, 0.0D, 0.0D);
            tessellator.draw();
            RenderHelper.enableStandardItemLighting();
        }
    }

    public void func_147496_a(World p_147496_1_)
    {
        this.field_147516_b = new RenderBlocks(p_147496_1_);
    }

    public void renderTileEntityAt(TileEntity p_147500_1_, double p_147500_2_, double p_147500_4_, double p_147500_6_, float p_147500_8_)
    {
        this.renderTileEntityAt((TileEntityPiston)p_147500_1_, p_147500_2_, p_147500_4_, p_147500_6_, p_147500_8_);
    }
}