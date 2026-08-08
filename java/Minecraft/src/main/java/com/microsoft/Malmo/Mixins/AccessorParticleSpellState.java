package com.microsoft.Malmo.Mixins;

import net.minecraft.client.particle.ParticleSpell;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Private texture-animation base used by ParticleSpell.onUpdate. */
@Mixin(ParticleSpell.class)
public interface AccessorParticleSpellState {
    @Accessor("baseSpellTextureIndex") int qrl$getBaseSpellTextureIndex();
}
