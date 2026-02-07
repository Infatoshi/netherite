package net.minecraft.stats;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;

@SideOnly(Side.CLIENT)
public interface IStatStringFormat
{
    /**
     * Formats the strings based on 'IStatStringFormat' interface.
     */
    String formatString(String p_74535_1_);
}