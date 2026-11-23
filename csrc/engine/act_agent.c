/* The agent form of an act, resolved in the client tick (player.h struct
 * act_agent): oracle/harness/netherite/oracle/Act.java's applyGui and applyInput,
 * which the oracle's agent mode runs at the same two points. What the oracle
 * refuses (ORACLE REFUSE: an op no screen takes) is dropped and counted. */
#include "player.h"

#include <string.h>

#include "container.h"

/* the tape form's ops, as session_parse_act fills them from a resolved row */
static void put_click(struct act *a, int w, int s, int b, int m)
{
    if (a->clicks >= (int)(sizeof a->gui_click / sizeof a->gui_click[0])) return;
    a->gui_click[a->clicks++] = (struct guiclick){w, s, b, m};
}

static void put_close(struct act *a)
{
    a->gui_close = 1;
    if (a->closes < (int)(sizeof a->close_at / sizeof a->close_at[0])) a->close_at[a->closes++] = a->clicks;
}

/* Act.applyGui: each op in order under the screen it finds, a close taking
 * the screen away (and a dead player's close opening GuiGameOver). */
void act_agent_gui(struct client_player *p, struct act *a)
{
    struct act_agent *ag = a->agent;
    const struct container *c = cp_gui_container(p);
    if (c == NULL) c = &p->own_container;
    int open = p->screen_inventory;             /* a GuiContainer is up */
    int gameover = p->screen_gameover, credits = p->screen_credits, sleep = p->screen_sleep;
    int closed = 0, closed_window = -1;

    for (int i = 0; i < ag->nops && i < ACT_AGENT_OPS; ++i)
    {
        int kind = ag->ops[i].kind, w = ag->ops[i].window, s = ag->ops[i].slot, b = ag->ops[i].button,
            m = ag->ops[i].mode;
        /* the closed screen's later keys (GuiContainer.keyTyped goes on) */
        if (closed && !open && (kind == AG_CLOSE || kind == AG_CLICK))
        {
            if (kind == AG_CLOSE) put_close(a);
            else if (w != closed_window || (m != 2 && m != 4) || s < 0 || s >= p->own_container.nslots) ++ag->refused;
            else put_click(a, w, s, b, m);
            continue;
        }
        switch (kind)
        {
        case AG_CLICK:
            if (!open) { ++ag->refused; break; }
            if (w < 0) w = c->window_id;
            if (w != c->window_id || s >= c->nslots || (s < 0 && s != -1 && s != -999) ||
                (s < 0 && (m == 2 || (m == 5 && (b & 3) == 1))))
            {
                ++ag->refused;
                break;
            }
            put_click(a, w, s, b, m);
            break;
        case AG_CLOSE:
            if (!open) { ++ag->refused; break; }
            closed = 1;
            closed_window = c->window_id;
            open = 0;
            /* closeScreen's displayGuiScreen(null) with no health left */
            if (p->sv.health <= 0.0F) gameover = 1;
            put_close(a);
            break;
        case AG_RESPAWN:
            if (!gameover && !credits) { ++ag->refused; break; }
            a->gui_respawn = 1;
            a->gui_respawn_after_close = a->gui_close;
            break;
        case AG_WAKE:
            if (!sleep) { ++ag->refused; break; }
            a->gui_wake = 1;
            break;
        case AG_TRSEL:
            if (!open || c->kind != CONTAINER_MERCHANT || c->window_id != w ||
                a->trsels >= (int)(sizeof a->trsel / sizeof a->trsel[0]))
            {
                ++ag->refused;
                break;
            }
            a->trsel[a->trsels].window = w;
            a->trsel[a->trsels].index = ag->ops[i].index;
            a->trsel[a->trsels].at = a->clicks;
            ++a->trsels;
            break;
        default:
            ++ag->refused;
        }
    }
}

/* Act.applyInput, when the input block runs: the left click counter's
 * decrement, the holds and presses (under GuiInventory the screen took the
 * keyboard and mouse: every binding as it is), the hotbar, focus and ctrl. */
void act_agent_input(struct client_player *p, struct act *a)
{
    const struct act_agent *ag = a->agent;
    if (p->screen_gameover || p->screen_sleep || p->screen_chat || p->screen_credits ||
        (p->screen_inventory && cp_gui_container(p) != &p->own_container))
    {
        a->has_in = 0;
        return;
    }
    int screen = p->screen_inventory;
    a->has_in = 1;
    for (int k = 0; k < K_N; ++k)
    {
        a->keys.held[k] = screen ? p->keys.held[k] : ag->held[k];
        a->keys.presses[k] = p->keys.presses[k] + (screen ? 0 : ag->presses[k]);
    }
    a->hb = !screen && ag->hb >= 0 ? ag->hb : p->hotbar;
    a->focus = p->in_game_has_focus;
    a->lcc = p->left_click_counter > 0 ? p->left_click_counter - 1 : p->left_click_counter;
    a->ctrl = ag->ctrl;
}
