/* The binding's action compiler (rlact.h). */
#define _GNU_SOURCE
#include "rlact.h"

#include <limits.h>
#include <string.h>

#include "../engine/container.h"
#include "../engine/items.h"
#include "../engine/player.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "../engine/recipes.h"
#pragma GCC diagnostic pop
#include "../engine/session.h"
#include "../engine/tileticks.h"
#include "rlact_int.h"
#include "rlgui.h"

/* ------------------------------------------------------------ the screen */

/* the open container screen as the client shows it */
struct scr {
    struct container *c;
    const struct client_player *cp;
    int screen, n, window;
    struct nwrl_slotinfo inf[90];
};

static int scr_get(struct session *ss, struct scr *v)
{
    struct client_player *cp = &ss->cp;
    if (cp->screen_gameover || cp->screen_sleep || cp->screen_chat || cp->screen_credits || !cp->screen_inventory)
        return 0;
    struct container *c = cp_gui_container(cp);
    if (c == NULL) return 0;
    switch (c->kind)
    {
    case CONTAINER_WORKBENCH: v->screen = PS_CRAFTING; break;
    case CONTAINER_CHEST: v->screen = PS_CHEST; break;
    case CONTAINER_FURNACE: v->screen = PS_FURNACE; break;
    case CONTAINER_MERCHANT: v->screen = PS_MERCHANT; break;
    case CONTAINER_DISPENSER: v->screen = PS_DISPENSER; break;
    case CONTAINER_HOPPER: v->screen = PS_HOPPER; break;
    default: v->screen = PS_INVENTORY; break;
    }
    v->c = c;
    v->cp = cp;
    v->n = c->nslots < 90 ? c->nslots : 90;
    v->window = c->window_id;
    if (nwrl_screen_slots(v->screen, c->nslots, v->inf, 90, NULL, NULL) != c->nslots) memset(v->inf, 0, sizeof v->inf);
    return 1;
}

static const struct craft_stack EMPTY = {-1, 0, 0, 0};

static struct craft_stack surv(const struct surv_stack *s)
{
    return s->count > 0 && s->item >= 0 ? (struct craft_stack){s->item, s->count, s->damage, s->tag} : EMPTY;
}

/* slot i as the client shows it: the player's half is the client's own
 * stacks (the container's copy follows them only at a click, survival.c
 * client_click_on), the cursor the client's own */
static struct craft_stack sget(const struct scr *v, int i)
{
    if (i < 0 || i >= v->c->nslots) return EMPTY;
    const struct slot *sl = &v->c->slots[i];
    if (sl->inv == &v->c->player && sl->index >= 0 && sl->index < 40) return surv(&v->cp->sv.inv[sl->index]);
    const struct craft_stack *s = container_slot(v->c, i);
    return s == NULL || s->item < 0 || s->count <= 0 ? EMPTY : *s;
}

static struct craft_stack cursor(const struct scr *v) { return surv(&v->cp->sv.cursor); }

static int empty(struct craft_stack s) { return s.item < 0; }
static int same(struct craft_stack a, struct craft_stack b)
{
    return a.item >= 0 && a.item == b.item && a.damage == b.damage && a.tag == b.tag;
}
static int role(const struct scr *v, int i) { return i >= 0 && i < v->n ? v->inf[i].role : NR_NONE; }
static int player_side(int r) { return r == NR_INV || r == NR_HOTBAR; }
static int output(int r) { return r == NR_RESULT || r == NR_FURN_OUT || r == NR_TRADE_OUT; }
static int storage(int r) { return r == NR_CHEST || r == NR_TILE; }

static int slot_max(const struct scr *v, int i, struct craft_stack s)
{
    int m = s.item >= 0 ? ITEMS[s.item & 4095].max_stack_size : 64, l = container_slot_limit(v->c, i);
    return m < l ? m : l;
}

/* how much of stack s slot i takes */
static int room(const struct scr *v, int i, struct craft_stack s)
{
    if (output(role(v, i)) || role(v, i) == NR_NONE || !container_slot_accepts(v->c, i, &s)) return 0;
    struct craft_stack h = sget(v, i);
    if (empty(h)) return slot_max(v, i, s);
    return same(h, s) ? slot_max(v, i, s) - h.count : 0;
}

