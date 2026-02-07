package net.minecraft.client.gui.achievement;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Random;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiButton;
import net.minecraft.client.gui.GuiOptionButton;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.IProgressMeter;
import net.minecraft.client.renderer.RenderHelper;
import net.minecraft.client.renderer.entity.RenderItem;
import net.minecraft.client.renderer.texture.TextureMap;
import net.minecraft.client.resources.I18n;
import net.minecraft.init.Blocks;
import net.minecraft.network.play.client.C16PacketClientStatus;
import net.minecraft.stats.Achievement;
import net.minecraft.stats.AchievementList;
import net.minecraft.stats.StatFileWriter;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ResourceLocation;
import org.lwjgl.input.Mouse;
import org.lwjgl.opengl.GL11;
import org.lwjgl.opengl.GL12;
import java.util.LinkedList;
import java.util.List;
import net.minecraftforge.common.AchievementPage;

@SideOnly(Side.CLIENT)
public class GuiAchievements extends GuiScreen implements IProgressMeter
{
    private static final int field_146572_y = AchievementList.minDisplayColumn * 24 - 112;
    private static final int field_146571_z = AchievementList.minDisplayRow * 24 - 112;
    private static final int field_146559_A = AchievementList.maxDisplayColumn * 24 - 77;
    private static final int field_146560_B = AchievementList.maxDisplayRow * 24 - 77;
    private static final ResourceLocation field_146561_C = new ResourceLocation("textures/gui/achievement/achievement_background.png");
    protected GuiScreen field_146562_a;
    protected int field_146555_f = 256;
    protected int field_146557_g = 202;
    protected int field_146563_h;
    protected int field_146564_i;
    protected float field_146570_r = 1.0F;
    protected double field_146569_s;
    protected double field_146568_t;
    protected double field_146567_u;
    protected double field_146566_v;
    protected double field_146565_w;
    protected double field_146573_x;
    private int field_146554_D;
    private StatFileWriter field_146556_E;
    private boolean field_146558_F = true;
    private static final String __OBFID = "CL_00000722";

    private int currentPage = -1;
    private GuiButton button;
    private LinkedList<Achievement> minecraftAchievements = new LinkedList<Achievement>();

    public GuiAchievements(GuiScreen p_i45026_1_, StatFileWriter p_i45026_2_)
    {
        this.field_146562_a = p_i45026_1_;
        this.field_146556_E = p_i45026_2_;
        short short1 = 141;
        short short2 = 141;
        this.field_146569_s = this.field_146567_u = this.field_146565_w = (double)(AchievementList.openInventory.displayColumn * 24 - short1 / 2 - 12);
        this.field_146568_t = this.field_146566_v = this.field_146573_x = (double)(AchievementList.openInventory.displayRow * 24 - short2 / 2);
        minecraftAchievements.clear();
        for (Object achievement : AchievementList.achievementList)
        {
            if (!AchievementPage.isAchievementInPages((Achievement)achievement))
            {
                minecraftAchievements.add((Achievement)achievement);
            }
        }
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        this.mc.getNetHandler().addToSendQueue(new C16PacketClientStatus(C16PacketClientStatus.EnumState.REQUEST_STATS));
        this.buttonList.clear();
        this.buttonList.add(new GuiOptionButton(1, this.width / 2 + 24, this.height / 2 + 74, 80, 20, I18n.format("gui.done", new Object[0])));
        this.buttonList.add(button = new GuiButton(2, (width - field_146555_f) / 2 + 24, height / 2 + 74, 125, 20, AchievementPage.getTitle(currentPage)));
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (!this.field_146558_F)
        {
            if (p_146284_1_.id == 1)
            {
                this.mc.displayGuiScreen(this.field_146562_a);
            }

            if (p_146284_1_.id == 2)
            {
                currentPage++;
                if (currentPage >= AchievementPage.getAchievementPages().size())
                {
                    currentPage = -1;
                }
                button.displayString = AchievementPage.getTitle(currentPage);
            }
        }
    }

