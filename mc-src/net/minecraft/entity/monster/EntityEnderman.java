package net.minecraft.entity.monster;

import java.util.IdentityHashMap;
import java.util.UUID;
import com.google.common.collect.Maps;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.entity.Entity;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.attributes.AttributeModifier;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.DamageSource;
import net.minecraft.util.EntityDamageSource;
import net.minecraft.util.EntityDamageSourceIndirect;
import net.minecraft.util.MathHelper;
import net.minecraft.util.Vec3;
import net.minecraft.world.World;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.event.entity.living.EnderTeleportEvent;

public class EntityEnderman extends EntityMob
{
    private static final UUID attackingSpeedBoostModifierUUID = UUID.fromString("020E0DFB-87AE-4653-9556-831010E291A0");
    private static final AttributeModifier attackingSpeedBoostModifier = (new AttributeModifier(attackingSpeedBoostModifierUUID, "Attacking speed boost", 6.199999809265137D, 0)).setSaved(false);
    @Deprecated //DO NOT TOUCH THIS EVER
    private static boolean[] carriableBlocks = new boolean[256];
    /** Counter to delay the teleportation of an enderman towards the currently attacked target */
    private int teleportDelay;
    /** A player must stare at an enderman for 5 ticks before it becomes aggressive. This field counts those ticks. */
    private int stareTimer;
    private Entity lastEntityToAttack;
    private boolean isAggressive;
    private static final String __OBFID = "CL_00001685";

    public EntityEnderman(World p_i1734_1_)
    {
        super(p_i1734_1_);
        this.setSize(0.6F, 2.9F);
        this.stepHeight = 1.0F;
    }

    protected void applyEntityAttributes()
    {
        super.applyEntityAttributes();
        this.getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue(40.0D);
        this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(0.30000001192092896D);
        this.getEntityAttribute(SharedMonsterAttributes.attackDamage).setBaseValue(7.0D);
    }

    protected void entityInit()
    {
        super.entityInit();
        this.dataWatcher.addObject(16, new Byte((byte)0));
        this.dataWatcher.addObject(17, new Byte((byte)0));
        this.dataWatcher.addObject(18, new Byte((byte)0));
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        super.writeEntityToNBT(p_70014_1_);
        p_70014_1_.setShort("carried", (short)Block.getIdFromBlock(this.func_146080_bZ()));
        p_70014_1_.setShort("carriedData", (short)this.getCarryingData());
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        super.readEntityFromNBT(p_70037_1_);
        this.func_146081_a(Block.getBlockById(p_70037_1_.getShort("carried")));
        this.setCarryingData(p_70037_1_.getShort("carriedData"));
    }

    /**
     * Finds the closest player within 16 blocks to attack, or null if this Entity isn't interested in attacking
     * (Animals, Spiders at day, peaceful PigZombies).
     */
    protected Entity findPlayerToAttack()
    {
        EntityPlayer entityplayer = this.worldObj.getClosestVulnerablePlayerToEntity(this, 64.0D);

        if (entityplayer != null)
        {
            if (this.shouldAttackPlayer(entityplayer))
            {
                this.isAggressive = true;

                if (this.stareTimer == 0)
                {
                    this.worldObj.playSoundEffect(entityplayer.posX, entityplayer.posY, entityplayer.posZ, "mob.endermen.stare", 1.0F, 1.0F);
                }

                if (this.stareTimer++ == 5)
                {
                    this.stareTimer = 0;
                    this.setScreaming(true);
                    return entityplayer;
                }
            }
            else
            {
                this.stareTimer = 0;
            }
        }

        return null;
    }

    /**
     * Checks to see if this enderman should be attacking this player
     */
    private boolean shouldAttackPlayer(EntityPlayer p_70821_1_)
    {
        ItemStack itemstack = p_70821_1_.inventory.armorInventory[3];

        if (itemstack != null && itemstack.getItem() == Item.getItemFromBlock(Blocks.pumpkin))
        {
            return false;
        }
        else
        {
            Vec3 vec3 = p_70821_1_.getLook(1.0F).normalize();
            Vec3 vec31 = Vec3.createVectorHelper(this.posX - p_70821_1_.posX, this.boundingBox.minY + (double)(this.height / 2.0F) - (p_70821_1_.posY + (double)p_70821_1_.getEyeHeight()), this.posZ - p_70821_1_.posZ);
            double d0 = vec31.lengthVector();
            vec31 = vec31.normalize();
            double d1 = vec3.dotProduct(vec31);
            return d1 > 1.0D - 0.025D / d0 && p_70821_1_.canEntityBeSeen(this);
        }
    }

