package net.minecraft.entity.passive;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.List;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityAgeable;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.stats.AchievementList;
import net.minecraft.stats.StatList;
import net.minecraft.util.DamageSource;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;

public abstract class EntityAnimal extends EntityAgeable implements IAnimals
{
    private int inLove;
    /**
     * This is representation of a counter for reproduction progress. (Note that this is different from the inLove which
     * represent being in Love-Mode)
     */
    private int breeding;
    private EntityPlayer field_146084_br;
    private static final String __OBFID = "CL_00001638";

    public EntityAnimal(World p_i1681_1_)
    {
        super(p_i1681_1_);
    }

    /**
     * main AI tick function, replaces updateEntityActionState
     */
    protected void updateAITick()
    {
        if (this.getGrowingAge() != 0)
        {
            this.inLove = 0;
        }

        super.updateAITick();
    }

    /**
     * Called frequently so the entity can update its state every tick as required. For example, zombies and skeletons
     * use this to react to sunlight and start to burn.
     */
    public void onLivingUpdate()
    {
        super.onLivingUpdate();

        if (this.getGrowingAge() != 0)
        {
            this.inLove = 0;
        }

        if (this.inLove > 0)
        {
            --this.inLove;
            String s = "heart";

            if (this.inLove % 10 == 0)
            {
                double d0 = this.rand.nextGaussian() * 0.02D;
                double d1 = this.rand.nextGaussian() * 0.02D;
                double d2 = this.rand.nextGaussian() * 0.02D;
                this.worldObj.spawnParticle(s, this.posX + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, this.posY + 0.5D + (double)(this.rand.nextFloat() * this.height), this.posZ + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, d0, d1, d2);
            }
        }
        else
        {
            this.breeding = 0;
        }
    }

    /**
     * Basic mob attack. Default to touch of death in EntityCreature. Overridden by each mob to define their attack.
     */
    protected void attackEntity(Entity p_70785_1_, float p_70785_2_)
    {
        if (p_70785_1_ instanceof EntityPlayer)
        {
            if (p_70785_2_ < 3.0F)
            {
                double d0 = p_70785_1_.posX - this.posX;
                double d1 = p_70785_1_.posZ - this.posZ;
                this.rotationYaw = (float)(Math.atan2(d1, d0) * 180.0D / Math.PI) - 90.0F;
                this.hasAttacked = true;
            }

            EntityPlayer entityplayer = (EntityPlayer)p_70785_1_;

            if (entityplayer.getCurrentEquippedItem() == null || !this.isBreedingItem(entityplayer.getCurrentEquippedItem()))
            {
                this.entityToAttack = null;
            }
        }
        else if (p_70785_1_ instanceof EntityAnimal)
        {
            EntityAnimal entityanimal = (EntityAnimal)p_70785_1_;

            if (this.getGrowingAge() > 0 && entityanimal.getGrowingAge() < 0)
            {
                if ((double)p_70785_2_ < 2.5D)
                {
                    this.hasAttacked = true;
                }
            }
            else if (this.inLove > 0 && entityanimal.inLove > 0)
            {
                if (entityanimal.entityToAttack == null)
                {
                    entityanimal.entityToAttack = this;
                }

                if (entityanimal.entityToAttack == this && (double)p_70785_2_ < 3.5D)
                {
                    ++entityanimal.inLove;
                    ++this.inLove;
                    ++this.breeding;

                    if (this.breeding % 4 == 0)
                    {
                        this.worldObj.spawnParticle("heart", this.posX + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, this.posY + 0.5D + (double)(this.rand.nextFloat() * this.height), this.posZ + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, 0.0D, 0.0D, 0.0D);
                    }

                    if (this.breeding == 60)
                    {
                        this.procreate((EntityAnimal)p_70785_1_);
                    }
                }
                else
                {
                    this.breeding = 0;
                }
            }
            else
            {
                this.breeding = 0;
                this.entityToAttack = null;
            }
        }
    }

