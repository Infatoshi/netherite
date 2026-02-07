package net.minecraft.client.gui;

import com.google.common.collect.Lists;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import net.minecraft.client.resources.I18n;
import net.minecraft.client.resources.ResourcePackListEntry;
import net.minecraft.client.resources.ResourcePackListEntryDefault;
import net.minecraft.client.resources.ResourcePackListEntryFound;
import net.minecraft.client.resources.ResourcePackRepository;
import net.minecraft.util.Util;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.lwjgl.Sys;

@SideOnly(Side.CLIENT)
public class GuiScreenResourcePacks extends GuiScreen
{
    private static final Logger logger = LogManager.getLogger();
    private GuiScreen field_146965_f;
    private List field_146966_g;
    private List field_146969_h;
    private GuiResourcePackAvailable field_146970_i;
    private GuiResourcePackSelected field_146967_r;
    private static final String __OBFID = "CL_00000820";

    public GuiScreenResourcePacks(GuiScreen p_i45050_1_)
    {
        this.field_146965_f = p_i45050_1_;
    }

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        this.buttonList.add(new GuiOptionButton(2, this.width / 2 - 154, this.height - 48, I18n.format("resourcePack.openFolder", new Object[0])));
        this.buttonList.add(new GuiOptionButton(1, this.width / 2 + 4, this.height - 48, I18n.format("gui.done", new Object[0])));
        this.field_146966_g = new ArrayList();
        this.field_146969_h = new ArrayList();
        ResourcePackRepository resourcepackrepository = this.mc.getResourcePackRepository();
        resourcepackrepository.updateRepositoryEntriesAll();
        ArrayList arraylist = Lists.newArrayList(resourcepackrepository.getRepositoryEntriesAll());
        arraylist.removeAll(resourcepackrepository.getRepositoryEntries());
        Iterator iterator = arraylist.iterator();
        ResourcePackRepository.Entry entry;

        while (iterator.hasNext())
        {
            entry = (ResourcePackRepository.Entry)iterator.next();
            this.field_146966_g.add(new ResourcePackListEntryFound(this, entry));
        }

        iterator = Lists.reverse(resourcepackrepository.getRepositoryEntries()).iterator();

        while (iterator.hasNext())
        {
            entry = (ResourcePackRepository.Entry)iterator.next();
            this.field_146969_h.add(new ResourcePackListEntryFound(this, entry));
        }

        this.field_146969_h.add(new ResourcePackListEntryDefault(this));
        this.field_146970_i = new GuiResourcePackAvailable(this.mc, 200, this.height, this.field_146966_g);
        this.field_146970_i.setSlotXBoundsFromLeft(this.width / 2 - 4 - 200);
        this.field_146970_i.registerScrollButtons(7, 8);
        this.field_146967_r = new GuiResourcePackSelected(this.mc, 200, this.height, this.field_146969_h);
        this.field_146967_r.setSlotXBoundsFromLeft(this.width / 2 + 4);
        this.field_146967_r.registerScrollButtons(7, 8);
    }

    public boolean func_146961_a(ResourcePackListEntry p_146961_1_)
    {
        return this.field_146969_h.contains(p_146961_1_);
    }

    public List func_146962_b(ResourcePackListEntry p_146962_1_)
    {
        return this.func_146961_a(p_146962_1_) ? this.field_146969_h : this.field_146966_g;
    }

    public List func_146964_g()
    {
        return this.field_146966_g;
    }

    public List func_146963_h()
    {
        return this.field_146969_h;
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.enabled)
        {
            if (p_146284_1_.id == 2)
            {
                File file1 = this.mc.getResourcePackRepository().getDirResourcepacks();
                String s = file1.getAbsolutePath();

                if (Util.getOSType() == Util.EnumOS.OSX)
                {
                    try
                    {
                        logger.info(s);
                        Runtime.getRuntime().exec(new String[] {"/usr/bin/open", s});
                        return;
                    }
                    catch (IOException ioexception1)
                    {
                        logger.error("Couldn\'t open file", ioexception1);
                    }
                }
                else if (Util.getOSType() == Util.EnumOS.WINDOWS)
                {
                    String s1 = String.format("cmd.exe /C start \"Open file\" \"%s\"", new Object[] {s});

                    try
                    {
                        Runtime.getRuntime().exec(s1);
                        return;
                    }
                    catch (IOException ioexception)
                    {
                        logger.error("Couldn\'t open file", ioexception);
                    }
                }

                boolean flag = false;

                try
                {
                    Class oclass = Class.forName("java.awt.Desktop");
                    Object object = oclass.getMethod("getDesktop", new Class[0]).invoke((Object)null, new Object[0]);
                    oclass.getMethod("browse", new Class[] {URI.class}).invoke(object, new Object[] {file1.toURI()});
                }
                catch (Throwable throwable)
                {
                    logger.error("Couldn\'t open link", throwable);
                    flag = true;
                }

                if (flag)
                {
                    logger.info("Opening via system class!");
                    Sys.openURL("file://" + s);
                }
            }
            else if (p_146284_1_.id == 1)
            {
                ArrayList arraylist = Lists.newArrayList();
                Iterator iterator = this.field_146969_h.iterator();

                while (iterator.hasNext())
                {
                    ResourcePackListEntry resourcepacklistentry = (ResourcePackListEntry)iterator.next();

                    if (resourcepacklistentry instanceof ResourcePackListEntryFound)
                    {
                        arraylist.add(((ResourcePackListEntryFound)resourcepacklistentry).func_148318_i());
                    }
                }

                Collections.reverse(arraylist);
                this.mc.getResourcePackRepository().func_148527_a(arraylist);
                this.mc.gameSettings.resourcePacks.clear();
                iterator = arraylist.iterator();

                while (iterator.hasNext())
                {
                    ResourcePackRepository.Entry entry = (ResourcePackRepository.Entry)iterator.next();
                    this.mc.gameSettings.resourcePacks.add(entry.getResourcePackName());
                }

                this.mc.gameSettings.saveOptions();
                this.mc.refreshResources();
                this.mc.displayGuiScreen(this.field_146965_f);
            }
        }
    }

    /**
     * Called when the mouse is clicked.
     */
    protected void mouseClicked(int p_73864_1_, int p_73864_2_, int p_73864_3_)
    {
        super.mouseClicked(p_73864_1_, p_73864_2_, p_73864_3_);
        this.field_146970_i.func_148179_a(p_73864_1_, p_73864_2_, p_73864_3_);
        this.field_146967_r.func_148179_a(p_73864_1_, p_73864_2_, p_73864_3_);
    }

    /**
     * Called when the mouse is moved or a mouse button is released.  Signature: (mouseX, mouseY, which) which==-1 is
     * mouseMove, which==0 or which==1 is mouseUp
     */
    protected void mouseMovedOrUp(int p_146286_1_, int p_146286_2_, int p_146286_3_)
    {
        super.mouseMovedOrUp(p_146286_1_, p_146286_2_, p_146286_3_);
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawBackground(0);
        this.field_146970_i.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
        this.field_146967_r.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
        this.drawCenteredString(this.fontRendererObj, I18n.format("resourcePack.title", new Object[0]), this.width / 2, 16, 16777215);
        this.drawCenteredString(this.fontRendererObj, I18n.format("resourcePack.folderInfo", new Object[0]), this.width / 2 - 77, this.height - 26, 8421504);
        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }
}