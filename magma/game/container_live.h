/* game/container_live.h - live container screen state + Container.slotClick over the
 * FULL player inventory and the open container's slots.
 *
 * PORT: net/minecraft/inventory/Container.java slotClick (PICKUP / QUICK_MOVE / THROW)
 *       + mergeItemStack(start,end,reverse), ContainerPlayer / ContainerWorkbench /
 *       ContainerFurnace / ContainerChest transferStackInSlot target ordering,
 *       SlotCrafting take (consume one per grid cell), SlotFurnaceFuel validity,
 *       ContainerPlayer armor slots (stack limit 1 + isValidArmor).
 *
 * This is the ONE shipped click path: the windowed screen and the JSONL harness both
 * reach it through GmAction.inv_click -> gm_runtime_tick. The 9-slot verified kernel
 * (blaze container_click.h) supplies the stack helpers; this module owns the unified
 * slot-id space and the runtime-backed slots:
 *
 *   0..35   player inventory (isr layout: 0..8 hotbar, 9..35 main)
 *   36..44  craft grid, row-major 3x3. Player screen (no container open) exposes the
 *           vanilla 2x2 = cells 36,37,39,40; a crafting table exposes all 9.
 *   45      craft result (take-only; consumes one item per non-empty grid cell)
 *   46..48  open furnace input/fuel/output (fuel slot rejects non-fuel, output is
 *           take-only; insert/extract go through the verified furnace_live wrappers)
 *   49..52  player armor GUI (feet/legs/chest/head) -> IsrInv 36..39; player screen only
 *   53..79  open single chest (27 slots; ContainerChest)
 *   80..84  brewing stand potions 0..2, ingredient, fuel
 *   85..86  enchanting table item and lapis
 *   87..89  enchanting offer buttons (click-only, not stack slots)
 *   -999    outside the window (PICKUP drops the cursor)
 *
 * IsrInv tape/set_inventory armor indices remain 36..39 (chest = 38) and are distinct
 * from craft-grid GMC ids 36..44.
 *
 * THROW / outside-drops spawn a REAL item entity at the player (vanilla dropItem),
 * unlike the synthetic 9-slot kernel which discards. Closing a container (walking
 * away, opening another, dying) returns grid + cursor stacks to the inventory and
 * drops whatever does not fit. Chest contents persist in the TE. */
#ifndef MAGMA_GAME_CONTAINER_LIVE_H
#define MAGMA_GAME_CONTAINER_LIVE_H

#include "items_core.h"

struct GmRuntime;

enum {
    GMC_INV_SLOTS   = 36,
    GMC_GRID0       = 36,
    GMC_RESULT      = 45,
    GMC_FURNACE0    = 46,   /* 46 input, 47 fuel, 48 output */
    GMC_ARMOR0      = 49,   /* 49 feet, 50 legs, 51 chest, 52 head -> isr 36..39 */
    GMC_CHEST0      = 53,   /* 53..79 single-chest 27 slots */
    GMC_CHEST_SLOTS = 27,
    GMC_BREWING0    = 80,   /* 80..84 TileEntityBrewingStand slots */
    GMC_BREWING_SLOTS = 5,
    GMC_ENCHANT0    = 85,
    GMC_ENCHANT_SLOTS = 2,
    GMC_ENCHANT_BUTTON0 = 87,
    GMC_SLOT_COUNT  = 90,
    GMC_OUTSIDE     = -999
};

/* Execute one Container.slotClick against the runtime. Returns 1 if the click was
 * valid for the currently open container (state may still be unchanged, e.g. a
 * PICKUP on an empty slot with an empty cursor), 0 for an invalid slot id. */
int gm_container_click(struct GmRuntime *r, int slot_id, int button, int click_type);

/* Current craft-result preview for the open grid (empty stack when no recipe). */
ICStack gm_container_result(const struct GmRuntime *r);

/* Return grid + cursor stacks to the inventory; drop what does not fit as live
 * item entities at the player. Called on container close / switch / death.
 * Chest TE contents are left in place (only open-count / lid is decremented by
 * the runtime). */
void gm_container_close(struct GmRuntime *r);

#endif /* MAGMA_GAME_CONTAINER_LIVE_H */
