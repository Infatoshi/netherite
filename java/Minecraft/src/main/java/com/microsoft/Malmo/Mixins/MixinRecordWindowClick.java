package com.microsoft.Malmo.Mixins;

import net.minecraft.client.multiplayer.PlayerControllerMP;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.inventory.ClickType;
import net.minecraft.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import qrl.Recorder;

/** Records semantic container actions after vanilla has resolved slotClick. */
@Mixin(PlayerControllerMP.class)
public abstract class MixinRecordWindowClick {
    @Inject(method = "windowClick", at = @At("RETURN"))
    private void qrl$recordWindowClick(int windowId, int slotId,
            int mouseButton, ClickType type, EntityPlayer player,
            CallbackInfoReturnable<ItemStack> cir) {
        Recorder.recordGuiWindowClick(
            windowId, slotId, mouseButton, type, cir.getReturnValue());
    }

    @Inject(method = "sendEnchantPacket", at = @At("HEAD"))
    private void qrl$recordEnchant(int windowId, int button,
            CallbackInfo ci) {
        Recorder.recordGuiEnchant(windowId, button);
    }

    @Inject(method = "sendSlotPacket", at = @At("HEAD"))
    private void qrl$recordCreativeSlot(ItemStack stack, int slotId,
            CallbackInfo ci) {
        Recorder.recordGuiCreativeSlot(slotId, stack, false);
    }

    @Inject(method = "sendPacketDropItem", at = @At("HEAD"))
    private void qrl$recordCreativeDrop(ItemStack stack, CallbackInfo ci) {
        Recorder.recordGuiCreativeSlot(-1, stack, true);
    }
}
