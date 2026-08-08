package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.Entity;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Captures the exact dynamic loaded-entity visit order for the mate oracle. */
@Mixin(World.class)
public abstract class MixinOracleWorldEntityUpdate {
    @Inject(method = "updateEntityWithOptionalForce", at = @At("HEAD"))
    private void qrl$captureMateEntityUpdate(
            Entity entity, boolean forceUpdate, CallbackInfo ci) {
        Recorder.oracleMateTickEntityUpdate(
            (World)(Object)this, entity);
    }
}
