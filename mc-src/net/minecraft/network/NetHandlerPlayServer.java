package net.minecraft.network;

import com.google.common.base.Charsets;
import com.google.common.collect.Lists;
import io.netty.buffer.Unpooled;
import io.netty.util.concurrent.Future;
import io.netty.util.concurrent.GenericFutureListener;
import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Date;
import java.util.Iterator;
import java.util.Random;
import java.util.concurrent.Callable;
import net.minecraft.block.material.Material;
import net.minecraft.command.server.CommandBlockLogic;
import net.minecraft.crash.CrashReport;
import net.minecraft.crash.CrashReportCategory;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityMinecartCommandBlock;
import net.minecraft.entity.item.EntityItem;
import net.minecraft.entity.item.EntityXPOrb;
// stripped: import net.minecraft.entity.passive.EntityHorse;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.entity.player.InventoryPlayer;
import net.minecraft.entity.projectile.EntityArrow;
import net.minecraft.init.Items;
import net.minecraft.inventory.Container;
// stripped: import net.minecraft.inventory.ContainerBeacon;
// stripped: import net.minecraft.inventory.ContainerMerchant;
// stripped: import net.minecraft.inventory.ContainerRepair;
import net.minecraft.inventory.Slot;
import net.minecraft.item.ItemEditableBook;
import net.minecraft.item.ItemStack;
import net.minecraft.item.ItemWritableBook;
import net.minecraft.nbt.NBTTagString;
import net.minecraft.network.play.INetHandlerPlayServer;
import net.minecraft.network.play.client.C00PacketKeepAlive;
import net.minecraft.network.play.client.C01PacketChatMessage;
import net.minecraft.network.play.client.C02PacketUseEntity;
import net.minecraft.network.play.client.C03PacketPlayer;
import net.minecraft.network.play.client.C07PacketPlayerDigging;
import net.minecraft.network.play.client.C08PacketPlayerBlockPlacement;
import net.minecraft.network.play.client.C09PacketHeldItemChange;
import net.minecraft.network.play.client.C0APacketAnimation;
import net.minecraft.network.play.client.C0BPacketEntityAction;
import net.minecraft.network.play.client.C0CPacketInput;
import net.minecraft.network.play.client.C0DPacketCloseWindow;
import net.minecraft.network.play.client.C0EPacketClickWindow;
import net.minecraft.network.play.client.C0FPacketConfirmTransaction;
import net.minecraft.network.play.client.C10PacketCreativeInventoryAction;
import net.minecraft.network.play.client.C11PacketEnchantItem;
import net.minecraft.network.play.client.C12PacketUpdateSign;
import net.minecraft.network.play.client.C13PacketPlayerAbilities;
import net.minecraft.network.play.client.C14PacketTabComplete;
import net.minecraft.network.play.client.C15PacketClientSettings;
import net.minecraft.network.play.client.C16PacketClientStatus;
import net.minecraft.network.play.client.C17PacketCustomPayload;
import net.minecraft.network.play.server.S00PacketKeepAlive;
import net.minecraft.network.play.server.S02PacketChat;
import net.minecraft.network.play.server.S08PacketPlayerPosLook;
import net.minecraft.network.play.server.S23PacketBlockChange;
import net.minecraft.network.play.server.S2FPacketSetSlot;
import net.minecraft.network.play.server.S32PacketConfirmTransaction;
import net.minecraft.network.play.server.S3APacketTabComplete;
import net.minecraft.network.play.server.S40PacketDisconnect;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.management.UserListBansEntry;
import net.minecraft.stats.AchievementList;
import net.minecraft.tileentity.TileEntity;
// stripped: import net.minecraft.tileentity.TileEntityBeacon;
// removed: import net.minecraft.tileentity.TileEntityCommandBlock;
import net.minecraft.tileentity.TileEntitySign;
import net.minecraft.util.AxisAlignedBB;
import net.minecraft.util.ChatAllowedCharacters;
import net.minecraft.util.ChatComponentText;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.EnumChatFormatting;
import net.minecraft.util.IChatComponent;
import net.minecraft.util.IntHashMap;
import net.minecraft.util.ReportedException;
import net.minecraft.world.WorldServer;
import org.apache.commons.lang3.StringUtils;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import net.minecraftforge.common.ForgeHooks;
import net.minecraftforge.common.MinecraftForge;
import cpw.mods.fml.common.eventhandler.Event;
import net.minecraftforge.event.ForgeEventFactory;
import net.minecraftforge.event.ServerChatEvent;
import net.minecraftforge.event.entity.player.PlayerInteractEvent;
import net.minecraftforge.event.entity.player.PlayerInteractEvent.Action;

public class NetHandlerPlayServer implements INetHandlerPlayServer
{
    private static final Logger logger = LogManager.getLogger();
    public final NetworkManager netManager;
    private final MinecraftServer serverController;
    public EntityPlayerMP playerEntity;
    private int networkTickCount;
    /**
     * Used to keep track of how the player is floating while gamerules should prevent that. Surpassing 80 ticks means
     * kick
     */
    private int floatingTickCount;
    private boolean field_147366_g;
    private int field_147378_h;
    private long field_147379_i;
    private static Random field_147376_j = new Random();
    private long field_147377_k;
    /**
     * Incremented by 20 each time a user sends a chat message, decreased by one every tick. Non-ops kicked when over
     * 200
     */
    private int chatSpamThresholdCount;
    private int field_147375_m;
    private IntHashMap field_147372_n = new IntHashMap();
    /** The last known x position for this connection. */
    private double lastPosX;
    /** The last known y position for this connection. */
    private double lastPosY;
    /** The last known z position for this connection. */
    private double lastPosZ;
    /** is true when the player has moved since his last movement packet */
    private boolean hasMoved = true;
    private static final String __OBFID = "CL_00001452";

    public NetHandlerPlayServer(MinecraftServer p_i1530_1_, NetworkManager p_i1530_2_, EntityPlayerMP p_i1530_3_)
    {
        this.serverController = p_i1530_1_;
        this.netManager = p_i1530_2_;
        p_i1530_2_.setNetHandler(this);
        this.playerEntity = p_i1530_3_;
        p_i1530_3_.playerNetServerHandler = this;
    }

