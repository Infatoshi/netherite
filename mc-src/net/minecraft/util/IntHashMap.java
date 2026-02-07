package net.minecraft.util;

import java.util.HashSet;
import java.util.Set;

public class IntHashMap
{
    /** An array of HashEntries representing the heads of hash slot lists */
    private transient IntHashMap.Entry[] slots = new IntHashMap.Entry[16];
    /** The number of items stored in this map */
    private transient int count;
    /** The grow threshold */
    private int threshold = 12;
    /** The scale factor used to determine when to grow the table */
    private final float growFactor = 0.75F;
    /** A serial stamp used to mark changes */
    private transient volatile int versionStamp;
    /** The set of all the keys stored in this MCHash object */
    private Set keySet = new HashSet();
    private static final String __OBFID = "CL_00001490";

    /**
     * Makes the passed in integer suitable for hashing by a number of shifts
     */
    private static int computeHash(int p_76044_0_)
    {
        p_76044_0_ ^= p_76044_0_ >>> 20 ^ p_76044_0_ >>> 12;
        return p_76044_0_ ^ p_76044_0_ >>> 7 ^ p_76044_0_ >>> 4;
    }

    /**
     * Computes the index of the slot for the hash and slot count passed in.
     */
    private static int getSlotIndex(int p_76043_0_, int p_76043_1_)
    {
        return p_76043_0_ & p_76043_1_ - 1;
    }

    /**
     * Returns the object associated to a key
     */
    public Object lookup(int p_76041_1_)
    {
        int j = computeHash(p_76041_1_);

        for (IntHashMap.Entry entry = this.slots[getSlotIndex(j, this.slots.length)]; entry != null; entry = entry.nextEntry)
        {
            if (entry.hashEntry == p_76041_1_)
            {
                return entry.valueEntry;
            }
        }

        return null;
    }

    /**
     * Return true if an object is associated with the given key
     */
    public boolean containsItem(int p_76037_1_)
    {
        return this.lookupEntry(p_76037_1_) != null;
    }

    /**
     * Returns the key/object mapping for a given key as a MCHashEntry
     */
    final IntHashMap.Entry lookupEntry(int p_76045_1_)
    {
        int j = computeHash(p_76045_1_);

        for (IntHashMap.Entry entry = this.slots[getSlotIndex(j, this.slots.length)]; entry != null; entry = entry.nextEntry)
        {
            if (entry.hashEntry == p_76045_1_)
            {
                return entry;
            }
        }

        return null;
    }

    /**
     * Adds a key and associated value to this map
     */
    public void addKey(int p_76038_1_, Object p_76038_2_)
    {
        this.keySet.add(Integer.valueOf(p_76038_1_));
        int j = computeHash(p_76038_1_);
        int k = getSlotIndex(j, this.slots.length);

        for (IntHashMap.Entry entry = this.slots[k]; entry != null; entry = entry.nextEntry)
        {
            if (entry.hashEntry == p_76038_1_)
            {
                entry.valueEntry = p_76038_2_;
                return;
            }
        }

        ++this.versionStamp;
        this.insert(j, p_76038_1_, p_76038_2_, k);
    }

    /**
     * Increases the number of hash slots
     */
    private void grow(int p_76047_1_)
    {
        IntHashMap.Entry[] aentry = this.slots;
        int j = aentry.length;

        if (j == 1073741824)
        {
            this.threshold = Integer.MAX_VALUE;
        }
        else
        {
            IntHashMap.Entry[] aentry1 = new IntHashMap.Entry[p_76047_1_];
            this.copyTo(aentry1);
            this.slots = aentry1;
            this.threshold = (int)((float)p_76047_1_ * this.growFactor);
        }
    }

    /**
     * Copies the hash slots to a new array
     */
    private void copyTo(IntHashMap.Entry[] p_76048_1_)
    {
        IntHashMap.Entry[] aentry = this.slots;
        int i = p_76048_1_.length;

        for (int j = 0; j < aentry.length; ++j)
        {
            IntHashMap.Entry entry = aentry[j];

            if (entry != null)
            {
                aentry[j] = null;
                IntHashMap.Entry entry1;

                do
                {
                    entry1 = entry.nextEntry;
                    int k = getSlotIndex(entry.slotHash, i);
                    entry.nextEntry = p_76048_1_[k];
                    p_76048_1_[k] = entry;
                    entry = entry1;
                }
                while (entry1 != null);
            }
        }
    }