/* a slot of the roles keep() takes for stack s: one holding it with room
 * first, else an empty one; -1 */
static int place_for(const struct scr *v, struct craft_stack s, int (*keep)(int), int not)
{
    int any = -1;
    for (int i = 0; i < v->n; ++i)
    {
        if (i == not || !keep(role(v, i)) || room(v, i, s) <= 0) continue;
        if (same(sget(v, i), s)) return i;
        if (any < 0) any = i;
    }
    return any;
}

static int is_inv(int r) { return r == NR_INV; }
static int is_hotbar(int r) { return r == NR_HOTBAR; }

/* where the cursor's stack goes back: the player's inventory */
static int put_back(const struct scr *v, struct craft_stack s) { return place_for(v, s, player_side, -1); }

/* the slot of hotbar slot b */
static int hotbar_slot(const struct scr *v, int b)
{
    for (int i = 0; i < v->n; ++i)
        if (v->inf[i].role == NR_HOTBAR && v->inf[i].index == b) return i;
    return -1;
}

static int grid_items(const struct scr *v)
{
    if (v->screen != PS_INVENTORY && v->screen != PS_CRAFTING) return 0;
    for (int i = 0; i < v->n; ++i)
        if (v->inf[i].role == NR_GRID && !empty(sget(v, i))) return 1;
    return 0;
}

/* a click (mode, button) on slot s that changes nothing */
static int noop_click(const struct scr *v, int s, int b, int m)
{
    struct craft_stack cur = cursor(v);
    if (s == -999) return (m == 0 || m == 1) && empty(cur);
    if (s < 0 || s >= v->n) return 0;
    struct craft_stack h = sget(v, s);
    switch (m)
    {
    case 0: return empty(h) && (empty(cur) || output(role(v, s)));
    case 1: return empty(h);
    case 2: { int hs = hotbar_slot(v, b); return empty(h) && (hs < 0 || empty(sget(v, hs))); }
    case 3: return 1;           /* the middle click: creative only */
    case 4: return empty(h) || !empty(cur);
    default: return 0;
    }
}

/* ------------------------------------------------------------ the recipes */

/* nwrl_recipes' index i: a crafting recipe (*r) or a furnace one (*smelt) */
static int recipe_at(int i, const struct recipe_def **r, int *smelt)
{
    if (i < 0) return 0;
    for (int k = 0; k < RECIPE_COUNT; ++k)
    {
        if (RECIPES[k].kind != RECIPE_SHAPED && RECIPES[k].kind != RECIPE_SHAPELESS) continue;
        if (i-- == 0) { *r = &RECIPES[k]; *smelt = -1; return 1; }
    }
    if (i < SMELTING_COUNT) { *r = NULL; *smelt = i; return 1; }
    return 0;
}

static int nrecipes(void)
{
    int n = SMELTING_COUNT;
    for (int k = 0; k < RECIPE_COUNT; ++k) n += RECIPES[k].kind == RECIPE_SHAPED || RECIPES[k].kind == RECIPE_SHAPELESS;
    return n;
}

static int matches(const struct stack_def *d, struct craft_stack s)
{
    return d->exists && s.item == d->item && (d->damage == 32767 || d->damage == s.damage);
}

/* the 3x3 cells (row * 3 + column) a crafting recipe wants, laid out from
 * the top left; how many it fits under a grid of side `side` (-1: it does
 * not fit) */
static int pattern(const struct recipe_def *r, int side, const struct stack_def *want[9])
{
    for (int k = 0; k < 9; ++k) want[k] = NULL;
    if (r->kind == RECIPE_SHAPED)
    {
        if (r->width > side || r->height > side) return -1;
        for (int y = 0; y < r->height; ++y)
            for (int x = 0; x < r->width; ++x)
            {
                const struct stack_def *d = &RECIPE_ITEMS[r->items + y * r->width + x];
                if (d->exists) want[y * 3 + x] = d;
            }
        return 0;
    }
    if (r->count > side * side) return -1;
    for (int k = 0; k < r->count; ++k) want[(k / side) * 3 + k % side] = &RECIPE_ITEMS[r->items + k];
    return 0;
}

static int grid_slot(const struct scr *v, int cell)
{
    int side = v->screen == PS_CRAFTING ? 3 : 2, y = cell / 3, x = cell % 3;
    return y < side && x < side ? 1 + y * side + x : -1;
}

