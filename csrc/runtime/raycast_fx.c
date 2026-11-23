/* The textured ray cast's fixed instance (lane/castexact, raycast.h "The instances"): raycast.c compiled with the
 * float scalar and RC_FIX (the eye's offset and the direction on a fixed grid, the walk's order exact), its entry
 * points named *_fx (raycast.c's own call them for a spec with fp RAYCAST_FP_FIXED). */
#define RC_FP 32
#define RC_FIX 1
#include "raycast.c"
