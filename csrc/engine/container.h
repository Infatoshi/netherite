/* Minecraft 1.7.10 inventory clicking: Container, Slot, the containers that
 * matter (player, workbench, furnace, chest) and the inventories behind them.
 *
 * The port is one function per Java method, in the same order, reading the same
 * fields, so a divergence can be found by reading the two side by side:
 *
 *   Container.slotClick          container_slot_click   (modes 0 to 6, and the
 *                                                        clicks the client never
 *                                                        sends)
 *   Container.mergeItemStack     merge_item_stack
 *   Container.transferStackInSlot  per kind, in transfer_*
 *   Slot rules                   slot_is_item_valid / slot_limit / slot_put /
 *                                slot_decr / slot_on_pickup
 *   SlotCrafting.onPickupFromSlot  craft_out_on_pickup   (grid consumption)
 *   SlotFurnace.onPickupFromSlot   furnace_out_on_pickup
 *   Container.onContainerClosed  container_close
 *
 * What is deliberately not here, because the reference probe does not record
 * it and another lane owns it:
 *
 *   - the EntityItem a drop spawns: its motion draws from the entity and world
 *     RNG streams. A drop is recorded as the stack it dropped.
 *   - the EntityXPOrb SlotFurnace.onCrafting spawns for a smelted output, and
 *     the achievements and stats the crafting and smelting paths award
 *     (EntityPlayer.addStat, ItemStack.onCrafting, Item.onCreated). They change
 *     nothing about a slot, a cursor or a return value, so the port skips them;
 *     SlotFurnace's own field_75228_b (the amount smelted since the last
 *     pickup) is still tracked, since the Java code resets it.
 *   - Item.hasContainerItem: it returns false in Item.java and no item in the
 *     1.7.10 registry overrides it, so SlotCrafting's container-item branch is
 *     dead code in vanilla. craft_out_on_pickup keeps the shape of the loop and
 *     notes where the branch would be.
 *
 * Every stack owns its tag. A function that hands a stack back writes into a
 * caller-owned slot that must be empty; craft_stack_free releases it.
 */
#ifndef NETHERITE_CONTAINER_H
#define NETHERITE_CONTAINER_H

#include "crafting.h"
#include "trades.h"

/* A 54-slot double chest plus the player's 36 is the largest container here. */
#define CONTAINER_MAX_SLOTS 90

/* One inventory holds at most a double chest. */
#define INV_MAX_SLOTS 54

/* Clicking one stack into the fire can drop at most one stack per click;
 * vanilla's craft-result shift-click is the only path that could drop more, and
 * only for an item with a container item (see above). */
#define CONTAINER_MAX_DROPS 16

enum inv_kind { INV_PLAYER, INV_CHEST, INV_FURNACE, INV_CRAFT, INV_RESULT, INV_MERCHANT };

enum slot_kind { SLOT_PLAIN, SLOT_CRAFT_OUT, SLOT_FURNACE_OUT, SLOT_ARMOR, SLOT_MERCHANT_OUT };

enum container_kind
{
    CONTAINER_PLAYER, CONTAINER_WORKBENCH, CONTAINER_FURNACE, CONTAINER_CHEST,
    CONTAINER_MERCHANT,
    CONTAINER_DISPENSER,        /* ContainerDispenser: 9 tile slots (a dropper's too) */
    CONTAINER_HOPPER            /* ContainerHopper: 5 tile slots */
};

/* The kinds whose tile slots live in the chest inventory (chest_size of them):
 * the chest, the dispenser (9) and the hopper (5). */
static inline int container_kind_tile_inv(enum container_kind kind)
{
    return kind == CONTAINER_CHEST || kind == CONTAINER_DISPENSER || kind == CONTAINER_HOPPER;
}

struct container;

/* One IInventory. The player's 40 are one array: 0..35 main, 36..39 armor,
 * which is exactly how InventoryPlayer.getStackInSlot and its friends index. */
struct inv {
    enum inv_kind kind;
    int size;
    int limit;
    struct craft_stack slot[INV_MAX_SLOTS];
    struct container *owner;   /* an InventoryCrafting's eventHandler */
};

struct slot {
    struct inv *inv;            /* the inventory slotIndex reads and writes */
    int index;
    int number;                 /* index in the container's slot list */
    enum slot_kind kind;
    int armor_type;             /* SLOT_ARMOR: 0 helmet, 1 chest, 2 legs, 3 boots */
};

struct container {
    enum container_kind kind;
    int nslots;
    char title[64];             /* the S2D title (GuiMerchant's field_147040_A) */
    int window_id;              /* the S2D window id the clicks must answer
                                 * (the player's own container is 0) */
    struct slot slots[CONTAINER_MAX_SLOTS];
    struct inv player;
    struct inv chest;
    struct inv furnace;
    struct inv grid;
    struct inv result;
    struct inv merchant;    /* InventoryMerchant: the 3 stacks (0/1 the buys,
                             * 2 the sell the recipe puts) */
    int chest_size;
    int grid_side;
    int creative;               /* EntityPlayer.capabilities.isCreativeMode */
    int current_recipe_index;   /* InventoryMerchant.currentRecipeIndex */
    int current_recipe;         /* InventoryMerchant.currentRecipe: the recipe
                                 * index the last resetRecipeAndSlots matched,
                                 * -1 when none */

    /* SlotMerchantResult's villager half, on the server's container only:
     * useRecipe(recipe index). NULL on the client's copy. */
    void (*merchant_use)(void *ctx, int recipe_index);
    void *merchant_use_ctx;

    /* IMerchant.func_110297_a_ (the sell slot's yes/no sound, gated on
     * livingSoundTime), called at the end of every resetRecipeAndSlots with
     * the sell stack; NULL on the client's copy. */
    void (*merchant_sound)(void *ctx, const struct craft_stack *sell);
    void *merchant_sound_ctx;