/* the items a craft can use: the player's inventory, the grid and the cursor */
static int avail(const struct scr *v, const struct stack_def *d)
{
    int n = 0;
    for (int i = 0; i < v->n; ++i)
        if (player_side(role(v, i)) || role(v, i) == NR_GRID)
        {
            struct craft_stack s = sget(v, i);
            if (matches(d, s)) n += s.count;
        }
    if (matches(d, cursor(v))) n += cursor(v).count;
    return n;
}

static int best_fuel(const struct scr *v, int not_item, int *value)
{
    int best = -1, bv = 0;
    for (int i = 0; i < v->n; ++i)
    {
        if (!player_side(role(v, i))) continue;
        struct craft_stack s = sget(v, i);
        if (empty(s) || s.item == not_item) continue;
        int fv = furnace_fuel_value(s.item);
        if (fv > bv) { bv = fv; best = s.item; }
    }
    *value = bv;
    return best;
}

/* how many times the open screen can make a furnace recipe now */
static int crafts_smelt(const struct scr *v, int smelt)
{
    if (v->screen != PS_FURNACE) return 0;
    const struct stack_def *d = &SMELTING[smelt].input;
    int n = avail(v, d), fv;
    struct craft_stack in = sget(v, 0);
    if (matches(d, in)) n += in.count;
    int burning = v->c->furnace_progress[1] > 0 || !empty(sget(v, 1));
    if (!burning && best_fuel(v, d->item, &fv) < 0) return 0;
    return n;
}

/* ... and a crafting recipe */
static int crafts_grid(const struct scr *v, const struct recipe_def *r)
{
    if (v->screen != PS_INVENTORY && v->screen != PS_CRAFTING) return 0;
    const struct stack_def *want[9];
    if (pattern(r, v->screen == PS_CRAFTING ? 3 : 2, want) < 0) return 0;
    int best = INT_MAX;
    for (int k = 0; k < 9; ++k)
    {
        if (!want[k]) continue;
        int need = 0;
        for (int j = 0; j < 9; ++j) need += want[j] && want[j]->item == want[k]->item && want[j]->damage == want[k]->damage;
        int c = avail(v, want[k]) / need;
        if (c < best) best = c;
    }
    return best == INT_MAX ? 0 : best;
}

/* recipe i (nwrl_recipes' order) */
static int crafts(const struct scr *v, int i)
{
    const struct recipe_def *r;
    int smelt;
    if (!recipe_at(i, &r, &smelt)) return 0;
    return r ? crafts_grid(v, r) : crafts_smelt(v, smelt);
}

/* ------------------------------------------------------------ the plans */

enum { P_EMIT, P_DONE, P_FAIL };

static int click(struct pool_gui_op *o, int slot, int button, int mode)
{
    *o = (struct pool_gui_op){PG_CLICK, -1, slot, button, mode, 0};
    return P_EMIT;
}

/* the cursor's stack back: into from if it takes it there, else the player's inventory */
static int return_cursor(const struct scr *v, int from, struct pool_gui_op *o)
{
    struct craft_stack cur = cursor(v);
    int s = -1;
    if (from >= 0 && !output(role(v, from)) && room(v, from, cur) > 0) s = from;
    if (s < 0) s = put_back(v, cur);
    if (s < 0) return P_FAIL;
    return click(o, s, 0, 0);
}

static int pick_by_item(const struct scr *v, int item, int damage)
{
    int best = -1, bclass = 9, bcount = 0;
    for (int i = 0; i < v->n; ++i)
    {
        struct craft_stack s = sget(v, i);
        int r = role(v, i);
        if (empty(s) || s.item != item || (damage >= 0 && s.damage != damage) || r == NR_ARMOUR) continue;
        int cl = storage(r) ? 0 : r == NR_INV ? 1 : r == NR_HOTBAR ? 2 : 3;
        if (cl < bclass || (cl == bclass && s.count > bcount)) { best = i; bclass = cl; bcount = s.count; }
    }
    return best;
}

/* a move's to by item: the other side of the screen from `from` */
static int to_by_item(const struct scr *v, int from, struct craft_stack s)
{
    int fr = role(v, from), st = 0;
    for (int i = 0; i < v->n; ++i) st |= storage(role(v, i));
    if (!player_side(fr)) return place_for(v, s, player_side, from);
    if (st) return place_for(v, s, storage, from);
    return place_for(v, s, fr == NR_INV ? is_hotbar : is_inv, from);
}

