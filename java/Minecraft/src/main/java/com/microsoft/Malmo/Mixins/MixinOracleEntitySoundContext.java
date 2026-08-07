package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.Entity;
import net.minecraft.util.SoundEvent;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

@Mixin(Entity.class)
public abstract class MixinOracleEntitySoundContext {
    @Inject(method = "playSound", at = @At("HEAD"))
    private void qrl$beginEntitySound(
            SoundEvent soundIn, float volume, float pitch, CallbackInfo ci) {
        Recorder.oracleBeginEntitySound((Entity)(Object)this);
    }

    @Inject(method = "playSound", at = @At("RETURN"))
    private void qrl$endEntitySound(
            SoundEvent soundIn, float volume, float pitch, CallbackInfo ci) {
        Recorder.oracleEndEntitySound((Entity)(Object)this);
    }
}
