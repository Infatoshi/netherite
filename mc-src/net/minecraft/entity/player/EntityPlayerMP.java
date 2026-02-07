package net.minecraft.entity.player;

import com.google.common.collect.Sets;
import com.mojang.authlib.GameProfile;
import io.netty.buffer.Unpooled;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.IMerchant;
import net.minecraft.entity.item.EntityMinecartHopper;
import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.ContainerBeacon;
import net.minecraft.inventory.ContainerBrewingStand;
import net.minecraft.inventory.ContainerChest;
import net.minecraft.inventory.ContainerDispenser;
import net.minecraft.inventory.ContainerEnchantment;
import net.minecraft.inventory.ContainerFurnace;
import net.minecraft.inventory.ContainerHopper;
import net.minecraft.inventory.ContainerHorseInventory;
import net.minecraft.inventory.ContainerMerchant;
import net.minecraft.inventory.ContainerRepair;
import net.minecraft.inventory.ContainerWorkbench;
import net.minecraft.inventory.ICrafting;
import net.minecraft.inventory.IInventory;
import net.minecraft.inventory.InventoryMerchant;
import net.minecraft.inventory.SlotCrafting;
import net.minecraft.item.EnumAction;
import net.minecraft.item.ItemMapBase;
import net.minecraft.item.ItemStack;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.network.NetHandlerPlayServer;
import net.minecraft.network.Packet;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.client.C15PacketClientSettings;
import net.minecraft.network.play.server.S02PacketChat;
import net.minecraft.network.play.server.S06PacketUpdateHealth;
import net.minecraft.network.play.server.S0APacketUseBed;
import net.minecraft.network.play.server.S0BPacketAnimation;
import net.minecraft.network.play.server.S13PacketDestroyEntities;
import net.minecraft.network.play.server.S19PacketEntityStatus;
import net.minecraft.network.play.server.S1BPacketEntityAttach;
import net.minecraft.network.play.server.S1DPacketEntityEffect;
import net.minecraft.network.play.server.S1EPacketRemoveEntityEffect;
import net.minecraft.network.play.server.S1FPacketSetExperience;
import net.minecraft.network.play.server.S26PacketMapChunkBulk;
import net.minecraft.network.play.server.S2BPacketChangeGameState;
import net.minecraft.network.play.server.S2DPacketOpenWindow;
import net.minecraft.network.play.server.S2EPacketCloseWindow;
import net.minecraft.network.play.server.S2FPacketSetSlot;
import net.minecraft.network.play.server.S30PacketWindowItems;
import net.minecraft.network.play.server.S31PacketWindowProperty;
import net.minecraft.network.play.server.S36PacketSignEditorOpen;
import net.minecraft.network.play.server.S39PacketPlayerAbilities;
import net.minecraft.network.play.server.S3FPacketCustomPayload;
import net.minecraft.potion.PotionEffect;
import net.minecraft.scoreboard.IScoreObjectiveCriteria;
import net.minecraft.scoreboard.Score;
import net.minecraft.scoreboard.ScoreObjective;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.management.ItemInWorldManager;
import net.minecraft.server.management.UserListOpsEntry;
import net.minecraft.stats.AchievementList;
import net.minecraft.stats.StatBase;
import net.minecraft.stats.StatList;
import net.minecraft.stats.StatisticsFile;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.tileentity.TileEntityBeacon;
import net.minecraft.tileentity.TileEntityBrewingStand;
import net.minecraft.tileentity.TileEntityDispenser;
import net.minecraft.tileentity.TileEntityDropper;
import net.minecraft.tileentity.TileEntityFurnace;
import net.minecraft.tileentity.TileEntityHopper;
import net.minecraft.tileentity.TileEntitySign;
import net.minecraft.util.ChunkCoordinates;
import net.minecraft.util.DamageSource;
import net.minecraft.util.EntityDamageSource;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.JsonSerializableSet;
import net.minecraft.util.MathHelper;
import net.minecraft.util.ReportedException;
import net.minecraft.village.MerchantRecipeList;
import net.minecraft.world.ChunkCoordIntPair;
import net.minecraft.world.WorldServer;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.biome.BiomeGenBase;
import net.minecraft.world.chunk.Chunk;
import org.apache.commons.io.Charsets;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import net.minecraft.entity.item.EntityItem;
import net.minecraftforge.common.ForgeHooks;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.event.entity.player.PlayerDropsEvent;
import net.minecraftforge.event.world.ChunkWatchEvent;

