package net.minecraft.client.network;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.network.EnumConnectionState;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.handshake.INetHandlerHandshakeServer;
import net.minecraft.network.handshake.client.C00Handshake;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.NetHandlerLoginServer;
import net.minecraft.util.IChatComponent;
import org.apache.commons.lang3.Validate;

@SideOnly(Side.CLIENT)
public class NetHandlerHandshakeMemory implements INetHandlerHandshakeServer
{
    private final MinecraftServer field_147385_a;
    private final NetworkManager field_147384_b;
    private static final String __OBFID = "CL_00001445";

    public NetHandlerHandshakeMemory(MinecraftServer p_i45287_1_, NetworkManager p_i45287_2_)
    {
        this.field_147385_a = p_i45287_1_;
        this.field_147384_b = p_i45287_2_;
    }

    /**
     * There are two recognized intentions for initiating a handshake: logging in and acquiring server status. The
     * NetworkManager's protocol will be reconfigured according to the specified intention, although a login-intention
     * must pass a versioncheck or receive a disconnect otherwise
     */
    public void processHandshake(C00Handshake p_147383_1_)
    {
        this.field_147384_b.setConnectionState(p_147383_1_.func_149594_c());
    }

    /**
     * Invoked when disconnecting, the parameter is a ChatComponent describing the reason for termination
     */
    public void onDisconnect(IChatComponent p_147231_1_) {}

    /**
     * Allows validation of the connection state transition. Parameters: from, to (connection state). Typically throws
     * IllegalStateException or UnsupportedOperationException if validation fails
     */
    public void onConnectionStateTransition(EnumConnectionState p_147232_1_, EnumConnectionState p_147232_2_)
    {
        Validate.validState(p_147232_2_ == EnumConnectionState.LOGIN || p_147232_2_ == EnumConnectionState.STATUS, "Unexpected protocol " + p_147232_2_, new Object[0]);

        switch (NetHandlerHandshakeMemory.SwitchEnumConnectionState.field_151263_a[p_147232_2_.ordinal()])
        {
            case 1:
                this.field_147384_b.setNetHandler(new NetHandlerLoginServer(this.field_147385_a, this.field_147384_b));
                break;
            case 2:
                throw new UnsupportedOperationException("NYI");
            default:
        }
    }

    /**
     * For scheduled network tasks. Used in NetHandlerPlayServer to send keep-alive packets and in NetHandlerLoginServer
     * for a login-timeout
     */
    public void onNetworkTick() {}

    @SideOnly(Side.CLIENT)

    static final class SwitchEnumConnectionState
        {
            static final int[] field_151263_a = new int[EnumConnectionState.values().length];
            private static final String __OBFID = "CL_00001446";

            static
            {
                try
                {
                    field_151263_a[EnumConnectionState.LOGIN.ordinal()] = 1;
                }
                catch (NoSuchFieldError var2)
                {
                    ;
                }

                try
                {
                    field_151263_a[EnumConnectionState.STATUS.ordinal()] = 2;
                }
                catch (NoSuchFieldError var1)
                {
                    ;
                }
            }
        }
}