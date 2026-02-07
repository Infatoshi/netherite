package net.minecraft.entity;

import java.util.Collection;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import net.minecraft.block.Block;
import net.minecraft.entity.ai.attributes.ServersideAttributeMap;
import net.minecraft.entity.boss.EntityDragon;
import net.minecraft.entity.item.EntityBoat;
import net.minecraft.entity.item.EntityEnderCrystal;
import net.minecraft.entity.item.EntityEnderEye;
import net.minecraft.entity.item.EntityEnderPearl;
import net.minecraft.entity.item.EntityExpBottle;
import net.minecraft.entity.item.EntityFallingBlock;
import net.minecraft.entity.item.EntityFireworkRocket;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityItemFrame;
import net.minecraft.entity.item.EntityMinecart;
import net.minecraft.entity.item.EntityPainting;
import net.minecraft.entity.item.EntityTNTPrimed;
import net.minecraft.entity.item.EntityXPOrb;
import net.minecraft.entity.passive.IAnimals;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.entity.projectile.EntityEgg;
import net.minecraft.entity.projectile.EntityFireball;
import net.minecraft.entity.projectile.EntityFishHook;
import net.minecraft.entity.projectile.EntityPotion;
import net.minecraft.entity.projectile.EntitySmallFireball;
import net.minecraft.entity.projectile.EntitySnowball;
import net.minecraft.entity.projectile.EntityWitherSkull;
import net.minecraft.init.Items;
import net.minecraft.item.ItemMap;
import net.minecraft.item.ItemStack;
import net.minecraft.network.Packet;
import net.minecraft.network.play.server.S04PacketEntityEquipment;
import net.minecraft.network.play.server.S0APacketUseBed;
import net.minecraft.network.play.server.S0CPacketSpawnPlayer;
import net.minecraft.network.play.server.S0EPacketSpawnObject;
import net.minecraft.network.play.server.S0FPacketSpawnMob;
import net.minecraft.network.play.server.S10PacketSpawnPainting;
import net.minecraft.network.play.server.S11PacketSpawnExperienceOrb;
import net.minecraft.network.play.server.S12PacketEntityVelocity;
import net.minecraft.network.play.server.S14PacketEntity;
import net.minecraft.network.play.server.S18PacketEntityTeleport;
import net.minecraft.network.play.server.S19PacketEntityHeadLook;
import net.minecraft.network.play.server.S1BPacketEntityAttach;
import net.minecraft.network.play.server.S1CPacketEntityMetadata;
import net.minecraft.network.play.server.S1DPacketEntityEffect;
import net.minecraft.network.play.server.S20PacketEntityProperties;
import net.minecraft.potion.PotionEffect;
import net.minecraft.util.MathHelper;
import net.minecraft.world.storage.MapData;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import cpw.mods.fml.common.network.internal.FMLNetworkHandler;

public class EntityTrackerEntry
{
    private static final Logger logger = LogManager.getLogger();
    public Entity myEntity;
    public int blocksDistanceThreshold;
    /** check for sync when ticks % updateFrequency==0 */
    public int updateFrequency;
    public int lastScaledXPosition;
    public int lastScaledYPosition;
    public int lastScaledZPosition;
    public int lastYaw;
    public int lastPitch;
    public int lastHeadMotion;
    public double motionX;
    public double motionY;
    public double motionZ;
    public int ticks;
    private double posX;
    private double posY;
    private double posZ;
    /** set to true on first sendLocationToClients */
    private boolean isDataInitialized;
    private boolean sendVelocityUpdates;
    /**
     * every 400 ticks a  full teleport packet is sent, rather than just a "move me +x" command, so that position
     * remains fully synced.
     */
    private int ticksSinceLastForcedTeleport;
    private Entity field_85178_v;
    private boolean ridingEntity;
    public boolean playerEntitiesUpdated;
    /** Holds references to all the players that are currently receiving position updates for this entity. */
    public Set trackingPlayers = new HashSet();
    private static final String __OBFID = "CL_00001443";

