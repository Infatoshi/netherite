package net.minecraft.server.gui;

import com.mojang.util.QueueLogAppender;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.awt.BorderLayout;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.Font;
import java.awt.event.ActionEvent;
import java.awt.event.ActionListener;
import java.awt.event.FocusAdapter;
import java.awt.event.FocusEvent;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.util.concurrent.CountDownLatch;
import javax.swing.JComponent;
import javax.swing.JFrame;
import javax.swing.JPanel;
import javax.swing.JScrollBar;
import javax.swing.JScrollPane;
import javax.swing.JTextArea;
import javax.swing.JTextField;
import javax.swing.SwingUtilities;
import javax.swing.UIManager;
import javax.swing.border.EtchedBorder;
import javax.swing.border.TitledBorder;
import javax.swing.text.AttributeSet;
import javax.swing.text.BadLocationException;
import javax.swing.text.Document;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.dedicated.DedicatedServer;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.SERVER)
public class MinecraftServerGui extends JComponent
{
    private static final Font field_164249_a = new Font("Monospaced", 0, 12);
    private static final Logger field_164248_b = LogManager.getLogger();
    private DedicatedServer field_120021_b;
    private static final String __OBFID = "CL_00001789";

    /**
     * Creates the server GUI and sets it visible for the user.
     */
    public static void createServerGui(final DedicatedServer p_120016_0_)
    {
        try
        {
            UIManager.setLookAndFeel(UIManager.getSystemLookAndFeelClassName());
        }
        catch (Exception exception)
        {
            ;
        }

        MinecraftServerGui minecraftservergui = new MinecraftServerGui(p_120016_0_);
        JFrame jframe = new JFrame("Minecraft server");
        jframe.add(minecraftservergui);
        jframe.pack();
        jframe.setLocationRelativeTo((Component)null);
        jframe.setVisible(true);
        jframe.addWindowListener(new WindowAdapter()
        {
            private static final String __OBFID = "CL_00001791";
            public void windowClosing(WindowEvent p_windowClosing_1_)
            {
                p_120016_0_.initiateShutdown();

                while (!p_120016_0_.isServerStopped())
                {
                    try
                    {
                        Thread.sleep(100L);
                    }
                    catch (InterruptedException interruptedexception)
                    {
                        interruptedexception.printStackTrace();
                    }
                }

                System.exit(0);
            }
        });
        minecraftservergui.latch.countDown();
    }

    public MinecraftServerGui(DedicatedServer p_i2362_1_)
    {
        this.field_120021_b = p_i2362_1_;
        this.setPreferredSize(new Dimension(854, 480));
        this.setLayout(new BorderLayout());

        try
        {
            this.add(this.getLogComponent(), "Center");
            this.add(this.getStatsComponent(), "West");
        }
        catch (Exception exception)
        {
            field_164248_b.error("Couldn\'t build server GUI", exception);
        }
    }

    /**
     * Generates new StatsComponent and returns it.
     */
    private JComponent getStatsComponent()
    {
        JPanel jpanel = new JPanel(new BorderLayout());
        jpanel.add(new StatsComponent(this.field_120021_b), "North");
        jpanel.add(this.getPlayerListComponent(), "Center");
        jpanel.setBorder(new TitledBorder(new EtchedBorder(), "Stats"));
        return jpanel;
    }

    /**
     * Generates new PlayerListComponent and returns it.
     */
    private JComponent getPlayerListComponent()
    {
        PlayerListComponent playerlistcomponent = new PlayerListComponent(this.field_120021_b);
        JScrollPane jscrollpane = new JScrollPane(playerlistcomponent, 22, 30);
        jscrollpane.setBorder(new TitledBorder(new EtchedBorder(), "Players"));
        return jscrollpane;
    }

    private JComponent getLogComponent()
    {
        JPanel jpanel = new JPanel(new BorderLayout());
        final JTextArea jtextarea = new JTextArea();
        final JScrollPane jscrollpane = new JScrollPane(jtextarea, 22, 30);
        jtextarea.setEditable(false);
        jtextarea.setFont(field_164249_a);
        final JTextField jtextfield = new JTextField();
        jtextfield.addActionListener(new ActionListener()
        {
            private static final String __OBFID = "CL_00001790";
            public void actionPerformed(ActionEvent p_actionPerformed_1_)
            {
                String s = jtextfield.getText().trim();

                if (s.length() > 0)
                {
                    MinecraftServerGui.this.field_120021_b.addPendingCommand(s, MinecraftServer.getServer());
                }

                jtextfield.setText("");
            }
        });
        jtextarea.addFocusListener(new FocusAdapter()
        {
            private static final String __OBFID = "CL_00001794";
            public void focusGained(FocusEvent p_focusGained_1_) {}
        });
        jpanel.add(jscrollpane, "Center");
        jpanel.add(jtextfield, "South");
        jpanel.setBorder(new TitledBorder(new EtchedBorder(), "Log and chat"));
        Thread thread = new Thread(new Runnable()
        {
            private static final String __OBFID = "CL_00001793";
            public void run()
            {
                String s;

                while ((s = QueueLogAppender.getNextLogEvent("ServerGuiConsole")) != null)
                {
                    MinecraftServerGui.this.func_164247_a(jtextarea, jscrollpane, s);
                }
            }
        });
        thread.setDaemon(true);
        thread.start();
        return jpanel;
    }

    private CountDownLatch latch = new CountDownLatch(1);
    public void func_164247_a(final JTextArea p_164247_1_, final JScrollPane p_164247_2_, final String p_164247_3_)
    {
        try
        {
            latch.await();
        } catch (InterruptedException e)
        {
        }
        if (!SwingUtilities.isEventDispatchThread())
        {
            SwingUtilities.invokeLater(new Runnable()
            {
                private static final String __OBFID = "CL_00001792";
                public void run()
                {
                    MinecraftServerGui.this.func_164247_a(p_164247_1_, p_164247_2_, p_164247_3_);
                }
            });
        }
        else
        {
            Document document = p_164247_1_.getDocument();
            JScrollBar jscrollbar = p_164247_2_.getVerticalScrollBar();
            boolean flag = false;

            if (p_164247_2_.getViewport().getView() == p_164247_1_)
            {
                flag = (double)jscrollbar.getValue() + jscrollbar.getSize().getHeight() + (double)(field_164249_a.getSize() * 4) > (double)jscrollbar.getMaximum();
            }

            try
            {
                document.insertString(document.getLength(), p_164247_3_, (AttributeSet)null);
            }
            catch (BadLocationException badlocationexception)
            {
                ;
            }

            if (flag)
            {
                jscrollbar.setValue(Integer.MAX_VALUE);
            }
        }
    }
}