    /**
     * Fired when a key is typed. This is the equivalent of KeyListener.keyTyped(KeyEvent e).
     */
    protected void keyTyped(char p_73869_1_, int p_73869_2_)
    {
        if (p_73869_2_ == this.mc.gameSettings.keyBindInventory.getKeyCode())
        {
            this.mc.displayGuiScreen((GuiScreen)null);
            this.mc.setIngameFocus();
        }
        else
        {
            super.keyTyped(p_73869_1_, p_73869_2_);
        }
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        if (this.field_146558_F)
        {
            this.drawDefaultBackground();
            this.drawCenteredString(this.fontRendererObj, I18n.format("multiplayer.downloadingStats", new Object[0]), this.width / 2, this.height / 2, 16777215);
            this.drawCenteredString(this.fontRendererObj, field_146510_b_[(int)(Minecraft.getSystemTime() / 150L % (long)field_146510_b_.length)], this.width / 2, this.height / 2 + this.fontRendererObj.FONT_HEIGHT * 2, 16777215);
        }
        else
        {
            int k;

            if (Mouse.isButtonDown(0))
            {
                k = (this.width - this.field_146555_f) / 2;
                int l = (this.height - this.field_146557_g) / 2;
                int i1 = k + 8;
                int j1 = l + 17;

                if ((this.field_146554_D == 0 || this.field_146554_D == 1) && p_73863_1_ >= i1 && p_73863_1_ < i1 + 224 && p_73863_2_ >= j1 && p_73863_2_ < j1 + 155)
                {
                    if (this.field_146554_D == 0)
                    {
                        this.field_146554_D = 1;
                    }
                    else
                    {
                        this.field_146567_u -= (double)((float)(p_73863_1_ - this.field_146563_h) * this.field_146570_r);
                        this.field_146566_v -= (double)((float)(p_73863_2_ - this.field_146564_i) * this.field_146570_r);
                        this.field_146565_w = this.field_146569_s = this.field_146567_u;
                        this.field_146573_x = this.field_146568_t = this.field_146566_v;
                    }

                    this.field_146563_h = p_73863_1_;
                    this.field_146564_i = p_73863_2_;
                }
            }
            else
            {
                this.field_146554_D = 0;
            }

            k = Mouse.getDWheel();
            float f4 = this.field_146570_r;

            if (k < 0)
            {
                this.field_146570_r += 0.25F;
            }
            else if (k > 0)
            {
                this.field_146570_r -= 0.25F;
            }

            this.field_146570_r = MathHelper.clamp_float(this.field_146570_r, 1.0F, 2.0F);

            if (this.field_146570_r != f4)
            {
                float f6 = f4 - this.field_146570_r;
                float f5 = f4 * (float)this.field_146555_f;
                float f1 = f4 * (float)this.field_146557_g;
                float f2 = this.field_146570_r * (float)this.field_146555_f;
                float f3 = this.field_146570_r * (float)this.field_146557_g;
                this.field_146567_u -= (double)((f2 - f5) * 0.5F);
                this.field_146566_v -= (double)((f3 - f1) * 0.5F);
                this.field_146565_w = this.field_146569_s = this.field_146567_u;
                this.field_146573_x = this.field_146568_t = this.field_146566_v;
            }

            if (this.field_146565_w < (double)field_146572_y)
            {
                this.field_146565_w = (double)field_146572_y;
            }

            if (this.field_146573_x < (double)field_146571_z)
            {
                this.field_146573_x = (double)field_146571_z;
            }

            if (this.field_146565_w >= (double)field_146559_A)
            {
                this.field_146565_w = (double)(field_146559_A - 1);
            }

            if (this.field_146573_x >= (double)field_146560_B)
            {
                this.field_146573_x = (double)(field_146560_B - 1);
            }

            this.drawDefaultBackground();
            this.func_146552_b(p_73863_1_, p_73863_2_, p_73863_3_);
            GL11.glDisable(GL11.GL_LIGHTING);
            GL11.glDisable(GL11.GL_DEPTH_TEST);
            this.func_146553_h();
            GL11.glEnable(GL11.GL_LIGHTING);
            GL11.glEnable(GL11.GL_DEPTH_TEST);
        }
    }

