package net.minecraft.client.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import net.minecraft.client.Minecraft;
import net.minecraft.client.audio.PositionedSoundRecord;
import net.minecraft.client.gui.GuiCommandBlock;
import net.minecraft.client.gui.GuiEnchantment;
import net.minecraft.client.gui.GuiHopper;
import net.minecraft.client.gui.GuiMerchant;
import net.minecraft.client.gui.GuiRepair;
import net.minecraft.client.gui.GuiScreen;
import net.minecraft.client.gui.GuiScreenBook;
import net.minecraft.client.gui.inventory.GuiBeacon;
import net.minecraft.client.gui.inventory.GuiBrewingStand;
import net.minecraft.client.gui.inventory.GuiChest;
import net.minecraft.client.gui.inventory.GuiCrafting;
import net.minecraft.client.gui.inventory.GuiDispenser;
import net.minecraft.client.gui.inventory.GuiEditSign;
import net.minecraft.client.gui.inventory.GuiFurnace;
import net.minecraft.client.gui.inventory.GuiScreenHorseInventory;
import net.minecraft.client.particle.EntityCrit2FX;
import net.minecraft.client.particle.EntityPickupFX;
import net.minecraft.command.server.CommandBlockLogic;
import net.minecraft.entity.Entity;
import net.minecraft.entity.IMerchant;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.item.EntityMinecartHopper;
import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.init.Items;
import net.minecraft.inventory.IInventory;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.potion.Potion;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityBeacon;
import net.minecraft.tileentity.TileEntityBrewingStand;
import net.minecraft.tileentity.TileEntityCommandBlock;
import net.minecraft.tileentity.TileEntityDispenser;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.tileentity.TileEntityHopper;
import net.minecraft.tileentity.TileEntitySign;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.DamageSource;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MouseFilter;
import net.minecraft.util.MovementInput;
import net.minecraft.util.ResourceLocation;
import net.minecraft.util.Session;
import net.minecraft.world.World;
import net.minecraftforge.client.ForgeHooksClient;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.event.entity.PlaySoundAtEntityEvent;

@SideOnly(Side.CLIENT)
public class EntityPlayerSP extends AbstractClientPlayer
{
    public MovementInput movementInput;
    protected Minecraft mc;
    /**
     * Used to tell if the player pressed forward twice. If this is at 0 and it's pressed (And they are allowed to
     * sprint, aka enough food on the ground etc) it sets this to 7. If it's pressed and it's greater than 0 enable
     * sprinting.
     */
    protected int sprintToggleTimer;
    /** Ticks left before sprinting is disabled. */
    public int sprintingTicksLeft;
    public float renderArmYaw;
    public float renderArmPitch;
    public float prevRenderArmYaw;
    public float prevRenderArmPitch;
    private int horseJumpPowerCounter;
    private float horseJumpPower;
    private MouseFilter field_71162_ch = new MouseFilter();
    private MouseFilter field_71160_ci = new MouseFilter();
    private MouseFilter field_71161_cj = new MouseFilter();
    /** The amount of time an entity has been in a Portal */
    public float timeInPortal;
    /** The amount of time an entity has been in a Portal the previous tick */
    public float prevTimeInPortal;
    private static final String __OBFID = "CL_00000938";

    public EntityPlayerSP(Minecraft p_i1238_1_, World p_i1238_2_, Session p_i1238_3_, int p_i1238_4_)
    {
        super(p_i1238_2_, p_i1238_3_.func_148256_e());
        this.mc = p_i1238_1_;
        this.dimension = p_i1238_4_;
    }

    public void updateEntityActionState()
    {
        super.updateEntityActionState();
        this.moveStrafing = this.movementInput.moveStrafe;
        this.moveForward = this.movementInput.moveForward;
        this.isJumping = this.movementInput.jump;
        this.prevRenderArmYaw = this.renderArmYaw;
        this.prevRenderArmPitch = this.renderArmPitch;
        this.renderArmPitch = (float)((double)this.renderArmPitch + (double)(this.rotationPitch - this.renderArmPitch) * 0.5D);
        this.renderArmYaw = (float)((double)this.renderArmYaw + (double)(this.rotationYaw - this.renderArmYaw) * 0.5D);
    }

