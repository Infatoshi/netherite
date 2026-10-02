/* The binding's screens (rlgui.h). */
#define _GNU_SOURCE
#include "rlgui.h"

#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../tests/rowrec.h"
#pragma GCC diagnostic pop
#define NETHERITE_DEV
#include "../engine/dev.h"
#include "../engine/player.h"
#include "../engine/potion.h"
#include "../engine/raster.h"
#include "pool.h"
#include "rlgui_int.h"

static void put(struct nwrl_slotinfo *out, int max, int *n, int role, int index, int x, int y)
{
    if (*n < max) out[*n] = (struct nwrl_slotinfo){(int8_t)role, (int8_t)index, (int16_t)x, (int16_t)y};
    ++*n;
}

/* the player's 27 and 9 below a screen's own slots: the main rows from y0,
 * the hotbar at yh (every container but the chest and the hopper: 84, 142) */
static void player_slots(struct nwrl_slotinfo *out, int max, int *n, int y0, int yh)
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 9; ++j) put(out, max, n, NR_INV, i * 9 + j, 8 + j * 18, y0 + i * 18);
    for (int j = 0; j < 9; ++j) put(out, max, n, NR_HOTBAR, j, 8 + j * 18, yh);
}

int nwrl_screen_slots(int screen, int nslots, struct nwrl_slotinfo *out, int max, int *w, int *h)
{
    int n = 0, sw = 176, sh = 166;
    switch (screen)
    {
    case PS_INVENTORY:      /* ContainerPlayer */
        if (nslots != 45) return 0;
        put(out, max, &n, NR_RESULT, 0, 144, 36);
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j) put(out, max, &n, NR_GRID, i * 3 + j, 88 + j * 18, 26 + i * 18);
        for (int i = 0; i < 4; ++i) put(out, max, &n, NR_ARMOUR, i, 8, 8 + i * 18);
        player_slots(out, max, &n, 84, 142);
        break;
    case PS_CRAFTING:       /* ContainerWorkbench */
        if (nslots != 46) return 0;
        put(out, max, &n, NR_RESULT, 0, 124, 35);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) put(out, max, &n, NR_GRID, i * 3 + j, 30 + j * 18, 17 + i * 18);
        player_slots(out, max, &n, 84, 142);
        break;
    case PS_CHEST:          /* ContainerChest: numRows from the slots */
    {
        if ((nslots - 36) % 9 != 0 || nslots - 36 < 9 || nslots - 36 > 54) return 0;
        int rows = (nslots - 36) / 9, off = (rows - 4) * 18;
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < 9; ++j) put(out, max, &n, NR_CHEST, i * 9 + j, 8 + j * 18, 18 + i * 18);
        player_slots(out, max, &n, 103 + off, 161 + off);
        sh = 114 + rows * 18;
        break;
    }
    case PS_FURNACE:        /* ContainerFurnace */
        if (nslots != 39) return 0;
        put(out, max, &n, NR_FURN_IN, 0, 56, 17);
        put(out, max, &n, NR_FUEL, 0, 56, 53);
        put(out, max, &n, NR_FURN_OUT, 0, 116, 35);
        player_slots(out, max, &n, 84, 142);
        break;
    case PS_DISPENSER:      /* ContainerDispenser (a dropper's too) */
        if (nslots != 45) return 0;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) put(out, max, &n, NR_TILE, i * 3 + j, 62 + j * 18, 17 + i * 18);
        player_slots(out, max, &n, 84, 142);
        break;
    case PS_HOPPER:         /* ContainerHopper: GuiHopper's ySize 133 */
        if (nslots != 41) return 0;
        for (int j = 0; j < 5; ++j) put(out, max, &n, NR_TILE, j, 44 + j * 18, 20);
        player_slots(out, max, &n, 51, 109);
        sh = 133;
        break;
    case PS_MERCHANT:       /* ContainerMerchant */
        if (nslots != 39) return 0;
        put(out, max, &n, NR_TRADE_IN, 0, 36, 53);
        put(out, max, &n, NR_TRADE_IN, 1, 62, 53);
        put(out, max, &n, NR_TRADE_OUT, 0, 120, 53);
        player_slots(out, max, &n, 84, 142);
        break;
    default:
        return 0;
    }
    if (w) *w = sw;
    if (h) *h = sh;
    return n;
}

void rlgui_row(FILE *f, struct session *ss, int64_t t, int64_t cw_from)
{
    /* the heap, not a thread-local: a loaded library's static TLS has no room for it (rlbind.mk) */
    enum { LINE = 16384 };
    char *line = malloc(LINE);
    rowrec_line(ss, t, cw_from >= 0 && t >= cw_from, line, LINE);
    fputs(line, f);
    free(line);
}

