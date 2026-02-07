package net.minecraft.entity;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Collection;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import java.util.UUID;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.enchantment.EnchantmentHelper;
import net.minecraft.entity.ai.attributes.AttributeModifier;
import net.minecraft.entity.ai.attributes.BaseAttributeMap;
import net.minecraft.entity.ai.attributes.IAttribute;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.ai.attributes.ServersideAttributeMap;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.monster.EntityZombie;
import net.minecraft.entity.passive.EntityPig;
import net.minecraft.entity.passive.EntityWolf;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.init.Blocks;
import net.minecraft.item.Item;
import net.minecraft.item.ItemArmor;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagFloat;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.nbt.NBTTagShort;
import net.minecraft.network.play.server.S04PacketEntityEquipment;
import net.minecraft.network.play.server.S0BPacketAnimation;
import net.minecraft.network.play.server.S0DPacketCollectItem;
import net.minecraft.potion.Potion;
import net.minecraft.potion.PotionEffect;
import net.minecraft.potion.PotionHelper;
import net.minecraft.scoreboard.Team;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.CombatTracker;
import net.minecraft.util.DamageSource;
import net.minecraft.util.IIcon;
import net.minecraft.util.MathHelper;
import net.minecraft.util.MovingObjectPosition;
import net.minecraft.util.Vec3;
import net.minecraft.world.World;
import net.minecraft.world.WorldServer;
import net.minecraftforge.common.ForgeHooks;

public abstract class EntityLivingBase extends Entity
{
    private static final UUID sprintingSpeedBoostModifierUUID = UUID.fromString("662A6B8D-DA3E-4C1C-8813-96EA6097278D");
    private static final AttributeModifier sprintingSpeedBoostModifier = (new AttributeModifier(sprintingSpeedBoostModifierUUID, "Sprinting speed boost", 0.30000001192092896D, 2)).setSaved(false);
    private BaseAttributeMap attributeMap;
    private final CombatTracker _combatTracker = new CombatTracker(this);
    private final HashMap activePotionsMap = new HashMap();
    /** The equipment this mob was previously wearing, used for syncing. */
    private final ItemStack[] previousEquipment = new ItemStack[5];
    /** Whether an arm swing is currently in progress. */
    public boolean isSwingInProgress;
    public int swingProgressInt;
    public int arrowHitTimer;
    public float prevHealth;
    /** The amount of time remaining this entity should act 'hurt'. (Visual appearance of red tint) */
    public int hurtTime;
    /** What the hurt time was max set to last. */
    public int maxHurtTime;
    /** The yaw at which this entity was last attacked from. */
    public float attackedAtYaw;
    /** The amount of time remaining this entity should act 'dead', i.e. have a corpse in the world. */
    public int deathTime;
    public int attackTime;
    public float prevSwingProgress;
    public float swingProgress;
    public float prevLimbSwingAmount;
    public float limbSwingAmount;
    /**
     * Only relevant when limbYaw is not 0(the entity is moving). Influences where in its swing legs and arms currently
     * are.
     */
    public float limbSwing;
    public int maxHurtResistantTime = 20;
    public float prevCameraPitch;
    public float cameraPitch;
    public float field_70769_ao;
    public float field_70770_ap;
    public float renderYawOffset;
    public float prevRenderYawOffset;
    /** Entity head rotation yaw */
    public float rotationYawHead;
    /** Entity head rotation yaw at previous tick */
    public float prevRotationYawHead;
    /** A factor used to determine how far this entity will move each tick if it is jumping or falling. */
    public float jumpMovementFactor = 0.02F;
    /** The most recent player that has attacked this entity */
    protected EntityPlayer attackingPlayer;
    /**
     * Set to 60 when hit by the player or the player's wolf, then decrements. Used to determine whether the entity
     * should drop items on death.
     */
    protected int recentlyHit;
    /** This gets set on entity death, but never used. Looks like a duplicate of isDead */
    protected boolean dead;
    /** The age of this EntityLiving (used to determine when it dies) */
    protected int entityAge;
    protected float field_70768_au;
    protected float field_110154_aX;
    protected float field_70764_aw;
    protected float field_70763_ax;
    protected float field_70741_aB;
    /** The score value of the Mob, the amount of points the mob is worth. */
    protected int scoreValue;
    /** Damage taken in the last hit. Mobs are resistant to damage less than this for a short time after taking damage. */
    protected float lastDamage;
    /** used to check whether entity is jumping. */
    protected boolean isJumping;
    public float moveStrafing;
    public float moveForward;
    protected float randomYawVelocity;
    /** The number of updates over which the new position and rotation are to be applied to the entity. */
    protected int newPosRotationIncrements;
    /** The new X position to be applied to the entity. */
    protected double newPosX;
    /** The new Y position to be applied to the entity. */
    protected double newPosY;
    protected double newPosZ;
    /** The new yaw rotation to be applied to the entity. */
    protected double newRotationYaw;
    /** The new yaw rotation to be applied to the entity. */
    protected double newRotationPitch;
    /** Whether the DataWatcher needs to be updated with the active potions */
    private boolean potionsNeedUpdate = true;
    /** is only being set, has no uses as of MC 1.1 */
    private EntityLivingBase entityLivingToAttack;
    private int revengeTimer;
    private EntityLivingBase lastAttacker;
    /** Holds the value of ticksExisted when setLastAttacker was last called. */
    private int lastAttackerTime;
    /**
     * A factor used to determine how far this entity will move each tick if it is walking on land. Adjusted by speed,
     * and slipperiness of the current block.
     */
    private float landMovementFactor;
    /** Number of ticks since last jump */
    private int jumpTicks;
    private float field_110151_bq;
    private static final String __OBFID = "CL_00001549";

    public EntityLivingBase(World p_i1594_1_)
    {
        super(p_i1594_1_);
        this.applyEntityAttributes();
        this.setHealth(this.getMaxHealth());
        this.preventEntitySpawning = true;
        this.field_70770_ap = (float)(Math.random() + 1.0D) * 0.01F;
        this.setPosition(this.posX, this.posY, this.posZ);
        this.field_70769_ao = (float)Math.random() * 12398.0F;
        this.rotationYaw = (float)(Math.random() * Math.PI * 2.0D);
        this.rotationYawHead = this.rotationYaw;
        this.stepHeight = 0.5F;
    }

    protected void entityInit()
    {
        this.dataWatcher.addObject(7, Integer.valueOf(0));
        this.dataWatcher.addObject(8, Byte.valueOf((byte)0));
        this.dataWatcher.addObject(9, Byte.valueOf((byte)0));
        this.dataWatcher.addObject(6, Float.valueOf(1.0F));
    }

    protected void applyEntityAttributes()
    {
        this.getAttributeMap().registerAttribute(SharedMonsterAttributes.maxHealth);
        this.getAttributeMap().registerAttribute(SharedMonsterAttributes.knockbackResistance);
        this.getAttributeMap().registerAttribute(SharedMonsterAttributes.movementSpeed);

        if (!this.isAIEnabled())
        {
            this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(0.10000000149011612D);
        }
    }

    /**
     * Takes in the distance the entity has fallen this tick and whether its on the ground to update the fall distance
     * and deal fall damage if landing on the ground.  Args: distanceFallenThisTick, onGround
     */
    protected void updateFallState(double p_70064_1_, boolean p_70064_3_)
    {
        if (!this.isInWater())
        {
            this.handleWaterMovement();
        }

        if (p_70064_3_ && this.fallDistance > 0.0F)
        {
            int i = MathHelper.floor_double(this.posX);
            int j = MathHelper.floor_double(this.posY - 0.20000000298023224D - (double)this.yOffset);
            int k = MathHelper.floor_double(this.posZ);
            Block block = this.worldObj.getBlock(i, j, k);

            if (block.getMaterial() == Material.air)
            {
                int l = this.worldObj.getBlock(i, j - 1, k).getRenderType();

                if (l == 11 || l == 32 || l == 21)
                {
                    block = this.worldObj.getBlock(i, j - 1, k);
                }
            }
            else if (!this.worldObj.isRemote && this.fallDistance > 3.0F)
            {
                this.worldObj.playAuxSFX(2006, i, j, k, MathHelper.ceiling_float_int(this.fallDistance - 3.0F));
            }

            block.onFallenUpon(this.worldObj, i, j, k, this, this.fallDistance);
        }

        super.updateFallState(p_70064_1_, p_70064_3_);
    }

    public boolean canBreatheUnderwater()
    {
        return false;
    }

