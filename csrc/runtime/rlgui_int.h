/* rlgui.c's part of rlbind.c's tick hook (rlgui.h nwrl_rows). */
#ifndef NETHERITE_RUNTIME_RLGUI_INT_H
#define NETHERITE_RUNTIME_RLGUI_INT_H

#include <stdint.h>
#include <stdio.h>

struct session;
struct pool_config;
/* nwrl_make_ex's NWRL_DEV: the pool takes dev starts */
void rlgui_dev(struct pool_config *pc);
/* nwrl_screen_layer for a session (its env current) */
int rlgui_layer(struct session *ss, int w, int h, float *mul, float *add);
/* tick t's row record of the env as a line to f */
void rlgui_row(FILE *f, struct session *ss, int64_t t, int64_t cw_from);

#endif
