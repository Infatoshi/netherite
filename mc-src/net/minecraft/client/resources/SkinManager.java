package net.minecraft.client.resources;

import com.google.common.cache.CacheBuilder;
import com.google.common.cache.CacheLoader;
import com.google.common.cache.LoadingCache;
import com.google.common.collect.Maps;
import com.mojang.authlib.GameProfile;
import com.mojang.authlib.minecraft.InsecureTextureException;
import com.mojang.authlib.minecraft.MinecraftProfileTexture;
import com.mojang.authlib.minecraft.MinecraftSessionService;
import com.mojang.authlib.minecraft.MinecraftProfileTexture.Type;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.image.BufferedImage;
import java.io.File;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.IImageBuffer;
import net.minecraft.client.renderer.ImageBufferDownload;
import net.minecraft.client.renderer.ThreadDownloadImageData;
import net.minecraft.client.renderer.texture.ITextureObject;
import net.minecraft.client.renderer.texture.TextureManager;
import net.minecraft.util.ResourceLocation;

@SideOnly(Side.CLIENT)
public class SkinManager
{
    public static final ResourceLocation field_152793_a = new ResourceLocation("textures/entity/steve.png");
    private static final ExecutorService field_152794_b = new ThreadPoolExecutor(0, 2, 1L, TimeUnit.MINUTES, new LinkedBlockingQueue());
    private final TextureManager field_152795_c;
    private final File field_152796_d;
    private final MinecraftSessionService field_152797_e;
    private final LoadingCache field_152798_f;
    private static final String __OBFID = "CL_00001830";

    public SkinManager(TextureManager p_i1044_1_, File p_i1044_2_, MinecraftSessionService p_i1044_3_)
    {
        this.field_152795_c = p_i1044_1_;
        this.field_152796_d = p_i1044_2_;
        this.field_152797_e = p_i1044_3_;
        this.field_152798_f = CacheBuilder.newBuilder().expireAfterAccess(15L, TimeUnit.SECONDS).build(new CacheLoader()
        {
            private static final String __OBFID = "CL_00001829";
            public Map func_152786_a(GameProfile p_152786_1_)
            {
                return Minecraft.getMinecraft().func_152347_ac().getTextures(p_152786_1_, false);
            }
            public Object load(Object p_load_1_)
            {
                return this.func_152786_a((GameProfile)p_load_1_);
            }
        });
    }

    public ResourceLocation func_152792_a(MinecraftProfileTexture p_152792_1_, Type p_152792_2_)
    {
        return this.func_152789_a(p_152792_1_, p_152792_2_, (SkinManager.SkinAvailableCallback)null);
    }

    public ResourceLocation func_152789_a(MinecraftProfileTexture p_152789_1_, final Type p_152789_2_, final SkinManager.SkinAvailableCallback p_152789_3_)
    {
        final ResourceLocation resourcelocation = new ResourceLocation("skins/" + p_152789_1_.getHash());
        ITextureObject itextureobject = this.field_152795_c.getTexture(resourcelocation);

        if (itextureobject != null)
        {
            if (p_152789_3_ != null)
            {
                p_152789_3_.func_152121_a(p_152789_2_, resourcelocation);
            }
        }
        else
        {
            File file1 = new File(this.field_152796_d, p_152789_1_.getHash().substring(0, 2));
            File file2 = new File(file1, p_152789_1_.getHash());
            final ImageBufferDownload imagebufferdownload = p_152789_2_ == Type.SKIN ? new ImageBufferDownload() : null;
            ThreadDownloadImageData threaddownloadimagedata = new ThreadDownloadImageData(file2, p_152789_1_.getUrl(), field_152793_a, new IImageBuffer()
            {
                private static final String __OBFID = "CL_00001828";
                public BufferedImage parseUserSkin(BufferedImage p_78432_1_)
                {
                    if (imagebufferdownload != null)
                    {
                        p_78432_1_ = imagebufferdownload.parseUserSkin(p_78432_1_);
                    }

                    return p_78432_1_;
                }
                public void func_152634_a()
                {
                    if (imagebufferdownload != null)
                    {
                        imagebufferdownload.func_152634_a();
                    }

                    if (p_152789_3_ != null)
                    {
                        p_152789_3_.func_152121_a(p_152789_2_, resourcelocation);
                    }
                }
            });
            this.field_152795_c.loadTexture(resourcelocation, threaddownloadimagedata);
        }

        return resourcelocation;
    }

    public void func_152790_a(final GameProfile p_152790_1_, final SkinManager.SkinAvailableCallback p_152790_2_, final boolean p_152790_3_)
    {
        field_152794_b.submit(new Runnable()
        {
            private static final String __OBFID = "CL_00001827";
            public void run()
            {
                final HashMap hashmap = Maps.newHashMap();

                try
                {
                    hashmap.putAll(SkinManager.this.field_152797_e.getTextures(p_152790_1_, p_152790_3_));
                }
                catch (InsecureTextureException insecuretextureexception)
                {
                    ;
                }

                if (hashmap.isEmpty() && p_152790_1_.getId().equals(Minecraft.getMinecraft().getSession().func_148256_e().getId()))
                {
                    hashmap.putAll(SkinManager.this.field_152797_e.getTextures(SkinManager.this.field_152797_e.fillProfileProperties(p_152790_1_, false), false));
                }

                Minecraft.getMinecraft().func_152344_a(new Runnable()
                {
                    private static final String __OBFID = "CL_00001826";
                    public void run()
                    {
                        if (hashmap.containsKey(Type.SKIN))
                        {
                            SkinManager.this.func_152789_a((MinecraftProfileTexture)hashmap.get(Type.SKIN), Type.SKIN, p_152790_2_);
                        }

                        if (hashmap.containsKey(Type.CAPE))
                        {
                            SkinManager.this.func_152789_a((MinecraftProfileTexture)hashmap.get(Type.CAPE), Type.CAPE, p_152790_2_);
                        }
                    }
                });
            }
        });
    }

    public Map func_152788_a(GameProfile p_152788_1_)
    {
        return (Map)this.field_152798_f.getUnchecked(p_152788_1_);
    }

    @SideOnly(Side.CLIENT)
    public interface SkinAvailableCallback
    {
        void func_152121_a(Type p_152121_1_, ResourceLocation p_152121_2_);
    }
}