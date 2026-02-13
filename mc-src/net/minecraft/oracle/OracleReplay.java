package net.minecraft.oracle;

import java.io.DataInputStream;
import java.io.FileInputStream;
import java.io.IOException;
import com.mojang.authlib.GameProfile;
import io.netty.buffer.Unpooled;
import java.util.UUID;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.network.NetHandlerPlayServer;
import net.minecraft.network.NetworkManager;
import net.minecraft.network.PacketBuffer;
import net.minecraft.network.play.client.*;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.management.ItemInWorldManager;
import net.minecraft.world.WorldServer;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

/**
 * Replays a recorded oracle session by injecting actions into a headless player.
 * No real client connection needed -- creates a fake EntityPlayerMP with a dummy NetworkManager.
 *
 * Packet construction uses no-arg constructors + readPacketData(PacketBuffer) to avoid
 * @SideOnly(Side.CLIENT) constructors that are stripped on the dedicated server.
 */
public class OracleReplay
{
    private static final Logger logger = LogManager.getLogger();
    private static final int MAGIC = 0x4E524543; // "NREC"

    // Recording header
    private long worldSeed;
    private int startTick;
    private int totalActions;

    // Action buffer: all actions loaded upfront
    private int[] actionTicks;
    private int[] actionTypes;
    private byte[][] actionPayloads;
    private int actionIndex;

    // Replay state
    private boolean active;
    private EntityPlayerMP replayPlayer;
    private NetHandlerPlayServer replayHandler;

    private static OracleReplay INSTANCE;

    private OracleReplay() {}

    public static synchronized OracleReplay get()
    {
        if (INSTANCE == null)
        {
            INSTANCE = new OracleReplay();
        }
        return INSTANCE;
    }

    public boolean isActive()
    {
        return this.active;
    }

    public long getWorldSeed()
    {
        return this.worldSeed;
    }

    /**
     * Load a recording file into memory.
     */
    public boolean loadRecording(String path)
    {
        try
        {
            DataInputStream dis = new DataInputStream(new FileInputStream(path));

            int magic = dis.readInt();
            if (magic != MAGIC)
            {
                logger.error("[Oracle Replay] Invalid magic: 0x" + Integer.toHexString(magic));
                dis.close();
                return false;
            }

            int version = dis.readInt();
            if (version != 1)
            {
                logger.error("[Oracle Replay] Unsupported version: " + version);
                dis.close();
                return false;
            }

            this.worldSeed = dis.readLong();
            this.startTick = dis.readInt();
            this.totalActions = dis.readInt();
            dis.readLong(); // reserved

            logger.info("[Oracle Replay] Loading recording: seed=" + this.worldSeed + ", startTick=" + this.startTick + ", actions=" + this.totalActions);

            this.actionTicks = new int[this.totalActions];
            this.actionTypes = new int[this.totalActions];
            this.actionPayloads = new byte[this.totalActions][];

            for (int i = 0; i < this.totalActions; i++)
            {
                this.actionTicks[i] = dis.readInt();
                this.actionTypes[i] = dis.readUnsignedByte();
                int payloadLen = dis.readUnsignedShort();
                this.actionPayloads[i] = new byte[payloadLen];
                if (payloadLen > 0)
                {
                    dis.readFully(this.actionPayloads[i]);
                }
            }

            dis.close();
            this.actionIndex = 0;
            logger.info("[Oracle Replay] Recording loaded successfully");
            return true;
        }
        catch (IOException e)
        {
            logger.error("[Oracle Replay] Failed to load recording", e);
            return false;
        }
    }

