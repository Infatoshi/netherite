package com.microsoft.Malmo.Mixins;

import net.minecraft.block.BlockGrass;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Redirect;
import qrl.Recorder;

/** Optional save-fork diagnostic for BlockGrass's loaded-edge early return. */
@Mixin(BlockGrass.class)
public abstract class MixinOracleGrassLoadedTrace {
    @Redirect(
        method = "updateTick",
        at = @At(
            value = "INVOKE",
            target = "Lnet/minecraft/world/World;isBlockLoaded(Lnet/minecraft/util/math/BlockPos;)Z"))
    private boolean qrlTraceLoadedTarget(World world, BlockPos target) {
        boolean loaded = world.isBlockLoaded(target);
        Recorder.oracleTraceGrassLoaded(world, target, loaded);
        return loaded;
    }
}
