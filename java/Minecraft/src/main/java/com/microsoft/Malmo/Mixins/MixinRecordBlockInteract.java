package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.entity.EntityPlayerSP;
import net.minecraft.client.multiplayer.PlayerControllerMP;
import net.minecraft.client.multiplayer.WorldClient;
import net.minecraft.util.EnumFacing;
import net.minecraft.util.EnumHand;
import net.minecraft.util.math.BlockPos;
import net.minecraft.util.math.Vec3d;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import netheritemod.Recorder;

/**
 * Notes the client right-click block target so container open events can
 * bind workbench/furnace/chest windows to a world position. SPacketOpenWindow
 * does not carry BlockPos (NetHandlerPlayClient.handleOpenWindow builds a
 * LocalBlockIntercommunication / InventoryBasic), so the use-site pos is the
 * stable identity for block containers.
 */
@Mixin(PlayerControllerMP.class)
public abstract class MixinRecordBlockInteract {

    @Inject(method = "processRightClickBlock", at = @At("HEAD"))
    private void qrl$noteUsePos(EntityPlayerSP player, WorldClient worldIn,
            BlockPos pos, EnumFacing facing, Vec3d hit, EnumHand hand,
            CallbackInfoReturnable<?> cir) {
        Minecraft mc = Minecraft.getMinecraft();
        if (mc == null || !mc.isCallingFromMinecraftThread()) return;
        if (pos == null) return;
        Recorder.noteBlockInteract(pos);
    }
}
