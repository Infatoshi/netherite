package net.minecraft.oracle;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;

/**
 * Action type IDs and payload serialization for oracle recordings.
 * Binary format is C-portable: fixed-size fields, no Java serialization.
 */
public class OracleAction
{
    // Action type IDs
    public static final int PLAYER_GROUND     = 0x01;
    public static final int PLAYER_POSITION   = 0x02;
    public static final int PLAYER_LOOK       = 0x03;
    public static final int PLAYER_POS_LOOK   = 0x04;
    public static final int BLOCK_DIG         = 0x05;
    public static final int BLOCK_PLACE       = 0x06;
    public static final int HELD_ITEM         = 0x07;
    public static final int ENTITY_ACTION     = 0x08;
    public static final int USE_ENTITY        = 0x09;
    public static final int PLAYER_INPUT      = 0x0A;
    public static final int CLICK_WINDOW      = 0x0B;
    public static final int CLOSE_WINDOW      = 0x0C;
    public static final int ANIMATION         = 0x0D;
    public static final int CHAT              = 0x0E;
    public static final int CLIENT_STATUS     = 0x0F;
    public static final int PLAYER_ABILITIES  = 0x10;

    // Payload builders -- each returns a byte[] ready to write

    public static byte[] playerGround(boolean onGround)
    {
        return new byte[] { (byte)(onGround ? 1 : 0) };
    }

    public static byte[] playerPosition(double x, double y, double z, double stance, boolean onGround)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(33);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeDouble(x);
            dos.writeDouble(y);
            dos.writeDouble(z);
            dos.writeDouble(stance);
            dos.writeByte(onGround ? 1 : 0);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] playerLook(float yaw, float pitch, boolean onGround)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(9);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeFloat(yaw);
            dos.writeFloat(pitch);
            dos.writeByte(onGround ? 1 : 0);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] playerPosLook(double x, double y, double z, double stance,
                                        float yaw, float pitch, boolean onGround)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(41);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeDouble(x);
            dos.writeDouble(y);
            dos.writeDouble(z);
            dos.writeDouble(stance);
            dos.writeFloat(yaw);
            dos.writeFloat(pitch);
            dos.writeByte(onGround ? 1 : 0);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] blockDig(int status, int x, int y, int z, int face)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(12);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeByte(status);
            dos.writeInt(x);
            dos.writeByte(y);
            dos.writeInt(z);
            dos.writeByte(face);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] blockPlace(int x, int y, int z, int face, int itemId,
                                     float cursorX, float cursorY, float cursorZ)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(24);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeInt(x);
            dos.writeByte(y);
            dos.writeInt(z);
            dos.writeByte(face);
            dos.writeShort(itemId);
            dos.writeFloat(cursorX);
            dos.writeFloat(cursorY);
            dos.writeFloat(cursorZ);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] heldItem(int slot)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(2);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeShort(slot);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] entityAction(int actionId, int jumpBoost)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(5);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeByte(actionId);
            dos.writeInt(jumpBoost);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] useEntity(int targetEntityId, int action)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(5);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeInt(targetEntityId);
            dos.writeByte(action);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] playerInput(float strafe, float forward, boolean jump, boolean sneak)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(10);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeFloat(strafe);
            dos.writeFloat(forward);
            dos.writeByte(jump ? 1 : 0);
            dos.writeByte(sneak ? 1 : 0);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] clickWindow(int windowId, int slot, int button, short action, int mode)
    {
        try
        {
            ByteArrayOutputStream baos = new ByteArrayOutputStream(7);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeByte(windowId);
            dos.writeShort(slot);
            dos.writeByte(button);
            dos.writeShort(action);
            dos.writeByte(mode);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] closeWindow(int windowId)
    {
        return new byte[] { (byte)windowId };
    }

    public static byte[] animation()
    {
        return new byte[0];
    }

    public static byte[] chat(String message)
    {
        try
        {
            byte[] msgBytes = message.getBytes("UTF-8");
            ByteArrayOutputStream baos = new ByteArrayOutputStream(2 + msgBytes.length);
            DataOutputStream dos = new DataOutputStream(baos);
            dos.writeShort(msgBytes.length);
            dos.write(msgBytes);
            return baos.toByteArray();
        }
        catch (IOException e) { throw new RuntimeException(e); }
    }

    public static byte[] clientStatus(int action)
    {
        return new byte[] { (byte)action };
    }

    public static byte[] playerAbilities(boolean flying)
    {
        return new byte[] { (byte)(flying ? 1 : 0) };
    }
}