    /**
     * Called frequently so the entity can update its state every tick as required. For example, zombies and skeletons
     * use this to react to sunlight and start to burn.
     */
    public void onLivingUpdate()
    {
        if (this.sprintingTicksLeft > 0)
        {
            --this.sprintingTicksLeft;

            if (this.sprintingTicksLeft == 0)
            {
                this.setSprinting(false);
            }
        }

        if (this.sprintToggleTimer > 0)
        {
            --this.sprintToggleTimer;
        }

        if (this.mc.playerController.enableEverythingIsScrewedUpMode())
        {
            this.posX = this.posZ = 0.5D;
            this.posX = 0.0D;
            this.posZ = 0.0D;
            this.rotationYaw = (float)this.ticksExisted / 12.0F;
            this.rotationPitch = 10.0F;
            this.posY = 68.5D;
        }
        else
        {
            this.prevTimeInPortal = this.timeInPortal;

            if (this.inPortal)
            {
                if (this.mc.currentScreen != null)
                {
                    this.mc.displayGuiScreen((GuiScreen)null);
                }

                if (this.timeInPortal == 0.0F)
                {
                    this.mc.getSoundHandler().playSound(PositionedSoundRecord.func_147674_a(new ResourceLocation("portal.trigger"), this.rand.nextFloat() * 0.4F + 0.8F));
                }

                this.timeInPortal += 0.0125F;

                if (this.timeInPortal >= 1.0F)
                {
                    this.timeInPortal = 1.0F;
                }

                this.inPortal = false;
            }
            else if (this.isPotionActive(Potion.confusion) && this.getActivePotionEffect(Potion.confusion).getDuration() > 60)
            {
                this.timeInPortal += 0.006666667F;

                if (this.timeInPortal > 1.0F)
                {
                    this.timeInPortal = 1.0F;
                }
            }
            else
            {
                if (this.timeInPortal > 0.0F)
                {
                    this.timeInPortal -= 0.05F;
                }

                if (this.timeInPortal < 0.0F)
                {
                    this.timeInPortal = 0.0F;
                }
            }

            if (this.timeUntilPortal > 0)
            {
                --this.timeUntilPortal;
            }

            boolean flag = this.movementInput.jump;
            float f = 0.8F;
            boolean flag1 = this.movementInput.moveForward >= f;
            this.movementInput.updatePlayerMoveState();

            if (this.isUsingItem() && !this.isRiding())
            {
                this.movementInput.moveStrafe *= 0.2F;
                this.movementInput.moveForward *= 0.2F;
                this.sprintToggleTimer = 0;
            }

            if (this.movementInput.sneak && this.ySize < 0.2F)
            {
                this.ySize = 0.2F;
            }

            this.func_145771_j(this.posX - (double)this.width * 0.35D, this.boundingBox.minY + 0.5D, this.posZ + (double)this.width * 0.35D);
            this.func_145771_j(this.posX - (double)this.width * 0.35D, this.boundingBox.minY + 0.5D, this.posZ - (double)this.width * 0.35D);
            this.func_145771_j(this.posX + (double)this.width * 0.35D, this.boundingBox.minY + 0.5D, this.posZ - (double)this.width * 0.35D);
            this.func_145771_j(this.posX + (double)this.width * 0.35D, this.boundingBox.minY + 0.5D, this.posZ + (double)this.width * 0.35D);
            boolean flag2 = (float)this.getFoodStats().getFoodLevel() > 6.0F || this.capabilities.allowFlying;

            if (this.onGround && !flag1 && this.movementInput.moveForward >= f && !this.isSprinting() && flag2 && !this.isUsingItem() && !this.isPotionActive(Potion.blindness))
            {
                if (this.sprintToggleTimer <= 0 && !this.mc.gameSettings.keyBindSprint.getIsKeyPressed())
                {
                    this.sprintToggleTimer = 7;
                }
                else
                {
                    this.setSprinting(true);
                }
            }

            if (!this.isSprinting() && this.movementInput.moveForward >= f && flag2 && !this.isUsingItem() && !this.isPotionActive(Potion.blindness) && this.mc.gameSettings.keyBindSprint.getIsKeyPressed())
            {
                this.setSprinting(true);
            }

            if (this.isSprinting() && (this.movementInput.moveForward < f || this.isCollidedHorizontally || !flag2))
            {
                this.setSprinting(false);
            }

            if (this.capabilities.allowFlying && !flag && this.movementInput.jump)
            {
                if (this.flyToggleTimer == 0)
                {
                    this.flyToggleTimer = 7;
                }
                else
                {
                    this.capabilities.isFlying = !this.capabilities.isFlying;
                    this.sendPlayerAbilities();
                    this.flyToggleTimer = 0;
                }
            }

            if (this.capabilities.isFlying)
            {
                if (this.movementInput.sneak)
                {
                    this.motionY -= 0.15D;
                }

                if (this.movementInput.jump)
                {
                    this.motionY += 0.15D;
                }
            }

            if (this.isRidingHorse())
            {
                if (this.horseJumpPowerCounter < 0)
                {
                    ++this.horseJumpPowerCounter;

                    if (this.horseJumpPowerCounter == 0)
                    {
                        this.horseJumpPower = 0.0F;
                    }
                }

                if (flag && !this.movementInput.jump)
                {
                    this.horseJumpPowerCounter = -10;
                    this.func_110318_g();
                }
                else if (!flag && this.movementInput.jump)
                {
                    this.horseJumpPowerCounter = 0;
                    this.horseJumpPower = 0.0F;
                }
                else if (flag)
                {
                    ++this.horseJumpPowerCounter;

                    if (this.horseJumpPowerCounter < 10)
                    {
                        this.horseJumpPower = (float)this.horseJumpPowerCounter * 0.1F;
                    }
                    else
                    {
                        this.horseJumpPower = 0.8F + 2.0F / (float)(this.horseJumpPowerCounter - 9) * 0.1F;
                    }
                }
            }
            else
            {
                this.horseJumpPower = 0.0F;
            }

            super.onLivingUpdate();

            if (this.onGround && this.capabilities.isFlying)
            {
                this.capabilities.isFlying = false;
                this.sendPlayerAbilities();
            }
        }
    }

