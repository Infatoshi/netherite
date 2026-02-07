package net.minecraft.client.renderer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.Graphics;
import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;
import java.awt.image.ImageObserver;

@SideOnly(Side.CLIENT)
public class ImageBufferDownload implements IImageBuffer
{
    private int[] imageData;
    private int imageWidth;
    private int imageHeight;
    private static final String __OBFID = "CL_00000956";

    public BufferedImage parseUserSkin(BufferedImage p_78432_1_)
    {
        if (p_78432_1_ == null)
        {
            return null;
        }
        else
        {
            this.imageWidth = 64;
            this.imageHeight = 32;
            BufferedImage bufferedimage1 = new BufferedImage(this.imageWidth, this.imageHeight, 2);
            Graphics graphics = bufferedimage1.getGraphics();
            graphics.drawImage(p_78432_1_, 0, 0, (ImageObserver)null);
            graphics.dispose();
            this.imageData = ((DataBufferInt)bufferedimage1.getRaster().getDataBuffer()).getData();
            this.setAreaOpaque(0, 0, 32, 16);
            this.setAreaTransparent(32, 0, 64, 32);
            this.setAreaOpaque(0, 16, 64, 32);
            return bufferedimage1;
        }
    }

    public void func_152634_a() {}

    /**
     * Makes the given area of the image transparent if it was previously completely opaque (used to remove the outer
     * layer of a skin around the head if it was saved all opaque; this would be redundant so it's assumed that the skin
     * maker is just using an image editor without an alpha channel)
     */
    private void setAreaTransparent(int p_78434_1_, int p_78434_2_, int p_78434_3_, int p_78434_4_)
    {
        if (!this.hasTransparency(p_78434_1_, p_78434_2_, p_78434_3_, p_78434_4_))
        {
            for (int i1 = p_78434_1_; i1 < p_78434_3_; ++i1)
            {
                for (int j1 = p_78434_2_; j1 < p_78434_4_; ++j1)
                {
                    this.imageData[i1 + j1 * this.imageWidth] &= 16777215;
                }
            }
        }
    }

    /**
     * Makes the given area of the image opaque
     */
    private void setAreaOpaque(int p_78433_1_, int p_78433_2_, int p_78433_3_, int p_78433_4_)
    {
        for (int i1 = p_78433_1_; i1 < p_78433_3_; ++i1)
        {
            for (int j1 = p_78433_2_; j1 < p_78433_4_; ++j1)
            {
                this.imageData[i1 + j1 * this.imageWidth] |= -16777216;
            }
        }
    }

    /**
     * Returns true if the given area of the image contains transparent pixels
     */
    private boolean hasTransparency(int p_78435_1_, int p_78435_2_, int p_78435_3_, int p_78435_4_)
    {
        for (int i1 = p_78435_1_; i1 < p_78435_3_; ++i1)
        {
            for (int j1 = p_78435_2_; j1 < p_78435_4_; ++j1)
            {
                int k1 = this.imageData[i1 + j1 * this.imageWidth];

                if ((k1 >> 24 & 255) < 128)
                {
                    return true;
                }
            }
        }

        return false;
    }
}