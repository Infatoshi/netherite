package net.minecraft.client.network;

import com.mojang.authlib.exceptions.AuthenticationException;
import com.mojang.authlib.exceptions.AuthenticationUnavailableException;
import com.mojang.authlib.exceptions.InvalidCredentialsException;
import com.mojang.authlib.minecraft.MinecraftSessionService;
import com.mojang.authlib.yggdrasil.YggdrasilAuthenticationService;

import cpw.mods.fml.client.FMLClientHandler;
import cpw.mods.fml.common.network.internal.FMLNetworkHandler;
import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import io.netty.util.concurrent.Future;
import io.netty.util.concurrent.GenericFutureListener;
import java.math.BigInteger;
import java.security.PublicKey;
import java.util.UUID;
import javax.crypto.SecretKey;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.GuiDisconnected;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.network.EnumConnectionState;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.login.INetHandlerLoginClient;
import net.minecraft.network.login.client.C01PacketEncryptionResponse;
import net.minecraft.network.login.server.S00PacketDisconnect;
import net.minecraft.network.login.server.S01PacketEncryptionRequest;
import net.minecraft.network.login.server.S02PacketLoginSuccess;
import net.minecraft.network.play.client.C17PacketCustomPayload;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.CryptManager;
import net.minecraft.util.IChatComponent;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

@SideOnly(Side.CLIENT)
public class NetHandlerLoginClient implements INetHandlerLoginClient
{
    private static final Logger logger = LogManager.getLogger();
    private final Minecraft field_147394_b;
    private final GuiScreen field_147395_c;
    private final NetworkManager field_147393_d;
    private static final String __OBFID = "CL_00000876";

    public NetHandlerLoginClient(NetworkManager p_i45059_1_, Minecraft p_i45059_2_, GuiScreen p_i45059_3_)
    {
        this.field_147393_d = p_i45059_1_;
        this.field_147394_b = p_i45059_2_;
        this.field_147395_c = p_i45059_3_;
    }

    public void handleEncryptionRequest(S01PacketEncryptionRequest p_147389_1_)
    {
        final SecretKey secretkey = CryptManager.createNewSharedKey();
        String s = p_147389_1_.func_149609_c();
        PublicKey publickey = p_147389_1_.func_149608_d();
        String s1 = (new BigInteger(CryptManager.getServerIdHash(s, publickey, secretkey))).toString(16);
        boolean flag = this.field_147394_b.func_147104_D() == null || !this.field_147394_b.func_147104_D().func_152585_d();

        try
        {
            this.func_147391_c().joinServer(this.field_147394_b.getSession().func_148256_e(), this.field_147394_b.getSession().getToken(), s1);
        }
        catch (AuthenticationUnavailableException authenticationunavailableexception)
        {
            if (flag)
            {
                this.field_147393_d.closeChannel(new ChatComponentTranslation("disconnect.loginFailedInfo", new Object[] {new ChatComponentTranslation("disconnect.loginFailedInfo.serversUnavailable", new Object[0])}));
                return;
            }
        }
        catch (InvalidCredentialsException invalidcredentialsexception)
        {
            if (flag)
            {
                this.field_147393_d.closeChannel(new ChatComponentTranslation("disconnect.loginFailedInfo", new Object[] {new ChatComponentTranslation("disconnect.loginFailedInfo.invalidSession", new Object[0])}));
                return;
            }
        }
        catch (AuthenticationException authenticationexception)
        {
            if (flag)
            {
                this.field_147393_d.closeChannel(new ChatComponentTranslation("disconnect.loginFailedInfo", new Object[] {authenticationexception.getMessage()}));
                return;
            }
        }

        this.field_147393_d.scheduleOutboundPacket(new C01PacketEncryptionResponse(secretkey, publickey, p_147389_1_.func_149607_e()), new GenericFutureListener[] {new GenericFutureListener()
        {
            private static final String __OBFID = "CL_00000877";
            public void operationComplete(Future p_operationComplete_1_)
            {
                NetHandlerLoginClient.this.field_147393_d.enableEncryption(secretkey);
            }
        }
                                                                                                                                                });
    }

    private MinecraftSessionService func_147391_c()
    {
        return (new YggdrasilAuthenticationService(this.field_147394_b.getProxy(), UUID.randomUUID().toString())).createMinecraftSessionService();
    }

    public void handleLoginSuccess(S02PacketLoginSuccess p_147390_1_)
    {
        FMLNetworkHandler.fmlClientHandshake(this.field_147393_d);
    }

    /**
     * Invoked when disconnecting, the parameter is a ChatComponent describing the reason for termination
     */
    public void onDisconnect(IChatComponent p_147231_1_)
    {
        this.field_147394_b.displayGuiScreen(new GuiDisconnected(this.field_147395_c, "connect.failed", p_147231_1_));
    }

    /**
     * Allows validation of the connection state transition. Parameters: from, to (connection state). Typically throws
     * IllegalStateException or UnsupportedOperationException if validation fails
     */
    public void onConnectionStateTransition(EnumConnectionState p_147232_1_, EnumConnectionState p_147232_2_)
    {
        logger.debug("Switching protocol from " + p_147232_1_ + " to " + p_147232_2_);

        if (p_147232_2_ == EnumConnectionState.PLAY)
        {
            NetHandlerPlayClient nhpc = new NetHandlerPlayClient(this.field_147394_b, this.field_147395_c, this.field_147393_d);
            this.field_147393_d.setNetHandler(nhpc);
            FMLClientHandler.instance().setPlayClient(nhpc);

        }
    }

    /**
     * For scheduled network tasks. Used in NetHandlerPlayServer to send keep-alive packets and in NetHandlerLoginServer
     * for a login-timeout
     */
    public void onNetworkTick() {}

    public void handleDisconnect(S00PacketDisconnect p_147388_1_)
    {
        this.field_147393_d.closeChannel(p_147388_1_.func_149603_c());
    }
}