    /**
     * Gets the player's field of view multiplier. (ex. when flying)
     */
    public float getFOVMultiplier()
    {
        float f = 1.0F;

        if (this.capabilities.isFlying)
        {
            f *= 1.1F;
        }

        IAttributeInstance iattributeinstance = this.getEntityAttribute(SharedMonsterAttributes.movementSpeed);
        f = (float)((double)f * ((iattributeinstance.getAttributeValue() / (double)this.capabilities.getWalkSpeed() + 1.0D) / 2.0D));

        if (this.capabilities.getWalkSpeed() == 0.0F || Float.isNaN(f) || Float.isInfinite(f))
        {
            f = 1.0F;
        }

        if (this.isUsingItem() && this.getItemInUse().getItem() == Items.bow)
        {
            int i = this.getItemInUseDuration();
            float f1 = (float)i / 20.0F;

            if (f1 > 1.0F)
            {
                f1 = 1.0F;
            }
            else
            {
                f1 *= f1;
            }

            f *= 1.0F - f1 * 0.15F;
        }

        return ForgeHooksClient.getOffsetFOV(this, f);
    }

    /**
     * sets current screen to null (used on escape buttons of GUIs); sets current crafting inventory back to the 2x2
     * square
     */
    public void closeScreen()
    {
        super.closeScreen();
        this.mc.displayGuiScreen((GuiScreen)null);
    }

    public void func_146100_a(TileEntity p_146100_1_)
    {
        if (p_146100_1_ instanceof TileEntitySign)
        {
            this.mc.displayGuiScreen(new GuiEditSign((TileEntitySign)p_146100_1_));
        }
        else if (p_146100_1_ instanceof TileEntityCommandBlock)
        {
            this.mc.displayGuiScreen(new GuiCommandBlock(((TileEntityCommandBlock)p_146100_1_).func_145993_a()));
        }
    }

    public void func_146095_a(CommandBlockLogic p_146095_1_)
    {
        this.mc.displayGuiScreen(new GuiCommandBlock(p_146095_1_));
    }

    /**
     * Displays the GUI for interacting with a book.
     */
    public void displayGUIBook(ItemStack p_71048_1_)
    {
        Item item = p_71048_1_.getItem();

        if (item == Items.written_book)
        {
            this.mc.displayGuiScreen(new GuiScreenBook(this, p_71048_1_, false));
        }
        else if (item == Items.writable_book)
        {
            this.mc.displayGuiScreen(new GuiScreenBook(this, p_71048_1_, true));
        }
    }

    /**
     * Displays the GUI for interacting with a chest inventory. Args: chestInventory
     */
    public void displayGUIChest(IInventory p_71007_1_)
    {
        this.mc.displayGuiScreen(new GuiChest(this.inventory, p_71007_1_));
    }

    public void func_146093_a(TileEntityHopper p_146093_1_)
    {
        this.mc.displayGuiScreen(new GuiHopper(this.inventory, p_146093_1_));
    }

    public void displayGUIHopperMinecart(EntityMinecartHopper p_96125_1_)
    {
        this.mc.displayGuiScreen(new GuiHopper(this.inventory, p_96125_1_));
    }