public class EntityPlayerMP extends EntityPlayer implements ICrafting
{
    private static final Logger logger = LogManager.getLogger();
    private String translator = "en_US";
    /** The NetServerHandler assigned to this player by the ServerConfigurationManager. */
    public NetHandlerPlayServer playerNetServerHandler;
    /** Reference to the MinecraftServer object. */
    public final MinecraftServer mcServer;
    /** The ItemInWorldManager belonging to this player */
    public final ItemInWorldManager theItemInWorldManager;
    /** player X position as seen by PlayerManager */
    public double managedPosX;
    /** player Z position as seen by PlayerManager */
    public double managedPosZ;
    /** LinkedList that holds the loaded chunks. */
    public final List loadedChunks = new LinkedList();
    /** entities added to this list will  be packet29'd to the player */
    private final List destroyedItemsNetCache = new LinkedList();
    private final StatisticsFile field_147103_bO;
    private float field_130068_bO = Float.MIN_VALUE;
    /** amount of health the client was last set to */
    private float lastHealth = -1.0E8F;
    /** set to foodStats.GetFoodLevel */
    private int lastFoodLevel = -99999999;
    /** set to foodStats.getSaturationLevel() == 0.0F each tick */
    private boolean wasHungry = true;
    /** Amount of experience the client was last set to */
    private int lastExperience = -99999999;
    private int field_147101_bU = 60;
    private EntityPlayer.EnumChatVisibility chatVisibility;
    private boolean chatColours = true;
    private long field_143005_bX = System.currentTimeMillis();
    /** The currently in use window ID. Incremented every time a window is opened. */
    public int currentWindowId;
    /**
     * set to true when player is moving quantity of items from one inventory to another(crafting) but item in either
     * slot is not changed
     */
    public boolean isChangingQuantityOnly;
    public int ping;
    /**
     * Set when a player beats the ender dragon, used to respawn the player at the spawn point while retaining inventory
     * and XP
     */
    public boolean playerConqueredTheEnd;
    private static final String __OBFID = "CL_00001440";

    public EntityPlayerMP(MinecraftServer p_i45285_1_, WorldServer p_i45285_2_, GameProfile p_i45285_3_, ItemInWorldManager p_i45285_4_)
    {
        super(p_i45285_2_, p_i45285_3_);
        p_i45285_4_.thisPlayerMP = this;
        this.theItemInWorldManager = p_i45285_4_;
        ChunkCoordinates chunkcoordinates = p_i45285_2_.provider.getRandomizedSpawnPoint();
        int i = chunkcoordinates.posX;
        int j = chunkcoordinates.posZ;
        int k = chunkcoordinates.posY;

        this.mcServer = p_i45285_1_;
        this.field_147103_bO = p_i45285_1_.getConfigurationManager().func_152602_a(this);
        this.stepHeight = 0.0F;
        this.yOffset = 0.0F;
        this.setLocationAndAngles((double)i + 0.5D, (double)k, (double)j + 0.5D, 0.0F, 0.0F);

        while (!p_i45285_2_.getCollidingBoundingBoxes(this, this.boundingBox).isEmpty())
        {
            this.setPosition(this.posX, this.posY + 1.0D, this.posZ);
        }
    }

    /**
     * (abstract) Protected helper method to read subclass entity data from NBT.
     */
    public void readEntityFromNBT(NBTTagCompound p_70037_1_)
    {
        super.readEntityFromNBT(p_70037_1_);

        if (p_70037_1_.hasKey("playerGameType", 99))
        {
            if (MinecraftServer.getServer().getForceGamemode())
            {
                this.theItemInWorldManager.setGameType(MinecraftServer.getServer().getGameType());
            }
            else
            {
                this.theItemInWorldManager.setGameType(WorldSettings.GameType.getByID(p_70037_1_.getInteger("playerGameType")));
            }
        }
    }

    /**
     * (abstract) Protected helper method to write subclass entity data to NBT.
     */
    public void writeEntityToNBT(NBTTagCompound p_70014_1_)
    {
        super.writeEntityToNBT(p_70014_1_);
        p_70014_1_.setInteger("playerGameType", this.theItemInWorldManager.getGameType().getID());
    }

    /**
     * Add experience levels to this player.
     */
    public void addExperienceLevel(int p_82242_1_)
    {
        super.addExperienceLevel(p_82242_1_);
        this.lastExperience = -1;
    }

    public void addSelfToInternalCraftingInventory()
    {
        this.openContainer.addCraftingToCrafters(this);
    }

    /**
     * sets the players height back to normal after doing things like sleeping and dieing
     */
    protected void resetHeight()
    {
        this.yOffset = 0.0F;
    }

    public float getEyeHeight()
    {
        return super.getEyeHeight();
    }

