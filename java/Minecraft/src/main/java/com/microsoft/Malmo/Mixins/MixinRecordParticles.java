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
 * the tape carries the actual constructed state. spawnEffectParticle is the id
 * funnel for both server SPacketParticles and client-local spawns. Whitelist:
 * explosion classes, SPELL_MOB, horse-taming SMOKE_NORMAL, and breeding/taming
 * HEART. */
@Mixin(ParticleManager.class)
public abstract class MixinRecordParticles {

    @Inject(method = "spawnEffectParticle(IDDDDDD[I)Lnet/minecraft/client/particle/Particle;",
            at = @At("RETURN"))
    private void qrl$recordSpawn(int particleId, double x, double y, double z,
            double xSpeed, double ySpeed, double zSpeed, int[] params,
            CallbackInfoReturnable<Particle> cir) {
        if (particleId == EnumParticleTypes.EXPLOSION_NORMAL.getParticleID()
                || particleId == EnumParticleTypes.EXPLOSION_LARGE.getParticleID()
                || particleId == EnumParticleTypes.EXPLOSION_HUGE.getParticleID()
                || particleId == EnumParticleTypes.SMOKE_NORMAL.getParticleID()
                || particleId == EnumParticleTypes.SPELL_MOB.getParticleID()
                || particleId == EnumParticleTypes.SPIT.getParticleID()
                || particleId == EnumParticleTypes.HEART.getParticleID())
            Recorder.recordParticleSpawn(particleId, x, y, z,
                                         xSpeed, ySpeed, zSpeed,
                                         cir.getReturnValue());
    }
}
