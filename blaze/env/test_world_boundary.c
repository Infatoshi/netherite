/* Shared CPU/CUDA decision semantics: finite horizontal bounds truncate
 * training episodes without fabricating a player death. */
#include "blaze_core.h"
#include <stdio.h>
#include <stdlib.h>

static void check(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
static Blaze *fresh(void) {
    Blaze *e = calloc(1, sizeof *e);
    check(e != NULL, "allocate environment");
    e->rx0 = e->rz0 = -32;
    e->rnx = e->rnz = 64;
    e->rny = 128;
    e->pl.ent.posY = 64;
    return e;
}
static void finish(Blaze *e) {
    blaze_subtick_post(e, 0, 4, 0, 0, 0, 0, 0);
}
int main(void) {
    /* Includes translated window coordinates, all four edges, and exact
     * safe-margin equality. Vertical state is not a horizontal truncation. */
    for (int axis = 0; axis < 2; axis++) for (int sign = -1; sign <= 1; sign += 2) {
        Blaze *e = fresh();
        if (axis == 0) { e->ox = 16; e->pl.ent.posX = sign * 30.0 - e->ox; }
        else { e->oz = -16; e->pl.ent.posZ = sign * 30.0 - e->oz; }
        finish(e);
        check(e->done == BLAZE_DONE_RUNNING, "safe edge remains live");
        if (axis == 0) e->pl.ent.posX += sign * 0.001;
        else e->pl.ent.posZ += sign * 0.001;
        finish(e);
        check(e->done == BLAZE_DONE_BOUNDARY && !e->dead, "edge exit truncates, does not die");
        check(!e->dec_plus10 && e->dec_have_last, "boundary completes decision without success bonus");
        check(e->dec_cam_fresh, "early boundary requests final observation render");
        float reward = 0; unsigned char done = 0;
        blaze_decision_finalize(e, NULL, NULL, &reward, &done, NULL, 0);
        check(done == BLAZE_DONE_BOUNDARY && reward == -0.01f, "two simulated ticks have time cost only");
        free(e);
    }
    Blaze *e = fresh();
    e->pl.ent.posY = -4;
    finish(e);
    check(e->done == BLAZE_DONE_RUNNING, "vertical position is left to actual death logic");
    e->pl.ent.posX = 31;
    e->dead = 1;
    finish(e);
    check(e->done == BLAZE_DONE_DEATH, "true death outranks simultaneous boundary exit");
    free(e);
    e = fresh();
    e->pl.ent.posX = 31;
    e->success_item = 263;
    isr_set_stack(&e->pl.inv, 0, ic_mk(263, 1, 0));
    finish(e);
    check(e->done == BLAZE_DONE_SUCCESS && e->dec_plus10, "actual success outranks boundary exit");
    free(e);
    puts("world boundary: four edges, translated origins, priorities, rewards and final observation PASS");
    return 0;
}
