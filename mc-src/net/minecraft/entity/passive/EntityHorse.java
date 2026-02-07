package net.minecraft.entity.passive;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.Iterator;
import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.block.material.Material;
import net.minecraft.command.IEntitySelector;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityAgeable;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.IEntityLivingData;
import net.minecraft.entity.SharedMonsterAttributes;
import net.minecraft.entity.ai.EntityAIFollowParent;
import net.minecraft.entity.ai.EntityAILookIdle;
import net.minecraft.entity.ai.EntityAIMate;
import net.minecraft.entity.ai.EntityAIPanic;
import net.minecraft.entity.ai.EntityAIRunAroundLikeCrazy;
import net.minecraft.entity.ai.EntityAISwimming;
import net.minecraft.entity.ai.EntityAIWander;
import net.minecraft.entity.ai.EntityAIWatchClosest;
import net.minecraft.entity.ai.attributes.IAttribute;
import net.minecraft.entity.ai.attributes.IAttributeInstance;
import net.minecraft.entity.ai.attributes.RangedAttribute;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Items;
import net.minecraft.inventory.AnimalChest;
import net.minecraft.inventory.IInvBasic;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.nbt.NBTTagList;
import net.minecraft.pathfinding.PathEntity;
import net.minecraft.potion.Potion;
import net.minecraft.util.DamageSource;
import net.minecraft.util.MathHelper;
import net.minecraft.util.StatCollector;
import net.minecraft.world.World;

public class EntityHorse extends EntityAnimal implements IInvBasic
{
    private static final IEntitySelector horseBreedingSelector = new IEntitySelector()
    {
        private static final String __OBFID = "CL_00001642";
        /**
         * Return whether the specified entity is applicable to this filter.
         */
        public boolean isEntityApplicable(Entity p_82704_1_)
        {
            return p_82704_1_ instanceof EntityHorse && ((EntityHorse)p_82704_1_).func_110205_ce();
        }
    };
    private static final IAttribute horseJumpStrength = (new RangedAttribute("horse.jumpStrength", 0.7D, 0.0D, 2.0D)).setDescription("Jump Strength").setShouldWatch(true);
    private static final String[] horseArmorTextures = new String[] {null, "textures/entity/horse/armor/horse_armor_iron.png", "textures/entity/horse/armor/horse_armor_gold.png", "textures/entity/horse/armor/horse_armor_diamond.png"};
    private static final String[] field_110273_bx = new String[] {"", "meo", "goo", "dio"};
    private static final int[] armorValues = new int[] {0, 5, 7, 11};
    private static final String[] horseTextures = new String[] {"textures/entity/horse/horse_white.png", "textures/entity/horse/horse_creamy.png", "textures/entity/horse/horse_chestnut.png", "textures/entity/horse/horse_brown.png", "textures/entity/horse/horse_black.png", "textures/entity/horse/horse_gray.png", "textures/entity/horse/horse_darkbrown.png"};
    private static final String[] field_110269_bA = new String[] {"hwh", "hcr", "hch", "hbr", "hbl", "hgr", "hdb"};
    private static final String[] horseMarkingTextures = new String[] {null, "textures/entity/horse/horse_markings_white.png", "textures/entity/horse/horse_markings_whitefield.png", "textures/entity/horse/horse_markings_whitedots.png", "textures/entity/horse/horse_markings_blackdots.png"};
    private static final String[] field_110292_bC = new String[] {"", "wo_", "wmo", "wdo", "bdo"};
    private int eatingHaystackCounter;
    private int openMouthCounter;
    private int jumpRearingCounter;
    public int field_110278_bp;
    public int field_110279_bq;
    protected boolean horseJumping;
    private AnimalChest horseChest;
    private boolean hasReproduced;
    /** "The higher this value, the more likely the horse is to be tamed next time a player rides it." */
    protected int temper;
    protected float jumpPower;
    private boolean field_110294_bI;
    private float headLean;
    private float prevHeadLean;
    private float rearingAmount;
    private float prevRearingAmount;
    private float mouthOpenness;
    private float prevMouthOpenness;
    private int field_110285_bP;
    private String field_110286_bQ;
    private String[] field_110280_bR = new String[3];
    private static final String __OBFID = "CL_00001641";

    public EntityHorse(World p_i1685_1_)
    {
        super(p_i1685_1_);
        this.setSize(1.4F, 1.6F);
        this.isImmuneToFire = false;
        this.setChested(false);
        this.getNavigator().setAvoidsWater(true);
        this.tasks.addTask(0, new EntityAISwimming(this));
        this.tasks.addTask(1, new EntityAIPanic(this, 1.2D));
        this.tasks.addTask(1, new EntityAIRunAroundLikeCrazy(this, 1.2D));
        this.tasks.addTask(2, new EntityAIMate(this, 1.0D));
        this.tasks.addTask(4, new EntityAIFollowParent(this, 1.0D));
        this.tasks.addTask(6, new EntityAIWander(this, 0.7D));
        this.tasks.addTask(7, new EntityAIWatchClosest(this, EntityPlayer.class, 6.0F));
        this.tasks.addTask(8, new EntityAILookIdle(this));
        this.func_110226_cD();
    }

    protected void entityInit()
    {
        super.entityInit();
        this.dataWatcher.addObject(16, Integer.valueOf(0));
        this.dataWatcher.addObject(19, Byte.valueOf((byte)0));
        this.dataWatcher.addObject(20, Integer.valueOf(0));
        this.dataWatcher.addObject(21, String.valueOf(""));
        this.dataWatcher.addObject(22, Integer.valueOf(0));
    }

    public void setHorseType(int p_110214_1_)
    {
        this.dataWatcher.updateObject(19, Byte.valueOf((byte)p_110214_1_));
        this.func_110230_cF();
    }

    /**
     * returns the horse type
     */
    public int getHorseType()
    {
        return this.dataWatcher.getWatchableObjectByte(19);
    }

    public void setHorseVariant(int p_110235_1_)
    {
        this.dataWatcher.updateObject(20, Integer.valueOf(p_110235_1_));
        this.func_110230_cF();
    }

    public int getHorseVariant()
    {
        return this.dataWatcher.getWatchableObjectInt(20);
    }

    /**
     * Gets the name of this command sender (usually username, but possibly "Rcon")
     */
    public String getCommandSenderName()
    {
        if (this.hasCustomNameTag())
        {
            return this.getCustomNameTag();
        }
        else
        {
            int i = this.getHorseType();

            switch (i)
            {
                case 0:
                default:
                    return StatCollector.translateToLocal("entity.horse.name");
                case 1:
                    return StatCollector.translateToLocal("entity.donkey.name");
                case 2:
                    return StatCollector.translateToLocal("entity.mule.name");
                case 3:
                    return StatCollector.translateToLocal("entity.zombiehorse.name");
                case 4:
                    return StatCollector.translateToLocal("entity.skeletonhorse.name");
            }
        }
    }

