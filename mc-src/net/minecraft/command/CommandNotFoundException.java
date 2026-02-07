package net.minecraft.command;

public class CommandNotFoundException extends CommandException
{
    private static final String __OBFID = "CL_00001191";

    public CommandNotFoundException()
    {
        this("commands.generic.notFound", new Object[0]);
    }

    public CommandNotFoundException(String p_i1363_1_, Object ... p_i1363_2_)
    {
        super(p_i1363_1_, p_i1363_2_);
    }
}