    /**
     * Gets called every tick from main Entity class
     */
    public void onEntityUpdate()
    {
        this.prevSwingProgress = this.swingProgress;
        super.onEntityUpdate();
        this.worldObj.theProfiler.startSection("livingEntityBaseTick");

        if (this.isEntityAlive() && this.isEntityInsideOpaqueBlock())
        {
            this.attackEntityFrom(DamageSource.inWall, 1.0F);
        }

        if (this.isImmuneToFire() || this.worldObj.isRemote)
        {
            this.extinguish();
        }

        boolean flag = this instanceof EntityPlayer && ((EntityPlayer)this).capabilities.disableDamage;

        if (this.isEntityAlive() && this.isInsideOfMaterial(Material.water))
        {
            if (!this.canBreatheUnderwater() && !this.isPotionActive(Potion.waterBreathing.id) && !flag)
            {
                this.setAir(this.decreaseAirSupply(this.getAir()));

                if (this.getAir() == -20)
                {
                    this.setAir(0);

                    for (int i = 0; i < 8; ++i)
                    {
                        float f = this.rand.nextFloat() - this.rand.nextFloat();
                        float f1 = this.rand.nextFloat() - this.rand.nextFloat();
                        float f2 = this.rand.nextFloat() - this.rand.nextFloat();
                        this.worldObj.spawnParticle("bubble", this.posX + (double)f, this.posY + (double)f1, this.posZ + (double)f2, this.motionX, this.motionY, this.motionZ);
                    }

                    this.attackEntityFrom(DamageSource.drown, 2.0F);
                }
            }

            if (!this.worldObj.isRemote && this.isRiding() && this.ridingEntity != null && ridingEntity.shouldDismountInWater(this))
            {
                this.mountEntity((Entity)null);
            }
        }
        else
        {
            this.setAir(300);
        }

        if (this.isEntityAlive() && this.isWet())
        {
            this.extinguish();
        }

        this.prevCameraPitch = this.cameraPitch;

        if (this.attackTime > 0)
        {
            --this.attackTime;
        }

        if (this.hurtTime > 0)
        {
            --this.hurtTime;
        }

        if (this.hurtResistantTime > 0 && !(this instanceof EntityPlayerMP))
        {
            --this.hurtResistantTime;
        }

        if (this.getHealth() <= 0.0F)
        {
            this.onDeathUpdate();
        }

        if (this.recentlyHit > 0)
        {
            --this.recentlyHit;
        }
        else
        {
            this.attackingPlayer = null;
        }

        if (this.lastAttacker != null && !this.lastAttacker.isEntityAlive())
        {
            this.lastAttacker = null;
        }

        if (this.entityLivingToAttack != null)
        {
            if (!this.entityLivingToAttack.isEntityAlive())
            {
                this.setRevengeTarget((EntityLivingBase)null);
            }
            else if (this.ticksExisted - this.revengeTimer > 100)
            {
                this.setRevengeTarget((EntityLivingBase)null);
            }
        }

        this.updatePotionEffects();
        this.field_70763_ax = this.field_70764_aw;
        this.prevRenderYawOffset = this.renderYawOffset;
        this.prevRotationYawHead = this.rotationYawHead;
        this.prevRotationYaw = this.rotationYaw;
        this.prevRotationPitch = this.rotationPitch;
        this.worldObj.theProfiler.endSection();
    }

    /**
     * If Animal, checks if the age timer is negative
     */
    public boolean isChild()
    {
        return false;
    }

    /**
     * handles entity death timer, experience orb and particle creation
     */
    protected void onDeathUpdate()
    {
        ++this.deathTime;

        if (this.deathTime == 20)
        {
            int i;

            if (!this.worldObj.isRemote && (this.recentlyHit > 0 || this.isPlayer()) && this.func_146066_aG() && this.worldObj.getGameRules().getGameRuleBooleanValue("doMobLoot"))
            {
                i = this.getExperiencePoints(this.attackingPlayer);

                while (i > 0)
                {
                    int j = EntityXPOrb.getXPSplit(i);
                    i -= j;
                    this.worldObj.spawnEntityInWorld(new EntityXPOrb(this.worldObj, this.posX, this.posY, this.posZ, j));
                }
            }

            this.setDead();

            for (i = 0; i < 20; ++i)
            {
                double d2 = this.rand.nextGaussian() * 0.02D;
                double d0 = this.rand.nextGaussian() * 0.02D;
                double d1 = this.rand.nextGaussian() * 0.02D;
                this.worldObj.spawnParticle("explode", this.posX + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, this.posY + (double)(this.rand.nextFloat() * this.height), this.posZ + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, d2, d0, d1);
            }
        }
    }

    protected boolean func_146066_aG()
    {
        return !this.isChild();
    }

    /**
     * Decrements the entity's air supply when underwater
     */
    protected int decreaseAirSupply(int p_70682_1_)
    {
        int j = EnchantmentHelper.getRespiration(this);
        return j > 0 && this.rand.nextInt(j + 1) > 0 ? p_70682_1_ : p_70682_1_ - 1;
    }

    /**
     * Get the experience points the entity currently has.
     */
    protected int getExperiencePoints(EntityPlayer p_70693_1_)
    {
        return 0;
    }

    /**
     * Only use is to identify if class is an instance of player for experience dropping
     */
    protected boolean isPlayer()
    {
        return false;
    }

    public Random getRNG()
    {
        return this.rand;
    }

    public EntityLivingBase getAITarget()
    {
        return this.entityLivingToAttack;
    }

    public int func_142015_aE()
    {
        return this.revengeTimer;
    }

    public void setRevengeTarget(EntityLivingBase p_70604_1_)
    {
        this.entityLivingToAttack = p_70604_1_;
        this.revengeTimer = this.ticksExisted;
        ForgeHooks.onLivingSetAttackTarget(this, p_70604_1_);
    }

    public EntityLivingBase getLastAttacker()
    {
        return this.lastAttacker;
    }

    public int getLastAttackerTime()
    {
        return this.lastAttackerTime;
    }

    public void setLastAttacker(Entity p_130011_1_)
    {
        if (p_130011_1_ instanceof EntityLivingBase)
        {
            this.lastAttacker = (EntityLivingBase)p_130011_1_;
        }
        else
        {
            this.lastAttacker = null;
        }

        this.lastAttackerTime = this.ticksExisted;
    }

    public int getAge()
    {
        return this.entityAge;
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        p_70014_1_.setFloat("HealF", this.getHealth());
        p_70014_1_.setShort("Health", (short)((int)Math.ceil((double)this.getHealth())));
        p_70014_1_.setShort("HurtTime", (short)this.hurtTime);
        p_70014_1_.setShort("DeathTime", (short)this.deathTime);
        p_70014_1_.setShort("AttackTime", (short)this.attackTime);
        p_70014_1_.setFloat("AbsorptionAmount", this.getAbsorptionAmount());
        ItemStack[] aitemstack = this.getLastActiveItems();
        int i = aitemstack.length;
        int j;
        ItemStack itemstack;

        for (j = 0; j < i; ++j)
        {
            itemstack = aitemstack[j];

            if (itemstack != null)
            {
                this.attributeMap.removeAttributeModifiers(itemstack.getAttributeModifiers());
            }
        }

        p_70014_1_.setTag("Attributes", SharedMonsterAttributes.writeBaseAttributeMapToNBT(this.getAttributeMap()));
        aitemstack = this.getLastActiveItems();
        i = aitemstack.length;

        for (j = 0; j < i; ++j)
        {
            itemstack = aitemstack[j];

            if (itemstack != null)
            {
                this.attributeMap.applyAttributeModifiers(itemstack.getAttributeModifiers());
            }
        }

        if (!this.activePotionsMap.isEmpty())
        {
            NBTTagList nbttaglist = new NBTTagList();
            Iterator iterator = this.activePotionsMap.values().iterator();

            while (iterator.hasNext())
            {
                PotionEffect potioneffect = (PotionEffect)iterator.next();
                nbttaglist.appendTag(potioneffect.writeCustomPotionEffectToNBT(new NBTTagCompound()));
            }

            p_70014_1_.setTag("ActiveEffects", nbttaglist);
        }
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        this.setAbsorptionAmount(p_70037_1_.getFloat("AbsorptionAmount"));

        if (p_70037_1_.hasKey("Attributes", 9) && this.worldObj != null && !this.worldObj.isRemote)
        {
            SharedMonsterAttributes.func_151475_a(this.getAttributeMap(), p_70037_1_.getTagList("Attributes", 10));
        }

        if (p_70037_1_.hasKey("ActiveEffects", 9))
        {
            NBTTagList nbttaglist = p_70037_1_.getTagList("ActiveEffects", 10);

            for (int i = 0; i < nbttaglist.tagCount(); ++i)
            {
                NBTTagCompound nbttagcompound1 = nbttaglist.getCompoundTagAt(i);
                PotionEffect potioneffect = PotionEffect.readCustomPotionEffectFromNBT(nbttagcompound1);

                if (potioneffect != null)
                {
                    this.activePotionsMap.put(Integer.valueOf(potioneffect.getPotionID()), potioneffect);
                }
            }
        }

        if (p_70037_1_.hasKey("HealF", 99))
        {
            this.setHealth(p_70037_1_.getFloat("HealF"));
        }
        else
        {
            NBTBase nbtbase = p_70037_1_.getTag("Health");

            if (nbtbase == null)
            {
                this.setHealth(this.getMaxHealth());
            }
            else if (nbtbase.getId() == 5)
            {
                this.setHealth(((NBTTagFloat)nbtbase).func_150288_h());
            }
            else if (nbtbase.getId() == 2)
            {
                this.setHealth((float)((NBTTagShort)nbtbase).func_150289_e());
            }
        }

        this.hurtTime = p_70037_1_.getShort("HurtTime");
        this.deathTime = p_70037_1_.getShort("DeathTime");
        this.attackTime = p_70037_1_.getShort("AttackTime");
    }