    private boolean getHorseWatchableBoolean(int p_110233_1_)
    {
        return (this.dataWatcher.getWatchableObjectInt(16) & p_110233_1_) != 0;
    }

    private void setHorseWatchableBoolean(int p_110208_1_, boolean p_110208_2_)
    {
        int j = this.dataWatcher.getWatchableObjectInt(16);

        if (p_110208_2_)
        {
            this.dataWatcher.updateObject(16, Integer.valueOf(j | p_110208_1_));
        }
        else
        {
            this.dataWatcher.updateObject(16, Integer.valueOf(j & ~p_110208_1_));
        }
    }

    public boolean isAdultHorse()
    {
        return !this.isChild();
    }

    public boolean isTame()
    {
        return this.getHorseWatchableBoolean(2);
    }

    public boolean func_110253_bW()
    {
        return this.isAdultHorse();
    }

    public String func_152119_ch()
    {
        return this.dataWatcher.getWatchableObjectString(21);
    }

    public void func_152120_b(String p_152120_1_)
    {
        this.dataWatcher.updateObject(21, p_152120_1_);
    }

    public float getHorseSize()
    {
        int i = this.getGrowingAge();
        return i >= 0 ? 1.0F : 0.5F + (float)(-24000 - i) / -24000.0F * 0.5F;
    }

    /**
     * "Sets the scale for an ageable entity according to the boolean parameter, which says if it's a child."
     */
    public void setScaleForAge(boolean p_98054_1_)
    {
        if (p_98054_1_)
        {
            this.setScale(this.getHorseSize());
        }
        else
        {
            this.setScale(1.0F);
        }
    }

    public boolean isHorseJumping()
    {
        return this.horseJumping;
    }

    public void setHorseTamed(boolean p_110234_1_)
    {
        this.setHorseWatchableBoolean(2, p_110234_1_);
    }

    public void setHorseJumping(boolean p_110255_1_)
    {
        this.horseJumping = p_110255_1_;
    }

    public boolean allowLeashing()
    {
        return !this.func_110256_cu() && super.allowLeashing();
    }

    protected void func_142017_o(float p_142017_1_)
    {
        if (p_142017_1_ > 6.0F && this.isEatingHaystack())
        {
            this.setEatingHaystack(false);
        }
    }

    public boolean isChested()
    {
        return this.getHorseWatchableBoolean(8);
    }

    public int func_110241_cb()
    {
        return this.dataWatcher.getWatchableObjectInt(22);
    }

    /**
     * 0 = iron, 1 = gold, 2 = diamond
     */
    private int getHorseArmorIndex(ItemStack p_110260_1_)
    {
        if (p_110260_1_ == null)
        {
            return 0;
        }
        else
        {
            Item item = p_110260_1_.getItem();
            return item == Items.iron_horse_armor ? 1 : (item == Items.golden_horse_armor ? 2 : (item == Items.diamond_horse_armor ? 3 : 0));
        }
    }

    public boolean isEatingHaystack()
    {
        return this.getHorseWatchableBoolean(32);
    }

    public boolean isRearing()
    {
        return this.getHorseWatchableBoolean(64);
    }

    public boolean func_110205_ce()
    {
        return this.getHorseWatchableBoolean(16);
    }

    public boolean getHasReproduced()
    {
        return this.hasReproduced;
    }

    public void func_146086_d(ItemStack p_146086_1_)
    {
        this.dataWatcher.updateObject(22, Integer.valueOf(this.getHorseArmorIndex(p_146086_1_)));
        this.func_110230_cF();
    }

    public void func_110242_l(boolean p_110242_1_)
    {
        this.setHorseWatchableBoolean(16, p_110242_1_);
    }

    public void setChested(boolean p_110207_1_)
    {
        this.setHorseWatchableBoolean(8, p_110207_1_);
    }

    public void setHasReproduced(boolean p_110221_1_)
    {
        this.hasReproduced = p_110221_1_;
    }

    public void setHorseSaddled(boolean p_110251_1_)
    {
        this.setHorseWatchableBoolean(4, p_110251_1_);
    }

    public int getTemper()
    {
        return this.temper;
    }

    public void setTemper(int p_110238_1_)
    {
        this.temper = p_110238_1_;
    }

    public int increaseTemper(int p_110198_1_)
    {
        int j = MathHelper.clamp_int(this.getTemper() + p_110198_1_, 0, this.getMaxTemper());
        this.setTemper(j);
        return j;
    }

    /**
     * Called when the entity is attacked.
     */
    public boolean attackEntityFrom(DamageSource p_70097_1_, float p_70097_2_)
    {
        Entity entity = p_70097_1_.getEntity();
        return this.riddenByEntity != null && this.riddenByEntity.equals(entity) ? false : super.attackEntityFrom(p_70097_1_, p_70097_2_);
    }

    /**
     * Returns the current armor value as determined by a call to InventoryPlayer.getTotalArmorValue
     */
    public int getTotalArmorValue()
    {
        return armorValues[this.func_110241_cb()];
    }

    /**
     * Returns true if this entity should push and be pushed by other entities when colliding.
     */
    public boolean canBePushed()
    {
        return this.riddenByEntity == null;
    }

    public boolean prepareChunkForSpawn()
    {
        int i = MathHelper.floor_double(this.posX);
        int j = MathHelper.floor_double(this.posZ);
        this.worldObj.getBiomeGenForCoords(i, j);
        return true;
    }

    public void dropChests()
    {
        if (!this.worldObj.isRemote && this.isChested())
        {
            this.dropItem(Item.getItemFromBlock(Blocks.chest), 1);
            this.setChested(false);
        }
    }

    private void func_110266_cB()
    {
        this.openHorseMouth();
        this.worldObj.playSoundAtEntity(this, "eating", 1.0F, 1.0F + (this.rand.nextFloat() - this.rand.nextFloat()) * 0.2F);
    }

    /**
     * Called when the mob is falling. Calculates and applies fall damage.
     */
    protected void fall(float p_70069_1_)
    {
        if (p_70069_1_ > 1.0F)
        {
            this.playSound("mob.horse.land", 0.4F, 1.0F);
        }

        int i = MathHelper.ceiling_float_int(p_70069_1_ * 0.5F - 3.0F);

        if (i > 0)
        {
            this.attackEntityFrom(DamageSource.fall, (float)i);

            if (this.riddenByEntity != null)
            {
                this.riddenByEntity.attackEntityFrom(DamageSource.fall, (float)i);
            }

            Block block = this.worldObj.getBlock(MathHelper.floor_double(this.posX), MathHelper.floor_double(this.posY - 0.2D - (double)this.prevRotationYaw), MathHelper.floor_double(this.posZ));

            if (block.getMaterial() != Material.air)
            {
                Block.SoundType soundtype = block.stepSound;
                this.worldObj.playSoundAtEntity(this, soundtype.getStepResourcePath(), soundtype.getVolume() * 0.5F, soundtype.getPitch() * 0.75F);
            }
        }
    }

