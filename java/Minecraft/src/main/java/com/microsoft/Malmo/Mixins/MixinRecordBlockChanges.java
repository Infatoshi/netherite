package com.microsoft.Malmo.Mixins;

import net.minecraft.block.state.IBlockState;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.World;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import netheritemod.Recorder;

/**
 * Records successful client-world block mutations for human-tape replay.
 * Hooks {@link World#setBlockState(BlockPos, IBlockState, int)} - the single
 * mutation path used by dig completion, place, SPacketBlockChange, explosions,
 * and invalidateRegionAndSetBlock - so the tape carries exact post-mutation
 * id/meta rather than nearby FNV digests alone.
 *
 * Filters (all required): WorldClient/isRemote, Minecraft client thread,
 * active recorder, and a successful return (chunk rejected / unchanged /
 * out-of-height changes are skipped so server-side and failed writes never
 * enter the queue).
 */
@Mixin(World.class)
public abstract class MixinRecordBlockChanges {
    @Final @Shadow public boolean isRemote;

    @Inject(method = "setBlockState(Lnet/minecraft/util/math/BlockPos;Lnet/minecraft/block/state/IBlockState;I)Z",
            at = @At("RETURN"))
    private void qrl$recordBlockChange(BlockPos pos, IBlockState newState, int flags,
            CallbackInfoReturnable<Boolean> cir) {
        Boolean ok = cir.getReturnValue();
        if (ok == null || !ok.booleanValue()) return;
        if (!this.isRemote) return;
        /* WorldClient only: integrated-server Worlds are isRemote=false, but
         * also reject any non-client World that might set isRemote. */
        if (!((Object) this instanceof WorldClient)) return;
        Minecraft mc = Minecraft.getMinecraft();
        if (mc == null || !mc.isCallingFromMinecraftThread()) return;
        if (!Recorder.isRecording()) return;
        if (pos == null || newState == null) return;
        /* Neighbor-caused: nested under World.notifyNeighbors* (dig_trace
         * depth counter). Dig-destroy and top-level packet/local edits stay
         * untagged so dig.bc_ref is the ordered neighbor cascade only. */
        Recorder.recordBlockChange(pos, newState, Recorder.neighborNotifyDepth() > 0);
    }
}