    protected void updatePotionEffects()
    {
        Iterator iterator = this.activePotionsMap.keySet().iterator();

        while (iterator.hasNext())
        {
            Integer integer = (Integer)iterator.next();
            PotionEffect potioneffect = (PotionEffect)this.activePotionsMap.get(integer);

            if (!potioneffect.onUpdate(this))
            {
                if (!this.worldObj.isRemote)
                {
                    iterator.remove();
                    this.onFinishedPotionEffect(potioneffect);
                }
            }
            else if (potioneffect.getDuration() % 600 == 0)
            {
                this.onChangedPotionEffect(potioneffect, false);
            }
        }

        int i;

        if (this.potionsNeedUpdate)
        {
            if (!this.worldObj.isRemote)
            {
                if (this.activePotionsMap.isEmpty())
                {
                    this.dataWatcher.updateObject(8, Byte.valueOf((byte)0));
                    this.dataWatcher.updateObject(7, Integer.valueOf(0));
                    this.setInvisible(false);
                }
                else
                {
                    i = PotionHelper.calcPotionLiquidColor(this.activePotionsMap.values());
                    this.dataWatcher.updateObject(8, Byte.valueOf((byte)(PotionHelper.func_82817_b(this.activePotionsMap.values()) ? 1 : 0)));
                    this.dataWatcher.updateObject(7, Integer.valueOf(i));
                    this.setInvisible(this.isPotionActive(Potion.invisibility.id));
                }
            }

            this.potionsNeedUpdate = false;
        }

        i = this.dataWatcher.getWatchableObjectInt(7);
        boolean flag1 = this.dataWatcher.getWatchableObjectByte(8) > 0;

        if (i > 0)
        {
            boolean flag = false;

            if (!this.isInvisible())
            {
                flag = this.rand.nextBoolean();
            }
            else
            {
                flag = this.rand.nextInt(15) == 0;
            }

            if (flag1)
            {
                flag &= this.rand.nextInt(5) == 0;
            }

            if (flag && i > 0)
            {
                double d0 = (double)(i >> 16 & 255) / 255.0D;
                double d1 = (double)(i >> 8 & 255) / 255.0D;
                double d2 = (double)(i >> 0 & 255) / 255.0D;
                this.worldObj.spawnParticle(flag1 ? "mobSpellAmbient" : "mobSpell", this.posX + (this.rand.nextDouble() - 0.5D) * (double)this.width, this.posY + this.rand.nextDouble() * (double)this.height - (double)this.yOffset, this.posZ + (this.rand.nextDouble() - 0.5D) * (double)this.width, d0, d1, d2);
            }
        }
    }

    public void clearActivePotions()
    {
        Iterator iterator = this.activePotionsMap.keySet().iterator();

        while (iterator.hasNext())
        {
            Integer integer = (Integer)iterator.next();
            PotionEffect potioneffect = (PotionEffect)this.activePotionsMap.get(integer);

            if (!this.worldObj.isRemote)
            {
                iterator.remove();
                this.onFinishedPotionEffect(potioneffect);
            }
        }
    }

    public Collection getActivePotionEffects()
    {
        return this.activePotionsMap.values();
    }

    public boolean isPotionActive(int p_82165_1_)
    {
        return this.activePotionsMap.containsKey(Integer.valueOf(p_82165_1_));
    }

    public boolean isPotionActive(Potion p_70644_1_)
    {
        return this.activePotionsMap.containsKey(Integer.valueOf(p_70644_1_.id));
    }

    /**
     * returns the PotionEffect for the supplied Potion if it is active, null otherwise.
     */
    public PotionEffect getActivePotionEffect(Potion p_70660_1_)
    {
        return (PotionEffect)this.activePotionsMap.get(Integer.valueOf(p_70660_1_.id));
    }

    /**
     * adds a PotionEffect to the entity
     */
    public void addPotionEffect(PotionEffect p_70690_1_)
    {
        if (this.isPotionApplicable(p_70690_1_))
        {
            if (this.activePotionsMap.containsKey(Integer.valueOf(p_70690_1_.getPotionID())))
            {
                ((PotionEffect)this.activePotionsMap.get(Integer.valueOf(p_70690_1_.getPotionID()))).combine(p_70690_1_);
                this.onChangedPotionEffect((PotionEffect)this.activePotionsMap.get(Integer.valueOf(p_70690_1_.getPotionID())), true);
            }
            else
            {
                this.activePotionsMap.put(Integer.valueOf(p_70690_1_.getPotionID()), p_70690_1_);
                this.onNewPotionEffect(p_70690_1_);
            }
        }
    }

    public boolean isPotionApplicable(PotionEffect p_70687_1_)
    {
        if (this.getCreatureAttribute() == EnumCreatureAttribute.UNDEAD)
        {
            int i = p_70687_1_.getPotionID();

            if (i == Potion.regeneration.id || i == Potion.poison.id)
            {
                return false;
            }
        }

        return true;
    }

    /**
     * Returns true if this entity is undead.
     */
    public boolean isEntityUndead()
    {
        return this.getCreatureAttribute() == EnumCreatureAttribute.UNDEAD;
    }

    /**
     * Remove the speified potion effect from this entity.
     */
    public void removePotionEffectClient(int p_70618_1_)
    {
        this.activePotionsMap.remove(Integer.valueOf(p_70618_1_));
    }

    /**
     * Remove the specified potion effect from this entity.
     */
    public void removePotionEffect(int p_82170_1_)
    {
        PotionEffect potioneffect = (PotionEffect)this.activePotionsMap.remove(Integer.valueOf(p_82170_1_));

        if (potioneffect != null)
        {
            this.onFinishedPotionEffect(potioneffect);
        }
    }

    protected void onNewPotionEffect(PotionEffect p_70670_1_)
    {
        this.potionsNeedUpdate = true;

        if (!this.worldObj.isRemote)
        {
            Potion.potionTypes[p_70670_1_.getPotionID()].applyAttributesModifiersToEntity(this, this.getAttributeMap(), p_70670_1_.getAmplifier());
        }
    }

    protected void onChangedPotionEffect(PotionEffect p_70695_1_, boolean p_70695_2_)
    {
        this.potionsNeedUpdate = true;

        if (p_70695_2_ && !this.worldObj.isRemote)
        {
            Potion.potionTypes[p_70695_1_.getPotionID()].removeAttributesModifiersFromEntity(this, this.getAttributeMap(), p_70695_1_.getAmplifier());
            Potion.potionTypes[p_70695_1_.getPotionID()].applyAttributesModifiersToEntity(this, this.getAttributeMap(), p_70695_1_.getAmplifier());
        }
    }

    protected void onFinishedPotionEffect(PotionEffect p_70688_1_)
    {
        this.potionsNeedUpdate = true;

        if (!this.worldObj.isRemote)
        {
            Potion.potionTypes[p_70688_1_.getPotionID()].removeAttributesModifiersFromEntity(this, this.getAttributeMap(), p_70688_1_.getAmplifier());
        }
    }

    /**
     * Heal living entity (param: amount of half-hearts)
     */
    public void heal(float p_70691_1_)
    {
        p_70691_1_ = net.minecraftforge.event.ForgeEventFactory.onLivingHeal(this, p_70691_1_);
        if (p_70691_1_ <= 0) return;
        float f1 = this.getHealth();

        if (f1 > 0.0F)
        {
            this.setHealth(f1 + p_70691_1_);
        }
    }

    public final float getHealth()
    {
        return this.dataWatcher.getWatchableObjectFloat(6);
    }

    public void setHealth(float p_70606_1_)
    {
        this.dataWatcher.updateObject(6, Float.valueOf(MathHelper.clamp_float(p_70606_1_, 0.0F, this.getMaxHealth())));
    }