    public EntityTrackerEntry(Entity p_i1525_1_, int p_i1525_2_, int p_i1525_3_, boolean p_i1525_4_)
    {
        this.myEntity = p_i1525_1_;
        this.blocksDistanceThreshold = p_i1525_2_;
        this.updateFrequency = p_i1525_3_;
        this.sendVelocityUpdates = p_i1525_4_;
        this.lastScaledXPosition = MathHelper.floor_double(p_i1525_1_.posX * 32.0D);
        this.lastScaledYPosition = MathHelper.floor_double(p_i1525_1_.posY * 32.0D);
        this.lastScaledZPosition = MathHelper.floor_double(p_i1525_1_.posZ * 32.0D);
        this.lastYaw = MathHelper.floor_float(p_i1525_1_.rotationYaw * 256.0F / 360.0F);
        this.lastPitch = MathHelper.floor_float(p_i1525_1_.rotationPitch * 256.0F / 360.0F);
        this.lastHeadMotion = MathHelper.floor_float(p_i1525_1_.getRotationYawHead() * 256.0F / 360.0F);
    }

    public boolean equals(Object p_equals_1_)
    {
        return p_equals_1_ instanceof EntityTrackerEntry ? ((EntityTrackerEntry)p_equals_1_).myEntity.getEntityId() == this.myEntity.getEntityId() : false;
    }

    public int hashCode()
    {
        return this.myEntity.getEntityId();
    }

