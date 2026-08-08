package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.Entity;
import net.minecraft.world.WorldServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

@Mixin(WorldServer.class)
public abstract class MixinOracleEntityStatus {
    @Inject(method = "setEntityState", at = @At("HEAD"))
    private void qrl$captureEntityStatus(
            Entity entityIn, byte state, CallbackInfo ci) {
        Recorder.oracleCaptureEntityStatus(
            (WorldServer)(Object)this, entityIn, state);
    }
}
