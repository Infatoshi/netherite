package net.minecraft.entity.boss;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.command.IEntitySelector;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.EnumCreatureAttribute;
import net.minecraft.entity.IRangedAttackMob;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.EntityAIArrowAttack;
import net.minecraft.entity.ai.EntityAIHurtByTarget;
import net.minecraft.entity.ai.EntityAILookIdle;
import net.minecraft.entity.ai.EntityAINearestAttackableTarget;
import net.minecraft.entity.ai.EntityAISwimming;
import net.minecraft.entity.ai.EntityAIWander;
import net.minecraft.entity.ai.EntityAIWatchClosest;
import net.minecraft.entity.monster.EntityMob;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.entity.projectile.EntityWitherSkull;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.potion.PotionEffect;
import net.minecraft.stats.AchievementList;
import net.minecraft.util.DamageSource;
import net.minecraft.util.MathHelper;
import net.minecraft.world.EnumDifficulty;
import net.minecraft.world.World;

public class EntityWither extends EntityMob implements IBossDisplayData, IRangedAttackMob
{
    private float[] field_82220_d = new float[2];
    private float[] field_82221_e = new float[2];
    private float[] field_82217_f = new float[2];
    private float[] field_82218_g = new float[2];
    private int[] field_82223_h = new int[2];
    private int[] field_82224_i = new int[2];
    private int field_82222_j;
    /** Selector used to determine the entities a wither boss should attack. */
    private static final IEntitySelector attackEntitySelector = new IEntitySelector()
    {
        private static final String __OBFID = "CL_00001662";
        /**
         * Return whether the specified entity is applicable to this filter.
         */
        public boolean isEntityApplicable(Entity p_82704_1_)
        {
            return p_82704_1_ instanceof EntityLivingBase && ((EntityLivingBase)p_82704_1_).getCreatureAttribute() != EnumCreatureAttribute.UNDEAD;
        }
    };
    private static final String __OBFID = "CL_00001661";

    public EntityWither(World p_i1701_1_)
    {
        super(p_i1701_1_);
        this.setHealth(this.getMaxHealth());
        this.setSize(0.9F, 4.0F);
        this.isImmuneToFire = true;
        this.getNavigator().setCanSwim(true);
        this.tasks.addTask(0, new EntityAISwimming(this));
        this.tasks.addTask(2, new EntityAIArrowAttack(this, 1.0D, 40, 20.0F));
        this.tasks.addTask(5, new EntityAIWander(this, 1.0D));
        this.tasks.addTask(6, new EntityAIWatchClosest(this, EntityPlayer.class, 8.0F));
        this.tasks.addTask(7, new EntityAILookIdle(this));
        this.targetTasks.addTask(1, new EntityAIHurtByTarget(this, false));
        this.targetTasks.addTask(2, new EntityAINearestAttackableTarget(this, EntityLiving.class, 0, false, false, attackEntitySelector));
        this.experienceValue = 50;
    }

    protected void entityInit()
    {
        super.entityInit();
        this.dataWatcher.addObject(17, new Integer(0));
        this.dataWatcher.addObject(18, new Integer(0));
        this.dataWatcher.addObject(19, new Integer(0));
        this.dataWatcher.addObject(20, new Integer(0));
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        super.writeEntityToNBT(p_70014_1_);
        p_70014_1_.setInteger("Invul", this.func_82212_n());
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        super.readEntityFromNBT(p_70037_1_);
        this.func_82215_s(p_70037_1_.getInteger("Invul"));
    }

    @SideOnly(Side.CLIENT)
    public float getShadowSize()
    {
        return this.height / 8.0F;
    }

    /**
     * Returns the sound this mob makes while it's alive.
     */
    protected String getLivingSound()
    {
        return "mob.wither.idle";
    }

    /**
     * Returns the sound this mob makes when it is hurt.
     */
    protected String getHurtSound()
    {
        return "mob.wither.hurt";
    }

    /**
     * Returns the sound this mob makes on death.
     */
    protected String getDeathSound()
    {
        return "mob.wither.death";
    }