    /**
     * also sends velocity, rotation, and riding info.
     */
    public void sendLocationToAllClients(List p_73122_1_)
    {
        this.playerEntitiesUpdated = false;

        if (!this.isDataInitialized || this.myEntity.getDistanceSq(this.posX, this.posY, this.posZ) > 16.0D)
        {
            this.posX = this.myEntity.posX;
            this.posY = this.myEntity.posY;
            this.posZ = this.myEntity.posZ;
            this.isDataInitialized = true;
            this.playerEntitiesUpdated = true;
            this.sendEventsToPlayers(p_73122_1_);
        }

        if (this.field_85178_v != this.myEntity.ridingEntity || this.myEntity.ridingEntity != null && this.ticks % 60 == 0)
        {
            this.field_85178_v = this.myEntity.ridingEntity;
            this.func_151259_a(new S1BPacketEntityAttach(0, this.myEntity, this.myEntity.ridingEntity));
        }

        if (this.myEntity instanceof EntityItemFrame && this.ticks % 10 == 0)
        {
            EntityItemFrame entityitemframe = (EntityItemFrame)this.myEntity;
            ItemStack itemstack = entityitemframe.getDisplayedItem();

            if (itemstack != null && itemstack.getItem() instanceof ItemMap)
            {
                MapData mapdata = Items.filled_map.getMapData(itemstack, this.myEntity.worldObj);
                Iterator iterator = p_73122_1_.iterator();

                while (iterator.hasNext())
                {
                    EntityPlayer entityplayer = (EntityPlayer)iterator.next();
                    EntityPlayerMP entityplayermp = (EntityPlayerMP)entityplayer;
                    mapdata.updateVisiblePlayers(entityplayermp, itemstack);
                    Packet packet = Items.filled_map.func_150911_c(itemstack, this.myEntity.worldObj, entityplayermp);

                    if (packet != null)
                    {
                        entityplayermp.playerNetServerHandler.sendPacket(packet);
                    }
                }
            }

            this.sendMetadataToAllAssociatedPlayers();
        }
        else if (this.ticks % this.updateFrequency == 0 || this.myEntity.isAirBorne || this.myEntity.getDataWatcher().hasChanges())
        {
            int i;
            int j;

            if (this.myEntity.ridingEntity == null)
            {
                ++this.ticksSinceLastForcedTeleport;
                i = this.myEntity.myEntitySize.multiplyBy32AndRound(this.myEntity.posX);
                j = MathHelper.floor_double(this.myEntity.posY * 32.0D);
                int k = this.myEntity.myEntitySize.multiplyBy32AndRound(this.myEntity.posZ);
                int l = MathHelper.floor_float(this.myEntity.rotationYaw * 256.0F / 360.0F);
                int i1 = MathHelper.floor_float(this.myEntity.rotationPitch * 256.0F / 360.0F);
                int j1 = i - this.lastScaledXPosition;
                int k1 = j - this.lastScaledYPosition;
                int l1 = k - this.lastScaledZPosition;
                Object object = null;
                boolean flag = Math.abs(j1) >= 4 || Math.abs(k1) >= 4 || Math.abs(l1) >= 4 || this.ticks % 60 == 0;
                boolean flag1 = Math.abs(l - this.lastYaw) >= 4 || Math.abs(i1 - this.lastPitch) >= 4;

                if (this.ticks > 0 || this.myEntity instanceof EntityArrow)
                {
                    if (j1 >= -128 && j1 < 128 && k1 >= -128 && k1 < 128 && l1 >= -128 && l1 < 128 && this.ticksSinceLastForcedTeleport <= 400 && !this.ridingEntity)
                    {
                        if (flag && flag1)
                        {
                            object = new S14PacketEntity.S17PacketEntityLookMove(this.myEntity.getEntityId(), (byte)j1, (byte)k1, (byte)l1, (byte)l, (byte)i1);
                        }
                        else if (flag)
                        {
                            object = new S14PacketEntity.S15PacketEntityRelMove(this.myEntity.getEntityId(), (byte)j1, (byte)k1, (byte)l1);
                        }
                        else if (flag1)
                        {
                            object = new S14PacketEntity.S16PacketEntityLook(this.myEntity.getEntityId(), (byte)l, (byte)i1);
                        }
                    }
                    else
                    {
                        this.ticksSinceLastForcedTeleport = 0;
                        object = new S18PacketEntityTeleport(this.myEntity.getEntityId(), i, j, k, (byte)l, (byte)i1);
                    }
                }

                if (this.sendVelocityUpdates)
                {
                    double d0 = this.myEntity.motionX - this.motionX;
                    double d1 = this.myEntity.motionY - this.motionY;
                    double d2 = this.myEntity.motionZ - this.motionZ;
                    double d3 = 0.02D;
                    double d4 = d0 * d0 + d1 * d1 + d2 * d2;

                    if (d4 > d3 * d3 || d4 > 0.0D && this.myEntity.motionX == 0.0D && this.myEntity.motionY == 0.0D && this.myEntity.motionZ == 0.0D)
                    {
                        this.motionX = this.myEntity.motionX;
                        this.motionY = this.myEntity.motionY;
                        this.motionZ = this.myEntity.motionZ;
                        this.func_151259_a(new S12PacketEntityVelocity(this.myEntity.getEntityId(), this.motionX, this.motionY, this.motionZ));
                    }
                }

                if (object != null)
                {
                    this.func_151259_a((Packet)object);
                }

                this.sendMetadataToAllAssociatedPlayers();

                if (flag)
                {
                    this.lastScaledXPosition = i;
                    this.lastScaledYPosition = j;
                    this.lastScaledZPosition = k;
                }

                if (flag1)
                {
                    this.lastYaw = l;
                    this.lastPitch = i1;
                }

                this.ridingEntity = false;
            }
            else
            {
                i = MathHelper.floor_float(this.myEntity.rotationYaw * 256.0F / 360.0F);
                j = MathHelper.floor_float(this.myEntity.rotationPitch * 256.0F / 360.0F);
                boolean flag2 = Math.abs(i - this.lastYaw) >= 4 || Math.abs(j - this.lastPitch) >= 4;

                if (flag2)
                {
                    this.func_151259_a(new S14PacketEntity.S16PacketEntityLook(this.myEntity.getEntityId(), (byte)i, (byte)j));
                    this.lastYaw = i;
                    this.lastPitch = j;
                }

                this.lastScaledXPosition = this.myEntity.myEntitySize.multiplyBy32AndRound(this.myEntity.posX);
                this.lastScaledYPosition = MathHelper.floor_double(this.myEntity.posY * 32.0D);
                this.lastScaledZPosition = this.myEntity.myEntitySize.multiplyBy32AndRound(this.myEntity.posZ);
                this.sendMetadataToAllAssociatedPlayers();
                this.ridingEntity = true;
            }

            i = MathHelper.floor_float(this.myEntity.getRotationYawHead() * 256.0F / 360.0F);

            if (Math.abs(i - this.lastHeadMotion) >= 4)
            {
                this.func_151259_a(new S19PacketEntityHeadLook(this.myEntity, (byte)i));
                this.lastHeadMotion = i;
            }

            this.myEntity.isAirBorne = false;
        }

        ++this.ticks;

        if (this.myEntity.velocityChanged)
        {
            this.func_151261_b(new S12PacketEntityVelocity(this.myEntity));
            this.myEntity.velocityChanged = false;
        }
    }

