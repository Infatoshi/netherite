package net.minecraft.client.renderer.texture;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.IntBuffer;
import javax.imageio.ImageIO;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GLAllocation;
import net.minecraft.client.renderer.OpenGlHelper;
import net.minecraft.client.resources.IResourceManager;
import net.minecraft.util.ResourceLocation;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL14;
import org.lwjgl.opengl.GL12;

@SideOnly(Side.CLIENT)
public class TextureUtil
{
    private static final Logger logger = LogManager.getLogger();
    private static final IntBuffer dataBuffer = GLAllocation.createDirectIntBuffer(4194304);
    public static final DynamicTexture missingTexture = new DynamicTexture(16, 16);
    public static final int[] missingTextureData = missingTexture.getTextureData();
    private static int field_147958_e = -1;
    private static int field_147956_f = -1;
    private static float field_152779_g = -1.0F;
    private static final int[] field_147957_g;
    private static final String __OBFID = "CL_00001067";

    public static int glGenTextures()
    {
        return GL11.glGenTextures();
    }

    public static void deleteTexture(int p_147942_0_)
    {
        GL11.glDeleteTextures(p_147942_0_);
    }

    public static int uploadTextureImage(int p_110987_0_, BufferedImage p_110987_1_)
    {
        return uploadTextureImageAllocate(p_110987_0_, p_110987_1_, false, false);
    }

    public static void uploadTexture(int p_110988_0_, int[] p_110988_1_, int p_110988_2_, int p_110988_3_)
    {
        bindTexture(p_110988_0_);
        uploadTextureSub(0, p_110988_1_, p_110988_2_, p_110988_3_, 0, 0, false, false, false);
    }

    public static int[][] generateMipmapData(int p_147949_0_, int p_147949_1_, int[][] p_147949_2_)
    {
        int[][] aint1 = new int[p_147949_0_ + 1][];
        aint1[0] = p_147949_2_[0];

        if (p_147949_0_ > 0)
        {
            boolean flag = false;
            int k;

            for (k = 0; k < p_147949_2_.length; ++k)
            {
                if (p_147949_2_[0][k] >> 24 == 0)
                {
                    flag = true;
                    break;
                }
            }

            for (k = 1; k <= p_147949_0_; ++k)
            {
                if (p_147949_2_[k] != null)
                {
                    aint1[k] = p_147949_2_[k];
                }
                else
                {
                    int[] aint2 = aint1[k - 1];
                    int[] aint3 = new int[aint2.length >> 2];
                    int l = p_147949_1_ >> k;
                    int i1 = aint3.length / l;
                    int j1 = l << 1;

                    for (int k1 = 0; k1 < l; ++k1)
                    {
                        for (int l1 = 0; l1 < i1; ++l1)
                        {
                            int i2 = 2 * (k1 + l1 * j1);
                            aint3[k1 + l1 * l] = func_147943_a(aint2[i2 + 0], aint2[i2 + 1], aint2[i2 + 0 + j1], aint2[i2 + 1 + j1], flag);
                        }
                    }

                    aint1[k] = aint3;
                }
            }
        }

        return aint1;
    }

    private static int func_147943_a(int p_147943_0_, int p_147943_1_, int p_147943_2_, int p_147943_3_, boolean p_147943_4_)
    {
        if (!p_147943_4_)
        {
            int i2 = func_147944_a(p_147943_0_, p_147943_1_, p_147943_2_, p_147943_3_, 24);
            int j2 = func_147944_a(p_147943_0_, p_147943_1_, p_147943_2_, p_147943_3_, 16);
            int k2 = func_147944_a(p_147943_0_, p_147943_1_, p_147943_2_, p_147943_3_, 8);
            int l2 = func_147944_a(p_147943_0_, p_147943_1_, p_147943_2_, p_147943_3_, 0);
            return i2 << 24 | j2 << 16 | k2 << 8 | l2;
        }
        else
        {
            field_147957_g[0] = p_147943_0_;
            field_147957_g[1] = p_147943_1_;
            field_147957_g[2] = p_147943_2_;
            field_147957_g[3] = p_147943_3_;
            float f = 0.0F;
            float f1 = 0.0F;
            float f2 = 0.0F;
            float f3 = 0.0F;
            int i1;

            for (i1 = 0; i1 < 4; ++i1)
            {
                if (field_147957_g[i1] >> 24 != 0)
                {
                    f += (float)Math.pow((double)((float)(field_147957_g[i1] >> 24 & 255) / 255.0F), 2.2D);
                    f1 += (float)Math.pow((double)((float)(field_147957_g[i1] >> 16 & 255) / 255.0F), 2.2D);
                    f2 += (float)Math.pow((double)((float)(field_147957_g[i1] >> 8 & 255) / 255.0F), 2.2D);
                    f3 += (float)Math.pow((double)((float)(field_147957_g[i1] >> 0 & 255) / 255.0F), 2.2D);
                }
            }

            f /= 4.0F;
            f1 /= 4.0F;
            f2 /= 4.0F;
            f3 /= 4.0F;
            i1 = (int)(Math.pow((double)f, 0.45454545454545453D) * 255.0D);
            int j1 = (int)(Math.pow((double)f1, 0.45454545454545453D) * 255.0D);
            int k1 = (int)(Math.pow((double)f2, 0.45454545454545453D) * 255.0D);
            int l1 = (int)(Math.pow((double)f3, 0.45454545454545453D) * 255.0D);

            if (i1 < 96)
            {
                i1 = 0;
            }

            return i1 << 24 | j1 << 16 | k1 << 8 | l1;
        }
    }