    public void displayGUIHorse(EntityHorse p_110298_1_, IInventory p_110298_2_)
    {
        this.mc.displayGuiScreen(new GuiScreenHorseInventory(this.inventory, p_110298_2_, p_110298_1_));
    }

    /**
     * Displays the crafting GUI for a workbench.
     */
    public void displayGUIWorkbench(int p_71058_1_, int p_71058_2_, int p_71058_3_)
    {
        this.mc.displayGuiScreen(new GuiCrafting(this.inventory, this.worldObj, p_71058_1_, p_71058_2_, p_71058_3_));
    }

    public void displayGUIEnchantment(int p_71002_1_, int p_71002_2_, int p_71002_3_, String p_71002_4_)
    {
        this.mc.displayGuiScreen(new GuiEnchantment(this.inventory, this.worldObj, p_71002_1_, p_71002_2_, p_71002_3_, p_71002_4_));
    }

    /**
     * Displays the GUI for interacting with an anvil.
     */
    public void displayGUIAnvil(int p_82244_1_, int p_82244_2_, int p_82244_3_)
    {
        this.mc.displayGuiScreen(new GuiRepair(this.inventory, this.worldObj, p_82244_1_, p_82244_2_, p_82244_3_));
    }

    public void func_146101_a(TileEntityFurnace p_146101_1_)
    {
        this.mc.displayGuiScreen(new GuiFurnace(this.inventory, p_146101_1_));
    }

    public void func_146098_a(TileEntityBrewingStand p_146098_1_)
    {
        this.mc.displayGuiScreen(new GuiBrewingStand(this.inventory, p_146098_1_));
    }

    public void func_146104_a(TileEntityBeacon p_146104_1_)
    {
        this.mc.displayGuiScreen(new GuiBeacon(this.inventory, p_146104_1_));
    }

    public void func_146102_a(TileEntityDispenser p_146102_1_)
    {
        this.mc.displayGuiScreen(new GuiDispenser(this.inventory, p_146102_1_));
    }

    public void displayGUIMerchant(IMerchant p_71030_1_, String p_71030_2_)
    {
        this.mc.displayGuiScreen(new GuiMerchant(this.inventory, p_71030_1_, this.worldObj, p_71030_2_));
    }

    /**
     * Called when the player performs a critical hit on the Entity. Args: entity that was hit critically
     */
    public void onCriticalHit(Entity p_71009_1_)
    {
        this.mc.effectRenderer.addEffect(new EntityCrit2FX(this.mc.theWorld, p_71009_1_));
    }

    public void onEnchantmentCritical(Entity p_71047_1_)
    {
        EntityCrit2FX entitycrit2fx = new EntityCrit2FX(this.mc.theWorld, p_71047_1_, "magicCrit");
        this.mc.effectRenderer.addEffect(entitycrit2fx);
    }

    /**
     * Called whenever an item is picked up from walking over it. Args: pickedUpEntity, stackSize
     */
    public void onItemPickup(Entity p_71001_1_, int p_71001_2_)
    {
        this.mc.effectRenderer.addEffect(new EntityPickupFX(this.mc.theWorld, p_71001_1_, this, -0.5F));
    }

    /**
     * Returns if this entity is sneaking.
     */
    public boolean isSneaking()
    {
        return this.movementInput.sneak && !this.sleeping;
    }

    /**
     * Updates health locally.
     */
    public void setPlayerSPHealth(float p_71150_1_)
    {
        float f1 = this.getHealth() - p_71150_1_;

        if (f1 <= 0.0F)
        {
            this.setHealth(p_71150_1_);

            if (f1 < 0.0F)
            {
                this.hurtResistantTime = this.maxHurtResistantTime / 2;
            }
        }
        else
        {
            this.lastDamage = f1;
            this.setHealth(this.getHealth());
            this.hurtResistantTime = this.maxHurtResistantTime;
            this.damageEntity(DamageSource.generic, f1);
            this.hurtTime = this.maxHurtTime = 10;
        }
    }

    public void addChatComponentMessage(IChatComponent p_146105_1_)
    {
        this.mc.ingameGUI.getChatGUI().printChatMessage(p_146105_1_);
    }

    private boolean isBlockTranslucent(int p_71153_1_, int p_71153_2_, int p_71153_3_)
    {
        return this.worldObj.getBlock(p_71153_1_, p_71153_2_, p_71153_3_).isNormalCube();
    }

    private boolean isHeadspaceFree(int x, int y, int z, int height)
    {
        for (int i1 = 0; i1 < height; i1++)
        {
            if (isBlockTranslucent(x, y + i1, z + 1)) return false;
        }
        return true;
    }

