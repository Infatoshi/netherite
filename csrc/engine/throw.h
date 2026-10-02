/* The player's bow and throwables, both halves:
 *
 *   ItemBow.onItemRightClick            the use starts (setItemInUse, 72000)
 *   ItemBow.onPlayerStoppedUsing        the release: the charge, the arrow
 *                                       (EntityArrow from the shooter), the
 *                                       full-draw crit, the durability, the
 *                                       itemRand pitch, the arrow consumed
 *   ItemSnowball / ItemEgg / ItemEnderPearl.onItemRightClick
 *                                       the stack decrement, the itemRand
 *                                       pitch, EntityThrowable from the thrower
 *   ItemEnderEye.onItemRightClick       the end portal frame check, then
 *                                       projectile.c's server throw
 *   EntityArrow.onCollideWithPlayer     the pickup (survival.c's pickup query)
 *   EntityEnderPearl.onImpact           the thrower's teleport and 5.0F fall
 *
 * The client runs the same item bodies on its own inventory copy (the client
 * world is remote, so it spawns nothing but still constructs the released
 * arrow and draws Item.itemRand on its own role). The server's entities join
 * the replay's living world (the pool the skeletons' arrows live in), so they
 * hit mobs and the player the way Java's one entity list does. */
#ifndef NETHERITE_THROW_H
#define NETHERITE_THROW_H

struct server_player;
struct client_player;
struct serverreplay;
struct surv_state;

/* The in-air C08's item use on the server (ItemInWorldManager.tryUseItem's
 * onItemRightClick) for the bow, the snowball, the egg, the pearl and the
 * eye. 1 when the held item is one of them (the use is done). */
int throw_server_use(struct server_player *p);

/* processPlayerDigging status 5's stopUsingItem: the bow's
 * onPlayerStoppedUsing, before the caller clears the use. */
void throw_server_release(struct server_player *p);

/* PlayerControllerMP.sendUseItem's client-side onItemRightClick for the same
 * items. 1 when handled. */
int throw_client_use(struct client_player *p);

/* PlayerControllerMP.onStoppedUsingItem's client stopUsingItem. */
void throw_client_release(struct client_player *p);

/* The replay's hooks: the pearl teleport and the eye drop on the living
 * world's projectile pool. */
void throw_bind(struct serverreplay *sr);

#endif
