package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.network.NetHandlerPlayClient;
import net.minecraft.network.play.server.SPacketEntityVelocity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import qrl.Recorder;

/** Records authoritative player velocity packets for human-tape replay. */
@Mixin(NetHandlerPlayClient.class)
public abstract class MixinRecordPlayerVelocity {
    @Shadow private Minecraft gameController;

    @Inject(method = "handleEntityVelocity", at = @At("HEAD"))
    private void qrl$recordPlayerVelocity(SPacketEntityVelocity packet, CallbackInfo ci) {
        /* PacketThreadUtil invokes this handler first on Netty and then again
         * from the scheduled client task. Record only the applied invocation. */
        if (this.gameController.isCallingFromMinecraftThread())
            Recorder.recordPlayerVelocityPacket(packet);
    }
}
