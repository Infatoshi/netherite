package com.microsoft.Malmo.Mixins;

import net.minecraft.block.BlockTNT;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Pins oracle cursors immediately before burning-arrow TNT ignition. */
@Mixin(BlockTNT.class)
public abstract class MixinTntArrowCollisionCursor {
    @Inject(method = "onEntityCollidedWithBlock", at = @At("HEAD"))
    private void qrl$restoreTntArrowCollisionCursors(
            World world, BlockPos pos, IBlockState state, Entity entity,
            CallbackInfo ci) {
        Recorder.oracleRestoreTntArrowCollisionCursors(world, entity);
    }
}
