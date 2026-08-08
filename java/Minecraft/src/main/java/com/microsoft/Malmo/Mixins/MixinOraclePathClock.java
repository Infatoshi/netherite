package com.microsoft.Malmo.Mixins;

import net.minecraft.pathfinding.PathNavigate;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import qrl.Recorder;

/** Make path timeout accounting deterministic during controlled fork ticks. */
@Mixin(PathNavigate.class)
public abstract class MixinOraclePathClock {
    @Redirect(
        method = "checkForStuck",
        at = @At(
            value = "INVOKE",
            target = "Ljava/lang/System;currentTimeMillis()J"))
    private long qrl$serverTimeMillis() {
        return Recorder.oracleServerCurrentTimeMillis();
    }
}