    /**
     * Sends the entity metadata (DataWatcher) and attributes to all players tracking this entity, including the entity
     * itself if a player.
     */
    private void sendMetadataToAllAssociatedPlayers()
    {
        DataWatcher datawatcher = this.myEntity.getDataWatcher();

        if (datawatcher.hasChanges())
        {
            this.func_151261_b(new S1CPacketEntityMetadata(this.myEntity.getEntityId(), datawatcher, false));
        }

        if (this.myEntity instanceof EntityLivingBase)
        {
            ServersideAttributeMap serversideattributemap = (ServersideAttributeMap)((EntityLivingBase)this.myEntity).getAttributeMap();
            Set set = serversideattributemap.getAttributeInstanceSet();

            if (!set.isEmpty())
            {
                this.func_151261_b(new S20PacketEntityProperties(this.myEntity.getEntityId(), set));
            }

            set.clear();
        }
    }

    public void func_151259_a(Packet p_151259_1_)
    {
        Iterator iterator = this.trackingPlayers.iterator();

        while (iterator.hasNext())
        {
            EntityPlayerMP entityplayermp = (EntityPlayerMP)iterator.next();
            entityplayermp.playerNetServerHandler.sendPacket(p_151259_1_);
        }
    }

    public void func_151261_b(Packet p_151261_1_)
    {
        this.func_151259_a(p_151261_1_);

        if (this.myEntity instanceof EntityPlayerMP)
        {
            ((EntityPlayerMP)this.myEntity).playerNetServerHandler.sendPacket(p_151261_1_);
        }
    }

    public void informAllAssociatedPlayersOfItemDestruction()
    {
        Iterator iterator = this.trackingPlayers.iterator();

        while (iterator.hasNext())
        {
            EntityPlayerMP entityplayermp = (EntityPlayerMP)iterator.next();
            entityplayermp.func_152339_d(this.myEntity);
        }
    }

    public void removeFromWatchingList(EntityPlayerMP p_73118_1_)
    {
        if (this.trackingPlayers.contains(p_73118_1_))
        {
            p_73118_1_.func_152339_d(this.myEntity);
            this.trackingPlayers.remove(p_73118_1_);
        }
    }

