package net.minecraft.command;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Random;
import net.minecraft.block.material.Material;
import net.minecraft.entity.EntityLivingBase;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.entity.player.EntityPlayerMP;
import net.minecraft.scoreboard.Team;
import net.minecraft.server.MinecraftServer;
import net.minecraft.util.ChatComponentTranslation;
import net.minecraft.util.MathHelper;
import net.minecraft.world.World;

public class CommandSpreadPlayers extends CommandBase
{
    private static final String __OBFID = "CL_00001080";

    public String getCommandName()
    {
        return "spreadplayers";
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
        return "commands.spreadplayers.usage";
    }

    public void processCommand(ICommandSender p_71515_1_, String[] p_71515_2_)
    {
        if (p_71515_2_.length < 6)
        {
            throw new WrongUsageException("commands.spreadplayers.usage", new Object[0]);
        }
        else
        {
            byte b0 = 0;
            int i = b0 + 1;
            double d0 = func_110666_a(p_71515_1_, Double.NaN, p_71515_2_[b0]);
            double d1 = func_110666_a(p_71515_1_, Double.NaN, p_71515_2_[i++]);
            double d2 = parseDoubleWithMin(p_71515_1_, p_71515_2_[i++], 0.0D);
            double d3 = parseDoubleWithMin(p_71515_1_, p_71515_2_[i++], d2 + 1.0D);
            boolean flag = parseBoolean(p_71515_1_, p_71515_2_[i++]);
            ArrayList arraylist = Lists.newArrayList();

            while (true)
            {
                while (i < p_71515_2_.length)
                {
                    String s = p_71515_2_[i++];

                    if (PlayerSelector.hasArguments(s))
                    {
                        EntityPlayerMP[] aentityplayermp = PlayerSelector.matchPlayers(p_71515_1_, s);

                        if (aentityplayermp == null || aentityplayermp.length == 0)
                        {
                            throw new PlayerNotFoundException();
                        }

                        Collections.addAll(arraylist, aentityplayermp);
                    }
                    else
                    {
                        EntityPlayerMP entityplayermp = MinecraftServer.getServer().getConfigurationManager().func_152612_a(s);

                        if (entityplayermp == null)
                        {
                            throw new PlayerNotFoundException();
                        }

                        arraylist.add(entityplayermp);
                    }
                }

                if (arraylist.isEmpty())
                {
                    throw new PlayerNotFoundException();
                }

                p_71515_1_.addChatMessage(new ChatComponentTranslation("commands.spreadplayers.spreading." + (flag ? "teams" : "players"), new Object[] {Integer.valueOf(arraylist.size()), Double.valueOf(d3), Double.valueOf(d0), Double.valueOf(d1), Double.valueOf(d2)}));
                this.func_110669_a(p_71515_1_, arraylist, new CommandSpreadPlayers.Position(d0, d1), d2, d3, ((EntityLivingBase)arraylist.get(0)).worldObj, flag);
                return;
            }
        }
    }

    private void func_110669_a(ICommandSender p_110669_1_, List p_110669_2_, CommandSpreadPlayers.Position p_110669_3_, double p_110669_4_, double p_110669_6_, World p_110669_8_, boolean p_110669_9_)
    {
        Random random = new Random();
        double d2 = p_110669_3_.field_111101_a - p_110669_6_;
        double d3 = p_110669_3_.field_111100_b - p_110669_6_;
        double d4 = p_110669_3_.field_111101_a + p_110669_6_;
        double d5 = p_110669_3_.field_111100_b + p_110669_6_;
        CommandSpreadPlayers.Position[] aposition = this.func_110670_a(random, p_110669_9_ ? this.func_110667_a(p_110669_2_) : p_110669_2_.size(), d2, d3, d4, d5);
        int i = this.func_110668_a(p_110669_3_, p_110669_4_, p_110669_8_, random, d2, d3, d4, d5, aposition, p_110669_9_);
        double d6 = this.func_110671_a(p_110669_2_, p_110669_8_, aposition, p_110669_9_);
        func_152373_a(p_110669_1_, this, "commands.spreadplayers.success." + (p_110669_9_ ? "teams" : "players"), new Object[] {Integer.valueOf(aposition.length), Double.valueOf(p_110669_3_.field_111101_a), Double.valueOf(p_110669_3_.field_111100_b)});

        if (aposition.length > 1)
        {
            p_110669_1_.addChatMessage(new ChatComponentTranslation("commands.spreadplayers.info." + (p_110669_9_ ? "teams" : "players"), new Object[] {String.format("%.2f", new Object[]{Double.valueOf(d6)}), Integer.valueOf(i)}));
        }
    }

