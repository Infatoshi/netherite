/* GuiScreen.handleInput over a container screen: GuiContainer's
 * mouseClicked, mouseClickMove, mouseMovedOrUp and keyTyped, one function
 * each in Java's order (gui_input.h). */
#include "gui_input.h"

#include <string.h>

#include "player.h"
#include "survival.h"

void gui_input_reset(struct gui_input *g)
{
    g->event_button = 0;
    g->last_mouse_event = 0;
    g->dragging = 0;
    g->drag_button = 0;
    g->drag_mode = 0;
    g->ndrag = 0;
    g->ignore_release = 1;      /* GuiContainer's constructor */
    g->last_slot = -1;
    g->last_time = 0;
    g->last_button = 0;
    g->double_click = 0;
    g->shift_have = 0;
    g->shift_stack = (struct craft_stack){-1, 0, 0, 0};
    g->screen = NULL;
    g->screen_window = -1;
}

void gui_input_push(struct gui_input *g, const struct gui_event *e)
{
    if (e->kind != GE_KEY)
    {
        g->cur_x = e->x;
        g->cur_y = e->y;
    }
    if (g->n < GUI_INPUT_MAX_EVENTS) g->ev[g->n++] = *e;
}

void gui_input_modifier(struct gui_input *g, int key, int down)
{
    /* one bit per key: isShiftKeyDown is either shift, isCtrlKeyDown either
     * control (either command key on a Mac) */
    int *m = key == GK_LSHIFT || key == GK_RSHIFT ? &g->cur_shift : &g->cur_ctrl;
    int bit = key == GK_LSHIFT || key == GK_LCONTROL || key == GK_LMETA ? 1 : 2;
    *m = down ? *m | bit : *m & ~bit;
}

void gui_input_frame(struct gui_input *g)
{
    g->frame_x = g->cur_x;
    g->frame_y = g->cur_y;
    g->frame_valid = 1;
}

void gui_input_opened(struct gui_input *g)
{
    g->frame_valid = 0;
}

struct gui_screen_layout gui_input_layout(const struct gui_input *g, const struct container *c, int has_effects)
{
    struct gui_screen_layout l = gui_screen_layout(c, g->width, g->height, g->gui_scale);
    gui_screen_effects_shift(&l, c, has_effects);
    return l;
}

/* one tick's run over the open screen */
struct run {
    struct gui_input *g;
    struct client_player *p;
    struct act *a;
    struct client_out *out;
    struct container *c;
    struct gui_screen_layout l;
    int64_t now;
};

/* the slot's stack as the client reads it: the player's own inventory for
 * the player half (the container's copy is refreshed only by a click) */
static struct craft_stack slot_stack(const struct run *r, int i)
{
    const struct slot *sl = &r->c->slots[i];

    if (sl->inv == &r->c->player)
    {
        const struct surv_stack *st = &r->p->sv.inv[sl->index];
        return (struct craft_stack){st->count > 0 ? st->item : -1, st->count,
                                    st->count > 0 ? st->damage : 0, st->count > 0 ? st->tag : 0};
    }
    return sl->inv->slot[sl->index];
}

static int slot_has(const struct run *r, int i)
{
    struct craft_stack s = slot_stack(r, i);
    return s.count > 0 && s.item >= 0;
}

/* InventoryPlayer.getItemStack */
static struct craft_stack cursor(const struct run *r)
{
    const struct surv_stack *st = &r->p->sv.cursor;
    return (struct craft_stack){st->count > 0 ? st->item : -1, st->count,
                                st->count > 0 ? st->damage : 0, st->count > 0 ? st->tag : 0};
}

static int cursor_empty(const struct run *r)
{
    return r->p->sv.cursor.count <= 0;
}

/* func_146975_c: the first slot whose 18x18 box holds the point, -1 none */
static int slot_at(const struct run *r, int x, int y)
{
    int sx = x - r->l.left, sy = y - r->l.top;

    for (int i = 0; i < r->c->nslots; ++i)
    {
        int px, py;
        if (!gui_screen_slot_xy(r->c, i, &px, &py)) continue;
        if (sx >= px - 1 && sx < px + 16 + 1 && sy >= py - 1 && sy < py + 16 + 1) return i;
    }
    return -1;
}

