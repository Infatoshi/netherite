/* Beds and sleeping in the whole-server replay: BlockBed.onBlockActivated's
 * server half, EntityPlayer.sleepInBedAt and wakeUpPlayer with the
 * EntityPlayerMP overrides (the S08 and the S0A / S0B packets), the sleep
 * block of EntityPlayer.onUpdate, WorldServer's areAllPlayersAsleep and
 * wakeAllPlayers through servertick's head hook, and the client's half: the
 * S0A's sleepInBedAt, the S0B's wakeUpPlayer and the GuiSleepMP screen; the
 * Nether's and the End's bed explosion (tnt.c's blast in the player's
 * world). The screen's Leave Bed button is the act's ["wake"] op: the
 * client's C0B action 3 (survival.c) and the server's wakeUpPlayer(false,
 * true, true) (player.c).
 */
#ifndef NETHERITE_SLEEP_H
#define NETHERITE_SLEEP_H

struct client_player;
struct server_player;
struct serverreplay;
struct servertick;

/* BlockBed.onBlockActivated on the server for the bed at (x, y, z). Always
 * answers true, as the block does. */
int sleep_bed_activate(struct server_player *p, int x, int y, int z);

/* EntityPlayer.onUpdate's sleep block on the server (the sleep timer and the
 * out-of-bed and daytime wakes). */
void sleep_server_update(struct server_player *p);

/* EntityPlayerMP.wakeUpPlayer(reset_timer, update_flag, set_spawn). */
void sleep_server_wake(struct server_player *p, int reset_timer, int update_flag, int set_spawn);

/* servertick's on_sleep_check and on_wake_all over the replay's player. */
int sleep_all_asleep(void *ctx);
void sleep_wake_all(void *ctx, struct servertick *s);

/* The client's S0A (sleepInBedAt on the client world) and S0B animation 2
 * (wakeUpPlayer(false, false, false)). */
void sleep_client_use_bed(struct client_player *p, int x, int y, int z);
void sleep_client_wake(struct client_player *p);

/* EntityPlayer.onUpdate's sleep block on the client (the timer only). */
void sleep_client_update(struct client_player *p);

#endif
