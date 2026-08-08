package com.microsoft.Malmo.Mixins;

import java.util.Random;
import net.minecraft.block.BlockFire;
import net.minecraft.block.state.IBlockState;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import qrl.Recorder;

/** Pins and records an armed oracle RNG at the exact fire callback boundary. */
@Mixin(BlockFire.class)
public abstract class MixinFireScheduledCursor {
    @Inject(method = "updateTick", at = @At("HEAD"))
    private void qrl$beforeScheduledFireTick(
            World world, BlockPos pos, IBlockState state, Random random,
            CallbackInfo ci) {
        Recorder.oracleBeforeScheduledFireTick(world, pos, random);
    }

    @Inject(method = "updateTick", at = @At("RETURN"))
    private void qrl$afterScheduledFireTick(
            World world, BlockPos pos, IBlockState state, Random random,
            CallbackInfo ci) {
        Recorder.oracleAfterScheduledFireTick(world, pos, random);
    }
}