    /**
     * For scheduled network tasks. Used in NetHandlerPlayServer to send keep-alive packets and in NetHandlerLoginServer
     * for a login-timeout
     */
    public void onNetworkTick()
    {
        this.field_147366_g = false;
        ++this.networkTickCount;
        this.serverController.theProfiler.startSection("keepAlive");

        if ((long)this.networkTickCount - this.field_147377_k > 40L)
        {
            this.field_147377_k = (long)this.networkTickCount;
            this.field_147379_i = this.func_147363_d();
            this.field_147378_h = (int)this.field_147379_i;
            this.sendPacket(new S00PacketKeepAlive(this.field_147378_h));
        }

        if (this.chatSpamThresholdCount > 0)
        {
            --this.chatSpamThresholdCount;
        }

        if (this.field_147375_m > 0)
        {
            --this.field_147375_m;
        }

        if (this.playerEntity.func_154331_x() > 0L && this.serverController.func_143007_ar() > 0 && MinecraftServer.getSystemTimeMillis() - this.playerEntity.func_154331_x() > (long)(this.serverController.func_143007_ar() * 1000 * 60))
        {
            this.kickPlayerFromServer("You have been idle for too long!");
        }
    }

    public NetworkManager func_147362_b()
    {
        return this.netManager;
    }

    /**
     * Kick a player from the server with a reason
     */
    public void kickPlayerFromServer(String p_147360_1_)
    {
        final ChatComponentText chatcomponenttext = new ChatComponentText(p_147360_1_);
        this.netManager.scheduleOutboundPacket(new S40PacketDisconnect(chatcomponenttext), new GenericFutureListener[] {new GenericFutureListener()
        {
            private static final String __OBFID = "CL_00001453";
            public void operationComplete(Future p_operationComplete_1_)
            {
                NetHandlerPlayServer.this.netManager.closeChannel(chatcomponenttext);
            }
        }
                                                                                                     });
        this.netManager.disableAutoRead();
    }

    /**
     * Processes player movement input. Includes walking, strafing, jumping, sneaking; excludes riding and toggling
     * flying/sprinting
     */
    public void processInput(C0CPacketInput p_147358_1_)
    {
        this.playerEntity.setEntityActionState(p_147358_1_.func_149620_c(), p_147358_1_.func_149616_d(), p_147358_1_.func_149618_e(), p_147358_1_.func_149617_f());
    }