static int outside(const struct run *r, int x, int y)
{
    return x < r->l.left || y < r->l.top || x >= r->l.left + r->l.width || y >= r->l.top + r->l.height;
}

/* func_146984_a: PlayerControllerMP.windowClick, predicted at once */
static void click(struct run *r, int slot, int button, int mode)
{
    if (r->a->clicks >= (int)(sizeof r->a->gui_click / sizeof r->a->gui_click[0]))
    {
        ++r->g->dropped;
        return;
    }
    int i = r->a->clicks++;
    r->a->gui_click[i] = (struct guiclick){r->c->window_id, slot, button, mode};
    surv_client_click(r->p, r->a, i, r->out);
}

/* Container.func_94534_d */
static int drag_code(int stage, int mode)
{
    return (stage & 3) | (mode & 3) << 2;
}

/* GuiMerchant's arrow buttons (GuiScreen.mouseClicked's button loop, left
 * button only): actionPerformed's index change and its C17, the ["trsel"] op */
static void merchant_buttons(struct run *r, int x, int y)
{
    struct container *c = r->c;

    if (c->kind != CONTAINER_MERCHANT || c->recipes == NULL) return;
    int sx = x - r->l.left, sy = y - r->l.top, index = -1;
    if (sx >= 147 && sx < 159 && sy >= 23 && sy < 42) index = c->current_recipe_index + 1;
    else if (sx >= 17 && sx < 29 && sy >= 23 && sy < 42) index = c->current_recipe_index - 1;
    if (index < 0 || index >= (int)c->recipes->n) return;   /* the button is disabled */
    if (r->a->trsels >= (int)(sizeof r->a->trsel / sizeof r->a->trsel[0])) return;
    r->a->trsel[r->a->trsels].window = c->window_id;
    r->a->trsel[r->a->trsels].index = index;
    r->a->trsel[r->a->trsels].at = r->a->clicks;
    ++r->a->trsels;
}

static void mouse_clicked(struct run *r, int x, int y, int button)
{
    struct gui_input *g = r->g;

    if (button == 0) merchant_buttons(r, x, y);
    int slot = slot_at(r, x, y);
    int64_t now = r->now;
    g->double_click = g->last_slot == slot && now - g->last_time < 250 && g->last_button == button;
    g->ignore_release = 0;

    if (button == 0 || button == 1 || button == 2)
    {
        int n = slot >= 0 ? slot : -1;
        if (outside(r, x, y)) n = -999;
        if (n != -1 && !g->dragging)
        {
            if (cursor_empty(r))
            {
                if (button == 2) click(r, n, button, 3);
                else
                {
                    int shift = n != -999 && g->cur_shift;
                    int mode = 0;
                    if (shift)
                    {
                        g->shift_have = slot >= 0 && slot_has(r, slot);
                        if (g->shift_have) g->shift_stack = slot_stack(r, slot);
                        mode = 1;
                    }
                    else if (n == -999) mode = 4;
                    click(r, n, button, mode);
                }
                g->ignore_release = 1;
            }
            else
            {
                g->dragging = 1;
                g->drag_button = button;
                g->ndrag = 0;
                if (button == 0) g->drag_mode = 0;
                else if (button == 1) g->drag_mode = 1;
            }
        }
    }
    g->last_slot = slot;
    g->last_time = now;
    g->last_button = button;
}

static void mouse_click_move(struct run *r, int x, int y)
{
    struct gui_input *g = r->g;
    int slot = slot_at(r, x, y);

    if (!g->dragging || slot < 0 || cursor_empty(r)) return;
    struct craft_stack cur = cursor(r), have = slot_stack(r, slot);
    if (cur.count <= g->ndrag) return;
    if (!container_stack_fits(&have, &cur) || !container_slot_accepts(r->c, slot, &cur)) return;
    for (int i = 0; i < g->ndrag; ++i)
        if (g->drag[i] == slot) return;   /* a Set */
    if (g->ndrag < CONTAINER_MAX_SLOTS) g->drag[g->ndrag++] = slot;
}

