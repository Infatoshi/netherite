package net.minecraft.util;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.settings.GameSettings;

@SideOnly(Side.CLIENT)
public class MovementInputFromOptions extends MovementInput
{
    private GameSettings gameSettings;
    private static final String __OBFID = "CL_00000937";

    public MovementInputFromOptions(GameSettings p_i1237_1_)
    {
        this.gameSettings = p_i1237_1_;
    }

    public void updatePlayerMoveState()
    {
        this.moveStrafe = 0.0F;
        this.moveForward = 0.0F;

        if (this.gameSettings.keyBindForward.getIsKeyPressed())
        {
            ++this.moveForward;
        }

        if (this.gameSettings.keyBindBack.getIsKeyPressed())
        {
            --this.moveForward;
        }

        if (this.gameSettings.keyBindLeft.getIsKeyPressed())
        {
            ++this.moveStrafe;
        }

        if (this.gameSettings.keyBindRight.getIsKeyPressed())
        {
            --this.moveStrafe;
        }

        this.jump = this.gameSettings.keyBindJump.getIsKeyPressed();
        this.sneak = this.gameSettings.keyBindSneak.getIsKeyPressed();

        if (this.sneak)
        {
            this.moveStrafe = (float)((double)this.moveStrafe * 0.3D);
            this.moveForward = (float)((double)this.moveForward * 0.3D);
        }
    }
}