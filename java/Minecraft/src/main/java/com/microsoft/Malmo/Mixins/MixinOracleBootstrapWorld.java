package com.microsoft.Malmo.Mixins;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.world.WorldServer;
import qrl.Recorder;

/** Keep disk state inert while the integrated client completes its login. */
@Mixin(WorldServer.class)
public abstract class MixinOracleBootstrapWorld {
    @Inject(method = "tick", at = @At("HEAD"), cancellable = true)
    private void qrl$freezeWorldTick(CallbackInfo callback) {
        if (Recorder.oracleFreezeWorldBootstrap()) callback.cancel();
    }
}