    /**
     * Called when the entity is attacked.
     */
    public boolean attackEntityFrom(DamageSource p_70097_1_, float p_70097_2_)
    {
        if (ForgeHooks.onLivingAttack(this, p_70097_1_, p_70097_2_)) return false;
        if (this.isEntityInvulnerable())
        {
            return false;
        }
        else if (this.worldObj.isRemote)
        {
            return false;
        }
        else
        {
            this.entityAge = 0;

            if (this.getHealth() <= 0.0F)
            {
                return false;
            }
            else if (p_70097_1_.isFireDamage() && this.isPotionActive(Potion.fireResistance))
            {
                return false;
            }
            else
            {
                if ((p_70097_1_ == DamageSource.anvil || p_70097_1_ == DamageSource.fallingBlock) && this.getEquipmentInSlot(4) != null)
                {
                    this.getEquipmentInSlot(4).damageItem((int)(p_70097_2_ * 4.0F + this.rand.nextFloat() * p_70097_2_ * 2.0F), this);
                    p_70097_2_ *= 0.75F;
                }

                this.limbSwingAmount = 1.5F;
                boolean flag = true;

                if ((float)this.hurtResistantTime > (float)this.maxHurtResistantTime / 2.0F)
                {
                    if (p_70097_2_ <= this.lastDamage)
                    {
                        return false;
                    }

                    this.damageEntity(p_70097_1_, p_70097_2_ - this.lastDamage);
                    this.lastDamage = p_70097_2_;
                    flag = false;
                }
                else
                {
                    this.lastDamage = p_70097_2_;
                    this.prevHealth = this.getHealth();
                    this.hurtResistantTime = this.maxHurtResistantTime;
                    this.damageEntity(p_70097_1_, p_70097_2_);
                    this.hurtTime = this.maxHurtTime = 10;
                }

                this.attackedAtYaw = 0.0F;
                Entity entity = p_70097_1_.getEntity();

                if (entity != null)
                {
                    if (entity instanceof EntityLivingBase)
                    {
                        this.setRevengeTarget((EntityLivingBase)entity);
                    }

                    if (entity instanceof EntityPlayer)
                    {
                        this.recentlyHit = 100;
                        this.attackingPlayer = (EntityPlayer)entity;
                    }
                    else if (entity instanceof net.minecraft.entity.passive.EntityTameable)
                    {
                        net.minecraft.entity.passive.EntityTameable entitywolf = (net.minecraft.entity.passive.EntityTameable)entity;

                        if (entitywolf.isTamed())
                        {
                            this.recentlyHit = 100;
                            this.attackingPlayer = null;
                        }
                    }
                }

                if (flag)
                {
                    this.worldObj.setEntityState(this, (byte)2);

                    if (p_70097_1_ != DamageSource.drown)
                    {
                        this.setBeenAttacked();
                    }

                    if (entity != null)
                    {
                        double d1 = entity.posX - this.posX;
                        double d0;

                        for (d0 = entity.posZ - this.posZ; d1 * d1 + d0 * d0 < 1.0E-4D; d0 = (Math.random() - Math.random()) * 0.01D)
                        {
                            d1 = (Math.random() - Math.random()) * 0.01D;
                        }

                        this.attackedAtYaw = (float)(Math.atan2(d0, d1) * 180.0D / Math.PI) - this.rotationYaw;
                        this.knockBack(entity, p_70097_2_, d1, d0);
                    }
                    else
                    {
                        this.attackedAtYaw = (float)((int)(Math.random() * 2.0D) * 180);
                    }
                }

                String s;

                if (this.getHealth() <= 0.0F)
                {
                    s = this.getDeathSound();

                    if (flag && s != null)
                    {
                        this.playSound(s, this.getSoundVolume(), this.getSoundPitch());
                    }

                    this.onDeath(p_70097_1_);
                }
                else
                {
                    s = this.getHurtSound();

                    if (flag && s != null)
                    {
                        this.playSound(s, this.getSoundVolume(), this.getSoundPitch());
                    }
                }

                return true;
            }
        }
    }

    /**
     * Renders broken item particles using the given ItemStack
     */
    public void renderBrokenItemStack(ItemStack p_70669_1_)
    {
        this.playSound("random.break", 0.8F, 0.8F + this.worldObj.rand.nextFloat() * 0.4F);

        for (int i = 0; i < 5; ++i)
        {
            Vec3 vec3 = Vec3.createVectorHelper(((double)this.rand.nextFloat() - 0.5D) * 0.1D, Math.random() * 0.1D + 0.1D, 0.0D);
            vec3.rotateAroundX(-this.rotationPitch * (float)Math.PI / 180.0F);
            vec3.rotateAroundY(-this.rotationYaw * (float)Math.PI / 180.0F);
            Vec3 vec31 = Vec3.createVectorHelper(((double)this.rand.nextFloat() - 0.5D) * 0.3D, (double)(-this.rand.nextFloat()) * 0.6D - 0.3D, 0.6D);
            vec31.rotateAroundX(-this.rotationPitch * (float)Math.PI / 180.0F);
            vec31.rotateAroundY(-this.rotationYaw * (float)Math.PI / 180.0F);
            vec31 = vec31.addVector(this.posX, this.posY + (double)this.getEyeHeight(), this.posZ);
            this.worldObj.spawnParticle("iconcrack_" + Item.getIdFromItem(p_70669_1_.getItem()), vec31.xCoord, vec31.yCoord, vec31.zCoord, vec3.xCoord, vec3.yCoord + 0.05D, vec3.zCoord);
        }
    }

    /**
     * Called when the mob's health reaches 0.
     */
    public void onDeath(DamageSource p_70645_1_)
    {
        if (ForgeHooks.onLivingDeath(this, p_70645_1_)) return;
        Entity entity = p_70645_1_.getEntity();
        EntityLivingBase entitylivingbase = this.func_94060_bK();

        if (this.scoreValue >= 0 && entitylivingbase != null)
        {
            entitylivingbase.addToPlayerScore(this, this.scoreValue);
        }

        if (entity != null)
        {
            entity.onKillEntity(this);
        }

        this.dead = true;
        this.func_110142_aN().func_94549_h();

        if (!this.worldObj.isRemote)
        {
            int i = 0;

            if (entity instanceof EntityPlayer)
            {
                i = EnchantmentHelper.getLootingModifier((EntityLivingBase)entity);
            }

            captureDrops = true;
            capturedDrops.clear();
            int j = 0;

            if (this.func_146066_aG() && this.worldObj.getGameRules().getGameRuleBooleanValue("doMobLoot"))
            {
                this.dropFewItems(this.recentlyHit > 0, i);
                this.dropEquipment(this.recentlyHit > 0, i);

                if (this.recentlyHit > 0)
                {
                    j = this.rand.nextInt(200) - i;

                    if (j < 5)
                    {
                        this.dropRareDrop(j <= 0 ? 1 : 0);
                    }
                }
            }

            captureDrops = false;

            if (!ForgeHooks.onLivingDrops(this, p_70645_1_, capturedDrops, i, recentlyHit > 0, j))
            {
                for (EntityItem item : capturedDrops)
                {
                    worldObj.spawnEntityInWorld(item);
                }
            }
        }

        this.worldObj.setEntityState(this, (byte)3);
    }

    /**
     * Drop the equipment for this entity.
     */
    protected void dropEquipment(boolean p_82160_1_, int p_82160_2_) {}

    /**
     * knocks back this entity
     */
    public void knockBack(Entity p_70653_1_, float p_70653_2_, double p_70653_3_, double p_70653_5_)
    {
        if (this.rand.nextDouble() >= this.getEntityAttribute(SharedMonsterAttributes.knockbackResistance).getAttributeValue())
        {
            this.isAirBorne = true;
            float f1 = MathHelper.sqrt_double(p_70653_3_ * p_70653_3_ + p_70653_5_ * p_70653_5_);
            float f2 = 0.4F;
            this.motionX /= 2.0D;
            this.motionY /= 2.0D;
            this.motionZ /= 2.0D;
            this.motionX -= p_70653_3_ / (double)f1 * (double)f2;
            this.motionY += (double)f2;
            this.motionZ -= p_70653_5_ / (double)f1 * (double)f2;

            if (this.motionY > 0.4000000059604645D)
            {
                this.motionY = 0.4000000059604645D;
            }
        }
    }

    /**
     * Returns the sound this mob makes when it is hurt.
     */
    protected String getHurtSound()
    {
        return "game.neutral.hurt";
    }

    /**
     * Returns the sound this mob makes on death.
     */
    protected String getDeathSound()
    {
        return "game.neutral.die";
    }

    protected void dropRareDrop(int p_70600_1_) {}

    /**
     * Drop 0-2 items of this living's type. @param par1 - Whether this entity has recently been hit by a player. @param
     * par2 - Level of Looting used to kill this mob.
     */
    protected void dropFewItems(boolean p_70628_1_, int p_70628_2_) {}

    /**
     * returns true if this entity is by a ladder, false otherwise
     */
    public boolean isOnLadder()
    {
        int i = MathHelper.floor_double(this.posX);
        int j = MathHelper.floor_double(this.boundingBox.minY);
        int k = MathHelper.floor_double(this.posZ);
        Block block = this.worldObj.getBlock(i, j, k);
        return ForgeHooks.isLivingOnLadder(block, worldObj, i, j, k, this);
    }

    /**
     * Checks whether target entity is alive.
     */
    public boolean isEntityAlive()
    {
        return !this.isDead && this.getHealth() > 0.0F;
    }

    /**
     * Called when the mob is falling. Calculates and applies fall damage.
     */
    protected void fall(float p_70069_1_)
    {
        p_70069_1_ = ForgeHooks.onLivingFall(this, p_70069_1_);
        if (p_70069_1_ <= 0) return;
        super.fall(p_70069_1_);
        PotionEffect potioneffect = this.getActivePotionEffect(Potion.jump);
        float f1 = potioneffect != null ? (float)(potioneffect.getAmplifier() + 1) : 0.0F;
        int i = MathHelper.ceiling_float_int(p_70069_1_ - 3.0F - f1);

        if (i > 0)
        {
            this.playSound(this.func_146067_o(i), 1.0F, 1.0F);
            this.attackEntityFrom(DamageSource.fall, (float)i);
            int j = MathHelper.floor_double(this.posX);
            int k = MathHelper.floor_double(this.posY - 0.20000000298023224D - (double)this.yOffset);
            int l = MathHelper.floor_double(this.posZ);
            Block block = this.worldObj.getBlock(j, k, l);

            if (block.getMaterial() != Material.air)
            {
                Block.SoundType soundtype = block.stepSound;
                this.playSound(soundtype.getStepResourcePath(), soundtype.getVolume() * 0.5F, soundtype.getPitch() * 0.75F);
            }
        }
    }