    private int func_110225_cC()
    {
        int i = this.getHorseType();
        return this.isChested() && (i == 1 || i == 2) ? 17 : 2;
    }

    private void func_110226_cD()
    {
        AnimalChest animalchest = this.horseChest;
        this.horseChest = new AnimalChest("HorseChest", this.func_110225_cC());
        this.horseChest.func_110133_a(this.getCommandSenderName());

        if (animalchest != null)
        {
            animalchest.func_110132_b(this);
            int i = Math.min(animalchest.getSizeInventory(), this.horseChest.getSizeInventory());

            for (int j = 0; j < i; ++j)
            {
                ItemStack itemstack = animalchest.getStackInSlot(j);

                if (itemstack != null)
                {
                    this.horseChest.setInventorySlotContents(j, itemstack.copy());
                }
            }

            animalchest = null;
        }

        this.horseChest.func_110134_a(this);
        this.func_110232_cE();
    }

    private void func_110232_cE()
    {
        if (!this.worldObj.isRemote)
        {
            this.setHorseSaddled(this.horseChest.getStackInSlot(0) != null);

            if (this.func_110259_cr())
            {
                this.func_146086_d(this.horseChest.getStackInSlot(1));
            }
        }
    }

    /**
     * Called by InventoryBasic.onInventoryChanged() on a array that is never filled.
     */
    public void onInventoryChanged(InventoryBasic p_76316_1_)
    {
        int i = this.func_110241_cb();
        boolean flag = this.isHorseSaddled();
        this.func_110232_cE();

        if (this.ticksExisted > 20)
        {
            if (i == 0 && i != this.func_110241_cb())
            {
                this.playSound("mob.horse.armor", 0.5F, 1.0F);
            }
            else if (i != this.func_110241_cb())
            {
                this.playSound("mob.horse.armor", 0.5F, 1.0F);
            }

            if (!flag && this.isHorseSaddled())
            {
                this.playSound("mob.horse.leather", 0.5F, 1.0F);
            }
        }
    }

    /**
     * Checks if the entity's current position is a valid location to spawn this entity.
     */
    public boolean getCanSpawnHere()
    {
        this.prepareChunkForSpawn();
        return super.getCanSpawnHere();
    }

    protected EntityHorse getClosestHorse(Entity p_110250_1_, double p_110250_2_)
    {
        double d1 = Double.MAX_VALUE;
        Entity entity1 = null;
        List list = this.worldObj.getEntitiesWithinAABBExcludingEntity(p_110250_1_, p_110250_1_.boundingBox.addCoord(p_110250_2_, p_110250_2_, p_110250_2_), horseBreedingSelector);
        Iterator iterator = list.iterator();

        while (iterator.hasNext())
        {
            Entity entity2 = (Entity)iterator.next();
            double d2 = entity2.getDistanceSq(p_110250_1_.posX, p_110250_1_.posY, p_110250_1_.posZ);

            if (d2 < d1)
            {
                entity1 = entity2;
                d1 = d2;
            }
        }

        return (EntityHorse)entity1;
    }

    public double getHorseJumpStrength()
    {
        return this.getEntityAttribute(horseJumpStrength).getAttributeValue();
    }

    /**
     * Returns the sound this mob makes on death.
     */
    protected String getDeathSound()
    {
        this.openHorseMouth();
        int i = this.getHorseType();
        return i == 3 ? "mob.horse.zombie.death" : (i == 4 ? "mob.horse.skeleton.death" : (i != 1 && i != 2 ? "mob.horse.death" : "mob.horse.donkey.death"));
    }

    protected Item getDropItem()
    {
        boolean flag = this.rand.nextInt(4) == 0;
        int i = this.getHorseType();
        return i == 4 ? Items.bone : (i == 3 ? (flag ? Item.getItemById(0) : Items.rotten_flesh) : Items.leather);
    }

    /**
     * Returns the sound this mob makes when it is hurt.
     */
    protected String getHurtSound()
    {
        this.openHorseMouth();

        if (this.rand.nextInt(3) == 0)
        {
            this.makeHorseRear();
        }

        int i = this.getHorseType();
        return i == 3 ? "mob.horse.zombie.hit" : (i == 4 ? "mob.horse.skeleton.hit" : (i != 1 && i != 2 ? "mob.horse.hit" : "mob.horse.donkey.hit"));
    }

    public boolean isHorseSaddled()
    {
        return this.getHorseWatchableBoolean(4);
    }

    /**
     * Returns the sound this mob makes while it's alive.
     */
    protected String getLivingSound()
    {
        this.openHorseMouth();

        if (this.rand.nextInt(10) == 0 && !this.isMovementBlocked())
        {
            this.makeHorseRear();
        }

        int i = this.getHorseType();
        return i == 3 ? "mob.horse.zombie.idle" : (i == 4 ? "mob.horse.skeleton.idle" : (i != 1 && i != 2 ? "mob.horse.idle" : "mob.horse.donkey.idle"));
    }

    protected String getAngrySoundName()
    {
        this.openHorseMouth();
        this.makeHorseRear();
        int i = this.getHorseType();
        return i != 3 && i != 4 ? (i != 1 && i != 2 ? "mob.horse.angry" : "mob.horse.donkey.angry") : null;
    }

    protected void func_145780_a(int p_145780_1_, int p_145780_2_, int p_145780_3_, Block p_145780_4_)
    {
        Block.SoundType soundtype = p_145780_4_.stepSound;

        if (this.worldObj.getBlock(p_145780_1_, p_145780_2_ + 1, p_145780_3_) == Blocks.snow_layer)
        {
            soundtype = Blocks.snow_layer.stepSound;
        }

        if (!p_145780_4_.getMaterial().isLiquid())
        {
            int l = this.getHorseType();

            if (this.riddenByEntity != null && l != 1 && l != 2)
            {
                ++this.field_110285_bP;

                if (this.field_110285_bP > 5 && this.field_110285_bP % 3 == 0)
                {
                    this.playSound("mob.horse.gallop", soundtype.getVolume() * 0.15F, soundtype.getPitch());

                    if (l == 0 && this.rand.nextInt(10) == 0)
                    {
                        this.playSound("mob.horse.breathe", soundtype.getVolume() * 0.6F, soundtype.getPitch());
                    }
                }
                else if (this.field_110285_bP <= 5)
                {
                    this.playSound("mob.horse.wood", soundtype.getVolume() * 0.15F, soundtype.getPitch());
                }
            }
            else if (soundtype == Block.soundTypeWood)
            {
                this.playSound("mob.horse.wood", soundtype.getVolume() * 0.15F, soundtype.getPitch());
            }
            else
            {
                this.playSound("mob.horse.soft", soundtype.getVolume() * 0.15F, soundtype.getPitch());
            }
        }
    }

