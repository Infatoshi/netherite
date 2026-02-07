package net.minecraft.client.renderer.texture;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.concurrent.Callable;
import javax.imageio.ImageIO;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.StitcherException;
import net.minecraft.client.renderer.entity.RenderManager;
import net.minecraft.client.resources.IResource;
import net.minecraft.client.resources.IResourceManager;
import net.minecraft.client.resources.data.AnimationMetadataSection;
import net.minecraft.client.resources.data.TextureMetadataSection;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.item.Item;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ReportedException;
import net.minecraft.util.ResourceLocation;
import net.minecraftforge.client.ForgeHooksClient;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class TextureMap extends AbstractTexture implements ITickableTextureObject, IIconRegister
{
    private static final boolean ENABLE_SKIP = Boolean.parseBoolean(System.getProperty("fml.skipFirstTextureLoad", "true"));
    private static final Logger logger = LogManager.getLogger();
    public static final ResourceLocation locationBlocksTexture = new ResourceLocation("textures/atlas/blocks.png");
    public static final ResourceLocation locationItemsTexture = new ResourceLocation("textures/atlas/items.png");
    private final List listAnimatedSprites = Lists.newArrayList();
    private final Map mapRegisteredSprites = Maps.newHashMap();
    private final Map mapUploadedSprites = Maps.newHashMap();
    /** 0 = terrain.png, 1 = items.png */
    private final int textureType;
    private final String basePath;
    private int mipmapLevels;
    private int anisotropicFiltering = 1;
    private final TextureAtlasSprite missingImage = new TextureAtlasSprite("missingno");
    private static final String __OBFID = "CL_00001058";
    private boolean skipFirst = false;

    public TextureMap(int p_i1281_1_, String p_i1281_2_)
    {
        this(p_i1281_1_, p_i1281_2_, false);
    }
    public TextureMap(int p_i1281_1_, String p_i1281_2_, boolean skipFirst)
    {
        this.textureType = p_i1281_1_;
        this.basePath = p_i1281_2_;
        this.registerIcons();
        this.skipFirst = skipFirst && ENABLE_SKIP;
    }

    private void initMissingImage()
    {
        int[] aint;

        if ((float)this.anisotropicFiltering > 1.0F)
        {
            boolean flag = true;
            boolean flag1 = true;
            boolean flag2 = true;
            this.missingImage.setIconWidth(32);
            this.missingImage.setIconHeight(32);
            aint = new int[1024];
            System.arraycopy(TextureUtil.missingTextureData, 0, aint, 0, TextureUtil.missingTextureData.length);
            TextureUtil.prepareAnisotropicData(aint, 16, 16, 8);
        }
        else
        {
            aint = TextureUtil.missingTextureData;
            this.missingImage.setIconWidth(16);
            this.missingImage.setIconHeight(16);
        }

        int[][] aint1 = new int[this.mipmapLevels + 1][];
        aint1[0] = aint;
        this.missingImage.setFramesTextureData(Lists.newArrayList(new int[][][] {aint1}));
    }

    public void loadTexture(IResourceManager p_110551_1_) throws IOException
    {
        this.initMissingImage();
        this.deleteGlTexture();
        this.loadTextureAtlas(p_110551_1_);
    }

    public void loadTextureAtlas(IResourceManager p_110571_1_)
    {
        registerIcons(); //Re-gather list of Icons, allows for addition/removal of blocks/items after this map was initially constructed.

        int i = Minecraft.getGLMaximumTextureSize();
        Stitcher stitcher = new Stitcher(i, i, true, 0, this.mipmapLevels);
        this.mapUploadedSprites.clear();
        this.listAnimatedSprites.clear();
        int j = Integer.MAX_VALUE;
        ForgeHooksClient.onTextureStitchedPre(this);
        cpw.mods.fml.common.ProgressManager.ProgressBar bar = cpw.mods.fml.common.ProgressManager.push("Texture Loading", skipFirst ? 0 : this.mapRegisteredSprites.size());
        Iterator iterator = this.mapRegisteredSprites.entrySet().iterator();
        TextureAtlasSprite textureatlassprite;

        while (!skipFirst && iterator.hasNext())
        {
            Entry entry = (Entry)iterator.next();
            ResourceLocation resourcelocation = new ResourceLocation((String)entry.getKey());
            textureatlassprite = (TextureAtlasSprite)entry.getValue();
            ResourceLocation resourcelocation1 = this.completeResourceLocation(resourcelocation, 0);
            bar.step(resourcelocation1.getResourcePath());

            if (textureatlassprite.hasCustomLoader(p_110571_1_, resourcelocation))
            {
                if (!textureatlassprite.load(p_110571_1_, resourcelocation))
                {
                    j = Math.min(j, Math.min(textureatlassprite.getIconWidth(), textureatlassprite.getIconHeight()));
                    stitcher.addSprite(textureatlassprite);
                }
                continue;
            }

            try
            {
                IResource iresource = p_110571_1_.getResource(resourcelocation1);
                BufferedImage[] abufferedimage = new BufferedImage[1 + this.mipmapLevels];
                abufferedimage[0] = ImageIO.read(iresource.getInputStream());
                TextureMetadataSection texturemetadatasection = (TextureMetadataSection)iresource.getMetadata("texture");

                if (texturemetadatasection != null)
                {
                    List list = texturemetadatasection.getListMipmaps();
                    int l;

                    if (!list.isEmpty())
                    {
                        int k = abufferedimage[0].getWidth();
                        l = abufferedimage[0].getHeight();

                        if (MathHelper.roundUpToPowerOfTwo(k) != k || MathHelper.roundUpToPowerOfTwo(l) != l)
                        {
                            throw new RuntimeException("Unable to load extra miplevels, source-texture is not power of two");
                        }
                    }

                    Iterator iterator3 = list.iterator();

                    while (iterator3.hasNext())
                    {
                        l = ((Integer)iterator3.next()).intValue();

                        if (l > 0 && l < abufferedimage.length - 1 && abufferedimage[l] == null)
                        {
                            ResourceLocation resourcelocation2 = this.completeResourceLocation(resourcelocation, l);

                            try
                            {
                                abufferedimage[l] = ImageIO.read(p_110571_1_.getResource(resourcelocation2).getInputStream());
                            }
                            catch (IOException ioexception)
                            {
                                logger.error("Unable to load miplevel {} from: {}", new Object[] {Integer.valueOf(l), resourcelocation2, ioexception});
                            }
                        }
                    }
                }

                AnimationMetadataSection animationmetadatasection = (AnimationMetadataSection)iresource.getMetadata("animation");
                textureatlassprite.loadSprite(abufferedimage, animationmetadatasection, (float)this.anisotropicFiltering > 1.0F);
            }
            catch (RuntimeException runtimeexception)
            {
                //logger.error("Unable to parse metadata from " + resourcelocation1, runtimeexception);
                cpw.mods.fml.client.FMLClientHandler.instance().trackBrokenTexture(resourcelocation1, runtimeexception.getMessage());
                continue;
            }
            catch (IOException ioexception1)
            {
                //logger.error("Using missing texture, unable to load " + resourcelocation1, ioexception1);
                cpw.mods.fml.client.FMLClientHandler.instance().trackMissingTexture(resourcelocation1);
                continue;
            }

            j = Math.min(j, Math.min(textureatlassprite.getIconWidth(), textureatlassprite.getIconHeight()));
            stitcher.addSprite(textureatlassprite);
        }

        cpw.mods.fml.common.ProgressManager.pop(bar);
        int i1 = MathHelper.calculateLogBaseTwo(j);

        if (i1 < this.mipmapLevels)
        {
            logger.debug("{}: dropping miplevel from {} to {}, because of minTexel: {}", new Object[] {this.basePath, Integer.valueOf(this.mipmapLevels), Integer.valueOf(i1), Integer.valueOf(j)});
            this.mipmapLevels = i1;
        }

        Iterator iterator1 = this.mapRegisteredSprites.values().iterator();
        bar = cpw.mods.fml.common.ProgressManager.push("Mipmap generation", skipFirst ? 0 : this.mapRegisteredSprites.size());

        while (!skipFirst && iterator1.hasNext())
        {
            final TextureAtlasSprite textureatlassprite1 = (TextureAtlasSprite)iterator1.next();
            bar.step(textureatlassprite1.getIconName());

            try
            {
                textureatlassprite1.generateMipmaps(this.mipmapLevels);
            }
            catch (Throwable throwable1)
            {
                CrashReport crashreport = CrashReport.makeCrashReport(throwable1, "Applying mipmap");
                CrashReportCategory crashreportcategory = crashreport.makeCategory("Sprite being mipmapped");
                crashreportcategory.addCrashSectionCallable("Sprite name", new Callable()
                {
                    private static final String __OBFID = "CL_00001059";
                    public String call()
                    {
                        return textureatlassprite1.getIconName();
                    }
                });
                crashreportcategory.addCrashSectionCallable("Sprite size", new Callable()
                {
                    private static final String __OBFID = "CL_00001060";
                    public String call()
                    {
                        return textureatlassprite1.getIconWidth() + " x " + textureatlassprite1.getIconHeight();
                    }
                });
                crashreportcategory.addCrashSectionCallable("Sprite frames", new Callable()
                {
                    private static final String __OBFID = "CL_00001061";
                    public String call()
                    {
                        return textureatlassprite1.getFrameCount() + " frames";
                    }
                });
                crashreportcategory.addCrashSection("Mipmap levels", Integer.valueOf(this.mipmapLevels));
                throw new ReportedException(crashreport);
            }
        }

        this.missingImage.generateMipmaps(this.mipmapLevels);
        stitcher.addSprite(this.missingImage);
        cpw.mods.fml.common.ProgressManager.pop(bar);
        skipFirst = false;
        bar = cpw.mods.fml.common.ProgressManager.push("Texture creation", 3);

        try
        {
            bar.step("Stitching");
            stitcher.doStitch();
        }
        catch (StitcherException stitcherexception)
        {
            throw stitcherexception;
        }

        logger.info("Created: {}x{} {}-atlas", new Object[] {Integer.valueOf(stitcher.getCurrentWidth()), Integer.valueOf(stitcher.getCurrentHeight()), this.basePath});
        bar.step("Allocating GL texture");
        TextureUtil.allocateTextureImpl(this.getGlTextureId(), this.mipmapLevels, stitcher.getCurrentWidth(), stitcher.getCurrentHeight(), (float)this.anisotropicFiltering);
        HashMap hashmap = Maps.newHashMap(this.mapRegisteredSprites);
        Iterator iterator2 = stitcher.getStichSlots().iterator();

        bar.step("Uploading GL texture");
        while (iterator2.hasNext())
        {
            textureatlassprite = (TextureAtlasSprite)iterator2.next();
            String s = textureatlassprite.getIconName();
            hashmap.remove(s);
            this.mapUploadedSprites.put(s, textureatlassprite);

            try
            {
                TextureUtil.uploadTextureMipmap(textureatlassprite.getFrameTextureData(0), textureatlassprite.getIconWidth(), textureatlassprite.getIconHeight(), textureatlassprite.getOriginX(), textureatlassprite.getOriginY(), false, false);
            }
            catch (Throwable throwable)
            {
                CrashReport crashreport1 = CrashReport.makeCrashReport(throwable, "Stitching texture atlas");
                CrashReportCategory crashreportcategory1 = crashreport1.makeCategory("Texture being stitched together");
                crashreportcategory1.addCrashSection("Atlas path", this.basePath);
                crashreportcategory1.addCrashSection("Sprite", textureatlassprite);
                throw new ReportedException(crashreport1);
            }

            if (textureatlassprite.hasAnimationMetadata())
            {
                this.listAnimatedSprites.add(textureatlassprite);
            }
            else
            {
                textureatlassprite.clearFramesTextureData();
            }
        }

        iterator2 = hashmap.values().iterator();

        while (iterator2.hasNext())
        {
            textureatlassprite = (TextureAtlasSprite)iterator2.next();
            textureatlassprite.copyFrom(this.missingImage);
        }
        ForgeHooksClient.onTextureStitchedPost(this);
        cpw.mods.fml.common.ProgressManager.pop(bar);
    }

    private ResourceLocation completeResourceLocation(ResourceLocation p_147634_1_, int p_147634_2_)
    {
        return p_147634_2_ == 0 ? new ResourceLocation(p_147634_1_.getResourceDomain(), String.format("%s/%s%s", new Object[] {this.basePath, p_147634_1_.getResourcePath(), ".png"})): new ResourceLocation(p_147634_1_.getResourceDomain(), String.format("%s/mipmaps/%s.%d%s", new Object[] {this.basePath, p_147634_1_.getResourcePath(), Integer.valueOf(p_147634_2_), ".png"}));
    }

    private void registerIcons()
    {
        this.mapRegisteredSprites.clear();
        Iterator iterator;

        if (this.textureType == 0)
        {
            iterator = Block.blockRegistry.iterator();

            while (iterator.hasNext())
            {
                Block block = (Block)iterator.next();

                if (block.getMaterial() != Material.air)
                {
                    block.registerBlockIcons(this);
                }
            }

            Minecraft.getMinecraft().renderGlobal.registerDestroyBlockIcons(this);
            RenderManager.instance.updateIcons(this);
        }

        iterator = Item.itemRegistry.iterator();

        while (iterator.hasNext())
        {
            Item item = (Item)iterator.next();

            if (item != null && item.getSpriteNumber() == this.textureType)
            {
                item.registerIcons(this);
            }
        }
    }

    public TextureAtlasSprite getAtlasSprite(String p_110572_1_)
    {
        TextureAtlasSprite textureatlassprite = (TextureAtlasSprite)this.mapUploadedSprites.get(p_110572_1_);

        if (textureatlassprite == null)
        {
            textureatlassprite = this.missingImage;
        }

        return textureatlassprite;
    }

    public void updateAnimations()
    {
        TextureUtil.bindTexture(this.getGlTextureId());
        Iterator iterator = this.listAnimatedSprites.iterator();

        while (iterator.hasNext())
        {
            TextureAtlasSprite textureatlassprite = (TextureAtlasSprite)iterator.next();
            textureatlassprite.updateAnimation();
        }
    }

    public IIcon registerIcon(String p_94245_1_)
    {
        if (p_94245_1_ == null)
        {
            throw new IllegalArgumentException("Name cannot be null!");
        }
        else if (p_94245_1_.indexOf(92) == -1) // Disable backslashes (\) in texture asset paths.
        {
            Object object = (TextureAtlasSprite)this.mapRegisteredSprites.get(p_94245_1_);

            if (object == null)
            {
                if (this.textureType == 1)
                {
                    if ("clock".equals(p_94245_1_))
                    {
                        object = new TextureClock(p_94245_1_);
                    }
                    else if ("compass".equals(p_94245_1_))
                    {
                        object = new TextureCompass(p_94245_1_);
                    }
                    else
                    {
                        object = new TextureAtlasSprite(p_94245_1_);
                    }
                }
                else
                {
                    object = new TextureAtlasSprite(p_94245_1_);
                }

                this.mapRegisteredSprites.put(p_94245_1_, object);
            }

            return (IIcon)object;
        }
        else
        {
            throw new IllegalArgumentException("Name cannot contain slashes!");
        }
    }

    public int getTextureType()
    {
        return this.textureType;
    }

    public void tick()
    {
        this.updateAnimations();
    }

    public void setMipmapLevels(int p_147633_1_)
    {
        this.mipmapLevels = p_147633_1_;
    }

    public void setAnisotropicFiltering(int p_147632_1_)
    {
        this.anisotropicFiltering = p_147632_1_;
    }

    //===================================================================================================
    //                                           Forge Start
    //===================================================================================================
    /**
     * Grabs the registered entry for the specified name, returning null if there was not a entry.
     * Opposed to registerIcon, this will not instantiate the entry, useful to test if a mapping exists.
     *
     * @param name The name of the entry to find
     * @return The registered entry, null if nothing was registered.
     */
    public TextureAtlasSprite getTextureExtry(String name)
    {
        return (TextureAtlasSprite)mapRegisteredSprites.get(name);
    }

    /**
     * Adds a texture registry entry to this map for the specified name if one does not already exist.
     * Returns false if the map already contains a entry for the specified name.
     *
     * @param name Entry name
     * @param entry Entry instance
     * @return True if the entry was added to the map, false otherwise.
     */
    public boolean setTextureEntry(String name, TextureAtlasSprite entry)
    {
        if (!mapRegisteredSprites.containsKey(name))
        {
            mapRegisteredSprites.put(name, entry);
            return true;
        }
        return false;
    }
}