static int plan_move(struct rlact_item *it, const struct scr *v, struct pool_gui_op *o)
{
    struct craft_stack cur = cursor(v);
    int amount = it->op.button;
    if (it->phase == 0)
    {
        if (!empty(cur))
        {
            /* something held before the move: back into the inventory first */
            if (++it->stuck > 4) return P_FAIL;
            int s = put_back(v, cur);
            return s < 0 ? P_FAIL : click(o, s, 0, 0);
        }
        if (it->from < 0)
        {
            it->from = it->op.window == NWRL_BY_ITEM ? pick_by_item(v, it->op.mode, it->op.index) : it->op.window;
            if (it->from < 0 || it->from >= v->n) return P_FAIL;
        }
        if (empty(sget(v, it->from))) return P_FAIL;
        it->phase = 1;
        it->stuck = 0;
        return click(o, it->from, amount == NWRL_HALF ? 1 : 0, 0);
    }
    if (it->phase == 1)
    {
        if (empty(cur)) return P_FAIL;   /* nothing came up */
        if (it->to < 0)
        {
            it->to = it->op.slot == NWRL_BY_ITEM ? to_by_item(v, it->from, cur) : it->op.slot;
            if (it->to >= v->n) it->to = -1;
        }
        it->phase = 2;
        if (it->to >= 0 && it->to != it->from && !output(role(v, it->to)))
            return click(o, it->to, amount == NWRL_ONE ? 1 : 0, 0);
    }
    /* phase 2: the rest back */
    if (empty(cur)) return P_DONE;
    if (++it->stuck > 4) return P_FAIL;
    return return_cursor(v, it->from, o);
}

/* the deficit of cell slot g for ingredient d to hold n: -1 when it holds another stack */
static int deficit(const struct scr *v, int g, const struct stack_def *d, int n, struct craft_stack with)
{
    struct craft_stack h = sget(v, g);
    if (empty(h)) return n;
    if (!matches(d, h) || (!empty(with) && !same(h, with))) return -1;
    return h.count < n ? n - h.count : 0;
}

static int place(struct craft_stack cur, int def, int slot, struct pool_gui_op *o)
{
    return click(o, slot, cur.count <= def ? 0 : 1, 0);
}

static int plan_grid(struct rlact_item *it, const struct scr *v, const struct recipe_def *r, struct pool_gui_op *o)
{
    if (v->screen != PS_INVENTORY && v->screen != PS_CRAFTING) return P_FAIL;
    const struct stack_def *want[9];
    if (pattern(r, v->screen == PS_CRAFTING ? 3 : 2, want) < 0) return P_FAIL;
    if (it->phase == 1) return P_DONE;      /* the result was shift-clicked */
    if (it->n <= 0)
    {
        int can = crafts(v, it->op.index);
        it->n = it->op.button > 0 && it->op.button < can ? it->op.button : can;
        if (it->n <= 0) return P_FAIL;
        for (int k = 0; k < 9; ++k)
            if (want[k] && it->n > ITEMS[want[k]->item & 4095].max_stack_size) it->n = ITEMS[want[k]->item & 4095].max_stack_size;
    }
    struct craft_stack cur = cursor(v);
    if (!empty(cur))
    {
        for (int k = 0; k < 9; ++k)
        {
            int g = grid_slot(v, k);
            if (!want[k] || g < 0 || !matches(want[k], cur)) continue;
            int def = deficit(v, g, want[k], it->n, cur);
            if (def > 0) return place(cur, def, g, o);
        }
        if (++it->stuck > 6) return P_FAIL;
        int s = put_back(v, cur);
        return s < 0 ? P_FAIL : click(o, s, 0, 0);
    }
    /* a cell holding what the recipe does not want there: out */
    for (int k = 0; k < 9; ++k)
    {
        int g = grid_slot(v, k);
        if (g < 0) continue;
        struct craft_stack h = sget(v, g);
        if (!empty(h) && (!want[k] || !matches(want[k], h)))
        {
            if (++it->stuck > 6) return P_FAIL;
            return click(o, g, 0, 1);
        }
    }
    /* a cell short of its ingredient: the largest stack that fills it up */
    int ready = 1;
    for (int k = 0; k < 9; ++k)
    {
        int g = grid_slot(v, k);
        if (!want[k] || g < 0) continue;
        int def = deficit(v, g, want[k], it->n, EMPTY);
        struct craft_stack h = sget(v, g);
        if (empty(h)) ready = 0;
        if (def <= 0) continue;
        int src = -1, cnt = 0;
        for (int i = 0; i < v->n; ++i)
        {
            struct craft_stack s = sget(v, i);
            if (!player_side(role(v, i)) || !matches(want[k], s) || (!empty(h) && !same(h, s))) continue;
            if (s.count > cnt) { src = i; cnt = s.count; }
        }
        if (src >= 0)
        {
            it->stuck = 0;
            return click(o, src, 0, 0);
        }
    }
    if (!ready) return P_FAIL;
    struct craft_stack res = sget(v, 0);
    if (empty(res) || res.item != r->output.item) return P_FAIL;
    it->phase = 1;
    return click(o, 0, 0, 1);
}

