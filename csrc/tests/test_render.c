/* The render scenes' pixel gate: renders SCENE (a directory under
 * out/java/render) with raster_render, compares the frame with the oracle's
 * golden.png, and fails when the frame is worse than the scene's budget in
 * tests/render_budget.txt: the mean absolute error of each channel over the
 * whole frame, and the pixels whose largest channel error is over 25 (pxdiff's
 * default threshold). A scene with no budget line fails, so a new recording
 * cannot join the suite unmeasured; its line is printed for the reviewer.
 * A line `SCENE SKIP reason` excludes a scene the renderer cannot read, by
 * name and with its reason, so the exclusion is reviewed like a budget.
 * A budget records what the renderer measured when the scene was accepted,
 * so the gate is a ratchet: lower a line when a fix lowers the error.
 *
 *   test_render SCENE_DIR */
#define _POSIX_C_SOURCE 200809L
#include "../engine/pngread.h"
#include "../engine/raster.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail(const char *what, const char *where)
{
    printf("FAIL test_render: %s %s\n", what, where);
    return 1;
}

/* An RGB copy of a PNG, through the shared reader. */
static unsigned char *png_read_rgb(const char *path, int *w, int *h)
{
    unsigned char *rgba = png_read_rgba(path, w, h);
    if (!rgba) return NULL;
    unsigned char *rgb = malloc((size_t)*w * *h * 3);
    if (rgb)
        for (size_t i = 0; i < (size_t)*w * *h; ++i) memcpy(rgb + i * 3, rgba + i * 4, 3);
    free(rgba);
    return rgb;
}

/* The scene's budget line: name mean_r mean_g mean_b px25. */
static int budget_for(const char *scene, double mean[3], long *px25)
{
    const char *paths[] = {"tests/render_budget.txt", "csrc/tests/render_budget.txt"};
    for (int k = 0; k < 2; ++k)
    {
        FILE *f = fopen(paths[k], "r");
        if (!f) continue;
        char line[512], name[256];
        while (fgets(line, sizeof line, f))
        {
            if (line[0] == '#') continue;
            char word[16];
            if (sscanf(line, "%255s %15s", name, word) == 2 && !strcmp(name, scene) && !strcmp(word, "SKIP"))
            { fclose(f); return 2; }
            if (sscanf(line, "%255s %lf %lf %lf %ld", name, &mean[0], &mean[1], &mean[2], px25) == 5
                && !strcmp(name, scene))
            { fclose(f); return 1; }
        }
        fclose(f);
        return 0;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: test_render SCENE_DIR\n"); return 2; }
    char dir[1024], golden[1200], out[1200];
    snprintf(dir, sizeof dir, "%s", argv[1]);
    size_t dl = strlen(dir);
    while (dl > 1 && dir[dl - 1] == '/') dir[--dl] = 0;
    const char *scene = strrchr(dir, '/') ? strrchr(dir, '/') + 1 : dir;

    snprintf(golden, sizeof golden, "%s/golden.png", dir);
    int gw, gh;
    unsigned char *want = png_read_rgb(golden, &gw, &gh);
    if (!want) return fail("missing or unreadable golden", golden);

    double bmean[3];
    long bpx;
    int b = budget_for(scene, bmean, &bpx);
    if (b < 0) { free(want); return fail("no tests/render_budget.txt for", scene); }
    if (b == 2)
    {
        /* a reviewed exclusion with its reason in the budget file */
        free(want);
        printf("SKIP render %s: listed as SKIP in tests/render_budget.txt\n", scene);
        return 0;
    }

    const char *tmp = getenv("TMPDIR");
    snprintf(out, sizeof out, "%s/test_render_%s_%d.png", tmp && *tmp ? tmp : ".", scene, (int)getpid());
    if (raster_render(dir, out, NULL, NULL) != 0) { free(want); return fail("raster_render failed on", dir); }
    int nw, nh;
    unsigned char *got = png_read_rgb(out, &nw, &nh);
    unlink(out);
    if (!got) { free(want); return fail("could not read the rendered frame of", dir); }
    if (nw != gw || nh != gh) { free(want); free(got); return fail("frame size differs from the golden in", dir); }

    double sum[3] = {0, 0, 0};
    long px25 = 0, n = (long)gw * gh;
    for (long i = 0; i < n; ++i)
    {
        int worst = 0;
        for (int c = 0; c < 3; ++c)
        {
            int d = abs((int)got[i * 3 + c] - (int)want[i * 3 + c]);
            sum[c] += d;
            if (d > worst) worst = d;
        }
        if (worst > 25) ++px25;
    }
    free(want); free(got);
    double mean[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
    printf("render %s: mean %.6f %.6f %.6f /ch, %ld px over 25 of %ld\n", scene, mean[0], mean[1], mean[2], px25, n);

    if (b == 0)
    {
        printf("FAIL test_render: %s has no budget; after review add: %s %.6f %.6f %.6f %ld\n",
               scene, scene, mean[0], mean[1], mean[2], px25);
        return 1;
    }
    int bad = px25 > bpx;
    for (int c = 0; c < 3; ++c) bad |= mean[c] > bmean[c] + 1e-6;
    if (bad)
    {
        printf("FAIL test_render: %s is worse than its budget %.6f %.6f %.6f /ch, %ld px\n",
               scene, bmean[0], bmean[1], bmean[2], bpx);
        return 1;
    }
    printf("PASS render %s within budget\n", scene);
    return 0;
}