    /**
     * Called frequently so the entity can update its state every tick as required. For example, zombies and skeletons
     * use this to react to sunlight and start to burn.
     */
    public void onLivingUpdate()
    {
        if (this.isWet())
        {
            this.attackEntityFrom(DamageSource.drown, 1.0F);
        }

        if (this.lastEntityToAttack != this.entityToAttack)
        {
            IAttributeInstance iattributeinstance = this.getEntityAttribute(SharedMonsterAttributes.movementSpeed);
            iattributeinstance.removeModifier(attackingSpeedBoostModifier);

            if (this.entityToAttack != null)
            {
                iattributeinstance.applyModifier(attackingSpeedBoostModifier);
            }
        }

        this.lastEntityToAttack = this.entityToAttack;
        int k;

        if (!this.worldObj.isRemote && this.worldObj.getGameRules().getGameRuleBooleanValue("mobGriefing"))
        {
            int i;
            int j;
            Block block;

            if (this.func_146080_bZ().getMaterial() == Material.air)
            {
                if (this.rand.nextInt(20) == 0)
                {
                    k = MathHelper.floor_double(this.posX - 2.0D + this.rand.nextDouble() * 4.0D);
                    i = MathHelper.floor_double(this.posY + this.rand.nextDouble() * 3.0D);
                    j = MathHelper.floor_double(this.posZ - 2.0D + this.rand.nextDouble() * 4.0D);
                    block = this.worldObj.getBlock(k, i, j);

                    if (EntityEnderman.getCarriable(block))
                    {
                        this.func_146081_a(block);
                        this.setCarryingData(this.worldObj.getBlockMetadata(k, i, j));
                        this.worldObj.setBlock(k, i, j, Blocks.air);
                    }
                }
            }
            else if (this.rand.nextInt(2000) == 0)
            {
                k = MathHelper.floor_double(this.posX - 1.0D + this.rand.nextDouble() * 2.0D);
                i = MathHelper.floor_double(this.posY + this.rand.nextDouble() * 2.0D);
                j = MathHelper.floor_double(this.posZ - 1.0D + this.rand.nextDouble() * 2.0D);
                block = this.worldObj.getBlock(k, i, j);
                Block block1 = this.worldObj.getBlock(k, i - 1, j);

                if (block.getMaterial() == Material.air && block1.getMaterial() != Material.air && block1.renderAsNormalBlock())
                {
                    this.worldObj.setBlock(k, i, j, this.func_146080_bZ(), this.getCarryingData(), 3);
                    this.func_146081_a(Blocks.air);
                }
            }
        }

        for (k = 0; k < 2; ++k)
        {
            this.worldObj.spawnParticle("portal", this.posX + (this.rand.nextDouble() - 0.5D) * (double)this.width, this.posY + this.rand.nextDouble() * (double)this.height - 0.25D, this.posZ + (this.rand.nextDouble() - 0.5D) * (double)this.width, (this.rand.nextDouble() - 0.5D) * 2.0D, -this.rand.nextDouble(), (this.rand.nextDouble() - 0.5D) * 2.0D);
        }

        if (this.worldObj.isDaytime() && !this.worldObj.isRemote)
        {
            float f = this.getBrightness(1.0F);

            if (f > 0.5F && this.worldObj.canBlockSeeTheSky(MathHelper.floor_double(this.posX), MathHelper.floor_double(this.posY), MathHelper.floor_double(this.posZ)) && this.rand.nextFloat() * 30.0F < (f - 0.4F) * 2.0F)
            {
                this.entityToAttack = null;
                this.setScreaming(false);
                this.isAggressive = false;
                this.teleportRandomly();
            }
        }

        if (this.isWet() || this.isBurning())
        {
            this.entityToAttack = null;
            this.setScreaming(false);
            this.isAggressive = false;
            this.teleportRandomly();
        }

        if (this.isScreaming() && !this.isAggressive && this.rand.nextInt(100) == 0)
        {
            this.setScreaming(false);
        }

        this.isJumping = false;

        if (this.entityToAttack != null)
        {
            this.faceEntity(this.entityToAttack, 100.0F, 100.0F);
        }

        if (!this.worldObj.isRemote && this.isEntityAlive())
        {
            if (this.entityToAttack != null)
            {
                if (this.entityToAttack instanceof EntityPlayer && this.shouldAttackPlayer((EntityPlayer)this.entityToAttack))
                {
                    if (this.entityToAttack.getDistanceSqToEntity(this) < 16.0D)
                    {
                        this.teleportRandomly();
                    }

                    this.teleportDelay = 0;
                }
                else if (this.entityToAttack.getDistanceSqToEntity(this) > 256.0D && this.teleportDelay++ >= 30 && this.teleportToEntity(this.entityToAttack))
                {
                    this.teleportDelay = 0;
                }
            }
            else
            {
                this.setScreaming(false);
                this.teleportDelay = 0;
            }
        }

        super.onLivingUpdate();
    }

