package net.minecraft.server.management;

import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.item.ItemStack;
import net.minecraft.item.ItemSword;
import net.minecraft.network.play.server.S23PacketBlockChange;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraft.world.WorldSettings;
import net.minecraftforge.common.ForgeHooks;
import net.minecraftforge.common.MinecraftForge;
import cpw.mods.fml.common.eventhandler.Event;
import net.minecraftforge.event.ForgeEventFactory;
import net.minecraftforge.event.entity.player.PlayerDestroyItemEvent;
import net.minecraftforge.event.entity.player.PlayerInteractEvent;
import net.minecraftforge.event.entity.player.PlayerInteractEvent.Action;
import net.minecraftforge.event.world.BlockEvent;

public class ItemInWorldManager
{
    /** Forge reach distance */
    private double blockReachDistance = 5.0d;
    /** The world object that this object is connected to. */
    public World theWorld;
    /** The EntityPlayerMP object that this object is connected to. */
    public EntityPlayerMP thisPlayerMP;
    private WorldSettings.GameType gameType;
    /** True if the player is destroying a block */
    private boolean isDestroyingBlock;
    private int initialDamage;
    private int partiallyDestroyedBlockX;
    private int partiallyDestroyedBlockY;
    private int partiallyDestroyedBlockZ;
    private int curblockDamage;
    /**
     * Set to true when the "finished destroying block" packet is received but the block wasn't fully damaged yet. The
     * block will not be destroyed while this is false.
     */
    private boolean receivedFinishDiggingPacket;
    private int posX;
    private int posY;
    private int posZ;
    private int initialBlockDamage;
    private int durabilityRemainingOnBlock;
    private static final String __OBFID = "CL_00001442";

    public ItemInWorldManager(World p_i1524_1_)
    {
        this.gameType = WorldSettings.GameType.NOT_SET;
        this.durabilityRemainingOnBlock = -1;
        this.theWorld = p_i1524_1_;
    }

    public void setGameType(WorldSettings.GameType p_73076_1_)
    {
        this.gameType = p_73076_1_;
        p_73076_1_.configurePlayerCapabilities(this.thisPlayerMP.capabilities);
        this.thisPlayerMP.sendPlayerAbilities();
    }

    public WorldSettings.GameType getGameType()
    {
        return this.gameType;
    }

    /**
     * Get if we are in creative game mode.
     */
    public boolean isCreative()
    {
        return this.gameType.isCreative();
    }

    /**
     * if the gameType is currently NOT_SET then change it to par1
     */
    public void initializeGameType(WorldSettings.GameType p_73077_1_)
    {
        if (this.gameType == WorldSettings.GameType.NOT_SET)
        {
            this.gameType = p_73077_1_;
        }

        this.setGameType(this.gameType);
    }