    protected String func_146067_o(int p_146067_1_)
    {
        return p_146067_1_ > 4 ? "game.neutral.hurt.fall.big" : "game.neutral.hurt.fall.small";
    }

    /**
     * Setups the entity to do the hurt animation. Only used by packets in multiplayer.
     */
    @SideOnly(Side.CLIENT)
    public void performHurtAnimation()
    {
        this.hurtTime = this.maxHurtTime = 10;
        this.attackedAtYaw = 0.0F;
    }

    /**
     * Returns the current armor value as determined by a call to InventoryPlayer.getTotalArmorValue
     */
    public int getTotalArmorValue()
    {
        int i = 0;
        ItemStack[] aitemstack = this.getLastActiveItems();
        int j = aitemstack.length;

        for (int k = 0; k < j; ++k)
        {
            ItemStack itemstack = aitemstack[k];

            if (itemstack != null && itemstack.getItem() instanceof ItemArmor)
            {
                int l = ((ItemArmor)itemstack.getItem()).damageReduceAmount;
                i += l;
            }
        }

        return i;
    }

    protected void damageArmor(float p_70675_1_) {}

    /**
     * Reduces damage, depending on armor
     */
    protected float applyArmorCalculations(DamageSource p_70655_1_, float p_70655_2_)
    {
        if (!p_70655_1_.isUnblockable())
        {
            int i = 25 - this.getTotalArmorValue();
            float f1 = p_70655_2_ * (float)i;
            this.damageArmor(p_70655_2_);
            p_70655_2_ = f1 / 25.0F;
        }

        return p_70655_2_;
    }

    /**
     * Reduces damage, depending on potions
     */
    protected float applyPotionDamageCalculations(DamageSource p_70672_1_, float p_70672_2_)
    {
        if (p_70672_1_.isDamageAbsolute())
        {
            return p_70672_2_;
        }
        else
        {
            if (this instanceof EntityZombie)
            {
                //par2 = par2; // Forge: Noop Warning
            }

            int i;
            int j;
            float f1;

            if (this.isPotionActive(Potion.resistance) && p_70672_1_ != DamageSource.outOfWorld)
            {
                i = (this.getActivePotionEffect(Potion.resistance).getAmplifier() + 1) * 5;
                j = 25 - i;
                f1 = p_70672_2_ * (float)j;
                p_70672_2_ = f1 / 25.0F;
            }

            if (p_70672_2_ <= 0.0F)
            {
                return 0.0F;
            }
            else
            {
                i = EnchantmentHelper.getEnchantmentModifierDamage(this.getLastActiveItems(), p_70672_1_);

                if (i > 20)
                {
                    i = 20;
                }

                if (i > 0 && i <= 20)
                {
                    j = 25 - i;
                    f1 = p_70672_2_ * (float)j;
                    p_70672_2_ = f1 / 25.0F;
                }

                return p_70672_2_;
            }
        }
    }

    /**
     * Deals damage to the entity. If its a EntityPlayer then will take damage from the armor first and then health
     * second with the reduced value. Args: damageAmount
     */
    protected void damageEntity(DamageSource p_70665_1_, float p_70665_2_)
    {
        if (!this.isEntityInvulnerable())
        {
            p_70665_2_ = ForgeHooks.onLivingHurt(this, p_70665_1_, p_70665_2_);
            if (p_70665_2_ <= 0) return;
            p_70665_2_ = this.applyArmorCalculations(p_70665_1_, p_70665_2_);
            p_70665_2_ = this.applyPotionDamageCalculations(p_70665_1_, p_70665_2_);
            float f1 = p_70665_2_;
            p_70665_2_ = Math.max(p_70665_2_ - this.getAbsorptionAmount(), 0.0F);
            this.setAbsorptionAmount(this.getAbsorptionAmount() - (f1 - p_70665_2_));

            if (p_70665_2_ != 0.0F)
            {
                float f2 = this.getHealth();
                this.setHealth(f2 - p_70665_2_);
                this.func_110142_aN().func_94547_a(p_70665_1_, f2, p_70665_2_);
                this.setAbsorptionAmount(this.getAbsorptionAmount() - p_70665_2_);
            }
        }
    }

    public CombatTracker func_110142_aN()
    {
        return this._combatTracker;
    }

    public EntityLivingBase func_94060_bK()
    {
        return (EntityLivingBase)(this._combatTracker.func_94550_c() != null ? this._combatTracker.func_94550_c() : (this.attackingPlayer != null ? this.attackingPlayer : (this.entityLivingToAttack != null ? this.entityLivingToAttack : null)));
    }

    public final float getMaxHealth()
    {
        return (float)this.getEntityAttribute(SharedMonsterAttributes.maxHealth).getAttributeValue();
    }

    /**
     * counts the amount of arrows stuck in the entity. getting hit by arrows increases this, used in rendering
     */
    public final int getArrowCountInEntity()
    {
        return this.dataWatcher.getWatchableObjectByte(9);
    }

    /**
     * sets the amount of arrows stuck in the entity. used for rendering those
     */
    public final void setArrowCountInEntity(int p_85034_1_)
    {
        this.dataWatcher.updateObject(9, Byte.valueOf((byte)p_85034_1_));
    }

    /**
     * Returns an integer indicating the end point of the swing animation, used by {@link #swingProgress} to provide a
     * progress indicator. Takes dig speed enchantments into account.
     */
    private int getArmSwingAnimationEnd()
    {
        return this.isPotionActive(Potion.digSpeed) ? 6 - (1 + this.getActivePotionEffect(Potion.digSpeed).getAmplifier()) * 1 : (this.isPotionActive(Potion.digSlowdown) ? 6 + (1 + this.getActivePotionEffect(Potion.digSlowdown).getAmplifier()) * 2 : 6);
    }

    /**
     * Swings the item the player is holding.
     */
    public void swingItem()
    {
        ItemStack stack = this.getHeldItem();

        if (stack != null && stack.getItem() != null)
        {
            Item item = stack.getItem();
            if (item.onEntitySwing(this, stack))
            {
                return;
            }
        }

        if (!this.isSwingInProgress || this.swingProgressInt >= this.getArmSwingAnimationEnd() / 2 || this.swingProgressInt < 0)
        {
            this.swingProgressInt = -1;
            this.isSwingInProgress = true;

            if (this.worldObj instanceof WorldServer)
            {
                ((WorldServer)this.worldObj).getEntityTracker().func_151247_a(this, new S0BPacketAnimation(this, 0));
            }
        }
    }

    @SideOnly(Side.CLIENT)
    public void handleHealthUpdate(byte p_70103_1_)
    {
        if (p_70103_1_ == 2)
        {
            this.limbSwingAmount = 1.5F;
            this.hurtResistantTime = this.maxHurtResistantTime;
            this.hurtTime = this.maxHurtTime = 10;
            this.attackedAtYaw = 0.0F;
            this.playSound(this.getHurtSound(), this.getSoundVolume(), (this.rand.nextFloat() - this.rand.nextFloat()) * 0.2F + 1.0F);
            this.attackEntityFrom(DamageSource.generic, 0.0F);
        }
        else if (p_70103_1_ == 3)
        {
            this.playSound(this.getDeathSound(), this.getSoundVolume(), (this.rand.nextFloat() - this.rand.nextFloat()) * 0.2F + 1.0F);
            this.setHealth(0.0F);
            this.onDeath(DamageSource.generic);
        }
        else
        {
            super.handleHealthUpdate(p_70103_1_);
        }
    }

    /**
     * sets the dead flag. Used when you fall off the bottom of the world.
     */
    protected void kill()
    {
        this.attackEntityFrom(DamageSource.outOfWorld, 4.0F);
    }

    /**
     * Updates the arm swing progress counters and animation progress
     */
    protected void updateArmSwingProgress()
    {
        int i = this.getArmSwingAnimationEnd();

        if (this.isSwingInProgress)
        {
            ++this.swingProgressInt;

            if (this.swingProgressInt >= i)
            {
                this.swingProgressInt = 0;
                this.isSwingInProgress = false;
            }
        }
        else
        {
            this.swingProgressInt = 0;
        }

        this.swingProgress = (float)this.swingProgressInt / (float)i;
    }

    public IAttributeInstance getEntityAttribute(IAttribute p_110148_1_)
    {
        return this.getAttributeMap().getAttributeInstance(p_110148_1_);
    }

    public BaseAttributeMap getAttributeMap()
    {
        if (this.attributeMap == null)
        {
            this.attributeMap = new ServersideAttributeMap();
        }

        return this.attributeMap;
    }

    /**
     * Get this Entity's EnumCreatureAttribute
     */
    public EnumCreatureAttribute getCreatureAttribute()
    {
        return EnumCreatureAttribute.UNDEFINED;
    }

    /**
     * Returns the item that this EntityLiving is holding, if any.
     */
    public abstract ItemStack getHeldItem();

    /**
     * 0: Tool in Hand; 1-4: Armor
     */
    public abstract ItemStack getEquipmentInSlot(int p_71124_1_);

    /**
     * Sets the held item, or an armor slot. Slot 0 is held item. Slot 1-4 is armor. Params: Item, slot
     */
    public abstract void setCurrentItemOrArmor(int p_70062_1_, ItemStack p_70062_2_);

