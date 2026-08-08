package com.microsoft.Malmo.Mixins;

import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.util.SoundCategory;
import net.minecraft.util.SoundEvent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

@Mixin(WorldClient.class)
public abstract class MixinOracleClientSound {
    @Inject(method = "playSound(DDDLnet/minecraft/util/SoundEvent;"
        + "Lnet/minecraft/util/SoundCategory;FFZ)V", at = @At("HEAD"))
    private void qrl$captureClientSound(
            double x, double y, double z, SoundEvent sound,
            SoundCategory category, float volume, float pitch,
            boolean distanceDelay, CallbackInfo ci) {
        Recorder.oracleCaptureClientSound(
            (WorldClient)(Object)this, sound, category,
            x, y, z, volume, pitch, distanceDelay);
    }
}
