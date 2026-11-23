/* The raw keyboard and mouse acts (rlraw.h). */
#include "rlraw.h"

#include <math.h>
#include <string.h>

#include "../engine/gui_screens.h"
#include "../engine/player.h"
#include "../engine/session.h"
#include "pool.h"

/* a container screen up (GuiContainer), its container */
static const struct container *screen_of(struct client_player *cp)
{
    return cp->screen_inventory ? cp_gui_container(cp) : NULL;
}

static struct gui_screen_layout layout_of(const struct rlraw_env *r, const struct container *c, struct client_player *cp)
{
    return gui_input_layout(&r->g, c, cp->sv.potions.size > 0);
}

/* MouseHelper.ungrabMouseCursor: the pointer at the window's centre (play.c
 * centre_mouse_x; LWJGL's y from the bottom) */
static void centre(struct rlraw_env *r)
{
    struct gui_screen_layout l = gui_screen_layout(NULL, RLRAW_W, RLRAW_H, RLRAW_SCALE);
    r->x = (float)((RLRAW_W / 2) * l.sw / RLRAW_W);
    r->y = (float)(l.sh - (RLRAW_H / 2) * l.sh / RLRAW_H - 1);
    r->g.cur_x = (int)r->x;
    r->g.cur_y = (int)r->y;
}

void rlraw_reset(struct rlraw_env *r)
{
    FILE *log = r->log;
    memset(r, 0, sizeof *r);
    r->log = log;
    gui_input_reset(&r->g);
    r->g.width = RLRAW_W;
    r->g.height = RLRAW_H;
    r->g.gui_scale = RLRAW_SCALE;
    r->window = -1;
    centre(r);
}

static void push(struct rlraw_env *r, int kind, int button, int key)
{
    struct gui_event e = {kind, r->g.cur_x, r->g.cur_y, button, key};
    gui_input_push(&r->g, &e);
}

int rlraw_hook(struct rlraw_env *r, struct session *ss, int k, const struct pool_act *src, int hold_only,
               struct pool_act *out)
{
    struct client_player *cp = &ss->cp;
    const struct container *c = screen_of(cp);
    int first = !hold_only;
    int want = (int)((src->hold >> PK_ATTACK) & 1) | (int)((src->hold >> PK_USE) & 1) << 1;
    int shift = (int)((src->hold >> PK_SNEAK) & 1);
    (void)k;

    memset(out, 0, sizeof *out);
    out->kind = PA_AGENT;
    out->hold = src->hold;
    out->look_mode = PL_NONE;
    out->hotbar = -1;
    if (c == NULL)
    {
        /* no screen: the agent form as it is (its holds alone on a step's
         * later ticks, Act.holdOnly) */
        if (first)
        {
            *out = *src;
            out->gui_in = NULL;
            r->buttons = want;
        }
        r->up = 0;
        r->window = -1;
        if (r->shift != shift) gui_input_modifier(&r->g, GK_LSHIFT, shift);
        r->shift = shift;
        return 1;
    }
    /* a screen up: a new one puts the pointer at the centre (the ungrab) */
    if (!r->up || r->window != c->window_id)
    {
        centre(r);
        r->up = 1;
        r->window = c->window_id;
    }
    /* the frame drawn since the last tick: its pointer is the hover */
    gui_input_frame(&r->g);
    out->gui_in = &r->g;
    if (!first) return 1;
    out->ctrl = src->ctrl;
    if (r->shift != shift) gui_input_modifier(&r->g, GK_LSHIFT, shift);
    r->shift = shift;
    r->g.cur_ctrl = src->ctrl ? 1 : 0;
    /* the buttons let go, then the motion, then the buttons pressed: a hand
     * that releases, moves on and clicks within one step */
    for (int b = 0; b < 2; ++b)
        if (!((want >> b) & 1) && ((r->buttons >> b) & 1)) push(r, GE_RELEASE, b, 0);
    /* the motion: counts (degrees / 0.15) as display pixels, GUI pixels at
     * the scale; the pointer stays in the window */
    if (src->look_mode == PL_DLOOK)
    {
        struct gui_screen_layout l = layout_of(r, c, cp);
        float x = r->x + src->look[0] / 0.15f / (float)RLRAW_SCALE;
        float y = r->y + src->look[1] / 0.15f / (float)RLRAW_SCALE;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > (float)(l.sw - 1)) x = (float)(l.sw - 1);
        if (y > (float)(l.sh - 1)) y = (float)(l.sh - 1);
        r->x = x;
        r->y = y;
        int ix = (int)floorf(x), iy = (int)floorf(y);
        if (ix != r->g.cur_x || iy != r->g.cur_y)
        {
            r->g.cur_x = ix;
            r->g.cur_y = iy;
            push(r, GE_MOVE, -1, 0);
        }
    }
    for (int b = 0; b < 2; ++b)
        if (((want >> b) & 1) && !((r->buttons >> b) & 1)) push(r, GE_PRESS, b, 0);
    r->buttons = want;
    /* the keys typed: E, Q and the number keys */
    if (src->press[PK_INVENTORY]) push(r, GE_KEY, -1, GK_E);
    if (src->press[PK_DROP]) push(r, GE_KEY, -1, GK_Q);
    for (int i = 0; i < 9; ++i)
        if (src->press[PK_HOTBAR1 + i]) push(r, GE_KEY, -1, GK_1 + i);
    return 1;
}

void rlraw_done(struct rlraw_env *r, int64_t t, const struct act *a)
{
    if (r->log == NULL) return;
    fprintf(r->log, "{\"t\":%lld,\"screen\":%d,\"gui\":[", (long long)t, a->gui_in != NULL);
    int n = 0, cl = 0;
    for (int i = 0; i <= a->clicks; ++i)
    {
        while (cl < a->closes && a->close_at[cl] == i)
        {
            fprintf(r->log, "%s[\"close\"]", n++ ? "," : "");
            ++cl;
        }
        if (i == a->clicks) break;
        const struct guiclick *g = &a->gui_click[i];
        fprintf(r->log, "%s[\"click\",%d,%d,%d,%d]", n++ ? "," : "", g->window, g->slot, g->button, g->mode);
    }
    if (a->gui_close && a->closes == 0) fprintf(r->log, "%s[\"close\"]", n++ ? "," : "");
    fprintf(r->log, "]}\n");
}
