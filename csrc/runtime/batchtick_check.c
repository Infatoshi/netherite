/* batchtick_check: batchtick.c's gate (make -C csrc/runtime batchtick-check), and with
 * arguments the model's bench of the prototype (BT_ARGS). */
#include <stdio.h>

#include "batchtick.h"

#ifndef BT_SNAPS
#define BT_SNAPS "out/java/snapshots"
#endif
#ifndef BT_ENGINE
#define BT_ENGINE "out/native/test_snapshots"
#endif
/* rows a recording's replay compares: the model's 32-block window holds about
 * this much walking from a recording's start state */
#define BT_ROWS 60

int main(int argc, char **argv)
{
    if (argc > 1) return nwbt_main(argc, argv);
    int ok = nwbt_selftest() == 0;
    if (nwbt_compare(BT_SNAPS, BT_ENGINE, BT_ROWS) != 0) ok = 0;
    if (!ok)
    {
        printf("batchtick: FAIL\n");
        return 1;
    }
    printf("batchtick: PASS\n");
    return 0;
}
