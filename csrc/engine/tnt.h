/* TNT in the whole-server replay: BlockTNT.onBlockActivated with flint and
 * steel (the live C08), and EntityTNTPrimed.explode's
 * WorldServer.newExplosion(tnt, x, y, z, 4.0F, false, true) over the replay's
 * pools, with the server player and the living entities in the blast's
 * entity pass and the S27 knockback the player is sent. The entity itself
 * (its fuse and motion) is item_entity.c's IE_TNT; fire's priming is
 * fire.c's hook, recorded by servertick.
 */
#ifndef NETHERITE_TNT_H
#define NETHERITE_TNT_H

struct ie_ent;
struct server_player;

/* ie_world.on_tnt_explode for the replay's item pool; ctx is the replay. */
/* WorldServer.newExplosion(null, x, y, z, power, flaming, smoking) in the
 * player's world, outside any entity's tick (World.rand of that world): the
 * blast, the entity pass over the living pool and the player, and the S27. */
struct serverreplay;
void tnt_replay_blast(struct serverreplay *sr, double x, double y, double z, float power,
                      int flaming, int smoking);

void tnt_replay_explode(void *ctx, struct ie_ent *tnt);

/* The blast entities outside the living pool, for a ghast fireball's
 * explosion (gh_world.blast_other; ctx is the replay): the item pool's
 * entities (EntityItem and EntityXPOrb health, every other kind's
 * setBeenAttacked) and the falling and hanging ones, with their pushes. */
struct aabb;
struct expl_extra;
int tnt_blast_other(void *ctx, struct aabb box, struct expl_extra *out, int cap);

/* BlockTNT.onBlockActivated's flint-and-steel branch on the server: the
 * primed entity (placed by the player), the block to air and the held
 * stack's one point of wear. 1 when it ran (the held item is flint and
 * steel), 0 to fall through to Block.onBlockActivated. */
int tnt_server_activate(struct server_player *p, int x, int y, int z);

#endif
