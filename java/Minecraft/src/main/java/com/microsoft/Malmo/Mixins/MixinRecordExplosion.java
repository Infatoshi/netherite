package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.network.NetHandlerPlayClient;
import net.minecraft.network.play.server.SPacketExplosion;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import qrl.Recorder;

/** Records SPacketExplosion for human-tape replay: the packet carries the
 * authoritative affected-block list and the local player's knockback motion,
 * neither of which is otherwise visible in the tape (creeper/TNT blasts). */
@Mixin(NetHandlerPlayClient.class)
public abstract class MixinRecordExplosion {
    @Shadow private Minecraft gameController;

    @Inject(method = "handleExplosion", at = @At("HEAD"))
    private void qrl$recordExplosion(SPacketExplosion packet, CallbackInfo ci) {
        /* PacketThreadUtil invokes this handler first on Netty and then again
         * from the scheduled client task. Record only the applied invocation. */
        if (this.gameController.isCallingFromMinecraftThread())
            Recorder.recordExplosionPacket(packet);
    }
}
