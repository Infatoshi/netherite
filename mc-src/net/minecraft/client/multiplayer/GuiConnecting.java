package net.minecraft.client.multiplayer;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import io.netty.util.concurrent.GenericFutureListener;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.concurrent.atomic.AtomicInteger;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiButton;
import net.minecraft.client.gui.GuiDisconnected;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.network.NetHandlerLoginClient;
import net.minecraft.client.resources.I18n;
import net.minecraft.network.EnumConnectionState;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.handshake.client.C00Handshake;
import net.minecraft.network.login.client.C00PacketLoginStart;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.ChatComponentTranslation;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class GuiConnecting extends GuiScreen
{
    private static final AtomicInteger field_146372_a = new AtomicInteger(0);
    private static final Logger logger = LogManager.getLogger();
    private NetworkManager field_146371_g;
    private boolean field_146373_h;
    private final GuiScreen field_146374_i;
    private static final String __OBFID = "CL_00000685";

    public GuiConnecting(GuiScreen p_i1181_1_, Minecraft p_i1181_2_, ServerData p_i1181_3_)
    {
        this.mc = p_i1181_2_;
        this.field_146374_i = p_i1181_1_;
        ServerAddress serveraddress = ServerAddress.func_78860_a(p_i1181_3_.serverIP);
        p_i1181_2_.loadWorld((WorldClient)null);
        p_i1181_2_.setServerData(p_i1181_3_);
        this.func_146367_a(serveraddress.getIP(), serveraddress.getPort());
    }

    public GuiConnecting(GuiScreen p_i1182_1_, Minecraft p_i1182_2_, String p_i1182_3_, int p_i1182_4_)
    {
        this.mc = p_i1182_2_;
        this.field_146374_i = p_i1182_1_;
        p_i1182_2_.loadWorld((WorldClient)null);
        this.func_146367_a(p_i1182_3_, p_i1182_4_);
    }

    private void func_146367_a(final String p_146367_1_, final int p_146367_2_)
    {
        logger.info("Connecting to " + p_146367_1_ + ", " + p_146367_2_);
        (new Thread("Server Connector #" + field_146372_a.incrementAndGet())
        {
            private static final String __OBFID = "CL_00000686";
            public void run()
            {
                InetAddress inetaddress = null;

                try
                {
                    if (GuiConnecting.this.field_146373_h)
                    {
                        return;
                    }

                    inetaddress = InetAddress.getByName(p_146367_1_);
                    GuiConnecting.this.field_146371_g = NetworkManager.provideLanClient(inetaddress, p_146367_2_);
                    GuiConnecting.this.field_146371_g.setNetHandler(new NetHandlerLoginClient(GuiConnecting.this.field_146371_g, GuiConnecting.this.mc, GuiConnecting.this.field_146374_i));
                    GuiConnecting.this.field_146371_g.scheduleOutboundPacket(new C00Handshake(5, p_146367_1_, p_146367_2_, EnumConnectionState.LOGIN), new GenericFutureListener[0]);
                    GuiConnecting.this.field_146371_g.scheduleOutboundPacket(new C00PacketLoginStart(GuiConnecting.this.mc.getSession().func_148256_e()), new GenericFutureListener[0]);
                }
                catch (UnknownHostException unknownhostexception)
                {
                    if (GuiConnecting.this.field_146373_h)
                    {
                        return;
                    }

                    GuiConnecting.logger.error("Couldn\'t connect to server", unknownhostexception);
                    GuiConnecting.this.mc.displayGuiScreen(new GuiDisconnected(GuiConnecting.this.field_146374_i, "connect.failed", new ChatComponentTranslation("disconnect.genericReason", new Object[] {"Unknown host"})));
                }
                catch (Exception exception)
                {
                    if (GuiConnecting.this.field_146373_h)
                    {
                        return;
                    }

                    GuiConnecting.logger.error("Couldn\'t connect to server", exception);
                    String s = exception.toString();

                    if (inetaddress != null)
                    {
                        String s1 = inetaddress.toString() + ":" + p_146367_2_;
                        s = s.replaceAll(s1, "");
                    }

                    GuiConnecting.this.mc.displayGuiScreen(new GuiDisconnected(GuiConnecting.this.field_146374_i, "connect.failed", new ChatComponentTranslation("disconnect.genericReason", new Object[] {s})));
                }
            }
        }).start();
    }

    /**
     * Called from the main game loop to update the screen.
     */
    public void updateScreen()
    {
        if (this.field_146371_g != null)
        {
            if (this.field_146371_g.isChannelOpen())
            {
                this.field_146371_g.processReceivedPackets();
            }
            else if (this.field_146371_g.getExitMessage() != null)
            {
                this.field_146371_g.getNetHandler().onDisconnect(this.field_146371_g.getExitMessage());
            }
        }
    }

    /**
     * Fired when a key is typed. This is the equivalent of KeyListener.keyTyped(KeyEvent e).
     */
    protected void keyTyped(char p_73869_1_, int p_73869_2_) {}

    /**
     * Adds the buttons (and other controls) to the screen in question.
     */
    public void initGui()
    {
        this.buttonList.clear();
        this.buttonList.add(new GuiButton(0, this.width / 2 - 100, this.height / 2 + 50, I18n.format("gui.cancel", new Object[0])));
    }

    protected void actionPerformed(GuiButton p_146284_1_)
    {
        if (p_146284_1_.id == 0)
        {
            this.field_146373_h = true;

            if (this.field_146371_g != null)
            {
                this.field_146371_g.closeChannel(new ChatComponentText("Aborted"));
            }

            this.mc.displayGuiScreen(this.field_146374_i);
        }
    }

    /**
     * Draws the screen and all the components in it.
     */
    public void drawScreen(int p_73863_1_, int p_73863_2_, float p_73863_3_)
    {
        this.drawDefaultBackground();

        if (this.field_146371_g == null)
        {
            this.drawCenteredString(this.fontRendererObj, I18n.format("connect.connecting", new Object[0]), this.width / 2, this.height / 2 - 50, 16777215);
        }
        else
        {
            this.drawCenteredString(this.fontRendererObj, I18n.format("connect.authorizing", new Object[0]), this.width / 2, this.height / 2 - 50, 16777215);
        }

        super.drawScreen(p_73863_1_, p_73863_2_, p_73863_3_);
    }
}