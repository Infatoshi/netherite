package net.minecraft.client.renderer.texture;

import com.google.common.collect.Lists;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import net.minecraft.client.renderer.StitcherException;
import net.minecraft.util.MathHelper;

@SideOnly(Side.CLIENT)
public class Stitcher
{
    private final int mipmapLevelStitcher;
    private final Set setStitchHolders = new HashSet(256);
    private final List stitchSlots = new ArrayList(256);
    private int currentWidth;
    private int currentHeight;
    private final int maxWidth;
    private final int maxHeight;
    private final boolean forcePowerOf2;
    /** Max size (width or height) of a single tile */
    private final int maxTileDimension;
    private static final String __OBFID = "CL_00001054";

    public Stitcher(int p_i45095_1_, int p_i45095_2_, boolean p_i45095_3_, int p_i45095_4_, int p_i45095_5_)
    {
        this.mipmapLevelStitcher = p_i45095_5_;
        this.maxWidth = p_i45095_1_;
        this.maxHeight = p_i45095_2_;
        this.forcePowerOf2 = p_i45095_3_;
        this.maxTileDimension = p_i45095_4_;
    }

    public int getCurrentWidth()
    {
        return this.currentWidth;
    }

    public int getCurrentHeight()
    {
        return this.currentHeight;
    }

    public void addSprite(TextureAtlasSprite p_110934_1_)
    {
        Stitcher.Holder holder = new Stitcher.Holder(p_110934_1_, this.mipmapLevelStitcher);

        if (this.maxTileDimension > 0)
        {
            holder.setNewDimension(this.maxTileDimension);
        }

        this.setStitchHolders.add(holder);
    }

    public void doStitch()
    {
        Stitcher.Holder[] aholder = (Stitcher.Holder[])this.setStitchHolders.toArray(new Stitcher.Holder[this.setStitchHolders.size()]);
        cpw.mods.fml.common.ProgressManager.ProgressBar bar = cpw.mods.fml.common.ProgressManager.push("Texture stitching", aholder.length);
        Arrays.sort(aholder);
        Stitcher.Holder[] aholder1 = aholder;
        int i = aholder.length;

        for (int j = 0; j < i; ++j)
        {
            Stitcher.Holder holder = aholder1[j];
            bar.step(holder.getAtlasSprite().getIconName());

            if (!this.allocateSlot(holder))
            {
                String s = String.format("Unable to fit: %s - size: %dx%d - Maybe try a lowerresolution resourcepack?", new Object[] {holder.getAtlasSprite().getIconName(), Integer.valueOf(holder.getAtlasSprite().getIconWidth()), Integer.valueOf(holder.getAtlasSprite().getIconHeight())});
                throw new StitcherException(holder, s);
            }
        }

        if (this.forcePowerOf2)
        {
            this.currentWidth = MathHelper.roundUpToPowerOfTwo(this.currentWidth);
            this.currentHeight = MathHelper.roundUpToPowerOfTwo(this.currentHeight);
        }
        cpw.mods.fml.common.ProgressManager.pop(bar);
    }

    public List getStichSlots()
    {
        ArrayList arraylist = Lists.newArrayList();
        Iterator iterator = this.stitchSlots.iterator();

        while (iterator.hasNext())
        {
            Stitcher.Slot slot = (Stitcher.Slot)iterator.next();
            slot.getAllStitchSlots(arraylist);
        }

        ArrayList arraylist1 = Lists.newArrayList();
        Iterator iterator1 = arraylist.iterator();

        while (iterator1.hasNext())
        {
            Stitcher.Slot slot1 = (Stitcher.Slot)iterator1.next();
            Stitcher.Holder holder = slot1.getStitchHolder();
            TextureAtlasSprite textureatlassprite = holder.getAtlasSprite();
            textureatlassprite.initSprite(this.currentWidth, this.currentHeight, slot1.getOriginX(), slot1.getOriginY(), holder.isRotated());
            arraylist1.add(textureatlassprite);
        }

        return arraylist1;
    }

    private static int getMipmapDimension(int p_147969_0_, int p_147969_1_)
    {
        return (p_147969_0_ >> p_147969_1_) + ((p_147969_0_ & (1 << p_147969_1_) - 1) == 0 ? 0 : 1) << p_147969_1_;
    }

    /**
     * Attempts to find space for specified tile
     */
    private boolean allocateSlot(Stitcher.Holder p_94310_1_)
    {
        for (int i = 0; i < this.stitchSlots.size(); ++i)
        {
            if (((Stitcher.Slot)this.stitchSlots.get(i)).addSlot(p_94310_1_))
            {
                return true;
            }

            p_94310_1_.rotate();

            if (((Stitcher.Slot)this.stitchSlots.get(i)).addSlot(p_94310_1_))
            {
                return true;
            }

            p_94310_1_.rotate();
        }

        return this.expandAndAllocateSlot(p_94310_1_);
    }