    private int func_110667_a(List p_110667_1_)
    {
        HashSet hashset = Sets.newHashSet();
        Iterator iterator = p_110667_1_.iterator();

        while (iterator.hasNext())
        {
            EntityLivingBase entitylivingbase = (EntityLivingBase)iterator.next();

            if (entitylivingbase instanceof EntityPlayer)
            {
                hashset.add(entitylivingbase.getTeam());
            }
            else
            {
                hashset.add((Object)null);
            }
        }

        return hashset.size();
    }

    private int func_110668_a(CommandSpreadPlayers.Position p_110668_1_, double p_110668_2_, World p_110668_4_, Random p_110668_5_, double p_110668_6_, double p_110668_8_, double p_110668_10_, double p_110668_12_, CommandSpreadPlayers.Position[] p_110668_14_, boolean p_110668_15_)
    {
        boolean flag1 = true;
        double d5 = 3.4028234663852886E38D;
        int i;

        for (i = 0; i < 10000 && flag1; ++i)
        {
            flag1 = false;
            d5 = 3.4028234663852886E38D;
            int k;
            CommandSpreadPlayers.Position position1;

            for (int j = 0; j < p_110668_14_.length; ++j)
            {
                CommandSpreadPlayers.Position position = p_110668_14_[j];
                k = 0;
                position1 = new CommandSpreadPlayers.Position();

                for (int l = 0; l < p_110668_14_.length; ++l)
                {
                    if (j != l)
                    {
                        CommandSpreadPlayers.Position position2 = p_110668_14_[l];
                        double d6 = position.func_111099_a(position2);
                        d5 = Math.min(d6, d5);

                        if (d6 < p_110668_2_)
                        {
                            ++k;
                            position1.field_111101_a += position2.field_111101_a - position.field_111101_a;
                            position1.field_111100_b += position2.field_111100_b - position.field_111100_b;
                        }
                    }
                }

                if (k > 0)
                {
                    position1.field_111101_a /= (double)k;
                    position1.field_111100_b /= (double)k;
                    double d7 = (double)position1.func_111096_b();

                    if (d7 > 0.0D)
                    {
                        position1.func_111095_a();
                        position.func_111094_b(position1);
                    }
                    else
                    {
                        position.func_111097_a(p_110668_5_, p_110668_6_, p_110668_8_, p_110668_10_, p_110668_12_);
                    }

                    flag1 = true;
                }

                if (position.func_111093_a(p_110668_6_, p_110668_8_, p_110668_10_, p_110668_12_))
                {
                    flag1 = true;
                }
            }

            if (!flag1)
            {
                CommandSpreadPlayers.Position[] aposition = p_110668_14_;
                int i1 = p_110668_14_.length;

                for (k = 0; k < i1; ++k)
                {
                    position1 = aposition[k];

                    if (!position1.func_111098_b(p_110668_4_))
                    {
                        position1.func_111097_a(p_110668_5_, p_110668_6_, p_110668_8_, p_110668_10_, p_110668_12_);
                        flag1 = true;
                    }
                }
            }
        }

        if (i >= 10000)
        {
            throw new CommandException("commands.spreadplayers.failure." + (p_110668_15_ ? "teams" : "players"), new Object[] {Integer.valueOf(p_110668_14_.length), Double.valueOf(p_110668_1_.field_111101_a), Double.valueOf(p_110668_1_.field_111100_b), String.format("%.2f", new Object[]{Double.valueOf(d5)})});
        }
        else
        {
            return i;
        }
    }

    private double func_110671_a(List p_110671_1_, World p_110671_2_, CommandSpreadPlayers.Position[] p_110671_3_, boolean p_110671_4_)
    {
        double d0 = 0.0D;
        int i = 0;
        HashMap hashmap = Maps.newHashMap();

        for (int j = 0; j < p_110671_1_.size(); ++j)
        {
            EntityLivingBase entitylivingbase = (EntityLivingBase)p_110671_1_.get(j);
            CommandSpreadPlayers.Position position;

            if (p_110671_4_)
            {
                Team team = entitylivingbase instanceof EntityPlayer ? entitylivingbase.getTeam() : null;

                if (!hashmap.containsKey(team))
                {
                    hashmap.put(team, p_110671_3_[i++]);
                }

                position = (CommandSpreadPlayers.Position)hashmap.get(team);
            }
            else
            {
                position = p_110671_3_[i++];
            }

            entitylivingbase.setPositionAndUpdate((double)((float)MathHelper.floor_double(position.field_111101_a) + 0.5F), (double)position.func_111092_a(p_110671_2_), (double)MathHelper.floor_double(position.field_111100_b) + 0.5D);
            double d2 = Double.MAX_VALUE;

            for (int k = 0; k < p_110671_3_.length; ++k)
            {
                if (position != p_110671_3_[k])
                {
                    double d1 = position.func_111099_a(p_110671_3_[k]);
                    d2 = Math.min(d1, d2);
                }
            }

            d0 += d2;
        }

        d0 /= (double)p_110671_1_.size();
        return d0;
    }