    /**
     * Called frequently so the entity can update its state every tick as required. For example, zombies and skeletons
     * use this to react to sunlight and start to burn.
     */
    public void onLivingUpdate()
    {
        this.motionY *= 0.6000000238418579D;
        double d1;
        double d3;
        double d5;

        if (!this.worldObj.isRemote && this.getWatchedTargetId(0) > 0)
        {
            Entity entity = this.worldObj.getEntityByID(this.getWatchedTargetId(0));

            if (entity != null)
            {
                if (this.posY < entity.posY || !this.isArmored() && this.posY < entity.posY + 5.0D)
                {
                    if (this.motionY < 0.0D)
                    {
                        this.motionY = 0.0D;
                    }

                    this.motionY += (0.5D - this.motionY) * 0.6000000238418579D;
                }

                double d0 = entity.posX - this.posX;
                d1 = entity.posZ - this.posZ;
                d3 = d0 * d0 + d1 * d1;

                if (d3 > 9.0D)
                {
                    d5 = (double)MathHelper.sqrt_double(d3);
                    this.motionX += (d0 / d5 * 0.5D - this.motionX) * 0.6000000238418579D;
                    this.motionZ += (d1 / d5 * 0.5D - this.motionZ) * 0.6000000238418579D;
                }
            }
        }

        if (this.motionX * this.motionX + this.motionZ * this.motionZ > 0.05000000074505806D)
        {
            this.rotationYaw = (float)Math.atan2(this.motionZ, this.motionX) * (180F / (float)Math.PI) - 90.0F;
        }

        super.onLivingUpdate();
        int i;

        for (i = 0; i < 2; ++i)
        {
            this.field_82218_g[i] = this.field_82221_e[i];
            this.field_82217_f[i] = this.field_82220_d[i];
        }

        int j;

        for (i = 0; i < 2; ++i)
        {
            j = this.getWatchedTargetId(i + 1);
            Entity entity1 = null;

            if (j > 0)
            {
                entity1 = this.worldObj.getEntityByID(j);
            }

            if (entity1 != null)
            {
                d1 = this.func_82214_u(i + 1);
                d3 = this.func_82208_v(i + 1);
                d5 = this.func_82213_w(i + 1);
                double d6 = entity1.posX - d1;
                double d7 = entity1.posY + (double)entity1.getEyeHeight() - d3;
                double d8 = entity1.posZ - d5;
                double d9 = (double)MathHelper.sqrt_double(d6 * d6 + d8 * d8);
                float f = (float)(Math.atan2(d8, d6) * 180.0D / Math.PI) - 90.0F;
                float f1 = (float)(-(Math.atan2(d7, d9) * 180.0D / Math.PI));
                this.field_82220_d[i] = this.func_82204_b(this.field_82220_d[i], f1, 40.0F);
                this.field_82221_e[i] = this.func_82204_b(this.field_82221_e[i], f, 10.0F);
            }
            else
            {
                this.field_82221_e[i] = this.func_82204_b(this.field_82221_e[i], this.renderYawOffset, 10.0F);
            }
        }

        boolean flag = this.isArmored();

        for (j = 0; j < 3; ++j)
        {
            double d10 = this.func_82214_u(j);
            double d2 = this.func_82208_v(j);
            double d4 = this.func_82213_w(j);
            this.worldObj.spawnParticle("smoke", d10 + this.rand.nextGaussian() * 0.30000001192092896D, d2 + this.rand.nextGaussian() * 0.30000001192092896D, d4 + this.rand.nextGaussian() * 0.30000001192092896D, 0.0D, 0.0D, 0.0D);

            if (flag && this.worldObj.rand.nextInt(4) == 0)
            {
                this.worldObj.spawnParticle("mobSpell", d10 + this.rand.nextGaussian() * 0.30000001192092896D, d2 + this.rand.nextGaussian() * 0.30000001192092896D, d4 + this.rand.nextGaussian() * 0.30000001192092896D, 0.699999988079071D, 0.699999988079071D, 0.5D);
            }
        }

        if (this.func_82212_n() > 0)
        {
            for (j = 0; j < 3; ++j)
            {
                this.worldObj.spawnParticle("mobSpell", this.posX + this.rand.nextGaussian() * 1.0D, this.posY + (double)(this.rand.nextFloat() * 3.3F), this.posZ + this.rand.nextGaussian() * 1.0D, 0.699999988079071D, 0.699999988079071D, 0.8999999761581421D);
            }
        }
    }