    private static int func_147944_a(int p_147944_0_, int p_147944_1_, int p_147944_2_, int p_147944_3_, int p_147944_4_)
    {
        float f = (float)Math.pow((double)((float)(p_147944_0_ >> p_147944_4_ & 255) / 255.0F), 2.2D);
        float f1 = (float)Math.pow((double)((float)(p_147944_1_ >> p_147944_4_ & 255) / 255.0F), 2.2D);
        float f2 = (float)Math.pow((double)((float)(p_147944_2_ >> p_147944_4_ & 255) / 255.0F), 2.2D);
        float f3 = (float)Math.pow((double)((float)(p_147944_3_ >> p_147944_4_ & 255) / 255.0F), 2.2D);
        float f4 = (float)Math.pow((double)(f + f1 + f2 + f3) * 0.25D, 0.45454545454545453D);
        return (int)((double)f4 * 255.0D);
    }

    public static void uploadTextureMipmap(int[][] p_147955_0_, int p_147955_1_, int p_147955_2_, int p_147955_3_, int p_147955_4_, boolean p_147955_5_, boolean p_147955_6_)
    {
        for (int i1 = 0; i1 < p_147955_0_.length; ++i1)
        {
            int[] aint1 = p_147955_0_[i1];
            uploadTextureSub(i1, aint1, p_147955_1_ >> i1, p_147955_2_ >> i1, p_147955_3_ >> i1, p_147955_4_ >> i1, p_147955_5_, p_147955_6_, p_147955_0_.length > 1);
        }
    }

    private static void uploadTextureSub(int p_147947_0_, int[] p_147947_1_, int p_147947_2_, int p_147947_3_, int p_147947_4_, int p_147947_5_, boolean p_147947_6_, boolean p_147947_7_, boolean p_147947_8_)
    {
        int j1 = 4194304 / p_147947_2_;
        func_147954_b(p_147947_6_, p_147947_8_);
        setTextureClamped(p_147947_7_);
        int i2;

        for (int k1 = 0; k1 < p_147947_2_ * p_147947_3_; k1 += p_147947_2_ * i2)
        {
            int l1 = k1 / p_147947_2_;
            i2 = Math.min(j1, p_147947_3_ - l1);
            int j2 = p_147947_2_ * i2;
            copyToBufferPos(p_147947_1_, k1, j2);
            GL11.glTexSubImage2D(GL11.GL_TEXTURE_2D, p_147947_0_, p_147947_4_, p_147947_5_ + l1, p_147947_2_, i2, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, dataBuffer);
        }
    }

    public static int uploadTextureImageAllocate(int p_110989_0_, BufferedImage p_110989_1_, boolean p_110989_2_, boolean p_110989_3_)
    {
        allocateTexture(p_110989_0_, p_110989_1_.getWidth(), p_110989_1_.getHeight());
        return uploadTextureImageSub(p_110989_0_, p_110989_1_, 0, 0, p_110989_2_, p_110989_3_);
    }

    public static void allocateTexture(int p_110991_0_, int p_110991_1_, int p_110991_2_)
    {
        allocateTextureImpl(p_110991_0_, 0, p_110991_1_, p_110991_2_, 1.0F);
    }

    public static void allocateTextureImpl(int p_147946_0_, int p_147946_1_, int p_147946_2_, int p_147946_3_, float p_147946_4_)
    {
        synchronized(cpw.mods.fml.client.SplashProgress.class)
        {
        deleteTexture(p_147946_0_);
        bindTexture(p_147946_0_);
        }

        if (OpenGlHelper.anisotropicFilteringSupported)
        {
            GL11.glTexParameterf(GL11.GL_TEXTURE_2D, 34046, p_147946_4_);
        }

        if (p_147946_1_ > 0)
        {
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL12.GL_TEXTURE_MAX_LEVEL, p_147946_1_);
            GL11.glTexParameterf(GL11.GL_TEXTURE_2D, GL12.GL_TEXTURE_MIN_LOD, 0.0F);
            GL11.glTexParameterf(GL11.GL_TEXTURE_2D, GL12.GL_TEXTURE_MAX_LOD, (float)p_147946_1_);
            GL11.glTexParameterf(GL11.GL_TEXTURE_2D, GL14.GL_TEXTURE_LOD_BIAS, 0.0F);
        }

