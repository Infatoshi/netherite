/* game/container_live.h - live container screen state + Container.slotClick over the
 * FULL player inventory and the open container's slots.
 *
 * PORT: net/minecraft/inventory/Container.java slotClick (all seven ClickType
 *       values)
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
 *   53..79  shared static-container range: first chest/shulker-box half
 *           (27 slots), dispenser/dropper slots 53..61, or hopper slots
 *           53..57. ContainerShulkerBox slots reject nested shulker boxes.
 *   80..84  brewing stand potions 0..2, ingredient, fuel
 *   85..86  enchanting table item and lapis
 *   87..89  enchanting offer buttons (click-only, not stack slots)
 *   90..92  anvil left input, right input, output (output take-only)
 *   93..95  merchant first input, second input, result (result take-only)
 *   96..97  merchant previous/next offer buttons (click-only)
 *   98..114 horse saddle, armor, and up to 15 donkey/mule chest slots
 *   115..141 second half of an open 54-slot InventoryLargeChest
 *   142     beacon payment
 *   143..151 beacon confirm/cancel and effect buttons (click-only)
 *   152     player offhand -> IsrInv 40; player screen only
 *   -999    outside the window (PICKUP drops the cursor)
 *
 * IsrInv tape/set_inventory armor indices remain 36..39 (chest = 38) and are distinct
 * from craft-grid GMC ids 36..44.
 *
 * THROW / outside-drops spawn a REAL item entity at the player (vanilla dropItem),
 * unlike the synthetic 9-slot kernel which discards. Closing a container (walking
 * away, opening another, dying) tosses the cursor and transient input/grid stacks
 * in vanilla order. Persistent chest/furnace/horse contents stay in place. */
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
    GMC_CHEST0      = 53,   /* shared start: chest/shulker 27, dispenser 9, hopper 5 */
    GMC_CHEST_SLOTS = 27,
    GMC_BREWING0    = 80,   /* 80..84 TileEntityBrewingStand slots */
    GMC_BREWING_SLOTS = 5,
    GMC_ENCHANT0    = 85,
    GMC_ENCHANT_SLOTS = 2,
    GMC_ENCHANT_BUTTON0 = 87,
    GMC_ANVIL0      = 90,
    GMC_ANVIL_SLOTS = 3,
    GMC_MERCHANT0   = 93,
    GMC_MERCHANT_SLOTS = 3,
    GMC_MERCHANT_PREV = 96,
    GMC_MERCHANT_NEXT = 97,
    GMC_HORSE0      = 98,
    GMC_HORSE_SLOTS = 17,
    GMC_CHEST_EXTRA0 = 115,
    GMC_LARGE_CHEST_SLOTS = 54,
    GMC_BEACON0     = 142,
    GMC_BEACON_CONFIRM = 143,
    GMC_BEACON_CANCEL = 144,
    GMC_BEACON_POWER0 = 145,
    GMC_BEACON_POWER_COUNT = 7,
    GMC_OFFHAND     = 152,
    GMC_SLOT_COUNT  = 153,
    GMC_OUTSIDE     = -999
};

typedef struct {
    int open;
    int wx, wy, wz;
    int maximum_cost;
    int material_cost;
    int repaired_name; /* runtime-local interned display-name id, 0 = blank */
    ICStack slots[3];  /* two inputs and computed output */
} GmAnvilLive;

void gm_anvil_live_init(GmAnvilLive *a);
void gm_anvil_live_open(GmAnvilLive *a, int wx, int wy, int wz);
void gm_anvil_live_recompute(GmAnvilLive *a, int creative);
void gm_anvil_live_set_name(GmAnvilLive *a, int custom_name, int creative);
int gm_enchantment_can_apply(int enchant_id, int item);
int gm_enchantments_compatible(int first, int second);
int gm_enchantment_max_level(int enchant_id);

/* Execute one Container.slotClick against the runtime. Returns 1 if the click was
 * valid for the currently open container (state may still be unchanged, e.g. a
 * PICKUP on an empty slot with an empty cursor), 0 for an invalid slot id. */
int gm_container_click(struct GmRuntime *r, int slot_id, int button, int click_type);

/* Readable slot/order/inventory identity used by GuiContainer's local gesture
 * sequencer. The returned ordering is the exact Container.inventorySlots order;
 * same-inventory distinguishes tile/player/craft-result backing inventories for
 * vanilla shift-double-click. */
int gm_container_slot_stack(
    const struct GmRuntime *r, int slot_id, ICStack *out);
int gm_container_ordered_slots(
    const struct GmRuntime *r, int out[GMC_SLOT_COUNT]);
int gm_container_slots_same_inventory(
    const struct GmRuntime *r, int first, int second);

/* GuiContainer's shift-double-click release action.  The first/second press
 * already QUICK_MOVEd clicked_slot; match is the saved pre-move stack.  Java
 * then QUICK_MOVEs every takeable matching stack from the same backing
 * inventory, in Container.inventorySlots order.  Returns the number of
 * QUICK_MOVE clicks issued. */
int gm_container_shift_double_click(
    struct GmRuntime *r, int clicked_slot, ICStack match);

/* Current craft-result preview for the open grid (empty stack when no recipe). */
ICStack gm_container_result(const struct GmRuntime *r);

/* Toss cursor plus transient grid/input/payment stacks as live item entities in
 * vanilla cursor-before-container-slot order. Called on close / switch / death;
 * persistent tile and horse contents stay in place. */
void gm_container_close(struct GmRuntime *r);

#endif /* MAGMA_GAME_CONTAINER_LIVE_H */
