package net.minecraft.client.renderer.tileentity;

import cpw.mods.fml.common.FMLLog;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Calendar;
import net.minecraft.block.Block;
import net.minecraft.block.BlockChest;
import net.minecraft.client.model.ModelChest;
import net.minecraft.client.model.ModelLargeChest;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityChest;
import net.minecraft.util.ResourceLocation;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;

@SideOnly(Side.CLIENT)
public class TileEntityChestRenderer extends TileEntitySpecialRenderer
{
    private static final ResourceLocation field_147507_b = new ResourceLocation("textures/entity/chest/trapped_double.png");
    private static final ResourceLocation field_147508_c = new ResourceLocation("textures/entity/chest/christmas_double.png");
    private static final ResourceLocation field_147505_d = new ResourceLocation("textures/entity/chest/normal_double.png");
    private static final ResourceLocation field_147506_e = new ResourceLocation("textures/entity/chest/trapped.png");
    private static final ResourceLocation field_147503_f = new ResourceLocation("textures/entity/chest/christmas.png");
    private static final ResourceLocation field_147504_g = new ResourceLocation("textures/entity/chest/normal.png");
    private ModelChest field_147510_h = new ModelChest();
    private ModelChest field_147511_i = new ModelLargeChest();
    private boolean field_147509_j;
    private static final String __OBFID = "CL_00000965";

    public TileEntityChestRenderer()
    {
        Calendar calendar = Calendar.getInstance();

        if (calendar.get(2) + 1 == 12 && calendar.get(5) >= 24 && calendar.get(5) <= 26)
        {
            this.field_147509_j = true;
        }
    }

    public void renderTileEntityAt(TileEntityChest p_147500_1_, double p_147500_2_, double p_147500_4_, double p_147500_6_, float p_147500_8_)
    {
        int i;

        if (!p_147500_1_.hasWorldObj())
        {
            i = 0;
        }
        else
        {
            Block block = p_147500_1_.getBlockType();
            i = p_147500_1_.getBlockMetadata();

            if (block instanceof BlockChest && i == 0)
            {
                try
                {
                ((BlockChest)block).func_149954_e(p_147500_1_.getWorldObj(), p_147500_1_.xCoord, p_147500_1_.yCoord, p_147500_1_.zCoord);
                }
                catch (ClassCastException e)
                {
                    FMLLog.severe("Attempted to render a chest at %d,  %d, %d that was not a chest", p_147500_1_.xCoord, p_147500_1_.yCoord, p_147500_1_.zCoord);
                }
                i = p_147500_1_.getBlockMetadata();
            }

            p_147500_1_.checkForAdjacentChests();
        }

        if (p_147500_1_.adjacentChestZNeg == null && p_147500_1_.adjacentChestXNeg == null)
        {
            ModelChest modelchest;

            if (p_147500_1_.adjacentChestXPos == null && p_147500_1_.adjacentChestZPos == null)
            {
                modelchest = this.field_147510_h;

                if (p_147500_1_.func_145980_j() == 1)
                {
                    this.bindTexture(field_147506_e);
                }
                else if (this.field_147509_j)
                {
                    this.bindTexture(field_147503_f);
                }
                else
                {
                    this.bindTexture(field_147504_g);
                }
            }
            else
            {
                modelchest = this.field_147511_i;

                if (p_147500_1_.func_145980_j() == 1)
                {
                    this.bindTexture(field_147507_b);
                }
                else if (this.field_147509_j)
                {
                    this.bindTexture(field_147508_c);
                }
                else
                {
                    this.bindTexture(field_147505_d);
                }
            }

            GL11.glPushMatrix();
            GL11.glEnable(GL12.GL_RESCALE_NORMAL);
            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
            GL11.glTranslatef((float)p_147500_2_, (float)p_147500_4_ + 1.0F, (float)p_147500_6_ + 1.0F);
            GL11.glScalef(1.0F, -1.0F, -1.0F);
            GL11.glTranslatef(0.5F, 0.5F, 0.5F);
            short short1 = 0;

            if (i == 2)
            {
                short1 = 180;
            }

            if (i == 3)
            {
                short1 = 0;
            }

            if (i == 4)
            {
                short1 = 90;
            }

            if (i == 5)
            {
                short1 = -90;
            }

            if (i == 2 && p_147500_1_.adjacentChestXPos != null)
            {
                GL11.glTranslatef(1.0F, 0.0F, 0.0F);
            }

            if (i == 5 && p_147500_1_.adjacentChestZPos != null)
            {
                GL11.glTranslatef(0.0F, 0.0F, -1.0F);
            }

            GL11.glRotatef((float)short1, 0.0F, 1.0F, 0.0F);
            GL11.glTranslatef(-0.5F, -0.5F, -0.5F);
            float f1 = p_147500_1_.prevLidAngle + (p_147500_1_.lidAngle - p_147500_1_.prevLidAngle) * p_147500_8_;
            float f2;

            if (p_147500_1_.adjacentChestZNeg != null)
            {
                f2 = p_147500_1_.adjacentChestZNeg.prevLidAngle + (p_147500_1_.adjacentChestZNeg.lidAngle - p_147500_1_.adjacentChestZNeg.prevLidAngle) * p_147500_8_;

                if (f2 > f1)
                {
                    f1 = f2;
                }
            }

            if (p_147500_1_.adjacentChestXNeg != null)
            {
                f2 = p_147500_1_.adjacentChestXNeg.prevLidAngle + (p_147500_1_.adjacentChestXNeg.lidAngle - p_147500_1_.adjacentChestXNeg.prevLidAngle) * p_147500_8_;

                if (f2 > f1)
                {
                    f1 = f2;
                }
            }

            f1 = 1.0F - f1;
            f1 = 1.0F - f1 * f1 * f1;
            modelchest.chestLid.rotateAngleX = -(f1 * (float)Math.PI / 2.0F);
            modelchest.renderAll();
            GL11.glDisable(GL12.GL_RESCALE_NORMAL);
            GL11.glPopMatrix();
            GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        }
    }

    public void renderTileEntityAt(TileEntity p_147500_1_, double p_147500_2_, double p_147500_4_, double p_147500_6_, float p_147500_8_)
    {
        this.renderTileEntityAt((TileEntityChest)p_147500_1_, p_147500_2_, p_147500_4_, p_147500_6_, p_147500_8_);
    }
}