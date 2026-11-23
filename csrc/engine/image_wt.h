/* The reset's write tracker, more (image.h has the rest; lane/resetcopy):
 * here so that env.h's includers do not rebuild for it. */
#ifndef NETHERITE_IMAGE_WT_H
#define NETHERITE_IMAGE_WT_H

#include "image.h"

/* Protect again the written pages of e's runs (whole pages, any order),
 * one walk a run, and leave the rest of the image as it is; then watch e's
 * frees as image_wtrack_protect does (img.track, the zap log emptied). For a
 * reset that knows every page written since the last protect: the scan's
 * runs less what stays unprotected, and the zap log's. 0 on success. */
int image_wtrack_protect_runs(struct img_wtrack *t, struct env *e, const struct img_wrun *runs, size_t n);

#endif