static void mouse_moved_or_up(struct run *r, int x, int y, int button)
{
    struct gui_input *g = r->g;
    int slot = slot_at(r, x, y);
    int n = slot >= 0 ? slot : -1;

    if (outside(r, x, y)) n = -999;
    if (g->double_click && slot >= 0 && button == 0 && container_slot_collectable(r->c, slot))
    {
        if (g->cur_shift)
        {
            if (g->shift_have)
                for (int i = 0; i < r->c->nslots; ++i)
                {
                    if (!slot_has(r, i) || r->c->slots[i].inv != r->c->slots[slot].inv) continue;
                    struct craft_stack have = slot_stack(r, i);
                    if (container_stack_fits(&have, &g->shift_stack)) click(r, i, button, 1);
                }
        }
        else click(r, n, button, 6);
        g->double_click = 0;
        g->last_time = 0;
    }
    else
    {
        if (g->dragging && g->drag_button != button)
        {
            g->dragging = 0;
            g->ndrag = 0;
            g->ignore_release = 1;
            return;
        }
        if (g->ignore_release)
        {
            g->ignore_release = 0;
            return;
        }
        if (g->dragging && g->ndrag > 0)
        {
            click(r, -999, drag_code(0, g->drag_mode), 5);
            for (int i = 0; i < g->ndrag; ++i) click(r, g->drag[i], drag_code(1, g->drag_mode), 5);
            click(r, -999, drag_code(2, g->drag_mode), 5);
        }
        else if (!cursor_empty(r))
        {
            if (button == 2) click(r, n, button, 3);
            else
            {
                int shift = n != -999 && g->cur_shift;
                if (shift)
                {
                    g->shift_have = slot >= 0 && slot_has(r, slot);
                    if (g->shift_have) g->shift_stack = slot_stack(r, slot);
                }
                click(r, n, button, shift ? 1 : 0);
            }
        }
    }
    if (cursor_empty(r)) g->last_time = 0;
    g->dragging = 0;
    g->ndrag = 0;
}

/* keyTyped; 1 when the key closed the screen */
static int key_typed(struct run *r, int key, int hovered)
{
    if (key == GK_ESCAPE || key == GK_E)
    {
        r->a->gui_close = 1;
        if (r->a->closes < (int)(sizeof r->a->close_at / sizeof r->a->close_at[0]))
            r->a->close_at[r->a->closes++] = r->a->clicks;
        return 1;
    }
    /* func_146983_a: the hotbar keys over the hovered slot */
    if (cursor_empty(r) && hovered >= 0 && key >= GK_1 && key <= GK_9)
    {
        click(r, hovered, key - GK_1, 2);
        return 0;
    }
    if (hovered >= 0 && slot_has(r, hovered) && key == GK_Q)
        click(r, hovered, r->g->cur_ctrl ? 1 : 0, 4);
    return 0;
}

void gui_input_run(struct gui_input *g, struct client_player *p, struct act *a,
                   struct client_out *out, int64_t now)
{
    struct container *c = cp_gui_container(p);
    int nev = g->n;

    g->n = 0;
    g->nafter = 0;
    if (c == NULL || !p->screen_inventory)
    {
        gui_input_reset(g);   /* the next screen is a new GuiContainer */
        return;
    }
    if (g->screen != c || g->screen_window != c->window_id)
    {
        gui_input_reset(g);
        g->screen = c;
        g->screen_window = c->window_id;
    }
    struct run r = {g, p, a, out, c, gui_input_layout(g, c, p->sv.potions.size > 0), now};

    /* GuiScreen.handleInput: the Mouse queue, then the Keyboard queue */
    for (int i = 0; i < nev; ++i)
    {
        const struct gui_event *e = &g->ev[i];
        if (e->kind == GE_PRESS)
        {
            g->event_button = e->button;
            g->last_mouse_event = now;
            mouse_clicked(&r, e->x, e->y, e->button);
        }
        else if (e->kind == GE_RELEASE)
        {
            g->event_button = -1;
            mouse_moved_or_up(&r, e->x, e->y, e->button);
        }
        else if (e->kind == GE_MOVE && g->event_button != -1 && g->last_mouse_event > 0)
            mouse_click_move(&r, e->x, e->y);
    }
    /* drawScreen's field_147006_u: the slot under the last frame's pointer */
    int hovered = g->frame_valid ? slot_at(&r, g->frame_x, g->frame_y) : -1;
    for (int i = 0; i < nev; ++i)
        if (g->ev[i].kind == GE_KEY && key_typed(&r, g->ev[i].key, hovered))
        {
            /* the screen is closed (closeScreen inside keyTyped), but
             * handleInput's loop goes on: its later keys are the closed
             * screen's, once the client has closed it */
            for (int j = i + 1; j < nev; ++j)
                if (g->ev[j].kind == GE_KEY && g->nafter < (int)(sizeof g->after_keys / sizeof g->after_keys[0]))
                    g->after_keys[g->nafter++] = g->ev[j].key;
            g->after_window = c->window_id;
            g->after_hovered = hovered;
            g->after_own = c == &p->own_container;
            g->after_has = hovered >= 0 && slot_has(&r, hovered);
            break;
        }
}