    /**
     * Expand stitched texture in order to make space for specified tile
     */
    private boolean expandAndAllocateSlot(Stitcher.Holder p_94311_1_)
    {
        int i = Math.min(p_94311_1_.getWidth(), p_94311_1_.getHeight());
        boolean flag = this.currentWidth == 0 && this.currentHeight == 0;
        boolean flag1;
        int j;

        if (this.forcePowerOf2)
        {
            j = MathHelper.roundUpToPowerOfTwo(this.currentWidth);
            int k = MathHelper.roundUpToPowerOfTwo(this.currentHeight);
            int l = MathHelper.roundUpToPowerOfTwo(this.currentWidth + i);
            int i1 = MathHelper.roundUpToPowerOfTwo(this.currentHeight + i);
            boolean flag2 = l <= this.maxWidth;
            boolean flag3 = i1 <= this.maxHeight;

            if (!flag2 && !flag3)
            {
                return false;
            }

            boolean flag4 = j != l;
            boolean flag5 = k != i1;

            if (flag4 ^ flag5)
            {
                flag1 = !flag4;
            }
            else
            {
                flag1 = flag2 && j <= k;
            }
        }
        else
        {
            boolean flag6 = this.currentWidth + i <= this.maxWidth;
            boolean flag7 = this.currentHeight + i <= this.maxHeight;

            if (!flag6 && !flag7)
            {
                return false;
            }

            flag1 = flag6 && (flag || this.currentWidth <= this.currentHeight);
        }

        j = Math.max(p_94311_1_.getWidth(), p_94311_1_.getHeight());

        if (MathHelper.roundUpToPowerOfTwo((flag1 ? this.currentHeight : this.currentWidth) + j) > (flag1 ? this.maxHeight : this.maxWidth))
        {
            return false;
        }
        else
        {
            Stitcher.Slot slot;

            if (flag1)
            {
                if (p_94311_1_.getWidth() > p_94311_1_.getHeight())
                {
                    p_94311_1_.rotate();
                }

                if (this.currentHeight == 0)
                {
                    this.currentHeight = p_94311_1_.getHeight();
                }

                slot = new Stitcher.Slot(this.currentWidth, 0, p_94311_1_.getWidth(), this.currentHeight);
                this.currentWidth += p_94311_1_.getWidth();
            }
            else
            {
                slot = new Stitcher.Slot(0, this.currentHeight, this.currentWidth, p_94311_1_.getHeight());
                this.currentHeight += p_94311_1_.getHeight();
            }

            slot.addSlot(p_94311_1_);
            this.stitchSlots.add(slot);
            return true;
        }
    }

    @SideOnly(Side.CLIENT)
    public static class Holder implements Comparable
        {
            private final TextureAtlasSprite theTexture;
            private final int width;
            private final int height;
            private final int mipmapLevelHolder;
            private boolean rotated;
            private float scaleFactor = 1.0F;
            private static final String __OBFID = "CL_00001055";

            public Holder(TextureAtlasSprite p_i45094_1_, int p_i45094_2_)
            {
                this.theTexture = p_i45094_1_;
                this.width = p_i45094_1_.getIconWidth();
                this.height = p_i45094_1_.getIconHeight();
                this.mipmapLevelHolder = p_i45094_2_;
                this.rotated = Stitcher.getMipmapDimension(this.height, p_i45094_2_) > Stitcher.getMipmapDimension(this.width, p_i45094_2_);
            }

            public TextureAtlasSprite getAtlasSprite()
            {
                return this.theTexture;
            }

            public int getWidth()
            {
                return this.rotated ? Stitcher.getMipmapDimension((int)((float)this.height * this.scaleFactor), this.mipmapLevelHolder) : Stitcher.getMipmapDimension((int)((float)this.width * this.scaleFactor), this.mipmapLevelHolder);
            }

            public int getHeight()
            {
                return this.rotated ? Stitcher.getMipmapDimension((int)((float)this.width * this.scaleFactor), this.mipmapLevelHolder) : Stitcher.getMipmapDimension((int)((float)this.height * this.scaleFactor), this.mipmapLevelHolder);
            }

            public void rotate()
            {
                this.rotated = !this.rotated;
            }

            public boolean isRotated()
            {
                return this.rotated;
            }

            public void setNewDimension(int p_94196_1_)
            {
                if (this.width > p_94196_1_ && this.height > p_94196_1_)
                {
                    this.scaleFactor = (float)p_94196_1_ / (float)Math.min(this.width, this.height);
                }
            }

