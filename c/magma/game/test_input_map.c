/* game/test_input_map.c - standalone unit test for gm_input_map / gm_input_reset.
 *
 * Feeds synthetic CrInput snapshots through gm_input_map and asserts every
 * GmAction field against hand-computed expected values. Build+run via
 * game/test_input_map.sh. No Makefile involvement.
 */
#include "game/input_map.h"

#include <stdio.h>
#include <math.h>

static int g_fail = 0;

static void expect_i(const char *what, int got, int want)
{
    if (got != want) {
        printf("FAIL %s: got %d want %d\n", what, got, want);
        g_fail = 1;
    }
}

/* floats here are all exact small integers/half-integers, compare bitwise-exact */
static void expect_f(const char *what, float got, float want)
{
    if (got != want) {
        printf("FAIL %s: got %.9g want %.9g\n", what, (double)got, (double)want);
        g_fail = 1;
    }
}

int main(void)
{
    gm_input_reset();

    /* ---- WASD combinations -> forward/strafe signs ---- */
    {
        CrInput in = { 0 };
        in.key_w = 1;                    /* forward only */
        GmAction a = gm_input_map(&in, 0.15f);
        expect_f("W.forward", a.forward, 1.0f);
        expect_f("W.strafe",  a.strafe,  0.0f);
    }
    {
        CrInput in = { 0 };
        in.key_s = 1; in.key_a = 1;      /* back + left */
        GmAction a = gm_input_map(&in, 0.15f);
        expect_f("SA.forward", a.forward, -1.0f);
        expect_f("SA.strafe",  a.strafe,  -1.0f);
    }
    {
        CrInput in = { 0 };
        in.key_w = 1; in.key_s = 1; in.key_d = 1; /* W+S cancel, D right */
        GmAction a = gm_input_map(&in, 0.15f);
        expect_f("WSD.forward", a.forward, 0.0f);
        expect_f("WSD.strafe",  a.strafe,  1.0f);
    }

    /* ---- mouse look, VANILLA signs (EntityPlayerSP.turn): mouse right
     * (dx=+10) -> +dyaw (turn right), mouse down (dy=+4) -> +dpitch (look
     * down); sens=0.15 -> dyaw=+1.5, dpitch=+0.6 ---- */
    {
        CrInput in = { 0 };
        in.mouse_dx = 10; in.mouse_dy = 4;
        GmAction a = gm_input_map(&in, 0.15f);
        expect_f("dyaw",   a.dyaw,   1.5f);
        expect_f("dpitch", a.dpitch, 0.6f);
    }

    /* ---- arrow-key look matches the mouse signs: Right -> +dyaw (turn
     * right), Up -> -dpitch (look up), 3.5 deg per poll ---- */
    {
        CrInput in = { 0 };
        in.key_right = 1; in.key_up = 1;
        GmAction a = gm_input_map(&in, 0.15f);
        expect_f("arrow.dyaw",   a.dyaw,    3.5f);
        expect_f("arrow.dpitch", a.dpitch, -3.5f);
    }
    {
        CrInput in = { 0 };
        in.key_left = 1; in.key_down = 1;
        GmAction a = gm_input_map(&in, 0.15f);
        expect_f("arrow2.dyaw",   a.dyaw,   -3.5f);
        expect_f("arrow2.dpitch", a.dpitch,  3.5f);
    }

    /* ---- held flags map straight through ---- */
    {
        CrInput in = { 0 };
        in.key_space = 1; in.key_shift = 1; in.key_ctrl = 1;
        GmAction a = gm_input_map(&in, 0.15f);
        expect_i("jump",   a.jump,   1);
        expect_i("sneak",  a.sneak,  1);
        expect_i("sprint", a.sprint, 1);
    }

    /* ---- attack held two polls in a row -> do_break only on the first ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.mouse_left = 1;
        GmAction a1 = gm_input_map(&in, 0.15f);
        expect_i("attack1.attack",   a1.attack,   1);
        expect_i("attack1.do_break", a1.do_break, 1);   /* rising edge */
        GmAction a2 = gm_input_map(&in, 0.15f);
        expect_i("attack2.attack",   a2.attack,   1);
        expect_i("attack2.do_break", a2.do_break, 0);   /* still held, no edge */
    }

    /* ---- use rising edge (place) mirrors attack ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.mouse_right = 1;
        GmAction a1 = gm_input_map(&in, 0.15f);
        expect_i("use1.use",      a1.use,      1);
        expect_i("use1.do_place", a1.do_place, 1);
        GmAction a2 = gm_input_map(&in, 0.15f);
        expect_i("use2.do_place", a2.do_place, 0);
    }

    /* ---- wheel=+1 twice from sel 0 -> wraps to 8 then 7 ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.wheel = 1;
        GmAction a1 = gm_input_map(&in, 0.15f);
        expect_i("wheel1.sel", a1.hotbar_sel, 8);   /* 0 - sign(+1) = -1 -> 8 */
        GmAction a2 = gm_input_map(&in, 0.15f);
        expect_i("wheel2.sel", a2.hotbar_sel, 7);   /* 8 - 1 = 7 */
    }

    /* ---- wheel=-1 cycles the other way (from 0 -> 1) ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.wheel = -1;
        GmAction a = gm_input_map(&in, 0.15f);
        expect_i("wheelneg.sel", a.hotbar_sel, 1); /* 0 - sign(-1) = +1 */
    }

    /* ---- key_num=3 -> hotbar_sel=2 ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.key_num = 3;
        GmAction a = gm_input_map(&in, 0.15f);
        expect_i("keynum3.sel", a.hotbar_sel, 2);
    }

    /* ---- key_num seeds the wheel-tracked selection ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.key_num = 5;                  /* sel -> 4 */
        GmAction a1 = gm_input_map(&in, 0.15f);
        expect_i("seed.keynum5", a1.hotbar_sel, 4);
        CrInput in2 = { 0 };
        in2.wheel = 1;                   /* 4 - 1 = 3 */
        GmAction a2 = gm_input_map(&in2, 0.15f);
        expect_i("seed.wheel", a2.hotbar_sel, 3);
    }

    /* ---- no selection input -> -1 (unchanged) ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.key_w = 1;
        GmAction a = gm_input_map(&in, 0.15f);
        expect_i("nosel.sel", a.hotbar_sel, -1);
    }

    /* ---- Tab edge steps the hotbar forward; held Tab does not repeat;
     * Shift+Tab steps back ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.key_tab = 1;
        GmAction a1 = gm_input_map(&in, 0.15f);
        expect_i("tab1.sel", a1.hotbar_sel, 1);      /* 0 + 1 */
        GmAction a2 = gm_input_map(&in, 0.15f);
        expect_i("tab2.sel", a2.hotbar_sel, -1);     /* still held: no step */
        CrInput up = { 0 };
        (void)gm_input_map(&up, 0.15f);              /* release */
        GmAction a3 = gm_input_map(&in, 0.15f);
        expect_i("tab3.sel", a3.hotbar_sel, 2);      /* new edge: 1 + 1 */
        CrInput back = { 0 };
        back.key_tab = 1; back.key_shift = 1;
        (void)gm_input_map(&up, 0.15f);              /* release */
        GmAction a4 = gm_input_map(&back, 0.15f);
        expect_i("shifttab.sel", a4.hotbar_sel, 1);  /* 2 - 1 */
    }

    /* ---- Q edge throws one; Ctrl+Q throws the whole stack (button 1) ---- */
    gm_input_reset();
    {
        CrInput in = { 0 };
        in.key_q = 1;
        GmAction a1 = gm_input_map(&in, 0.15f);
        expect_i("q1.inv_click",  a1.inv_click,  1);
        expect_i("q1.inv_type",   a1.inv_type,   4); /* CC_CLICK_THROW */
        expect_i("q1.inv_button", a1.inv_button, 0); /* one item */
        GmAction a2 = gm_input_map(&in, 0.15f);
        expect_i("q2.inv_click",  a2.inv_click,  0); /* held: no repeat */
        CrInput up = { 0 };
        (void)gm_input_map(&up, 0.15f);              /* release */
        CrInput cq = { 0 };
        cq.key_q = 1; cq.key_ctrl = 1;
        GmAction a3 = gm_input_map(&cq, 0.15f);
        expect_i("ctrlq.inv_click",  a3.inv_click,  1);
        expect_i("ctrlq.inv_button", a3.inv_button, 1); /* whole stack */
    }

    if (g_fail) {
        printf("TEST FAILED\n");
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