    /**
     * Set sprinting switch for Entity.
     */
    public void setSprinting(boolean p_70031_1_)
    {
        super.setSprinting(p_70031_1_);
        IAttributeInstance iattributeinstance = this.getEntityAttribute(SharedMonsterAttributes.movementSpeed);

        if (iattributeinstance.getModifier(sprintingSpeedBoostModifierUUID) != null)
        {
            iattributeinstance.removeModifier(sprintingSpeedBoostModifier);
        }

        if (p_70031_1_)
        {
            iattributeinstance.applyModifier(sprintingSpeedBoostModifier);
        }
    }

    public abstract ItemStack[] getLastActiveItems();

    /**
     * Returns the volume for the sounds this mob makes.
     */
    protected float getSoundVolume()
    {
        return 1.0F;
    }

    /**
     * Gets the pitch of living sounds in living entities.
     */
    protected float getSoundPitch()
    {
        return this.isChild() ? (this.rand.nextFloat() - this.rand.nextFloat()) * 0.2F + 1.5F : (this.rand.nextFloat() - this.rand.nextFloat()) * 0.2F + 1.0F;
    }

    /**
     * Dead and sleeping entities cannot move
     */
    protected boolean isMovementBlocked()
    {
        return this.getHealth() <= 0.0F;
    }

    /**
     * Sets the position of the entity, keeps yaw/pitch,  and updates the 'last' variables
     */
    public void setPositionAndUpdate(double p_70634_1_, double p_70634_3_, double p_70634_5_)
    {
        this.setLocationAndAngles(p_70634_1_, p_70634_3_, p_70634_5_, this.rotationYaw, this.rotationPitch);
    }

    /**
     * Moves the entity to a position out of the way of its mount.
     */
    public void dismountEntity(Entity p_110145_1_)
    {
        double d0 = p_110145_1_.posX;
        double d1 = p_110145_1_.boundingBox.minY + (double)p_110145_1_.height;
        double d2 = p_110145_1_.posZ;
        byte b0 = 1;

        for (int i = -b0; i <= b0; ++i)
        {
            for (int j = -b0; j < b0; ++j)
            {
                if (i != 0 || j != 0)
                {
                    int k = (int)(this.posX + (double)i);
                    int l = (int)(this.posZ + (double)j);
                    AxisAlignedBB axisalignedbb = this.boundingBox.getOffsetBoundingBox((double)i, 1.0D, (double)j);

                    if (this.worldObj.func_147461_a(axisalignedbb).isEmpty())
                    {
                        if (World.doesBlockHaveSolidTopSurface(this.worldObj, k, (int)this.posY, l))
                        {
                            this.setPositionAndUpdate(this.posX + (double)i, this.posY + 1.0D, this.posZ + (double)j);
                            return;
                        }

                        if (World.doesBlockHaveSolidTopSurface(this.worldObj, k, (int)this.posY - 1, l) || this.worldObj.getBlock(k, (int)this.posY - 1, l).getMaterial() == Material.water)
                        {
                            d0 = this.posX + (double)i;
                            d1 = this.posY + 1.0D;
                            d2 = this.posZ + (double)j;
                        }
                    }
                }
            }
        }

        this.setPositionAndUpdate(d0, d1, d2);
    }

    @SideOnly(Side.CLIENT)
    public boolean getAlwaysRenderNameTagForRender()
    {
        return false;
    }

    /**
     * Gets the Icon Index of the item currently held
     */
    @SideOnly(Side.CLIENT)
    public IIcon getItemIcon(ItemStack p_70620_1_, int p_70620_2_)
    {
        return p_70620_1_.getItem().requiresMultipleRenderPasses() ? p_70620_1_.getItem().getIconFromDamageForRenderPass(p_70620_1_.getItemDamage(), p_70620_2_) : p_70620_1_.getIconIndex();
    }

    /**
     * Causes this entity to do an upwards motion (jumping).
     */
    protected void jump()
    {
        this.motionY = 0.41999998688697815D;

        if (this.isPotionActive(Potion.jump))
        {
            this.motionY += (double)((float)(this.getActivePotionEffect(Potion.jump).getAmplifier() + 1) * 0.1F);
        }

        if (this.isSprinting())
        {
            float f = this.rotationYaw * 0.017453292F;
            this.motionX -= (double)(MathHelper.sin(f) * 0.2F);
            this.motionZ += (double)(MathHelper.cos(f) * 0.2F);
        }

        this.isAirBorne = true;
        ForgeHooks.onLivingJump(this);
    }

    /**
     * Moves the entity based on the specified heading.  Args: strafe, forward
     */
    public void moveEntityWithHeading(float p_70612_1_, float p_70612_2_)
    {
        double d0;

        if (this.isInWater() && (!(this instanceof EntityPlayer) || !((EntityPlayer)this).capabilities.isFlying))
        {
            d0 = this.posY;
            this.moveFlying(p_70612_1_, p_70612_2_, this.isAIEnabled() ? 0.04F : 0.02F);
            this.moveEntity(this.motionX, this.motionY, this.motionZ);
            this.motionX *= 0.800000011920929D;
            this.motionY *= 0.800000011920929D;
            this.motionZ *= 0.800000011920929D;
            this.motionY -= 0.02D;

            if (this.isCollidedHorizontally && this.isOffsetPositionInLiquid(this.motionX, this.motionY + 0.6000000238418579D - this.posY + d0, this.motionZ))
            {
                this.motionY = 0.30000001192092896D;
            }
        }
        else if (this.handleLavaMovement() && (!(this instanceof EntityPlayer) || !((EntityPlayer)this).capabilities.isFlying))
        {
            d0 = this.posY;
            this.moveFlying(p_70612_1_, p_70612_2_, 0.02F);
            this.moveEntity(this.motionX, this.motionY, this.motionZ);
            this.motionX *= 0.5D;
            this.motionY *= 0.5D;
            this.motionZ *= 0.5D;
            this.motionY -= 0.02D;

            if (this.isCollidedHorizontally && this.isOffsetPositionInLiquid(this.motionX, this.motionY + 0.6000000238418579D - this.posY + d0, this.motionZ))
            {
                this.motionY = 0.30000001192092896D;
            }
        }
        else
        {
            float f2 = 0.91F;

            if (this.onGround)
            {
                f2 = this.worldObj.getBlock(MathHelper.floor_double(this.posX), MathHelper.floor_double(this.boundingBox.minY) - 1, MathHelper.floor_double(this.posZ)).slipperiness * 0.91F;
            }

            float f3 = 0.16277136F / (f2 * f2 * f2);
            float f4;

            if (this.onGround)
            {
                f4 = this.getAIMoveSpeed() * f3;
            }
            else
            {
                f4 = this.jumpMovementFactor;
            }

            this.moveFlying(p_70612_1_, p_70612_2_, f4);
            f2 = 0.91F;

            if (this.onGround)
            {
                f2 = this.worldObj.getBlock(MathHelper.floor_double(this.posX), MathHelper.floor_double(this.boundingBox.minY) - 1, MathHelper.floor_double(this.posZ)).slipperiness * 0.91F;
            }

            if (this.isOnLadder())
            {
                float f5 = 0.15F;

                if (this.motionX < (double)(-f5))
                {
                    this.motionX = (double)(-f5);
                }

                if (this.motionX > (double)f5)
                {
                    this.motionX = (double)f5;
                }

                if (this.motionZ < (double)(-f5))
                {
                    this.motionZ = (double)(-f5);
                }

                if (this.motionZ > (double)f5)
                {
                    this.motionZ = (double)f5;
                }

                this.fallDistance = 0.0F;

                if (this.motionY < -0.15D)
                {
                    this.motionY = -0.15D;
                }

                boolean flag = this.isSneaking() && this instanceof EntityPlayer;

                if (flag && this.motionY < 0.0D)
                {
                    this.motionY = 0.0D;
                }
            }

            this.moveEntity(this.motionX, this.motionY, this.motionZ);

            if (this.isCollidedHorizontally && this.isOnLadder())
            {
                this.motionY = 0.2D;
            }

            if (this.worldObj.isRemote && (!this.worldObj.blockExists((int)this.posX, 0, (int)this.posZ) || !this.worldObj.getChunkFromBlockCoords((int)this.posX, (int)this.posZ).isChunkLoaded))
            {
                if (this.posY > 0.0D)
                {
                    this.motionY = -0.1D;
                }
                else
                {
                    this.motionY = 0.0D;
                }
            }
            else
            {
                this.motionY -= 0.08D;
            }

            this.motionY *= 0.9800000190734863D;
            this.motionX *= (double)f2;
            this.motionZ *= (double)f2;
        }

        this.prevLimbSwingAmount = this.limbSwingAmount;
        d0 = this.posX - this.prevPosX;
        double d1 = this.posZ - this.prevPosZ;
        float f6 = MathHelper.sqrt_double(d0 * d0 + d1 * d1) * 4.0F;

        if (f6 > 1.0F)
        {
            f6 = 1.0F;
        }

        this.limbSwingAmount += (f6 - this.limbSwingAmount) * 0.4F;
        this.limbSwing += this.limbSwingAmount;
    }

    /**
     * Returns true if the newer Entity AI code should be run
     */
    protected boolean isAIEnabled()
    {
        return false;
    }