    private CommandSpreadPlayers.Position[] func_110670_a(Random p_110670_1_, int p_110670_2_, double p_110670_3_, double p_110670_5_, double p_110670_7_, double p_110670_9_)
    {
        CommandSpreadPlayers.Position[] aposition = new CommandSpreadPlayers.Position[p_110670_2_];

        for (int j = 0; j < aposition.length; ++j)
        {
            CommandSpreadPlayers.Position position = new CommandSpreadPlayers.Position();
            position.func_111097_a(p_110670_1_, p_110670_3_, p_110670_5_, p_110670_7_, p_110670_9_);
            aposition[j] = position;
        }

        return aposition;
    }

    static class Position
        {
            double field_111101_a;
            double field_111100_b;
            private static final String __OBFID = "CL_00001105";

            Position() {}

            Position(double p_i1358_1_, double p_i1358_3_)
            {
                this.field_111101_a = p_i1358_1_;
                this.field_111100_b = p_i1358_3_;
            }

            double func_111099_a(CommandSpreadPlayers.Position p_111099_1_)
            {
                double d0 = this.field_111101_a - p_111099_1_.field_111101_a;
                double d1 = this.field_111100_b - p_111099_1_.field_111100_b;
                return Math.sqrt(d0 * d0 + d1 * d1);
            }

            void func_111095_a()
            {
                double d0 = (double)this.func_111096_b();
                this.field_111101_a /= d0;
                this.field_111100_b /= d0;
            }

            float func_111096_b()
            {
                return MathHelper.sqrt_double(this.field_111101_a * this.field_111101_a + this.field_111100_b * this.field_111100_b);
            }

            public void func_111094_b(CommandSpreadPlayers.Position p_111094_1_)
            {
                this.field_111101_a -= p_111094_1_.field_111101_a;
                this.field_111100_b -= p_111094_1_.field_111100_b;
            }

            public boolean func_111093_a(double p_111093_1_, double p_111093_3_, double p_111093_5_, double p_111093_7_)
            {
                boolean flag = false;

                if (this.field_111101_a < p_111093_1_)
                {
                    this.field_111101_a = p_111093_1_;
                    flag = true;
                }
                else if (this.field_111101_a > p_111093_5_)
                {
                    this.field_111101_a = p_111093_5_;
                    flag = true;
                }

                if (this.field_111100_b < p_111093_3_)
                {
                    this.field_111100_b = p_111093_3_;
                    flag = true;
                }
                else if (this.field_111100_b > p_111093_7_)
                {
                    this.field_111100_b = p_111093_7_;
                    flag = true;
                }

                return flag;
            }

            public int func_111092_a(World p_111092_1_)
            {
                int i = MathHelper.floor_double(this.field_111101_a);
                int j = MathHelper.floor_double(this.field_111100_b);

                for (int k = 256; k > 0; --k)
                {
                    if (p_111092_1_.getBlock(i, k, j).getMaterial() != Material.air)
                    {
                        return k + 1;
                    }
                }

                return 257;
            }

            public boolean func_111098_b(World p_111098_1_)
            {
                int i = MathHelper.floor_double(this.field_111101_a);
                int j = MathHelper.floor_double(this.field_111100_b);
                short short1 = 256;

                if (short1 <= 0)
                {
                    return false;
                }
                else
                {
                    Material material = p_111098_1_.getBlock(i, short1, j).getMaterial();
                    return !material.isLiquid() && material != Material.fire;
                }
            }

            public void func_111097_a(Random p_111097_1_, double p_111097_2_, double p_111097_4_, double p_111097_6_, double p_111097_8_)
            {
                this.field_111101_a = MathHelper.getRandomDoubleInRange(p_111097_1_, p_111097_2_, p_111097_6_);
                this.field_111100_b = MathHelper.getRandomDoubleInRange(p_111097_1_, p_111097_4_, p_111097_8_);
            }
        }
}