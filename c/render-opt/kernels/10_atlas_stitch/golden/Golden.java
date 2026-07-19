/* GOLDEN: MC texture-atlas Stitcher bin-packer, run on a deterministic sprite sequence.
 * Source: src/net/minecraft/client/renderer/texture/Stitcher.java
 *   allocateSlot :110 / expandAndAllocateSlot :141 / Holder :201 / Slot :287 / getMipmapDimension :102
 * (smallestEncompassingPowerOfTwo is MathHelper :331, copied below).
 *
 * The only changes vs. the decompiled source are dependency-stripping so it runs standalone:
 *  - TextureAtlasSprite is replaced by a tiny Holder backing record carrying (width,height,name).
 *  - mipmapLevelStitcher = 0 and maxTileDimension = 0, which makes getMipmapDimension the identity
 *    and leaves scaleFactor = 1.0 (no setNewDimension). The bin-packing logic itself is VERBATIM.
 *  - The Forge ProgressManager / StitcherException calls in doStitch are removed; an unfittable
 *    sprite throws a plain RuntimeException instead.
 *
 * Driver: stdin line 1 = "maxWidth maxHeight count"; next `count` lines = "width height name".
 * Output: one line per placed sprite, "name originX originY width height rotated", sorted by name
 * (rotated = 1/0). width/height are the placed (post-rotation) dimensions. */
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.HashSet;

public class Golden {

    // ---- MathHelper.smallestEncompassingPowerOfTwo (verbatim) ----
    private static int smallestEncompassingPowerOfTwo(int value) {
        int i = value - 1;
        i = i | i >> 1;
        i = i | i >> 2;
        i = i | i >> 4;
        i = i | i >> 8;
        i = i | i >> 16;
        return i + 1;
    }

    // ---- Stitcher (verbatim bin-packing; deps stripped) ----
    static class Stitcher {
        private final int mipmapLevelStitcher;
        private final Set<Holder> setStitchHolders = new HashSet<Holder>();
        private final List<Slot> stitchSlots = new ArrayList<Slot>();
        private int currentWidth;
        private int currentHeight;
        private final int maxWidth;
        private final int maxHeight;
        private final int maxTileDimension;

        public Stitcher(int maxWidthIn, int maxHeightIn, int maxTileDimensionIn, int mipmapLevelStitcherIn) {
            this.mipmapLevelStitcher = mipmapLevelStitcherIn;
            this.maxWidth = maxWidthIn;
            this.maxHeight = maxHeightIn;
            this.maxTileDimension = maxTileDimensionIn;
        }

        public void addSprite(int w, int h, String name) {
            Holder stitcher$holder = new Holder(w, h, name, this.mipmapLevelStitcher);
            if (this.maxTileDimension > 0) {
                stitcher$holder.setNewDimension(this.maxTileDimension);
            }
            this.setStitchHolders.add(stitcher$holder);
        }

        public void doStitch() {
            Holder[] astitcher$holder = this.setStitchHolders.toArray(new Holder[this.setStitchHolders.size()]);
            java.util.Arrays.sort(astitcher$holder);

            for (Holder stitcher$holder : astitcher$holder) {
                if (!this.allocateSlot(stitcher$holder)) {
                    throw new RuntimeException("Unable to fit: " + stitcher$holder);
                }
            }

            this.currentWidth = smallestEncompassingPowerOfTwo(this.currentWidth);
            this.currentHeight = smallestEncompassingPowerOfTwo(this.currentHeight);
        }

        public List<Slot> getStichSlots() {
            List<Slot> list = new ArrayList<Slot>();
            for (Slot stitcher$slot : this.stitchSlots) {
                stitcher$slot.getAllStitchSlots(list);
            }
            return list;
        }

        private static int getMipmapDimension(int p_147969_0_, int p_147969_1_) {
            return (p_147969_0_ >> p_147969_1_) + ((p_147969_0_ & (1 << p_147969_1_) - 1) == 0 ? 0 : 1) << p_147969_1_;
        }

