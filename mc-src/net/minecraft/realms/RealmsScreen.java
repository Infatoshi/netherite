package net.minecraft.realms;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.AbstractClientPlayer;
import net.minecraft.client.gui.Gui;
import net.minecraft.client.gui.GuiScreenRealmsProxy;
import net.minecraft.client.resources.I18n;
import net.minecraft.item.ItemStack;
import net.minecraft.util.ResourceLocation;

@SideOnly(Side.CLIENT)
public class RealmsScreen
{
    public static final int SKIN_HEAD_U = 8;
    public static final int SKIN_HEAD_V = 8;
    public static final int SKIN_HEAD_WIDTH = 8;
    public static final int SKIN_HEAD_HEIGHT = 8;
    public static final int SKIN_TEX_WIDTH = 64;
    public static final int SKIN_TEX_HEIGHT = 32;
    public static final int SKIN_HAT_U = 40;
    public static final int SKIN_HAT_V = 8;
    public static final int SKIN_HAT_WIDTH = 8;
    public static final int SKIN_HAT_HEIGHT = 8;
    protected Minecraft minecraft;
    public int width;
    public int height;
    private GuiScreenRealmsProxy proxy = new GuiScreenRealmsProxy(this);
    private static final String __OBFID = "CL_00001898";

    public GuiScreenRealmsProxy getProxy()
    {
        return this.proxy;
    }

    public void init() {}

    public void init(Minecraft p_init_1_, int p_init_2_, int p_init_3_) {}

    public void drawCenteredString(String p_drawCenteredString_1_, int p_drawCenteredString_2_, int p_drawCenteredString_3_, int p_drawCenteredString_4_)
    {
        this.proxy.func_154325_a(p_drawCenteredString_1_, p_drawCenteredString_2_, p_drawCenteredString_3_, p_drawCenteredString_4_);
    }

    public void drawString(String p_drawString_1_, int p_drawString_2_, int p_drawString_3_, int p_drawString_4_)
    {
        this.proxy.func_154322_b(p_drawString_1_, p_drawString_2_, p_drawString_3_, p_drawString_4_);
    }

    public void blit(int p_blit_1_, int p_blit_2_, int p_blit_3_, int p_blit_4_, int p_blit_5_, int p_blit_6_)
    {
        this.proxy.drawTexturedModalRect(p_blit_1_, p_blit_2_, p_blit_3_, p_blit_4_, p_blit_5_, p_blit_6_);
    }

    public static void blit(int p_blit_0_, int p_blit_1_, float p_blit_2_, float p_blit_3_, int p_blit_4_, int p_blit_5_, int p_blit_6_, int p_blit_7_, float p_blit_8_, float p_blit_9_)
    {
        Gui.func_152125_a(p_blit_0_, p_blit_1_, p_blit_2_, p_blit_3_, p_blit_4_, p_blit_5_, p_blit_6_, p_blit_7_, p_blit_8_, p_blit_9_);
    }

    public static void blit(int p_blit_0_, int p_blit_1_, float p_blit_2_, float p_blit_3_, int p_blit_4_, int p_blit_5_, float p_blit_6_, float p_blit_7_)
    {
        Gui.func_146110_a(p_blit_0_, p_blit_1_, p_blit_2_, p_blit_3_, p_blit_4_, p_blit_5_, p_blit_6_, p_blit_7_);
    }

    public void fillGradient(int p_fillGradient_1_, int p_fillGradient_2_, int p_fillGradient_3_, int p_fillGradient_4_, int p_fillGradient_5_, int p_fillGradient_6_)
    {
        this.proxy.drawGradientRect(p_fillGradient_1_, p_fillGradient_2_, p_fillGradient_3_, p_fillGradient_4_, p_fillGradient_5_, p_fillGradient_6_);
    }

    public void renderBackground()
    {
        this.proxy.drawDefaultBackground();
    }

    public boolean isPauseScreen()
    {
        return this.proxy.doesGuiPauseGame();
    }

    public void renderBackground(int p_renderBackground_1_)
    {
        this.proxy.drawWorldBackground(p_renderBackground_1_);
    }

    public void render(int p_render_1_, int p_render_2_, float p_render_3_)
    {
        for (int k = 0; k < this.proxy.func_154320_j().size(); ++k)
        {
            ((RealmsButton)this.proxy.func_154320_j().get(k)).render(p_render_1_, p_render_2_);
        }
    }

    public void renderTooltip(ItemStack p_renderTooltip_1_, int p_renderTooltip_2_, int p_renderTooltip_3_)
    {
        this.proxy.renderToolTip(p_renderTooltip_1_, p_renderTooltip_2_, p_renderTooltip_3_);
    }

    public void renderTooltip(String p_renderTooltip_1_, int p_renderTooltip_2_, int p_renderTooltip_3_)
    {
        this.proxy.drawCreativeTabHoveringText(p_renderTooltip_1_, p_renderTooltip_2_, p_renderTooltip_3_);
    }

