package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.Entity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Capture exact push pairs for the bounded skeleton-trap oracle. */
@Mixin(Entity.class)
public abstract class MixinOracleEntityCollision {
    @Inject(method = "applyEntityCollision", at = @At("HEAD"))
    private void qrl$beginEntityCollision(Entity other, CallbackInfo ci) {
        Recorder.oracleBeginEntityCollision((Entity)(Object)this, other);
    }

    @Inject(method = "applyEntityCollision", at = @At("RETURN"))
    private void qrl$endEntityCollision(Entity other, CallbackInfo ci) {
        Recorder.oracleEndEntityCollision((Entity)(Object)this, other);
    }
}
