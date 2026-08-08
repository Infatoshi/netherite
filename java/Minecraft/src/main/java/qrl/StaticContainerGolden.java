package qrl;

import com.mojang.authlib.GameProfile;
import java.util.UUID;
import net.minecraft.block.state.IBlockState;
import net.minecraft.entity.Entity;
import net.minecraft.entity.player.EntityPlayer;
import net.minecraft.init.Blocks;
import net.minecraft.init.Bootstrap;
import net.minecraft.inventory.ClickType;
import net.minecraft.inventory.Container;
import net.minecraft.inventory.ContainerDispenser;
import net.minecraft.inventory.ContainerHopper;
import net.minecraft.inventory.IInventory;
import net.minecraft.inventory.InventoryBasic;
import net.minecraft.inventory.Slot;
import net.minecraft.item.Item;
import net.minecraft.item.ItemStack;
import net.minecraft.profiler.Profiler;
import net.minecraft.util.math.BlockPos;
import net.minecraft.world.GameType;
import net.minecraft.world.World;
import net.minecraft.world.WorldProviderSurface;
import net.minecraft.world.WorldSettings;
import net.minecraft.world.WorldType;
import net.minecraft.world.chunk.IChunkProvider;
import net.minecraft.world.storage.SaveHandlerMP;
import net.minecraft.world.storage.WorldInfo;

/** Actual 1.11.2 dispenser and hopper container layout/click oracle. */
public final class StaticContainerGolden {
    private static final class MemoryWorld extends World {
        MemoryWorld() {
            super(new SaveHandlerMP(),
                new WorldInfo(new WorldSettings(0L, GameType.SURVIVAL, true,
                    false, WorldType.DEFAULT), "static-container-oracle"),
                new WorldProviderSurface(), new Profiler(), false);
        }
        protected IChunkProvider createChunkProvider() { return null; }
        protected boolean isChunkLoaded(int x, int z, boolean allowEmpty) {
            return true;
        }
        public BlockPos getSpawnPoint() { return new BlockPos(0, 64, 0); }
        public IBlockState getBlockState(BlockPos pos) {
            return Blocks.AIR.getDefaultState();
        }
    }

    private static final class TestPlayer extends EntityPlayer {
        TestPlayer(World world) {
            super(world, new GameProfile(
                new UUID(19L, 23L), "static-container-test"));
        }
        public boolean isSpectator() { return false; }
        public boolean isCreative() { return false; }
        public void onItemPickup(Entity entity, int count) {}
    }

    private static String stack(ItemStack value) {
        return value.isEmpty() ? "0 0 0"
            : Item.getIdFromItem(value.getItem()) + " "
                + value.getCount() + " " + value.getMetadata();
    }

    private static void layout(String tag, Container container) {
        System.out.printf("L %s %d", tag, container.inventorySlots.size());
        for (Slot slot : container.inventorySlots)
            System.out.printf(" %d %d", slot.xPos, slot.yPos);
        System.out.println();
    }

    private static void row(String tag, TestPlayer player,
                            IInventory inventory) {
        StringBuilder line = new StringBuilder(tag);
        for (int slot = 0; slot < inventory.getSizeInventory(); ++slot)
            line.append(' ').append(stack(inventory.getStackInSlot(slot)));
        line.append(' ').append(stack(player.inventory.getItemStack()));
        line.append(' ').append(stack(player.inventory.getStackInSlot(8)));
        System.out.printf("%s%n", line.toString());
    }

    private static void exercise(String tag, TestPlayer player,
                                 IInventory inventory,
                                 Container container, int hotbarStart) {
        player.inventory.setInventorySlotContents(
            0, new ItemStack(Blocks.STONE, 20));
        container.slotClick(hotbarStart, 0, ClickType.QUICK_MOVE, player);
        row(tag + "A", player, inventory);
        container.slotClick(0, 1, ClickType.PICKUP, player);
        row(tag + "B", player, inventory);
        container.slotClick(1, 0, ClickType.PICKUP, player);
        row(tag + "C", player, inventory);
        container.slotClick(0, 0, ClickType.PICKUP, player);
        container.slotClick(1, 0, ClickType.PICKUP, player);
        row(tag + "D", player, inventory);
        container.slotClick(1, 0, ClickType.QUICK_MOVE, player);
        row(tag + "E", player, inventory);
    }

    public static void main(String[] args) {
        Bootstrap.register();
        MemoryWorld world = new MemoryWorld();
        TestPlayer dispenserPlayer = new TestPlayer(world);
        InventoryBasic dispenser = new InventoryBasic("Dispenser", false, 9);
        ContainerDispenser dispenserContainer = new ContainerDispenser(
            dispenserPlayer.inventory, dispenser);
        layout("D", dispenserContainer);
        exercise("D", dispenserPlayer, dispenser, dispenserContainer, 36);

        TestPlayer hopperPlayer = new TestPlayer(world);
        InventoryBasic hopper = new InventoryBasic("Item Hopper", false, 5);
        ContainerHopper hopperContainer = new ContainerHopper(
            hopperPlayer.inventory, hopper, hopperPlayer);
        layout("H", hopperContainer);
        exercise("H", hopperPlayer, hopper, hopperContainer, 32);
    }
}
