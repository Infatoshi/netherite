package com.microsoft.Malmo.Mixins;

import net.minecraft.entity.Entity;
import net.minecraft.entity.ai.EntityAIMate;
import net.minecraft.entity.passive.EntityAnimal;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Keeps the parked mate oracle's child and XP server-local. */
@Mixin(EntityAIMate.class)
public abstract class MixinOracleMateSpawn {
    @Shadow @Final private EntityAnimal theAnimal;
    @Shadow private EntityAnimal targetMate;
    @Shadow private int spawnBabyDelay;

    @Inject(method = "updateTask", at = @At("HEAD"))
    private void qrl$restoreMateDelay(CallbackInfo ci) {
        int delay = Recorder.oracleMateTickDelay(
            this.theAnimal, this.targetMate);
        if (delay >= 0) this.spawnBabyDelay = delay;
    }

    @Inject(method = "spawnBaby", at = @At("HEAD"))
    private void qrl$pinMateBirthCursors(CallbackInfo ci) {
        Recorder.oracleMateTickBirthStart(
            this.theAnimal, this.targetMate);
    }

    @Redirect(
        method = "spawnBaby",
        at = @At(
            value = "INVOKE",
            target = "Lnet/minecraft/world/World;spawnEntity(Lnet/minecraft/entity/Entity;)Z"))
    private boolean qrl$spawnMateEntity(World world, Entity entity) {
        int result = Recorder.oracleMateSpawnEntity(
            world, entity, this.theAnimal);
        return result >= 0 ? result != 0 : world.spawnEntity(entity);
    }
}
