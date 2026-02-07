package net.minecraft.client.shader;

import com.google.common.collect.Lists;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.List;
import javax.vecmath.Matrix4f;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.Tessellator;
import net.minecraft.client.resources.IResourceManager;
import net.minecraft.client.util.JsonException;
import org.lwjgl.opengl.GL11;

@SideOnly(Side.CLIENT)
public class Shader
{
    private final ShaderManager manager;
    public final Framebuffer framebufferIn;
    public final Framebuffer framebufferOut;
    private final List listAuxFramebuffers = Lists.newArrayList();
    private final List listAuxNames = Lists.newArrayList();
    private final List listAuxWidths = Lists.newArrayList();
    private final List listAuxHeights = Lists.newArrayList();
    private Matrix4f projectionMatrix;
    private static final String __OBFID = "CL_00001042";

    public Shader(IResourceManager p_i45089_1_, String p_i45089_2_, Framebuffer p_i45089_3_, Framebuffer p_i45089_4_) throws JsonException
    {
        this.manager = new ShaderManager(p_i45089_1_, p_i45089_2_);
        this.framebufferIn = p_i45089_3_;
        this.framebufferOut = p_i45089_4_;
    }

    public void deleteShader()
    {
        this.manager.func_147988_a();
    }

    public void addAuxFramebuffer(String p_148041_1_, Object p_148041_2_, int p_148041_3_, int p_148041_4_)
    {
        this.listAuxNames.add(this.listAuxNames.size(), p_148041_1_);
        this.listAuxFramebuffers.add(this.listAuxFramebuffers.size(), p_148041_2_);
        this.listAuxWidths.add(this.listAuxWidths.size(), Integer.valueOf(p_148041_3_));
        this.listAuxHeights.add(this.listAuxHeights.size(), Integer.valueOf(p_148041_4_));
    }

    private void preLoadShader()
    {
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        GL11.glDisable(GL11.GL_BLEND);
        GL11.glDisable(GL11.GL_DEPTH_TEST);
        GL11.glDisable(GL11.GL_ALPHA_TEST);
        GL11.glDisable(GL11.GL_FOG);
        GL11.glDisable(GL11.GL_LIGHTING);
        GL11.glDisable(GL11.GL_COLOR_MATERIAL);
        GL11.glEnable(GL11.GL_TEXTURE_2D);
        GL11.glBindTexture(GL11.GL_TEXTURE_2D, 0);
    }

    public void setProjectionMatrix(Matrix4f p_148045_1_)
    {
        this.projectionMatrix = p_148045_1_;
    }

    public void loadShader(float p_148042_1_)
    {
        this.preLoadShader();
        this.framebufferIn.unbindFramebuffer();
        float f1 = (float)this.framebufferOut.framebufferTextureWidth;
        float f2 = (float)this.framebufferOut.framebufferTextureHeight;
        GL11.glViewport(0, 0, (int)f1, (int)f2);
        this.manager.func_147992_a("DiffuseSampler", this.framebufferIn);

        for (int i = 0; i < this.listAuxFramebuffers.size(); ++i)
        {
            this.manager.func_147992_a((String)this.listAuxNames.get(i), this.listAuxFramebuffers.get(i));
            this.manager.func_147984_b("AuxSize" + i).func_148087_a((float)((Integer)this.listAuxWidths.get(i)).intValue(), (float)((Integer)this.listAuxHeights.get(i)).intValue());
        }

        this.manager.func_147984_b("ProjMat").func_148088_a(this.projectionMatrix);
        this.manager.func_147984_b("InSize").func_148087_a((float)this.framebufferIn.framebufferTextureWidth, (float)this.framebufferIn.framebufferTextureHeight);
        this.manager.func_147984_b("OutSize").func_148087_a(f1, f2);
        this.manager.func_147984_b("Time").func_148090_a(p_148042_1_);
        Minecraft minecraft = Minecraft.getMinecraft();
        this.manager.func_147984_b("ScreenSize").func_148087_a((float)minecraft.displayWidth, (float)minecraft.displayHeight);
        this.manager.func_147995_c();
        this.framebufferOut.framebufferClear();
        this.framebufferOut.bindFramebuffer(false);
        GL11.glDepthMask(false);
        GL11.glColorMask(true, true, true, false);
        Tessellator tessellator = Tessellator.instance;
        tessellator.startDrawingQuads();
        tessellator.setColorOpaque_I(-1);
        tessellator.addVertex(0.0D, (double)f2, 500.0D);
        tessellator.addVertex((double)f1, (double)f2, 500.0D);
        tessellator.addVertex((double)f1, 0.0D, 500.0D);
        tessellator.addVertex(0.0D, 0.0D, 500.0D);
        tessellator.draw();
        GL11.glDepthMask(true);
        GL11.glColorMask(true, true, true, true);
        this.manager.func_147993_b();
        this.framebufferOut.unbindFramebuffer();
        this.framebufferIn.unbindFramebufferTexture();
        Iterator iterator = this.listAuxFramebuffers.iterator();

        while (iterator.hasNext())
        {
            Object object = iterator.next();

            if (object instanceof Framebuffer)
            {
                ((Framebuffer)object).unbindFramebufferTexture();
            }
        }
    }

    public ShaderManager getShaderManager()
    {
        return this.manager;
    }
}