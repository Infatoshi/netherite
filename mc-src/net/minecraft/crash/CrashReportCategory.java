package net.minecraft.crash;

import cpw.mods.fml.relauncher.Side;
import cpw.mods.fml.relauncher.SideOnly;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.Callable;
import net.minecraft.block.Block;
import net.minecraft.util.MathHelper;

public class CrashReportCategory
{
    private final CrashReport theCrashReport;
    private final String field_85076_b;
    private final List field_85077_c = new ArrayList();
    private StackTraceElement[] stackTrace = new StackTraceElement[0];
    private static final String __OBFID = "CL_00001409";

    public CrashReportCategory(CrashReport p_i1353_1_, String p_i1353_2_)
    {
        this.theCrashReport = p_i1353_1_;
        this.field_85076_b = p_i1353_2_;
    }

    @SideOnly(Side.CLIENT)
    public static String func_85074_a(double p_85074_0_, double p_85074_2_, double p_85074_4_)
    {
        return String.format("%.2f,%.2f,%.2f - %s", new Object[] {Double.valueOf(p_85074_0_), Double.valueOf(p_85074_2_), Double.valueOf(p_85074_4_), getLocationInfo(MathHelper.floor_double(p_85074_0_), MathHelper.floor_double(p_85074_2_), MathHelper.floor_double(p_85074_4_))});
    }

    /**
     * Returns a string with world information on location.Args:x,y,z
     */
    public static String getLocationInfo(int p_85071_0_, int p_85071_1_, int p_85071_2_)
    {
        StringBuilder stringbuilder = new StringBuilder();

        try
        {
            stringbuilder.append(String.format("World: (%d,%d,%d)", new Object[] {Integer.valueOf(p_85071_0_), Integer.valueOf(p_85071_1_), Integer.valueOf(p_85071_2_)}));
        }
        catch (Throwable throwable2)
        {
            stringbuilder.append("(Error finding world loc)");
        }

        stringbuilder.append(", ");
        int l;
        int i1;
        int j1;
        int k1;
        int l1;
        int i2;
        int j2;
        int k2;
        int l2;

        try
        {
            l = p_85071_0_ >> 4;
            i1 = p_85071_2_ >> 4;
            j1 = p_85071_0_ & 15;
            k1 = p_85071_1_ >> 4;
            l1 = p_85071_2_ & 15;
            i2 = l << 4;
            j2 = i1 << 4;
            k2 = (l + 1 << 4) - 1;
            l2 = (i1 + 1 << 4) - 1;
            stringbuilder.append(String.format("Chunk: (at %d,%d,%d in %d,%d; contains blocks %d,0,%d to %d,255,%d)", new Object[] {Integer.valueOf(j1), Integer.valueOf(k1), Integer.valueOf(l1), Integer.valueOf(l), Integer.valueOf(i1), Integer.valueOf(i2), Integer.valueOf(j2), Integer.valueOf(k2), Integer.valueOf(l2)}));
        }
        catch (Throwable throwable1)
        {
            stringbuilder.append("(Error finding chunk loc)");
        }

        stringbuilder.append(", ");

        try
        {
            l = p_85071_0_ >> 9;
            i1 = p_85071_2_ >> 9;
            j1 = l << 5;
            k1 = i1 << 5;
            l1 = (l + 1 << 5) - 1;
            i2 = (i1 + 1 << 5) - 1;
            j2 = l << 9;
            k2 = i1 << 9;
            l2 = (l + 1 << 9) - 1;
            int i3 = (i1 + 1 << 9) - 1;
            stringbuilder.append(String.format("Region: (%d,%d; contains chunks %d,%d to %d,%d, blocks %d,0,%d to %d,255,%d)", new Object[] {Integer.valueOf(l), Integer.valueOf(i1), Integer.valueOf(j1), Integer.valueOf(k1), Integer.valueOf(l1), Integer.valueOf(i2), Integer.valueOf(j2), Integer.valueOf(k2), Integer.valueOf(l2), Integer.valueOf(i3)}));
        }
        catch (Throwable throwable)
        {
            stringbuilder.append("(Error finding world loc)");
        }

        return stringbuilder.toString();
    }

    /**
     * Adds a Crashreport section with the given name with the value set to the result of the given Callable;
     */
    public void addCrashSectionCallable(String p_71500_1_, Callable p_71500_2_)
    {
        try
        {
            this.addCrashSection(p_71500_1_, p_71500_2_.call());
        }
        catch (Throwable throwable)
        {
            this.addCrashSectionThrowable(p_71500_1_, throwable);
        }
    }

    /**
     * Adds a Crashreport section with the given name with the given value (convered .toString())
     */
    public void addCrashSection(String p_71507_1_, Object p_71507_2_)
    {
        this.field_85077_c.add(new CrashReportCategory.Entry(p_71507_1_, p_71507_2_));
    }

    /**
     * Adds a Crashreport section with the given name with the given Throwable
     */
    public void addCrashSectionThrowable(String p_71499_1_, Throwable p_71499_2_)
    {
        this.addCrashSection(p_71499_1_, p_71499_2_);
    }

    /**
     * Resets our stack trace according to the current trace, pruning the deepest 3 entries.  The parameter indicates
     * how many additional deepest entries to prune.  Returns the number of entries in the resulting pruned stack trace.
     */
    public int getPrunedStackTrace(int p_85073_1_)
    {
        StackTraceElement[] astacktraceelement = Thread.currentThread().getStackTrace();

        if (astacktraceelement.length <= 0)
        {
            return 0;
        }
        else
        {
            int len = astacktraceelement.length - 3 - p_85073_1_;
            // Really Mojang, Still, god damn...
            if (len <= 0) len = astacktraceelement.length;
            this.stackTrace = new StackTraceElement[len];
            System.arraycopy(astacktraceelement, astacktraceelement.length - len, this.stackTrace, 0, this.stackTrace.length);
            return this.stackTrace.length;
        }
    }