static int plan_furnace(struct rlact_item *it, const struct scr *v, int smelt, struct pool_gui_op *o)
{
    if (v->screen != PS_FURNACE) return P_FAIL;
    const struct stack_def *d = &SMELTING[smelt].input;
    if (it->n <= 0)
    {
        int can = crafts(v, it->op.index);
        it->n = it->op.button > 0 && it->op.button < can ? it->op.button : can;
        if (it->n <= 0) return P_FAIL;
        if (it->n > 64) it->n = 64;
        it->fuel_item = -1;
        it->fuel_need = 0;
        if (v->c->furnace_progress[1] <= 0 && empty(sget(v, 1)))
        {
            int fv;
            it->fuel_item = best_fuel(v, d->item, &fv);
            if (it->fuel_item < 0) return P_FAIL;
            it->fuel_need = (it->n * 200 + fv - 1) / fv;
        }
    }
    struct craft_stack cur = cursor(v), in = sget(v, 0), fuel = sget(v, 1);
    int in_def = empty(in) ? it->n : matches(d, in) ? (in.count < it->n ? it->n - in.count : 0) : -1;
    int fuel_def = it->fuel_item < 0 ? 0 : empty(fuel) ? it->fuel_need
                 : fuel.item == it->fuel_item ? (fuel.count < it->fuel_need ? it->fuel_need - fuel.count : 0) : 0;
    if (!empty(cur))
    {
        if (matches(d, cur) && in_def > 0 && (empty(in) || same(in, cur)))
            return click(o, 0, cur.count <= in_def || in_def > 4 ? 0 : 1, 0);
        if (cur.item == it->fuel_item && fuel_def > 0 && (empty(fuel) || same(fuel, cur)))
            return click(o, 1, cur.count <= fuel_def || fuel_def > 4 ? 0 : 1, 0);
        if (++it->stuck > 6) return P_FAIL;
        int s = put_back(v, cur);
        return s < 0 ? P_FAIL : click(o, s, 0, 0);
    }
    if (in_def < 0)
    {
        if (++it->stuck > 6) return P_FAIL;
        return click(o, 0, 0, 1);
    }
    int src = -1, cnt = 0;
    if (in_def > 0)
        for (int i = 0; i < v->n; ++i)
        {
            struct craft_stack s = sget(v, i);
            if (player_side(role(v, i)) && matches(d, s) && (empty(in) || same(in, s)) && s.count > cnt) { src = i; cnt = s.count; }
        }
    if (src < 0 && fuel_def > 0)
        for (int i = 0; i < v->n; ++i)
        {
            struct craft_stack s = sget(v, i);
            if (player_side(role(v, i)) && s.item == it->fuel_item && (empty(fuel) || same(fuel, s)) && s.count > cnt)
            {
                src = i;
                cnt = s.count;
            }
        }
    if (src >= 0)
    {
        it->stuck = 0;
        return click(o, src, 0, 0);
    }
    in = sget(v, 0);
    return !empty(in) && (v->c->furnace_progress[1] > 0 || !empty(sget(v, 1))) ? P_DONE : P_FAIL;
}

