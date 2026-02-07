package net.minecraft.client.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public interface GuiYesNoCallback
{
    void confirmClicked(boolean p_73878_1_, int p_73878_2_);
}