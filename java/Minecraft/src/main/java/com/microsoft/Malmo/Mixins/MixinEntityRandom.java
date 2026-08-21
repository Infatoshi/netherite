package com.microsoft.Malmo.Mixins;

import java.util.Random;

import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import net.minecraft.entity.Entity;
import net.minecraft.world.World;

import netheritemod.DetEntityRng;
import netheritemod.QLaunch;

/**
 * Determinism pin (qrl_launch.json determinism.det_entity_rng): replace the
 * nanoTime Entity.rand with {@code new Random(DetEntityRng.userSeed(id))}
 * at Entity constructor RETURN (entityId is assigned). Mixin 0.7.5 forbids
 * INVOKE+Shift in a ctor; only RETURN is legal. UUID already drew from the
 * vanilla Random; magma hydrates the live seed48 after construction.
 * Off by default = vanilla new Random().
 *
 * The previous FIELD redirect targeting Entity.rand:F was a broken Malmo
 * stub (wrong descriptor) and did not run.
 */
@Mixin(Entity.class)
public abstract class MixinEntityRandom {
    @Shadow protected Random rand;

    @Inject(method = "<init>(Lnet/minecraft/world/World;)V", at = @At("RETURN"))
    private void qrl$detEntityRand(World worldIn, CallbackInfo ci) {
        if (!QLaunch.DET_ENTITY_RNG) return;
        Entity self = (Entity) (Object) this;
        boolean server = worldIn != null && !worldIn.isRemote;
        this.rand = DetEntityRng.create(self.getEntityId(), server);
    }
}