    protected void applyEntityAttributes()
    {
        super.applyEntityAttributes();
        this.getAttributeMap().registerAttribute(horseJumpStrength);
        this.getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue(53.0D);
        this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(0.22499999403953552D);
    }

    /**
     * Will return how many at most can spawn in a chunk at once.
     */
    public int getMaxSpawnedInChunk()
    {
        return 6;
    }

    public int getMaxTemper()
    {
        return 100;
    }

    /**
     * Returns the volume for the sounds this mob makes.
     */
    protected float getSoundVolume()
    {
        return 0.8F;
    }

    /**
     * Get number of ticks, at least during which the living entity will be silent.
     */
    public int getTalkInterval()
    {
        return 400;
    }

    @SideOnly(Side.CLIENT)
    public boolean func_110239_cn()
    {
        return this.getHorseType() == 0 || this.func_110241_cb() > 0;
    }

    private void func_110230_cF()
    {
        this.field_110286_bQ = null;
    }

    @SideOnly(Side.CLIENT)
    private void setHorseTexturePaths()
    {
        this.field_110286_bQ = "horse/";
        this.field_110280_bR[0] = null;
        this.field_110280_bR[1] = null;
        this.field_110280_bR[2] = null;
        int i = this.getHorseType();
        int j = this.getHorseVariant();
        int k;

        if (i == 0)
        {
            k = j & 255;
            int l = (j & 65280) >> 8;
            this.field_110280_bR[0] = horseTextures[k];
            this.field_110286_bQ = this.field_110286_bQ + field_110269_bA[k];
            this.field_110280_bR[1] = horseMarkingTextures[l];
            this.field_110286_bQ = this.field_110286_bQ + field_110292_bC[l];
        }
        else
        {
            this.field_110280_bR[0] = "";
            this.field_110286_bQ = this.field_110286_bQ + "_" + i + "_";
        }

        k = this.func_110241_cb();
        this.field_110280_bR[2] = horseArmorTextures[k];
        this.field_110286_bQ = this.field_110286_bQ + field_110273_bx[k];
    }

    @SideOnly(Side.CLIENT)
    public String getHorseTexture()
    {
        if (this.field_110286_bQ == null)
        {
            this.setHorseTexturePaths();
        }

        return this.field_110286_bQ;
    }

    @SideOnly(Side.CLIENT)
    public String[] getVariantTexturePaths()
    {
        if (this.field_110286_bQ == null)
        {
            this.setHorseTexturePaths();
        }

        return this.field_110280_bR;
    }

    public void openGUI(EntityPlayer p_110199_1_)
    {
        if (!this.worldObj.isRemote && (this.riddenByEntity == null || this.riddenByEntity == p_110199_1_) && this.isTame())
        {
            this.horseChest.func_110133_a(this.getCommandSenderName());
            p_110199_1_.displayGUIHorse(this, this.horseChest);
        }
    }

    /**
     * Called when a player interacts with a mob. e.g. gets milk from a cow, gets into the saddle on a pig.
     */
    public boolean interact(EntityPlayer p_70085_1_)
    {
        ItemStack itemstack = p_70085_1_.inventory.getCurrentItem();

        if (itemstack != null && itemstack.getItem() == Items.spawn_egg)
        {
            return super.interact(p_70085_1_);
        }
        else if (!this.isTame() && this.func_110256_cu())
        {
            return false;
        }
        else if (this.isTame() && this.isAdultHorse() && p_70085_1_.isSneaking())
        {
            this.openGUI(p_70085_1_);
            return true;
        }
        else if (this.func_110253_bW() && this.riddenByEntity != null)
        {
            return super.interact(p_70085_1_);
        }
        else
        {
            if (itemstack != null)
            {
                boolean flag = false;

                if (this.func_110259_cr())
                {
                    byte b0 = -1;

                    if (itemstack.getItem() == Items.iron_horse_armor)
                    {
                        b0 = 1;
                    }
                    else if (itemstack.getItem() == Items.golden_horse_armor)
                    {
                        b0 = 2;
                    }
                    else if (itemstack.getItem() == Items.diamond_horse_armor)
                    {
                        b0 = 3;
                    }

                    if (b0 >= 0)
                    {
                        if (!this.isTame())
                        {
                            this.makeHorseRearWithSound();
                            return true;
                        }

                        this.openGUI(p_70085_1_);
                        return true;
                    }
                }

                if (!flag && !this.func_110256_cu())
                {
                    float f = 0.0F;
                    short short1 = 0;
                    byte b1 = 0;

                    if (itemstack.getItem() == Items.wheat)
                    {
                        f = 2.0F;
                        short1 = 60;
                        b1 = 3;
                    }
                    else if (itemstack.getItem() == Items.sugar)
                    {
                        f = 1.0F;
                        short1 = 30;
                        b1 = 3;
                    }
                    else if (itemstack.getItem() == Items.bread)
                    {
                        f = 7.0F;
                        short1 = 180;
                        b1 = 3;
                    }
                    else if (Block.getBlockFromItem(itemstack.getItem()) == Blocks.hay_block)
                    {
                        f = 20.0F;
                        short1 = 180;
                    }
                    else if (itemstack.getItem() == Items.apple)
                    {
                        f = 3.0F;
                        short1 = 60;
                        b1 = 3;
                    }
                    else if (itemstack.getItem() == Items.golden_carrot)
                    {
                        f = 4.0F;
                        short1 = 60;
                        b1 = 5;

                        if (this.isTame() && this.getGrowingAge() == 0)
                        {
                            flag = true;
                            this.func_146082_f(p_70085_1_);
                        }
                    }
                    else if (itemstack.getItem() == Items.golden_apple)
                    {
                        f = 10.0F;
                        short1 = 240;
                        b1 = 10;

                        if (this.isTame() && this.getGrowingAge() == 0)
                        {
                            flag = true;
                            this.func_146082_f(p_70085_1_);
                        }
                    }

                    if (this.getHealth() < this.getMaxHealth() && f > 0.0F)
                    {
                        this.heal(f);
                        flag = true;
                    }

                    if (!this.isAdultHorse() && short1 > 0)
                    {
                        this.addGrowth(short1);
                        flag = true;
                    }

                    if (b1 > 0 && (flag || !this.isTame()) && b1 < this.getMaxTemper())
                    {
                        flag = true;
                        this.increaseTemper(b1);
                    }

                    if (flag)
                    {
                        this.func_110266_cB();
                    }
                }

                if (!this.isTame() && !flag)
                {
                    if (itemstack != null && itemstack.interactWithEntity(p_70085_1_, this))
                    {
                        return true;
                    }

                    this.makeHorseRearWithSound();
                    return true;
                }

                if (!flag && this.func_110229_cs() && !this.isChested() && itemstack.getItem() == Item.getItemFromBlock(Blocks.chest))
                {
                    this.setChested(true);
                    this.playSound("mob.chickenplop", 1.0F, (this.rand.nextFloat() - this.rand.nextFloat()) * 0.2F + 1.0F);
                    flag = true;
                    this.func_110226_cD();
                }

                if (!flag && this.func_110253_bW() && !this.isHorseSaddled() && itemstack.getItem() == Items.saddle)
                {
                    this.openGUI(p_70085_1_);
                    return true;
                }

                if (flag)
                {
                    if (!p_70085_1_.capabilities.isCreativeMode && --itemstack.stackSize == 0)
                    {
                        p_70085_1_.inventory.setInventorySlotContents(p_70085_1_.inventory.currentItem, (ItemStack)null);
                    }

                    return true;
                }
            }

            if (this.func_110253_bW() && this.riddenByEntity == null)
            {
                if (itemstack != null && itemstack.interactWithEntity(p_70085_1_, this))
                {
                    return true;
                }
                else
                {
                    this.func_110237_h(p_70085_1_);
                    return true;
                }
            }
            else
            {
                return super.interact(p_70085_1_);
            }
        }
    }