    public void func_146509_g()
    {
        if (this.field_146558_F)
        {
            this.field_146558_F = false;
        }
    }

    /**
     * Called from the main game loop to update the screen.
     */
    public void updateScreen()
    {
        if (!this.field_146558_F)
        {
            this.field_146569_s = this.field_146567_u;
            this.field_146568_t = this.field_146566_v;
            double d0 = this.field_146565_w - this.field_146567_u;
            double d1 = this.field_146573_x - this.field_146566_v;

            if (d0 * d0 + d1 * d1 < 4.0D)
            {
                this.field_146567_u += d0;
                this.field_146566_v += d1;
            }
            else
            {
                this.field_146567_u += d0 * 0.85D;
                this.field_146566_v += d1 * 0.85D;
            }
        }
    }

    protected void func_146553_h()
    {
        int i = (this.width - this.field_146555_f) / 2;
        int j = (this.height - this.field_146557_g) / 2;
        this.fontRendererObj.drawString(I18n.format("gui.achievements", new Object[0]), i + 15, j + 5, 4210752);
    }

    protected void func_146552_b(int p_146552_1_, int p_146552_2_, float p_146552_3_)
    {
        int k = MathHelper.floor_double(this.field_146569_s + (this.field_146567_u - this.field_146569_s) * (double)p_146552_3_);
        int l = MathHelper.floor_double(this.field_146568_t + (this.field_146566_v - this.field_146568_t) * (double)p_146552_3_);

        if (k < field_146572_y)
        {
            k = field_146572_y;
        }

        if (l < field_146571_z)
        {
            l = field_146571_z;
        }

        if (k >= field_146559_A)
        {
            k = field_146559_A - 1;
        }

        if (l >= field_146560_B)
        {
            l = field_146560_B - 1;
        }

        int i1 = (this.width - this.field_146555_f) / 2;
        int j1 = (this.height - this.field_146557_g) / 2;
        int k1 = i1 + 16;
        int l1 = j1 + 17;
        this.zLevel = 0.0F;
        GL11.glDepthFunc(GL11.GL_GEQUAL);
        GL11.glPushMatrix();
        GL11.glTranslatef((float)k1, (float)l1, -200.0F);
        // FIXES models rendering weirdly in the acheivements pane
        // see https://github.com/MinecraftForge/MinecraftForge/commit/1b7ce7592caafb760ec93066184182ae0711e793#commitcomment-10512284
        GL11.glScalef(1.0F / this.field_146570_r, 1.0F / this.field_146570_r, 1.0F);
        GL11.glEnable(GL11.GL_TEXTURE_2D);
        GL11.glDisable(GL11.GL_LIGHTING);
        GL11.glEnable(GL12.GL_RESCALE_NORMAL);
        GL11.glEnable(GL11.GL_COLOR_MATERIAL);
        int i2 = k + 288 >> 4;
        int j2 = l + 288 >> 4;
        int k2 = (k + 288) % 16;
        int l2 = (l + 288) % 16;
        boolean flag = true;
        boolean flag1 = true;
        boolean flag2 = true;
        boolean flag3 = true;
        boolean flag4 = true;
        Random random = new Random();
        float f1 = 16.0F / this.field_146570_r;
        float f2 = 16.0F / this.field_146570_r;
        int i3;
        int j3;
        int k3;

        for (i3 = 0; (float)i3 * f1 - (float)l2 < 155.0F; ++i3)
        {
            float f3 = 0.6F - (float)(j2 + i3) / 25.0F * 0.3F;
            GL11.glColor4f(f3, f3, f3, 1.0F);

            for (j3 = 0; (float)j3 * f2 - (float)k2 < 224.0F; ++j3)
            {
                random.setSeed((long)(this.mc.getSession().getPlayerID().hashCode() + i2 + j3 + (j2 + i3) * 16));
                k3 = random.nextInt(1 + j2 + i3) + (j2 + i3) / 2;
                IIcon iicon = Blocks.sand.getIcon(0, 0);

                if (k3 <= 37 && j2 + i3 != 35)
                {
                    if (k3 == 22)
                    {
                        if (random.nextInt(2) == 0)
                        {
                            iicon = Blocks.diamond_ore.getIcon(0, 0);
                        }
                        else
                        {
                            iicon = Blocks.redstone_ore.getIcon(0, 0);
                        }
                    }
                    else if (k3 == 10)
                    {
                        iicon = Blocks.iron_ore.getIcon(0, 0);
                    }
                    else if (k3 == 8)
                    {
                        iicon = Blocks.coal_ore.getIcon(0, 0);
                    }
                    else if (k3 > 4)
                    {
                        iicon = Blocks.stone.getIcon(0, 0);
                    }
                    else if (k3 > 0)
                    {
                        iicon = Blocks.dirt.getIcon(0, 0);
                    }
                }
                else
                {
                    iicon = Blocks.bedrock.getIcon(0, 0);
                }

                this.mc.getTextureManager().bindTexture(TextureMap.locationBlocksTexture);
                this.drawTexturedModelRectFromIcon(j3 * 16 - k2, i3 * 16 - l2, iicon, 16, 16);
            }
        }

        GL11.glEnable(GL11.GL_DEPTH_TEST);
        GL11.glDepthFunc(GL11.GL_LEQUAL);
        this.mc.getTextureManager().bindTexture(field_146561_C);
        int i4;
        int j4;
        int l4;

        List<Achievement> achievementList = (currentPage == -1 ? minecraftAchievements : AchievementPage.getAchievementPage(currentPage).getAchievements());
        for (i3 = 0; i3 < achievementList.size(); ++i3)
        {
            Achievement achievement1 = achievementList.get(i3);

            if (achievement1.parentAchievement != null && achievementList.contains(achievement1.parentAchievement))
            {
                j3 = achievement1.displayColumn * 24 - k + 11;
                k3 = achievement1.displayRow * 24 - l + 11;
                l4 = achievement1.parentAchievement.displayColumn * 24 - k + 11;
                int l3 = achievement1.parentAchievement.displayRow * 24 - l + 11;
                boolean flag5 = this.field_146556_E.hasAchievementUnlocked(achievement1);
                boolean flag6 = this.field_146556_E.canUnlockAchievement(achievement1);
                i4 = this.field_146556_E.func_150874_c(achievement1);

                if (i4 <= 4)
                {
                    j4 = -16777216;

                    if (flag5)
                    {
                        j4 = -6250336;
                    }
                    else if (flag6)
                    {
                        j4 = -16711936;
                    }

                    this.drawHorizontalLine(j3, l4, k3, j4);
                    this.drawVerticalLine(l4, k3, l3, j4);

                    if (j3 > l4)
                    {
                        this.drawTexturedModalRect(j3 - 11 - 7, k3 - 5, 114, 234, 7, 11);
                    }
                    else if (j3 < l4)
                    {
                        this.drawTexturedModalRect(j3 + 11, k3 - 5, 107, 234, 7, 11);
                    }
                    else if (k3 > l3)
                    {
                        this.drawTexturedModalRect(j3 - 5, k3 - 11 - 7, 96, 234, 11, 7);
                    }
                    else if (k3 < l3)
                    {
                        this.drawTexturedModalRect(j3 - 5, k3 + 11, 96, 241, 11, 7);
                    }
                }
            }
        }

        Achievement achievement = null;
        RenderItem renderitem = new RenderItem();
        float f4 = (float)(p_146552_1_ - k1) * this.field_146570_r;
        float f5 = (float)(p_146552_2_ - l1) * this.field_146570_r;
        RenderHelper.enableGUIStandardItemLighting();
        GL11.glDisable(GL11.GL_LIGHTING);
        GL11.glEnable(GL12.GL_RESCALE_NORMAL);
        GL11.glEnable(GL11.GL_COLOR_MATERIAL);
        int i5;
        int j5;

        for (l4 = 0; l4 < achievementList.size(); ++l4)
        {
            Achievement achievement2 = (Achievement)achievementList.get(l4);
            i5 = achievement2.displayColumn * 24 - k;
            j5 = achievement2.displayRow * 24 - l;

            if (i5 >= -24 && j5 >= -24 && (float)i5 <= 224.0F * this.field_146570_r && (float)j5 <= 155.0F * this.field_146570_r)
            {
                i4 = this.field_146556_E.func_150874_c(achievement2);
                float f6;

                if (this.field_146556_E.hasAchievementUnlocked(achievement2))
                {
                    f6 = 0.75F;
                    GL11.glColor4f(f6, f6, f6, 1.0F);
                }
                else if (this.field_146556_E.canUnlockAchievement(achievement2))
                {
                    f6 = 1.0F;
                    GL11.glColor4f(f6, f6, f6, 1.0F);
                }
                else if (i4 < 3)
                {
                    f6 = 0.3F;
                    GL11.glColor4f(f6, f6, f6, 1.0F);
                }
                else if (i4 == 3)
                {
                    f6 = 0.2F;
                    GL11.glColor4f(f6, f6, f6, 1.0F);
                }
                else
                {
                    if (i4 != 4)
                    {
                        continue;
                    }

                    f6 = 0.1F;
                    GL11.glColor4f(f6, f6, f6, 1.0F);
                }

                this.mc.getTextureManager().bindTexture(field_146561_C);

                GL11.glEnable(GL11.GL_BLEND);// Forge: Specifically enable blend because it is needed here. And we fix Generic RenderItem's leakage of it.
                if (achievement2.getSpecial())
                {
                    this.drawTexturedModalRect(i5 - 2, j5 - 2, 26, 202, 26, 26);
                }
                else
                {
                    this.drawTexturedModalRect(i5 - 2, j5 - 2, 0, 202, 26, 26);
                }
                GL11.glDisable(GL11.GL_BLEND); //Forge: Cleanup states we set.

                if (!this.field_146556_E.canUnlockAchievement(achievement2))
                {
                    f6 = 0.1F;
                    GL11.glColor4f(f6, f6, f6, 1.0F);
                    renderitem.renderWithColor = false;
                }

                GL11.glDisable(GL11.GL_LIGHTING); //Forge: Make sure Lighting is disabled. Fixes MC-33065
                GL11.glEnable(GL11.GL_CULL_FACE);
                renderitem.renderItemAndEffectIntoGUI(this.mc.fontRenderer, this.mc.getTextureManager(), achievement2.theItemStack, i5 + 3, j5 + 3);
                GL11.glBlendFunc(GL11.GL_SRC_ALPHA, GL11.GL_ONE_MINUS_SRC_ALPHA);
                GL11.glDisable(GL11.GL_LIGHTING);

                if (!this.field_146556_E.canUnlockAchievement(achievement2))
                {
                    renderitem.renderWithColor = true;
                }

                GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);

                if (f4 >= (float)i5 && f4 <= (float)(i5 + 22) && f5 >= (float)j5 && f5 <= (float)(j5 + 22))
                {
                    achievement = achievement2;
                }
            }
        }