        private boolean allocateSlot(Holder p_94310_1_) {
            boolean flag = p_94310_1_.iconWidth != p_94310_1_.iconHeight;

            for (int i = 0; i < this.stitchSlots.size(); ++i) {
                if (this.stitchSlots.get(i).addSlot(p_94310_1_)) {
                    return true;
                }

                if (flag) {
                    p_94310_1_.rotate();

                    if (this.stitchSlots.get(i).addSlot(p_94310_1_)) {
                        return true;
                    }

                    p_94310_1_.rotate();
                }
            }

            return this.expandAndAllocateSlot(p_94310_1_);
        }

        private boolean expandAndAllocateSlot(Holder p_94311_1_) {
            int i = Math.min(p_94311_1_.getWidth(), p_94311_1_.getHeight());
            int j = Math.max(p_94311_1_.getWidth(), p_94311_1_.getHeight());
            int k = smallestEncompassingPowerOfTwo(this.currentWidth);
            int l = smallestEncompassingPowerOfTwo(this.currentHeight);
            int i1 = smallestEncompassingPowerOfTwo(this.currentWidth + i);
            int j1 = smallestEncompassingPowerOfTwo(this.currentHeight + i);
            boolean flag1 = i1 <= this.maxWidth;
            boolean flag2 = j1 <= this.maxHeight;

            if (!flag1 && !flag2) {
                return false;
            } else {
                boolean flag3 = flag1 && k != i1;
                boolean flag4 = flag2 && l != j1;
                boolean flag;

                if (flag3 ^ flag4) {
                    flag = !flag3 && flag1; //Forge: Fix stitcher not expanding entire height before growing width, and {potentially} growing larger then the max size.
                } else {
                    flag = flag1 && k <= l;
                }

                Slot stitcher$slot;

                if (flag) {
                    if (p_94311_1_.getWidth() > p_94311_1_.getHeight()) {
                        p_94311_1_.rotate();
                    }

                    if (this.currentHeight == 0) {
                        this.currentHeight = p_94311_1_.getHeight();
                    }

                    stitcher$slot = new Slot(this.currentWidth, 0, p_94311_1_.getWidth(), this.currentHeight);
                    this.currentWidth += p_94311_1_.getWidth();
                } else {
                    stitcher$slot = new Slot(0, this.currentHeight, this.currentWidth, p_94311_1_.getHeight());
                    this.currentHeight += p_94311_1_.getHeight();
                }

                stitcher$slot.addSlot(p_94311_1_);
                this.stitchSlots.add(stitcher$slot);
                return true;
            }
        }

        // ---- Holder (verbatim, TextureAtlasSprite -> plain fields) ----
        static class Holder implements Comparable<Holder> {
            final int iconWidth;
            final int iconHeight;
            private final int width;
            private final int height;
            private final String name;
            private final int mipmapLevelHolder;
            private boolean rotated;
            private float scaleFactor = 1.0F;

            public Holder(int w, int h, String nameIn, int mipmapLevelHolderIn) {
                this.iconWidth = w;
                this.iconHeight = h;
                this.width = w;
                this.height = h;
                this.name = nameIn;
                this.mipmapLevelHolder = mipmapLevelHolderIn;
                this.rotated = Stitcher.getMipmapDimension(this.height, mipmapLevelHolderIn) > Stitcher.getMipmapDimension(this.width, mipmapLevelHolderIn);
            }

            public int getWidth() {
                int i = this.rotated ? this.height : this.width;
                return Stitcher.getMipmapDimension((int)((float)i * this.scaleFactor), this.mipmapLevelHolder);
            }

            public int getHeight() {
                int i = this.rotated ? this.width : this.height;
                return Stitcher.getMipmapDimension((int)((float)i * this.scaleFactor), this.mipmapLevelHolder);
            }

            public void rotate() {
                this.rotated = !this.rotated;
            }

            public boolean isRotated() {
                return this.rotated;
            }

            public void setNewDimension(int p_94196_1_) {
                if (this.width > p_94196_1_ && this.height > p_94196_1_) {
                    this.scaleFactor = (float)p_94196_1_ / (float)Math.min(this.width, this.height);
                }
            }

            public String toString() {
                return "Holder{width=" + this.width + ", height=" + this.height + ", name=" + this.name + '}';
            }