    public void renderTooltip(List p_renderTooltip_1_, int p_renderTooltip_2_, int p_renderTooltip_3_)
    {
        this.proxy.func_146283_a(p_renderTooltip_1_, p_renderTooltip_2_, p_renderTooltip_3_);
    }

    public static void bindFace(String p_bindFace_0_)
    {
        ResourceLocation resourcelocation = AbstractClientPlayer.getLocationSkin(p_bindFace_0_);

        if (resourcelocation == null)
        {
            resourcelocation = AbstractClientPlayer.getLocationSkin("default");
        }

        AbstractClientPlayer.getDownloadImageSkin(resourcelocation, p_bindFace_0_);
        Minecraft.getMinecraft().getTextureManager().bindTexture(resourcelocation);
    }

    public static void bind(String p_bind_0_)
    {
        ResourceLocation resourcelocation = new ResourceLocation(p_bind_0_);
        Minecraft.getMinecraft().getTextureManager().bindTexture(resourcelocation);
    }

    public void tick() {}

    public int width()
    {
        return this.proxy.width;
    }

    public int height()
    {
        return this.proxy.height;
    }

    public int fontLineHeight()
    {
        return this.proxy.func_154329_h();
    }

    public int fontWidth(String p_fontWidth_1_)
    {
        return this.proxy.func_154326_c(p_fontWidth_1_);
    }

    public void fontDrawShadow(String p_fontDrawShadow_1_, int p_fontDrawShadow_2_, int p_fontDrawShadow_3_, int p_fontDrawShadow_4_)
    {
        this.proxy.func_154319_c(p_fontDrawShadow_1_, p_fontDrawShadow_2_, p_fontDrawShadow_3_, p_fontDrawShadow_4_);
    }

    public List fontSplit(String p_fontSplit_1_, int p_fontSplit_2_)
    {
        return this.proxy.func_154323_a(p_fontSplit_1_, p_fontSplit_2_);
    }

    public void buttonClicked(RealmsButton p_buttonClicked_1_) {}

    public static RealmsButton newButton(int p_newButton_0_, int p_newButton_1_, int p_newButton_2_, String p_newButton_3_)
    {
        return new RealmsButton(p_newButton_0_, p_newButton_1_, p_newButton_2_, p_newButton_3_);
    }

    public static RealmsButton newButton(int p_newButton_0_, int p_newButton_1_, int p_newButton_2_, int p_newButton_3_, int p_newButton_4_, String p_newButton_5_)
    {
        return new RealmsButton(p_newButton_0_, p_newButton_1_, p_newButton_2_, p_newButton_3_, p_newButton_4_, p_newButton_5_);
    }

    public void buttonsClear()
    {
        this.proxy.func_154324_i();
    }

    public void buttonsAdd(RealmsButton p_buttonsAdd_1_)
    {
        this.proxy.func_154327_a(p_buttonsAdd_1_);
    }

    public List buttons()
    {
        return this.proxy.func_154320_j();
    }

    public void buttonsRemove(RealmsButton p_buttonsRemove_1_)
    {
        this.proxy.func_154328_b(p_buttonsRemove_1_);
    }

    public RealmsEditBox newEditBox(int p_newEditBox_1_, int p_newEditBox_2_, int p_newEditBox_3_, int p_newEditBox_4_)
    {
        return new RealmsEditBox(p_newEditBox_1_, p_newEditBox_2_, p_newEditBox_3_, p_newEditBox_4_);
    }

    public void mouseClicked(int p_mouseClicked_1_, int p_mouseClicked_2_, int p_mouseClicked_3_) {}

    public void mouseEvent() {}

    public void keyboardEvent() {}

    public void mouseReleased(int p_mouseReleased_1_, int p_mouseReleased_2_, int p_mouseReleased_3_) {}

    public void mouseDragged(int p_mouseDragged_1_, int p_mouseDragged_2_, int p_mouseDragged_3_, long p_mouseDragged_4_) {}

    public void keyPressed(char p_keyPressed_1_, int p_keyPressed_2_) {}

    public void confirmResult(boolean p_confirmResult_1_, int p_confirmResult_2_) {}

    public static String getLocalizedString(String p_getLocalizedString_0_)
    {
        return I18n.format(p_getLocalizedString_0_, new Object[0]);
    }

    public static String getLocalizedString(String p_getLocalizedString_0_, Object ... p_getLocalizedString_1_)
    {
        return I18n.format(p_getLocalizedString_0_, p_getLocalizedString_1_);
    }

    public RealmsAnvilLevelStorageSource getLevelStorageSource()
    {
        return new RealmsAnvilLevelStorageSource(Minecraft.getMinecraft().getSaveLoader());
    }

    public void removed() {}
}