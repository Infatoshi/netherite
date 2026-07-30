// Verbatim MC 1.11.2 Entity.move collision extensions (AxisAlignedBB + Entity.move branches for
// stepHeight, sneak step-back, isInWeb, MoverType.PISTON) from net/minecraft/entity/Entity.java
// and net/minecraft/util/math/AxisAlignedBB.java / MathHelper.clamp. World query is a FIXED baked
// block list mirroring core/physics_collision_full.h (web NULL_AABB, ladder thin box, liquid
// pass-through via block_props_table flags). NOT derived from any hand-port.
import java.util.List;
import java.util.ArrayList;

public class Golden {

    static final class AxisAlignedBB {
        public final double minX, minY, minZ, maxX, maxY, maxZ;
        public AxisAlignedBB(double x1, double y1, double z1, double x2, double y2, double z2) {
            this.minX = Math.min(x1, x2); this.minY = Math.min(y1, y2); this.minZ = Math.min(z1, z2);
            this.maxX = Math.max(x1, x2); this.maxY = Math.max(y1, y2); this.maxZ = Math.max(z1, z2);
        }
        public AxisAlignedBB addCoord(double x, double y, double z) {
            double d0 = minX, d1 = minY, d2 = minZ, d3 = maxX, d4 = maxY, d5 = maxZ;
            if (x < 0.0D) d0 += x; else if (x > 0.0D) d3 += x;
            if (y < 0.0D) d1 += y; else if (y > 0.0D) d4 += y;
            if (z < 0.0D) d2 += z; else if (z > 0.0D) d5 += z;
            return new AxisAlignedBB(d0, d1, d2, d3, d4, d5);
        }
        public AxisAlignedBB offset(double x, double y, double z) {
            return new AxisAlignedBB(minX + x, minY + y, minZ + z, maxX + x, maxY + y, maxZ + z);
        }
        public double calculateXOffset(AxisAlignedBB other, double offsetX) {
            if (other.maxY > minY && other.minY < maxY && other.maxZ > minZ && other.minZ < maxZ) {
                if (offsetX > 0.0D && other.maxX <= minX) {
                    double d1 = minX - other.maxX;
                    if (d1 < offsetX) offsetX = d1;
                } else if (offsetX < 0.0D && other.minX >= maxX) {
                    double d0 = maxX - other.minX;
                    if (d0 > offsetX) offsetX = d0;
                }
                return offsetX;
            }
            return offsetX;
        }
        public double calculateYOffset(AxisAlignedBB other, double offsetY) {
            if (other.maxX > minX && other.minX < maxX && other.maxZ > minZ && other.minZ < maxZ) {
                if (offsetY > 0.0D && other.maxY <= minY) {
                    double d1 = minY - other.maxY;
                    if (d1 < offsetY) offsetY = d1;
                } else if (offsetY < 0.0D && other.minY >= maxY) {
                    double d0 = maxY - other.minY;
                    if (d0 > offsetY) offsetY = d0;
                }
                return offsetY;
            }
            return offsetY;
        }
        public double calculateZOffset(AxisAlignedBB other, double offsetZ) {
            if (other.maxX > minX && other.minX < maxX && other.maxY > minY && other.minY < maxY) {
                if (offsetZ > 0.0D && other.maxZ <= minZ) {
                    double d1 = minZ - other.maxZ;
                    if (d1 < offsetZ) offsetZ = d1;
                } else if (offsetZ < 0.0D && other.minZ >= maxZ) {
                    double d0 = maxZ - other.minZ;
                    if (d0 > offsetZ) offsetZ = d0;
                }
                return offsetZ;
            }
            return offsetZ;
        }
        public boolean intersectsWith(AxisAlignedBB other) {
            return minX < other.maxX && maxX > other.minX && minY < other.maxY && maxY > other.minY
                && minZ < other.maxZ && maxZ > other.minZ;
        }
    }

    static final int BF_SOLID = 1;
    static final int BF_LIQUID = 2;

