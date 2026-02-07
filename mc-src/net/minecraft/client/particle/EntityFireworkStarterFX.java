package net.minecraft.client.particle;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntityFireworkStarterFX extends EntityFX
{
    private int fireworkAge;
    private final EffectRenderer theEffectRenderer;
    private NBTTagList fireworkExplosions;
    boolean twinkle;
    private static final String __OBFID = "CL_00000906";

    public EntityFireworkStarterFX(World p_i1208_1_, double p_i1208_2_, double p_i1208_4_, double p_i1208_6_, double p_i1208_8_, double p_i1208_10_, double p_i1208_12_, EffectRenderer p_i1208_14_, NBTTagCompound p_i1208_15_)
    {
        super(p_i1208_1_, p_i1208_2_, p_i1208_4_, p_i1208_6_, 0.0D, 0.0D, 0.0D);
        this.motionX = p_i1208_8_;
        this.motionY = p_i1208_10_;
        this.motionZ = p_i1208_12_;
        this.theEffectRenderer = p_i1208_14_;
        this.particleMaxAge = 8;

        if (p_i1208_15_ != null)
        {
            this.fireworkExplosions = p_i1208_15_.getTagList("Explosions", 10);

            if (this.fireworkExplosions.tagCount() == 0)
            {
                this.fireworkExplosions = null;
            }
            else
            {
                this.particleMaxAge = this.fireworkExplosions.tagCount() * 2 - 1;

                for (int i = 0; i < this.fireworkExplosions.tagCount(); ++i)
                {
                    NBTTagCompound nbttagcompound1 = this.fireworkExplosions.getCompoundTagAt(i);

                    if (nbttagcompound1.getBoolean("Flicker"))
                    {
                        this.twinkle = true;
                        this.particleMaxAge += 15;
                        break;
                    }
                }
            }
        }
    }

    public void renderParticle(Tessellator p_70539_1_, float p_70539_2_, float p_70539_3_, float p_70539_4_, float p_70539_5_, float p_70539_6_, float p_70539_7_) {}

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        boolean flag;

        if (this.fireworkAge == 0 && this.fireworkExplosions != null)
        {
            flag = this.func_92037_i();
            boolean flag1 = false;

            if (this.fireworkExplosions.tagCount() >= 3)
            {
                flag1 = true;
            }
            else
            {
                for (int i = 0; i < this.fireworkExplosions.tagCount(); ++i)
                {
                    NBTTagCompound nbttagcompound = this.fireworkExplosions.getCompoundTagAt(i);

                    if (nbttagcompound.getByte("Type") == 1)
                    {
                        flag1 = true;
                        break;
                    }
                }
            }

            String s1 = "fireworks." + (flag1 ? "largeBlast" : "blast") + (flag ? "_far" : "");
            this.worldObj.playSound(this.posX, this.posY, this.posZ, s1, 20.0F, 0.95F + this.rand.nextFloat() * 0.1F, true);
        }

        if (this.fireworkAge % 2 == 0 && this.fireworkExplosions != null && this.fireworkAge / 2 < this.fireworkExplosions.tagCount())
        {
            int k = this.fireworkAge / 2;
            NBTTagCompound nbttagcompound1 = this.fireworkExplosions.getCompoundTagAt(k);
            byte b0 = nbttagcompound1.getByte("Type");
            boolean flag3 = nbttagcompound1.getBoolean("Trail");
            boolean flag2 = nbttagcompound1.getBoolean("Flicker");
            int[] aint = nbttagcompound1.getIntArray("Colors");
            int[] aint1 = nbttagcompound1.getIntArray("FadeColors");

            if (b0 == 1)
            {
                this.createBall(0.5D, 4, aint, aint1, flag3, flag2);
            }
            else if (b0 == 2)
            {
                this.createShaped(0.5D, new double[][] {{0.0D, 1.0D}, {0.3455D, 0.309D}, {0.9511D, 0.309D}, {0.3795918367346939D, -0.12653061224489795D}, {0.6122448979591837D, -0.8040816326530612D}, {0.0D, -0.35918367346938773D}}, aint, aint1, flag3, flag2, false);
            }
            else if (b0 == 3)
            {
                this.createShaped(0.5D, new double[][] {{0.0D, 0.2D}, {0.2D, 0.2D}, {0.2D, 0.6D}, {0.6D, 0.6D}, {0.6D, 0.2D}, {0.2D, 0.2D}, {0.2D, 0.0D}, {0.4D, 0.0D}, {0.4D, -0.6D}, {0.2D, -0.6D}, {0.2D, -0.4D}, {0.0D, -0.4D}}, aint, aint1, flag3, flag2, true);
            }
            else if (b0 == 4)
            {
                this.createBurst(aint, aint1, flag3, flag2);
            }
            else
            {
                this.createBall(0.25D, 2, aint, aint1, flag3, flag2);
            }

            int j = aint[0];
            float f = (float)((j & 16711680) >> 16) / 255.0F;
            float f1 = (float)((j & 65280) >> 8) / 255.0F;
            float f2 = (float)((j & 255) >> 0) / 255.0F;
            EntityFireworkOverlayFX entityfireworkoverlayfx = new EntityFireworkOverlayFX(this.worldObj, this.posX, this.posY, this.posZ);
            entityfireworkoverlayfx.setRBGColorF(f, f1, f2);
            this.theEffectRenderer.addEffect(entityfireworkoverlayfx);
        }

        ++this.fireworkAge;

        if (this.fireworkAge > this.particleMaxAge)
        {
            if (this.twinkle)
            {
                flag = this.func_92037_i();
                String s = "fireworks." + (flag ? "twinkle_far" : "twinkle");
                this.worldObj.playSound(this.posX, this.posY, this.posZ, s, 20.0F, 0.9F + this.rand.nextFloat() * 0.15F, true);
            }

            this.setDead();
        }
    }

    private boolean func_92037_i()
    {
        Minecraft minecraft = Minecraft.getMinecraft();
        return minecraft == null || minecraft.renderViewEntity == null || minecraft.renderViewEntity.getDistanceSq(this.posX, this.posY, this.posZ) >= 256.0D;
    }

    /**
     * Creates a single particle. Args: x, y, z, x velocity, y velocity, z velocity, colours, fade colours, whether to
     * trail, whether to twinkle
     */
    private void createParticle(double p_92034_1_, double p_92034_3_, double p_92034_5_, double p_92034_7_, double p_92034_9_, double p_92034_11_, int[] p_92034_13_, int[] p_92034_14_, boolean p_92034_15_, boolean p_92034_16_)
    {
        EntityFireworkSparkFX entityfireworksparkfx = new EntityFireworkSparkFX(this.worldObj, p_92034_1_, p_92034_3_, p_92034_5_, p_92034_7_, p_92034_9_, p_92034_11_, this.theEffectRenderer);
        entityfireworksparkfx.setTrail(p_92034_15_);
        entityfireworksparkfx.setTwinkle(p_92034_16_);
        int i = this.rand.nextInt(p_92034_13_.length);
        entityfireworksparkfx.setColour(p_92034_13_[i]);

        if (p_92034_14_ != null && p_92034_14_.length > 0)
        {
            entityfireworksparkfx.setFadeColour(p_92034_14_[this.rand.nextInt(p_92034_14_.length)]);
        }

        this.theEffectRenderer.addEffect(entityfireworksparkfx);
    }

    /**
     * Creates a small ball or large ball type explosion. Args: particle speed, size, colours, fade colours, whether to
     * trail, whether to flicker
     */
    private void createBall(double p_92035_1_, int p_92035_3_, int[] p_92035_4_, int[] p_92035_5_, boolean p_92035_6_, boolean p_92035_7_)
    {
        double d1 = this.posX;
        double d2 = this.posY;
        double d3 = this.posZ;

        for (int j = -p_92035_3_; j <= p_92035_3_; ++j)
        {
            for (int k = -p_92035_3_; k <= p_92035_3_; ++k)
            {
                for (int l = -p_92035_3_; l <= p_92035_3_; ++l)
                {
                    double d4 = (double)k + (this.rand.nextDouble() - this.rand.nextDouble()) * 0.5D;
                    double d5 = (double)j + (this.rand.nextDouble() - this.rand.nextDouble()) * 0.5D;
                    double d6 = (double)l + (this.rand.nextDouble() - this.rand.nextDouble()) * 0.5D;
                    double d7 = (double)MathHelper.sqrt_double(d4 * d4 + d5 * d5 + d6 * d6) / p_92035_1_ + this.rand.nextGaussian() * 0.05D;
                    this.createParticle(d1, d2, d3, d4 / d7, d5 / d7, d6 / d7, p_92035_4_, p_92035_5_, p_92035_6_, p_92035_7_);

                    if (j != -p_92035_3_ && j != p_92035_3_ && k != -p_92035_3_ && k != p_92035_3_)
                    {
                        l += p_92035_3_ * 2 - 1;
                    }
                }
            }
        }
    }

    /**
     * Creates a creeper-shaped or star-shaped explosion. Args: particle speed, shape, colours, fade colours, whether to
     * trail, whether to flicker, unknown
     */
    private void createShaped(double p_92038_1_, double[][] p_92038_3_, int[] p_92038_4_, int[] p_92038_5_, boolean p_92038_6_, boolean p_92038_7_, boolean p_92038_8_)
    {
        double d1 = p_92038_3_[0][0];
        double d2 = p_92038_3_[0][1];
        this.createParticle(this.posX, this.posY, this.posZ, d1 * p_92038_1_, d2 * p_92038_1_, 0.0D, p_92038_4_, p_92038_5_, p_92038_6_, p_92038_7_);
        float f = this.rand.nextFloat() * (float)Math.PI;
        double d3 = p_92038_8_ ? 0.034D : 0.34D;

        for (int i = 0; i < 3; ++i)
        {
            double d4 = (double)f + (double)((float)i * (float)Math.PI) * d3;
            double d5 = d1;
            double d6 = d2;

            for (int j = 1; j < p_92038_3_.length; ++j)
            {
                double d7 = p_92038_3_[j][0];
                double d8 = p_92038_3_[j][1];

                for (double d9 = 0.25D; d9 <= 1.0D; d9 += 0.25D)
                {
                    double d10 = (d5 + (d7 - d5) * d9) * p_92038_1_;
                    double d11 = (d6 + (d8 - d6) * d9) * p_92038_1_;
                    double d12 = d10 * Math.sin(d4);
                    d10 *= Math.cos(d4);

                    for (double d13 = -1.0D; d13 <= 1.0D; d13 += 2.0D)
                    {
                        this.createParticle(this.posX, this.posY, this.posZ, d10 * d13, d11, d12 * d13, p_92038_4_, p_92038_5_, p_92038_6_, p_92038_7_);
                    }
                }

                d5 = d7;
                d6 = d8;
            }
        }
    }

    /**
     * Creates a burst type explosion. Args: colours, fade colours, whether to trail, whether to flicker
     */
    private void createBurst(int[] p_92036_1_, int[] p_92036_2_, boolean p_92036_3_, boolean p_92036_4_)
    {
        double d0 = this.rand.nextGaussian() * 0.05D;
        double d1 = this.rand.nextGaussian() * 0.05D;

        for (int i = 0; i < 70; ++i)
        {
            double d2 = this.motionX * 0.5D + this.rand.nextGaussian() * 0.15D + d0;
            double d3 = this.motionZ * 0.5D + this.rand.nextGaussian() * 0.15D + d1;
            double d4 = this.motionY * 0.5D + this.rand.nextDouble() * 0.5D;
            this.createParticle(this.posX, this.posY, this.posZ, d2, d4, d3, p_92036_1_, p_92036_2_, p_92036_3_, p_92036_4_);
        }
    }

    public int getFXLayer()
    {
        return 0;
    }
}