    /**
     * Start replay mode. Must be called after world is loaded.
     * Creates a fake player and handler.
     */
    public void startReplay(MinecraftServer server)
    {
        WorldServer world = server.worldServerForDimension(0);
        if (world == null)
        {
            logger.error("[Oracle Replay] No overworld loaded");
            return;
        }

        // Create fake player
        GameProfile profile = new GameProfile(UUID.nameUUIDFromBytes("OracleReplayBot".getBytes()), "OracleReplayBot");
        ItemInWorldManager itemManager = new ItemInWorldManager(world);
        this.replayPlayer = new EntityPlayerMP(server, world, profile, itemManager);

        // Create dummy network manager (server-side, no real connection)
        NetworkManager netManager = new NetworkManager(false);
        this.replayHandler = new NetHandlerPlayServer(server, netManager, this.replayPlayer);

        // Add player to world via config manager
        server.getConfigurationManager().initializeConnectionToPlayer(netManager, this.replayPlayer, this.replayHandler);

        this.actionIndex = 0;
        this.active = true;
        logger.info("[Oracle Replay] Replay started, player spawned at (" + this.replayPlayer.posX + ", " + this.replayPlayer.posY + ", " + this.replayPlayer.posZ + ")");
    }

    /**
     * Called each server tick to inject actions for the current tick.
     */
    public void tickReplay(int currentTick)
    {
        if (!this.active || this.replayHandler == null) return;

        while (this.actionIndex < this.totalActions && this.actionTicks[this.actionIndex] <= currentTick)
        {
            injectAction(this.actionTypes[this.actionIndex], this.actionPayloads[this.actionIndex]);
            this.actionIndex++;
        }

        // Check if replay is done
        if (this.actionIndex >= this.totalActions)
        {
            logger.info("[Oracle Replay] All {} actions replayed, replay complete", this.totalActions);
            this.active = false;
        }
    }