    /**
     * Removes the specified object from the map and returns it
     */
    public Object removeObject(int p_76049_1_)
    {
        this.keySet.remove(Integer.valueOf(p_76049_1_));
        IntHashMap.Entry entry = this.removeEntry(p_76049_1_);
        return entry == null ? null : entry.valueEntry;
    }

    /**
     * Removes the specified entry from the map and returns it
     */
    final IntHashMap.Entry removeEntry(int p_76036_1_)
    {
        int j = computeHash(p_76036_1_);
        int k = getSlotIndex(j, this.slots.length);
        IntHashMap.Entry entry = this.slots[k];
        IntHashMap.Entry entry1;
        IntHashMap.Entry entry2;

        for (entry1 = entry; entry1 != null; entry1 = entry2)
        {
            entry2 = entry1.nextEntry;

            if (entry1.hashEntry == p_76036_1_)
            {
                ++this.versionStamp;
                --this.count;

                if (entry == entry1)
                {
                    this.slots[k] = entry2;
                }
                else
                {
                    entry.nextEntry = entry2;
                }

                return entry1;
            }

            entry = entry1;
        }

        return entry1;
    }

    /**
     * Removes all entries from the map
     */
    public void clearMap()
    {
        ++this.versionStamp;
        IntHashMap.Entry[] aentry = this.slots;

        for (int i = 0; i < aentry.length; ++i)
        {
            aentry[i] = null;
        }

        this.count = 0;
    }

    /**
     * Adds an object to a slot
     */
    private void insert(int p_76040_1_, int p_76040_2_, Object p_76040_3_, int p_76040_4_)
    {
        IntHashMap.Entry entry = this.slots[p_76040_4_];
        this.slots[p_76040_4_] = new IntHashMap.Entry(p_76040_1_, p_76040_2_, p_76040_3_, entry);

        if (this.count++ >= this.threshold)
        {
            this.grow(2 * this.slots.length);
        }
    }

    static class Entry
        {
            /** The hash code of this entry */
            final int hashEntry;
            /** The object stored in this entry */
            Object valueEntry;
            /** The next entry in this slot */
            IntHashMap.Entry nextEntry;
            /** The id of the hash slot computed from the hash */
            final int slotHash;
            private static final String __OBFID = "CL_00001491";

            Entry(int p_i1552_1_, int p_i1552_2_, Object p_i1552_3_, IntHashMap.Entry p_i1552_4_)
            {
                this.valueEntry = p_i1552_3_;
                this.nextEntry = p_i1552_4_;
                this.hashEntry = p_i1552_2_;
                this.slotHash = p_i1552_1_;
            }

            /**
             * Returns the hash code for this entry
             */
            public final int getHash()
            {
                return this.hashEntry;
            }

            /**
             * Returns the object stored in this entry
             */
            public final Object getValue()
            {
                return this.valueEntry;
            }

            public final boolean equals(Object p_equals_1_)
            {
                if (!(p_equals_1_ instanceof IntHashMap.Entry))
                {
                    return false;
                }
                else
                {
                    IntHashMap.Entry entry = (IntHashMap.Entry)p_equals_1_;
                    Integer integer = Integer.valueOf(this.getHash());
                    Integer integer1 = Integer.valueOf(entry.getHash());

                    if (integer == integer1 || integer != null && integer.equals(integer1))
                    {
                        Object object1 = this.getValue();
                        Object object2 = entry.getValue();

                        if (object1 == object2 || object1 != null && object1.equals(object2))
                        {
                            return true;
                        }
                    }

                    return false;
                }
            }

            public final int hashCode()
            {
                return IntHashMap.computeHash(this.hashEntry);
            }

            public final String toString()
            {
                return this.getHash() + "=" + this.getValue();
            }
        }
}