    private void func_110237_h(EntityPlayer p_110237_1_)
    {
        p_110237_1_.rotationYaw = this.rotationYaw;
        p_110237_1_.rotationPitch = this.rotationPitch;
        this.setEatingHaystack(false);
        this.setRearing(false);

        if (!this.worldObj.isRemote)
        {
            p_110237_1_.mountEntity(this);
        }
    }

    public boolean func_110259_cr()
    {
        return this.getHorseType() == 0;
    }

    public boolean func_110229_cs()
    {
        int i = this.getHorseType();
        return i == 2 || i == 1;
    }

    /**
     * Dead and sleeping entities cannot move
     */
    protected boolean isMovementBlocked()
    {
        return this.riddenByEntity != null && this.isHorseSaddled() ? true : this.isEatingHaystack() || this.isRearing();
    }

    public boolean func_110256_cu()
    {
        int i = this.getHorseType();
        return i == 3 || i == 4;
    }

    public boolean func_110222_cv()
    {
        return this.func_110256_cu() || this.getHorseType() == 2;
    }

    /**
     * Checks if the parameter is an item which this animal can be fed to breed it (wheat, carrots or seeds depending on
     * the animal type)
     */
    public boolean isBreedingItem(ItemStack p_70877_1_)
    {
        return false;
    }

    private void func_110210_cH()
    {
        this.field_110278_bp = 1;
    }

    /**
     * Called when the mob's health reaches 0.
     */
    public void onDeath(DamageSource p_70645_1_)
    {
        super.onDeath(p_70645_1_);

        if (!this.worldObj.isRemote)
        {
            this.dropChestItems();
        }
    }

    /**
     * Called frequently so the entity can update its state every tick as required. For example, zombies and skeletons
     * use this to react to sunlight and start to burn.
     */
    public void onLivingUpdate()
    {
        if (this.rand.nextInt(200) == 0)
        {
            this.func_110210_cH();
        }

        super.onLivingUpdate();

        if (!this.worldObj.isRemote)
        {
            if (this.rand.nextInt(900) == 0 && this.deathTime == 0)
            {
                this.heal(1.0F);
            }

            if (!this.isEatingHaystack() && this.riddenByEntity == null && this.rand.nextInt(300) == 0 && this.worldObj.getBlock(MathHelper.floor_double(this.posX), MathHelper.floor_double(this.posY) - 1, MathHelper.floor_double(this.posZ)) == Blocks.grass)
            {
                this.setEatingHaystack(true);
            }

            if (this.isEatingHaystack() && ++this.eatingHaystackCounter > 50)
            {
                this.eatingHaystackCounter = 0;
                this.setEatingHaystack(false);
            }

            if (this.func_110205_ce() && !this.isAdultHorse() && !this.isEatingHaystack())
            {
                EntityHorse entityhorse = this.getClosestHorse(this, 16.0D);

                if (entityhorse != null && this.getDistanceSqToEntity(entityhorse) > 4.0D)
                {
                    PathEntity pathentity = this.worldObj.getPathEntityToEntity(this, entityhorse, 16.0F, true, false, false, true);
                    this.setPathToEntity(pathentity);
                }
            }
        }
    }

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        super.onUpdate();

        if (this.worldObj.isRemote && this.dataWatcher.hasChanges())
        {
            this.dataWatcher.func_111144_e();
            this.func_110230_cF();
        }

        if (this.openMouthCounter > 0 && ++this.openMouthCounter > 30)
        {
            this.openMouthCounter = 0;
            this.setHorseWatchableBoolean(128, false);
        }

        if (!this.worldObj.isRemote && this.jumpRearingCounter > 0 && ++this.jumpRearingCounter > 20)
        {
            this.jumpRearingCounter = 0;
            this.setRearing(false);
        }

        if (this.field_110278_bp > 0 && ++this.field_110278_bp > 8)
        {
            this.field_110278_bp = 0;
        }

        if (this.field_110279_bq > 0)
        {
            ++this.field_110279_bq;

            if (this.field_110279_bq > 300)
            {
                this.field_110279_bq = 0;
            }
        }

        this.prevHeadLean = this.headLean;

        if (this.isEatingHaystack())
        {
            this.headLean += (1.0F - this.headLean) * 0.4F + 0.05F;

            if (this.headLean > 1.0F)
            {
                this.headLean = 1.0F;
            }
        }
        else
        {
            this.headLean += (0.0F - this.headLean) * 0.4F - 0.05F;

            if (this.headLean < 0.0F)
            {
                this.headLean = 0.0F;
            }
        }

        this.prevRearingAmount = this.rearingAmount;

        if (this.isRearing())
        {
            this.prevHeadLean = this.headLean = 0.0F;
            this.rearingAmount += (1.0F - this.rearingAmount) * 0.4F + 0.05F;

            if (this.rearingAmount > 1.0F)
            {
                this.rearingAmount = 1.0F;
            }
        }
        else
        {
            this.field_110294_bI = false;
            this.rearingAmount += (0.8F * this.rearingAmount * this.rearingAmount * this.rearingAmount - this.rearingAmount) * 0.6F - 0.05F;

            if (this.rearingAmount < 0.0F)
            {
                this.rearingAmount = 0.0F;
            }
        }