    /**
     * Do the deepest two elements of our saved stack trace match the given elements, in order from the deepest?
     */
    public boolean firstTwoElementsOfStackTraceMatch(StackTraceElement p_85069_1_, StackTraceElement p_85069_2_)
    {
        if (this.stackTrace.length != 0 && p_85069_1_ != null)
        {
            StackTraceElement stacktraceelement2 = this.stackTrace[0];

            if (stacktraceelement2.isNativeMethod() == p_85069_1_.isNativeMethod() && stacktraceelement2.getClassName().equals(p_85069_1_.getClassName()) && stacktraceelement2.getFileName().equals(p_85069_1_.getFileName()) && stacktraceelement2.getMethodName().equals(p_85069_1_.getMethodName()))
            {
                if (p_85069_2_ != null != this.stackTrace.length > 1)
                {
                    return false;
                }
                else if (p_85069_2_ != null && !this.stackTrace[1].equals(p_85069_2_))
                {
                    return false;
                }
                else
                {
                    this.stackTrace[0] = p_85069_1_;
                    return true;
                }
            }
            else
            {
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    /**
     * Removes the given number entries from the bottom of the stack trace.
     */
    public void trimStackTraceEntriesFromBottom(int p_85070_1_)
    {
        StackTraceElement[] astacktraceelement = new StackTraceElement[this.stackTrace.length - p_85070_1_];
        System.arraycopy(this.stackTrace, 0, astacktraceelement, 0, astacktraceelement.length);
        this.stackTrace = astacktraceelement;
    }

    public void appendToStringBuilder(StringBuilder p_85072_1_)
    {
        p_85072_1_.append("-- ").append(this.field_85076_b).append(" --\n");
        p_85072_1_.append("Details:");
        Iterator iterator = this.field_85077_c.iterator();

        while (iterator.hasNext())
        {
            CrashReportCategory.Entry entry = (CrashReportCategory.Entry)iterator.next();
            p_85072_1_.append("\n\t");
            p_85072_1_.append(entry.func_85089_a());
            p_85072_1_.append(": ");
            p_85072_1_.append(entry.func_85090_b());
        }

        if (this.stackTrace != null && this.stackTrace.length > 0)
        {
            p_85072_1_.append("\nStacktrace:");
            StackTraceElement[] astacktraceelement = this.stackTrace;
            int j = astacktraceelement.length;

            for (int i = 0; i < j; ++i)
            {
                StackTraceElement stacktraceelement = astacktraceelement[i];
                p_85072_1_.append("\n\tat ");
                p_85072_1_.append(stacktraceelement.toString());
            }
        }
    }

    public StackTraceElement[] func_147152_a()
    {
        return this.stackTrace;
    }

    public static void func_147153_a(CrashReportCategory p_147153_0_, final int p_147153_1_, final int p_147153_2_, final int p_147153_3_, final Block p_147153_4_, final int p_147153_5_)
    {
        final int i = Block.getIdFromBlock(p_147153_4_);
        p_147153_0_.addCrashSectionCallable("Block type", new Callable()
        {
            private static final String __OBFID = "CL_00001426";
            public String call()
            {
                try
                {
                    return String.format("ID #%d (%s // %s)", new Object[] {Integer.valueOf(i), p_147153_4_.getUnlocalizedName(), p_147153_4_.getClass().getCanonicalName()});
                }
                catch (Throwable throwable)
                {
                    return "ID #" + i;
                }
            }
        });
        p_147153_0_.addCrashSectionCallable("Block data value", new Callable()
        {
            private static final String __OBFID = "CL_00001441";
            public String call()
            {
                if (p_147153_5_ < 0)
                {
                    return "Unknown? (Got " + p_147153_5_ + ")";
                }
                else
                {
                    String s = String.format("%4s", new Object[] {Integer.toBinaryString(p_147153_5_)}).replace(" ", "0");
                    return String.format("%1$d / 0x%1$X / 0b%2$s", new Object[] {Integer.valueOf(p_147153_5_), s});
                }
            }
        });
        p_147153_0_.addCrashSectionCallable("Block location", new Callable()
        {
            private static final String __OBFID = "CL_00001465";
            public String call()
            {
                return CrashReportCategory.getLocationInfo(p_147153_1_, p_147153_2_, p_147153_3_);
            }
        });
    }

    static class Entry
        {
            private final String field_85092_a;
            private final String field_85091_b;
            private static final String __OBFID = "CL_00001489";

            public Entry(String p_i1352_1_, Object p_i1352_2_)
            {
                this.field_85092_a = p_i1352_1_;

                if (p_i1352_2_ == null)
                {
                    this.field_85091_b = "~~NULL~~";
                }
                else if (p_i1352_2_ instanceof Throwable)
                {
                    Throwable throwable = (Throwable)p_i1352_2_;
                    this.field_85091_b = "~~ERROR~~ " + throwable.getClass().getSimpleName() + ": " + throwable.getMessage();
                }
                else
                {
                    this.field_85091_b = p_i1352_2_.toString();
                }
            }

            public String func_85089_a()
            {
                return this.field_85092_a;
            }

            public String func_85090_b()
            {
                return this.field_85091_b;
            }
        }
}