            public int compareTo(Holder p_compareTo_1_) {
                int i;

                if (this.getHeight() == p_compareTo_1_.getHeight()) {
                    if (this.getWidth() == p_compareTo_1_.getWidth()) {
                        if (this.name == null) {
                            return p_compareTo_1_.name == null ? 0 : -1;
                        }

                        return this.name.compareTo(p_compareTo_1_.name);
                    }

                    i = this.getWidth() < p_compareTo_1_.getWidth() ? 1 : -1;
                } else {
                    i = this.getHeight() < p_compareTo_1_.getHeight() ? 1 : -1;
                }

                return i;
            }
        }

        // ---- Slot (verbatim) ----
        static class Slot {
            private final int originX;
            private final int originY;
            private final int width;
            private final int height;
            private List<Slot> subSlots;
            private Holder holder;

            public Slot(int originXIn, int originYIn, int widthIn, int heightIn) {
                this.originX = originXIn;
                this.originY = originYIn;
                this.width = widthIn;
                this.height = heightIn;
            }

            public Holder getStitchHolder() {
                return this.holder;
            }

            public int getOriginX() {
                return this.originX;
            }

            public int getOriginY() {
                return this.originY;
            }

            public boolean addSlot(Holder holderIn) {
                if (this.holder != null) {
                    return false;
                } else {
                    int i = holderIn.getWidth();
                    int j = holderIn.getHeight();

                    if (i <= this.width && j <= this.height) {
                        if (i == this.width && j == this.height) {
                            this.holder = holderIn;
                            return true;
                        } else {
                            if (this.subSlots == null) {
                                this.subSlots = new ArrayList<Slot>(1);
                                this.subSlots.add(new Slot(this.originX, this.originY, i, j));
                                int k = this.width - i;
                                int l = this.height - j;

                                if (l > 0 && k > 0) {
                                    int i1 = Math.max(this.height, k);
                                    int j1 = Math.max(this.width, l);

                                    if (i1 >= j1) {
                                        this.subSlots.add(new Slot(this.originX, this.originY + j, i, l));
                                        this.subSlots.add(new Slot(this.originX + i, this.originY, k, this.height));
                                    } else {
                                        this.subSlots.add(new Slot(this.originX + i, this.originY, k, j));
                                        this.subSlots.add(new Slot(this.originX, this.originY + j, this.width, l));
                                    }
                                } else if (k == 0) {
                                    this.subSlots.add(new Slot(this.originX, this.originY + j, i, l));
                                } else if (l == 0) {
                                    this.subSlots.add(new Slot(this.originX + i, this.originY, k, j));
                                }
                            }

                            for (Slot stitcher$slot : this.subSlots) {
                                if (stitcher$slot.addSlot(holderIn)) {
                                    return true;
                                }
                            }

                            return false;
                        }
                    } else {
                        return false;
                    }
                }
            }

            public void getAllStitchSlots(List<Slot> p_94184_1_) {
                if (this.holder != null) {
                    p_94184_1_.add(this);
                } else if (this.subSlots != null) {
                    for (Slot stitcher$slot : this.subSlots) {
                        stitcher$slot.getAllStitchSlots(p_94184_1_);
                    }
                }
            }
        }
    }

    public static void main(String[] args) throws Exception {
        BufferedReader br = new BufferedReader(new InputStreamReader(System.in));
        String[] hdr = br.readLine().trim().split("\\s+");
        int maxW = Integer.parseInt(hdr[0]);
        int maxH = Integer.parseInt(hdr[1]);
        int count = Integer.parseInt(hdr[2]);

        Stitcher st = new Stitcher(maxW, maxH, 0, 0);
        for (int n = 0; n < count; n++) {
            String[] t = br.readLine().trim().split("\\s+");
            int w = Integer.parseInt(t[0]);
            int h = Integer.parseInt(t[1]);
            String name = t[2];
            st.addSprite(w, h, name);
        }
        st.doStitch();

        List<String> lines = new ArrayList<String>();
        for (Stitcher.Slot slot : st.getStichSlots()) {
            Stitcher.Holder hld = slot.getStitchHolder();
            lines.add(hld.name + " " + slot.getOriginX() + " " + slot.getOriginY()
                    + " " + hld.getWidth() + " " + hld.getHeight()
                    + " " + (hld.isRotated() ? 1 : 0));
        }
        Collections.sort(lines);
        StringBuilder sb = new StringBuilder();
        for (String s : lines) sb.append(s).append('\n');
        System.out.print(sb);
    }
}