    public void updateBlockRemoving()
    {
        ++this.curblockDamage;
        float f;
        int j;

        if (this.receivedFinishDiggingPacket)
        {
            int i = this.curblockDamage - this.initialBlockDamage;
            Block block = this.theWorld.getBlock(this.posX, this.posY, this.posZ);

            if (block.getMaterial() == Material.air)
            {
                this.receivedFinishDiggingPacket = false;
            }
            else
            {
                f = block.getPlayerRelativeBlockHardness(this.thisPlayerMP, this.thisPlayerMP.worldObj, this.posX, this.posY, this.posZ) * (float)(i + 1);
                j = (int)(f * 10.0F);

                if (j != this.durabilityRemainingOnBlock)
                {
                    this.theWorld.destroyBlockInWorldPartially(this.thisPlayerMP.getEntityId(), this.posX, this.posY, this.posZ, j);
                    this.durabilityRemainingOnBlock = j;
                }

                if (f >= 1.0F)
                {
                    this.receivedFinishDiggingPacket = false;
                    this.tryHarvestBlock(this.posX, this.posY, this.posZ);
                }
            }
        }
        else if (this.isDestroyingBlock)
        {
            Block block1 = this.theWorld.getBlock(this.partiallyDestroyedBlockX, this.partiallyDestroyedBlockY, this.partiallyDestroyedBlockZ);

            if (block1.getMaterial() == Material.air)
            {
                this.theWorld.destroyBlockInWorldPartially(this.thisPlayerMP.getEntityId(), this.partiallyDestroyedBlockX, this.partiallyDestroyedBlockY, this.partiallyDestroyedBlockZ, -1);
                this.durabilityRemainingOnBlock = -1;
                this.isDestroyingBlock = false;
            }
            else
            {
                int k = this.curblockDamage - this.initialDamage;
                f = block1.getPlayerRelativeBlockHardness(this.thisPlayerMP, this.thisPlayerMP.worldObj, this.partiallyDestroyedBlockX, this.partiallyDestroyedBlockY, this.partiallyDestroyedBlockZ) * (float)(k + 1);
                j = (int)(f * 10.0F);

                if (j != this.durabilityRemainingOnBlock)
                {
                    this.theWorld.destroyBlockInWorldPartially(this.thisPlayerMP.getEntityId(), this.partiallyDestroyedBlockX, this.partiallyDestroyedBlockY, this.partiallyDestroyedBlockZ, j);
                    this.durabilityRemainingOnBlock = j;
                }
            }
        }
    }

    /**
     * if not creative, it calls destroyBlockInWorldPartially untill the block is broken first. par4 is the specific
     * side. tryHarvestBlock can also be the result of this call
     */
    public void onBlockClicked(int p_73074_1_, int p_73074_2_, int p_73074_3_, int p_73074_4_)
    {
        if (!this.gameType.isAdventure() || this.thisPlayerMP.isCurrentToolAdventureModeExempt(p_73074_1_, p_73074_2_, p_73074_3_))
        {
            PlayerInteractEvent event = ForgeEventFactory.onPlayerInteract(thisPlayerMP, Action.LEFT_CLICK_BLOCK, p_73074_1_, p_73074_2_, p_73074_3_, p_73074_4_, theWorld);
            if (event.isCanceled())
            {
                thisPlayerMP.playerNetServerHandler.sendPacket(new S23PacketBlockChange(p_73074_1_, p_73074_2_, p_73074_3_, theWorld));
                return;
            }

            if (this.isCreative())
            {
                if (!this.theWorld.extinguishFire((EntityPlayer)null, p_73074_1_, p_73074_2_, p_73074_3_, p_73074_4_))
                {
                    this.tryHarvestBlock(p_73074_1_, p_73074_2_, p_73074_3_);
                }
            }
            else
            {
                this.initialDamage = this.curblockDamage;
                float f = 1.0F;
                Block block = this.theWorld.getBlock(p_73074_1_, p_73074_2_, p_73074_3_);


                if (!block.isAir(theWorld, p_73074_1_, p_73074_2_, p_73074_3_))
                {
                    if (event.useBlock != Event.Result.DENY)
                    {
                        block.onBlockClicked(theWorld, p_73074_1_, p_73074_2_, p_73074_3_, thisPlayerMP);
                        theWorld.extinguishFire(null, p_73074_1_, p_73074_2_, p_73074_3_, p_73074_4_);
                    }
                    else
                    {
                        thisPlayerMP.playerNetServerHandler.sendPacket(new S23PacketBlockChange(p_73074_1_, p_73074_2_, p_73074_3_, theWorld));
                    }
                    f = block.getPlayerRelativeBlockHardness(thisPlayerMP, thisPlayerMP.worldObj, p_73074_1_, p_73074_2_, p_73074_3_);
                }

                if (event.useItem == Event.Result.DENY)
                {
                    if (f >= 1.0f)
                    {
                        thisPlayerMP.playerNetServerHandler.sendPacket(new S23PacketBlockChange(p_73074_1_, p_73074_2_, p_73074_3_, theWorld));
                    }
                    return;
                }

                if (!block.isAir(theWorld, p_73074_1_, p_73074_2_, p_73074_3_) && f >= 1.0F)
                {
                    this.tryHarvestBlock(p_73074_1_, p_73074_2_, p_73074_3_);
                }
                else
                {
                    this.isDestroyingBlock = true;
                    this.partiallyDestroyedBlockX = p_73074_1_;
                    this.partiallyDestroyedBlockY = p_73074_2_;
                    this.partiallyDestroyedBlockZ = p_73074_3_;
                    int i1 = (int)(f * 10.0F);
                    this.theWorld.destroyBlockInWorldPartially(this.thisPlayerMP.getEntityId(), p_73074_1_, p_73074_2_, p_73074_3_, i1);
                    this.durabilityRemainingOnBlock = i1;
                }
            }
        }
    }