    static final class BlockProps {
        final int flags;
        BlockProps(int flags) { this.flags = flags; }
    }

    static BlockProps bptProps(int id) {
        switch (id) {
            case 1: return new BlockProps(BF_SOLID);
            case 8: return new BlockProps(BF_LIQUID);
            case 30: return new BlockProps(BF_SOLID);
            case 65: return new BlockProps(BF_SOLID);
            default: return new BlockProps(BF_SOLID);
        }
    }

    static final class PcfBlock {
        int blockId;
        double ox, oy, oz;
        int ladderFacing;
        PcfBlock(int id, double x, double y, double z, int facing) {
            blockId = id; ox = x; oy = y; oz = z; ladderFacing = facing;
        }
    }

    static AxisAlignedBB ladderAabb(PcfBlock b) {
        if (b.ladderFacing == 2)
            return new AxisAlignedBB(b.ox, b.oy, b.oz + 0.8125D, b.ox + 1.0D, b.oy + 1.0D, b.oz + 1.0D);
        if (b.ladderFacing == 3)
            return new AxisAlignedBB(b.ox, b.oy, b.oz, b.ox + 1.0D, b.oy + 1.0D, b.oz + 0.1875D);
        if (b.ladderFacing == 4)
            return new AxisAlignedBB(b.ox + 0.8125D, b.oy, b.oz, b.ox + 1.0D, b.oy + 1.0D, b.oz + 1.0D);
        return new AxisAlignedBB(b.ox, b.oy, b.oz, b.ox + 0.1875D, b.oy + 1.0D, b.oz + 1.0D);
    }

    static AxisAlignedBB blockCollisionAabb(PcfBlock b) {
        if (b.blockId == 0) return null;
        if (b.blockId == 30) return null;
        BlockProps p = bptProps(b.blockId);
        if ((p.flags & BF_LIQUID) != 0) return null;
        if (b.blockId == 65) return ladderAabb(b);
        if ((p.flags & BF_SOLID) != 0)
            return new AxisAlignedBB(b.ox, b.oy, b.oz, b.ox + 1.0D, b.oy + 1.0D, b.oz + 1.0D);
        return null;
    }

    static List<AxisAlignedBB> getCollisionBoxes(AxisAlignedBB query, List<PcfBlock> world) {
        List<AxisAlignedBB> list = new ArrayList<AxisAlignedBB>();
        for (int i = 0; i < world.size(); ++i) {
            AxisAlignedBB bb = blockCollisionAabb(world.get(i));
            if (bb != null && query.intersectsWith(bb)) list.add(bb);
        }
        return list;
    }

    static boolean collisionBoxesEmpty(AxisAlignedBB query, List<PcfBlock> world) {
        return getCollisionBoxes(query, world).isEmpty();
    }

    static double clamp(double num, double min, double max) {
        return num < min ? min : (num > max ? max : num);
    }

    static double posX, posY, posZ;
    static double motionX, motionY, motionZ;
    static float stepHeight;
    static boolean onGround, isInWeb, isPlayer, isSneaking;
    static boolean isCollidedHorizontally, isCollidedVertically, isCollided;
    static int moverType;
    static long worldTime;
    static double[] pistonAxis = new double[] {0.0D, 0.0D, 0.0D};
    static long pistonAxisTick = -1L;
    static AxisAlignedBB boundingBox;

    static AxisAlignedBB playerBox(double px, double py, double pz) {
        return new AxisAlignedBB(px - 0.3D, py, pz - 0.3D, px + 0.3D, py + 1.8D, pz + 0.3D);
    }

