package net.minecraft.command;

import java.util.List;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.ChunkCoordinates;

public class CommandSetSpawnpoint extends CommandBase
{
    private static final String __OBFID = "CL_00001026";

    public String getCommandName()
    {
        return "spawnpoint";
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
        return "commands.spawnpoint.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        EntityPlayerMP entityplayermp = p_71515_2_.length == 0 ? getCommandSenderAsPlayer(p_71515_1_) : getPlayer(p_71515_1_, p_71515_2_[0]);

        if (p_71515_2_.length == 4)
        {
            if (entityplayermp.worldObj != null)
            {
                byte b0 = 1;
                int i = 30000000;
                int i1 = b0 + 1;
                int j = parseIntBounded(p_71515_1_, p_71515_2_[b0], -i, i);
                int k = parseIntBounded(p_71515_1_, p_71515_2_[i1++], 0, 256);
                int l = parseIntBounded(p_71515_1_, p_71515_2_[i1++], -i, i);
                entityplayermp.setSpawnChunk(new ChunkCoordinates(j, k, l), true);
                func_152373_a(p_71515_1_, this, "commands.spawnpoint.success", new Object[] {entityplayermp.getCommandSenderName(), Integer.valueOf(j), Integer.valueOf(k), Integer.valueOf(l)});
            }
        }
        else
        {
            if (p_71515_2_.length > 1)
            {
                throw new WrongUsageException("commands.spawnpoint.usage", new Object[0]);
            }

            ChunkCoordinates chunkcoordinates = entityplayermp.getPlayerCoordinates();
            entityplayermp.setSpawnChunk(chunkcoordinates, true);
            func_152373_a(p_71515_1_, this, "commands.spawnpoint.success", new Object[] {entityplayermp.getCommandSenderName(), Integer.valueOf(chunkcoordinates.posX), Integer.valueOf(chunkcoordinates.posY), Integer.valueOf(chunkcoordinates.posZ)});
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length != 1 && p_71516_2_.length != 2 ? null : getListOfStringsMatchingLastWord(p_71516_2_, MinecraftServer.getServer().getAllUsernames());
    }

    /**
     * Return whether the specified command parameter index is a username parameter.
     */
    public boolean isUsernameIndex(String[] p_82358_1_, int p_82358_2_)
    {
        return p_82358_2_ == 0;
    }
}