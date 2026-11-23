#ifndef NETHERITE_DEV_H
#define NETHERITE_DEV_H

struct jval;
struct serverreplay;
struct server_player;

#ifdef NETHERITE_DEV
/* Apply one header command at the head of the integrated server tick. */
int dev_apply(struct serverreplay *sr, struct server_player *sp, const struct jval *cmd,
              char *error, int error_size);
void dev_end(struct serverreplay *sr);
#endif

#endif
