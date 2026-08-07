package com.microsoft.Malmo.Mixins;

import net.minecraft.client.particle.Particle;
import net.minecraft.client.particle.ParticleManager;
import net.minecraft.util.EnumParticleTypes;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import qrl.Recorder;

/** Records whitelisted client particle spawns for human-tape replay.
 * Placement RNG (Entity.rand / Particle.rand, seeded from system time) is
 * unrecoverable from entity rows alone (SCOPE "Unrecoverable from tape"), so
 * the tape carries the actual spawn calls. spawnEffectParticle is the id
 * funnel for both server SPacketParticles and client-local Explosion
 * .doExplosionB spawns. Whitelist: explosion classes only (creeper/TNT blast
 * clouds, dragon-death puffs); everything else stays reconstruction. */
@Mixin(ParticleManager.class)
public abstract class MixinRecordParticles {

    @Inject(method = "spawnEffectParticle(IDDDDDD[I)Lnet/minecraft/client/particle/Particle;",
            at = @At("HEAD"))
    private void qrl$recordSpawn(int particleId, double x, double y, double z,
            double xSpeed, double ySpeed, double zSpeed, int[] params,
            CallbackInfoReturnable<Particle> cir) {
        if (particleId == EnumParticleTypes.EXPLOSION_NORMAL.getParticleID()
                || particleId == EnumParticleTypes.EXPLOSION_LARGE.getParticleID()
                || particleId == EnumParticleTypes.EXPLOSION_HUGE.getParticleID())
            Recorder.recordParticleSpawn(particleId, x, y, z,
                                         xSpeed, ySpeed, zSpeed);
    }
}