        GL11.glDisable(GL11.GL_DEPTH_TEST);
        GL11.glEnable(GL11.GL_BLEND);
        GL11.glPopMatrix();
        GL11.glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
        this.mc.getTextureManager().bindTexture(field_146561_C);
        this.drawTexturedModalRect(i1, j1, 0, 0, this.field_146555_f, this.field_146557_g);
        this.zLevel = 0.0F;
        GL11.glDepthFunc(GL11.GL_LEQUAL);
        GL11.glDisable(GL11.GL_DEPTH_TEST);
        GL11.glEnable(GL11.GL_TEXTURE_2D);
        super.drawScreen(p_146552_1_, p_146552_2_, p_146552_3_);

        if (achievement != null)
        {
            String s1 = achievement.func_150951_e().getUnformattedText();
            String s2 = achievement.getDescription();
            i5 = p_146552_1_ + 12;
            j5 = p_146552_2_ - 4;
            i4 = this.field_146556_E.func_150874_c(achievement);

            if (!this.field_146556_E.canUnlockAchievement(achievement))
            {
                String s;
                int k4;

                if (i4 == 3)
                {
                    s1 = I18n.format("achievement.unknown", new Object[0]);
                    j4 = Math.max(this.fontRendererObj.getStringWidth(s1), 120);
                    s = (new ChatComponentTranslation("achievement.requires", new Object[] {achievement.parentAchievement.func_150951_e()})).getUnformattedText();
                    k4 = this.fontRendererObj.splitStringWidth(s, j4);
                    this.drawGradientRect(i5 - 3, j5 - 3, i5 + j4 + 3, j5 + k4 + 12 + 3, -1073741824, -1073741824);
                    this.fontRendererObj.drawSplitString(s, i5, j5 + 12, j4, -9416624);
                }
                else if (i4 < 3)
                {
                    j4 = Math.max(this.fontRendererObj.getStringWidth(s1), 120);
                    s = (new ChatComponentTranslation("achievement.requires", new Object[] {achievement.parentAchievement.func_150951_e()})).getUnformattedText();
                    k4 = this.fontRendererObj.splitStringWidth(s, j4);
                    this.drawGradientRect(i5 - 3, j5 - 3, i5 + j4 + 3, j5 + k4 + 12 + 3, -1073741824, -1073741824);
                    this.fontRendererObj.drawSplitString(s, i5, j5 + 12, j4, -9416624);
                }
                else
                {
                    s1 = null;
                }
            }
            else
            {
                j4 = Math.max(this.fontRendererObj.getStringWidth(s1), 120);
                int k5 = this.fontRendererObj.splitStringWidth(s2, j4);

                if (this.field_146556_E.hasAchievementUnlocked(achievement))
                {
                    k5 += 12;
                }

                this.drawGradientRect(i5 - 3, j5 - 3, i5 + j4 + 3, j5 + k5 + 3 + 12, -1073741824, -1073741824);
                this.fontRendererObj.drawSplitString(s2, i5, j5 + 12, j4, -6250336);

                if (this.field_146556_E.hasAchievementUnlocked(achievement))
                {
                    this.fontRendererObj.drawStringWithShadow(I18n.format("achievement.taken", new Object[0]), i5, j5 + k5 + 4, -7302913);
                }
            }

            if (s1 != null)
            {
                this.fontRendererObj.drawStringWithShadow(s1, i5, j5, this.field_146556_E.canUnlockAchievement(achievement) ? (achievement.getSpecial() ? -128 : -1) : (achievement.getSpecial() ? -8355776 : -8355712));
            }
        }

        GL11.glEnable(GL11.GL_DEPTH_TEST);
        GL11.glEnable(GL11.GL_LIGHTING);
        RenderHelper.disableStandardItemLighting();
    }

    /**
     * Returns true if this GUI should pause the game when it is displayed in single-player
     */
    public boolean doesGuiPauseGame()
    {
        return !this.field_146558_F;
    }
}