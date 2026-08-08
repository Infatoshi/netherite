package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.passive.AbstractHorse;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Capture the exact vehicle inputs immediately before horse travel. */
@Mixin(AbstractHorse.class)
public abstract class MixinOracleHorseTravel {
    @Inject(method = "moveEntityWithHeading", at = @At("HEAD"))
    private void qrl$captureTravel(
            float strafe, float forward, CallbackInfo callback) {
        Recorder.oracleCaptureHorseTravel(
            (AbstractHorse)(Object)this, strafe, forward);
    }
}
