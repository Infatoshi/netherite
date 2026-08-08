package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayerMP;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Captures the entity-attached critical particle emitter before its animation
 * packet is broadcast to tracking clients. */
@Mixin(EntityPlayerMP.class)
public abstract class MixinOraclePlayerAttackParticle {
    @Inject(method = "onCriticalHit", at = @At("HEAD"))
    private void qrl$captureCritical(Entity target, CallbackInfo ci) {
        Recorder.oracleCapturePlayerAttackEmitter(
            (EntityPlayerMP)(Object)this, target, 9);
    }

    @Inject(method = "onEnchantmentCritical", at = @At("HEAD"))
    private void qrl$captureMagicCritical(Entity target, CallbackInfo ci) {
        Recorder.oracleCapturePlayerAttackEmitter(
            (EntityPlayerMP)(Object)this, target, 10);
    }
}
