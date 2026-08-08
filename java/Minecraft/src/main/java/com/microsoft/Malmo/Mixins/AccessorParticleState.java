package com.microsoft.Malmo.Mixins;

import net.minecraft.client.particle.Particle;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Read-only constructor-state seam for recorder-only particle observations. */
@Mixin(Particle.class)
public interface AccessorParticleState {
    @Accessor("prevPosX") double qrl$getPrevPosX();
    @Accessor("prevPosY") double qrl$getPrevPosY();
    @Accessor("prevPosZ") double qrl$getPrevPosZ();
    @Accessor("posX") double qrl$getPosX();
    @Accessor("posY") double qrl$getPosY();
    @Accessor("posZ") double qrl$getPosZ();
    @Accessor("motionX") double qrl$getMotionX();
    @Accessor("motionY") double qrl$getMotionY();
    @Accessor("motionZ") double qrl$getMotionZ();
    @Accessor("onGround") boolean qrl$getOnGround();
    @Accessor("canCollide") boolean qrl$getCanCollide();
    @Accessor("particleTextureIndexX") int qrl$getTextureIndexX();
    @Accessor("particleTextureIndexY") int qrl$getTextureIndexY();
    @Accessor("particleTextureJitterX") float qrl$getTextureJitterX();
    @Accessor("particleTextureJitterY") float qrl$getTextureJitterY();
    @Accessor("particleAge") int qrl$getParticleAge();
    @Accessor("particleMaxAge") int qrl$getParticleMaxAge();
    @Accessor("particleScale") float qrl$getParticleScale();
    @Accessor("particleGravity") float qrl$getParticleGravity();
    @Accessor("particleRed") float qrl$getParticleRed();
    @Accessor("particleGreen") float qrl$getParticleGreen();
    @Accessor("particleBlue") float qrl$getParticleBlue();
    @Accessor("particleAlpha") float qrl$getParticleAlpha();
}
