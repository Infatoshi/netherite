package net.minecraft.client.renderer.entity;

import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.model.ModelBase;
import net.minecraft.client.renderer.texture.LayeredTexture;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.util.ResourceLocation;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class RenderHorse extends RenderLiving
{
    private static final Map field_110852_a = Maps.newHashMap();
    private static final ResourceLocation whiteHorseTextures = new ResourceLocation("textures/entity/horse/horse_white.png");
    private static final ResourceLocation muleTextures = new ResourceLocation("textures/entity/horse/mule.png");
    private static final ResourceLocation donkeyTextures = new ResourceLocation("textures/entity/horse/donkey.png");
    private static final ResourceLocation zombieHorseTextures = new ResourceLocation("textures/entity/horse/horse_zombie.png");
    private static final ResourceLocation skeletonHorseTextures = new ResourceLocation("textures/entity/horse/horse_skeleton.png");
    private static final String __OBFID = "CL_00001000";

    public RenderHorse(ModelBase p_i1256_1_, float p_i1256_2_)
    {
        super(p_i1256_1_, p_i1256_2_);
    }

    /**
     * Allows the render to do any OpenGL state modifications necessary before the model is rendered. Args:
     * entityLiving, partialTickTime
     */
    protected void preRenderCallback(EntityHorse p_77041_1_, float p_77041_2_)
    {
        float f1 = 1.0F;
        int i = p_77041_1_.getHorseType();

        if (i == 1)
        {
            f1 *= 0.87F;
        }
        else if (i == 2)
        {
            f1 *= 0.92F;
        }

        GL11.glScalef(f1, f1, f1);
        super.preRenderCallback(p_77041_1_, p_77041_2_);
    }

    /**
     * Renders the model in RenderLiving
     */
    protected void renderModel(EntityHorse p_77036_1_, float p_77036_2_, float p_77036_3_, float p_77036_4_, float p_77036_5_, float p_77036_6_, float p_77036_7_)
    {
        if (p_77036_1_.isInvisible())
        {
            this.mainModel.setRotationAngles(p_77036_2_, p_77036_3_, p_77036_4_, p_77036_5_, p_77036_6_, p_77036_7_, p_77036_1_);
        }
        else
        {
            this.bindEntityTexture(p_77036_1_);
            this.mainModel.render(p_77036_1_, p_77036_2_, p_77036_3_, p_77036_4_, p_77036_5_, p_77036_6_, p_77036_7_);
        }
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(EntityHorse p_110775_1_)
    {
        if (!p_110775_1_.func_110239_cn())
        {
            switch (p_110775_1_.getHorseType())
            {
                case 0:
                default:
                    return whiteHorseTextures;
                case 1:
                    return donkeyTextures;
                case 2:
                    return muleTextures;
                case 3:
                    return zombieHorseTextures;
                case 4:
                    return skeletonHorseTextures;
            }
        }
        else
        {
            return this.func_110848_b(p_110775_1_);
        }
    }

    private ResourceLocation func_110848_b(EntityHorse p_110848_1_)
    {
        String s = p_110848_1_.getHorseTexture();
        ResourceLocation resourcelocation = (ResourceLocation)field_110852_a.get(s);

        if (resourcelocation == null)
        {
            resourcelocation = new ResourceLocation(s);
            Minecraft.getMinecraft().getTextureManager().loadTexture(resourcelocation, new LayeredTexture(p_110848_1_.getVariantTexturePaths()));
            field_110852_a.put(s, resourcelocation);
        }

        return resourcelocation;
    }

    /**
     * Allows the render to do any OpenGL state modifications necessary before the model is rendered. Args:
     * entityLiving, partialTickTime
     */
    protected void preRenderCallback(EntityLivingBase p_77041_1_, float p_77041_2_)
    {
        this.preRenderCallback((EntityHorse)p_77041_1_, p_77041_2_);
    }

    /**
     * Renders the model in RenderLiving
     */
    protected void renderModel(EntityLivingBase p_77036_1_, float p_77036_2_, float p_77036_3_, float p_77036_4_, float p_77036_5_, float p_77036_6_, float p_77036_7_)
    {
        this.renderModel((EntityHorse)p_77036_1_, p_77036_2_, p_77036_3_, p_77036_4_, p_77036_5_, p_77036_6_, p_77036_7_);
    }

    /**
     * Returns the location of an entity's texture. Doesn't seem to be called unless you call Render.bindEntityTexture.
     */
    protected ResourceLocation getEntityTexture(Entity p_110775_1_)
    {
        return this.getEntityTexture((EntityHorse)p_110775_1_);
    }
}