static int plan(struct rlact_item *it, struct session *ss, struct pool_gui_op *o)
{
    if (it->op.kind != NWRL_OP_MOVE && it->op.kind != NWRL_OP_CRAFT)
    {
        *o = it->op;
        return P_EMIT;
    }
    struct scr v;
    if (!scr_get(ss, &v)) return P_FAIL;
    if (it->window == -100) it->window = v.window;
    if (it->window != v.window) return P_FAIL;
    if (it->clicks > 160) return P_FAIL;
    if (it->op.kind == NWRL_OP_MOVE) return plan_move(it, &v, o);
    const struct recipe_def *r;
    int smelt;
    if (!recipe_at(it->op.index, &r, &smelt)) return P_FAIL;
    return r ? plan_grid(it, &v, r, o) : plan_furnace(it, &v, smelt, o);
}

/* the masks over a raw op about to be given: 1 refuses it */
static int masked(const struct nwrl_actconf *c, struct session *ss, const struct pool_gui_op *o)
{
    struct scr v;
    if (!scr_get(ss, &v)) return 0;
    if (c->mask_close_grid && o->kind == PG_CLOSE && grid_items(&v)) return 1;
    if (c->mask_noop_clicks && o->kind == PG_CLICK && (o->window < 0 || o->window == v.window) &&
        noop_click(&v, o->slot, o->button, o->mode))
        return 1;
    return 0;
}

/* ------------------------------------------------------------ the queue */

int rlact_on(const struct nwrl_actconf *c)
{
    return c->click_interval > 1 || c->mask_close_grid || c->mask_noop_clicks || c->move_stack || c->item_select ||
           c->recipes;
}

int rlact_conf_ok(const struct nwrl_actconf *c)
{
    return c->click_interval >= 1 && c->click_interval <= 100 && (unsigned)c->mask_close_grid <= 1 &&
           (unsigned)c->mask_noop_clicks <= 1 && (unsigned)c->move_stack <= 1 && (unsigned)c->item_select <= 1 &&
           (unsigned)c->recipes <= 1;
}

void rlact_reset(struct rlact_env *e)
{
    FILE *log = e->log;
    memset(e, 0, sizeof *e);
    e->log = log;
    e->last_t = INT64_MIN / 2;
}

static int accepts(const struct nwrl_actconf *c, const struct pool_gui_op *o)
{
    if (o->kind == NWRL_OP_MOVE)
        return c->move_stack && (unsigned)o->button <= NWRL_ONE &&
               ((o->window != NWRL_BY_ITEM && o->slot != NWRL_BY_ITEM) || c->item_select) &&
               (o->window >= 0 || o->window == NWRL_BY_ITEM) && (o->slot >= 0 || o->slot == NWRL_BY_ITEM);
    if (o->kind == NWRL_OP_CRAFT) return c->recipes && o->index >= 0 && o->index < nrecipes() && o->button >= 0 && o->button <= 64;
    return o->kind >= PG_CLICK && o->kind <= PG_TRSEL;
}

static void log_op(FILE *f, const struct pool_gui_op *o, int first)
{
    if (!first) fputc(',', f);
    switch (o->kind)
    {
    case PG_CLICK: fprintf(f, "[\"click\",%d,%d,%d,%d]", o->window, o->slot, o->button, o->mode); break;
    case PG_CLOSE: fputs("[\"close\"]", f); break;
    case PG_RESPAWN: fputs("[\"respawn\"]", f); break;
    case PG_WAKE: fputs("[\"wake\"]", f); break;
    case PG_TRSEL: fprintf(f, "[\"trsel\",%d,%d]", o->window, o->index); break;
    default: fputs("[\"?\"]", f); break;
    }
}