    /* IMerchant.getRecipes (the villager's trade list, or a client mirror);
     * resetRecipeAndSlots and the drawing read it. NULL means no recipes
     * (the client's NpcMerchant before its S3F arrives). */
    const struct trade_list *recipes;
    struct trade_list client_recipes;   /* the client's S3F copy, when
                                         * recipes points at it */

    /* Container's own click state */
    int click_detect;           /* a mode-6 click ran slotClick's own
                                 * detectAndSendChanges (the server reads and
                                 * clears it) */
    int drag_state;             /* field_94536_g: 0 idle, 1 dragging, 2 done */
    int drag_button;            /* field_94535_f: 0 split evenly, 1 one each */
    int drag_slot[CONTAINER_MAX_SLOTS];   /* field_94537_h, the Slots of the drag */
    int drag_count;

    /* the counters the two output slots keep between clicks */
    int amount_crafted;         /* SlotCrafting.amountCrafted */
    int furnace_used;           /* SlotFurnace.field_75228_b */
    int merchant_used;          /* SlotMerchantResult.field_75231_g */
    /* SlotFurnace.onCrafting's server half (the XP orbs), called with the
     * output stack and field_75228_b; NULL on the client's copy */
    void (*furnace_xp)(void *ctx, const struct craft_stack *s, int n);
    void *furnace_xp_ctx;
    /* SlotCrafting.onCrafting's and SlotFurnace.onCrafting's stat half
     * (ItemStack.onCrafting's craftItem counter, then the achievement
     * chain), called with the output stack and the counter BEFORE the
     * counter resets; NULL on the client's copy and on containers with no
     * player. */
    void (*crafted)(void *ctx, const struct craft_stack *s, int n);
    void *crafted_ctx;
    /* SlotMerchantResult.onCrafting(ItemStack)'s ItemStack.onCrafting: the
     * craftItem counter alone (no achievement chain), called with the
     * output stack and field_75231_g before it resets; NULL on the
     * client's copy. */
    void (*merchant_crafted)(void *ctx, const struct craft_stack *s, int n);
    void *merchant_crafted_ctx;

    /* ContainerFurnace's progress bars, ids 0 cook time (field_145961_j),
     * 1 burn time (field_145956_a), 2 the fuel's total (field_145963_i): on
     * the client's container the fields of its own TileEntityFurnace that
     * the S31s write (updateProgressBar); on the server's, the last values
     * detectAndSendChanges sent (lastCookTime, lastBurnTime,
     * lastItemBurnTime). */
    int furnace_progress[3];

    /* InventoryPlayer.getItemStack, the cursor */
    struct craft_stack cursor;

    /* what the last click dropped, in drop order */
    struct craft_stack drops[CONTAINER_MAX_DROPS];
    int ndrops;
};

/* A fresh container of one kind over inventories of the given sizes: the player
 * kind takes no chest, the chest kind 27 or 54 (54 is the double chest); the
 * dispenser and hopper kinds ignore chest_size (9 and 5). The
 * result slot is computed from the grid, which starts empty. */
void container_init(struct container *c, enum container_kind kind, int chest_size, int creative);

/* Releases every stack the container owns. */
void container_free(struct container *c);

/* The contents, by inventory index: the player's 40 (main then armor), the
 * chest's, the furnace's 3, the grid's. Each takes ownership of s, which may be
 * empty; a non-empty s is copied. contract: *s is left empty. */
void container_set_player(struct container *c, int i, struct craft_stack *s);
void container_set_chest(struct container *c, int i, struct craft_stack *s);
void container_set_furnace(struct container *c, int i, struct craft_stack *s);
void container_set_merchant(struct container *c, int i, struct craft_stack *s);
void container_set_grid(struct container *c, int i, struct craft_stack *s);
void container_set_cursor(struct container *c, struct craft_stack *s);

/* The slot's stack, or NULL when the slot is empty. */
struct craft_stack *container_slot(struct container *c, int i);

/* Container.slotClick: returns 1 when the click returned a stack, and moves it
 * into ret (which must be empty). Drops land in c->drops, in order. */
int container_slot_click(struct container *c, int slot, int button, int mode, struct craft_stack *ret);

/* GuiContainer's reads of slot I: Container.func_94527_a(slot, s, true)
 * over the slot's stack HAVE (NULL or empty: an empty slot), Slot.isItemValid
 * with Container.canDragIntoSlot, and Container.func_94530_a (the double
 * click's collect). */
int container_stack_fits(const struct craft_stack *have, const struct craft_stack *s);
int container_slot_accepts(const struct container *c, int i, const struct craft_stack *s);
/* Container.canDragIntoSlot alone (the drag prune at a drawn frame). */
int container_slot_can_drag(const struct container *c, int i);
int container_slot_collectable(const struct container *c, int i);
/* Slot.getSlotStackLimit */
int container_slot_limit(const struct container *c, int i);

/* Container.onContainerClosed and the container's own close. */
void container_close(struct container *c);
/* The same on the client world (GuiContainer.onGuiClosed): the cursor and
 * the player grid's spill land in c->drops; the cursor is left empty. */
void container_close_client(struct container *c);

/* InventoryMerchant.resetRecipeAndSlots: the recipe match over slots 0/1 and
 * the sell into slot 2, with the villager's own func_110297_a_ at the end (the
 * server draws its sound pitch draws; the container's merchant_sound callback
 * carries the server half, NULL on the client). */
void container_merchant_reset(struct container *c);

/* InventoryMerchant.setCurrentRecipeIndex (the C17 MC|TrSel's server half and
 * the GuiMerchant arrow's client half). */
void container_merchant_set_recipe_index(struct container *c, int index);

#endif