    /**
     * if the player is more than the distance threshold (typically 64) then the player is removed instead
     */
    public void tryStartWachingThis(EntityPlayerMP p_73117_1_)
    {
        if (p_73117_1_ != this.myEntity)
        {
            double d0 = p_73117_1_.posX - (double)(this.lastScaledXPosition / 32);
            double d1 = p_73117_1_.posZ - (double)(this.lastScaledZPosition / 32);

            if (d0 >= (double)(-this.blocksDistanceThreshold) && d0 <= (double)this.blocksDistanceThreshold && d1 >= (double)(-this.blocksDistanceThreshold) && d1 <= (double)this.blocksDistanceThreshold)
            {
                if (!this.trackingPlayers.contains(p_73117_1_) && (this.isPlayerWatchingThisChunk(p_73117_1_) || this.myEntity.forceSpawn))
                {
                    this.trackingPlayers.add(p_73117_1_);
                    Packet packet = this.func_151260_c();
                    p_73117_1_.playerNetServerHandler.sendPacket(packet);

                    if (!this.myEntity.getDataWatcher().getIsBlank())
                    {
                        p_73117_1_.playerNetServerHandler.sendPacket(new S1CPacketEntityMetadata(this.myEntity.getEntityId(), this.myEntity.getDataWatcher(), true));
                    }

                    if (this.myEntity instanceof EntityLivingBase)
                    {
                        ServersideAttributeMap serversideattributemap = (ServersideAttributeMap)((EntityLivingBase)this.myEntity).getAttributeMap();
                        Collection collection = serversideattributemap.getWatchedAttributes();

                        if (!collection.isEmpty())
                        {
                            p_73117_1_.playerNetServerHandler.sendPacket(new S20PacketEntityProperties(this.myEntity.getEntityId(), collection));
                        }
                    }

                    this.motionX = this.myEntity.motionX;
                    this.motionY = this.myEntity.motionY;
                    this.motionZ = this.myEntity.motionZ;

                    int posX = MathHelper.floor_double(this.myEntity.posX * 32.0D);
                    int posY = MathHelper.floor_double(this.myEntity.posY * 32.0D);
                    int posZ = MathHelper.floor_double(this.myEntity.posZ * 32.0D);
                    if (posX != this.lastScaledXPosition || posY != this.lastScaledYPosition || posZ != this.lastScaledZPosition)
                    {
                        FMLNetworkHandler.makeEntitySpawnAdjustment(this.myEntity, p_73117_1_, this.lastScaledXPosition, this.lastScaledYPosition, this.lastScaledZPosition);
                    }

                    if (this.sendVelocityUpdates && !(packet instanceof S0FPacketSpawnMob))
                    {
                        p_73117_1_.playerNetServerHandler.sendPacket(new S12PacketEntityVelocity(this.myEntity.getEntityId(), this.myEntity.motionX, this.myEntity.motionY, this.myEntity.motionZ));
                    }

                    if (this.myEntity.ridingEntity != null)
                    {
                        p_73117_1_.playerNetServerHandler.sendPacket(new S1BPacketEntityAttach(0, this.myEntity, this.myEntity.ridingEntity));
                    }

                    if (this.myEntity instanceof EntityLiving && ((EntityLiving)this.myEntity).getLeashedToEntity() != null)
                    {
                        p_73117_1_.playerNetServerHandler.sendPacket(new S1BPacketEntityAttach(1, this.myEntity, ((EntityLiving)this.myEntity).getLeashedToEntity()));
                    }

                    if (this.myEntity instanceof EntityLivingBase)
                    {
                        for (int i = 0; i < 5; ++i)
                        {
                            ItemStack itemstack = ((EntityLivingBase)this.myEntity).getEquipmentInSlot(i);

                            if (itemstack != null)
                            {
                                p_73117_1_.playerNetServerHandler.sendPacket(new S04PacketEntityEquipment(this.myEntity.getEntityId(), i, itemstack));
                            }
                        }
                    }

                    if (this.myEntity instanceof EntityPlayer)
                    {
                        EntityPlayer entityplayer = (EntityPlayer)this.myEntity;

                        if (entityplayer.isPlayerSleeping())
                        {
                            p_73117_1_.playerNetServerHandler.sendPacket(new S0APacketUseBed(entityplayer, MathHelper.floor_double(this.myEntity.posX), MathHelper.floor_double(this.myEntity.posY), MathHelper.floor_double(this.myEntity.posZ)));
                        }
                    }

                    if (this.myEntity instanceof EntityLivingBase)
                    {
                        EntityLivingBase entitylivingbase = (EntityLivingBase)this.myEntity;
                        Iterator iterator = entitylivingbase.getActivePotionEffects().iterator();

                        while (iterator.hasNext())
                        {
                            PotionEffect potioneffect = (PotionEffect)iterator.next();
                            p_73117_1_.playerNetServerHandler.sendPacket(new S1DPacketEntityEffect(this.myEntity.getEntityId(), potioneffect));
                        }
                    }
                    net.minecraftforge.event.ForgeEventFactory.onStartEntityTracking(myEntity, p_73117_1_);
                }
            }
            else if (this.trackingPlayers.contains(p_73117_1_))
            {
                this.trackingPlayers.remove(p_73117_1_);
                p_73117_1_.func_152339_d(this.myEntity);
                net.minecraftforge.event.ForgeEventFactory.onStopEntityTracking(myEntity, p_73117_1_);
            }
        }
    }