    /**
     * Processes clients perspective on player positioning and/or orientation
     */
    public void processPlayer(C03PacketPlayer p_147347_1_)
    {
        WorldServer worldserver = this.serverController.worldServerForDimension(this.playerEntity.dimension);
        this.field_147366_g = true;

        if (!this.playerEntity.playerConqueredTheEnd)
        {
            double d0;

            if (!this.hasMoved)
            {
                d0 = p_147347_1_.func_149467_d() - this.lastPosY;

                if (p_147347_1_.func_149464_c() == this.lastPosX && d0 * d0 < 0.01D && p_147347_1_.func_149472_e() == this.lastPosZ)
                {
                    this.hasMoved = true;
                }
            }

            if (this.hasMoved)
            {
                double d1;
                double d2;
                double d3;

                if (this.playerEntity.ridingEntity != null)
                {
                    float f4 = this.playerEntity.rotationYaw;
                    float f = this.playerEntity.rotationPitch;
                    this.playerEntity.ridingEntity.updateRiderPosition();
                    d1 = this.playerEntity.posX;
                    d2 = this.playerEntity.posY;
                    d3 = this.playerEntity.posZ;

                    if (p_147347_1_.func_149463_k())
                    {
                        f4 = p_147347_1_.func_149462_g();
                        f = p_147347_1_.func_149470_h();
                    }

                    this.playerEntity.onGround = p_147347_1_.func_149465_i();
                    this.playerEntity.onUpdateEntity();
                    this.playerEntity.ySize = 0.0F;
                    this.playerEntity.setPositionAndRotation(d1, d2, d3, f4, f);

                    if (this.playerEntity.ridingEntity != null)
                    {
                        this.playerEntity.ridingEntity.updateRiderPosition();
                    }

                    if (!this.hasMoved) //Fixes teleportation kick while riding entities
                    {
                        return;
                    }

                    this.serverController.getConfigurationManager().updatePlayerPertinentChunks(this.playerEntity);

                    if (this.hasMoved)
                    {
                        this.lastPosX = this.playerEntity.posX;
                        this.lastPosY = this.playerEntity.posY;
                        this.lastPosZ = this.playerEntity.posZ;
                    }

                    worldserver.updateEntity(this.playerEntity);
                    return;
                }

                if (this.playerEntity.isPlayerSleeping())
                {
                    this.playerEntity.onUpdateEntity();
                    this.playerEntity.setPositionAndRotation(this.lastPosX, this.lastPosY, this.lastPosZ, this.playerEntity.rotationYaw, this.playerEntity.rotationPitch);
                    worldserver.updateEntity(this.playerEntity);
                    return;
                }

                d0 = this.playerEntity.posY;
                this.lastPosX = this.playerEntity.posX;
                this.lastPosY = this.playerEntity.posY;
                this.lastPosZ = this.playerEntity.posZ;
                d1 = this.playerEntity.posX;
                d2 = this.playerEntity.posY;
                d3 = this.playerEntity.posZ;
                float f1 = this.playerEntity.rotationYaw;
                float f2 = this.playerEntity.rotationPitch;

                if (p_147347_1_.func_149466_j() && p_147347_1_.func_149467_d() == -999.0D && p_147347_1_.func_149471_f() == -999.0D)
                {
                    p_147347_1_.func_149469_a(false);
                }

                double d4;

                if (p_147347_1_.func_149466_j())
                {
                    d1 = p_147347_1_.func_149464_c();
                    d2 = p_147347_1_.func_149467_d();
                    d3 = p_147347_1_.func_149472_e();
                    d4 = p_147347_1_.func_149471_f() - p_147347_1_.func_149467_d();

                    if (!this.playerEntity.isPlayerSleeping() && (d4 > 1.65D || d4 < 0.1D))
                    {
                        this.kickPlayerFromServer("Illegal stance");
                        logger.warn(this.playerEntity.getCommandSenderName() + " had an illegal stance: " + d4);
                        return;
                    }

                    if (Math.abs(p_147347_1_.func_149464_c()) > 3.2E7D || Math.abs(p_147347_1_.func_149472_e()) > 3.2E7D)
                    {
                        this.kickPlayerFromServer("Illegal position");
                        return;
                    }
                }

                if (p_147347_1_.func_149463_k())
                {
                    f1 = p_147347_1_.func_149462_g();
                    f2 = p_147347_1_.func_149470_h();
                }

                this.playerEntity.onUpdateEntity();
                this.playerEntity.ySize = 0.0F;
                this.playerEntity.setPositionAndRotation(this.lastPosX, this.lastPosY, this.lastPosZ, f1, f2);

                if (!this.hasMoved)
                {
                    return;
                }

                d4 = d1 - this.playerEntity.posX;
                double d5 = d2 - this.playerEntity.posY;
                double d6 = d3 - this.playerEntity.posZ;
                //BUGFIX: min -> max, grabs the highest distance
                double d7 = Math.max(Math.abs(d4), Math.abs(this.playerEntity.motionX));
                double d8 = Math.max(Math.abs(d5), Math.abs(this.playerEntity.motionY));
                double d9 = Math.max(Math.abs(d6), Math.abs(this.playerEntity.motionZ));
                double d10 = d7 * d7 + d8 * d8 + d9 * d9;

                if (d10 > 100.0D && (!this.serverController.isSinglePlayer() || !this.serverController.getServerOwner().equals(this.playerEntity.getCommandSenderName())))
                {
                    logger.warn(this.playerEntity.getCommandSenderName() + " moved too quickly! " + d4 + "," + d5 + "," + d6 + " (" + d7 + ", " + d8 + ", " + d9 + ")");
                    this.setPlayerLocation(this.lastPosX, this.lastPosY, this.lastPosZ, this.playerEntity.rotationYaw, this.playerEntity.rotationPitch);
                    return;
                }

                float f3 = 0.0625F;
                boolean flag = worldserver.getCollidingBoundingBoxes(this.playerEntity, this.playerEntity.boundingBox.copy().contract((double)f3, (double)f3, (double)f3)).isEmpty();

                if (this.playerEntity.onGround && !p_147347_1_.func_149465_i() && d5 > 0.0D)
                {
                    this.playerEntity.jump();
                }

                if (!this.hasMoved) //Fixes "Moved Too Fast" kick when being teleported while moving
                {
                    return;
                }

                this.playerEntity.moveEntity(d4, d5, d6);
                this.playerEntity.onGround = p_147347_1_.func_149465_i();
                this.playerEntity.addMovementStat(d4, d5, d6);
                double d11 = d5;
                d4 = d1 - this.playerEntity.posX;
                d5 = d2 - this.playerEntity.posY;

                if (d5 > -0.5D || d5 < 0.5D)
                {
                    d5 = 0.0D;
                }

                d6 = d3 - this.playerEntity.posZ;
                d10 = d4 * d4 + d5 * d5 + d6 * d6;
                boolean flag1 = false;

                if (d10 > 0.0625D && !this.playerEntity.isPlayerSleeping() && !this.playerEntity.theItemInWorldManager.isCreative())
                {
                    flag1 = true;
                    logger.warn(this.playerEntity.getCommandSenderName() + " moved wrongly!");
                }

                if (!this.hasMoved) //Fixes "Moved Too Fast" kick when being teleported while moving
                {
                    return;
                }

                this.playerEntity.setPositionAndRotation(d1, d2, d3, f1, f2);
                boolean flag2 = worldserver.getCollidingBoundingBoxes(this.playerEntity, this.playerEntity.boundingBox.copy().contract((double)f3, (double)f3, (double)f3)).isEmpty();

                if (flag && (flag1 || !flag2) && !this.playerEntity.isPlayerSleeping() && !this.playerEntity.noClip)
                {
                    this.setPlayerLocation(this.lastPosX, this.lastPosY, this.lastPosZ, f1, f2);
                    return;
                }

                AxisAlignedBB axisalignedbb = this.playerEntity.boundingBox.copy().expand((double)f3, (double)f3, (double)f3).addCoord(0.0D, -0.55D, 0.0D);

                if (!this.serverController.isFlightAllowed() && !this.playerEntity.theItemInWorldManager.isCreative() && !worldserver.checkBlockCollision(axisalignedbb) && !this.playerEntity.capabilities.allowFlying)
                {
                    if (d11 >= -0.03125D)
                    {
                        ++this.floatingTickCount;

                        if (this.floatingTickCount > 80)
                        {
                            logger.warn(this.playerEntity.getCommandSenderName() + " was kicked for floating too long!");
                            this.kickPlayerFromServer("Flying is not enabled on this server");
                            return;
                        }
                    }
                }
                else
                {
                    this.floatingTickCount = 0;
                }

                if (!this.hasMoved) //Fixes "Moved Too Fast" kick when being teleported while moving
                {
                    return;
                }

                this.playerEntity.onGround = p_147347_1_.func_149465_i();
                this.serverController.getConfigurationManager().updatePlayerPertinentChunks(this.playerEntity);
                this.playerEntity.handleFalling(this.playerEntity.posY - d0, p_147347_1_.func_149465_i());
            }
            else if (this.networkTickCount % 20 == 0)
            {
                this.setPlayerLocation(this.lastPosX, this.lastPosY, this.lastPosZ, this.playerEntity.rotationYaw, this.playerEntity.rotationPitch);
            }
        }
    }

    /**
     * Moves the player to the specified destination and rotation
     */
    public void setPlayerLocation(double p_147364_1_, double p_147364_3_, double p_147364_5_, float p_147364_7_, float p_147364_8_)
    {
        this.hasMoved = false;
        this.lastPosX = p_147364_1_;
        this.lastPosY = p_147364_3_;
        this.lastPosZ = p_147364_5_;
        this.playerEntity.setPositionAndRotation(p_147364_1_, p_147364_3_, p_147364_5_, p_147364_7_, p_147364_8_);
        this.playerEntity.playerNetServerHandler.sendPacket(new S08PacketPlayerPosLook(p_147364_1_, p_147364_3_ + 1.6200000047683716D, p_147364_5_, p_147364_7_, p_147364_8_, false));
    }

