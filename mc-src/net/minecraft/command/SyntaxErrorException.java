package net.minecraft.command;

public class SyntaxErrorException extends CommandException
{
    private static final String __OBFID = "CL_00001189";

    public SyntaxErrorException()
    {
        this("commands.generic.snytax", new Object[0]);
    }

    public SyntaxErrorException(String p_i1361_1_, Object ... p_i1361_2_)
    {
        super(p_i1361_1_, p_i1361_2_);
    }
}