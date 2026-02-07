package net.minecraft.client.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;
import net.minecraft.client.audio.MovingSoundMinecartRiding;
import net.minecraft.client.network.NetHandlerPlayClient;
import net.minecraft.entity.Entity;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.item.ItemStack;
import net.minecraft.network.play.client.C01PacketChatMessage;
import net.minecraft.network.play.client.C03PacketPlayer;
import net.minecraft.network.play.client.C07PacketPlayerDigging;
import net.minecraft.network.play.client.C0APacketAnimation;
import net.minecraft.network.play.client.C0BPacketEntityAction;
import net.minecraft.network.play.client.C0CPacketInput;
import net.minecraft.network.play.client.C0DPacketCloseWindow;
import net.minecraft.network.play.client.C13PacketPlayerAbilities;
import net.minecraft.network.play.client.C16PacketClientStatus;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatFileWriter;
import net.minecraft.util.DamageSource;
import net.minecraft.util.MathHelper;
import net.minecraft.util.Session;
import net.minecraft.world.World;

@SideOnly(Side.CLIENT)
public class EntityClientPlayerMP extends EntityPlayerSP
{
    public final NetHandlerPlayClient sendQueue;
    private final StatFileWriter field_146108_bO;
    private double oldPosX;
    /** Old Minimum Y of the bounding box */
    private double oldMinY;
    private double oldPosY;
    private double oldPosZ;
    private float oldRotationYaw;
    private float oldRotationPitch;
    /** Check if was on ground last update */
    private boolean wasOnGround;
    /** Used to check if the player has started or stopped sneaking and needs to tell the server. */
    private boolean wasSneaking;
    /** Used to check if the player has started or stopped sprinting and needs to tell the server. */
    private boolean wasSprinting;
    /**
     * Counter used to ensure that the server sends a move packet (Packet11, 12 or 13) to the client at least once a
     * second.
     */
    private int ticksSinceMovePacket;
    /** has the client player's health been set? */
    private boolean hasSetHealth;
    private String field_142022_ce;
    private static final String __OBFID = "CL_00000887";

    public EntityClientPlayerMP(Minecraft p_i45064_1_, World p_i45064_2_, Session p_i45064_3_, NetHandlerPlayClient p_i45064_4_, StatFileWriter p_i45064_5_)
    {
        super(p_i45064_1_, p_i45064_2_, p_i45064_3_, 0);
        this.sendQueue = p_i45064_4_;
        this.field_146108_bO = p_i45064_5_;
    }

    /**
     * Called when the entity is attacked.
     */
    public boolean attackEntityFrom(DamageSource p_70097_1_, float p_70097_2_)
    {
        return false;
    }

    /**
     * Heal living entity (param: amount of half-hearts)
     */
    public void heal(float p_70691_1_) {}