    public void uncheckedTryHarvestBlock(int p_73082_1_, int p_73082_2_, int p_73082_3_)
    {
        if (p_73082_1_ == this.partiallyDestroyedBlockX && p_73082_2_ == this.partiallyDestroyedBlockY && p_73082_3_ == this.partiallyDestroyedBlockZ)
        {
            int l = this.curblockDamage - this.initialDamage;
            Block block = this.theWorld.getBlock(p_73082_1_, p_73082_2_, p_73082_3_);

            if (!block.isAir(theWorld, p_73082_1_, p_73082_2_, p_73082_3_))
            {
                float f = block.getPlayerRelativeBlockHardness(this.thisPlayerMP, this.thisPlayerMP.worldObj, p_73082_1_, p_73082_2_, p_73082_3_) * (float)(l + 1);

                if (f >= 0.7F)
                {
                    this.isDestroyingBlock = false;
                    this.theWorld.destroyBlockInWorldPartially(this.thisPlayerMP.getEntityId(), p_73082_1_, p_73082_2_, p_73082_3_, -1);
                    this.tryHarvestBlock(p_73082_1_, p_73082_2_, p_73082_3_);
                }
                else if (!this.receivedFinishDiggingPacket)
                {
                    this.isDestroyingBlock = false;
                    this.receivedFinishDiggingPacket = true;
                    this.posX = p_73082_1_;
                    this.posY = p_73082_2_;
                    this.posZ = p_73082_3_;
                    this.initialBlockDamage = this.initialDamage;
                }
            }
        }
    }

    /**
     * note: this ignores the pars passed in and continues to destroy the onClickedBlock
     */
    public void cancelDestroyingBlock(int p_73073_1_, int p_73073_2_, int p_73073_3_)
    {
        this.isDestroyingBlock = false;
        this.theWorld.destroyBlockInWorldPartially(this.thisPlayerMP.getEntityId(), this.partiallyDestroyedBlockX, this.partiallyDestroyedBlockY, this.partiallyDestroyedBlockZ, -1);
    }

    /**
     * Removes a block and triggers the appropriate events
     */
    private boolean removeBlock(int p_73079_1_, int p_73079_2_, int p_73079_3_)
    {
        return removeBlock(p_73079_1_, p_73079_2_, p_73079_3_, false);
    }
    private boolean removeBlock(int p_73079_1_, int p_73079_2_, int p_73079_3_, boolean canHarvest)
    {
        Block block = this.theWorld.getBlock(p_73079_1_, p_73079_2_, p_73079_3_);
        int l = this.theWorld.getBlockMetadata(p_73079_1_, p_73079_2_, p_73079_3_);
        block.onBlockHarvested(this.theWorld, p_73079_1_, p_73079_2_, p_73079_3_, l, this.thisPlayerMP);
        boolean flag = block.removedByPlayer(theWorld, thisPlayerMP, p_73079_1_, p_73079_2_, p_73079_3_, canHarvest);

        if (flag)
        {
            block.onBlockDestroyedByPlayer(this.theWorld, p_73079_1_, p_73079_2_, p_73079_3_, l);
        }

        return flag;
    }