    /**
     * Construct packets using no-arg constructor + readPacketData(PacketBuffer).
     * This avoids @SideOnly(Side.CLIENT) constructors that don't exist on dedicated server.
     */
    private void injectAction(int type, byte[] payload)
    {
        try
        {
            DataInputStream dis = new DataInputStream(new java.io.ByteArrayInputStream(payload));

            switch (type)
            {
                case OracleAction.PLAYER_GROUND:
                {
                    // C03PacketPlayer: just onGround byte
                    boolean onGround = dis.readByte() != 0;
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(1));
                    buf.writeByte(onGround ? 1 : 0);
                    C03PacketPlayer pkt = new C03PacketPlayer();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processPlayer(pkt);
                    break;
                }
                case OracleAction.PLAYER_POSITION:
                {
                    // C04PacketPlayerPosition: x, y, stance, z, onGround
                    double x = dis.readDouble();
                    double y = dis.readDouble();
                    double z = dis.readDouble();
                    double stance = dis.readDouble();
                    boolean onGround = dis.readByte() != 0;
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(33));
                    buf.writeDouble(x);
                    buf.writeDouble(y);
                    buf.writeDouble(stance);
                    buf.writeDouble(z);
                    buf.writeByte(onGround ? 1 : 0);
                    C03PacketPlayer.C04PacketPlayerPosition pkt = new C03PacketPlayer.C04PacketPlayerPosition();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processPlayer(pkt);
                    break;
                }
                case OracleAction.PLAYER_LOOK:
                {
                    // C05PacketPlayerLook: yaw, pitch, onGround
                    float yaw = dis.readFloat();
                    float pitch = dis.readFloat();
                    boolean onGround = dis.readByte() != 0;
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(9));
                    buf.writeFloat(yaw);
                    buf.writeFloat(pitch);
                    buf.writeByte(onGround ? 1 : 0);
                    C03PacketPlayer.C05PacketPlayerLook pkt = new C03PacketPlayer.C05PacketPlayerLook();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processPlayer(pkt);
                    break;
                }
                case OracleAction.PLAYER_POS_LOOK:
                {
                    // C06PacketPlayerPosLook: x, y, stance, z, yaw, pitch, onGround
                    double x = dis.readDouble();
                    double y = dis.readDouble();
                    double z = dis.readDouble();
                    double stance = dis.readDouble();
                    float yaw = dis.readFloat();
                    float pitch = dis.readFloat();
                    boolean onGround = dis.readByte() != 0;
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(41));
                    buf.writeDouble(x);
                    buf.writeDouble(y);
                    buf.writeDouble(stance);
                    buf.writeDouble(z);
                    buf.writeFloat(yaw);
                    buf.writeFloat(pitch);
                    buf.writeByte(onGround ? 1 : 0);
                    C03PacketPlayer.C06PacketPlayerPosLook pkt = new C03PacketPlayer.C06PacketPlayerPosLook();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processPlayer(pkt);
                    break;
                }
                case OracleAction.BLOCK_DIG:
                {
                    // C07: status(u8), x(i32), y(u8), z(i32), face(u8)
                    int status = dis.readUnsignedByte();
                    int bx = dis.readInt();
                    int by = dis.readUnsignedByte();
                    int bz = dis.readInt();
                    int face = dis.readUnsignedByte();
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(11));
                    buf.writeByte(status);
                    buf.writeInt(bx);
                    buf.writeByte(by);
                    buf.writeInt(bz);
                    buf.writeByte(face);
                    C07PacketPlayerDigging pkt = new C07PacketPlayerDigging();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processPlayerDigging(pkt);
                    break;
                }
                case OracleAction.BLOCK_PLACE:
                {
                    // C08: x(i32), y(u8), z(i32), face(u8), itemStack, cursorX/Y/Z
                    int bx = dis.readInt();
                    int by = dis.readUnsignedByte();
                    int bz = dis.readInt();
                    int face = dis.readUnsignedByte();
                    int itemId = dis.readShort();
                    float cx = dis.readFloat();
                    float cy = dis.readFloat();
                    float cz = dis.readFloat();
                    // Build PacketBuffer matching C08's readPacketData format:
                    // x(int), y(ubyte), z(int), face(ubyte), itemStack, cx/cy/cz (ubyte each, scaled by 16)
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(32));
                    buf.writeInt(bx);
                    buf.writeByte(by);
                    buf.writeInt(bz);
                    buf.writeByte(face);
                    // writeItemStackToBuffer: short itemId, then if >= 0: byte count, short damage, short nbt(-1=none)
                    if (itemId < 0)
                    {
                        buf.writeShort(-1);
                    }
                    else
                    {
                        buf.writeShort(itemId);
                        buf.writeByte(1); // stack size 1
                        buf.writeShort(0); // damage 0
                        buf.writeShort(-1); // no NBT
                    }
                    buf.writeByte((int)(cx * 16.0F));
                    buf.writeByte((int)(cy * 16.0F));
                    buf.writeByte((int)(cz * 16.0F));
                    C08PacketPlayerBlockPlacement pkt = new C08PacketPlayerBlockPlacement();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processPlayerBlockPlacement(pkt);
                    break;
                }
                case OracleAction.HELD_ITEM:
                {
                    // C09: slot(short)
                    int slot = dis.readShort();
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(2));
                    buf.writeShort(slot);
                    C09PacketHeldItemChange pkt = new C09PacketHeldItemChange();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processHeldItemChange(pkt);
                    break;
                }
                case OracleAction.ENTITY_ACTION:
                {
                    // C0B: entityId(int), actionId(byte), jumpBoost(int)
                    int actionId = dis.readUnsignedByte();
                    int jumpBoost = dis.readInt();
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(9));
                    buf.writeInt(this.replayPlayer.getEntityId());
                    buf.writeByte(actionId);
                    buf.writeInt(jumpBoost);
                    C0BPacketEntityAction pkt = new C0BPacketEntityAction();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processEntityAction(pkt);
                    break;
                }
                case OracleAction.USE_ENTITY:
                {
                    // C02: entityId(int), action(byte)
                    int targetId = dis.readInt();
                    int action = dis.readUnsignedByte();
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(5));
                    buf.writeInt(targetId);
                    buf.writeByte(action);
                    C02PacketUseEntity pkt = new C02PacketUseEntity();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processUseEntity(pkt);
                    break;
                }
                case OracleAction.PLAYER_INPUT:
                {
                    // C0C: strafe(float), forward(float), jump(bool), sneak(bool)
                    float strafe = dis.readFloat();
                    float forward = dis.readFloat();
                    boolean jump = dis.readByte() != 0;
                    boolean sneak = dis.readByte() != 0;
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(10));
                    buf.writeFloat(strafe);
                    buf.writeFloat(forward);
                    buf.writeBoolean(jump);
                    buf.writeBoolean(sneak);
                    C0CPacketInput pkt = new C0CPacketInput();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processInput(pkt);
                    break;
                }
                case OracleAction.CLICK_WINDOW:
                {
                    // C0E: windowId(byte), slot(short), button(byte), action(short), mode(byte), itemStack
                    int windowId = dis.readUnsignedByte();
                    int slot = dis.readShort();
                    int button = dis.readUnsignedByte();
                    short actionNum = dis.readShort();
                    int mode = dis.readUnsignedByte();
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(10));
                    buf.writeByte(windowId);
                    buf.writeShort(slot);
                    buf.writeByte(button);
                    buf.writeShort(actionNum);
                    buf.writeByte(mode);
                    buf.writeShort(-1); // null item stack
                    C0EPacketClickWindow pkt = new C0EPacketClickWindow();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processClickWindow(pkt);
                    break;
                }
                case OracleAction.CLOSE_WINDOW:
                {
                    // C0D: windowId(byte)
                    int windowId = dis.readUnsignedByte();
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(1));
                    buf.writeByte(windowId);
                    C0DPacketCloseWindow pkt = new C0DPacketCloseWindow();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processCloseWindow(pkt);
                    break;
                }
                case OracleAction.ANIMATION:
                {
                    // C0A: entityId(int), animationType(byte)
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(5));
                    buf.writeInt(this.replayPlayer.getEntityId());
                    buf.writeByte(1); // swing arm
                    C0APacketAnimation pkt = new C0APacketAnimation();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processAnimation(pkt);
                    break;
                }
                case OracleAction.CHAT:
                {
                    int msgLen = dis.readUnsignedShort();
                    byte[] msgBytes = new byte[msgLen];
                    dis.readFully(msgBytes);
                    String message = new String(msgBytes, "UTF-8");
                    // Skip chat/commands during replay to avoid side effects
                    logger.debug("[Oracle Replay] Skipping chat: {}", message);
                    break;
                }
                case OracleAction.CLIENT_STATUS:
                {
                    int statusAction = dis.readUnsignedByte();
                    // C16: action(varint -- but in 1.7.10 it's a byte)
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(1));
                    buf.writeByte(statusAction);
                    C16PacketClientStatus pkt = new C16PacketClientStatus();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processClientStatus(pkt);
                    break;
                }
                case OracleAction.PLAYER_ABILITIES:
                {
                    // C13: flags(byte), flySpeed(float), walkSpeed(float)
                    boolean flying = dis.readByte() != 0;
                    PacketBuffer buf = new PacketBuffer(Unpooled.buffer(9));
                    buf.writeByte(flying ? 0x02 : 0x00); // bit 1 = isFlying
                    buf.writeFloat(0.05F); // default fly speed
                    buf.writeFloat(0.1F);  // default walk speed
                    C13PacketPlayerAbilities pkt = new C13PacketPlayerAbilities();
                    pkt.readPacketData(buf);
                    buf.release();
                    this.replayHandler.processPlayerAbilities(pkt);
                    break;
                }
                default:
                    logger.warn("[Oracle Replay] Unknown action type: 0x" + Integer.toHexString(type));
            }
        }
        catch (IOException e)
        {
            logger.error("[Oracle Replay] Error injecting action type 0x" + Integer.toHexString(type), e);
        }
    }
}
