package com.microsoft.Malmo.Mixins;

import net.minecraft.client.particle.ParticleExplosionLarge;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Private animation state used by ParticleExplosionLarge.renderParticle. */
@Mixin(ParticleExplosionLarge.class)
public interface AccessorParticleExplosionLargeState {
    @Accessor("life") int qrl$getLife();
    @Accessor("lifeTime") int qrl$getLifeTime();
    @Accessor("size") float qrl$getSize();
}