    /**
     * Attempts to harvest a block at the given coordinate
     */
    public boolean tryHarvestBlock(int p_73084_1_, int p_73084_2_, int p_73084_3_)
    {
        BlockEvent.BreakEvent event = ForgeHooks.onBlockBreakEvent(theWorld, gameType, thisPlayerMP, p_73084_1_, p_73084_2_, p_73084_3_);
        if (event.isCanceled())
        {
            return false;
        }
        else
        {
            ItemStack stack = thisPlayerMP.getCurrentEquippedItem();
            if (stack != null && stack.getItem().onBlockStartBreak(stack, p_73084_1_, p_73084_2_, p_73084_3_, thisPlayerMP))
            {
                return false;
            }
            Block block = this.theWorld.getBlock(p_73084_1_, p_73084_2_, p_73084_3_);
            int l = this.theWorld.getBlockMetadata(p_73084_1_, p_73084_2_, p_73084_3_);
            this.theWorld.playAuxSFXAtEntity(this.thisPlayerMP, 2001, p_73084_1_, p_73084_2_, p_73084_3_, Block.getIdFromBlock(block) + (this.theWorld.getBlockMetadata(p_73084_1_, p_73084_2_, p_73084_3_) << 12));
            boolean flag = false;

            if (this.isCreative())
            {
                flag = this.removeBlock(p_73084_1_, p_73084_2_, p_73084_3_);
                this.thisPlayerMP.playerNetServerHandler.sendPacket(new S23PacketBlockChange(p_73084_1_, p_73084_2_, p_73084_3_, this.theWorld));
            }
            else
            {
                ItemStack itemstack = this.thisPlayerMP.getCurrentEquippedItem();
                boolean flag1 = block.canHarvestBlock(thisPlayerMP, l);

                if (itemstack != null)
                {
                    itemstack.func_150999_a(this.theWorld, block, p_73084_1_, p_73084_2_, p_73084_3_, this.thisPlayerMP);

                    if (itemstack.stackSize == 0)
                    {
                        this.thisPlayerMP.destroyCurrentEquippedItem();
                    }
                }

                flag = this.removeBlock(p_73084_1_, p_73084_2_, p_73084_3_, flag1);
                if (flag && flag1)
                {
                    block.harvestBlock(this.theWorld, this.thisPlayerMP, p_73084_1_, p_73084_2_, p_73084_3_, l);
                }
            }

            // Drop experience
            if (!this.isCreative() && flag && event != null)
            {
                block.dropXpOnBlockBreak(this.theWorld, p_73084_1_, p_73084_2_, p_73084_3_, event.getExpToDrop());
            }
            return flag;
        }
    }

    /**
     * Attempts to right-click use an item by the given EntityPlayer in the given World
     */
    public boolean tryUseItem(EntityPlayer p_73085_1_, World p_73085_2_, ItemStack p_73085_3_)
    {
        int i = p_73085_3_.stackSize;
        int j = p_73085_3_.getItemDamage();
        ItemStack itemstack1 = p_73085_3_.useItemRightClick(p_73085_2_, p_73085_1_);

        if (itemstack1 == p_73085_3_ && (itemstack1 == null || itemstack1.stackSize == i && itemstack1.getMaxItemUseDuration() <= 0 && itemstack1.getItemDamage() == j))
        {
            return false;
        }
        else
        {
            p_73085_1_.inventory.mainInventory[p_73085_1_.inventory.currentItem] = itemstack1;

            if (this.isCreative())
            {
                itemstack1.stackSize = i;

                if (itemstack1.isItemStackDamageable())
                {
                    itemstack1.setItemDamage(j);
                }
            }

            if (itemstack1.stackSize == 0)
            {
                p_73085_1_.inventory.mainInventory[p_73085_1_.inventory.currentItem] = null;
                MinecraftForge.EVENT_BUS.post(new PlayerDestroyItemEvent(thisPlayerMP, itemstack1));
            }

            if (!p_73085_1_.isUsingItem())
            {
                ((EntityPlayerMP)p_73085_1_).sendContainerToPlayer(p_73085_1_.inventoryContainer);
            }

            return true;
        }
    }

