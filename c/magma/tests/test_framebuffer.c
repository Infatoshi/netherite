#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include "core/types.h"

int main(void) {
    CrFramebuffer fb = {123, 456, (CrRgba *)(uintptr_t)1,
                        (float *)(uintptr_t)1};

    if (cr_fb_alloc(&fb, INT_MAX, INT_MAX) || fb.w || fb.h || fb.color ||
        fb.depth) {
        fputs("FAIL: oversized framebuffer was not rejected safely\n", stderr);
        return 1;
    }
    if (cr_fb_alloc(&fb, 0, 1) || fb.w || fb.h || fb.color || fb.depth) {
        fputs("FAIL: zero-sized framebuffer was not rejected safely\n", stderr);
        return 1;
    }
    if (!cr_fb_alloc(&fb, 1, 1) || !fb.color || !fb.depth ||
        fb.w != 1 || fb.h != 1) {
        fputs("FAIL: valid framebuffer allocation failed\n", stderr);
        cr_fb_free(&fb);
        return 1;
    }
    cr_fb_clear(&fb, (CrRgba){1, 2, 3, 4});
    if (fb.color[0].r != 1 || fb.color[0].g != 2 ||
        fb.color[0].b != 3 || fb.color[0].a != 4 || fb.depth[0] != 1.0f) {
        fputs("FAIL: framebuffer clear contract changed\n", stderr);
        cr_fb_free(&fb);
        return 1;
    }
    cr_fb_free(&fb);
    if (fb.w || fb.h || fb.color || fb.depth) {
        fputs("FAIL: framebuffer free did not leave an empty value\n", stderr);
        return 1;
    }
    puts("FRAMEBUFFER PASS");
    return 0;
}
