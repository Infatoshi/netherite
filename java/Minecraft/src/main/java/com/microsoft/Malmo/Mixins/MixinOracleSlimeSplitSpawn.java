package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.Entity;
import net.minecraft.entity.monster.EntitySlime;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import qrl.Recorder;

/** Captures terminal Slime children only for a parked Recorder fixture. */
@Mixin(EntitySlime.class)
public abstract class MixinOracleSlimeSplitSpawn {
    @Redirect(
        method = "setDead",
        at = @At(
            value = "INVOKE",
            target = "Lnet/minecraft/world/World;spawnEntity(Lnet/minecraft/entity/Entity;)Z"))
    private boolean qrl$captureSlimeChild(World world, Entity entity) {
        int result = Recorder.oracleSlimeSplitSpawn(
            world, entity, (EntitySlime)(Object)this);
        return result >= 0 ? result != 0 : world.spawnEntity(entity);
    }
}