    protected void updateAITasks()
    {
        int i;

        if (this.func_82212_n() > 0)
        {
            i = this.func_82212_n() - 1;

            if (i <= 0)
            {
                this.worldObj.newExplosion(this, this.posX, this.posY + (double)this.getEyeHeight(), this.posZ, 7.0F, false, this.worldObj.getGameRules().getGameRuleBooleanValue("mobGriefing"));
                this.worldObj.playBroadcastSound(1013, (int)this.posX, (int)this.posY, (int)this.posZ, 0);
            }

            this.func_82215_s(i);

            if (this.ticksExisted % 10 == 0)
            {
                this.heal(10.0F);
            }
        }
        else
        {
            super.updateAITasks();
            int i1;

            for (i = 1; i < 3; ++i)
            {
                if (this.ticksExisted >= this.field_82223_h[i - 1])
                {
                    this.field_82223_h[i - 1] = this.ticksExisted + 10 + this.rand.nextInt(10);

                    if (this.worldObj.difficultySetting == EnumDifficulty.NORMAL || this.worldObj.difficultySetting == EnumDifficulty.HARD)
                    {
                        int k2 = i - 1;
                        int l2 = this.field_82224_i[i - 1];
                        this.field_82224_i[k2] = this.field_82224_i[i - 1] + 1;

                        if (l2 > 15)
                        {
                            float f = 10.0F;
                            float f1 = 5.0F;
                            double d0 = MathHelper.getRandomDoubleInRange(this.rand, this.posX - (double)f, this.posX + (double)f);
                            double d1 = MathHelper.getRandomDoubleInRange(this.rand, this.posY - (double)f1, this.posY + (double)f1);
                            double d2 = MathHelper.getRandomDoubleInRange(this.rand, this.posZ - (double)f, this.posZ + (double)f);
                            this.func_82209_a(i + 1, d0, d1, d2, true);
                            this.field_82224_i[i - 1] = 0;
                        }
                    }

                    i1 = this.getWatchedTargetId(i);

                    if (i1 > 0)
                    {
                        Entity entity = this.worldObj.getEntityByID(i1);

                        if (entity != null && entity.isEntityAlive() && this.getDistanceSqToEntity(entity) <= 900.0D && this.canEntityBeSeen(entity))
                        {
                            this.func_82216_a(i + 1, (EntityLivingBase)entity);
                            this.field_82223_h[i - 1] = this.ticksExisted + 40 + this.rand.nextInt(20);
                            this.field_82224_i[i - 1] = 0;
                        }
                        else
                        {
                            this.func_82211_c(i, 0);
                        }
                    }
                    else
                    {
                        List list = this.worldObj.selectEntitiesWithinAABB(EntityLivingBase.class, this.boundingBox.expand(20.0D, 8.0D, 20.0D), attackEntitySelector);

                        for (int k1 = 0; k1 < 10 && !list.isEmpty(); ++k1)
                        {
                            EntityLivingBase entitylivingbase = (EntityLivingBase)list.get(this.rand.nextInt(list.size()));

                            if (entitylivingbase != this && entitylivingbase.isEntityAlive() && this.canEntityBeSeen(entitylivingbase))
                            {
                                if (entitylivingbase instanceof EntityPlayer)
                                {
                                    if (!((EntityPlayer)entitylivingbase).capabilities.disableDamage)
                                    {
                                        this.func_82211_c(i, entitylivingbase.getEntityId());
                                    }
                                }
                                else
                                {
                                    this.func_82211_c(i, entitylivingbase.getEntityId());
                                }

                                break;
                            }

                            list.remove(entitylivingbase);
                        }
                    }
                }
            }

            if (this.getAttackTarget() != null)
            {
                this.func_82211_c(0, this.getAttackTarget().getEntityId());
            }
            else
            {
                this.func_82211_c(0, 0);
            }

            if (this.field_82222_j > 0)
            {
                --this.field_82222_j;

                if (this.field_82222_j == 0 && this.worldObj.getGameRules().getGameRuleBooleanValue("mobGriefing"))
                {
                    i = MathHelper.floor_double(this.posY);
                    i1 = MathHelper.floor_double(this.posX);
                    int j1 = MathHelper.floor_double(this.posZ);
                    boolean flag = false;

                    for (int l1 = -1; l1 <= 1; ++l1)
                    {
                        for (int i2 = -1; i2 <= 1; ++i2)
                        {
                            for (int j = 0; j <= 3; ++j)
                            {
                                int j2 = i1 + l1;
                                int k = i + j;
                                int l = j1 + i2;
                                Block block = this.worldObj.getBlock(j2, k, l);

                                if (!block.isAir(worldObj, j2, k, l) && block.canEntityDestroy(worldObj, j2, k, l, this))
                                {
                                    flag = this.worldObj.func_147480_a(j2, k, l, true) || flag;
                                }
                            }
                        }
                    }

                    if (flag)
                    {
                        this.worldObj.playAuxSFXAtEntity((EntityPlayer)null, 1012, (int)this.posX, (int)this.posY, (int)this.posZ, 0);
                    }
                }
            }

            if (this.ticksExisted % 20 == 0)
            {
                this.heal(1.0F);
            }
        }
    }