    /**
     * the movespeed used for the new AI system
     */
    public float getAIMoveSpeed()
    {
        return this.isAIEnabled() ? this.landMovementFactor : 0.1F;
    }

    /**
     * set the movespeed used for the new AI system
     */
    public void setAIMoveSpeed(float p_70659_1_)
    {
        this.landMovementFactor = p_70659_1_;
    }

    public boolean attackEntityAsMob(Entity p_70652_1_)
    {
        this.setLastAttacker(p_70652_1_);
        return false;
    }

    /**
     * Returns whether player is sleeping or not
     */
    public boolean isPlayerSleeping()
    {
        return false;
    }

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        if (ForgeHooks.onLivingUpdate(this)) return;
        super.onUpdate();

        if (!this.worldObj.isRemote)
        {
            int i = this.getArrowCountInEntity();

            if (i > 0)
            {
                if (this.arrowHitTimer <= 0)
                {
                    this.arrowHitTimer = 20 * (30 - i);
                }

                --this.arrowHitTimer;

                if (this.arrowHitTimer <= 0)
                {
                    this.setArrowCountInEntity(i - 1);
                }
            }

            for (int j = 0; j < 5; ++j)
            {
                ItemStack itemstack = this.previousEquipment[j];
                ItemStack itemstack1 = this.getEquipmentInSlot(j);

                if (!ItemStack.areItemStacksEqual(itemstack1, itemstack))
                {
                    ((WorldServer)this.worldObj).getEntityTracker().func_151247_a(this, new S04PacketEntityEquipment(this.getEntityId(), j, itemstack1));

                    if (itemstack != null)
                    {
                        this.attributeMap.removeAttributeModifiers(itemstack.getAttributeModifiers());
                    }

                    if (itemstack1 != null)
                    {
                        this.attributeMap.applyAttributeModifiers(itemstack1.getAttributeModifiers());
                    }

                    this.previousEquipment[j] = itemstack1 == null ? null : itemstack1.copy();
                }
            }

            if (this.ticksExisted % 20 == 0)
            {
                this.func_110142_aN().func_94549_h();
            }
        }

        this.onLivingUpdate();
        double d0 = this.posX - this.prevPosX;
        double d1 = this.posZ - this.prevPosZ;
        float f = (float)(d0 * d0 + d1 * d1);
        float f1 = this.renderYawOffset;
        float f2 = 0.0F;
        this.field_70768_au = this.field_110154_aX;
        float f3 = 0.0F;

        if (f > 0.0025000002F)
        {
            f3 = 1.0F;
            f2 = (float)Math.sqrt((double)f) * 3.0F;
            f1 = (float)Math.atan2(d1, d0) * 180.0F / (float)Math.PI - 90.0F;
        }

        if (this.swingProgress > 0.0F)
        {
            f1 = this.rotationYaw;
        }

        if (!this.onGround)
        {
            f3 = 0.0F;
        }

        this.field_110154_aX += (f3 - this.field_110154_aX) * 0.3F;
        this.worldObj.theProfiler.startSection("headTurn");
        f2 = this.func_110146_f(f1, f2);
        this.worldObj.theProfiler.endSection();
        this.worldObj.theProfiler.startSection("rangeChecks");

        while (this.rotationYaw - this.prevRotationYaw < -180.0F)
        {
            this.prevRotationYaw -= 360.0F;
        }

        while (this.rotationYaw - this.prevRotationYaw >= 180.0F)
        {
            this.prevRotationYaw += 360.0F;
        }

        while (this.renderYawOffset - this.prevRenderYawOffset < -180.0F)
        {
            this.prevRenderYawOffset -= 360.0F;
        }

        while (this.renderYawOffset - this.prevRenderYawOffset >= 180.0F)
        {
            this.prevRenderYawOffset += 360.0F;
        }

        while (this.rotationPitch - this.prevRotationPitch < -180.0F)
        {
            this.prevRotationPitch -= 360.0F;
        }

        while (this.rotationPitch - this.prevRotationPitch >= 180.0F)
        {
            this.prevRotationPitch += 360.0F;
        }

        while (this.rotationYawHead - this.prevRotationYawHead < -180.0F)
        {
            this.prevRotationYawHead -= 360.0F;
        }

        while (this.rotationYawHead - this.prevRotationYawHead >= 180.0F)
        {
            this.prevRotationYawHead += 360.0F;
        }