    static void move(double x, double y, double z, List<PcfBlock> world) {
        double d2 = x;
        double d3 = y;
        double d4 = z;

        if (moverType == 2) {
            long i = worldTime;
            if (i != pistonAxisTick) {
                pistonAxis[0] = 0.0D; pistonAxis[1] = 0.0D; pistonAxis[2] = 0.0D;
                pistonAxisTick = i;
            }
            if (x != 0.0D) {
                int j = 0;
                double d0 = clamp(x + pistonAxis[j], -0.51D, 0.51D);
                x = d0 - pistonAxis[j];
                pistonAxis[j] = d0;
                if (Math.abs(x) <= 9.999999747378752E-6D) return;
            } else if (y != 0.0D) {
                int l4 = 1;
                double d12 = clamp(y + pistonAxis[l4], -0.51D, 0.51D);
                y = d12 - pistonAxis[l4];
                pistonAxis[l4] = d12;
                if (Math.abs(y) <= 9.999999747378752E-6D) return;
            } else {
                if (z == 0.0D) return;
                int i5 = 2;
                double d13 = clamp(z + pistonAxis[i5], -0.51D, 0.51D);
                z = d13 - pistonAxis[i5];
                pistonAxis[i5] = d13;
                if (Math.abs(z) <= 9.999999747378752E-6D) return;
            }
        }

        if (isInWeb) {
            isInWeb = false;
            x *= 0.25D;
            y *= 0.05000000074505806D;
            z *= 0.25D;
            motionX = 0.0D;
            motionY = 0.0D;
            motionZ = 0.0D;
        }

        d2 = x;
        d3 = y;
        d4 = z;

        if ((moverType == 0 || moverType == 1) && onGround && isSneaking && isPlayer) {
            for (double d5 = 0.05D; x != 0.0D && collisionBoxesEmpty(
                    boundingBox.offset(x, (double)(-stepHeight), 0.0D), world); d2 = x) {
                if (x < 0.05D && x >= -0.05D) x = 0.0D;
                else if (x > 0.0D) x -= 0.05D;
                else x += 0.05D;
            }
            for (; z != 0.0D && collisionBoxesEmpty(
                    boundingBox.offset(0.0D, (double)(-stepHeight), z), world); d4 = z) {
                if (z < 0.05D && z >= -0.05D) z = 0.0D;
                else if (z > 0.0D) z -= 0.05D;
                else z += 0.05D;
            }
            for (; x != 0.0D && z != 0.0D && collisionBoxesEmpty(
                    boundingBox.offset(x, (double)(-stepHeight), z), world); d4 = z) {
                if (x < 0.05D && x >= -0.05D) x = 0.0D;
                else if (x > 0.0D) x -= 0.05D;
                else x += 0.05D;
                d2 = x;
                if (z < 0.05D && z >= -0.05D) z = 0.0D;
                else if (z > 0.0D) z -= 0.05D;
                else z += 0.05D;
            }
        }

        List<AxisAlignedBB> list1 = getCollisionBoxes(boundingBox.addCoord(x, y, z), world);
        AxisAlignedBB axisalignedbb = boundingBox;

        if (y != 0.0D) {
            int k = 0;
            for (int l = list1.size(); k < l; ++k)
                y = list1.get(k).calculateYOffset(boundingBox, y);
            boundingBox = boundingBox.offset(0.0D, y, 0.0D);
        }
        if (x != 0.0D) {
            int j5 = 0;
            for (int l5 = list1.size(); j5 < l5; ++j5)
                x = list1.get(j5).calculateXOffset(boundingBox, x);
            if (x != 0.0D) boundingBox = boundingBox.offset(x, 0.0D, 0.0D);
        }
        if (z != 0.0D) {
            int k5 = 0;
            for (int i6 = list1.size(); k5 < i6; ++k5)
                z = list1.get(k5).calculateZOffset(boundingBox, z);
            if (z != 0.0D) boundingBox = boundingBox.offset(0.0D, 0.0D, z);
        }

        boolean flag = onGround || d3 != y && d3 < 0.0D;

        if (stepHeight > 0.0F && flag && (d2 != x || d4 != z)) {
            double d14 = x;
            double d6 = y;
            double d7 = z;
            AxisAlignedBB axisalignedbb1 = boundingBox;
            boundingBox = axisalignedbb;
            y = (double)stepHeight;
            List<AxisAlignedBB> list = getCollisionBoxes(boundingBox.addCoord(d2, y, d4), world);
            AxisAlignedBB axisalignedbb2 = boundingBox;
            AxisAlignedBB axisalignedbb3 = axisalignedbb2.addCoord(d2, 0.0D, d4);
            double d8 = y;
            int j1 = 0;
            for (int k1 = list.size(); j1 < k1; ++j1)
                d8 = list.get(j1).calculateYOffset(axisalignedbb3, d8);
            axisalignedbb2 = axisalignedbb2.offset(0.0D, d8, 0.0D);
            double d18 = d2;
            int l1 = 0;
            for (int i2 = list.size(); l1 < i2; ++l1)
                d18 = list.get(l1).calculateXOffset(axisalignedbb2, d18);
            axisalignedbb2 = axisalignedbb2.offset(d18, 0.0D, 0.0D);
            double d19 = d4;
            int j2 = 0;
            for (int k2 = list.size(); j2 < k2; ++j2)
                d19 = list.get(j2).calculateZOffset(axisalignedbb2, d19);
            axisalignedbb2 = axisalignedbb2.offset(0.0D, 0.0D, d19);
            AxisAlignedBB axisalignedbb4 = boundingBox;
            double d20 = y;
            int l2 = 0;
            for (int i3 = list.size(); l2 < i3; ++l2)
                d20 = list.get(l2).calculateYOffset(axisalignedbb4, d20);
            axisalignedbb4 = axisalignedbb4.offset(0.0D, d20, 0.0D);
            double d21 = d2;
            int j3 = 0;
            for (int k3 = list.size(); j3 < k3; ++j3)
                d21 = list.get(j3).calculateXOffset(axisalignedbb4, d21);
            axisalignedbb4 = axisalignedbb4.offset(d21, 0.0D, 0.0D);
            double d22 = d4;
            int l3 = 0;
            for (int i4 = list.size(); l3 < i4; ++l3)
                d22 = list.get(l3).calculateZOffset(axisalignedbb4, d22);
            axisalignedbb4 = axisalignedbb4.offset(0.0D, 0.0D, d22);
            double d23 = d18 * d18 + d19 * d19;
            double d9 = d21 * d21 + d22 * d22;
            if (d23 > d9) {
                x = d18; z = d19; y = -d8;
                boundingBox = axisalignedbb2;
            } else {
                x = d21; z = d22; y = -d20;
                boundingBox = axisalignedbb4;
            }
            int j4 = 0;
            for (int k4 = list.size(); j4 < k4; ++j4)
                y = list.get(j4).calculateYOffset(boundingBox, y);
            boundingBox = boundingBox.offset(0.0D, y, 0.0D);
            if (d14 * d14 + d7 * d7 >= x * x + z * z) {
                x = d14; y = d6; z = d7;
                boundingBox = axisalignedbb1;
            }
        }

        posX = (boundingBox.minX + boundingBox.maxX) / 2.0D;
        posY = boundingBox.minY;
        posZ = (boundingBox.minZ + boundingBox.maxZ) / 2.0D;

        isCollidedHorizontally = d2 != x || d4 != z;
        isCollidedVertically = d3 != y;
        onGround = isCollidedVertically && d3 < 0.0D;
        isCollided = isCollidedHorizontally || isCollidedVertically;

        if (d2 != x) motionX = 0.0D;
        if (d4 != z) motionZ = 0.0D;
        if (d3 != y) motionY = 0.0D;
    }