    /**
     * Processes the player initiating/stopping digging on a particular spot, as well as a player dropping items?. (0:
     * initiated, 1: reinitiated, 2? , 3-4 drop item (respectively without or with player control), 5: stopped; x,y,z,
     * side clicked on;)
     */
    public void processPlayerDigging(C07PacketPlayerDigging p_147345_1_)
    {
        WorldServer worldserver = this.serverController.worldServerForDimension(this.playerEntity.dimension);
        this.playerEntity.func_143004_u();

        if (p_147345_1_.func_149506_g() == 4)
        {
            this.playerEntity.dropOneItem(false);
        }
        else if (p_147345_1_.func_149506_g() == 3)
        {
            this.playerEntity.dropOneItem(true);
        }
        else if (p_147345_1_.func_149506_g() == 5)
        {
            this.playerEntity.stopUsingItem();
        }
        else
        {
            boolean flag = false;

            if (p_147345_1_.func_149506_g() == 0)
            {
                flag = true;
            }

            if (p_147345_1_.func_149506_g() == 1)
            {
                flag = true;
            }

            if (p_147345_1_.func_149506_g() == 2)
            {
                flag = true;
            }

            int i = p_147345_1_.func_149505_c();
            int j = p_147345_1_.func_149503_d();
            int k = p_147345_1_.func_149502_e();

            if (flag)
            {
                double d0 = this.playerEntity.posX - ((double)i + 0.5D);
                double d1 = this.playerEntity.posY - ((double)j + 0.5D) + 1.5D;
                double d2 = this.playerEntity.posZ - ((double)k + 0.5D);
                double d3 = d0 * d0 + d1 * d1 + d2 * d2;

                double dist = playerEntity.theItemInWorldManager.getBlockReachDistance() + 1;
                dist *= dist;

                if (d3 > dist)
                {
                    return;
                }

                if (j >= this.serverController.getBuildLimit())
                {
                    return;
                }
            }

            if (p_147345_1_.func_149506_g() == 0)
            {
                if (!this.serverController.isBlockProtected(worldserver, i, j, k, this.playerEntity))
                {
                    this.playerEntity.theItemInWorldManager.onBlockClicked(i, j, k, p_147345_1_.func_149501_f());
                }
                else
                {
                    this.playerEntity.playerNetServerHandler.sendPacket(new S23PacketBlockChange(i, j, k, worldserver));
                }
            }
            else if (p_147345_1_.func_149506_g() == 2)
            {
                this.playerEntity.theItemInWorldManager.uncheckedTryHarvestBlock(i, j, k);

                if (worldserver.getBlock(i, j, k).getMaterial() != Material.air)
                {
                    this.playerEntity.playerNetServerHandler.sendPacket(new S23PacketBlockChange(i, j, k, worldserver));
                }
            }
            else if (p_147345_1_.func_149506_g() == 1)
            {
                this.playerEntity.theItemInWorldManager.cancelDestroyingBlock(i, j, k);

                if (worldserver.getBlock(i, j, k).getMaterial() != Material.air)
                {
                    this.playerEntity.playerNetServerHandler.sendPacket(new S23PacketBlockChange(i, j, k, worldserver));
                }
            }
        }
    }

    /**
     * Processes block placement and block activation (anvil, furnace, etc.)
     */
    public void processPlayerBlockPlacement(C08PacketPlayerBlockPlacement p_147346_1_)
    {
        WorldServer worldserver = this.serverController.worldServerForDimension(this.playerEntity.dimension);
        ItemStack itemstack = this.playerEntity.inventory.getCurrentItem();
        boolean flag = false;
        boolean placeResult = true;
        int i = p_147346_1_.func_149576_c();
        int j = p_147346_1_.func_149571_d();
        int k = p_147346_1_.func_149570_e();
        int l = p_147346_1_.func_149568_f();
        this.playerEntity.func_143004_u();

        if (p_147346_1_.func_149568_f() == 255)
        {
            if (itemstack == null)
            {
                return;
            }

            PlayerInteractEvent event = ForgeEventFactory.onPlayerInteract(playerEntity, PlayerInteractEvent.Action.RIGHT_CLICK_AIR, 0, 0, 0, -1, worldserver);
            if (event.useItem != Event.Result.DENY)
            {
                this.playerEntity.theItemInWorldManager.tryUseItem(this.playerEntity, worldserver, itemstack);
            }
        }
        else if (p_147346_1_.func_149571_d() >= this.serverController.getBuildLimit() - 1 && (p_147346_1_.func_149568_f() == 1 || p_147346_1_.func_149571_d() >= this.serverController.getBuildLimit()))
        {
            ChatComponentTranslation chatcomponenttranslation = new ChatComponentTranslation("build.tooHigh", new Object[] {Integer.valueOf(this.serverController.getBuildLimit())});
            chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.RED);
            this.playerEntity.playerNetServerHandler.sendPacket(new S02PacketChat(chatcomponenttranslation));
            flag = true;
        }
        else
        {
            double dist = playerEntity.theItemInWorldManager.getBlockReachDistance() + 1;
            dist *= dist;
            if (this.hasMoved && this.playerEntity.getDistanceSq((double)i + 0.5D, (double)j + 0.5D, (double)k + 0.5D) < dist && !this.serverController.isBlockProtected(worldserver, i, j, k, this.playerEntity))
            {
                // record block place result so we can update client itemstack size if place event was cancelled.
                if (!this.playerEntity.theItemInWorldManager.activateBlockOrUseItem(this.playerEntity, worldserver, itemstack, i, j, k, l, p_147346_1_.func_149573_h(), p_147346_1_.func_149569_i(), p_147346_1_.func_149575_j()))
                {
                    placeResult = false;
                }
            }

            flag = true;
        }

        if (flag)
        {
            this.playerEntity.playerNetServerHandler.sendPacket(new S23PacketBlockChange(i, j, k, worldserver));

            if (l == 0)
            {
                --j;
            }

            if (l == 1)
            {
                ++j;
            }

            if (l == 2)
            {
                --k;
            }

            if (l == 3)
            {
                ++k;
            }

            if (l == 4)
            {
                --i;
            }

            if (l == 5)
            {
                ++i;
            }

            this.playerEntity.playerNetServerHandler.sendPacket(new S23PacketBlockChange(i, j, k, worldserver));
        }

        itemstack = this.playerEntity.inventory.getCurrentItem();

        if (itemstack != null && itemstack.stackSize == 0)
        {
            this.playerEntity.inventory.mainInventory[this.playerEntity.inventory.currentItem] = null;
            itemstack = null;
        }

