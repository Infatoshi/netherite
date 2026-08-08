package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.ai.EntityAISkeletonRiders;
import net.minecraft.entity.passive.EntitySkeletonHorse;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Snapshot the rider group after construction but before same-tick updates. */
@Mixin(EntityAISkeletonRiders.class)
public abstract class MixinOracleSkeletonTrap {
    @Shadow @Final private EntitySkeletonHorse horse;

    @Inject(method = "updateTask", at = @At("RETURN"))
    private void qrl$captureConstructedGroup(CallbackInfo callback) {
        Recorder.oracleCaptureSkeletonTrapPreTick(horse.world);
    }
}