    /**
     * Called to update the entity's position/logic.
     */
    public void onUpdate()
    {
        this.theItemInWorldManager.updateBlockRemoving();
        --this.field_147101_bU;

        if (this.hurtResistantTime > 0)
        {
            --this.hurtResistantTime;
        }

        this.openContainer.detectAndSendChanges();

        if (!this.worldObj.isRemote && !ForgeHooks.canInteractWith(this, this.openContainer))
        {
            this.closeScreen();
            this.openContainer = this.inventoryContainer;
        }

        while (!this.destroyedItemsNetCache.isEmpty())
        {
            int i = Math.min(this.destroyedItemsNetCache.size(), 127);
            int[] aint = new int[i];
            Iterator iterator = this.destroyedItemsNetCache.iterator();
            int j = 0;

            while (iterator.hasNext() && j < i)
            {
                aint[j++] = ((Integer)iterator.next()).intValue();
                iterator.remove();
            }

            this.playerNetServerHandler.sendPacket(new S13PacketDestroyEntities(aint));
        }

        if (!this.loadedChunks.isEmpty())
        {
            ArrayList arraylist = new ArrayList();
            Iterator iterator1 = this.loadedChunks.iterator();
            ArrayList arraylist1 = new ArrayList();
            Chunk chunk;

            while (iterator1.hasNext() && arraylist.size() < S26PacketMapChunkBulk.func_149258_c())
            {
                ChunkCoordIntPair chunkcoordintpair = (ChunkCoordIntPair)iterator1.next();

                if (chunkcoordintpair != null)
                {
                    if (this.worldObj.blockExists(chunkcoordintpair.chunkXPos << 4, 0, chunkcoordintpair.chunkZPos << 4))
                    {
                        chunk = this.worldObj.getChunkFromChunkCoords(chunkcoordintpair.chunkXPos, chunkcoordintpair.chunkZPos);

                        if (chunk.func_150802_k())
                        {
                            arraylist.add(chunk);
                            arraylist1.addAll(((WorldServer)this.worldObj).func_147486_a(chunkcoordintpair.chunkXPos * 16, 0, chunkcoordintpair.chunkZPos * 16, chunkcoordintpair.chunkXPos * 16 + 15, 256, chunkcoordintpair.chunkZPos * 16 + 15));
                            //BugFix: 16 makes it load an extra chunk, which isn't associated with a player, which makes it not unload unless a player walks near it.
                            iterator1.remove();
                        }
                    }
                }
                else
                {
                    iterator1.remove();
                }
            }

            if (!arraylist.isEmpty())
            {
                this.playerNetServerHandler.sendPacket(new S26PacketMapChunkBulk(arraylist));
                Iterator iterator2 = arraylist1.iterator();

                while (iterator2.hasNext())
                {
                    TileEntity tileentity = (TileEntity)iterator2.next();
                    this.func_147097_b(tileentity);
                }

                iterator2 = arraylist.iterator();

                while (iterator2.hasNext())
                {
                    chunk = (Chunk)iterator2.next();
                    this.getServerForPlayer().getEntityTracker().func_85172_a(this, chunk);
                    MinecraftForge.EVENT_BUS.post(new ChunkWatchEvent.Watch(chunk.getChunkCoordIntPair(), this));
                }
            }
        }
    }

    public void onUpdateEntity()
    {
        try
        {
            super.onUpdate();

            for (int i = 0; i < this.inventory.getSizeInventory(); ++i)
            {
                ItemStack itemstack = this.inventory.getStackInSlot(i);

                if (itemstack != null && itemstack.getItem().isMap())
                {
                    Packet packet = ((ItemMapBase)itemstack.getItem()).func_150911_c(itemstack, this.worldObj, this);

                    if (packet != null)
                    {
                        this.playerNetServerHandler.sendPacket(packet);
                    }
                }
            }

            if (this.getHealth() != this.lastHealth || this.lastFoodLevel != this.foodStats.getFoodLevel() || this.foodStats.getSaturationLevel() == 0.0F != this.wasHungry)
            {
                this.playerNetServerHandler.sendPacket(new S06PacketUpdateHealth(this.getHealth(), this.foodStats.getFoodLevel(), this.foodStats.getSaturationLevel()));
                this.lastHealth = this.getHealth();
                this.lastFoodLevel = this.foodStats.getFoodLevel();
                this.wasHungry = this.foodStats.getSaturationLevel() == 0.0F;
            }

            if (this.getHealth() + this.getAbsorptionAmount() != this.field_130068_bO)
            {
                this.field_130068_bO = this.getHealth() + this.getAbsorptionAmount();
                Collection collection = this.getWorldScoreboard().func_96520_a(IScoreObjectiveCriteria.health);
                Iterator iterator = collection.iterator();

                while (iterator.hasNext())
                {
                    ScoreObjective scoreobjective = (ScoreObjective)iterator.next();
                    this.getWorldScoreboard().func_96529_a(this.getCommandSenderName(), scoreobjective).func_96651_a(Arrays.asList(new EntityPlayer[] {this}));
                }
            }

            if (this.experienceTotal != this.lastExperience)
            {
                this.lastExperience = this.experienceTotal;
                this.playerNetServerHandler.sendPacket(new S1FPacketSetExperience(this.experience, this.experienceTotal, this.experienceLevel));
            }

            if (this.ticksExisted % 20 * 5 == 0 && !this.func_147099_x().hasAchievementUnlocked(AchievementList.field_150961_L))
            {
                this.func_147098_j();
            }
        }
        catch (Throwable throwable)
        {
            CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Ticking player");
            CrashReportCategory crashreportcategory = crashreport.makeCategory("Player being ticked");
            this.addEntityCrashInfo(crashreportcategory);
            throw new ReportedException(crashreport);
        }
    }

    protected void func_147098_j()
    {
        BiomeGenBase biomegenbase = this.worldObj.getBiomeGenForCoords(MathHelper.floor_double(this.posX), MathHelper.floor_double(this.posZ));

        if (biomegenbase != null)
        {
            String s = biomegenbase.biomeName;
            JsonSerializableSet jsonserializableset = (JsonSerializableSet)this.func_147099_x().func_150870_b(AchievementList.field_150961_L);

            if (jsonserializableset == null)
            {
                jsonserializableset = (JsonSerializableSet)this.func_147099_x().func_150872_a(AchievementList.field_150961_L, new JsonSerializableSet());
            }

            jsonserializableset.add(s);

            if (this.func_147099_x().canUnlockAchievement(AchievementList.field_150961_L) && jsonserializableset.size() == BiomeGenBase.explorationBiomesList.size())
            {
                HashSet hashset = Sets.newHashSet(BiomeGenBase.explorationBiomesList);
                Iterator iterator = jsonserializableset.iterator();

                while (iterator.hasNext())
                {
                    String s1 = (String)iterator.next();
                    Iterator iterator1 = hashset.iterator();

                    while (iterator1.hasNext())
                    {
                        BiomeGenBase biomegenbase1 = (BiomeGenBase)iterator1.next();

                        if (biomegenbase1.biomeName.equals(s1))
                        {
                            iterator1.remove();
                        }
                    }

                    if (hashset.isEmpty())
                    {
                        break;
                    }
                }

                if (hashset.isEmpty())
                {
                    this.triggerAchievement(AchievementList.field_150961_L);
                }
            }
        }
    }