    /**
     * Called when a player mounts an entity. e.g. mounts a pig, mounts a boat.
     */
    public void mountEntity(Entity p_70078_1_)
    {
        super.mountEntity(p_70078_1_);

        if (p_70078_1_ instanceof EntityMinecart)
        {
            this.mc.getSoundHandler().playSound(new MovingSoundMinecartRiding(this, (EntityMinecart)p_70078_1_));
        }
    }

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        if (this.worldObj.blockExists(MathHelper.floor_double(this.posX), 0, MathHelper.floor_double(this.posZ)))
        {
            super.onUpdate();

            if (this.isRiding())
            {
                this.sendQueue.addToSendQueue(new C03PacketPlayer.C05PacketPlayerLook(this.rotationYaw, this.rotationPitch, this.onGround));
                this.sendQueue.addToSendQueue(new C0CPacketInput(this.moveStrafing, this.moveForward, this.movementInput.jump, this.movementInput.sneak));
            }
            else
            {
                this.sendMotionUpdates();
            }
        }
    }

    /**
     * Send updated motion and position information to the server
     */
    public void sendMotionUpdates()
    {
        boolean flag = this.isSprinting();

        if (flag != this.wasSprinting)
        {
            if (flag)
            {
                this.sendQueue.addToSendQueue(new C0BPacketEntityAction(this, 4));
            }
            else
            {
                this.sendQueue.addToSendQueue(new C0BPacketEntityAction(this, 5));
            }

            this.wasSprinting = flag;
        }

        boolean flag1 = this.isSneaking();

        if (flag1 != this.wasSneaking)
        {
            if (flag1)
            {
                this.sendQueue.addToSendQueue(new C0BPacketEntityAction(this, 1));
            }
            else
            {
                this.sendQueue.addToSendQueue(new C0BPacketEntityAction(this, 2));
            }

            this.wasSneaking = flag1;
        }

        double d0 = this.posX - this.oldPosX;
        double d1 = this.boundingBox.minY - this.oldMinY;
        double d2 = this.posZ - this.oldPosZ;
        double d3 = (double)(this.rotationYaw - this.oldRotationYaw);
        double d4 = (double)(this.rotationPitch - this.oldRotationPitch);
        boolean flag2 = d0 * d0 + d1 * d1 + d2 * d2 > 9.0E-4D || this.ticksSinceMovePacket >= 20;
        boolean flag3 = d3 != 0.0D || d4 != 0.0D;

        if (this.ridingEntity != null)
        {
            this.sendQueue.addToSendQueue(new C03PacketPlayer.C06PacketPlayerPosLook(this.motionX, -999.0D, -999.0D, this.motionZ, this.rotationYaw, this.rotationPitch, this.onGround));
            flag2 = false;
        }
        else if (flag2 && flag3)
        {
            this.sendQueue.addToSendQueue(new C03PacketPlayer.C06PacketPlayerPosLook(this.posX, this.boundingBox.minY, this.posY, this.posZ, this.rotationYaw, this.rotationPitch, this.onGround));
        }
        else if (flag2)
        {
            this.sendQueue.addToSendQueue(new C03PacketPlayer.C04PacketPlayerPosition(this.posX, this.boundingBox.minY, this.posY, this.posZ, this.onGround));
        }
        else if (flag3)
        {
            this.sendQueue.addToSendQueue(new C03PacketPlayer.C05PacketPlayerLook(this.rotationYaw, this.rotationPitch, this.onGround));
        }
        else
        {
            this.sendQueue.addToSendQueue(new C03PacketPlayer(this.onGround));
        }

        ++this.ticksSinceMovePacket;
        this.wasOnGround = this.onGround;

        if (flag2)
        {
            this.oldPosX = this.posX;
            this.oldMinY = this.boundingBox.minY;
            this.oldPosY = this.posY;
            this.oldPosZ = this.posZ;
            this.ticksSinceMovePacket = 0;
        }

        if (flag3)
        {
            this.oldRotationYaw = this.rotationYaw;
            this.oldRotationPitch = this.rotationPitch;
        }
    }

    /**
     * Called when player presses the drop item key
     */
    public EntityItem dropOneItem(boolean p_71040_1_)
    {
        int i = p_71040_1_ ? 3 : 4;
        this.sendQueue.addToSendQueue(new C07PacketPlayerDigging(i, 0, 0, 0, 0));
        return null;
    }

    /**
     * Joins the passed in entity item with the world. Args: entityItem
     */
    public void joinEntityItemWithWorld(EntityItem p_71012_1_) {}

    /**
     * Sends a chat message from the player. Args: chatMessage
     */
    public void sendChatMessage(String p_71165_1_)
    {
        this.sendQueue.addToSendQueue(new C01PacketChatMessage(p_71165_1_));
    }

    /**
     * Swings the item the player is holding.
     */
    public void swingItem()
    {
        super.swingItem();
        this.sendQueue.addToSendQueue(new C0APacketAnimation(this, 1));
    }

    public void respawnPlayer()
    {
        this.sendQueue.addToSendQueue(new C16PacketClientStatus(C16PacketClientStatus.EnumState.PERFORM_RESPAWN));
    }

    /**
     * Deals damage to the entity. If its a EntityPlayer then will take damage from the armor first and then health
     * second with the reduced value. Args: damageAmount
     */
    protected void damageEntity(DamageSource p_70665_1_, float p_70665_2_)
    {
        if (!this.isEntityInvulnerable())
        {
            this.setHealth(this.getHealth() - p_70665_2_);
        }
    }

    /**
     * sets current screen to null (used on escape buttons of GUIs); sets current crafting inventory back to the 2x2
     * square
     */
    public void closeScreen()
    {
        this.sendQueue.addToSendQueue(new C0DPacketCloseWindow(this.openContainer.windowId));
        this.closeScreenNoPacket();
    }

    /**
     * Closes the GUI screen without sending a packet to the server
     */
    public void closeScreenNoPacket()
    {
        this.inventory.setItemStack((ItemStack)null);
        super.closeScreen();
    }

    /**
     * Updates health locally.
     */
    public void setPlayerSPHealth(float p_71150_1_)
    {
        if (this.hasSetHealth)
        {
            super.setPlayerSPHealth(p_71150_1_);
        }
        else
        {
            this.setHealth(p_71150_1_);
            this.hasSetHealth = true;
        }
    }

    /**
     * Adds a value to a statistic field.
     */
    public void addStat(StatBase p_71064_1_, int p_71064_2_)
    {
        if (p_71064_1_ != null)
        {
            if (p_71064_1_.isIndependent)
            {
                super.addStat(p_71064_1_, p_71064_2_);
            }
        }
    }

    /**
     * Sends the player's abilities to the server (if there is one).
     */
    public void sendPlayerAbilities()
    {
        this.sendQueue.addToSendQueue(new C13PacketPlayerAbilities(this.capabilities));
    }

    protected void func_110318_g()
    {
        this.sendQueue.addToSendQueue(new C0BPacketEntityAction(this, 6, (int)(this.getHorseJumpPower() * 100.0F)));
    }

    public void func_110322_i()
    {
        this.sendQueue.addToSendQueue(new C0BPacketEntityAction(this, 7));
    }

    public void func_142020_c(String p_142020_1_)
    {
        this.field_142022_ce = p_142020_1_;
    }

    public String func_142021_k()
    {
        return this.field_142022_ce;
    }

    public StatFileWriter getStatFileWriter()
    {
        return this.field_146108_bO;
    }
}