    private boolean isPlayerWatchingThisChunk(EntityPlayerMP p_73121_1_)
    {
        return p_73121_1_.getServerForPlayer().getPlayerManager().isPlayerWatchingChunk(p_73121_1_, this.myEntity.chunkCoordX, this.myEntity.chunkCoordZ);
    }

    public void sendEventsToPlayers(List p_73125_1_)
    {
        for (int i = 0; i < p_73125_1_.size(); ++i)
        {
            this.tryStartWachingThis((EntityPlayerMP)p_73125_1_.get(i));
        }
    }

    private Packet func_151260_c()
    {
        if (this.myEntity.isDead)
        {
            logger.warn("Fetching addPacket for removed entity");
        }

        Packet pkt = FMLNetworkHandler.getEntitySpawningPacket(this.myEntity);

        if (pkt != null)
        {
            return pkt;
        }
        if (this.myEntity instanceof EntityItem)
        {
            return new S0EPacketSpawnObject(this.myEntity, 2, 1);
        }
        else if (this.myEntity instanceof EntityPlayerMP)
        {
            return new S0CPacketSpawnPlayer((EntityPlayer)this.myEntity);
        }
        else if (this.myEntity instanceof EntityMinecart)
        {
            EntityMinecart entityminecart = (EntityMinecart)this.myEntity;
            return new S0EPacketSpawnObject(this.myEntity, 10, entityminecart.getMinecartType());
        }
        else if (this.myEntity instanceof EntityBoat)
        {
            return new S0EPacketSpawnObject(this.myEntity, 1);
        }
        else if (!(this.myEntity instanceof IAnimals) && !(this.myEntity instanceof EntityDragon))
        {
            if (this.myEntity instanceof EntityFishHook)
            {
                EntityPlayer entityplayer = ((EntityFishHook)this.myEntity).field_146042_b;
                return new S0EPacketSpawnObject(this.myEntity, 90, entityplayer != null ? entityplayer.getEntityId() : this.myEntity.getEntityId());
            }
            else if (this.myEntity instanceof EntityArrow)
            {
                Entity entity = ((EntityArrow)this.myEntity).shootingEntity;
                return new S0EPacketSpawnObject(this.myEntity, 60, entity != null ? entity.getEntityId() : this.myEntity.getEntityId());
            }
            else if (this.myEntity instanceof EntitySnowball)
            {
                return new S0EPacketSpawnObject(this.myEntity, 61);
            }
            else if (this.myEntity instanceof EntityPotion)
            {
                return new S0EPacketSpawnObject(this.myEntity, 73, ((EntityPotion)this.myEntity).getPotionDamage());
            }
            else if (this.myEntity instanceof EntityExpBottle)
            {
                return new S0EPacketSpawnObject(this.myEntity, 75);
            }
            else if (this.myEntity instanceof EntityEnderPearl)
            {
                return new S0EPacketSpawnObject(this.myEntity, 65);
            }
            else if (this.myEntity instanceof EntityEnderEye)
            {
                return new S0EPacketSpawnObject(this.myEntity, 72);
            }
            else if (this.myEntity instanceof EntityFireworkRocket)
            {
                return new S0EPacketSpawnObject(this.myEntity, 76);
            }
            else
            {
                S0EPacketSpawnObject s0epacketspawnobject;

                if (this.myEntity instanceof EntityFireball)
                {
                    EntityFireball entityfireball = (EntityFireball)this.myEntity;
                    s0epacketspawnobject = null;
                    byte b0 = 63;

                    if (this.myEntity instanceof EntitySmallFireball)
                    {
                        b0 = 64;
                    }
                    else if (this.myEntity instanceof EntityWitherSkull)
                    {
                        b0 = 66;
                    }

                    if (entityfireball.shootingEntity != null)
                    {
                        s0epacketspawnobject = new S0EPacketSpawnObject(this.myEntity, b0, ((EntityFireball)this.myEntity).shootingEntity.getEntityId());
                    }
                    else
                    {
                        s0epacketspawnobject = new S0EPacketSpawnObject(this.myEntity, b0, 0);
                    }

                    s0epacketspawnobject.func_149003_d((int)(entityfireball.accelerationX * 8000.0D));
                    s0epacketspawnobject.func_149000_e((int)(entityfireball.accelerationY * 8000.0D));
                    s0epacketspawnobject.func_149007_f((int)(entityfireball.accelerationZ * 8000.0D));
                    return s0epacketspawnobject;
                }
                else if (this.myEntity instanceof EntityEgg)
                {
                    return new S0EPacketSpawnObject(this.myEntity, 62);
                }
                else if (this.myEntity instanceof EntityTNTPrimed)
                {
                    return new S0EPacketSpawnObject(this.myEntity, 50);
                }
                else if (this.myEntity instanceof EntityEnderCrystal)
                {
                    return new S0EPacketSpawnObject(this.myEntity, 51);
                }
                else if (this.myEntity instanceof EntityFallingBlock)
                {
                    EntityFallingBlock entityfallingblock = (EntityFallingBlock)this.myEntity;
                    return new S0EPacketSpawnObject(this.myEntity, 70, Block.getIdFromBlock(entityfallingblock.func_145805_f()) | entityfallingblock.field_145814_a << 16);
                }
                else if (this.myEntity instanceof EntityPainting)
                {
                    return new S10PacketSpawnPainting((EntityPainting)this.myEntity);
                }
                else if (this.myEntity instanceof EntityItemFrame)
                {
                    EntityItemFrame entityitemframe = (EntityItemFrame)this.myEntity;
                    s0epacketspawnobject = new S0EPacketSpawnObject(this.myEntity, 71, entityitemframe.hangingDirection);
                    s0epacketspawnobject.func_148996_a(MathHelper.floor_float((float)(entityitemframe.field_146063_b * 32)));
                    s0epacketspawnobject.func_148995_b(MathHelper.floor_float((float)(entityitemframe.field_146064_c * 32)));
                    s0epacketspawnobject.func_149005_c(MathHelper.floor_float((float)(entityitemframe.field_146062_d * 32)));
                    return s0epacketspawnobject;
                }
                else if (this.myEntity instanceof EntityLeashKnot)
                {
                    EntityLeashKnot entityleashknot = (EntityLeashKnot)this.myEntity;
                    s0epacketspawnobject = new S0EPacketSpawnObject(this.myEntity, 77);
                    s0epacketspawnobject.func_148996_a(MathHelper.floor_float((float)(entityleashknot.field_146063_b * 32)));
                    s0epacketspawnobject.func_148995_b(MathHelper.floor_float((float)(entityleashknot.field_146064_c * 32)));
                    s0epacketspawnobject.func_149005_c(MathHelper.floor_float((float)(entityleashknot.field_146062_d * 32)));
                    return s0epacketspawnobject;
                }
                else if (this.myEntity instanceof EntityXPOrb)
                {
                    return new S11PacketSpawnExperienceOrb((EntityXPOrb)this.myEntity);
                }
                else
                {
                    throw new IllegalArgumentException("Don\'t know how to add " + this.myEntity.getClass() + "!");
                }
            }
        }
        else
        {
            this.lastHeadMotion = MathHelper.floor_float(this.myEntity.getRotationYawHead() * 256.0F / 360.0F);
            return new S0FPacketSpawnMob((EntityLivingBase)this.myEntity);
        }
    }

    public void removePlayerFromTracker(EntityPlayerMP p_73123_1_)
    {
        if (this.trackingPlayers.contains(p_73123_1_))
        {
            this.trackingPlayers.remove(p_73123_1_);
            p_73123_1_.func_152339_d(this.myEntity);
        }
    }
}