        this.prevMouthOpenness = this.mouthOpenness;

        if (this.getHorseWatchableBoolean(128))
        {
            this.mouthOpenness += (1.0F - this.mouthOpenness) * 0.7F + 0.05F;

            if (this.mouthOpenness > 1.0F)
            {
                this.mouthOpenness = 1.0F;
            }
        }
        else
        {
            this.mouthOpenness += (0.0F - this.mouthOpenness) * 0.7F - 0.05F;

            if (this.mouthOpenness < 0.0F)
            {
                this.mouthOpenness = 0.0F;
            }
        }
    }

    private void openHorseMouth()
    {
        if (!this.worldObj.isRemote)
        {
            this.openMouthCounter = 1;
            this.setHorseWatchableBoolean(128, true);
        }
    }

    private boolean func_110200_cJ()
    {
        return this.riddenByEntity == null && this.ridingEntity == null && this.isTame() && this.isAdultHorse() && !this.func_110222_cv() && this.getHealth() >= this.getMaxHealth();
    }

    public void setEating(boolean p_70019_1_)
    {
        this.setHorseWatchableBoolean(32, p_70019_1_);
    }

    public void setEatingHaystack(boolean p_110227_1_)
    {
        this.setEating(p_110227_1_);
    }

    public void setRearing(boolean p_110219_1_)
    {
        if (p_110219_1_)
        {
            this.setEatingHaystack(false);
        }

        this.setHorseWatchableBoolean(64, p_110219_1_);
    }

    private void makeHorseRear()
    {
        if (!this.worldObj.isRemote)
        {
            this.jumpRearingCounter = 1;
            this.setRearing(true);
        }
    }

    public void makeHorseRearWithSound()
    {
        this.makeHorseRear();
        String s = this.getAngrySoundName();

        if (s != null)
        {
            this.playSound(s, this.getSoundVolume(), this.getSoundPitch());
        }
    }

    public void dropChestItems()
    {
        this.dropItemsInChest(this, this.horseChest);
        this.dropChests();
    }

    private void dropItemsInChest(Entity p_110240_1_, AnimalChest p_110240_2_)
    {
        if (p_110240_2_ != null && !this.worldObj.isRemote)
        {
            for (int i = 0; i < p_110240_2_.getSizeInventory(); ++i)
            {
                ItemStack itemstack = p_110240_2_.getStackInSlot(i);

                if (itemstack != null)
                {
                    this.entityDropItem(itemstack, 0.0F);
                }
            }
        }
    }

    public boolean setTamedBy(EntityPlayer p_110263_1_)
    {
        this.func_152120_b(p_110263_1_.getUniqueID().toString());
        this.setHorseTamed(true);
        return true;
    }

    /**
     * Moves the entity based on the specified heading.  Args: strafe, forward
     */
    public void moveEntityWithHeading(float p_70612_1_, float p_70612_2_)
    {
        if (this.riddenByEntity != null && this.riddenByEntity instanceof EntityLivingBase && this.isHorseSaddled())
        {
            this.prevRotationYaw = this.rotationYaw = this.riddenByEntity.rotationYaw;
            this.rotationPitch = this.riddenByEntity.rotationPitch * 0.5F;
            this.setRotation(this.rotationYaw, this.rotationPitch);
            this.rotationYawHead = this.renderYawOffset = this.rotationYaw;
            p_70612_1_ = ((EntityLivingBase)this.riddenByEntity).moveStrafing * 0.5F;
            p_70612_2_ = ((EntityLivingBase)this.riddenByEntity).moveForward;

            if (p_70612_2_ <= 0.0F)
            {
                p_70612_2_ *= 0.25F;
                this.field_110285_bP = 0;
            }

            if (this.onGround && this.jumpPower == 0.0F && this.isRearing() && !this.field_110294_bI)
            {
                p_70612_1_ = 0.0F;
                p_70612_2_ = 0.0F;
            }

            if (this.jumpPower > 0.0F && !this.isHorseJumping() && this.onGround)
            {
                this.motionY = this.getHorseJumpStrength() * (double)this.jumpPower;

                if (this.isPotionActive(Potion.jump))
                {
                    this.motionY += (double)((float)(this.getActivePotionEffect(Potion.jump).getAmplifier() + 1) * 0.1F);
                }

                this.setHorseJumping(true);
                this.isAirBorne = true;

                if (p_70612_2_ > 0.0F)
                {
                    float f2 = MathHelper.sin(this.rotationYaw * (float)Math.PI / 180.0F);
                    float f3 = MathHelper.cos(this.rotationYaw * (float)Math.PI / 180.0F);
                    this.motionX += (double)(-0.4F * f2 * this.jumpPower);
                    this.motionZ += (double)(0.4F * f3 * this.jumpPower);
                    this.playSound("mob.horse.jump", 0.4F, 1.0F);
                }

                this.jumpPower = 0.0F;
                net.minecraftforge.common.ForgeHooks.onLivingJump(this);
            }

            this.stepHeight = 1.0F;
            this.jumpMovementFactor = this.getAIMoveSpeed() * 0.1F;

            if (!this.worldObj.isRemote)
            {
                this.setAIMoveSpeed((float)this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).getAttributeValue());
                super.moveEntityWithHeading(p_70612_1_, p_70612_2_);
            }

            if (this.onGround)
            {
                this.jumpPower = 0.0F;
                this.setHorseJumping(false);
            }

            this.prevLimbSwingAmount = this.limbSwingAmount;
            double d1 = this.posX - this.prevPosX;
            double d0 = this.posZ - this.prevPosZ;
            float f4 = MathHelper.sqrt_double(d1 * d1 + d0 * d0) * 4.0F;

            if (f4 > 1.0F)
            {
                f4 = 1.0F;
            }

            this.limbSwingAmount += (f4 - this.limbSwingAmount) * 0.4F;
            this.limbSwing += this.limbSwingAmount;
        }
        else
        {
            this.stepHeight = 0.5F;
            this.jumpMovementFactor = 0.02F;
            super.moveEntityWithHeading(p_70612_1_, p_70612_2_);
        }
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        super.writeEntityToNBT(p_70014_1_);
        p_70014_1_.setBoolean("EatingHaystack", this.isEatingHaystack());
        p_70014_1_.setBoolean("ChestedHorse", this.isChested());
        p_70014_1_.setBoolean("HasReproduced", this.getHasReproduced());
        p_70014_1_.setBoolean("Bred", this.func_110205_ce());
        p_70014_1_.setInteger("Type", this.getHorseType());
        p_70014_1_.setInteger("Variant", this.getHorseVariant());
        p_70014_1_.setInteger("Temper", this.getTemper());
        p_70014_1_.setBoolean("Tame", this.isTame());
        p_70014_1_.setString("OwnerUUID", this.func_152119_ch());

        if (this.isChested())
        {
            NBTTagList nbttaglist = new NBTTagList();

            for (int i = 2; i < this.horseChest.getSizeInventory(); ++i)
            {
                ItemStack itemstack = this.horseChest.getStackInSlot(i);

                if (itemstack != null)
                {
                    NBTTagCompound nbttagcompound1 = new NBTTagCompound();
                    nbttagcompound1.setByte("Slot", (byte)i);
                    itemstack.writeToNBT(nbttagcompound1);
                    nbttaglist.appendTag(nbttagcompound1);
                }
            }

            p_70014_1_.setTag("Items", nbttaglist);
        }

        if (this.horseChest.getStackInSlot(1) != null)
        {
            p_70014_1_.setTag("ArmorItem", this.horseChest.getStackInSlot(1).writeToNBT(new NBTTagCompound()));
        }

        if (this.horseChest.getStackInSlot(0) != null)
        {
            p_70014_1_.setTag("SaddleItem", this.horseChest.getStackInSlot(0).writeToNBT(new NBTTagCompound()));
        }
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        super.readEntityFromNBT(p_70037_1_);
        this.setEatingHaystack(p_70037_1_.getBoolean("EatingHaystack"));
        this.func_110242_l(p_70037_1_.getBoolean("Bred"));
        this.setChested(p_70037_1_.getBoolean("ChestedHorse"));
        this.setHasReproduced(p_70037_1_.getBoolean("HasReproduced"));
        this.setHorseType(p_70037_1_.getInteger("Type"));
        this.setHorseVariant(p_70037_1_.getInteger("Variant"));
        this.setTemper(p_70037_1_.getInteger("Temper"));
        this.setHorseTamed(p_70037_1_.getBoolean("Tame"));

        if (p_70037_1_.hasKey("OwnerUUID", 8))
        {
            this.func_152120_b(p_70037_1_.getString("OwnerUUID"));
        }

        IAttributeInstance iattributeinstance = this.getAttributeMap().getAttributeInstanceByName("Speed");

        if (iattributeinstance != null)
        {
            this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(iattributeinstance.getBaseValue() * 0.25D);
        }

        if (this.isChested())
        {
            NBTTagList nbttaglist = p_70037_1_.getTagList("Items", 10);
            this.func_110226_cD();

            for (int i = 0; i < nbttaglist.tagCount(); ++i)
            {
                NBTTagCompound nbttagcompound1 = nbttaglist.getCompoundTagAt(i);
                int j = nbttagcompound1.getByte("Slot") & 255;

                if (j >= 2 && j < this.horseChest.getSizeInventory())
                {
                    this.horseChest.setInventorySlotContents(j, ItemStack.loadItemStackFromNBT(nbttagcompound1));
                }
            }
        }

        ItemStack itemstack;

        if (p_70037_1_.hasKey("ArmorItem", 10))
        {
            itemstack = ItemStack.loadItemStackFromNBT(p_70037_1_.getCompoundTag("ArmorItem"));

            if (itemstack != null && func_146085_a(itemstack.getItem()))
            {
                this.horseChest.setInventorySlotContents(1, itemstack);
            }
        }

        if (p_70037_1_.hasKey("SaddleItem", 10))
        {
            itemstack = ItemStack.loadItemStackFromNBT(p_70037_1_.getCompoundTag("SaddleItem"));

            if (itemstack != null && itemstack.getItem() == Items.saddle)
            {
                this.horseChest.setInventorySlotContents(0, itemstack);
            }
        }
        else if (p_70037_1_.getBoolean("Saddle"))
        {
            this.horseChest.setInventorySlotContents(0, new ItemStack(Items.saddle));
        }

        this.func_110232_cE();
    }

    /**
     * Returns true if the mob is currently able to mate with the specified mob.
     */
    public boolean canMateWith(EntityAnimal p_70878_1_)
    {
        if (p_70878_1_ == this)
        {
            return false;
        }
        else if (p_70878_1_.getClass() != this.getClass())
        {
            return false;
        }
        else
        {
            EntityHorse entityhorse = (EntityHorse)p_70878_1_;

            if (this.func_110200_cJ() && entityhorse.func_110200_cJ())
            {
                int i = this.getHorseType();
                int j = entityhorse.getHorseType();
                return i == j || i == 0 && j == 1 || i == 1 && j == 0;
            }
            else
            {
                return false;
            }
        }
    }

    public EntityAgeable createChild(EntityAgeable p_90011_1_)
    {
        EntityHorse entityhorse = (EntityHorse)p_90011_1_;
        EntityHorse entityhorse1 = new EntityHorse(this.worldObj);
        int i = this.getHorseType();
        int j = entityhorse.getHorseType();
        int k = 0;

        if (i == j)
        {
            k = i;
        }
        else if (i == 0 && j == 1 || i == 1 && j == 0)
        {
            k = 2;
        }

        if (k == 0)
        {
            int i1 = this.rand.nextInt(9);
            int l;

            if (i1 < 4)
            {
                l = this.getHorseVariant() & 255;
            }
            else if (i1 < 8)
            {
                l = entityhorse.getHorseVariant() & 255;
            }
            else
            {
                l = this.rand.nextInt(7);
            }

            int j1 = this.rand.nextInt(5);

            if (j1 < 2)
            {
                l |= this.getHorseVariant() & 65280;
            }
            else if (j1 < 4)
            {
                l |= entityhorse.getHorseVariant() & 65280;
            }
            else
            {
                l |= this.rand.nextInt(5) << 8 & 65280;
            }

            entityhorse1.setHorseVariant(l);
        }

        entityhorse1.setHorseType(k);
        double d1 = this.getEntityAttribute(SharedMonsterAttributes.maxHealth).getBaseValue() + p_90011_1_.getEntityAttribute(SharedMonsterAttributes.maxHealth).getBaseValue() + (double)this.func_110267_cL();
        entityhorse1.getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue(d1 / 3.0D);
        double d2 = this.getEntityAttribute(horseJumpStrength).getBaseValue() + p_90011_1_.getEntityAttribute(horseJumpStrength).getBaseValue() + this.func_110245_cM();
        entityhorse1.getEntityAttribute(horseJumpStrength).setBaseValue(d2 / 3.0D);
        double d0 = this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).getBaseValue() + p_90011_1_.getEntityAttribute(SharedMonsterAttributes.movementSpeed).getBaseValue() + this.func_110203_cN();
        entityhorse1.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(d0 / 3.0D);
        return entityhorse1;
    }

    public IEntityLivingData onSpawnWithEgg(IEntityLivingData p_110161_1_)
    {
        Object p_110161_1_1 = super.onSpawnWithEgg(p_110161_1_);
        boolean flag = false;
        int i = 0;
        int l;

        if (p_110161_1_1 instanceof EntityHorse.GroupData)
        {
            l = ((EntityHorse.GroupData)p_110161_1_1).field_111107_a;
            i = ((EntityHorse.GroupData)p_110161_1_1).field_111106_b & 255 | this.rand.nextInt(5) << 8;
        }
        else
        {
            if (this.rand.nextInt(10) == 0)
            {
                l = 1;
            }
            else
            {
                int j = this.rand.nextInt(7);
                int k = this.rand.nextInt(5);
                l = 0;
                i = j | k << 8;
            }

            p_110161_1_1 = new EntityHorse.GroupData(l, i);
        }

        this.setHorseType(l);
        this.setHorseVariant(i);

        if (this.rand.nextInt(5) == 0)
        {
            this.setGrowingAge(-24000);
        }

        if (l != 4 && l != 3)
        {
            this.getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue((double)this.func_110267_cL());

            if (l == 0)
            {
                this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(this.func_110203_cN());
            }
            else
            {
                this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(0.17499999701976776D);
            }
        }
        else
        {
            this.getEntityAttribute(SharedMonsterAttributes.maxHealth).setBaseValue(15.0D);
            this.getEntityAttribute(SharedMonsterAttributes.movementSpeed).setBaseValue(0.20000000298023224D);
        }

        if (l != 2 && l != 1)
        {
            this.getEntityAttribute(horseJumpStrength).setBaseValue(this.func_110245_cM());
        }
        else
        {
            this.getEntityAttribute(horseJumpStrength).setBaseValue(0.5D);
        }

        this.setHealth(this.getMaxHealth());
        return (IEntityLivingData)p_110161_1_1;
    }

    @SideOnly(Side.CLIENT)
    public float getGrassEatingAmount(float p_110258_1_)
    {
        return this.prevHeadLean + (this.headLean - this.prevHeadLean) * p_110258_1_;
    }

    @SideOnly(Side.CLIENT)
    public float getRearingAmount(float p_110223_1_)
    {
        return this.prevRearingAmount + (this.rearingAmount - this.prevRearingAmount) * p_110223_1_;
    }

    @SideOnly(Side.CLIENT)
    public float func_110201_q(float p_110201_1_)
    {
        return this.prevMouthOpenness + (this.mouthOpenness - this.prevMouthOpenness) * p_110201_1_;
    }

    /**
     * Returns true if the newer Entity AI code should be run
     */
    protected boolean isAIEnabled()
    {
        return true;
    }

    public void setJumpPower(int p_110206_1_)
    {
        if (this.isHorseSaddled())
        {
            if (p_110206_1_ < 0)
            {
                p_110206_1_ = 0;
            }
            else
            {
                this.field_110294_bI = true;
                this.makeHorseRear();
            }

            if (p_110206_1_ >= 90)
            {
                this.jumpPower = 1.0F;
            }
            else
            {
                this.jumpPower = 0.4F + 0.4F * (float)p_110206_1_ / 90.0F;
            }
        }
    }

    /**
     * "Spawns particles for the horse entity. par1 tells whether to spawn hearts. If it is false, it spawns smoke."
     */
    @SideOnly(Side.CLIENT)
    protected void spawnHorseParticles(boolean p_110216_1_)
    {
        String s = p_110216_1_ ? "heart" : "smoke";

        for (int i = 0; i < 7; ++i)
        {
            double d0 = this.rand.nextGaussian() * 0.02D;
            double d1 = this.rand.nextGaussian() * 0.02D;
            double d2 = this.rand.nextGaussian() * 0.02D;
            this.worldObj.spawnParticle(s, this.posX + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, this.posY + 0.5D + (double)(this.rand.nextFloat() * this.height), this.posZ + (double)(this.rand.nextFloat() * this.width * 2.0F) - (double)this.width, d0, d1, d2);
        }
    }

    @SideOnly(Side.CLIENT)
    public void handleHealthUpdate(byte p_70103_1_)
    {
        if (p_70103_1_ == 7)
        {
            this.spawnHorseParticles(true);
        }
        else if (p_70103_1_ == 6)
        {
            this.spawnHorseParticles(false);
        }
        else
        {
            super.handleHealthUpdate(p_70103_1_);
        }
    }

    public void updateRiderPosition()
    {
        super.updateRiderPosition();

        if (this.prevRearingAmount > 0.0F)
        {
            float f = MathHelper.sin(this.renderYawOffset * (float)Math.PI / 180.0F);
            float f1 = MathHelper.cos(this.renderYawOffset * (float)Math.PI / 180.0F);
            float f2 = 0.7F * this.prevRearingAmount;
            float f3 = 0.15F * this.prevRearingAmount;
            this.riddenByEntity.setPosition(this.posX + (double)(f2 * f), this.posY + this.getMountedYOffset() + this.riddenByEntity.getYOffset() + (double)f3, this.posZ - (double)(f2 * f1));

            if (this.riddenByEntity instanceof EntityLivingBase)
            {
                ((EntityLivingBase)this.riddenByEntity).renderYawOffset = this.renderYawOffset;
            }
        }
    }

    private float func_110267_cL()
    {
        return 15.0F + (float)this.rand.nextInt(8) + (float)this.rand.nextInt(9);
    }

    private double func_110245_cM()
    {
        return 0.4000000059604645D + this.rand.nextDouble() * 0.2D + this.rand.nextDouble() * 0.2D + this.rand.nextDouble() * 0.2D;
    }

    private double func_110203_cN()
    {
        return (0.44999998807907104D + this.rand.nextDouble() * 0.3D + this.rand.nextDouble() * 0.3D + this.rand.nextDouble() * 0.3D) * 0.25D;
    }

    public static boolean func_146085_a(Item p_146085_0_)
    {
        return p_146085_0_ == Items.iron_horse_armor || p_146085_0_ == Items.golden_horse_armor || p_146085_0_ == Items.diamond_horse_armor;
    }

    /**
     * returns true if this entity is by a ladder, false otherwise
     */
    public boolean isOnLadder()
    {
        return false;
    }

    public static class GroupData implements IEntityLivingData
        {
            public int field_111107_a;
            public int field_111106_b;
            private static final String __OBFID = "CL_00001643";

            public GroupData(int p_i1684_1_, int p_i1684_2_)
            {
                this.field_111107_a = p_i1684_1_;
                this.field_111106_b = p_i1684_2_;
            }
        }
}