            public String toString()
            {
                return "Holder{width=" + this.width + ", height=" + this.height + '}';
            }

            public int compareTo(Stitcher.Holder p_compareTo_1_)
            {
                int i;

                if (this.getHeight() == p_compareTo_1_.getHeight())
                {
                    if (this.getWidth() == p_compareTo_1_.getWidth())
                    {
                        if (this.theTexture.getIconName() == null)
                        {
                            return p_compareTo_1_.theTexture.getIconName() == null ? 0 : -1;
                        }

                        return this.theTexture.getIconName().compareTo(p_compareTo_1_.theTexture.getIconName());
                    }

                    i = this.getWidth() < p_compareTo_1_.getWidth() ? 1 : -1;
                }
                else
                {
                    i = this.getHeight() < p_compareTo_1_.getHeight() ? 1 : -1;
                }

                return i;
            }

            public int compareTo(Object p_compareTo_1_)
            {
                return this.compareTo((Stitcher.Holder)p_compareTo_1_);
            }
        }

    @SideOnly(Side.CLIENT)
    public static class Slot
        {
            private final int originX;
            private final int originY;
            private final int width;
            private final int height;
            private List subSlots;
            private Stitcher.Holder holder;
            private static final String __OBFID = "CL_00001056";

            public Slot(int p_i1277_1_, int p_i1277_2_, int p_i1277_3_, int p_i1277_4_)
            {
                this.originX = p_i1277_1_;
                this.originY = p_i1277_2_;
                this.width = p_i1277_3_;
                this.height = p_i1277_4_;
            }

            public Stitcher.Holder getStitchHolder()
            {
                return this.holder;
            }

            public int getOriginX()
            {
                return this.originX;
            }

            public int getOriginY()
            {
                return this.originY;
            }

            public boolean addSlot(Stitcher.Holder p_94182_1_)
            {
                if (this.holder != null)
                {
                    return false;
                }
                else
                {
                    int i = p_94182_1_.getWidth();
                    int j = p_94182_1_.getHeight();

                    if (i <= this.width && j <= this.height)
                    {
                        if (i == this.width && j == this.height)
                        {
                            this.holder = p_94182_1_;
                            return true;
                        }
                        else
                        {
                            if (this.subSlots == null)
                            {
                                this.subSlots = new ArrayList(1);
                                this.subSlots.add(new Stitcher.Slot(this.originX, this.originY, i, j));
                                int k = this.width - i;
                                int l = this.height - j;

                                if (l > 0 && k > 0)
                                {
                                    int i1 = Math.max(this.height, k);
                                    int j1 = Math.max(this.width, l);

                                    if (i1 >= j1)
                                    {
                                        this.subSlots.add(new Stitcher.Slot(this.originX, this.originY + j, i, l));
                                        this.subSlots.add(new Stitcher.Slot(this.originX + i, this.originY, k, this.height));
                                    }
                                    else
                                    {
                                        this.subSlots.add(new Stitcher.Slot(this.originX + i, this.originY, k, j));
                                        this.subSlots.add(new Stitcher.Slot(this.originX, this.originY + j, this.width, l));
                                    }
                                }
                                else if (k == 0)
                                {
                                    this.subSlots.add(new Stitcher.Slot(this.originX, this.originY + j, i, l));
                                }
                                else if (l == 0)
                                {
                                    this.subSlots.add(new Stitcher.Slot(this.originX + i, this.originY, k, j));
                                }
                            }

                            Iterator iterator = this.subSlots.iterator();
                            Stitcher.Slot slot;

                            do
                            {
                                if (!iterator.hasNext())
                                {
                                    return false;
                                }

                                slot = (Stitcher.Slot)iterator.next();
                            }
                            while (!slot.addSlot(p_94182_1_));

                            return true;
                        }
                    }
                    else
                    {
                        return false;
                    }
                }
            }

            /**
             * Gets the slot and all its subslots
             */
            public void getAllStitchSlots(List p_94184_1_)
            {
                if (this.holder != null)
                {
                    p_94184_1_.add(this);
                }
                else if (this.subSlots != null)
                {
                    Iterator iterator = this.subSlots.iterator();

                    while (iterator.hasNext())
                    {
                        Stitcher.Slot slot = (Stitcher.Slot)iterator.next();
                        slot.getAllStitchSlots(p_94184_1_);
                    }
                }
            }

            public String toString()
            {
                return "Slot{originX=" + this.originX + ", originY=" + this.originY + ", width=" + this.width + ", height=" + this.height + ", texture=" + this.holder + ", subSlots=" + this.subSlots + '}';
            }
        }
}