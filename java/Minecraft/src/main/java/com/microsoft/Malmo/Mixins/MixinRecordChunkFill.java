package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.network.PacketBuffer;
import net.minecraft.world.World;
import net.minecraft.world.chunk.Chunk;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import netheritemod.Recorder;

/**
 * Records client block finals applied by {@code SPacketChunkData -> Chunk.fillChunk}.
 *
 * <p>Vanilla path (MC 1.11.2):
 * {@code NetHandlerPlayClient.handleChunkData} calls {@link Chunk#fillChunk}, which
 * writes section palettes in place and never routes through
 * {@code World.setBlockState}. That bypasses {@link MixinRecordBlockChanges}.
 * Forge's {@code clumpingThreshold} (default 64) upgrades large multi-block
 * resyncs from {@code SPacketMultiBlockChange} to a partial section
 * {@code SPacketChunkData}, so bulk edits would otherwise vanish from the tape.
 *
 * <p>Filters: active recorder, WorldClient / isRemote, client thread, partial
 * fills only (full {@code loadChunk} streams are initial terrain / dim download
 * and are covered by the recstart MCA snapshot). Unchanged cells are not
 * emitted. Call order is deterministic: section 0..15, then storage index
 * 0..4095 ({@code y<<8|z<<4|x}). Dimension handoff still drops in-flight
 * {@code bc} via Recorder's existing forcedLoading clear.
 */
@Mixin(Chunk.class)
public abstract class MixinRecordChunkFill {
    @Final @Shadow private World world;
    @Shadow public int xPosition;
    @Shadow public int zPosition;

    @Inject(method = "fillChunk(Lnet/minecraft/network/PacketBuffer;IZ)V",
            at = @At("HEAD"))
    private void qrl$beginChunkFill(PacketBuffer buf, int availableSections,
            boolean loadChunk, CallbackInfo ci) {
        if (!Recorder.isRecording()) return;
        if (this.world == null || !this.world.isRemote) return;
        if (!(this.world instanceof WorldClient)) return;
        Minecraft mc = Minecraft.getMinecraft();
        if (mc == null || !mc.isCallingFromMinecraftThread()) return;
        /* Full loadChunk packets are terrain stream / dim download after
         * doPreChunk wiped the client chunk to empty; recording every non-air
         * cell would flood the tape with snapshot-covered terrain. Partial
         * section packets (clumpingThreshold path) keep pre-existing cells. */
        if (loadChunk) {
            Recorder.abortChunkFillCapture();
            return;
        }
        Recorder.beginChunkFillCapture((Chunk) (Object) this, availableSections);
    }

    @Inject(method = "fillChunk(Lnet/minecraft/network/PacketBuffer;IZ)V",
            at = @At("RETURN"))
    private void qrl$endChunkFill(PacketBuffer buf, int availableSections,
            boolean loadChunk, CallbackInfo ci) {
        if (loadChunk) return;
        Recorder.endChunkFillCapture((Chunk) (Object) this, availableSections);
    }
}
