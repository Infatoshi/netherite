package net.minecraft.util;

import java.util.Collection;
import java.util.Iterator;
import java.util.Random;

public class WeightedRandom
{
    private static final String __OBFID = "CL_00001503";

    /**
     * Returns the total weight of all items in a collection.
     */
    public static int getTotalWeight(Collection p_76272_0_)
    {
        int i = 0;
        WeightedRandom.Item item;

        for (Iterator iterator = p_76272_0_.iterator(); iterator.hasNext(); i += item.itemWeight)
        {
            item = (WeightedRandom.Item)iterator.next();
        }

        return i;
    }

    /**
     * Returns a random choice from the input items, with a total weight value.
     */
    public static WeightedRandom.Item getRandomItem(Random p_76273_0_, Collection p_76273_1_, int p_76273_2_)
    {
        if (p_76273_2_ <= 0)
        {
            throw new IllegalArgumentException();
        }
        return getItem(p_76273_1_, p_76273_0_.nextInt(p_76273_2_));
    }

    //Forge: Added to allow custom random implementations, Modder is responsible for making sure the 
    //'weight' is under the totalWeight of the items.
    public static WeightedRandom.Item getItem(Collection par1Collection, int weight)
    {
        {
            int j = weight;
            Iterator iterator = par1Collection.iterator();
            WeightedRandom.Item item;

            do
            {
                if (!iterator.hasNext())
                {
                    return null;
                }

                item = (WeightedRandom.Item)iterator.next();
                j -= item.itemWeight;
            }
            while (j >= 0);

            return item;
        }
    }

    /**
     * Returns a random choice from the input items.
     */
    public static WeightedRandom.Item getRandomItem(Random p_76271_0_, Collection p_76271_1_)
    {
        /**
         * Returns a random choice from the input items, with a total weight value.
         */
        return getRandomItem(p_76271_0_, p_76271_1_, getTotalWeight(p_76271_1_));
    }

    /**
     * Returns the total weight of all items in a array.
     */
    public static int getTotalWeight(WeightedRandom.Item[] p_76270_0_)
    {
        int i = 0;
        WeightedRandom.Item[] aitem = p_76270_0_;
        int j = p_76270_0_.length;

        for (int k = 0; k < j; ++k)
        {
            WeightedRandom.Item item = aitem[k];
            i += item.itemWeight;
        }

        return i;
    }

    /**
     * Returns a random choice from the input array of items, with a total weight value.
     */
    public static WeightedRandom.Item getRandomItem(Random p_76269_0_, WeightedRandom.Item[] p_76269_1_, int p_76269_2_)
    {
        if (p_76269_2_ <= 0)
        {
            throw new IllegalArgumentException();
        }
        return getItem(p_76269_1_, p_76269_0_.nextInt(p_76269_2_));
    }

    //Forge: Added to allow custom random implementations, Modder is responsible for making sure the 
    //'weight' is under the totalWeight of the items.
    public static WeightedRandom.Item getItem(WeightedRandom.Item[] par1ArrayOfWeightedRandomItem, int weight)
    {
        {
            int j = weight;
            WeightedRandom.Item[] aitem = par1ArrayOfWeightedRandomItem;
            int k = par1ArrayOfWeightedRandomItem.length;

            for (int l = 0; l < k; ++l)
            {
                WeightedRandom.Item item = aitem[l];
                j -= item.itemWeight;

                if (j < 0)
                {
                    return item;
                }
            }

            return null;
        }
    }

    /**
     * Returns a random choice from the input items.
     */
    public static WeightedRandom.Item getRandomItem(Random p_76274_0_, WeightedRandom.Item[] p_76274_1_)
    {
        /**
         * Returns a random choice from the input array of items, with a total weight value.
         */
        return getRandomItem(p_76274_0_, p_76274_1_, getTotalWeight(p_76274_1_));
    }

    public static class Item
        {
            /** The Weight is how often the item is chosen(higher number is higher chance(lower is lower)) */
            public int itemWeight;
            private static final String __OBFID = "CL_00001504";

            public Item(int p_i1556_1_)
            {
                this.itemWeight = p_i1556_1_;
            }
        }
}