        if (itemstack == null || itemstack.getMaxItemUseDuration() == 0)
        {
            this.playerEntity.isChangingQuantityOnly = true;
            this.playerEntity.inventory.mainInventory[this.playerEntity.inventory.currentItem] = ItemStack.copyItemStack(this.playerEntity.inventory.mainInventory[this.playerEntity.inventory.currentItem]);
            Slot slot = this.playerEntity.openContainer.getSlotFromInventory(this.playerEntity.inventory, this.playerEntity.inventory.currentItem);
            this.playerEntity.openContainer.detectAndSendChanges();
            this.playerEntity.isChangingQuantityOnly = false;

            if (!ItemStack.areItemStacksEqual(this.playerEntity.inventory.getCurrentItem(), p_147346_1_.func_149574_g()) || !placeResult) // force client itemstack update if place event was cancelled
            {
                this.sendPacket(new S2FPacketSetSlot(this.playerEntity.openContainer.windowId, slot.slotNumber, this.playerEntity.inventory.getCurrentItem()));
            }
        }
    }

    /**
     * Invoked when disconnecting, the parameter is a ChatComponent describing the reason for termination
     */
    public void onDisconnect(IChatComponent p_147231_1_)
    {
        logger.info(this.playerEntity.getCommandSenderName() + " lost connection: " + p_147231_1_);
        this.serverController.func_147132_au();
        ChatComponentTranslation chatcomponenttranslation = new ChatComponentTranslation("multiplayer.player.left", new Object[] {this.playerEntity.func_145748_c_()});
        chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.YELLOW);
        this.serverController.getConfigurationManager().sendChatMsg(chatcomponenttranslation);
        this.playerEntity.mountEntityAndWakeUp();
        this.serverController.getConfigurationManager().playerLoggedOut(this.playerEntity);

        if (this.serverController.isSinglePlayer() && this.playerEntity.getCommandSenderName().equals(this.serverController.getServerOwner()))
        {
            logger.info("Stopping singleplayer server as player logged out");
            this.serverController.initiateShutdown();
        }
    }

    public void sendPacket(final Packet p_147359_1_)
    {
        if (p_147359_1_ instanceof S02PacketChat)
        {
            S02PacketChat s02packetchat = (S02PacketChat)p_147359_1_;
            EntityPlayer.EnumChatVisibility enumchatvisibility = this.playerEntity.func_147096_v();

            if (enumchatvisibility == EntityPlayer.EnumChatVisibility.HIDDEN)
            {
                return;
            }

            if (enumchatvisibility == EntityPlayer.EnumChatVisibility.SYSTEM && !s02packetchat.func_148916_d())
            {
                return;
            }
        }

        try
        {
            this.netManager.scheduleOutboundPacket(p_147359_1_, new GenericFutureListener[0]);
        }
        catch (Throwable throwable)
        {
            CrashReport crashreport = CrashReport.makeCrashReport(throwable, "Sending packet");
            CrashReportCategory crashreportcategory = crashreport.makeCategory("Packet being sent");
            crashreportcategory.addCrashSectionCallable("Packet class", new Callable()
            {
                private static final String __OBFID = "CL_00001454";
                public String call()
                {
                    return p_147359_1_.getClass().getCanonicalName();
                }
            });
            throw new ReportedException(crashreport);
        }
    }

    /**
     * Updates which quickbar slot is selected
     */
    public void processHeldItemChange(C09PacketHeldItemChange p_147355_1_)
    {
        if (p_147355_1_.func_149614_c() >= 0 && p_147355_1_.func_149614_c() < InventoryPlayer.getHotbarSize())
        {
            this.playerEntity.inventory.currentItem = p_147355_1_.func_149614_c();
            this.playerEntity.func_143004_u();
        }
        else
        {
            logger.warn(this.playerEntity.getCommandSenderName() + " tried to set an invalid carried item");
        }
    }

    /**
     * Process chat messages (broadcast back to clients) and commands (executes)
     */
    public void processChatMessage(C01PacketChatMessage p_147354_1_)
    {
        if (this.playerEntity.func_147096_v() == EntityPlayer.EnumChatVisibility.HIDDEN)
        {
            ChatComponentTranslation chatcomponenttranslation = new ChatComponentTranslation("chat.cannotSend", new Object[0]);
            chatcomponenttranslation.getChatStyle().setColor(EnumChatFormatting.RED);
            this.sendPacket(new S02PacketChat(chatcomponenttranslation));
        }
        else
        {
            this.playerEntity.func_143004_u();
            String s = p_147354_1_.func_149439_c();
            s = StringUtils.normalizeSpace(s);

            for (int i = 0; i < s.length(); ++i)
            {
                if (!ChatAllowedCharacters.isAllowedCharacter(s.charAt(i)))
                {
                    this.kickPlayerFromServer("Illegal characters in chat");
                    return;
                }
            }

            if (s.startsWith("/"))
            {
                this.handleSlashCommand(s);
            }
            else
            {
                ChatComponentTranslation chatcomponenttranslation1 = new ChatComponentTranslation("chat.type.text", new Object[] {this.playerEntity.func_145748_c_(), ForgeHooks.newChatWithLinks(s)}); // Fixes chat links
                chatcomponenttranslation1 = ForgeHooks.onServerChatEvent(this, s, chatcomponenttranslation1);
                if (chatcomponenttranslation1 == null) return;
                this.serverController.getConfigurationManager().sendChatMsgImpl(chatcomponenttranslation1, false);
            }

            this.chatSpamThresholdCount += 20;

            if (this.chatSpamThresholdCount > 200 && !this.serverController.getConfigurationManager().func_152596_g(this.playerEntity.getGameProfile()))
            {
                this.kickPlayerFromServer("disconnect.spam");
            }
        }
    }

    /**
     * Handle commands that start with a /
     */
    private void handleSlashCommand(String p_147361_1_)
    {
        this.serverController.getCommandManager().executeCommand(this.playerEntity, p_147361_1_);
    }

    /**
     * Processes the player swinging its held item
     */
    public void processAnimation(C0APacketAnimation p_147350_1_)
    {
        this.playerEntity.func_143004_u();

        if (p_147350_1_.func_149421_d() == 1)
        {
            this.playerEntity.swingItem();
        }
    }

    /**
     * Processes a range of action-types: sneaking, sprinting, waking from sleep, opening the inventory or setting jump
     * height of the horse the player is riding
     */
    public void processEntityAction(C0BPacketEntityAction p_147357_1_)
    {
        this.playerEntity.func_143004_u();

        if (p_147357_1_.func_149513_d() == 1)
        {
            this.playerEntity.setSneaking(true);
        }
        else if (p_147357_1_.func_149513_d() == 2)
        {
            this.playerEntity.setSneaking(false);
        }
        else if (p_147357_1_.func_149513_d() == 4)
        {
            this.playerEntity.setSprinting(true);
        }
        else if (p_147357_1_.func_149513_d() == 5)
        {
            this.playerEntity.setSprinting(false);
        }
        else if (p_147357_1_.func_149513_d() == 3)
        {
            this.playerEntity.wakeUpPlayer(false, true, true);
            this.hasMoved = false;
        }
        else if (p_147357_1_.func_149513_d() == 6)
        {
            // stripped: EntityHorse jump power
        }
        else if (false) // stripped: EntityHorse open GUI
        {
        }
    }

    /**
     * Processes interactions ((un)leashing, opening command block GUI) and attacks on an entity with players currently
     * equipped item
     */
    public void processUseEntity(C02PacketUseEntity p_147340_1_)
    {
        WorldServer worldserver = this.serverController.worldServerForDimension(this.playerEntity.dimension);
        Entity entity = p_147340_1_.func_149564_a(worldserver);
        this.playerEntity.func_143004_u();

        if (entity != null)
        {
            boolean flag = this.playerEntity.canEntityBeSeen(entity);
            double d0 = 36.0D;

            if (!flag)
            {
                d0 = 9.0D;
            }

            if (this.playerEntity.getDistanceSqToEntity(entity) < d0)
            {
                if (p_147340_1_.func_149565_c() == C02PacketUseEntity.Action.INTERACT)
                {
                    this.playerEntity.interactWith(entity);
                }
                else if (p_147340_1_.func_149565_c() == C02PacketUseEntity.Action.ATTACK)
                {
                    if (entity instanceof EntityItem || entity instanceof EntityXPOrb || entity instanceof EntityArrow || entity == this.playerEntity)
                    {
                        this.kickPlayerFromServer("Attempting to attack an invalid entity");
                        this.serverController.logWarning("Player " + this.playerEntity.getCommandSenderName() + " tried to attack an invalid entity");
                        return;
                    }

                    this.playerEntity.attackTargetEntityWithCurrentItem(entity);
                }
            }
        }
    }

    /**
     * Processes the client status updates: respawn attempt from player, opening statistics or achievements, or
     * acquiring 'open inventory' achievement
     */
    public void processClientStatus(C16PacketClientStatus p_147342_1_)
    {
        this.playerEntity.func_143004_u();
        C16PacketClientStatus.EnumState enumstate = p_147342_1_.func_149435_c();

        switch (NetHandlerPlayServer.SwitchEnumState.field_151290_a[enumstate.ordinal()])
        {
            case 1:
                if (this.playerEntity.playerConqueredTheEnd)
                {
                    this.playerEntity = this.serverController.getConfigurationManager().respawnPlayer(this.playerEntity, 0, true);
                }
                else if (this.playerEntity.getServerForPlayer().getWorldInfo().isHardcoreModeEnabled())
                {
                    if (this.serverController.isSinglePlayer() && this.playerEntity.getCommandSenderName().equals(this.serverController.getServerOwner()))
                    {
                        this.playerEntity.playerNetServerHandler.kickPlayerFromServer("You have died. Game over, man, it\'s game over!");
                        this.serverController.deleteWorldAndStopServer();
                    }
                    else
                    {
                        UserListBansEntry userlistbansentry = new UserListBansEntry(this.playerEntity.getGameProfile(), (Date)null, "(You just lost the game)", (Date)null, "Death in Hardcore");
                        this.serverController.getConfigurationManager().func_152608_h().func_152687_a(userlistbansentry);
                        this.playerEntity.playerNetServerHandler.kickPlayerFromServer("You have died. Game over, man, it\'s game over!");
                    }
                }
                else
                {
                    if (this.playerEntity.getHealth() > 0.0F)
                    {
                        return;
                    }

                    this.playerEntity = this.serverController.getConfigurationManager().respawnPlayer(this.playerEntity, playerEntity.dimension, false);
                }

                break;
            case 2:
                this.playerEntity.func_147099_x().func_150876_a(this.playerEntity);
                break;
            case 3:
                this.playerEntity.triggerAchievement(AchievementList.openInventory);
        }
    }

    /**
     * Processes the client closing windows (container)
     */
    public void processCloseWindow(C0DPacketCloseWindow p_147356_1_)
    {
        this.playerEntity.closeContainer();
    }

    /**
     * Executes a container/inventory slot manipulation as indicated by the packet. Sends the serverside result if they
     * didn't match the indicated result and prevents further manipulation by the player until he confirms that it has
     * the same open container/inventory
     */
    public void processClickWindow(C0EPacketClickWindow p_147351_1_)
    {
        this.playerEntity.func_143004_u();

        if (this.playerEntity.openContainer.windowId == p_147351_1_.func_149548_c() && this.playerEntity.openContainer.isPlayerNotUsingContainer(this.playerEntity))
        {
            ItemStack itemstack = this.playerEntity.openContainer.slotClick(p_147351_1_.func_149544_d(), p_147351_1_.func_149543_e(), p_147351_1_.func_149542_h(), this.playerEntity);

            if (ItemStack.areItemStacksEqual(p_147351_1_.func_149546_g(), itemstack))
            {
                this.playerEntity.playerNetServerHandler.sendPacket(new S32PacketConfirmTransaction(p_147351_1_.func_149548_c(), p_147351_1_.func_149547_f(), true));
                this.playerEntity.isChangingQuantityOnly = true;
                this.playerEntity.openContainer.detectAndSendChanges();
                this.playerEntity.updateHeldItem();
                this.playerEntity.isChangingQuantityOnly = false;
            }
            else
            {
                this.field_147372_n.addKey(this.playerEntity.openContainer.windowId, Short.valueOf(p_147351_1_.func_149547_f()));
                this.playerEntity.playerNetServerHandler.sendPacket(new S32PacketConfirmTransaction(p_147351_1_.func_149548_c(), p_147351_1_.func_149547_f(), false));
                this.playerEntity.openContainer.setPlayerIsPresent(this.playerEntity, false);
                ArrayList arraylist = new ArrayList();

                for (int i = 0; i < this.playerEntity.openContainer.inventorySlots.size(); ++i)
                {
                    arraylist.add(((Slot)this.playerEntity.openContainer.inventorySlots.get(i)).getStack());
                }

                this.playerEntity.sendContainerAndContentsToPlayer(this.playerEntity.openContainer, arraylist);
            }
        }
    }

    /**
     * Enchants the item identified by the packet given some convoluted conditions (matching window, which
     * should/shouldn't be in use?)
     */
    public void processEnchantItem(C11PacketEnchantItem p_147338_1_)
    {
        this.playerEntity.func_143004_u();

        if (this.playerEntity.openContainer.windowId == p_147338_1_.func_149539_c() && this.playerEntity.openContainer.isPlayerNotUsingContainer(this.playerEntity))
        {
            this.playerEntity.openContainer.enchantItem(this.playerEntity, p_147338_1_.func_149537_d());
            this.playerEntity.openContainer.detectAndSendChanges();
        }
    }

    /**
     * Update the server with an ItemStack in a slot.
     */
    public void processCreativeInventoryAction(C10PacketCreativeInventoryAction p_147344_1_)
    {
        if (this.playerEntity.theItemInWorldManager.isCreative())
        {
            boolean flag = p_147344_1_.func_149627_c() < 0;
            ItemStack itemstack = p_147344_1_.func_149625_d();
            boolean flag1 = p_147344_1_.func_149627_c() >= 1 && p_147344_1_.func_149627_c() < 36 + InventoryPlayer.getHotbarSize();
            boolean flag2 = itemstack == null || itemstack.getItem() != null;
            boolean flag3 = itemstack == null || itemstack.getItemDamage() >= 0 && itemstack.stackSize <= 64 && itemstack.stackSize > 0;

            if (flag1 && flag2 && flag3)
            {
                if (itemstack == null)
                {
                    this.playerEntity.inventoryContainer.putStackInSlot(p_147344_1_.func_149627_c(), (ItemStack)null);
                }
                else
                {
                    this.playerEntity.inventoryContainer.putStackInSlot(p_147344_1_.func_149627_c(), itemstack);
                }

                this.playerEntity.inventoryContainer.setPlayerIsPresent(this.playerEntity, true);
            }
            else if (flag && flag2 && flag3 && this.field_147375_m < 200)
            {
                this.field_147375_m += 20;
                EntityItem entityitem = this.playerEntity.dropPlayerItemWithRandomChoice(itemstack, true);

                if (entityitem != null)
                {
                    entityitem.setAgeToCreativeDespawnTime();
                }
            }
        }
    }

    /**
     * Received in response to the server requesting to confirm that the client-side open container matches the servers'
     * after a mismatched container-slot manipulation. It will unlock the player's ability to manipulate the container
     * contents
     */
    public void processConfirmTransaction(C0FPacketConfirmTransaction p_147339_1_)
    {
        Short oshort = (Short)this.field_147372_n.lookup(this.playerEntity.openContainer.windowId);

        if (oshort != null && p_147339_1_.func_149533_d() == oshort.shortValue() && this.playerEntity.openContainer.windowId == p_147339_1_.func_149532_c() && !this.playerEntity.openContainer.isPlayerNotUsingContainer(this.playerEntity))
        {
            this.playerEntity.openContainer.setPlayerIsPresent(this.playerEntity, true);
        }
    }

    public void processUpdateSign(C12PacketUpdateSign p_147343_1_)
    {
        this.playerEntity.func_143004_u();
        WorldServer worldserver = this.serverController.worldServerForDimension(this.playerEntity.dimension);

        if (worldserver.blockExists(p_147343_1_.func_149588_c(), p_147343_1_.func_149586_d(), p_147343_1_.func_149585_e()))
        {
            TileEntity tileentity = worldserver.getTileEntity(p_147343_1_.func_149588_c(), p_147343_1_.func_149586_d(), p_147343_1_.func_149585_e());

            if (tileentity instanceof TileEntitySign)
            {
                TileEntitySign tileentitysign = (TileEntitySign)tileentity;

                if (!tileentitysign.func_145914_a() || tileentitysign.func_145911_b() != this.playerEntity)
                {
                    this.serverController.logWarning("Player " + this.playerEntity.getCommandSenderName() + " just tried to change non-editable sign");
                    return;
                }
            }

            int i;
            int j;

            for (j = 0; j < 4; ++j)
            {
                boolean flag = true;

                if (p_147343_1_.func_149589_f()[j].length() > 15)
                {
                    flag = false;
                }
                else
                {
                    for (i = 0; i < p_147343_1_.func_149589_f()[j].length(); ++i)
                    {
                        if (!ChatAllowedCharacters.isAllowedCharacter(p_147343_1_.func_149589_f()[j].charAt(i)))
                        {
                            flag = false;
                        }
                    }
                }

                if (!flag)
                {
                    p_147343_1_.func_149589_f()[j] = "!?";
                }
            }

            if (tileentity instanceof TileEntitySign)
            {
                j = p_147343_1_.func_149588_c();
                int k = p_147343_1_.func_149586_d();
                i = p_147343_1_.func_149585_e();
                TileEntitySign tileentitysign1 = (TileEntitySign)tileentity;
                System.arraycopy(p_147343_1_.func_149589_f(), 0, tileentitysign1.signText, 0, 4);
                tileentitysign1.markDirty();
                worldserver.markBlockForUpdate(j, k, i);
            }
        }
    }

    /**
     * Updates a players' ping statistics
     */
    public void processKeepAlive(C00PacketKeepAlive p_147353_1_)
    {
        if (p_147353_1_.func_149460_c() == this.field_147378_h)
        {
            int i = (int)(this.func_147363_d() - this.field_147379_i);
            this.playerEntity.ping = (this.playerEntity.ping * 3 + i) / 4;
        }
    }

    private long func_147363_d()
    {
        return System.nanoTime() / 1000000L;
    }

    /**
     * Processes a player starting/stopping flying
     */
    public void processPlayerAbilities(C13PacketPlayerAbilities p_147348_1_)
    {
        this.playerEntity.capabilities.isFlying = p_147348_1_.func_149488_d() && this.playerEntity.capabilities.allowFlying;
    }

    /**
     * Retrieves possible tab completions for the requested command string and sends them to the client
     */
    public void processTabComplete(C14PacketTabComplete p_147341_1_)
    {
        ArrayList arraylist = Lists.newArrayList();
        Iterator iterator = this.serverController.getPossibleCompletions(this.playerEntity, p_147341_1_.func_149419_c()).iterator();

        while (iterator.hasNext())
        {
            String s = (String)iterator.next();
            arraylist.add(s);
        }

        this.playerEntity.playerNetServerHandler.sendPacket(new S3APacketTabComplete((String[])arraylist.toArray(new String[arraylist.size()])));
    }

    /**
     * Updates serverside copy of client settings: language, render distance, chat visibility, chat colours, difficulty,
     * and whether to show the cape
     */
    public void processClientSettings(C15PacketClientSettings p_147352_1_)
    {
        this.playerEntity.func_147100_a(p_147352_1_);
    }

    /**
     * Synchronizes serverside and clientside book contents and signing
     */
    public void processVanilla250Packet(C17PacketCustomPayload p_147349_1_)
    {
        PacketBuffer packetbuffer;
        ItemStack itemstack;
        ItemStack itemstack1;

        if ("MC|BEdit".equals(p_147349_1_.func_149559_c()))
        {
            packetbuffer = new PacketBuffer(Unpooled.wrappedBuffer(p_147349_1_.func_149558_e()));

            try
            {
                itemstack = packetbuffer.readItemStackFromBuffer();

                if (itemstack == null)
                {
                    return;
                }

                if (!ItemWritableBook.func_150930_a(itemstack.getTagCompound()))
                {
                    throw new IOException("Invalid book tag!");
                }

                itemstack1 = this.playerEntity.inventory.getCurrentItem();

                if (itemstack1 != null)
                {
                    if (itemstack.getItem() == Items.writable_book && itemstack.getItem() == itemstack1.getItem())
                    {
                        itemstack1.setTagInfo("pages", itemstack.getTagCompound().getTagList("pages", 8));
                    }

                    return;
                }
            }
            catch (Exception exception4)
            {
                logger.error("Couldn\'t handle book info", exception4);
                return;
            }
            finally
            {
                packetbuffer.release();
            }

            return;
        }
        else if ("MC|BSign".equals(p_147349_1_.func_149559_c()))
        {
            packetbuffer = new PacketBuffer(Unpooled.wrappedBuffer(p_147349_1_.func_149558_e()));

            try
            {
                itemstack = packetbuffer.readItemStackFromBuffer();

                if (itemstack == null)
                {
                    return;
                }

                if (!ItemEditableBook.validBookTagContents(itemstack.getTagCompound()))
                {
                    throw new IOException("Invalid book tag!");
                }

                itemstack1 = this.playerEntity.inventory.getCurrentItem();

                if (itemstack1 != null)
                {
                    if (itemstack.getItem() == Items.written_book && itemstack1.getItem() == Items.writable_book)
                    {
                        itemstack1.setTagInfo("author", new NBTTagString(this.playerEntity.getCommandSenderName()));
                        itemstack1.setTagInfo("title", new NBTTagString(itemstack.getTagCompound().getString("title")));
                        itemstack1.setTagInfo("pages", itemstack.getTagCompound().getTagList("pages", 8));
                        itemstack1.func_150996_a(Items.written_book);
                    }

                    return;
                }
            }
            catch (Exception exception3)
            {
                logger.error("Couldn\'t sign book", exception3);
                return;
            }
            finally
            {
                packetbuffer.release();
            }

            return;
        }
        else
        {
            DataInputStream datainputstream;
            int i;

            if ("MC|TrSel".equals(p_147349_1_.func_149559_c()))
            {
                try
                {
                    datainputstream = new DataInputStream(new ByteArrayInputStream(p_147349_1_.func_149558_e()));
                    i = datainputstream.readInt();
                    Container container = this.playerEntity.openContainer;

                    // stripped: ContainerMerchant recipe index
                }
                catch (Exception exception2)
                {
                    logger.error("Couldn\'t select trade", exception2);
                }
            }
            else if ("MC|AdvCdm".equals(p_147349_1_.func_149559_c()))
            {
                if (!this.serverController.isCommandBlockEnabled())
                {
                    this.playerEntity.addChatMessage(new ChatComponentTranslation("advMode.notEnabled", new Object[0]));
                }
                else if (this.playerEntity.canCommandSenderUseCommand(2, "") && this.playerEntity.capabilities.isCreativeMode)
                {
                    packetbuffer = new PacketBuffer(Unpooled.wrappedBuffer(p_147349_1_.func_149558_e()));

                    try
                    {
                        byte b0 = packetbuffer.readByte();
                        CommandBlockLogic commandblocklogic = null;

                        if (b0 == 0)
                        {
                            TileEntity tileentity = this.playerEntity.worldObj.getTileEntity(packetbuffer.readInt(), packetbuffer.readInt(), packetbuffer.readInt());

                            // stripped: command block tile entity
                        }
                        else if (b0 == 1)
                        {
                            Entity entity = this.playerEntity.worldObj.getEntityByID(packetbuffer.readInt());

                            if (entity instanceof EntityMinecartCommandBlock)
                            {
                                commandblocklogic = ((EntityMinecartCommandBlock)entity).func_145822_e();
                            }
                        }

                        String s1 = packetbuffer.readStringFromBuffer(packetbuffer.readableBytes());

                        if (commandblocklogic != null)
                        {
                            commandblocklogic.func_145752_a(s1);
                            commandblocklogic.func_145756_e();
                            this.playerEntity.addChatMessage(new ChatComponentTranslation("advMode.setCommand.success", new Object[] {s1}));
                        }
                    }
                    catch (Exception exception1)
                    {
                        logger.error("Couldn\'t set command block", exception1);
                    }
                    finally
                    {
                        packetbuffer.release();
                    }
                }
                else
                {
                    this.playerEntity.addChatMessage(new ChatComponentTranslation("advMode.notAllowed", new Object[0]));
                }
            }
            // stripped: MC|Beacon and MC|ItemName (anvil rename) packet handlers
        }
    }

    /**
     * Allows validation of the connection state transition. Parameters: from, to (connection state). Typically throws
     * IllegalStateException or UnsupportedOperationException if validation fails
     */
    public void onConnectionStateTransition(EnumConnectionState p_147232_1_, EnumConnectionState p_147232_2_)
    {
        if (p_147232_2_ != EnumConnectionState.PLAY)
        {
            throw new IllegalStateException("Unexpected change in protocol!");
        }
    }

    static final class SwitchEnumState
        {
            static final int[] field_151290_a = new int[C16PacketClientStatus.EnumState.values().length];
            private static final String __OBFID = "CL_00001455";

            static
            {
                try
                {
                    field_151290_a[C16PacketClientStatus.EnumState.PERFORM_RESPAWN.ordinal()] = 1;
                }
                catch (NoSuchFieldError var3)
                {
                    ;
                }

                try
                {
                    field_151290_a[C16PacketClientStatus.EnumState.REQUEST_STATS.ordinal()] = 2;
                }
                catch (NoSuchFieldError var2)
                {
                    ;
                }

                try
                {
                    field_151290_a[C16PacketClientStatus.EnumState.OPEN_INVENTORY_ACHIEVEMENT.ordinal()] = 3;
                }
                catch (NoSuchFieldError var1)
                {
                    ;
                }
            }
        }
}