package net.minecraft.command.server;

import java.util.List;
import net.minecraft.command.CommandBase;
import net.minecraft.command.ICommandSender;
import net.minecraft.command.WrongUsageException;
import net.minecraft.entity.Entity;
import net.minecraft.entity.EntityList;
import net.minecraft.entity.EntityLiving;
import net.minecraft.entity.IEntityLivingData;
import net.minecraft.nbt.JsonToNBT;
import net.minecraft.nbt.NBTBase;
import net.minecraft.nbt.NBTException;
import net.minecraft.nbt.NBTTagCompound;
import net.minecraft.util.IChatComponent;
import net.minecraft.world.World;

public class CommandSummon extends CommandBase
{
    private static final String __OBFID = "CL_00001158";

    public String getCommandName()
    {
        return "summon";
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
        return "commands.summon.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length < 1)
        {
            throw new WrongUsageException("commands.summon.usage", new Object[0]);
        }
        else
        {
            String s = p_71515_2_[0];
            double d0 = (double)p_71515_1_.getPlayerCoordinates().posX + 0.5D;
            double d1 = (double)p_71515_1_.getPlayerCoordinates().posY;
            double d2 = (double)p_71515_1_.getPlayerCoordinates().posZ + 0.5D;

            if (p_71515_2_.length >= 4)
            {
                d0 = func_110666_a(p_71515_1_, d0, p_71515_2_[1]);
                d1 = func_110666_a(p_71515_1_, d1, p_71515_2_[2]);
                d2 = func_110666_a(p_71515_1_, d2, p_71515_2_[3]);
            }

            World world = p_71515_1_.getEntityWorld();

            if (!world.blockExists((int)d0, (int)d1, (int)d2))
            {
                func_152373_a(p_71515_1_, this, "commands.summon.outOfWorld", new Object[0]);
            }
            else
            {
                NBTTagCompound nbttagcompound = new NBTTagCompound();
                boolean flag = false;

                if (p_71515_2_.length >= 5)
                {
                    IChatComponent ichatcomponent = func_147178_a(p_71515_1_, p_71515_2_, 4);

                    try
                    {
                        NBTBase nbtbase = JsonToNBT.func_150315_a(ichatcomponent.getUnformattedText());

                        if (!(nbtbase instanceof NBTTagCompound))
                        {
                            func_152373_a(p_71515_1_, this, "commands.summon.tagError", new Object[] {"Not a valid tag"});
                            return;
                        }

                        nbttagcompound = (NBTTagCompound)nbtbase;
                        flag = true;
                    }
                    catch (NBTException nbtexception)
                    {
                        func_152373_a(p_71515_1_, this, "commands.summon.tagError", new Object[] {nbtexception.getMessage()});
                        return;
                    }
                }

                nbttagcompound.setString("id", s);
                Entity entity1 = EntityList.createEntityFromNBT(nbttagcompound, world);

                if (entity1 == null)
                {
                    func_152373_a(p_71515_1_, this, "commands.summon.failed", new Object[0]);
                }
                else
                {
                    entity1.setLocationAndAngles(d0, d1, d2, entity1.rotationYaw, entity1.rotationPitch);

                    if (!flag && entity1 instanceof EntityLiving)
                    {
                        ((EntityLiving)entity1).onSpawnWithEgg((IEntityLivingData)null);
                    }

                    world.spawnEntityInWorld(entity1);
                    Entity entity2 = entity1;

                    for (NBTTagCompound nbttagcompound1 = nbttagcompound; entity2 != null && nbttagcompound1.hasKey("Riding", 10); nbttagcompound1 = nbttagcompound1.getCompoundTag("Riding"))
                    {
                        Entity entity = EntityList.createEntityFromNBT(nbttagcompound1.getCompoundTag("Riding"), world);

                        if (entity != null)
                        {
                            entity.setLocationAndAngles(d0, d1, d2, entity.rotationYaw, entity.rotationPitch);
                            world.spawnEntityInWorld(entity);
                            entity2.mountEntity(entity);
                        }

                        entity2 = entity;
                    }

                    func_152373_a(p_71515_1_, this, "commands.summon.success", new Object[0]);
                }
            }
        }
    }

    /**
     * Adds the strings available in this command to the given list of tab completion options.
     */
    public List addTabCompletionOptions(ICommandSender p_71516_1_, String[] p_71516_2_)
    {
        return p_71516_2_.length == 1 ? getListOfStringsMatchingLastWord(p_71516_2_, this.func_147182_d()) : null;
    }

    protected String[] func_147182_d()
    {
        return (String[])EntityList.func_151515_b().toArray(new String[0]);
    }
}