        for (int i1 = 0; i1 <= p_147946_1_; ++i1)
        {
            GL11.glTexImage2D(GL11.GL_TEXTURE_2D, i1, GL11.GL_RGBA, p_147946_2_ >> i1, p_147946_3_ >> i1, 0, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, (IntBuffer)null);
        }
    }

    public static int uploadTextureImageSub(int p_110995_0_, BufferedImage p_110995_1_, int p_110995_2_, int p_110995_3_, boolean p_110995_4_, boolean p_110995_5_)
    {
        bindTexture(p_110995_0_);
        uploadTextureImageSubImpl(p_110995_1_, p_110995_2_, p_110995_3_, p_110995_4_, p_110995_5_);
        return p_110995_0_;
    }

    private static void uploadTextureImageSubImpl(BufferedImage p_110993_0_, int p_110993_1_, int p_110993_2_, boolean p_110993_3_, boolean p_110993_4_)
    {
        int k = p_110993_0_.getWidth();
        int l = p_110993_0_.getHeight();
        int i1 = 4194304 / k;
        int[] aint = new int[i1 * k];
        setTextureBlurred(p_110993_3_);
        setTextureClamped(p_110993_4_);

        for (int j1 = 0; j1 < k * l; j1 += k * i1)
        {
            int k1 = j1 / k;
            int l1 = Math.min(i1, l - k1);
            int i2 = k * l1;
            p_110993_0_.getRGB(0, k1, k, l1, aint, 0, k);
            copyToBuffer(aint, i2);
            GL11.glTexSubImage2D(GL11.GL_TEXTURE_2D, 0, p_110993_1_, p_110993_2_ + k1, k, l1, GL12.GL_BGRA, GL12.GL_UNSIGNED_INT_8_8_8_8_REV, dataBuffer);
        }
    }

    private static void setTextureClamped(boolean p_110997_0_)
    {
        if (p_110997_0_)
        {
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_S, GL11.GL_CLAMP);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_T, GL11.GL_CLAMP);
        }
        else
        {
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_S, GL11.GL_REPEAT);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_WRAP_T, GL11.GL_REPEAT);
        }
    }

    private static void setTextureBlurred(boolean p_147951_0_)
    {
        func_147954_b(p_147951_0_, false);
    }

    public static void func_152777_a(boolean p_152777_0_, boolean p_152777_1_, float p_152777_2_)
    {
        field_147958_e = GL11.glGetTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER);
        field_147956_f = GL11.glGetTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER);
        field_152779_g = GL11.glGetTexParameterf(GL11.GL_TEXTURE_2D, 34046);
        func_147954_b(p_152777_0_, p_152777_1_);
        func_152778_a(p_152777_2_);
    }

    public static void func_147945_b()
    {
        if (field_147958_e >= 0 && field_147956_f >= 0 && field_152779_g >= 0.0F)
        {
            func_147952_b(field_147958_e, field_147956_f);
            func_152778_a(field_152779_g);
            field_152779_g = -1.0F;
            field_147958_e = -1;
            field_147956_f = -1;
        }
    }

    private static void func_147952_b(int p_147952_0_, int p_147952_1_)
    {
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, p_147952_0_);
        GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, p_147952_1_);
    }

    private static void func_152778_a(float p_152778_0_)
    {
        GL11.glTexParameterf(GL11.GL_TEXTURE_2D, 34046, p_152778_0_);
    }

    private static void func_147954_b(boolean p_147954_0_, boolean p_147954_1_)
    {
        if (p_147954_0_)
        {
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, p_147954_1_ ? GL11.GL_LINEAR_MIPMAP_LINEAR : GL11.GL_LINEAR);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_LINEAR);
        }
        else
        {
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MIN_FILTER, p_147954_1_ ? GL11.GL_NEAREST_MIPMAP_LINEAR : GL11.GL_NEAREST);
            GL11.glTexParameteri(GL11.GL_TEXTURE_2D, GL11.GL_TEXTURE_MAG_FILTER, GL11.GL_NEAREST);
        }
    }

    private static void copyToBuffer(int[] p_110990_0_, int p_110990_1_)
    {
        copyToBufferPos(p_110990_0_, 0, p_110990_1_);
    }

    private static void copyToBufferPos(int[] p_110994_0_, int p_110994_1_, int p_110994_2_)
    {
        int[] aint1 = p_110994_0_;

        if (Minecraft.getMinecraft().gameSettings.anaglyph)
        {
            aint1 = updateAnaglyph(p_110994_0_);
        }

        dataBuffer.clear();
        dataBuffer.put(aint1, p_110994_1_, p_110994_2_);
        dataBuffer.position(0).limit(p_110994_2_);
    }

    static void bindTexture(int p_94277_0_)
    {
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, p_94277_0_);
    }

    public static int[] readImageData(IResourceManager p_110986_0_, ResourceLocation p_110986_1_) throws IOException
    {
        BufferedImage bufferedimage = ImageIO.read(p_110986_0_.getResource(p_110986_1_).getInputStream());
        int i = bufferedimage.getWidth();
        int j = bufferedimage.getHeight();
        int[] aint = new int[i * j];
        bufferedimage.getRGB(0, 0, i, j, aint, 0, i);
        return aint;
    }

    public static int[] updateAnaglyph(int[] p_110985_0_)
    {
        int[] aint1 = new int[p_110985_0_.length];

        for (int i = 0; i < p_110985_0_.length; ++i)
        {
            int j = p_110985_0_[i] >> 24 & 255;
            int k = p_110985_0_[i] >> 16 & 255;
            int l = p_110985_0_[i] >> 8 & 255;
            int i1 = p_110985_0_[i] & 255;
            int j1 = (k * 30 + l * 59 + i1 * 11) / 100;
            int k1 = (k * 30 + l * 70) / 100;
            int l1 = (k * 30 + i1 * 70) / 100;
            aint1[i] = j << 24 | j1 << 16 | k1 << 8 | l1;
        }

        return aint1;
    }

    public static int[] prepareAnisotropicData(int[] p_147948_0_, int p_147948_1_, int p_147948_2_, int p_147948_3_)
    {
        int l = p_147948_1_ + 2 * p_147948_3_;
        int i1;
        int j1;

        for (i1 = p_147948_2_ - 1; i1 >= 0; --i1)
        {
            j1 = i1 * p_147948_1_;
            int k1 = p_147948_3_ + (i1 + p_147948_3_) * l;
            int l1;

            for (l1 = 0; l1 < p_147948_3_; l1 += p_147948_1_)
            {
                int i2 = Math.min(p_147948_1_, p_147948_3_ - l1);
                System.arraycopy(p_147948_0_, j1 + p_147948_1_ - i2, p_147948_0_, k1 - l1 - i2, i2);
            }

            System.arraycopy(p_147948_0_, j1, p_147948_0_, k1, p_147948_1_);

            for (l1 = 0; l1 < p_147948_3_; l1 += p_147948_1_)
            {
                System.arraycopy(p_147948_0_, j1, p_147948_0_, k1 + p_147948_1_ + l1, Math.min(p_147948_1_, p_147948_3_ - l1));
            }
        }

        for (i1 = 0; i1 < p_147948_3_; i1 += p_147948_2_)
        {
            j1 = Math.min(p_147948_2_, p_147948_3_ - i1);
            System.arraycopy(p_147948_0_, (p_147948_3_ + p_147948_2_ - j1) * l, p_147948_0_, (p_147948_3_ - i1 - j1) * l, l * j1);
        }

        for (i1 = 0; i1 < p_147948_3_; i1 += p_147948_2_)
        {
            j1 = Math.min(p_147948_2_, p_147948_3_ - i1);
            System.arraycopy(p_147948_0_, p_147948_3_ * l, p_147948_0_, (p_147948_2_ + p_147948_3_ + i1) * l, l * j1);
        }

        return p_147948_0_;
    }

    public static void func_147953_a(int[] p_147953_0_, int p_147953_1_, int p_147953_2_)
    {
        int[] aint1 = new int[p_147953_1_];
        int k = p_147953_2_ / 2;

        for (int l = 0; l < k; ++l)
        {
            System.arraycopy(p_147953_0_, l * p_147953_1_, aint1, 0, p_147953_1_);
            System.arraycopy(p_147953_0_, (p_147953_2_ - 1 - l) * p_147953_1_, p_147953_0_, l * p_147953_1_, p_147953_1_);
            System.arraycopy(aint1, 0, p_147953_0_, (p_147953_2_ - 1 - l) * p_147953_1_, p_147953_1_);
        }
    }

    static
    {
        int var0 = -16777216;
        int var1 = -524040;
        int[] var2 = new int[] { -524040, -524040, -524040, -524040, -524040, -524040, -524040, -524040};
        int[] var3 = new int[] { -16777216, -16777216, -16777216, -16777216, -16777216, -16777216, -16777216, -16777216};
        int var4 = var2.length;

        for (int var5 = 0; var5 < 16; ++var5)
        {
            System.arraycopy(var5 < var4 ? var2 : var3, 0, missingTextureData, 16 * var5, var4);
            System.arraycopy(var5 < var4 ? var3 : var2, 0, missingTextureData, 16 * var5 + var4, var4);
        }

        missingTexture.updateDynamicTexture();
        field_147957_g = new int[4];
    }
}