    protected boolean func_145771_j(double p_145771_1_, double p_145771_3_, double p_145771_5_)
    {
        if (this.noClip)
        {
            return false;
        }
        int i = MathHelper.floor_double(p_145771_1_);
        int j = MathHelper.floor_double(p_145771_3_);
        int k = MathHelper.floor_double(p_145771_5_);
        double d3 = p_145771_1_ - (double)i;
        double d4 = p_145771_5_ - (double)k;

        int entHeight = Math.max(Math.round(this.height), 1);

        boolean inTranslucentBlock = true;

        for (int i1 = 0; i1 < entHeight; i1++)
        {
            if (!this.isBlockTranslucent(i, j + i1, k))
            {
                inTranslucentBlock = false;
            }
        }

        if (inTranslucentBlock)
        {
            boolean flag  = !isHeadspaceFree(i - 1, j, k, entHeight);
            boolean flag1 = !isHeadspaceFree(i + 1, j, k, entHeight);
            boolean flag2 = !isHeadspaceFree(i, j, k - 1, entHeight);
            boolean flag3 = !isHeadspaceFree(i, j, k + 1, entHeight);
            byte b0 = -1;
            double d5 = 9999.0D;

            if (flag && d3 < d5)
            {
                d5 = d3;
                b0 = 0;
            }

            if (flag1 && 1.0D - d3 < d5)
            {
                d5 = 1.0D - d3;
                b0 = 1;
            }

            if (flag2 && d4 < d5)
            {
                d5 = d4;
                b0 = 4;
            }

            if (flag3 && 1.0D - d4 < d5)
            {
                d5 = 1.0D - d4;
                b0 = 5;
            }

            float f = 0.1F;

            if (b0 == 0)
            {
                this.motionX = (double)(-f);
            }

            if (b0 == 1)
            {
                this.motionX = (double)f;
            }

            if (b0 == 4)
            {
                this.motionZ = (double)(-f);
            }

            if (b0 == 5)
            {
                this.motionZ = (double)f;
            }
        }

        return false;
    }

    /**
     * Set sprinting switch for Entity.
     */
    public void setSprinting(boolean p_70031_1_)
    {
        super.setSprinting(p_70031_1_);
        this.sprintingTicksLeft = p_70031_1_ ? 600 : 0;
    }

    /**
     * Sets the current XP, total XP, and level number.
     */
    public void setXPStats(float p_71152_1_, int p_71152_2_, int p_71152_3_)
    {
        this.experience = p_71152_1_;
        this.experienceTotal = p_71152_2_;
        this.experienceLevel = p_71152_3_;
    }

    /**
     * Notifies this sender of some sort of information.  This is for messages intended to display to the user.  Used
     * for typical output (like "you asked for whether or not this game rule is set, so here's your answer"), warnings
     * (like "I fetched this block for you by ID, but I'd like you to know that every time you do this, I die a little
     * inside"), and errors (like "it's not called iron_pixacke, silly").
     */
    public void addChatMessage(IChatComponent p_145747_1_)
    {
        this.mc.ingameGUI.getChatGUI().printChatMessage(p_145747_1_);
    }

    /**
     * Returns true if the command sender is allowed to use the given command.
     */
    public boolean canCommandSenderUseCommand(int p_70003_1_, String p_70003_2_)
    {
        return p_70003_1_ <= 0;
    }

    /**
     * Return the position for this command sender.
     */
    public ChunkCoordinates getPlayerCoordinates()
    {
        return new ChunkCoordinates(MathHelper.floor_double(this.posX + 0.5D), MathHelper.floor_double(this.posY + 0.5D), MathHelper.floor_double(this.posZ + 0.5D));
    }

    public void playSound(String p_85030_1_, float p_85030_2_, float p_85030_3_)
    {
        PlaySoundAtEntityEvent event = new PlaySoundAtEntityEvent(this, p_85030_1_, p_85030_2_, p_85030_3_);
        if (MinecraftForge.EVENT_BUS.post(event))
        {
            return;
        }
        p_85030_1_ = event.name;
        this.worldObj.playSound(this.posX, this.posY - (double)this.yOffset, this.posZ, p_85030_1_, p_85030_2_, p_85030_3_, false);
    }

    /**
     * Returns whether the entity is in a local (client) world
     */
    public boolean isClientWorld()
    {
        return true;
    }

    public boolean isRidingHorse()
    {
        return this.ridingEntity != null && this.ridingEntity instanceof EntityHorse;
    }

    public float getHorseJumpPower()
    {
        return this.horseJumpPower;
    }

    protected void func_110318_g() {}
}