    /**
     * Creates a baby animal according to the animal type of the target at the actual position and spawns 'love'
     * particles.
     */
    private void procreate(EntityAnimal p_70876_1_)
    {
        EntityAgeable entityageable = this.createChild(p_70876_1_);

        if (entityageable != null)
        {
            if (this.field_146084_br == null && p_70876_1_.func_146083_cb() != null)
            {
                this.field_146084_br = p_70876_1_.func_146083_cb();
            }

            if (this.field_146084_br != null)
            {
                this.field_146084_br.triggerAchievement(StatList.field_151186_x);

                if (this instanceof EntityCow)
                {
                    this.field_146084_br.triggerAchievement(AchievementList.field_150962_H);
                }
            }

            this.setGrowingAge(6000);
            p_70876_1_.setGrowingAge(6000);
            this.inLove = 0;
            this.breeding = 0;
            this.entityToAttack = null;
            p_70876_1_.entityToAttack = null;
            p_70876_1_.breeding = 0;
            p_70876_1_.inLove = 0;
            entityageable.setGrowingAge(-24000);
            entityageable.setLocationAndAngles(this.posX, this.posY, this.posZ, this.rotationYaw, this.rotationPitch);

            for (int i = 0; i < 7; ++i)
            {
                double d0 = this.rand.nextGaussian() * 0.02D;
                double d1 = this.rand.nextGaussian() * 0.02D;
                double d2 = this.rand.nextGaussian() * 0.02D;
                this.worldObj.spawnParticle("heart", this.posX + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, this.posY + 0.5D + (double)(this.rand.nextFloat() * this.height), this.posZ + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, d0, d1, d2);
            }

            this.worldObj.spawnEntityInWorld(entityageable);
        }
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
            this.fleeingTick = 60;

            if (!this.isAIEnabled())
            {
                IAttributeInstance iattributeinstance = this.getEntityAttribute(SharedMonsterAttributes.movementSpeed);

                if (iattributeinstance.getModifier(field_110179_h) == null)
                {
                    iattributeinstance.applyModifier(field_110181_i);
                }
            }

            this.entityToAttack = null;
            this.inLove = 0;
            return super.attackEntityFrom(p_70097_1_, p_70097_2_);
        }
    }

    /**
     * Takes a coordinate in and returns a weight to determine how likely this creature will try to path to the block.
     * Args: x, y, z
     */
    public float getBlockPathWeight(int p_70783_1_, int p_70783_2_, int p_70783_3_)
    {
        return this.worldObj.getBlock(p_70783_1_, p_70783_2_ - 1, p_70783_3_) == Blocks.grass ? 10.0F : this.worldObj.getLightBrightness(p_70783_1_, p_70783_2_, p_70783_3_) - 0.5F;
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        super.writeEntityToNBT(p_70014_1_);
        p_70014_1_.setInteger("InLove", this.inLove);
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        super.readEntityFromNBT(p_70037_1_);
        this.inLove = p_70037_1_.getInteger("InLove");
    }

    /**
     * Finds the closest player within 16 blocks to attack, or null if this Entity isn't interested in attacking
     * (Animals, Spiders at day, peaceful PigZombies).
     */
    protected Entity findPlayerToAttack()
    {
        if (this.fleeingTick > 0)
        {
            return null;
        }
        else
        {
            float f = 8.0F;
            List list;
            int i;
            EntityAnimal entityanimal;

            if (this.inLove > 0)
            {
                list = this.worldObj.getEntitiesWithinAABB(this.getClass(), this.boundingBox.expand((double)f, (double)f, (double)f));

                for (i = 0; i < list.size(); ++i)
                {
                    entityanimal = (EntityAnimal)list.get(i);

                    if (entityanimal != this && entityanimal.inLove > 0)
                    {
                        return entityanimal;
                    }
                }
            }
            else if (this.getGrowingAge() == 0)
            {
                list = this.worldObj.getEntitiesWithinAABB(EntityPlayer.class, this.boundingBox.expand((double)f, (double)f, (double)f));

                for (i = 0; i < list.size(); ++i)
                {
                    EntityPlayer entityplayer = (EntityPlayer)list.get(i);

                    if (entityplayer.getCurrentEquippedItem() != null && this.isBreedingItem(entityplayer.getCurrentEquippedItem()))
                    {
                        return entityplayer;
                    }
                }
            }
            else if (this.getGrowingAge() > 0)
            {
                list = this.worldObj.getEntitiesWithinAABB(this.getClass(), this.boundingBox.expand((double)f, (double)f, (double)f));

                for (i = 0; i < list.size(); ++i)
                {
                    entityanimal = (EntityAnimal)list.get(i);

                    if (entityanimal != this && entityanimal.getGrowingAge() < 0)
                    {
                        return entityanimal;
                    }
                }
            }

            return null;
        }
    }

    /**
     * Checks if the entity's current position is a valid location to spawn this entity.
     */
    public boolean getCanSpawnHere()
    {
        int i = MathHelper.floor_double(this.posX);
        int j = MathHelper.floor_double(this.boundingBox.minY);
        int k = MathHelper.floor_double(this.posZ);
        return this.worldObj.getBlock(i, j - 1, k) == Blocks.grass && this.worldObj.getFullBlockLightValue(i, j, k) > 8 && super.getCanSpawnHere();
    }

    /**
     * Get number of ticks, at least during which the living entity will be silent.
     */
    public int getTalkInterval()
    {
        return 120;
    }

    /**
     * Determines if an entity can be despawned, used on idle far away entities
     */
    protected boolean canDespawn()
    {
        return false;
    }

    /**
     * Get the experience points the entity currently has.
     */
    protected int getExperiencePoints(EntityPlayer p_70693_1_)
    {
        return 1 + this.worldObj.rand.nextInt(3);
    }

    /**
     * Checks if the parameter is an item which this animal can be fed to breed it (wheat, carrots or seeds depending on
     * the animal type)
     */
    public boolean isBreedingItem(ItemStack p_70877_1_)
    {
        return p_70877_1_.getItem() == Items.wheat;
    }

    /**
     * Called when a player interacts with a mob. e.g. gets milk from a cow, gets into the saddle on a pig.
     */
    public boolean interact(EntityPlayer p_70085_1_)
    {
        ItemStack itemstack = p_70085_1_.inventory.getCurrentItem();

        if (itemstack != null && this.isBreedingItem(itemstack) && this.getGrowingAge() == 0 && this.inLove <= 0)
        {
            if (!p_70085_1_.capabilities.isCreativeMode)
            {
                --itemstack.stackSize;

                if (itemstack.stackSize <= 0)
                {
                    p_70085_1_.inventory.setInventorySlotContents(p_70085_1_.inventory.currentItem, (ItemStack)null);
                }
            }

            this.func_146082_f(p_70085_1_);
            return true;
        }
        else
        {
            return super.interact(p_70085_1_);
        }
    }

    public void func_146082_f(EntityPlayer p_146082_1_)
    {
        this.inLove = 600;
        this.field_146084_br = p_146082_1_;
        this.entityToAttack = null;
        this.worldObj.setEntityState(this, (byte)18);
    }

    public EntityPlayer func_146083_cb()
    {
        return this.field_146084_br;
    }

    /**
     * Returns if the entity is currently in 'love mode'.
     */
    public boolean isInLove()
    {
        return this.inLove > 0;
    }

    public void resetInLove()
    {
        this.inLove = 0;
    }

    /**
     * Returns true if the mob is currently able to mate with the specified mob.
     */
    public boolean canMateWith(EntityAnimal p_70878_1_)
    {
        return p_70878_1_ == this ? false : (p_70878_1_.getClass() != this.getClass() ? false : this.isInLove() && p_70878_1_.isInLove());
    }

    @SideOnly(Side.CLIENT)
    public void handleHealthUpdate(byte p_70103_1_)
    {
        if (p_70103_1_ == 18)
        {
            for (int i = 0; i < 7; ++i)
            {
                double d0 = this.rand.nextGaussian() * 0.02D;
                double d1 = this.rand.nextGaussian() * 0.02D;
                double d2 = this.rand.nextGaussian() * 0.02D;
                this.worldObj.spawnParticle("heart", this.posX + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, this.posY + 0.5D + (double)(this.rand.nextFloat() * this.height), this.posZ + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, d0, d1, d2);
            }
        }
        else
        {
            super.handleHealthUpdate(p_70103_1_);
        }
    }
}