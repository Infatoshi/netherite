package net.minecraft.command;

public class NumberInvalidException extends CommandException
{
    private static final String __OBFID = "CL_00001188";

    public NumberInvalidException()
    {
        this("commands.generic.num.invalid", new Object[0]);
    }

    public NumberInvalidException(String p_i1360_1_, Object ... p_i1360_2_)
    {
        super(p_i1360_1_, p_i1360_2_);
    }
}