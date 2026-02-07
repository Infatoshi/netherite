package net.minecraft.server.gui;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.text.DecimalFormat;
import javax.swing.JComponent;
import javax.swing.Timer;
import net.minecraft.server.MinecraftServer;

@SideOnly(Side.SERVER)
public class StatsComponent extends JComponent
{
    private static final DecimalFormat field_120040_a = new DecimalFormat("########0.000");
    private int[] field_120038_b = new int[256];
    private int field_120039_c;
    private String[] field_120036_d = new String[11];
    private final MinecraftServer field_120037_e;
    private static final String __OBFID = "CL_00001796";

    public StatsComponent(MinecraftServer p_i2367_1_)
    {
        this.field_120037_e = p_i2367_1_;
        this.setPreferredSize(new Dimension(456, 246));
        this.setMinimumSize(new Dimension(456, 246));
        this.setMaximumSize(new Dimension(456, 246));
        (new Timer(500, new ActionListener()
        {
            private static final String __OBFID = "CL_00001797";
            public void actionPerformed(ActionEvent p_actionPerformed_1_)
            {
                StatsComponent.this.func_120034_a();
            }
        })).start();
        this.setBackground(Color.BLACK);
    }

    private void func_120034_a()
    {
        long i = Runtime.getRuntime().totalMemory() - Runtime.getRuntime().freeMemory();
        System.gc();
        this.field_120036_d[0] = "Memory use: " + i / 1024L / 1024L + " mb (" + Runtime.getRuntime().freeMemory() * 100L / Runtime.getRuntime().maxMemory() + "% free)";
        this.field_120036_d[1] = "Avg tick: " + field_120040_a.format(this.func_120035_a(this.field_120037_e.tickTimeArray) * 1.0E-6D) + " ms";
        this.repaint();
    }

    private double func_120035_a(long[] p_120035_1_)
    {
        long i = 0L;

        for (int j = 0; j < p_120035_1_.length; ++j)
        {
            i += p_120035_1_[j];
        }

        return (double)i / (double)p_120035_1_.length;
    }

    public void paint(Graphics p_paint_1_)
    {
        p_paint_1_.setColor(new Color(16777215));
        p_paint_1_.fillRect(0, 0, 456, 246);
        int i;

        for (i = 0; i < 256; ++i)
        {
            int j = this.field_120038_b[i + this.field_120039_c & 255];
            p_paint_1_.setColor(new Color(j + 28 << 16));
            p_paint_1_.fillRect(i, 100 - j, 1, j);
        }

        p_paint_1_.setColor(Color.BLACK);

        for (i = 0; i < this.field_120036_d.length; ++i)
        {
            String s = this.field_120036_d[i];

            if (s != null)
            {
                p_paint_1_.drawString(s, 32, 116 + i * 16);
            }
        }
    }
}