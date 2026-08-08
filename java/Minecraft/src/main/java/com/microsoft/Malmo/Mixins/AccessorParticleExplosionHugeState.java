package com.microsoft.Malmo.Mixins;

import net.minecraft.client.particle.ParticleExplosionHuge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Private child-emission clock used by ParticleExplosionHuge.onUpdate. */
@Mixin(ParticleExplosionHuge.class)
public interface AccessorParticleExplosionHugeState {
    @Accessor("timeSinceStart") int qrl$getTimeSinceStart();
}