    /**
     * Teleport the enderman to a random nearby position
     */
    protected boolean teleportRandomly()
    {
        double d0 = this.posX + (this.rand.nextDouble() - 0.5D) * 64.0D;
        double d1 = this.posY + (double)(this.rand.nextInt(64) - 32);
        double d2 = this.posZ + (this.rand.nextDouble() - 0.5D) * 64.0D;
        return this.teleportTo(d0, d1, d2);
    }

    /**
     * Teleport the enderman to another entity
     */
    protected boolean teleportToEntity(Entity p_70816_1_)
    {
        Vec3 vec3 = Vec3.createVectorHelper(this.posX - p_70816_1_.posX, this.boundingBox.minY + (double)(this.height / 2.0F) - p_70816_1_.posY + (double)p_70816_1_.getEyeHeight(), this.posZ - p_70816_1_.posZ);
        vec3 = vec3.normalize();
        double d0 = 16.0D;
        double d1 = this.posX + (this.rand.nextDouble() - 0.5D) * 8.0D - vec3.xCoord * d0;
        double d2 = this.posY + (double)(this.rand.nextInt(16) - 8) - vec3.yCoord * d0;
        double d3 = this.posZ + (this.rand.nextDouble() - 0.5D) * 8.0D - vec3.zCoord * d0;
        return this.teleportTo(d1, d2, d3);
    }

    /**
     * Teleport the enderman
     */
    protected boolean teleportTo(double p_70825_1_, double p_70825_3_, double p_70825_5_)
    {
        EnderTeleportEvent event = new EnderTeleportEvent(this, p_70825_1_, p_70825_3_, p_70825_5_, 0);
        if (MinecraftForge.EVENT_BUS.post(event)){
            return false;
        }
        double d3 = this.posX;
        double d4 = this.posY;
        double d5 = this.posZ;
        this.posX = event.targetX;
        this.posY = event.targetY;
        this.posZ = event.targetZ;
        boolean flag = false;
        int i = MathHelper.floor_double(this.posX);
        int j = MathHelper.floor_double(this.posY);
        int k = MathHelper.floor_double(this.posZ);

        if (this.worldObj.blockExists(i, j, k))
        {
            boolean flag1 = false;

            while (!flag1 && j > 0)
            {
                Block block = this.worldObj.getBlock(i, j - 1, k);

                if (block.getMaterial().blocksMovement())
                {
                    flag1 = true;
                }
                else
                {
                    --this.posY;
                    --j;
                }
            }

            if (flag1)
            {
                this.setPosition(this.posX, this.posY, this.posZ);

                if (this.worldObj.getCollidingBoundingBoxes(this, this.boundingBox).isEmpty() && !this.worldObj.isAnyLiquid(this.boundingBox))
                {
                    flag = true;
                }
            }
        }

        if (!flag)
        {
            this.setPosition(d3, d4, d5);
            return false;
        }
        else
        {
            short short1 = 128;

            for (int l = 0; l < short1; ++l)
            {
                double d6 = (double)l / ((double)short1 - 1.0D);
                float f = (this.rand.nextFloat() - 0.5F) * 0.2F;
                float f1 = (this.rand.nextFloat() - 0.5F) * 0.2F;
                float f2 = (this.rand.nextFloat() - 0.5F) * 0.2F;
                double d7 = d3 + (this.posX - d3) * d6 + (this.rand.nextDouble() - 0.5D) * (double)this.width * 2.0D;
                double d8 = d4 + (this.posY - d4) * d6 + this.rand.nextDouble() * (double)this.height;
                double d9 = d5 + (this.posZ - d5) * d6 + (this.rand.nextDouble() - 0.5D) * (double)this.width * 2.0D;
                this.worldObj.spawnParticle("portal", d7, d8, d9, (double)f, (double)f1, (double)f2);
            }

            this.worldObj.playSoundEffect(d3, d4, d5, "mob.endermen.portal", 1.0F, 1.0F);
            this.playSound("mob.endermen.portal", 1.0F, 1.0F);
            return true;
        }
    }

