package com.microsoft.Malmo.Mixins;

import net.minecraft.util.EnumParticleTypes;
import net.minecraft.world.WorldServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Captures the authoritative WorldServer particle packet descriptor while a
 * locked player-attack oracle fixture is active. */
@Mixin(WorldServer.class)
public abstract class MixinOracleServerParticle {
    @Inject(method = "spawnParticle(Lnet/minecraft/util/EnumParticleTypes;ZDDDIDDDD[I)V",
            at = @At("HEAD"))
    private void qrl$captureServerParticle(
            EnumParticleTypes type, boolean longDistance,
            double x, double y, double z, int count,
            double xOffset, double yOffset, double zOffset, double speed,
            int[] parameters, CallbackInfo ci) {
        Recorder.oracleCapturePlayerAttackParticle(
            (WorldServer)(Object)this, type, longDistance,
            x, y, z, count, xOffset, yOffset, zOffset, speed,
            parameters);
    }
}