    /**
     * Activate the clicked on block, otherwise use the held item. Args: player, world, itemStack, x, y, z, side,
     * xOffset, yOffset, zOffset
     */
    public boolean activateBlockOrUseItem(EntityPlayer p_73078_1_, World p_73078_2_, ItemStack p_73078_3_, int p_73078_4_, int p_73078_5_, int p_73078_6_, int p_73078_7_, float p_73078_8_, float p_73078_9_, float p_73078_10_)
    {
        PlayerInteractEvent event = ForgeEventFactory.onPlayerInteract(p_73078_1_, Action.RIGHT_CLICK_BLOCK, p_73078_4_, p_73078_5_, p_73078_6_, p_73078_7_, p_73078_2_);
        if (event.isCanceled())
        {
            thisPlayerMP.playerNetServerHandler.sendPacket(new S23PacketBlockChange(p_73078_4_, p_73078_5_, p_73078_6_, theWorld));
            return false;
        }

        if (p_73078_3_ != null && p_73078_3_.getItem().onItemUseFirst(p_73078_3_, p_73078_1_, p_73078_2_, p_73078_4_, p_73078_5_, p_73078_6_, p_73078_7_, p_73078_8_, p_73078_9_, p_73078_10_))
        {
            if (p_73078_3_.stackSize <= 0) ForgeEventFactory.onPlayerDestroyItem(thisPlayerMP, p_73078_3_);
            return true;
        }

        Block block = p_73078_2_.getBlock(p_73078_4_, p_73078_5_, p_73078_6_);
        boolean isAir = block.isAir(p_73078_2_, p_73078_4_, p_73078_5_, p_73078_6_);
        boolean useBlock = !p_73078_1_.isSneaking() || p_73078_1_.getHeldItem() == null;
        if (!useBlock) useBlock = p_73078_1_.getHeldItem().getItem().doesSneakBypassUse(p_73078_2_, p_73078_4_, p_73078_5_, p_73078_6_, p_73078_1_);
        boolean result = false;

        if (useBlock)
        {
            if (event.useBlock != Event.Result.DENY)
            {
                result = block.onBlockActivated(p_73078_2_, p_73078_4_, p_73078_5_, p_73078_6_, p_73078_1_, p_73078_7_, p_73078_8_, p_73078_9_, p_73078_10_);
            }
            else
            {
                thisPlayerMP.playerNetServerHandler.sendPacket(new S23PacketBlockChange(p_73078_4_, p_73078_5_, p_73078_6_, theWorld));
                result = event.useItem != Event.Result.ALLOW;
            }
        }

        if (p_73078_3_ != null && !result && event.useItem != Event.Result.DENY)
        {
            int meta = p_73078_3_.getItemDamage();
            int size = p_73078_3_.stackSize;
            result = p_73078_3_.tryPlaceItemIntoWorld(p_73078_1_, p_73078_2_, p_73078_4_, p_73078_5_, p_73078_6_, p_73078_7_, p_73078_8_, p_73078_9_, p_73078_10_);
            if (isCreative())
            {
                p_73078_3_.setItemDamage(meta);
                p_73078_3_.stackSize = size;
            }
            if (p_73078_3_.stackSize <= 0) ForgeEventFactory.onPlayerDestroyItem(thisPlayerMP, p_73078_3_);
        }

        /* Re-enable if this causes bukkit incompatibility, or re-write client side to only send a single packet per right click.
        if (par3ItemStack != null && ((!result && event.useItem != Event.Result.DENY) || event.useItem == Event.Result.ALLOW))
        {
            this.tryUseItem(thisPlayerMP, par2World, par3ItemStack);
        }*/
        return result;
    }

    /**
     * Sets the world instance.
     */
    public void setWorld(WorldServer p_73080_1_)
    {
        this.theWorld = p_73080_1_;
    }

    public double getBlockReachDistance()
    {
        return blockReachDistance;
    }
    public void setBlockReachDistance(double distance)
    {
        blockReachDistance = distance;
    }
}