    /**
     * Returns the sound this mob makes while it's alive.
     */
    protected String getLivingSound()
    {
        return this.isScreaming() ? "mob.endermen.scream" : "mob.endermen.idle";
    }

    /**
     * Returns the sound this mob makes when it is hurt.
     */
    protected String getHurtSound()
    {
        return "mob.endermen.hit";
    }

    /**
     * Returns the sound this mob makes on death.
     */
    protected String getDeathSound()
    {
        return "mob.endermen.death";
    }

    protected Item getDropItem()
    {
        return Items.ender_pearl;
    }

    /**
     * Drop 0-2 items of this living's type. @param par1 - Whether this entity has recently been hit by a player. @param
     * par2 - Level of Looting used to kill this mob.
     */
    protected void dropFewItems(boolean p_70628_1_, int p_70628_2_)
    {
        Item item = this.getDropItem();

        if (item != null)
        {
            int j = this.rand.nextInt(2 + p_70628_2_);

            for (int k = 0; k < j; ++k)
            {
                this.dropItem(item, 1);
            }
        }
    }

    public void func_146081_a(Block p_146081_1_)
    {
        this.dataWatcher.updateObject(16, Byte.valueOf((byte)(Block.getIdFromBlock(p_146081_1_) & 255)));
    }

    public Block func_146080_bZ()
    {
        return Block.getBlockById(this.dataWatcher.getWatchableObjectByte(16));
    }

    /**
     * Set the metadata of the block an enderman carries
     */
    public void setCarryingData(int p_70817_1_)
    {
        this.dataWatcher.updateObject(17, Byte.valueOf((byte)(p_70817_1_ & 255)));
    }

    /**
     * Get the metadata of the block an enderman carries
     */
    public int getCarryingData()
    {
        return this.dataWatcher.getWatchableObjectByte(17);
    }

    /**
     * Called when the entity is attacked.
     */
    public boolean attackEntityFrom(DamageSource p_70097_1_, float p_70097_2_)
    {
        if (this.isEntityInvulnerable())
        {
            return false;
        }
        else
        {
            this.setScreaming(true);

            if (p_70097_1_ instanceof EntityDamageSource && p_70097_1_.getEntity() instanceof EntityPlayer)
            {
                this.isAggressive = true;
            }

            if (p_70097_1_ instanceof EntityDamageSourceIndirect)
            {
                this.isAggressive = false;

                for (int i = 0; i < 64; ++i)
                {
                    if (this.teleportRandomly())
                    {
                        return true;
                    }
                }

                return super.attackEntityFrom(p_70097_1_, p_70097_2_);
            }
            else
            {
                return super.attackEntityFrom(p_70097_1_, p_70097_2_);
            }
        }
    }

    public boolean isScreaming()
    {
        return this.dataWatcher.getWatchableObjectByte(18) > 0;
    }

    public void setScreaming(boolean p_70819_1_)
    {
        this.dataWatcher.updateObject(18, Byte.valueOf((byte)(p_70819_1_ ? 1 : 0)));
    }

    static
    {
        carriableBlocks[Block.getIdFromBlock(Blocks.grass)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.dirt)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.sand)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.gravel)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.yellow_flower)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.red_flower)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.brown_mushroom)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.red_mushroom)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.tnt)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.cactus)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.clay)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.pumpkin)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.melon_block)] = true;
        carriableBlocks[Block.getIdFromBlock(Blocks.mycelium)] = true;
        for (int x = 0; x < carriableBlocks.length; x++)
        {
            if (carriableBlocks[x]) setCarriable(Block.getBlockById(x), true);
        }
    }

    /*===================================== Forge Start ==============================*/
    private static IdentityHashMap<Block, Boolean> carriable;
    public static void setCarriable(Block block, boolean canCarry)
    {
        if (carriable == null) carriable = new IdentityHashMap(4096);
        carriable.put(block, canCarry);
    }
    public static boolean getCarriable(Block block)
    {
        Boolean ret = carriable.get(block);
        return ret != null ? ret : false;
    }
    /*===================================== Forge End ==============================*/
}