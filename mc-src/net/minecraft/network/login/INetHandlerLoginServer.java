package net.minecraft.network.login;

import net.minecraft.network.INetHandler;
import net.minecraft.network.login.client.C00PacketLoginStart;
import net.minecraft.network.login.client.C01PacketEncryptionResponse;

public interface INetHandlerLoginServer extends INetHandler
{
    void processLoginStart(C00PacketLoginStart p_147316_1_);

    void processEncryptionResponse(C01PacketEncryptionResponse p_147315_1_);
}