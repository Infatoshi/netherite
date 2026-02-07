package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.concurrent.Callable;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.client.renderer.ActiveRenderInfo;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.entity.Entity;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.ReportedException;
import net.minecraft.util.ResourceLocation;
import net.minecraft.world.World;

import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class EffectRenderer
{
    private static final ResourceLocation particleTextures = new ResourceLocation("textures/particle/particles.png");
    /** Reference to the World object. */
    protected World worldObj;
    private List[] fxLayers = new List[4];
    private TextureManager renderer;
    /** RNG. */
    private Random rand = new Random();
    private static final String __OBFID = "CL_00000915";

    public EffectRenderer(World p_i1220_1_, TextureManager p_i1220_2_)
    {
        if (p_i1220_1_ != null)
        {
            this.worldObj = p_i1220_1_;
        }

        this.renderer = p_i1220_2_;

        for (int i = 0; i < 4; ++i)
        {
            this.fxLayers[i] = new ArrayList();
        }
    }

    public void addEffect(EntityFX p_78873_1_)
    {
        int i = p_78873_1_.getFXLayer();

        if (this.fxLayers[i].size() >= 4000)
        {
            this.fxLayers[i].remove(0);
        }

        this.fxLayers[i].add(p_78873_1_);
    }

    public void updateEffects()
    {
        for (int k = 0; k < 4; ++k)
        {
            final int i = k;

            for (int j = 0; j < this.fxLayers[i].size(); ++j)
            {
                final EntityFX entityfx = (EntityFX)this.fxLayers[i].get(j);

                try
                {
                    if (entityfx != null)
                    {
                        entityfx.onUpdate();
                    }
                }
                catch (Throwable throwable)
                {
                    CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Ticking Particle");
                    CrashReportCategory crashreportcategory = crashreport.makeCategory("Particle being ticked");
                    crashreportcategory.addCrashSectionCallable("Particle", new Callable()
                    {
                        private static final String __OBFID = "CL_00000916";
                        public String call()
                        {
                            return entityfx.toString();
                        }
                    });
                    crashreportcategory.addCrashSectionCallable("Particle Type", new Callable()
                    {
                        private static final String __OBFID = "CL_00000917";
                        public String call()
                        {
                            return i == 0 ? "MISC_TEXTURE" : (i == 1 ? "TERRAIN_TEXTURE" : (i == 2 ? "ITEM_TEXTURE" : (i == 3 ? "ENTITY_PARTICLE_TEXTURE" : "Unknown - " + i)));
                        }
                    });
                    throw new ReportedException(crashreport);
                }

                if (entityfx == null || entityfx.isDead)
                {
                    this.fxLayers[i].remove(j--);
                }
            }
        }
    }

    /**
     * Renders all current particles. Args player, partialTickTime
     */
    public void renderParticles(Entity p_78874_1_, float p_78874_2_)
    {
        float f1 = ActiveRenderInfo.rotationX;
        float f2 = ActiveRenderInfo.rotationZ;
        float f3 = ActiveRenderInfo.rotationYZ;
        float f4 = ActiveRenderInfo.rotationXY;
        float f5 = ActiveRenderInfo.rotationXZ;
        EntityFX.interpPosX = p_78874_1_.lastTickPosX + (p_78874_1_.posX - p_78874_1_.lastTickPosX) * (double)p_78874_2_;
        EntityFX.interpPosY = p_78874_1_.lastTickPosY + (p_78874_1_.posY - p_78874_1_.lastTickPosY) * (double)p_78874_2_;
        EntityFX.interpPosZ = p_78874_1_.lastTickPosZ + (p_78874_1_.posZ - p_78874_1_.lastTickPosZ) * (double)p_78874_2_;

        for (int k = 0; k < 3; ++k)
        {
            final int i = k;

            if (!this.fxLayers[i].isEmpty())
            {
                switch (i)
                {
                    case 0:
                    default:
                        this.renderer.bindTexture(particleTextures);
                        break;
                    case 1:
                        this.renderer.bindTexture(TextureMap.locationBlocksTexture);
                        break;
                    case 2:
                        this.renderer.bindTexture(TextureMap.locationItemsTexture);
                }

                GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
                GL11.glDepthMask(false);
                GL11.glEnable(GL11.GL_BLEND);
                GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);
                GL11.glAlphaFunc(GL11.GL_GREATER, 0.003921569F);
                Tessellator tessellator = Tessellator.instance;
                tessellator.startDrawingQuads();

                for (int j = 0; j < this.fxLayers[i].size(); ++j)
                {
                    final EntityFX entityfx = (EntityFX)this.fxLayers[i].get(j);
                    if (entityfx == null) continue;
                    tessellator.setBrightness(entityfx.getBrightnessForRender(p_78874_2_));

                    try
                    {
                        entityfx.renderParticle(tessellator, p_78874_2_, f1, f5, f2, f3, f4);
                    }
                    catch (Throwable throwable)
                    {
                        CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Rendering Particle");
                        CrashReportCategory crashreportcategory = crashreport.makeCategory("Particle being rendered");
                        crashreportcategory.addCrashSectionCallable("Particle", new Callable()
                        {
                            private static final String __OBFID = "CL_00000918";
                            public String call()
                            {
                                return entityfx.toString();
                            }
                        });
                        crashreportcategory.addCrashSectionCallable("Particle Type", new Callable()
                        {
                            private static final String __OBFID = "CL_00000919";
                            public String call()
                            {
                                return i == 0 ? "MISC_TEXTURE" : (i == 1 ? "TERRAIN_TEXTURE" : (i == 2 ? "ITEM_TEXTURE" : (i == 3 ? "ENTITY_PARTICLE_TEXTURE" : "Unknown - " + i)));
                            }
                        });
                        throw new ReportedException(crashreport);
                    }
                }

                tessellator.draw();
                GL11.glDisable(GL11.GL_BLEND);
                GL11.glDepthMask(true);
                GL11.glAlphaFunc(GL11.GL_GREATER, 0.1F);
            }
        }
    }

    public void renderLitParticles(Entity p_78872_1_, float p_78872_2_)
    {
        float f1 = 0.017453292F;
        float f2 = MathHelper.cos(p_78872_1_.rotationYaw * 0.017453292F);
        float f3 = MathHelper.sin(p_78872_1_.rotationYaw * 0.017453292F);
        float f4 = -f3 * MathHelper.sin(p_78872_1_.rotationPitch * 0.017453292F);
        float f5 = f2 * MathHelper.sin(p_78872_1_.rotationPitch * 0.017453292F);
        float f6 = MathHelper.cos(p_78872_1_.rotationPitch * 0.017453292F);
        byte b0 = 3;
        List list = this.fxLayers[b0];

        if (!list.isEmpty())
        {
            Tessellator tessellator = Tessellator.instance;

            for (int i = 0; i < list.size(); ++i)
            {
                EntityFX entityfx = (EntityFX)list.get(i);
                if (entityfx == null) continue;
                tessellator.setBrightness(entityfx.getBrightnessForRender(p_78872_2_));
                entityfx.renderParticle(tessellator, p_78872_2_, f2, f6, f3, f4, f5);
            }
        }
    }

    public void clearEffects(World p_78870_1_)
    {
        this.worldObj = p_78870_1_;

        for (int i = 0; i < 4; ++i)
        {
            this.fxLayers[i].clear();
        }
    }

    public void addBlockDestroyEffects(int p_147215_1_, int p_147215_2_, int p_147215_3_, Block p_147215_4_, int p_147215_5_)
    {
        if (!p_147215_4_.isAir(worldObj, p_147215_1_, p_147215_2_, p_147215_3_) && !p_147215_4_.addDestroyEffects(worldObj, p_147215_1_, p_147215_2_, p_147215_3_, p_147215_5_, this))
        {
            byte b0 = 4;

            for (int i1 = 0; i1 < b0; ++i1)
            {
                for (int j1 = 0; j1 < b0; ++j1)
                {
                    for (int k1 = 0; k1 < b0; ++k1)
                    {
                        double d0 = (double)p_147215_1_ + ((double)i1 + 0.5D) / (double)b0;
                        double d1 = (double)p_147215_2_ + ((double)j1 + 0.5D) / (double)b0;
                        double d2 = (double)p_147215_3_ + ((double)k1 + 0.5D) / (double)b0;
                        this.addEffect((new EntityDiggingFX(this.worldObj, d0, d1, d2, d0 - (double)p_147215_1_ - 0.5D, d1 - (double)p_147215_2_ - 0.5D, d2 - (double)p_147215_3_ - 0.5D, p_147215_4_, p_147215_5_)).applyColourMultiplier(p_147215_1_, p_147215_2_, p_147215_3_));
                    }
                }
            }
        }
    }

    /**
     * Adds block hit particles for the specified block. Args: x, y, z, sideHit
     */
    public void addBlockHitEffects(int p_78867_1_, int p_78867_2_, int p_78867_3_, int p_78867_4_)
    {
        Block block = this.worldObj.getBlock(p_78867_1_, p_78867_2_, p_78867_3_);

        if (block.getMaterial() != Material.air)
        {
            float f = 0.1F;
            double d0 = (double)p_78867_1_ + this.rand.nextDouble() * (block.getBlockBoundsMaxX() - block.getBlockBoundsMinX() - (double)(f * 2.0F)) + (double)f + block.getBlockBoundsMinX();
            double d1 = (double)p_78867_2_ + this.rand.nextDouble() * (block.getBlockBoundsMaxY() - block.getBlockBoundsMinY() - (double)(f * 2.0F)) + (double)f + block.getBlockBoundsMinY();
            double d2 = (double)p_78867_3_ + this.rand.nextDouble() * (block.getBlockBoundsMaxZ() - block.getBlockBoundsMinZ() - (double)(f * 2.0F)) + (double)f + block.getBlockBoundsMinZ();

            if (p_78867_4_ == 0)
            {
                d1 = (double)p_78867_2_ + block.getBlockBoundsMinY() - (double)f;
            }

            if (p_78867_4_ == 1)
            {
                d1 = (double)p_78867_2_ + block.getBlockBoundsMaxY() + (double)f;
            }

            if (p_78867_4_ == 2)
            {
                d2 = (double)p_78867_3_ + block.getBlockBoundsMinZ() - (double)f;
            }

            if (p_78867_4_ == 3)
            {
                d2 = (double)p_78867_3_ + block.getBlockBoundsMaxZ() + (double)f;
            }

            if (p_78867_4_ == 4)
            {
                d0 = (double)p_78867_1_ + block.getBlockBoundsMinX() - (double)f;
            }

            if (p_78867_4_ == 5)
            {
                d0 = (double)p_78867_1_ + block.getBlockBoundsMaxX() + (double)f;
            }

            this.addEffect((new EntityDiggingFX(this.worldObj, d0, d1, d2, 0.0D, 0.0D, 0.0D, block, this.worldObj.getBlockMetadata(p_78867_1_, p_78867_2_, p_78867_3_))).applyColourMultiplier(p_78867_1_, p_78867_2_, p_78867_3_).multiplyVelocity(0.2F).multipleParticleScaleBy(0.6F));
        }
    }

    public String getStatistics()
    {
        return "" + (this.fxLayers[0].size() + this.fxLayers[1].size() + this.fxLayers[2].size());
    }

    public void addBlockHitEffects(int x, int y, int z, MovingObjectPosition target)
    {
        Block block = worldObj.getBlock(x, y, z);
        if (block != null && !block.addHitEffects(worldObj, target, this))
        {
            addBlockHitEffects(x, y, z, target.sideHit);
        }
     }
}