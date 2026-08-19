package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.block.Block;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import netheritemod.Recorder;

/**
 * Depth counter around neighbor notify so dig-trace can tag ordered
 * neighbor-caused setBlockState finals separately from dig/packet top-level
 * mutations. Diagnostic only.
 *
 * Source: World.notifyNeighborsOfStateChange / notifyNeighborsOfStateExcept
 * call neighborChanged on the six faces; those may recurse into setBlockState.
 */
@Mixin(World.class)
public abstract class MixinNeighborNotifyDig {
    private boolean dig$isClientWorldThread() {
        Minecraft mc = Minecraft.getMinecraft();
        return (Object) this instanceof WorldClient && mc != null
            && mc.isCallingFromMinecraftThread();
    }

    @Inject(method = "notifyNeighborsOfStateChange", at = @At("HEAD"))
    private void dig$notifyBegin(BlockPos pos, Block blockType, boolean updateObservers,
            CallbackInfo ci) {
        if (dig$isClientWorldThread()) Recorder.neighborNotifyBegin();
    }

    @Inject(method = "notifyNeighborsOfStateChange", at = @At("RETURN"))
    private void dig$notifyEnd(BlockPos pos, Block blockType, boolean updateObservers,
            CallbackInfo ci) {
        if (dig$isClientWorldThread()) Recorder.neighborNotifyEnd();
    }

    @Inject(method = "notifyNeighborsOfStateExcept", at = @At("HEAD"))
    private void dig$notifyExceptBegin(BlockPos pos, Block blockType, EnumFacing skipSide,
            CallbackInfo ci) {
        if (dig$isClientWorldThread()) Recorder.neighborNotifyBegin();
    }

    @Inject(method = "notifyNeighborsOfStateExcept", at = @At("RETURN"))
    private void dig$notifyExceptEnd(BlockPos pos, Block blockType, EnumFacing skipSide,
            CallbackInfo ci) {
        if (dig$isClientWorldThread()) Recorder.neighborNotifyEnd();
    }
}
