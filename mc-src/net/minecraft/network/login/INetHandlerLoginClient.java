package net.minecraft.network.login;

import net.minecraft.network.INetHandler;
import net.minecraft.network.login.server.S00PacketDisconnect;
import net.minecraft.network.login.server.S01PacketEncryptionRequest;
import net.minecraft.network.login.server.S02PacketLoginSuccess;

public interface INetHandlerLoginClient extends INetHandler
{
    void handleEncryptionRequest(S01PacketEncryptionRequest p_147389_1_);

    void handleLoginSuccess(S02PacketLoginSuccess p_147390_1_);

    void handleDisconnect(S00PacketDisconnect p_147388_1_);
}