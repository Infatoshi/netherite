package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import qrl.Recorder;

/** Captures terminal XP constructors only for a parked Recorder fixture. */
@Mixin(EntityLivingBase.class)
public abstract class MixinOracleTerminalXpSpawn {
    @Redirect(
        method = "onDeathUpdate",
        at = @At(
            value = "INVOKE",
            target = "Lnet/minecraft/world/World;spawnEntity(Lnet/minecraft/entity/Entity;)Z"))
    private boolean qrl$captureTerminalXp(World world, Entity entity) {
        int result = Recorder.oracleTerminalXpSpawn(
            world, entity, (EntityLivingBase)(Object)this);
        return result >= 0 ? result != 0 : world.spawnEntity(entity);
    }
}