void rlact_tick(const struct nwrl_actconf *c, struct rlact_env *e, struct session *ss, int first, struct pool_act *a)
{
    if (!rlact_on(c) || a->kind != PA_AGENT) return;
    int64_t t = e->tick++;
    if (first)
    {
        struct scr v;
        int window = scr_get(ss, &v) ? v.window : -100;
        for (int i = 0; i < a->nops && i < POOL_GUI_OPS; ++i)
        {
            if (!accepts(c, &a->ops[i]) || e->n == RLACT_QUEUE)
            {
                ++e->refused;
                continue;
            }
            struct rlact_item *it = &e->q[(e->head + e->n++) % RLACT_QUEUE];
            memset(it, 0, sizeof *it);
            it->op = a->ops[i];
            it->window = window;
            it->from = it->to = -1;
            it->fuel_item = -1;
        }
    }
    a->nops = 0;
    if (e->log) fprintf(e->log, "{\"t\":%lld,\"gui\":[", (long long)t);
    /* the front actions until one gives an op (a few finish or fail without one) */
    for (int guard = 0; e->n > 0 && t - e->last_t >= c->click_interval && guard < 8; ++guard)
    {
        struct rlact_item *it = &e->q[e->head];
        struct pool_gui_op o;
        int rc = plan(it, ss, &o);
        int raw = it->op.kind != NWRL_OP_MOVE && it->op.kind != NWRL_OP_CRAFT;
        if (rc == P_EMIT && masked(c, ss, &o))
        {
            ++e->refused;
            rc = raw ? P_DONE : P_FAIL;
            if (raw)
            {
                e->head = (e->head + 1) % RLACT_QUEUE;
                --e->n;
                continue;
            }
        }
        if (rc == P_EMIT)
        {
            a->ops[0] = o;
            a->nops = 1;
            e->last_t = t;
            ++e->emitted;
            ++it->clicks;
            if (e->log) log_op(e->log, &o, 1);
            if (raw)
            {
                e->head = (e->head + 1) % RLACT_QUEUE;
                --e->n;
            }
            break;
        }
        if (rc == P_DONE) ++e->done;
        else ++e->failed;
        e->head = (e->head + 1) % RLACT_QUEUE;
        --e->n;
    }
    if (e->log) fputs("]}\n", e->log);
}

void rlact_info(const struct nwrl_actconf *c, const struct rlact_env *e, struct session *ss, struct nwrl_actinfo *out)
{
    memset(out, 0, sizeof *out);
    out->queued = e->n;
    out->emitted = e->emitted;
    out->refused = e->refused;
    out->failed = e->failed;
    out->done = e->done;
    out->nrecipes = nrecipes();
    struct scr v;
    if (!scr_get(ss, &v))
    {
        out->window = -1;
        return;
    }
    out->screen = v.screen;
    out->nslots = v.n;
    out->window = v.window;
    out->close_ok = !(c->mask_close_grid && grid_items(&v));
    struct craft_stack cur = cursor(&v);
    out->outside_ok = !empty(cur);
    for (int i = 0; i < v.n; ++i)
    {
        struct craft_stack h = sget(&v, i);
        int r = role(&v, i), k = 0;
        if (!noop_click(&v, i, 0, 0)) k |= NWRL_CK_CLICK;
        if (!empty(h)) k |= NWRL_CK_SHIFT | NWRL_CK_FROM;
        if (!noop_click(&v, i, 0, 4)) k |= NWRL_CK_DROP;
        if (!output(r) && r != NR_ARMOUR && r != NR_NONE) k |= NWRL_CK_TO;
        out->click_ok[i] = (uint8_t)k;
    }
    /* every recipe, in nwrl_recipes' order; one whose ingredients are not all
     * on the screen at all is not counted (the most of them) */
    uint8_t present[4096] = {0};
    for (int i = 0; i < v.n; ++i)
    {
        struct craft_stack h = sget(&v, i);
        if (!empty(h)) present[h.item & 4095] = 1;
    }
    if (!empty(cur)) present[cur.item & 4095] = 1;
    int idx = 0;
    for (int k = 0; k < RECIPE_COUNT && idx < NWRL_RMAX; ++k)
    {
        const struct recipe_def *r = &RECIPES[k];
        if (r->kind != RECIPE_SHAPED && r->kind != RECIPE_SHAPELESS) continue;
        int ok = 1;
        for (int j = 0; j < r->count && ok; ++j)
            if (RECIPE_ITEMS[r->items + j].exists && !present[RECIPE_ITEMS[r->items + j].item & 4095]) ok = 0;
        int n = ok ? crafts_grid(&v, r) : 0;
        out->crafts[idx++] = (int16_t)(n > 32767 ? 32767 : n);
    }
    for (int k = 0; k < SMELTING_COUNT && idx < NWRL_RMAX; ++k)
    {
        int n = present[SMELTING[k].input.item & 4095] ? crafts_smelt(&v, k) : 0;
        out->crafts[idx++] = (int16_t)(n > 32767 ? 32767 : n);
    }
}