        this.worldObj.theProfiler.endSection();
        this.field_70764_aw += f2;
    }

    protected float func_110146_f(float p_110146_1_, float p_110146_2_)
    {
        float f2 = MathHelper.wrapAngleTo180_float(p_110146_1_ - this.renderYawOffset);
        this.renderYawOffset += f2 * 0.3F;
        float f3 = MathHelper.wrapAngleTo180_float(this.rotationYaw - this.renderYawOffset);
        boolean flag = f3 < -90.0F || f3 >= 90.0F;

        if (f3 < -75.0F)
        {
            f3 = -75.0F;
        }

        if (f3 >= 75.0F)
        {
            f3 = 75.0F;
        }

        this.renderYawOffset = this.rotationYaw - f3;

        if (f3 * f3 > 2500.0F)
        {
            this.renderYawOffset += f3 * 0.2F;
        }

        if (flag)
        {
            p_110146_2_ *= -1.0F;
        }

        return p_110146_2_;
    }

    /**
     * Called frequently so the entity can update its state every tick as required. For example, zombies and skeletons
     * use this to react to sunlight and start to burn.
     */
    public void onLivingUpdate()
    {
        if (this.jumpTicks > 0)
        {
            --this.jumpTicks;
        }

        if (this.newPosRotationIncrements > 0)
        {
            double d0 = this.posX + (this.newPosX - this.posX) / (double)this.newPosRotationIncrements;
            double d1 = this.posY + (this.newPosY - this.posY) / (double)this.newPosRotationIncrements;
            double d2 = this.posZ + (this.newPosZ - this.posZ) / (double)this.newPosRotationIncrements;
            double d3 = MathHelper.wrapAngleTo180_double(this.newRotationYaw - (double)this.rotationYaw);
            this.rotationYaw = (float)((double)this.rotationYaw + d3 / (double)this.newPosRotationIncrements);
            this.rotationPitch = (float)((double)this.rotationPitch + (this.newRotationPitch - (double)this.rotationPitch) / (double)this.newPosRotationIncrements);
            --this.newPosRotationIncrements;
            this.setPosition(d0, d1, d2);
            this.setRotation(this.rotationYaw, this.rotationPitch);
        }
        else if (!this.isClientWorld())
        {
            this.motionX *= 0.98D;
            this.motionY *= 0.98D;
            this.motionZ *= 0.98D;
        }

        if (Math.abs(this.motionX) < 0.005D)
        {
            this.motionX = 0.0D;
        }

        if (Math.abs(this.motionY) < 0.005D)
        {
            this.motionY = 0.0D;
        }

        if (Math.abs(this.motionZ) < 0.005D)
        {
            this.motionZ = 0.0D;
        }

        this.worldObj.theProfiler.startSection("ai");

        if (this.isMovementBlocked())
        {
            this.isJumping = false;
            this.moveStrafing = 0.0F;
            this.moveForward = 0.0F;
            this.randomYawVelocity = 0.0F;
        }
        else if (this.isClientWorld())
        {
            if (this.isAIEnabled())
            {
                this.worldObj.theProfiler.startSection("newAi");
                this.updateAITasks();
                this.worldObj.theProfiler.endSection();
            }
            else
            {
                this.worldObj.theProfiler.startSection("oldAi");
                this.updateEntityActionState();
                this.worldObj.theProfiler.endSection();
                this.rotationYawHead = this.rotationYaw;
            }
        }

        this.worldObj.theProfiler.endSection();
        this.worldObj.theProfiler.startSection("jump");

        if (this.isJumping)
        {
            if (!this.isInWater() && !this.handleLavaMovement())
            {
                if (this.onGround && this.jumpTicks == 0)
                {
                    this.jump();
                    this.jumpTicks = 10;
                }
            }
            else
            {
                this.motionY += 0.03999999910593033D;
            }
        }
        else
        {
            this.jumpTicks = 0;
        }

        this.worldObj.theProfiler.endSection();
        this.worldObj.theProfiler.startSection("travel");
        this.moveStrafing *= 0.98F;
        this.moveForward *= 0.98F;
        this.randomYawVelocity *= 0.9F;
        this.moveEntityWithHeading(this.moveStrafing, this.moveForward);
        this.worldObj.theProfiler.endSection();
        this.worldObj.theProfiler.startSection("push");

        if (!this.worldObj.isRemote)
        {
            this.collideWithNearbyEntities();
        }

        this.worldObj.theProfiler.endSection();
    }

    protected void updateAITasks() {}

    protected void collideWithNearbyEntities()
    {
        List list = this.worldObj.getEntitiesWithinAABBExcludingEntity(this, this.boundingBox.expand(0.20000000298023224D, 0.0D, 0.20000000298023224D));

        if (list != null && !list.isEmpty())
        {
            for (int i = 0; i < list.size(); ++i)
            {
                Entity entity = (Entity)list.get(i);

                if (entity.canBePushed())
                {
                    this.collideWithEntity(entity);
                }
            }
        }
    }

    protected void collideWithEntity(Entity p_82167_1_)
    {
        p_82167_1_.applyEntityCollision(this);
    }

    /**
     * Handles updating while being ridden by an entity
     */
    public void updateRidden()
    {
        super.updateRidden();
        this.field_70768_au = this.field_110154_aX;
        this.field_110154_aX = 0.0F;
        this.fallDistance = 0.0F;
    }

    /**
     * Sets the position and rotation. Only difference from the other one is no bounding on the rotation. Args: posX,
     * posY, posZ, yaw, pitch
     */
    @SideOnly(Side.CLIENT)
    public void setPositionAndRotation2(double p_70056_1_, double p_70056_3_, double p_70056_5_, float p_70056_7_, float p_70056_8_, int p_70056_9_)
    {
        this.yOffset = 0.0F;
        this.newPosX = p_70056_1_;
        this.newPosY = p_70056_3_;
        this.newPosZ = p_70056_5_;
        this.newRotationYaw = (double)p_70056_7_;
        this.newRotationPitch = (double)p_70056_8_;
        this.newPosRotationIncrements = p_70056_9_;
    }

    /**
     * main AI tick function, replaces updateEntityActionState
     */
    protected void updateAITick() {}

    protected void updateEntityActionState()
    {
        ++this.entityAge;
    }

    public void setJumping(boolean p_70637_1_)
    {
        this.isJumping = p_70637_1_;
    }

    /**
     * Called whenever an item is picked up from walking over it. Args: pickedUpEntity, stackSize
     */
    public void onItemPickup(Entity p_71001_1_, int p_71001_2_)
    {
        if (!p_71001_1_.isDead && !this.worldObj.isRemote)
        {
            EntityTracker entitytracker = ((WorldServer)this.worldObj).getEntityTracker();

            if (p_71001_1_ instanceof EntityItem)
            {
                entitytracker.func_151247_a(p_71001_1_, new S0DPacketCollectItem(p_71001_1_.getEntityId(), this.getEntityId()));
            }

            if (p_71001_1_ instanceof EntityArrow)
            {
                entitytracker.func_151247_a(p_71001_1_, new S0DPacketCollectItem(p_71001_1_.getEntityId(), this.getEntityId()));
            }

            if (p_71001_1_ instanceof EntityXPOrb)
            {
                entitytracker.func_151247_a(p_71001_1_, new S0DPacketCollectItem(p_71001_1_.getEntityId(), this.getEntityId()));
            }
        }
    }

    /**
     * returns true if the entity provided in the argument can be seen. (Raytrace)
     */
    public boolean canEntityBeSeen(Entity p_70685_1_)
    {
        return this.worldObj.rayTraceBlocks(Vec3.createVectorHelper(this.posX, this.posY + (double)this.getEyeHeight(), this.posZ), Vec3.createVectorHelper(p_70685_1_.posX, p_70685_1_.posY + (double)p_70685_1_.getEyeHeight(), p_70685_1_.posZ)) == null;
    }

    /**
     * returns a (normalized) vector of where this entity is looking
     */
    public Vec3 getLookVec()
    {
        return this.getLook(1.0F);
    }

    /**
     * interpolated look vector
     */
    public Vec3 getLook(float p_70676_1_)
    {
        float f1;
        float f2;
        float f3;
        float f4;

        if (p_70676_1_ == 1.0F)
        {
            f1 = MathHelper.cos(-this.rotationYaw * 0.017453292F - (float)Math.PI);
            f2 = MathHelper.sin(-this.rotationYaw * 0.017453292F - (float)Math.PI);
            f3 = -MathHelper.cos(-this.rotationPitch * 0.017453292F);
            f4 = MathHelper.sin(-this.rotationPitch * 0.017453292F);
            return Vec3.createVectorHelper((double)(f2 * f3), (double)f4, (double)(f1 * f3));
        }
        else
        {
            f1 = this.prevRotationPitch + (this.rotationPitch - this.prevRotationPitch) * p_70676_1_;
            f2 = this.prevRotationYaw + (this.rotationYaw - this.prevRotationYaw) * p_70676_1_;
            f3 = MathHelper.cos(-f2 * 0.017453292F - (float)Math.PI);
            f4 = MathHelper.sin(-f2 * 0.017453292F - (float)Math.PI);
            float f5 = -MathHelper.cos(-f1 * 0.017453292F);
            float f6 = MathHelper.sin(-f1 * 0.017453292F);
            return Vec3.createVectorHelper((double)(f4 * f5), (double)f6, (double)(f3 * f5));
        }
    }

    /**
     * Returns where in the swing animation the living entity is (from 0 to 1).  Args: partialTickTime
     */
    @SideOnly(Side.CLIENT)
    public float getSwingProgress(float p_70678_1_)
    {
        float f1 = this.swingProgress - this.prevSwingProgress;

        if (f1 < 0.0F)
        {
            ++f1;
        }

        return this.prevSwingProgress + f1 * p_70678_1_;
    }

    /**
     * interpolated position vector
     */
    @SideOnly(Side.CLIENT)
    public Vec3 getPosition(float p_70666_1_)
    {
        if (p_70666_1_ == 1.0F)
        {
            return Vec3.createVectorHelper(this.posX, this.posY, this.posZ);
        }
        else
        {
            double d0 = this.prevPosX + (this.posX - this.prevPosX) * (double)p_70666_1_;
            double d1 = this.prevPosY + (this.posY - this.prevPosY) * (double)p_70666_1_;
            double d2 = this.prevPosZ + (this.posZ - this.prevPosZ) * (double)p_70666_1_;
            return Vec3.createVectorHelper(d0, d1, d2);
        }
    }

    /**
     * Performs a ray trace for the distance specified and using the partial tick time. Args: distance, partialTickTime
     */
    @SideOnly(Side.CLIENT)
    public MovingObjectPosition rayTrace(double p_70614_1_, float p_70614_3_)
    {
        Vec3 vec3 = this.getPosition(p_70614_3_);
        Vec3 vec31 = this.getLook(p_70614_3_);
        Vec3 vec32 = vec3.addVector(vec31.xCoord * p_70614_1_, vec31.yCoord * p_70614_1_, vec31.zCoord * p_70614_1_);
        return this.worldObj.func_147447_a(vec3, vec32, false, false, true);
    }

    /**
     * Returns whether the entity is in a local (client) world
     */
    public boolean isClientWorld()
    {
        return !this.worldObj.isRemote;
    }

    /**
     * Returns true if other Entities should be prevented from moving through this Entity.
     */
    public boolean canBeCollidedWith()
    {
        return !this.isDead;
    }

    /**
     * Returns true if this entity should push and be pushed by other entities when colliding.
     */
    public boolean canBePushed()
    {
        return !this.isDead;
    }

    public float getEyeHeight()
    {
        return this.height * 0.85F;
    }

    /**
     * Sets that this entity has been attacked.
     */
    protected void setBeenAttacked()
    {
        this.velocityChanged = this.rand.nextDouble() >= this.getEntityAttribute(SharedMonsterAttributes.knockbackResistance).getAttributeValue();
    }

    public float getRotationYawHead()
    {
        return this.rotationYawHead;
    }

    /**
     * Sets the head's yaw rotation of the entity.
     */
    @SideOnly(Side.CLIENT)
    public void setRotationYawHead(float p_70034_1_)
    {
        this.rotationYawHead = p_70034_1_;
    }

    public float getAbsorptionAmount()
    {
        return this.field_110151_bq;
    }

    public void setAbsorptionAmount(float p_110149_1_)
    {
        if (p_110149_1_ < 0.0F)
        {
            p_110149_1_ = 0.0F;
        }

        this.field_110151_bq = p_110149_1_;
    }

    public Team getTeam()
    {
        return null;
    }

    public boolean isOnSameTeam(EntityLivingBase p_142014_1_)
    {
        return this.isOnTeam(p_142014_1_.getTeam());
    }

    /**
     * Returns true if the entity is on a specific team.
     */
    public boolean isOnTeam(Team p_142012_1_)
    {
        return this.getTeam() != null ? this.getTeam().isSameTeam(p_142012_1_) : false;
    }

    /***
     * Removes all potion effects that have curativeItem as a curative item for its effect
     * @param curativeItem The itemstack we are using to cure potion effects
     */
    public void curePotionEffects(ItemStack curativeItem)
    {
        Iterator<Integer> potionKey = activePotionsMap.keySet().iterator();

        if (worldObj.isRemote)
        {
            return;
        }

        while (potionKey.hasNext())
        {
            Integer key = potionKey.next();
            PotionEffect effect = (PotionEffect)activePotionsMap.get(key);

            if (effect.isCurativeItem(curativeItem))
            {
                potionKey.remove();
                onFinishedPotionEffect(effect);
            }
        }
    }

    /**
     * Returns true if the entity's rider (EntityPlayer) should face forward when mounted.
     * currently only used in vanilla code by pigs.
     *
     * @param player The player who is riding the entity.
     * @return If the player should orient the same direction as this entity.
     */
    public boolean shouldRiderFaceForward(EntityPlayer player)
    {
        return this instanceof EntityPig;
    }

    public void func_152111_bt() {}

    public void func_152112_bu() {}
}