    static double dx, dy, dz;

    static List<PcfBlock> scenario(int idx) {
        List<PcfBlock> world = new ArrayList<PcfBlock>();
        motionX = motionY = motionZ = 0.0D;
        onGround = false; isInWeb = false; isPlayer = false; isSneaking = false;
        isCollidedHorizontally = false; isCollidedVertically = false; isCollided = false;
        moverType = 0; worldTime = 0;
        pistonAxis[0] = pistonAxis[1] = pistonAxis[2] = 0.0D;
        pistonAxisTick = -1L;
        stepHeight = 0.0F;

        if (idx == 0) {
            posX = 0.5D; posY = 1.0D; posZ = 0.5D;
            boundingBox = playerBox(posX, posY, posZ);
            stepHeight = 1.0F; onGround = true;
            dx = 0.8D; dy = 0.0D; dz = 0.0D; motionX = 0.8D;
            world.add(new PcfBlock(1, 0.0D, 0.0D, 0.0D, 0));
            world.add(new PcfBlock(1, 1.0D, 1.0D, 0.0D, 0));
        } else if (idx == 1) {
            posX = 1.7D; posY = 1.0D; posZ = 0.5D;
            boundingBox = playerBox(posX, posY, posZ);
            stepHeight = 0.6F; onGround = true; isPlayer = true; isSneaking = true; moverType = 1;
            dx = 0.5D; dy = 0.0D; dz = 0.0D; motionX = 0.5D;
            world.add(new PcfBlock(1, 0.0D, 0.0D, 0.0D, 0));
            world.add(new PcfBlock(1, 1.0D, 0.0D, 0.0D, 0));
        } else if (idx == 2) {
            posX = 0.0D; posY = 1.0D; posZ = 0.0D;
            boundingBox = playerBox(posX, posY, posZ);
            isInWeb = true;
            dx = 1.0D; dy = 0.0D; dz = 0.0D; motionX = 1.0D;
            world.add(new PcfBlock(30, 0.0D, 0.0D, 0.0D, 0));
            world.add(new PcfBlock(1, -1.0D, 0.0D, -1.0D, 0));
        } else if (idx == 3) {
            posX = 0.0D; posY = 1.0D; posZ = 0.5D;
            boundingBox = playerBox(posX, posY, posZ);
            onGround = true;
            dx = 1.5D; dy = 0.0D; dz = 0.0D; motionX = 1.5D;
            world.add(new PcfBlock(1, 0.0D, 0.0D, 0.0D, 0));
            world.add(new PcfBlock(65, 1.0D, 1.0D, 0.0D, 2));
        } else if (idx == 4) {
            posX = 0.0D; posY = 3.0D; posZ = 0.0D;
            boundingBox = playerBox(posX, posY, posZ);
            dx = 0.0D; dy = -2.0D; dz = 0.0D; motionY = -2.0D;
            world.add(new PcfBlock(8, 0.0D, 1.0D, 0.0D, 0));
            world.add(new PcfBlock(1, 0.0D, 0.0D, 0.0D, 0));
        } else if (idx == 5) {
            posX = 0.0D; posY = 1.0D; posZ = 0.0D;
            boundingBox = playerBox(posX, posY, posZ);
            moverType = 2; worldTime = 100L;
            dx = 0.4D; dy = 0.0D; dz = 0.0D;
            world.add(new PcfBlock(1, -1.0D, 0.0D, -1.0D, 0));
        }
        return world;
    }

    static final int NUM_SCENARIOS = 6;

    static void runScenario(int idx, StringBuilder sb) {
        List<PcfBlock> world = scenario(idx);
        if (idx == 5) {
            move(dx, dy, dz, world);
            move(dx, dy, dz, world);
        } else {
            move(dx, dy, dz, world);
        }
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(posX)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(posY)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(posZ)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(motionX)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(motionY)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(motionZ)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.minX)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.minY)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.minZ)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.maxX)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.maxY)));
        sb.append(String.format("%016x%n", Double.doubleToRawLongBits(boundingBox.maxZ)));
        sb.append(String.format("%08x%n", isCollidedHorizontally ? 1 : 0));
        sb.append(String.format("%08x%n", isCollidedVertically ? 1 : 0));
        sb.append(String.format("%08x%n", onGround ? 1 : 0));
        sb.append(String.format("%08x%n", isInWeb ? 1 : 0));
    }

    public static void main(String[] args) {
        StringBuilder sb = new StringBuilder();
        if (args.length > 0) {
            runScenario(Integer.parseInt(args[0]), sb);
        } else {
            for (int i = 0; i < NUM_SCENARIOS; ++i) runScenario(i, sb);
        }
        System.out.print(sb);
    }
}