    /**
     * Called when the mob's health reaches 0.
     */
    public void onDeath(DamageSource p_70645_1_)
    {
        if (ForgeHooks.onLivingDeath(this, p_70645_1_)) return;
        this.mcServer.getConfigurationManager().sendChatMsg(this.func_110142_aN().func_151521_b());

        if (!this.worldObj.getGameRules().getGameRuleBooleanValue("keepInventory"))
        {
            captureDrops = true;
            capturedDrops.clear();

            this.inventory.dropAllItems();

            captureDrops = false;
            PlayerDropsEvent event = new PlayerDropsEvent(this, p_70645_1_, capturedDrops, recentlyHit > 0);
            if (!MinecraftForge.EVENT_BUS.post(event))
            {
                for (EntityItem item : capturedDrops)
                {
                    joinEntityItemWithWorld(item);
                }
            }
        }

        Collection collection = this.worldObj.getScoreboard().func_96520_a(IScoreObjectiveCriteria.deathCount);
        Iterator iterator = collection.iterator();

        while (iterator.hasNext())
        {
            ScoreObjective scoreobjective = (ScoreObjective)iterator.next();
            Score score = this.getWorldScoreboard().func_96529_a(this.getCommandSenderName(), scoreobjective);
            score.func_96648_a();
        }

        EntityLivingBase entitylivingbase = this.func_94060_bK();

        if (entitylivingbase != null)
        {
            int i = EntityList.getEntityID(entitylivingbase);
            EntityList.EntityEggInfo entityegginfo = (EntityList.EntityEggInfo)EntityList.entityEggs.get(Integer.valueOf(i));

            if (entityegginfo != null)
            {
                this.addStat(entityegginfo.field_151513_e, 1);
            }

            entitylivingbase.addToPlayerScore(this, this.scoreValue);
        }

        this.addStat(StatList.deathsStat, 1);
        this.func_110142_aN().func_94549_h();
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
            boolean flag = this.mcServer.isDedicatedServer() && this.mcServer.isPVPEnabled() && "fall".equals(p_70097_1_.damageType);

            if (!flag && this.field_147101_bU > 0 && p_70097_1_ != DamageSource.outOfWorld)
            {
                return false;
            }
            else
            {
                if (p_70097_1_ instanceof EntityDamageSource)
                {
                    Entity entity = p_70097_1_.getEntity();

                    if (entity instanceof EntityPlayer && !this.canAttackPlayer((EntityPlayer)entity))
                    {
                        return false;
                    }

                    if (entity instanceof EntityArrow)
                    {
                        EntityArrow entityarrow = (EntityArrow)entity;

                        if (entityarrow.shootingEntity instanceof EntityPlayer && !this.canAttackPlayer((EntityPlayer)entityarrow.shootingEntity))
                        {
                            return false;
                        }
                    }
                }

                return super.attackEntityFrom(p_70097_1_, p_70097_2_);
            }
        }
    }

    public boolean canAttackPlayer(EntityPlayer p_96122_1_)
    {
        return !this.mcServer.isPVPEnabled() ? false : super.canAttackPlayer(p_96122_1_);
    }

    /**
     * Teleports the entity to another dimension. Params: Dimension number to teleport to
     */
    public void travelToDimension(int p_71027_1_)
    {
        if (this.dimension == 1 && p_71027_1_ == 1)
        {
            this.triggerAchievement(AchievementList.theEnd2);
            this.worldObj.removeEntity(this);
            this.playerConqueredTheEnd = true;
            this.playerNetServerHandler.sendPacket(new S2BPacketChangeGameState(4, 0.0F));
        }
        else
        {
            if (this.dimension == 0 && p_71027_1_ == 1)
            {
                this.triggerAchievement(AchievementList.theEnd);
                ChunkCoordinates chunkcoordinates = this.mcServer.worldServerForDimension(p_71027_1_).getEntrancePortalLocation();

                if (chunkcoordinates != null)
                {
                    this.playerNetServerHandler.setPlayerLocation((double)chunkcoordinates.posX, (double)chunkcoordinates.posY, (double)chunkcoordinates.posZ, 0.0F, 0.0F);
                }

                p_71027_1_ = 1;
            }
            else
            {
                this.triggerAchievement(AchievementList.portal);
            }

            this.mcServer.getConfigurationManager().transferPlayerToDimension(this, p_71027_1_);
            this.lastExperience = -1;
            this.lastHealth = -1.0F;
            this.lastFoodLevel = -1;
        }
    }

    private void func_147097_b(TileEntity p_147097_1_)
    {
        if (p_147097_1_ != null)
        {
            Packet packet = p_147097_1_.getDescriptionPacket();

            if (packet != null)
            {
                this.playerNetServerHandler.sendPacket(packet);
            }
        }
    }

    /**
     * Called whenever an item is picked up from walking over it. Args: pickedUpEntity, stackSize
     */
    public void onItemPickup(Entity p_71001_1_, int p_71001_2_)
    {
        super.onItemPickup(p_71001_1_, p_71001_2_);
        this.openContainer.detectAndSendChanges();
    }

    /**
     * puts player to sleep on specified bed if possible
     */
    public EntityPlayer.EnumStatus sleepInBedAt(int p_71018_1_, int p_71018_2_, int p_71018_3_)
    {
        EntityPlayer.EnumStatus enumstatus = super.sleepInBedAt(p_71018_1_, p_71018_2_, p_71018_3_);

        if (enumstatus == EntityPlayer.EnumStatus.OK)
        {
            S0APacketUseBed s0apacketusebed = new S0APacketUseBed(this, p_71018_1_, p_71018_2_, p_71018_3_);
            this.getServerForPlayer().getEntityTracker().func_151247_a(this, s0apacketusebed);
            this.playerNetServerHandler.setPlayerLocation(this.posX, this.posY, this.posZ, this.rotationYaw, this.rotationPitch);
            this.playerNetServerHandler.sendPacket(s0apacketusebed);
        }

        return enumstatus;
    }

    /**
     * Wake up the player if they're sleeping.
     */
    public void wakeUpPlayer(boolean p_70999_1_, boolean p_70999_2_, boolean p_70999_3_)
    {
        if (this.isPlayerSleeping())
        {
            this.getServerForPlayer().getEntityTracker().func_151248_b(this, new S0BPacketAnimation(this, 2));
        }

        super.wakeUpPlayer(p_70999_1_, p_70999_2_, p_70999_3_);

        if (this.playerNetServerHandler != null)
        {
            this.playerNetServerHandler.setPlayerLocation(this.posX, this.posY, this.posZ, this.rotationYaw, this.rotationPitch);
        }
    }

    /**
     * Called when a player mounts an entity. e.g. mounts a pig, mounts a boat.
     */
    public void mountEntity(Entity p_70078_1_)
    {
        super.mountEntity(p_70078_1_);
        this.playerNetServerHandler.sendPacket(new S1BPacketEntityAttach(0, this, this.ridingEntity));
        this.playerNetServerHandler.setPlayerLocation(this.posX, this.posY, this.posZ, this.rotationYaw, this.rotationPitch);
    }

    /**
     * Takes in the distance the entity has fallen this tick and whether its on the ground to update the fall distance
     * and deal fall damage if landing on the ground.  Args: distanceFallenThisTick, onGround
     */
    protected void updateFallState(double p_70064_1_, boolean p_70064_3_) {}

    /**
     * process player falling based on movement packet
     */
    public void handleFalling(double p_71122_1_, boolean p_71122_3_)
    {
        super.updateFallState(p_71122_1_, p_71122_3_);
    }

    public void func_146100_a(TileEntity p_146100_1_)
    {
        if (p_146100_1_ instanceof TileEntitySign)
        {
            ((TileEntitySign)p_146100_1_).func_145912_a(this);
            this.playerNetServerHandler.sendPacket(new S36PacketSignEditorOpen(p_146100_1_.xCoord, p_146100_1_.yCoord, p_146100_1_.zCoord));
        }
    }

    /**
     * get the next window id to use
     */
    public void getNextWindowId()
    {
        this.currentWindowId = this.currentWindowId % 100 + 1;
    }

    /**
     * Displays the crafting GUI for a workbench.
     */
    public void displayGUIWorkbench(int p_71058_1_, int p_71058_2_, int p_71058_3_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 1, "Crafting", 9, true));
        this.openContainer = new ContainerWorkbench(this.inventory, this.worldObj, p_71058_1_, p_71058_2_, p_71058_3_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    public void displayGUIEnchantment(int p_71002_1_, int p_71002_2_, int p_71002_3_, String p_71002_4_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 4, p_71002_4_ == null ? "" : p_71002_4_, 9, p_71002_4_ != null));
        this.openContainer = new ContainerEnchantment(this.inventory, this.worldObj, p_71002_1_, p_71002_2_, p_71002_3_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    /**
     * Displays the GUI for interacting with an anvil.
     */
    public void displayGUIAnvil(int p_82244_1_, int p_82244_2_, int p_82244_3_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 8, "Repairing", 9, true));
        this.openContainer = new ContainerRepair(this.inventory, this.worldObj, p_82244_1_, p_82244_2_, p_82244_3_, this);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    /**
     * Displays the GUI for interacting with a chest inventory. Args: chestInventory
     */
    public void displayGUIChest(IInventory p_71007_1_)
    {
        if (this.openContainer != this.inventoryContainer)
        {
            this.closeScreen();
        }

        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 0, p_71007_1_.getInventoryName(), p_71007_1_.getSizeInventory(), p_71007_1_.hasCustomInventoryName()));
        this.openContainer = new ContainerChest(this.inventory, p_71007_1_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    public void func_146093_a(TileEntityHopper p_146093_1_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 9, p_146093_1_.getInventoryName(), p_146093_1_.getSizeInventory(), p_146093_1_.hasCustomInventoryName()));
        this.openContainer = new ContainerHopper(this.inventory, p_146093_1_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    public void displayGUIHopperMinecart(EntityMinecartHopper p_96125_1_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 9, p_96125_1_.getInventoryName(), p_96125_1_.getSizeInventory(), p_96125_1_.hasCustomInventoryName()));
        this.openContainer = new ContainerHopper(this.inventory, p_96125_1_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    public void func_146101_a(TileEntityFurnace p_146101_1_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 2, p_146101_1_.getInventoryName(), p_146101_1_.getSizeInventory(), p_146101_1_.hasCustomInventoryName()));
        this.openContainer = new ContainerFurnace(this.inventory, p_146101_1_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    public void func_146102_a(TileEntityDispenser p_146102_1_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, p_146102_1_ instanceof TileEntityDropper ? 10 : 3, p_146102_1_.getInventoryName(), p_146102_1_.getSizeInventory(), p_146102_1_.hasCustomInventoryName()));
        this.openContainer = new ContainerDispenser(this.inventory, p_146102_1_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    public void func_146098_a(TileEntityBrewingStand p_146098_1_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 5, p_146098_1_.getInventoryName(), p_146098_1_.getSizeInventory(), p_146098_1_.hasCustomInventoryName()));
        this.openContainer = new ContainerBrewingStand(this.inventory, p_146098_1_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    public void func_146104_a(TileEntityBeacon p_146104_1_)
    {
        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 7, p_146104_1_.getInventoryName(), p_146104_1_.getSizeInventory(), p_146104_1_.hasCustomInventoryName()));
        this.openContainer = new ContainerBeacon(this.inventory, p_146104_1_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    public void displayGUIMerchant(IMerchant p_71030_1_, String p_71030_2_)
    {
        this.getNextWindowId();
        this.openContainer = new ContainerMerchant(this.inventory, p_71030_1_, this.worldObj);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
        InventoryMerchant inventorymerchant = ((ContainerMerchant)this.openContainer).getMerchantInventory();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 6, p_71030_2_ == null ? "" : p_71030_2_, inventorymerchant.getSizeInventory(), p_71030_2_ != null));
        MerchantRecipeList merchantrecipelist = p_71030_1_.getRecipes(this);

        if (merchantrecipelist != null)
        {
            PacketBuffer packetbuffer = new PacketBuffer(Unpooled.buffer());

            try
            {
                packetbuffer.writeInt(this.currentWindowId);
                merchantrecipelist.func_151391_a(packetbuffer);
                this.playerNetServerHandler.sendPacket(new S3FPacketCustomPayload("MC|TrList", packetbuffer));
            }
            catch (IOException ioexception)
            {
                logger.error("Couldn\'t send trade list", ioexception);
            }
            finally
            {
                packetbuffer.release();
            }
        }
    }

    public void displayGUIHorse(EntityHorse p_110298_1_, IInventory p_110298_2_)
    {
        if (this.openContainer != this.inventoryContainer)
        {
            this.closeScreen();
        }

        this.getNextWindowId();
        this.playerNetServerHandler.sendPacket(new S2DPacketOpenWindow(this.currentWindowId, 11, p_110298_2_.getInventoryName(), p_110298_2_.getSizeInventory(), p_110298_2_.hasCustomInventoryName(), p_110298_1_.getEntityId()));
        this.openContainer = new ContainerHorseInventory(this.inventory, p_110298_2_, p_110298_1_);
        this.openContainer.windowId = this.currentWindowId;
        this.openContainer.addCraftingToCrafters(this);
    }

    /**
     * Sends the contents of an inventory slot to the client-side Container. This doesn't have to match the actual
     * contents of that slot. Args: Container, slot number, slot contents
     */
    public void sendSlotContents(Container p_71111_1_, int p_71111_2_, ItemStack p_71111_3_)
    {
        if (!(p_71111_1_.getSlot(p_71111_2_) instanceof SlotCrafting))
        {
            if (!this.isChangingQuantityOnly)
            {
                this.playerNetServerHandler.sendPacket(new S2FPacketSetSlot(p_71111_1_.windowId, p_71111_2_, p_71111_3_));
            }
        }
    }

    public void sendContainerToPlayer(Container p_71120_1_)
    {
        this.sendContainerAndContentsToPlayer(p_71120_1_, p_71120_1_.getInventory());
    }

    public void sendContainerAndContentsToPlayer(Container p_71110_1_, List p_71110_2_)
    {
        this.playerNetServerHandler.sendPacket(new S30PacketWindowItems(p_71110_1_.windowId, p_71110_2_));
        this.playerNetServerHandler.sendPacket(new S2FPacketSetSlot(-1, -1, this.inventory.getItemStack()));
    }

    /**
     * Sends two ints to the client-side Container. Used for furnace burning time, smelting progress, brewing progress,
     * and enchanting level. Normally the first int identifies which variable to update, and the second contains the new
     * value. Both are truncated to shorts in non-local SMP.
     */
    public void sendProgressBarUpdate(Container p_71112_1_, int p_71112_2_, int p_71112_3_)
    {
        this.playerNetServerHandler.sendPacket(new S31PacketWindowProperty(p_71112_1_.windowId, p_71112_2_, p_71112_3_));
    }

    /**
     * sets current screen to null (used on escape buttons of GUIs); sets current crafting inventory back to the 2x2
     * square
     */
    public void closeScreen()
    {
        this.playerNetServerHandler.sendPacket(new S2EPacketCloseWindow(this.openContainer.windowId));
        this.closeContainer();
    }

    /**
     * updates item held by mouse
     */
    public void updateHeldItem()
    {
        if (!this.isChangingQuantityOnly)
        {
            this.playerNetServerHandler.sendPacket(new S2FPacketSetSlot(-1, -1, this.inventory.getItemStack()));
        }
    }

    /**
     * Closes the container the player currently has open.
     */
    public void closeContainer()
    {
        this.openContainer.onContainerClosed(this);
        this.openContainer = this.inventoryContainer;
    }

    public void setEntityActionState(float p_110430_1_, float p_110430_2_, boolean p_110430_3_, boolean p_110430_4_)
    {
        if (this.ridingEntity != null)
        {
            if (p_110430_1_ >= -1.0F && p_110430_1_ <= 1.0F)
            {
                this.moveStrafing = p_110430_1_;
            }

            if (p_110430_2_ >= -1.0F && p_110430_2_ <= 1.0F)
            {
                this.moveForward = p_110430_2_;
            }

            this.isJumping = p_110430_3_;
            this.setSneaking(p_110430_4_);
        }
    }

    /**
     * Adds a value to a statistic field.
     */
    public void addStat(StatBase p_71064_1_, int p_71064_2_)
    {
        if (p_71064_1_ != null)
        {
            if (p_71064_1_.isAchievement() && MinecraftForge.EVENT_BUS.post(new net.minecraftforge.event.entity.player.AchievementEvent(this, (net.minecraft.stats.Achievement) p_71064_1_))) return;
            this.field_147103_bO.func_150871_b(this, p_71064_1_, p_71064_2_);
            Iterator iterator = this.getWorldScoreboard().func_96520_a(p_71064_1_.func_150952_k()).iterator();

            while (iterator.hasNext())
            {
                ScoreObjective scoreobjective = (ScoreObjective)iterator.next();
                this.getWorldScoreboard().func_96529_a(this.getCommandSenderName(), scoreobjective).func_96648_a();
            }

            if (this.field_147103_bO.func_150879_e())
            {
                this.field_147103_bO.func_150876_a(this);
            }
        }
    }

    public void mountEntityAndWakeUp()
    {
        if (this.riddenByEntity != null)
        {
            this.riddenByEntity.mountEntity(this);
        }

        if (this.sleeping)
        {
            this.wakeUpPlayer(true, false, false);
        }
    }

    /**
     * this function is called when a players inventory is sent to him, lastHealth is updated on any dimension
     * transitions, then reset.
     */
    public void setPlayerHealthUpdated()
    {
        this.lastHealth = -1.0E8F;
    }

    public void addChatComponentMessage(IChatComponent p_146105_1_)
    {
        this.playerNetServerHandler.sendPacket(new S02PacketChat(p_146105_1_));
    }

    /**
     * Used for when item use count runs out, ie: eating completed
     */
    protected void onItemUseFinish()
    {
        this.playerNetServerHandler.sendPacket(new S19PacketEntityStatus(this, (byte)9));
        super.onItemUseFinish();
    }

    /**
     * sets the itemInUse when the use item button is clicked. Args: itemstack, int maxItemUseDuration
     */
    public void setItemInUse(ItemStack p_71008_1_, int p_71008_2_)
    {
        super.setItemInUse(p_71008_1_, p_71008_2_);

        if (p_71008_1_ != null && p_71008_1_.getItem() != null && p_71008_1_.getItem().getItemUseAction(p_71008_1_) == EnumAction.eat)
        {
            this.getServerForPlayer().getEntityTracker().func_151248_b(this, new S0BPacketAnimation(this, 3));
        }
    }

    /**
     * Copies the values from the given player into this player if boolean par2 is true. Always clones Ender Chest
     * Inventory.
     */
    public void clonePlayer(EntityPlayer p_71049_1_, boolean p_71049_2_)
    {
        super.clonePlayer(p_71049_1_, p_71049_2_);
        this.lastExperience = -1;
        this.lastHealth = -1.0F;
        this.lastFoodLevel = -1;
        this.destroyedItemsNetCache.addAll(((EntityPlayerMP)p_71049_1_).destroyedItemsNetCache);
    }

    protected void onNewPotionEffect(PotionEffect p_70670_1_)
    {
        super.onNewPotionEffect(p_70670_1_);
        this.playerNetServerHandler.sendPacket(new S1DPacketEntityEffect(this.getEntityId(), p_70670_1_));
    }

    protected void onChangedPotionEffect(PotionEffect p_70695_1_, boolean p_70695_2_)
    {
        super.onChangedPotionEffect(p_70695_1_, p_70695_2_);
        this.playerNetServerHandler.sendPacket(new S1DPacketEntityEffect(this.getEntityId(), p_70695_1_));
    }

    protected void onFinishedPotionEffect(PotionEffect p_70688_1_)
    {
        super.onFinishedPotionEffect(p_70688_1_);
        this.playerNetServerHandler.sendPacket(new S1EPacketRemoveEntityEffect(this.getEntityId(), p_70688_1_));
    }

    /**
     * Sets the position of the entity, keeps yaw/pitch,  and updates the 'last' variables
     */
    public void setPositionAndUpdate(double p_70634_1_, double p_70634_3_, double p_70634_5_)
    {
        this.playerNetServerHandler.setPlayerLocation(p_70634_1_, p_70634_3_, p_70634_5_, this.rotationYaw, this.rotationPitch);
    }

    /**
     * Called when the player performs a critical hit on the Entity. Args: entity that was hit critically
     */
    public void onCriticalHit(Entity p_71009_1_)
    {
        this.getServerForPlayer().getEntityTracker().func_151248_b(this, new S0BPacketAnimation(p_71009_1_, 4));
    }

    public void onEnchantmentCritical(Entity p_71047_1_)
    {
        this.getServerForPlayer().getEntityTracker().func_151248_b(this, new S0BPacketAnimation(p_71047_1_, 5));
    }

    /**
     * Sends the player's abilities to the server (if there is one).
     */
    public void sendPlayerAbilities()
    {
        if (this.playerNetServerHandler != null)
        {
            this.playerNetServerHandler.sendPacket(new S39PacketPlayerAbilities(this.capabilities));
        }
    }

    public WorldServer getServerForPlayer()
    {
        return (WorldServer)this.worldObj;
    }

    /**
     * Sets the player's game mode and sends it to them.
     */
    public void setGameType(WorldSettings.GameType p_71033_1_)
    {
        this.theItemInWorldManager.setGameType(p_71033_1_);
        this.playerNetServerHandler.sendPacket(new S2BPacketChangeGameState(3, (float)p_71033_1_.getID()));
    }

    /**
     * Notifies this sender of some sort of information.  This is for messages intended to display to the user.  Used
     * for typical output (like "you asked for whether or not this game rule is set, so here's your answer"), warnings
     * (like "I fetched this block for you by ID, but I'd like you to know that every time you do this, I die a little
     * inside"), and errors (like "it's not called iron_pixacke, silly").
     */
    public void addChatMessage(IChatComponent p_145747_1_)
    {
        this.playerNetServerHandler.sendPacket(new S02PacketChat(p_145747_1_));
    }

    /**
     * Returns true if the command sender is allowed to use the given command.
     */
    public boolean canCommandSenderUseCommand(int p_70003_1_, String p_70003_2_)
    {
        if ("seed".equals(p_70003_2_) && !this.mcServer.isDedicatedServer())
        {
            return true;
        }
        else if (!"tell".equals(p_70003_2_) && !"help".equals(p_70003_2_) && !"me".equals(p_70003_2_))
        {
            if (this.mcServer.getConfigurationManager().func_152596_g(this.getGameProfile()))
            {
                UserListOpsEntry userlistopsentry = (UserListOpsEntry)this.mcServer.getConfigurationManager().func_152603_m().func_152683_b(this.getGameProfile());
                return userlistopsentry != null ? userlistopsentry.func_152644_a() >= p_70003_1_ : this.mcServer.getOpPermissionLevel() >= p_70003_1_;
            }
            else
            {
                return false;
            }
        }
        else
        {
            return true;
        }
    }

    /**
     * Gets the player's IP address. Used in /banip.
     */
    public String getPlayerIP()
    {
        String s = this.playerNetServerHandler.netManager.getSocketAddress().toString();
        s = s.substring(s.indexOf("/") + 1);
        s = s.substring(0, s.indexOf(":"));
        return s;
    }

    public void func_147100_a(C15PacketClientSettings p_147100_1_)
    {
        this.translator = p_147100_1_.func_149524_c();
        int i = 256 >> p_147100_1_.func_149521_d();

        if (i > 3 && i < 20)
        {
            ;
        }

        this.chatVisibility = p_147100_1_.func_149523_e();
        this.chatColours = p_147100_1_.func_149520_f();

        if (this.mcServer.isSinglePlayer() && this.mcServer.getServerOwner().equals(this.getCommandSenderName()))
        {
            this.mcServer.func_147139_a(p_147100_1_.func_149518_g());
        }

        this.setHideCape(1, !p_147100_1_.func_149519_h());
    }

    public EntityPlayer.EnumChatVisibility func_147096_v()
    {
        return this.chatVisibility;
    }

    /**
     * on receiving this message the client (if permission is given) will download the requested textures
     */
    public void requestTexturePackLoad(String p_147095_1_)
    {
        this.playerNetServerHandler.sendPacket(new S3FPacketCustomPayload("MC|RPack", p_147095_1_.getBytes(Charsets.UTF_8)));
    }

    /**
     * Return the position for this command sender.
     */
    public ChunkCoordinates getPlayerCoordinates()
    {
        return new ChunkCoordinates(MathHelper.floor_double(this.posX), MathHelper.floor_double(this.posY + 0.5D), MathHelper.floor_double(this.posZ));
    }

    public void func_143004_u()
    {
        this.field_143005_bX = MinecraftServer.getSystemTimeMillis();
    }

    public StatisticsFile func_147099_x()
    {
        return this.field_147103_bO;
    }

    public void func_152339_d(Entity p_152339_1_)
    {
        if (p_152339_1_ instanceof EntityPlayer)
        {
            this.playerNetServerHandler.sendPacket(new S13PacketDestroyEntities(new int[] {p_152339_1_.getEntityId()}));
        }
        else
        {
            this.destroyedItemsNetCache.add(Integer.valueOf(p_152339_1_.getEntityId()));
        }
    }

    public long func_154331_x()
    {
        return this.field_143005_bX;
    }

    /* ===================================== FORGE START =====================================*/
    /**
     * Returns the default eye height of the player
     * @return player default eye height
     */
    @Override
    public float getDefaultEyeHeight()
    {
        return 1.62F;
    }
    /* ===================================== FORGE END =====================================*/
}