/* the agent window (the tape header's w, h) and its GUI scale (options gui 2) */
enum { LAYER_W = 854, LAYER_H = 480, LAYER_SCALE = 2 };

int rlgui_layer(struct session *ss, int w, int h, float *mul, float *add)
{
    struct client_player *cp = &ss->cp;
    const struct container *oc = cp_gui_container(cp);
    if (!cp->screen_inventory || oc == NULL) return 0;
    /* what play's live frame draws (play.c live_gui): a copy of the client
     * container whose player half and cursor are the client's own */
    struct container *shown = malloc(sizeof *shown);
    *shown = *oc;
    for (int s = 0; s < shown->nslots; ++s)
        shown->slots[s].inv = (struct inv *)((char *)shown + ((const char *)oc->slots[s].inv - (const char *)oc));
    if (oc->recipes == &oc->client_recipes) shown->recipes = &shown->client_recipes;
    for (int s = 0; s < 40; ++s)
    {
        const struct surv_stack *st = &cp->sv.inv[s];
        shown->player.slot[s] = (struct craft_stack){st->count > 0 ? st->item : -1, st->count,
                                                     st->count > 0 ? st->damage : 0, st->count > 0 ? st->tag : 0};
    }
    const struct surv_stack *cur = &cp->sv.cursor;
    shown->cursor = (struct craft_stack){cur->count > 0 ? cur->item : -1, cur->count, cur->count > 0 ? cur->damage : 0,
                                         cur->count > 0 ? cur->tag : 0};
    int burn = 0, fuel = 0, cook = 0;
    if (oc->kind == CONTAINER_FURNACE)
    {
        cook = oc->furnace_progress[0];
        burn = oc->furnace_progress[1];
        fuel = oc->furnace_progress[2];
    }
    struct gui_screen_extra *x = calloc(1, sizeof *x);
    if (oc->kind == CONTAINER_MERCHANT) x->merchant_name = oc->title;
    uint8_t pids[POT_COUNT];
    int npids = potion_map_order(&cp->sv.potions, pids);
    for (int i = 0; i < npids && x->neffects < 32; ++i)
    {
        const struct potion_effect *e = &cp->sv.potions.eff[pids[i]];
        x->effects[x->neffects++] = (struct gui_effect){e->id, e->amplifier, e->duration, e->duration_max};
    }
    x->partial_tick = 1.0f;
    x->user = "Player";
    /* the screen over black and over white: a pixel is k * background + c */
    size_t px = (size_t)LAYER_W * LAYER_H * 3;
    unsigned char *b = calloc(px, 1), *wt = malloc(px);
    memset(wt, 255, px);
    raster_gui_live(b, LAYER_W, LAYER_H, "out/java/render/hud_mixed", shown, 1, 0, 0, LAYER_SCALE, -1000, -1000,
                    0, 0, burn, fuel, cook, x);
    raster_gui_live(wt, LAYER_W, LAYER_H, "out/java/render/hud_mixed", shown, 1, 0, 0, LAYER_SCALE, -1000, -1000,
                    0, 0, burn, fuel, cook, x);
    /* by area: output pixel (ox, oy) averages the window's pixels whose
     * centres fall in its box */
    for (int oy = 0; oy < h; ++oy)
    {
        int y0 = (int)((int64_t)oy * LAYER_H / h), y1 = (int)((int64_t)(oy + 1) * LAYER_H / h);
        for (int ox = 0; ox < w; ++ox)
        {
            int x0 = (int)((int64_t)ox * LAYER_W / w), x1 = (int)((int64_t)(ox + 1) * LAYER_W / w);
            double sb[3] = {0, 0, 0}, sw[3] = {0, 0, 0};
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx)
                {
                    size_t o = ((size_t)yy * LAYER_W + (size_t)xx) * 3;
                    for (int c = 0; c < 3; ++c) { sb[c] += b[o + c]; sw[c] += wt[o + c]; }
                }
            double k = (double)(y1 - y0) * (x1 - x0);
            for (int c = 0; c < 3; ++c)
            {
                size_t o = ((size_t)oy * w + (size_t)ox) * 3 + (size_t)c;
                add[o] = (float)(sb[c] / k);
                mul[o] = (float)((sw[c] - sb[c]) / k / 255.0);
            }
        }
    }
    free(b);
    free(wt);
    free(x);
    free(shown);
    return 1;
}

void rlgui_dev(struct pool_config *pc)
{
    pc->dev_apply = dev_apply;
    pc->dev_end = dev_end;
}

int64_t nwrl_cw_from(const char *tape, int64_t t0)
{
    uint64_t seed;
    int32_t lcg;
    int64_t from = -1;
    cwrand_from_tape(tape, t0, &seed, &lcg, &from);
    return from;
}