    public void func_82206_m()
    {
        this.func_82215_s(220);
        this.setHealth(this.getMaxHealth() / 3.0F);
    }

    /**
     * Sets the Entity inside a web block.
     */
    public void setInWeb() {}

    /**
     * Returns the current armor value as determined by a call to InventoryPlayer.getTotalArmorValue
     */
    public int getTotalArmorValue()
    {
        return 4;
    }

    private double func_82214_u(int p_82214_1_)
    {
        if (p_82214_1_ <= 0)
        {
            return this.posX;
        }
        else
        {
            float f = (this.renderYawOffset + (float)(180 * (p_82214_1_ - 1))) / 180.0F * (float)Math.PI;
            float f1 = MathHelper.cos(f);
            return this.posX + (double)f1 * 1.3D;
        }
    }

    private double func_82208_v(int p_82208_1_)
    {
        return p_82208_1_ <= 0 ? this.posY + 3.0D : this.posY + 2.2D;
    }

    private double func_82213_w(int p_82213_1_)
    {
        if (p_82213_1_ <= 0)
        {
            return this.posZ;
        }
        else
        {
            float f = (this.renderYawOffset + (float)(180 * (p_82213_1_ - 1))) / 180.0F * (float)Math.PI;
            float f1 = MathHelper.sin(f);
            return this.posZ + (double)f1 * 1.3D;
        }
    }

    private float func_82204_b(float p_82204_1_, float p_82204_2_, float p_82204_3_)
    {
        float f3 = MathHelper.wrapAngleTo180_float(p_82204_2_ - p_82204_1_);

        if (f3 > p_82204_3_)
        {
            f3 = p_82204_3_;
        }

        if (f3 < -p_82204_3_)
        {
            f3 = -p_82204_3_;
        }

        return p_82204_1_ + f3;
    }

    private void func_82216_a(int p_82216_1_, EntityLivingBase p_82216_2_)
    {
        this.func_82209_a(p_82216_1_, p_82216_2_.posX, p_82216_2_.posY + (double)p_82216_2_.getEyeHeight() * 0.5D, p_82216_2_.posZ, p_82216_1_ == 0 && this.rand.nextFloat() < 0.001F);
    }

    private void func_82209_a(int p_82209_1_, double p_82209_2_, double p_82209_4_, double p_82209_6_, boolean p_82209_8_)
    {
        this.worldObj.playAuxSFXAtEntity((EntityPlayer)null, 1014, (int)this.posX, (int)this.posY, (int)this.posZ, 0);
        double d3 = this.func_82214_u(p_82209_1_);
        double d4 = this.func_82208_v(p_82209_1_);
        double d5 = this.func_82213_w(p_82209_1_);
        double d6 = p_82209_2_ - d3;
        double d7 = p_82209_4_ - d4;
        double d8 = p_82209_6_ - d5;
        EntityWitherSkull entitywitherskull = new EntityWitherSkull(this.worldObj, this, d6, d7, d8);

        if (p_82209_8_)
        {
            entitywitherskull.setInvulnerable(true);
        }

        entitywitherskull.posY = d4;
        entitywitherskull.posX = d3;
        entitywitherskull.posZ = d5;
        this.worldObj.spawnEntityInWorld(entitywitherskull);
    }

