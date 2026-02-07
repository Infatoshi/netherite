package net.minecraft.network.rcon;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;

@SideOnly(Side.SERVER)
public class RConOutputStream
{
    /** Output stream */
    private ByteArrayOutputStream byteArrayOutput;
    /** ByteArrayOutputStream wrapper */
    private DataOutputStream output;
    private static final String __OBFID = "CL_00001798";

    public RConOutputStream(int p_i1533_1_)
    {
        this.byteArrayOutput = new ByteArrayOutputStream(p_i1533_1_);
        this.output = new DataOutputStream(this.byteArrayOutput);
    }

    /**
     * Writes the given byte array to the output stream
     */
    public void writeByteArray(byte[] p_72670_1_) throws IOException
    {
        this.output.write(p_72670_1_, 0, p_72670_1_.length);
    }

    /**
     * Writes the given String to the output stream
     */
    public void writeString(String p_72671_1_) throws IOException
    {
        this.output.writeBytes(p_72671_1_);
        this.output.write(0);
    }

    /**
     * Writes the given int to the output stream
     */
    public void writeInt(int p_72667_1_) throws IOException
    {
        this.output.write(p_72667_1_);
    }

    /**
     * Writes the given short to the output stream
     */
    public void writeShort(short p_72668_1_) throws IOException
    {
        this.output.writeShort(Short.reverseBytes(p_72668_1_));
    }

    /**
     * Returns the contents of the output stream as a byte array
     */
    public byte[] toByteArray()
    {
        return this.byteArrayOutput.toByteArray();
    }

    /**
     * Resets the byte array output.
     */
    public void reset()
    {
        this.byteArrayOutput.reset();
    }
}