package net.minecraft.command.server;

import java.util.List;
import net.minecraft.block.Block;
import net.minecraft.command.CommandBase;
import net.minecraft.command.CommandException;
import net.minecraft.command.ICommandSender;
import net.minecraft.command.WrongUsageException;
import net.minecraft.nbt.JsonToNBT;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTException;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.tileentity.TileEntity;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;

public class CommandSetBlock extends CommandBase
{
    private static final String __OBFID = "CL_00000949";

    public String getCommandName()
    {
        return "setblock";
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
        return "commands.setblock.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length >= 4)
        {
            int i = p_71515_1_.getPlayerCoordinates().posX;
            int j = p_71515_1_.getPlayerCoordinates().posY;
            int k = p_71515_1_.getPlayerCoordinates().posZ;
            i = MathHelper.floor_double(func_110666_a(p_71515_1_, (double)i, p_71515_2_[0]));
            j = MathHelper.floor_double(func_110666_a(p_71515_1_, (double)j, p_71515_2_[1]));
            k = MathHelper.floor_double(func_110666_a(p_71515_1_, (double)k, p_71515_2_[2]));
            Block block = CommandBase.getBlockByText(p_71515_1_, p_71515_2_[3]);
            int l = 0;

            if (p_71515_2_.length >= 5)
            {
                l = parseIntBounded(p_71515_1_, p_71515_2_[4], 0, 15);
            }

            World world = p_71515_1_.getEntityWorld();

            if (!world.blockExists(i, j, k))
            {
                throw new CommandException("commands.setblock.outOfWorld", new Object[0]);
            }
            else
            {
                NBTTagCompound nbttagcompound = new NBTTagCompound();
                boolean flag = false;

                if (p_71515_2_.length >= 7 && block.hasTileEntity())
                {
                    String s = func_147178_a(p_71515_1_, p_71515_2_, 6).getUnformattedText();

                    try
                    {
                        NBTBase nbtbase = JsonToNBT.func_150315_a(s);

                        if (!(nbtbase instanceof NBTTagCompound))
                        {
                            throw new CommandException("commands.setblock.tagError", new Object[] {"Not a valid tag"});
                        }

                        nbttagcompound = (NBTTagCompound)nbtbase;
                        flag = true;
                    }
                    catch (NBTException nbtexception)
                    {
                        throw new CommandException("commands.setblock.tagError", new Object[] {nbtexception.getMessage()});
                    }
                }

                if (p_71515_2_.length >= 6)
                {
                    if (p_71515_2_[5].equals("destroy"))
                    {
                        world.func_147480_a(i, j, k, true);
                    }
                    else if (p_71515_2_[5].equals("keep") && !world.isAirBlock(i, j, k))
                    {
                        throw new CommandException("commands.setblock.noChange", new Object[0]);
                    }
                }

                if (!world.setBlock(i, j, k, block, l, 3))
                {
                    throw new CommandException("commands.setblock.noChange", new Object[0]);
                }
                else
                {
                    if (flag)
                    {
                        TileEntity tileentity = world.getTileEntity(i, j, k);

                        if (tileentity != null)
                        {
                            nbttagcompound.setInteger("x", i);
                            nbttagcompound.setInteger("y", j);
                            nbttagcompound.setInteger("z", k);
                            tileentity.readFromNBT(nbttagcompound);
                        }
                    }

                    func_152373_a(p_71515_1_, this, "commands.setblock.success", new Object[0]);
                }
            }
        }
        else
        {
            throw new WrongUsageException("commands.setblock.usage", new Object[0]);
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 4 ? getListOfStringsFromIterableMatchingLastWord(p_71516_2_, Block.blockRegistry.getKeys()) : (p_71516_2_.length == 6 ? getListOfStringsMatchingLastWord(p_71516_2_, new String[] {"replace", "destroy", "keep"}): null);
    }
}