package net.minecraft.command.server;

import net.minecraft.command.CommandBase;
import net.minecraft.command.ICommandSender;
import net.minecraft.command.WrongUsageException;
import net.minecraft.util.ChunkCoordinates;

public class CommandSetDefaultSpawnpoint extends CommandBase
{
    private static final String __OBFID = "CL_00000973";

    public String getCommandName()
    {
        return "setworldspawn";
    }

    /**
     * Return the required permission level for this command.
     */
    public int getRequiredPermissionLevel()
    {
        return 2;
    }

    public String getCommandUsage(ICommandSender p_71518_1_)
    {
        return "commands.setworldspawn.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length == 3)
        {
            if (p_71515_1_.getEntityWorld() == null)
            {
                throw new WrongUsageException("commands.setworldspawn.usage", new Object[0]);
            }

            byte b0 = 0;
            int l = b0 + 1;
            int i = parseIntBounded(p_71515_1_, p_71515_2_[b0], -30000000, 30000000);
            int j = parseIntBounded(p_71515_1_, p_71515_2_[l++], 0, 256);
            int k = parseIntBounded(p_71515_1_, p_71515_2_[l++], -30000000, 30000000);
            p_71515_1_.getEntityWorld().setSpawnLocation(i, j, k);
            func_152373_a(p_71515_1_, this, "commands.setworldspawn.success", new Object[] {Integer.valueOf(i), Integer.valueOf(j), Integer.valueOf(k)});
        }
        else
        {
            if (p_71515_2_.length != 0)
            {
                throw new WrongUsageException("commands.setworldspawn.usage", new Object[0]);
            }

            ChunkCoordinates chunkcoordinates = getCommandSenderAsPlayer(p_71515_1_).getPlayerCoordinates();
            p_71515_1_.getEntityWorld().setSpawnLocation(chunkcoordinates.posX, chunkcoordinates.posY, chunkcoordinates.posZ);
            func_152373_a(p_71515_1_, this, "commands.setworldspawn.success", new Object[] {Integer.valueOf(chunkcoordinates.posX), Integer.valueOf(chunkcoordinates.posY), Integer.valueOf(chunkcoordinates.posZ)});
        }
    }
}