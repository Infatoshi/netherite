package com.microsoft.Malmo.Mixins;

import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.inventory.GuiContainer;
import net.minecraft.inventory.ClickType;
import net.minecraft.inventory.Slot;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import netheritemod.Recorder;

/** Records every human GuiContainer.handleMouseClick for tape replay.
 * Authoritative client path: vanilla resolves slotIn.slotNumber then
 * PlayerControllerMP.windowClick. Capture the resolved slot id, mouse button,
 * ClickType, and openContainer.windowId so replay can re-issue
 * gm_container_click in order without inferring clicks from post-tick
 * gslots/gcur render truth. */
@Mixin(GuiContainer.class)
public abstract class MixinRecordGuiClick {

    @Inject(method = "handleMouseClick", at = @At("HEAD"))
    private void qrl$recordGuiClick(Slot slotIn, int slotId, int mouseButton,
            ClickType type, CallbackInfo ci) {
        Minecraft mc = Minecraft.getMinecraft();
        if (mc == null || !mc.isCallingFromMinecraftThread()) return;
        /* Mirror GuiContainer.handleMouseClick: non-null slot wins over the
         * caller-supplied slotId (including the -999 outside-GUI sentinel). */
        int resolved = slotIn != null ? slotIn.slotNumber : slotId;
        String gui = ((Object) this).getClass().getSimpleName();
        int wid = 0;
        if (mc.player != null && mc.player.openContainer != null)
            wid = mc.player.openContainer.windowId;
        Recorder.recordGuiContainerClick(gui, resolved, mouseButton, type, wid);
    }
}