/* a closed screen's click: the old window's id and slot, the prediction on
 * the inventory container (PlayerControllerMP.windowClick's
 * player.openContainer.slotClick) */
static void closed_click(struct gui_input *g, struct client_player *p, struct act *a, struct client_out *out,
                         int button, int mode)
{
    if (a->clicks >= (int)(sizeof a->gui_click / sizeof a->gui_click[0]))
    {
        ++g->dropped;
        return;
    }
    int i = a->clicks++;
    a->gui_click[i] = (struct guiclick){g->after_window, g->after_hovered, button, mode};
    surv_client_click_closed(p, a, i, out);
}

void gui_input_closed(struct gui_input *g, struct client_player *p, struct act *a, struct client_out *out)
{
    for (int i = 0; i < g->nafter; ++i)
    {
        int key = g->after_keys[i];
        if (key == GK_ESCAPE || key == GK_E)
        {
            /* closeScreen again: another C0D */
            if (a->closes < (int)(sizeof a->close_at / sizeof a->close_at[0])) a->close_at[a->closes++] = a->clicks;
            continue;
        }
        if (g->after_hovered < 0) continue;
        /* func_146983_a: the close emptied the cursor */
        if (key >= GK_1 && key <= GK_9)
        {
            closed_click(g, p, a, out, key - GK_1, 2);
            continue;
        }
        if (key != GK_Q) continue;
        /* the hovered Slot's getHasStack: the inventory container's own
         * slot as it is now, another window's as the close left it */
        int has = g->after_has;
        if (g->after_own)
        {
            struct run r = {g, p, a, out, &p->own_container, {0}, 0};
            has = g->after_hovered < p->own_container.nslots && slot_has(&r, g->after_hovered);
        }
        if (has) closed_click(g, p, a, out, g->cur_ctrl ? 1 : 0, 4);
    }
    g->nafter = 0;
}

void gui_input_drawn(struct gui_input *g, struct client_player *p)
{
    struct container *c = cp_gui_container(p);

    if (c == NULL || !p->screen_inventory || g->screen != c || g->screen_window != c->window_id) return;
    if (!g->dragging || g->ndrag == 0 || p->sv.cursor.count <= 0) return;
    struct run r = {g, p, NULL, NULL, c, gui_input_layout(g, c, p->sv.potions.size > 0), 0};
    struct craft_stack cur = cursor(&r);

    /* drawScreen's slot loop: func_146977_a over each slot in container
     * order; one in the drag set that no longer takes the cursor
     * (Container.func_94527_a with true, canDragIntoSlot) leaves it, and
     * func_146980_g recounts the remainder the frame draws. A set of one
     * returns before the test. */
    for (int i = 0; i < c->nslots; ++i)
    {
        int k = 0;
        while (k < g->ndrag && g->drag[k] != i) ++k;
        if (k == g->ndrag) continue;
        if (g->ndrag == 1) continue;
        struct craft_stack have = slot_stack(&r, i);
        if (container_stack_fits(&have, &cur) && container_slot_can_drag(c, i)) continue;
        memmove(&g->drag[k], &g->drag[k + 1], (size_t)(g->ndrag - k - 1) * sizeof g->drag[0]);
        --g->ndrag;
    }
}

int gui_input_drag(const struct gui_input *g, const int **slots, int *n, int *mode)
{
    if (!g->dragging || g->ndrag == 0) return 0;
    *slots = g->drag;
    *n = g->ndrag;
    *mode = g->drag_mode;
    return 1;
}
