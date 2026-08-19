package com.microsoft.Malmo.Mixins;

import net.minecraft.client.multiplayer.PlayerControllerMP;
import net.minecraft.util.math.BlockPos;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import netheritemod.DigControllerAccess;
import netheritemod.Recorder;

/**
 * Dig-trace accessors for PlayerControllerMP private dig state, plus a
 * successful destroy hook so the recorder can mark a per-tick break event.
 * Diagnostic only: no dig behaviour changes.
 *
 * Source evidence (oracle PlayerControllerMP):
 *   currentBlock, curBlockDamageMP, blockHitDelay, isHittingBlock fields
 *   onPlayerDestroyBlock return true on successful client break
 */
@Mixin(PlayerControllerMP.class)
public abstract class MixinPlayerControllerDig implements DigControllerAccess {
    @Shadow private BlockPos currentBlock;
    @Shadow private float curBlockDamageMP;
    @Shadow private int blockHitDelay;
    @Shadow private boolean isHittingBlock;

    @Override
    public BlockPos dig$currentBlock() {
        return this.currentBlock;
    }

    @Override
    public float dig$curBlockDamageMP() {
        return this.curBlockDamageMP;
    }

    @Override
    public int dig$blockHitDelay() {
        return this.blockHitDelay;
    }

    @Override
    public boolean dig$isHittingBlock() {
        return this.isHittingBlock;
    }

    @Inject(method = "onPlayerDestroyBlock", at = @At("HEAD"))
    private void dig$destroyBegin(BlockPos pos, CallbackInfoReturnable<Boolean> cir) {
        Recorder.digDestroyBegin();
    }

    @Inject(method = "onPlayerDestroyBlock", at = @At("RETURN"))
    private void dig$destroyEnd(BlockPos pos, CallbackInfoReturnable<Boolean> cir) {
        Boolean ok = cir.getReturnValue();
        Recorder.digDestroyEnd(pos, ok != null && ok.booleanValue());
    }
}
