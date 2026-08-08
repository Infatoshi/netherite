package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.network.NetHandlerPlayClient;
import net.minecraft.network.play.server.SPacketPlayerPosLook;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import qrl.Recorder;

/** Records authoritative local-player position packets for tape replay. */
@Mixin(NetHandlerPlayClient.class)
public abstract class MixinRecordPlayerPosition {
    @Shadow private Minecraft gameController;

    @Inject(method = "handlePlayerPosLook", at = @At("TAIL"))
    private void qrl$recordPlayerPosition(SPacketPlayerPosLook packet, CallbackInfo ci) {
        /* PacketThreadUtil aborts the Netty-thread invocation before TAIL, but
         * keep the same explicit client-thread guard as the velocity hook. */
        if (this.gameController.isCallingFromMinecraftThread())
            Recorder.recordPlayerPositionPacket();
    }
}