    /**
     * Attack the specified entity using a ranged attack.
     */
    public void attackEntityWithRangedAttack(EntityLivingBase p_82196_1_, float p_82196_2_)
    {
        this.func_82216_a(0, p_82196_1_);
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
        else if (p_70097_1_ == DamageSource.drown)
        {
            return false;
        }
        else if (this.func_82212_n() > 0)
        {
            return false;
        }
        else
        {
            Entity entity;

            if (this.isArmored())
            {
                entity = p_70097_1_.getSourceOfDamage();

                if (entity instanceof EntityArrow)
                {
                    return false;
                }
            }

            entity = p_70097_1_.getEntity();

            if (entity != null && !(entity instanceof EntityPlayer) && entity instanceof EntityLivingBase && ((EntityLivingBase)entity).getCreatureAttribute() == this.getCreatureAttribute())
            {
                return false;
            }
            else
            {
                if (this.field_82222_j <= 0)
                {
                    this.field_82222_j = 20;
                }

                for (int i = 0; i < this.field_82224_i.length; ++i)
                {
                    this.field_82224_i[i] += 3;
                }

                return super.attackEntityFrom(p_70097_1_, p_70097_2_);
            }
        }
    }

    /**
     * Drop 0-2 items of this living's type. @param par1 - Whether this entity has recently been hit by a player. @param
     * par2 - Level of Looting used to kill this mob.
     */
    protected void dropFewItems(boolean p_70628_1_, int p_70628_2_)
    {
        this.dropItem(Items.nether_star, 1);

        if (!this.worldObj.isRemote)
        {
            Iterator iterator = this.worldObj.getEntitiesWithinAABB(EntityPlayer.class, this.boundingBox.expand(50.0D, 100.0D, 50.0D)).iterator();

            while (iterator.hasNext())
            {
                EntityPlayer entityplayer = (EntityPlayer)iterator.next();
                entityplayer.triggerAchievement(AchievementList.field_150964_J);
            }
        }
    }

    /**
     * Makes the entity despawn if requirements are reached
     */
    protected void despawnEntity()
    {
        this.entityAge = 0;
    }

    @SideOnly(Side.CLIENT)
    public int getBrightnessForRender(float p_70070_1_)
    {
        return 15728880;
    }

    /**
     * Called when the mob is falling. Calculates and applies fall damage.
     */
    protected void fall(float p_70069_1_) {}

    /**
     * adds a PotionEffect to the entity
     */
    public void addPotionEffect(PotionEffect p_70690_1_) {}

    /**
     * Returns true if the newer Entity AI code should be run
     */
    protected boolean isAIEnabled()
    {
        return true;
    }

    protected void applyEntityAttributes()
    {
        super.applyEntityAttributes();
        this.getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue(300.0D);
        this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(0.6000000238418579D);
        this.getEntityAttribute(SharedMonsterAttributes.followRange).setBaseValue(40.0D);
    }

    @SideOnly(Side.CLIENT)
    public float func_82207_a(int p_82207_1_)
    {
        return this.field_82221_e[p_82207_1_];
    }

    @SideOnly(Side.CLIENT)
    public float func_82210_r(int p_82210_1_)
    {
        return this.field_82220_d[p_82210_1_];
    }

    public int func_82212_n()
    {
        return this.dataWatcher.getWatchableObjectInt(20);
    }

    public void func_82215_s(int p_82215_1_)
    {
        this.dataWatcher.updateObject(20, Integer.valueOf(p_82215_1_));
    }

    /**
     * Returns the target entity ID if present, or -1 if not @param par1 The target offset, should be from 0-2
     */
    public int getWatchedTargetId(int p_82203_1_)
    {
        return this.dataWatcher.getWatchableObjectInt(17 + p_82203_1_);
    }

    public void func_82211_c(int p_82211_1_, int p_82211_2_)
    {
        this.dataWatcher.updateObject(17 + p_82211_1_, Integer.valueOf(p_82211_2_));
    }

    /**
     * Returns whether the wither is armored with its boss armor or not by checking whether its health is below half of
     * its maximum.
     */
    public boolean isArmored()
    {
        return this.getHealth() <= this.getMaxHealth() / 2.0F;
    }

    /**
     * Get this Entity's EnumCreatureAttribute
     */
    public EnumCreatureAttribute getCreatureAttribute()
    {
        return EnumCreatureAttribute.UNDEAD;
    }

    /**
     * Called when a player mounts an entity. e.g. mounts a pig, mounts a boat.
     */
    public void mountEntity(Entity p_70078_1_)
    {
        this.ridingEntity = null;
    }
}