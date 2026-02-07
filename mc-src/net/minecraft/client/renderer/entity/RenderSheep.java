package net.minecraft.client.renderer.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.model.ModelBase;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.passive.EntitySheep;
import net.minecraft.util.ResourceLocation;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class RenderSheep extends RenderLiving
{
    private static final ResourceLocation sheepTextures = new ResourceLocation("textures/entity/sheep/sheep_fur.png");
    private static final ResourceLocation shearedSheepTextures = new ResourceLocation("textures/entity/sheep/sheep.png");
    private static final String __OBFID = "CL_00001021";

    public RenderSheep(ModelBase p_i1266_1_, ModelBase p_i1266_2_, float p_i1266_3_)
    {
        super(p_i1266_1_, p_i1266_3_);
        this.setRenderPassModel(p_i1266_2_);
    }

    /**
     * Queries whether should render the specified pass or not.
     */
    protected int shouldRenderPass(EntitySheep p_77032_1_, int p_77032_2_, float p_77032_3_)
    {
        if (p_77032_2_ == 0 && !p_77032_1_.getSheared())
        {
            this.bindTexture(sheepTextures);

            if (p_77032_1_.hasCustomNameTag() && "jeb_".equals(p_77032_1_.getCustomNameTag()))
            {
                boolean flag = true;
                int k = p_77032_1_.ticksExisted / 25 + p_77032_1_.getEntityId();
                int l = k % EntitySheep.fleeceColorTable.length;
                int i1 = (k + 1) % EntitySheep.fleeceColorTable.length;
                float f1 = ((float)(p_77032_1_.ticksExisted % 25) + p_77032_3_) / 25.0F;
                GL11.glColor3f(EntitySheep.fleeceColorTable[l][0] * (1.0F - f1) + EntitySheep.fleeceColorTable[i1][0] * f1, EntitySheep.fleeceColorTable[l][1] * (1.0F - f1) + EntitySheep.fleeceColorTable[i1][1] * f1, EntitySheep.fleeceColorTable[l][2] * (1.0F - f1) + EntitySheep.fleeceColorTable[i1][2] * f1);
            }
            else
            {
                int j = p_77032_1_.getFleeceColor();
                GL11.glColor3f(EntitySheep.fleeceColorTable[j][0], EntitySheep.fleeceColorTable[j][1], EntitySheep.fleeceColorTable[j][2]);
            }

            return 1;
        }
        else
        {
            return -1;
        }
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(EntitySheep p_110775_1_)
    {
        return shearedSheepTextures;
    }

    /**
     * Queries whether should render the specified pass or not.
     */
    protected int shouldRenderPass(EntityLivingBase p_77032_1_, int p_77032_2_, float p_77032_3_)
    {
        return this.shouldRenderPass((EntitySheep)p_77032_1_, p_77032_2_, p_77032_3_);
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(Entity p_110775_1_)
    {
        return this.getEntityTexture((EntitySheep)p_110775_1_);
    }
}