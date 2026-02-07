package net.minecraft.entity.player;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.nbt.NBTTagCompound;

public class PlayerCapabilities
{
    /** Disables player damage. */
    public boolean disableDamage;
    /** Sets/indicates whether the player is flying. */
    public boolean isFlying;
    /** whether or not to allow the player to fly when they double jump. */
    public boolean allowFlying;
    /** Used to determine if creative mode is enabled, and therefore if items should be depleted on usage */
    public boolean isCreativeMode;
    /** Indicates whether the player is allowed to modify the surroundings */
    public boolean allowEdit = true;
    private float flySpeed = 0.05F;
    private float walkSpeed = 0.1F;
    private static final String __OBFID = "CL_00001708";

    public void writeCapabilitiesToNBT(NBTTagCompound p_75091_1_)
    {
        NBTTagCompound nbttagcompound1 = new NBTTagCompound();
        nbttagcompound1.setBoolean("invulnerable", this.disableDamage);
        nbttagcompound1.setBoolean("flying", this.isFlying);
        nbttagcompound1.setBoolean("mayfly", this.allowFlying);
        nbttagcompound1.setBoolean("instabuild", this.isCreativeMode);
        nbttagcompound1.setBoolean("mayBuild", this.allowEdit);
        nbttagcompound1.setFloat("flySpeed", this.flySpeed);
        nbttagcompound1.setFloat("walkSpeed", this.walkSpeed);
        p_75091_1_.setTag("abilities", nbttagcompound1);
    }

    public void readCapabilitiesFromNBT(NBTTagCompound p_75095_1_)
    {
        if (p_75095_1_.hasKey("abilities", 10))
        {
            NBTTagCompound nbttagcompound1 = p_75095_1_.getCompoundTag("abilities");
            this.disableDamage = nbttagcompound1.getBoolean("invulnerable");
            this.isFlying = nbttagcompound1.getBoolean("flying");
            this.allowFlying = nbttagcompound1.getBoolean("mayfly");
            this.isCreativeMode = nbttagcompound1.getBoolean("instabuild");

            if (nbttagcompound1.hasKey("flySpeed", 99))
            {
                this.flySpeed = nbttagcompound1.getFloat("flySpeed");
                this.walkSpeed = nbttagcompound1.getFloat("walkSpeed");
            }

            if (nbttagcompound1.hasKey("mayBuild", 1))
            {
                this.allowEdit = nbttagcompound1.getBoolean("mayBuild");
            }
        }
    }

    public float getFlySpeed()
    {
        return this.flySpeed;
    }

    @SideOnly(Side.CLIENT)
    public void setFlySpeed(float p_75092_1_)
    {
        this.flySpeed = p_75092_1_;
    }

    public float getWalkSpeed()
    {
        return this.walkSpeed;
    }

    @SideOnly(Side.CLIENT)
    public void setPlayerWalkSpeed(float p_82877_1_)
    {
        this.walkSpeed = p_82877_1_;
    }
}