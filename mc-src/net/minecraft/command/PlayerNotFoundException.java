package net.minecraft.command;

public class PlayerNotFoundException extends CommandException
{
    private static final String __OBFID = "CL_00001190";

    public PlayerNotFoundException()
    {
        this("commands.generic.player.notFound", new Object[0]);
    }

    public PlayerNotFoundException(String p_i1362_1_, Object ... p_i1362_2_)
    {
        super(p_i1362_1_, p_i1362_2_);
    }
}